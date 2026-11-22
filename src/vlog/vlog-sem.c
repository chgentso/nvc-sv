//
//  Copyright (C) 2022-2026  Nick Gasson
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
#include "array.h"
#include "diag.h"
#include "hash.h"
#include "ident.h"
#include "mask.h"
#include "vlog/vlog-node.h"
#include "vlog/vlog-number.h"
#include "vlog/vlog-phase.h"
#include "vlog/vlog-util.h"

#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>

typedef uint32_t type_mask_t;
#define TM(x) (1u << (x))
#define TM_ERROR  0
#define TM_ANY    ~0u
#define TM_EMPTY  TM(31)
#define TM_CONST  TM(30)
#define TM_CLASS  TM(29)
#define TM_STRUCT TM(28)
#define TM_UNION  TM(27)
#define TM_ENUM   TM(26)
#define TM_VOID   TM(25)
#define TM_ARRAY  \
   (TM(DT_DYNARRAY) | TM(DT_QUEUE) | TM(DT_STRING) | TM(DT_ASSOC))
#define TM_INTEGRAL \
   (TM(DT_LOGIC) | TM(DT_INTEGER) | TM(DT_BYTE) | TM(DT_SHORTINT) \
    | TM(DT_INT) | TM(DT_LONGINT) | TM(DT_TIME) | TM(DT_BIT) | TM_ENUM \
    | TM(DT_IMPLICIT))
#define TM_REAL (TM(DT_REAL) | TM(DT_SHORTREAL) | TM(DT_REALTIME))
#define TM_STRICT (TM_STRUCT | TM_UNION | TM_CLASS)

static void vlog_check_decls(vlog_node_t v);
static void vlog_check_stmts(vlog_node_t v);
static void vlog_check_ranges(vlog_node_t v);
static type_mask_t vlog_check_expr(vlog_node_t v);
static void vlog_check_ref_ports(vlog_node_t v);
static type_mask_t vlog_check_cast_call(vlog_node_t v);

static void name_for_diag(diag_t *d, vlog_node_t v, const char *alt)
{
   switch (vlog_kind(v)) {
   case V_REF:
      diag_printf(d, "'%s'", istr(vlog_ident(v)));
      break;
   default:
      diag_printf(d, "%s", alt);
      break;
   }
}

static type_mask_t get_type_mask(vlog_node_t v)
{
   switch (vlog_kind(v)) {
   case V_DATA_TYPE:
      switch (vlog_subkind(v)) {
      case DT_PROCESS:
      case DT_VIRTUAL:
      case DT_CHANDLE:
      case DT_EVENT:
      case DT_MAILBOX:
      case DT_SEMAPHORE:
         return TM(vlog_subkind(v)) | TM_CLASS;   // Can be compared with null
      default:
         return TM(vlog_subkind(v));
      }
   case V_STRUCT_DECL:
      return (vlog_flags(v) & VLOG_F_PACKED) ? TM_INTEGRAL : TM_STRUCT;
   case V_UNION_DECL:
      return (vlog_flags(v) & VLOG_F_PACKED) ? TM_INTEGRAL : TM_UNION;
   case V_CLASS_DECL:
      return TM_CLASS;
   case V_ENUM_DECL:
      return TM_ENUM;
   case V_ENUM_NAME:
      return TM_ENUM | TM_CONST;
   case V_PARAM_DECL:
   case V_LOCALPARAM:
   case V_GENVAR_DECL:
      return get_type_mask(vlog_type(v)) | TM_CONST;
   case V_VAR_DECL:
   case V_NET_DECL:
   case V_PORT_DECL:
   case V_TF_PORT_DECL:
   case V_TYPE_DECL:
   case V_LOCAL_DECL:
      return get_type_mask(vlog_type(v));
   case V_FUNC_DECL:
      if (!vlog_has_type(v))
         return TM_VOID;
      return get_type_mask(vlog_type(v));
   case V_TASK_DECL:
      return TM_VOID;
   case V_MODULE:
      return TM_INTEGRAL;   // Scope given to a system task
   default:
      CANNOT_HANDLE(v);
   }
}

static const char *type_mask_str(type_mask_t tm)
{
   switch (tm & ~(TM_CONST)) {
   case TM(DT_LOGIC): return "logic";
   case TM(DT_BIT): return "bit";
   case TM(DT_BYTE): return "byte";
   case TM(DT_INT): return "int";
   case TM(DT_SHORTINT): return "shortint";
   case TM(DT_REAL): return "real";
   case TM(DT_SHORTREAL): return "shortreal";
   case TM(DT_TIME): return "time";
   case TM(DT_STRING): return "string";
   case TM(DT_DYNARRAY): return "dynamic array";
   case TM(DT_QUEUE): return "queue";
   case TM(DT_ASSOC): return "associative array";
   case TM_REAL: return "real";
   case TM_CLASS: return "class";
   case TM(DT_PROCESS) | TM_CLASS: return "process";
   case TM_STRUCT: return "struct";
   case TM_UNION: return "union";
   case TM_ENUM: return "enum";
   default: return (tm & TM_INTEGRAL) ? "integral" : "unknown";
   }
}

static int vlog_unpacked_dims(vlog_node_t v)
{
   switch (vlog_kind(v)) {
   case V_REF:
   case V_MEMBER_REF:
      {
         vlog_node_t decl = vlog_has_ref(v) ? vlog_ref(v) : NULL;
         if (decl == NULL)
            return 0;

         int ndims = 0;
         switch (vlog_kind(decl)) {
         case V_VAR_DECL:
         case V_NET_DECL:
         case V_PORT_DECL:
         case V_LOCAL_DECL:
         case V_TF_PORT_DECL:
         case V_PARAM_DECL:
         case V_LOCALPARAM:
            ndims += vlog_ranges(decl);
            if (vlog_has_type(decl)) {
               vlog_node_t dt = vlog_get_type(vlog_type(decl));
               if (dt != NULL && vlog_kind(dt) == V_DATA_TYPE
                   && vlog_subkind(dt) == DT_DYNARRAY)
                  ndims += vlog_ranges(dt);
            }
            break;
         default:
            break;
         }
         return ndims;
      }
   case V_BIT_SELECT:
      {
         const int d = vlog_unpacked_dims(vlog_value(v)) - vlog_params(v);
         return d > 0 ? d : 0;
      }
   case V_PART_SELECT:
      return vlog_unpacked_dims(vlog_value(v));
   default:
      return 0;
   }
}

static void vlog_check_concat_lvalue(vlog_node_t v)
{
   if (vlog_kind(v) == V_CONCAT)
      return;

   if (vlog_unpacked_dims(v) > 0) {
      error_at(vlog_loc(v), "unpacked array cannot be used in an lvalue "
               "concatenation");
      return;
   }

   vlog_node_t type = vlog_get_type(v);
   if (type == NULL)
      return;

   const type_mask_t mask = get_type_mask(type);
   if (mask != TM_ERROR && !(mask & TM_INTEGRAL))
      error_at(vlog_loc(v), "non-integral target of type '%s' cannot be used "
               "in an lvalue concatenation", type_mask_str(mask));
}

static bool vlog_is_handle_member(vlog_node_t v)
{
   vlog_node_t pt = vlog_get_type(vlog_value(v));
   return pt != NULL
      && (vlog_kind(pt) == V_CLASS_DECL
          || (vlog_kind(pt) == V_DATA_TYPE && vlog_subkind(pt) == DT_VIRTUAL));
}

static void vlog_check_variable_lvalue(vlog_node_t v, vlog_node_t where)
{
   switch (vlog_kind(v)) {
   case V_VAR_DECL:
   case V_LOCAL_DECL:
      if (vlog_flags(v) & VLOG_F_CONST)
         error_at(vlog_loc(where), "cannot assign to const variable '%s'",
                  istr(vlog_ident(v)));
      return;
   case V_FUNC_DECL:
   case V_STRUCT_DECL:
   case V_ENUM_DECL:
   case V_UNION_DECL:
   case V_GENVAR_DECL:
   case V_TF_PORT_DECL:
   case V_HIER_REF:
      return;
   case V_THIS:
   case V_METHOD_CALL:
   case V_USER_FCALL:
      if (v != where)
         return;
      break;
   case V_REF:
      if (vlog_has_ref(v))
         vlog_check_variable_lvalue(vlog_ref(v), v);
      return;
   case V_BIT_SELECT:
   case V_PART_SELECT:
      vlog_check_variable_lvalue(vlog_value(v), v);
      return;
   case V_MEMBER_REF:
      // Modifying a property of a const class handle or virtual interface
      // modifies the referenced object, not the handle itself (8.19)
      if (vlog_is_handle_member(v)) {
         if (vlog_has_ref(v))
            vlog_check_variable_lvalue(vlog_ref(v), v);
         return;
      }
      vlog_check_variable_lvalue(vlog_value(v), v);
      return;
   case V_CONCAT:
      {
         const int nparams = vlog_params(v);
         for (int i = 0; i < nparams; i++) {
            vlog_node_t p = vlog_param(v, i);
            vlog_check_variable_lvalue(p, p);
            vlog_check_concat_lvalue(p);
         }
      }
      return;
   case V_STREAM:
      {
         const int nparams = vlog_params(v);
         for (int i = 0; i < nparams; i++) {
            vlog_node_t p = vlog_param(v, i);
            vlog_check_variable_lvalue(p, p);
         }
      }
      return;
   case V_PORT_DECL:
      if (vlog_has_ref(v)) {
         vlog_check_variable_lvalue(vlog_ref(v), where);
         return;
      }
      else if (!is_implicit_data_type(vlog_type(v)))
         return;
      break;
   default:
      break;
   }

   diag_t *d = diag_new(DIAG_ERROR, vlog_loc(where));
   name_for_diag(d, where, "target");
   diag_printf(d, " cannot be assigned in a procedural block");
   diag_emit(d);
}

static void vlog_check_net_lvalue(vlog_node_t v, vlog_node_t where)
{
   switch (vlog_kind(v)) {
   case V_VAR_DECL:
      if (vlog_flags(v) & VLOG_F_CONST)
         error_at(vlog_loc(where), "cannot assign to const variable '%s'",
                  istr(vlog_ident(v)));
      return;
   case V_NET_DECL:
   case V_HIER_REF:
      return;
   case V_PORT_DECL:
      if (vlog_has_ref(v)) {
         vlog_check_net_lvalue(vlog_ref(v), where);
         return;
      }
      else if (vlog_subkind(v) != V_PORT_INPUT)
         return;
      else if (vlog_has_type(v) && vlog_kind(vlog_type(v)) == V_TYPE_DECL
               && (vlog_flags(vlog_type(v)) & VLOG_F_NETTYPE))
         return;    // Merged with the net connected to the port
      else
         break;
   case V_REF:
      if (vlog_has_ref(v))
         vlog_check_net_lvalue(vlog_ref(v), v);
      return;
   case V_BIT_SELECT:
   case V_PART_SELECT:
   case V_MEMBER_REF:
      vlog_check_net_lvalue(vlog_value(v), v);
      return;
   case V_CONCAT:
      {
         const int nparams = vlog_params(v);
         for (int i = 0; i < nparams; i++) {
            vlog_node_t p = vlog_param(v, i);
            vlog_check_net_lvalue(p, p);
            vlog_check_concat_lvalue(p);
         }
      }
      return;
   default:
      break;
   }

   diag_t *d = diag_new(DIAG_ERROR, vlog_loc(where));
   name_for_diag(d, where, "target");
   diag_printf(d, " cannot be driven by continuous assignment");
   diag_emit(d);
}

static bool in_function = false;
static bool in_final = false;
static bool in_sva_expr = false;

static bool vlog_is_assoc_type(vlog_node_t type)
{
   return type != NULL && vlog_kind(type) == V_DATA_TYPE
      && vlog_subkind(type) == DT_ASSOC;
}

static bool vlog_same_assoc_type(vlog_node_t a, vlog_node_t b)
{
   vlog_node_t ka = vlog_assoc_key(a), kb = vlog_assoc_key(b);
   if ((ka == NULL) != (kb == NULL))
      return false;
   else if (ka != NULL) {
      if (get_type_mask(ka) != get_type_mask(kb))
         return false;
      else if (vlog_size(ka) != vlog_size(kb)
               || vlog_is_signed(ka) != vlog_is_signed(kb))
         return false;
   }

   vlog_node_t ea = vlog_get_type(vlog_type(a));
   vlog_node_t eb = vlog_get_type(vlog_type(b));
   if (vlog_is_assoc_type(ea) || vlog_is_assoc_type(eb))
      return vlog_is_assoc_type(ea) && vlog_is_assoc_type(eb)
         && vlog_same_assoc_type(ea, eb);

   return get_type_mask(ea) == get_type_mask(eb);
}

static void vlog_check_same_type(vlog_node_t left, type_mask_t lmask,
                                 vlog_node_t right, type_mask_t rmask)
{
   if (lmask == TM_ERROR || rmask == TM_ERROR)
      return;   // Suppress cascading errors
   else if (vlog_kind(right) == V_HIER_REF && !vlog_has_ref(right))
      return;   // Type not known until elaboration
   else if (vlog_kind(left) == V_HIER_REF && !vlog_has_ref(left))
      return;
   else if ((vlog_kind(left) == V_REF && vlog_has_ref(left)
             && vlog_kind(vlog_ref(left)) == V_LOCAL_DECL
             && (vlog_flags(vlog_ref(left)) & VLOG_F_FORWARD))
            || (vlog_kind(right) == V_REF && vlog_has_ref(right)
                && vlog_kind(vlog_ref(right)) == V_LOCAL_DECL
                && (vlog_flags(vlog_ref(right)) & VLOG_F_FORWARD)))
      return;   // Deferred foreach index type not known until lowering

   vlog_node_t ltype = vlog_get_type(left), rtype = vlog_get_type(right);
   if (ltype != NULL && rtype != NULL && vlog_kind(ltype) == V_DATA_TYPE
       && vlog_kind(rtype) == V_DATA_TYPE && vlog_subkind(ltype) == DT_PROCESS
       && vlog_subkind(rtype) == DT_PROCESS)
      return;

   if (vlog_kind(right) == V_CONCAT && ltype != NULL
       && vlog_kind(ltype) == V_DATA_TYPE
       && (vlog_subkind(ltype) == DT_QUEUE
           || vlog_subkind(ltype) == DT_DYNARRAY))
      return;   // The elements are the items of the concatenation

   if (vlog_is_assoc_type(ltype) || vlog_is_assoc_type(rtype)) {
      if (vlog_kind(right) == V_PATTERN_EXPR)
         return;
      else if (!vlog_is_assoc_type(ltype) || !vlog_is_assoc_type(rtype)
               || !vlog_same_assoc_type(ltype, rtype))
         error_at(vlog_loc(right), "value is not compatible with an "
                  "associative array of a different type");
      return;
   }
   else if (((lmask | rmask) & TM_STRICT) == 0)
      return;   // Convertible
   else if (lmask & rmask) {
      if (vlog_kind(left) == V_NULL)
         vlog_set_type(left, vlog_get_type(right));
      else if (vlog_kind(right) == V_NULL)
         vlog_set_type(right, vlog_get_type(left));
      else {
         vlog_node_t ltype = vlog_get_type(left);
         vlog_node_t rtype = vlog_get_type(right);

         if (ltype == NULL || rtype == NULL)
            return;   // Not known until elaboration
         else if (vlog_kind(ltype) == V_CLASS_DECL
                  && vlog_kind(rtype) == V_CLASS_DECL) {
            // Upcasts are always allowed and a downcast needs $cast
            // which is not implemented: this is checked at runtime only
            if (vlog_class_derives(rtype, ltype))
               return;
         }
         else if (vlog_kind(ltype) == V_DATA_TYPE
                  && vlog_kind(rtype) == V_DATA_TYPE
                  && vlog_subkind(ltype) == vlog_subkind(rtype)) {
            // These types are built in and have no declaration to compare
            if (vlog_subkind(ltype) != DT_VIRTUAL
                || !vlog_has_ref(ltype) || !vlog_has_ref(rtype)
                || vlog_ref(ltype) == vlog_ref(rtype))
               return;
         }

         if (ltype != rtype)
            error_at(vlog_loc(right), "value of type '%s' is not compatible "
                     "with '%s'", istr(vlog_ident(rtype)),
                     istr(vlog_ident(ltype)));
      }
   }
   else
      error_at(vlog_loc(right), "value of type '%s' is not compatible with "
               "'%s'", type_mask_str(rmask), type_mask_str(lmask));
}

static void vlog_check_dpi_export(vlog_node_t v, vlog_node_t scope)
{
   // An exported subroutine cannot have unsized arguments (35.5.6.1)

   const int ndecls = vlog_decls(scope);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t sub = vlog_decl(scope, i);
      const vlog_kind_t kind = vlog_kind(sub);
      if ((kind != V_FUNC_DECL && kind != V_TASK_DECL)
          || (vlog_flags(sub) & VLOG_F_DPI)
          || vlog_ident(sub) != vlog_ident(v))
         continue;

      const int nports = vlog_ports(sub);
      for (int j = 0; j < nports; j++) {
         vlog_node_t port = vlog_port(sub, j);
         if (vlog_kind(port) == V_TF_PORT_DECL && vlog_is_open_array(port)) {
            error_at(vlog_loc(v), "exported DPI subroutine '%s' cannot have "
                     "dynamic array argument '%s'", istr(vlog_ident(v)),
                     istr(vlog_ident(port)));
            return;
         }
      }

      return;
   }
}

static void vlog_check_decls(vlog_node_t v)
{
   const int ndecls = vlog_decls(v);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(v, i);
      vlog_check(d);

      if (vlog_kind(d) == V_DPI_EXPORT)
         vlog_check_dpi_export(d, v);
   }
}

static void vlog_blocking_cb(vlog_node_t v, void *ctx)
{
   switch (vlog_kind(v)) {
   case V_TIMING:
   case V_WAIT:
   case V_FORK:
   case V_FORK_CTRL:
   case V_USER_TCALL:
   case V_METHOD_CALL:
   case V_DISABLE:
   case V_EVENT_CONTROL:
   case V_DELAY_CONTROL:
      *(bool *)ctx = true;
      break;
   default:
      break;
   }
}

static void vlog_check_always_blocks(vlog_node_t v)
{
   // A plain always procedure which can never wait would loop forever
   // without time advancing

   if (vlog_subkind(v) != V_ALWAYS_PLAIN)
      return;

   bool blocks = false;
   vlog_visit(v, vlog_blocking_cb, &blocks);

   // Legal, but the procedure never lets time advance: a warning, not an
   // error, so that nothing the standard allows is refused
   if (!blocks)
      warn_at(vlog_loc(v), "always procedure has no timing control, delay "
              "or blocking statement so it would loop forever without "
              "advancing time");
}

static void vlog_check_stmts(vlog_node_t v)
{
   const int nstmts = vlog_stmts(v);
   for (int i = 0; i < nstmts; i++)
      vlog_check(vlog_stmt(v, i));
}

static void vlog_check_ranges(vlog_node_t v)
{
   const int nranges = vlog_ranges(v);
   for (int i = 0; i < nranges; i++)
      vlog_check(vlog_range(v, i));
}

static type_mask_t vlog_check_params(vlog_node_t v)
{
   type_mask_t mask = TM_CONST;
   const int nparams = vlog_params(v);
   for (int i = 0; i < nparams; i++) {
      vlog_node_t p = vlog_param(v, i);
      if (vlog_kind(p) == V_EVENT_CONTROL)
         vlog_check(p);
      else
         mask &= vlog_check_expr(p);
   }
   return mask;
}

static void vlog_check_enum_value(vlog_node_t target, vlog_node_t value)
{
   // An enumeration variable can only be assigned a value of the same
   // enumeration type, but not every expression has a known type

   vlog_node_t ttype = vlog_get_type(target);
   if (ttype == NULL || vlog_kind(ttype) != V_ENUM_DECL)
      return;

   vlog_node_t vtype = vlog_get_type(value);
   if (vtype != NULL && vlog_kind(vtype) == V_ENUM_DECL)
      return;

   bool bad = false;
   switch (vlog_kind(value)) {
   case V_NUMBER:
   case V_REAL:
   case V_STRING:
   case V_BINARY:
   case V_UNARY:
      bad = true;
      break;
   case V_COND_EXPR:
      vlog_check_enum_value(target, vlog_left(value));
      vlog_check_enum_value(target, vlog_right(value));
      return;
   case V_REF:
   case V_MEMBER_REF:
   case V_BIT_SELECT:
   case V_USER_FCALL:
      bad = vtype != NULL;
      break;
   default:
      break;
   }

   if (bad)
      error_at(vlog_loc(value), "cannot assign this value to a variable of "
               "enumeration type without a cast");
}

static bool vlog_nba_target_is_automatic(vlog_node_t target)
{
   // Class objects, the elements they own and the interface instance
   // behind a virtual interface outlive the assignment

   for (;;) {
      switch (vlog_kind(target)) {
      case V_BIT_SELECT:
      case V_PART_SELECT:
         target = vlog_value(target);
         break;
      case V_MEMBER_REF:
         {
            vlog_node_t prefix = vlog_value(target);
            vlog_node_t type = vlog_get_type(prefix);
            if (type == NULL || vlog_kind(type) == V_CLASS_DECL
                || (vlog_kind(type) == V_DATA_TYPE
                    && vlog_subkind(type) == DT_VIRTUAL))
               return false;
            target = prefix;
         }
         break;
      case V_REF:
         {
            if (!vlog_has_ref(target))
               return false;

            vlog_node_t d = vlog_ref(target);
            switch (vlog_kind(d)) {
            case V_TF_PORT_DECL:
               return !(vlog_flags(d) & VLOG_F_IMPLICIT_STATIC);
            case V_LOCAL_DECL:
               return true;
            default:
               return false;
            }
         }
      default:
         return false;
      }
   }
}

