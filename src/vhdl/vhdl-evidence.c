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
#include "conn-priv.h"
#include "fsm.h"
#include "hash.h"
#include "ident.h"
#include "tree.h"
#include "type.h"

#include <assert.h>
#include <inttypes.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

//
// Clocking evidence of VHDL processes and attributes of signals and
// units, for --netlist --evidence.
//
// A process is walked twice at most.  Guards (the reads of the conditions
// of the enclosing if and case statements) and the clock region are
// tracked on the way; every assignment to a signal adds the reads of its
// right-hand side to the data set and the guards to the control set of
// the driven ranges.  Variables carry the sets of what was assigned to
// them and are substituted where they are read.
//

typedef struct _var var_t;

struct _var {
   tree_t       decl;
   range_list_t data, ctrl;
   bool         assigned;
   bool         has_copy;
   conn_range_t copy;
   var_t       *next;
};

typedef struct {
   range_list_t data, ctrl;
   unsigned     nseq, ncomb;
   unsigned     why;
   int          clock;
   bool         seen;
   int          copy;      // 0 none yet, 1 candidate, 2 broken
   conn_range_t tgt, src;
} out_t;

typedef A(conn_ev_clock_t) clock_list_t;
typedef A(conn_ev_async_t) async_list_t;

typedef struct {
   conn_design_t *cd;
   conn_proc_t   *proc;
   clock_list_t   clocks;
   async_list_t   async;
   range_list_t   sync_reset;
   range_list_t   guards;
   range_list_t   pcall;
   out_t         *outs;
   hash_t        *vars;
   var_t         *varlist;
   bool           approx;
   unsigned       why;       // conn_ev_why_t of the whole process
   unsigned       loop_depth;// Inside a loop with approximate bounds
   bool           region;
   bool           first_stmt;
   bool           in_async;
   int            clock;
   unsigned       cond_depth;
   bool           has_vars;
   bool           reg_var;
   int            pass;
} walker_t;

static void approx_because(walker_t *w, unsigned why)
{
   w->approx = true;
   w->why |= why;
}

////////////////////////////////////////////////////////////////////////////////
// Names and expressions

// Last component of a function name without quotes and signature
static bool name_is(ident_t id, const char *word)
{
   const char *s = istr(id);
   const char *dot = strrchr(s, '.');
   if (dot != NULL)
      s = dot + 1;
   if (*s == '"')
      s++;

   const size_t n = strlen(word);
   return strncasecmp(s, word, n) == 0
      && (s[n] == '\0' || s[n] == '"' || s[n] == '(');
}

static tree_t strip(tree_t e)
{
   while (e != NULL) {
      switch (tree_kind(e)) {
      case T_QUALIFIED:
      case T_TYPE_CONV:
         e = tree_value(e);
         break;
      default:
         return e;
      }
   }

   return e;
}

static tree_t param_value(tree_t call, unsigned n)
{
   if (n >= tree_params(call))
      return NULL;
   tree_t p = tree_param(call, n);
   return tree_has_value(p) ? tree_value(p) : NULL;
}

static bool is_call(tree_t e, const char *name, unsigned nparams)
{
   return e != NULL && tree_kind(e) == T_FCALL && tree_has_ident(e)
      && tree_params(e) == nparams && name_is(tree_ident(e), name);
}

static bool name_kind_p(tree_t e)
{
   switch (tree_kind(e)) {
   case T_REF:
   case T_ARRAY_REF:
   case T_ARRAY_SLICE:
   case T_RECORD_REF:
      return true;
   default:
      return false;
   }
}

// The object a name denotes, for variables
static tree_t name_base(tree_t e)
{
   while (e != NULL) {
      switch (tree_kind(e)) {
      case T_ARRAY_REF:
      case T_ARRAY_SLICE:
      case T_RECORD_REF:
      case T_QUALIFIED:
      case T_TYPE_CONV:
         e = tree_value(e);
         break;
      default:
         return e;
      }
   }

   return NULL;
}

static bool var_ref_p(tree_t e, tree_t *decl)
{
   if (e == NULL || tree_kind(e) != T_REF || !tree_has_ref(e))
      return false;

   tree_t d = tree_ref(e);
   if (tree_kind(d) != T_VAR_DECL)
      return false;

   *decl = d;
   return true;
}

// The edge test of an expression: fsm.c knows the usual forms and
// rising_edge(x(i)) is the same with a longer name
static bool edge_of(tree_t e, tree_t *clk, conn_edge_t *edge)
{
   bool rising;
   if (fsm_vhdl_edge(e, clk, &rising)) {
      *edge = rising ? CONN_EDGE_RISE : CONN_EDGE_FALL;
      return true;
   }

   e = strip(e);
   if (e == NULL || tree_kind(e) != T_FCALL || !tree_has_ident(e)
       || tree_params(e) != 1)
      return false;

   const bool r = name_is(tree_ident(e), "rising_edge");
   if (!r && !name_is(tree_ident(e), "falling_edge"))
      return false;

   tree_t arg = strip(param_value(e, 0));
   if (arg == NULL || !name_kind_p(arg))
      return false;

   *clk = arg;
   *edge = r ? CONN_EDGE_RISE : CONN_EDGE_FALL;
   return true;
}

