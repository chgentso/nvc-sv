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
#include "scan.h"
#include "tree.h"
#include "type.h"
#include "vhdl/vhdl-phase.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

////////////////////////////////////////////////////////////////////////////////
// Sensitivity lists

typedef struct {
   tree_t        decl;
   loc_t         loc;
} read_t;

typedef A(read_t) read_list_t;

typedef struct {
   hash_t      *lvalues;   // T_REF nodes that are only written
   hash_t      *skip;      // T_REF nodes that are not reads of the value
   read_list_t  reads;
} sens_ctx_t;

static void sens_lvalue_mark(tree_t v, sens_ctx_t *ctx, bool also_read)
{
   switch (tree_kind(v)) {
   case T_AGGREGATE:
      {
         const int nassocs = tree_assocs(v);
         for (int i = 0; i < nassocs; i++)
            sens_lvalue_mark(tree_value(tree_assoc(v, i)), ctx, also_read);
      }
      break;
   case T_TYPE_CONV:
      sens_lvalue_mark(tree_value(v), ctx, also_read);
      break;
   default:
      {
         tree_t ref = name_to_ref(v);
         if (ref != NULL && !also_read)
            hash_put(ctx->lvalues, ref, ref);
      }
      break;
   }
}

static void sens_lvalue_cb(tree_t t, void *context)
{
   sens_ctx_t *ctx = context;

   switch (tree_kind(t)) {
   case T_SIGNAL_ASSIGN:
   case T_VAR_ASSIGN:
   case T_COND_ASSIGN:
   case T_FORCE:
   case T_RELEASE:
      sens_lvalue_mark(tree_target(t), ctx, false);
      break;
   case T_ATTR_REF:
      // The prefix of an attribute is not a read of the value
      if (tree_has_name(t)) {
         tree_t ref = name_to_ref(tree_name(t));
         if (ref != NULL)
            hash_put(ctx->skip, ref, ref);
      }
      break;
   default:
      break;
   }
}

static void sens_ref_cb(tree_t t, void *context)
{
   sens_ctx_t *ctx = context;

   if (!tree_has_ref(t) || hash_get(ctx->lvalues, t) != NULL
       || hash_get(ctx->skip, t) != NULL)
      return;

   tree_t decl = tree_ref(t);
   switch (tree_kind(decl)) {
   case T_SIGNAL_DECL:
   case T_PORT_DECL:
      break;
   default:
      return;
   }

   if (tree_kind(decl) == T_PORT_DECL && tree_class(decl) != C_SIGNAL)
      return;

   for (unsigned i = 0; i < ctx->reads.count; i++) {
      if (ctx->reads.items[i].decl == decl)
         return;
   }

   read_t r = { .decl = decl, .loc = *tree_loc(t) };
   APUSH(ctx->reads, r);
}

static bool loc_text(const loc_t *loc, char *buf, size_t max)
{
   // The text at a location that is on a single line

   if (loc_invalid_p(loc) || loc->line_delta != 0)
      return false;

   const char *line = loc_get_source(loc);
   if (line == NULL)
      return false;

   const size_t len = loc->column_delta + 1;
   if (len + 1 > max)
      return false;

   for (size_t i = 0; i < loc->first_column + len; i++) {
      if (line[i] == '\0' || line[i] == '\n')
         return false;
   }

   memcpy(buf, line + loc->first_column, len);
   buf[len] = '\0';
   return true;
}

