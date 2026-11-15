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
#include "array.h"
#include "cov/cov-api.h"
#include "diag.h"
#include "hash.h"
#include "ident.h"
#include "vlog/vlog-node.h"
#include "vlog/vlog-phase.h"
#include "vlog/vlog-util.h"

#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>

// Code coverage for the statements of a Verilog instance.  The
// walk below runs in two modes over the same code: create makes the scopes
// and items of an instance in the coverage database when it is elaborated,
// and bind finds them again when the body is lowered, remembering the
// counter tag of each syntax node so that the lowering can ask for it.  The
// tags are relative to the instance so one body serves all its instances.

typedef struct _lazy_cscope lazy_cscope_t;

typedef struct _lazy_cscope {
   lazy_cscope_t *parent;
   cover_obj_t    cscope;
   vlog_node_t    node;
   ident_t        name;
   bool           resolved;
   unsigned       nstmt;
   unsigned       nitem[NUM_COVER_KINDS];
} lazy_cscope_t;

typedef struct {
   cover_data_t *db;
   cover_obj_t   inst;
   bool          bind;
} vlog_cover_t;

static hash_t *stmt_tags = NULL;

static void vlog_cover_stmt_list(vlog_cover_t *g, vlog_node_t v,
                                 lazy_cscope_t *parent);

static lazy_cscope_t lazy_cover_scope(vlog_node_t v, lazy_cscope_t *parent,
                                      ident_t name)
{
   lazy_cscope_t lcs = {
      .parent = parent,
      .cscope = COVER_NULL_OBJ,
      .node   = v,
      .name   = name,
   };
   return lcs;
}

static cover_obj_t get_cover_scope(vlog_cover_t *g, lazy_cscope_t *lcs)
{
   if (lcs->resolved)
      return lcs->cscope;

   cover_obj_t parent = get_cover_scope(g, lcs->parent);
   lcs->resolved = true;

   if (cover_is_null(parent))
      return lcs->cscope = COVER_NULL_OBJ;
   else if (g->bind)
      return lcs->cscope = cover_get_child(g->db, parent, lcs->name);
   else
      return lcs->cscope = cover_scope_new(g->db, g->inst, parent,
                                           CSCOPE_NONE, lcs->name,
                                           *vlog_loc(lcs->node));
}

static void vlog_cover_remember(hash_t **map, vlog_node_t v, uint32_t tag)
{
   if (*map == NULL)
      *map = hash_new(256);

#ifdef DEBUG
   void *old = hash_get(*map, v);
   if (old != NULL && (uintptr_t)old != (uintptr_t)tag + 1)
      fatal_trace("inconsistent coverage tags for %s at %s:%d",
                  vlog_kind_str(vlog_kind(v)), loc_file_str(vlog_loc(v)),
                  vlog_loc(v)->first_line);
#endif

   hash_put(*map, v, (void *)((uintptr_t)tag + 1));
}

static cover_obj_t vlog_cover_item(vlog_cover_t *g, lazy_cscope_t *lcs,
                                   cover_item_kind_t kind, const loc_t *loc,
                                   int nbins)
{
   cover_obj_t cs = get_cover_scope(g, lcs);
   if (cover_is_null(cs))
      return COVER_NULL_OBJ;

   cover_obj_t item;
   if (g->bind)
      item = cover_get_item(g->db, cs, kind, lcs->nitem[kind]);
   else
      item = cover_item_new(g->db, cs, kind, *loc, nbins);

   if (!cover_is_null(item))
      lcs->nitem[kind]++;

   return item;
}

static bool vlog_cover_is_property(vlog_node_t p)
{
   // An always process made for a concurrent assertion

   if (vlog_kind(p) != V_ALWAYS || vlog_stmts(p) != 1)
      return false;

   vlog_node_t s0 = vlog_stmt(p, 0);
   if (vlog_kind(s0) != V_TIMING || vlog_stmts(s0) < 1)
      return false;

   vlog_node_t s1 = vlog_stmt(s0, 0);
   switch (vlog_kind(s1)) {
   case V_ASSERT:
   case V_ASSUME:
   case V_COVER:
      return vlog_has_value(s1) && vlog_kind(vlog_value(s1)) == V_PROP_SPEC;
   default:
      return false;
   }
}

bool vlog_cover_wants_stmt(vlog_node_t s)
{
   switch (vlog_kind(s)) {
   case V_SEQ_BLOCK:
      return false;
   case V_SYS_TCALL:
   case V_USER_TCALL:
      // Initialisation of a package made by the parser
      if (vlog_has_ident(s)
          && ident_starts_with(vlog_ident(s), ident_new("$__init:")))
         return false;
      break;
   default:
      break;
   }

   return !loc_invalid_p(vlog_loc(s));
}

