//
//  Copyright (C) 2024-2026 Nick Gasson
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
#include "ident.h"
#include "hash.h"
#include "lib.h"
#include "vlog/vlog-node.h"
#include "vlog/vlog-number.h"
#include "vlog/vlog-phase.h"
#include "vlog/vlog-util.h"

#include <assert.h>
#include <string.h>
#include <inttypes.h>

bool vlog_is_net(vlog_node_t v)
{
   switch (vlog_kind(v)) {
   case V_REF:
   case V_HIER_REF:
      return vlog_has_ref(v) ? vlog_is_net(vlog_ref(v)) : false;
   case V_PORT_DECL:
      return vlog_has_ref(v) ? vlog_is_net(vlog_ref(v)) : true;
   case V_NET_DECL:
      return true;
   case V_BIT_SELECT:
   case V_PART_SELECT:
      return vlog_is_net(vlog_value(v));
   case V_CONCAT:
      {
         const int nparams = vlog_params(v);
         for (int i = 0; i < nparams; i++) {
            if (!vlog_is_net(vlog_param(v, i)))
               return false;
         }
         return true;
      }
   default:
      return false;
   }
}

vlog_node_t vlog_gate_dimension(vlog_node_t gate)
{
   const int nparams = vlog_params(gate);
   for (int i = 0; i < nparams; i++) {
      vlog_node_t p = vlog_param(gate, i);
      if (vlog_kind(p) == V_DIMENSION)
         return p;
   }

   return NULL;
}

unsigned vlog_dimensions(vlog_node_t v)
{
   return vlog_ranges(vlog_type(v)) + vlog_ranges(v);
}

bool vlog_get_const(vlog_node_t v, int64_t *value)
{
   switch (vlog_kind(v)) {
   case V_NUMBER:
      {
         number_t n = vlog_number(v);
         if (number_is_defined(n)) {
            *value = number_integer(n);
            return true;
         }
         else
            return false;
      }
   case V_REF:
      return vlog_get_const(vlog_ref(v), value);
   case V_LOCALPARAM:
   case V_PARAM_DECL:
      return vlog_get_const(vlog_value(v), value);
   case V_ENUM_NAME:
      return vlog_has_value(v) && vlog_get_const(vlog_value(v), value);
   case V_CONCAT:
      if (vlog_params(v) == 1 && !vlog_has_value(v))
         return vlog_get_const(vlog_param(v, 0), value);
      else
         return false;
   default:
      return false;
   }
}

int64_t vlog_must_be_const(vlog_node_t v)
{
   int64_t value;
   if (vlog_get_const(v, &value))
      return value;

   fatal_at(vlog_loc(v), "expression is not constant");
}

bool vlog_is_const(vlog_node_t v)
{
   switch (vlog_kind(v)) {
   case V_NUMBER:
      return true;
   case V_REF:
      return vlog_is_const(vlog_ref(v));
   case V_LOCALPARAM:
   case V_PARAM_DECL:
      return vlog_is_const(vlog_value(v));
   case V_CONCAT:
      if (vlog_params(v) == 1 && !vlog_has_value(v))
         return vlog_is_const(vlog_param(v, 0));
      return false;
   default:
      return false;
   }
}

bool vlog_is_up(vlog_node_t v)
{
   assert(vlog_kind(v) == V_DIMENSION);

   int64_t left, right;
   vlog_bounds(v, &left, &right);

   return left <= right;
}

bool vlog_range_bound(vlog_node_t v, int64_t *value)
{
   // Constant bound of an array before parameters have been folded

   if (vlog_kind(v) == V_BINARY && (vlog_subkind(v) == V_BINARY_MINUS
                                    || vlog_subkind(v) == V_BINARY_PLUS)) {
      int64_t l, r;
      if (!vlog_range_bound(vlog_left(v), &l)
          || !vlog_range_bound(vlog_right(v), &r))
         return false;

      *value = vlog_subkind(v) == V_BINARY_MINUS ? l - r : l + r;
      return true;
   }

   return vlog_get_const(v, value);
}

bool vlog_bounds(vlog_node_t v, int64_t *left, int64_t *right)
{
   bool is_defined = true;
   is_defined &= vlog_get_const(vlog_left(v), left);
   is_defined &= vlog_get_const(vlog_right(v), right);
   return is_defined;
}

bool vlog_is_real(vlog_node_t v)
{
   switch (vlog_kind(v)) {
   case V_REAL:
      return true;
   case V_BINARY:
      switch (vlog_subkind(v)) {
      case V_BINARY_PLUS:
      case V_BINARY_MINUS:
      case V_BINARY_TIMES:
      case V_BINARY_DIVIDE:
      case V_BINARY_EXP:
         return vlog_is_real(vlog_left(v)) || vlog_is_real(vlog_right(v));
      default:
         return false;
      }
   case V_UNARY:
      switch (vlog_subkind(v)) {
      case V_UNARY_IDENTITY:
      case V_UNARY_NEG:
         return vlog_is_real(vlog_value(v));
      default:
         return false;
      }
   case V_COND_EXPR:
      return vlog_is_real(vlog_left(v)) || vlog_is_real(vlog_right(v));
   case V_MIN_TYP_MAX:
      return vlog_is_real(vlog_value(v));
   default:
      {
         vlog_node_t type = vlog_get_type(v);
         if (type == NULL || vlog_kind(type) != V_DATA_TYPE)
            return false;

         switch (vlog_subkind(type)) {
         case DT_REAL:
         case DT_SHORTREAL:
         case DT_REALTIME:
            return true;
         default:
            return false;
         }
      }
   }
}

bool vlog_is_fixed_unpacked(vlog_node_t decl)
{
   switch (vlog_kind(decl)) {
   case V_VAR_DECL:
   case V_NET_DECL:
   case V_PORT_DECL:
   case V_TF_PORT_DECL:
   case V_LOCAL_DECL:
   case V_PARAM_DECL:
   case V_LOCALPARAM:
      return vlog_ranges(decl) > 0
         && vlog_subkind(vlog_range(decl, 0)) == V_DIM_UNPACKED;
   default:
      return false;
   }
}

unsigned vlog_tag_bits(vlog_node_t v)
{
   assert(vlog_kind(v) == V_UNION_DECL);

   // The tag of a packed tagged union is held above the largest member
   return MAX(1, ilog2(vlog_decls(v)));
}

bool vlog_is_two_state(vlog_node_t type)
{
   // A packed structure is two-state if every member is

   switch (vlog_kind(type)) {
   case V_TYPE_DECL:
      return vlog_is_two_state(vlog_type(type));
   case V_DATA_TYPE:
      switch (vlog_subkind(type)) {
      case DT_BIT:
      case DT_INT:
      case DT_SHORTINT:
      case DT_LONGINT:
      case DT_BYTE:
         return true;
      default:
         return false;
      }
   case V_UNION_DECL:
      {
         if (!(vlog_flags(type) & VLOG_F_PACKED)
             || (vlog_flags(type) & VLOG_F_TAGGED))
            return false;

         const int ndecls = vlog_decls(type);
         for (int i = 0; i < ndecls; i++) {
            if (!vlog_is_two_state(vlog_type(vlog_decl(type, i))))
               return false;
         }

         return ndecls > 0;
      }
   case V_STRUCT_DECL:
      {
         if (!(vlog_flags(type) & VLOG_F_PACKED))
            return false;

         const int ndecls = vlog_decls(type);
         for (int i = 0; i < ndecls; i++) {
            if (!vlog_is_two_state(vlog_type(vlog_decl(type, i))))
               return false;
         }

         return ndecls > 0;
      }
   default:
      return false;
   }
}

static bool type_is_four_state(vlog_node_t type)
{
   if (type == NULL)
      return true;
   else if (vlog_kind(type) == V_ENUM_DECL)
      return !vlog_has_type(type) ? false
         : type_is_four_state(vlog_get_type(vlog_type(type)));
   return !vlog_is_two_state(type);
}

