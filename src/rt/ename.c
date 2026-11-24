//
//  Copyright (C) 2024-2026  Nick Gasson
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
#include "diag.h"
#include "hash.h"
#include "ident.h"
#include "jit/jit-exits.h"
#include "jit/jit.h"
#include "lib.h"
#include "printf.h"
#include "rt/island.h"
#include "rt/model.h"
#include "rt/mspace.h"
#include "rt/structs.h"
#include "tree.h"
#include "type.h"
#include "vlog/vlog-node.h"
#include "vlog/vlog-number.h"
#include "vlog/vlog-phase.h"
#include "vlog/vlog-util.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

static bool is_verilog_block(tree_t block)
{
   if (tree_kind(block) != T_BLOCK || tree_decls(block) == 0)
      return false;

   tree_t hier = tree_decl(block, 0);
   return tree_kind(hier) == T_HIER && tree_subkind(hier) == T_VERILOG;
}

static bool is_vhdl_shim_block(tree_t block)
{
   if (!is_verilog_block(block) || tree_stmts(block) != 1)
      return false;

   tree_t child = tree_stmt(block, 0);
   if (tree_kind(child) != T_BLOCK || tree_decls(child) == 0)
      return false;

   tree_t hier = tree_decl(child, 0);
   return tree_kind(hier) == T_HIER && tree_subkind(hier) == T_ARCH;
}

static tree_t select_name(tree_t where, ident_t id)
{
   const bool ci = is_verilog_block(where);

   const int ndecls = tree_decls(where);
   for (int i = 0; i < ndecls; i++) {
      tree_t d = tree_decl(where, i);
      if (tree_kind(d) == T_VERILOG) {
         vlog_node_t vd = tree_vlog(d);
         if (vlog_kind(vd) == V_PORT_MAP || vlog_kind(vd) == V_IFACE_PORT)
            continue;
      }
      if (ci ? ident_casecmp(tree_ident(d), id) : tree_ident(d) == id)
         return d;
   }

   const int ngenerics = tree_generics(where);
   for (int i = 0; i < ngenerics; i++) {
      tree_t g = tree_generic(where, i);
      if (ci ? ident_casecmp(tree_ident(g), id) : tree_ident(g) == id)
         return g;
   }

   switch (tree_kind(where)) {
   case T_PACKAGE:
      return select_name(body_of(where), id);
   case T_PACK_BODY:
      return NULL;
   default:
      break;
   }

   const int nstmts = tree_stmts(where);
   for (int i = 0; i < nstmts; i++) {
      tree_t s = tree_stmt(where, i);
      if (ci ? ident_casecmp(tree_ident(s), id) : tree_ident(s) == id)
         return s;
   }

   const int nports = tree_ports(where);
   for (int i = 0; i < nports; i++) {
      tree_t p = tree_port(where, i);
      if (ci ? ident_casecmp(tree_ident(p), id) : tree_ident(p) == id)
         return p;
   }

   return NULL;
}

static bool is_implicit_block(tree_t block)
{
   if (tree_kind(block) != T_BLOCK)
      return false;

   tree_t hier = tree_decl(block, 0);
   assert(tree_kind(hier) == T_HIER);

   return tree_subkind(hier) == T_COMPONENT;
}

static vlog_node_t unwrap_verilog_type(vlog_node_t t)
{
   while (t != NULL) {
      if (vlog_kind(t) == V_TYPE_DECL)
         t = vlog_type(t);
      else if (vlog_kind(t) == V_REF && vlog_has_ref(t))
         t = vlog_ref(t);
      else
         break;
   }
   return t;
}

static const char *verilog_dynamic_kind_str(vlog_node_t vtype)
{
   vlog_node_t ut = unwrap_verilog_type(vtype);
   if (ut == NULL)
      return "dynamic object";

   switch (vlog_kind(ut)) {
   case V_CLASS_DECL:
      return "class handle";
   case V_DATA_TYPE:
      switch (vlog_subkind(ut)) {
      case DT_STRING:    return "string";
      case DT_DYNARRAY:  return "dynamic array";
      case DT_QUEUE:     return "queue";
      case DT_ASSOC:     return "associative array";
      case DT_EVENT:     return "event";
      case DT_CHANDLE:   return "chandle";
      case DT_MAILBOX:   return "mailbox";
      case DT_SEMAPHORE: return "semaphore";
      case DT_PROCESS:   return "process handle";
      case DT_VIRTUAL:   return "virtual interface";
      default:           return "dynamic object";
      }
   default:
      return "dynamic object";
   }
}

static void fmt_verilog_type(vlog_node_t vd, uint32_t width, bool is_net,
                             text_buf_t *tb)
{
   vlog_node_t vtype = vlog_has_type(vd) ? vlog_type(vd) : NULL;
   vlog_node_t ut = unwrap_verilog_type(vtype);

   if (ut == NULL) {
      tb_printf(tb, "%s", is_net ? "wire" : "logic");
      if (width > 1)
         tb_printf(tb, " [%u:0]", width - 1);
      return;
   }

   switch (vlog_kind(ut)) {
   case V_CLASS_DECL:
      tb_printf(tb, "class %s", istr(vlog_ident(ut)));
      return;
   case V_STRUCT_DECL:
      tb_printf(tb, "%s struct",
                (vlog_flags(ut) & VLOG_F_PACKED) ? "packed" : "unpacked");
      if ((vlog_flags(ut) & VLOG_F_PACKED) && width > 1)
         tb_printf(tb, " [%u:0]", width - 1);
      return;
   case V_UNION_DECL:
      tb_printf(tb, "%s union",
                (vlog_flags(ut) & VLOG_F_PACKED) ? "packed" : "unpacked");
      if ((vlog_flags(ut) & VLOG_F_PACKED) && width > 1)
         tb_printf(tb, " [%u:0]", width - 1);
      return;
   case V_ENUM_DECL:
      tb_printf(tb, "enum");
      if (width > 1)
         tb_printf(tb, " [%u:0]", width - 1);
      return;
   case V_DATA_TYPE:
      switch (vlog_subkind(ut)) {
      case DT_REAL:      tb_cat(tb, "real"); return;
      case DT_SHORTREAL: tb_cat(tb, "shortreal"); return;
      case DT_REALTIME:  tb_cat(tb, "realtime"); return;
      case DT_INT:       tb_cat(tb, "int"); return;
      case DT_INTEGER:   tb_cat(tb, "integer"); return;
      case DT_SHORTINT:  tb_cat(tb, "shortint"); return;
      case DT_LONGINT:   tb_cat(tb, "longint"); return;
      case DT_BYTE:      tb_cat(tb, "byte"); return;
      case DT_TIME:      tb_cat(tb, "time"); return;
      case DT_STRING:    tb_cat(tb, "string"); return;
      case DT_DYNARRAY:  tb_cat(tb, "dynamic array"); return;
      case DT_QUEUE:     tb_cat(tb, "queue"); return;
      case DT_ASSOC:     tb_cat(tb, "associative array"); return;
      case DT_EVENT:     tb_cat(tb, "event"); return;
      case DT_CHANDLE:   tb_cat(tb, "chandle"); return;
      case DT_BIT:
         tb_cat(tb, "bit");
         if (width > 1)
            tb_printf(tb, " [%u:0]", width - 1);
         return;
      default:
         tb_cat(tb, is_net ? "wire" : "logic");
         if (width > 1)
            tb_printf(tb, " [%u:0]", width - 1);
         return;
      }
   default:
      tb_cat(tb, is_net ? "wire" : "logic");
      if (width > 1)
         tb_printf(tb, " [%u:0]", width - 1);
      return;
   }
}

