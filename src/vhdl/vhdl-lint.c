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
// Latches in combinational processes

typedef struct {
   tree_t decl;
   loc_t  loc;
} assigned_t;

typedef A(assigned_t) assigned_list_t;

typedef struct {
   assigned_list_t possible;   // Assigned on at least one path
   bool            clocked;
   unsigned        waits;
} latch_ctx_t;

static bool set_has(const assigned_list_t *set, tree_t decl)
{
   for (unsigned i = 0; i < set->count; i++) {
      if (set->items[i].decl == decl)
         return true;
   }

   return false;
}

static void set_add(assigned_list_t *set, tree_t decl, const loc_t *loc)
{
   if (!set_has(set, decl)) {
      assigned_t a = { .decl = decl, .loc = *loc };
      APUSH(*set, a);
   }
}

static void set_union(assigned_list_t *dst, const assigned_list_t *src)
{
   for (unsigned i = 0; i < src->count; i++)
      set_add(dst, src->items[i].decl, &(src->items[i].loc));
}

static void set_intersect(assigned_list_t *dst, const assigned_list_t *other)
{
   unsigned out = 0;
   for (unsigned i = 0; i < dst->count; i++) {
      if (set_has(other, dst->items[i].decl))
         dst->items[out++] = dst->items[i];
   }

   dst->count = out;
}

static void target_decls(tree_t target, assigned_list_t *set, const loc_t *loc)
{
   if (tree_kind(target) == T_AGGREGATE) {
      const int nassocs = tree_assocs(target);
      for (int i = 0; i < nassocs; i++)
         target_decls(tree_value(tree_assoc(target, i)), set, loc);
      return;
   }

   tree_t ref = name_to_ref(target);
   if (ref == NULL || !tree_has_ref(ref))
      return;

   tree_t decl = tree_ref(ref);
   switch (tree_kind(decl)) {
   case T_SIGNAL_DECL:
   case T_PORT_DECL:
      set_add(set, decl, loc);
      break;
   default:
      break;
   }
}

static bool is_static_condition(tree_t e)
{
   // True if the value depends only on generics and constants so that the
   // same branch is taken for the whole life of the process

   switch (tree_kind(e)) {
   case T_LITERAL:
   case T_STRING:
      return true;

   case T_REF:
      if (!tree_has_ref(e))
         return false;

      switch (tree_kind(tree_ref(e))) {
      case T_GENERIC_DECL:
      case T_CONST_DECL:
      case T_ENUM_LIT:
      case T_UNIT_DECL:
         return true;
      default:
         return false;
      }

   case T_FCALL:
      {
         const int nparams = tree_params(e);
         for (int i = 0; i < nparams; i++) {
            if (!is_static_condition(tree_value(tree_param(e, i))))
               return false;
         }
         return true;
      }

   case T_QUALIFIED:
   case T_TYPE_CONV:
   case T_RECORD_REF:
      return is_static_condition(tree_value(e));

   case T_ARRAY_REF:
      {
         if (!is_static_condition(tree_value(e)))
            return false;
         const int nparams = tree_params(e);
         for (int i = 0; i < nparams; i++) {
            if (!is_static_condition(tree_value(tree_param(e, i))))
               return false;
         }
         return true;
      }

   default:
      return false;
   }
}

static void walk_stmts(tree_t parent, assigned_list_t *def, latch_ctx_t *ctx);

static void walk_branches(tree_t v, assigned_list_t *def, latch_ctx_t *ctx,
                          bool complete)
{
   // Statements assigned on every branch are assigned on the whole
   assigned_list_t all = AINIT, any = AINIT;
   bool first = true, is_static = false;

   const int nbranches = tree_kind(v) == T_IF || tree_kind(v) == T_COND_ASSIGN
      ? tree_conds(v) : tree_stmts(v);
   for (int i = 0; i < nbranches; i++) {
      tree_t b;
      switch (tree_kind(v)) {
      case T_IF:
      case T_COND_ASSIGN:
         b = tree_cond(v, i);
         // A branch with a static condition is always or never taken
         // so the signals assigned in it are not latches
         if (i == 0 && tree_has_value(b) && is_static_condition(tree_value(b)))
            is_static = true;
         complete |= !tree_has_value(b) || is_static;
         break;
      default:
         b = tree_stmt(v, i);
         break;
      }

      assigned_list_t branch = AINIT;
      walk_stmts(b, &branch, ctx);

      set_union(&any, &branch);

      if (first)
         set_union(&all, &branch);
      else
         set_intersect(&all, &branch);

      first = false;
      ACLEAR(branch);
   }

   if (is_static) {
      // Only one branch is ever taken so a signal assigned in any of
      // them is not a latch
      set_union(def, &any);
   }
   else if (complete)
      set_union(def, &all);

   ACLEAR(all);
   ACLEAR(any);
}