bool vlog_is_four_state(vlog_node_t v)
{
   // An operation is four-state if any of its operands is (11.8.1)

   switch (vlog_kind(v)) {
   case V_NUMBER:
      return !number_is_defined(vlog_number(v));
   case V_STRING:
   case V_REAL:
      return false;
   case V_UNARY:
      return vlog_is_four_state(vlog_value(v));
   case V_BINARY:
      return vlog_is_four_state(vlog_left(v))
         || vlog_is_four_state(vlog_right(v));
   case V_COND_EXPR:
      return vlog_is_four_state(vlog_value(v))
         || vlog_is_four_state(vlog_left(v))
         || vlog_is_four_state(vlog_right(v));
   case V_CONCAT:
      {
         const int nparams = vlog_params(v);
         for (int i = 0; i < nparams; i++) {
            if (vlog_is_four_state(vlog_param(v, i)))
               return true;
         }
         return false;
      }
   case V_MIN_TYP_MAX:
      return vlog_is_four_state(vlog_value(v));
   case V_OP_ASSIGN:
   case V_PREFIX:
   case V_POSTFIX:
      return vlog_is_four_state(vlog_target(v));
   case V_PART_SELECT:
      return vlog_is_four_state(vlog_value(v));
   case V_BIT_SELECT:
      {
         vlog_node_t etype = vlog_select_type(v);
         if (etype != NULL)
            return type_is_four_state(etype);
         return vlog_is_four_state(vlog_value(v));
      }
   case V_CAST:
      switch (vlog_subkind(v)) {
      case V_CAST_TYPE:
         return type_is_four_state(vlog_get_type(vlog_type(v)));
      case V_CAST_WIDTH:
         return vlog_is_four_state(vlog_value(v));
      default:
         return true;
      }
   case V_SYS_FCALL:
      switch (vlog_subkind(v)) {
      case V_SYSTF_SIGNED:
      case V_SYSTF_UNSIGNED:
         return vlog_is_four_state(vlog_param(v, 0));
      default:
         {
            // Declared to return int, int unsigned or bit (18.13, 20.9)
            static const char *const names[] = {
               "$urandom", "$urandom_range", "$countbits", "$countones",
               "$onehot", "$onehot0", "$isunknown",
            };

            for (int i = 0; i < ARRAY_LEN(names); i++) {
               if (vlog_ident(v) == ident_new(names[i]))
                  return false;
            }

            return true;
         }
      }
   case V_METHOD_CALL:
      {
         vlog_node_t type = vlog_get_type(v);
         if (type != NULL)
            return type_is_four_state(type);

         switch (vlog_subkind(v)) {
         case V_METHOD_USER:
         case V_METHOD_CLASS:
            return true;
         default:
            return false;   // Built-in methods returning int
         }
      }
   default:
      {
         vlog_node_t type = vlog_get_type(v);
         return type == NULL || type_is_four_state(type);
      }
   }
}

bool vlog_has_static_shape(vlog_node_t v)
{
   // The dimensions of a variable do not depend on its value

   switch (vlog_kind(v)) {
   case V_REF:
      if (vlog_has_ref(v)) {
         vlog_node_t d = vlog_ref(v);
         return vlog_kind(d) == V_LOCALPARAM || vlog_kind(d) == V_PARAM_DECL
            || !vlog_is_dynamic_type(vlog_type(d));
      }
      return false;
   case V_MEMBER_REF:
   case V_BIT_SELECT:
      {
         vlog_node_t type = vlog_get_type(v);
         return vlog_has_static_shape(vlog_value(v))
            && (type != NULL ? !vlog_is_dynamic_type(type)
                : vlog_kind(v) == V_BIT_SELECT);
      }
   case V_PART_SELECT:
      return vlog_has_static_shape(vlog_value(v)) && vlog_size(v) != 0;
   case V_CONCAT:
      {
         const int nparams = vlog_params(v);
         for (int i = 0; i < nparams; i++) {
            if (!vlog_has_static_shape(vlog_param(v, i)))
               return false;
         }

         return true;
      }
   default:
      return false;
   }
}

unsigned vlog_size(vlog_node_t v)
{
   switch (vlog_kind(v)) {
   case V_DATA_TYPE:
   case V_VAR_DECL:
   case V_NET_DECL:
   case V_PORT_DECL:
   case V_TF_PORT_DECL:
   case V_LOCAL_DECL:
   case V_PARAM_DECL:
   case V_LOCALPARAM:
      {
         unsigned size = 1;

         const int nranges = vlog_ranges(v);
         for (int i = 0; i < nranges; i++)
            size *= vlog_size(vlog_range(v, i));

         // Packed array of structures
         if (vlog_kind(v) == V_DATA_TYPE && vlog_has_type(v)
             && !vlog_is_dynamic_type(v))
            size *= vlog_size(vlog_type(v));

         return size;
      }
   case V_FUNC_DECL:
      return 1;  // No unpacked dimensions
   case V_PART_SELECT:
      if (vlog_subkind(v) != V_RANGE_CONST) {
         int64_t width;
         if (vlog_get_const(vlog_right(v), &width))
            return width;
         else
            return 0;  // Undefined
      }
      // Fall-through
   case V_DIMENSION:
      {
         if (vlog_subkind(v) == V_DIM_UNSIZED)
            return 0;   // Unknown

         int64_t left, right;
         if (!vlog_bounds(v, &left, &right))
            return 0;  // Undefined

         if (left < right)
            return right - left + 1;
         else
            return left - right + 1;
      }
   case V_TYPE_DECL:
   case V_ENUM_DECL:
      return vlog_size(vlog_type(v));
   case V_UNION_DECL:
      if (vlog_flags(v) & VLOG_F_PACKED) {
         unsigned width = 0;

         const int ndecls = vlog_decls(v);
         for (int i = 0; i < ndecls; i++)
            width = MAX(width, vlog_size(vlog_type(vlog_decl(v, i))));

         if (vlog_flags(v) & VLOG_F_TAGGED)
            width += vlog_tag_bits(v);

         return width;
      }
      else
         return 0;  // Undefined
   case V_STRUCT_DECL:
      if (vlog_flags(v) & VLOG_F_PACKED) {
         unsigned width = 0;

         const int ndecls = vlog_decls(v);
         for (int i = 0; i < ndecls; i++)
            width += vlog_size(vlog_type(vlog_decl(v, i)));

         return width;
      }
      else
         return 0;  // Undefined
   case V_CLASS_DECL:
      return 0;  // Undefined
   default:
      CANNOT_HANDLE(v);
   }
}

static bool accept_interface(object_t *obj)
{
   vlog_node_t v = vlog_from_object(obj);
   return v != NULL && vlog_kind(v) == V_MODULE
      && (vlog_flags(v) & VLOG_F_INTERFACE);
}

vlog_node_t vlog_virtual_iface(vlog_node_t type)
{
   // The interface of a virtual interface type, which may be declared
   // after the type is used

   assert(vlog_kind(type) == V_DATA_TYPE && vlog_subkind(type) == DT_VIRTUAL);

   if (vlog_has_ref(type))
      return vlog_ref(type);

   LOCAL_TEXT_BUF tb = tb_new();
   tb_istr(tb, vlog_ident(type));
   tb_upcase(tb);

   lib_t work = lib_work();
   ident_t qual = ident_prefix(lib_name(work), ident_new(tb_get(tb)), '.');
   object_t *obj = lib_get_generic(work, qual, NULL);
   if (obj == NULL) {
      // The interface may be in another library on the search path
      lib_t where;
      obj = lib_search_generic(ident_new(tb_get(tb)), accept_interface,
                               &where);
   }

   return obj != NULL ? vlog_from_object(obj) : NULL;
}

bool vlog_virtual_pending(vlog_node_t v)
{
   // The prefix has a virtual interface type whose interface was not
   // declared when the reference was parsed

   if (vlog_has_ref(v))
      return false;

   vlog_node_t type = vlog_get_type(vlog_value(v));
   return type != NULL && vlog_kind(type) == V_DATA_TYPE
      && vlog_subkind(type) == DT_VIRTUAL;
}

static vlog_node_t virtual_find_decl(vlog_node_t v)
{
   assert(vlog_kind(v) == V_MEMBER_REF || vlog_kind(v) == V_METHOD_CALL);

   static hash_t *cache = NULL;
   if (cache == NULL)
      cache = hash_new(16);

   vlog_node_t decl = hash_get(cache, v);
   if (decl != NULL)
      return decl;

   vlog_node_t iface = vlog_virtual_iface(vlog_get_type(vlog_value(v)));
   if (iface == NULL)
      return NULL;

   // A clocking block on its own is its event and a name through a
   // modport is the name of the member
   ident_t id = vlog_ident(v), alt = NULL;
   const char *dollar = strchr(istr(id), '$');
   if (dollar == NULL)
      alt = ident_sprintf("%s$ev", istr(id));
   else if (dollar != istr(id)) {
      const int nmp = vlog_decls(iface);
      ident_t mp = ident_new_n(istr(id), dollar - istr(id));
      for (int i = 0; i < nmp; i++) {
         vlog_node_t d = vlog_decl(iface, i);
         if (vlog_kind(d) == V_MODPORT && vlog_ident(d) == mp)
            alt = ident_new(dollar + 1);
      }
   }

   const bool method = vlog_kind(v) == V_METHOD_CALL;
   const int ndecls = vlog_decls(iface);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(iface, i);
      switch (vlog_kind(d)) {
      case V_FUNC_DECL:
      case V_TASK_DECL:
         if (!method)
            continue;
         break;
      case V_PORT_DECL:
      case V_NET_DECL:
      case V_VAR_DECL:
         if (method)
            continue;
         break;
      default:
         continue;
      }

      if (vlog_ident(d) == id || (alt != NULL && vlog_ident(d) == alt)) {
         hash_put(cache, v, d);
         return d;
      }
   }

   return NULL;
}

static vlog_node_t member_decl_or_null(vlog_node_t v)
{
   if (vlog_has_ref(v))
      return vlog_member_decl(v);
   else if (vlog_virtual_pending(v))
      return virtual_find_decl(v);
   else
      return NULL;
}

vlog_node_t vlog_virtual_decl_or_null(vlog_node_t v)
{
   assert(vlog_virtual_pending(v));
   return virtual_find_decl(v);
}

