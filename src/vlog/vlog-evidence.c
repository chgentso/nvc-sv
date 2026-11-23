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
#include "conn-priv.h"
#include "vlog/vlog-node.h"
#include "vlog/vlog-number.h"
#include "vlog/vlog-util.h"

#include <ctype.h>
#include <stdlib.h>

//
// Clocking evidence of Verilog processes and attributes of nets and
// modules, for --netlist --evidence.
//
// The walker follows the statements of an always block with a stack of
// guards: the reads of the conditions around an assignment are its
// control, the reads of the right-hand side its data.  Blocking
// assignments are tracked like VHDL variables: a later read of the target
// stands for the sources of the value.
//

typedef struct {
   range_list_t data;
   range_list_t ctrl;
   bool         full;       // Whole variable assigned on every path
} var_t;

typedef enum { COPY_NONE, COPY_ONE, COPY_BROKEN } copy_state_t;

typedef struct {
   range_list_t data;
   range_list_t ctrl;
   int          clock;
   copy_state_t copy;
   conn_range_t src;
   bool         assigned;
   unsigned     why;
} out_t;

typedef struct {
   conn_range_t range;
   vlog_node_t  decl;
   bool         whole;     // The target is a reference to the whole variable
   bool         is_net;
} target_t;

typedef A(target_t) target_list_t;

typedef struct {
   conn_design_t   *cd;
   conn_proc_t     *proc;
   hash_t          *covered;
   hash_t          *vars;
   hash_t          *seen_callee;
   range_list_t     guard;
   range_list_t     call;      // Reads of subroutines called
   range_list_t    *rd_data;   // Where the current expression delivers
   range_list_t    *rd_ctrl;
   bool             subst;
   bool             approx;
   bool             in_callee;  // Scanning the body of a subroutine
   unsigned         why;       // conn_ev_why_t
   bool             other;     // Bench-like: waits, forks, forever
   A(vlog_node_t)   callees;
   A(var_t *)       varlist;
   A(conn_ev_clock_t) clocks;
   A(vlog_node_t)   skip;      // Conditions that are not guards
   A(conn_read_ev_t) uses;     // How the reads are used
   hash_t          *ucov;     // Names inside selects counted as part of them
   unsigned         opctx;    // Use of the target of an operator assignment
   out_t           *outs;
   unsigned         nouts;
   int              cur_clock;
   unsigned         async_depth;
   unsigned         base_guard;
} wk_t;

static void add_list(range_list_t *dst, const range_list_t *src)
{
   for (unsigned i = 0; i < src->count; i++)
      add_range(dst, src->items[i].net, src->items[i].lo, src->items[i].hi);
}

static vlog_node_t base_decl(vlog_node_t n)
{
   for (;;) {
      switch (vlog_kind(n)) {
      case V_BIT_SELECT:
      case V_PART_SELECT:
      case V_MEMBER_REF:
         n = vlog_value(n);
         continue;
      case V_REF:
      case V_HIER_REF:
         return vlog_has_ref(n) ? vlog_ref(n) : NULL;
      default:
         return NULL;
      }
   }
}

static bool is_name(vlog_node_t n)
{
   switch (vlog_kind(n)) {
   case V_REF:
   case V_HIER_REF:
   case V_BIT_SELECT:
   case V_PART_SELECT:
   case V_MEMBER_REF:
      return true;
   default:
      return false;
   }
}

static bool is_local_decl(vlog_node_t d)
{
   if (d == NULL)
      return false;

   switch (vlog_kind(d)) {
   case V_VAR_DECL:
   case V_LOCAL_DECL:
   case V_TF_PORT_DECL:
      return true;
   default:
      return false;
   }
}

////////////////////////////////////////////////////////////////////////////////
// Uses of what is read

// The use of a name that is assigned to a variable to be read later: it
// is the use of the variable that counts
#define U_DEFER   0x8000

static void use_add(wk_t *w, const conn_range_t *r, unsigned uses)
{
   if (r->net == NULL)
      return;

   conn_read_ev_t u = { .sig = *r, .uses = uses };
   APUSH(w->uses, u);
}

// A name whose bits are used as CTX say
static void use_name(wk_t *w, vlog_node_t e, unsigned ctx)
{
   conn_range_t r;
   bool approx = false;
   vlog_node_t d = base_decl(e);
   const bool ok = vlog_select(w->cd, e, &r, &approx);

   var_t *var = (w->subst && d != NULL) ? hash_get(w->vars, d) : NULL;
   if (var != NULL) {
      // What the variable holds is used when it is read
      if (ctx != U_DEFER) {
         const unsigned k = ctx == CONN_USE_COPY ? CONN_USE_STORE : ctx;
         for (unsigned i = 0; i < var->data.count; i++) {
            const conn_range_t *v = &(var->data.items[i]);
            use_add(w, v, v->hi == v->lo ? CONN_USE_BIT : k);
         }
      }
      if (var->full || !ok)
         return;
   }

   if (!ok || ctx == U_DEFER)
      return;

   unsigned k = ctx;
   const bool whole = !approx && (vlog_kind(e) == V_REF
                                  || vlog_kind(e) == V_HIER_REF
                                  || vlog_kind(e) == V_MEMBER_REF);
   if (r.hi == r.lo && !approx)
      k = CONN_USE_BIT;
   else if (approx) {
      // A select that is not static is a multiplexer of the bits
      switch (ctx) {
      case CONN_USE_COPY:
      case CONN_USE_BIT:
      case CONN_USE_CONST:
         k = CONN_USE_LOGIC;
         break;
      default:
         break;
      }
   }
   else if (!whole && ctx == CONN_USE_COPY)
      k = CONN_USE_BIT;   // A slice is not the whole pin

   use_add(w, &r, k);
}

static void use_selects_cb(vlog_node_t v, void *ctx)
{
   wk_t *w = ctx;

   switch (vlog_kind(v)) {
   case V_BIT_SELECT:
   case V_PART_SELECT:
   case V_MEMBER_REF:
      {
         if (hash_get(w->ucov, v) != NULL)
            return;

         conn_range_t r;
         bool approx = false;
         if (!vlog_select(w->cd, v, &r, &approx))
            return;

         use_name(w, v, w->opctx);
         for (;;) {
            switch (vlog_kind(v)) {
            case V_BIT_SELECT:
            case V_PART_SELECT:
            case V_MEMBER_REF:
               v = vlog_value(v);
               hash_put(w->ucov, v, v);
               continue;
            default:
               return;
            }
         }
      }
      break;
   default:
      break;
   }
}

static void use_refs_cb(vlog_node_t v, void *ctx)
{
   wk_t *w = ctx;

   switch (vlog_kind(v)) {
   case V_REF:
   case V_HIER_REF:
      if (hash_get(w->ucov, v) == NULL)
         use_name(w, v, w->opctx);
      break;
   default:
      break;
   }
}

