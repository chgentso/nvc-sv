//
//  Copyright (C) 2026 Nick Gasson
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
#include "jit/jit.h"
#include "mir/mir-node.h"
#include "mir/mir-unit.h"
#include "printf.h"
#include "vlog/vlog-node.h"
#include "vlog/vlog-number.h"
#include "vlog/vlog-phase.h"
#include "vlog/vlog-util.h"

#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>

typedef struct {
   vlog_node_t func;
   unsigned    later;
} fold_saved_t;

typedef struct {
   mir_context_t  *mir;
   jit_t          *jit;
   vlog_node_t     func;
   vlog_node_t     nonconst;
   hset_t         *funcdeps;
   unsigned        later;
   bool            retry;
   unsigned        gen_depth;
   A(fold_saved_t) stack;
} fold_ctx_t;

typedef enum {
   FOLD_CONST,
   FOLD_POSSIBLE,
   FOLD_LATER,
   FOLD_NON_CONST,
} fold_state_t;

static bool is_array_query(vlog_node_t v)
{
   if (vlog_params(v) < 1)
      return false;
   else if (vlog_subkind(v) == V_SYSTF_BITS)
      return true;
   else if (vlog_subkind(v) != V_SYSTF_NONE)
      return false;

   static const char *const names[] = {
      "$size", "$left", "$right", "$low", "$high", "$increment",
      "$dimensions", "$unpacked_dimensions",
   };

   ident_t id = vlog_ident(v);
   for (int i = 0; i < ARRAY_LEN(names); i++) {
      if (id == ident_new(names[i]))
         return true;
   }

   return false;
}

static fold_state_t get_fold_state(vlog_node_t v, fold_ctx_t *ctx);

static fold_state_t get_ranges_state(vlog_node_t v, fold_ctx_t *ctx)
{
   fold_state_t state = FOLD_CONST;
   const int nranges = vlog_ranges(v);
   for (int i = 0; i < nranges; i++) {
      vlog_node_t r = vlog_range(v, i);
      if (vlog_subkind(r) == V_DIM_UNSIZED || !vlog_has_left(r))
         continue;

      state = MAX(state, get_fold_state(vlog_left(r), ctx));
      if (vlog_has_right(r))
         state = MAX(state, get_fold_state(vlog_right(r), ctx));
   }

   return state;
}

static fold_state_t get_type_state(vlog_node_t t, fold_ctx_t *ctx)
{
   // A dimension of a type which depends on a parameter that is not known
   // until the design is elaborated makes the size of the type unknown

   fold_state_t state = FOLD_CONST;
   switch (vlog_kind(t)) {
   case V_DATA_TYPE:
      state = get_ranges_state(t, ctx);
      if (vlog_has_type(t))
         state = MAX(state, get_type_state(vlog_type(t), ctx));
      break;
   case V_TYPE_DECL:
   case V_ENUM_DECL:
      if (vlog_has_type(t))
         state = get_type_state(vlog_type(t), ctx);
      break;
   case V_STRUCT_DECL:
   case V_UNION_DECL:
      {
         const int ndecls = vlog_decls(t);
         for (int i = 0; i < ndecls; i++) {
            vlog_node_t m = vlog_decl(t, i);
            state = MAX(state, get_ranges_state(m, ctx));
            if (vlog_has_type(m))
               state = MAX(state, get_type_state(vlog_type(m), ctx));
         }
      }
      break;
   default:
      break;
   }

   return state;
}