vlog_node_t vlog_virtual_decl(vlog_node_t v)
{
   // The declaration of a member or method of a virtual interface whose
   // type was not known when the reference was parsed

   assert(vlog_virtual_pending(v));

   vlog_node_t decl = virtual_find_decl(v);
   if (decl != NULL)
      return decl;

   vlog_node_t type = vlog_get_type(vlog_value(v));
   vlog_node_t iface = vlog_virtual_iface(type);
   if (iface == NULL)
      fatal_at(vlog_loc(type), "no interface named %pQ", vlog_ident(type));

   fatal_at(vlog_loc(v), "interface %pQ has no %s named %pQ",
            vlog_ident2(iface),
            vlog_kind(v) == V_METHOD_CALL ? "method" : "member",
            vlog_ident(v));
}

vlog_node_t vlog_method_decl(vlog_node_t v)
{
   // The subroutine called by a method of a class or virtual interface

   assert(vlog_kind(v) == V_METHOD_CALL && vlog_subkind(v) == V_METHOD_CLASS);

   return vlog_has_ref(v) ? vlog_ref(v) : vlog_virtual_decl(v);
}

vlog_node_t vlog_member_decl(vlog_node_t v)
{
   // A member of a virtual interface is declared by the interface with
   // parameters whose values are not known until it is specialised

   assert(vlog_kind(v) == V_MEMBER_REF);

   vlog_node_t decl = vlog_has_ref(v) ? vlog_ref(v) : vlog_virtual_decl(v);
   switch (vlog_kind(decl)) {
   case V_NET_DECL:
   case V_VAR_DECL:
   case V_PORT_DECL:
      break;
   default:
      return decl;
   }

   vlog_node_t type = vlog_get_type(vlog_value(v));
   if (type != NULL && (vlog_kind(type) == V_STRUCT_DECL
                        || vlog_kind(type) == V_UNION_DECL)) {
      // The type of the prefix may be that of a parameter of a virtual
      // interface with a value other than the default used when the name
      // was looked up
      const int nfields = vlog_decls(type);
      for (int i = 0; i < nfields; i++) {
         if (vlog_decl(type, i) == decl)
            return decl;
      }

      for (int i = 0; i < nfields; i++) {
         vlog_node_t d = vlog_decl(type, i);
         if (vlog_has_ident(d) && vlog_ident(d) == vlog_ident(decl))
            return d;
      }

      return decl;
   }

   if (type == NULL || vlog_kind(type) != V_DATA_TYPE
       || vlog_subkind(type) != DT_VIRTUAL)
      return decl;

   vlog_node_t spec = vlog_virtual_spec(type);
   if (spec == NULL)
      return decl;

   const int ndecls = vlog_decls(spec);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(spec, i);
      if (vlog_has_ident(d) && vlog_ident(d) == vlog_ident(decl))
         return d;
   }

   return decl;
}

bool vlog_is_scan_target(vlog_node_t call, int n)
{
   // A variable which $fscanf, $sscanf or $value$plusargs assigns an
   // integral value to

   static ident_t fscanf_i, sscanf_i, plusargs_i;
   if (fscanf_i == NULL) {
      fscanf_i = ident_new("$fscanf");
      sscanf_i = ident_new("$sscanf");
      plusargs_i = ident_new("$value$plusargs");
   }

   if (vlog_kind(call) != V_SYS_FCALL && vlog_kind(call) != V_SYS_TCALL)
      return false;

   ident_t id = vlog_ident(call);
   if (id == plusargs_i) {
      if (n != 1)
         return false;
   }
   else if ((id != fscanf_i && id != sscanf_i) || n < 2)
      return false;

   vlog_node_t p = vlog_param(call, n);
   switch (vlog_kind(p)) {
   case V_REF:
   case V_HIER_REF:
   case V_MEMBER_REF:
   case V_BIT_SELECT:
      break;
   default:
      return false;
   }

   if (vlog_is_real(p))
      return true;

   const unsigned width = vlog_width(p);
   return width > 0 && width <= 64;
}

unsigned vlog_packed_offset(vlog_node_t type, vlog_node_t member)
{
   assert(vlog_flags(type) & VLOG_F_PACKED);

   if (vlog_kind(type) == V_UNION_DECL) {
      // Members of a union are right justified, below the tag if it has one
      return vlog_size(type) - vlog_size(vlog_type(member));
   }

   unsigned offset = 0;

   const int ndecls = vlog_decls(type);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(type, i);
      if (d == member)
         return offset;

      offset += vlog_size(vlog_type(d));
   }

   should_not_reach_here();
}

unsigned vlog_width(vlog_node_t v)
{
   switch (vlog_kind(v)) {
   case V_NUMBER:
   case V_STRING:
      return number_width(vlog_number(v));
   case V_REAL:
   case V_CLASS_DECL:
   case V_NULL:
      return 0;  // Undefined
   case V_MEMBER_REF:
      {
         vlog_node_t decl = member_decl_or_null(v);
         return decl != NULL ? vlog_width(decl) : 1;
      }
   case V_REF:
   case V_USER_FCALL:
      return vlog_width(vlog_ref(v));
   case V_PORT_DECL:
   case V_VAR_DECL:
   case V_NET_DECL:
   case V_TF_PORT_DECL:
   case V_PARAM_DECL:
   case V_LOCALPARAM:
   case V_FUNC_DECL:
   case V_GENVAR_DECL:
   case V_ENUM_NAME:
   case V_ENUM_DECL:
   case V_LOCAL_DECL:
      return vlog_size(vlog_type(v));
   case V_HIER_REF:
      if (vlog_has_ref(v))
         return vlog_width(vlog_ref(v));
      else
         fatal_at(vlog_loc(v), "unbound hierarchical reference");
   case V_THIS:
      return 0;
   case V_PATTERN_EXPR:
      return vlog_size(vlog_get_type(v));
   case V_DOLLAR:
   case V_STD_RANDOMIZE:
      return 32;
   case V_INSIDE:
   case V_DIST:
   case V_MATCHES:
      return 1;
   case V_METHOD_CALL:
      {
         vlog_node_t type = vlog_get_type(v);
         if (type != NULL)
            return vlog_size(type);
         else if (vlog_kind(vlog_value(v)) == V_HIER_REF
                  && vlog_has_ref(vlog_value(v)))
            return vlog_width(vlog_ref(vlog_value(v)));
         return 32;
      }
   case V_BIT_SELECT:
      {
         vlog_node_t etype = vlog_select_type(v);
         if (etype != NULL)
            return vlog_size(etype);

         vlog_node_t value = vlog_value(v);

         vlog_node_t vtype = vlog_get_type(value);
         if (vtype != NULL && vlog_kind(vtype) == V_DATA_TYPE
             && vlog_subkind(vtype) == DT_STRING)
            return 8;   // One character
         const int nparams = vlog_params(v);

         if (vtype != NULL && vlog_kind(vtype) == V_DATA_TYPE
             && vlog_has_type(vtype) && nparams > 1) {
            switch (vlog_subkind(vtype)) {
            case DT_DYNARRAY:
            case DT_QUEUE:
            case DT_ASSOC:
               {
                  // Selects within an element of the container
                  vlog_node_t elem = vlog_get_type(vlog_type(vtype));
                  if (elem != NULL && vlog_kind(elem) == V_DATA_TYPE) {
                     unsigned ew = vlog_size(elem);
                     const int nr = MIN(nparams - 1, vlog_ranges(elem));
                     for (int i = 0; i < nr; i++)
                        ew /= vlog_size(vlog_range(elem, i));
                     return ew;
                  }
               }
               break;
            default:
               break;
            }
         }

         unsigned width = vlog_width(value);

         for (int i = 0; i < nparams && width > 0; i++)
         {
            // The width of the value excludes its unpacked dimensions
            vlog_node_t dim = vlog_get_dim(value, i);
            if (dim == NULL)
               width = 1;
            else if (vlog_subkind(dim) == V_DIM_PACKED)
               width /= MAX(1, vlog_size(dim));
         }

         return width;
      }
   case V_PART_SELECT:
      return vlog_size(v);
   case V_CONCAT:
      {
         unsigned width = 0;

         const int nparams = vlog_params(v);
         for (int i = 0; i < nparams; i++)
            width += vlog_width(vlog_param(v, i));

         if (vlog_has_value(v)) {
            int64_t repeat;
            if (vlog_get_const(vlog_value(v), &repeat))
               width *= MAX(0, repeat);
            else
               return 0;  // Undefined
         }

         return width;
      }
   case V_COND_EXPR:
      return MAX(vlog_width(vlog_left(v)), vlog_width(vlog_right(v)));
   case V_OP_ASSIGN:
   case V_PREFIX:
   case V_POSTFIX:
      return vlog_width(vlog_target(v));
   case V_STREAM:
      {
         unsigned width = 0;
         const int nparams = vlog_params(v);
         for (int i = 0; i < nparams; i++)
            width += vlog_width(vlog_param(v, i));
         return width;
      }
   case V_MIN_TYP_MAX:
      return vlog_width(vlog_value(v));
   case V_UNARY:
      switch (vlog_subkind(v)) {
      case V_UNARY_BITNEG:
      case V_UNARY_IDENTITY:
      case V_UNARY_NEG:
         return vlog_width(vlog_value(v));
      case V_UNARY_NOT:
      case V_UNARY_AND:
      case V_UNARY_OR:
      case V_UNARY_NAND:
      case V_UNARY_NOR:
      case V_UNARY_XOR:
      case V_UNARY_XNOR:
         return 1;
      default:
         CANNOT_HANDLE(v);
      }
   case V_BINARY:
      switch (vlog_subkind(v)) {
      case V_BINARY_PLUS:
      case V_BINARY_MINUS:
      case V_BINARY_TIMES:
      case V_BINARY_DIVIDE:
      case V_BINARY_MOD:
      case V_BINARY_EXP:
      case V_BINARY_OR:
      case V_BINARY_AND:
      case V_BINARY_XOR:
      case V_BINARY_XNOR:
      case V_BINARY_NAND:
      case V_BINARY_NOR:
         return MAX(vlog_width(vlog_left(v)), vlog_width(vlog_right(v)));
      case V_BINARY_SHIFT_LL:
      case V_BINARY_SHIFT_RL:
      case V_BINARY_SHIFT_LA:
      case V_BINARY_SHIFT_RA:
         return vlog_width(vlog_left(v));
      case V_BINARY_LOG_OR:
      case V_BINARY_LOG_AND:
      case V_BINARY_LT:
      case V_BINARY_LEQ:
      case V_BINARY_GT:
      case V_BINARY_GEQ:
      case V_BINARY_LOG_EQ:
      case V_BINARY_LOG_NEQ:
      case V_BINARY_CASE_EQ:
      case V_BINARY_CASE_NEQ:
      case V_BINARY_WILD_EQ:
      case V_BINARY_WILD_NEQ:
         return 1;
      default:
         CANNOT_HANDLE(v);
      }
   case V_SYS_FCALL:
      switch (vlog_subkind(v)) {
      case V_SYSTF_UNSIGNED:
      case V_SYSTF_SIGNED:
         return vlog_width(vlog_param(v, 0));
      default:
         if (vlog_ident(v) == ident_new("$onehot")
             || vlog_ident(v) == ident_new("$onehot0")
             || vlog_ident(v) == ident_new("$isunknown"))
            return 1;
         return 32;  // TODO: call VPI
      }
   case V_CAST:
      if (vlog_subkind(v) == V_CAST_TYPEREF)
         return 32;
      else if (vlog_subkind(v) == V_CAST_TYPE)
         return vlog_size(vlog_get_type(vlog_type(v)));
      {
         assert(vlog_subkind(v) == V_CAST_WIDTH);

         int64_t width;
         if (vlog_get_const(vlog_left(v), &width))
            return width;
         else
            return 0;  // Undefined
      }
   default:
      CANNOT_HANDLE(v);
   }
}

