//
//  Copyright (C) 2024-2025 Nick Gasson
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
#include "diag.h"
#include "hash.h"
#include "ident.h"
#include "vlog/vlog-node.h"
#include "vlog/vlog-number.h"
#include "vlog/vlog-phase.h"
#include "vlog/vlog-util.h"

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>

static vlog_node_t simp_net_decl(vlog_node_t decl, vlog_node_t mod)
{
   const vlog_net_kind_t kind = vlog_subkind(decl);
   if (kind == V_NET_SUPPLY0 || kind == V_NET_SUPPLY1 || kind == V_NET_TRI0
       || kind == V_NET_TRI1) {
      vlog_node_t g = vlog_new(V_GATE_INST);
      vlog_set_loc(g, vlog_loc(decl));

      ident_t name = vlog_ident(decl);
      ident_t id = ident_prefix(name, ident_new("pull"), '#');
      vlog_set_ident(g, id);

      vlog_node_t s = vlog_new(V_STRENGTH);
      vlog_set_subkind(s, kind == V_NET_TRI0 || kind == V_NET_TRI1
                       ? ST_PULLUP : ST_SUPPLY);

      vlog_add_param(g, s);

      if (kind == V_NET_SUPPLY0 || kind == V_NET_TRI0)
         vlog_set_subkind(g, V_GATE_PULLDOWN);
      else
         vlog_set_subkind(g, V_GATE_PULLUP);

      vlog_node_t target = vlog_new(V_REF);
      vlog_set_loc(target, vlog_loc(decl));
      vlog_set_ident(target, name);
      vlog_set_ref(target, decl);

      vlog_set_target(g, target);

      vlog_add_stmt(mod, g);
   }

   return decl;
}

static vlog_node_t simp_port_decl(vlog_node_t v, vlog_node_t mod)
{
   if (vlog_has_ref(v))
      return v;

   vlog_node_t dt = vlog_type(v), decl;
   if (is_implicit_data_type(dt)) {
      decl = vlog_new(V_NET_DECL);
      vlog_set_subkind(decl, V_NET_WIRE);
   }
   else
      decl = vlog_new(V_VAR_DECL);

   vlog_set_loc(decl, vlog_loc(v));
   vlog_set_ident(decl, vlog_ident(v));
   vlog_set_type(decl, vlog_type(v));

   const int nranges = vlog_ranges(v);
   for (int i = 0; i < nranges; i++)
      vlog_add_range(decl, vlog_range(v, i));

   if (vlog_has_value(v))
      vlog_set_value(decl, vlog_value(v));

   vlog_set_ref(v, decl);
   vlog_add_decl(mod, decl);

   return v;
}

static void build_sensitivity(vlog_node_t ctrl, vlog_node_t v, hset_t *set,
                              bool is_comb)
{
   switch (vlog_kind(v)) {
   case V_NBASSIGN:
   case V_BASSIGN:
   case V_FORCE:
   case V_UNARY:
   case V_CAST:
   case V_OP_ASSIGN:
      build_sensitivity(ctrl, vlog_value(v), set, is_comb);
      break;
   case V_BINARY:
      build_sensitivity(ctrl, vlog_left(v), set, is_comb);
      build_sensitivity(ctrl, vlog_right(v), set, is_comb);
      break;
   case V_REF:
      {
         vlog_node_t d = vlog_ref(v);
         if (hset_contains(set, d))
            break;

         hset_insert(set, d);
      }
      // Fall-through
   case V_BIT_SELECT:
   case V_PART_SELECT:
   case V_MEMBER_REF:
   case V_HIER_REF:
      {
         vlog_node_t lsp = vlog_longest_static_prefix(v);
         if (lsp == NULL)
            break;
         else if (lsp == v) {
            vlog_node_t e = vlog_new(V_EVENT);
            vlog_set_subkind(e, V_EVENT_LEVEL);
            vlog_set_value(e, v);
            vlog_set_loc(e, vlog_loc(ctrl));

            vlog_add_param(ctrl, e);

            // The indices of a hierarchical name are also read
            if (vlog_kind(v) == V_HIER_REF) {
               for (vlog_node_t it = vlog_value(v); ; it = vlog_value(it)) {
                  if (vlog_kind(it) != V_MOD_REF)
                     break;

                  const int nparams = vlog_params(it);
                  for (int i = 0; i < nparams; i++)
                     build_sensitivity(ctrl, vlog_param(it, i), set, is_comb);

                  if (!vlog_has_value(it))
                     break;
               }
            }
        }
        else {
            build_sensitivity(ctrl, vlog_value(v), set, is_comb);

            switch (vlog_kind(v)) {
            case V_BIT_SELECT:
               {
                  const int nparams = vlog_params(v);
                  for (int i = 0; i < nparams; i++)
                     build_sensitivity(ctrl, vlog_param(v, i), set, is_comb);
               }
               break;
            case V_PART_SELECT:
               build_sensitivity(ctrl, vlog_left(v), set, is_comb);
               build_sensitivity(ctrl, vlog_right(v), set, is_comb);
               break;
            case V_MEMBER_REF:
               break;
            default:
               should_not_reach_here();
            }
         }
      }
      break;
   case V_NUMBER:
   case V_STRING:
      break;
   case V_ASSERT:
   case V_ASSUME:
   case V_COVER:
      build_sensitivity(ctrl, vlog_value(v), set, is_comb);
      // Fall-through
   case V_IF:
      {
         const int nconds = vlog_conds(v);
         for (int i = 0; i < nconds; i++)
            build_sensitivity(ctrl, vlog_cond(v, i), set, is_comb);
      }
      break;
   case V_COND:
      {
         if (vlog_has_value(v))
            build_sensitivity(ctrl, vlog_value(v), set, is_comb);

         const int nstmts = vlog_stmts(v);
         for (int i = 0; i < nstmts; i++)
            build_sensitivity(ctrl, vlog_stmt(v, i), set, is_comb);
      }
      break;
   case V_CASE:
      build_sensitivity(ctrl, vlog_value(v), set, is_comb);
      // Fall-through
   case V_CASE_ITEM:
      if (vlog_kind(v) == V_CASE_ITEM) {
         const int nparams = vlog_params(v);
         for (int i = 0; i < nparams; i++)
            build_sensitivity(ctrl, vlog_param(v, i), set, is_comb);
      }
      // Fall-through
   case V_TIMING:
      {
         const int nstmts = vlog_stmts(v);
         for (int i = 0; i < nstmts; i++)
            build_sensitivity(ctrl, vlog_stmt(v, i), set, is_comb);
      }
      break;
   case V_FUNC_DECL:
   case V_TASK_DECL:
      {
         // Ignore task/function ports
         const int nports = vlog_ports(v);
         for (int i = 0; i < nports; i++)
            hset_insert(set, vlog_port(v, i));
      }
      // Fall-through
   case V_SEQ_BLOCK:
      {
         // Ignore local variables
         const int ndecls = vlog_decls(v);
         for (int i = 0; i < ndecls; i++)
            hset_insert(set, vlog_decl(v, i));

         const int nstmts = vlog_stmts(v);
         for (int i = 0; i < nstmts; i++)
            build_sensitivity(ctrl, vlog_stmt(v, i), set, is_comb);
      }
      break;
   case V_USER_FCALL:
   case V_USER_TCALL:
      if (is_comb)
         build_sensitivity(ctrl, vlog_ref(v), set, is_comb);
      // Fall-through
   case V_SYS_TCALL:
      if (vlog_ident(v) == ident_new("$__defer"))
         break;   // The variable it changes is not read
      // Fall-through
   case V_CONCAT:
   case V_SYS_FCALL:
   case V_METHOD_CALL:
      {
         const int nparams = vlog_params(v);
         for (int i = 0; i < nparams; i++)
            build_sensitivity(ctrl, vlog_param(v, i), set, is_comb);
      }
      break;
   case V_COND_EXPR:
      build_sensitivity(ctrl, vlog_value(v), set, is_comb);
      build_sensitivity(ctrl, vlog_left(v), set, is_comb);
      build_sensitivity(ctrl, vlog_right(v), set, is_comb);
      break;
   case V_FOR_LOOP:
      {
         build_sensitivity(ctrl, vlog_left(v), set, is_comb);
         if (vlog_has_value(v))
            build_sensitivity(ctrl, vlog_value(v), set, is_comb);
         build_sensitivity(ctrl, vlog_right(v), set, is_comb);
         const int nstmts = vlog_stmts(v);
         for (int i = 0; i < nstmts; i++)
            build_sensitivity(ctrl, vlog_stmt(v, i), set, is_comb);
      }
      break;
   case V_FOR_INIT:
   case V_FOR_STEP:
      {
         const int nstmts = vlog_stmts(v);
         for (int i = 0; i < nstmts; i++)
            build_sensitivity(ctrl, vlog_stmt(v, i), set, is_comb);
      }
      break;
   case V_POSTFIX:
   case V_PREFIX:
      build_sensitivity(ctrl, vlog_target(v), set, is_comb);
      break;
   case V_INSIDE:
      {
         build_sensitivity(ctrl, vlog_value(v), set, is_comb);

         const int nparams = vlog_params(v);
         for (int i = 0; i < nparams; i++)
            build_sensitivity(ctrl, vlog_param(v, i), set, is_comb);
      }
      break;
   case V_INSIDE_RANGE:
      build_sensitivity(ctrl, vlog_left(v), set, is_comb);
      build_sensitivity(ctrl, vlog_right(v), set, is_comb);
      break;
   case V_MATCHES:
      build_sensitivity(ctrl, vlog_value(v), set, is_comb);
      break;
   case V_DOLLAR:
   case V_EMPTY:
   case V_LOOP_JUMP:
   case V_DISABLE:
   case V_REAL:
   case V_NULL:
      break;
   case V_RETURN:
      if (vlog_has_value(v))
         build_sensitivity(ctrl, vlog_value(v), set, is_comb);
      break;
   case V_VOID_CALL:
      build_sensitivity(ctrl, vlog_value(v), set, is_comb);
      break;
   case V_WHILE:
   case V_DO_WHILE:
   case V_REPEAT:
      build_sensitivity(ctrl, vlog_value(v), set, is_comb);
      // Fall-through
   case V_FOREVER:
      {
         const int nstmts = vlog_stmts(v);
         for (int i = 0; i < nstmts; i++)
            build_sensitivity(ctrl, vlog_stmt(v, i), set, is_comb);
      }
      break;
   case V_MIN_TYP_MAX:
      build_sensitivity(ctrl, vlog_value(v), set, is_comb);
      break;
   case V_PATTERN_EXPR:
      {
         const int nparams = vlog_params(v);
         for (int i = 0; i < nparams; i++)
            build_sensitivity(ctrl, vlog_param(v, i), set, is_comb);
      }
      break;
   case V_PATTERN_ITEM:
      build_sensitivity(ctrl, vlog_right(v), set, is_comb);
      break;
   case V_STREAM:
      {
         if (vlog_has_value(v))
            build_sensitivity(ctrl, vlog_value(v), set, is_comb);

         const int nparams = vlog_params(v);
         for (int i = 0; i < nparams; i++)
            build_sensitivity(ctrl, vlog_param(v, i), set, is_comb);
      }
      break;
   default:
      fatal_at(vlog_loc(v), "sorry, %s in an expression or procedure with an "
               "implicit sensitivity list is not supported",
               vlog_kind_str(vlog_kind(v)));
   }
}