static uint8_t vhdl_int_byte_size(type_t type)
{
   type_t base = type_base_recur(type);
   tree_t r = type_dim(base, 0);
   int64_t low, high;
   if (!folded_bounds(r, &low, &high))
      return 8;

   switch (bits_for_range(low, high)) {
   case 1:
   case 8:  return 1;
   case 16: return 2;
   case 32: return 4;
   default: return 8;
   }
}

static void *shared_param_thunk_cb(jit_scalar_t *args, void *ctx)
{
   int64_t *value = ctx;
   if (args[1].integer != 0)
      return NULL;   // Has X or Z bits

   *value = args[0].integer;
   return value;
}

static void write_vhdl_int_slot(void *slot, uint8_t vsize, int64_t ival)
{
   switch (vsize) {
   case 1: *(int8_t *)slot = (int8_t)ival; break;
   case 2: *(int16_t *)slot = (int16_t)ival; break;
   case 4: *(int32_t *)slot = (int32_t)ival; break;
   case 8: *(int64_t *)slot = ival; break;
   default: should_not_reach_here();
   }
}

static void bind_verilog_constant(tree_t name, vlog_node_t vd,
                                  vlog_node_t body, void *ctx,
                                  jit_t *j, jit_scalar_t *args)
{
   ident_t pname = vlog_ident(vd);

   if (!vlog_has_value(vd))
      jit_msg(tree_loc(name), DIAG_FATAL, "sorry, VHDL external constant "
              "referencing Verilog parameter %s without a value is not "
              "supported", istr(pname));

   vlog_node_t vtype = vlog_has_type(vd) ? vlog_type(vd) : NULL;
   vlog_node_t utype = unwrap_verilog_type(vtype);

   if (vlog_is_fixed_unpacked(vd))
      jit_msg(tree_loc(name), DIAG_FATAL, "sorry, VHDL external constant "
              "referencing Verilog unpacked array parameter %s is not "
              "supported", istr(pname));

   if (utype != NULL
       && (vlog_kind(utype) == V_STRUCT_DECL || vlog_kind(utype) == V_UNION_DECL)
       && !(vlog_flags(utype) & VLOG_F_PACKED))
      jit_msg(tree_loc(name), DIAG_FATAL, "sorry, VHDL external constant "
              "referencing Verilog unpacked %s parameter %s is not supported",
              vlog_kind(utype) == V_STRUCT_DECL ? "struct" : "union",
              istr(pname));

   if (vtype != NULL && vlog_is_dynamic_type(vtype)
       && (utype == NULL || vlog_kind(utype) != V_DATA_TYPE
           || vlog_subkind(utype) != DT_STRING))
      jit_msg(tree_loc(name), DIAG_FATAL, "sorry, VHDL external constant "
              "referencing Verilog %s parameter %s is not supported",
              verilog_dynamic_kind_str(vtype), istr(pname));

   vlog_node_t val = vlog_value(vd);
   type_t expect = tree_type(name);

   // Case 1: string literal / string parameter
   if (vlog_kind(val) == V_STRING) {
      const bool expect_str = type_is_array(expect)
         && dimension_of(expect) == 1
         && type_eq(type_elem(expect), type_elem(std_type(NULL, STD_STRING)));
      if (!expect_str)
         jit_msg(tree_loc(name), DIAG_FATAL, "type of Verilog parameter %s is "
                 "string but external name has type %pT",
                 istr(pname), expect);

      number_t num = vlog_number(val);
      const uint32_t slen = number_width(num) / 8;

      int64_t left = 1;
      int64_t len_dir = (int64_t)slen;
      if (type_const_bounds(expect)) {
         tree_t r = range_of(expect, 0);
         const range_kind_t dir = tree_subkind(r);
         left = assume_int(tree_left(r));
         const int64_t right = assume_int(tree_right(r));
         const int64_t elen =
            MAX(0, dir == RANGE_TO ? right - left + 1 : left - right + 1);
         if (elen != slen)
            jit_msg(tree_loc(name), DIAG_FATAL, "type of Verilog parameter %s "
                    "is string (length %u) but external name has type %pT "
                    "(length %"PRIi64")", istr(pname), slen, expect, elen);
         len_dir = (dir == RANGE_TO) ? elen : ~elen;
      }

      uint8_t *buf = jit_mspace_alloc(MAX(slen, 1));
      for (uint32_t i = 0; i < slen; i++)
         buf[i] = number_byte(num, slen - 1 - i);

      jit_scalar_t *slots = jit_mspace_alloc(3 * sizeof(jit_scalar_t));
      slots[0].pointer = buf;
      slots[1].integer = left;
      slots[2].integer = len_dir;
      args[0].pointer = slots;
      return;
   }

   // Case 2: real parameter
   bool is_real_val = false;
   double dval = 0.0;
   if (vlog_kind(val) == V_REAL) {
      is_real_val = true;
      dval = vlog_dval(val);
   }
   else if (vlog_kind(val) == V_UNARY
            && vlog_kind(vlog_value(val)) == V_REAL
            && (vlog_subkind(val) == V_UNARY_NEG
                || vlog_subkind(val) == V_UNARY_IDENTITY)) {
      is_real_val = true;
      dval = vlog_dval(vlog_value(val));
      if (vlog_subkind(val) == V_UNARY_NEG)
         dval = -dval;
   }
   else if (vtype != NULL && vlog_is_real(vtype) && vlog_kind(val) == V_NUMBER
            && number_is_defined(vlog_number(val))) {
      is_real_val = true;
      dval = (double)number_integer(vlog_number(val));
   }

   if (is_real_val) {
      if (!type_is_real(expect))
         jit_msg(tree_loc(name), DIAG_FATAL, "type of Verilog parameter %s is "
                 "real but external name has type %pT", istr(pname), expect);

      double *slot = jit_mspace_alloc(sizeof(double));
      *slot = dval;
      args[0].pointer = slot;
      return;
   }

   // Case 3: integral / logic parameter (including genvar-dependent
   // localparams inside shared Verilog generate blocks)
   bool has_num = false, has_int = false;
   number_t num = { .bits = 0 };
   int64_t ival = 0;

   if (vlog_kind(val) == V_NUMBER) {
      num = vlog_number(val);
      has_num = true;
      if (number_is_defined(num) && number_width(num) <= 64) {
         ival = number_integer(num);
         has_int = true;
      }
   }
   else if (vlog_get_const(val, &ival)) {
      num = number_from_int(ival);
      has_num = true;
      has_int = true;
   }
   else if (body != NULL && (vlog_flags(body) & VLOG_F_GEN_SHARED)
            && ctx != NULL) {
      const int w = vlog_width(vd);
      if (w > 0 && w <= 64) {
         mir_unit_t *thunk = vlog_lower_thunk(jit_get_mir(j),
                                              vlog_ident(body), val);
         if (jit_call_thunk2(j, thunk, ctx, shared_param_thunk_cb, &ival)
             != NULL) {
            num = number_from_int(ival);
            has_num = true;
            has_int = true;
         }
      }
   }

   if (!has_num)
      jit_msg(tree_loc(name), DIAG_FATAL, "sorry, VHDL external constant "
              "referencing non-constant Verilog parameter %s is not supported",
              istr(pname));

   const uint32_t decl_width = (vtype != NULL && vlog_kind(vtype) == V_DATA_TYPE
                                && vlog_subkind(vtype) != DT_IMPLICIT)
      ? vlog_size(vtype) : 0;
   const uint32_t pwidth = decl_width > 0 ? decl_width : number_width(num);

   LOCAL_TEXT_BUF vtb = tb_new();
   fmt_verilog_type(vd, pwidth, false, vtb);

   if (type_is_integer(expect)) {
      if (!has_int)
         jit_msg(tree_loc(name), DIAG_FATAL, "type of Verilog parameter %s is "
                 "%s but external name has type %pT",
                 istr(pname), tb_get(vtb), expect);

      if (type_const_bounds(expect)) {
         tree_t r = range_of(expect, 0);
         int64_t low, high;
         if (folded_bounds(r, &low, &high) && (ival < low || ival > high))
            jit_msg(tree_loc(name), DIAG_FATAL, "value %"PRIi64" of Verilog "
                    "parameter %s is outside range of %pT (%"PRIi64" to "
                    "%"PRIi64")", ival, istr(pname), expect, low, high);
      }

      void *slot = jit_mspace_alloc(8);
      write_vhdl_int_slot(slot, vhdl_int_byte_size(expect), ival);
      args[0].pointer = slot;
      return;
   }

   const bool is_arr = type_is_array(expect);
   type_t elem = is_arr ? type_elem(expect) : expect;
   const bool vhdl_is_bool = !is_arr
      && type_eq(expect, std_type(NULL, STD_BOOLEAN));
   const bool vhdl_is_bit = vhdl_is_bool
      || type_eq(elem, std_type(NULL, STD_BIT));
   const bool vhdl_is_sl = type_eq(elem, ieee_type(IEEE_STD_ULOGIC));

   if ((!vhdl_is_bit && !vhdl_is_sl) || (is_arr && dimension_of(expect) != 1))
      jit_msg(tree_loc(name), DIAG_FATAL, "type of Verilog parameter %s is %s "
              "but external name has type %pT",
              istr(pname), tb_get(vtb), expect);

   if (!is_arr) {
      if (vhdl_is_bool) {
         if (!has_int || (ival != 0 && ival != 1))
            jit_msg(tree_loc(name), DIAG_FATAL, "type of Verilog parameter %s "
                    "is %s but external name has type %pT",
                    istr(pname), tb_get(vtb), expect);
         uint8_t *slot = jit_mspace_alloc(8);
         *slot = ival ? 1 : 0;
         args[0].pointer = slot;
         return;
      }

      if (pwidth != 1)
         jit_msg(tree_loc(name), DIAG_FATAL, "type of Verilog parameter %s is "
                 "%s but external name has scalar type %pT",
                 istr(pname), tb_get(vtb), expect);

      const vlog_logic_t b = number_bit(num, 0);
      static const uint8_t sl_map[4] = { 2, 3, 4, 1 };
      uint8_t *slot = jit_mspace_alloc(8);
      *slot = vhdl_is_bit ? (b == LOGIC_1 ? 1 : 0) : sl_map[b & 3];
      args[0].pointer = slot;
      return;
   }

   int64_t left = (int64_t)pwidth - 1;
   int64_t len_dir = ~(int64_t)pwidth;
   if (type_const_bounds(expect)) {
      tree_t r = range_of(expect, 0);
      const range_kind_t dir = tree_subkind(r);
      left = assume_int(tree_left(r));
      const int64_t right = assume_int(tree_right(r));
      const int64_t elen =
         MAX(0, dir == RANGE_TO ? right - left + 1 : left - right + 1);
      if (elen != pwidth)
         jit_msg(tree_loc(name), DIAG_FATAL, "type of Verilog parameter %s is "
                 "%s (width %u) but external name has type %pT (length "
                 "%"PRIi64")", istr(pname), tb_get(vtb), pwidth, expect, elen);
      len_dir = (dir == RANGE_TO) ? elen : ~elen;
   }

   static const uint8_t sl_map[4] = { 2, 3, 4, 1 };
   const uint32_t nw = number_width(num);
   uint8_t *data = jit_mspace_alloc(MAX(pwidth, 1));
   for (uint32_t i = 0; i < pwidth; i++) {
      const uint32_t bit_idx = pwidth - 1 - i;
      const vlog_logic_t b =
         (bit_idx < nw) ? number_bit(num, bit_idx) : LOGIC_0;
      data[i] = vhdl_is_bit ? (b == LOGIC_1 ? 1 : 0) : sl_map[b & 3];
   }

   jit_scalar_t *slots = jit_mspace_alloc(3 * sizeof(jit_scalar_t));
   slots[0].pointer = data;
   slots[1].integer = left;
   slots[2].integer = len_dir;
   args[0].pointer = slots;
}