bool vlog_is_sformatf(ident_t id)
{
   // $sformatf or the forms of it with another default radix which
   // implement $swriteb, $swriteo and $swriteh

   return id == ident_new("$sformatf") || id == ident_new("$__swriteb")
      || id == ident_new("$__swriteo") || id == ident_new("$__swriteh");
}

bool vlog_is_signed(vlog_node_t v)
{
   switch (vlog_kind(v)) {
   case V_NUMBER:
      return number_signed(vlog_number(v));
   case V_VAR_DECL:
   case V_NET_DECL:
   case V_PORT_DECL:
   case V_TF_PORT_DECL:
   case V_LOCALPARAM:
   case V_LOCAL_DECL:
   case V_GENVAR_DECL:
      return vlog_is_signed(vlog_type(v));
   case V_FUNC_DECL:
      return vlog_has_type(v) && vlog_is_signed(vlog_type(v));
   case V_MEMBER_REF:
      {
         vlog_node_t decl = member_decl_or_null(v);
         return decl != NULL && vlog_is_signed(decl);
      }
   case V_REF:
   case V_HIER_REF:
   case V_USER_FCALL:
      return vlog_has_ref(v) && vlog_is_signed(vlog_ref(v));
   case V_STD_RANDOMIZE:
      return true;
   case V_METHOD_CALL:
      {
         vlog_node_t type = vlog_get_type(v);
         if (type != NULL)
            return vlog_is_signed(type);
         else if (vlog_kind(vlog_value(v)) == V_HIER_REF
                  && vlog_has_ref(vlog_value(v)))
            return vlog_is_signed(vlog_ref(vlog_value(v)));
         return true;
      }
   case V_CAST:
      if (vlog_subkind(v) == V_CAST_TYPEREF)
         return false;
      else if (vlog_subkind(v) == V_CAST_TYPE)
         return vlog_is_signed(vlog_get_type(vlog_type(v)));
      return vlog_is_signed(vlog_value(v));
   case V_BIT_SELECT:
      {
         vlog_node_t etype = vlog_select_type(v);
         return etype != NULL && vlog_is_signed(etype);
      }
   case V_PART_SELECT:
      return false;
   case V_UNARY:
      switch (vlog_subkind(v)) {
      case V_UNARY_BITNEG:
      case V_UNARY_IDENTITY:
      case V_UNARY_NEG:
         return vlog_is_signed(vlog_value(v));
      default:
         return false;   // Logical and reduction results are unsigned
      }
   case V_BINARY:
      switch (vlog_subkind(v)) {
      case V_BINARY_LOG_OR:
      case V_BINARY_LOG_AND:
      case V_BINARY_LT:
      case V_BINARY_LEQ:
      case V_BINARY_GT:
      case V_BINARY_GEQ:
      case V_BINARY_LOG_EQ:
      case V_BINARY_LOG_NEQ:
      case V_BINARY_CASE_EQ:
      case V_BINARY_CASE_NEQ:
      case V_BINARY_WILD_EQ:
      case V_BINARY_WILD_NEQ:
         return false;   // Relational and equality results are unsigned
      case V_BINARY_SHIFT_LL:
      case V_BINARY_SHIFT_RL:
      case V_BINARY_SHIFT_LA:
      case V_BINARY_SHIFT_RA:
      case V_BINARY_EXP:
         return vlog_is_signed(vlog_left(v));
      default:
         return vlog_is_signed(vlog_left(v)) && vlog_is_signed(vlog_right(v));
      }
   case V_COND_EXPR:
      return vlog_is_signed(vlog_left(v)) && vlog_is_signed(vlog_right(v));
   case V_OP_ASSIGN:
   case V_PREFIX:
   case V_POSTFIX:
      return vlog_is_signed(vlog_target(v));
   case V_STREAM:
      return false;
   case V_SYS_FCALL:
      switch (vlog_subkind(v)) {
      case V_SYSTF_SIGNED:   return true;
      case V_SYSTF_UNSIGNED: return false;
      default:
         {
            static const char *const names[] = {
               "$random", "$dist_uniform", "$dist_normal", "$dist_exponential",
               "$dist_poisson", "$dist_chi_square", "$dist_t", "$dist_erlang",
               "$countbits", "$countones", "$ftell", "$fgetc", "$ferror",
               "$fseek", "$test$plusargs", "$value$plusargs", "$dimensions",
               "$unpacked_dimensions", "$left", "$right", "$low", "$high",
               "$increment", "$size", "$rtoi", "$clog2", "$bits", "$fopen",
               "$fscanf", "$sscanf", "$fgets", "$fread", "$feof", "$ungetc",
               "$countdrivers", "$random", "$timeunit", "$timeprecision",
            };

            for (int i = 0; i < ARRAY_LEN(names); i++) {
               if (vlog_ident(v) == ident_new(names[i]))
                  return true;
            }

            return false;
         }
      }
   case V_MIN_TYP_MAX:
      return vlog_is_signed(vlog_value(v));
   case V_DATA_TYPE:
   case V_STRUCT_DECL:
   case V_UNION_DECL:
      return !!(vlog_flags(v) & VLOG_F_SIGNED);
   case V_TYPE_DECL:
      return vlog_is_signed(vlog_type(v));
   case V_ENUM_DECL:
      return vlog_has_type(v) ? vlog_is_signed(vlog_type(v)) : true;
   default:
      return false;
   }
}

int vlog_open_dims(vlog_node_t port, vlog_node_t *elem)
{
   // Number of unsized unpacked dimensions of a formal argument and the
   // type of the elements

   vlog_node_t type = vlog_type(port);
   int ndims = 0;
   while (vlog_kind(type) == V_DATA_TYPE && vlog_subkind(type) == DT_DYNARRAY
          && vlog_ranges(type) == 0 && vlog_has_type(type)) {
      ndims++;
      type = vlog_type(type);
   }

   if (elem != NULL)
      *elem = type;

   return ndims;
}

bool vlog_has_open_packed(vlog_node_t type)
{
   while (vlog_kind(type) == V_TYPE_DECL && vlog_has_type(type))
      type = vlog_type(type);

   if (vlog_kind(type) != V_DATA_TYPE)
      return false;

   const int nranges = vlog_ranges(type);
   for (int i = 0; i < nranges; i++) {
      vlog_node_t r = vlog_range(type, i);
      if (vlog_subkind(r) == V_DIM_UNSIZED && (vlog_flags(r) & VLOG_F_OPEN_PACKED))
         return true;
   }

   return false;
}

