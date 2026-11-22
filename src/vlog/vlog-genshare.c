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
#include "hash.h"
#include "ident.h"
#include "vlog/vlog-node.h"
#include "vlog/vlog-phase.h"
#include "vlog/vlog-util.h"

#include <assert.h>
#include <string.h>

//
// Decides whether the body of a for-generate loop can be lowered once for
// all iterations with the genvar as a variable of each iteration's scope.
// The genvar and every local parameter computed from it (the derived set)
// may only appear where the lowering accepts a value which is not known
// until the iteration is created: anywhere else, and for any construct
// not in the list below, the loop is instantiated once per iteration.
//

typedef A(vlog_node_t) node_list_t;

typedef struct {
   hset_t             *derived;
   vlog_find_mod_fn_t  find_mod;
   void               *find_ctx;
   node_list_t  stack;
   node_list_t  typed;
   const char  *why;
   vlog_node_t  where;
} share_ctx_t;

static void share_fail(share_ctx_t *ctx, vlog_node_t where, const char *why)
{
   if (ctx->why == NULL) {
      ctx->why = why;
      ctx->where = where;
   }
}

static bool is_derived_ref(share_ctx_t *ctx, vlog_node_t v)
{
   return vlog_kind(v) == V_REF && vlog_has_ref(v)
      && hset_contains(ctx->derived, vlog_ref(v));
}

static void find_derived_cb(vlog_node_t v, void *arg)
{
   share_ctx_t *ctx = arg;
   if (is_derived_ref(ctx, v))
      ctx->where = v;
}

static bool has_derived_ref(share_ctx_t *ctx, vlog_node_t v)
{
   vlog_node_t saved = ctx->where;
   ctx->where = NULL;
   vlog_visit(v, find_derived_cb, ctx);
   const bool found = ctx->where != NULL;
   ctx->where = saved;
   return found;
}

static void collect_localparams_cb(vlog_node_t v, void *arg)
{
   node_list_t *list = arg;
   APUSH(*list, v);
}

typedef struct {
   share_ctx_t *ctx;
   hset_t      *seen;
   bool         found;
} type_scan_t;

static void scan_type(type_scan_t *ts, vlog_node_t type);

static void scan_type_cb(vlog_node_t v, void *arg)
{
   type_scan_t *ts = arg;

   if (is_derived_ref(ts->ctx, v))
      ts->found = true;
   else if (vlog_kind_has_type(vlog_kind(v)) && vlog_has_type(v))
      scan_type(ts, vlog_type(v));
}

static void scan_type(type_scan_t *ts, vlog_node_t type)
{
   if (ts->found || hset_contains(ts->seen, type))
      return;

   hset_insert(ts->seen, type);
   vlog_visit(type, scan_type_cb, ts);
}

static bool is_display_task(vlog_node_t v)
{
   static const char *names[] = {
      "$display", "$displayb", "$displayh", "$displayo",
      "$write", "$writeb", "$writeh", "$writeo",
      "$finish", "$stop",
   };

   const char *name = istr(vlog_ident(v));
   for (int i = 0; i < ARRAY_LEN(names); i++) {
      if (strcmp(name, names[i]) == 0)
         return true;
   }

   return false;
}

static bool is_value_of(vlog_node_t parent, vlog_node_t child)
{
   return vlog_has_value(parent) && vlog_value(parent) == child;
}

static bool is_param_of(vlog_node_t parent, vlog_node_t child)
{
   const int nparams = vlog_params(parent);
   for (int i = 0; i < nparams; i++) {
      if (vlog_param(parent, i) == child)
         return true;
   }

   return false;
}

static bool is_stmt_of(vlog_node_t parent, vlog_node_t child)
{
   const int nstmts = vlog_stmts(parent);
   for (int i = 0; i < nstmts; i++) {
      if (vlog_stmt(parent, i) == child)
         return true;
   }

   return false;
}

static bool check_port_conn(share_ctx_t *ctx, int k)
{
   // A port connection where the module is known and the port is an input
   // or output which is not an unpacked array is lowered in the block of
   // the instance which may compute the select when it runs

   if (k < 2 || ctx->find_mod == NULL)
      return false;

   vlog_node_t conn = ctx->stack.items[k];
   vlog_node_t inst = ctx->stack.items[k - 1];
   vlog_node_t list = ctx->stack.items[k - 2];

   if (vlog_kind(inst) != V_MOD_INST || vlog_kind(list) != V_INST_LIST)
      return false;
   else if (vlog_ranges(inst) > 0)
      return false;   // Divided between the elements of an instance array

   const int nlparams = vlog_params(list);
   for (int i = 0; i < nlparams; i++) {
      if (has_derived_ref(ctx, vlog_param(list, i)))
         return false;   // Selects the body of the instance
   }

   vlog_node_t mod = (*ctx->find_mod)(list, ctx->find_ctx);
   if (mod == NULL || vlog_kind(mod) != V_MODULE)
      return false;

   vlog_node_t port = NULL;
   const int nports = vlog_ports(mod);
   if (vlog_has_ident(conn)) {
      for (int i = 0; i < nports && port == NULL; i++) {
         vlog_node_t p = vlog_ref(vlog_port(mod, i));
         if (vlog_ident(p) == vlog_ident(conn))
            port = p;
      }
   }
   else {
      const int nparams = vlog_params(inst);
      for (int i = 0; i < nparams && i < nports; i++) {
         if (vlog_param(inst, i) == conn)
            port = vlog_ref(vlog_port(mod, i));
      }
   }

   if (port == NULL || vlog_kind(port) != V_PORT_DECL)
      return false;

   switch (vlog_subkind(port)) {
   case V_PORT_INPUT:
   case V_PORT_OUTPUT:
      break;
   default:
      return false;
   }

   if (!vlog_has_ref(port))
      return false;

   vlog_node_t decl = vlog_ref(port);
   switch (vlog_kind(decl)) {
   case V_NET_DECL:
   case V_VAR_DECL:
      return vlog_ranges(decl) == 0;
   default:
      return false;
   }
}