static vlog_node_t vlog_unpacked_element(vlog_node_t v)
{
   // The element type of a whole unpacked array named by V

   if (vlog_kind(v) != V_REF || !vlog_has_ref(v))
      return NULL;

   vlog_node_t decl = vlog_ref(v);
   switch (vlog_kind(decl)) {
   case V_VAR_DECL:
   case V_NET_DECL:
   case V_PORT_DECL:
   case V_TF_PORT_DECL:
   case V_LOCAL_DECL:
      break;
   default:
      return NULL;
   }

   if (!vlog_has_type(decl))
      return NULL;

   vlog_node_t type = vlog_get_type(vlog_type(decl));
   bool unpacked = vlog_is_fixed_unpacked(decl);
   while (type != NULL && vlog_kind(type) == V_DATA_TYPE
          && (vlog_subkind(type) == DT_QUEUE
              || vlog_subkind(type) == DT_DYNARRAY)) {
      type = vlog_get_type(vlog_type(type));
      unpacked = true;
   }

   return unpacked ? type : NULL;
}

static bool vlog_equivalent_elements(vlog_node_t a, vlog_node_t b)
{
   // Packed and built-in integral types are equivalent if they have the
   // same number of bits, signedness and state (6.22.2)

   const type_mask_t ma = get_type_mask(a), mb = get_type_mask(b);
   const bool ia = (ma & TM_INTEGRAL) && !(ma & TM_ENUM);
   const bool ib = (mb & TM_INTEGRAL) && !(mb & TM_ENUM);

   if (ia && ib)
      return vlog_size(a) == vlog_size(b)
         && vlog_is_signed(a) == vlog_is_signed(b)
         && vlog_is_two_state(a) == vlog_is_two_state(b);
   else if (ia || ib)
      return !((ma | mb) & (TM_REAL | TM(DT_STRING)));
   else
      return true;   // Checked elsewhere
}

static void vlog_check_unpacked_assign(vlog_node_t target, vlog_node_t value)
{
   // Assignment between unpacked arrays needs equivalent element types
   // (7.6)

   vlog_node_t te = vlog_unpacked_element(target);
   vlog_node_t ve = vlog_unpacked_element(value);
   if (te == NULL || ve == NULL || te == ve)
      return;
   else if (vlog_equivalent_elements(te, ve))
      return;

   diag_t *d = diag_new_relaxable(vlog_loc(value));
   diag_printf(d, "cannot assign unpacked array %s to unpacked array %s as "
               "their element types are not equivalent",
               istr(vlog_ident(value)), istr(vlog_ident(target)));
   diag_hint(d, vlog_loc(value), "the elements of an unpacked array must "
             "have the same number of bits, signedness and 2-state or "
             "4-state kind as those of the target");
   diag_emit(d);
}

static bool vlog_modport_input(vlog_node_t target, ident_t name,
                               vlog_node_t mp)
{
   // Report an assignment to an item that the modport lists as an input

   const int nmembers = vlog_decls(mp);
   for (int i = 0; i < nmembers; i++) {
      vlog_node_t m = vlog_decl(mp, i);
      if (vlog_kind(m) != V_MODPORT_PORT || !vlog_has_ident(m)
          || vlog_ident(m) != name)
         continue;
      else if (vlog_subkind(m) != V_MODPORT_INPUT)
         return false;

      diag_t *d = diag_new_relaxable(vlog_loc(target));
      diag_printf(d, "cannot assign %s which is an input of modport %s",
                  istr(name), istr(vlog_ident(mp)));
      diag_hint(d, vlog_loc(m), "declared as an input here");
      diag_emit(d);
      return true;
   }

   return false;
}

static void vlog_check_modport_write(vlog_node_t target)
{
   // A module cannot assign an interface item that its modport declares
   // as an input, nor can code that uses the interface through a virtual
   // interface with that modport (25.5, 25.9, 23.3.3.2)

   vlog_node_t v = target;
   for (;;) {
      switch (vlog_kind(v)) {
      case V_BIT_SELECT:
      case V_PART_SELECT:
         v = vlog_value(v);
         continue;
      case V_MEMBER_REF:
         {
            vlog_node_t type = vlog_get_type(vlog_value(v));
            if (type != NULL && vlog_kind(type) == V_DATA_TYPE
                && vlog_subkind(type) == DT_VIRTUAL && vlog_has_type(type)
                && vlog_kind(vlog_type(type)) == V_MODPORT
                && vlog_modport_input(target, vlog_ident(v), vlog_type(type)))
               return;
         }
         v = vlog_value(v);
         continue;
      case V_HIER_REF:
         if (vlog_has_value(v) && vlog_kind(vlog_value(v)) == V_MOD_REF)
            break;
         return;
      case V_CONCAT:
         {
            const int nparams = vlog_params(v);
            for (int i = 0; i < nparams; i++)
               vlog_check_modport_write(vlog_param(v, i));
         }
         return;
      default:
         return;
      }
      break;
   }

   vlog_node_t mr = vlog_value(v);
   if (vlog_has_value(mr) || !vlog_has_ref(mr)
       || vlog_kind(vlog_ref(mr)) != V_IFACE_PORT)
      return;

   vlog_node_t port = vlog_ref(mr);
   if (!vlog_has_ref(port))
      return;

   vlog_node_t mp = vlog_ref(port);
   if (vlog_kind(mp) != V_MODPORT)
      return;

   (void)vlog_modport_input(target, vlog_ident(v), mp);
}

static vlog_node_t vlog_select_dim(vlog_node_t v, int skip);

static bool vlog_eval_const(vlog_node_t v, int64_t *value)
{
   if (vlog_get_const(v, value))
      return true;

   if (v != NULL && vlog_kind(v) == V_BINARY) {
      int64_t l, r;
      if (vlog_eval_const(vlog_left(v), &l)
          && vlog_eval_const(vlog_right(v), &r)) {
         switch (vlog_subkind(v)) {
         case V_BINARY_PLUS:  *value = l + r; return true;
         case V_BINARY_MINUS: *value = l - r; return true;
         case V_BINARY_TIMES: *value = l * r; return true;
         default: return false;
         }
      }
   }

   return false;
}

static bool vlog_eval_bounds(vlog_node_t dim, int64_t *left, int64_t *right)
{
   return vlog_eval_const(vlog_left(dim), left)
      && vlog_eval_const(vlog_right(dim), right);
}

static int vlog_target_unpacked_dims(vlog_node_t target)
{
   switch (vlog_kind(target)) {
   case V_VAR_DECL:
   case V_LOCAL_DECL:
   case V_NET_DECL:
   case V_PORT_DECL:
   case V_TF_PORT_DECL:
      {
         int ndims = vlog_ranges(target);
         if (vlog_has_type(target)) {
            vlog_node_t dt = vlog_get_type(vlog_type(target));
            if (dt != NULL && vlog_kind(dt) == V_DATA_TYPE
                && vlog_subkind(dt) == DT_DYNARRAY)
               ndims += vlog_ranges(dt);
         }
         return ndims;
      }
   default:
      return vlog_unpacked_dims(target);
   }
}

static void vlog_check_unpacked_scalar_assign(vlog_node_t target,
                                              vlog_node_t value,
                                              type_mask_t rmask)
{
   const int ndims = vlog_target_unpacked_dims(target);
   if (ndims <= 0 || rmask == TM_ERROR)
      return;

   if (vlog_kind(value) == V_STRING) {
      vlog_node_t et = vlog_get_type(target);
      if (et != NULL && vlog_kind(et) == V_DATA_TYPE
          && vlog_subkind(et) == DT_DYNARRAY && vlog_has_type(et))
         et = vlog_get_type(vlog_type(et));

      const bool ok_byte = ndims == 1 && et != NULL
         && vlog_kind(et) == V_DATA_TYPE
         && (get_type_mask(et) & TM_INTEGRAL)
         && !vlog_is_four_state(et)
         && vlog_size(et) == 8;
      if (!ok_byte)
         error_at(vlog_loc(value), "string literal can only be assigned to a "
                  "1-dimensional unpacked array of 8-bit 2-state elements");
      return;
   }

   switch (vlog_kind(value)) {
   case V_PATTERN_EXPR:
   case V_CONCAT:
   case V_STREAM:
   case V_DYNAMIC_NEW:
   case V_COND_EXPR:
   case V_USER_FCALL:
   case V_METHOD_CALL:
   case V_CAST:
   case V_HIER_REF:
   case V_SYS_FCALL:
      return;
   default:
      break;
   }

   if ((rmask & TM_ARRAY) || vlog_unpacked_dims(value) > 0)
      return;

   error_at(vlog_loc(value), "cannot assign scalar value to unpacked array");
}

static vlog_node_t vlog_target_outer_dim(vlog_node_t target)
{
   switch (vlog_kind(target)) {
   case V_VAR_DECL:
   case V_LOCAL_DECL:
   case V_NET_DECL:
   case V_PORT_DECL:
   case V_TF_PORT_DECL:
      if (vlog_ranges(target) > 0)
         return vlog_range(target, 0);
      if (vlog_has_type(target)) {
         vlog_node_t dt = vlog_get_type(vlog_type(target));
         if (dt != NULL && vlog_kind(dt) == V_DATA_TYPE
             && vlog_subkind(dt) == DT_DYNARRAY && vlog_ranges(dt) > 0)
            return vlog_range(dt, 0);
      }
      return NULL;
   default:
      if (vlog_unpacked_dims(target) > 0)
         return vlog_select_dim(target, 0);
      return NULL;
   }
}

static void vlog_check_pattern_target(vlog_node_t target, vlog_node_t pat)
{
   if (vlog_kind(pat) != V_PATTERN_EXPR)
      return;

   const int nparams = vlog_params(pat);
   if (nparams == 0)
      return;

   bool seen_pos = false, seen_keyed = false, seen_default = false;
   bool seen_type = false;
   for (int i = 0; i < nparams; i++) {
      vlog_node_t item = vlog_param(pat, i);
      if (vlog_kind(item) != V_PATTERN_ITEM)
         continue;
      switch (vlog_subkind(item)) {
      case 0:
         if (vlog_has_left(item))
            seen_keyed = true;
         else
            seen_pos = true;
         break;
      case 1:
         seen_default = true;
         seen_keyed = true;
         break;
      case 2:
         seen_pos = true;
         break;
      case 3:
         seen_type = true;
         seen_keyed = true;
         break;
      }
   }

   if (seen_pos && seen_keyed)
      return;   // Already reported in vlog_check_pattern_expr

   vlog_node_t dim = vlog_target_outer_dim(target);
   if (dim != NULL) {
      int64_t dl, dr;
      if (!vlog_eval_bounds(dim, &dl, &dr))
         return;

      const int64_t lo = MIN(dl, dr), hi = MAX(dl, dr);
      const int64_t size = hi - lo + 1;

      if (seen_pos) {
         int64_t total = 0;
         bool known = true;
         for (int i = 0; i < nparams && known; i++) {
            vlog_node_t item = vlog_param(pat, i);
            if (vlog_subkind(item) == 2) {
               int64_t rep;
               if (!vlog_get_const(vlog_left(item), &rep) || rep <= 0)
                  known = false;
               else {
                  const int64_t inner = vlog_params(vlog_right(item));
                  if (inner > 0 && rep > (INT64_MAX - total) / inner)
                     total = size + 1;
                  else
                     total += rep * inner;
               }
            }
            else
               total++;
         }

         if (known && total != size)
            error_at(vlog_loc(pat), "array assignment pattern has %"PRIi64
                     " element%s but target array has %"PRIi64,
                     total, total == 1 ? "" : "s", size);
      }
      else if (seen_keyed && !seen_type) {
         SCOPED_A(int64_t) keys = AINIT;
         bool all_const = true;
         for (int i = 0; i < nparams; i++) {
            vlog_node_t item = vlog_param(pat, i);
            if (vlog_subkind(item) != 0 || !vlog_has_left(item))
               continue;

            int64_t k;
            if (!vlog_get_const(vlog_left(item), &k)) {
               all_const = false;
               continue;
            }

            if (k < lo || k > hi) {
               error_at(vlog_loc(vlog_left(item)), "array assignment pattern "
                        "key %"PRIi64" is outside declared range "
                        "[%"PRIi64":%"PRIi64"]", k, dl, dr);
               continue;
            }

            bool dup = false;
            for (unsigned j = 0; j < keys.count; j++) {
               if (keys.items[j] == k) {
                  dup = true;
                  break;
               }
            }

            if (dup)
               error_at(vlog_loc(vlog_left(item)), "duplicate key %"PRIi64
                        " in array assignment pattern", k);
            else
               APUSH(keys, k);
         }

         if (!seen_default && all_const && (int64_t)keys.count < size)
            error_at(vlog_loc(pat), "keyed array assignment pattern does not "
                     "cover all %"PRIi64" elements and has no default", size);
      }
      return;
   }

   // Non-array struct/union target
   vlog_node_t ttype = vlog_get_type(target);
   if (ttype != NULL && (vlog_kind(ttype) == V_STRUCT_DECL
                         || vlog_kind(ttype) == V_UNION_DECL)
       && seen_keyed) {
      const int ndecls = vlog_decls(ttype);
      LOCAL_BIT_MASK seen;
      mask_init(&seen, MAX(ndecls, 1));
      bool all_valid = true;

      for (int i = 0; i < nparams; i++) {
         vlog_node_t item = vlog_param(pat, i);
         if (vlog_subkind(item) != 0 || !vlog_has_left(item))
            continue;

         vlog_node_t key = vlog_left(item);
         if (vlog_kind(key) == V_REF && !vlog_has_ref(key)) {
            all_valid = false;   // Unknown member already reported by parser
            continue;
         }

         int idx = -1;
         if (vlog_kind(key) == V_REF && vlog_has_ref(key)) {
            vlog_node_t ref = vlog_ref(key);
            for (int j = 0; j < ndecls; j++) {
               if (vlog_decl(ttype, j) == ref) {
                  idx = j;
                  break;
               }
            }
         }

         if (idx < 0) {
            error_at(vlog_loc(key), "struct assignment pattern key must be a "
                     "member name");
            all_valid = false;
         }
         else if (mask_test(&seen, idx))
            error_at(vlog_loc(key), "duplicate member %pQ in struct assignment "
                     "pattern", vlog_ident(vlog_decl(ttype, idx)));
         else
            mask_set(&seen, idx);
      }

      if (vlog_kind(ttype) == V_STRUCT_DECL && !seen_default && !seen_type
          && all_valid && (int)mask_popcount(&seen) < ndecls)
         error_at(vlog_loc(pat), "keyed struct assignment pattern does not "
                  "specify all members and has no default");
   }
}

static void vlog_check_nbassign(vlog_node_t v)
{
   vlog_node_t target = vlog_target(v);
   type_mask_t lmask = vlog_check_expr(target);

   vlog_check_variable_lvalue(target, target);

   if (vlog_kind(target) == V_CONCAT) {
      const int nparams = vlog_params(target);
      for (int i = 0; i < nparams; i++) {
         if (vlog_nba_target_is_automatic(vlog_param(target, i)))
            error_at(vlog_loc(vlog_param(target, i)), "a non-blocking "
                     "assignment cannot write to an automatic variable or a "
                     "ref argument");
      }
   }
   else if (vlog_nba_target_is_automatic(target))
      error_at(vlog_loc(target), "a non-blocking assignment cannot write to "
               "an automatic variable or a ref argument");

   vlog_node_t value = vlog_value(v);
   type_mask_t rmask = vlog_check_expr(value);

   vlog_check_same_type(target, lmask, value, rmask);
   vlog_check_enum_value(target, value);
   vlog_check_unpacked_assign(target, value);
   vlog_check_unpacked_scalar_assign(target, value, rmask);
   vlog_check_pattern_target(target, value);
   vlog_check_modport_write(target);
}

static void vlog_check_bassign(vlog_node_t v)
{
   vlog_node_t target = vlog_target(v);
   type_mask_t lmask = vlog_check_expr(target);

   vlog_check_variable_lvalue(target, target);

   vlog_node_t value = vlog_value(v);
   type_mask_t rmask = vlog_check_expr(value);

   vlog_check_same_type(target, lmask, value, rmask);
   vlog_check_enum_value(target, value);
   vlog_check_unpacked_assign(target, value);
   vlog_check_unpacked_scalar_assign(target, value, rmask);
   vlog_check_pattern_target(target, value);
   vlog_check_modport_write(target);
}

static void vlog_check_assign(vlog_node_t v)
{
   vlog_node_t target = vlog_target(v);
   type_mask_t lmask = vlog_check_expr(target);

   vlog_check_net_lvalue(target, target);

   vlog_node_t value = vlog_value(v);
   type_mask_t rmask = vlog_check_expr(value);

   vlog_check_same_type(target, lmask, value, rmask);
   vlog_check_unpacked_scalar_assign(target, value, rmask);
   vlog_check_pattern_target(target, value);
   vlog_check_modport_write(target);
}

static void vlog_force_auto_cb(vlog_node_t v, void *ctx)
{
   if (vlog_kind(v) == V_REF && vlog_has_ref(v)) {
      vlog_node_t d = vlog_ref(v);
      if ((vlog_kind(d) == V_LOCAL_DECL || vlog_kind(d) == V_TF_PORT_DECL)
          && !(vlog_flags(d) & (VLOG_F_STATIC | VLOG_F_IMPLICIT_STATIC)))
         error_at(vlog_loc(v), "automatic variable '%s' cannot be referenced "
                  "in a force statement", istr(vlog_ident(d)));
   }
}

static void vlog_check_force(vlog_node_t v)
{
   vlog_node_t target = vlog_target(v);
   type_mask_t lmask = vlog_check_expr(target);

   vlog_check_net_lvalue(target, target);

   vlog_node_t value = vlog_value(v);
   type_mask_t rmask = vlog_check_expr(value);

   vlog_check_same_type(target, lmask, value, rmask);
   vlog_visit_only(value, vlog_force_auto_cb, NULL, V_REF);
}

static void vlog_check_release(vlog_node_t v)
{
   vlog_node_t target = vlog_target(v);

   vlog_check_expr(target);
   vlog_check_net_lvalue(target, target);
}

static void vlog_check_type(vlog_node_t v)
{
   vlog_node_t type = vlog_type(v);

   switch (vlog_kind(type)) {
   case V_CLASS_DECL:
      return;   // Checked at original declaration
   default:
      break;
   }

   vlog_check(type);
}

static void vlog_has_call_cb(vlog_node_t v, void *context)
{
   *(bool *)context = true;
}

static bool vlog_has_user_call(vlog_node_t v)
{
   bool found = false;
   vlog_visit_only(v, vlog_has_call_cb, &found, V_USER_FCALL);
   return found;
}

static void vlog_check_consistent(vlog_node_t a, vlog_node_t b)
{
   vlog_node_t at = vlog_type(a);
   vlog_node_t bt = vlog_type(b);

   if (at == bt)
      return;

   const int aranges = vlog_ranges(at);
   assert(aranges == vlog_ranges(bt));

   for (int i = 0; i < aranges; i++) {
      vlog_node_t ar = vlog_range(at, i);
      vlog_node_t br = vlog_range(bt, i);

      vlog_node_t aleft = vlog_left(ar);
      vlog_node_t bleft = vlog_left(br);

      vlog_node_t aright = vlog_right(ar);
      vlog_node_t bright = vlog_right(br);

      if (vlog_has_user_call(aleft) || vlog_has_user_call(aright)
          || vlog_has_user_call(bleft) || vlog_has_user_call(bright))
         continue;   // Constant function calls are checked after folding

      if (!vlog_equal_node(aleft, bleft) || !vlog_equal_node(aright, bright)) {
         diag_t *d = diag_new(DIAG_ERROR, vlog_loc(br));
         diag_printf(d, "inconsistent dimensions for '%s'",
                     istr(vlog_ident(b)));
         diag_hint(d, vlog_loc(a), "earlier declaration here");
         diag_emit(d);
      }
   }
}

static void vlog_check_port_decl(vlog_node_t v)
{
   if (vlog_has_ref(v)) {
      vlog_node_t ref = vlog_ref(v);
      vlog_node_t type = vlog_type(v);

      if (vlog_flags(type) & VLOG_F_SIGNED)
         vlog_set_flags(vlog_type(ref), VLOG_F_SIGNED);

      if (is_implicit_data_type(type) && vlog_ranges(type) == 0)
         vlog_set_type(v, vlog_type(ref));

      vlog_check_consistent(v, ref);

      if (!is_implicit_data_type(type)) {
         diag_t *d = diag_new(DIAG_ERROR, vlog_loc(ref));
         diag_printf(d, "completely declared port '%s' cannot be declared "
                     "again in a data type declaration", istr(vlog_ident(v)));
         diag_hint(d, vlog_loc(v), "port declaration contains a data type");
         diag_hint(d, vlog_loc(ref), "redeclared here");
         diag_emit(d);
      }
   }
   else {
      vlog_check_type(v);
      vlog_check_ranges(v);
   }

   if (vlog_has_value(v))
      vlog_check_expr(vlog_value(v));
}

static void vlog_check_net_decl(vlog_node_t v)
{
   vlog_check_type(v);
   vlog_check_ranges(v);

   if (vlog_has_value(v))
      vlog_check_expr(vlog_value(v));
}