bool vlog_is_open_array(vlog_node_t port)
{
   // A formal argument of a DPI import with an unsized dimension

   vlog_node_t elem;
   return vlog_open_dims(port, &elem) > 0 || vlog_has_open_packed(elem);
}

bool vlog_is_unsized(vlog_node_t v)
{
   const int nranges = vlog_ranges(v);
   for (int i = 0; i < nranges; i++) {
      if (vlog_subkind(vlog_range(v, i)) == V_DIM_UNSIZED)
         return true;
   }

   return false;
}

bool is_top_level(vlog_node_t v)
{
   switch (vlog_kind(v)) {
   case V_MODULE:
   case V_PRIMITIVE:
   case V_INST_BODY:
   case V_PACKAGE:
   case V_PROGRAM:
   case V_NAMESPACE:
   case V_BIND:
   case V_CONFIG:
      return true;
   default:
      return false;
   }
}

bool is_data_type(vlog_node_t v)
{
   switch (vlog_kind(v)) {
   case V_DATA_TYPE:
   case V_ENUM_DECL:
   case V_STRUCT_DECL:
   case V_UNION_DECL:
   case V_TYPE_DECL:
   case V_CLASS_DECL:
      return true;
   default:
      return false;
   }
}

bool is_implicit_data_type(vlog_node_t v)
{
   return vlog_kind(v) == V_DATA_TYPE && vlog_subkind(v) == DT_IMPLICIT;
}

static bool vlog_is_const_arith(vlog_node_t v)
{
   // A constant index may be computed from parameters and genvars

   switch (vlog_kind(v)) {
   case V_BINARY:
      switch (vlog_subkind(v)) {
      case V_BINARY_PLUS:
      case V_BINARY_MINUS:
      case V_BINARY_TIMES:
      case V_BINARY_DIVIDE:
      case V_BINARY_MOD:
      case V_BINARY_SHIFT_LL:
      case V_BINARY_SHIFT_RL:
         return vlog_is_const_arith(vlog_left(v))
            && vlog_is_const_arith(vlog_right(v));
      default:
         return false;
      }
   case V_UNARY:
      switch (vlog_subkind(v)) {
      case V_UNARY_NEG:
         return vlog_is_const_arith(vlog_value(v));
      default:
         return false;
      }
   default:
      return vlog_is_const(v);
   }
}

vlog_node_t vlog_longest_static_prefix(vlog_node_t v)
{
   switch (vlog_kind(v)) {
   case V_HIER_REF:
      if (!vlog_has_ref(v))
         return v;
      // Fall-through
   case V_REF:
      switch (vlog_kind(vlog_ref(v))) {
      case V_PARAM_DECL:
      case V_LOCALPARAM:
      case V_LOCAL_DECL:
         return NULL;
      default:
         return v;
      }
   case V_BIT_SELECT:
      {
         vlog_node_t value = vlog_value(v);
         vlog_node_t prefix = vlog_longest_static_prefix(value);

         if (prefix != value)
            return prefix;

         const int nparams = vlog_params(v);
         for (int i = 0; i < nparams; i++) {
            if (!vlog_is_const_arith(vlog_param(v, i)))
               return prefix;
         }

         return v;
      }
   case V_PART_SELECT:
      {
         vlog_node_t value = vlog_value(v);
         vlog_node_t prefix = vlog_longest_static_prefix(value);

         if (prefix != value)
            return prefix;

         if (!vlog_is_const_arith(vlog_left(v)))
            return prefix;

         return v;
      }
   case V_MEMBER_REF:
      {
         vlog_node_t value = vlog_value(v);
         vlog_node_t prefix = vlog_longest_static_prefix(value);

         if (prefix != value)
            return prefix;

         return v;
      }
   case V_NUMBER:
   case V_BINARY:
   case V_UNARY:
   case V_COND_EXPR:
      return NULL;
   default:
      fatal_at(vlog_loc(v), "cannot calculate longest static prefix");
   }
}

bool vlog_equal_node(vlog_node_t a, vlog_node_t b)
{
   if (a == b)
      return true;

   const vlog_kind_t kind = vlog_kind(a);
   if (kind != vlog_kind(b))
      return false;

   switch (kind) {
   case V_NUMBER:
      {
         number_t an = vlog_number(a);
         number_t bn = vlog_number(b);

         return number_equal(an, bn);
      }
   case V_BINARY:
      return vlog_subkind(a) == vlog_subkind(b)
         && vlog_equal_node(vlog_left(a), vlog_left(b))
         && vlog_equal_node(vlog_right(a), vlog_right(b));
   case V_REF:
      if (!vlog_has_ref(a) || !vlog_has_ref(b))
         return true;   // Suppress cascading errors
      else
         return vlog_ref(a) == vlog_ref(b);
   case V_PARAM_ASSIGN:
      {
         // TODO: should handle parameter assignment in different order
         ident_t ia = vlog_has_ident(a) ? vlog_ident(a) : NULL;
         ident_t ib = vlog_has_ident(b) ? vlog_ident(b) : NULL;
         if (ia != ib)
            return false;

         vlog_node_t va = vlog_has_value(a) ? vlog_value(a) : NULL;
         vlog_node_t vb = vlog_has_value(b) ? vlog_value(b) : NULL;
         if (va == NULL && vb == NULL)
            return true;
         else if (va == NULL || vb == NULL)
            return false;
         else
            return vlog_equal_node(va, vb);
      }
   default:
      return false;
   }
}

uint32_t vlog_hash_node(vlog_node_t v)
{
   switch (vlog_kind(v)) {
   case V_NUMBER:
      return number_hash(vlog_number(v));
   case V_REF:
      return vlog_hash_node(vlog_ref(v));
   case V_PARAM_ASSIGN:
      {
         uint32_t h = 0;
         if (vlog_has_value(v))
            h ^= vlog_hash_node(vlog_value(v));
         if (vlog_has_ident(v))
            h ^= ident_hash(vlog_ident(v));
         return h;
      }
   default:
      return mix_bits_64((uintptr_t)v);
   }
}

static bool vlog_is_iface_decl(vlog_node_t d)
{
   // An interface class which is implemented, or extended by an interface
   // class, is kept as a type declaration with a reserved name

   return vlog_kind(d) == V_TYPE_DECL && vlog_ident(d) == ident_new("$implements");
}

unsigned vlog_class_ifaces(vlog_node_t class)
{
   unsigned count = 0;
   const int ndecls = vlog_decls(class);
   for (int i = 0; i < ndecls; i++) {
      if (vlog_is_iface_decl(vlog_decl(class, i)))
         count++;
   }

   return count;
}

vlog_node_t vlog_class_iface(vlog_node_t class, unsigned n)
{
   const int ndecls = vlog_decls(class);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(class, i);
      if (vlog_is_iface_decl(d) && n-- == 0)
         return vlog_type(d);
   }

   return NULL;
}

bool vlog_class_derives(vlog_node_t c, vlog_node_t base)
{
   // True if c is the same class as base or is derived from it by
   // extending a class or implementing an interface class

   for (;; c = vlog_ref(c)) {
      // The same specialisation may be declared in two scopes
      if (c == base || vlog_ident(c) == vlog_ident(base))
         return true;

      const unsigned nifaces = vlog_class_ifaces(c);
      for (unsigned i = 0; i < nifaces; i++) {
         if (vlog_class_derives(vlog_class_iface(c, i), base))
            return true;
      }

      if (!vlog_has_ref(c))
         return false;
   }
}

vlog_node_t vlog_class_member(vlog_node_t class, ident_t id)
{
   for (vlog_node_t c = class;; c = vlog_ref(c)) {
      const int ndecls = vlog_decls(c);
      for (int i = 0; i < ndecls; i++) {
         vlog_node_t d = vlog_decl(c, i);
         switch (vlog_kind(d)) {
         case V_VAR_DECL:
         case V_FUNC_DECL:
         case V_TASK_DECL:
         case V_CONSTRAINT:
            if (vlog_ident(d) == id)
               return d;
            break;
         default:
            break;
         }
      }

      // An interface class inherits the methods of those it extends
      if (vlog_flags(c) & VLOG_F_INTERFACE) {
         const unsigned nifaces = vlog_class_ifaces(c);
         for (unsigned i = 0; i < nifaces; i++) {
            vlog_node_t m = vlog_class_member(vlog_class_iface(c, i), id);
            if (m != NULL)
               return m;
         }
      }

      if (!vlog_has_ref(c))
         return NULL;
   }
}