static void bind_verilog_external_name(tree_t name, tree_t where,
                                       tree_t container, jit_scalar_t *args)
{
   if (tree_class(name) == C_VARIABLE)
      jit_msg(tree_loc(name), DIAG_FATAL, "sorry, VHDL external variable "
              "referencing Verilog object %s is not supported",
              istr(tree_ident(where)));

   tree_t h = tree_decl(container, 0);
   assert(tree_kind(h) == T_HIER);

   rt_scope_t *rts = find_scope(get_model(), container);
   assert(rts != NULL);

   void *ctx = *mptr_get(rts->privdata);
   if (ctx == NULL)
      jit_msg(tree_loc(name), DIAG_FATAL, "%pI has not yet been elaborated",
              tree_ident(container));

   jit_t *j = jit_for_thread();
   vlog_node_t body = tree_has_ref(h) && tree_kind(tree_ref(h)) == T_VERILOG
      ? tree_vlog(tree_ref(h)) : NULL;

   vlog_node_t vd = NULL;
   ident_t var_name = NULL;
   bool sv_is_net = false;
   if (tree_kind(where) == T_GENERIC_DECL) {
      if (body != NULL) {
         const int ndecls = vlog_decls(body);
         for (int i = 0; i < ndecls; i++) {
            vlog_node_t d = vlog_decl(body, i);
            if ((vlog_kind(d) == V_PARAM_DECL || vlog_kind(d) == V_LOCALPARAM)
                && ident_casecmp(vlog_ident(d), tree_ident(where))) {
               vd = d;
               break;
            }
         }
      }
      if (vd == NULL)
         jit_msg(tree_loc(name), DIAG_FATAL, "Verilog parameter %s not found",
                 istr(tree_ident(where)));
   }
   else if (tree_kind(where) == T_VERILOG) {
      vd = tree_vlog(where);
   }
   else {
      assert(tree_kind(where) == T_PORT_DECL);
      const int nports = body != NULL ? vlog_ports(body) : 0;
      for (int i = 0; i < nports; i++) {
         vlog_node_t p = vlog_ref(vlog_port(body, i));
         if (ident_casecmp(vlog_ident(p), tree_ident(where))) {
            vd = vlog_ref(p);
            break;
         }
      }
      if (vd == NULL)
         jit_msg(tree_loc(name), DIAG_FATAL, "Verilog port %s not found",
                 istr(tree_ident(where)));
   }

   if (tree_class(name) == C_CONSTANT) {
      if (vlog_kind(vd) != V_PARAM_DECL && vlog_kind(vd) != V_LOCALPARAM)
         jit_msg(tree_loc(name), DIAG_FATAL, "class of Verilog object %s is "
                 "not constant", istr(tree_ident(where)));

      bind_verilog_constant(name, vd, body, ctx, j, args);
      return;
   }

   if (tree_class(name) != C_SIGNAL)
      jit_msg(tree_loc(name), DIAG_FATAL, "external name referencing Verilog "
              "object %s must have class signal or constant",
              istr(tree_ident(where)));

   switch (vlog_kind(vd)) {
   case V_VAR_DECL:
   case V_NET_DECL:
      var_name = vlog_ident(vd);
      sv_is_net = (vlog_kind(vd) == V_NET_DECL);
      break;
   case V_PARAM_DECL:
   case V_LOCALPARAM:
      jit_msg(tree_loc(name), DIAG_FATAL, "class of Verilog parameter %s is "
              "constant, not signal", istr(tree_ident(where)));
   case V_CLASS_DECL:
      jit_msg(tree_loc(name), DIAG_FATAL, "sorry, VHDL external name "
              "referencing Verilog class %s is not supported",
              istr(tree_ident(where)));
   case V_FUNC_DECL:
   case V_TASK_DECL:
      jit_msg(tree_loc(name), DIAG_FATAL, "sorry, VHDL external name "
              "referencing Verilog subprogram %s is not supported",
              istr(tree_ident(where)));
   default:
      jit_msg(tree_loc(name), DIAG_FATAL, "sorry, VHDL external name "
              "referencing Verilog object %s is not supported",
              istr(tree_ident(where)));
   }

   jit_handle_t handle = jit_compile(j, tree_ident2(h));

   // Guard non-signal Verilog representations before dereferencing the
   // instance frame slot as a sig_shared_t *.
   vlog_node_t vtype = vlog_has_type(vd) ? vlog_type(vd) : NULL;
   vlog_node_t utype = unwrap_verilog_type(vtype);
   if (vtype != NULL && vlog_is_dynamic_type(vtype))
      jit_msg(tree_loc(name), DIAG_FATAL, "sorry, VHDL external name "
              "referencing Verilog %s %s is not supported",
              verilog_dynamic_kind_str(vtype), istr(var_name));

   if (vlog_is_fixed_unpacked(vd))
      jit_msg(tree_loc(name), DIAG_FATAL, "sorry, VHDL external name "
              "referencing Verilog unpacked array %s is not supported",
              istr(var_name));

   if (utype != NULL
       && (vlog_kind(utype) == V_STRUCT_DECL || vlog_kind(utype) == V_UNION_DECL)
       && !(vlog_flags(utype) & VLOG_F_PACKED))
      jit_msg(tree_loc(name), DIAG_FATAL, "sorry, VHDL external name "
              "referencing Verilog unpacked %s %s is not supported",
              vlog_kind(utype) == V_STRUCT_DECL ? "struct" : "union",
              istr(var_name));

   void *ptr = jit_get_frame_var(j, handle, ctx, var_name);
   sig_shared_t *sv_ss = *(sig_shared_t **)ptr;
   rt_signal_t *sv_sig = container_of(sv_ss, rt_signal_t, shared);

   if (unlikely(sv_sig->shared.flags & SIG_F_ISLAND))
      rt_island_on_observer_attached(get_model(), sv_sig);

   const uint32_t sv_width = signal_width(sv_sig);
   const bool sv_is_packed = !!(sv_sig->shared.flags & SIG_F_PACKED);
   const bool sv_is_real = !sv_is_packed
      && ((vtype != NULL && vlog_is_real(vtype))
          || (sv_sig->nexus.size == 8 && sv_width == 1));

   LOCAL_TEXT_BUF vtb = tb_new();
   fmt_verilog_type(vd, sv_width, sv_is_net, vtb);

   type_t expect = tree_type(name);

   if (type_is_real(expect)) {
      if (!sv_is_real || sv_sig->nexus.size != 8 || sv_width != 1)
         jit_msg(tree_loc(name), DIAG_FATAL, "type of Verilog signal %s is %s "
                 "but external name has type %pT",
                 istr(var_name), tb_get(vtb), expect);

      sig_shared_t *proxy = model_mixed_signal_proxy(get_model(), sv_sig,
                                                     sv_is_net,
                                                     MIXED_BRIDGE_REAL,
                                                     false, sizeof(double),
                                                     where);
      jit_scalar_t *slots = jit_mspace_alloc(2 * sizeof(jit_scalar_t));
      slots[0].pointer = proxy;
      slots[1].integer = 0;
      args[0].pointer = slots;
      return;
   }

   if (sv_is_real || (!sv_is_packed && sv_sig->nexus.size != 1))
      jit_msg(tree_loc(name), DIAG_FATAL, "type of Verilog signal %s is %s "
              "but external name has type %pT",
              istr(var_name), tb_get(vtb), expect);

   if (type_is_integer(expect)) {
      if (sv_width == 0 || sv_width > 64)
         jit_msg(tree_loc(name), DIAG_FATAL, "type of Verilog signal %s is %s "
                 "but external name has type %pT",
                 istr(var_name), tb_get(vtb), expect);

      const bool sv_signed = vtype != NULL && vlog_is_signed(vtype);
      const uint8_t vsize = vhdl_int_byte_size(expect);
      sig_shared_t *proxy = model_mixed_signal_proxy(get_model(), sv_sig,
                                                     sv_is_net,
                                                     MIXED_BRIDGE_INT,
                                                     sv_signed, vsize, where);
      jit_scalar_t *slots = jit_mspace_alloc(2 * sizeof(jit_scalar_t));
      slots[0].pointer = proxy;
      slots[1].integer = 0;
      args[0].pointer = slots;
      return;
   }

   const bool is_arr = type_is_array(expect);
   type_t elem = is_arr ? type_elem(expect) : expect;
   const bool vhdl_is_bool = !is_arr && type_eq(expect, std_type(NULL, STD_BOOLEAN));
   const bool vhdl_is_bit = vhdl_is_bool || type_eq(elem, std_type(NULL, STD_BIT));
   const bool vhdl_is_sl = type_eq(elem, ieee_type(IEEE_STD_ULOGIC));
   if ((!vhdl_is_bit && !vhdl_is_sl) || (is_arr && dimension_of(expect) != 1))
      jit_msg(tree_loc(name), DIAG_FATAL, "type of Verilog signal %s is %s "
              "but external name has type %pT",
              istr(var_name), tb_get(vtb), expect);

   if (!is_arr) {
      if (sv_width != 1)
         jit_msg(tree_loc(name), DIAG_FATAL, "type of Verilog signal %s is %s "
                 "but external name has scalar type %pT",
                 istr(var_name), tb_get(vtb), expect);
   }
   else if (type_const_bounds(expect)) {
      tree_t r = range_of(expect, 0);
      const range_kind_t dir = tree_subkind(r);
      const int64_t left = assume_int(tree_left(r));
      const int64_t right = assume_int(tree_right(r));
      const int64_t length =
         MAX(0, dir == RANGE_TO ? right - left + 1 : left - right + 1);
      if (length != sv_width)
         jit_msg(tree_loc(name), DIAG_FATAL, "type of Verilog signal %s is %s "
                 "(width %u) but external name has type %pT (length %"PRIi64")",
                 istr(var_name), tb_get(vtb), sv_width, expect, length);
   }

   const mixed_bridge_kind_t kind =
      vhdl_is_bit ? MIXED_BRIDGE_BIT : MIXED_BRIDGE_LOGIC;
   sig_shared_t *proxy = model_mixed_signal_proxy(get_model(), sv_sig,
                                                  sv_is_net, kind, false, 1,
                                                  where);

   if (is_arr) {
      jit_scalar_t *slots = jit_mspace_alloc(4 * sizeof(jit_scalar_t));
      slots[0].pointer = proxy;
      slots[1].integer = 0;

      if (type_const_bounds(expect)) {
         tree_t r = range_of(expect, 0);
         const range_kind_t dir = tree_subkind(r);
         const int64_t left = assume_int(tree_left(r));
         const int64_t right = assume_int(tree_right(r));
         const int64_t length =
            MAX(0, dir == RANGE_TO ? right - left + 1 : left - right + 1);
         slots[2].integer = left;
         slots[3].integer = dir == RANGE_TO ? length : ~length;
      }
      else {
         slots[2].integer = (int64_t)sv_width - 1;
         slots[3].integer = ~(int64_t)sv_width;
      }

      args[0].pointer = slots;
   }
   else {
      jit_scalar_t *slots = jit_mspace_alloc(2 * sizeof(jit_scalar_t));
      slots[0].pointer = proxy;
      slots[1].integer = 0;
      args[0].pointer = slots;
   }
}