// Everything that E reads is used as CTX
static void wk_use_all(wk_t *w, vlog_node_t e, unsigned ctx)
{
   const unsigned saved = w->opctx;
   w->opctx = ctx;
   vlog_visit(e, use_selects_cb, w);
   vlog_visit(e, use_refs_cb, w);
   w->opctx = saved;
}

static bool is_const_expr(vlog_node_t e)
{
   int64_t c;
   return vlog_kind(e) == V_NUMBER || vlog_get_const(e, &c);
}

static void wk_uses(wk_t *w, vlog_node_t e, unsigned ctx);

// The indices of a select and of the selects it is made of
static void wk_use_indices(wk_t *w, vlog_node_t e)
{
   for (;;) {
      switch (vlog_kind(e)) {
      case V_BIT_SELECT:
         {
            const int n = vlog_params(e);
            for (int i = 0; i < n; i++)
               wk_uses(w, vlog_param(e, i), CONN_USE_INDEX);
         }
         e = vlog_value(e);
         continue;
      case V_PART_SELECT:
         if (vlog_has_left(e) && !is_const_expr(vlog_left(e)))
            wk_uses(w, vlog_left(e), CONN_USE_INDEX);
         if (vlog_has_right(e) && !is_const_expr(vlog_right(e)))
            wk_uses(w, vlog_right(e), CONN_USE_INDEX);
         e = vlog_value(e);
         continue;
      case V_MEMBER_REF:
         e = vlog_value(e);
         continue;
      default:
         return;
      }
   }
}

// The expression E delivers its value to something that uses it as CTX:
// CONN_USE_COPY if the whole value is assigned to a target of its width
static void wk_uses(wk_t *w, vlog_node_t e, unsigned ctx)
{
   switch (vlog_kind(e)) {
   case V_REF:
   case V_HIER_REF:
      use_name(w, e, ctx);
      return;

   case V_BIT_SELECT:
   case V_PART_SELECT:
   case V_MEMBER_REF:
      {
         conn_range_t r;
         bool approx = false;
         if (vlog_select(w->cd, e, &r, &approx)
             || is_local_decl(base_decl(e))) {
            use_name(w, e, ctx);
            wk_use_indices(w, e);
         }
         else
            wk_use_all(w, e, CONN_USE_CALL);
      }
      return;

   case V_NUMBER:
   case V_REAL:
   case V_STRING:
      return;

   case V_UNARY:
      switch ((vlog_unary_t)vlog_subkind(e)) {
      case V_UNARY_IDENTITY:
         wk_uses(w, vlog_value(e), ctx);
         break;
      case V_UNARY_BITNEG:
         wk_uses(w, vlog_value(e), CONN_USE_LOGIC);
         break;
      case V_UNARY_NOT:
      case V_UNARY_AND:
      case V_UNARY_OR:
      case V_UNARY_NAND:
      case V_UNARY_NOR:
         wk_uses(w, vlog_value(e), CONN_USE_CONST);
         break;
      default:
         wk_uses(w, vlog_value(e), CONN_USE_ARITH);
         break;
      }
      return;

   case V_BINARY:
      {
         vlog_node_t l = vlog_left(e), r = vlog_right(e);
         switch ((vlog_binary_t)vlog_subkind(e)) {
         case V_BINARY_OR:
         case V_BINARY_AND:
         case V_BINARY_XOR:
         case V_BINARY_XNOR:
         case V_BINARY_NAND:
         case V_BINARY_NOR:
            wk_uses(w, l, CONN_USE_LOGIC);
            wk_uses(w, r, CONN_USE_LOGIC);
            break;
         case V_BINARY_LOG_AND:
         case V_BINARY_LOG_OR:
            wk_uses(w, l, CONN_USE_CONST);
            wk_uses(w, r, CONN_USE_CONST);
            break;
         case V_BINARY_LT:
         case V_BINARY_GT:
         case V_BINARY_LEQ:
         case V_BINARY_GEQ:
            wk_uses(w, l, CONN_USE_COMPARE);
            wk_uses(w, r, CONN_USE_COMPARE);
            break;
         case V_BINARY_LOG_EQ:
         case V_BINARY_LOG_NEQ:
         case V_BINARY_CASE_EQ:
         case V_BINARY_CASE_NEQ:
         case V_BINARY_WILD_EQ:
         case V_BINARY_WILD_NEQ:
            if (is_const_expr(r)) {
               wk_uses(w, l, CONN_USE_CONST);
               wk_uses(w, r, CONN_USE_CONST);
            }
            else if (is_const_expr(l)) {
               wk_uses(w, l, CONN_USE_CONST);
               wk_uses(w, r, CONN_USE_CONST);
            }
            else {
               wk_uses(w, l, CONN_USE_COMPARE);
               wk_uses(w, r, CONN_USE_COMPARE);
            }
            break;
         case V_BINARY_SHIFT_LL:
         case V_BINARY_SHIFT_RL:
         case V_BINARY_SHIFT_LA:
         case V_BINARY_SHIFT_RA:
            // Moving the bits by a known amount keeps them apart
            wk_uses(w, l, is_const_expr(r) ? CONN_USE_LOGIC : CONN_USE_ARITH);
            wk_uses(w, r, CONN_USE_ARITH);
            break;
         default:
            wk_uses(w, l, CONN_USE_ARITH);
            wk_uses(w, r, CONN_USE_ARITH);
            break;
         }
      }
      return;

   case V_COND_EXPR:
      wk_uses(w, vlog_value(e), CONN_USE_CONST);
      wk_uses(w, vlog_left(e), ctx == CONN_USE_COPY ? CONN_USE_LOGIC : ctx);
      wk_uses(w, vlog_right(e), ctx == CONN_USE_COPY ? CONN_USE_LOGIC : ctx);
      return;

   case V_CONCAT:
      {
         const int n = vlog_params(e);
         if (n == 1 && !vlog_has_value(e)) {
            wk_uses(w, vlog_param(e, 0), ctx);
            return;
         }

         for (int i = 0; i < n; i++)
            wk_uses(w, vlog_param(e, i), CONN_USE_STORE);
         if (vlog_has_value(e))
            wk_uses(w, vlog_value(e), CONN_USE_ARITH);
      }
      return;

   case V_SYS_FCALL:
      {
         const int n = vlog_params(e);
         const char *name = vlog_has_ident(e) ? istr(vlog_ident(e)) : "";
         const bool conv = n == 1 && (strcmp(name, "$signed") == 0
                                      || strcmp(name, "$unsigned") == 0);
         for (int i = 0; i < n; i++)
            wk_uses(w, vlog_param(e, i), conv ? ctx : CONN_USE_CALL);
      }
      return;

   case V_USER_FCALL:
      {
         const int n = vlog_params(e);
         for (int i = 0; i < n; i++)
            wk_uses(w, vlog_param(e, i), CONN_USE_CALL);
      }
      return;

   default:
      wk_use_all(w, e, CONN_USE_CALL);
      return;
   }
}

