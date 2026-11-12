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
#include "diag.h"
#include "hash.h"
#include "ident.h"
#include "lint.h"
#include "vlog/vlog-node.h"
#include "vlog/vlog-number.h"
#include "vlog/vlog-phase.h"
#include "vlog/vlog-util.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

////////////////////////////////////////////////////////////////////////////////
// Unused and undriven declarations

enum {
   USE_READ    = (1 << 0),
   USE_WRITTEN = (1 << 1),
};

typedef struct {
   hash_t *lvalues;     // V_REF nodes in the target of an assignment
   hash_t *uses;        // Declaration to USE_* mask
   hash_t *excluded;    // Declarations that are not of interest
   hash_t *ports;
} use_ctx_t;

static void mark_lvalue(vlog_node_t v, use_ctx_t *ctx, bool also_read)
{
   switch (vlog_kind(v)) {
   case V_REF:
      hash_put(ctx->lvalues, v, (void *)(uintptr_t)(also_read ? 2 : 1));
      break;
   case V_BIT_SELECT:
   case V_PART_SELECT:
      mark_lvalue(vlog_value(v), ctx, also_read);
      break;
   case V_MEMBER_REF:
      // The handle of an object is read to assign to its member
      mark_lvalue(vlog_value(v), ctx, true);
      break;
   case V_CONCAT:
      {
         const int nparams = vlog_params(v);
         for (int i = 0; i < nparams; i++)
            mark_lvalue(vlog_param(v, i), ctx, also_read);
      }
      break;
   default:
      break;
   }
}

static bool is_print_task(const char *name)
{
   // System tasks which only read their arguments
   static const char *const pure[] = {
      "$display", "$write", "$strobe", "$monitor", "$fdisplay", "$fwrite",
      "$fstrobe", "$fmonitor", "$sformatf", "$error", "$warning", "$info",
      "$fatal", "$dumpvars", "$signed", "$unsigned", "$clog2",
      "$time", "$stime", "$realtime", "$test$plusargs", "$fopen", "$fclose",
      "$finish", "$stop", "$exit", "$monitoron", "$monitoroff",
      "$isunknown", "$onehot", "$onehot0", "$countones", "$past", "$rose",
      "$fell", "$stable", "$changed",
   };

   for (int i = 0; i < ARRAY_LEN(pure); i++) {
      if (strncmp(name, pure[i], strlen(pure[i])) == 0)
         return true;
   }

   return false;
}

static void mark_args(vlog_node_t v, use_ctx_t *ctx)
{
   const int nparams = vlog_params(v);
   for (int i = 0; i < nparams; i++)
      mark_lvalue(vlog_param(v, i), ctx, true);
}

static void lvalue_cb(vlog_node_t v, void *context)
{
   use_ctx_t *ctx = context;

   switch (vlog_kind(v)) {
   case V_BASSIGN:
   case V_NBASSIGN:
   case V_ASSIGN:
   case V_FORCE:
      mark_lvalue(vlog_target(v), ctx, false);
      break;
   case V_OP_ASSIGN:
   case V_PREFIX:
   case V_POSTFIX:
      mark_lvalue(vlog_target(v), ctx, true);
      break;
   case V_RELEASE:
   case V_DEASSIGN:
      mark_lvalue(vlog_target(v), ctx, false);
      break;
   case V_EVENT_TRIGGER:
      if (vlog_has_value(v))
         mark_lvalue(vlog_value(v), ctx, false);
      break;
   case V_SYS_TCALL:
   case V_SYS_FCALL:
      if (!is_print_task(istr(vlog_ident(v))))
         mark_args(v, ctx);
      break;
   case V_USER_TCALL:
   case V_USER_FCALL:
   case V_STD_RANDOMIZE:
      mark_args(v, ctx);
      break;
   case V_NAMED_ARG:
      mark_lvalue(vlog_value(v), ctx, true);
      break;
   case V_STREAM:
      mark_args(v, ctx);
      break;
   case V_GATE_INST:
      {
         // The first terminal is the output unless the gate is a switch
         const vlog_gate_kind_t kind = vlog_subkind(v);
         const bool sw = kind >= V_GATE_TRAN && kind <= V_GATE_RTRANIF1;
         if (vlog_has_target(v))
            mark_lvalue(vlog_target(v), ctx, sw);
         if (sw)
            mark_args(v, ctx);
      }
      break;
   case V_METHOD_CALL:
      mark_args(v, ctx);
      if (vlog_has_value(v))
         mark_lvalue(vlog_value(v), ctx, true);
      break;
   default:
      break;
   }

   switch (vlog_kind(v)) {
   case V_PORT_CONN:
   case V_PORT_MAP:
      // The direction of the port is not known here
      if (vlog_has_value(v))
         mark_lvalue(vlog_value(v), ctx, true);
      break;
   case V_HIER_REF:
   case V_MOD_REF:
      lint_hier_name(vlog_ident(v));
      break;
   default:
      break;
   }
}