static void bind_external_name(tree_t name, jit_handle_t scope,
                               jit_scalar_t *args)
{
   assert(tree_kind(name) == T_EXTERNAL_NAME);

   jit_t *j = jit_for_thread();
   tree_t where = tree_from_object(jit_get_object(j, scope));
   tree_t container = NULL, next = NULL;
   ident_t path = jit_get_name(j, scope);
   int next_arg = 2;

   // When a VHDL entity is instantiated inside a shared Verilog generate
   // loop, all iterations share one JIT unit so jit_get_object(j, scope)
   // refers to iteration 0; use the running instance scope for relative
   // pathnames when it matches this JIT unit.
   rt_model_t *m = get_model_or_null();
   if (m != NULL && tree_parts(name) > 0
       && tree_subkind(tree_part(name, 0)) == PE_RELATIVE) {
      rt_scope_t *active = get_active_scope(m);
      if (active != NULL && active->kind == SCOPE_INSTANCE
          && tree_kind(active->where) == T_BLOCK
          && tree_decls(active->where) > 0) {
         tree_t ah = tree_decl(active->where, 0);
         if (tree_kind(ah) == T_HIER && tree_ident2(ah) == path) {
            where = active->where;
            path = active->name;
         }
      }
   }

   const int nparts = tree_parts(name);
   for (int i = 0; i < nparts; i++, where = next, next = NULL) {
      tree_t pe = tree_part(name, i);
      assert(tree_kind(pe) == T_PATH_ELT);

      ident_t id = NULL;
      bool is_gen = false;
      ident_t gen_base = NULL;
      int64_t gen_idx = 0;
      switch (tree_subkind(pe)) {
      case PE_ABSOLUTE:
         {
            tree_t top;
            rt_model_t *m = get_model_or_null();
            if (m == NULL)
               top = tree_container(where);
            else
               top = root_scope(m)->where;

            if (tree_kind(top) != T_ELAB)
               jit_msg(tree_loc(pe), DIAG_FATAL, "design hierarchy root has "
                       "not yet been elaborated");

            tree_t entity = tree_part(name, ++i);
            assert(tree_subkind(entity) == PE_SIMPLE);

            const int nroots = tree_stmts(top);
            tree_t root = NULL;
            for (int i = 0; i < nroots; i++) {
               tree_t s = tree_stmt(top, i);
               if (ident_casecmp(tree_ident(s), tree_ident(entity))) {
                  root = s;
                  break;
               }
            }

            if (root == NULL)
               jit_msg(tree_loc(pe), DIAG_FATAL, "%pI is not the name of a "
                       "root of the design hierarchy", tree_ident(entity));

            path = ident_prefix(lib_name(lib_work()), tree_ident(root), '.');
            next = root;
         }
         continue;
      case PE_SIMPLE:
         id = tree_ident(pe);
         break;
      case PE_GENERATE:
         is_gen = true;
         gen_base = tree_ident(pe);
         gen_idx = args[next_arg++].integer;
         break;
      case PE_LIBRARY:
         {
            lib_t lib = lib_require(tree_ident(pe));

            pe = tree_part(name, ++i);
            assert(tree_subkind(pe) == PE_SIMPLE);

            ident_t qual = ident_prefix(lib_name(lib), tree_ident(pe), '.');

            tree_t pack = lib_get(lib, qual);
            if (pack == NULL || !is_package(pack))
               jit_msg(tree_loc(pe), DIAG_FATAL, "%s is not a package",
                       istr(qual));

            path = tree_ident(pack);
            next = pack;
         }
         continue;
      case PE_RELATIVE:
         next = where;
         continue;
      case PE_CARET:
         {
            rt_model_t *m = get_model_or_null();
            rt_scope_t *rts = (m != NULL && tree_kind(where) == T_BLOCK)
               ? find_scope(m, where) : NULL;
            do {
               if (rts != NULL && rts->parent != NULL
                   && tree_kind(rts->parent->where) == T_BLOCK) {
                  rts = rts->parent;
                  next = rts->where;
                  path = rts->name;
                  continue;
               }

               ident_t parent = ident_runtil(path, '.');
               assert(parent != NULL);

               if (parent == lib_name(lib_work()) || tree_kind(where) != T_BLOCK)
                  jit_msg(tree_loc(pe), DIAG_FATAL, "relative pathname has "
                          "no containing declarative region");

               scope = jit_lazy_compile(j, parent);
               path = jit_get_name(j, scope);
               next = tree_from_object(jit_get_object(j, scope));
            } while (is_implicit_block(next) || is_vhdl_shim_block(next));
         }
         continue;
      default:
         should_not_reach_here();
      }

      if (!is_concurrent_block(where) && !is_package(where)) {
         diag_t *d = diag_new(DIAG_ERROR, tree_loc(pe));
         diag_printf(d, "%s is not a concurrent region",
                     istr(tree_ident(where)));
         diag_hint(d, tree_loc(where), "location of %s",
                   istr(tree_ident(where)));
         diag_emit(d);
         jit_abort_with_status(1);
      }
      else if (is_implicit_block(where)) {
         // Skip over implicit block for component declaration
         where = tree_stmt(where, 0);
         assert(tree_kind(where) == T_BLOCK);
         path = ident_prefix(path, tree_ident(where), '.');
      }

      container = where;

      if (is_gen) {
         if (is_verilog_block(where))
            id = ident_sprintf("%s[%"PRIi64"]", istr(gen_base), gen_idx);
         else
            id = ident_sprintf("%s(%"PRIi64")", istr(gen_base), gen_idx);
      }

      next = select_name(where, id);
      if (next == NULL && !is_gen && is_verilog_block(where)) {
         // VHDL extended identifiers such as \gen_x[3]\ carry outer
         // backslashes; strip them when matching Verilog scope/object names
         const char *s = istr(id);
         const size_t len = ident_len(id);
         if (len > 2 && s[0] == '\\' && s[len - 1] == '\\') {
            LOCAL_TEXT_BUF tb = tb_new();
            for (size_t k = 1; k + 1 < len; k++) {
               tb_append(tb, s[k]);
               if (s[k] == '\\' && k + 2 < len && s[k + 1] == '\\')
                  k++;
            }
            next = select_name(where, ident_new(tb_get(tb)));
         }
      }
      if (next == NULL && is_vhdl_shim_block(where)) {
         // Descend into the VHDL entity nested beneath the generated shim
         where = tree_stmt(where, 0);
         path = ident_prefix(path, tree_ident(where), '.');
         container = where;
         if (is_gen)
            id = ident_sprintf("%s(%"PRIi64")", istr(gen_base), gen_idx);
         next = select_name(where, id);
      }

      if (next == NULL) {
         diag_t *d = diag_new(DIAG_ERROR, tree_loc(pe));
         diag_printf(d, "external name %s not found",
                     istr(tree_ident(tree_part(name, nparts - 1))));
         diag_hint(d, tree_loc(pe), "name %s not found inside %s", istr(id),
                   istr(tree_ident(where)));

         if (!tree_frozen(where))
            diag_hint(d, NULL, "an object cannot be referenced by an "
                      "external name until it has been elaborated");

         diag_emit(d);
         jit_abort_with_status(1);
      }

      if (i + 1 < nparts)
         path = ident_prefix(path, tree_ident(next), '.');
   }

   if (tree_kind(where) == T_VERILOG
       || (container != NULL && is_verilog_block(container)
           && (tree_kind(where) == T_PORT_DECL
               || tree_kind(where) == T_GENERIC_DECL))) {
      bind_verilog_external_name(name, where, container, args);
      return;
   }

   if (class_of(where) != tree_class(name)) {
      diag_t *d = diag_new(DIAG_ERROR, tree_loc(name));
      diag_printf(d, "class of object %s is not %s", istr(tree_ident(where)),
                  class_str(tree_class(name)));
      diag_hint(d, tree_loc(name), "external name with class %s",
                class_str(tree_class(name)));
      diag_hint(d, tree_loc(where), "declaration of %s as %s",
                istr(tree_ident(where)), class_str(class_of(where)));
      diag_emit(d);
      jit_abort_with_status(1);
   }

   type_t type = tree_type(where);
   type_t expect = tree_type(name);

   if (!type_eq(type, expect)) {
      diag_t *d = diag_new(DIAG_ERROR, tree_loc(name));
      diag_printf(d, "type of %s %pI is %pT but external name has %pT",
                  class_str(tree_class(name)), tree_ident(where), type, expect);
      diag_hint(d, tree_loc(name), "external name with type %pT", expect);
      diag_hint(d, tree_loc(where), "declaration of %pI with type %pT",
                tree_ident(where), type);
      diag_emit(d);
      jit_abort_with_status(1);
   }

   jit_handle_t handle = jit_compile(j, path);

   void *ctx;
   if (is_concurrent_block(container)) {
      rt_scope_t *rts = find_scope(get_model(), container);
      assert(rts != NULL);

      if ((ctx = *mptr_get(rts->privdata)) == NULL)
         jit_msg(tree_loc(name), DIAG_FATAL, "%pI has not yet been elaborated",
                 tree_ident(container));
   }
   else
      ctx = jit_link(j, handle);

   void *ptr = jit_get_frame_var(j, handle, ctx, tree_ident(where));

   if (type_is_array(type) && type_const_bounds(type)) {
      int ndims = 0;
      for (type_t t = type; type_is_array(t); t = type_elem(t))
         ndims += dimension_of(t);

      jit_scalar_t *slots =
         jit_mspace_alloc((2 + ndims * 2) * sizeof(jit_scalar_t));

      int pos = 0;
      if (tree_class(name) == C_SIGNAL && type_is_homogeneous(type)) {
         slots[pos++].pointer = *(sig_shared_t **)ptr;
         slots[pos++].integer = *(int32_t *)(ptr + 8);
      }
      else
         slots[pos++].pointer = ptr;

      for (type_t t = type; type_is_array(t); t = type_elem(t)) {
         const int ndims = dimension_of(t);
         for (int i = 0; i < ndims; i++) {
            tree_t r = range_of(t, i);
            const range_kind_t dir = tree_subkind(r);
            const int64_t left = assume_int(tree_left(r));
            const int64_t right = assume_int(tree_right(r));
            const int64_t length =
               MAX(0, dir == RANGE_TO ? right - left + 1 : left - right + 1);

            slots[pos++].integer = left;
            slots[pos++].integer = dir == RANGE_TO ? length : ~length;
         }
      }

      args[0].pointer = slots;
   }
   else
      args[0].pointer = ptr;
}