static vlog_node_t simp_timing(vlog_node_t v)
{
   vlog_node_t ctrl = vlog_value(v);

   if (vlog_kind(ctrl) == V_EVENT_CONTROL && vlog_params(ctrl) == 0) {
      hset_t *set = hset_new(16);

      const int nstmts = vlog_stmts(v);
      for (int i = 0; i < nstmts; i++)
         build_sensitivity(ctrl, vlog_stmt(v, i), set, false);

      if (vlog_params(ctrl) == 0)
         warn_at(vlog_loc(v), "timing statement has no sensitivities and "
                 "so will never trigger");

      hset_free(set);
   }

   return v;
}

static vlog_node_t simp_always(vlog_node_t v)
{
   switch (vlog_subkind(v)) {
   case V_ALWAYS_COMB:
   case V_ALWAYS_LATCH:
      {
         // See 1800-2023 section 9.2.2.2.1
         vlog_node_t ctrl = vlog_new(V_EVENT_CONTROL);
         vlog_set_loc(ctrl, vlog_loc(v));

         hset_t *set = hset_new(16);

         vlog_node_t s0 = vlog_stmt(v, 0);
         build_sensitivity(ctrl, s0, set, true);

         hset_free(set);

         vlog_node_t timing = vlog_new(V_TIMING);
         vlog_set_loc(timing, vlog_loc(v));
         vlog_set_value(timing, ctrl);

         vlog_node_t b = vlog_new(V_SEQ_BLOCK);
         vlog_set_loc(b, vlog_loc(v));
         vlog_add_stmt(b, s0);
         vlog_add_stmt(b, timing);

         vlog_node_t new = vlog_new(V_ALWAYS);
         vlog_set_loc(new, vlog_loc(v));
         vlog_set_subkind(new, V_ALWAYS_PLAIN);
         vlog_set_ident(new, vlog_ident(v));
         vlog_add_stmt(new, b);

         return new;
      }
   default:
      return v;
   }
}

static vlog_node_t simp_enum_decl(vlog_node_t v)
{
   vlog_node_t last = NULL;
   const int ndecls = vlog_decls(v);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(v, i);
      if (!vlog_has_value(d)) {
         if (last == NULL) {
            vlog_node_t n = vlog_new(V_NUMBER);
            vlog_set_loc(n, vlog_loc(d));
            vlog_set_number(n, number_from_int(i));

            vlog_set_value(d, n);
         }
         else if (vlog_kind(last) == V_NUMBER) {
            number_t next = number_add(vlog_number(last), number_from_int(1));

            vlog_node_t n = vlog_new(V_NUMBER);
            vlog_set_loc(n, vlog_loc(d));
            vlog_set_number(n, next);

            vlog_set_value(d, n);
         }
         else {
            vlog_node_t one = vlog_new(V_NUMBER);
            vlog_set_number(one, number_from_int(1));

            vlog_node_t plus = vlog_new(V_BINARY);
            vlog_set_loc(plus, vlog_loc(d));
            vlog_set_subkind(plus, V_BINARY_PLUS);
            vlog_set_left(plus, last);
            vlog_set_right(plus, one);

            vlog_set_value(d, plus);
         }
      }

      last = vlog_value(d);
   }

   return v;
}

static vlog_node_t simp_concat(vlog_node_t v)
{
   if (vlog_params(v) != 1 || vlog_has_value(v))
      return v;

   vlog_node_t p0 = vlog_param(v, 0);
   if (vlog_kind(p0) == V_NUMBER)
      return p0;

   return v;
}

static vlog_node_t simp_call_args(vlog_node_t v)
{
   vlog_node_t sub = vlog_ref(v);

   const int nparams = vlog_params(v);
   const int nports = vlog_ports(sub);

   vlog_node_t *actuals LOCAL = xcalloc_array(nports + 1, sizeof(vlog_node_t));

   for (int i = 0; i < nports; i++) {
      vlog_node_t port = vlog_port(sub, i);
      vlog_node_t value = vlog_has_value(port) ? vlog_value(port) : NULL;
      if (i < nparams) {
         vlog_node_t p = vlog_param(v, i);
         if (vlog_kind(p) == V_EMPTY && value != NULL) {
            p = vlog_bind_default(sub, value, actuals, i);
            vlog_set_param(v, i, p);
         }

         if (vlog_kind(p) != V_NAMED_ARG)
            actuals[i] = p;
      }
      else if (value != NULL) {
         vlog_node_t p = vlog_bind_default(sub, value, actuals, i);
         vlog_add_param(v, p);
         actuals[i] = p;
      }
   }

   return v;
}

static vlog_node_t simp_wait(vlog_node_t v)
{
   vlog_node_t expr = vlog_value(v);

   vlog_node_t type = vlog_get_type(expr), cond;
   if (type == NULL ? !vlog_is_real(expr)
       : vlog_kind(type) == V_DATA_TYPE && vlog_subkind(type) != DT_REAL
       && vlog_subkind(type) != DT_SHORTREAL
       && vlog_subkind(type) != DT_REALTIME) {
      // The condition is true only if some bit is known to be one
      vlog_node_t any = vlog_new(V_UNARY);
      vlog_set_loc(any, vlog_loc(expr));
      vlog_set_subkind(any, V_UNARY_OR);
      vlog_set_value(any, expr);

      vlog_node_t one = vlog_new(V_NUMBER);
      vlog_set_loc(one, vlog_loc(expr));
      vlog_set_number(one, number_from_int(1));

      cond = vlog_new(V_BINARY);
      vlog_set_loc(cond, vlog_loc(expr));
      vlog_set_subkind(cond, V_BINARY_CASE_NEQ);
      vlog_set_left(cond, any);
      vlog_set_right(cond, one);
   }
   else {
      cond = vlog_new(V_UNARY);
      vlog_set_loc(cond, vlog_loc(expr));
      vlog_set_subkind(cond, V_UNARY_NOT);
      vlog_set_value(cond, expr);
   }

   vlog_node_t wh = vlog_new(V_WHILE);
   vlog_set_loc(wh, vlog_loc(v));
   vlog_set_value(wh, cond);

   vlog_node_t ctrl = vlog_new(V_EVENT_CONTROL);
   vlog_set_loc(ctrl, vlog_loc(v));

   if (vlog_needs_polling(expr)) {
      vlog_node_t e = vlog_new(V_EVENT);
      vlog_set_subkind(e, V_EVENT_LEVEL);
      vlog_set_value(e, expr);
      vlog_set_loc(e, vlog_loc(expr));
      vlog_add_param(ctrl, e);
   }
   else {
      hset_t *set = hset_new(16);
      build_sensitivity(ctrl, expr, set, false);
      hset_free(set);
   }

   vlog_node_t timing = vlog_new(V_TIMING);
   vlog_set_loc(timing, vlog_loc(v));
   vlog_set_value(timing, ctrl);
   vlog_add_stmt(wh, timing);

   const int nstmts = vlog_stmts(v);
   if (nstmts == 0)
      return wh;

   vlog_node_t block = vlog_new(V_SEQ_BLOCK);
   vlog_set_loc(block, vlog_loc(v));
   vlog_add_stmt(block, wh);

   for (int i = 0; i < nstmts; i++)
      vlog_add_stmt(block, vlog_stmt(v, i));

   return block;
}