static var_t *get_var(walker_t *w, tree_t decl)
{
   var_t *v = hash_get(w->vars, decl);
   if (v == NULL) {
      v = xcalloc(sizeof(var_t));
      v->decl = decl;
      v->next = w->varlist;
      w->varlist = v;
      hash_put(w->vars, decl, v);
   }

   return v;
}

typedef struct {
   walker_t     *w;
   range_list_t *data, *ctrl;
} varvisit_t;

static void var_read_cb(tree_t t, void *ctx)
{
   varvisit_t *vv = ctx;
   tree_t decl;
   if (!var_ref_p(t, &decl))
      return;

   var_t *v = get_var(vv->w, decl);
   if (!v->assigned) {
      // Read before it is assigned: a register
      vv->w->reg_var = true;
   }

   for (unsigned i = 0; i < v->data.count; i++)
      APUSH(*vv->data, v->data.items[i]);
   for (unsigned i = 0; i < v->ctrl.count; i++)
      APUSH(*vv->ctrl, v->ctrl.items[i]);
}

// Add what the expression reads to DATA, and the control sets of the
// variables it reads to CTRL
static void expr_deps(walker_t *w, tree_t e, range_list_t *data,
                      range_list_t *ctrl)
{
   vhdl_collect_reads(w->cd, e, data, false);

   if (w->has_vars) {
      varvisit_t vv = { w, data, ctrl };
      tree_visit_only(e, var_read_cb, &vv, T_REF);
   }
}

// An expression that reads no signal and no variable
static bool constant_expr(walker_t *w, tree_t e)
{
   SCOPED_LIST(range_list_t, data);
   SCOPED_LIST(range_list_t, ctrl);
   const bool saved = w->reg_var;
   expr_deps(w, e, &data, &ctrl);
   w->reg_var = saved;
   return data.count == 0 && ctrl.count == 0;
}

// A test of one signal against a literal: its level
static bool simple_test_name(walker_t *w, tree_t e, conn_range_t *r,
                             conn_level_t *level, tree_t *name)
{
   e = strip(e);
   if (e == NULL)
      return false;

   bool approx = false;

   if (is_call(e, "not", 1)) {
      tree_t a = strip(param_value(e, 0));
      if (a != NULL && name_kind_p(a) && vhdl_select(w->cd, a, r, &approx)) {
         *level = CONN_LEVEL_LOW;
         *name = a;
         return true;
      }
      return false;
   }

   if (name_kind_p(e)) {
      if (!vhdl_select(w->cd, e, r, &approx))
         return false;
      *level = CONN_LEVEL_HIGH;
      *name = e;
      return true;
   }

   const bool eq = is_call(e, "=", 2);
   if (!eq && !is_call(e, "/=", 2))
      return false;

   for (int i = 0; i < 2; i++) {
      tree_t a = strip(param_value(e, i)), b = strip(param_value(e, 1 - i));
      if (a == NULL || b == NULL || !name_kind_p(a)
          || tree_kind(b) != T_REF || !tree_has_ref(b)
          || tree_kind(tree_ref(b)) != T_ENUM_LIT)
         continue;

      const char *lit = istr(tree_ident(tree_ref(b)));
      bool high;
      if (!strcmp(lit, "'1'") || !strcmp(lit, "'H'") || !strcasecmp(lit, "true"))
         high = true;
      else if (!strcmp(lit, "'0'") || !strcmp(lit, "'L'")
               || !strcasecmp(lit, "false"))
         high = false;
      else
         continue;

      if (!vhdl_select(w->cd, a, r, &approx))
         continue;

      *level = high == eq ? CONN_LEVEL_HIGH : CONN_LEVEL_LOW;
      *name = a;
      return true;
   }

   return false;
}

static bool simple_test(walker_t *w, tree_t e, conn_range_t *r,
                        conn_level_t *level)
{
   tree_t name;
   return simple_test_name(w, e, r, level, &name);
}

static conn_role_t guess_role(const conn_net_t *net)
{
   char *lc LOCAL = xstrdup(net->name);
   for (char *p = lc; *p; p++)
      *p = tolower((unsigned char)*p);

   if (strstr(lc, "rst") || strstr(lc, "reset") || strstr(lc, "clr")
       || strstr(lc, "clear"))
      return CONN_ROLE_RESET;
   else if (strstr(lc, "set"))
      return CONN_ROLE_SET;
   else
      return CONN_ROLE_UNKNOWN;
}

