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
#include "common.h"
#include "diag.h"
#include "hash.h"
#include "tree.h"
#include "type.h"
#include "vlog/vlog-defs.h"
#include "vlog/vlog-node.h"
#include "vlog/vlog-number.h"
#include "vlog/vlog-phase.h"
#include "vlog/vlog-util.h"

#include <assert.h>
#include <inttypes.h>

typedef struct {
   tree_t  out;
   tree_t  chars[256];
} trans_gen_t;

static tree_t trans_expr(trans_gen_t *g, vlog_node_t v)
{
   switch (vlog_kind(v)) {
   case V_NUMBER:
      {
         number_t n = vlog_number(v);
         type_t std_int = std_type(NULL, STD_INTEGER);

         tree_t lit = tree_new(T_LITERAL);
         tree_set_subkind(lit, L_INT);
         tree_set_type(lit, std_int);

         if (number_is_defined(n))
            tree_set_ival(lit, number_integer(n));
         else
            tree_set_ival(lit, 0);

         return lit;
      }
   case V_STRING:
      {
         number_t n = vlog_number(v);
         type_t std_string = std_type(NULL, STD_STRING);
         type_t std_char = type_elem(std_string);

         tree_t str = tree_new(T_STRING);
         tree_set_type(str, std_string);

         const int width = number_width(n);
         for (int i = 0; i < width / 8; i++) {
            const int byte = number_byte(n, width/8 - i - 1);
            if (g->chars[byte] == NULL)
               g->chars[byte] = make_ref(type_enum_literal(std_char, byte));

            tree_add_char(str, g->chars[byte]);
         }

         return str;
      }
   case V_REF:
      {
         vlog_node_t d = vlog_ref(v);
         if (vlog_kind(d) == V_LOCALPARAM)
            return trans_expr(g, vlog_value(d));

         return NULL;
      }
   default:
      return NULL;
   }
}

static type_t trans_sized_type(vlog_node_t decl, verilog_type_t packed_type,
                               int width)
{
   type_t packed = verilog_type(packed_type);

   tree_t c = tree_new(T_CONSTRAINT);
   tree_set_subkind(c, C_INDEX);
   tree_set_loc(c, vlog_loc(decl));

   type_t std_int = std_type(NULL, STD_INTEGER);

   tree_t left = tree_new(T_LITERAL);
   tree_set_subkind(left, L_INT);
   tree_set_type(left, std_int);
   tree_set_ival(left, width - 1);

   tree_t right = tree_new(T_LITERAL);
   tree_set_subkind(right, L_INT);
   tree_set_type(right, std_int);
   tree_set_ival(right, 0);

   tree_t r = tree_new(T_RANGE);
   tree_set_subkind(r, RANGE_DOWNTO);
   tree_set_left(r, left);
   tree_set_right(r, right);
   tree_set_loc(r, vlog_loc(decl));
   tree_set_type(r, std_int);

   tree_add_range(c, r);

   type_t sub = type_new(T_SUBTYPE);
   type_set_base(sub, packed);
   type_set_constraint(sub, c);

   return sub;
}