static fold_state_t get_fold_state(vlog_node_t v, fold_ctx_t *ctx)
{
   switch (vlog_kind(v)) {
   case V_REAL:
   case V_STRING:
   case V_NUMBER:
   case V_DOLLAR:
      return FOLD_CONST;
   case V_REF:
      {
         vlog_node_t d = vlog_ref(v);
         switch (vlog_kind(d)) {
         case V_LOCALPARAM:
            return get_fold_state(vlog_value(d), ctx);
         case V_PARAM_DECL:
         case V_GENVAR_DECL:
            ctx->later++;
            return FOLD_LATER;
         case V_ENUM_NAME:
            return FOLD_CONST;
         default:
            ctx->nonconst = d;
            return FOLD_NON_CONST;
         }
      }
   case V_BIT_SELECT:
      {
         fold_state_t state = get_fold_state(vlog_value(v), ctx);

         const int nparams = vlog_params(v);
         for (int i = 0; i < nparams; i++)
            state = MAX(state, get_fold_state(vlog_param(v, i), ctx));

         return state == FOLD_CONST ? FOLD_POSSIBLE : state;
      }
   case V_HIER_REF:
      // A parameter of another instance is known after elaboration
      if (vlog_has_ref(v)) {
         ctx->nonconst = vlog_ref(v);
         return FOLD_NON_CONST;
      }

      ctx->later++;
      return FOLD_LATER;
   case V_MEMBER_REF:
      {
         fold_state_t state = get_fold_state(vlog_value(v), ctx);
         return state == FOLD_CONST ? FOLD_POSSIBLE : state;
      }
   case V_PART_SELECT:
      {
         fold_state_t value = get_fold_state(vlog_value(v), ctx);
         fold_state_t left = get_fold_state(vlog_left(v), ctx);
         fold_state_t right = get_fold_state(vlog_right(v), ctx);
         fold_state_t state = MAX(value, MAX(left, right));
         return state == FOLD_CONST ? FOLD_POSSIBLE : state;
      }
   case V_BINARY:
      {
         fold_state_t left = get_fold_state(vlog_left(v), ctx);
         fold_state_t right = get_fold_state(vlog_right(v), ctx);
         fold_state_t state = MAX(left, right);
         return state == FOLD_CONST ? FOLD_POSSIBLE : state;
      }
   case V_UNARY:
   case V_MIN_TYP_MAX:
      {
         fold_state_t state = get_fold_state(vlog_value(v), ctx);
         return state == FOLD_CONST ? FOLD_POSSIBLE : state;
      }
   case V_CAST:
      {
         fold_state_t state = get_fold_state(vlog_value(v), ctx);
         if (vlog_subkind(v) != V_CAST_TYPE
             && vlog_subkind(v) != V_CAST_TYPEREF)
            state = MAX(state, get_fold_state(vlog_left(v), ctx));
         return state == FOLD_CONST ? FOLD_POSSIBLE : state;
      }
   case V_USER_FCALL:
      {
         vlog_node_t d = vlog_ref(v);
         const vlog_flags_t flags = vlog_flags(d);
         if (!(flags & VLOG_F_CONST)) {
            ctx->nonconst = d;
            return FOLD_NON_CONST;
         }
         else if (!(flags & VLOG_F_FOLDED)) {
            hset_insert(ctx->funcdeps, d);
            ctx->later++;
            return FOLD_LATER;
         }

         fold_state_t state = FOLD_CONST;
         const int nparams = vlog_params(v);
         for (int i = 0; i < nparams; i++)
            state = MAX(state, get_fold_state(vlog_param(v, i), ctx));

         return state == FOLD_CONST ? FOLD_POSSIBLE : state;
      }
   case V_SYS_FCALL:
      {
         if (is_array_query(v)) {
            // The dimensions of a variable do not depend on its value
            vlog_node_t arg = vlog_param(v, 0);
            if (vlog_has_static_shape(arg)) {
               fold_state_t state = FOLD_POSSIBLE;
               if (vlog_kind(arg) == V_REF && vlog_has_ref(arg)) {
                  vlog_node_t d = vlog_ref(arg);
                  switch (vlog_kind(d)) {
                  case V_TYPE_DECL:
                  case V_STRUCT_DECL:
                  case V_UNION_DECL:
                     state = MAX(state, get_type_state(d, ctx));
                     break;
                  case V_VAR_DECL:
                  case V_NET_DECL:
                  case V_PORT_DECL:
                     if (vlog_has_type(d))
                        state = MAX(state, get_type_state(vlog_type(d), ctx));
                     state = MAX(state, get_ranges_state(d, ctx));
                     break;
                  default:
                     break;
                  }
               }
               return state;
            }

            ctx->nonconst = v;
            return FOLD_NON_CONST;
         }

         // See 1800-2023 section 11.2.1 for list of constant system functions
         // and 20.6.3 for $isunbounded
         switch (vlog_subkind(v)) {
         case V_SYSTF_NONE:
            if (vlog_is_sformatf(vlog_ident(v))) {
               // Formatting constant arguments is a constant expression
               // but the result, for example the hierarchical name of
               // a %m, is only known when the instance is lowered
               fold_state_t state = FOLD_CONST;
               const int nparams = vlog_params(v);
               for (int i = 0; i < nparams; i++)
                  state = MAX(state, get_fold_state(vlog_param(v, i), ctx));
               return state == FOLD_CONST ? FOLD_LATER : state;
            }
            else if (vlog_ident(v) != ident_new("$isunbounded")) {
               ctx->nonconst = v;
               return FOLD_NON_CONST;
            }
            break;
         case V_SYSTF_CLOG2:
         case V_SYSTF_SIGNED:
         case V_SYSTF_UNSIGNED:
         case V_SYSTF_RTOI:
         case V_SYSTF_SQRT:
         case V_SYSTF_CEIL:
         case V_SYSTF_FLOOR:
         case V_SYSTF_MATH:
            break;
         default:
            ctx->nonconst = v;
            return FOLD_NON_CONST;
         }

         fold_state_t state = FOLD_CONST;
         const int nparams = vlog_params(v);
         for (int i = 0; i < nparams; i++)
            state = MAX(state, get_fold_state(vlog_param(v, i), ctx));

         return state == FOLD_CONST ? FOLD_POSSIBLE : state;
      }
   case V_METHOD_CALL:
      if (vlog_ident(v) == ident_new("name")) {
         // The name of a constant enumeration value is known when the
         // instance is lowered
         fold_state_t state = get_fold_state(vlog_value(v), ctx);
         const int nparams = vlog_params(v);
         for (int i = 0; i < nparams; i++)
            state = MAX(state, get_fold_state(vlog_param(v, i), ctx));
         return state == FOLD_CONST ? FOLD_LATER : state;
      }
      ctx->nonconst = v;
      return FOLD_NON_CONST;
   case V_COND_EXPR:
      {
         fold_state_t value = get_fold_state(vlog_value(v), ctx);
         fold_state_t left = get_fold_state(vlog_left(v), ctx);
         fold_state_t right = get_fold_state(vlog_right(v), ctx);
         fold_state_t state = MAX(value, MAX(left, right));
         return state == FOLD_CONST ? FOLD_POSSIBLE : state;
      }
   case V_PATTERN_EXPR:
      {
         fold_state_t state = FOLD_CONST;
         const int nparams = vlog_params(v);
         for (int i = 0; i < nparams; i++) {
            vlog_node_t item = vlog_param(v, i);
            if (vlog_has_left(item)) {
               // The key of a structure pattern is a member
               vlog_node_t key = vlog_left(item);
               if (vlog_kind(key) != V_REF || !vlog_has_ref(key)
                   || vlog_kind(vlog_ref(key)) != V_VAR_DECL)
                  state = MAX(state, get_fold_state(key, ctx));
            }
            state = MAX(state, get_fold_state(vlog_right(item), ctx));
         }

         return state;
      }
   case V_CONCAT:
      {
         fold_state_t state = FOLD_CONST;
         const int nparams = vlog_params(v);
         for (int i = 0; i < nparams; i++)
            state = MAX(state, get_fold_state(vlog_param(v, i), ctx));

         if (vlog_has_value(v))
            state = MAX(state, get_fold_state(vlog_value(v), ctx));

         return state == FOLD_CONST ? FOLD_POSSIBLE : state;
      }
   default:
      ctx->nonconst = NULL;
      return FOLD_NON_CONST;
   }
}