bool vlog_needs_polling(vlog_node_t v)
{
   // An event expression can only use a trigger if it depends on nothing
   // but signals: other state must be checked again as the simulation runs

   switch (vlog_kind(v)) {
   case V_NUMBER:
   case V_STRING:
   case V_REAL:
      return false;
   case V_REF:
   case V_HIER_REF:
      if (!vlog_has_ref(v))
         return false;
      else {
         vlog_node_t d = vlog_ref(v);
         if (vlog_kind(d) == V_VAR_DECL && (vlog_flags(d) & VLOG_F_VHDL_PATH)) {
            // The signal selected by a variable index can change
            for (vlog_node_t it = vlog_value(d); ; it = vlog_value(it)) {
               const int nparams = vlog_params(it);
               for (int i = 0; i < nparams; i++) {
                  if (!vlog_is_const(vlog_param(it, i)))
                     return true;
               }

               if (!vlog_has_value(it))
                  break;
            }

            return false;
         }

         switch (vlog_kind(d)) {
         case V_NET_DECL:
         case V_PORT_DECL:
         case V_PARAM_DECL:
         case V_LOCALPARAM:
         case V_ENUM_NAME:
         case V_GENVAR_DECL:
            return false;
         case V_VAR_DECL:
            // Variables in a package are memory rather than signals
            return (vlog_flags(d) & VLOG_F_PROPERTY)
               || vlog_is_dynamic_type(vlog_type(d))
               || vlog_has_ident2(d) || (vlog_flags(d) & VLOG_F_STATIC);
         default:
            return true;
         }
      }
   case V_MEMBER_REF:
      {
         vlog_node_t type = vlog_get_type(vlog_value(v));
         if (type == NULL || vlog_is_dynamic_type(type))
            return true;

         return vlog_needs_polling(vlog_value(v));
      }
   case V_BIT_SELECT:
      {
         if (vlog_needs_polling(vlog_value(v)))
            return true;

         const int nparams = vlog_params(v);
         for (int i = 0; i < nparams; i++) {
            if (vlog_needs_polling(vlog_param(v, i)))
               return true;
         }

         return false;
      }
   case V_PART_SELECT:
      return vlog_needs_polling(vlog_value(v))
         || vlog_needs_polling(vlog_left(v))
         || vlog_needs_polling(vlog_right(v));
   case V_BINARY:
      return vlog_needs_polling(vlog_left(v))
         || vlog_needs_polling(vlog_right(v));
   case V_UNARY:
      return vlog_needs_polling(vlog_value(v));
   case V_COND_EXPR:
      return vlog_needs_polling(vlog_value(v))
         || vlog_needs_polling(vlog_left(v))
         || vlog_needs_polling(vlog_right(v));
   case V_CONCAT:
   case V_SYS_FCALL:
      {
         const int nparams = vlog_params(v);
         if (nparams == 0)
            return true;

         for (int i = 0; i < nparams; i++) {
            if (vlog_needs_polling(vlog_param(v, i)))
               return true;
         }

         return false;
      }
   default:
      return true;
   }
}

bool vlog_is_dynamic_type(vlog_node_t type)
{
   // Types which have no representation as a signal

   switch (vlog_kind(type)) {
   case V_TYPE_DECL:
   case V_ENUM_DECL:
      return vlog_is_dynamic_type(vlog_type(type));
   case V_CLASS_DECL:
      return true;
   case V_STRUCT_DECL:
   case V_UNION_DECL:
      return !(vlog_flags(type) & VLOG_F_PACKED);
   case V_DATA_TYPE:
      switch (vlog_subkind(type)) {
      case DT_STRING:
      case DT_DYNARRAY:
      case DT_QUEUE:
      case DT_ASSOC:
      case DT_MAILBOX:
      case DT_SEMAPHORE:
      case DT_PROCESS:
      case DT_CHANDLE:
      case DT_VIRTUAL:
      case DT_EVENT:
         return true;
      default:
         return false;
      }
   default:
      return false;
   }
}

vlog_node_t vlog_select_type(vlog_node_t v)
{
   // Type of a selected element of a dynamic or packed array

   vlog_node_t value = vlog_value(v);
   vlog_node_t type = vlog_get_type(value);
   if (type == NULL)
      return NULL;

   int nparams = vlog_params(v);

   // Fixed unpacked dimensions of the declaration come first
   switch (vlog_kind(value)) {
   case V_REF:
   case V_HIER_REF:
   case V_MEMBER_REF:
      if (vlog_kind(value) == V_MEMBER_REF ? member_decl_or_null(value) != NULL
          : vlog_has_ref(value)) {
         vlog_node_t decl = vlog_kind(value) == V_MEMBER_REF
            ? vlog_member_decl(value) : vlog_ref(value);
         switch (vlog_kind(decl)) {
         case V_VAR_DECL:
         case V_LOCAL_DECL:
         case V_NET_DECL:
         case V_PORT_DECL:
         case V_TF_PORT_DECL:
         case V_PARAM_DECL:
         case V_LOCALPARAM:
            nparams -= vlog_ranges(decl);
            break;
         default:
            break;
         }
      }
      break;
   default:
      break;
   }

   for (; nparams > 0; nparams--) {
      if (vlog_kind(type) != V_DATA_TYPE)
         return NULL;

      switch (vlog_subkind(type)) {
      case DT_DYNARRAY:
      case DT_QUEUE:
      case DT_ASSOC:
         type = vlog_get_type(vlog_type(type));
         break;
      default:
         if (vlog_has_type(type)) {
            // Packed array of a non-vector element
            const int ranges = vlog_ranges(type);
            if (nparams >= ranges) {
               nparams -= ranges - 1;
               type = vlog_get_type(vlog_type(type));
            }
            else
               return NULL;
         }
         else
            return NULL;
         break;
      }
   }

   return type;
}

vlog_node_t vlog_get_type(vlog_node_t v)
{
   switch (vlog_kind(v)) {
   case V_STRUCT_DECL:
   case V_UNION_DECL:
   case V_ENUM_DECL:
   case V_DATA_TYPE:
   case V_CLASS_DECL:
      return v;
   case V_VAR_DECL:
   case V_NET_DECL:
   case V_PORT_DECL:
   case V_TF_PORT_DECL:
   case V_TYPE_DECL:
   case V_CLASS_NEW:
   case V_DYNAMIC_NEW:
   case V_LOCAL_DECL:
   case V_PARAM_DECL:
   case V_LOCALPARAM:
   case V_ENUM_NAME:
      return vlog_get_type(vlog_type(v));
   case V_MEMBER_REF:
      {
         vlog_node_t decl = member_decl_or_null(v);
         return decl != NULL ? vlog_get_type(decl) : NULL;
      }
   case V_REF:
   case V_USER_FCALL:
      if (vlog_has_ref(v))
         return vlog_get_type(vlog_ref(v));
      else
         return NULL;
   case V_FUNC_DECL:
      return vlog_has_type(v) ? vlog_get_type(vlog_type(v)) : NULL;
   case V_METHOD_CALL:
      switch (vlog_subkind(v)) {
      case V_METHOD_USER:
      case V_METHOD_CLASS:
         {
            vlog_node_t decl = NULL;
            if (vlog_has_ref(v))
               decl = vlog_ref(v);
            else if (vlog_virtual_pending(v))
               decl = virtual_find_decl(v);
            else if (vlog_kind(vlog_value(v)) == V_HIER_REF
                     && vlog_has_ref(vlog_value(v)))
               decl = vlog_ref(vlog_value(v));   // Bound by elaboration

            if (decl != NULL && vlog_kind(decl) == V_FUNC_DECL)
               return vlog_get_type(decl);
            return NULL;
         }
      case V_METHOD_POP_BACK:
      case V_METHOD_POP_FRONT:
         {
            vlog_node_t q = vlog_get_type(vlog_value(v));
            return q ? vlog_get_type(vlog_type(q)) : NULL;
         }
      case V_METHOD_ENUM_FIRST:
      case V_METHOD_ENUM_LAST:
      case V_METHOD_ENUM_NEXT:
      case V_METHOD_ENUM_PREV:
         return vlog_get_type(vlog_value(v));
      case V_METHOD_STR_TOUPPER:
      case V_METHOD_STR_TOLOWER:
      case V_METHOD_STR_SUBSTR:
      case V_METHOD_STR_ATOREAL:
      case V_METHOD_STR_GETC:
      case V_METHOD_ARR_SUM:
      case V_METHOD_ARR_PRODUCT:
      case V_METHOD_ARR_AND:
      case V_METHOD_ARR_OR:
      case V_METHOD_ARR_XOR:
      case V_METHOD_ARR_MIN:
      case V_METHOD_ARR_MAX:
      case V_METHOD_ARR_UNIQUE:
      case V_METHOD_ARR_UNIQUE_INDEX:
      case V_METHOD_ARR_FIND:
      case V_METHOD_ARR_FIND_INDEX:
      case V_METHOD_ARR_FIND_FIRST:
      case V_METHOD_ARR_FIND_FIRST_INDEX:
      case V_METHOD_ARR_FIND_LAST:
      case V_METHOD_ARR_FIND_LAST_INDEX:
      case V_METHOD_RANDSTATE_GET:
      case V_METHOD_EVENT_TRIGGERED:
      case V_METHOD_ENUM_NAME:
         return vlog_has_ref(v) ? vlog_ref(v) : NULL;
      default:
         return NULL;
      }
   case V_CAST:
      if (vlog_subkind(v) == V_CAST_TYPE) {
         // Only an enumeration has methods and is printed by name
         vlog_node_t type = vlog_get_type(vlog_type(v));
         if (type != NULL && vlog_kind(type) == V_ENUM_DECL)
            return type;
      }
      return NULL;
   case V_BIT_SELECT:
      return vlog_select_type(v);
   case V_PART_SELECT:
      {
         // Slice of a queue is a queue
         vlog_node_t type = vlog_get_type(vlog_value(v));
         if (type != NULL && vlog_kind(type) == V_DATA_TYPE
             && vlog_subkind(type) == DT_QUEUE)
            return type;
         return NULL;
      }
   case V_THIS:
      return vlog_has_type(v) ? vlog_type(v) : NULL;
   case V_COND_EXPR:
      {
         // Only of interest when the operands are not integral
         vlog_node_t type = vlog_get_type(vlog_left(v));
         if (type == NULL)
            type = vlog_get_type(vlog_right(v));
         if (type != NULL && (vlog_kind(type) == V_CLASS_DECL
                              || (vlog_kind(type) == V_DATA_TYPE
                                  && vlog_subkind(type) == DT_STRING)))
            return type;
         return NULL;
      }
   case V_HIER_REF:
      if (vlog_has_ref(v) && vlog_kind(vlog_ref(v)) != V_FUNC_DECL
          && vlog_kind(vlog_ref(v)) != V_TASK_DECL)
         return vlog_get_type(vlog_ref(v));
      return NULL;
   case V_MOD_INST:
      return NULL;
   case V_NULL:
   case V_PATTERN_EXPR:
      if (vlog_has_type(v))
         return vlog_type(v);
      else
         return NULL;
   default:
      return NULL;
   }
}