static void vlog_check_rand_var(vlog_node_t v)
{
   const bool is_randc = !!(vlog_flags(v) & VLOG_F_RANDC);

   vlog_node_t base = vlog_get_type(vlog_type(v));
   while (base != NULL && vlog_kind(base) == V_DATA_TYPE
          && (vlog_subkind(base) == DT_DYNARRAY
              || vlog_subkind(base) == DT_QUEUE))
      base = vlog_get_type(vlog_type(base));

   if (base == NULL)
      return;   // Was earlier error

   vlog_node_t atype = vlog_get_type(vlog_type(v));
   if (vlog_is_assoc_type(atype)) {
      if (!(get_type_mask(vlog_get_type(vlog_type(atype))) & TM_INTEGRAL)) {
         warn_at(vlog_loc(v), "sorry, the elements of the rand associative "
                 "array %pQ are not randomized", vlog_ident(v));
         vlog_clear_flags(v, VLOG_F_RAND | VLOG_F_RANDC);
      }
      return;
   }

   const type_mask_t tmask = get_type_mask(base);
   const type_mask_t allow = is_randc ? TM_INTEGRAL : TM_INTEGRAL | TM_CLASS;

   if (tmask & (TM_STRUCT | TM_UNION)) {
      if (is_randc)
         error_at(vlog_loc(v), "sorry, randc variable %pQ of structure type "
                  "is not supported", vlog_ident(v));
   }
   else if (!(tmask & allow) || (tmask & (TM_REAL | TM_ARRAY))) {
      diag_t *d = diag_new(DIAG_ERROR, vlog_loc(v));
      diag_printf(d, "%s variable %pQ must have %s",
                  is_randc ? "randc" : "rand", vlog_ident(v),
                  is_randc ? "an integral or enum type"
                  : "an integral, enum, or class handle type or be an array "
                  "of these");
      diag_hint(d, vlog_loc(v), "have '%s'", type_mask_str(tmask));
      diag_emit(d);
   }
}

static void vlog_check_var_decl(vlog_node_t v)
{
   vlog_check_type(v);
   vlog_check_ranges(v);

   if (vlog_has_value(v)) {
      const type_mask_t rmask = vlog_check_expr(vlog_value(v));
      vlog_check_enum_value(v, vlog_value(v));
      vlog_check_unpacked_scalar_assign(v, vlog_value(v), rmask);
      vlog_check_pattern_target(v, vlog_value(v));
   }

   if (vlog_kind(v) == V_VAR_DECL
       && (vlog_flags(v) & (VLOG_F_RAND | VLOG_F_RANDC)))
      vlog_check_rand_var(v);
}

static void vlog_specparam_cb(vlog_node_t v, void *context)
{
   if (vlog_kind(v) != V_REF || !vlog_has_ref(v))
      return;

   vlog_node_t decl = vlog_ref(v);
   if (vlog_kind(decl) == V_LOCALPARAM && (vlog_flags(decl) & VLOG_F_SPECPARAM))
      error_at(vlog_loc(v), "specparam %pQ cannot be used in the value of a "
               "parameter", vlog_ident(decl));
}

static void vlog_check_param_decl(vlog_node_t v)
{
   if (vlog_has_value(v)) {
      vlog_check_expr(vlog_value(v));
      vlog_visit(vlog_value(v), vlog_specparam_cb, NULL);
   }
}

static void vlog_check_genvar_decl(vlog_node_t v)
{
}

static void vlog_check_type_decl(vlog_node_t v)
{
   // A class is checked at its original declaration
   if (vlog_kind(vlog_type(v)) != V_CLASS_DECL)
      vlog_check(vlog_type(v));
}

static void vlog_check_data_type(vlog_node_t v)
{
   vlog_check_ranges(v);
}

static bool vlog_has_unknown_literal(vlog_node_t v)
{
   switch (vlog_kind(v)) {
   case V_NUMBER:
      return !number_is_defined(vlog_number(v));
   case V_CONCAT:
      {
         const int nparams = vlog_params(v);
         for (int i = 0; i < nparams; i++) {
            if (vlog_has_unknown_literal(vlog_param(v, i)))
               return true;
         }
         return false;
      }
   default:
      return false;
   }
}

static void vlog_check_enum_decl(vlog_node_t v)
{
   vlog_check_decls(v);

   vlog_node_t base = vlog_has_type(v) ? vlog_get_type(vlog_type(v)) : NULL;
   const bool four_state = base != NULL && vlog_kind(base) == V_DATA_TYPE
      && vlog_subkind(base) != DT_BYTE && vlog_subkind(base) != DT_SHORTINT
      && vlog_subkind(base) != DT_INT && vlog_subkind(base) != DT_LONGINT
      && vlog_subkind(base) != DT_BIT && vlog_subkind(base) != DT_IMPLICIT;

   unsigned base_bits = 32;
   bool base_signed = true;
   if (base != NULL && vlog_kind(base) == V_DATA_TYPE) {
      base_signed = vlog_is_signed(base);
      switch (vlog_subkind(base)) {
      case DT_BYTE:     base_bits = 8; break;
      case DT_SHORTINT: base_bits = 16; break;
      case DT_INT:
      case DT_INTEGER:  base_bits = 32; break;
      case DT_LONGINT:
      case DT_TIME:     base_bits = 64; break;
      default:
         base_bits = vlog_ranges(base) > 0 ? vlog_size(base) : 1;
         break;
      }
   }

   bool after_unknown = false, have_val = false;
   int64_t sval = 0;
   uint64_t uval = 0;
   const int ndecls = vlog_decls(v);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t n = vlog_decl(v, i);
      if (!vlog_has_value(n)) {
         if (after_unknown) {
            error_at(vlog_loc(n), "enumeration name %pQ without a value "
                     "cannot follow a name with an unknown value",
                     vlog_ident(n));
            after_unknown = false;
            have_val = false;
         }
         else if (base_bits > 0 && base_bits <= 64) {
            if (i == 0) {
               have_val = true;
               sval = 0;
               uval = 0;
            }
            else if (have_val) {
               if (base_signed) {
                  const int64_t max_s = base_bits == 64
                     ? INT64_MAX : ((INT64_C(1) << (base_bits - 1)) - 1);
                  if (sval == max_s) {
                     error_at(vlog_loc(n), "enumeration name %pQ overflows "
                              "base type", vlog_ident(n));
                     have_val = false;
                  }
                  else
                     sval++;
               }
               else {
                  const uint64_t max_u = base_bits == 64
                     ? UINT64_MAX : ((UINT64_C(1) << base_bits) - 1);
                  if (uval == max_u) {
                     error_at(vlog_loc(n), "enumeration name %pQ overflows "
                              "base type", vlog_ident(n));
                     have_val = false;
                  }
                  else
                     uval++;
               }
            }
         }
         continue;
      }

      vlog_node_t value = vlog_value(n);
      if (vlog_kind(value) != V_NUMBER && vlog_kind(value) != V_CONCAT) {
         have_val = false;
         continue;
      }

      const bool has_xz = vlog_has_unknown_literal(value);
      if (has_xz) {
         if (!four_state)
            error_at(vlog_loc(value), "the value of enumeration name %pQ "
                     "cannot contain x or z as the base type is two-state",
                     vlog_ident(n));
         else
            after_unknown = true;
         have_val = false;
      }
      else if (vlog_kind(value) == V_NUMBER && base_bits > 0
               && base_bits <= 64) {
         if (vlog_flags(value) & VLOG_F_FILL) {
            const uint64_t raw = number_integer(vlog_number(value)) ?
               (base_bits == 64 ? UINT64_MAX : (UINT64_C(1) << base_bits) - 1)
               : 0;
            uval = raw;
            sval = raw ? -1 : 0;
         }
         else {
            uint64_t raw = (uint64_t)number_integer(vlog_number(value));
            if (base_bits < 64)
               raw &= (UINT64_C(1) << base_bits) - 1;
            uval = raw;
            if (base_signed && base_bits < 64
                && (raw & (UINT64_C(1) << (base_bits - 1))))
               sval = (int64_t)(raw | (~UINT64_C(0) << base_bits));
            else
               sval = (int64_t)raw;
         }
         have_val = true;
      }
      else
         have_val = false;

      if (vlog_kind(value) != V_NUMBER
          || (vlog_flags(value) & VLOG_F_FILL))
         continue;   // An unbased unsized literal fills the base type

      number_t num = vlog_number(value);

      // An unsized literal has the width of an integer
      if (base != NULL && vlog_kind(base) == V_DATA_TYPE
          && vlog_ranges(base) > 0) {
         const unsigned width = number_width(num), bits = vlog_size(base);
         if (bits > 0 && width != 32 && width != bits)
            error_at(vlog_loc(value), "the size of the value of enumeration "
                     "name %pQ is %u but the base type has %u bits",
                     vlog_ident(n), width, bits);
      }
   }
}

static void vlog_check_struct_decl(vlog_node_t v)
{
   vlog_check_decls(v);

   if (!(vlog_flags(v) & VLOG_F_PACKED))
      return;

   const int ndecls = vlog_decls(v);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(v, i);
      if (vlog_kind(d) == V_VAR_DECL && vlog_has_value(d))
         error_at(vlog_loc(d), "a member of a packed structure cannot have "
                  "a default value");
   }
}

static void vlog_check_union_decl(vlog_node_t v)
{
   vlog_check_decls(v);
}

static void vlog_const_func_cb(vlog_node_t v, void *ctx)
{
   switch (vlog_kind(v)) {
   case V_REF:
      {
         if (!vlog_has_ref(v))
            break;

         vlog_node_t decl = vlog_ref(v);
         switch (vlog_kind(decl)) {
         case V_PARAM_DECL:
         case V_LOCALPARAM:
         case V_GENVAR_DECL:
         case V_ENUM_NAME:
         case V_LOCAL_DECL:
         case V_FUNC_DECL:
         case V_TF_PORT_DECL:
            break;
         case V_VAR_DECL:
            // Variable local to this function: see vlog_lower_locals
            if (!(vlog_flags(decl) & VLOG_F_STATIC))
               vlog_clear_flags(ctx, VLOG_F_CONST);
            break;
         default:
            vlog_clear_flags(ctx, VLOG_F_CONST);
            break;
         }
      }
      break;
   case V_USER_FCALL:
      if (!(vlog_flags(vlog_ref(v)) & VLOG_F_CONST))
         vlog_clear_flags(ctx, VLOG_F_CONST);
      break;
   case V_HIER_REF:
      vlog_clear_flags(ctx, VLOG_F_CONST);
      break;
   default:
      break;
   }
}

static void vlog_check_func_decl(vlog_node_t v)
{
   vlog_set_flags(v, VLOG_F_CONST);

   vlog_check_ref_ports(v);

   const int nports = vlog_ports(v);
   for (int i = 0; i < nports; i++)
      vlog_check(vlog_port(v, i));

   vlog_check_decls(v);

   const bool was_in_function = in_function;
   in_function = true;
   vlog_check_stmts(v);
   in_function = was_in_function;

   vlog_visit(v, vlog_const_func_cb, v);
}

static void vlog_check_ref_ports(vlog_node_t v)
{
   // A subroutine with ref arguments must be automatic (13.5.2)

   const int nports = vlog_ports(v);
   for (int i = 0; i < nports; i++) {
      vlog_node_t p = vlog_port(v, i);
      if (vlog_kind(p) == V_TF_PORT_DECL && vlog_subkind(p) == V_PORT_REF
          && (vlog_flags(p) & VLOG_F_IMPLICIT_STATIC))
         error_at(vlog_loc(p), "ref argument %pQ is not allowed in static "
                  "%s %pQ", vlog_ident(p),
                  vlog_kind(v) == V_TASK_DECL ? "task" : "function",
                  vlog_ident(v));
   }
}

static void vlog_check_task_decl(vlog_node_t v)
{
   vlog_check_ref_ports(v);

   const int nports = vlog_ports(v);
   for (int i = 0; i < nports; i++)
      vlog_check(vlog_port(v, i));

   vlog_check_decls(v);
   vlog_check_stmts(v);
}

static void vlog_check_primitive(vlog_node_t udp)
{
   const int nports = vlog_ports(udp);
   for (int i = 0; i < nports; i++) {
      vlog_node_t p = vlog_port(udp, i);

      if (vlog_has_ref(p)) {
         vlog_node_t decl = vlog_ref(p);
         assert(vlog_kind(decl) == V_PORT_DECL);

         if (i == 0 && vlog_subkind(decl) != V_PORT_OUTPUT) {
            diag_t *d = diag_new(DIAG_ERROR, vlog_loc(p));
            diag_printf(d, "the first port of a primitive must be an output");
            diag_hint(d, vlog_loc(decl), "port declaration here");
            diag_emit(d);
         }
         else if (i > 0 && vlog_subkind(decl) != V_PORT_INPUT) {
            diag_t *d = diag_new(DIAG_ERROR, vlog_loc(p));
            diag_printf(d, "all ports of a primitive except the first must "
                        "be inputs");
            diag_hint(d, vlog_loc(decl), "port declaration here");
            diag_emit(d);
         }
      }
   }

   assert(vlog_stmts(udp) == 1);

   vlog_node_t table = vlog_stmt(udp, 0);
   assert(vlog_kind(table) == V_UDP_TABLE);

   const vlog_udp_kind_t kind = vlog_subkind(table);
   const int expect = kind == V_UDP_SEQ ? nports + 1 : nports;

   const int nparams = vlog_params(table);
   for (int i = 0; i < nparams; i++) {
      vlog_node_t row = vlog_param(table, i);
      assert(vlog_kind(row) == V_UDP_ENTRY);

      const int nsymbols = vlog_params(row);
      if (nsymbols != expect) {
         error_at(vlog_loc(row), "expected %d symbols in UDP table entry but "
                  "have %d", expect, nsymbols);
         break;
      }
   }
}

static void vlog_check_dimension(vlog_node_t v)
{
   if (vlog_subkind(v) == V_DIM_UNSIZED) {
      if ((vlog_flags(v) & VLOG_F_OPEN_PACKED) && !(vlog_flags(v) & VLOG_F_DPI))
         error_at(vlog_loc(v), "unsized packed dimension is only allowed in "
                  "a formal argument of a DPI import");
      return;
   }

   vlog_node_t left = vlog_left(v);
   vlog_check_expr(left);

   vlog_node_t right = vlog_right(v);
   vlog_check_expr(right);
}

static void vlog_check_localparam(vlog_node_t v)
{
   if (vlog_has_value(v)) {
      vlog_check_expr(vlog_value(v));
      if (!(vlog_flags(v) & VLOG_F_SPECPARAM))
         vlog_visit(vlog_value(v), vlog_specparam_cb, NULL);
   }
   else
      error_at(vlog_loc(v), "local parameter declaration must have a "
               "default value");
}

static void vlog_check_defparam(vlog_node_t v)
{
   vlog_check_expr(vlog_value(v));
}

static void vlog_check_case(vlog_node_t v)
{
   vlog_check_expr(vlog_value(v));

   bool seen_default = false;
   const int nstmts = vlog_stmts(v);
   for (int i = 0; i < nstmts; i++) {
      vlog_node_t item = vlog_stmt(v, i);
      assert(vlog_kind(item) == V_CASE_ITEM);

      const int nparams = vlog_params(item);
      if (nparams == 0 && !seen_default)
         seen_default = true;
      else if (nparams == 0)
         error_at(vlog_loc(item), "multiple default statements within a single "
                  "case statement");

      vlog_check_stmts(item);
   }
}

static void vlog_check_for_loop(vlog_node_t v)
{
   vlog_node_t init = vlog_left(v);
   assert(vlog_kind(init) == V_FOR_INIT);
   vlog_check_decls(init);
   vlog_check_stmts(init);

   vlog_check_expr(vlog_value(v));

   vlog_node_t step = vlog_right(v);
   assert(vlog_kind(step) == V_FOR_STEP);
   vlog_check_stmts(step);

   vlog_check_stmts(v);
}

static void vlog_check_repeat(vlog_node_t v)
{
   vlog_check_expr(vlog_value(v));

   vlog_check_stmts(v);
}

static void vlog_check_while(vlog_node_t v)
{
   vlog_check_expr(vlog_value(v));

   vlog_check_stmts(v);
}

static vlog_node_t vlog_simple_integral_type(vlog_node_t decl)
{
   // The type of a scalar or packed vector variable of a built-in type

   switch (vlog_kind(decl)) {
   case V_VAR_DECL:
   case V_LOCAL_DECL:
   case V_TF_PORT_DECL:
   case V_PORT_DECL:
      break;
   default:
      return NULL;
   }

   if (!vlog_has_type(decl))
      return NULL;

   const int nranges = vlog_ranges(decl);
   for (int i = 0; i < nranges; i++) {
      if (vlog_subkind(vlog_range(decl, i)) == V_DIM_UNPACKED)
         return NULL;
   }

   vlog_node_t type = vlog_type(decl);
   if (vlog_kind(type) != V_DATA_TYPE || vlog_has_type(type))
      return NULL;

   switch (vlog_subkind(type)) {
   case DT_LOGIC:
   case DT_INTEGER:
   case DT_BYTE:
   case DT_SHORTINT:
   case DT_INT:
   case DT_LONGINT:
   case DT_BIT:
   case DT_TIME:
      return type;
   default:
      return NULL;
   }
}

static const char *vlog_type_name(vlog_node_t type)
{
   static const char *names[] = {
      [DT_LOGIC] = "logic", [DT_INTEGER] = "integer", [DT_BYTE] = "byte",
      [DT_SHORTINT] = "shortint", [DT_INT] = "int", [DT_LONGINT] = "longint",
      [DT_BIT] = "bit", [DT_TIME] = "time",
   };

   return names[vlog_subkind(type)];
}

static void vlog_check_ref_arg(vlog_node_t port, vlog_node_t arg)
{
   // The actual for a ref argument is a variable of an equivalent type
   // (13.5.2)

   vlog_node_t decl = NULL;

   switch (vlog_kind(arg)) {
   case V_NUMBER:
      if (vlog_flags(port) & VLOG_F_CONST)
         return;
      // Fall-through
   case V_REAL:
   case V_STRING:
   case V_BINARY:
   case V_UNARY:
   case V_COND_EXPR:
   case V_CONCAT:
      error_at(vlog_loc(arg), "illegal ref argument for %pQ",
               vlog_ident(port));
      return;
   case V_REF:
      if (!vlog_has_ref(arg))
         return;
      decl = vlog_ref(arg);
      if (vlog_kind(decl) == V_NET_DECL) {
         error_at(vlog_loc(arg), "illegal ref argument for %pQ",
                  vlog_ident(port));
         return;
      }
      break;
   default:
      return;
   }

   vlog_node_t atype = vlog_simple_integral_type(decl);
   vlog_node_t ptype = vlog_simple_integral_type(port);
   if (atype == NULL || ptype == NULL)
      return;

   if (vlog_size(atype) * vlog_size(decl) != vlog_size(ptype) * vlog_size(port)
       || vlog_is_signed(atype) != vlog_is_signed(ptype)
       || vlog_is_two_state(atype) != vlog_is_two_state(ptype)) {
      diag_t *d = diag_new_relaxable(vlog_loc(arg));
      diag_printf(d, "type of actual for ref argument %pQ is not equivalent "
                  "to %s", vlog_ident(port), vlog_type_name(ptype));
      diag_emit(d);
   }
}

static void vlog_check_const_target(vlog_node_t arg)
{
   switch (vlog_kind(arg)) {
   case V_REF:
      if (vlog_has_ref(arg)) {
         vlog_node_t d = vlog_ref(arg);
         if ((vlog_kind(d) == V_VAR_DECL || vlog_kind(d) == V_LOCAL_DECL)
             && (vlog_flags(d) & VLOG_F_CONST))
            error_at(vlog_loc(arg), "cannot assign to const variable '%s'",
                     istr(vlog_ident(d)));
      }
      break;
   case V_BIT_SELECT:
   case V_PART_SELECT:
      vlog_check_const_target(vlog_value(arg));
      break;
   case V_MEMBER_REF:
      if (!vlog_is_handle_member(arg))
         vlog_check_const_target(vlog_value(arg));
      break;
   case V_CONCAT:
      {
         const int nparams = vlog_params(arg);
         for (int i = 0; i < nparams; i++)
            vlog_check_const_target(vlog_param(arg, i));
      }
      break;
   default:
      break;
   }
}

static void vlog_check_output_arg(vlog_node_t port, vlog_node_t arg)
{
   if (vlog_kind(port) != V_TF_PORT_DECL)
      return;

   const int dir = vlog_subkind(port);
   if (dir == V_PORT_REF) {
      vlog_check_ref_arg(port, arg);
      return;
   }

   if (dir != V_PORT_OUTPUT && dir != V_PORT_INOUT)
      return;

   switch (vlog_kind(arg)) {
   case V_NUMBER:
   case V_REAL:
   case V_STRING:
   case V_BINARY:
   case V_UNARY:
   case V_COND_EXPR:
      error_at(vlog_loc(arg), "illegal %s argument for %pQ",
               dir == V_PORT_OUTPUT ? "output" : "inout", vlog_ident(port));
      break;
   default:
      vlog_check_const_target(arg);
      break;
   }
}

static void vlog_open_actual(vlog_node_t arg, int *nfixed, int *ndyn,
                             vlog_node_t *elem, bool *known)
{
   // Dimensions and element type of an actual argument

   *nfixed = *ndyn = 0;
   *elem = NULL;
   *known = true;

   vlog_node_t base = arg;
   int nselects = 0;
   while (vlog_kind(base) == V_BIT_SELECT || vlog_kind(base) == V_PART_SELECT) {
      nselects += vlog_kind(base) == V_BIT_SELECT ? vlog_params(base) : 1;
      base = vlog_value(base);
   }

   switch (vlog_kind(base)) {
   case V_REF:
   case V_MEMBER_REF:
   case V_HIER_REF:
      if (!vlog_has_ref(base))
         break;
      else {
         vlog_node_t decl = vlog_ref(base);
         switch (vlog_kind(decl)) {
         case V_VAR_DECL:
         case V_NET_DECL:
         case V_PORT_DECL:
         case V_TF_PORT_DECL:
         case V_LOCAL_DECL:
            break;
         default:
            *known = false;
            return;
         }

         if (!vlog_has_type(decl)) {
            *known = false;
            return;
         }

         if (vlog_is_fixed_unpacked(decl))
            *nfixed = vlog_ranges(decl);

         vlog_node_t type = vlog_type(decl);
         while (vlog_kind(type) == V_DATA_TYPE && vlog_has_type(type)
                && (vlog_subkind(type) == DT_DYNARRAY
                    || vlog_subkind(type) == DT_QUEUE)) {
            (*ndyn)++;
            type = vlog_type(type);
         }

         if (nselects > 0 && *ndyn > 0) {
            *known = false;
            return;
         }

         if (nselects > 0)
            *nfixed = MAX(*nfixed - nselects, 0);

         *elem = vlog_get_type(type);
         return;
      }
   default:
      break;
   }

   *elem = vlog_get_type(base);
}