static void *fold_make_node_cb(jit_scalar_t *result, void *arg)
{
   mir_unit_t *mu = arg;

   mir_type_t type = mir_get_result(mu);

   switch (mir_get_class(mu, type)) {
   case MIR_TYPE_VEC2:
      {
         const int size = mir_get_size(mu, type);
         if (size > 64)
            return NULL;   // TODO

         const jit_scalar_t zero = { .integer = 0 };
         number_t n = number_from_jit(size, mir_get_signed(mu, type),
                                      result[0], zero);

         vlog_node_t v = vlog_new(V_NUMBER);
         vlog_set_number(v, n);
         return v;
      }
   case MIR_TYPE_VEC4:
      {
         const int size = mir_get_size(mu, type);
         const bool issigned = mir_get_signed(mu, type);

         number_t n = number_from_jit(size, issigned, result[0], result[1]);

         vlog_node_t v = vlog_new(V_NUMBER);
         vlog_set_number(v, n);
         return v;
      }
   case MIR_TYPE_REAL:
      {
         vlog_node_t v = vlog_new(V_REAL);
         vlog_set_dval(v, result[0].real);
         return v;
      }
   default:
      return NULL;  // TODO
   }
}

static vlog_node_t fold_call_thunk(vlog_node_t v, fold_ctx_t *ctx)
{
   mir_unit_t *mu = vlog_lower_thunk(ctx->mir, NULL, v);

   vlog_node_t result = jit_call_thunk2(ctx->jit, mu, NULL,
                                        fold_make_node_cb, mu);

   mir_unit_free(mu);
   return result;
}