////////////////////////////////////////////////////////////////////////////////
// Clocks and asynchronous controls

static int add_clock(walker_t *w, const conn_range_t *r, conn_edge_t edge)
{
   for (unsigned i = 0; i < w->clocks.count; i++) {
      const conn_ev_clock_t *c = &(w->clocks.items[i]);
      if (c->sig.net == r->net && c->sig.lo == r->lo && c->sig.hi == r->hi
          && c->edge == edge)
         return i;
   }

   conn_ev_clock_t c = { .sig = *r, .edge = edge };
   APUSH(w->clocks, c);
   return w->clocks.count - 1;
}

static void add_async(walker_t *w, const conn_range_t *r, conn_level_t level)
{
   for (unsigned i = 0; i < w->async.count; i++) {
      const conn_ev_async_t *a = &(w->async.items[i]);
      if (a->sig.net == r->net && a->sig.lo == r->lo && a->sig.hi == r->hi
          && a->level == level)
         return;
   }

   conn_ev_async_t a = { .sig = *r, .level = level,
                         .role = guess_role(r->net) };
   APUSH(w->async, a);
}

// Does the expression test a clock edge, perhaps and-ed with other terms
// whose reads are added to REST
static bool split_edge(walker_t *w, tree_t e, tree_t *clk, conn_edge_t *edge,
                       range_list_t *rest)
{
   if (e == NULL)
      return false;
   else if (edge_of(e, clk, edge))
      return true;

   e = strip(e);
   if (!is_call(e, "and", 2))
      return false;

   for (int i = 0; i < 2; i++) {
      SCOPED_LIST(range_list_t, tmp);
      if (split_edge(w, param_value(e, i), clk, edge, &tmp)) {
         for (unsigned j = 0; j < tmp.count; j++)
            APUSH(*rest, tmp.items[j]);
         expr_deps(w, param_value(e, 1 - i), rest, rest);
         return true;
      }
   }

   return false;
}

static int clock_index(walker_t *w, tree_t clk, conn_edge_t edge)
{
   conn_range_t r;
   bool approx = false;
   if (!vhdl_select(w->cd, clk, &r, &approx)) {
      approx_because(w, CONN_WHY_OTHER);   // Not a signal of this block
      return -1;
   }

   if (approx)
      approx_because(w, CONN_WHY_DYNAMIC);

   return add_clock(w, &r, edge);
}

////////////////////////////////////////////////////////////////////////////////
// Assignments

static bool copy_source(walker_t *w, tree_t value, conn_range_t *src)
{
   tree_t e = strip(value);
   if (e == NULL || !name_kind_p(e))
      return false;

   bool approx = false;
   if (vhdl_select(w->cd, e, src, &approx))
      return !approx;

   tree_t decl;
   if (tree_kind(e) == T_REF && var_ref_p(e, &decl)) {
      var_t *v = get_var(w, decl);
      if (v->has_copy) {
         *src = v->copy;
         return true;
      }
   }

   return false;
}

static void append_list(range_list_t *to, const range_list_t *from)
{
   for (unsigned i = 0; i < from->count; i++)
      APUSH(*to, from->items[i]);
}