static type_t trans_type(trans_gen_t *g, vlog_node_t decl,
                         verilog_type_t scalar_type,
                         verilog_type_t packed_type)
{
   // TODO: cache decl ^ scalar_type ^ packed_type

   if (vlog_is_dynamic_type(decl))
      return NULL;

   switch (vlog_kind(decl)) {
   case V_TYPE_DECL:
   case V_ENUM_DECL:
      return trans_type(g, vlog_type(decl), scalar_type, packed_type);
   case V_CLASS_DECL:
      return NULL;
   case V_STRUCT_DECL:
   case V_UNION_DECL:
      assert(vlog_flags(decl) & VLOG_F_PACKED);
      return trans_sized_type(decl, packed_type, vlog_size(decl));
   case V_DATA_TYPE:
      break;
   default:
      CANNOT_HANDLE(decl);
   }

   switch (vlog_subkind(decl)) {
   case DT_IMPLICIT:
   case DT_LOGIC:
   case DT_BIT:
      {
         const int nranges = vlog_ranges(decl);
         if (nranges == 0)
            return verilog_type(scalar_type);

         type_t packed = verilog_type(packed_type);

         tree_t c = tree_new(T_CONSTRAINT);
         tree_set_subkind(c, C_INDEX);
         tree_set_loc(c, vlog_loc(decl));

         for (int i = 0; i < nranges; i++) {
            type_t index_type = index_type_of(packed, i);
            vlog_node_t vr = vlog_range(decl, i);

            tree_t left = trans_expr(g, vlog_left(vr));
            tree_t right = trans_expr(g, vlog_right(vr));
            if (left == NULL || right == NULL)
               return packed;   // Non-literal bounds

            int64_t ileft, iright;
            const bool left_is_const = folded_int(left, &ileft);
            const bool right_is_const = folded_int(right, &iright);

            range_kind_t dir;
            if (left_is_const && right_is_const)
               dir = ileft < iright ? RANGE_TO : RANGE_DOWNTO;
            else if (left_is_const)
               dir = RANGE_TO;   // Heuristic
            else
               dir = RANGE_DOWNTO;   // Heuristic

            tree_t r = tree_new(T_RANGE);
            tree_set_subkind(r, dir);
            tree_set_left(r, left);
            tree_set_right(r, right);
            tree_set_loc(r, vlog_loc(decl));
            tree_set_type(r, index_type);

            tree_add_range(c, r);
         }

         type_t sub = type_new(T_SUBTYPE);
         type_set_base(sub, packed);
         type_set_constraint(sub, c);

         return sub;
      }

   case DT_INT:
   case DT_INTEGER:
      return trans_sized_type(decl, packed_type, 32);

   case DT_BYTE:
      return trans_sized_type(decl, packed_type, 8);

   case DT_SHORTINT:
      return trans_sized_type(decl, packed_type, 16);

   case DT_LONGINT:
      return trans_sized_type(decl, packed_type, 64);

   case DT_REAL:
   case DT_SHORTREAL:
   case DT_REALTIME:
      return std_type(NULL, STD_REAL);

   default:
      CANNOT_HANDLE(decl);
   }
}

static type_t trans_var_type(trans_gen_t *g, vlog_node_t v)
{
   vlog_node_t type = vlog_type(v);
   return trans_type(g, type, VERILOG_LOGIC, VERILOG_LOGIC_ARRAY);
}

static type_t trans_net_type(trans_gen_t *g, vlog_node_t v)
{
   vlog_node_t type = vlog_type(v);
   return trans_type(g, type, VERILOG_WIRE, VERILOG_WIRE_ARRAY);
}

static void trans_port_decl(trans_gen_t *g, vlog_node_t v)
{
   static const port_mode_t map[] = {
      [V_PORT_INPUT] = PORT_IN,
      [V_PORT_OUTPUT] = PORT_OUT,
      [V_PORT_INOUT] = PORT_INOUT,
      [V_PORT_REF] = PORT_INOUT,
   };

   const v_port_kind_t kind = vlog_subkind(v);

   tree_t t = tree_new(T_PORT_DECL);
   tree_set_ident(t, vlog_ident(v));
   tree_set_subkind(t, map[kind]);
   tree_set_class(t, C_SIGNAL);
   tree_set_loc(t, vlog_loc(v));

   vlog_node_t net = vlog_ref(v);
   if (vlog_is_net(net)) {
      type_t type = trans_net_type(g, net);
      tree_set_type(t, type);
   }
   else {
      type_t type = trans_var_type(g, net);
      tree_set_type(t, type);
   }

   tree_add_port(g->out, t);
}

static void trans_param_decl(trans_gen_t *g, vlog_node_t v)
{
   tree_t t = tree_new(T_GENERIC_DECL);
   tree_set_ident(t, vlog_ident(v));
   tree_set_class(t, C_CONSTANT);
   tree_set_subkind(t, PORT_IN);

   // XXX: should be integer for untyped only
   /// tree_set_type(t, trans_var_type(g, v));
   tree_set_type(t, std_type(NULL, STD_INTEGER));

   tree_add_generic(g->out, t);
}

static void trans_generic(trans_gen_t *g, vlog_node_t decl)
{
   tree_t wrap = tree_new(T_VERILOG);
   tree_set_vlog(wrap, decl);
   tree_set_ident(wrap, vlog_ident(decl));
   tree_set_loc(wrap, vlog_loc(decl));

   tree_add_decl(g->out, wrap);
}