static void sensitivity_check_list(tree_t proc, int nbody,
                                   tree_t *triggers, int ntriggers)
{
   if (!lint_enabled(LINT_SENSITIVITY) || lint_suppressed(tree_loc(proc),
                                                         LINT_SENSITIVITY))
      return;

   sens_ctx_t ctx = {
      .lvalues = hash_new(32),
      .skip    = hash_new(32),
      .reads   = AINIT,
   };

   for (int i = 0; i < nbody; i++)
      tree_visit(tree_stmt(proc, i), sens_lvalue_cb, &ctx);
   for (int i = 0; i < nbody; i++)
      tree_visit_only(tree_stmt(proc, i), sens_ref_cb, &ctx, T_REF);

   // Signals in the list
   read_list_t missing = AINIT;
   for (unsigned i = 0; i < ctx.reads.count; i++) {
      bool found = false;
      for (int j = 0; j < ntriggers && !found; j++) {
         tree_t ref = name_to_ref(triggers[j]);
         found = ref != NULL && tree_has_ref(ref)
            && tree_ref(ref) == ctx.reads.items[i].decl;
      }

      if (!found && !lint_suppressed(&ctx.reads.items[i].loc, LINT_SENSITIVITY))
         APUSH(missing, ctx.reads.items[i]);
   }

   if (missing.count > 0) {
      diag_t *d = lint_diag(tree_loc(proc), LINT_SENSITIVITY);
      if (d != NULL) {
         LOCAL_TEXT_BUF names = tb_new();
         for (unsigned i = 0; i < missing.count; i++)
            tb_printf(names, "%s%s", i > 0 ? ", " : "",
                      istr(tree_ident(missing.items[i].decl)));

         if (missing.count == 1)
            diag_printf(d, "signal %s is read in this process but is not in "
                        "its sensitivity list", tb_get(names));
         else
            diag_printf(d, "signals %s are read in this process but are not "
                        "in its sensitivity list", tb_get(names));

         for (unsigned i = 0; i < missing.count; i++)
            diag_hint(d, &(missing.items[i].loc), "%s is read here",
                      istr(tree_ident(missing.items[i].decl)));

         // The fix appends the signals to the list
         const loc_t *last = tree_loc(triggers[ntriggers - 1]);
         char text[256];
         if (loc_text(last, text, sizeof(text) - 1)) {
            LOCAL_TEXT_BUF fix = tb_new();
            tb_cat(fix, text);
            for (unsigned i = 0; i < missing.count; i++) {
               char name[128];
               if (!loc_text(&(missing.items[i].loc), name, sizeof(name)))
                  strcpy(name, istr(tree_ident(missing.items[i].decl)));
               tb_printf(fix, ", %s", name);
            }

            diag_fix(d, last, tb_get(fix), missing.count == 1
                     ? "Add the signal to the sensitivity list"
                     : "Add the signals to the sensitivity list");
         }

         diag_emit(d);
      }
   }

   ACLEAR(missing);
   ACLEAR(ctx.reads);
   hash_free(ctx.lvalues);
   hash_free(ctx.skip);
}

static void sensitivity_check(tree_t proc, tree_t wait)
{
   // Everything but the wait statement at the end that stands for the list
   const int ntriggers = tree_triggers(wait);
   tree_t *triggers = xmalloc_array(ntriggers, sizeof(tree_t));
   for (int i = 0; i < ntriggers; i++)
      triggers[i] = tree_trigger(wait, i);

   sensitivity_check_list(proc, tree_stmts(proc) - 1, triggers, ntriggers);
   free(triggers);
}

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