static void assign_to(walker_t *w, tree_t target, tree_t s)
{
   SCOPED_LIST(range_list_t, data);
   SCOPED_LIST(range_list_t, ctrl);

   const unsigned nwaves = tree_kind(s) == T_SIGNAL_ASSIGN
      ? tree_waveforms(s) : 1;
   tree_t single = NULL;

   if (tree_kind(s) == T_SIGNAL_ASSIGN) {
      for (unsigned i = 0; i < nwaves; i++) {
         tree_t wv = tree_waveform(s, i);
         if (tree_has_value(wv)) {
            expr_deps(w, tree_value(wv), &data, &ctrl);
            if (nwaves == 1 && !tree_has_delay(wv))
               single = tree_value(wv);
         }
      }
   }
   else if (tree_has_value(s)) {
      expr_deps(w, tree_value(s), &data, &ctrl);
      single = tree_value(s);
   }

   append_list(&ctrl, &w->guards);

   tree_t base = name_base(target);
   tree_t vdecl;
   if (base != NULL && var_ref_p(base, &vdecl)) {
      var_t *v = get_var(w, vdecl);
      const bool whole = strip(target) == base && w->cond_depth == 0;

      if (whole) {
         v->data.count = 0;
         v->ctrl.count = 0;
      }

      append_list(&v->data, &data);
      append_list(&v->ctrl, &ctrl);
      v->assigned = true;

      conn_range_t src;
      v->has_copy = whole && single != NULL && copy_source(w, single, &src);
      if (v->has_copy)
         v->copy = src;
      return;
   }

   conn_range_t r;
   bool approx = false;
   if (target == NULL || !name_kind_p(strip(target))
       || !vhdl_select(w->cd, strip(target), &r, &approx)) {
      approx_because(w, CONN_WHY_OTHER);
      return;
   }

   if (approx)
      approx_because(w, CONN_WHY_DYNAMIC);

   conn_proc_t *p = w->proc;
   for (unsigned j = 0; j < p->ndrives; j++) {
      const conn_range_t *d = &(p->drives[j]);
      if (d->net != r.net || d->lo > r.hi || r.lo > d->hi)
         continue;

      out_t *o = &(w->outs[j]);
      o->seen = true;
      if (approx)
         o->why |= CONN_WHY_DYNAMIC;
      if (w->loop_depth > 0)
         o->why |= CONN_WHY_LOOP;
      append_list(&o->data, &data);
      append_list(&o->ctrl, &ctrl);

      if (w->region) {
         o->nseq++;
         if (o->clock < 0)
            o->clock = w->clock;
         else if (o->clock != w->clock)
            approx_because(w, CONN_WHY_OTHER);
      }
      else
         o->ncomb++;

      // A copy of one whole input: the constants assigned by the
      // asynchronous controls do not count, those of a synchronous reset
      // do
      if (w->in_async || o->copy == 2)
         continue;

      conn_range_t src;
      if (single != NULL && copy_source(w, single, &src)
          && src.hi - src.lo == r.hi - r.lo
          && (o->copy == 0 || (o->src.net == src.net && o->src.lo == src.lo
                               && o->src.hi == src.hi
                               && o->tgt.lo == r.lo && o->tgt.hi == r.hi))) {
         o->copy = 1;
         o->src = src;
         o->tgt = r;
      }
      else
         o->copy = 2;
   }
}

////////////////////////////////////////////////////////////////////////////////
// Statements

static void walk_stmts(walker_t *w, tree_t parent);

// Every statement is an assignment of constants
static bool only_constants(walker_t *w, tree_t parent)
{
   const int n = tree_stmts(parent);
   if (n == 0)
      return false;

   for (int i = 0; i < n; i++) {
      tree_t s = tree_stmt(parent, i);
      switch (tree_kind(s)) {
      case T_SIGNAL_ASSIGN:
         for (unsigned j = 0; j < tree_waveforms(s); j++) {
            tree_t wv = tree_waveform(s, j);
            if (tree_has_value(wv) && !constant_expr(w, tree_value(wv)))
               return false;
         }
         break;
      case T_VAR_ASSIGN:
         if (!tree_has_value(s) || !constant_expr(w, tree_value(s)))
            return false;
         break;
      default:
         return false;
      }
   }

   return true;
}

static void walk_clocked_if(walker_t *w, tree_t s, unsigned k, tree_t clk,
                            conn_edge_t edge, range_list_t *extra)
{
   const int ci = clock_index(w, clk, edge);

   const bool saved_region = w->region;
   const int saved_clock = w->clock;
   const bool saved_first = w->first_stmt;
   const unsigned saved_depth = w->cond_depth;
   const size_t mark = w->guards.count;

   // Branches before the edge are asynchronous controls
   for (unsigned i = 0; i < k; i++) {
      tree_t c = tree_cond(s, i);

      conn_range_t r;
      conn_level_t level;
      if (tree_has_value(c) && simple_test(w, tree_value(c), &r, &level))
         add_async(w, &r, level);
      else if (tree_has_value(c)) {
         SCOPED_LIST(range_list_t, reads);
         expr_deps(w, tree_value(c), &reads, &reads);
         merge_ranges(&reads);
         for (unsigned j = 0; j < reads.count; j++)
            add_async(w, &reads.items[j], CONN_LEVEL_UNKNOWN);
      }

      w->region = true;
      w->clock = ci;
      w->first_stmt = false;
      w->in_async = true;
      walk_stmts(w, c);
      w->in_async = false;
   }

   w->region = true;
   w->clock = ci;
   w->first_stmt = true;
   append_list(&w->guards, extra);
   walk_stmts(w, tree_cond(s, k));
   w->guards.count = mark;

   w->region = saved_region;
   w->clock = saved_clock;
   w->first_stmt = saved_first;
   w->cond_depth = saved_depth;

   // What follows the edge branch is not clocked by it, unless it tests
   // another edge: both edges of a clock or a second clock
   for (unsigned i = k + 1; i < tree_conds(s); i++) {
      tree_t c = tree_cond(s, i);
      tree_t clk2;
      conn_edge_t edge2;
      SCOPED_LIST(range_list_t, extra2);
      if (tree_has_value(c)
          && split_edge(w, tree_value(c), &clk2, &edge2, &extra2)) {
         const int cj = clock_index(w, clk2, edge2);
         w->region = true;
         w->clock = cj;
         w->first_stmt = false;
         append_list(&w->guards, &extra2);
         walk_stmts(w, c);
         w->guards.count = mark;
         w->region = saved_region;
         w->clock = saved_clock;
      }
      else
         walk_stmts(w, c);
   }
}