////////////////////////////////////////////////////////////////////////////////
// Reads of expressions

static void wk_why(wk_t *w, unsigned why)
{
   w->approx = true;
   w->why |= why;
}

static void wk_handle_read(wk_t *w, vlog_node_t decl, const conn_range_t *r,
                           bool approx)
{
   var_t *var = (w->subst && decl != NULL) ? hash_get(w->vars, decl) : NULL;
   if (var != NULL) {
      add_list(w->rd_data, &var->data);
      add_list(w->rd_ctrl, &var->ctrl);
      if (!var->full && r != NULL)
         add_range(w->rd_data, r->net, r->lo, r->hi);
      return;
   }

   if (r != NULL) {
      add_range(w->rd_data, r->net, r->lo, r->hi);
      if (approx)
         wk_why(w, CONN_WHY_DYNAMIC);
   }
   else if (is_local_decl(decl) && !w->in_callee)
      wk_why(w, CONN_WHY_VARIABLE);   // Read before assigned: the previous value
}

static void wk_cover_chain(wk_t *w, vlog_node_t v)
{
   for (;;) {
      switch (vlog_kind(v)) {
      case V_BIT_SELECT:
      case V_PART_SELECT:
      case V_MEMBER_REF:
         v = vlog_value(v);
         hash_put(w->covered, v, v);
         continue;
      default:
         return;
      }
   }
}

static void wk_selects_cb(vlog_node_t v, void *ctx)
{
   wk_t *w = ctx;

   switch (vlog_kind(v)) {
   case V_BIT_SELECT:
   case V_PART_SELECT:
   case V_MEMBER_REF:
      {
         if (hash_get(w->covered, v) != NULL)
            return;

         conn_range_t r;
         bool approx = false;
         const bool ok = vlog_select(w->cd, v, &r, &approx);
         vlog_node_t d = base_decl(v);
         if (ok)
            wk_handle_read(w, d, &r, approx);
         else if (is_local_decl(d))
            wk_handle_read(w, d, NULL, false);
         else
            return;

         wk_cover_chain(w, v);
         hash_put(w->covered, v, v);
      }
      break;
   default:
      break;
   }
}

static void wk_refs_cb(vlog_node_t v, void *ctx)
{
   wk_t *w = ctx;

   switch (vlog_kind(v)) {
   case V_REF:
   case V_HIER_REF:
      {
         if (hash_get(w->covered, v) != NULL)
            return;

         conn_range_t r;
         bool approx = false;
         vlog_node_t d = vlog_has_ref(v) ? vlog_ref(v) : NULL;
         if (vlog_select(w->cd, v, &r, &approx))
            wk_handle_read(w, d, &r, approx);
         else if (is_local_decl(d))
            wk_handle_read(w, d, NULL, false);
         else if (vlog_kind(v) == V_HIER_REF && d == NULL)
            wk_why(w, CONN_WHY_OTHER);
      }
      break;

   case V_USER_FCALL:
   case V_USER_TCALL:
      wk_why(w, CONN_WHY_CALL);
      if (vlog_has_ref(v) && hash_get(w->seen_callee, vlog_ref(v)) == NULL) {
         hash_put(w->seen_callee, vlog_ref(v), vlog_ref(v));
         APUSH(w->callees, vlog_ref(v));
      }
      break;

   case V_METHOD_CALL:
   case V_CLASS_NEW:
   case V_DYNAMIC_NEW:
      wk_why(w, CONN_WHY_OTHER);
      break;

   default:
      break;
   }
}

static void wk_visit(wk_t *w, vlog_node_t e)
{
   vlog_visit(e, wk_selects_cb, w);
   vlog_visit(e, wk_refs_cb, w);
}

// Reads of E go to DATA, and those that stand for the control of a
// variable substituted to CTRL
static void wk_collect(wk_t *w, vlog_node_t e, range_list_t *data,
                       range_list_t *ctrl, bool subst, unsigned uctx)
{
   w->rd_data = data;
   w->rd_ctrl = ctrl;
   w->subst = subst;

   wk_visit(w, e);
   if (uctx != 0)
      wk_uses(w, e, uctx);

   // What a subroutine reads goes to every output of the process
   w->rd_data = &w->call;
   w->rd_ctrl = &w->call;
   w->subst = false;

   w->in_callee = true;
   while (w->callees.count > 0) {
      vlog_node_t callee = APOP(w->callees);
      switch (vlog_kind(callee)) {
      case V_FUNC_DECL:
      case V_TASK_DECL:
         wk_visit(w, callee);
         if (uctx != 0)
            wk_use_all(w, callee, CONN_USE_CALL);
         break;
      default:
         break;
      }
   }

   w->in_callee = false;
}

////////////////////////////////////////////////////////////////////////////////
// Tests of a signal

// Does the expression test one signal: the signal itself, its negation or
// a comparison with a constant.  NEG is set if true means a low level.
static bool simple_test(wk_t *w, vlog_node_t e, conn_range_t *r, bool *neg)
{
   switch (vlog_kind(e)) {
   case V_REF:
   case V_HIER_REF:
   case V_BIT_SELECT:
   case V_PART_SELECT:
   case V_MEMBER_REF:
      {
         bool approx = false;
         *neg = false;
         return vlog_select(w->cd, e, r, &approx);
      }

   case V_UNARY:
      switch ((vlog_unary_t)vlog_subkind(e)) {
      case V_UNARY_NOT:
      case V_UNARY_BITNEG:
         if (!simple_test(w, vlog_value(e), r, neg))
            return false;
         *neg = !*neg;
         return true;
      case V_UNARY_IDENTITY:
         return simple_test(w, vlog_value(e), r, neg);
      default:
         return false;
      }

   case V_BINARY:
      {
         const vlog_binary_t op = vlog_subkind(e);
         if (op != V_BINARY_LOG_EQ && op != V_BINARY_LOG_NEQ
             && op != V_BINARY_CASE_EQ && op != V_BINARY_CASE_NEQ)
            return false;

         vlog_node_t name = vlog_left(e), other = vlog_right(e);
         int64_t c;
         if (!vlog_get_const(other, &c)) {
            name = vlog_right(e);
            other = vlog_left(e);
            if (!vlog_get_const(other, &c))
               return false;
         }

         if (!simple_test(w, name, r, neg))
            return false;

         const bool is_eq = op == V_BINARY_LOG_EQ || op == V_BINARY_CASE_EQ;
         *neg = is_eq ? (c == 0) : (c != 0);
         return true;
      }

   default:
      return false;
   }
}

static bool ranges_overlap(const conn_range_t *a, const conn_range_t *b)
{
   return a->net == b->net && a->lo <= b->hi && b->lo <= a->hi;
}