static void vlog_check_open_arg(vlog_node_t sub, vlog_node_t port,
                                vlog_node_t arg)
{
   // The actual argument for an open array formal of a DPI import must
   // have the same number of unpacked dimensions and equivalent elements
   // (35.5.6.1, 7.7)

   if (!(vlog_flags(sub) & VLOG_F_DPI) || vlog_kind(port) != V_TF_PORT_DECL
       || vlog_kind(arg) == V_EMPTY || vlog_kind(arg) == V_NAMED_ARG)
      return;

   vlog_node_t felem;
   const int fdims = vlog_open_dims(port, &felem);
   const bool open_packed = vlog_has_open_packed(felem);
   if (fdims == 0 && !open_packed)
      return;

   int nfixed, ndyn;
   vlog_node_t aelem;
   bool known;
   vlog_open_actual(arg, &nfixed, &ndyn, &aelem, &known);
   if (!known)
      return;

   if (fdims > 0 && ndyn > 0 && vlog_subkind(port) == V_PORT_OUTPUT) {
      diag_t *d = diag_new_relaxable(vlog_loc(arg));
      diag_printf(d, "dynamic array cannot be passed to unsized output "
                  "argument '%s' of DPI import '%s'",
                  istr(vlog_ident(port)), istr(vlog_ident(sub)));
      diag_emit(d);
      if (!relaxed_rules())
         return;
   }

   bool ok = nfixed + ndyn == fdims;
   if (ok && ndyn > 0 && nfixed > 0)
      ok = false;
   if (ok && aelem != NULL && !open_packed && fdims > 0) {
      vlog_node_t fe = vlog_get_type(felem);
      if (fe != NULL)
         ok = vlog_equivalent_elements(fe, aelem);
   }

   if (!ok)
      error_at(vlog_loc(arg), "actual argument is not compatible with open "
               "array argument '%s'", istr(vlog_ident(port)));
}

static void vlog_check_call_args(vlog_node_t v, vlog_node_t sub)
{
   const int nparams = vlog_params(v);
   const int nports = vlog_ports(sub);

   LOCAL_BIT_MASK optmask;
   mask_init(&optmask, nports);

   for (int i = 0; i < nports; i++) {
      if (vlog_has_value(vlog_port(sub, i)))
         mask_set(&optmask, i);
   }

   const int minargs = nports - mask_popcount(&optmask);

   int npositional = nparams;
   for (int i = 0; i < nparams; i++) {
      if (vlog_kind(vlog_param(v, i)) == V_NAMED_ARG) {
         npositional = i;
         break;
      }
   }

   if (npositional < nparams) {
      // Each argument by name must match a port that has no other value
      for (int i = npositional; i < nparams; i++) {
         vlog_node_t p = vlog_param(v, i);
         if (vlog_kind(p) != V_NAMED_ARG) {
            error_at(vlog_loc(p), "an argument by position cannot follow one "
                     "by name");
            return;
         }

         if (vlog_has_value(p))
            vlog_check_expr(vlog_value(p));

         bool found = false;
         for (int j = npositional; j < nports; j++) {
            vlog_node_t port = vlog_port(sub, j);
            if (vlog_ident(port) != vlog_ident(p))
               continue;

            found = true;
            if (vlog_has_value(p)) {
               vlog_check_output_arg(port, vlog_value(p));
               vlog_check_open_arg(sub, port, vlog_value(p));
            }
         }

         if (!found) {
            error_at(vlog_loc(p), "%pQ has no argument named %pQ that does "
                     "not already have a value", vlog_ident(sub),
                     vlog_ident(p));
            return;
         }
      }

      for (int i = 0; i < npositional && i < nports; i++) {
         vlog_check_expr(vlog_param(v, i));
         vlog_check_output_arg(vlog_port(sub, i), vlog_param(v, i));
         vlog_check_open_arg(sub, vlog_port(sub, i), vlog_param(v, i));
      }

      for (int i = npositional; i < nports; i++) {
         vlog_node_t port = vlog_port(sub, i);
         bool found = vlog_has_value(port);
         for (int j = npositional; j < nparams; j++) {
            vlog_node_t p = vlog_param(v, j);
            found |= vlog_ident(p) == vlog_ident(port) && vlog_has_value(p);
         }

         if (!found) {
            diag_t *d = diag_new(DIAG_ERROR, vlog_loc(v));
            diag_printf(d, "missing %pQ argument %pQ without a default value",
                        vlog_ident(sub), vlog_ident(port));
            diag_hint(d, vlog_loc(port), "%pQ declared here",
                      vlog_ident(port));
            diag_emit(d);
         }
      }

      return;
   }

   if (nparams > nports || nparams < minargs) {
      diag_t *d = diag_new(DIAG_ERROR, vlog_loc(v));
      diag_printf(d, "expected ");
      if (minargs < nports && nparams < nports)
         diag_printf(d, "at least %d ", minargs);
      else if (minargs < nports && nparams > nports)
         diag_printf(d, "at most %d ", nports);
      else
         diag_printf(d, "%d ", nports);
      diag_printf(d, "argument%s for %pQ but have %d",
                  minargs != 1 ? "s" : "", vlog_ident(sub), nparams);
      diag_hint(d, vlog_loc(sub), "%pQ declared here", vlog_ident(sub));
      diag_emit(d);
      return;
   }

   for (int i = 0; i < nports; i++) {
      vlog_node_t port = vlog_port(sub, i);

      if (i < nparams) {
         vlog_node_t p = vlog_param(v, i);
         vlog_check_expr(p);
         vlog_check_output_arg(port, p);
         vlog_check_open_arg(sub, port, p);

         if (vlog_kind(p) == V_EMPTY && !vlog_has_value(port)) {
            diag_t *d = diag_new(DIAG_ERROR, vlog_loc(v));
            diag_printf(d, "%pQ argument %pQ cannot be empty as it does "
                        "not have a default value", vlog_ident(sub),
                        vlog_ident(port));
            diag_hint(d, vlog_loc(port), "%pQ declared here",
                      vlog_ident(port));
            diag_emit(d);
         }
      }
      else if (!mask_test(&optmask, i)) {
         diag_t *d = diag_new(DIAG_ERROR, vlog_loc(v));
         diag_printf(d, "missing %pQ argument %pQ without a default value",
                     vlog_ident(sub), vlog_ident(port));
         diag_hint(d, vlog_loc(port), "%pQ declared here", vlog_ident(port));
         diag_emit(d);
      }
   }
}

static void vlog_check_discarded(vlog_node_t v, vlog_node_t func)
{
   // A function with a return value called as a statement is legal but
   // shall give a warning, which a cast to void avoids (13.4.1)

   if (vlog_kind(func) != V_FUNC_DECL || !vlog_has_type(func))
      return;

   diag_t *d = diag_new(DIAG_WARN, vlog_loc(v));
   diag_printf(d, "return value of function '%s' is discarded",
               istr(vlog_ident(func)));
   diag_hint(d, vlog_loc(v), "use void'(...) to discard the value without "
             "a warning");
   diag_emit(d);
}

static void vlog_check_user_tcall(vlog_node_t v)
{
   vlog_node_t func = vlog_ref(v);
   if (vlog_kind(func) != V_TASK_DECL && vlog_kind(func) != V_FUNC_DECL) {
      diag_t *d = diag_new(DIAG_ERROR, vlog_loc(v));
      diag_printf(d, "'%s' is not a task", istr(vlog_ident(func)));
      diag_hint(d, vlog_loc(func), "'%s' declared here",
                istr(vlog_ident(func)));
      diag_emit(d);
      return;
   }

   if (in_function && vlog_kind(func) == V_TASK_DECL)
      error_at(vlog_loc(v), "task '%s' cannot be called from a function as "
               "functions must not block", istr(vlog_ident(func)));

   vlog_check_call_args(v, func);
   vlog_check_discarded(v, func);
}

static void vlog_check_arg_order(vlog_node_t v)
{
   bool seen_named = false;
   const int nparams = vlog_params(v);
   for (int i = 0; i < nparams; i++) {
      vlog_node_t p = vlog_param(v, i);
      if (vlog_kind(p) == V_NAMED_ARG)
         seen_named = true;
      else if (seen_named) {
         error_at(vlog_loc(p), "an argument by position cannot follow one "
                  "by name");
         return;
      }
   }
}

static void vlog_check_super_call(vlog_node_t v)
{
   vlog_check_arg_order(v);
}

static void vlog_check_deassign(vlog_node_t v)
{
   error_at(vlog_loc(v), "procedural deassign statements are not supported "
            "as they are being considered for removal from the System Verilog "
            "standard");
}

static void vlog_check_return(vlog_node_t v)
{
   if (!vlog_has_ref(v))
      return;   // Was earlier error

   vlog_node_t subr = vlog_ref(v);
   const vlog_kind_t kind = vlog_kind(subr);

   if (kind == V_FUNC_DECL && vlog_has_type(subr) && !vlog_has_value(v))
      error_at(vlog_loc(v), "return statement in a non-void function must "
               "have an expression");
   else if (kind == V_TASK_DECL && vlog_has_value(v))
      error_at(vlog_loc(v), "return statement in a task cannot have an "
               "expression");
   else if (kind == V_FUNC_DECL && vlog_has_value(v)
            && !vlog_has_type(subr))
      error_at(vlog_loc(v), "return statement in a void function cannot have "
               "an expression");
}

static void vlog_check_sys_tcall(vlog_node_t v)
{
   if (vlog_ident(v) == ident_new("$cast")) {
      (void)vlog_check_cast_call(v);
      return;
   }

   vlog_check_params(v);
}

static void vlog_check_timing(vlog_node_t v)
{
   if (in_final)
      error_at(vlog_loc(v), "%s inside a final block is illegal as final "
               "procedures must not block",
               vlog_kind(vlog_value(v)) == V_DELAY_CONTROL
               ? "a delay control" : "an event control");

   vlog_check(vlog_value(v));
   vlog_check_stmts(v);
}

static void vlog_check_sva_operand(vlog_node_t v)
{
   const bool saved = in_sva_expr;
   in_sva_expr = true;

   switch (vlog_kind(v)) {
   case V_SVA_OP:
   case V_EVENT_CONTROL:
      vlog_check(v);
      break;
   default:
      vlog_check_expr(v);
      break;
   }

   in_sva_expr = saved;
}

static bool sva_is_tracked_local(vlog_node_t d)
{
   switch (vlog_kind(d)) {
   case V_VAR_DECL:
   case V_TF_PORT_DECL:
      return !!(vlog_flags(d) & VLOG_F_SVA_LOCAL);
   default:
      return false;
   }
}

static void sva_abort_local_cb(vlog_node_t v, void *ctx)
{
   if (vlog_kind(v) == V_REF && vlog_has_ref(v)
       && sva_is_tracked_local(vlog_ref(v)))
      error_at(vlog_loc(v), "local variable '%s' cannot be used in %s",
               istr(vlog_ident(v)), (const char *)ctx);
}

#define SVA_MAX_LOCALS 64

typedef struct {
   uint64_t asg;     // Assigned on every path
   uint64_t both_and;  // Assigned in both operands of and
   uint64_t both_int;  // Assigned in both operands of intersect
} sva_flow_t;

static A(vlog_node_t) sva_locals = AINIT;

static int sva_local_index(vlog_node_t d)
{
   for (unsigned i = 0; i < sva_locals.count; i++) {
      if (sva_locals.items[i] == d)
         return i;
   }

   if (sva_locals.count == SVA_MAX_LOCALS)
      return -1;

   APUSH(sva_locals, d);
   return sva_locals.count - 1;
}

static A(vlog_node_t) sva_skip = AINIT;

static void sva_skip_args_cb(vlog_node_t v, void *ctx)
{
   vlog_node_t func = vlog_ref(v);
   if (vlog_kind(func) != V_FUNC_DECL)
      return;

   const int nparams = MIN(vlog_params(v), vlog_ports(func));
   for (int i = 0; i < nparams; i++) {
      switch (vlog_subkind(vlog_port(func, i))) {
      case V_PORT_OUTPUT:
      case V_PORT_INOUT:
      case V_PORT_REF:
         APUSH(sva_skip, vlog_param(v, i));
         break;
      default:
         break;
      }
   }
}

static void sva_flow_ref_cb(vlog_node_t v, void *ctx)
{
   sva_flow_t *st = ctx;

   if (vlog_kind(v) != V_REF || !vlog_has_ref(v))
      return;

   vlog_node_t d = vlog_ref(v);
   if (!sva_is_tracked_local(d))
      return;

   for (unsigned i = 0; i < sva_skip.count; i++) {
      if (sva_skip.items[i] == v)
         return;
   }

   const int idx = sva_local_index(d);
   if (idx < 0)
      return;

   const uint64_t bit = UINT64_C(1) << idx;
   if (st->both_and & bit)
      error_at(vlog_loc(v), "local variable '%s' is assigned in both "
               "operands of and and cannot be referenced after it",
               istr(vlog_ident(v)));
   else if (st->both_int & bit)
      error_at(vlog_loc(v), "local variable '%s' is assigned in both "
               "operands of intersect and cannot be referenced after it",
               istr(vlog_ident(v)));
   else if (!(st->asg & bit))
      error_at(vlog_loc(v), "local variable '%s' may be unassigned here",
               istr(vlog_ident(v)));
}

static void sva_flow_mark_decls(vlog_node_t decl, sva_flow_t *st)
{
   const int nports = vlog_ports(decl);
   for (int i = 0; i < nports; i++) {
      vlog_node_t p = vlog_port(decl, i);
      if (sva_is_tracked_local(p)) {
         const int idx = sva_local_index(p);
         if (idx >= 0)
            st->asg |= UINT64_C(1) << idx;
      }
   }

   const int ndecls = vlog_decls(decl);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(decl, i);
      if (sva_is_tracked_local(d) && vlog_has_value(d)) {
         const int idx = sva_local_index(d);
         if (idx >= 0)
            st->asg |= UINT64_C(1) << idx;
      }
   }
}

static void sva_visit_expr(vlog_node_t v, sva_flow_t *st)
{
   sva_skip.count = 0;
   vlog_visit_only(v, sva_skip_args_cb, NULL, V_USER_FCALL);
   vlog_visit(v, sva_flow_ref_cb, st);
}

static sva_flow_t sva_flow(vlog_node_t v, sva_flow_t st);

static sva_flow_t sva_flow_join(sva_flow_t in, sva_flow_t a, sva_flow_t b,
                                bool both)
{
   // Combine two operands which each started from the state "in"

   sva_flow_t out = in;
   out.both_and |= a.both_and | b.both_and;
   out.both_int |= a.both_int | b.both_int;

   if (both) {
      out.asg = a.asg & b.asg;
      return out;
   }

   out.asg = a.asg | b.asg;
   return out;
}

static sva_flow_t sva_flow(vlog_node_t v, sva_flow_t st)
{
   switch (vlog_kind(v)) {
   case V_SVA_OP:
      break;
   case V_EVENT_CONTROL:
      return st;
   default:
      sva_visit_expr(v, &st);
      return st;
   }

   const int nparams = vlog_params(v);

   switch (vlog_subkind(v)) {
   case V_SVA_OR:
   case V_SVA_IF:
      {
         if (vlog_has_value(v))
            sva_visit_expr(vlog_value(v), &st);

         if (nparams < 2)
            break;

         const sva_flow_t a = sva_flow(vlog_param(v, 0), st);
         const sva_flow_t b = sva_flow(vlog_param(v, 1), st);
         return sva_flow_join(st, a, b, true);
      }
   case V_SVA_AND:
   case V_SVA_INTERSECT:
   case V_SVA_WITHIN:
      {
         if (nparams < 2)
            break;

         const sva_flow_t a = sva_flow(vlog_param(v, 0), st);
         const sva_flow_t b = sva_flow(vlog_param(v, 1), st);
         sva_flow_t out = sva_flow_join(st, a, b, false);

         const uint64_t both =
            (a.asg & ~st.asg) & (b.asg & ~st.asg);
         out.asg &= ~both;
         if (vlog_subkind(v) == V_SVA_AND)
            out.both_and |= both;
         else
            out.both_int |= both;
         return out;
      }
   case V_SVA_REPEAT:
   case V_SVA_GOTO:
   case V_SVA_NONCONS:
      {
         const sva_flow_t body = sva_flow(vlog_param(v, 0), st);
         if (vlog_has_left(v) && vlog_kind(vlog_left(v)) == V_NUMBER
             && number_is_defined(vlog_number(vlog_left(v)))
             && number_integer(vlog_number(vlog_left(v))) == 0)
            return sva_flow_join(st, st, body, true);
         return body;
      }
   case V_SVA_MATCH:
      {
         st = sva_flow(vlog_param(v, 0), st);
         for (int i = 1; i < nparams; i++) {
            vlog_node_t item = vlog_param(v, i);
            if (vlog_kind(item) == V_BASSIGN) {
               vlog_node_t target = vlog_target(item);
               sva_visit_expr(vlog_value(item), &st);
               if (vlog_kind(target) == V_REF && vlog_has_ref(target)
                   && sva_is_tracked_local(vlog_ref(target))) {
                  const int idx = sva_local_index(vlog_ref(target));
                  if (idx >= 0)
                     st.asg |= UINT64_C(1) << idx;
               }
               else
                  sva_visit_expr(target, &st);
            }
            else
               sva_visit_expr(item, &st);
         }
         return st;
      }
   default:
      break;
   }

   switch (vlog_subkind(v)) {
   case V_SVA_ACCEPT_ON:
   case V_SVA_REJECT_ON:
   case V_SVA_SYNC_ACCEPT_ON:
   case V_SVA_SYNC_REJECT_ON:
   case V_SVA_DISABLE:
   case V_SVA_CLOCKED:
      break;
   default:
      if (vlog_has_value(v) && vlog_kind(vlog_value(v)) != V_EVENT_CONTROL)
         sva_visit_expr(vlog_value(v), &st);
      break;
   }

   for (int i = 0; i < nparams; i++)
      st = sva_flow(vlog_param(v, i), st);

   return st;
}

static void vlog_check_sva_flow(vlog_node_t decl)
{
   sva_locals.count = 0;

   sva_flow_t st = {};
   sva_flow_mark_decls(decl, &st);
   (void)sva_flow(vlog_value(decl), st);

   sva_locals.count = 0;
}

static void vlog_check_sva_op(vlog_node_t v)
{
   switch (vlog_subkind(v)) {
   case V_SVA_DISABLE:
      if (vlog_has_value(v))
         vlog_visit(vlog_value(v), sva_abort_local_cb,
                    (void *)"a disable iff condition");
      break;
   default:
      break;
   }

   if (vlog_has_value(v))
      vlog_check_sva_operand(vlog_value(v));

   if (vlog_has_left(v))
      vlog_check_expr(vlog_left(v));

   if (vlog_has_right(v) && vlog_right(v) != vlog_left(v))
      vlog_check_expr(vlog_right(v));

   const int nparams = vlog_params(v);
   for (int i = 0; i < nparams; i++) {
      if (i > 0 && vlog_subkind(v) == V_SVA_MATCH) {
         const bool saved = in_sva_expr;
         in_sva_expr = false;
         vlog_check(vlog_param(v, i));
         in_sva_expr = saved;
      }
      else
         vlog_check_sva_operand(vlog_param(v, i));
   }
}

static void vlog_check_sva_decl(vlog_node_t v)
{
   const int nports = vlog_ports(v);
   for (int i = 0; i < nports; i++)
      vlog_check(vlog_port(v, i));

   if (vlog_has_value(v)) {
      vlog_check_sva_operand(vlog_value(v));
      vlog_check_sva_flow(v);
   }
}

static void vlog_check_prop_spec(vlog_node_t v)
{
   if (vlog_has_target(v))
      vlog_check(vlog_target(v));

   if (vlog_has_left(v))
      vlog_check_expr(vlog_left(v));

   vlog_check_sva_operand(vlog_value(v));
}

static void vlog_check_assertion(vlog_node_t v)
{
   if (vlog_kind(vlog_value(v)) == V_PROP_SPEC)
      vlog_check(vlog_value(v));
   else
      vlog_check_expr(vlog_value(v));

   const int nconds = vlog_conds(v);
   for (int i = 0; i < nconds; i++)
      vlog_check_stmts(vlog_cond(v, i));
}

static void vlog_check_if(vlog_node_t v)
{
   const int nconds = vlog_conds(v);
   for (int i = 0; i < nconds; i++)
      vlog_check(vlog_cond(v, i));
}

static void vlog_check_if_generate(vlog_node_t v)
{
   const int nconds = vlog_conds(v);
   for (int i = 0; i < nconds; i++) {
      vlog_node_t c = vlog_cond(v, i);
      assert(vlog_kind(c) == V_COND);

      if (vlog_has_value(c)) {
         vlog_node_t value = vlog_value(c);
         vlog_check_expr(value);
      }

      vlog_check_stmts(c);
   }
}

static void vlog_check_cond(vlog_node_t v)
{
   if (vlog_has_value(v))
      vlog_check_expr(vlog_value(v));

   vlog_check_stmts(v);
}

static void vlog_check_event_control(vlog_node_t v)
{
   const int nparams = vlog_params(v);
   for (int i = 0; i < nparams; i++)
      vlog_check_expr(vlog_param(v, i));
}

static void vlog_check_delay_control(vlog_node_t v)
{
   vlog_check_expr(vlog_value(v));
}