static int format_arg_index(vlog_node_t v)
{
   // The position of the format string among the arguments

   static const struct {
      const char *name;
      int         index;
   } tab[] = {
      { "$display", 0 }, { "$displayb", 0 }, { "$displayh", 0 },
      { "$displayo", 0 }, { "$write", 0 }, { "$writeb", 0 },
      { "$writeh", 0 }, { "$writeo", 0 }, { "$strobe", 0 },
      { "$monitor", 0 }, { "$sformatf", 0 }, { "$error", 0 },
      { "$warning", 0 }, { "$info", 0 }, { "$fdisplay", 1 },
      { "$fwrite", 1 }, { "$fstrobe", 1 }, { "$fmonitor", 1 },
      { "$sformat", 1 }, { "$swrite", 1 }, { "$fatal", 1 },
      { "$__swriteb", 0 }, { "$__swriteo", 0 }, { "$__swriteh", 0 },
   };

   ident_t id = vlog_ident(v);
   for (int i = 0; i < ARRAY_LEN(tab); i++) {
      if (id == ident_new(tab[i].name))
         return tab[i].index;
   }

   return -1;
}

static bool format_is_scalar(vlog_node_t arg)
{
   switch (vlog_kind(arg)) {
   case V_REF:
   case V_MEMBER_REF:
   case V_HIER_REF:
      {
         if (!vlog_has_ref(arg))
            return true;

         vlog_node_t decl = vlog_ref(arg);
         switch (vlog_kind(decl)) {
         case V_VAR_DECL:
         case V_NET_DECL:
         case V_PORT_DECL:
         case V_TF_PORT_DECL:
         case V_LOCAL_DECL:
         case V_PARAM_DECL:
         case V_LOCALPARAM:
            return vlog_ranges(decl) == 0;
         default:
            return true;
         }
      }
   default:
      return true;
   }
}

static char format_p_kind(vlog_node_t arg)
{
   // How the %p conversion of a scalar value is printed: d for integral,
   // g for real, q for a string and s for the name of an enumeration

   if (!format_is_scalar(arg))
      return 0;

   vlog_node_t type = vlog_get_type(arg);
   if (type == NULL && vlog_kind(arg) == V_REF && vlog_has_ref(arg)
       && vlog_kind(vlog_ref(arg)) == V_ENUM_NAME)
      type = vlog_type(vlog_ref(arg));
   else if (type == NULL && vlog_kind(arg) == V_CAST
            && vlog_subkind(arg) == V_CAST_TYPE)
      type = vlog_get_type(vlog_type(arg));

   if (type == NULL) {
      switch (vlog_kind(arg)) {
      case V_NUMBER:
      case V_REAL:
      case V_BINARY:
      case V_UNARY:
         return vlog_is_real(arg) ? 'g' : 'd';
      case V_METHOD_CALL:
         {
            const ident_t id = vlog_ident(arg);
            if (id == ident_new("name") || id == ident_new("substr")
                || id == ident_new("toupper") || id == ident_new("tolower"))
               return 'q';
            return id == ident_new("atoreal") ? 'g' : 'd';
         }
      case V_SYS_FCALL:
         if (vlog_ident(arg) == ident_new("$bitstoreal"))
            return 'g';
         return vlog_subkind(arg) == V_SYSTF_SIGNED
            || vlog_subkind(arg) == V_SYSTF_UNSIGNED ? 'd' : 0;
      default:
         return 0;
      }
   }
   else if (vlog_kind(type) == V_ENUM_DECL)
      return 's';
   else if (vlog_kind(type) == V_STRUCT_DECL || vlog_kind(type) == V_UNION_DECL)
      return (vlog_flags(type) & VLOG_F_PACKED)
         && !(vlog_flags(type) & (VLOG_F_TAGGED | VLOG_F_UNPACKED_VIEW))
         ? 'd' : 0;
   else if (vlog_kind(type) != V_DATA_TYPE)
      return 0;

   switch (vlog_subkind(type)) {
   case DT_IMPLICIT:
   case DT_LOGIC:
   case DT_INTEGER:
   case DT_BYTE:
   case DT_SHORTINT:
   case DT_INT:
   case DT_LONGINT:
   case DT_TIME:
   case DT_BIT:
      return 'd';
   case DT_REAL:
   case DT_SHORTREAL:
   case DT_REALTIME:
      return 'g';
   case DT_STRING:
      return 'q';
   default:
      return 0;
   }
}

static vlog_node_t format_enum_name(vlog_node_t v, vlog_node_t value)
{
   // The name of an enumeration value or its number if there is none

   vlog_node_t call = vlog_new(V_METHOD_CALL);
   vlog_set_loc(call, vlog_loc(value));
   vlog_set_value(call, value);
   vlog_set_ident(call, ident_new("name"));
   vlog_resolve_method(call);

   vlog_node_t fmt = vlog_new(V_STRING);
   vlog_set_loc(fmt, vlog_loc(value));
   vlog_set_number(fmt, number_from_string("%0d", 3));

   vlog_node_t number = vlog_new(V_SYS_FCALL);
   vlog_set_ident(number, ident_new("$sformatf"));
   vlog_set_subkind(number, V_SYSTF_NONE);
   vlog_set_ival(number, vlog_ival(v));
   vlog_set_loc(number, vlog_loc(value));
   vlog_add_param(number, fmt);
   vlog_add_param(number, value);

   vlog_add_param(call, number);
   return call;
}

typedef struct {
   text_buf_t  *tb;
   vlog_node_t *args;
   int          nargs;
   int          maxargs;
   bool         plain;   // The conversion is %0p
   int          depth;
} pfmt_t;

static void pfmt_leaf(pfmt_t *c, vlog_node_t e, vlog_node_t type);
static vlog_node_t simp_format_p(vlog_node_t v);

static void pfmt_push(pfmt_t *c, vlog_node_t e)
{
   if (c->nargs == c->maxargs) {
      c->maxargs = MAX(16, c->maxargs * 2);
      c->args = xrealloc_array(c->args, c->maxargs, sizeof(vlog_node_t));
   }

   c->args[c->nargs++] = e;
}

static void pfmt_elements(pfmt_t *c, vlog_node_t m, vlog_node_t mt,
                          const int64_t *index, int dim, int ndims,
                          bool is_signed, bool ascending)
{
   // The elements of an array overlaid by a union in the order of the
   // declaration

   if (dim == ndims) {
      vlog_node_t e = vlog_new(V_BIT_SELECT);
      vlog_set_value(e, m);
      vlog_set_loc(e, vlog_loc(m));

      for (int i = 0; i < ndims; i++) {
         vlog_node_t number = vlog_new(V_NUMBER);
         vlog_set_number(number, number_from_int(index[i]));
         vlog_set_loc(number, vlog_loc(m));
         vlog_add_param(e, number);
      }

      vlog_node_t conv = vlog_new(V_SYS_FCALL);
      vlog_set_ident(conv, ident_new(is_signed ? "$signed" : "$unsigned"));
      vlog_set_subkind(conv, is_signed ? V_SYSTF_SIGNED : V_SYSTF_UNSIGNED);
      vlog_set_ival(conv, 0);
      vlog_set_loc(conv, vlog_loc(m));
      vlog_add_param(conv, e);
      e = conv;

      pfmt_leaf(c, e, NULL);
      return;
   }

   int64_t left, right;
   vlog_bounds(vlog_range(mt, dim), &left, &right);
   const int64_t hi = MAX(left, right), lo = MIN(left, right);

   tb_cat(c->tb, "'{");
   c->depth++;
   for (int64_t i = 0; i <= hi - lo; i++) {
      int64_t next[8];
      memcpy(next, index, dim * sizeof(int64_t));
      next[dim] = ascending ? lo + i : hi - i;

      if (i > 0)
         tb_cat(c->tb, c->plain ? "," : ", ");
      pfmt_elements(c, m, mt, next, dim + 1, ndims, is_signed, ascending);
   }
   c->depth--;
   tb_append(c->tb, '}');
}