// Does the expression read any bit of R
// Collect the reads of an expression that is walked again later
static void wk_probe(wk_t *w, vlog_node_t e, range_list_t *data,
                     range_list_t *ctrl)
{
   hash_t *saved = w->covered;
   w->covered = hash_new(16);
   wk_collect(w, e, data, ctrl, false, 0);
   hash_free(w->covered);
   w->covered = saved;
}

static bool reads_range(wk_t *w, vlog_node_t e, const conn_range_t *r)
{
   SCOPED_LIST(range_list_t, data);
   SCOPED_LIST(range_list_t, ctrl);

   const bool saved_approx = w->approx;
   const unsigned saved_why = w->why;
   const unsigned ncall = w->call.count;
   wk_probe(w, e, &data, &ctrl);
   w->approx = saved_approx;
   w->why = saved_why;
   w->call.count = ncall;

   for (unsigned i = 0; i < data.count; i++) {
      if (ranges_overlap(&(data.items[i]), r))
         return true;
   }

   return false;
}

static bool is_skipped(wk_t *w, vlog_node_t c)
{
   for (unsigned i = 0; i < w->skip.count; i++) {
      if (w->skip.items[i] == c)
         return true;
   }

   return false;
}

////////////////////////////////////////////////////////////////////////////////
// Assignments

static int find_drive(const conn_proc_t *p, const conn_range_t *r)
{
   for (unsigned i = 0; i < p->ndrives; i++) {
      const conn_range_t *d = &(p->drives[i]);
      if (d->net == r->net && d->lo <= r->lo && d->hi >= r->hi)
         return i;
   }

   return -1;
}

static void wk_targets(wk_t *w, vlog_node_t t, target_list_t *list)
{
   switch (vlog_kind(t)) {
   case V_CONCAT:
      {
         const int n = vlog_params(t);
         for (int i = 0; i < n; i++)
            wk_targets(w, vlog_param(t, i), list);
      }
      break;

   case V_REF:
   case V_HIER_REF:
   case V_BIT_SELECT:
   case V_PART_SELECT:
   case V_MEMBER_REF:
      {
         target_t tg = { .decl = base_decl(t), .whole = is_name(t) };
         tg.whole = vlog_kind(t) == V_REF || vlog_kind(t) == V_HIER_REF;
         bool approx = false;
         tg.is_net = vlog_select(w->cd, t, &tg.range, &approx);
         if (tg.is_net || is_local_decl(tg.decl))
            APUSH(*list, tg);
         if (approx)
            wk_why(w, CONN_WHY_DYNAMIC);
      }
      break;

   default:
      wk_why(w, CONN_WHY_OTHER);
      break;
   }
}

// The reads that select a part of a target are control
static void wk_target_index(wk_t *w, vlog_node_t t, range_list_t *ctrl)
{
   switch (vlog_kind(t)) {
   case V_CONCAT:
      {
         const int n = vlog_params(t);
         for (int i = 0; i < n; i++)
            wk_target_index(w, vlog_param(t, i), ctrl);
      }
      break;

   case V_BIT_SELECT:
      {
         const int n = vlog_params(t);
         for (int i = 0; i < n; i++)
            wk_collect(w, vlog_param(t, i), ctrl, ctrl, true, CONN_USE_INDEX);
         wk_target_index(w, vlog_value(t), ctrl);
      }
      break;

   case V_PART_SELECT:
      {
         int64_t c;
         if (vlog_has_left(t) && !vlog_get_const(vlog_left(t), &c))
            wk_collect(w, vlog_left(t), ctrl, ctrl, true, CONN_USE_INDEX);
         if (vlog_has_right(t) && !vlog_get_const(vlog_right(t), &c))
            wk_collect(w, vlog_right(t), ctrl, ctrl, true, CONN_USE_INDEX);
         wk_target_index(w, vlog_value(t), ctrl);
      }
      break;

   case V_MEMBER_REF:
      wk_target_index(w, vlog_value(t), ctrl);
      break;

   default:
      break;
   }
}

static var_t *wk_var(wk_t *w, vlog_node_t decl)
{
   var_t *var = hash_get(w->vars, decl);
   if (var == NULL) {
      var = xcalloc(sizeof(var_t));
      hash_put(w->vars, decl, var);
      APUSH(w->varlist, var);
   }
   return var;
}

// Assignment of VALUE (NULL if the target is its own value) to TARGET
static void wk_assign(wk_t *w, vlog_node_t target, vlog_node_t value,
                      bool blocking, bool own_value)
{
   SCOPED_LIST(range_list_t, data);
   SCOPED_LIST(range_list_t, ctrl);
   target_list_t tgts = AINIT;

   add_list(&ctrl, &w->guard);
   wk_target_index(w, target, &ctrl);
   wk_targets(w, target, &tgts);

   // The value is the whole of one net: a copy
   bool copy = false;
   conn_range_t src = { 0 };
   if (value != NULL && tgts.count == 1 && tgts.items[0].is_net
       && is_name(value) && !own_value) {
      bool approx = false, tapprox = false;
      conn_range_t tr;
      vlog_node_t d = base_decl(value);
      if (d != NULL && hash_get(w->vars, d) == NULL
          && vlog_select(w->cd, value, &src, &approx) && !approx
          && vlog_select(w->cd, target, &tr, &tapprox) && !tapprox
          && tr.hi - tr.lo == src.hi - src.lo)
         copy = true;
   }

   // How the value is used: a whole copy of a name, the value of a name that
   // is only kept in a variable until it is read, or a word stored
   // not bit for bit
   unsigned vctx = CONN_USE_COPY;
   if (value != NULL && is_name(value)) {
      if (copy)
         vctx = CONN_USE_COPY;
      else if (tgts.count == 1 && !tgts.items[0].is_net)
         vctx = U_DEFER;
      else
         vctx = CONN_USE_STORE;
   }

   const unsigned optx = w->opctx;
   w->opctx = 0;
   if (value != NULL)
      wk_collect(w, value, &data, &ctrl, true, own_value ? optx : vctx);
   if (own_value)
      wk_collect(w, target, &data, &ctrl, true, optx);

   for (unsigned i = 0; i < tgts.count; i++) {
      const target_t *tg = &(tgts.items[i]);

      if (tg->is_net) {
         const int idx = find_drive(w->proc, &tg->range);
         if (idx >= 0) {
            out_t *o = &(w->outs[idx]);
            add_list(&o->data, &data);
            add_list(&o->ctrl, &ctrl);

            if (!o->assigned || w->async_depth == 0)
               o->clock = w->cur_clock;
            o->assigned = true;
            o->why |= w->why;

            if (w->async_depth == 0) {
               if (!copy)
                  o->copy = COPY_BROKEN;
               else if (o->copy == COPY_NONE) {
                  o->copy = COPY_ONE;
                  o->src = src;
               }
               else if (o->copy == COPY_ONE
                        && (o->src.net != src.net || o->src.lo != src.lo
                            || o->src.hi != src.hi))
                  o->copy = COPY_BROKEN;
            }
         }
      }

      if (blocking && tg->decl != NULL) {
         var_t *var = wk_var(w, tg->decl);
         const bool whole = tg->whole && w->guard.count == w->base_guard
            && w->async_depth == 0;

         if (whole) {
            var->data.count = var->ctrl.count = 0;
            var->full = true;
         }
         else if (var->data.count == 0 && var->ctrl.count == 0)
            var->full = false;

         add_list(&var->data, &data);
         add_list(&var->ctrl, &ctrl);
      }
   }

   ACLEAR(tgts);
}