vlog_node_t vlog_scope_member(vlog_node_t scope, ident_t id)
{
   // Declaration of a name in a package or class which is referenced with
   // the scope resolution operator

   for (vlog_node_t s = scope;; s = vlog_ref(s)) {
      const int ndecls = vlog_decls(s);
      for (int i = 0; i < ndecls; i++) {
         vlog_node_t d = vlog_decl(s, i);
         switch (vlog_kind(d)) {
         case V_IMPORT_DECL:
         case V_CONSTRUCTOR:
            break;
         default:
            if (vlog_has_ident(d) && vlog_ident(d) == id)
               return d;
            break;
         }
      }

      // The names of an enumeration are declared in the enclosing scope
      for (int i = 0; i < ndecls; i++) {
         vlog_node_t d = vlog_decl(s, i), e = NULL;
         if (vlog_kind(d) == V_IMPORT_DECL && (vlog_flags(d) & VLOG_F_EXPORT)
             && vlog_kind(s) == V_PACKAGE) {
            if (vlog_has_ident(d) && vlog_ident(d) != id)
               continue;

            if (vlog_has_ref(d)) {
               vlog_node_t m = vlog_scope_member(vlog_ref(d), id);
               if (m != NULL)
                  return m;
            }
            else {
               for (int j = 0; j < ndecls; j++) {
                  vlog_node_t imp = vlog_decl(s, j);
                  if (vlog_kind(imp) != V_IMPORT_DECL
                      || (vlog_flags(imp) & VLOG_F_EXPORT)
                      || !vlog_has_ref(imp))
                     continue;

                  vlog_node_t m = vlog_scope_member(vlog_ref(imp), id);
                  if (m != NULL)
                     return m;
               }
            }
         }
         else if (vlog_kind(d) == V_TYPE_DECL && vlog_has_type(d))
            e = vlog_get_type(vlog_type(d));
         else if (vlog_kind(d) == V_VAR_DECL && vlog_has_type(d))
            e = vlog_get_type(vlog_type(d));

         if (e == NULL || vlog_kind(e) != V_ENUM_DECL)
            continue;

         const int nnames = vlog_decls(e);
         for (int j = 0; j < nnames; j++) {
            vlog_node_t n = vlog_decl(e, j);
            if (vlog_ident(n) == id)
               return n;
         }
      }

      if (vlog_kind(s) != V_CLASS_DECL || !vlog_has_ref(s))
         return NULL;
   }
}

vlog_node_t vlog_assoc_key(vlog_node_t type)
{
   // Index type of an associative array or NULL for a wildcard index

   assert(vlog_kind(type) == V_DATA_TYPE && vlog_subkind(type) == DT_ASSOC);

   return vlog_has_ref(type) ? vlog_get_type(vlog_ref(type)) : NULL;
}

static vlog_node_t vlog_dim_of_type(vlog_node_t dt, int n)
{
   // A packed structure has no declared dimension: it is selected from
   // as though declared [size-1:0]
   if (vlog_kind(dt) == V_STRUCT_DECL)
      return NULL;

   if (vlog_kind(dt) == V_ENUM_DECL) {
      // The dimensions of the base type follow those of the enumeration
      const int nranges = vlog_ranges(dt);
      if (n < nranges)
         return vlog_range(dt, n);

      vlog_node_t base = vlog_type(dt);
      return n - nranges < (int)vlog_ranges(base)
         ? vlog_range(base, n - nranges) : NULL;
   }

   return vlog_range(dt, n);
}

vlog_node_t vlog_get_dim(vlog_node_t v, int n)
{
   switch (vlog_kind(v)) {
   case V_REF:
   case V_MEMBER_REF:
   case V_HIER_REF:
      {
         vlog_node_t decl = vlog_kind(v) == V_MEMBER_REF
            ? vlog_member_decl(v) : vlog_ref(v);
         vlog_node_t dt = vlog_get_type(vlog_type(decl));

         switch (vlog_kind(decl)) {
         case V_NET_DECL:
         case V_VAR_DECL:
         case V_PORT_DECL:
         case V_TF_PORT_DECL:
         case V_LOCAL_DECL:
         case V_PARAM_DECL:
         case V_LOCALPARAM:
            {
               const int nunpacked = vlog_ranges(decl);
               if (n < nunpacked)
                  return vlog_range(decl, n);

               // Each level of dynamic array, queue or associative array
               // takes one of the indices
               int k = n - nunpacked;
               while (k > 0 && dt != NULL && vlog_kind(dt) == V_DATA_TYPE
                      && (vlog_subkind(dt) == DT_DYNARRAY
                          || vlog_subkind(dt) == DT_QUEUE
                          || vlog_subkind(dt) == DT_ASSOC)) {
                  dt = vlog_get_type(vlog_type(dt));
                  k--;
               }

               return vlog_dim_of_type(dt, k);
            }
         default:
            return vlog_dim_of_type(dt, n);
         }
      }
   case V_USER_FCALL:
   case V_METHOD_CALL:
      return vlog_dim_of_type(vlog_get_type(v), n);
   case V_BIT_SELECT:
      return vlog_get_dim(vlog_value(v), vlog_params(v) + n);
   default:
      CANNOT_HANDLE(v);
   }
}

static tree_t vlog_unwrap_component(tree_t b)
{
   // The block for a VHDL component instance contains the bound entity

   if (tree_decls(b) > 0 && tree_kind(tree_decl(b, 0)) == T_HIER
       && tree_subkind(tree_decl(b, 0)) == T_COMPONENT && tree_stmts(b) == 1
       && tree_kind(tree_stmt(b, 0)) == T_BLOCK)
      return tree_stmt(b, 0);

   return b;
}

tree_t vlog_hier_vhdl_port(tree_t b, ident_t id)
{
   // The ports of a generated shim or Verilog module are not VHDL objects

   if (tree_decls(b) == 0)
      return NULL;

   tree_t h = tree_decl(b, 0);
   if (tree_kind(h) != T_HIER || tree_subkind(h) != T_ARCH)
      return NULL;

   const int nports = tree_ports(b);
   for (int i = 0; i < nports; i++) {
      tree_t p = tree_port(b, i);
      if (tree_kind(p) == T_PORT_DECL && ident_casecmp(tree_ident(p), id))
         return p;
   }

   return NULL;
}

tree_t vlog_hier_target_block(tree_t b, ident_t id)
{
   // The target may be inside the VHDL entity below a generated shim

   if (vlog_hier_vhdl_port(b, id) != NULL)
      return b;

   const int ndecls = tree_decls(b);
   for (int i = 0; i < ndecls; i++) {
      tree_t d = tree_decl(b, i);
      switch (tree_kind(d)) {
      case T_SIGNAL_DECL:
         if (ident_casecmp(tree_ident(d), id))
            return b;
         break;
      case T_VERILOG:
         {
            vlog_node_t vd = tree_vlog(d);
            if (vlog_kind(vd) != V_PORT_MAP && vlog_has_ident(vd)
                && vlog_ident(vd) == id)
               return b;
         }
         break;
      default:
         break;
      }
   }

   if (tree_stmts(b) == 1 && tree_kind(tree_stmt(b, 0)) == T_BLOCK)
      return vlog_hier_target_block(tree_stmt(b, 0), id);

   return b;
}

tree_t vlog_iface_port_target(tree_t b, vlog_node_t port, ident_t id)
{
   // The instance of an interface connected to a port of this block

   const int ndecls = tree_decls(b);
   for (int i = 0; i < ndecls; i++) {
      tree_t d = tree_decl(b, i);
      if (tree_kind(d) != T_VERILOG)
         continue;

      vlog_node_t vd = tree_vlog(d);
      if (vlog_kind(vd) != V_IFACE_PORT)
         continue;
      else if (port != NULL && id != NULL && vd == port) {
         // An element of an array of ports
         if (tree_ident(d) == id)
            return tree_has_ref(d) ? tree_ref(d) : NULL;
      }
      else if (port != NULL ? vd == port : vlog_ident(vd) == id)
         return tree_has_ref(d) ? tree_ref(d) : NULL;
   }

   return NULL;
}