static vlog_node_t trans_bit_select(vlog_node_t v, unsigned pos)
{
   // Select the bit POS places from the left of terminal V

   if (vlog_width(v) == 1)
      return v;

   switch (vlog_kind(v)) {
   case V_NUMBER:
      {
         number_t n = vlog_number(v);
         static const char digits[] = { [LOGIC_0] = '0', [LOGIC_1] = '1',
                                        [LOGIC_X] = 'x', [LOGIC_Z] = 'z' };
         const char bit[] = { '1', '\'', 'b',
                              digits[number_bit(n, number_width(n) - 1 - pos)],
                              '\0' };

         vlog_node_t b = vlog_new(V_NUMBER);
         vlog_set_number(b, number_new(bit, vlog_loc(v)));
         vlog_set_loc(b, vlog_loc(v));
         return b;
      }
   case V_BINARY:
      switch (vlog_subkind(v)) {
      case V_BINARY_AND:
      case V_BINARY_OR:
      case V_BINARY_XOR:
      case V_BINARY_XNOR:
      case V_BINARY_NAND:
      case V_BINARY_NOR:
         {
            vlog_node_t b = vlog_new(V_BINARY);
            vlog_set_subkind(b, vlog_subkind(v));
            vlog_set_left(b, trans_bit_select(vlog_left(v), pos));
            vlog_set_right(b, trans_bit_select(vlog_right(v), pos));
            vlog_set_loc(b, vlog_loc(v));
            return b;
         }
      default:
         break;
      }
      break;
   case V_UNARY:
      if (vlog_subkind(v) == V_UNARY_BITNEG) {
         vlog_node_t u = vlog_new(V_UNARY);
         vlog_set_subkind(u, V_UNARY_BITNEG);
         vlog_set_value(u, trans_bit_select(vlog_value(v), pos));
         vlog_set_loc(u, vlog_loc(v));
         return u;
      }
      break;
   case V_CONCAT:
      {
         if (vlog_has_value(v)) {
            // Replication of the same bits
            unsigned width = 0;
            for (int i = 0; i < vlog_params(v); i++)
               width += vlog_width(vlog_param(v, i));
            pos %= width;
         }

         const int nparams = vlog_params(v);
         for (int i = 0; i < nparams; i++) {
            vlog_node_t p = vlog_param(v, i);
            const unsigned width = vlog_width(p);
            if (pos < width)
               return trans_bit_select(p, pos);
            pos -= width;
         }
      }
      break;
   case V_REF:
   case V_PART_SELECT:
      {
         vlog_node_t base = v;
         int64_t left, right;
         bool known = false;
         if (vlog_kind(v) == V_REF) {
            base = v;
            if (vlog_dimensions(vlog_ref(v)) > 0) {
               vlog_node_t dim = vlog_get_dim(v, 0);
               known = vlog_bounds(dim, &left, &right);
            }
         }
         else if (vlog_subkind(v) == V_RANGE_CONST) {
            base = vlog_value(v);
            known = vlog_get_const(vlog_left(v), &left)
               && vlog_get_const(vlog_right(v), &right);
         }

         if (!known || vlog_kind(base) != V_REF)
            break;

         vlog_node_t idx = vlog_new(V_NUMBER);
         vlog_set_number(idx, number_from_int(left >= right
                                              ? left - pos : left + pos));
         vlog_set_loc(idx, vlog_loc(v));

         vlog_node_t bs = vlog_new(V_BIT_SELECT);
         vlog_node_t ref = vlog_new(V_REF);
         vlog_set_ident(ref, vlog_ident(base));
         vlog_set_ref(ref, vlog_ref(base));
         vlog_set_loc(ref, vlog_loc(base));

         vlog_set_value(bs, ref);
         vlog_add_param(bs, idx);
         vlog_set_loc(bs, vlog_loc(v));
         return bs;
      }
   default:
      break;
   }

   fatal_at(vlog_loc(v), "sorry, this terminal cannot be split across an "
            "array of gate instances");
}

static vlog_node_t trans_gate_terminal(vlog_node_t v, unsigned count,
                                       unsigned pos)
{
   const unsigned width = vlog_width(v);
   if (width == count && count != 1)
      return trans_bit_select(v, pos);
   else
      return v;
}