////////////////////////////////////////////////////////////////////////////////
// Statements

static void wk_stmt(wk_t *w, vlog_node_t s)
{
   switch (vlog_kind(s)) {
   case V_SEQ_BLOCK:
      {
         const int n = vlog_stmts(s);
         for (int i = 0; i < n; i++)
            wk_stmt(w, vlog_stmt(s, i));
      }
      break;

   case V_NBASSIGN:
      wk_assign(w, vlog_target(s), vlog_value(s), false, false);
      break;

   case V_BASSIGN:
      wk_assign(w, vlog_target(s), vlog_value(s), true, false);
      break;

   case V_ASSIGN:
      wk_assign(w, vlog_target(s), vlog_value(s), false, false);
      break;

   case V_OP_ASSIGN:
      switch ((vlog_assign_t)vlog_subkind(s)) {
      case V_ASSIGN_AND:
      case V_ASSIGN_OR:
      case V_ASSIGN_XOR:
         w->opctx = CONN_USE_LOGIC;
         break;
      default:
         w->opctx = CONN_USE_ARITH;
         break;
      }
      wk_assign(w, vlog_target(s), vlog_value(s), true, true);
      break;

   case V_PREFIX:
   case V_POSTFIX:
      w->opctx = CONN_USE_ARITH;
      wk_assign(w, vlog_target(s), NULL, true, true);
      break;

   case V_IF:
      {
         const unsigned mark = w->guard.count;
         const int n = vlog_conds(s);
         for (int i = 0; i < n; i++) {
            vlog_node_t c = vlog_cond(s, i);
            const bool skip = is_skipped(w, c);
            const int saved_clock = w->cur_clock;

            if (vlog_has_value(c) && !skip) {
               conn_range_t r;
               bool neg;
               int clk = -1;
               if (simple_test(w, vlog_value(c), &r, &neg)) {
                  for (unsigned k = 0; k < w->clocks.count; k++) {
                     if (ranges_overlap(&r, &(w->clocks.items[k].sig)))
                        clk = k;
                  }
               }

               if (clk >= 0)
                  w->cur_clock = clk;
               else
                  wk_collect(w, vlog_value(c), &w->guard, &w->guard, true,
                              CONN_USE_CONST);
            }

            if (skip)
               w->async_depth++;

            const int ns = vlog_stmts(c);
            for (int j = 0; j < ns; j++)
               wk_stmt(w, vlog_stmt(c, j));

            if (skip)
               w->async_depth--;

            w->cur_clock = saved_clock;
         }
         w->guard.count = mark;
      }
      break;

   case V_CASE:
      {
         const unsigned mark = w->guard.count;

         // The selector is compared with the items: with constants it is
         // only tested for equality
         bool all_const = true;
         const int n = vlog_stmts(s);
         for (int i = 0; i < n; i++) {
            vlog_node_t item = vlog_stmt(s, i);
            const int np = vlog_params(item);
            for (int j = 0; j < np; j++)
               all_const &= is_const_expr(vlog_param(item, j));
         }

         wk_collect(w, vlog_value(s), &w->guard, &w->guard, true,
                    all_const ? CONN_USE_CONST : CONN_USE_COMPARE);

         for (int i = 0; i < n; i++) {
            vlog_node_t item = vlog_stmt(s, i);
            const unsigned mark2 = w->guard.count;

            const int np = vlog_params(item);
            for (int j = 0; j < np; j++)
               wk_collect(w, vlog_param(item, j), &w->guard, &w->guard, true,
                           all_const ? 0 : CONN_USE_COMPARE);

            const int ns = vlog_stmts(item);
            for (int j = 0; j < ns; j++)
               wk_stmt(w, vlog_stmt(item, j));

            w->guard.count = mark2;
         }
         w->guard.count = mark;
      }
      break;

   case V_FOREACH:
      {
         const unsigned mark = w->guard.count;
         wk_collect(w, vlog_value(s), &w->guard, &w->guard, true,
                 CONN_USE_CONST);
         if (w->guard.count > mark)
            wk_why(w, CONN_WHY_LOOP);

         const int n = vlog_stmts(s);
         for (int i = 0; i < n; i++)
            wk_stmt(w, vlog_stmt(s, i));

         w->guard.count = mark;
      }
      break;

   case V_FOR_LOOP:
      {
         const unsigned mark = w->guard.count;

         if (vlog_has_left(s))
            wk_stmt(w, vlog_left(s));
         if (vlog_has_value(s))
            wk_collect(w, vlog_value(s), &w->guard, &w->guard, true,
                 CONN_USE_CONST);

         // A loop whose bounds depend on signals is not unrolled
         if (w->guard.count > mark)
            wk_why(w, CONN_WHY_LOOP);

         const int n = vlog_stmts(s);
         for (int i = 0; i < n; i++)
            wk_stmt(w, vlog_stmt(s, i));

         if (vlog_has_right(s))
            wk_stmt(w, vlog_right(s));

         w->guard.count = mark;
      }
      break;

   case V_FOR_INIT:
   case V_FOR_STEP:
      {
         const int n = vlog_stmts(s);
         for (int i = 0; i < n; i++)
            wk_stmt(w, vlog_stmt(s, i));
      }
      break;

   case V_WHILE:
   case V_DO_WHILE:
   case V_REPEAT:
      {
         const unsigned mark = w->guard.count;
         wk_collect(w, vlog_value(s), &w->guard, &w->guard, true,
                 CONN_USE_CONST);
         if (w->guard.count > mark)
            wk_why(w, CONN_WHY_LOOP);

         const int n = vlog_stmts(s);
         for (int i = 0; i < n; i++)
            wk_stmt(w, vlog_stmt(s, i));

         w->guard.count = mark;
      }
      break;

   case V_USER_TCALL:
   case V_USER_FCALL:
   case V_VOID_CALL:
      wk_why(w, CONN_WHY_CALL);
      wk_collect(w, s, &w->call, &w->call, false, CONN_USE_CALL);
      break;

   case V_TIMING:
   case V_WAIT:
   case V_FORK:
   case V_FOREVER:
   case V_EVENT_TRIGGER:
      w->other = true;
      break;

   case V_ASSERT:
   case V_ASSUME:
   case V_COVER:
      // Checks do not take part in what the process computes, the bits they
      // look at are only observed
      if (vlog_has_value(s)) {
         vlog_node_t c = vlog_value(s);
         if (vlog_kind(c) == V_PROP_SPEC)
            wk_use_all(w, c, CONN_USE_LOGIC);
         else
            wk_uses(w, c, CONN_USE_CONST);
      }
      break;

   case V_EMPTY:
   case V_SYS_TCALL:
   case V_DISABLE:
   case V_RETURN:
   case V_LOOP_JUMP:
      break;

   default:
      // Not understood: its reads are possible sources of every output
      wk_why(w, CONN_WHY_OTHER);
      wk_collect(w, s, &w->call, &w->call, false, CONN_USE_CALL);
      break;
   }
}