static void walk_if(walker_t *w, tree_t s, bool first)
{
   const unsigned n = tree_conds(s);

   for (unsigned i = 0; i < n; i++) {
      tree_t c = tree_cond(s, i);
      if (!tree_has_value(c))
         continue;

      tree_t clk;
      conn_edge_t edge;
      SCOPED_LIST(range_list_t, extra);
      if (split_edge(w, tree_value(c), &clk, &edge, &extra)) {
         walk_clocked_if(w, s, i, clk, edge, &extra);
         return;
      }
   }

   if (first && w->region && w->cond_depth == 0 && n > 0) {
      // The first statement of the clocked part tests a reset
      tree_t c = tree_cond(s, 0);
      conn_range_t r;
      conn_level_t level;
      if (tree_has_value(c) && simple_test(w, tree_value(c), &r, &level)
          && only_constants(w, c))
         expr_deps(w, tree_value(c), &w->sync_reset, &w->sync_reset);
   }

   const size_t mark = w->guards.count;
   const bool saved_first = w->first_stmt;
   w->cond_depth++;

   for (unsigned i = 0; i < n; i++) {
      tree_t c = tree_cond(s, i);
      if (tree_has_value(c))
         expr_deps(w, tree_value(c), &w->guards, &w->guards);

      w->first_stmt = false;
      walk_stmts(w, c);
   }

   w->first_stmt = saved_first;
   w->cond_depth--;
   w->guards.count = mark;
}

static void walk_stmt(walker_t *w, tree_t s, bool first)
{
   switch (tree_kind(s)) {
   case T_IF:
      walk_if(w, s, first);
      break;

   case T_CASE:
   case T_MATCH_CASE:
      {
         const size_t mark = w->guards.count;
         expr_deps(w, tree_value(s), &w->guards, &w->guards);
         w->cond_depth++;
         for (unsigned i = 0; i < tree_stmts(s); i++)
            walk_stmts(w, tree_stmt(s, i));
         w->cond_depth--;
         w->guards.count = mark;
      }
      break;

   case T_SIGNAL_ASSIGN:
   case T_VAR_ASSIGN:
      assign_to(w, tree_target(s), s);
      break;

   case T_FOR:
      {
         // A bound that depends on a signal or variable is approximated
         SCOPED_LIST(range_list_t, bound);
         for (unsigned i = 0; i < tree_ranges(s); i++)
            expr_deps(w, tree_range(s, i), &bound, &bound);

         const size_t mark = w->guards.count;
         if (bound.count > 0) {
            approx_because(w, CONN_WHY_LOOP);
            append_list(&w->guards, &bound);
            w->loop_depth++;
         }
         w->cond_depth++;
         walk_stmts(w, s);
         w->cond_depth--;
         if (bound.count > 0)
            w->loop_depth--;
         w->guards.count = mark;
      }
      break;

   case T_WHILE:
      {
         approx_because(w, CONN_WHY_LOOP);
         const size_t mark = w->guards.count;
         if (tree_has_value(s))
            expr_deps(w, tree_value(s), &w->guards, &w->guards);
         w->cond_depth++;
         w->loop_depth++;
         walk_stmts(w, s);
         w->loop_depth--;
         w->cond_depth--;
         w->guards.count = mark;
      }
      break;

   case T_LOOP:
      approx_because(w, CONN_WHY_LOOP);
      w->cond_depth++;
      w->loop_depth++;
      walk_stmts(w, s);
      w->loop_depth--;
      w->cond_depth--;
      break;

   case T_SEQUENCE:
   case T_COND_STMT:
   case T_ALTERNATIVE:
      walk_stmts(w, s);
      break;

   case T_PCALL:
      // Which signals the procedure drives and from what is not known
      approx_because(w, CONN_WHY_CALL);
      for (unsigned i = 0; i < tree_params(s); i++) {
         tree_t v = param_value(s, i);
         if (v != NULL)
            expr_deps(w, v, &w->pcall, &w->pcall);
      }
      break;

   case T_COND_ASSIGN:
   case T_SELECT:
      approx_because(w, CONN_WHY_OTHER);   // Normally rewritten to if/case
      break;

   default:
      break;
   }
}

static void walk_stmts(walker_t *w, tree_t parent)
{
   const unsigned n = tree_stmts(parent);
   for (unsigned i = 0; i < n; i++) {
      const bool first = w->first_stmt;
      w->first_stmt = false;
      walk_stmt(w, tree_stmt(parent, i), first);
   }
}

////////////////////////////////////////////////////////////////////////////////
// Processes

typedef struct {
   unsigned  count;
   tree_t    last;
} waits_t;