static void vlog_record_driver(hash_t *map, vlog_node_t target, bool cont)
{
   switch (vlog_kind(target)) {
   case V_CONCAT:
      {
         const int nparams = vlog_params(target);
         for (int i = 0; i < nparams; i++)
            vlog_record_driver(map, vlog_param(target, i), cont);
      }
      break;
   case V_REF:
      {
         if (!vlog_has_ref(target))
            break;

         vlog_node_t decl = vlog_ref(target);
         if (vlog_kind(decl) != V_VAR_DECL
             || (vlog_flags(decl) & VLOG_F_USERNET))
            break;

         const uintptr_t mask = cont ? 1 : 2;
         const uintptr_t old = (uintptr_t)hash_get(map, decl);
         if (old == (mask ^ 3))
            error_at(vlog_loc(target), "variable %pQ cannot be written by "
                     "both a continuous assignment and a procedural "
                     "assignment", vlog_ident(decl));
         else if (old == 1 && cont)
            error_at(vlog_loc(target), "variable %pQ cannot be driven by "
                     "more than one continuous assignment", vlog_ident(decl));

         hash_put(map, decl, (void *)(old | mask));
      }
      break;
   default:
      break;
   }
}

static void vlog_driver_cb(vlog_node_t v, void *ctx)
{
   switch (vlog_kind(v)) {
   case V_ASSIGN:
      vlog_record_driver(ctx, vlog_target(v), true);
      break;
   case V_BASSIGN:
   case V_NBASSIGN:
      vlog_record_driver(ctx, vlog_target(v), false);
      break;
   default:
      break;
   }
}

typedef struct {
   hash_t      *map;
   vlog_node_t  proc;
} ff_driver_t;

static void vlog_ff_driver_cb(vlog_node_t v, void *ctx)
{
   ff_driver_t *d = ctx;

   switch (vlog_kind(v)) {
   case V_ASSIGN:
   case V_BASSIGN:
   case V_NBASSIGN:
      break;
   default:
      return;
   }

   vlog_node_t target = vlog_target(v);
   if (vlog_kind(target) != V_REF || !vlog_has_ref(target)
       || vlog_kind(vlog_ref(target)) != V_VAR_DECL)
      return;

   vlog_node_t decl = vlog_ref(target);
   vlog_node_t other = hash_get(d->map, decl);
   if (other == NULL)
      hash_put(d->map, decl, d->proc);
   else if (other != d->proc) {
      const bool ff = (vlog_kind(other) == V_ALWAYS
                       && vlog_subkind(other) == V_ALWAYS_FF)
         || (vlog_kind(d->proc) == V_ALWAYS
             && vlog_subkind(d->proc) == V_ALWAYS_FF);
      if (ff) {
         diag_t *dg = diag_new(DIAG_ERROR, vlog_loc(target));
         diag_printf(dg, "variable %pQ written in an always_ff process cannot "
                     "be written by any other process", vlog_ident(decl));
         diag_hint(dg, vlog_loc(other), "also written here");
         diag_emit(dg);
      }
   }
}

static void vlog_genvar_loop_cb(vlog_node_t v, void *ctx)
{
   if (vlog_kind(v) == V_REF && vlog_has_ref(v)
       && vlog_kind(vlog_ref(v)) == V_GENVAR_DECL)
      hset_insert(ctx, v);
}

static void vlog_genvar_for_cb(vlog_node_t v, void *ctx)
{
   if (vlog_kind(v) == V_FOR_GENERATE)
      vlog_visit(v, vlog_genvar_loop_cb, ctx);
}

static void vlog_genvar_use_cb(vlog_node_t v, void *ctx)
{
   if (vlog_kind(v) == V_REF && vlog_has_ref(v)
       && vlog_kind(vlog_ref(v)) == V_GENVAR_DECL
       && !hset_contains(ctx, v))
      error_at(vlog_loc(v), "genvar '%s' used outside generate for loop",
               istr(vlog_ident(v)));
}

static void vlog_check_genvar_uses(vlog_node_t v)
{
   // A genvar is only used in the generate loop which declares it
   // (IEEE 1800-2017 27.4)

   hset_t *legit = hset_new(16);
   vlog_visit(v, vlog_genvar_for_cb, legit);
   vlog_visit(v, vlog_genvar_use_cb, legit);
   hset_free(legit);
}

static void vlog_check_module(vlog_node_t v)
{
   const int nports = vlog_ports(v);
   for (int i = 0; i < nports; i++) {
      vlog_node_t ref = vlog_port(v, i);
      if (!vlog_has_ref(ref))
         error_at(vlog_loc(ref), "missing port declaration for '%s'",
                  istr(vlog_ident(ref)));
   }

   vlog_check_decls(v);
   vlog_check_stmts(v);
   vlog_check_genvar_uses(v);

   // Do not look inside generate blocks as only one branch is elaborated
   hash_t *drivers = hash_new(64);
   const int nstmts = vlog_stmts(v);
   for (int i = 0; i < nstmts; i++) {
      vlog_node_t s = vlog_stmt(v, i);
      switch (vlog_kind(s)) {
      case V_ASSIGN:
      case V_ALWAYS:
      case V_INITIAL:
      case V_FINAL:
         vlog_visit(s, vlog_driver_cb, drivers);
         break;
      default:
         break;
      }
   }
   hash_free(drivers);

   hash_t *writers = hash_new(64);
   ff_driver_t ff = { .map = writers };
   for (int i = 0; i < nstmts; i++) {
      vlog_node_t s = vlog_stmt(v, i);
      switch (vlog_kind(s)) {
      case V_ASSIGN:
      case V_ALWAYS:
      case V_INITIAL:
         ff.proc = s;
         vlog_visit(s, vlog_ff_driver_cb, &ff);
         break;
      default:
         break;
      }
   }
   hash_free(writers);
}

static void vlog_check_gate_inst(vlog_node_t v)
{
   const int nparams = vlog_params(v);
   for (int i = 0; i < nparams; i++) {
      vlog_node_t p = vlog_param(v, i);
      if (vlog_kind(p) != V_DIMENSION)
         vlog_check_expr(p);
   }
}

static void vlog_check_inst_list(vlog_node_t v)
{
   const int nparams = vlog_params(v);
   for (int i = 0; i < nparams; i++)
      vlog_check(vlog_param(v, i));

   vlog_check_stmts(v);
}

static void vlog_check_mod_inst(vlog_node_t v)
{
   const int nparams = vlog_params(v);
   for (int i = 0; i < nparams; i++)
      vlog_check(vlog_param(v, i));
}

static vlog_node_t interconnect_ok = NULL;

static void vlog_check_port_conn(vlog_node_t v)
{
   if (!vlog_has_value(v))
      return;

   // An interconnect can be connected whole or by a select
   vlog_node_t root = vlog_value(v);
   while (vlog_kind(root) == V_BIT_SELECT || vlog_kind(root) == V_PART_SELECT)
      root = vlog_value(root);

   interconnect_ok = root;
   vlog_check_expr(vlog_value(v));
   interconnect_ok = NULL;
}

static void vlog_check_param_assign(vlog_node_t v)
{
   if (vlog_has_value(v) && !is_data_type(vlog_value(v)))
      vlog_check_expr(vlog_value(v));
}

static void vlog_check_enum_name(vlog_node_t v)
{
   if (vlog_has_value(v))
      vlog_check_expr(vlog_value(v));
}

static void vlog_check_wait(vlog_node_t v)
{
   if (in_final)
      error_at(vlog_loc(v), "a wait statement inside a final block is illegal "
               "as final procedures must not block");

   // TODO
}

static type_mask_t vlog_check_mod_ref(vlog_node_t v)
{
   if (vlog_has_ref(v)) {
      vlog_node_t inst = vlog_ref(v);
      if (vlog_kind(inst) != V_MOD_INST && vlog_kind(inst) != V_IFACE_PORT) {
         error_at(vlog_loc(v), "prefix of hierarchical identifier is not an "
                  "instance");
         return TM_ERROR;
      }
   }

   return TM_INTEGRAL;
}

static type_mask_t vlog_check_hier_ref(vlog_node_t v)
{
   const type_mask_t mask = vlog_check_expr(vlog_value(v));

   // An instance of an interface used as a value
   if (vlog_has_ident(v) && vlog_ident(v) == ident_new("$self"))
      return TM(DT_VIRTUAL) | TM_CLASS;

   if (!vlog_has_ref(v))
      return ~TM_CONST;   // Not known until elaboration

   return mask;
}

static type_mask_t vlog_check_member_ref(vlog_node_t v)
{
   vlog_check_expr(vlog_value(v));

   if (vlog_virtual_pending(v)) {
      // The interface may be declared later
      vlog_node_t decl = vlog_virtual_decl_or_null(v);
      return decl != NULL ? get_type_mask(decl) : ~TM_CONST;
   }
   else if (!vlog_has_ref(v)) {
      if (!vlog_virtual_chain(vlog_value(v)) && !vlog_typeparam_pending(v))
         return TM_ERROR;   // Reported by the lookup

      // The structure may be known only when the interface is declared
      return ~TM_CONST;
   }

   return get_type_mask(vlog_member_decl(v));
}

static bool vlog_is_interconnect(vlog_node_t d)
{
   switch (vlog_kind(d)) {
   case V_NET_DECL:
   case V_PORT_DECL:
      {
         if (!vlog_has_type(d))
            return false;
         vlog_node_t t = vlog_type(d);
         return vlog_kind(t) == V_DATA_TYPE && (vlog_flags(t) & VLOG_F_USERNET);
      }
   default:
      return false;
   }
}

static type_mask_t vlog_check_ref(vlog_node_t v)
{
   if (v != interconnect_ok && vlog_has_ref(v)
       && vlog_is_interconnect(vlog_ref(v)))
      error_at(vlog_loc(v), "interconnect '%s' can only be used in a port "
               "connection", istr(vlog_ident(v)));

   if (!vlog_has_ref(v))
      return TM_ERROR;   // The lookup failed and was reported

   if (vlog_kind(vlog_ref(v)) == V_LOCAL_DECL
       && (vlog_flags(vlog_ref(v)) & VLOG_F_FORWARD))
      return TM_INTEGRAL | TM(DT_STRING);

   return get_type_mask(vlog_ref(v));
}

static bool vlog_is_unpacked(vlog_node_t v)
{
   // An array with unpacked dimensions of a type that cannot be indexed

   vlog_node_t value = vlog_value(v);
   if (vlog_kind(value) != V_REF && vlog_kind(value) != V_MEMBER_REF)
      return false;
   else if (!vlog_has_ref(value))
      return false;

   vlog_node_t decl = vlog_kind(value) == V_MEMBER_REF
      ? vlog_member_decl(value) : vlog_ref(value);
   switch (vlog_kind(decl)) {
   case V_PORT_DECL:
   case V_VAR_DECL:
   case V_LOCAL_DECL:
   case V_TF_PORT_DECL:
   case V_NET_DECL:
   case V_PARAM_DECL:
   case V_LOCALPARAM:
      return vlog_ranges(decl) > 0;
   default:
      return false;
   }
}

static type_mask_t vlog_check_index(vlog_node_t v)
{
   type_mask_t tmask = vlog_check_expr(vlog_value(v));
   if (tmask == TM_ERROR)
      return TM_ERROR;
   else if (!(tmask & (TM_INTEGRAL | TM_ARRAY)) && !vlog_is_unpacked(v)) {
      error_at(vlog_loc(v), "value of type '%s' cannot be indexed",
               type_mask_str(tmask));
      return TM_ERROR;
   }

   return tmask;
}

static bool vlog_is_string_literal(vlog_node_t v)
{
   switch (vlog_kind(v)) {
   case V_STRING:
      return true;
   case V_COND_EXPR:
      return vlog_is_string_literal(vlog_left(v))
         && vlog_is_string_literal(vlog_right(v));
   default:
      return false;
   }
}

static void vlog_check_assoc_key(vlog_node_t aty, vlog_node_t expr,
                                 type_mask_t mask, const char *what)
{
   if (mask == TM_ERROR)
      return;

   vlog_node_t key = vlog_assoc_key(aty);
   if (key != NULL && vlog_kind(key) == V_DATA_TYPE
       && vlog_subkind(key) == DT_STRING) {
      if (!(mask & TM(DT_STRING)) && !vlog_is_string_literal(expr))
         error_at(vlog_loc(expr), "%s of an associative array with a string "
                  "index must have type string", what);
   }
   else if (key != NULL && vlog_kind(key) == V_CLASS_DECL) {
      vlog_node_t et = vlog_get_type(expr);
      if (vlog_kind(expr) != V_NULL
          && (et == NULL || vlog_kind(et) != V_CLASS_DECL))
         error_at(vlog_loc(expr), "%s of an associative array with a class "
                  "index must be a class handle", what);
   }
   else if (key != NULL && vlog_kind(key) == V_STRUCT_DECL
            && !(vlog_flags(key) & VLOG_F_PACKED)) {
      if (vlog_get_type(expr) != key)
         error_at(vlog_loc(expr), "%s of an associative array with a "
                  "structure index must have the same structure type", what);
   }
   else if (!(mask & TM_INTEGRAL))
      error_at(vlog_loc(expr), "%s of an associative array with an integral "
               "index must have an integral type", what);
}

static void vlog_check_assoc_indices(vlog_node_t v)
{
   vlog_node_t value = vlog_value(v);
   vlog_node_t t = vlog_get_type(value);

   int skip = 0;
   if ((vlog_kind(value) == V_REF || vlog_kind(value) == V_MEMBER_REF)
       && vlog_has_ref(value)) {
      vlog_node_t decl = vlog_ref(value);
      switch (vlog_kind(decl)) {
      case V_VAR_DECL:
      case V_LOCAL_DECL:
      case V_NET_DECL:
      case V_PORT_DECL:
      case V_TF_PORT_DECL:
         skip = vlog_ranges(decl);
         break;
      default:
         break;
      }
   }

   const int nparams = vlog_params(v);
   for (int i = skip; t != NULL && i < nparams; i++) {
      if (vlog_kind(t) != V_DATA_TYPE)
         break;

      switch (vlog_subkind(t)) {
      case DT_ASSOC:
         vlog_check_assoc_key(t, vlog_param(v, i),
                              vlog_check_expr(vlog_param(v, i)), "index");
         t = vlog_get_type(vlog_type(t));
         continue;
      case DT_DYNARRAY:
      case DT_QUEUE:
         t = vlog_get_type(vlog_type(t));
         continue;
      default:
         break;
      }
      break;
   }
}

static void vlog_dollar_cb(vlog_node_t v, void *ctx)
{
   *(bool *)ctx = true;
}

static type_mask_t vlog_check_bit_select(vlog_node_t v)
{
   vlog_node_t vtype = vlog_get_type(vlog_value(v));
   const bool is_darray = vtype != NULL && vlog_kind(vtype) == V_DATA_TYPE
      && vlog_subkind(vtype) == DT_DYNARRAY;

   const int nparams = vlog_params(v);
   for (int i = 0; i < nparams; i++) {
      vlog_node_t p = vlog_param(v, i);
      const type_mask_t pmask = vlog_check_expr(p);
      vlog_node_t base = p;
      while (vlog_kind(base) == V_PART_SELECT || vlog_kind(base) == V_BIT_SELECT)
         base = vlog_value(base);

      if (pmask != TM_ERROR && (pmask & TM_REAL)
          && !(vlog_kind(base) == V_HIER_REF && !vlog_has_ref(base)))
         error_at(vlog_loc(p), "index expression cannot have real type");

      if (is_darray && i == 0) {
         bool has_dollar = false;
         vlog_visit_only(p, vlog_dollar_cb, &has_dollar, V_DOLLAR);
         if (has_dollar)
            error_at(vlog_loc(p), "'$' index is only allowed for queues");
      }
   }

   vlog_check_assoc_indices(v);

   const type_mask_t tmask = vlog_check_index(v);

   if (tmask != TM_ERROR && vtype != NULL && vlog_kind(vtype) == V_DATA_TYPE
       && vlog_subkind(vtype) == DT_STRING)
      return TM(DT_BYTE);   // One character of the string

   vlog_node_t etype = vlog_select_type(v);
   if (tmask != TM_ERROR && etype != NULL)
      return get_type_mask(etype);

   if (tmask != TM_ERROR && vtype != NULL && vlog_kind(vtype) == V_DATA_TYPE
       && vlog_has_type(vtype) && vlog_params(v) > 1) {
      // A bit of an element of a dynamic array, queue or associative array
      switch (vlog_subkind(vtype)) {
      case DT_DYNARRAY:
      case DT_QUEUE:
      case DT_ASSOC:
         {
            vlog_node_t elem = vlog_get_type(vlog_type(vtype));
            if (elem != NULL && vlog_kind(elem) == V_DATA_TYPE) {
               const type_mask_t emask = get_type_mask(elem);
               if (emask & TM_INTEGRAL)
                  return emask;
            }
         }
         break;
      default:
         break;
      }
   }

   return tmask;
}

static vlog_node_t vlog_select_dim(vlog_node_t v, int skip)
{
   switch (vlog_kind(v)) {
   case V_BIT_SELECT:
      return vlog_select_dim(vlog_value(v), skip + vlog_params(v));
   case V_REF:
   case V_MEMBER_REF:
      {
         vlog_node_t decl = vlog_has_ref(v) ? vlog_ref(v) : NULL;
         if (decl == NULL)
            return NULL;

         switch (vlog_kind(decl)) {
         case V_VAR_DECL:
         case V_NET_DECL:
         case V_PORT_DECL:
         case V_LOCAL_DECL:
         case V_TF_PORT_DECL:
         case V_PARAM_DECL:
         case V_LOCALPARAM:
            break;
         default:
            return NULL;
         }

         const int nu = vlog_ranges(decl);
         if (skip < nu)
            return vlog_range(decl, skip);
         skip -= nu;

         if (!vlog_has_type(decl))
            return NULL;

         vlog_node_t dt = vlog_get_type(vlog_type(decl));
         while (dt != NULL && vlog_kind(dt) == V_DATA_TYPE) {
            if (vlog_subkind(dt) == DT_DYNARRAY || vlog_subkind(dt) == DT_QUEUE
                || vlog_subkind(dt) == DT_ASSOC
                || vlog_subkind(dt) == DT_STRING) {
               const int np = vlog_ranges(dt);
               if (skip == 0)
                  return np > 0 ? vlog_range(dt, 0) : NULL;
               skip--;
               dt = vlog_has_type(dt) ? vlog_get_type(vlog_type(dt)) : NULL;
               continue;
            }
            const int np = vlog_ranges(dt);
            if (skip < np)
               return vlog_range(dt, skip);
            skip -= np;
            dt = vlog_has_type(dt) ? vlog_get_type(vlog_type(dt)) : NULL;
         }
         return NULL;
      }
   default:
      return NULL;
   }
}

static type_mask_t vlog_check_part_select(vlog_node_t v)
{
   if (vlog_subkind(v) == V_RANGE_CONST) {
      vlog_node_t left = vlog_left(v);
      vlog_check_expr(left);
   }

   vlog_node_t right = vlog_right(v);
   vlog_check_expr(right);

   const type_mask_t mask = vlog_check_index(v);

   if (vlog_subkind(v) == V_RANGE_CONST) {
      int64_t sl, sr, dl, dr;
      if (vlog_get_const(vlog_left(v), &sl)
          && vlog_get_const(vlog_right(v), &sr) && sl != sr) {
         vlog_node_t dim = vlog_select_dim(vlog_value(v), 0);
         if (dim != NULL && vlog_bounds(dim, &dl, &dr) && dl != dr
             && (sl > sr) != (dl > dr))
            error_at(vlog_loc(v), "part-select direction [%"PRIi64":%"PRIi64"] "
                     "does not match declared range [%"PRIi64":%"PRIi64"]",
                     sl, sr, dl, dr);
      }
   }

   return mask;
}

static type_mask_t vlog_check_user_fcall(vlog_node_t v)
{
   vlog_node_t func = vlog_ref(v);
   if (vlog_kind(func) != V_FUNC_DECL) {
      diag_t *d = diag_new(DIAG_ERROR, vlog_loc(v));
      diag_printf(d, "'%s' is not a function", istr(vlog_ident(func)));
      diag_hint(d, vlog_loc(func), "'%s' declared here",
                istr(vlog_ident(func)));
      diag_emit(d);
      return TM_ERROR;
   }

   if (in_sva_expr) {
      const int nports = vlog_ports(func);
      for (int i = 0; i < nports; i++) {
         switch (vlog_subkind(vlog_port(func, i))) {
         case V_PORT_OUTPUT:
         case V_PORT_INOUT:
            error_at(vlog_loc(v), "function '%s' with an output argument "
                     "cannot be called in a sequence or property expression",
                     istr(vlog_ident(func)));
            i = nports;
            break;
         case V_PORT_REF:
            error_at(vlog_loc(v), "function '%s' with a ref argument cannot "
                     "be called in a sequence or property expression",
                     istr(vlog_ident(func)));
            i = nports;
            break;
         default:
            break;
         }
      }
   }

   vlog_check_call_args(v, func);

   type_mask_t tmask = get_type_mask(func);

   // Per IEEE 1800-2023 section 11.2.1, a user-defined function call is a
   // constant expression when all its arguments are constant expressions.
   // Check if every actual parameter is constant.
   const int nparams = vlog_params(v);
   bool all_const = nparams > 0;
   for (int i = 0; i < nparams && all_const; i++) {
      type_mask_t pmask = vlog_check_expr(vlog_param(v, i));
      if (!(pmask & TM_CONST))
         all_const = false;
   }

   if (all_const)
      tmask |= TM_CONST;

   return tmask;
}

static type_mask_t vlog_check_concat(vlog_node_t v)
{
   if (vlog_has_value(v)) {
      vlog_node_t repeat = vlog_value(v);
      vlog_check_expr(repeat);
   }

   type_mask_t mask = TM_ANY;
   const int nparams = vlog_params(v);
   for (int i = 0; i < nparams; i++)
      mask &= vlog_check_expr(vlog_param(v, i));

   return TM(DT_LOGIC) | mask;
}