////////////////////////////////////////////////////////////////////////////////
// Processes

static void wk_init(wk_t *w, conn_design_t *cd, conn_proc_t *proc)
{
   memset(w, '\0', sizeof(*w));
   w->cd = cd;
   w->proc = proc;
   w->covered = hash_new(64);
   w->vars = hash_new(16);
   w->seen_callee = hash_new(16);
   w->ucov = hash_new(16);
   w->nouts = proc->ndrives;
   w->outs = xcalloc_array(MAX(proc->ndrives, 1), sizeof(out_t));
}

static void wk_free(wk_t *w)
{
   hash_free(w->covered);
   hash_free(w->vars);
   hash_free(w->seen_callee);
   hash_free(w->ucov);

   for (unsigned i = 0; i < w->varlist.count; i++) {
      ACLEAR(w->varlist.items[i]->data);
      ACLEAR(w->varlist.items[i]->ctrl);
      free(w->varlist.items[i]);
   }
   ACLEAR(w->varlist);

   for (unsigned i = 0; i < w->nouts; i++) {
      ACLEAR(w->outs[i].data);
      ACLEAR(w->outs[i].ctrl);
   }
   free(w->outs);

   ACLEAR(w->guard);
   ACLEAR(w->call);
   ACLEAR(w->callees);
   ACLEAR(w->clocks);
   ACLEAR(w->skip);
   ACLEAR(w->uses);
}

static conn_ev_class_t out_class(conn_ev_class_t cls)
{
   return cls == CONN_EV_SEQ || cls == CONN_EV_LATCH ? cls : CONN_EV_COMB;
}

// Fill the outputs of the evidence from what was learnt walking the body
static void wk_outputs(wk_t *w, conn_evidence_t *ev)
{
   conn_out_ev_t *outs = dalloc(w->cd, MAX(w->nouts, 1) * sizeof(*outs));

   for (unsigned i = 0; i < w->nouts; i++) {
      out_t *o = &(w->outs[i]);

      add_list(&o->data, &w->call);
      merge_ranges(&o->data);
      merge_ranges(&o->ctrl);

      outs[i].drive = i;
      outs[i].reasons = o->why | (w->call.count > 0 ? CONN_WHY_CALL : 0);
      outs[i].cls = out_class(ev->cls);
      outs[i].clock = ev->cls == CONN_EV_SEQ ? MAX(o->clock, 0) : -1;
      outs[i].data = DDUP(w->cd, o->data, conn_range_t);
      outs[i].ndata = o->data.count;
      outs[i].ctrl = DDUP(w->cd, o->ctrl, conn_range_t);
      outs[i].nctrl = o->ctrl.count;

      if (o->copy == COPY_ONE && w->call.count == 0) {
         outs[i].has_copy = true;
         outs[i].copy = o->src;
      }
   }

   ev->outs = outs;
   ev->nouts = w->nouts;
}

static void wk_class_other(conn_evidence_t *ev)
{
   ev->cls = CONN_EV_OTHER;
   ev->nclocks = ev->nasync = ev->nsync_reset = ev->nouts = 0;
}

// Does the statement assign only constants to its targets
static bool assigns_constants(wk_t *w, vlog_node_t s, bool *zero, bool *ones,
                              bool *any)
{
   switch (vlog_kind(s)) {
   case V_SEQ_BLOCK:
      {
         const int n = vlog_stmts(s);
         for (int i = 0; i < n; i++) {
            if (!assigns_constants(w, vlog_stmt(s, i), zero, ones, any))
               return false;
         }
         return true;
      }

   case V_NBASSIGN:
   case V_BASSIGN:
      {
         vlog_node_t value = vlog_value(s);

         SCOPED_LIST(range_list_t, data);
         SCOPED_LIST(range_list_t, ctrl);
         const bool saved_approx = w->approx;
         const unsigned saved_why = w->why;
         const unsigned ncallees = w->call.count;
         wk_probe(w, value, &data, &ctrl);
         const bool pure = !w->approx;
         w->approx = saved_approx;
         w->why = saved_why;
         w->call.count = ncallees;

         if (data.count > 0 || !pure)
            return false;

         int64_t c;
         *any = true;
         if (vlog_kind(value) == V_NUMBER && number_is_defined(vlog_number(value))) {
            number_t n = vlog_number(value);
            bool z = true, o = true;
            for (unsigned b = 0; b < number_width(n); b++) {
               const vlog_logic_t bit = number_bit(n, b);
               z &= bit == LOGIC_0;
               o &= bit == LOGIC_1;
            }
            *zero &= z;
            *ones &= o;
         }
         else if (vlog_get_const(value, &c)) {
            *zero &= c == 0;
            *ones &= c == -1;
         }
         else
            *zero = *ones = false;
         return true;
      }

   case V_EMPTY:
      return true;

   default:
      return false;
   }
}

static vlog_node_t first_stmt(vlog_node_t s)
{
   while (s != NULL && vlog_kind(s) == V_SEQ_BLOCK) {
      if (vlog_stmts(s) == 0)
         return NULL;
      s = vlog_stmt(s, 0);
   }

   return s;
}