static void count_waits_cb(tree_t t, void *ctx)
{
   waits_t *ws = ctx;
   ws->count++;
   ws->last = t;
}

static void clear_out(out_t *o)
{
   ACLEAR(o->data);
   ACLEAR(o->ctrl);
   memset(o, 0, sizeof(out_t));
   o->clock = -1;
}

static void run_pass(walker_t *w, tree_t t, int pass, bool wait_clocked,
                     tree_t wait, tree_t clk, conn_edge_t edge,
                     range_list_t *extra)
{
   w->pass = pass;
   w->region = false;
   w->clock = -1;
   w->first_stmt = false;
   w->cond_depth = 0;
   w->guards.count = 0;
   w->clocks.count = 0;
   w->async.count = 0;
   w->sync_reset.count = 0;
   w->pcall.count = 0;
   for (unsigned i = 0; i < w->proc->ndrives; i++)
      clear_out(&w->outs[i]);

   if (wait_clocked) {
      w->region = true;
      w->clock = clock_index(w, clk, edge);
      w->first_stmt = true;
      append_list(&w->guards, extra);
      (void)wait;
   }

   walk_stmts(w, t);
}

static void other_process(conn_design_t *cd, conn_proc_t *proc)
{
   conn_evidence_t *ev = dalloc(cd, sizeof(conn_evidence_t));
   ev->cls = CONN_EV_OTHER;
   proc->ev = ev;
}

// The range is a clock or an asynchronous control of the process
static bool control_range(const walker_t *w, const conn_range_t *r)
{
   for (unsigned i = 0; i < w->clocks.count; i++) {
      const conn_range_t *c = &(w->clocks.items[i].sig);
      if (c->net == r->net && c->lo <= r->lo && c->hi >= r->hi)
         return true;
   }

   for (unsigned i = 0; i < w->async.count; i++) {
      const conn_range_t *c = &(w->async.items[i].sig);
      if (c->net == r->net && c->lo <= r->lo && c->hi >= r->hi)
         return true;
   }

   return false;
}

static range_list_t *finish_list(range_list_t *l)
{
   merge_ranges(l);
   return l;
}