static void trans_gate_array(vlog_node_t mod, vlog_node_t s)
{
   vlog_node_t dim = vlog_gate_dimension(s);

   int64_t left, right;
   if (!vlog_bounds(dim, &left, &right)) {
      error_at(vlog_loc(dim), "range of gate instance array is not constant");
      return;
   }

   const unsigned count = (left > right ? left - right : right - left) + 1;
   const int nparams = vlog_params(s);

   for (int i = -1; i < nparams; i++) {
      vlog_node_t p = i < 0 ? vlog_target(s) : vlog_param(s, i);
      if (vlog_kind(p) == V_STRENGTH || vlog_kind(p) == V_DIMENSION)
         continue;

      const unsigned width = vlog_width(p);
      if (width != 1 && width != count)
         error_at(vlog_loc(p), "terminal of width %u does not match array of "
                  "%u gate instances", width, count);
   }

   for (unsigned n = 0; n < count; n++) {
      const int64_t index = left > right ? left - n : left + n;

      vlog_node_t gate = vlog_new(V_GATE_INST);
      vlog_set_subkind(gate, vlog_subkind(s));
      vlog_set_ident(gate, ident_sprintf("%s_%"PRIi64, istr(vlog_ident(s)),
                                         index));
      vlog_set_loc(gate, vlog_loc(s));

      for (int i = 0; i < nparams; i++) {
         vlog_node_t p = vlog_param(s, i);
         if (vlog_kind(p) == V_DIMENSION)
            continue;
         else if (vlog_kind(p) != V_STRENGTH)
            p = trans_gate_terminal(p, count, n);
         vlog_add_param(gate, p);
      }

      vlog_set_target(gate, trans_gate_terminal(vlog_target(s), count, n));

      vlog_add_stmt(mod, gate);
   }
}

void vlog_trans(vlog_node_t mod, tree_t out)
{
   trans_gen_t gen = {
      .out = out,
   };

   const int nbefore = vlog_stmts(mod);
   for (int i = 0; i < nbefore; i++) {
      vlog_node_t s = vlog_stmt(mod, i);
      if (vlog_kind(s) == V_GATE_INST && vlog_gate_dimension(s) != NULL)
         trans_gate_array(mod, s);
   }

   const int ndecls = vlog_decls(mod);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(mod, i);
      switch (vlog_kind(d)) {
      case V_PORT_DECL:
         break;   // Translated below
      case V_PARAM_DECL:
         trans_param_decl(&gen, d);
         break;
      case V_FUNC_DECL:
      case V_TASK_DECL:
      case V_NET_DECL:
      case V_VAR_DECL:
      case V_LOCAL_DECL:
      case V_LOCALPARAM:
         trans_generic(&gen, d);
         break;
      case V_GENVAR_DECL:
      case V_TYPE_DECL:
      case V_CLASS_DECL:
      case V_STRUCT_DECL:
      case V_IMPORT_DECL:
      case V_DPI_EXPORT:
      case V_MODPORT:
      case V_IFACE_PORT:
      case V_SVA_DECL:
         break;
      default:
         CANNOT_HANDLE(d);
      }
   }

   if (vlog_kind(mod) == V_GEN_BLOCK)
      return;

   const int nports = vlog_ports(mod);
   for (int i = 0; i < nports; i++) {
      vlog_node_t ref = vlog_port(mod, i);
      assert(vlog_kind(ref) == V_REF);

      vlog_node_t port = vlog_ref(ref);
      assert(vlog_kind(port) == V_PORT_DECL);
      trans_port_decl(&gen, port);
   }

   const int nstmts = vlog_stmts(mod);
   for (int i = 0; i < nstmts; i++) {
      vlog_node_t s = vlog_stmt(mod, i);
      switch (vlog_kind(s)) {
      case V_INITIAL:
      case V_ALWAYS:
      case V_FINAL:
      case V_ASSIGN:
      case V_UDP_TABLE:
         {
            tree_t wrap = tree_new(T_VERILOG);
            tree_set_ident(wrap, vlog_ident(s));
            tree_set_vlog(wrap, s);
            tree_set_loc(wrap, vlog_loc(s));

            tree_add_stmt(out, wrap);
         }
         break;
      case V_GATE_INST:
         if (vlog_gate_dimension(s) == NULL) {
            tree_t wrap = tree_new(T_VERILOG);
            tree_set_ident(wrap, vlog_ident(s));
            tree_set_vlog(wrap, s);
            tree_set_loc(wrap, vlog_loc(s));

            tree_add_stmt(out, wrap);
         }
         break;
      case V_SPECIFY:
         {
            int n_stmts = vlog_stmts(s);
            for (int i = 0; i < n_stmts; i++) {
               vlog_node_t stmt = vlog_stmt(s, i);
               if (vlog_kind(stmt) != V_TCHECK)
                  continue;

               tree_t wrap = tree_new(T_VERILOG);
               tree_set_vlog(wrap, stmt);
               tree_set_loc(wrap, vlog_loc(stmt));

               tree_add_stmt(out, wrap);
            }
         }
         break;
      default:
         break;
      }
   }
}