static void fold_report_non_const(vlog_node_t v, fold_ctx_t *ctx)
{
   if (ctx->nonconst == NULL)
      error_at(vlog_loc(v), "expression is not a constant");
   else {
      diag_t *d = diag_new(DIAG_ERROR, vlog_loc(v));
      diag_printf(d, "cannot ");
      switch (vlog_kind(ctx->nonconst)) {
      case V_FUNC_DECL:
         diag_printf(d, "call non-constant user function");
         break;
      case V_SYS_FCALL:
         diag_printf(d, "call system function");
         break;
      case V_VAR_DECL:
         diag_printf(d, "reference variable");
         break;
      case V_NET_DECL:
         diag_printf(d, "reference net");
         break;
      default:
         diag_printf(d, "reference");
         break;
      }
      diag_printf(d, " %pQ in constant expression",
                  vlog_ident(ctx->nonconst));

      if (vlog_kind(ctx->nonconst) != V_SYS_FCALL)
         diag_hint(d, vlog_loc(ctx->nonconst), "%pQ declared here",
                   vlog_ident(ctx->nonconst));

      diag_emit(d);
   }
}

static vlog_node_t fold_const_expr(vlog_node_t v, fold_ctx_t *ctx)
{
   switch (get_fold_state(v, ctx)) {
   case FOLD_CONST:
      return v;
   case FOLD_POSSIBLE:
      return fold_call_thunk(v, ctx);
   case FOLD_LATER:
      return NULL;
   case FOLD_NON_CONST:
      fold_report_non_const(v, ctx);
      return NULL;
   default:
      should_not_reach_here();
   }
}

static vlog_node_t fold_pattern_items(vlog_node_t v, fold_ctx_t *ctx)
{
   // An assignment pattern has no value that can be returned from a thunk
   // so fold the expression of each item and keep the pattern

   vlog_node_t copy = vlog_new(V_PATTERN_EXPR);
   vlog_set_loc(copy, vlog_loc(v));
   if (vlog_has_type(v))
      vlog_set_type(copy, vlog_type(v));

   const int nparams = vlog_params(v);
   for (int i = 0; i < nparams; i++) {
      vlog_node_t item = vlog_param(v, i);

      vlog_node_t right = vlog_right(item);
      if (vlog_kind(right) == V_PATTERN_EXPR)
         right = fold_pattern_items(right, ctx);
      else {
         vlog_node_t folded = fold_const_expr(right, ctx);
         if (folded == NULL)
            return NULL;
         right = folded;
      }

      if (right == NULL)
         return NULL;

      vlog_node_t it = vlog_new(V_PATTERN_ITEM);
      vlog_set_subkind(it, vlog_subkind(item));
      vlog_set_loc(it, vlog_loc(item));
      if (vlog_has_left(item))
         vlog_set_left(it, vlog_left(item));
      vlog_set_right(it, right);
      vlog_add_param(copy, it);
   }

   return copy;
}