static bool vlog_block_has_child(tree_t b, ident_t id)
{
   if (tree_kind(b) != T_BLOCK)
      return false;

   const int nstmts = tree_stmts(b);
   for (int i = 0; i < nstmts; i++) {
      tree_t s = tree_stmt(b, i);
      if (tree_kind(s) == T_BLOCK && ident_casecmp(tree_ident(s), id))
         return true;
   }

   return false;
}

static bool vlog_block_has_modport(tree_t b, ident_t id)
{
   if (tree_decls(b) == 0)
      return false;

   tree_t h = tree_decl(b, 0);
   if (tree_kind(h) != T_HIER || !tree_has_ref(h)
       || tree_kind(tree_ref(h)) != T_VERILOG)
      return false;

   vlog_node_t body = tree_vlog(tree_ref(h));
   if (!(vlog_flags(body) & VLOG_F_INTERFACE))
      return false;

   const int ndecls = vlog_decls(body);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(body, i);
      if (vlog_kind(d) == V_MODPORT && vlog_ident(d) == id)
         return true;
   }

   return false;
}

ident_t vlog_mod_ref_name(vlog_node_t v)
{
   // The name of an element of an instance array includes the index

   assert(vlog_kind(v) == V_MOD_REF);

   if (vlog_params(v) == 0)
      return vlog_ident(v);

   int64_t index;
   if (!vlog_get_const(vlog_param(v, 0), &index)) {
      error_at(vlog_loc(v), "nonconstant index into instance array %pQ",
               vlog_ident(v));
      return vlog_ident(v);
   }

   return ident_sprintf("%s[%"PRIi64"]", istr(vlog_ident(v)), index);
}

bool vlog_block_is_named(tree_t b, ident_t name)
{
   if (ident_casecmp(tree_ident(b), name))
      return true;

   if (tree_decls(b) == 0)
      return false;

   tree_t h = tree_decl(b, 0);
   if (tree_kind(h) != T_HIER || !tree_has_ref(h)
       || tree_kind(tree_ref(h)) != T_VERILOG)
      return false;

   vlog_node_t body = tree_vlog(tree_ref(h));
   if (!vlog_has_ident2(body))
      return false;

   const char *mod = istr(vlog_ident2(body));
   const char *dot = strrchr(mod, '.');
   return ident_casecmp(ident_new(dot ? dot + 1 : mod), name);
}

static tree_t mod_ref_alias = NULL;
static ident_t mod_ref_alias_id = NULL;

void vlog_set_mod_ref_alias(ident_t id, tree_t block)
{
   mod_ref_alias_id = id;
   mod_ref_alias = block;
}

tree_t vlog_walk_mod_refs(vlog_node_t v, tree_t where)
{
   assert(vlog_kind(v) == V_MOD_REF);

   if (vlog_has_value(v)) {
      if ((where = vlog_walk_mod_refs(vlog_value(v), where)) == NULL)
         return NULL;
   }
   else if (!vlog_has_ref(v)) {
      if (mod_ref_alias != NULL && vlog_mod_ref_name(v) == mod_ref_alias_id)
         return mod_ref_alias;

      if (!vlog_block_has_child(where, vlog_mod_ref_name(v))) {
         where = tree_container(where);
         assert(tree_kind(where) == T_ELAB);
      }
   }
   else if (vlog_kind(vlog_ref(v)) == V_IFACE_PORT) {
      assert(tree_kind(where) == T_BLOCK);

      vlog_node_t port = vlog_ref(v);
      tree_t target = vlog_iface_port_target(
         where, port, vlog_ranges(port) > 0 ? vlog_mod_ref_name(v) : NULL);
      if (target == NULL)
         error_at(vlog_loc(v), "interface port %pQ is not connected",
                  vlog_ident(v));

      return target;
   }
   else {
      assert(vlog_kind(vlog_ref(v)) == V_MOD_INST);
      assert(tree_kind(where) == T_BLOCK);
   }

   ident_t id = vlog_mod_ref_name(v);

   // An element of a VHDL for-generate has the index in parentheses
   ident_t alt = id;
   if (vlog_params(v) > 0) {
      int64_t index;
      if (vlog_get_const(vlog_param(v, 0), &index))
         alt = ident_sprintf("%s(%"PRIi64")", istr(vlog_ident(v)), index);
   }

   const int nstmts = tree_stmts(where);
   for (int i = 0; i < nstmts; i++) {
      tree_t s = tree_stmt(where, i);
      if (tree_kind(s) == T_BLOCK && (ident_casecmp(tree_ident(s), id)
                                      || ident_casecmp(tree_ident(s), alt)))
         return vlog_unwrap_component(s);
   }

   // A VHDL entity instantiated from Verilog is one level down in the shim
   if (nstmts == 1 && tree_kind(tree_stmt(where, 0)) == T_BLOCK) {
      tree_t inner = tree_stmt(where, 0);
      const int ninner = tree_stmts(inner);
      for (int i = 0; i < ninner; i++) {
         tree_t s = tree_stmt(inner, i);
         if (tree_kind(s) == T_BLOCK && (ident_casecmp(tree_ident(s), id)
                                         || ident_casecmp(tree_ident(s), alt)))
            return vlog_unwrap_component(s);
      }
   }

   if (tree_kind(where) == T_BLOCK) {
      // The name of a modport selects the same objects as the interface
      if (vlog_block_has_modport(where, id))
         return where;

      tree_t target = vlog_iface_port_target(where, NULL, id);
      if (target != NULL)
         return target;
   }

   error_at(vlog_loc(v), "cannot find %pQ in hierarchical identifier", id);
   return NULL;
}

static int design_precision = -1;

void vlog_set_design_precision(int log10)
{
   design_precision = log10;
}

int vlog_design_precision(void)
{
   return design_precision;
}

int64_t vlog_time_quantum(unsigned log10)
{
   int64_t quantum = 1;
   for (unsigned i = 0; i < log10; i++)
      quantum *= 10;

   return quantum;
}

int64_t vlog_time_resolve(int64_t ival)
{
   // A module without a timescale uses the default time unit and
   // precision of 1ns/1ns.  IEEE 1800-2017 3.14.2.3 leaves the default
   // to the implementation.  nvc picks 1ns so that a delay such as #10
   // has a plausible magnitude for digital logic and matches the unit
   // most often written in VHDL, and a precision of 1ns keeps such
   // delays integral

   if (VLOG_TIME_LOG10(ival) != VLOG_TIME_UNSCALED)
      return ival;

   const int64_t high = ival & ~((INT64_C(1) << 59) - 1);
   return high | VLOG_TIME_ENCODE(vlog_time_quantum(VLOG_DEFAULT_LOG10),
                                  VLOG_DEFAULT_LOG10);
}

int64_t vlog_time_literal(int64_t fs)
{
   // Value of a time literal in an expression where there is no timescale

   const int64_t unit =
      VLOG_TIME_UNIT(vlog_time_resolve(VLOG_TIME_ENCODE(1, VLOG_TIME_UNSCALED)));
   return (fs + unit / 2) / unit;
}

int64_t vlog_round_time(int64_t fs, unsigned log10)
{
   // Round to the nearest multiple of the quantum with ties rounding up

   const int64_t quantum = vlog_time_quantum(log10);
   return (fs + quantum / 2) / quantum * quantum;
}

typedef struct {
   vlog_node_t  subr;
   vlog_node_t *actuals;
   int          nbound;
} bind_default_t;

static bool bind_default_pred(vlog_node_t v, void *ctx)
{
   return vlog_kind(v) == V_REF && vlog_has_ref(v)
      && vlog_kind(vlog_ref(v)) == V_TF_PORT_DECL;
}

static vlog_node_t bind_default_cb(vlog_node_t v, void *ctx)
{
   const bind_default_t *b = ctx;
   if (vlog_kind(v) != V_REF || !vlog_has_ref(v))
      return v;

   for (int j = 0; j < b->nbound; j++) {
      if (vlog_ident(vlog_ref(v)) == vlog_ident(vlog_port(b->subr, j))
          && b->actuals[j] != NULL)
         return b->actuals[j];
   }

   return v;
}

vlog_node_t vlog_bind_default(vlog_node_t subr, vlog_node_t value,
                              vlog_node_t *actuals, int nbound)
{
   // The default value of an argument can use the arguments before it

   if (nbound == 0)
      return value;

   bind_default_t b = { .subr = subr, .actuals = actuals, .nbound = nbound };
   if (vlog_kind(value) == V_REF)
      return bind_default_cb(value, &b);

   vlog_node_t copy = vlog_copy(value, bind_default_pred, NULL);
   return vlog_rewrite(copy, NULL, bind_default_cb, &b);
}

int64_t vlog_label_hash(ident_t name)
{
   uint64_t hash = UINT64_C(14695981039346656037);
   for (const char *p = istr(name); *p != '\0'; p++)
      hash = (hash ^ (uint8_t)*p) * UINT64_C(1099511628211);
   return hash;
}