typedef struct {
   vlog_node_t  v;
   rt_scope_t  *scope;
   void        *ptr;
   bool         is_net;
} hier_cache_entry_t;

static rt_model_t         *hier_cache_model = NULL;
static hier_cache_entry_t *hier_cache = NULL;
static unsigned            hier_cache_cap = 0;
static unsigned            hier_cache_used = 0;

void ename_free_cache(rt_model_t *m)
{
   if (hier_cache_model == m) {
      free(hier_cache);
      hier_cache = NULL;
      hier_cache_cap = 0;
      hier_cache_used = 0;
      hier_cache_model = NULL;
   }
}

static inline uint64_t hier_cache_hash(vlog_node_t v, rt_scope_t *s, bool is_net)
{
   return mix_bits_64((uintptr_t)v
                      ^ ((uintptr_t)s * UINT64_C(0x9e3779b97f4a7c15))
                      ^ (uint64_t)is_net);
}

static void *hier_cache_get(rt_model_t *m, vlog_node_t v, rt_scope_t *s,
                            bool is_net)
{
   if (hier_cache_model != m || hier_cache_cap == 0)
      return NULL;

   const unsigned mask = hier_cache_cap - 1;
   for (unsigned h = hier_cache_hash(v, s, is_net) & mask; ;
        h = (h + 1) & mask) {
      const hier_cache_entry_t *e = &hier_cache[h];
      if (e->v == NULL)
         return NULL;
      if (e->v == v && e->scope == s && e->is_net == is_net)
         return e->ptr;
   }
}