static bool check_derived_use(share_ctx_t *ctx)
{
   // The reference at the top of the stack is allowed if every node from
   // it up to the process, declaration or generate construct which holds
   // it accepts a value computed when the iteration is created

   vlog_node_t c = ctx->stack.items[ctx->stack.count - 1];

   for (int k = ctx->stack.count - 2; k >= 0; c = ctx->stack.items[k--]) {
      vlog_node_t p = ctx->stack.items[k];

      switch (vlog_kind(p)) {
      case V_BIT_SELECT:
         // The longest static prefix of a select must be the same as when
         // the genvar is a constant
         if (is_param_of(p, c) && !vlog_is_const_arith(c))
            return false;
         continue;
      case V_BINARY:
      case V_UNARY:
      case V_COND_EXPR:
      case V_USER_FCALL:
      case V_MEMBER_REF:
      case V_SEQ_BLOCK:
      case V_IF:
      case V_FOR_LOOP:
         continue;
      case V_SYS_FCALL:
         switch (vlog_subkind(p)) {
         case V_SYSTF_SIGNED:
         case V_SYSTF_UNSIGNED:
            continue;
         default:
            return false;
         }
      case V_SYS_TCALL:
         if (!is_display_task(p) || !is_param_of(p, c)
             || vlog_param(p, 0) == c)
            return false;
         continue;
      case V_PART_SELECT:
         if (c == vlog_value(p))
            continue;
         else if (c == vlog_left(p) && vlog_subkind(p) != V_RANGE_CONST
                  && vlog_is_const_arith(c))
            continue;   // Base of an indexed part select
         else
            return false;
      case V_CONCAT:
         if (is_value_of(p, c))
            return false;   // Replication count
         continue;
      case V_CAST:
         if (is_value_of(p, c))
            continue;
         return false;
      case V_LOCALPARAM:
         // The parameter is in the derived set and its uses are checked
         return is_value_of(p, c) && vlog_ranges(p) == 0;
      case V_ASSIGN:
         return c == vlog_target(p) || is_value_of(p, c);
      case V_BASSIGN:
      case V_NBASSIGN:
         if (c == vlog_target(p) || is_value_of(p, c))
            continue;
         return false;
      case V_NET_DECL:
      case V_VAR_DECL:
         return is_value_of(p, c);
      case V_COND:
         if (is_value_of(p, c)) {
            // Condition of a generate construct or of a statement
            if (k > 0 && vlog_kind(ctx->stack.items[k - 1]) == V_IF_GENERATE)
               return true;
            continue;
         }
         else if (is_stmt_of(p, c))
            continue;
         return false;
      case V_CASE:
         continue;
      case V_CASE_ITEM:
         if (is_stmt_of(p, c))
            continue;
         return false;   // Item expression
      case V_TIMING:
         if (is_stmt_of(p, c))
            continue;
         return false;   // Event or delay control
      case V_FOR_INIT:
      case V_FOR_STEP:
         if (k > 0 && vlog_kind(ctx->stack.items[k - 1]) == V_FOR_GENERATE)
            return true;
         continue;
      case V_FOR_GENERATE:
         return !is_stmt_of(p, c);   // Loop header evaluated in the scope
      case V_ALWAYS:
      case V_INITIAL:
      case V_FINAL:
         return true;
      case V_PORT_CONN:
         return is_value_of(p, c) && check_port_conn(ctx, k);
      default:
         return false;
      }
   }

   return false;
}