ident_t vlog_cover_scope_name(vlog_node_t v, int nth)
{
   switch (vlog_kind(v)) {
   case V_FUNC_DECL:
   case V_TASK_DECL:
      return vlog_ident(v);
   case V_SEQ_BLOCK:
   case V_FORK:
      if (vlog_has_ident(v))
         return vlog_ident(v);
      return ident_sprintf("_S%u", nth);
   case V_ALWAYS:
   case V_INITIAL:
   case V_FINAL:
   case V_ASSIGN:
      return ident_sprintf("_P%u", nth);
   case V_NET_DECL:
      return ident_sprintf("_D%s", istr(vlog_ident(v)));
   case V_COND:
      return ident_sprintf("_B%u", nth);
   case V_CASE_ITEM:
      if (vlog_params(v) == 0)
         return ident_new("_B_OTHERS");
      return ident_sprintf("_B%u", nth);
   default:
      return ident_sprintf("_S%u", nth);
   }
}

static void vlog_cover_stmt(vlog_cover_t *g, vlog_node_t s,
                            lazy_cscope_t *lcs)
{
   if (!cover_enabled(g->db, COVER_MASK_STMT) || !vlog_cover_wants_stmt(s))
      return;

   const loc_t *loc = vlog_loc(s);

   cover_obj_t item = vlog_cover_item(g, lcs, COV_ITEM_STMT, loc, 1);
   if (cover_is_null(item))
      return;

   if (g->bind) {
      cover_obj_t bin = cover_at(g->db, item, COV_REL_BINS, 0);
      vlog_cover_remember(&stmt_tags, s, cover_get_tag(g->db, bin));
      return;
   }

   cover_put_loc(g->db, item, COV_ATTR_LOC, *loc);

   cover_src_t src;
   switch (vlog_kind(s)) {
   case V_IF:
      src = COV_SRC_IF_STMT;
      break;
   case V_FOREVER:
   case V_REPEAT:
   case V_WHILE:
   case V_DO_WHILE:
   case V_FOR_LOOP:
      src = COV_SRC_LOOP_STMT;
      break;
   case V_TIMING:
      src = COV_SRC_WAIT;
      break;
   case V_BASSIGN:
   case V_OP_ASSIGN:
      src = COV_SRC_VAR_ASSIGN;
      break;
   case V_NBASSIGN:
   case V_ASSIGN:
      src = COV_SRC_SIGNAL_ASSIGN;
      break;
   case V_ASSERT:
   case V_ASSUME:
      src = COV_SRC_ASSERT;
      break;
   default:
      src = COV_SRC_STATEMENT;
      break;
   }

   cover_put_u32(g->db, item, COV_ATTR_SOURCE, src);
}

static void vlog_cover_if(vlog_cover_t *g, vlog_node_t s, lazy_cscope_t *lcs)
{
   const int nconds = vlog_conds(s);
   for (int i = 0; i < nconds; i++) {
      vlog_node_t c = vlog_cond(s, i);
      lazy_cscope_t cl =
         lazy_cover_scope(c, lcs, vlog_cover_scope_name(c, i));

      vlog_cover_stmt_list(g, c, &cl);
   }
}

static void vlog_cover_case(vlog_cover_t *g, vlog_node_t s,
                            lazy_cscope_t *lcs)
{
   const int nitems = vlog_stmts(s);
   for (int i = 0; i < nitems; i++) {
      vlog_node_t item = vlog_stmt(s, i);
      assert(vlog_kind(item) == V_CASE_ITEM);

      lazy_cscope_t il =
         lazy_cover_scope(item, lcs, vlog_cover_scope_name(item, i));

      vlog_cover_stmt_list(g, item, &il);
   }
}

static void vlog_cover_stmt_one(vlog_cover_t *g, vlog_node_t s,
                                lazy_cscope_t *parent)
{
   const vlog_kind_t kind = vlog_kind(s);

   if (kind == V_SEQ_BLOCK && !vlog_has_ident(s)) {
      // Transparent: the statements belong to the enclosing scope
      vlog_cover_stmt_list(g, s, parent);
      return;
   }

   lazy_cscope_t lcs =
      lazy_cover_scope(s, parent, vlog_cover_scope_name(s, parent->nstmt++));

   vlog_cover_stmt(g, s, &lcs);

   switch (kind) {
   case V_SEQ_BLOCK:
   case V_FORK:
   case V_TIMING:
   case V_FOREVER:
   case V_REPEAT:
   case V_WHILE:
   case V_DO_WHILE:
   case V_FOR_LOOP:
      vlog_cover_stmt_list(g, s, &lcs);
      break;
   case V_IF:
      vlog_cover_if(g, s, &lcs);
      break;
   case V_CASE:
      vlog_cover_case(g, s, &lcs);
      break;
   case V_RANDCASE:
      {
         const int nitems = vlog_stmts(s);
         for (int i = 0; i < nitems; i++)
            vlog_cover_stmt_list(g, vlog_stmt(s, i), &lcs);
      }
      break;
   default:
      break;
   }
}