static vlog_node_t fold_localparam(vlog_node_t v, fold_ctx_t *ctx)
{
   vlog_node_t value = vlog_value(v);
   switch (get_fold_state(value, ctx)) {
   case FOLD_CONST:
      break;
   case FOLD_POSSIBLE:
      if (vlog_kind(value) == V_PATTERN_EXPR && vlog_ranges(v) > 0) {
         vlog_node_t result = fold_pattern_items(value, ctx);
         if (result == NULL)
            return v;

         vlog_set_value(v, (value = result));
         break;
      }
      else {
         vlog_node_t result = fold_call_thunk(v, ctx);
         if (result == NULL)
            return v;

         vlog_set_value(v, (value = result));
      }
      break;
   case FOLD_LATER:
      return v;
   case FOLD_NON_CONST:
      fold_report_non_const(v, ctx);
      return v;
   }

   vlog_node_t vtype = vlog_type(v);
   if (!is_implicit_data_type(vtype) || vlog_ranges(vtype) > 0
       || vlog_kind(value) == V_DOLLAR)
      return v;

   vlog_node_t dt = vlog_new(V_DATA_TYPE);
   vlog_set_loc(dt, vlog_loc(v));

   int width = 32;
   bool issigned = true;
   const vlog_kind_t kind = vlog_kind(value);
   if (kind == V_REAL || vlog_is_real(value))
      vlog_set_subkind(dt, DT_REAL);
   else {
      width = vlog_width(value);
      issigned = vlog_is_signed(value);

      // The signing declared with the parameter overrides that of the value
      // (1800-2017 6.20.2)
      const vlog_flags_t sign =
         vlog_flags(vtype) & (VLOG_F_SIGNED | VLOG_F_UNSIGNED);
      if (sign != 0 && issigned != !!(sign & VLOG_F_SIGNED)) {
         issigned = !!(sign & VLOG_F_SIGNED);
         if (kind == V_NUMBER) {
            vlog_node_t n = vlog_new(V_NUMBER);
            vlog_set_number(n, number_with_sign(vlog_number(value), issigned));
            vlog_set_loc(n, vlog_loc(value));
            vlog_set_value(v, (value = n));
         }
      }

      if (width == 32 && issigned) {
         vlog_set_subkind(dt, DT_INTEGER);
         vlog_set_flags(dt, VLOG_F_SIGNED);
      }
      else {
         vlog_set_subkind(dt, DT_LOGIC);
         if (issigned)
            vlog_set_flags(dt, VLOG_F_SIGNED);
      }
   }

   if (width != 1) {
      vlog_node_t left = vlog_new(V_NUMBER);
      vlog_set_number(left, number_from_int(width - 1));

      vlog_node_t right = vlog_new(V_NUMBER);
      vlog_set_number(right, number_from_int(0));

      vlog_node_t r = vlog_new(V_DIMENSION);
      vlog_set_subkind(r, V_DIM_PACKED);
      vlog_set_left(r, left);
      vlog_set_right(r, right);

      vlog_add_range(dt, r);
   }

   vlog_set_type(v, dt);
   return v;
}

static vlog_node_t fold_dimension(vlog_node_t v, fold_ctx_t *ctx)
{
   if (vlog_subkind(v) == V_DIM_UNSIZED)
      return v;

   vlog_node_t left = fold_const_expr(vlog_left(v), ctx);
   if (left != NULL)
      vlog_set_left(v, left);

   vlog_node_t right = fold_const_expr(vlog_right(v), ctx);
   if (right != NULL)
      vlog_set_right(v, right);

   return v;
}

static vlog_node_t fold_sva_op(vlog_node_t v, fold_ctx_t *ctx)
{
   if (!vlog_has_left(v))
      return v;

   // The bound may be a formal argument of a sequence or property
   if (get_fold_state(vlog_left(v), ctx) == FOLD_NON_CONST
       || get_fold_state(vlog_right(v), ctx) == FOLD_NON_CONST)
      return v;

   vlog_node_t left = fold_const_expr(vlog_left(v), ctx);
   if (left != NULL)
      vlog_set_left(v, left);

   vlog_node_t right = fold_const_expr(vlog_right(v), ctx);
   if (right != NULL)
      vlog_set_right(v, right);

   return v;
}

static vlog_node_t fold_param_decl(vlog_node_t v, fold_ctx_t *ctx)
{
   if (vlog_has_value(v)) {
      vlog_node_t value = vlog_value(v);
      if (get_fold_state(value, ctx) == FOLD_NON_CONST)
         fold_report_non_const(value, ctx);
   }

   return v;
}