static void pfmt_field(pfmt_t *c, vlog_node_t e, vlog_node_t fdecl)
{
   vlog_node_t m = vlog_new(V_MEMBER_REF);
   vlog_set_value(m, e);
   vlog_set_ident(m, vlog_ident(fdecl));
   vlog_set_ref(m, fdecl);
   vlog_set_loc(m, vlog_loc(e));

   vlog_node_t type = vlog_get_type(fdecl);

   if (vlog_flags(fdecl) & VLOG_F_REALVIEW) {
      // A real member of a union is held as its IEEE bits
      vlog_node_t call = vlog_new(V_SYS_FCALL);
      vlog_set_ident(call, ident_new("$bitstoreal"));
      vlog_set_subkind(call, V_SYSTF_NONE);
      vlog_set_ival(call, 0);
      vlog_set_loc(call, vlog_loc(e));
      vlog_add_param(call, m);
      pfmt_leaf(c, call, NULL);
      return;
   }

   vlog_node_t mt = vlog_type(fdecl);
   if (vlog_kind(mt) == V_DATA_TYPE && vlog_params(mt) == 3) {
      // A member of a union which overlays an unpacked array
      int64_t ndims, is_signed, ascending;
      vlog_get_const(vlog_param(mt, 0), &ndims);
      vlog_get_const(vlog_param(mt, 1), &is_signed);
      vlog_get_const(vlog_param(mt, 2), &ascending);
      pfmt_elements(c, m, mt, NULL, 0, ndims, is_signed, ascending);
      return;
   }

   // The unpacked dimension is on the declaration or its type
   vlog_node_t dims = fdecl, dt = vlog_type(fdecl);
   if (vlog_ranges(fdecl) == 0 && vlog_kind(dt) == V_DATA_TYPE
       && vlog_ranges(dt) == 1 && vlog_has_type(dt))
      dims = dt;

   int64_t left, right;
   if (vlog_ranges(dims) == 1
       && vlog_subkind(vlog_range(dims, 0)) == V_DIM_UNPACKED
       && vlog_range_bound(vlog_left(vlog_range(dims, 0)), &left)
       && vlog_range_bound(vlog_right(vlog_range(dims, 0)), &right)) {
      const int64_t n = (left > right ? left - right : right - left) + 1;
      const int64_t low = MIN(left, right);

      tb_cat(c->tb, "'{");
      c->depth++;
      for (int64_t i = 0; i < n; i++) {
         vlog_node_t sel = vlog_new(V_BIT_SELECT);
         vlog_set_value(sel, m);
         vlog_set_loc(sel, vlog_loc(e));

         vlog_node_t index = vlog_new(V_NUMBER);
         vlog_set_number(index, number_from_int(low + i));
         vlog_set_loc(index, vlog_loc(e));
         vlog_add_param(sel, index);

         if (i > 0)
            tb_cat(c->tb, c->plain ? "," : ", ");
         pfmt_leaf(c, sel, type);
      }
      c->depth--;
      tb_append(c->tb, '}');
   }
   else
      pfmt_leaf(c, m, type);
}

static bool pfmt_is_unpacked_union(vlog_node_t type)
{
   return type != NULL && vlog_kind(type) == V_UNION_DECL
      && !(vlog_flags(type) & VLOG_F_TAGGED)
      && (!(vlog_flags(type) & VLOG_F_PACKED)
          || (vlog_flags(type) & VLOG_F_UNPACKED_VIEW));
}

static bool pfmt_is_unpacked_struct(vlog_node_t type)
{
   // A packed structure is an integral value printed as one number (7.2.1)
   return type != NULL && vlog_kind(type) == V_STRUCT_DECL
      && !(vlog_flags(type) & VLOG_F_PACKED);
}

static bool pfmt_is_tagged_union(vlog_node_t type)
{
   return type != NULL && vlog_kind(type) == V_UNION_DECL
      && (vlog_flags(type) & VLOG_F_TAGGED);
}

static void pfmt_union(pfmt_t *c, vlog_node_t e, vlog_node_t type)
{
   // The member of a tagged union to show is only known when the call
   // runs so each member has a string expression selected by its tag.
   // It is shown as a tagged union expression (11.9).

   const bool packed = !!(vlog_flags(type) & VLOG_F_PACKED);

   const char *none_text = "<no tag>";

   vlog_node_t none = vlog_new(V_STRING);
   vlog_set_loc(none, vlog_loc(e));
   vlog_set_number(none, number_from_string(none_text, strlen(none_text)));

   vlog_node_t chain = none;
   for (int i = vlog_decls(type) - (packed ? 1 : 2); i >= 0; i--) {
      vlog_node_t fdecl = vlog_decl(type, i);

      pfmt_t sub = { .tb = tb_new(), .plain = c->plain, .depth = c->depth + 1 };
      tb_cat(sub.tb, "tagged ");
      tb_cat(sub.tb, istr(vlog_ident(fdecl)));

      if (!(vlog_flags(fdecl) & VLOG_F_VOID)) {
         tb_append(sub.tb, ' ');
         pfmt_field(&sub, e, fdecl);
      }

      vlog_node_t fmt = vlog_new(V_STRING);
      vlog_set_loc(fmt, vlog_loc(e));
      vlog_set_number(fmt, number_from_string(tb_get(sub.tb), tb_len(sub.tb)));

      vlog_node_t call = vlog_new(V_SYS_FCALL);
      vlog_set_ident(call, ident_new("$sformatf"));
      vlog_set_subkind(call, V_SYSTF_NONE);
      vlog_set_loc(call, vlog_loc(e));
      vlog_add_param(call, fmt);
      for (int j = 0; j < sub.nargs; j++)
         vlog_add_param(call, sub.args[j]);

      free(sub.args);
      tb_free(sub.tb);

      call = simp_format_p(call);

      vlog_node_t pat = vlog_new(V_MATCH_PAT);
      vlog_set_subkind(pat, V_PAT_TAGGED);
      vlog_set_type(pat, type);
      vlog_set_ref(pat, fdecl);
      vlog_set_loc(pat, vlog_loc(e));

      vlog_node_t test = vlog_new(V_MATCHES);
      vlog_set_value(test, e);
      vlog_set_left(test, pat);
      vlog_set_loc(test, vlog_loc(e));

      vlog_node_t cond = vlog_new(V_COND_EXPR);
      vlog_set_value(cond, test);
      vlog_set_left(cond, call);
      vlog_set_right(cond, chain);
      vlog_set_loc(cond, vlog_loc(e));
      chain = cond;
   }

   tb_cat(c->tb, "%s");
   pfmt_push(c, chain);
}

static void pfmt_leaf(pfmt_t *c, vlog_node_t e, vlog_node_t type)
{
   if (pfmt_is_tagged_union(type)) {
      pfmt_union(c, e, type);
      return;
   }

   if (type == NULL || (!pfmt_is_unpacked_struct(type)
                        && !pfmt_is_unpacked_union(type))) {
      tb_cat(c->tb, c->plain ? "%0p" : "%p");
      pfmt_push(c, e);
      return;
   }

   // Only the first member of an untagged union is shown (21.2.1.7)
   const int ndecls = pfmt_is_unpacked_union(type) ? 1 : vlog_decls(type);

   tb_cat(c->tb, "'{");
   c->depth++;
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t fdecl = vlog_decl(type, i);
      if (i > 0)
         tb_cat(c->tb, c->plain ? "," : ", ");
      tb_printf(c->tb, "%s:", istr(vlog_ident(fdecl)));
      pfmt_field(c, e, fdecl);
   }
   c->depth--;
   tb_append(c->tb, '}');
}

static bool pfmt_is_struct_arg(vlog_node_t arg)
{
   if (vlog_kind(arg) != V_REF && vlog_kind(arg) != V_MEMBER_REF
       && vlog_kind(arg) != V_BIT_SELECT)
      return false;

   if (!format_is_scalar(arg))
      return false;

   vlog_node_t type = vlog_get_type(arg);
   return type != NULL && (pfmt_is_unpacked_struct(type)
                           || pfmt_is_unpacked_union(type)
                           || pfmt_is_tagged_union(type));
}