void vhdl_proc_evidence(conn_design_t *cd, conn_proc_t *proc, tree_t t)
{
   // What kind of process is it?  A process that waits only at its end on
   // a sensitivity list, or only on one clock edge at its start or its
   // end is analysed; any other wait makes it bench-like code.
   waits_t ws = { 0 };
   tree_visit_only(t, count_waits_cb, &ws, T_WAIT);

   const unsigned nstmts = tree_stmts(t);
   if (ws.count != 1 || nstmts == 0) {
      other_process(cd, proc);
      return;
   }

   tree_t first = tree_stmt(t, 0), last = tree_stmt(t, nstmts - 1);
   tree_t wait = ws.last;
   if (wait != first && wait != last) {
      other_process(cd, proc);
      return;
   }

   walker_t w = {
      .cd = cd,
      .proc = proc,
      .vars = hash_new(16),
      .outs = xcalloc_array(MAX(proc->ndrives, 1), sizeof(out_t)),
   };

   bool wait_clocked = false;
   tree_t wclk = NULL;
   conn_edge_t wedge = CONN_EDGE_RISE;
   SCOPED_LIST(range_list_t, extra);

   if (tree_has_delay(wait)) {
      other_process(cd, proc);
      goto cleanup;
   }
   else if (tree_has_value(wait)) {
      tree_t cond = tree_value(wait);
      conn_range_t r;
      conn_level_t level;
      if (split_edge(&w, cond, &wclk, &wedge, &extra))
         wait_clocked = true;
      else if (simple_test_name(&w, cond, &r, &level, &wclk)) {
         // wait until clk = '1' resumes on the event that makes it true
         wait_clocked = true;
         wedge = level == CONN_LEVEL_LOW ? CONN_EDGE_FALL : CONN_EDGE_RISE;
      }
      else {
         other_process(cd, proc);
         goto cleanup;
      }
   }
   else if (wait != last) {
      other_process(cd, proc);
      goto cleanup;
   }

   for (unsigned i = 0; i < tree_decls(t); i++) {
      if (tree_kind(tree_decl(t, i)) == T_VAR_DECL)
         w.has_vars = true;
   }

   for (unsigned i = 0; i < proc->ndrives; i++)
      w.outs[i].clock = -1;

   run_pass(&w, t, 1, wait_clocked, wait, wclk, wedge, &extra);

   bool registers = false;
   if (w.has_vars) {
      // A variable read before it is assigned holds a register: walk
      // again so that it carries what it was assigned in the last pass
      registers = w.reg_var;
      run_pass(&w, t, 2, wait_clocked, wait, wclk, wedge, &extra);
   }

   conn_evidence_t *ev = dalloc(cd, sizeof(conn_evidence_t));
   ev->approx = w.approx || registers;
   ev->reasons = w.why | (registers ? CONN_WHY_VARIABLE : 0);

   ev->nclocks = w.clocks.count;
   ev->clocks = DDUP(cd, w.clocks, conn_ev_clock_t);
   ev->nasync = w.async.count;
   ev->async = DDUP(cd, w.async, conn_ev_async_t);
   finish_list(&w.sync_reset);
   ev->nsync_reset = w.sync_reset.count;
   ev->sync_reset = DDUP(cd, w.sync_reset, conn_range_t);

   merge_ranges(&w.pcall);

   ev->nouts = proc->ndrives;
   ev->outs = dalloc(cd, MAX(proc->ndrives, 1) * sizeof(conn_out_ev_t));

   unsigned nseq = 0, ncomb = 0;
   for (unsigned j = 0; j < proc->ndrives; j++) {
      out_t *o = &(w.outs[j]);
      conn_out_ev_t *oe = &(ev->outs[j]);
      oe->drive = j;
      oe->clock = -1;

      if (!o->seen) {
         // Driven in a way that was not followed: assume it depends on
         // everything the process reads
         ev->approx = true;
         ev->reasons |= w.pcall.count > 0 ? CONN_WHY_CALL : CONN_WHY_OTHER;
         o->why |= w.pcall.count > 0 ? CONN_WHY_CALL : CONN_WHY_OTHER;
         for (unsigned i = 0; i < proc->nreads; i++) {
            if (!control_range(&w, &(proc->reads[i])))
               APUSH(o->data, proc->reads[i]);
         }
         if (w.clocks.count > 0)
            o->nseq = 1;
         else
            o->ncomb = 1;
         o->clock = 0;
      }

      for (unsigned i = 0; i < w.pcall.count; i++) {
         if (!control_range(&w, &(w.pcall.items[i])))
            APUSH(o->data, w.pcall.items[i]);
      }

      if (o->nseq > 0 && o->ncomb == 0) {
         oe->cls = CONN_EV_SEQ;
         oe->clock = MAX(o->clock, 0);
         if (w.clocks.count == 0)
            oe->clock = -1;
         nseq++;
      }
      else {
         oe->cls = CONN_EV_COMB;
         if (o->nseq > 0) {
            ev->approx = true;
            ev->reasons |= CONN_WHY_OTHER;
            o->why |= CONN_WHY_OTHER;
         }
         ncomb++;
      }

      oe->reasons = o->why;
      ev->reasons |= o->why;
      finish_list(&o->data);
      finish_list(&o->ctrl);
      oe->ndata = o->data.count;
      oe->data = DDUP(cd, o->data, conn_range_t);
      oe->nctrl = o->ctrl.count;
      oe->ctrl = DDUP(cd, o->ctrl, conn_range_t);

      if (o->copy == 1 && o->tgt.net == proc->drives[j].net
          && o->tgt.lo == proc->drives[j].lo
          && o->tgt.hi == proc->drives[j].hi) {
         oe->has_copy = true;
         oe->copy = o->src;
      }
   }

   if (nseq > 0 && ncomb > 0)
      ev->cls = CONN_EV_MIXED;
   else if (nseq > 0 || (ncomb == 0 && w.clocks.count > 0))
      ev->cls = CONN_EV_SEQ;
   else
      ev->cls = CONN_EV_COMB;

   if (ev->approx && ev->reasons == 0)
      ev->reasons = CONN_WHY_OTHER;

   proc->ev = ev;

cleanup:
   for (unsigned i = 0; i < proc->ndrives; i++) {
      ACLEAR(w.outs[i].data);
      ACLEAR(w.outs[i].ctrl);
   }
   free(w.outs);

   for (var_t *v = w.varlist, *next; v; v = next) {
      next = v->next;
      ACLEAR(v->data);
      ACLEAR(v->ctrl);
      free(v);
   }

   hash_free(w.vars);
   ACLEAR(w.clocks);
   ACLEAR(w.async);
   ACLEAR(w.sync_reset);
   ACLEAR(w.guards);
   ACLEAR(w.pcall);
}

////////////////////////////////////////////////////////////////////////////////
// Attributes

typedef struct _spec_node spec_node_t;

struct _spec_node {
   tree_t       spec;
   spec_node_t *next;
};

static void add_spec(conn_design_t *cd, tree_t decl, tree_t spec)
{
   hash_t *h = conn_specs(cd);

   for (spec_node_t *n = hash_get(h, decl); n; n = n->next) {
      if (n->spec == spec)
         return;
   }

   spec_node_t *n = dalloc(cd, sizeof(spec_node_t));
   n->spec = spec;
   n->next = hash_get(h, decl);
   hash_put(h, decl, n);
}