static bool analyse_edges(wk_t *w, conn_evidence_t *ev,
                          vlog_node_t body_first, vlog_node_t *events,
                          unsigned nedges)
{
   // An edge in the list which the first if of the body tests, with a branch
   // that assigns only constants, is an asynchronous control

   bool *is_async = xcalloc_array(MAX(nedges, 1), sizeof(bool));
   A(conn_ev_async_t) asyncs = AINIT;

   conn_range_t *ranges = xcalloc_array(MAX(nedges, 1), sizeof(conn_range_t));
   for (unsigned i = 0; i < nedges; i++) {
      bool approx = false;
      if (!vlog_select(w->cd, vlog_value(events[i]), &ranges[i], &approx)) {
         // A clock which is not a net of the design
         free(is_async);
         free(ranges);
         ACLEAR(asyncs);
         w->other = true;
         return false;
      }
   }

   if (nedges > 1 && body_first != NULL && vlog_kind(body_first) == V_IF) {
      unsigned remaining = nedges;

      const int nconds = vlog_conds(body_first);
      for (int k = 0; k < nconds; k++) {
         vlog_node_t c = vlog_cond(body_first, k);
         if (!vlog_has_value(c))
            break;

         int ev_idx = -1;
         for (unsigned i = 0; i < nedges && ev_idx < 0; i++) {
            if (!is_async[i] && reads_range(w, vlog_value(c), &ranges[i]))
               ev_idx = i;
         }

         if (ev_idx < 0 || remaining <= 1)
            break;

         bool zero = true, ones = true, any = false;
         bool consts = true;
         const int ns = vlog_stmts(c);
         for (int j = 0; j < ns && consts; j++)
            consts = assigns_constants(w, vlog_stmt(c, j), &zero, &ones, &any);

         if (!consts || !any)
            break;

         conn_range_t r;
         bool neg;
         conn_level_t level;
         const v_event_kind_t edge = vlog_subkind(events[ev_idx]);
         if (simple_test(w, vlog_value(c), &r, &neg))
            level = neg ? CONN_LEVEL_LOW : CONN_LEVEL_HIGH;
         else if (edge == V_EVENT_NEGEDGE)
            level = CONN_LEVEL_LOW;
         else if (edge == V_EVENT_POSEDGE)
            level = CONN_LEVEL_HIGH;
         else
            level = CONN_LEVEL_UNKNOWN;

         conn_ev_async_t a = {
            .sig   = ranges[ev_idx],
            .level = level,
            .role  = zero ? CONN_ROLE_RESET
                          : (ones ? CONN_ROLE_SET : CONN_ROLE_UNKNOWN),
         };
         APUSH(asyncs, a);
         APUSH(w->skip, c);
         is_async[ev_idx] = true;
         remaining--;
      }
   }

   // The edges that are not asynchronous controls are clocks
   for (unsigned i = 0; i < nedges; i++) {
      if (is_async[i])
         continue;

      conn_ev_clock_t clk = { .sig = ranges[i] };
      switch ((v_event_kind_t)vlog_subkind(events[i])) {
      case V_EVENT_POSEDGE: clk.edge = CONN_EDGE_RISE; break;
      case V_EVENT_NEGEDGE: clk.edge = CONN_EDGE_FALL; break;
      default: clk.edge = CONN_EDGE_BOTH; break;
      }
      APUSH(w->clocks, clk);
   }

   ev->nasync = asyncs.count;
   ev->async = DDUP(w->cd, asyncs, conn_ev_async_t);

   free(is_async);
   free(ranges);
   ACLEAR(asyncs);
   return true;
}

static void find_sync_reset(wk_t *w, conn_evidence_t *ev, vlog_node_t first)
{
   // The first if of the clocked region whose branch assigns only
   // constants and tests one signal is a synchronous reset

   if (first == NULL || vlog_kind(first) != V_IF)
      return;

   // Skip the asynchronous branches
   int n = vlog_conds(first);
   int k = 0;
   vlog_node_t region = NULL;
   while (k < n && is_skipped(w, vlog_cond(first, k)))
      k++;

   if (k > 0) {
      if (k >= n)
         return;
      vlog_node_t c = vlog_cond(first, k);
      if (vlog_stmts(c) == 0)
         return;
      region = first_stmt(vlog_stmt(c, 0));
   }
   else
      region = first;

   if (region == NULL || vlog_kind(region) != V_IF)
      return;

   vlog_node_t c0 = vlog_cond(region, 0);
   if (!vlog_has_value(c0))
      return;

   conn_range_t r;
   bool neg;
   if (!simple_test(w, vlog_value(c0), &r, &neg))
      return;

   for (unsigned i = 0; i < w->clocks.count; i++) {
      if (ranges_overlap(&r, &(w->clocks.items[i].sig)))
         return;
   }

   bool zero = true, ones = true, any = false;
   bool consts = true;
   const int ns = vlog_stmts(c0);
   for (int j = 0; j < ns && consts; j++)
      consts = assigns_constants(w, vlog_stmt(c0, j), &zero, &ones, &any);

   if (!consts || !any)
      return;

   conn_range_t *list = dalloc(w->cd, sizeof(conn_range_t));
   list[0] = r;
   ev->sync_reset = list;
   ev->nsync_reset = 1;
}

static void evidence_always(wk_t *w, conn_evidence_t *ev, vlog_node_t v)
{
   const vlog_always_kind_t kind = vlog_subkind(v);

   if (vlog_stmts(v) == 0) {
      wk_class_other(ev);
      return;
   }

   if (kind == V_ALWAYS_COMB_DONE || kind == V_ALWAYS_LATCH_DONE
       || kind == V_ALWAYS_COMB || kind == V_ALWAYS_LATCH) {
      // The statement of the block with its sensitivity list made explicit
      vlog_node_t b = vlog_stmt(v, 0);
      if (vlog_kind(b) == V_SEQ_BLOCK && vlog_stmts(b) > 0) {
         const int n = vlog_stmts(b);
         for (int i = 0; i < n; i++) {
            vlog_node_t s = vlog_stmt(b, i);
            if (i == n - 1 && vlog_kind(s) == V_TIMING
                && vlog_stmts(s) == 0)
               break;   // The trailing wait
            wk_stmt(w, s);
         }
      }
      else
         wk_stmt(w, b);

      ev->cls = (kind == V_ALWAYS_LATCH_DONE || kind == V_ALWAYS_LATCH)
         ? CONN_EV_LATCH : CONN_EV_COMB;
      return;
   }

   // The first statement is the event control, maybe inside a block
   vlog_node_t top = vlog_stmt(v, 0), timing = top, blk = NULL;

   if (vlog_stmts(v) == 1 && vlog_kind(top) == V_SEQ_BLOCK
       && vlog_stmts(top) > 0) {
      blk = top;
      timing = vlog_stmt(blk, 0);
   }

   if (vlog_kind(timing) != V_TIMING) {
      wk_class_other(ev);
      return;
   }

   vlog_node_t ctrl = vlog_value(timing);
   if (vlog_kind(ctrl) != V_EVENT_CONTROL || vlog_params(ctrl) == 0) {
      wk_class_other(ev);
      return;
   }

   const int nparams = vlog_params(ctrl);
   A(vlog_node_t) edges = AINIT;
   bool all_events = true;
   bool has_level = false;
   for (int i = 0; i < nparams; i++) {
      vlog_node_t e = vlog_param(ctrl, i);
      if (vlog_kind(e) != V_EVENT) {
         all_events = false;
         break;
      }

      if (vlog_subkind(e) == V_EVENT_LEVEL)
         has_level = true;
      else
         APUSH(edges, e);
   }

   if (!all_events) {
      ACLEAR(edges);
      wk_class_other(ev);
      return;
   }

   // The statements that follow the event control
   A(vlog_node_t) body = AINIT;
   for (int i = 0; i < vlog_stmts(timing); i++)
      APUSH(body, vlog_stmt(timing, i));
   if (blk != NULL) {
      for (int i = 1; i < vlog_stmts(blk); i++)
         APUSH(body, vlog_stmt(blk, i));
   }

   if (edges.count == 0) {
      // A sensitivity list of levels: combinational
      for (unsigned i = 0; i < body.count; i++)
         wk_stmt(w, body.items[i]);
      ev->cls = CONN_EV_COMB;
      ACLEAR(edges);
      ACLEAR(body);
      return;
   }

   if (has_level)
      wk_why(w, CONN_WHY_OTHER);

   // The first statement tells the asynchronous controls
   vlog_node_t first = body.count > 0 ? first_stmt(body.items[0]) : NULL;
   const bool ok = analyse_edges(w, ev, first, edges.items, edges.count);
   if (!ok) {
      ACLEAR(edges);
      ACLEAR(body);
      return;
   }

   // iff conditions of the events enable the clock
   for (int i = 0; i < nparams; i++) {
      vlog_node_t e = vlog_param(ctrl, i);
      if (vlog_has_left(e))
         wk_collect(w, vlog_left(e), &w->guard, &w->guard, false,
                    CONN_USE_CONST);
   }
   w->base_guard = w->guard.count;

   find_sync_reset(w, ev, first);

   for (unsigned i = 0; i < body.count; i++)
      wk_stmt(w, body.items[i]);

   ev->cls = CONN_EV_SEQ;

   ACLEAR(edges);
   ACLEAR(body);
}