static void latch_report(tree_t proc, const latch_ctx_t *ctx,
                         const assigned_list_t *def)
{
   for (unsigned i = 0; i < ctx->possible.count; i++) {
      const assigned_t *a = &(ctx->possible.items[i]);
      if (set_has(def, a->decl) || lint_suppressed(&a->loc, LINT_LATCH))
         continue;

      lint_warn(tree_loc(proc), LINT_LATCH, "latch inferred for signal "
                "%s: not assigned on every path through this process",
                istr(tree_ident(a->decl)));
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

   if (!ctx.clocked && ctx.waits == 1)
      sensitivity_check(proc, last);

   if (!ctx.clocked && ctx.waits == 1)
      latch_report(proc, &ctx, &def);

   ACLEAR(ctx.possible);
   ACLEAR(def);
}

////////////////////////////////////////////////////////////////////////////////
// Units with errors
//
// The tree of a unit with errors has not been simplified, so a sensitivity
// list is still on the process and concurrent statements have not been
// turned into processes.  Only constructs that are free of errors are
// checked and the rules that need the whole unit are not run.

typedef A(tree_t) tree_list_t;

typedef struct {
   tree_t   unit;
   unsigned skipped;
} partial_ctx_t;

static void trigger_collect_cb(tree_t expr, void *context)
{
   tree_list_t *list = context;

   for (unsigned i = 0; i < list->count; i++) {
      if (same_tree(list->items[i], expr))
         return;
   }

   APUSH(*list, expr);
}

static bool decl_is_reliable(tree_t decl)
{
   const loc_t *loc = tree_loc(decl);
   if (loc_invalid_p(loc))
      return true;

   return !lint_span_has_error(loc);
}

static void clean_cb(tree_t t, void *context)
{
   bool *clean = context;

   switch (tree_kind(t)) {
   case T_REF:
      if (!tree_has_ref(t) || !tree_has_ident(t)
          || tree_ident(t) == error_marker()
          || !decl_is_reliable(tree_ref(t)))
         *clean = false;
      break;
   case T_FCALL:
   case T_PCALL:
      if (!tree_has_ref(t) || !tree_has_ident(t)
          || tree_ident(t) == error_marker())
         *clean = false;
      break;
   default:
      break;
   }

   switch (tree_kind(t)) {
   case T_REF:
   case T_FCALL:
   case T_ARRAY_REF:
   case T_ARRAY_SLICE:
   case T_RECORD_REF:
   case T_LITERAL:
   case T_STRING:
   case T_AGGREGATE:
   case T_ATTR_REF:
   case T_QUALIFIED:
   case T_TYPE_CONV:
   case T_SIGNAL_DECL:
   case T_VAR_DECL:
   case T_CONST_DECL:
      if (tree_has_type(t) && type_is_none(tree_type(t)))
         *clean = false;
      break;
   default:
      break;
   }
}

static bool construct_is_trusted(partial_ctx_t *pc, tree_t t)
{
   if (!lint_trusted(tree_loc(pc->unit), tree_loc(t)))
      return false;

   bool clean = true;
   tree_visit(t, clean_cb, &clean);
   return clean;
}

static void partial_process(tree_t proc, partial_ctx_t *pc)
{
   const int nstmts = tree_stmts(proc);
   const int ntriggers = tree_triggers(proc);

   // Which statements are the body and where the sensitivity list is
   tree_t last = nstmts > 0 ? tree_stmt(proc, nstmts - 1) : NULL;
   bool all = false, final_wait = false;
   if (ntriggers > 0) {
      all = ntriggers == 1 && tree_kind(tree_trigger(proc, 0)) == T_ALL;
      if (nstmts < 1)
         return;
   }
   else if (nstmts >= 2 && tree_kind(last) == T_WAIT
            && (tree_triggers(last) > 0 || tree_has_value(last)))
      final_wait = true;
   else
      return;

   if (!construct_is_trusted(pc, proc)) {
      pc->skipped++;
      return;
   }

   latch_ctx_t ctx = { .possible = AINIT };
   assigned_list_t def = AINIT;

   tree_visit(proc, clock_edge_cb, &ctx.clocked);
   walk_stmts(proc, &def, &ctx);

   // The wait statement that the simplifier would have added
   const int expected_waits = final_wait ? 1 : 0;

   if (!ctx.clocked && ctx.waits == expected_waits && !all) {
      tree_list_t triggers = AINIT;
      if (final_wait && tree_triggers(last) > 0) {
         for (int i = 0; i < tree_triggers(last); i++)
            APUSH(triggers, tree_trigger(last, i));
      }
      else if (final_wait)
         build_wait(tree_value(last), trigger_collect_cb, &triggers);
      else {
         for (int i = 0; i < ntriggers; i++)
            APUSH(triggers, tree_trigger(proc, i));
      }

      if (triggers.count > 0)
         sensitivity_check_list(proc, final_wait ? nstmts - 1 : nstmts,
                                triggers.items, triggers.count);
      ACLEAR(triggers);
   }

   if (!ctx.clocked && ctx.waits == expected_waits)
      latch_report(proc, &ctx, &def);

   ACLEAR(ctx.possible);
   ACLEAR(def);
}

static void partial_concurrent(tree_t conc, partial_ctx_t *pc)
{
   if (tree_stmts(conc) == 0)
      return;

   // The simplifier turns the statement into a process that waits on
   // every signal it reads, so only a latch can be found
   tree_t s0 = tree_stmt(conc, 0);
   switch (tree_kind(s0)) {
   case T_SIGNAL_ASSIGN:
      return;   // Always assigns
   case T_COND_ASSIGN:
   case T_SELECT:
   case T_MATCH_SELECT:
      if (tree_has_guard(s0))
         return;
      break;
   default:
      return;
   }

   if (!construct_is_trusted(pc, conc)) {
      pc->skipped++;
      return;
   }

   latch_ctx_t ctx = { .possible = AINIT };
   assigned_list_t def = AINIT;

   tree_visit(conc, clock_edge_cb, &ctx.clocked);
   walk_stmt(s0, &def, &ctx);

   if (!ctx.clocked)
      latch_report(conc, &ctx, &def);

   ACLEAR(ctx.possible);
   ACLEAR(def);
}

static void partial_cb(tree_t t, void *context)
{
   partial_ctx_t *pc = context;

   switch (tree_kind(t)) {
   case T_PROCESS:
      partial_process(t, pc);
      break;
   case T_CONCURRENT:
      partial_concurrent(t, pc);
      break;
   default:
      break;
   }
}

void vhdl_lint_partial(tree_t unit)
{
   if (tree_kind(unit) != T_ARCH)
      return;

   partial_ctx_t pc = { .unit = unit };

   if (lint_enabled(LINT_LATCH | LINT_SENSITIVITY))
      tree_visit(unit, partial_cb, &pc);

   lint_partial_note(tree_loc(unit), pc.skipped);
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

   if (lint_enabled(LINT_LATCH | LINT_SENSITIVITY))
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