static void hier_cache_put(rt_model_t *m, vlog_node_t v, rt_scope_t *s,
                           bool is_net, void *ptr)
{
   if (hier_cache_model != m) {
      free(hier_cache);
      hier_cache = NULL;
      hier_cache_cap = 0;
      hier_cache_used = 0;
      hier_cache_model = m;
   }

   if (hier_cache_cap == 0 || (hier_cache_used + 1) * 10 >= hier_cache_cap * 7) {
      const unsigned ncap = hier_cache_cap ? hier_cache_cap * 2 : 256;
      hier_cache_entry_t *ntab = xcalloc_array(ncap, sizeof(hier_cache_entry_t));
      const unsigned nmask = ncap - 1;

      for (unsigned i = 0; i < hier_cache_cap; i++) {
         const hier_cache_entry_t *e = &hier_cache[i];
         if (e->v == NULL)
            continue;
         unsigned h = hier_cache_hash(e->v, e->scope, e->is_net) & nmask;
         while (ntab[h].v != NULL)
            h = (h + 1) & nmask;
         ntab[h] = *e;
      }

      free(hier_cache);
      hier_cache = ntab;
      hier_cache_cap = ncap;
   }

   const unsigned mask = hier_cache_cap - 1;
   unsigned h = hier_cache_hash(v, s, is_net) & mask;
   while (hier_cache[h].v != NULL) {
      if (hier_cache[h].v == v && hier_cache[h].scope == s
          && hier_cache[h].is_net == is_net) {
         hier_cache[h].ptr = ptr;
         return;
      }
      h = (h + 1) & mask;
   }

   hier_cache[h] = (hier_cache_entry_t){
      .v = v,
      .scope = s,
      .ptr = ptr,
      .is_net = is_net,
   };
   hier_cache_used++;
}