static bool pfmt_is_packed_tagged_arg(vlog_node_t arg)
{
   if (vlog_kind(arg) != V_REF && vlog_kind(arg) != V_MEMBER_REF
       && vlog_kind(arg) != V_BIT_SELECT)
      return false;

   vlog_node_t type = vlog_get_type(arg);
   return pfmt_is_tagged_union(type) && (vlog_flags(type) & VLOG_F_PACKED);
}

static vlog_node_t pfmt_tagged_radix(vlog_node_t e, vlog_node_t type,
                                     const char *conv)
{
   // The name of the member that is set followed by its value in the same
   // radix as the conversion

   vlog_node_t chain = NULL;
   for (int i = vlog_decls(type) - 1; i >= 0; i--) {
      vlog_node_t fdecl = vlog_decl(type, i);

      LOCAL_TEXT_BUF tb = tb_new();
      tb_printf(tb, "%s:", istr(vlog_ident(fdecl)));

      vlog_node_t call = vlog_new(V_SYS_FCALL);
      vlog_set_ident(call, ident_new("$sformatf"));
      vlog_set_subkind(call, V_SYSTF_NONE);
      vlog_set_loc(call, vlog_loc(e));

      vlog_node_t fmt = vlog_new(V_STRING);
      vlog_set_loc(fmt, vlog_loc(e));
      vlog_add_param(call, fmt);

      if (!(vlog_flags(fdecl) & VLOG_F_VOID)) {
         tb_cat(tb, conv);

         vlog_node_t m = vlog_new(V_MEMBER_REF);
         vlog_set_value(m, e);
         vlog_set_ident(m, vlog_ident(fdecl));
         vlog_set_ref(m, fdecl);
         vlog_set_loc(m, vlog_loc(e));
         vlog_add_param(call, m);
      }

      vlog_set_number(fmt, number_from_string(tb_get(tb), tb_len(tb)));

      if (chain == NULL)
         chain = call;
      else {
         vlog_node_t pat = vlog_new(V_MATCH_PAT);
         vlog_set_subkind(pat, V_PAT_TAGGED);
         vlog_set_type(pat, type);
         vlog_set_ref(pat, fdecl);
         vlog_set_loc(pat, vlog_loc(e));

         vlog_node_t test = vlog_new(V_MATCHES);
         vlog_set_value(test, e);
         vlog_set_left(test, pat);
         vlog_set_loc(test, vlog_loc(e));

         vlog_node_t cond = vlog_new(V_COND_EXPR);
         vlog_set_value(cond, test);
         vlog_set_left(cond, call);
         vlog_set_right(cond, chain);
         vlog_set_loc(cond, vlog_loc(e));
         chain = cond;
      }
   }

   return chain;
}

static vlog_node_t simp_format_struct(vlog_node_t v, int fmt, const char *text)
{
   // The %p conversion of a structure is replaced by the conversions of
   // its members in assignment pattern format

   bool any = false;
   const int nparams = vlog_params(v);
   for (int i = fmt + 1; i < nparams && !any; i++)
      any = pfmt_is_struct_arg(vlog_param(v, i))
         || pfmt_is_packed_tagged_arg(vlog_param(v, i));

   if (!any)
      return v;

   pfmt_t c = { .tb = tb_new() };
   for (int i = 0; i <= fmt; i++)
      pfmt_push(&c, vlog_param(v, i));

   int arg = fmt + 1;
   bool changed = false;
   for (const char *p = text; *p; p++) {
      if (*p != '%') {
         tb_append(c.tb, *p);
         continue;
      }

      const char *start = p++;
      while (*p == '-' || *p == '0' || isdigit((unsigned char)*p)
             || *p == '.')
         p++;

      if (*p == '{') {
         while (*p != '}' && *p != '\0')
            p++;
         if (*p == '}')
            p++;
      }

      const char spec = *p;
      if (spec == '\0') {
         tb_cat(c.tb, start);
         break;
      }

      if (spec == '%' || spec == 'm' || spec == 'M' || spec == 'l'
          || spec == 'L') {
         tb_catn(c.tb, start, p - start + 1);
         continue;
      }

      const int this = arg++;
      if (this >= nparams) {
         tb_catn(c.tb, start, p - start + 1);
         continue;
      }

      vlog_node_t value = vlog_param(v, this);
      if ((spec == 'p' || spec == 'P')
          && (p - start == 1 || (p - start == 2 && start[1] == '0'))
          && pfmt_is_struct_arg(value)) {
         c.plain = p - start == 2;
         c.depth = 0;
         pfmt_leaf(&c, value, vlog_get_type(value));
         changed = true;
      }
      else if (strchr("dDhHxXbBoO", spec) != NULL
               && pfmt_is_packed_tagged_arg(value)) {
         LOCAL_TEXT_BUF conv = tb_new();
         if (p - start == 1 && (spec == 'd' || spec == 'D'))
            tb_cat(conv, "%0");   // Not padded to the width of the type
         tb_catn(conv, p - start == 1 && (spec == 'd' || spec == 'D')
                 ? p : start, p - start == 1 && (spec == 'd' || spec == 'D')
                 ? 1 : p - start + 1);
         tb_cat(c.tb, "%s");
         pfmt_push(&c, pfmt_tagged_radix(value, vlog_get_type(value),
                                         tb_get(conv)));
         changed = true;
      }
      else {
         tb_catn(c.tb, start, p - start + 1);
         pfmt_push(&c, value);
      }
   }

   for (int i = arg; i < nparams; i++)
      pfmt_push(&c, vlog_param(v, i));

   if (!changed) {
      free(c.args);
      tb_free(c.tb);
      return v;
   }

   vlog_node_t str = vlog_param(v, fmt);
   vlog_node_t copy = vlog_new(V_STRING);
   vlog_set_loc(copy, vlog_loc(str));
   vlog_set_number(copy, number_from_string(tb_get(c.tb), tb_len(c.tb)));
   c.args[fmt] = copy;

   vlog_node_t call = vlog_new(vlog_kind(v));
   vlog_set_ident(call, vlog_ident(v));
   vlog_set_subkind(call, vlog_subkind(v));
   vlog_set_ival(call, vlog_ival(v));
   vlog_set_loc(call, vlog_loc(v));
   vlog_set_flags(call, vlog_flags(v));

   for (int i = 0; i < c.nargs; i++)
      vlog_add_param(call, c.args[i]);

   free(c.args);
   tb_free(c.tb);
   return call;
}


static bool pfmt_array(vlog_node_t arg, bool *dynamic, int64_t *count,
                       int64_t *left, int64_t *step, vlog_node_t *key)
{
   // An unpacked array, dynamic array or queue passed to %p

   vlog_node_t base = arg;
   int consumed = 0;
   while (vlog_kind(base) == V_BIT_SELECT) {
      consumed += vlog_params(base);
      base = vlog_value(base);
   }

   if ((vlog_kind(base) != V_REF && vlog_kind(base) != V_MEMBER_REF)
       || !vlog_has_ref(base))
      return false;

   vlog_node_t decl = vlog_ref(base);
   switch (vlog_kind(decl)) {
   case V_VAR_DECL:
   case V_LOCAL_DECL:
   case V_NET_DECL:
   case V_PORT_DECL:
   case V_TF_PORT_DECL:
      break;
   default:
      return false;
   }

   const int ndims = vlog_ranges(decl);
   if (consumed < ndims) {
      vlog_node_t dim = vlog_range(decl, consumed);
      int64_t l, r;
      if (vlog_subkind(dim) != V_DIM_UNPACKED
          || !vlog_range_bound(vlog_left(dim), &l)
          || !vlog_range_bound(vlog_right(dim), &r))
         return false;

      *dynamic = false;
      *key = NULL;
      *count = (l > r ? l - r : r - l) + 1;
      *left = l;
      *step = l > r ? -1 : 1;
      return true;
   }

   vlog_node_t type = vlog_kind(arg) == V_BIT_SELECT
      ? vlog_select_type(arg) : vlog_get_type(arg);
   if (type == NULL || vlog_kind(type) != V_DATA_TYPE)
      return false;

   switch (vlog_subkind(type)) {
   case DT_QUEUE:
   case DT_DYNARRAY:
      *dynamic = true;
      *key = NULL;
      *count = 0;
      *left = 0;
      *step = 1;
      return true;
   case DT_ASSOC:
      {
         *key = vlog_assoc_key(type);
         if (*key == NULL)
            return false;

         switch (vlog_kind(*key)) {
         case V_ENUM_DECL:
            break;
         case V_DATA_TYPE:
            switch (vlog_subkind(*key)) {
            case DT_STRING:
               break;
            case DT_BIT:
            case DT_LOGIC:
            case DT_BYTE:
            case DT_SHORTINT:
            case DT_INT:
            case DT_LONGINT:
            case DT_INTEGER:
               if (vlog_size(*key) > 64)
                  return false;
               break;
            default:
               return false;
            }
            break;
         default:
            return false;
         }

         *dynamic = true;
         *count = 0;
         *left = 0;
         *step = 1;
         return true;
      }
   default:
      return false;
   }
}