static bool has_spec_named(conn_design_t *cd, tree_t decl, ident_t name)
{
   for (spec_node_t *n = hash_get(conn_specs(cd), decl); n; n = n->next) {
      if (tree_ident(n->spec) == name)
         return true;
   }

   return false;
}

static void collect_specs(conn_design_t *cd, tree_t owner)
{
   const int ndecls = tree_decls(owner);

   for (int i = 0; i < ndecls; i++) {
      tree_t d = tree_decl(owner, i);
      if (tree_kind(d) != T_ATTR_SPEC || tree_class(d) != C_SIGNAL)
         continue;

      if (tree_subkind(d) == SPEC_EXACT && tree_has_ref(d))
         add_spec(cd, tree_ref(d), d);
   }

   for (int i = 0; i < ndecls; i++) {
      tree_t d = tree_decl(owner, i);
      if (tree_kind(d) != T_ATTR_SPEC || tree_class(d) != C_SIGNAL
          || tree_subkind(d) == SPEC_EXACT)
         continue;

      // all and others: the signals declared here.  The ports are not
      // included: the specification may be in the architecture, which
      // does not cover them, and the declarations of the entity and the
      // architecture are not told apart in the block.
      for (int j = 0; j < ndecls; j++) {
         tree_t s = tree_decl(owner, j);
         if (tree_kind(s) == T_SIGNAL_DECL
             && !has_spec_named(cd, s, tree_ident(d)))
            add_spec(cd, s, d);
      }
   }
}

// The value of an attribute as the text of a literal
static char *spec_value(conn_design_t *cd, tree_t value)
{
   if (value == NULL)
      return NULL;

   switch (tree_kind(value)) {
   case T_STRING:
      {
         LOCAL_TEXT_BUF tb = tb_new();
         const int n = tree_chars(value);
         for (int i = 0; i < n; i++) {
            tree_t c = tree_char(value, i);
            if (tree_kind(c) != T_REF || !tree_has_ref(c))
               return NULL;
            const char *id = istr(tree_ident(tree_ref(c)));
            if (strlen(id) != 3 || id[0] != '\'')
               return NULL;
            tb_append(tb, id[1]);
         }
         return dstr(cd, tb_get(tb));
      }

   case T_LITERAL:
      if (tree_subkind(value) == L_INT) {
         char buf[32];
         checked_sprintf(buf, sizeof(buf), "%"PRIi64, tree_ival(value));
         return dstr(cd, buf);
      }
      return NULL;

   case T_REF:
      if (tree_has_ref(value) && tree_kind(tree_ref(value)) == T_ENUM_LIT) {
         const char *id = istr(tree_ident(tree_ref(value)));
         if (id[0] == '\'' && strlen(id) == 3)
            {
               char b[2] = { id[1], '\0' };
               return dstr(cd, b);
            }
         char *lc = dstr(cd, id);
         for (char *p = lc; *p; p++)
            *p = tolower((unsigned char)*p);
         return lc;
      }
      return NULL;

   default:
      return NULL;
   }
}

static void add_spec_attr(conn_design_t *cd, conn_attr_t **list,
                          unsigned *count, tree_t spec)
{
   char *value = spec_value(cd, tree_has_value(spec) ? tree_value(spec) : NULL);
   if (value == NULL)
      return;

   char *name LOCAL = xstrdup(istr(tree_ident(spec)));
   for (char *p = name; *p; p++)
      *p = tolower((unsigned char)*p);

   conn_add_attr(cd, list, count, name, value);
}

void vhdl_net_attrs(conn_design_t *cd, conn_net_t *net, tree_t decl)
{
   // The oldest specification first
   spec_node_t *rev = NULL;
   for (spec_node_t *n = hash_get(conn_specs(cd), decl); n; n = n->next) {
      spec_node_t *c = dalloc(cd, sizeof(spec_node_t));
      c->spec = n->spec;
      c->next = rev;
      rev = c;
   }

   for (spec_node_t *n = rev; n; n = n->next)
      add_spec_attr(cd, &net->attrs, &net->nattrs, n->spec);
}

static void unit_specs(conn_design_t *cd, conn_block_t *blk, tree_t owner)
{
   const int ndecls = tree_decls(owner);
   for (int i = 0; i < ndecls; i++) {
      tree_t d = tree_decl(owner, i);
      if (tree_kind(d) != T_ATTR_SPEC)
         continue;

      const class_t class = tree_class(d);
      if (class == C_ENTITY || class == C_ARCHITECTURE)
         add_spec_attr(cd, &blk->attrs, &blk->nattrs, d);
   }
}

void vhdl_block_attrs(conn_design_t *cd, conn_block_t *blk, tree_t block,
                      tree_t outer)
{
   if (outer != NULL)
      collect_specs(cd, outer);

   collect_specs(cd, block);

   if (outer != NULL)
      unit_specs(cd, blk, outer);

   unit_specs(cd, blk, block);
}