static rt_scope_t *scope_for_context_walk(rt_scope_t *s, void *ctx)
{
   if (s->kind == SCOPE_INSTANCE && *mptr_get(s->privdata) == ctx)
      return s;

   for (int i = 0; i < s->children.count; i++) {
      rt_scope_t *found = scope_for_context_walk(s->children.items[i], ctx);
      if (found != NULL)
         return found;
   }

   return NULL;
}

rt_scope_t *scope_for_context(rt_scope_t *s, void *ctx)
{
   if (ctx == NULL)
      return NULL;

   rt_model_t *m = get_model_or_null();
   if (m != NULL && s == root_scope(m))
      return find_scope_by_context(m, ctx);

   return scope_for_context_walk(s, ctx);
}

void *x_sv_vif_child(void *context, int64_t label)
{
   // The context of an interface instance inside the instance of the
   // interface which a virtual interface refers to

   if (context == NULL)
      jit_msg(NULL, DIAG_FATAL, "null virtual interface handle");

   rt_model_t *m = get_model();
   rt_scope_t *s = find_scope_by_context(m, context);
   if (s == NULL)
      jit_msg(NULL, DIAG_FATAL, "virtual interface handle does not refer "
              "to an interface instance");

   for (int i = 0; i < s->children.count; i++) {
      rt_scope_t *c = s->children.items[i];
      if (c->kind != SCOPE_INSTANCE || tree_kind(c->where) != T_BLOCK)
         continue;

      uint64_t hash = UINT64_C(14695981039346656037);
      for (const char *p = istr(tree_ident(c->where)); *p != '\0'; p++)
         hash = (hash ^ (uint8_t)*p) * UINT64_C(1099511628211);

      if ((int64_t)hash == label)
         return *mptr_get(c->privdata);
   }

   jit_msg(NULL, DIAG_FATAL, "interface instance not found in the virtual "
           "interface");
   return NULL;
}