static vlog_node_t pfmt_number(int64_t value, const loc_t *loc)
{
   vlog_node_t n = vlog_new(V_NUMBER);
   vlog_set_number(n, number_from_int(value));
   vlog_set_loc(n, loc);
   return n;
}

static vlog_node_t pfmt_string(const char *text, const loc_t *loc)
{
   vlog_node_t str = vlog_new(V_STRING);
   vlog_set_loc(str, loc);
   vlog_set_number(str, number_from_string(text, strlen(text)));
   return str;
}

static vlog_node_t pfmt_array_string(vlog_node_t v, vlog_node_t arg,
                                     bool dynamic, int64_t count,
                                     int64_t left, int64_t step, bool plain,
                                     vlog_node_t key)
{
   // The elements of the array are converted by a loop at run time

   const loc_t *loc = vlog_loc(arg);

   vlog_node_t dim = vlog_new(V_DIMENSION);
   vlog_set_subkind(dim, V_DIM_PACKED);
   vlog_set_left(dim, pfmt_number(31, loc));
   vlog_set_right(dim, pfmt_number(0, loc));

   vlog_node_t itype = vlog_new(V_DATA_TYPE);
   vlog_set_subkind(itype, DT_INT);
   vlog_add_range(itype, dim);
   vlog_set_loc(itype, loc);

   if (key != NULL)
      itype = key;

   vlog_node_t idx = vlog_new(V_VAR_DECL);
   vlog_set_ident(idx, ident_new("$pidx"));
   vlog_set_type(idx, itype);
   vlog_set_loc(idx, loc);

   vlog_node_t ref = vlog_new(V_REF);
   vlog_set_ident(ref, vlog_ident(idx));
   vlog_set_ref(ref, idx);
   vlog_set_loc(ref, loc);

   vlog_node_t sel = vlog_new(V_BIT_SELECT);
   if (vlog_kind(arg) == V_BIT_SELECT) {
      vlog_set_value(sel, vlog_value(arg));
      for (int i = 0; i < vlog_params(arg); i++)
         vlog_add_param(sel, vlog_param(arg, i));
   }
   else
      vlog_set_value(sel, arg);
   vlog_add_param(sel, ref);
   vlog_set_loc(sel, loc);

   vlog_node_t len;
   if (dynamic) {
      len = vlog_new(V_METHOD_CALL);
      vlog_set_loc(len, loc);
      vlog_set_value(len, arg);
      vlog_set_ident(len, ident_new(key != NULL ? "num" : "size"));
      vlog_resolve_method(len);
   }
   else
      len = pfmt_number(count, loc);

   // The conversion of an element is the same as that of the array
   // except that an element which is itself a string or array is in braces
   vlog_node_t fmt = vlog_new(V_STRING);
   vlog_set_loc(fmt, loc);
   vlog_set_number(fmt, plain ? number_from_string("%0p", 3)
                   : number_from_string("%p", 2));

   vlog_node_t elem = vlog_new(V_SYS_FCALL);
   vlog_set_ident(elem, ident_new("$sformatf"));
   vlog_set_subkind(elem, V_SYSTF_NONE);
   vlog_set_ival(elem, vlog_ival(v));
   vlog_set_loc(elem, loc);
   vlog_add_param(elem, fmt);
   vlog_add_param(elem, sel);

   vlog_node_t cls = vlog_get_type(sel);
   if (cls != NULL && vlog_kind(cls) == V_CLASS_DECL
       && !(vlog_flags(cls) & VLOG_F_INTERFACE)) {
      // A handle in a container is shown by class name and number
      vlog_node_t decl = vlog_class_member(cls, ident_new("__pn"));
      if (decl != NULL && vlog_kind(decl) == V_FUNC_DECL) {
         vlog_node_t call = vlog_new(V_USER_FCALL);
         vlog_set_ident(call, ident_new("__pn"));
         vlog_set_ref(call, decl);
         vlog_set_loc(call, loc);
         vlog_add_param(call, sel);
         vlog_set_param(elem, 1, call);
         vlog_set_number(fmt, number_from_string("%s", 2));
      }
   }

   vlog_node_t piece = simp_format_p(elem);
   if (key != NULL) {
      // Each element is the key and the value
      vlog_node_t kfmt = pfmt_string(plain ? "%0p" : "%p", loc);

      vlog_node_t kcall = vlog_new(V_SYS_FCALL);
      vlog_set_ident(kcall, ident_new("$sformatf"));
      vlog_set_subkind(kcall, V_SYSTF_NONE);
      vlog_set_ival(kcall, vlog_ival(v));
      vlog_set_loc(kcall, loc);
      vlog_add_param(kcall, kfmt);
      vlog_add_param(kcall, ref);

      vlog_node_t cat = vlog_new(V_CONCAT);
      vlog_set_loc(cat, loc);
      vlog_add_param(cat, simp_format_p(kcall));
      vlog_add_param(cat, pfmt_string(":", loc));
      vlog_add_param(cat, piece);
      piece = cat;
   }

   vlog_node_t call = vlog_new(V_SYS_FCALL);
   vlog_set_ident(call, ident_new("$__pfmt"));
   vlog_set_subkind(call, V_SYSTF_NONE);
   vlog_set_ival(call, vlog_ival(v));
   vlog_set_loc(call, loc);
   vlog_add_param(call, len);
   vlog_add_param(call, piece);
   vlog_add_param(call, ref);
   vlog_add_param(call, pfmt_number(left, loc));
   vlog_add_param(call, pfmt_number(step, loc));
   vlog_add_param(call, pfmt_number(plain, loc));
   if (key != NULL)
      vlog_add_param(call, arg);
   return call;
}

static bool format_is_handle(vlog_node_t arg)
{
   if (vlog_kind(arg) == V_NULL)
      return false;

   vlog_node_t type = vlog_get_type(arg);
   return type != NULL && vlog_kind(type) == V_CLASS_DECL
      && !(vlog_flags(type) & VLOG_F_INTERFACE);
}

static void format_handle_warning(vlog_node_t arg, char spec)
{
   // A class handle is not an integral value (8.4) so only %p is
   // defined for it

   diag_t *d = diag_new(DIAG_WARN, vlog_loc(arg));
   if (spec == 0)
      diag_printf(d, "class handle printed as an integer");
   else
      diag_printf(d, "class handle printed with %%%c", spec);
   diag_hint(d, vlog_loc(arg), "use %%p to print the object");
   diag_emit(d);
}

static vlog_node_t simp_format_handles(vlog_node_t v)
{
   // A class handle as the first argument of $display is printed as if
   // by %p and other uses of a handle as a number give a warning

   const int fmt = format_arg_index(v);
   const int nparams = vlog_params(v);
   if (fmt < 0 || fmt >= nparams)
      return v;

   const ident_t id = vlog_ident(v);
   const bool display = !vlog_is_sformatf(id)
      && id != ident_new("$sformat") && id != ident_new("$swrite")
      && id != ident_new("$error") && id != ident_new("$warning")
      && id != ident_new("$info") && id != ident_new("$fatal");

   if (display && format_is_handle(vlog_param(v, fmt))) {
      vlog_node_t first = vlog_param(v, fmt);

      vlog_node_t str = vlog_new(V_STRING);
      vlog_set_loc(str, vlog_loc(first));
      vlog_set_number(str, number_from_string("%p", 2));

      vlog_node_t call = vlog_new(vlog_kind(v));
      vlog_set_ident(call, vlog_ident(v));
      vlog_set_subkind(call, vlog_subkind(v));
      vlog_set_ival(call, vlog_ival(v));
      vlog_set_loc(call, vlog_loc(v));
      vlog_set_flags(call, vlog_flags(v));

      for (int i = 0; i < nparams; i++) {
         if (i == fmt)
            vlog_add_param(call, str);
         vlog_add_param(call, vlog_param(v, i));
      }

      v = call;
   }

   for (int i = fmt; i < vlog_params(v); i++) {
      vlog_node_t str = vlog_param(v, i);
      if (vlog_kind(str) != V_STRING) {
         if (display && format_is_handle(str))
            format_handle_warning(str, 0);
         continue;
      }

      number_t n = vlog_number(str);
      if (!number_is_defined(n))
         continue;

      const int len = number_width(n) / 8;
      char *text LOCAL = xmalloc(len + 1);
      for (int j = 0; j < len; j++)
         text[j] = number_byte(n, len - 1 - j);
      text[len] = '\0';

      for (const char *p = text; *p; p++) {
         if (*p != '%')
            continue;

         p++;
         while (*p == '-' || *p == '0' || isdigit((unsigned char)*p)
                || *p == '.')
            p++;

         if (*p == '{') {
            while (*p != '}' && *p != '\0')
               p++;
            if (*p == '}')
               p++;
         }

         const char spec = *p;
         if (spec == '\0')
            break;
         else if (strchr("%mMlL", spec) != NULL)
            continue;
         else if (++i >= vlog_params(v))
            break;

         if (strchr("dDhHxXbBoOsScC", spec) != NULL
             && format_is_handle(vlog_param(v, i)))
            format_handle_warning(vlog_param(v, i), spec);
      }

      if (!display)
         break;
   }

   return v;
}