static type_mask_t vlog_check_event(vlog_node_t v)
{
   const type_mask_t mask = vlog_check_expr(vlog_value(v));

   switch (vlog_subkind(v)) {
   case V_EVENT_POSEDGE:
   case V_EVENT_NEGEDGE:
   case V_EVENT_EDGE:
      if (mask != TM_ERROR && (mask & TM_REAL) && !(mask & TM_INTEGRAL))
         error_at(vlog_loc(v), "an edge event control cannot be used with a "
                  "real value");
      else if (mask != TM_ERROR && (mask & TM(DT_EVENT))
               && !(mask & TM_INTEGRAL))
         error_at(vlog_loc(v), "an edge event control cannot be used with a "
                  "named event");
      break;
   default:
      break;
   }

   return TM(DT_LOGIC);
}

static bool vlog_aggregate_has_real(vlog_node_t type, int depth)
{
   // A real or shortreal anywhere in a structure or union

   if (type == NULL || depth > 16)
      return false;

   switch (vlog_kind(type)) {
   case V_TYPE_DECL:
      return vlog_has_type(type)
         && vlog_aggregate_has_real(vlog_type(type), depth + 1);
   case V_STRUCT_DECL:
   case V_UNION_DECL:
      {
         const int ndecls = vlog_decls(type);
         for (int i = 0; i < ndecls; i++) {
            vlog_node_t d = vlog_decl(type, i);
            if (vlog_has_type(d)
                && vlog_aggregate_has_real(vlog_type(d), depth + 1))
               return true;
         }
         return false;
      }
   case V_DATA_TYPE:
      switch (vlog_subkind(type)) {
      case DT_REAL:
      case DT_SHORTREAL:
      case DT_REALTIME:
         return depth > 0;
      case DT_QUEUE:
      case DT_DYNARRAY:
      case DT_ASSOC:
         return depth > 0 && vlog_has_type(type)
            && vlog_aggregate_has_real(vlog_type(type), depth + 1);
      default:
         return false;
      }
   default:
      return false;
   }
}

static type_mask_t vlog_check_binary(vlog_node_t v)
{
   vlog_node_t left = vlog_left(v);
   vlog_node_t right = vlog_right(v);

   type_mask_t lmask = vlog_check_expr(left);
   type_mask_t rmask = vlog_check_expr(right);

   vlog_node_t ltype = vlog_get_type(left);
   vlog_node_t rtype = vlog_get_type(right);
   if (vlog_is_assoc_type(ltype) || vlog_is_assoc_type(rtype)) {
      const vlog_binary_t op = vlog_subkind(v);
      if ((op == V_BINARY_LOG_EQ || op == V_BINARY_LOG_NEQ
           || op == V_BINARY_CASE_EQ || op == V_BINARY_CASE_NEQ)
          && vlog_is_assoc_type(ltype) && vlog_is_assoc_type(rtype)
          && vlog_same_assoc_type(ltype, rtype))
         return TM(DT_BIT);

      error_at(vlog_loc(v), "operator cannot be applied to an associative "
               "array");
      return TM_ERROR;
   }

   if ((lmask & TM_REAL) && (rmask & TM_INTEGRAL))
      rmask |= TM_REAL;

   if ((rmask & TM_REAL) && (lmask & TM_INTEGRAL))
      lmask |= TM_REAL;

   if ((lmask & TM_INTEGRAL) && (rmask & TM_INTEGRAL)) {
      lmask |= TM_INTEGRAL;
      rmask |= TM_INTEGRAL;
   }

   const vlog_binary_t binop = vlog_subkind(v);
   if ((binop == V_BINARY_LOG_AND || binop == V_BINARY_LOG_OR)
       && ((lmask | rmask) & TM_CLASS)) {
      // A class handle is true when it is not null
      const type_mask_t truth = TM_INTEGRAL | TM_REAL | TM_CLASS | TM_CONST;
      if ((lmask & truth) && (rmask & truth))
         return TM(DT_LOGIC);
   }

   // See table 11-1 in 1800-2023 section 11.3 for allowed operand types
   type_mask_t allow = TM_ANY, result = lmask & rmask;
   switch (vlog_subkind(v)) {
   case V_BINARY_PLUS:
   case V_BINARY_MINUS:
   case V_BINARY_TIMES:
   case V_BINARY_DIVIDE:
   case V_BINARY_EXP:
   case V_BINARY_LOG_OR:
   case V_BINARY_LOG_AND:
      allow = TM_INTEGRAL | TM_REAL | TM_CONST;
      break;
   case V_BINARY_MOD:
   case V_BINARY_OR:
   case V_BINARY_AND:
   case V_BINARY_XOR:
   case V_BINARY_XNOR:
   case V_BINARY_NAND:
   case V_BINARY_NOR:
   case V_BINARY_SHIFT_LL:
   case V_BINARY_SHIFT_RL:
   case V_BINARY_SHIFT_LA:
   case V_BINARY_SHIFT_RA:
      allow = TM_INTEGRAL | TM_CONST;
      break;
   case V_BINARY_CASE_EQ:
   case V_BINARY_CASE_NEQ:
      allow = TM_ANY & ~TM_REAL;
      result = TM(DT_BIT) | (lmask & rmask & TM_CONST);
      break;
   case V_BINARY_WILD_EQ:
   case V_BINARY_WILD_NEQ:
      allow = TM_INTEGRAL | TM_CONST;
      result = TM(DT_LOGIC);
      break;
   }

   if (lmask & rmask & allow) {
      // Handles of related classes can be compared in either direction
      vlog_node_t lt = vlog_get_type(left), rt = vlog_get_type(right);
      const bool related = lt != NULL && rt != NULL
         && vlog_kind(lt) == V_CLASS_DECL && vlog_kind(rt) == V_CLASS_DECL
         && (vlog_class_derives(lt, rt) || vlog_class_derives(rt, lt));

      if (!related)
         vlog_check_same_type(left, lmask, right, rmask);

      // Comparing strings, arrays or class handles gives a single bit
      if (result & TM_ARRAY && !(result & TM(DT_LOGIC)))
         result = TM(DT_BIT);
      else if (result & TM_CLASS)
         result = TM(DT_BIT);

      if ((binop == V_BINARY_CASE_EQ || binop == V_BINARY_CASE_NEQ)
          && (vlog_aggregate_has_real(lt, 0)
              || vlog_aggregate_has_real(rt, 0))) {
         // Case equality is not defined for real operands (table 11-1)
         // nor so for the real members of an aggregate (11.4.5)
         diag_t *d = pedantic_diag(vlog_loc(v));
         if (d != NULL) {
            diag_printf(d, "case equality is not allowed on an aggregate "
                        "with a real member");
            diag_hint(d, vlog_loc(v), "use %s instead",
                      binop == V_BINARY_CASE_EQ ? "==" : "!=");
            diag_emit(d);
         }
      }

      switch (binop) {
      case V_BINARY_LT:
      case V_BINARY_LEQ:
      case V_BINARY_GT:
      case V_BINARY_GEQ:
      case V_BINARY_LOG_EQ:
      case V_BINARY_LOG_NEQ:
      case V_BINARY_LOG_AND:
      case V_BINARY_LOG_OR:
         // The result of a relational, equality or logical operator is a
         // 1-bit integral value whatever the operand types (11.4.4,
         // 11.4.5, 11.4.7)
         if (!(result & TM_INTEGRAL))
            result = TM(DT_LOGIC) | (result & TM_CONST);
         break;
      default:
         break;
      }

      return result;
   }

   bool string_compare = false;
   if (!((lmask | rmask) & ~(TM(DT_STRING) | TM_INTEGRAL | TM_CONST))
       && ((lmask & TM(DT_STRING)) || (rmask & TM(DT_STRING)))) {
      switch (binop) {
      case V_BINARY_LOG_EQ:
      case V_BINARY_LOG_NEQ:
      case V_BINARY_CASE_EQ:
      case V_BINARY_CASE_NEQ:
      case V_BINARY_LT:
      case V_BINARY_LEQ:
      case V_BINARY_GT:
      case V_BINARY_GEQ:
         string_compare = true;
         break;
      default:
         break;
      }
   }

   if (relaxed_rules() && string_compare) {
      // Not allowed by table 11-1 but a character compared with a string is
      // converted to a string like in an assignment to a string (6.16)
      diag_t *w = diag_new(DIAG_WARN, vlog_loc(v));
      diag_printf(w, "comparison of an integral value with a string "
                  "converts it to a string");
      diag_emit(w);
      return TM(DT_BIT);
   }

   diag_t *d = diag_new(DIAG_ERROR, vlog_loc(v));
   diag_printf(d, "invalid operands for binary expression");
   diag_hint(d, vlog_loc(v), "have '%s' and '%s'", type_mask_str(lmask),
             type_mask_str(rmask));
   if (string_compare)
      diag_hint_relaxable(d);
   diag_emit(d);

   return TM_ERROR;
}

static type_mask_t vlog_check_unary(vlog_node_t v)
{
   type_mask_t tmask = vlog_check_expr(vlog_value(v));

   // See table 11-1 in 1800-2023 section 11.3 for allowed operand types
   type_mask_t allow = TM_ANY, result = tmask;
   switch (vlog_subkind(v)) {
   case V_UNARY_BITNEG:
   case V_UNARY_AND:
   case V_UNARY_OR:
   case V_UNARY_NAND:
   case V_UNARY_NOR:
   case V_UNARY_XOR:
   case V_UNARY_XNOR:
      allow = TM_INTEGRAL | TM_CONST;
      break;
   case V_UNARY_IDENTITY:
   case V_UNARY_NEG:
      allow = TM_INTEGRAL | TM_REAL | TM_CONST;
      break;
   case V_UNARY_NOT:
      allow = TM_INTEGRAL | TM_REAL | TM_CONST | TM_CLASS;
      if (tmask == TM_CLASS)
         result = TM_INTEGRAL;
      break;
   }

   if (tmask == TM_ERROR)
      return TM_ERROR;
   else if (tmask & allow)
      return result;

   diag_t *d = diag_new(DIAG_ERROR, vlog_loc(v));
   diag_printf(d, "invalid operands for unary expression");
   diag_hint(d, vlog_loc(v), "have '%s'", type_mask_str(tmask));
   diag_emit(d);

   return TM_ERROR;
}

static type_mask_t vlog_check_cond_expr(vlog_node_t v)
{
   type_mask_t vmask = vlog_check_expr(vlog_value(v));

   type_mask_t lmask = vlog_check_expr(vlog_left(v));
   type_mask_t rmask = vlog_check_expr(vlog_right(v));

   const type_mask_t common = lmask & rmask;
   if ((common & TM_CLASS) && !(common & TM_INTEGRAL))
      return TM_CLASS;   // Both could be integral if the types are not known
   else if ((common & TM(DT_STRING)) && !(common & TM_CONST))
      return TM(DT_STRING);

   return TM_INTEGRAL | (vmask & lmask & rmask & TM_CONST);
}

static type_mask_t vlog_check_cast_call(vlog_node_t v)
{
   // $cast ( singular_variable_lvalue , singular_expression )

   const int nparams = vlog_params(v);
   if (nparams != 2) {
      error_at(vlog_loc(v), "$cast expects 2 arguments but has %d", nparams);
      return TM_ERROR;
   }

   vlog_node_t dest = vlog_param(v, 0), src = vlog_param(v, 1);
   const type_mask_t dmask = vlog_check_expr(dest);
   const type_mask_t smask = vlog_check_expr(src);

   vlog_check_variable_lvalue(dest, dest);

   if (dmask == TM_ERROR || smask == TM_ERROR)
      return TM(DT_INT);

   if (dmask & TM_CLASS) {
      // Casting between unrelated classes is allowed and simply fails
      if (!(smask & TM_CLASS))
         error_at(vlog_loc(src), "the source of $cast to a class handle must "
                  "be a class handle");
   }
   else if (smask & TM_CLASS)
      error_at(vlog_loc(src), "a class handle can only be cast to a class "
               "handle");
   else if (dmask == TM(DT_STRING) && (smask & (TM_INTEGRAL | TM(DT_STRING))))
      ;   // Converted as string'() does (6.24.2)
   else if ((dmask & TM_ARRAY) || (smask & TM_ARRAY))
      error_at(vlog_loc(dest), "sorry, $cast to or from this type is not "
               "supported");
   else if ((dmask & (TM_STRUCT | TM_UNION)) || (smask & (TM_STRUCT | TM_UNION)))
      error_at(vlog_loc(dest), "sorry, $cast to or from this type is not "
               "supported");

   return TM(DT_INT);
}

static type_mask_t vlog_check_sys_fcall(vlog_node_t v)
{
   if (vlog_ident(v) == ident_new("$cast"))
      return vlog_check_cast_call(v);
   else if (vlog_ident(v) == ident_new("$process_self"))
      return TM(DT_PROCESS) | TM_CLASS;

   type_mask_t mask = vlog_check_params(v);

   // These return a string which can also be assigned to an integral
   if (vlog_is_sformatf(vlog_ident(v))
       || vlog_ident(v) == ident_new("$typename"))
      return TM_INTEGRAL | TM(DT_STRING);

   // See 1800-2023 section 11.2.1 for list of constant system functions
   switch (vlog_subkind(v)) {
   case V_SYSTF_CLOG2:
   case V_SYSTF_SIGNED:
   case V_SYSTF_UNSIGNED:
   case V_SYSTF_RTOI:
   case V_SYSTF_FLOOR:
      return TM_INTEGRAL | (TM_CONST & mask);
   case V_SYSTF_SQRT:
   case V_SYSTF_CEIL:
   case V_SYSTF_MATH:
      return TM_REAL | (TM_CONST & mask);
   default:
      return TM_INTEGRAL;
   }
}

static void vlog_check_ctor_args(vlog_node_t v, vlog_node_t class)
{
   // A class without a constructor has one which takes no arguments

   vlog_check_arg_order(v);

   const int nparams = vlog_params(v);
   for (int i = 0; i < nparams; i++) {
      if (vlog_kind(vlog_param(v, i)) == V_NAMED_ARG)
         return;
   }

   vlog_node_t ctor = NULL;
   const int ndecls = vlog_decls(class);
   for (int i = 0; i < ndecls && ctor == NULL; i++) {
      vlog_node_t d = vlog_decl(class, i);
      if (vlog_kind(d) == V_CONSTRUCTOR)
         ctor = d;
   }

   const int nports = ctor != NULL ? vlog_params(ctor) : 0;

   int minargs = 0;
   for (int i = 0; i < nports; i++) {
      if (!vlog_has_value(vlog_param(ctor, i)))
         minargs = i + 1;
   }

   if (nparams > nports || nparams < minargs) {
      diag_t *d = diag_new(DIAG_ERROR, vlog_loc(v));
      diag_printf(d, "constructor of class %pQ expects %s%d argument%s but "
                  "have %d", vlog_ident(class),
                  minargs < nports ? "at most " : "", nports,
                  nports != 1 ? "s" : "", nparams);
      if (ctor != NULL)
         diag_hint(d, vlog_loc(ctor), "constructor declared here");
      diag_emit(d);
   }
}

static type_mask_t vlog_check_class_new(vlog_node_t v)
{
   const int nparams = vlog_params(v);
   for (int i = 0; i < nparams; i++)
      vlog_check_expr(vlog_param(v, i));

   if (vlog_has_value(v)) {
      // The operand of a shallow copy is an object handle (8.12)
      vlog_node_t src = vlog_value(v);
      (void)vlog_check_expr(src);
      vlog_node_t ref = vlog_kind(src) == V_REF && vlog_has_ref(src)
         ? vlog_ref(src) : NULL;
      if (ref != NULL && (vlog_kind(ref) == V_CLASS_DECL
                          || vlog_kind(ref) == V_TYPE_DECL)) {
         diag_t *d = diag_new(DIAG_ERROR, vlog_loc(src));
         diag_printf(d, "%pQ is a type but new must be followed by an "
                     "object handle to copy or by constructor arguments",
                     vlog_ident(ref));
         diag_hint(d, NULL, "the class of the new object is the type of "
                   "the target, or use %s::new", istr(vlog_ident(ref)));
         diag_emit(d);
         return TM_ERROR;
      }
   }

   if (vlog_has_type(v) && vlog_kind(vlog_type(v)) == V_DATA_TYPE
       && vlog_subkind(vlog_type(v)) == DT_PROCESS) {
      error_at(vlog_loc(v), "a process cannot be created with new");
      return TM_ERROR;
   }

   if (vlog_has_type(v) && vlog_kind(vlog_type(v)) == V_DATA_TYPE)
      return get_type_mask(vlog_type(v));   // Mailbox

   if (vlog_has_type(v)) {
      vlog_node_t class = vlog_get_type(vlog_type(v));
      if (class != NULL && vlog_kind(class) == V_CLASS_DECL
          && (vlog_flags(class) & VLOG_F_VIRTUAL) && !vlog_has_value(v))
         error_at(vlog_loc(v), "cannot create an instance of %s class %pQ",
                  (vlog_flags(class) & VLOG_F_INTERFACE) ? "interface"
                  : "virtual", vlog_ident(class));
      else if (class != NULL && vlog_kind(class) == V_CLASS_DECL
               && !vlog_has_value(v))
         vlog_check_ctor_args(v, class);
   }

   return TM_CLASS;
}

static type_mask_t vlog_check_pattern_expr_ex(vlog_node_t v, bool sized_dyn);

static type_mask_t vlog_check_dynamic_new(vlog_node_t v)
{
   vlog_check_expr(vlog_left(v));

   if (vlog_has_value(v)) {
      vlog_node_t init = vlog_value(v);
      if (vlog_kind(init) == V_PATTERN_EXPR)
         vlog_check_pattern_expr_ex(init, true);
      else
         vlog_check_expr(init);
   }

   return get_type_mask(vlog_type(v));
}

static type_mask_t vlog_check_op_assign(vlog_node_t v)
{
   vlog_node_t target = vlog_target(v);
   type_mask_t tmask = vlog_check_expr(target);

   vlog_check_variable_lvalue(target, target);

   vlog_check_expr(vlog_value(v));

   vlog_node_t ttype = vlog_get_type(target);
   if (ttype != NULL && vlog_kind(ttype) == V_ENUM_DECL) {
      if (vlog_subkind(v) != V_ASSIGN_EQUALS)
         error_at(vlog_loc(v), "an arithmetic assignment cannot be used with "
                  "a variable of enumeration type");
      else
         vlog_check_enum_value(target, vlog_value(v));
   }

   return tmask;
}

static type_mask_t vlog_check_prefix_postfix(vlog_node_t v)
{
   vlog_node_t target = vlog_target(v);
   type_mask_t tmask = vlog_check_expr(target);

   vlog_check_variable_lvalue(target, target);

   return tmask;
}

static type_mask_t vlog_check_min_typ_max(vlog_node_t v)
{
   vlog_check_expr(vlog_left(v));
   vlog_check_expr(vlog_right(v));
   return vlog_check_expr(vlog_value(v));
}

static type_mask_t vlog_check_pattern_expr_ex(vlog_node_t v, bool sized_dyn)
{
   if (!vlog_has_type(v))
      return TM_ERROR;

   vlog_node_t type = vlog_type(v);
   vlog_node_t bt = vlog_get_type(type);
   const bool assoc = vlog_is_assoc_type(bt);
   const bool unsized_arr = !sized_dyn && bt != NULL
      && vlog_kind(bt) == V_DATA_TYPE
      && (vlog_subkind(bt) == DT_QUEUE
          || (vlog_subkind(bt) == DT_DYNARRAY && vlog_ranges(bt) == 0));
   const bool is_struct = bt != NULL
      && (vlog_kind(bt) == V_STRUCT_DECL || vlog_kind(bt) == V_UNION_DECL);

   bool seen_pos = false, seen_keyed = false, seen_default = false;
   const int nparams = vlog_params(v);
   for (int i = 0; i < nparams; i++) {
      vlog_node_t item = vlog_param(v, i);
      if (vlog_kind(item) != V_PATTERN_ITEM)
         continue;

      const int subk = vlog_subkind(item);
      if (subk == 1) {
         if (seen_default)
            error_at(vlog_loc(item), "duplicate default item in assignment "
                     "pattern");
         seen_default = true;
         if (unsized_arr)
            error_at(vlog_loc(item), "keyed or default item is not allowed "
                     "in a dynamic array or queue assignment pattern");
      }
      else if (subk == 3)
         seen_keyed = true;
      else if (subk == 2) {
         seen_pos = true;
         if (vlog_has_left(item)) {
            vlog_node_t count = vlog_left(item);
            const type_mask_t cmask = vlog_check_expr(count);
            int64_t rep;
            if (cmask != TM_ERROR && !(cmask & TM_CONST))
               error_at(vlog_loc(count), "assignment pattern replication count "
                        "must be a constant expression");
            else if (vlog_get_const(count, &rep) && rep <= 0)
               error_at(vlog_loc(count), "assignment pattern replication count "
                        "must be positive");
         }
      }
      else if (vlog_has_left(item)) {
         seen_keyed = true;
         if (unsized_arr)
            error_at(vlog_loc(item), "keyed or default item is not allowed "
                     "in a dynamic array or queue assignment pattern");
         else if (!assoc) {
            vlog_node_t key = vlog_left(item);
            bool is_field = false;
            if (is_struct && vlog_kind(key) == V_REF) {
               if (!vlog_has_ref(key))
                  is_field = true;
               else {
                  vlog_node_t ref = vlog_ref(key);
                  const int ndecls = vlog_decls(bt);
                  for (int j = 0; j < ndecls; j++) {
                     if (vlog_decl(bt, j) == ref) {
                        is_field = true;
                        break;
                     }
                  }
               }
            }

            if (!is_field) {
               const type_mask_t kmask = vlog_check_expr(key);
               if (kmask != TM_ERROR && !(kmask & TM_CONST))
                  error_at(vlog_loc(key), "array assignment pattern key must "
                           "be a constant expression");
               else if (kmask != TM_ERROR && !(kmask & TM_INTEGRAL))
                  error_at(vlog_loc(key), "array assignment pattern key must "
                           "be an integral constant expression");
            }
         }
      }
      else
         seen_pos = true;

      if (vlog_has_right(item)) {
         vlog_node_t rhs = vlog_right(item);
         if (subk == 2 && vlog_kind(rhs) == V_PATTERN_EXPR) {
            const int nr = vlog_params(rhs);
            for (int j = 0; j < nr; j++) {
               vlog_node_t ri = vlog_param(rhs, j);
               if (vlog_has_right(ri)
                   && vlog_kind(vlog_right(ri)) == V_PATTERN_EXPR)
                  (void)vlog_check_pattern_expr_ex(vlog_right(ri), false);
            }
         }
         else if (vlog_kind(rhs) == V_PATTERN_EXPR)
            (void)vlog_check_pattern_expr_ex(rhs, false);
      }
   }

   if (seen_pos && seen_keyed)
      error_at(vlog_loc(v), "cannot mix positional and keyed items in "
               "assignment pattern");

   return get_type_mask(type);
}