static void ref_cb(vlog_node_t v, void *context)
{
   use_ctx_t *ctx = context;

   if (!vlog_has_ref(v))
      return;

   vlog_node_t decl = vlog_ref(v);

   unsigned mask = USE_READ;
   const uintptr_t lv = (uintptr_t)hash_get(ctx->lvalues, v);
   if (lv == 1)
      mask = USE_WRITTEN;
   else if (lv == 2)
      mask = USE_READ | USE_WRITTEN;

   const uintptr_t old = (uintptr_t)hash_get(ctx->uses, decl);
   hash_put(ctx->uses, decl, (void *)(old | mask));
}

static void exclude_decl_cb(vlog_node_t v, void *context)
{
   use_ctx_t *ctx = context;
   hash_put(ctx->excluded, v, ctx);
}

static void scope_cb(vlog_node_t v, void *context)
{
   // Declarations of subroutines, classes and aggregate types are not
   // declarations of the module
   switch (vlog_kind(v)) {
   case V_FUNC_DECL:
   case V_TASK_DECL:
   case V_CLASS_DECL:
   case V_STRUCT_DECL:
   case V_UNION_DECL:
   case V_FOR_INIT:
   case V_SVA_DECL:
   case V_SPECIFY:
   case V_CONSTRAINT:
      vlog_visit_only(v, exclude_decl_cb, context, V_VAR_DECL);
      vlog_visit_only(v, exclude_decl_cb, context, V_NET_DECL);
      break;
   default:
      break;
   }
}

typedef struct {
   use_ctx_t *use;
   vlog_node_t mod;
} candidate_ctx_t;

static void candidate_cb(vlog_node_t v, void *context)
{
   candidate_ctx_t *cc = context;
   use_ctx_t *ctx = cc->use;

   const bool is_net = vlog_kind(v) == V_NET_DECL;

   if (hash_get(ctx->excluded, v) || hash_get(ctx->ports, v))
      return;

   const loc_t *loc = vlog_loc(v);
   if (loc_invalid_p(loc))
      return;

   const char *name = istr(vlog_ident(v));
   if (name[0] == '$' || !lint_name_at(loc, name))
      return;

   if (is_net) {
      switch (vlog_subkind(v)) {
      case V_NET_SUPPLY0:
      case V_NET_SUPPLY1:
      case V_NET_TRI0:
      case V_NET_TRI1:
      case V_NET_TRIREG:
         return;   // Always driven
      default:
         break;
      }
   }
   else if (vlog_flags(v) & (VLOG_F_NESTED_INST | VLOG_F_VHDL))
      return;

   const uintptr_t use = (uintptr_t)hash_get(ctx->uses, v);
   const bool init = vlog_has_value(v);

   const char *what = is_net ? "net" : "variable";
   if (!(use & USE_READ))
      lint_declaration(loc, LINT_UNUSED, vlog_ident(v), what);
   else if (!(use & USE_WRITTEN) && !init)
      lint_declaration(loc, LINT_UNDRIVEN, vlog_ident(v), what);
}

static void port_decl_cb(vlog_node_t v, void *context)
{
   // The nets and variables implicitly declared for a port
   use_ctx_t *ctx = context;
   if (vlog_has_ref(v))
      hash_put(ctx->ports, vlog_ref(v), ctx);
}

void vlog_lint_design(vlog_node_t mod)
{
   if (vlog_kind(mod) != V_MODULE
       || (vlog_flags(mod) & (VLOG_F_INTERFACE | VLOG_F_CHECKER
                              | VLOG_F_BLACKBOX)))
      return;
   else if (!lint_first_visit(mod))
      return;

   use_ctx_t ctx = {
      .lvalues  = hash_new(64),
      .uses     = hash_new(64),
      .excluded = hash_new(64),
      .ports    = hash_new(16),
   };

   vlog_visit(mod, scope_cb, &ctx);
   vlog_visit_only(mod, port_decl_cb, &ctx, V_PORT_DECL);
   vlog_visit(mod, lvalue_cb, &ctx);
   vlog_visit_only(mod, ref_cb, &ctx, V_REF);

   candidate_ctx_t cc = { .use = &ctx, .mod = mod };
   vlog_visit_only(mod, candidate_cb, &cc, V_VAR_DECL);
   vlog_visit_only(mod, candidate_cb, &cc, V_NET_DECL);

   hash_free(ctx.lvalues);
   hash_free(ctx.uses);
   hash_free(ctx.excluded);
   hash_free(ctx.ports);
}