static void evidence_assign(wk_t *w, conn_evidence_t *ev, vlog_node_t v)
{
   wk_assign(w, vlog_target(v), vlog_value(v), false, false);
   ev->cls = CONN_EV_COMB;
}

static void evidence_gate(wk_t *w, conn_evidence_t *ev, vlog_node_t v)
{
   SCOPED_LIST(range_list_t, data);
   SCOPED_LIST(range_list_t, ctrl);
   target_list_t tgts = AINIT;

   const int np = vlog_params(v);
   for (int i = 0; i < np; i++)
      wk_collect(w, vlog_param(v, i), &data, &ctrl, false,
                 vlog_subkind(v) == V_GATE_BUF && np == 1
                 ? CONN_USE_COPY : CONN_USE_LOGIC);

   if (vlog_has_target(v))
      wk_targets(w, vlog_target(v), &tgts);

   bool copy = false;
   conn_range_t src = { 0 };
   if (vlog_subkind(v) == V_GATE_BUF && np == 1 && tgts.count == 1
       && tgts.items[0].is_net && is_name(vlog_param(v, 0))) {
      bool approx = false;
      if (vlog_select(w->cd, vlog_param(v, 0), &src, &approx) && !approx
          && src.hi - src.lo == tgts.items[0].range.hi - tgts.items[0].range.lo)
         copy = true;
   }

   for (unsigned i = 0; i < tgts.count; i++) {
      if (!tgts.items[i].is_net)
         continue;

      const int idx = find_drive(w->proc, &tgts.items[i].range);
      if (idx < 0)
         continue;

      out_t *o = &(w->outs[idx]);
      add_list(&o->data, &data);
      o->assigned = true;
      o->why |= w->why;
      if (copy) {
         o->copy = COPY_ONE;
         o->src = src;
      }
      else
         o->copy = COPY_BROKEN;
   }

   ACLEAR(tgts);
   ev->cls = CONN_EV_COMB;
}

void vlog_proc_evidence(conn_design_t *cd, conn_proc_t *proc, vlog_node_t v)
{
   conn_evidence_t *ev = dalloc(cd, sizeof(conn_evidence_t));
   ev->cls = CONN_EV_OTHER;
   proc->ev = ev;

   switch (vlog_kind(v)) {
   case V_ALWAYS:
   case V_ASSIGN:
   case V_GATE_INST:
      break;
   default:
      // initial, final, UDP tables
      ev->approx = proc->approx;
      ev->reasons = proc->approx ? CONN_WHY_OTHER : 0;
      return;
   }

   wk_t w;
   wk_init(&w, cd, proc);

   switch (vlog_kind(v)) {
   case V_ALWAYS:
      evidence_always(&w, ev, v);
      break;
   case V_ASSIGN:
      evidence_assign(&w, ev, v);
      break;
   case V_GATE_INST:
      evidence_gate(&w, ev, v);
      break;
   default:
      break;
   }

   if (w.other)
      wk_class_other(ev);

   ev->reads = DDUP(cd, w.uses, conn_read_ev_t);
   ev->nreads = w.uses.count;
   ev->has_reads = true;

   // What the connectivity scan could not resolve is approximate too
   if (proc->approx && w.why == 0)
      w.why = CONN_WHY_OTHER;

   ev->approx = w.approx || proc->approx;
   ev->reasons = ev->approx ? (w.why ?: CONN_WHY_OTHER) : 0;

   if (ev->cls != CONN_EV_OTHER) {
      if (w.clocks.count > 0) {
         ev->nclocks = w.clocks.count;
         ev->clocks = DDUP(cd, w.clocks, conn_ev_clock_t);
      }

      wk_outputs(&w, ev);
   }

   wk_free(&w);
}

////////////////////////////////////////////////////////////////////////////////
// Attributes

static char *lower_name(const char *s)
{
   char *r = xstrdup(s);
   for (char *p = r; *p; p++)
      *p = tolower((unsigned char)*p);
   return r;
}

static void add_pragmas(conn_design_t *cd, conn_attr_t **list,
                        unsigned *count, vlog_node_t decl)
{
   const int n = vlog_pragmas(decl);
   for (int i = 0; i < n; i++) {
      vlog_node_t a = vlog_pragma(decl, i);
      if (!vlog_has_ident(a))
         continue;

      char *name LOCAL = lower_name(istr(vlog_ident(a)));
      const char *value = vlog_has_ident2(a) ? istr(vlog_ident2(a)) : "1";
      conn_add_attr(cd, list, count, name, value);
   }
}

void vlog_net_attrs(conn_design_t *cd, conn_net_t *net, vlog_node_t decl)
{
   switch (vlog_kind(decl)) {
   case V_NET_DECL:
   case V_VAR_DECL:
   case V_PORT_DECL:
      add_pragmas(cd, &net->attrs, &net->nattrs, decl);
      break;
   default:
      break;
   }
}

void vlog_block_attrs(conn_design_t *cd, conn_block_t *blk, vlog_node_t body)
{
   switch (vlog_kind(body)) {
   case V_MODULE:
   case V_INST_BODY:
      add_pragmas(cd, &blk->attrs, &blk->nattrs, body);
      break;
   default:
      break;
   }

   // A Verilog top level has no instance to take the location from
   if (blk->src == NULL && !loc_invalid_p(vlog_loc(body))) {
      const char *f = loc_file_str(vlog_loc(body));
      if (f != NULL) {
         blk->src = dstr(cd, f);
         blk->line = vlog_loc(body)->first_line;
      }
   }
}