static vlog_node_t simp_format_p(vlog_node_t v)
{
   // The %p conversion of a scalar prints the value as the assignment
   // pattern format does: integers in decimal, strings in quotes and
   // enumerations by name

   const int fmt = format_arg_index(v);
   if (fmt < 0 || fmt >= vlog_params(v))
      return v;

   vlog_node_t str = vlog_param(v, fmt);
   if (vlog_kind(str) != V_STRING)
      return v;

   number_t n = vlog_number(str);
   if (!number_is_defined(n))
      return v;

   const int len = number_width(n) / 8;
   char *text LOCAL = xmalloc(len + 1);
   for (int i = 0; i < len; i++)
      text[i] = number_byte(n, len - 1 - i);
   text[len] = '\0';

   vlog_node_t expanded = simp_format_struct(v, fmt, text);
   if (expanded == v && strpbrk(text, "pPsS") == NULL)
      return v;

   if (expanded != v)
      return simp_format_p(expanded);

   LOCAL_TEXT_BUF tb = tb_new();
   bool changed = false;
   int arg = fmt + 1;
   for (const char *p = text; *p; p++) {
      if (*p != '%') {
         tb_append(tb, *p);
         continue;
      }

      const char *start = p++;
      while (*p == '-' || *p == '0' || isdigit((unsigned char)*p)
             || *p == '.')
         p++;

      if (*p == '{') {
         while (*p != '}' && *p != '\0')
            p++;
         if (*p == '}')
            p++;
      }

      const char spec = *p;
      if (spec == '\0') {
         tb_cat(tb, start);
         break;
      }
      else if (spec == '%' || spec == 'm' || spec == 'M' || spec == 'l'
               || spec == 'L') {
         tb_catn(tb, start, p - start + 1);
         continue;
      }

      const int this = arg++;
      if (this >= vlog_params(v)) {
         tb_catn(tb, start, p - start + 1);
         continue;
      }

      vlog_node_t value = vlog_param(v, this);

      bool dynamic;
      int64_t count, left0, step0;
      vlog_node_t key;
      if ((spec == 'p' || spec == 'P')
          && (p - start == 1 || (p - start == 2 && start[1] == '0'))
          && pfmt_array(value, &dynamic, &count, &left0, &step0, &key)) {
         const bool plain = p - start == 2;
         vlog_set_param(v, this, pfmt_array_string(v, value, dynamic, count,
                                                   left0, step0, plain, key));
         tb_cat(tb, "%s");
         changed = true;
         continue;
      }

      if ((spec == 'p' || spec == 'P')
          && (p - start == 1 || (p - start == 2 && start[1] == '0'))) {
         vlog_node_t cls = vlog_get_type(value);
         if (cls != NULL && vlog_kind(cls) == V_CLASS_DECL
             && !(vlog_flags(cls) & VLOG_F_INTERFACE)
             && (vlog_kind(value) == V_REF || vlog_kind(value) == V_MEMBER_REF
                 || vlog_kind(value) == V_BIT_SELECT
                 || vlog_kind(value) == V_USER_FCALL)) {
            ident_t id = ident_new(p - start == 2 ? "__ps1" : "__ps0");
            vlog_node_t decl = vlog_class_member(cls, id);
            if (decl != NULL && vlog_kind(decl) == V_FUNC_DECL) {
               vlog_node_t call = vlog_new(V_USER_FCALL);
               vlog_set_ident(call, id);
               vlog_set_ref(call, decl);
               vlog_set_loc(call, vlog_loc(value));
               vlog_add_param(call, value);
               vlog_set_param(v, this, call);
               tb_cat(tb, "%s");
               changed = true;
               continue;
            }
         }
      }

      const char kind = format_p_kind(value);

      if (spec == 's' || spec == 'S') {
         // An enumeration is an integral value shown as 8-bit ASCII
         // codes like any other (21.2.1.8)
         if (kind == 's') {
            diag_t *d = diag_new(DIAG_WARN, vlog_loc(value));
            diag_printf(d, "%%s prints the value of an enumeration as "
                        "ASCII characters");
            diag_hint(d, vlog_loc(value), "use the name() method or %%p "
                      "to print the name of the value");
            diag_emit(d);
         }
         tb_catn(tb, start, p - start + 1);
         continue;
      }
      else if (spec != 'p' && spec != 'P') {
         tb_catn(tb, start, p - start + 1);
         continue;
      }

      // The field width and justification of the conversion
      const char *f = start + 1;
      const bool left = *f == '-';
      if (left)
         f++;
      if (*f == '0')
         f++;
      int width = 0;
      while (isdigit((unsigned char)*f))
         width = width * 10 + (*f++ - '0');
      const bool sized = width > 0;

      switch (kind) {
      case 'd':
         if (p - start > 1) {
            // The field is padded at run time
            tb_catn(tb, start, p - start + 1);
            continue;
         }
         tb_cat(tb, "%0d");
         break;
      case 'g':
         if (!sized)
            tb_cat(tb, "%g");
         else
            tb_printf(tb, left ? "%%-%dg" : "%%%dg", width);
         break;
      case 'q':
         // In double quotes with special characters escaped
         if (!sized)
            tb_cat(tb, "%\x02");
         else
            tb_printf(tb, left ? "%%-%d\x02" : "%%%d\x02", width);
         break;
      case 's':
         vlog_set_param(v, this, format_enum_name(v, value));
         if (!sized)
            tb_cat(tb, "%s");
         else
            tb_printf(tb, left ? "%%-%ds" : "%%%ds", width);
         break;
      default:
         tb_catn(tb, start, p - start + 1);
         continue;
      }

      changed = true;
   }

   if (!changed)
      return v;

   vlog_node_t copy = vlog_new(V_STRING);
   vlog_set_loc(copy, vlog_loc(str));
   vlog_set_number(copy, number_from_string(tb_get(tb), tb_len(tb)));
   vlog_set_param(v, fmt, copy);
   return v;
}

static bool pfmt_builtin_array_call(vlog_node_t e)
{
   // A method of an array or queue which returns a queue

   if (vlog_kind(e) != V_METHOD_CALL)
      return false;

   vlog_node_t type = vlog_get_type(e);
   return type != NULL && vlog_kind(type) == V_DATA_TYPE
      && (vlog_subkind(type) == DT_QUEUE || vlog_subkind(type) == DT_DYNARRAY);
}

static bool pfmt_fixed_slice(vlog_node_t e)
{
   // A range of the elements of an array declared with a fixed size

   if (vlog_subkind(e) != V_RANGE_CONST)
      return false;

   vlog_node_t base = vlog_value(e);
   if (vlog_kind(base) != V_REF || !vlog_has_ref(base))
      return false;

   vlog_node_t decl = vlog_ref(base);
   switch (vlog_kind(decl)) {
   case V_VAR_DECL:
   case V_LOCAL_DECL:
   case V_NET_DECL:
   case V_PORT_DECL:
      return vlog_ranges(decl) > 0
         && (vlog_subkind(vlog_range(decl, 0)) == V_DIM_UNPACKED
             || vlog_subkind(vlog_range(decl, 0)) == V_DIM_UNSIZED);
   default:
      return false;
   }
}