static vlog_node_t fold_enum_decl(vlog_node_t v, fold_ctx_t *ctx)
{
   const int ndecls = vlog_decls(v);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t di = vlog_decl(v, i);
      assert(vlog_has_value(di));

      vlog_node_t value = vlog_value(di);
      if (vlog_kind(value) != V_NUMBER)
         continue;

      number_t n = vlog_number(value);

      for (int j = 0; j < i; j++) {
         vlog_node_t dj = vlog_decl(v, j), cmp = vlog_value(dj);
         if (vlog_kind(cmp) != V_NUMBER)
            continue;
         else if (number_equal(vlog_number(cmp), n)) {
            error_at(vlog_loc(di), "enum names %pQ and %pQ both have "
                     "value %"PRIi64, vlog_ident(dj), vlog_ident(di),
                     number_integer(n));
            return v;
         }
      }
   }

   return v;
}

static vlog_node_t fold_part_select(vlog_node_t v, fold_ctx_t *ctx)
{
   vlog_node_t type = vlog_get_type(vlog_value(v));
   if (type != NULL && vlog_kind(type) == V_DATA_TYPE
       && vlog_subkind(type) == DT_QUEUE)
      return v;   // Queue slice bounds are evaluated at run time

   if (type != NULL && vlog_kind(type) == V_DATA_TYPE
       && vlog_subkind(type) == DT_DYNARRAY) {
      // The slice of a streamed dynamic array need not be constant
      fold_state_t state = get_fold_state(vlog_right(v), ctx);
      if (vlog_subkind(v) == V_RANGE_CONST)
         state = MAX(state, get_fold_state(vlog_left(v), ctx));
      if (state >= FOLD_LATER)
         return v;
   }

   if (vlog_subkind(v) == V_RANGE_CONST) {
      vlog_node_t left = fold_const_expr(vlog_left(v), ctx);
      if (left != NULL)
         vlog_set_left(v, left);
   }

   vlog_node_t right = fold_const_expr(vlog_right(v), ctx);
   if (right != NULL)
      vlog_set_right(v, right);

   // A generate block which is not selected may use values for which the
   // width is not valid
   int64_t width;
   if (vlog_subkind(v) != V_RANGE_CONST && ctx->gen_depth == 0
       && vlog_get_const(vlog_right(v), &width) && width <= 0)
      error_at(vlog_loc(vlog_right(v)), "width of an indexed part-select must "
               "be positive");

   return v;
}

static vlog_node_t fold_const_value(vlog_node_t v, fold_ctx_t *ctx)
{
   if (vlog_has_value(v) && !is_data_type(vlog_value(v))
       && !(vlog_kind(vlog_value(v)) == V_REF && vlog_has_ref(vlog_value(v))
            && is_data_type(vlog_ref(vlog_value(v))))) {
      vlog_node_t value = fold_const_expr(vlog_value(v), ctx);
      if (value != NULL)
         vlog_set_value(v, value);
   }

   return v;
}

static vlog_node_t fold_cast(vlog_node_t v, fold_ctx_t *ctx)
{
   if (vlog_subkind(v) != V_CAST_WIDTH)
      return v;

   vlog_node_t width = fold_const_expr(vlog_left(v), ctx);
   if (width != NULL)
      vlog_set_left(v, width);

   return v;
}

static vlog_node_t fold_if_generate(vlog_node_t v, fold_ctx_t *ctx)
{
   const int nconds = vlog_conds(v);
   for (int i = 0; i < nconds; i++) {
      vlog_node_t c = vlog_cond(v, i);
      if (vlog_has_value(c)) {
         vlog_node_t value = fold_const_expr(vlog_value(c), ctx);
         if (value == NULL)
            return v;

         vlog_set_value(c, value);

         int64_t ival;
         if (!vlog_get_const(value, &ival) || ival == 0)
            continue;
      }

      return vlog_stmts(c) == 1 ? vlog_stmt(c, 0) : NULL;
   }

   return NULL;   // None of the conditions are true
}