static void walk_stmt(tree_t s, assigned_list_t *def, latch_ctx_t *ctx)
{
   switch (tree_kind(s)) {
   case T_SIGNAL_ASSIGN:
   case T_FORCE:
      target_decls(tree_target(s), def, tree_loc(s));
      set_union(&ctx->possible, def);
      break;

   case T_IF:
   case T_COND_ASSIGN:
      walk_branches(s, def, ctx, false);
      break;

   case T_CASE:
   case T_MATCH_CASE:
   case T_SELECT:
      // A case statement must cover every value of the expression
      walk_branches(s, def, ctx, true);
      break;

   case T_FOR:
   case T_WHILE:
   case T_LOOP:
   case T_SEQUENCE:
      // Loops are assumed to run at least once
      walk_stmts(s, def, ctx);
      break;

   case T_WAIT:
      ctx->waits++;
      break;

   default:
      break;
   }
}

static void walk_stmts(tree_t parent, assigned_list_t *def, latch_ctx_t *ctx)
{
   const int nstmts = tree_stmts(parent);
   for (int i = 0; i < nstmts; i++)
      walk_stmt(tree_stmt(parent, i), def, ctx);
}

static void clock_edge_cb(tree_t t, void *context)
{
   bool *clocked = context;

   // Simple heuristic to detect clock expressions
   switch (tree_kind(t)) {
   case T_ATTR_REF:
      if (tree_subkind(t) == ATTR_EVENT)
         *clocked = true;
      break;

   case T_FCALL:
      {
         const char *name = istr(tree_ident(t));
         if (strcasecmp(name, "rising_edge") == 0
             || strcasecmp(name, "falling_edge") == 0)
            *clocked = true;
      }
      break;

   default:
      break;
   }
}

static void process_cb(tree_t proc, void *context)
{
   // The simplifier has replaced the sensitivity list with a wait
   // statement at the end of the process
   const int nstmts = tree_stmts(proc);
   if (nstmts < 2)
      return;

   tree_t last = tree_stmt(proc, nstmts - 1);
   if (tree_kind(last) != T_WAIT || tree_triggers(last) == 0)
      return;

   latch_ctx_t ctx = { .possible = AINIT };
   assigned_list_t def = AINIT;

   tree_visit(proc, clock_edge_cb, &ctx.clocked);
   walk_stmts(proc, &def, &ctx);

   if (!ctx.clocked && ctx.waits == 1) {
      for (unsigned i = 0; i < ctx.possible.count; i++) {
         const assigned_t *a = &(ctx.possible.items[i]);
         if (set_has(&def, a->decl) || lint_suppressed(&a->loc, LINT_LATCH))
            continue;

         lint_warn(tree_loc(proc), LINT_LATCH, "latch inferred for signal "
                   "%s: not assigned on every path through this process",
                   istr(tree_ident(a->decl)));
      }
   }

   ACLEAR(ctx.possible);
   ACLEAR(def);
}

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

static void attr_spec_cb(tree_t t, void *context)
{
   use_ctx_t *ctx = context;

   // A signal with an attribute specification such as keep or dont_touch
   // is deliberately left in the design
   if (tree_subkind(t) != SPEC_EXACT || !tree_has_ref(t))
      return;

   tree_t decl = tree_ref(t);
   if (tree_kind(decl) == T_SIGNAL_DECL) {
      const uintptr_t old = (uintptr_t)hash_get(ctx->uses, decl);
      hash_put(ctx->uses, decl, (void *)(old | USE_READ | USE_WRITTEN));
   }
}

void vhdl_lint(tree_t unit)
{
   if (tree_kind(unit) != T_ARCH)
      return;

   if (lint_enabled(LINT_LATCH))
      tree_visit_only(unit, process_cb, NULL, T_PROCESS);

   if (lint_enabled(LINT_UNUSED | LINT_UNDRIVEN)) {
      use_ctx_t ctx = {
         .lvalues = hash_new(64),
         .uses    = hash_new(64),
      };

      tree_visit(unit, lvalue_cb, &ctx);
      tree_visit_only(unit, ref_cb, &ctx, T_REF);
      tree_visit_only(unit, attr_spec_cb, &ctx, T_ATTR_SPEC);
      tree_visit_only(unit, signal_cb, &ctx, T_SIGNAL_DECL);

      hash_free(ctx.lvalues);
      hash_free(ctx.uses);

      lint_flush();
   }
}