static type_mask_t vlog_check_pattern_expr(vlog_node_t v)
{
   return vlog_check_pattern_expr_ex(v, false);
}

static type_mask_t vlog_check_cast(vlog_node_t v)
{
   if (vlog_subkind(v) == V_CAST_TYPEREF)
      return TM(DT_INT);
   else if (vlog_subkind(v) == V_CAST_TYPE) {
      vlog_node_t val = vlog_value(v);
      const type_mask_t smask = vlog_check_expr(val);
      const type_mask_t dmask = get_type_mask(vlog_type(v));
      if ((dmask & TM_REAL) && smask != TM_ERROR) {
         const bool is_unpacked = vlog_unpacked_dims(val) > 0;
         if (is_unpacked || !(smask & (TM_INTEGRAL | TM_REAL)))
            error_at(vlog_loc(v), "cannot cast %s to '%s'",
                     is_unpacked ? "an unpacked array" : type_mask_str(smask),
                     type_mask_str(dmask));
      }
      return dmask;
   }

   vlog_check_expr(vlog_left(v));
   return vlog_check_expr(vlog_value(v));
}

static void vlog_check_cons_items(vlog_node_t v);

static void vlog_check_integral(vlog_node_t v, const char *what)
{
   const type_mask_t tmask = vlog_check_expr(v);
   if (tmask == TM_ERROR || (tmask & TM_INTEGRAL))
      return;

   diag_t *d = diag_new(DIAG_ERROR, vlog_loc(v));
   diag_printf(d, "%s must have an integral type", what);
   diag_hint(d, vlog_loc(v), "have '%s'", type_mask_str(tmask));
   diag_emit(d);
}

static vlog_node_t vlog_real_operand(vlog_node_t v)
{
   switch (vlog_kind(v)) {
   case V_BINARY:
      if (vlog_is_real(vlog_left(v)))
         return vlog_left(v);
      else if (vlog_is_real(vlog_right(v)))
         return vlog_right(v);
      else {
         vlog_node_t r = vlog_real_operand(vlog_left(v));
         return r ?: vlog_real_operand(vlog_right(v));
      }
   case V_UNARY:
      if (vlog_is_real(vlog_value(v)))
         return vlog_value(v);
      return vlog_real_operand(vlog_value(v));
   default:
      return NULL;
   }
}

static void vlog_check_cons_integral(vlog_node_t v, const char *what)
{
   // Constraint expressions may only use integral operands (18.3)
   const type_mask_t tmask = vlog_check_expr(v);
   if (tmask == TM_ERROR)
      return;

   vlog_node_t real = NULL;
   if (!(tmask & TM_INTEGRAL) || (real = vlog_real_operand(v)) != NULL) {
      diag_t *d = diag_new(DIAG_ERROR, vlog_loc(v));
      diag_printf(d, "%s must have an integral type", what);
      if (real != NULL)
         diag_hint(d, vlog_loc(real), "operand has type 'real'");
      else
         diag_hint(d, vlog_loc(v), "have '%s'", type_mask_str(tmask));
      diag_emit(d);
   }
}

static type_mask_t vlog_check_inside(vlog_node_t v)
{
   // A real or string value is compared with each item
   const type_mask_t vmask = vlog_check_expr(vlog_value(v));
   const type_mask_t scalar = TM_REAL | TM(DT_STRING);
   bool is_scalar = vmask != TM_ERROR && !(vmask & TM_INTEGRAL)
      && (vmask & scalar);

   // A class handle is compared with handles or arrays of them
   bool is_handle = vmask != TM_ERROR && !(vmask & TM_INTEGRAL)
      && (vmask & TM_CLASS) && !(vmask & scalar);
   const int nparams = vlog_params(v);
   type_mask_t *masks LOCAL = xcalloc_array(nparams + 1, sizeof(type_mask_t));
   for (int i = 0; i < nparams; i++) {
      vlog_node_t p = vlog_param(v, i);
      if (vlog_kind(p) == V_INSIDE_RANGE)
         is_handle = false;
      else {
         masks[i] = vlog_check_expr(p);
         is_handle = is_handle && (masks[i] == TM_ERROR
            || (masks[i] & (TM_CLASS | TM(DT_DYNARRAY) | TM(DT_QUEUE))));
      }
   }

   is_scalar |= is_handle;

   if (vmask != TM_ERROR && !is_scalar && !(vmask & TM_INTEGRAL)) {
      diag_t *d = diag_new(DIAG_ERROR, vlog_loc(vlog_value(v)));
      diag_printf(d, "expression before inside must have an integral type");
      diag_hint(d, vlog_loc(vlog_value(v)), "have '%s'", type_mask_str(vmask));
      diag_emit(d);
   }

   for (int i = 0; i < nparams; i++) {
      vlog_node_t p = vlog_param(v, i);
      if (vlog_kind(p) == V_INSIDE_RANGE) {
         if (is_scalar) {
            vlog_check_expr(vlog_left(p));
            vlog_check_expr(vlog_right(p));
         }
         else {
            vlog_check_integral(vlog_left(p), "range bound");
            vlog_check_integral(vlog_right(p), "range bound");
         }
         continue;
      }

      const type_mask_t tmask = masks[i];
      if (tmask != TM_ERROR && !is_scalar
          && !(tmask & (TM_INTEGRAL | TM(DT_DYNARRAY) | TM(DT_QUEUE)))) {
         diag_t *d = diag_new(DIAG_ERROR, vlog_loc(p));
         diag_printf(d, "inside set item must be integral or an array");
         diag_hint(d, vlog_loc(p), "have '%s'", type_mask_str(tmask));
         diag_emit(d);
      }
   }

   return TM(DT_BIT);
}

static void vlog_find_randc_cb(vlog_node_t v, void *ctx)
{
   switch (vlog_kind(v)) {
   case V_REF:
   case V_MEMBER_REF:
      if (vlog_has_ref(v)) {
         vlog_node_t decl = vlog_ref(v);
         if (vlog_kind(decl) == V_VAR_DECL && (vlog_flags(decl) & VLOG_F_RANDC))
            *(vlog_node_t *)ctx = decl;
      }
      break;
   default:
      break;
   }
}

static void vlog_check_no_randc(vlog_node_t expr, const char *what)
{
   vlog_node_t randc = NULL;
   vlog_visit(expr, vlog_find_randc_cb, &randc);

   if (randc != NULL)
      error_at(vlog_loc(expr), "randc variable %pQ cannot be used %s",
               vlog_ident(randc), what);
}

static void vlog_check_pattern(vlog_node_t v)
{
   switch (vlog_subkind(v)) {
   case V_PAT_CONST:
      (void)vlog_check_expr(vlog_value(v));
      break;
   case V_PAT_TAGGED:
   case V_PAT_STRUCT:
      {
         const int nparams = vlog_params(v);
         for (int i = 0; i < nparams; i++)
            vlog_check_pattern(vlog_param(v, i));
      }
      break;
   default:
      break;
   }
}

static type_mask_t vlog_check_dist(vlog_node_t v)
{
   vlog_check_integral(vlog_value(v), "expression before dist");
   vlog_check_no_randc(vlog_value(v), "in a dist constraint");

   const int nparams = vlog_params(v);
   for (int i = 0; i < nparams; i++) {
      vlog_node_t p = vlog_param(v, i);
      vlog_check_integral(vlog_left(p), "dist value");
      if (vlog_has_right(p))
         vlog_check_integral(vlog_right(p), "dist range bound");
      vlog_check_integral(vlog_delay(p), "dist weight");
   }

   return TM(DT_BIT);
}

static void vlog_check_cons_item(vlog_node_t v)
{
   switch (vlog_kind(v)) {
   case V_CONS_EXPR:
      if (vlog_flags(v) & VLOG_F_DISABLE) {
         vlog_node_t value = vlog_value(v);
         (void)vlog_check_expr(value);

         switch (vlog_kind(value)) {
         case V_REF:
         case V_MEMBER_REF:
         case V_BIT_SELECT:
            break;
         default:
            error_at(vlog_loc(value), "the operand of disable soft must be "
                     "a random variable");
            break;
         }
         break;
      }

      vlog_check_cons_integral(vlog_value(v), "constraint expression");
      if ((vlog_flags(v) & VLOG_F_SOFT) && vlog_kind(vlog_value(v)) == V_DIST)
         error_at(vlog_loc(v), "soft constraint cannot be used with dist");
      else if (vlog_flags(v) & VLOG_F_SOFT)
         vlog_check_no_randc(vlog_value(v), "in a soft constraint");
      break;
   case V_CONS_IMPLY:
      vlog_check_cons_integral(vlog_value(v), "implication condition");
      vlog_check_cons_items(v);
      break;
   case V_CONS_IF:
      vlog_check_cons_integral(vlog_value(v), "condition");
      vlog_check_cons_item(vlog_left(v));
      if (vlog_has_right(v))
         vlog_check_cons_item(vlog_right(v));
      break;
   case V_CONS_BLOCK:
      vlog_check_cons_items(v);
      break;
   case V_CONS_FOREACH:
      (void)vlog_check_expr(vlog_value(v));
      vlog_check_cons_items(v);
      break;
   case V_CONS_UNIQUE:
      {
         const int nparams = vlog_params(v);
         for (int i = 0; i < nparams; i++)
            (void)vlog_check_expr(vlog_param(v, i));
      }
      break;
   case V_CONS_SOLVE:
      (void)vlog_check_expr(vlog_left(v));
      (void)vlog_check_expr(vlog_right(v));
      vlog_check_no_randc(vlog_left(v), "in a solve before constraint");
      vlog_check_no_randc(vlog_right(v), "in a solve before constraint");
      break;
   default:
      CANNOT_HANDLE(v);
   }
}

static void vlog_check_cons_items(vlog_node_t v)
{
   const int nstmts = vlog_stmts(v);
   for (int i = 0; i < nstmts; i++)
      vlog_check_cons_item(vlog_stmt(v, i));
}

static void vlog_check_randomize_args(vlog_node_t v)
{
   const int nparams = vlog_params(v);
   for (int i = 0; i < nparams; i++) {
      vlog_node_t p = vlog_param(v, i);
      if (vlog_kind(p) == V_NULL)
         continue;

      (void)vlog_check_expr(p);
      vlog_check_variable_lvalue(p, p);
   }
}

static type_mask_t vlog_check_std_randomize(vlog_node_t v)
{
   vlog_check_randomize_args(v);
   vlog_check_cons_items(v);
   return TM(DT_INT);
}

static void vlog_check_process_method(vlog_node_t v)
{
   switch ((vlog_method_t)vlog_subkind(v)) {
   case V_METHOD_PROC_STATUS:
   case V_METHOD_PROC_KILL:
   case V_METHOD_PROC_AWAIT:
   case V_METHOD_PROC_SUSPEND:
   case V_METHOD_PROC_RESUME:
      if (vlog_params(v) != 0)
         error_at(vlog_loc(v), "method %pQ of process expects no arguments "
                  "but has %d", vlog_ident(v), vlog_params(v));
      break;
   default:
      break;
   }
}

static void vlog_check_string_method(vlog_node_t v)
{
   int want;
   switch ((vlog_method_t)vlog_subkind(v)) {
   case V_METHOD_STR_LEN:
   case V_METHOD_STR_TOUPPER:
   case V_METHOD_STR_TOLOWER:
   case V_METHOD_STR_ATOI:
   case V_METHOD_STR_ATOHEX:
   case V_METHOD_STR_ATOOCT:
   case V_METHOD_STR_ATOBIN:
   case V_METHOD_STR_ATOREAL:
      want = 0;
      break;
   case V_METHOD_STR_GETC:
   case V_METHOD_STR_COMPARE:
   case V_METHOD_STR_ICOMPARE:
   case V_METHOD_STR_ITOA:
   case V_METHOD_STR_HEXTOA:
   case V_METHOD_STR_OCTTOA:
   case V_METHOD_STR_BINTOA:
   case V_METHOD_STR_REALTOA:
      want = 1;
      break;
   case V_METHOD_STR_PUTC:
   case V_METHOD_STR_SUBSTR:
      want = 2;
      break;
   default:
      return;
   }

   if (vlog_params(v) != want)
      error_at(vlog_loc(v), "method %pQ of a string expects %d argument%s "
               "but has %d", vlog_ident(v), want, want == 1 ? "" : "s",
               vlog_params(v));
}

static void vlog_check_queue_method(vlog_node_t v)
{
   vlog_node_t qty = vlog_get_type(vlog_value(v));
   if (qty == NULL || vlog_kind(qty) != V_DATA_TYPE
       || vlog_subkind(qty) != DT_QUEUE)
      return;

   int min_args, max_args;
   switch ((vlog_method_t)vlog_subkind(v)) {
   case V_METHOD_SIZE:
   case V_METHOD_POP_FRONT:
   case V_METHOD_POP_BACK:
      min_args = max_args = 0;
      break;
   case V_METHOD_PUSH_FRONT:
   case V_METHOD_PUSH_BACK:
      min_args = max_args = 1;
      break;
   case V_METHOD_ARR_INSERT:
      min_args = max_args = 2;
      break;
   case V_METHOD_DELETE:
      min_args = 0;
      max_args = 1;
      break;
   default:
      return;
   }

   const int nparams = vlog_params(v);
   if (nparams < min_args || nparams > max_args) {
      if (min_args == max_args)
         error_at(vlog_loc(v), "method %pQ of a queue expects %d argument%s "
                  "but has %d", vlog_ident(v), min_args,
                  min_args == 1 ? "" : "s", nparams);
      else
         error_at(vlog_loc(v), "method %pQ of a queue expects at most %d "
                  "argument%s but has %d", vlog_ident(v), max_args,
                  max_args == 1 ? "" : "s", nparams);
   }
}

static void vlog_check_assoc_method(vlog_node_t v)
{
   vlog_node_t aty = vlog_get_type(vlog_value(v));
   if (aty == NULL || vlog_kind(aty) != V_DATA_TYPE
       || vlog_subkind(aty) != DT_ASSOC)
      return;

   const int nparams = vlog_params(v);
   int want_min = 0, want_max = 0;
   bool output = false;

   switch ((vlog_method_t)vlog_subkind(v)) {
   case V_METHOD_SIZE:
      break;
   case V_METHOD_DELETE:
      want_max = 1;
      break;
   case V_METHOD_ASSOC_EXISTS:
      want_min = want_max = 1;
      break;
   case V_METHOD_ASSOC_FIRST:
   case V_METHOD_ASSOC_LAST:
   case V_METHOD_ASSOC_NEXT:
   case V_METHOD_ASSOC_PREV:
      want_min = want_max = 1;
      output = true;
      break;
   default:
      return;
   }

   if (nparams < want_min || nparams > want_max) {
      error_at(vlog_loc(v), "method %pQ of an associative array expects %d "
               "argument%s but has %d", vlog_ident(v), want_max,
               want_max == 1 ? "" : "s", nparams);
      return;
   }

   if (nparams == 0 || vlog_kind(vlog_param(v, 0)) == V_EMPTY)
      return;

   vlog_node_t arg = vlog_param(v, 0);
   const type_mask_t mask = vlog_check_expr(arg);

   if (output) {
      vlog_check_variable_lvalue(arg, arg);
      if (vlog_assoc_key(aty) == NULL && !(mask & TM_INTEGRAL))
         error_at(vlog_loc(arg), "argument must have an integral type");
      else
         vlog_check_assoc_key(aty, arg, mask, "argument");
   }
   else
      vlog_check_assoc_key(aty, arg, mask, "argument");
}

static type_mask_t vlog_check_expr(vlog_node_t v)
{
   switch (vlog_kind(v)) {
   case V_BINARY:
      return vlog_check_binary(v);
   case V_UNARY:
      return vlog_check_unary(v);
   case V_COND_EXPR:
      return vlog_check_cond_expr(v);
   case V_MIN_TYP_MAX:
      return vlog_check_min_typ_max(v);
   case V_SYS_FCALL:
      return vlog_check_sys_fcall(v);
   case V_USER_FCALL:
      return vlog_check_user_fcall(v);
   case V_REF:
      return vlog_check_ref(v);
   case V_MOD_REF:
      return vlog_check_mod_ref(v);
   case V_HIER_REF:
      return vlog_check_hier_ref(v);
   case V_MEMBER_REF:
      return vlog_check_member_ref(v);
   case V_BIT_SELECT:
      return vlog_check_bit_select(v);
   case V_PART_SELECT:
      return vlog_check_part_select(v);
   case V_EVENT:
      return vlog_check_event(v);
   case V_CLASS_NEW:
      return vlog_check_class_new(v);
   case V_DYNAMIC_NEW:
      return vlog_check_dynamic_new(v);
   case V_CONCAT:
      return vlog_check_concat(v);
   case V_PREFIX:
   case V_POSTFIX:
      return vlog_check_prefix_postfix(v);
   case V_OP_ASSIGN:
      return vlog_check_op_assign(v);
   case V_STREAM:
      {
         if (vlog_has_value(v))
            vlog_check_expr(vlog_value(v));

         const int nparams = vlog_params(v);
         for (int i = 0; i < nparams; i++)
            vlog_check_expr(vlog_param(v, i));

         return TM_INTEGRAL;
      }
   case V_PATTERN_EXPR:
      return vlog_check_pattern_expr(v);
   case V_CAST:
      return vlog_check_cast(v);
   case V_INSIDE:
      return vlog_check_inside(v);
   case V_DIST:
      return vlog_check_dist(v);
   case V_MATCHES:
      (void)vlog_check_expr(vlog_value(v));
      vlog_check_pattern(vlog_left(v));
      return TM(DT_BIT);
   case V_STD_RANDOMIZE:
      return vlog_check_std_randomize(v);
   case V_NUMBER:
   case V_STRENGTH:
      return TM_INTEGRAL | TM_CONST;
   case V_REAL:
      return TM_REAL | TM_CONST;
   case V_STRING:
      return TM(DT_LOGIC) | TM(DT_STRING) | TM_CONST;
   case V_NAMED_ARG:
      if (vlog_has_value(v))
         return vlog_check_expr(vlog_value(v));
      return TM_ANY;
   case V_EMPTY:
      return TM_EMPTY;
   case V_NULL:
      return TM_CLASS | TM_CONST;
   case V_THIS:
      return TM_CLASS;
   case V_DOLLAR:
      return TM(DT_INT);
   case V_METHOD_CALL:
      {
         switch (vlog_subkind(v)) {
         case V_METHOD_RANDOMIZE:
            vlog_check_randomize_args(v);
            vlog_check_cons_items(v);
            return TM(DT_INT);
         case V_METHOD_RAND_MODE:
         case V_METHOD_CONSTRAINT_MODE:
            vlog_check_params(v);
            return TM(DT_INT);
         default:
            break;
         }

         vlog_check_assoc_method(v);
         vlog_check_process_method(v);
         vlog_check_string_method(v);
         vlog_check_queue_method(v);

         const int nparams = vlog_params(v);
         for (int i = 0; i < nparams; i++) {
            vlog_node_t p = vlog_param(v, i);
            if (vlog_kind(p) != V_EMPTY)
               (void)vlog_check_expr(p);
         }

         vlog_node_t type = vlog_get_type(v);
         if (type != NULL)
            return get_type_mask(type);
         else if (vlog_kind(vlog_value(v)) == V_HIER_REF
                  || vlog_subkind(v) == V_METHOD_CLASS)
            return ~TM_CONST;   // Not known until elaboration
         else
            return TM_INTEGRAL;
      }
   default:
      CANNOT_HANDLE(v);
   }
}

static void vlog_check_tcheck_event(vlog_node_t v)
{
   assert(vlog_kind(v) == V_TCHECK_EVENT);

   vlog_node_t val = vlog_param(v, 0);
   vlog_node_t cond = (vlog_params(v) > 1) ? vlog_param(v, 1) : NULL;

   if (vlog_kind(val) == V_EVENT)
      val = vlog_value(val);

   assert(vlog_kind(val) == V_REF);
   vlog_node_t ref = vlog_ref(val);
   vlog_kind_t ref_kind = vlog_kind(ref);

   switch (ref_kind) {
   case V_PORT_DECL:
   case V_VAR_DECL:
   case V_NET_DECL:
      break;
   default:
      {
         diag_t *d = diag_new(DIAG_ERROR, vlog_loc(val));
         diag_printf(d, "invalid specify terminal descriptor kind: %s",
                     vlog_kind_str(ref_kind));
         diag_emit(d);
      }
   }

   if (cond != NULL)
      vlog_check_expr(cond);
}