static vlog_node_t fold_func_decl(vlog_node_t v, fold_ctx_t *ctx)
{
   assert(ctx->func == v);

   if (ctx->later == 0) {
      vlog_set_flags(v, VLOG_F_FOLDED);
      ctx->retry |= hset_contains(ctx->funcdeps, v);
   }

   // Back to any function which this was first reached from
   if (ctx->stack.count > 0) {
      const fold_saved_t saved = APOP(ctx->stack);
      ctx->func = saved.func;
      ctx->later = saved.later;
   }
   else
      ctx->func = NULL;

   return v;
}

static vlog_node_t fold_ref(vlog_node_t v, fold_ctx_t *ctx)
{
   if (ctx->func != NULL) {
      // Do not evalulate constant functions which reference unbound or
      // unfolded parameters
      (void)get_fold_state(v, ctx);
   }

   return v;
}

static vlog_node_t fixed_real_array(vlog_node_t v, int64_t *left,
                                    int64_t *right)
{
   // A variable which is a one dimensional unpacked array of reals

   if (vlog_kind(v) != V_REF || !vlog_has_ref(v))
      return NULL;

   vlog_node_t decl = vlog_ref(v);
   if (vlog_kind(decl) != V_VAR_DECL || vlog_ranges(decl) != 1)
      return NULL;

   vlog_node_t type = vlog_get_type(decl);
   if (type == NULL || vlog_kind(type) != V_DATA_TYPE
       || (vlog_subkind(type) != DT_REAL && vlog_subkind(type) != DT_SHORTREAL
           && vlog_subkind(type) != DT_REALTIME))
      return NULL;

   return vlog_bounds(vlog_range(decl, 0), left, right) ? decl : NULL;
}

static vlog_node_t array_element(vlog_node_t ref, int64_t index)
{
   vlog_node_t sel = vlog_new(V_BIT_SELECT);
   vlog_set_value(sel, ref);
   vlog_set_loc(sel, vlog_loc(ref));

   vlog_node_t num = vlog_new(V_NUMBER);
   vlog_set_number(num, number_from_int(index));
   vlog_set_loc(num, vlog_loc(ref));
   vlog_add_param(sel, num);
   return sel;
}

static vlog_node_t fold_binary(vlog_node_t v)
{
   // Fixed size arrays of reals are equal if every element is equal

   const vlog_binary_t op = vlog_subkind(v);
   if (op != V_BINARY_LOG_EQ && op != V_BINARY_LOG_NEQ
       && op != V_BINARY_CASE_EQ && op != V_BINARY_CASE_NEQ)
      return v;

   int64_t ll, lr, rl, rr;
   if (fixed_real_array(vlog_left(v), &ll, &lr) == NULL
       || fixed_real_array(vlog_right(v), &rl, &rr) == NULL)
      return v;

   const bool ne = op == V_BINARY_LOG_NEQ || op == V_BINARY_CASE_NEQ;
   const int64_t n = llabs(lr - ll) + 1;
   if (n != llabs(rr - rl) + 1)
      return v;

   vlog_node_t chain = NULL;
   for (int64_t i = 0; i < n; i++) {
      vlog_node_t cmp = vlog_new(V_BINARY);
      vlog_set_subkind(cmp, ne ? V_BINARY_LOG_NEQ : V_BINARY_LOG_EQ);
      vlog_set_loc(cmp, vlog_loc(v));
      vlog_set_left(cmp, array_element(vlog_left(v),
                                       ll + (lr >= ll ? i : -i)));
      vlog_set_right(cmp, array_element(vlog_right(v),
                                        rl + (rr >= rl ? i : -i)));

      if (chain == NULL)
         chain = cmp;
      else {
         vlog_node_t join = vlog_new(V_BINARY);
         vlog_set_subkind(join, ne ? V_BINARY_LOG_OR : V_BINARY_LOG_AND);
         vlog_set_loc(join, vlog_loc(v));
         vlog_set_left(join, chain);
         vlog_set_right(join, cmp);
         chain = join;
      }
   }

   return chain;
}

static void vlog_fold_pre_cb(vlog_node_t v, void *context)
{
   fold_ctx_t *ctx = context;

   if (vlog_kind(v) == V_GEN_BLOCK)
      ctx->gen_depth++;

   if (vlog_kind(v) == V_FUNC_DECL) {
      if (ctx->func != NULL) {
         const fold_saved_t saved = { ctx->func, ctx->later };
         APUSH(ctx->stack, saved);
      }

      ctx->func = v;
      ctx->later = 0;
   }
}