static void vlog_cover_stmt_list(vlog_cover_t *g, vlog_node_t v,
                                 lazy_cscope_t *parent)
{
   const int nstmts = vlog_stmts(v);
   for (int i = 0; i < nstmts; i++)
      vlog_cover_stmt_one(g, vlog_stmt(v, i), parent);
}

static void vlog_cover_process(vlog_cover_t *g, vlog_node_t p, int nth,
                               lazy_cscope_t *root)
{
   if (vlog_kind(p) == V_ALWAYS && vlog_cover_is_property(p))
      return;

   lazy_cscope_t pl = lazy_cover_scope(p, root, vlog_cover_scope_name(p, nth));

   if (vlog_kind(p) == V_ASSIGN) {
      vlog_cover_stmt(g, p, &pl);
      return;
   }

   if (vlog_kind(p) == V_ALWAYS
       && (vlog_subkind(p) == V_ALWAYS_COMB_DONE
           || vlog_subkind(p) == V_ALWAYS_LATCH_DONE)) {
      // The event control at the end of the block was made up
      vlog_node_t b = vlog_stmt(p, 0);
      assert(vlog_kind(b) == V_SEQ_BLOCK);

      const int nstmts = vlog_stmts(b);
      for (int i = 0; i < nstmts - 1; i++)
         vlog_cover_stmt_one(g, vlog_stmt(b, i), &pl);
      return;
   }

   vlog_cover_stmt_list(g, p, &pl);
}

static void vlog_cover_walk(vlog_node_t body, cover_data_t *db,
                            cover_obj_t cs, bool bind)
{
   assert(vlog_kind(body) == V_INST_BODY);

   if (db == NULL || cover_is_null(cs))
      return;

   if (!cover_enabled(db, COVER_MASK_STMT))
      return;

   vlog_cover_t g = {
      .db   = db,
      .inst = cover_get_obj(db, cs, COV_ATTR_INST),
      .bind = bind,
   };

   lazy_cscope_t root = lazy_cover_scope(body, NULL, NULL);
   root.cscope = cs;
   root.resolved = true;

   const int ndecls = vlog_decls(body);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(body, i);
      switch (vlog_kind(d)) {
      case V_FUNC_DECL:
      case V_TASK_DECL:
         {
            lazy_cscope_t fl =
               lazy_cover_scope(d, &root, vlog_cover_scope_name(d, i));
            vlog_cover_stmt_list(&g, d, &fl);
         }
         break;
      case V_NET_DECL:
         if (vlog_has_value(d)) {
            lazy_cscope_t dl =
               lazy_cover_scope(d, &root, vlog_cover_scope_name(d, i));
            vlog_cover_stmt(&g, d, &dl);
         }
         break;
      default:
         break;
      }
   }

   const int nstmts = vlog_stmts(body);
   for (int i = 0; i < nstmts; i++) {
      vlog_node_t s = vlog_stmt(body, i);
      switch (vlog_kind(s)) {
      case V_ALWAYS:
      case V_INITIAL:
      case V_FINAL:
      case V_ASSIGN:
         vlog_cover_process(&g, s, i, &root);
         break;
      default:
         break;
      }
   }
}

void vlog_cover_block(vlog_node_t body, cover_data_t *db, cover_obj_t cs)
{
   vlog_cover_walk(body, db, cs, false);
}

void vlog_cover_bind(vlog_node_t body, cover_data_t *db, cover_obj_t cs)
{
   vlog_cover_walk(body, db, cs, true);
}

static bool vlog_cover_lookup(hash_t *map, vlog_node_t v, uint32_t *tag)
{
   if (map == NULL)
      return false;

   void *ptr = hash_get(map, v);
   if (ptr == NULL)
      return false;

   *tag = (uintptr_t)ptr - 1;
   return true;
}

bool vlog_cover_tag(vlog_node_t v, cover_item_kind_t kind, uint32_t *tag)
{
   switch (kind) {
   case COV_ITEM_STMT:
      return vlog_cover_lookup(stmt_tags, v, tag);
   default:
      return false;
   }
}