static void vlog_check_tcheck(vlog_node_t v)
{
   vlog_tcheck_kind_t kind = vlog_subkind(v);

   switch (kind) {
   case V_TCHECK_SETUP:
   case V_TCHECK_HOLD:
      {
         vlog_check_tcheck_event(vlog_param(v, 0));
         vlog_check_tcheck_event(vlog_param(v, 1));

         vlog_node_t limit = vlog_param(v, 2);
         type_mask_t tmask = vlog_check_expr(limit);

         if ((tmask & TM_CONST) == 0) {
            diag_t *d = diag_new(DIAG_ERROR, vlog_loc(limit));
            diag_printf(d, "not a constant expression");
            diag_hint(d, vlog_loc(limit), "have '%s'", type_mask_str(tmask));
            diag_emit(d);
         }
      }
      break;

   default:
      break;
   }
}

static void vlog_check_specify(vlog_node_t v)
{
   const int n_stmts = vlog_stmts(v);

   for (int i = 0; i < n_stmts; i++) {
      vlog_node_t stmt = vlog_stmt(v, i);

      switch (vlog_kind(stmt)) {
      case V_TCHECK:
         vlog_check_tcheck(stmt);
         break;
      default:
         break;
      }
   }
}

static bool vlog_same_result(vlog_node_t a, vlog_node_t b)
{
   if (vlog_kind(a) != V_FUNC_DECL)
      return true;
   else if (vlog_has_type(a) != vlog_has_type(b))
      return false;
   else if (!vlog_has_type(a))
      return true;

   vlog_node_t ta = vlog_get_type(a), tb = vlog_get_type(b);
   if (ta == NULL || tb == NULL)
      return true;

   if (vlog_kind(ta) == V_CLASS_DECL && vlog_kind(tb) == V_CLASS_DECL)
      return vlog_class_derives(ta, tb);

   return get_type_mask(ta) == get_type_mask(tb)
      && vlog_size(ta) == vlog_size(tb)
      && vlog_is_signed(ta) == vlog_is_signed(tb);
}

typedef struct {
   ident_t     name;
   vlog_node_t decl;
} vlog_vis_name_t;

static void vlog_collect_names(vlog_node_t class, vlog_vis_name_t *names,
                               int *count, int max)
{
   // Names of types and parameters which are visible in an interface class
   // either because it declares them or inherits them

   const int ndecls = vlog_decls(class);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(class, i);
      if (vlog_kind(d) != V_TYPE_DECL && vlog_kind(d) != V_LOCALPARAM
          && vlog_kind(d) != V_PARAM_DECL)
         continue;
      else if (vlog_ident(d) == ident_new("$implements"))
         continue;

      bool seen = false;
      for (int j = 0; j < *count; j++)
         seen |= names[j].name == vlog_ident(d);

      if (!seen && *count < max)
         names[(*count)++] = (vlog_vis_name_t){ vlog_ident(d), d };
   }

   const unsigned nifaces = vlog_class_ifaces(class);
   for (unsigned i = 0; i < nifaces; i++)
      vlog_collect_names(vlog_class_iface(class, i), names, count, max);
}

static void vlog_check_iface_conflicts(vlog_node_t v)
{
   // A name which is inherited from more than one interface class must be
   // declared again if it is not the same declaration

   const unsigned nifaces = vlog_class_ifaces(v);
   if (nifaces < 2)
      return;

   vlog_vis_name_t own[64];
   int nown = 0;
   const int ndecls = vlog_decls(v);
   for (int i = 0; i < ndecls && nown < ARRAY_LEN(own); i++) {
      vlog_node_t d = vlog_decl(v, i);
      if (vlog_kind(d) == V_TYPE_DECL || vlog_kind(d) == V_LOCALPARAM
          || vlog_kind(d) == V_PARAM_DECL)
         own[nown++] = (vlog_vis_name_t){ vlog_ident(d), d };
   }

   for (unsigned i = 0; i < nifaces; i++) {
      vlog_vis_name_t a[64];
      int na = 0;
      vlog_collect_names(vlog_class_iface(v, i), a, &na, ARRAY_LEN(a));

      for (unsigned j = i + 1; j < nifaces; j++) {
         vlog_vis_name_t b[64];
         int nb = 0;
         vlog_collect_names(vlog_class_iface(v, j), b, &nb, ARRAY_LEN(b));

         for (int x = 0; x < na; x++) {
            for (int y = 0; y < nb; y++) {
               if (a[x].name != b[y].name || a[x].decl == b[y].decl)
                  continue;

               bool resolved = false;
               for (int z = 0; z < nown; z++)
                  resolved |= own[z].name == a[x].name;

               if (!resolved)
                  error_at(vlog_loc(v), "the name %pQ must be defined in %pQ "
                           "due to having conflicting visibility in multiple "
                           "base classes", a[x].name, vlog_ident(v));
            }
         }
      }
   }
}

static void vlog_check_iface_methods(vlog_node_t class, vlog_node_t iface)
{
   // Every method of an interface class, and of those it extends, must be
   // implemented by a class which implements it

   const int ndecls = vlog_decls(iface);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(iface, i);
      if (vlog_kind(d) != V_FUNC_DECL && vlog_kind(d) != V_TASK_DECL)
         continue;

      vlog_node_t impl = vlog_class_member(class, vlog_ident(d));
      if (impl == NULL || vlog_kind(impl) != vlog_kind(d)
          || (vlog_flags(impl) & VLOG_F_PURE)) {
         diag_t *dg = diag_new(DIAG_ERROR, vlog_loc(class));
         diag_printf(dg, "class %pQ does not implement method %pQ of "
                     "interface class %pQ", vlog_ident(class), vlog_ident(d),
                     vlog_ident(iface));
         diag_hint(dg, vlog_loc(d), "declared here");
         diag_emit(dg);
      }
      else if (vlog_ports(impl) != vlog_ports(d)) {
         diag_t *dg = diag_new(DIAG_ERROR, vlog_loc(impl));
         diag_printf(dg, "method %pQ has a different number of arguments than "
                     "in interface class %pQ", vlog_ident(d),
                     vlog_ident(iface));
         diag_hint(dg, vlog_loc(d), "declared here");
         diag_emit(dg);
      }
      else if (!vlog_same_result(impl, d)) {
         diag_t *dg = diag_new(DIAG_ERROR, vlog_loc(impl));
         diag_printf(dg, "method %pQ has a different return type than in "
                     "interface class %pQ", vlog_ident(d), vlog_ident(iface));
         diag_hint(dg, vlog_loc(d), "declared here");
         diag_emit(dg);
      }
   }

   const unsigned nifaces = vlog_class_ifaces(iface);
   for (unsigned i = 0; i < nifaces; i++)
      vlog_check_iface_methods(class, vlog_class_iface(iface, i));
}

static void vlog_find_return_cb(vlog_node_t v, void *context)
{
   if (vlog_kind(v) == V_RETURN)
      *(bool *)context = true;
}

static void vlog_check_virtual_override(vlog_node_t class, vlog_node_t d)
{
   // A method which overrides a virtual method must have the same
   // number of arguments, argument names and directions and return type

   for (vlog_node_t c = class; vlog_has_ref(c); ) {
      c = vlog_ref(c);

      const int nbase = vlog_decls(c);
      for (int i = 0; i < nbase; i++) {
         vlog_node_t b = vlog_decl(c, i);
         if (vlog_ident(b) != vlog_ident(d))
            continue;
         else if (vlog_kind(b) != V_FUNC_DECL && vlog_kind(b) != V_TASK_DECL)
            continue;
         else if (!(vlog_flags(b) & (VLOG_F_VIRTUAL | VLOG_F_PURE)))
            return;
         else if (vlog_kind(b) != vlog_kind(d)) {
            diag_t *dg = diag_new(DIAG_ERROR, vlog_loc(d));
            diag_printf(dg, "%s %pQ in class %pQ cannot override %s in "
                        "superclass %pQ",
                        vlog_kind(d) == V_TASK_DECL ? "task" : "function",
                        vlog_ident(d), vlog_ident(class),
                        vlog_kind(b) == V_TASK_DECL ? "task" : "function",
                        vlog_ident(c));
            diag_hint(dg, vlog_loc(b), "declared here");
            diag_emit(dg);
            return;
         }

         if (vlog_ports(b) != vlog_ports(d) || !vlog_same_result(d, b)) {
            diag_t *dg = diag_new(DIAG_ERROR, vlog_loc(d));
            diag_printf(dg, "virtual method %pQ has a different number of "
                        "arguments or return type than the method in class "
                        "%pQ", vlog_ident(d), vlog_ident(c));
            diag_hint(dg, vlog_loc(b), "declared here");
            diag_emit(dg);
            return;
         }

         const int nports = vlog_ports(d);
         for (int j = 0; j < nports; j++) {
            vlog_node_t pd = vlog_port(d, j), pb = vlog_port(b, j);
            if (vlog_ident(pd) != vlog_ident(pb)) {
               diag_t *dg = diag_new(DIAG_ERROR, vlog_loc(pd));
               diag_printf(dg, "argument name %pQ for virtual method %pQ in "
                           "class %pQ does not match the argument name %pQ in "
                           "superclass %pQ", vlog_ident(pd), vlog_ident(d),
                           vlog_ident(class), vlog_ident(pb), vlog_ident(c));
               diag_hint(dg, vlog_loc(pb), "declared here");
               diag_emit(dg);
            }
            else if (get_type_mask(vlog_type(pd))
                     != get_type_mask(vlog_type(pb))) {
               diag_t *dg = diag_new(DIAG_ERROR, vlog_loc(pd));
               diag_printf(dg, "type of argument %pQ of virtual method %pQ in "
                           "class %pQ does not match the type of the argument "
                           "in superclass %pQ", vlog_ident(pd), vlog_ident(d),
                           vlog_ident(class), vlog_ident(c));
               diag_hint(dg, vlog_loc(pb), "declared here");
               diag_emit(dg);
            }
            else if (vlog_subkind(pd) != vlog_subkind(pb)) {
               diag_t *dg = diag_new(DIAG_ERROR, vlog_loc(pd));
               diag_printf(dg, "direction of argument %pQ for virtual method "
                           "%pQ in class %pQ does not match the direction in "
                           "superclass %pQ", vlog_ident(pd), vlog_ident(d),
                           vlog_ident(class), vlog_ident(c));
               diag_hint(dg, vlog_loc(pb), "declared here");
               diag_emit(dg);
            }
         }

         return;
      }
   }
}

static void vlog_check_class_protos(vlog_node_t v)
{
   // Prototypes must be defined in the same scope as the class and a pure
   // constraint is implemented by a class derived from the one declaring it

   const int ndecls = vlog_decls(v);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(v, i);
      switch (vlog_kind(d)) {
      case V_FUNC_DECL:
      case V_TASK_DECL:
      case V_CONSTRUCTOR:
      case V_CONSTRAINT:
         break;
      default:
         continue;
      }

      static const char *const builtin[] = {
         "randomize", "rand_mode", "constraint_mode", "srandom",
         "get_randstate", "set_randstate",
      };

      if (vlog_kind(d) != V_CONSTRAINT) {
         for (int j = 0; j < ARRAY_LEN(builtin); j++) {
            if (vlog_ident(d) == ident_new(builtin[j]))
               error_at(vlog_loc(d), "built-in method %pQ cannot be "
                        "overridden", vlog_ident(d));
         }
      }

      if ((vlog_kind(d) == V_FUNC_DECL || vlog_kind(d) == V_TASK_DECL)
          && vlog_has_ref(v))
         vlog_check_virtual_override(v, d);

      if ((vlog_kind(d) == V_FUNC_DECL || vlog_kind(d) == V_TASK_DECL)
          && (vlog_flags(d) & VLOG_F_PURE)
          && !(vlog_flags(v) & VLOG_F_VIRTUAL))
         error_at(vlog_loc(d), "a pure virtual method can only be declared "
                  "in a virtual class");

      if (vlog_flags(d) & VLOG_F_EXTERN) {
         // The block of an implicit prototype is empty if it is not defined
         const bool implicit = vlog_kind(d) == V_CONSTRAINT
            && (vlog_flags(d) & VLOG_F_IMPLICIT);

         diag_t *dg = diag_new(implicit ? DIAG_WARN : DIAG_ERROR, vlog_loc(d));
         diag_printf(dg, "%s %pQ of class %pQ is declared %s but is "
                     "never defined", vlog_kind(d) == V_CONSTRAINT
                     ? "constraint" : "method", vlog_ident(d), vlog_ident(v),
                     implicit ? "without a block" : "extern");
         diag_hint(dg, vlog_loc(d), "declared here");
         diag_emit(dg);
      }
      else if (vlog_kind(d) == V_CONSTRAINT && (vlog_flags(d) & VLOG_F_PURE)
               && !(vlog_flags(v) & VLOG_F_VIRTUAL))
         error_at(vlog_loc(d), "a pure constraint can only be declared in a "
                  "virtual class");
   }

   if (vlog_flags(v) & VLOG_F_VIRTUAL)
      return;

   for (vlog_node_t c = v; vlog_has_ref(c); ) {
      c = vlog_ref(c);

      const int nbase = vlog_decls(c);
      for (int i = 0; i < nbase; i++) {
         vlog_node_t d = vlog_decl(c, i);
         if (vlog_kind(d) != V_CONSTRAINT || !(vlog_flags(d) & VLOG_F_PURE))
            continue;

         vlog_node_t impl = vlog_class_member(v, vlog_ident(d));
         if (impl == NULL || (vlog_flags(impl) & VLOG_F_PURE)) {
            diag_t *dg = diag_new(DIAG_ERROR, vlog_loc(v));
            diag_printf(dg, "class %pQ does not implement the pure "
                        "constraint %pQ", vlog_ident(v), vlog_ident(d));
            diag_hint(dg, vlog_loc(d), "declared here");
            diag_emit(dg);
         }
      }
   }
}

static void vlog_check_class(vlog_node_t v)
{
   vlog_check_arg_order(v);
   vlog_check_decls(v);
   vlog_check_class_protos(v);
   vlog_check_iface_conflicts(v);

   // An abstract class need not implement everything
   if (vlog_flags(v) & VLOG_F_VIRTUAL)
      return;

   for (vlog_node_t c = v;; c = vlog_ref(c)) {
      const unsigned nifaces = vlog_class_ifaces(c);
      for (unsigned i = 0; i < nifaces; i++)
         vlog_check_iface_methods(v, vlog_class_iface(c, i));

      if (!vlog_has_ref(c))
         break;
   }
}

void vlog_check(vlog_node_t v)
{
   switch (vlog_kind(v)) {
   case V_MODULE:
      vlog_check_module(v);
      break;
   case V_PACKAGE:
   case V_NAMESPACE:
      vlog_check_decls(v);
      break;
   case V_BIND:
      vlog_check_stmts(v);
      break;
   case V_CONFIG:
      break;
   case V_CLASS_DECL:
      vlog_check_class(v);
      break;
   case V_NET_DECL:
      vlog_check_net_decl(v);
      break;
   case V_VAR_DECL:
   case V_LOCAL_DECL:
      vlog_check_var_decl(v);
      break;
   case V_PORT_DECL:
   case V_TF_PORT_DECL:
      vlog_check_port_decl(v);
      break;
   case V_MODPORT:
   case V_MODPORT_PORT:
   case V_IFACE_PORT:
      break;
   case V_PARAM_DECL:
      vlog_check_param_decl(v);
      break;
   case V_LOCALPARAM:
      vlog_check_localparam(v);
      break;
   case V_GENVAR_DECL:
      vlog_check_genvar_decl(v);
      break;
   case V_TYPE_DECL:
      vlog_check_type_decl(v);
      break;
   case V_ENUM_DECL:
      vlog_check_enum_decl(v);
      break;
   case V_STRUCT_DECL:
      vlog_check_struct_decl(v);
      break;
   case V_UNION_DECL:
      vlog_check_union_decl(v);
      break;
   case V_ENUM_NAME:
      vlog_check_enum_name(v);
      break;
   case V_ALWAYS:
      vlog_check_always_blocks(v);
      vlog_check_stmts(v);
      break;
   case V_INITIAL:
   case V_FOREVER:
      vlog_check_stmts(v);
      break;
   case V_FINAL:
      in_final = true;
      vlog_check_stmts(v);
      in_final = false;
      break;
   case V_SEQ_BLOCK:
   case V_GEN_BLOCK:
   case V_PROGRAM:
      vlog_check_decls(v);
      vlog_check_stmts(v);
      break;
   case V_CONSTRUCTOR:
      {
         vlog_check_decls(v);

         const bool was_in_function = in_function;
         in_function = true;
         vlog_check_stmts(v);
         in_function = was_in_function;
      }
      break;
   case V_INST_LIST:
      vlog_check_inst_list(v);
      break;
   case V_MOD_INST:
      vlog_check_mod_inst(v);
      break;
   case V_PORT_CONN:
      vlog_check_port_conn(v);
      break;
   case V_PARAM_ASSIGN:
      vlog_check_param_assign(v);
      break;
   case V_FUNC_DECL:
      vlog_check_func_decl(v);
      break;
   case V_TASK_DECL:
      vlog_check_task_decl(v);
      break;
   case V_TIMING:
      vlog_check_timing(v);
      break;
   case V_IF:
      vlog_check_if(v);
      break;
   case V_IF_GENERATE:
      vlog_check_if_generate(v);
      break;
   case V_COND:
      vlog_check_cond(v);
      break;
   case V_CASE:
      vlog_check_case(v);
      break;
   case V_FOR_LOOP:
   case V_FOR_GENERATE:
      vlog_check_for_loop(v);
      break;
   case V_FOREACH:
      vlog_check_expr(vlog_value(v));
      vlog_check_stmts(v);
      break;
   case V_REPEAT:
      vlog_check_repeat(v);
      break;
   case V_WHILE:
   case V_DO_WHILE:
      vlog_check_while(v);
      break;
   case V_BASSIGN:
      vlog_check_bassign(v);
      break;
   case V_NBASSIGN:
      vlog_check_nbassign(v);
      break;
   case V_RETURN:
      vlog_check_return(v);
      break;
   case V_SYS_TCALL:
      vlog_check_sys_tcall(v);
      break;
   case V_USER_TCALL:
      vlog_check_user_tcall(v);
      break;
   case V_SUPER_CALL:
      vlog_check_super_call(v);
      break;
   case V_VOID_CALL:
      vlog_check_expr(vlog_value(v));
      break;
   case V_EVENT_CONTROL:
      vlog_check_event_control(v);
      break;
   case V_DELAY_CONTROL:
      vlog_check_delay_control(v);
      break;
   case V_GATE_INST:
      vlog_check_gate_inst(v);
      break;
   case V_ASSIGN:
      vlog_check_assign(v);
      break;
   case V_FORCE:
      vlog_check_force(v);
      break;
   case V_RELEASE:
      vlog_check_release(v);
      break;
   case V_OP_ASSIGN:
      vlog_check_op_assign(v);
      break;
   case V_PREFIX:
   case V_POSTFIX:
      vlog_check_prefix_postfix(v);
      break;
   case V_DEASSIGN:
      vlog_check_deassign(v);
      break;
   case V_PRIMITIVE:
      vlog_check_primitive(v);
      break;
   case V_DATA_TYPE:
      vlog_check_data_type(v);
      break;
   case V_DIMENSION:
      vlog_check_dimension(v);
      break;
   case V_WAIT:
      vlog_check_wait(v);
      break;
   case V_SPECIFY:
      vlog_check_specify(v);
      break;
   case V_IMPORT_DECL:
   case V_DPI_EXPORT:
   case V_DISABLE:
   case V_LOOP_JUMP:
      break;
   case V_ASSERT:
   case V_ASSUME:
   case V_COVER:
      vlog_check_assertion(v);
      break;
   case V_SVA_OP:
      vlog_check_sva_op(v);
      break;
   case V_SVA_DECL:
      vlog_check_sva_decl(v);
      break;
   case V_PROP_SPEC:
      vlog_check_prop_spec(v);
      break;
   case V_FORK_CTRL:
      if (in_function && vlog_subkind(v) == V_FORK_WAIT)
         error_at(vlog_loc(v), "wait fork cannot be used in a function as "
                  "functions must not block");
      break;
   case V_FORK:
      {
         const bool blocks = vlog_subkind(v) != V_FORK_JOIN_NONE;
         if (in_function && blocks)
            error_at(vlog_loc(v), "fork in a function must use join_none as "
                     "functions must not block");

         vlog_check_decls(v);

         bool found_return = false;
         vlog_visit(v, vlog_find_return_cb, &found_return);
         if (found_return)
            error_at(vlog_loc(v), "a return statement is not allowed inside "
                     "a fork block");

         // The statements of a fork run in their own processes
         const bool was_in_function = in_function;
         in_function = false;
         vlog_check_stmts(v);
         in_function = was_in_function;
      }
      break;
   case V_METHOD_CALL:
      (void)vlog_check_expr(v);
      if ((vlog_subkind(v) == V_METHOD_USER
           || vlog_subkind(v) == V_METHOD_CLASS) && vlog_has_ref(v))
         vlog_check_discarded(v, vlog_ref(v));
      break;
   case V_MEMBER_REF:
   case V_STD_RANDOMIZE:
      (void)vlog_check_expr(v);
      break;
   case V_CONSTRAINT:
      vlog_check_cons_items(v);
      break;
   case V_RANDCASE:
      vlog_check_stmts(v);
      break;
   case V_RANDCASE_ITEM:
      vlog_check_integral(vlog_value(v), "randcase weight");
      vlog_check_stmts(v);
      break;
   case V_EVENT_TRIGGER:
      {
         vlog_node_t value = vlog_value(v);
         if (vlog_check_expr(value)) {
            vlog_node_t type = vlog_get_type(value);
            if (type != NULL && vlog_kind(type) == V_DATA_TYPE
                && vlog_subkind(type) != DT_EVENT)
               error_at(vlog_loc(value), "'%s' is not an event",
                        istr(vlog_ident(v)));
         }
      }
      break;
   case V_DEFPARAM:
      vlog_check_defparam(v);
      break;
   default:
      CANNOT_HANDLE(v);
   }
}