static vlog_node_t vlog_fold_post_cb(vlog_node_t v, void *context)
{
   fold_ctx_t *fctx = context;
   if (vlog_kind(v) == V_GEN_BLOCK && fctx->gen_depth > 0)
      fctx->gen_depth--;

   switch (vlog_kind(v)) {
   case V_BINARY:
      return fold_binary(v);
   case V_LOCALPARAM:
      return fold_localparam(v, context);
   case V_PARAM_DECL:
      return fold_param_decl(v, context);
   case V_ENUM_DECL:
      return fold_enum_decl(v, context);
   case V_DIMENSION:
      return fold_dimension(v, context);
   case V_SVA_OP:
      return fold_sva_op(v, context);
   case V_PART_SELECT:
      return fold_part_select(v, context);
   case V_CONCAT:
      if (vlog_has_value(v)) {
         // The number of repeats of a string does not have to be constant
         for (int i = 0; i < vlog_params(v); i++) {
            if (vlog_kind(vlog_param(v, i)) == V_STRING)
               return v;
         }
      }
      // Fall-through
   case V_PARAM_ASSIGN:
   case V_ENUM_NAME:
   case V_DEFPARAM:
      return fold_const_value(v, context);
   case V_CAST:
      return fold_cast(v, context);
   case V_IF_GENERATE:
      return fold_if_generate(v, context);
   case V_FUNC_DECL:
      return fold_func_decl(v, context);
   case V_REF:
      return fold_ref(v, context);
   default:
      return v;
   }
}

static void fold_check_port_ranges(vlog_node_t mod)
{
   // The consistency check in the semantic pass cannot evaluate calls to
   // constant functions so compare the ranges again now they are folded

   const int nports = vlog_ports(mod);
   for (int i = 0; i < nports; i++) {
      vlog_node_t p = vlog_port(mod, i);
      if (vlog_kind(p) == V_REF && vlog_has_ref(p))
         p = vlog_ref(p);   // Non-ANSI port list

      if (vlog_kind(p) != V_PORT_DECL || !vlog_has_ref(p))
         continue;

      vlog_node_t ref = vlog_ref(p);
      vlog_node_t pt = vlog_type(p), rt = vlog_type(ref);
      if (pt == rt || vlog_ranges(pt) != vlog_ranges(rt))
         continue;

      for (int j = 0; j < vlog_ranges(pt); j++) {
         vlog_node_t pr = vlog_range(pt, j), rr = vlog_range(rt, j);
         if (vlog_subkind(pr) == V_DIM_UNSIZED
             || vlog_subkind(rr) == V_DIM_UNSIZED)
            continue;

         int64_t pl, pright, rl, rright;
         if (!vlog_get_const(vlog_left(pr), &pl)
             || !vlog_get_const(vlog_right(pr), &pright)
             || !vlog_get_const(vlog_left(rr), &rl)
             || !vlog_get_const(vlog_right(rr), &rright))
            continue;

         if (pl != rl || pright != rright) {
            diag_t *d = diag_new(DIAG_ERROR, vlog_loc(rr));
            diag_printf(d, "inconsistent dimensions for '%s'",
                        istr(vlog_ident(ref)));
            diag_hint(d, vlog_loc(p), "earlier declaration here");
            diag_emit(d);
         }
      }
   }
}

void vlog_fold(vlog_node_t mod, mir_context_t *mc, jit_t *j)
{
   fold_ctx_t ctx = {
      .mir = mc,
      .jit = j,
   };

   vlog_lower_const_funcs(mc, mod);

   const int base_errors = error_count();
   do {
      ctx.funcdeps = hset_new(4);
      ctx.retry = false;
      ctx.later = 0;
      vlog_rewrite(mod, vlog_fold_pre_cb, vlog_fold_post_cb, &ctx);
      hset_free(ctx.funcdeps);
   } while (ctx.retry && error_count() == base_errors);

   ACLEAR(ctx.stack);

   if ((vlog_kind(mod) == V_MODULE || vlog_kind(mod) == V_INST_BODY)
       && error_count() == base_errors)
      fold_check_port_ranges(mod);
}