static bool pfmt_hoistable(vlog_node_t e)
{
   // A call returning an aggregate is evaluated once to a temporary as
   // %p shows it by element

   if (vlog_kind(e) != V_USER_FCALL && vlog_kind(e) != V_METHOD_CALL
       && vlog_kind(e) != V_PART_SELECT)
      return false;
   else if (vlog_kind(e) != V_PART_SELECT
            && (!vlog_has_ref(e) || vlog_kind(vlog_ref(e)) != V_FUNC_DECL
                || !vlog_has_type(vlog_ref(e)))
            && !pfmt_builtin_array_call(e))
      return false;

   if (vlog_kind(e) == V_PART_SELECT && pfmt_fixed_slice(e))
      return true;

   vlog_node_t type = vlog_get_type(e);
   if (vlog_kind(e) == V_PART_SELECT) {
      // Only a slice of a dynamic array or queue is an array
      if (type == NULL)
         type = vlog_get_type(vlog_value(e));
      if (type == NULL || vlog_kind(type) != V_DATA_TYPE
          || (vlog_subkind(type) != DT_QUEUE
              && vlog_subkind(type) != DT_DYNARRAY))
         return false;
   }

   if (type == NULL)
      return false;

   switch (vlog_kind(type)) {
   case V_STRUCT_DECL:
   case V_UNION_DECL:
      return true;
   case V_DATA_TYPE:
      switch (vlog_subkind(type)) {
      case DT_QUEUE:
      case DT_DYNARRAY:
      case DT_ASSOC:
         return true;
      default:
         break;
      }
      for (int i = 0; i < vlog_ranges(type); i++) {
         if (vlog_subkind(vlog_range(type, i)) == V_DIM_UNPACKED)
            return true;
      }
      return false;
   default:
      return false;
   }
}

static void hoist_format_calls(vlog_node_t blk, vlog_node_t e)
{
   switch (vlog_kind(e)) {
   case V_SYS_FCALL:
   case V_SYS_TCALL:
   case V_USER_FCALL:
   case V_METHOD_CALL:
   case V_CONCAT:
      break;
   case V_BINARY:
      hoist_format_calls(blk, vlog_left(e));
      hoist_format_calls(blk, vlog_right(e));
      return;
   default:
      return;
   }

   const int nparams = vlog_params(e);
   int fmt = -1;
   if (vlog_kind(e) == V_SYS_FCALL || vlog_kind(e) == V_SYS_TCALL) {
      fmt = format_arg_index(e);
      if (fmt >= 0 && (fmt >= nparams || vlog_kind(vlog_param(e, fmt)) != V_STRING))
         fmt = -1;
   }

   static unsigned count_tmp = 0;

   for (int i = 0; i < nparams; i++) {
      vlog_node_t p = vlog_param(e, i);
      if (fmt < 0 || i <= fmt || !pfmt_hoistable(p)) {
         hoist_format_calls(blk, p);
         continue;
      }

      hoist_format_calls(blk, p);

      vlog_node_t tmp = vlog_new(V_LOCAL_DECL);
      vlog_set_ident(tmp, ident_sprintf("$pf%u", count_tmp++));
      if (vlog_kind(p) != V_PART_SELECT && vlog_has_ref(p)
          && vlog_kind(vlog_ref(p)) == V_FUNC_DECL)
         vlog_set_type(tmp, vlog_type(vlog_ref(p)));
      else if (vlog_kind(p) == V_PART_SELECT && pfmt_fixed_slice(p)) {
         vlog_node_t decl = vlog_ref(vlog_value(p));
         vlog_set_type(tmp, vlog_type(decl));

         if (vlog_subkind(vlog_range(decl, 0)) == V_DIM_UNSIZED)
            vlog_add_range(tmp, vlog_range(decl, 0));
         else {
            vlog_node_t dim = vlog_new(V_DIMENSION);
            vlog_set_subkind(dim, V_DIM_UNPACKED);
            vlog_set_left(dim, vlog_left(p));
            vlog_set_right(dim, vlog_right(p));
            vlog_set_loc(dim, vlog_loc(p));
            vlog_add_range(tmp, dim);
         }

         for (int j = 1; j < vlog_ranges(decl); j++)
            vlog_add_range(tmp, vlog_range(decl, j));
      }
      else if (vlog_get_type(p) == NULL && vlog_kind(p) == V_PART_SELECT)
         vlog_set_type(tmp, vlog_get_type(vlog_value(p)));
      else
         vlog_set_type(tmp, vlog_get_type(p));
      vlog_set_loc(tmp, vlog_loc(p));
      vlog_add_decl(blk, tmp);

      vlog_node_t target = vlog_new(V_REF);
      vlog_set_ident(target, vlog_ident(tmp));
      vlog_set_ref(target, tmp);
      vlog_set_loc(target, vlog_loc(p));

      vlog_node_t set = vlog_new(V_BASSIGN);
      vlog_set_target(set, target);
      vlog_set_value(set, p);
      vlog_set_loc(set, vlog_loc(p));
      vlog_add_stmt(blk, set);

      vlog_node_t use = vlog_new(V_REF);
      vlog_set_ident(use, vlog_ident(tmp));
      vlog_set_ref(use, tmp);
      vlog_set_loc(use, vlog_loc(p));
      vlog_set_param(e, i, use);
   }
}

static vlog_node_t simp_hoist_cb(vlog_node_t v, void *context)
{
   switch (vlog_kind(v)) {
   case V_SYS_TCALL:
   case V_USER_TCALL:
   case V_BASSIGN:
      break;
   default:
      return v;
   }

   vlog_node_t blk = vlog_new(V_SEQ_BLOCK);
   vlog_set_loc(blk, vlog_loc(v));

   if (vlog_kind(v) == V_BASSIGN)
      hoist_format_calls(blk, vlog_value(v));
   else
      hoist_format_calls(blk, v);

   if (vlog_decls(blk) == 0)
      return v;

   vlog_add_stmt(blk, v);
   return blk;
}

static vlog_node_t simp_handle_name(vlog_node_t v)
{
   // The class name and object number of a handle, resolved once the
   // class of the handle is complete

   vlog_node_t arg = vlog_param(v, 0);
   vlog_node_t cls = vlog_get_type(arg);
   if (cls != NULL && vlog_kind(cls) == V_CLASS_DECL) {
      vlog_node_t decl = vlog_class_member(cls, ident_new("__pn"));
      if (decl != NULL && vlog_kind(decl) == V_FUNC_DECL) {
         vlog_node_t call = vlog_new(V_USER_FCALL);
         vlog_set_ident(call, ident_new("__pn"));
         vlog_set_ref(call, decl);
         vlog_set_loc(call, vlog_loc(v));
         vlog_add_param(call, arg);
         return call;
      }
   }

   return v;
}

static vlog_node_t vlog_simp_cb(vlog_node_t v, void *context)
{
   switch (vlog_kind(v)) {
   case V_NET_DECL:
      return simp_net_decl(v, context);
   case V_PORT_DECL:
      return simp_port_decl(v, context);
   case V_TIMING:
      return simp_timing(v);
   case V_ALWAYS:
      return simp_always(v);
   case V_ENUM_DECL:
      return simp_enum_decl(v);
   case V_CONCAT:
      return simp_concat(v);
   case V_WAIT:
      return simp_wait(v);
   case V_USER_FCALL:
   case V_USER_TCALL:
      return simp_call_args(v);
   case V_SYS_FCALL:
      if (vlog_ident(v) == ident_new("$__pn"))
         return simp_handle_name(v);
      // Fall-through
   case V_SYS_TCALL:
      return simp_format_p(simp_format_handles(v));
   default:
      return v;
   }
}

static vlog_node_t simp_memory_port_ref_cb(vlog_node_t v, void *context)
{
   // A port of a type with no representation as a signal is an ordinary
   // variable of the instance connected by a process so its name refers
   // to the variable

   if (vlog_kind(v) != V_REF || !vlog_has_ref(v))
      return v;

   vlog_node_t port = vlog_ref(v);
   if (vlog_kind(port) != V_PORT_DECL || !vlog_has_ref(port)
       || hset_contains(context, v))
      return v;

   // An input of event type may be replaced by an event expression in
   // each instance
   vlog_node_t type = vlog_get_type(vlog_type(port));
   if (vlog_subkind(port) == V_PORT_INPUT && type != NULL
       && vlog_kind(type) == V_DATA_TYPE && vlog_subkind(type) == DT_EVENT)
      return v;

   vlog_node_t d = vlog_ref(port);
   if (vlog_kind(d) == V_VAR_DECL && vlog_is_dynamic_type(vlog_type(d)))
      vlog_set_ref(v, d);

   return v;
}

void vlog_simp(vlog_node_t mod)
{
   vlog_rewrite(mod, NULL, simp_hoist_cb, mod);
   vlog_rewrite(mod, NULL, vlog_simp_cb, mod);

   // The formals of a checker may be replaced by their actuals
   if ((vlog_kind(mod) == V_MODULE && !(vlog_flags(mod) & VLOG_F_CHECKER))
       || vlog_kind(mod) == V_PROGRAM) {
      // The references in the list of ports stay references to the ports
      const int nports = vlog_ports(mod);
      hset_t *list = hset_new(MAX(nports, 1));
      for (int i = 0; i < nports; i++)
         hset_insert(list, vlog_port(mod, i));

      vlog_rewrite(mod, NULL, simp_memory_port_ref_cb, list);
      hset_free(list);
   }
}