static void share_pre_cb(vlog_node_t v, void *arg)
{
   share_ctx_t *ctx = arg;

   APUSH(ctx->stack, v);

   if (ctx->why != NULL)
      return;

   const vlog_kind_t kind = vlog_kind(v);
   switch (kind) {
   case V_GEN_BLOCK:
   case V_IF_GENERATE:
   case V_FOR_GENERATE:
   case V_COND:
   case V_FOR_INIT:
   case V_FOR_STEP:
   case V_FOR_LOOP:
   case V_PREFIX:
   case V_POSTFIX:
   case V_GENVAR_DECL:
   case V_ASSIGN:
   case V_ALWAYS:
   case V_INITIAL:
   case V_FINAL:
   case V_TIMING:
   case V_EVENT_CONTROL:
   case V_EVENT:
   case V_DELAY_CONTROL:
   case V_SEQ_BLOCK:
   case V_IF:
   case V_BASSIGN:
   case V_NBASSIGN:
   case V_CASE:
   case V_CASE_ITEM:
   case V_EMPTY:
   case V_REF:
   case V_NUMBER:
   case V_STRING:
   case V_BINARY:
   case V_UNARY:
   case V_COND_EXPR:
   case V_BIT_SELECT:
   case V_PART_SELECT:
   case V_CONCAT:
   case V_CAST:
   case V_MEMBER_REF:
   case V_USER_FCALL:
   case V_NET_DECL:
   case V_VAR_DECL:
   case V_LOCALPARAM:
   case V_DATA_TYPE:
   case V_DIMENSION:
   case V_INST_LIST:
   case V_MOD_INST:
   case V_PARAM_ASSIGN:
   case V_PORT_CONN:
   case V_ATTR_INST:
      break;
   case V_SYS_FCALL:
      switch (vlog_subkind(v)) {
      case V_SYSTF_SIGNED:
      case V_SYSTF_UNSIGNED:
      case V_SYSTF_CLOG2:
      case V_SYSTF_BITS:
         break;
      default:
         share_fail(ctx, v, "system function call");
         return;
      }
      break;
   case V_SYS_TCALL:
      if (!is_display_task(v)) {
         share_fail(ctx, v, "system task call");
         return;
      }
      break;
   default:
      share_fail(ctx, v, vlog_kind_str(kind));
      return;
   }

   if (vlog_kind_has_type(kind) && vlog_has_type(v))
      APUSH(ctx->typed, v);

   if (is_derived_ref(ctx, v) && !check_derived_use(ctx))
      share_fail(ctx, v, "genvar used where a constant is needed");
}

static void share_post_cb(vlog_node_t v, void *arg)
{
   share_ctx_t *ctx = arg;

   assert(ctx->stack.count > 0);
   assert(ctx->stack.items[ctx->stack.count - 1] == v);
   ctx->stack.count--;
}

bool vlog_gen_shareable(vlog_node_t loop, vlog_find_mod_fn_t find_mod,
                        void *find_ctx, const char **why, vlog_node_t *where)
{
   assert(vlog_kind(loop) == V_FOR_GENERATE);

   *why = NULL;
   *where = loop;

   vlog_node_t init = vlog_left(loop);
   if (vlog_kind(init) != V_FOR_INIT || vlog_stmts(init) != 1) {
      *why = "loop initialisation";
      return false;
   }

   vlog_node_t s0 = vlog_stmt(init, 0);
   if (vlog_kind(s0) != V_BASSIGN || vlog_kind(vlog_target(s0)) != V_REF) {
      *why = "loop initialisation";
      return false;
   }

   vlog_node_t genvar = vlog_ref(vlog_target(s0));
   if (vlog_kind(genvar) != V_GENVAR_DECL || vlog_width(genvar) != 32) {
      *why = "genvar type";
      return false;
   }

   vlog_node_t block = vlog_stmt(loop, 0);
   assert(vlog_kind(block) == V_GEN_BLOCK);

   share_ctx_t ctx = {
      .derived  = hset_new(16),
      .find_mod = find_mod,
      .find_ctx = find_ctx,
   };

   hset_insert(ctx.derived, genvar);

   // Local parameters computed from the genvar or from each other
   node_list_t lparams = AINIT;
   vlog_visit_only(block, collect_localparams_cb, &lparams, V_LOCALPARAM);

   for (bool changed = true; changed; ) {
      changed = false;
      for (int i = 0; i < lparams.count; i++) {
         vlog_node_t lp = lparams.items[i];
         if (hset_contains(ctx.derived, lp) || !vlog_has_value(lp))
            continue;
         else if (has_derived_ref(&ctx, vlog_value(lp))) {
            hset_insert(ctx.derived, lp);
            changed = true;
         }
      }
   }

   ACLEAR(lparams);

   vlog_visit_prepost(block, share_pre_cb, share_post_cb, &ctx);

   if (ctx.why == NULL) {
      // A type with a size computed from the genvar
      type_scan_t ts = {
         .ctx  = &ctx,
         .seen = hset_new(16),
      };

      for (int i = 0; i < ctx.typed.count && ctx.why == NULL; i++) {
         scan_type(&ts, vlog_type(ctx.typed.items[i]));
         if (ts.found)
            share_fail(&ctx, ctx.typed.items[i], "genvar in a type");
      }

      hset_free(ts.seen);
   }

   ACLEAR(ctx.stack);
   ACLEAR(ctx.typed);
   hset_free(ctx.derived);

   if (ctx.why != NULL) {
      *why = ctx.why;
      *where = ctx.where;
      return false;
   }

   return true;
}
