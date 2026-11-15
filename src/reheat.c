//
//  Copyright (C) 2026  Nick Gasson
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#include "util.h"
#include "common.h"
#include "cov/cov-api.h"
#include "hash.h"
#include "lower.h"
#include "mir/mir-unit.h"
#include "phase.h"
#include "rt/model.h"
#include "tree.h"
#include "vlog/vlog-phase.h"
#include "vlog/vlog-util.h"
#include "vhdl/vhdl-phase.h"

#include <assert.h>

typedef struct _reheat_ctx reheat_ctx_t;

typedef struct _reheat_ctx {
   const reheat_ctx_t *parent;
   rt_model_t         *model;
   mir_context_t      *mir;
   unit_registry_t    *registry;
   cover_data_t       *cover;
   lower_unit_t       *lowered;
   rt_scope_t         *scope;
   ident_t             dotted;
   ident_t             cloned;
   hset_t             *instances;
   hset_t             *lowered_blocks;
} reheat_ctx_t;

static void reheat_inherit(reheat_ctx_t *ctx, const reheat_ctx_t *parent)
{
   ctx->parent    = parent;
   ctx->model     = parent->model;
   ctx->mir       = parent->mir;
   ctx->registry  = parent->registry;
   ctx->cover     = parent->cover;
   ctx->instances = parent->instances;
   ctx->lowered_blocks = parent->lowered_blocks;
}

static bool reheat_is_vhdl_shim(tree_t b)
{
   // A Verilog block containing a VHDL block is the generated shim for
   // a VHDL entity instantiated from Verilog
   return find_vhdl_shim_entity(b) != NULL;
}

static cover_obj_t reheat_cover_scope(cover_data_t *cover, ident_t dotted)
{
   // The root scope of the coverage instance made for the block, if any

   if (cover == NULL)
      return COVER_NULL_OBJ;

   cover_obj_t inst = cover_find(cover, dotted);
   if (cover_is_null(inst))
      return COVER_NULL_OBJ;

   return cover_get_obj(cover, inst, COV_ATTR_ROOT);
}

static void reheat_lower_verilog(tree_t b, const reheat_ctx_t *parent,
                                 ident_t parent_cloned)
{
   // Every Verilog instance whose parents are also Verilog is lowered before
   // any code runs as the code which dispatches on the class of an object
   // needs all of the classes

   tree_t hier = tree_decl(b, 0);
   assert(tree_kind(hier) == T_HIER);

   if (tree_subkind(hier) != T_VERILOG || reheat_is_vhdl_shim(b))
      return;

   vlog_node_t body = tree_vlog(tree_ref(hier));

   if (!hset_contains(parent->instances, body)) {
      vlog_lower_instance(parent->mir, body, parent_cloned, b, parent->cover,
                          reheat_cover_scope(parent->cover, tree_ident(hier)));
      hset_insert(parent->instances, body);
   }

   vlog_lower_block(parent->mir, parent_cloned, b);
   hset_insert(parent->lowered_blocks, b);

   const int nstmts = tree_stmts(b);
   for (int i = 0; i < nstmts; i++) {
      tree_t s = tree_stmt(b, i);
      if (tree_kind(s) == T_BLOCK)
         reheat_lower_verilog(s, parent, tree_ident2(hier));
   }
}

static void reheat_block(tree_t b, const reheat_ctx_t *parent)
{
   reheat_ctx_t ctx = {};
   reheat_inherit(&ctx, parent);

   tree_t hier = tree_decl(b, 0);
   assert(tree_kind(hier) == T_HIER);

   ctx.dotted = tree_ident(hier);
   ctx.cloned = tree_ident2(hier);

   if (tree_subkind(hier) == T_VERILOG) {
      if (!hset_contains(ctx.lowered_blocks, b)) {
         vlog_node_t body = tree_vlog(tree_ref(hier));

         if (!hset_contains(ctx.instances, body)) {
            vlog_lower_instance(ctx.mir, body, parent->cloned, b, ctx.cover,
                                reheat_cover_scope(ctx.cover, ctx.dotted));
            hset_insert(ctx.instances, body);
         }

         vlog_lower_block(ctx.mir, parent->cloned, b);
      }

      if (reheat_is_vhdl_shim(b)) {
         // Recreate the placeholder unit which exposes the ports of the
         // shim to the VHDL entity
         const int nports = tree_ports(b);
         mir_shape_t *shape = mir_get_shape(ctx.mir, ctx.cloned);
         assert(shape != NULL);

         int *slots LOCAL = xmalloc_array(nports, sizeof(int));
         for (int i = 0; i < nports; i++) {
            slots[i] = mir_find_slot(shape, tree_ident(tree_port(b, i)));
            assert(slots[i] >= 0);
         }

         ctx.lowered = lower_verilog_parent(ctx.registry, ctx.cloned,
                                            ctx.dotted, b, slots);
      }
   }
   else {
      cover_obj_t scope = reheat_cover_scope(ctx.cover, ctx.dotted);

      ctx.lowered = lower_instance(ctx.registry, parent->lowered, ctx.cover,
                                   scope, b);
   }

   ctx.scope = create_scope(ctx.model, b, parent->scope);

   const int nstmts = tree_stmts(b);
   for (int i = 0; i < nstmts; i++) {
      tree_t s = tree_stmt(b, i);
      if (tree_kind(s) == T_BLOCK)
         reheat_block(s, &ctx);
   }
}

void reheat(tree_t top, unit_registry_t *ur, mir_context_t *mc,
            cover_data_t *cover, rt_model_t *m)
{
   assert(tree_kind(top) == T_ELAB);

   vlog_set_design_precision(tree_ival(top));

   rt_scope_t *root = create_scope(m, top, NULL);

   reheat_ctx_t ctx = {
      .model     = m,
      .scope     = root,
      .cover     = cover,
      .registry  = ur,
      .mir       = mc,
      .instances = hset_new(16),
      .lowered_blocks = hset_new(16),
   };

   const int ntops = tree_stmts(top);
   for (int i = 0; i < ntops; i++)
      reheat_lower_verilog(tree_stmt(top, i), &ctx, NULL);

   for (int i = 0; i < ntops; i++)
      reheat_block(tree_stmt(top, i), &ctx);

   hset_free(ctx.instances);
   hset_free(ctx.lowered_blocks);
}