static void bind_hier_ref(vlog_node_t v, jit_handle_t scope,
                          jit_scalar_t *args)
{
   assert(vlog_kind(v) == V_HIER_REF);

   rt_model_t *m = get_model();
   rt_scope_t *s = get_active_scope(m);

   if (args[2].pointer != NULL) {
      rt_scope_t *lexical = find_scope_by_context(m, args[2].pointer);
      if (lexical != NULL)
         s = lexical;
   }

   const bool is_net = (args[4].integer == 1);
   const uint32_t expect = args[3].integer;
   rt_scope_t *const orig_s = s;

   void *cached = hier_cache_get(m, v, orig_s, is_net);
   if (cached != NULL) {
      args[0].pointer = cached;
      if (unlikely(expect > 0)) {
         sig_shared_t *ss = *(sig_shared_t **)cached;
         rt_signal_t *sig = container_of(ss, rt_signal_t, shared);
         if (signal_width(sig) != expect)
            jit_msg(vlog_loc(v), DIAG_FATAL, "sorry, an inout port connected to "
                    "a hierarchical name of a different width is not supported");
      }
      return;
   }

   tree_t alias = NULL;
   ident_t alias_id = NULL;

   // Port connections are evaluated in the child instance but the name
   // is relative to the parent where the connection was written
   vlog_node_t root = vlog_value(v);
   while (vlog_has_value(root))
      root = vlog_value(root);

   if (vlog_has_ref(root) || vlog_kind(root) == V_MOD_REF) {
      for (rt_scope_t *it = s; it != NULL; it = it->parent) {
         if (it->kind != SCOPE_INSTANCE || tree_kind(it->where) != T_BLOCK)
            continue;

         if (vlog_has_ref(root) && vlog_kind(vlog_ref(root)) == V_IFACE_PORT) {
            vlog_node_t port = vlog_ref(root);
            if (vlog_iface_port_target(it->where, port,
                                       vlog_ranges(port) > 0
                                       ? vlog_mod_ref_name(root) : NULL)) {
               s = it;
               goto found;
            }

            continue;
         }

         const int nstmts = tree_stmts(it->where);
         for (int i = 0; i < nstmts; i++) {
            tree_t c = tree_stmt(it->where, i);
            if (tree_kind(c) == T_BLOCK
                && ident_casecmp(tree_ident(c), vlog_mod_ref_name(root))) {
               s = it;
               goto found;
            }
         }
      }
   }

   if (!vlog_has_ref(root) && vlog_kind(root) == V_MOD_REF) {
      // An upwards name can be that of an enclosing instance or module
      ident_t name = vlog_mod_ref_name(root);
      bool child = false;
      for (rt_scope_t *it = s; it != NULL && !child; it = it->parent) {
         if (it->kind != SCOPE_INSTANCE || tree_kind(it->where) != T_BLOCK)
            continue;

         const int nstmts = tree_stmts(it->where);
         for (int i = 0; i < nstmts && !child; i++) {
            tree_t c = tree_stmt(it->where, i);
            child = tree_kind(c) == T_BLOCK && ident_casecmp(tree_ident(c), name);
         }

         if (!child && vlog_block_is_named(it->where, name)) {
            alias = it->where;
            alias_id = name;
            break;
         }
      }
   }

 found:
   ;
   vlog_set_mod_ref_alias(alias_id, alias);
   tree_t b = vlog_walk_mod_refs(vlog_value(v), s->where);
   vlog_set_mod_ref_alias(NULL, NULL);
   assert(b != NULL);

   if (vlog_ident(v) == ident_new("$self"))
      ;   // The context of the interface instance itself
   else if (!vlog_has_ref(v) || (vlog_kind(vlog_ref(v)) != V_FUNC_DECL
                                 && vlog_kind(vlog_ref(v)) != V_TASK_DECL))
      b = vlog_hier_target_block(b, vlog_ident(v));

   tree_t h = tree_decl(b, 0);
   assert(tree_kind(h) == T_HIER);

   rt_scope_t *rts = find_scope(get_model(), b);
   assert(rts != NULL);

   void *ctx = *mptr_get(rts->privdata);
   if (ctx == NULL)
      jit_msg(vlog_loc(v), DIAG_FATAL, "%pi has not yet been elaborated",
              tree_ident(b));

   jit_t *j = jit_for_thread();
   jit_handle_t handle = jit_compile(j, tree_ident2(h));

   // Subroutines are called with the context of the instance
   ident_t name = vlog_ident(v);
   if (vlog_has_ref(v)) {
      const vlog_kind_t kind = vlog_kind(vlog_ref(v));
      if (kind == V_FUNC_DECL || kind == V_TASK_DECL)
         name = ident_new("$self");
   }

   if (is_net)   // The signal of a structure net
      name = ident_prefix(name, ident_new("net"), '$');

   void *ptr = jit_get_frame_var(j, handle, ctx, name);
   args[0].pointer = ptr;

   if (vlog_has_ref(v)) {
      vlog_node_t target = vlog_ref(v);
      if (vlog_kind(target) == V_PORT_DECL && vlog_has_ref(target))
         target = vlog_ref(target);

      if ((vlog_kind(target) == V_VAR_DECL || vlog_kind(target) == V_NET_DECL)
          && !vlog_is_fixed_unpacked(target)) {
         vlog_node_t vt = vlog_has_type(target) ? vlog_type(target) : NULL;
         vlog_node_t ut = unwrap_verilog_type(vt);
         const bool is_unpacked_agg = ut != NULL
            && (vlog_kind(ut) == V_STRUCT_DECL || vlog_kind(ut) == V_UNION_DECL)
            && !(vlog_flags(ut) & VLOG_F_PACKED);
         if ((vt == NULL || !vlog_is_dynamic_type(vt)) && !is_unpacked_agg
             && name != ident_new("$self")) {
            sig_shared_t *ss = *(sig_shared_t **)ptr;
            if (ss != NULL && unlikely(ss->flags & SIG_F_ISLAND))
               rt_island_on_observer_attached(m, container_of(ss, rt_signal_t,
                                                              shared));
         }
      }
   }

   // The width of a net connected to an inout port cannot be checked
   // before the name is bound
   if (expect > 0) {
      sig_shared_t *ss = *(sig_shared_t **)ptr;
      rt_signal_t *sig = container_of(ss, rt_signal_t, shared);
      if (signal_width(sig) != expect)
         jit_msg(vlog_loc(v), DIAG_FATAL, "sorry, an inout port connected to "
                 "a hierarchical name of a different width is not supported");
   }

   hier_cache_put(m, v, orig_s, is_net, ptr);
}

void x_bind_external(object_t *where, jit_handle_t scope, jit_scalar_t *args)
{
   tree_t t = tree_from_object(where);
   if (t != NULL)
      bind_external_name(t, scope, args);

   vlog_node_t v = vlog_from_object(where);
   if (v != NULL)
      bind_hier_ref(v, scope, args);
}
