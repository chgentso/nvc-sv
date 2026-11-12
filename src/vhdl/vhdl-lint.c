//
//  Copyright (C) 2025  Nick Gasson
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
#include "common.h"
#include "diag.h"
#include "hash.h"
#include "ident.h"
#include "lint.h"
#include "tree.h"
#include "vhdl/vhdl-phase.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>


////////////////////////////////////////////////////////////////////////////////
// Unused and undriven signals

enum {
   USE_READ    = (1 << 0),
   USE_WRITTEN = (1 << 1),
};

typedef struct {
   hash_t *lvalues;    // T_REF nodes that are only written
   hash_t *uses;       // Declaration to USE_* mask
} use_ctx_t;

static void mark_lvalue(tree_t v, use_ctx_t *ctx, bool also_read)
{
   switch (tree_kind(v)) {
   case T_AGGREGATE:
      {
         const int nassocs = tree_assocs(v);
         for (int i = 0; i < nassocs; i++)
            mark_lvalue(tree_value(tree_assoc(v, i)), ctx, also_read);
      }
      break;
   case T_TYPE_CONV:
      mark_lvalue(tree_value(v), ctx, also_read);
      break;
   default:
      {
         tree_t ref = name_to_ref(v);
         if (ref != NULL)
            hash_put(ctx->lvalues, ref, (void *)(uintptr_t)(also_read ? 2 : 1));
      }
      break;
   }
}

static void mark_actuals(tree_t call, tree_t unit, use_ctx_t *ctx)
{
   // The mode of the formal is not known unless the unit is
   const int nparams = tree_params(call);
   const int nports = unit != NULL ? tree_ports(unit) : 0;

   for (int i = 0; i < nparams; i++) {
      tree_t p = tree_param(call, i);
      tree_t port = NULL;

      if (tree_subkind(p) == P_POS) {
         const int pos = tree_pos(p);
         if (pos < nports)
            port = tree_port(unit, pos);
      }
      else if (tree_subkind(p) == P_NAMED) {
         tree_t ref = name_to_ref(tree_name(p));
         if (ref != NULL) {
            ident_t id = tree_ident(ref);
            for (int j = 0; j < nports; j++) {
               if (tree_ident(tree_port(unit, j)) == id) {
                  port = tree_port(unit, j);
                  break;
               }
            }
         }
      }

      port_mode_t mode = PORT_INOUT;
      if (port != NULL && tree_kind(port) != T_GENERIC_DECL)
         mode = tree_subkind(port);

      switch (mode) {
      case PORT_IN:
         break;
      case PORT_OUT:
         mark_lvalue(tree_value(p), ctx, false);
         break;
      default:
         mark_lvalue(tree_value(p), ctx, true);
         break;
      }
   }
}

static void lvalue_cb(tree_t t, void *context)
{
   use_ctx_t *ctx = context;

   switch (tree_kind(t)) {
   case T_SIGNAL_ASSIGN:
   case T_VAR_ASSIGN:
   case T_FORCE:
   case T_RELEASE:
      mark_lvalue(tree_target(t), ctx, false);
      break;
   case T_COND_ASSIGN:
      mark_lvalue(tree_target(t), ctx, false);
      break;
   case T_PCALL:
      mark_actuals(t, tree_has_ref(t) ? tree_ref(t) : NULL, ctx);
      break;
   case T_INSTANCE:
      mark_actuals(t, tree_has_ref(t) ? tree_ref(t) : NULL, ctx);
      break;
   case T_ALIAS:
      // Assignments through the alias are not tracked
      mark_lvalue(tree_value(t), ctx, true);
      break;
   default:
      break;
   }
}

static void ref_cb(tree_t t, void *context)
{
   use_ctx_t *ctx = context;

   if (!tree_has_ref(t))
      return;

   tree_t decl = tree_ref(t);

   unsigned mask = USE_READ;
   const uintptr_t lv = (uintptr_t)hash_get(ctx->lvalues, t);
   if (lv == 1)
      mask = USE_WRITTEN;
   else if (lv == 2)
      mask = USE_READ | USE_WRITTEN;

   const uintptr_t old = (uintptr_t)hash_get(ctx->uses, decl);
   hash_put(ctx->uses, decl, (void *)(old | mask));
}

static void signal_cb(tree_t t, void *context)
{
   use_ctx_t *ctx = context;

   const loc_t *loc = tree_loc(t);
   if (loc_invalid_p(loc))
      return;

   const char *name = istr(tree_ident(t));
   if (!lint_name_at(loc, name))
      return;

   const uintptr_t use = (uintptr_t)hash_get(ctx->uses, t);
   if (!(use & USE_READ))
      lint_declaration(loc, LINT_UNUSED, tree_ident(t), "signal");
   else if (!(use & USE_WRITTEN) && !tree_has_value(t))
      lint_declaration(loc, LINT_UNDRIVEN, tree_ident(t), "signal");
}

void vhdl_lint(tree_t unit)
{
   if (tree_kind(unit) != T_ARCH)
      return;

   if (lint_enabled(LINT_UNUSED | LINT_UNDRIVEN)) {
      use_ctx_t ctx = {
         .lvalues = hash_new(64),
         .uses    = hash_new(64),
      };

      tree_visit(unit, lvalue_cb, &ctx);
      tree_visit_only(unit, ref_cb, &ctx, T_REF);
      tree_visit_only(unit, signal_cb, &ctx, T_SIGNAL_DECL);

      hash_free(ctx.lvalues);
      hash_free(ctx.uses);

      lint_flush();
   }
}
