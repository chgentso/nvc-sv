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
#include "common.h"
#include "hash.h"
#include "ident.h"
#include "option.h"
#include "psl/psl-node.h"
#include "rt/island.h"
#include "tree.h"
#include "type.h"
#include "vhdl/vhdl-island.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef A(uint32_t) u32_list_t;
typedef A(vhdl_isig_t) sig_list_t;
typedef A(vhdl_iproc_t) proc_list_t;
typedef A(vhdl_island_t) island_list_t;

static void push_unique_u32(u32_list_t *list, uint32_t val)
{
   for (unsigned i = 0; i < list->count; i++) {
      if (list->items[i] == val)
         return;
   }
   APUSH(*list, val);
}

static bool ident_pkg_match(ident_t fn_name, const char *pkg_prefix,
                            const char *op_name)
{
   const char *s = istr(fn_name);
   const size_t plen = strlen(pkg_prefix);
   if (strncmp(s, pkg_prefix, plen) != 0)
      return false;
   const char *rest = s + plen;
   const size_t olen = strlen(op_name);
   return strncmp(rest, op_name, olen) == 0
      && (rest[olen] == '(' || rest[olen] == '\0');
}

static bool ident_has_std_pkg_fn(tree_t decl, const char **pkg_out,
                                 const char **fn_out)
{
   ident_t m = tree_ident2(decl);
   const char *s = istr(m);
   static const char *const pkgs[] = {
      "IEEE.STD_LOGIC_1164.",
      "IEEE.NUMERIC_STD.",
      "IEEE.NUMERIC_STD_UNSIGNED.",
      "IEEE.STD_LOGIC_UNSIGNED.",
      "IEEE.STD_LOGIC_SIGNED.",
      "IEEE.STD_LOGIC_ARITH.",
   };
   for (size_t i = 0; i < ARRAY_LEN(pkgs); i++) {
      const size_t plen = strlen(pkgs[i]);
      if (strncmp(s, pkgs[i], plen) == 0) {
         *pkg_out = pkgs[i];
         *fn_out = s + plen;
         return true;
      }
   }
   return false;
}

static bool fn_name_is(const char *rest, const char *target)
{
   const size_t len = strlen(target);
   return strncmp(rest, target, len) == 0
      && (rest[len] == '(' || rest[len] == '[' || rest[len] == '\0');
}

static bool eval_const_int_depth(tree_t block, tree_t expr, int64_t *out,
                                 int depth)
{
   if (expr == NULL || depth > 16)
      return false;

   int64_t fval;
   if (folded_int(expr, &fval)) {
      *out = fval;
      return true;
   }

   switch (tree_kind(expr)) {
   case T_LITERAL:
      if (tree_subkind(expr) == L_INT) {
         *out = tree_ival(expr);
         return true;
      }
      return false;

   case T_REF:
      {
         tree_t decl = tree_ref(expr);
         if (decl == NULL)
            return false;
         switch (tree_kind(decl)) {
         case T_ENUM_LIT:
            *out = tree_pos(decl);
            return true;
         case T_CONST_DECL:
            if (tree_has_value(decl))
               return eval_const_int_depth(block, tree_value(decl),
                                           out, depth + 1);
            return false;
         case T_GENERIC_DECL:
            if (block != NULL) {
               const int ng = tree_generics(block);
               const int np = tree_params(block);
               for (int i = 0; i < ng && i < np; i++) {
                  if (tree_generic(block, i) == decl)
                     return eval_const_int_depth(block,
                                                 tree_value(tree_param(block, i)),
                                                 out, depth + 1);
               }
            }
            if (tree_has_value(decl))
               return eval_const_int_depth(block, tree_value(decl),
                                           out, depth + 1);
            return false;
         default:
            return false;
         }
      }

   case T_QUALIFIED:
   case T_TYPE_CONV:
      return eval_const_int_depth(block, tree_value(expr), out, depth + 1);

   case T_ATTR_REF:
      {
         const attr_kind_t ak = tree_subkind(expr);
         type_t t = tree_type(tree_name(expr));
         if (t != NULL && type_is_array(t) && !type_is_unconstrained(t)
             && dimension_of(t) >= 1) {
            int64_t dim = 1;
            if (tree_params(expr) > 0) {
               if (tree_params(expr) != 1
                   || !eval_const_int_depth(block,
                                            tree_value(tree_param(expr, 0)),
                                            &dim, depth + 1))
                  return false;
            }
            if (dim < 1 || dim > dimension_of(t))
               return false;
            tree_t rg = range_of(t, (unsigned)(dim - 1));
            if (rg == NULL || tree_kind(rg) != T_RANGE)
               return false;
            const range_kind_t dir = tree_subkind(rg);
            if (dir != RANGE_TO && dir != RANGE_DOWNTO)
               return false;
            int64_t l, r, len;
            if (!eval_const_int_depth(block, tree_left(rg), &l, depth + 1)
                || !eval_const_int_depth(block, tree_right(rg), &r, depth + 1))
               return false;
            if (dir == RANGE_TO)
               len = (r >= l) ? (r - l + 1) : 0;
            else
               len = (l >= r) ? (l - r + 1) : 0;
            switch (ak) {
            case ATTR_LENGTH: *out = len; return true;
            case ATTR_LEFT:   *out = l; return true;
            case ATTR_RIGHT:  *out = r; return true;
            case ATTR_LOW:    *out = (dir == RANGE_TO) ? l : r; return true;
            case ATTR_HIGH:   *out = (dir == RANGE_TO) ? r : l; return true;
            default: break;
            }
         }
         return false;
      }

   case T_FCALL:
      {
         tree_t decl = tree_ref(expr);
         const subprogram_kind_t sk = tree_subkind(decl);
         if (tree_params(expr) == 1) {
            int64_t a;
            if (!eval_const_int_depth(block, tree_value(tree_param(expr, 0)),
                                        &a, depth + 1))
               return false;
            switch (sk) {
            case S_IDENTITY: *out = a; return true;
            case S_NEGATE:   *out = (a != INT64_MIN) ? -a : 0; return a != INT64_MIN;
            case S_ABS:      *out = (a < 0) ? -a : a; return a != INT64_MIN;
            default: return false;
            }
         }
         else if (tree_params(expr) == 2) {
            int64_t a, b;
            if (!eval_const_int_depth(block, tree_value(tree_param(expr, 0)),
                                        &a, depth + 1)
                || !eval_const_int_depth(block, tree_value(tree_param(expr, 1)),
                                          &b, depth + 1))
               return false;
            switch (sk) {
            case S_ADD: *out = a + b; return true;
            case S_SUB: *out = a - b; return true;
            case S_MUL: *out = a * b; return true;
            case S_DIV: if (b == 0) return false; *out = a / b; return true;
            case S_REM: if (b == 0) return false; *out = a % b; return true;
            case S_MOD:
               if (b == 0) return false;
               {
                  int64_t r = a % b;
                  if (r != 0 && ((r < 0) != (b < 0)))
                     r += b;
                  *out = r;
               }
               return true;
            default: return false;
            }
         }
         return false;
      }

   default:
      return false;
   }
}

bool vhdl_island_eval_const_int(tree_t block, tree_t expr, int64_t *out)
{
   return eval_const_int_depth(block, expr, out, 0);
}

static bool eval_array_bounds(tree_t block, type_t t, int dim,
                              range_kind_t *dir_out, int64_t *left_out,
                              int64_t *right_out, uint32_t *len_out)
{
   if (type_is_unconstrained(t))
      return false;
   if (dim >= dimension_of(t))
      return false;

   tree_t r = range_of(t, dim);
   if (r == NULL || tree_kind(r) != T_RANGE)
      return false;

   const range_kind_t rk = tree_subkind(r);
   if (rk != RANGE_TO && rk != RANGE_DOWNTO)
      return false;

   int64_t l, rr;
   if (!eval_const_int_depth(block, tree_left(r), &l, 0)
       || !eval_const_int_depth(block, tree_right(r), &rr, 0))
      return false;

   int64_t len = (rk == RANGE_TO) ? (rr - l + 1) : (l - rr + 1);
   if (len <= 0 || len > VHDL_ISLAND_MAX_ELEMS)
      return false;

   *dir_out = rk;
   *left_out = l;
   *right_out = rr;
   *len_out = (uint32_t)len;
   return true;
}

static bool type_has_disallowed_resolution(type_t t)
{
   for (type_t cur = t; cur != NULL; ) {
      if (type_kind(cur) == T_SUBTYPE && type_has_resolution(cur)) {
         tree_t r = type_resolution(cur);
         if (tree_kind(r) == T_REF) {
            tree_t fn = tree_ref(r);
            if (!ident_pkg_match(tree_ident2(fn), "IEEE.STD_LOGIC_1164.",
                                 "RESOLVED"))
               return true;
         }
         else if (tree_kind(r) == T_ELEM_RESOLUTION) {
            for (int i = 0; i < tree_assocs(r); i++) {
               tree_t val = tree_value(tree_assoc(r, i));
               if (tree_kind(val) != T_REF)
                  return true;
               tree_t fn = tree_ref(val);
               if (!ident_pkg_match(tree_ident2(fn), "IEEE.STD_LOGIC_1164.",
                                    "RESOLVED"))
                  return true;
            }
         }
         else
            return true;
      }
      if (type_kind(cur) == T_SUBTYPE)
         cur = type_base(cur);
      else
         break;
   }
   return false;
}

static bool classify_scalar_elem_ex(type_t scalar, vhdl_isig_elem_t *kind_out,
                                    uint8_t *bytes_out, bool *signed_out)
{
   if (type_has_disallowed_resolution(scalar))
      return false;

   type_t base = type_base_recur(scalar);
   well_known_t wk = is_well_known(type_ident(base));
   switch (wk) {
   case W_IEEE_ULOGIC:
   case W_IEEE_LOGIC:
      *kind_out = VISIG_STD_LOGIC;
      if (bytes_out != NULL) *bytes_out = 1;
      if (signed_out != NULL) *signed_out = false;
      return true;
   case W_STD_BIT:
      *kind_out = VISIG_BIT;
      if (bytes_out != NULL) *bytes_out = 1;
      if (signed_out != NULL) *signed_out = false;
      return true;
   case W_STD_BOOL:
      *kind_out = VISIG_BOOL;
      if (bytes_out != NULL) *bytes_out = 1;
      if (signed_out != NULL) *signed_out = false;
      return true;
   default:
      break;
   }

   if (type_kind(base) == T_ENUM) {
      const unsigned nlits = type_enum_literals(base);
      if (nlits == 0)
         return false;
      const unsigned bits = bits_for_range(0, (int64_t)nlits - 1);
      uint8_t bsz = (bits <= 8) ? 1 : (uint8_t)(bits / 8);
      if (bsz != 1 && bsz != 2 && bsz != 4 && bsz != 8)
         return false;
      *kind_out = VISIG_ENUM;
      if (bytes_out != NULL) *bytes_out = bsz;
      if (signed_out != NULL) *signed_out = false;
      return true;
   }

   if (type_kind(base) == T_INTEGER) {
      int64_t low = INT64_MIN, high = INT64_MAX;
      uint8_t bsz = 8;
      bool sgn = true;
      if (type_dims(base) > 0 && folded_bounds(type_dim(base, 0), &low, &high)) {
         const unsigned bits = bits_for_range(low, high);
         if (bits > 0) {
            bsz = (bits <= 8) ? 1 : (uint8_t)(bits / 8);
            sgn = (low < 0);
         }
      }
      if (bsz != 1 && bsz != 2 && bsz != 4 && bsz != 8)
         return false;
      *kind_out = VISIG_INT;
      if (bytes_out != NULL) *bytes_out = bsz;
      if (signed_out != NULL) *signed_out = sgn;
      return true;
   }

   return false;
}

static bool classify_scalar_elem(type_t scalar, vhdl_isig_elem_t *kind_out)
{
   return classify_scalar_elem_ex(scalar, kind_out, NULL, NULL);
}

static bool classify_vector_type(tree_t block, type_t t,
                                 vhdl_isig_elem_t *elem_out, bool *signed_out,
                                 range_kind_t *dir_out, int64_t *left_out,
                                 int64_t *right_out, uint32_t *width_out)
{
   if (!type_is_array(t) || dimension_of(t) != 1)
      return false;
   if (type_has_disallowed_resolution(t))
      return false;

   type_t elem = type_elem(t);
   if (type_is_array(elem) || type_is_record(elem))
      return false;
   if (!classify_scalar_elem(elem, elem_out))
      return false;
   if (*elem_out != VISIG_STD_LOGIC && *elem_out != VISIG_BIT
       && *elem_out != VISIG_BOOL)
      return false;

   type_t base = type_base_recur(t);
   const char *bname = istr(type_ident(base));
   *signed_out = (strcmp(bname, "IEEE.NUMERIC_STD.SIGNED") == 0
                  || strcmp(bname, "IEEE.NUMERIC_STD.UNRESOLVED_SIGNED") == 0);

   uint32_t len = 0;
   if (!eval_array_bounds(block, t, 0, dir_out, left_out, right_out, &len))
      return false;
   if (len == 0 || len > VHDL_ISLAND_MAX_WIDTH)
      return false;

   *width_out = len;
   return true;
}

static void collect_decl_leaves(tree_t block, tree_t decl, type_t type,
                                int *path, uint8_t depth, bool two_state,
                                sig_list_t *sigs)
{
   if (type_has_disallowed_resolution(type))
      return;

   if (type_is_record(type)) {
      if (depth >= VHDL_ISIG_MAX_DEPTH)
         return;
      type_t base = type_base_recur(type);
      const int nfields = type_fields(base);
      for (int i = 0; i < nfields; i++) {
         tree_t f = type_field(base, i);
         path[depth] = i;
         collect_decl_leaves(block, decl, tree_type(f), path,
                             (uint8_t)(depth + 1), two_state, sigs);
      }
      return;
   }

   vhdl_isig_t s = {
      .id           = sigs->count,
      .decl         = decl,
      .type         = type,
      .path_len     = depth,
      .byte_size    = 1,
      .is_port      = (tree_kind(decl) == T_PORT_DECL),
      .port_mode    = (tree_kind(decl) == T_PORT_DECL)
         ? (port_mode_t)tree_subkind(decl) : PORT_INVALID,
      .two_state    = two_state,
      .driver_proc  = -1,
   };
   if (depth > 0)
      memcpy(s.field_path, path, depth * sizeof(int));

   if (s.is_port && s.port_mode != PORT_IN && s.port_mode != PORT_OUT
       && s.port_mode != PORT_BUFFER)
      return;

   if (!type_is_array(type)) {
      if (!classify_scalar_elem_ex(type, &s.elem_kind, &s.byte_size, &s.is_signed))
         return;
      if (s.elem_kind == VISIG_ENUM || s.elem_kind == VISIG_INT)
         s.two_state = false;
      s.width = 1;
      s.nelems = 1;
      APUSH(*sigs, s);
      return;
   }

   if (dimension_of(type) != 1)
      return;

   type_t elem = type_elem(type);
   if (!type_is_array(elem) && !type_is_record(elem)) {
      if (!classify_vector_type(block, type, &s.elem_kind, &s.is_signed,
                                &s.dir, &s.left, &s.right, &s.width))
         return;
      s.is_vector = true;
      s.nelems = 1;
      APUSH(*sigs, s);
      return;
   }

   if (type_is_array(elem) && !type_is_record(elem) && dimension_of(elem) == 1) {
      if (!classify_vector_type(block, elem, &s.elem_kind, &s.is_signed,
                                &s.dir, &s.left, &s.right, &s.width))
         return;
      if (!eval_array_bounds(block, type, 0, &s.outer_dir, &s.outer_left,
                             &s.outer_right, &s.nelems))
         return;
      if ((uint64_t)s.width * s.nelems > VHDL_ISLAND_MAX_TOTAL_BITS)
         return;
      s.is_vector = true;
      s.is_array2d = true;
      APUSH(*sigs, s);
   }
}

bool vhdl_island_extract_path(tree_t expr, tree_t *decl_out,
                              int *path_out, uint8_t *len_out)
{
   int rev_path[VHDL_ISIG_MAX_DEPTH];
   uint8_t n = 0;
   tree_t cur = expr;

   while (cur != NULL) {
      switch (tree_kind(cur)) {
      case T_RECORD_REF:
         {
            if (n >= VHDL_ISIG_MAX_DEPTH)
               return false;
            tree_t field = tree_ref(cur);
            if (tree_kind(field) != T_FIELD_DECL)
               return false;
            rev_path[n++] = tree_pos(field);
            cur = tree_value(cur);
         }
         break;
      case T_REF:
         {
            tree_t decl = tree_ref(cur);
            if (decl == NULL
                || (tree_kind(decl) != T_PORT_DECL
                    && tree_kind(decl) != T_SIGNAL_DECL))
               return false;
            *decl_out = decl;
            for (uint8_t i = 0; i < n; i++)
               path_out[i] = rev_path[n - 1 - i];
            *len_out = n;
            return true;
         }
      default:
         return false;
      }
   }
   return false;
}

int vhdl_island_lookup_leaf(const vhdl_island_ctx_t *ctx, tree_t decl,
                            const int *path, uint8_t path_len)
{
   for (uint32_t i = 0; i < ctx->n_sigs; i++) {
      const vhdl_isig_t *s = &(ctx->sigs[i]);
      if (s->ineligible || s->decl != decl || s->path_len != path_len)
         continue;
      if (path_len == 0 || memcmp(s->field_path, path, path_len * sizeof(int)) == 0)
         return (int)i;
   }
   return -1;
}

static bool collect_matching_prefix_leaves(const vhdl_island_ctx_t *ctx,
                                           tree_t decl, const int *path,
                                           uint8_t path_len,
                                           u32_list_t *out)
{
   bool found = false;
   for (uint32_t i = 0; i < ctx->n_sigs; i++) {
      const vhdl_isig_t *s = &(ctx->sigs[i]);
      if (s->decl != decl || s->path_len < path_len)
         continue;
      if (path_len > 0 && memcmp(s->field_path, path, path_len * sizeof(int)) != 0)
         continue;
      if (s->ineligible)
         return false;
      push_unique_u32(out, i);
      found = true;
   }
   return found;
}

static bool type_all_leaves_eligible(tree_t block, type_t type, uint8_t depth)
{
   if (type_has_disallowed_resolution(type))
      return false;

   if (type_is_record(type)) {
      if (depth >= VHDL_ISIG_MAX_DEPTH)
         return false;
      type_t base = type_base_recur(type);
      const int nfields = type_fields(base);
      if (nfields <= 0)
         return false;
      for (int i = 0; i < nfields; i++) {
         tree_t f = type_field(base, i);
         if (!type_all_leaves_eligible(block, tree_type(f),
                                       (uint8_t)(depth + 1)))
            return false;
      }
      return true;
   }

   vhdl_isig_elem_t ek;
   if (!type_is_array(type))
      return classify_scalar_elem(type, &ek);

   if (dimension_of(type) != 1)
      return false;

   type_t elem = type_elem(type);
   bool is_signed = false;
   range_kind_t dir = RANGE_TO;
   int64_t left = 0, right = 0;
   uint32_t width = 0, nelems = 0;
   if (!type_is_array(elem) && !type_is_record(elem))
      return classify_vector_type(block, type, &ek, &is_signed,
                                  &dir, &left, &right, &width);

   if (type_is_array(elem) && !type_is_record(elem) && dimension_of(elem) == 1) {
      if (!classify_vector_type(block, elem, &ek, &is_signed,
                                &dir, &left, &right, &width))
         return false;
      if (!eval_array_bounds(block, type, 0, &dir, &left, &right, &nelems))
         return false;
      return (uint64_t)width * nelems <= VHDL_ISLAND_MAX_TOTAL_BITS;
   }

   return false;
}

bool vhdl_island_collect_expr_leaves(const vhdl_island_ctx_t *ctx, tree_t expr,
                                     uint32_t **out_ids, uint32_t *out_n)
{
   tree_t cur = expr;
   while (cur != NULL && (tree_kind(cur) == T_ARRAY_REF
                          || tree_kind(cur) == T_ARRAY_SLICE
                          || tree_kind(cur) == T_QUALIFIED
                          || tree_kind(cur) == T_TYPE_CONV))
      cur = tree_value(cur);

   tree_t decl = NULL;
   int path[VHDL_ISIG_MAX_DEPTH];
   uint8_t len = 0;
   if (!vhdl_island_extract_path(cur, &decl, path, &len))
      return false;
   if (type_is_record(tree_type(cur))
       && !type_all_leaves_eligible(ctx->block, tree_type(cur), len))
      return false;

   u32_list_t tmp = AINIT;
   for (uint32_t i = 0; i < *out_n; i++)
      APUSH(tmp, (*out_ids)[i]);
   free(*out_ids);

   const bool ok = collect_matching_prefix_leaves(ctx, decl, path, len, &tmp);
   *out_ids = tmp.items;
   *out_n = tmp.count;
   return ok;
}

static tree_t unwrap_signal_base(tree_t expr)
{
   tree_t cur = expr;
   while (cur != NULL) {
      switch (tree_kind(cur)) {
      case T_ARRAY_REF:
      case T_ARRAY_SLICE:
      case T_QUALIFIED:
      case T_TYPE_CONV:
         cur = tree_value(cur);
         break;
      default:
         return cur;
      }
   }
   return NULL;
}

static bool has_ineligible_signal_ref(const vhdl_island_ctx_t *ctx, tree_t expr)
{
   tree_t base = unwrap_signal_base(expr);
   tree_t decl = NULL;
   int path[VHDL_ISIG_MAX_DEPTH];
   uint8_t len = 0;
   if (!vhdl_island_extract_path(base, &decl, path, &len))
      return true;
   if (type_is_record(tree_type(base))
       && !type_all_leaves_eligible(ctx->block, tree_type(base), len))
      return true;
   u32_list_t tmp = AINIT;
   bool ok = collect_matching_prefix_leaves(ctx, decl, path, len, &tmp);
   ACLEAR(tmp);
   return !ok;
}

bool vhdl_island_is_sig_root(tree_t expr)
{
   tree_t base = unwrap_signal_base(expr);
   if (base == NULL)
      return false;
   while (tree_kind(base) == T_RECORD_REF)
      base = tree_value(base);
   if (tree_kind(base) != T_REF)
      return false;
   tree_t decl = tree_ref(base);
   return decl != NULL
      && (tree_kind(decl) == T_PORT_DECL || tree_kind(decl) == T_SIGNAL_DECL);
}

static bool enum_lit_disallowed_in_2state(tree_t lit)
{
   type_t base = type_base_recur(tree_type(lit));
   if (is_well_known(type_ident(base)) != W_IEEE_ULOGIC)
      return false;
   // 0='U',1='X',2='0',3='1',4='Z',5='W',6='L',7='H',8='-'
   const unsigned pos = tree_pos(lit);
   return pos == 4 || pos == 5 || pos == 6 || pos == 7 || pos == 8;
}

typedef struct {
   const vhdl_island_ctx_t *ctx;
   bool                     two_state;
   bool                     allow_signals;
   bool                     in_func;
   bool                     allow_sig_attr_event;
   int                      call_depth;
   u32_list_t               reads;
   u32_list_t               writes;
   A(vhdl_write_range_t)    write_ranges;
   A(tree_t)                var_decls;
   A(bool)                  var_assigned;
   A(tree_t)                loop_vars;
   A(tree_t)                active_funcs;
   tree_t                   clk_sig_ref;
   bool                     rejected;
} check_ctx_t;

static bool check_expr(check_ctx_t *c, tree_t expr, bool is_target);
static bool check_stmt_list(check_ctx_t *c, tree_t owner, int start, int end,
                            bool is_func);

static bool is_supported_std_fn(const char *pkg, const char *fn, bool two_state)
{
   if (strcmp(pkg, "IEEE.STD_LOGIC_1164.") == 0) {
      if (two_state && (fn_name_is(fn, "IS_X") || fn_name_is(fn, "STD_MATCH")))
         return false;
      return fn_name_is(fn, "\"and\"") || fn_name_is(fn, "\"or\"")
         || fn_name_is(fn, "\"nand\"") || fn_name_is(fn, "\"nor\"")
         || fn_name_is(fn, "\"xor\"") || fn_name_is(fn, "\"xnor\"")
         || fn_name_is(fn, "\"not\"")
         || fn_name_is(fn, "TO_STDLOGICVECTOR")
         || fn_name_is(fn, "TO_STDULOGICVECTOR")
         || fn_name_is(fn, "TO_BITVECTOR")
         || fn_name_is(fn, "TO_BIT")
         || fn_name_is(fn, "TO_X01")
         || fn_name_is(fn, "TO_01");
   }
   if (strcmp(pkg, "IEEE.NUMERIC_STD.") == 0
       || strcmp(pkg, "IEEE.NUMERIC_STD_UNSIGNED.") == 0
       || strcmp(pkg, "IEEE.STD_LOGIC_UNSIGNED.") == 0
       || strcmp(pkg, "IEEE.STD_LOGIC_SIGNED.") == 0
       || strcmp(pkg, "IEEE.STD_LOGIC_ARITH.") == 0) {
      if (two_state && (fn_name_is(fn, "STD_MATCH") || fn_name_is(fn, "IS_X")))
         return false;
      return fn_name_is(fn, "\"+\"") || fn_name_is(fn, "\"-\"")
         || fn_name_is(fn, "\"*\"")
         || fn_name_is(fn, "\"=\"") || fn_name_is(fn, "\"/=\"")
         || fn_name_is(fn, "\"<\"") || fn_name_is(fn, "\"<=\"")
         || fn_name_is(fn, "\">\"") || fn_name_is(fn, "\">=\"")
         || fn_name_is(fn, "\"and\"") || fn_name_is(fn, "\"or\"")
         || fn_name_is(fn, "\"nand\"") || fn_name_is(fn, "\"nor\"")
         || fn_name_is(fn, "\"xor\"") || fn_name_is(fn, "\"xnor\"")
         || fn_name_is(fn, "\"not\"")
         || fn_name_is(fn, "\"sll\"") || fn_name_is(fn, "\"srl\"")
         || fn_name_is(fn, "\"sla\"") || fn_name_is(fn, "\"sra\"")
         || fn_name_is(fn, "\"rol\"") || fn_name_is(fn, "\"ror\"")
         || fn_name_is(fn, "SHIFT_LEFT") || fn_name_is(fn, "SHIFT_RIGHT")
         || fn_name_is(fn, "ROTATE_LEFT") || fn_name_is(fn, "ROTATE_RIGHT")
         || fn_name_is(fn, "RESIZE")
         || fn_name_is(fn, "TO_UNSIGNED") || fn_name_is(fn, "TO_SIGNED")
         || fn_name_is(fn, "TO_INTEGER")
         || fn_name_is(fn, "CONV_INTEGER") || fn_name_is(fn, "CONV_STD_LOGIC_VECTOR")
         || fn_name_is(fn, "CONV_UNSIGNED") || fn_name_is(fn, "CONV_SIGNED")
         || fn_name_is(fn, "UNSIGNED") || fn_name_is(fn, "SIGNED")
         || fn_name_is(fn, "TO_01") || fn_name_is(fn, "MINIMUM")
         || fn_name_is(fn, "MAXIMUM");
   }
   return false;
}

static bool type_ok_for_local(type_t t)
{
   if (type_is_record(t)) {
      type_t base = type_base_recur(t);
      const int nf = type_fields(base);
      for (int i = 0; i < nf; i++) {
         if (!type_ok_for_local(tree_type(type_field(base, i))))
            return false;
      }
      return true;
   }
   if (!type_is_array(t)) {
      if (type_is_integer(t) || type_is_enum(t))
         return true;
      return false;
   }
   if (dimension_of(t) != 1)
      return false;
   type_t elem = type_elem(t);
   if (type_is_array(elem))
      return !type_is_array(type_elem(elem)) && !type_is_record(type_elem(elem))
         && (type_is_enum(type_elem(elem)) || type_is_integer(type_elem(elem)));
   return type_is_enum(elem) || type_is_integer(elem);
}

static bool check_pure_func_body(check_ctx_t *outer, tree_t func_decl)
{
   if (outer->call_depth >= VHDL_ISLAND_MAX_CALL_DEPTH)
      return false;
   if (tree_kind(func_decl) != T_FUNC_BODY)
      return false;
   if (tree_flags(func_decl) & TREE_F_IMPURE)
      return false;

   for (unsigned i = 0; i < outer->active_funcs.count; i++) {
      if (outer->active_funcs.items[i] == func_decl)
         return false;
   }

   for (int i = 0; i < tree_ports(func_decl); i++) {
      tree_t p = tree_port(func_decl, i);
      if (tree_class(p) == C_SIGNAL || tree_class(p) == C_FILE)
         return false;
   }

   for (int i = 0; i < tree_decls(func_decl); i++) {
      tree_t d = tree_decl(func_decl, i);
      switch (tree_kind(d)) {
      case T_VAR_DECL:
         if (tree_flags(d) & TREE_F_SHARED)
            return false;
         if (!type_ok_for_local(tree_type(d)))
            return false;
         if (tree_has_value(d) && !check_expr(outer, tree_value(d), false))
            return false;
         break;
      case T_CONST_DECL:
         if (!type_ok_for_local(tree_type(d)))
            return false;
         if (tree_has_value(d) && !check_expr(outer, tree_value(d), false))
            return false;
         break;
      case T_TYPE_DECL:
      case T_SUBTYPE_DECL:
         break;
      default:
         return false;
      }
   }

   check_ctx_t fc = {
      .ctx           = outer->ctx,
      .two_state     = outer->two_state,
      .allow_signals = false,
      .in_func       = true,
      .call_depth    = outer->call_depth + 1,
      .active_funcs  = outer->active_funcs,
   };
   APUSH(fc.active_funcs, func_decl);

   const bool ok = check_stmt_list(&fc, func_decl, 0,
                                   tree_stmts(func_decl), true);
   ACLEAR(fc.reads);
   ACLEAR(fc.writes);
   ACLEAR(fc.var_decls);
   ACLEAR(fc.var_assigned);
   ACLEAR(fc.loop_vars);
   // fc.active_funcs aliases outer->active_funcs only on items pushed? Wait: APUSH reallocates!
   // So don't share buffer blindly; clean if reallocated or keep separate copy.
   if (fc.active_funcs.items != outer->active_funcs.items)
      free(fc.active_funcs.items);
   return ok;
}

static bool is_sig_root_expr(tree_t expr)
{
   tree_t base = unwrap_signal_base(expr);
   if (base == NULL)
      return false;
   while (tree_kind(base) == T_RECORD_REF)
      base = tree_value(base);
   if (tree_kind(base) != T_REF)
      return false;
   tree_t decl = tree_ref(base);
   return decl != NULL
      && (tree_kind(decl) == T_PORT_DECL || tree_kind(decl) == T_SIGNAL_DECL);
}

static int find_var_idx(check_ctx_t *c, tree_t decl)
{
   for (unsigned i = 0; i < c->var_decls.count; i++) {
      if (c->var_decls.items[i] == decl)
         return (int)i;
   }
   return -1;
}

static bool check_expr(check_ctx_t *c, tree_t expr, bool is_target)
{
   if (expr == NULL || c->rejected)
      return false;

   switch (tree_kind(expr)) {
   case T_LITERAL:
      switch (tree_subkind(expr)) {
      case L_INT:
         return true;
      default:
         return false;
      }

   case T_STRING:
      {
         const int n = tree_chars(expr);
         for (int i = 0; i < n; i++) {
            tree_t ch = tree_char(expr, i);
            if (tree_kind(ch) != T_REF || tree_kind(tree_ref(ch)) != T_ENUM_LIT)
               return false;
            if (c->two_state && enum_lit_disallowed_in_2state(tree_ref(ch)))
               return false;
         }
         return true;
      }

   case T_REF:
      {
         tree_t decl = tree_ref(expr);
         if (decl == NULL)
            return false;
         switch (tree_kind(decl)) {
         case T_ENUM_LIT:
            if (c->two_state && enum_lit_disallowed_in_2state(decl))
               return false;
            return true;
         case T_PORT_DECL:
         case T_SIGNAL_DECL:
            {
               if (!c->allow_signals)
                  return false;
               u32_list_t *dst = is_target ? &c->writes : &c->reads;
               uint32_t *ids = NULL;
               uint32_t n = 0;
               if (!vhdl_island_collect_expr_leaves(c->ctx, expr, &ids, &n)) {
                  free(ids);
                  return false;
               }
               for (uint32_t i = 0; i < n; i++)
                  push_unique_u32(dst, ids[i]);
               free(ids);
               return true;
            }
         case T_VAR_DECL:
            {
               if (tree_flags(decl) & TREE_F_SHARED)
                  return false;
               if (!c->in_func) {
                  const int vi = find_var_idx(c, decl);
                  if (vi < 0)
                     return false;
                  if (!is_target && !c->var_assigned.items[vi])
                     return false;
               }
               return true;
            }
         case T_PARAM_DECL:
         case T_GENERIC_DECL:
         case T_CONST_DECL:
            return true;
         default:
            return false;
         }
      }

   case T_RECORD_REF:
      if (is_sig_root_expr(expr)) {
         if (!c->allow_signals)
            return false;
         u32_list_t *dst = is_target ? &c->writes : &c->reads;
         uint32_t *ids = NULL;
         uint32_t n = 0;
         if (!vhdl_island_collect_expr_leaves(c->ctx, expr, &ids, &n)) {
            free(ids);
            return false;
         }
         for (uint32_t i = 0; i < n; i++)
            push_unique_u32(dst, ids[i]);
         free(ids);
         return true;
      }
      return check_expr(c, tree_value(expr), is_target);

   case T_ARRAY_REF:
      {
         for (int i = 0; i < tree_params(expr); i++) {
            if (!check_expr(c, tree_value(tree_param(expr, i)), false))
               return false;
         }
         return check_expr(c, tree_value(expr), is_target);
      }

   case T_ARRAY_SLICE:
      {
         tree_t rg = tree_range(expr, 0);
         int64_t l, r;
         if (!vhdl_island_eval_const_int(c->ctx->block, tree_left(rg), &l)
             || !vhdl_island_eval_const_int(c->ctx->block, tree_right(rg), &r)) {
            if (is_target)
               return false;
            // Allow if slice endpoints are readable expressions ONLY if width is constant?
            // Safer: eject variable-offset slices unless both endpoints eval const.
            return false;
         }
         return check_expr(c, tree_value(expr), is_target);
      }

   case T_QUALIFIED:
   case T_TYPE_CONV:
      return check_expr(c, tree_value(expr), is_target);

   case T_COND_EXPR:
      for (int i = 0; i < tree_conds(expr); i++) {
         tree_t ce = tree_cond(expr, i);
         if (tree_has_value(ce) && !check_expr(c, tree_value(ce), false))
            return false;
         if (tree_stmts(ce) != 1 || tree_kind(tree_stmt(ce, 0)) != T_RETURN)
            return false;
         if (!check_expr(c, tree_value(tree_stmt(ce, 0)), false))
            return false;
      }
      return true;

   case T_AGGREGATE:
      for (int i = 0; i < tree_assocs(expr); i++) {
         tree_t a = tree_assoc(expr, i);
         switch (tree_subkind(a)) {
         case A_NAMED:
            if (!check_expr(c, tree_name(a), false))
               return false;
            break;
         case A_RANGE:
            {
               tree_t rg = tree_range(a, 0);
               const range_kind_t rk = tree_subkind(rg);
               if (rk != RANGE_TO && rk != RANGE_DOWNTO)
                  return false;
               int64_t l, r;
               if (!vhdl_island_eval_const_int(c->ctx->block, tree_left(rg), &l)
                   || !vhdl_island_eval_const_int(c->ctx->block,
                                                  tree_right(rg), &r))
                  return false;
            }
            break;
         case A_POS:
         case A_OTHERS:
         case A_CONCAT:
            break;
         default:
            return false;
         }
         if (!check_expr(c, tree_value(a), is_target))
            return false;
      }
      return true;

   case T_ATTR_REF:
      {
         const attr_kind_t ak = tree_subkind(expr);
         switch (ak) {
         case ATTR_LENGTH:
         case ATTR_LEFT:
         case ATTR_RIGHT:
         case ATTR_LOW:
         case ATTR_HIGH:
         case ATTR_ASCENDING:
         case ATTR_POS:
         case ATTR_VAL:
            for (int i = 0; i < tree_params(expr); i++) {
               if (!check_expr(c, tree_value(tree_param(expr, i)), false))
                  return false;
            }
            return !is_sig_root_expr(tree_name(expr))
               || !has_ineligible_signal_ref(c->ctx, tree_name(expr));
         case ATTR_EVENT:
            return false;
         default:
            return false;
         }
      }

   case T_FCALL:
      {
         if (is_target)
            return false;
         tree_t decl = tree_ref(expr);
         const subprogram_kind_t sk = tree_subkind(decl);
         if (c->two_state && (sk == S_MATCH_LT || sk == S_MATCH_LE
                              || sk == S_MATCH_GT || sk == S_MATCH_GE
                              || sk == S_MATCH_EQ || sk == S_MATCH_NEQ))
            return false;
         if (sk == S_RISING_EDGE || sk == S_FALLING_EDGE)
            return false;

         for (int i = 0; i < tree_params(expr); i++) {
            tree_t p = tree_param(expr, i);
            if (tree_subkind(p) != P_POS)
               return false;
            if (!check_expr(c, tree_value(p), false))
               return false;
         }

         if (sk != S_USER && sk != S_IEEE_MISC) {
            if (sk == S_IEEE_LOGICAL)
               return false;
            if (sk == S_DIV || sk == S_MOD || sk == S_REM || sk == S_EXP) {
               type_t rt = tree_type(expr);
               if (!type_is_integer(rt))
                  return false;
            }
            return true;
         }

         const char *pkg = NULL, *fn = NULL;
         if (ident_has_std_pkg_fn(decl, &pkg, &fn))
            return is_supported_std_fn(pkg, fn, c->two_state);

         // Allow pure user VHDL functions (either in same arch or package body!)
         return check_pure_func_body(c, decl);
      }

   default:
      return false;
   }
}

static bool check_stmt(check_ctx_t *c, tree_t stmt, bool is_func)
{
   if (stmt == NULL || c->rejected)
      return false;

   switch (tree_kind(stmt)) {
   case T_NULL:
      return true;

   case T_RETURN:
      if (!is_func)
         return false;
      return !tree_has_value(stmt) || check_expr(c, tree_value(stmt), false);

   case T_SIGNAL_ASSIGN:
      {
         if (is_func || !c->allow_signals)
            return false;
         if (tree_waveforms(stmt) != 1)
            return false;
         tree_t wf = tree_waveform(stmt, 0);
         if (tree_has_delay(wf) || !tree_has_value(wf))
            return false;
         if (!check_expr(c, tree_value(wf), false))
            return false;
         tree_t target = tree_target(stmt);
         if (!check_expr(c, target, true))
            return false;

         // Record exact constant bit/slice range for disjoint multi-driver support
         bool recorded = false;
         if (tree_kind(target) == T_ARRAY_SLICE) {
            tree_t rg = tree_range(target, 0);
            int64_t sl, sr;
            tree_t decl = NULL;
            int path[VHDL_ISIG_MAX_DEPTH];
            uint8_t plen = 0;
            if (vhdl_island_eval_const_int(c->ctx->block, tree_left(rg), &sl)
                && vhdl_island_eval_const_int(c->ctx->block, tree_right(rg), &sr)
                && vhdl_island_extract_path(tree_value(target), &decl, path, &plen)) {
               int sid = vhdl_island_lookup_leaf(c->ctx, decl, path, plen);
               if (sid >= 0) {
                  const vhdl_isig_t *s = &(c->ctx->sigs[sid]);
                  range_kind_t sdir = tree_subkind(rg);
                  int64_t sw = (sdir == RANGE_TO) ? (sr - sl + 1) : (sl - sr + 1);
                  int64_t pos = (s->dir == RANGE_TO) ? (sl - s->left) : (s->left - sl);
                  if (s->is_vector && !s->is_array2d && sdir == s->dir
                      && sw > 0 && pos >= 0 && pos + sw <= s->width) {
                     vhdl_write_range_t wr = {
                        .sig_id = (uint32_t)sid,
                        .pos_lo = (uint32_t)pos,
                        .pos_hi = (uint32_t)(pos + sw - 1),
                     };
                     APUSH(c->write_ranges, wr);
                     recorded = true;
                  }
               }
            }
         }
         else if (tree_kind(target) == T_ARRAY_REF && tree_params(target) == 1) {
            int64_t idx;
            tree_t decl = NULL;
            int path[VHDL_ISIG_MAX_DEPTH];
            uint8_t plen = 0;
            if (vhdl_island_eval_const_int(c->ctx->block,
                                           tree_value(tree_param(target, 0)), &idx)
                && vhdl_island_extract_path(tree_value(target), &decl, path, &plen)) {
               int sid = vhdl_island_lookup_leaf(c->ctx, decl, path, plen);
               if (sid >= 0) {
                  const vhdl_isig_t *s = &(c->ctx->sigs[sid]);
                  int64_t pos = (s->dir == RANGE_TO) ? (idx - s->left) : (s->left - idx);
                  if (s->is_vector && !s->is_array2d && pos >= 0 && pos < s->width) {
                     vhdl_write_range_t wr = {
                        .sig_id = (uint32_t)sid,
                        .pos_lo = (uint32_t)pos,
                        .pos_hi = (uint32_t)pos,
                     };
                     APUSH(c->write_ranges, wr);
                     recorded = true;
                  }
               }
            }
         }
         if (!recorded) {
            uint32_t *ids = NULL;
            uint32_t n = 0;
            if (vhdl_island_collect_expr_leaves(c->ctx, target, &ids, &n)) {
               for (uint32_t k = 0; k < n; k++) {
                  const vhdl_isig_t *s = &(c->ctx->sigs[ids[k]]);
                  vhdl_write_range_t wr = {
                     .sig_id = ids[k],
                     .pos_lo = 0,
                     .pos_hi = s->width > 0 ? s->width - 1 : 0,
                  };
                  APUSH(c->write_ranges, wr);
               }
            }
            free(ids);
         }
         return true;
      }

   case T_VAR_ASSIGN:
      {
         if (!check_expr(c, tree_value(stmt), false))
            return false;
         tree_t target = tree_target(stmt);
         tree_t base = target;
         while (tree_kind(base) == T_ARRAY_REF || tree_kind(base) == T_ARRAY_SLICE
                || tree_kind(base) == T_RECORD_REF)
            base = tree_value(base);
         if (tree_kind(base) != T_REF || tree_kind(tree_ref(base)) != T_VAR_DECL)
            return false;
         tree_t vdecl = tree_ref(base);
         if (!is_func) {
            int vi = find_var_idx(c, vdecl);
            if (vi < 0)
               return false;
            if (target == base) {
               // Full variable assignment marks it definitely assigned
               if (!check_expr(c, target, true))
                  return false;
               c->var_assigned.items[vi] = true;
               return true;
            }
            else {
               // Partial element/slice write reads prior value unless already assigned
               if (!c->var_assigned.items[vi])
                  return false;
               return check_expr(c, target, true);
            }
         }
         return check_expr(c, target, true);
      }

   case T_IF:
      {
         bool *saved = NULL;
         if (!is_func && c->var_decls.count > 0) {
            saved = xmalloc_array(c->var_decls.count, sizeof(bool));
            memcpy(saved, c->var_assigned.items,
                   c->var_decls.count * sizeof(bool));
         }
         bool has_else = false;
         bool *all_assigned = NULL;
         if (saved != NULL) {
            all_assigned = xmalloc_array(c->var_decls.count, sizeof(bool));
            memset(all_assigned, 1, c->var_decls.count * sizeof(bool));
         }

         const int nconds = tree_conds(stmt);
         for (int i = 0; i < nconds; i++) {
            tree_t br = tree_cond(stmt, i);
            if (tree_has_value(br)) {
               if (!check_expr(c, tree_value(br), false)) {
                  free(saved);
                  free(all_assigned);
                  return false;
               }
            }
            else
               has_else = true;

            if (saved != NULL)
               memcpy(c->var_assigned.items, saved,
                      c->var_decls.count * sizeof(bool));

            if (!check_stmt_list(c, br, 0, tree_stmts(br), is_func)) {
               free(saved);
               free(all_assigned);
               return false;
            }

            if (all_assigned != NULL) {
               for (unsigned v = 0; v < c->var_decls.count; v++)
                  all_assigned[v] = all_assigned[v] && c->var_assigned.items[v];
            }
         }

         if (saved != NULL) {
            for (unsigned v = 0; v < c->var_decls.count; v++)
               c->var_assigned.items[v] = saved[v] || (has_else && all_assigned[v]);
            free(saved);
            free(all_assigned);
         }
         return true;
      }

   case T_CASE:
      {
         if (!check_expr(c, tree_value(stmt), false))
            return false;

         bool *saved = NULL;
         if (!is_func && c->var_decls.count > 0) {
            saved = xmalloc_array(c->var_decls.count, sizeof(bool));
            memcpy(saved, c->var_assigned.items,
                   c->var_decls.count * sizeof(bool));
         }
         bool *all_assigned = NULL;
         if (saved != NULL) {
            all_assigned = xmalloc_array(c->var_decls.count, sizeof(bool));
            memset(all_assigned, 1, c->var_decls.count * sizeof(bool));
         }

         const int nalts = tree_stmts(stmt);
         for (int i = 0; i < nalts; i++) {
            tree_t alt = tree_stmt(stmt, i);
            const int nch = tree_choices(alt);
            for (int j = 0; j < nch; j++) {
               tree_t ch = tree_choice(alt, j);
               if (tree_ranges(ch) > 0) {
                  tree_t rg = tree_range(ch, 0);
                  int64_t l, r;
                  if (!vhdl_island_eval_const_int(c->ctx->block,
                                                 tree_left(rg), &l)
                      || !vhdl_island_eval_const_int(c->ctx->block,
                                                     tree_right(rg), &r)) {
                     free(saved);
                     free(all_assigned);
                     return false;
                  }
               }
               else if (tree_has_name(ch)) {
                  if (!check_expr(c, tree_name(ch), false)) {
                     free(saved);
                     free(all_assigned);
                     return false;
                  }
               }
            }

            if (saved != NULL)
               memcpy(c->var_assigned.items, saved,
                      c->var_decls.count * sizeof(bool));

            if (!check_stmt_list(c, alt, 0, tree_stmts(alt), is_func)) {
               free(saved);
               free(all_assigned);
               return false;
            }

            if (all_assigned != NULL) {
               for (unsigned v = 0; v < c->var_decls.count; v++)
                  all_assigned[v] = all_assigned[v] && c->var_assigned.items[v];
            }
         }

         if (saved != NULL) {
            // VHDL case statements are exhaustive
            for (unsigned v = 0; v < c->var_decls.count; v++)
               c->var_assigned.items[v] = saved[v] || all_assigned[v];
            free(saved);
            free(all_assigned);
         }
         return true;
      }

   case T_FOR:
      {
         tree_t rg = tree_range(stmt, 0);
         if (tree_kind(rg) != T_RANGE)
            return false;
         const range_kind_t rk = tree_subkind(rg);
         if (rk != RANGE_TO && rk != RANGE_DOWNTO)
            return false;
         int64_t l, r;
         if (!vhdl_island_eval_const_int(c->ctx->block, tree_left(rg), &l)
             || !vhdl_island_eval_const_int(c->ctx->block, tree_right(rg), &r))
            return false;
         const int64_t niters = (rk == RANGE_TO) ? (r - l + 1) : (l - r + 1);
         if (niters < 0 || niters > VHDL_ISLAND_MAX_LOOP_ITERS)
            return false;
         bool *saved = NULL;
         if (!is_func && c->var_decls.count > 0) {
            saved = xmalloc_array(c->var_decls.count, sizeof(bool));
            memcpy(saved, c->var_assigned.items,
                   c->var_decls.count * sizeof(bool));
         }
         const bool ok = check_stmt_list(c, stmt, 0, tree_stmts(stmt), is_func);
         if (saved != NULL) {
            memcpy(c->var_assigned.items, saved,
                   c->var_decls.count * sizeof(bool));
            free(saved);
         }
         return ok;
      }

   default:
      return false;
   }
}

static bool check_stmt_list(check_ctx_t *c, tree_t owner, int start, int end,
                            bool is_func)
{
   for (int i = start; i < end; i++) {
      if (!check_stmt(c, tree_stmt(owner, i), is_func))
         return false;
   }
   return true;
}

static bool extract_clk_edge(const vhdl_island_ctx_t *ctx, tree_t cond,
                             uint32_t *clk_sig_out, bool *posedge_out,
                             uint8_t *edge_flags_out)
{
   if (cond == NULL)
      return false;

   if (tree_kind(cond) == T_FCALL) {
      tree_t decl = tree_ref(cond);
      const subprogram_kind_t sk = tree_subkind(decl);
      if ((sk == S_RISING_EDGE || sk == S_FALLING_EDGE
           || ident_pkg_match(tree_ident2(decl), "IEEE.STD_LOGIC_1164.", "RISING_EDGE")
           || ident_pkg_match(tree_ident2(decl), "IEEE.STD_LOGIC_1164.", "FALLING_EDGE"))
          && tree_params(cond) == 1) {
         tree_t arg = tree_value(tree_param(cond, 0));
         tree_t rdecl = NULL;
         int path[VHDL_ISIG_MAX_DEPTH];
         uint8_t len = 0;
         if (!vhdl_island_extract_path(arg, &rdecl, path, &len))
            return false;
         int id = vhdl_island_lookup_leaf(ctx, rdecl, path, len);
         if (id < 0 || ctx->sigs[id].is_vector || ctx->sigs[id].width != 1
             || (ctx->sigs[id].elem_kind != VISIG_STD_LOGIC
                 && ctx->sigs[id].elem_kind != VISIG_BIT
                 && ctx->sigs[id].elem_kind != VISIG_BOOL))
            return false;
         const bool pos = (sk == S_RISING_EDGE
                           || ident_pkg_match(tree_ident2(decl),
                                              "IEEE.STD_LOGIC_1164.", "RISING_EDGE"));
         *clk_sig_out = (uint32_t)id;
         *posedge_out = pos;
         *edge_flags_out = RT_IEDGE_VHDL | (pos ? RT_IEDGE_CLK_POSEDGE : 0u);
         return true;
      }

      // Recognize clk'event and clk = '1' (in either order)
      if (sk == S_SCALAR_AND && tree_params(cond) == 2) {
         tree_t p0 = tree_value(tree_param(cond, 0));
         tree_t p1 = tree_value(tree_param(cond, 1));
         tree_t ev_expr = NULL, val_expr = NULL;
         if (tree_kind(p0) == T_ATTR_REF && tree_subkind(p0) == ATTR_EVENT) {
            ev_expr = p0;
            val_expr = p1;
         }
         else if (tree_kind(p1) == T_ATTR_REF && tree_subkind(p1) == ATTR_EVENT) {
            ev_expr = p1;
            val_expr = p0;
         }
         if (ev_expr != NULL) {
            tree_t clk_e = tree_name(ev_expr);
            tree_t rdecl = NULL;
            int path[VHDL_ISIG_MAX_DEPTH];
            uint8_t len = 0;
            if (!vhdl_island_extract_path(clk_e, &rdecl, path, &len))
               return false;
            int id = vhdl_island_lookup_leaf(ctx, rdecl, path, len);
            if (id < 0 || ctx->sigs[id].is_vector || ctx->sigs[id].width != 1
                || (ctx->sigs[id].elem_kind != VISIG_STD_LOGIC
                    && ctx->sigs[id].elem_kind != VISIG_BIT
                    && ctx->sigs[id].elem_kind != VISIG_BOOL))
               return false;

            bool pos = false;
            bool matched_val = false;
            if (tree_kind(val_expr) == T_FCALL
                && tree_subkind(tree_ref(val_expr)) == S_SCALAR_EQ
                && tree_params(val_expr) == 2) {
               tree_t lhs = tree_value(tree_param(val_expr, 0));
               tree_t rhs = tree_value(tree_param(val_expr, 1));
               tree_t lit_e = NULL;
               tree_t sig_e = NULL;
               tree_t ld = NULL, rd = NULL;
               int lp[VHDL_ISIG_MAX_DEPTH], rp[VHDL_ISIG_MAX_DEPTH];
               uint8_t ll = 0, rl = 0;
               if (vhdl_island_extract_path(lhs, &ld, lp, &ll)
                   && vhdl_island_lookup_leaf(ctx, ld, lp, ll) == id) {
                  sig_e = lhs;
                  lit_e = rhs;
               }
               else if (vhdl_island_extract_path(rhs, &rd, rp, &rl)
                        && vhdl_island_lookup_leaf(ctx, rd, rp, rl) == id) {
                  sig_e = rhs;
                  lit_e = lhs;
               }
               if (sig_e != NULL && tree_kind(lit_e) == T_REF
                   && tree_kind(tree_ref(lit_e)) == T_ENUM_LIT) {
                  unsigned p = tree_pos(tree_ref(lit_e));
                  if (ctx->sigs[id].elem_kind == VISIG_STD_LOGIC) {
                     if (p == 3 || p == 2) { // '1' or '0'
                        pos = (p == 3);
                        matched_val = true;
                     }
                  }
                  else {
                     pos = (p == 1);
                     matched_val = true;
                  }
               }
            }
            if (matched_val) {
               *clk_sig_out = (uint32_t)id;
               *posedge_out = pos;
               *edge_flags_out = RT_IEDGE_VHDL_EVENT
                  | (pos ? RT_IEDGE_CLK_POSEDGE : 0u);
               return true;
            }
         }
      }
   }

   return false;
}

static bool extract_rst_cond(const vhdl_island_ctx_t *ctx, tree_t cond,
                             uint32_t *rst_sig_out, bool *posedge_out)
{
   if (cond == NULL)
      return false;

   // Boolean reset: rst or not(rst)
   if (tree_kind(cond) == T_REF || tree_kind(cond) == T_RECORD_REF) {
      tree_t rdecl = NULL;
      int path[VHDL_ISIG_MAX_DEPTH];
      uint8_t len = 0;
      if (vhdl_island_extract_path(cond, &rdecl, path, &len)) {
         int id = vhdl_island_lookup_leaf(ctx, rdecl, path, len);
         if (id >= 0 && !ctx->sigs[id].is_vector
             && ctx->sigs[id].elem_kind == VISIG_BOOL) {
            *rst_sig_out = (uint32_t)id;
            *posedge_out = true;
            return true;
         }
      }
   }

   if (tree_kind(cond) == T_FCALL && tree_subkind(tree_ref(cond)) == S_SCALAR_NOT
       && tree_params(cond) == 1) {
      tree_t arg = tree_value(tree_param(cond, 0));
      tree_t rdecl = NULL;
      int path[VHDL_ISIG_MAX_DEPTH];
      uint8_t len = 0;
      if (vhdl_island_extract_path(arg, &rdecl, path, &len)) {
         int id = vhdl_island_lookup_leaf(ctx, rdecl, path, len);
         if (id >= 0 && !ctx->sigs[id].is_vector
             && ctx->sigs[id].elem_kind == VISIG_BOOL) {
            *rst_sig_out = (uint32_t)id;
            *posedge_out = false;
            return true;
         }
      }
   }

   if (tree_kind(cond) == T_FCALL && tree_subkind(tree_ref(cond)) == S_SCALAR_EQ
       && tree_params(cond) == 2) {
      tree_t lhs = tree_value(tree_param(cond, 0));
      tree_t rhs = tree_value(tree_param(cond, 1));
      tree_t rdecl = NULL;
      int path[VHDL_ISIG_MAX_DEPTH];
      uint8_t len = 0;
      tree_t lit_e = NULL;
      int id = -1;
      if (vhdl_island_extract_path(lhs, &rdecl, path, &len)) {
         id = vhdl_island_lookup_leaf(ctx, rdecl, path, len);
         lit_e = rhs;
      }
      else if (vhdl_island_extract_path(rhs, &rdecl, path, &len)) {
         id = vhdl_island_lookup_leaf(ctx, rdecl, path, len);
         lit_e = lhs;
      }
      if (id >= 0 && !ctx->sigs[id].is_vector && ctx->sigs[id].width == 1
          && (ctx->sigs[id].elem_kind == VISIG_STD_LOGIC
              || ctx->sigs[id].elem_kind == VISIG_BIT
              || ctx->sigs[id].elem_kind == VISIG_BOOL)
          && tree_kind(lit_e) == T_REF
          && tree_kind(tree_ref(lit_e)) == T_ENUM_LIT) {
         unsigned p = tree_pos(tree_ref(lit_e));
         if (ctx->sigs[id].elem_kind == VISIG_STD_LOGIC) {
            if (p == 3 || p == 2) {
               *rst_sig_out = (uint32_t)id;
               *posedge_out = (p == 3);
               return true;
            }
         }
         else {
            *rst_sig_out = (uint32_t)id;
            *posedge_out = (p == 1);
            return true;
         }
      }
   }

   return false;
}

static bool wait_covers_sig(const vhdl_island_ctx_t *ctx, tree_t wait_stmt,
                            uint32_t sig_id)
{
   const vhdl_isig_t *target = &(ctx->sigs[sig_id]);
   const int ntrig = tree_triggers(wait_stmt);
   for (int i = 0; i < ntrig; i++) {
      tree_t t = tree_trigger(wait_stmt, i);
      tree_t decl = NULL;
      int path[VHDL_ISIG_MAX_DEPTH];
      uint8_t len = 0;
      if (!vhdl_island_extract_path(t, &decl, path, &len))
         continue;
      if (decl == target->decl && target->path_len >= len
          && (len == 0 || memcmp(target->field_path, path,
                                 len * sizeof(int)) == 0))
         return true;
   }
   return false;
}

static bool check_process_decls(check_ctx_t *c, tree_t proc)
{
   const int nd = tree_decls(proc);
   for (int i = 0; i < nd; i++) {
      tree_t d = tree_decl(proc, i);
      switch (tree_kind(d)) {
      case T_VAR_DECL:
         if (tree_flags(d) & TREE_F_SHARED)
            return false;
         if (!type_ok_for_local(tree_type(d)))
            return false;
         APUSH(c->var_decls, d);
         {
            // In VHDL, process variables have static storage duration: their
            // initializer runs once at t=0, not on every process activation.
            // A variable is only safe as a stack-local island temporary if it
            // is definitely assigned before any read on every activation path.
            if (tree_has_value(d) && !check_expr(c, tree_value(d), false))
               return false;
            APUSH(c->var_assigned, false);
         }
         break;
      case T_CONST_DECL:
         if (!type_ok_for_local(tree_type(d)))
            return false;
         if (tree_has_value(d) && !check_expr(c, tree_value(d), false))
            return false;
         break;
      case T_TYPE_DECL:
      case T_SUBTYPE_DECL:
         break;
      default:
         return false;
      }
   }
   return true;
}

static void analyze_process(vhdl_island_ctx_t *ctx, tree_t proc, int stmt_idx,
                            bool two_state, proc_list_t *procs,
                            u32_list_t *ext_reads)
{
   const int nstmts = tree_stmts(proc);
   if (nstmts < 2)
      goto eject_scan_reads;

   tree_t wait = tree_stmt(proc, nstmts - 1);
   if (tree_kind(wait) != T_WAIT || tree_has_delay(wait)
       || tree_has_value(wait) || tree_triggers(wait) == 0)
      goto eject_scan_reads;

   // Try FF classification first: single T_IF statement + final T_WAIT
   if (nstmts == 2 && tree_kind(tree_stmt(proc, 0)) == T_IF) {
      tree_t if_stmt = tree_stmt(proc, 0);
      const int nconds = tree_conds(if_stmt);
      uint32_t clk_sig = 0;
      int32_t rst_sig = -1;
      bool clk_pos = false, rst_pos = false;
      uint8_t edge_flags = 0;
      tree_t clk_br = NULL, rst_br = NULL;

      if (nconds == 1) {
         tree_t b0 = tree_cond(if_stmt, 0);
         if (tree_has_value(b0)
             && extract_clk_edge(ctx, tree_value(b0), &clk_sig, &clk_pos,
                                 &edge_flags))
            clk_br = b0;
      }
      else if (nconds == 2) {
         tree_t b0 = tree_cond(if_stmt, 0);
         tree_t b1 = tree_cond(if_stmt, 1);
         uint32_t rsig = 0;
         bool rpos = false;
         if (tree_has_value(b0) && tree_has_value(b1)
             && extract_rst_cond(ctx, tree_value(b0), &rsig, &rpos)
             && extract_clk_edge(ctx, tree_value(b1), &clk_sig, &clk_pos,
                                 &edge_flags)
             && rsig != clk_sig) {
            rst_sig = (int32_t)rsig;
            rst_pos = rpos;
            rst_br = b0;
            clk_br = b1;
            edge_flags |= RT_IEDGE_RST_LEVEL
               | (rst_pos ? RT_IEDGE_RST_POSEDGE : 0u);
         }
      }

      if (clk_br != NULL
          && wait_covers_sig(ctx, wait, clk_sig)
          && (rst_sig < 0 || wait_covers_sig(ctx, wait, (uint32_t)rst_sig))) {
         check_ctx_t c = {
            .ctx           = ctx,
            .two_state     = two_state,
            .allow_signals = true,
            .in_func       = false,
         };
         bool ok = check_process_decls(&c, proc);
         if (ok && rst_br != NULL)
            ok = check_stmt_list(&c, rst_br, 0, tree_stmts(rst_br), false);
         if (ok) {
            // Reset per-branch var assignment before checking clk_br
            for (unsigned i = 0; i < c.var_decls.count; i++)
               c.var_assigned.items[i] = false;
            ok = check_stmt_list(&c, clk_br, 0, tree_stmts(clk_br), false);
         }
         // FF must not write its own clk or rst signal
         if (ok) {
            for (unsigned i = 0; i < c.writes.count; i++) {
               if (c.writes.items[i] == clk_sig
                   || (rst_sig >= 0 && c.writes.items[i] == (uint32_t)rst_sig))
                  ok = false;
            }
         }
         if (ok && c.writes.count > 0) {
            push_unique_u32(&c.reads, clk_sig);
            if (rst_sig >= 0)
               push_unique_u32(&c.reads, (uint32_t)rst_sig);

            vhdl_iproc_t p = {
               .stmt           = proc,
               .block_stmt_idx = stmt_idx,
               .kind           = VIPROC_FF,
               .clk_sig        = clk_sig,
               .rst_sig        = rst_sig,
               .clk_posedge    = clk_pos,
               .rst_posedge    = rst_pos,
               .edge_flags     = edge_flags,
               .if_stmt        = if_stmt,
               .clk_branch     = clk_br,
               .rst_branch     = rst_br,
               .reads          = c.reads.items,
               .n_reads        = c.reads.count,
               .writes         = c.writes.items,
               .n_writes       = c.writes.count,
               .write_ranges   = c.write_ranges.items,
               .n_write_ranges = c.write_ranges.count,
               .island_idx     = -1,
               .dom_idx        = -1,
            };
            ACLEAR(c.var_decls);
            ACLEAR(c.var_assigned);
            ACLEAR(c.loop_vars);
            ACLEAR(c.active_funcs);
            APUSH(*procs, p);
            return;
         }
         ACLEAR(c.reads);
         ACLEAR(c.writes);
         ACLEAR(c.write_ranges);
         ACLEAR(c.var_decls);
         ACLEAR(c.var_assigned);
         ACLEAR(c.loop_vars);
         ACLEAR(c.active_funcs);
      }
   }

   // Try COMB classification
   {
      check_ctx_t c = {
         .ctx           = ctx,
         .two_state     = two_state,
         .allow_signals = true,
         .in_func       = false,
      };
      bool ok = check_process_decls(&c, proc)
         && check_stmt_list(&c, proc, 0, nstmts - 1, false);
      if (ok && c.writes.count > 0) {
         // Verify all read signals covered by sensitivity list
         for (unsigned i = 0; i < c.reads.count && ok; i++) {
            if (!wait_covers_sig(ctx, wait, c.reads.items[i]))
               ok = false;
         }
         // Eject self-reading combinational loops inside a single process
         for (unsigned i = 0; i < c.reads.count && ok; i++) {
            for (unsigned j = 0; j < c.writes.count; j++) {
               if (c.reads.items[i] == c.writes.items[j]) {
                  ok = false;
                  break;
               }
            }
         }
         if (ok) {
            vhdl_iproc_t p = {
               .stmt           = proc,
               .block_stmt_idx = stmt_idx,
               .kind           = VIPROC_COMB,
               .rst_sig        = -1,
               .reads          = c.reads.items,
               .n_reads        = c.reads.count,
               .writes         = c.writes.items,
               .n_writes       = c.writes.count,
               .write_ranges   = c.write_ranges.items,
               .n_write_ranges = c.write_ranges.count,
               .island_idx     = -1,
               .dom_idx        = -1,
            };
            ACLEAR(c.var_decls);
            ACLEAR(c.var_assigned);
            ACLEAR(c.loop_vars);
            ACLEAR(c.active_funcs);
            APUSH(*procs, p);
            return;
         }
      }
      ACLEAR(c.reads);
      ACLEAR(c.writes);
      ACLEAR(c.write_ranges);
      ACLEAR(c.var_decls);
      ACLEAR(c.var_assigned);
      ACLEAR(c.loop_vars);
      ACLEAR(c.active_funcs);
   }

eject_scan_reads:
   // Walk conservatively to collect any signal references by unfused process
   // Note: below walk is done cleanly by scan_unfused_tree
   (void)ext_reads;
}

static void scan_unfused_refs(const vhdl_island_ctx_t *ctx, tree_t t,
                              bool is_target, u32_list_t *ext_reads,
                              bool mark_ext_drivers, sig_list_t *sigs_mut,
                              int depth);

static void scan_psl_refs(const vhdl_island_ctx_t *ctx, psl_node_t p,
                          u32_list_t *ext_reads, sig_list_t *sigs_mut,
                          int depth)
{
   if (p == NULL || depth > 64)
      return;

   switch (psl_kind(p)) {
   case P_HDL_EXPR:
   case P_CLOCK_DECL:
      if (psl_has_tree(p))
         scan_unfused_refs(ctx, psl_tree(p), false, ext_reads, false,
                           sigs_mut, 0);
      return;

   case P_PARAM_DECL:
      if (psl_has_tree(p))
         scan_unfused_refs(ctx, psl_tree(p), false, ext_reads, false,
                           sigs_mut, 0);
      if (psl_has_value(p))
         scan_psl_refs(ctx, psl_value(p), ext_reads, sigs_mut, depth + 1);
      return;

   case P_PROC_BLOCK:
      if (psl_has_tree(p))
         scan_unfused_refs(ctx, psl_tree(p), false, ext_reads, true,
                           sigs_mut, 0);
      if (psl_has_value(p))
         scan_psl_refs(ctx, psl_value(p), ext_reads, sigs_mut, depth + 1);
      return;

   case P_CLOCKED:
      if (psl_has_tree(p))
         scan_unfused_refs(ctx, psl_tree(p), false, ext_reads, false,
                           sigs_mut, 0);
      if (psl_has_value(p))
         scan_psl_refs(ctx, psl_value(p), ext_reads, sigs_mut, depth + 1);
      if (psl_has_ref(p))
         scan_psl_refs(ctx, psl_ref(p), ext_reads, sigs_mut, depth + 1);
      return;

   case P_ASSERT:
   case P_ASSUME:
   case P_RESTRICT:
   case P_COVER:
      if (psl_has_message(p))
         scan_unfused_refs(ctx, psl_message(p), false, ext_reads, false,
                           sigs_mut, 0);
      if (psl_has_value(p))
         scan_psl_refs(ctx, psl_value(p), ext_reads, sigs_mut, depth + 1);
      return;

   case P_FAIRNESS:
      if (psl_has_message(p))
         scan_unfused_refs(ctx, psl_message(p), false, ext_reads, false,
                           sigs_mut, 0);
      for (unsigned i = 0; i < psl_operands(p); i++)
         scan_psl_refs(ctx, psl_operand(p, i), ext_reads, sigs_mut, depth + 1);
      return;

   case P_ALWAYS:
   case P_NEVER:
   case P_EVENTUALLY:
   case P_PROPERTY_DECL:
   case P_SEQUENCE_DECL:
   case P_ENDPOINT_DECL:
      if (psl_has_value(p))
         scan_psl_refs(ctx, psl_value(p), ext_reads, sigs_mut, depth + 1);
      return;

   case P_NEXT:
   case P_NEXT_A:
   case P_NEXT_E:
   case P_REPEAT:
      if (psl_has_value(p))
         scan_psl_refs(ctx, psl_value(p), ext_reads, sigs_mut, depth + 1);
      if (psl_has_delay(p))
         scan_psl_refs(ctx, psl_delay(p), ext_reads, sigs_mut, depth + 1);
      return;

   case P_NEXT_EVENT:
   case P_NEXT_EVENT_A:
   case P_NEXT_EVENT_E:
      if (psl_has_value(p))
         scan_psl_refs(ctx, psl_value(p), ext_reads, sigs_mut, depth + 1);
      if (psl_has_delay(p))
         scan_psl_refs(ctx, psl_delay(p), ext_reads, sigs_mut, depth + 1);
      for (unsigned i = 0; i < psl_operands(p); i++)
         scan_psl_refs(ctx, psl_operand(p, i), ext_reads, sigs_mut, depth + 1);
      return;

   case P_PROPERTY_INST:
   case P_SEQUENCE_INST:
      if (psl_has_ref(p))
         scan_psl_refs(ctx, psl_ref(p), ext_reads, sigs_mut, depth + 1);
      for (unsigned i = 0; i < psl_operands(p); i++)
         scan_psl_refs(ctx, psl_operand(p, i), ext_reads, sigs_mut, depth + 1);
      return;

   case P_PARAM_SERE:
   case P_PARAM_PROP:
      if (psl_has_value(p))
         scan_psl_refs(ctx, psl_value(p), ext_reads, sigs_mut, depth + 1);
      for (unsigned i = 0; i < psl_operands(p); i++)
         scan_psl_refs(ctx, psl_operand(p, i), ext_reads, sigs_mut, depth + 1);
      return;

   case P_SERE:
   case P_UNION:
   case P_BUILTIN_FCALL:
   case P_VALUE_SET:
   case P_UNTIL:
   case P_ABORT:
   case P_BEFORE:
   case P_SUFFIX_IMPL:
   case P_LOGICAL:
      for (unsigned i = 0; i < psl_operands(p); i++)
         scan_psl_refs(ctx, psl_operand(p, i), ext_reads, sigs_mut, depth + 1);
      return;

   case P_RANGE:
      scan_psl_refs(ctx, psl_left(p), ext_reads, sigs_mut, depth + 1);
      scan_psl_refs(ctx, psl_right(p), ext_reads, sigs_mut, depth + 1);
      return;

   default:
      return;
   }
}

static void scan_unfused_refs(const vhdl_island_ctx_t *ctx, tree_t t,
                              bool is_target, u32_list_t *ext_reads,
                              bool mark_ext_drivers, sig_list_t *sigs_mut,
                              int depth)
{
   if (t == NULL || depth > 64)
      return;

   switch (tree_kind(t)) {
   case T_REF:
   case T_RECORD_REF:
      {
         tree_t base = t;
         while (tree_kind(base) == T_RECORD_REF)
            base = tree_value(base);
         if (tree_kind(base) == T_REF) {
            tree_t d = tree_ref(base);
            if (d != NULL && (tree_kind(d) == T_PORT_DECL
                              || tree_kind(d) == T_SIGNAL_DECL)) {
               uint32_t *ids = NULL;
               uint32_t n = 0, cap = 0;
               tree_t decl = NULL;
               int path[VHDL_ISIG_MAX_DEPTH];
               uint8_t plen = 0;
               if (vhdl_island_extract_path(t, &decl, path, &plen)) {
                  for (uint32_t i = 0; i < ctx->n_sigs; i++) {
                     const vhdl_isig_t *s = &(ctx->sigs[i]);
                     if (s->decl == decl && s->path_len >= plen
                         && (plen == 0
                             || memcmp(s->field_path, path,
                                       plen * sizeof(int)) == 0)) {
                        if (is_target && mark_ext_drivers && sigs_mut != NULL)
                           sigs_mut->items[i].has_ext_driver = true;
                        else {
                           push_unique_u32(ext_reads, i);
                           if (sigs_mut != NULL)
                              sigs_mut->items[i].ext_read = true;
                        }
                     }
                  }
               }
               free(ids);
               (void)n;
               (void)cap;
               return;
            }
         }
         if (tree_kind(t) == T_RECORD_REF)
            scan_unfused_refs(ctx, tree_value(t), is_target, ext_reads,
                              mark_ext_drivers, sigs_mut, depth + 1);
      }
      return;

   case T_SIGNAL_ASSIGN:
      scan_unfused_refs(ctx, tree_target(t), true, ext_reads,
                        mark_ext_drivers, sigs_mut, depth + 1);
      for (int i = 0; i < tree_waveforms(t); i++) {
         tree_t w = tree_waveform(t, i);
         if (tree_has_value(w))
            scan_unfused_refs(ctx, tree_value(w), false, ext_reads,
                              mark_ext_drivers, sigs_mut, depth + 1);
      }
      return;

   case T_VAR_ASSIGN:
      scan_unfused_refs(ctx, tree_target(t), false, ext_reads,
                        mark_ext_drivers, sigs_mut, depth + 1);
      scan_unfused_refs(ctx, tree_value(t), false, ext_reads,
                        mark_ext_drivers, sigs_mut, depth + 1);
      return;

   case T_ARRAY_REF:
      scan_unfused_refs(ctx, tree_value(t), is_target, ext_reads,
                        mark_ext_drivers, sigs_mut, depth + 1);
      for (int i = 0; i < tree_params(t); i++)
         scan_unfused_refs(ctx, tree_value(tree_param(t, i)), false,
                           ext_reads, mark_ext_drivers, sigs_mut, depth + 1);
      return;

   case T_ARRAY_SLICE:
      scan_unfused_refs(ctx, tree_value(t), is_target, ext_reads,
                        mark_ext_drivers, sigs_mut, depth + 1);
      return;

   case T_QUALIFIED:
   case T_TYPE_CONV:
      scan_unfused_refs(ctx, tree_value(t), is_target, ext_reads,
                        mark_ext_drivers, sigs_mut, depth + 1);
      return;

   case T_ATTR_REF:
      scan_unfused_refs(ctx, tree_name(t), false, ext_reads,
                        mark_ext_drivers, sigs_mut, depth + 1);
      for (int i = 0; i < tree_params(t); i++)
         scan_unfused_refs(ctx, tree_value(tree_param(t, i)), false,
                           ext_reads, mark_ext_drivers, sigs_mut, depth + 1);
      return;

   case T_AGGREGATE:
      for (int i = 0; i < tree_assocs(t); i++) {
         tree_t a = tree_assoc(t, i);
         if (tree_subkind(a) == A_NAMED)
            scan_unfused_refs(ctx, tree_name(a), false, ext_reads,
                              mark_ext_drivers, sigs_mut, depth + 1);
         scan_unfused_refs(ctx, tree_value(a), is_target, ext_reads,
                           mark_ext_drivers, sigs_mut, depth + 1);
      }
      return;

   case T_FCALL:
      for (int i = 0; i < tree_params(t); i++)
         scan_unfused_refs(ctx, tree_value(tree_param(t, i)), false,
                           ext_reads, mark_ext_drivers, sigs_mut, depth + 1);
      return;

   case T_IF:
   case T_COND_EXPR:
      for (int i = 0; i < tree_conds(t); i++) {
         tree_t c = tree_cond(t, i);
         if (tree_has_value(c))
            scan_unfused_refs(ctx, tree_value(c), false, ext_reads,
                              mark_ext_drivers, sigs_mut, depth + 1);
         for (int j = 0; j < tree_stmts(c); j++)
            scan_unfused_refs(ctx, tree_stmt(c, j), false, ext_reads,
                              mark_ext_drivers, sigs_mut, depth + 1);
      }
      return;

   case T_CASE:
   case T_MATCH_CASE:
      scan_unfused_refs(ctx, tree_value(t), false, ext_reads,
                        mark_ext_drivers, sigs_mut, depth + 1);
      for (int i = 0; i < tree_stmts(t); i++) {
         tree_t alt = tree_stmt(t, i);
         for (int j = 0; j < tree_stmts(alt); j++)
            scan_unfused_refs(ctx, tree_stmt(alt, j), false, ext_reads,
                              mark_ext_drivers, sigs_mut, depth + 1);
      }
      return;

   case T_PROCESS:
   case T_FOR:
   case T_WHILE:
   case T_LOOP:
   case T_SEQUENCE:
      for (int i = 0; i < tree_stmts(t); i++)
         scan_unfused_refs(ctx, tree_stmt(t, i), false, ext_reads,
                           mark_ext_drivers, sigs_mut, depth + 1);
      return;

   case T_WAIT:
      if (tree_has_value(t))
         scan_unfused_refs(ctx, tree_value(t), false, ext_reads,
                           mark_ext_drivers, sigs_mut, depth + 1);
      for (int i = 0; i < tree_triggers(t); i++)
         scan_unfused_refs(ctx, tree_trigger(t, i), false, ext_reads,
                           mark_ext_drivers, sigs_mut, depth + 1);
      return;

   case T_ASSERT:
      if (tree_has_value(t))
         scan_unfused_refs(ctx, tree_value(t), false, ext_reads,
                           mark_ext_drivers, sigs_mut, depth + 1);
      // Fallthrough
   case T_REPORT:
      if (tree_has_message(t))
         scan_unfused_refs(ctx, tree_message(t), false, ext_reads,
                           mark_ext_drivers, sigs_mut, depth + 1);
      if (tree_has_severity(t))
         scan_unfused_refs(ctx, tree_severity(t), false, ext_reads,
                           mark_ext_drivers, sigs_mut, depth + 1);
      return;

   case T_FORCE:
      scan_unfused_refs(ctx, tree_target(t), true, ext_reads,
                        mark_ext_drivers, sigs_mut, depth + 1);
      if (tree_has_value(t))
         scan_unfused_refs(ctx, tree_value(t), false, ext_reads,
                           mark_ext_drivers, sigs_mut, depth + 1);
      return;

   case T_RELEASE:
      scan_unfused_refs(ctx, tree_target(t), true, ext_reads,
                        mark_ext_drivers, sigs_mut, depth + 1);
      return;

   case T_PCALL:
   case T_PROT_PCALL:
   case T_PROT_FCALL:
      for (int i = 0; i < tree_params(t); i++)
         scan_unfused_refs(ctx, tree_value(tree_param(t, i)), false,
                           ext_reads, mark_ext_drivers, sigs_mut, depth + 1);
      return;

   case T_RETURN:
      if (tree_has_value(t))
         scan_unfused_refs(ctx, tree_value(t), false, ext_reads,
                           mark_ext_drivers, sigs_mut, depth + 1);
      return;

   case T_PSL_DIRECT:
   case T_PSL_DECL:
   case T_PSL_FCALL:
   case T_PSL_UNION:
      scan_psl_refs(ctx, tree_psl(t), ext_reads, sigs_mut, 0);
      return;

   default:
      return;
   }
}

static bool enum_lit_nonbinary_ulogic(tree_t lit)
{
   type_t base = type_base_recur(tree_type(lit));
   if (is_well_known(type_ident(base)) != W_IEEE_ULOGIC)
      return false;
   const unsigned pos = tree_pos(lit);
   return pos != 2 && pos != 3; // Anything outside '0'/'1'
}

static bool expr_has_9state_lit(tree_t t, int depth)
{
   if (t == NULL || depth > 48)
      return false;

   switch (tree_kind(t)) {
   case T_REF:
      {
         tree_t d = tree_ref(t);
         return d != NULL && tree_kind(d) == T_ENUM_LIT
            && enum_lit_nonbinary_ulogic(d);
      }
   case T_STRING:
      for (int i = 0; i < tree_chars(t); i++) {
         tree_t ch = tree_char(t, i);
         if (tree_kind(ch) == T_REF && tree_kind(tree_ref(ch)) == T_ENUM_LIT
             && enum_lit_nonbinary_ulogic(tree_ref(ch)))
            return true;
      }
      return false;
   case T_AGGREGATE:
      for (int i = 0; i < tree_assocs(t); i++) {
         if (expr_has_9state_lit(tree_value(tree_assoc(t, i)), depth + 1))
            return true;
      }
      return false;
   case T_QUALIFIED:
   case T_TYPE_CONV:
   case T_ARRAY_SLICE:
   case T_ARRAY_REF:
      return expr_has_9state_lit(tree_value(t), depth + 1);
   case T_FCALL:
      for (int i = 0; i < tree_params(t); i++) {
         if (expr_has_9state_lit(tree_value(tree_param(t, i)), depth + 1))
            return true;
      }
      return false;
   case T_COND_EXPR:
      for (int i = 0; i < tree_conds(t); i++) {
         tree_t c = tree_cond(t, i);
         if (tree_has_value(c) && expr_has_9state_lit(tree_value(c), depth + 1))
            return true;
         for (int j = 0; j < tree_stmts(c); j++) {
            if (expr_has_9state_lit(tree_stmt(c, j), depth + 1))
               return true;
         }
      }
      return false;
   default:
      return false;
   }
}

static void mark_9state_drivers(const vhdl_island_ctx_t *ctx, sig_list_t *sigs,
                                tree_t t, int depth)
{
   if (t == NULL || depth > 48)
      return;

   switch (tree_kind(t)) {
   case T_SIGNAL_ASSIGN:
      {
         bool has9 = false;
         for (int i = 0; i < tree_waveforms(t) && !has9; i++) {
            tree_t w = tree_waveform(t, i);
            if (tree_has_value(w) && expr_has_9state_lit(tree_value(w), 0))
               has9 = true;
         }
         if (has9) {
            tree_t decl = NULL;
            int path[VHDL_ISIG_MAX_DEPTH];
            uint8_t plen = 0;
            if (vhdl_island_extract_path(tree_target(t), &decl, path, &plen)) {
               for (uint32_t i = 0; i < ctx->n_sigs; i++) {
                  const vhdl_isig_t *s = &(ctx->sigs[i]);
                  if (s->decl == decl && s->path_len >= plen
                      && (plen == 0
                          || memcmp(s->field_path, path,
                                    plen * sizeof(int)) == 0))
                     sigs->items[i].has_9state_driver = true;
               }
            }
         }
      }
      return;
   case T_PROCESS:
   case T_FOR:
   case T_WHILE:
   case T_LOOP:
   case T_SEQUENCE:
      for (int i = 0; i < tree_stmts(t); i++)
         mark_9state_drivers(ctx, sigs, tree_stmt(t, i), depth + 1);
      return;
   case T_IF:
      for (int i = 0; i < tree_conds(t); i++) {
         tree_t c = tree_cond(t, i);
         for (int j = 0; j < tree_stmts(c); j++)
            mark_9state_drivers(ctx, sigs, tree_stmt(c, j), depth + 1);
      }
      return;
   case T_CASE:
   case T_MATCH_CASE:
      for (int i = 0; i < tree_stmts(t); i++) {
         tree_t alt = tree_stmt(t, i);
         for (int j = 0; j < tree_stmts(alt); j++)
            mark_9state_drivers(ctx, sigs, tree_stmt(alt, j), depth + 1);
      }
      return;
   default:
      return;
   }
}

static void scan_child_block_uses(const vhdl_island_ctx_t *ctx, tree_t blk,
                                  u32_list_t *ext_reads, sig_list_t *sigs,
                                  int depth)
{
   if (blk == NULL || depth > 16)
      return;

   const int cp = tree_ports(blk);
   const int cpm = tree_params(blk);
   for (int j = 0; j < cp && j < cpm; j++) {
      tree_t fp = tree_port(blk, j);
      tree_t actual = tree_value(tree_param(blk, j));
      const port_mode_t pm = tree_subkind(fp);
      const bool writes_parent = (pm == PORT_OUT || pm == PORT_INOUT
                                  || pm == PORT_BUFFER);
      const bool reads_parent = (pm == PORT_IN || pm == PORT_INOUT
                                 || pm == PORT_BUFFER);
      if (writes_parent)
         scan_unfused_refs(ctx, actual, true, ext_reads, true, sigs, 0);
      if (reads_parent) {
         u32_list_t dummy = AINIT;
         scan_unfused_refs(ctx, actual, false, &dummy, false, NULL, 0);
         for (unsigned k = 0; k < dummy.count; k++)
            sigs->items[dummy.items[k]].child_read = true;
         ACLEAR(dummy);
      }
   }

   bool is_entity_inst = false;
   if (tree_decls(blk) > 0 && tree_kind(tree_decl(blk, 0)) == T_HIER) {
      tree_t unit = tree_ref(tree_decl(blk, 0));
      if (unit != NULL && (is_design_unit(unit)
                           || tree_kind(unit) == T_COMPONENT))
         is_entity_inst = true;
   }
   if (!is_entity_inst) {
      const int nd = tree_decls(blk);
      for (int i = 0; i < nd; i++) {
         tree_t cd = tree_decl(blk, i);
         if (tree_kind(cd) == T_PSL_DECL)
            scan_psl_refs(ctx, tree_psl(cd), ext_reads, sigs, 0);
      }
      const int n = tree_stmts(blk);
      for (int i = 0; i < n; i++) {
         tree_t cs = tree_stmt(blk, i);
         if (tree_kind(cs) == T_BLOCK)
            scan_child_block_uses(ctx, cs, ext_reads, sigs, depth + 1);
         else if (tree_kind(cs) == T_PROCESS || tree_kind(cs) == T_PSL_DIRECT)
            scan_unfused_refs(ctx, cs, false, ext_reads, true, sigs, 0);
      }
   }
}

static void mark_target_sig_flag(sig_list_t *sigs, tree_t target,
                                 bool set_ineligible, bool set_ext_driver)
{
   if (target == NULL)
      return;
   if (tree_kind(target) == T_AGGREGATE) {
      for (int i = 0; i < tree_assocs(target); i++)
         mark_target_sig_flag(sigs, tree_value(tree_assoc(target, i)),
                              set_ineligible, set_ext_driver);
      return;
   }
   tree_t base = unwrap_signal_base(target);
   tree_t decl = NULL;
   int path[VHDL_ISIG_MAX_DEPTH];
   uint8_t plen = 0;
   if (vhdl_island_extract_path(base, &decl, path, &plen)) {
      for (uint32_t i = 0; i < sigs->count; i++) {
         if (sigs->items[i].decl == decl
             && (plen == 0
                 || (sigs->items[i].path_len >= plen
                     && memcmp(sigs->items[i].field_path, path,
                               plen * sizeof(int)) == 0))) {
            if (set_ineligible)
               sigs->items[i].ineligible = true;
            if (set_ext_driver)
               sigs->items[i].has_ext_driver = true;
         }
      }
   }
}

static void mark_ineligible_attr_expr(vhdl_island_ctx_t *ctx, sig_list_t *sigs,
                                      tree_t t, int depth)
{
   if (t == NULL || depth > 48)
      return;
   switch (tree_kind(t)) {
   case T_ATTR_REF:
      {
         const attr_kind_t ak = tree_subkind(t);
         if (ak == ATTR_DELAYED || ak == ATTR_STABLE
             || ak == ATTR_QUIET || ak == ATTR_TRANSACTION
             || ak == ATTR_ACTIVE || ak == ATTR_LAST_ACTIVE
             || ak == ATTR_DRIVING || ak == ATTR_DRIVING_VALUE) {
            mark_target_sig_flag(sigs, tree_name(t), true, false);
         }
         else if (ak == ATTR_LAST_EVENT || ak == ATTR_LAST_VALUE) {
            mark_target_sig_flag(sigs, tree_name(t), false, true);
         }
         mark_ineligible_attr_expr(ctx, sigs, tree_name(t), depth + 1);
         for (int i = 0; i < tree_params(t); i++)
            mark_ineligible_attr_expr(ctx, sigs, tree_value(tree_param(t, i)),
                                      depth + 1);
      }
      break;
   case T_FORCE:
      mark_target_sig_flag(sigs, tree_target(t), true, true);
      if (tree_has_value(t))
         mark_ineligible_attr_expr(ctx, sigs, tree_value(t), depth + 1);
      break;
   case T_RELEASE:
      mark_target_sig_flag(sigs, tree_target(t), true, true);
      break;
   case T_IF:
   case T_COND_EXPR:
      for (int i = 0; i < tree_conds(t); i++) {
         tree_t c = tree_cond(t, i);
         if (tree_has_value(c))
            mark_ineligible_attr_expr(ctx, sigs, tree_value(c), depth + 1);
         for (int j = 0; j < tree_stmts(c); j++)
            mark_ineligible_attr_expr(ctx, sigs, tree_stmt(c, j), depth + 1);
      }
      break;
   case T_CASE:
   case T_MATCH_CASE:
      mark_ineligible_attr_expr(ctx, sigs, tree_value(t), depth + 1);
      for (int i = 0; i < tree_stmts(t); i++) {
         tree_t a = tree_stmt(t, i);
         for (int j = 0; j < tree_stmts(a); j++)
            mark_ineligible_attr_expr(ctx, sigs, tree_stmt(a, j), depth + 1);
      }
      break;
   case T_SIGNAL_ASSIGN:
      for (int i = 0; i < tree_waveforms(t); i++) {
         tree_t w = tree_waveform(t, i);
         if (tree_has_value(w))
            mark_ineligible_attr_expr(ctx, sigs, tree_value(w), depth + 1);
      }
      break;
   case T_VAR_ASSIGN:
      mark_ineligible_attr_expr(ctx, sigs, tree_target(t), depth + 1);
      mark_ineligible_attr_expr(ctx, sigs, tree_value(t), depth + 1);
      break;
   case T_ARRAY_REF:
      mark_ineligible_attr_expr(ctx, sigs, tree_value(t), depth + 1);
      for (int i = 0; i < tree_params(t); i++)
         mark_ineligible_attr_expr(ctx, sigs, tree_value(tree_param(t, i)),
                                   depth + 1);
      break;
   case T_ARRAY_SLICE:
   case T_RECORD_REF:
   case T_QUALIFIED:
   case T_TYPE_CONV:
      mark_ineligible_attr_expr(ctx, sigs, tree_value(t), depth + 1);
      break;
   case T_AGGREGATE:
      for (int i = 0; i < tree_assocs(t); i++) {
         tree_t a = tree_assoc(t, i);
         if (tree_subkind(a) == A_NAMED)
            mark_ineligible_attr_expr(ctx, sigs, tree_name(a), depth + 1);
         mark_ineligible_attr_expr(ctx, sigs, tree_value(a), depth + 1);
      }
      break;
   case T_RETURN:
      if (tree_has_value(t))
         mark_ineligible_attr_expr(ctx, sigs, tree_value(t), depth + 1);
      break;
   case T_BLOCK:
      {
         bool is_entity_inst = false;
         if (tree_decls(t) > 0 && tree_kind(tree_decl(t, 0)) == T_HIER) {
            tree_t unit = tree_ref(tree_decl(t, 0));
            if (unit != NULL && (is_design_unit(unit)
                                 || tree_kind(unit) == T_COMPONENT))
               is_entity_inst = true;
         }
         if (!is_entity_inst) {
            for (int i = 0; i < tree_stmts(t); i++)
               mark_ineligible_attr_expr(ctx, sigs, tree_stmt(t, i), depth + 1);
         }
      }
      break;
   case T_PROCESS:
   case T_FOR:
   case T_WHILE:
   case T_LOOP:
   case T_SEQUENCE:
      for (int i = 0; i < tree_stmts(t); i++)
         mark_ineligible_attr_expr(ctx, sigs, tree_stmt(t, i), depth + 1);
      break;
   case T_WAIT:
      if (tree_has_value(t))
         mark_ineligible_attr_expr(ctx, sigs, tree_value(t), depth + 1);
      for (int i = 0; i < tree_triggers(t); i++)
         mark_ineligible_attr_expr(ctx, sigs, tree_trigger(t, i), depth + 1);
      break;
   case T_ASSERT:
      if (tree_has_value(t))
         mark_ineligible_attr_expr(ctx, sigs, tree_value(t), depth + 1);
      // Fallthrough
   case T_REPORT:
      if (tree_has_message(t))
         mark_ineligible_attr_expr(ctx, sigs, tree_message(t), depth + 1);
      if (tree_has_severity(t))
         mark_ineligible_attr_expr(ctx, sigs, tree_severity(t), depth + 1);
      break;
   case T_FCALL:
   case T_PCALL:
   case T_PROT_PCALL:
   case T_PROT_FCALL:
      for (int i = 0; i < tree_params(t); i++)
         mark_ineligible_attr_expr(ctx, sigs, tree_value(tree_param(t, i)),
                                   depth + 1);
      break;
   default:
      break;
   }
}

typedef struct {
   uint32_t *index;
   uint32_t *lowlink;
   bool     *on_stack;
   uint32_t *stack;
   uint32_t  sp;
   uint32_t  next_idx;
   bool    **adj;
   uint32_t  n;
   bool     *in_cycle;
   uint32_t *topo;
   uint32_t  topo_n;
} scc_state_t;

static void tarjan_dfs(scc_state_t *st, uint32_t v)
{
   st->index[v] = st->lowlink[v] = ++st->next_idx;
   st->stack[st->sp++] = v;
   st->on_stack[v] = true;

   for (uint32_t w = 0; w < st->n; w++) {
      if (!st->adj[v][w])
         continue;
      if (st->index[w] == 0) {
         tarjan_dfs(st, w);
         if (st->lowlink[w] < st->lowlink[v])
            st->lowlink[v] = st->lowlink[w];
      }
      else if (st->on_stack[w] && st->index[w] < st->lowlink[v])
         st->lowlink[v] = st->index[w];
   }

   if (st->lowlink[v] == st->index[v]) {
      uint32_t count = 0;
      uint32_t start = st->sp;
      for (;;) {
         uint32_t w = st->stack[--st->sp];
         st->on_stack[w] = false;
         count++;
         if (w == v)
            break;
      }
      if (count > 1 || (count == 1 && st->adj[v][v])) {
         for (uint32_t k = st->sp; k < start; k++)
            st->in_cycle[st->stack[k]] = true;
      }
      else
         st->topo[st->topo_n++] = v;
   }
}

static bool procs_have_write_conflict(const vhdl_iproc_t *a,
                                      const vhdl_iproc_t *b, uint32_t sid)
{
   if (a->kind != b->kind)
      return true;
   if (a->kind == VIPROC_FF) {
      if (a->clk_sig != b->clk_sig || a->rst_sig != b->rst_sig
          || a->clk_posedge != b->clk_posedge
          || a->rst_posedge != b->rst_posedge
          || a->edge_flags != b->edge_flags)
         return true;
   }
   bool a_has = false, b_has = false;
   for (uint32_t i = 0; i < a->n_write_ranges; i++) {
      if (a->write_ranges[i].sig_id != sid)
         continue;
      a_has = true;
      for (uint32_t j = 0; j < b->n_write_ranges; j++) {
         if (b->write_ranges[j].sig_id != sid)
            continue;
         b_has = true;
         if (a->write_ranges[i].pos_lo <= b->write_ranges[j].pos_hi
             && b->write_ranges[j].pos_lo <= a->write_ranges[i].pos_hi)
            return true;
      }
   }
   return !a_has || !b_has;
}

static uint32_t find_root(uint32_t *parent, uint32_t x)
{
   while (parent[x] != x) {
      parent[x] = parent[parent[x]];
      x = parent[x];
   }
   return x;
}

static void union_nodes(uint32_t *parent, uint32_t a, uint32_t b)
{
   uint32_t ra = find_root(parent, a);
   uint32_t rb = find_root(parent, b);
   if (ra != rb)
      parent[rb] = ra;
}

vhdl_island_ctx_t *vhdl_island_analyze(tree_t block, ident_t scope_prefix)
{
   const bool two_state = opt_get_int(OPT_TWO_STATE) >= TWO_STATE_ISLANDS;

   sig_list_t sigs = AINIT;
   int path[VHDL_ISIG_MAX_DEPTH];

   const int nports = tree_ports(block);
   for (int i = 0; i < nports; i++) {
      tree_t p = tree_port(block, i);
      if (tree_class(p) == C_SIGNAL)
         collect_decl_leaves(block, p, tree_type(p), path, 0, two_state, &sigs);
   }

   const int ndecls = tree_decls(block);
   for (int i = 0; i < ndecls; i++) {
      tree_t d = tree_decl(block, i);
      if (tree_kind(d) == T_SIGNAL_DECL)
         collect_decl_leaves(block, d, tree_type(d), path, 0, two_state, &sigs);
      else if (tree_kind(d) == T_IMPLICIT_SIGNAL) {
         // Implicit attributes like 'delayed/'stable/'quiet invalidate target
         for (uint32_t j = 0; j < sigs.count; j++) {
            if (tree_ident(sigs.items[j].decl) != NULL
                && ident_starts_with(tree_ident(d), tree_ident(sigs.items[j].decl)))
               sigs.items[j].ineligible = true;
         }
      }
   }

   const int nstmts = tree_stmts(block);
   uint32_t total_procs = 0;
   for (int i = 0; i < nstmts; i++) {
      if (tree_kind(tree_stmt(block, i)) == T_PROCESS)
         total_procs++;
   }

   if (sigs.count == 0) {
      if (opt_get_int(OPT_DUMP_ISLANDS) && total_procs > 0)
         printf("VHDL_ISLAND_ELAB: %s islands=0 fused_procs=0/%u "
                "sigs=0 state_bytes=0 two_state=0\n",
                istr(scope_prefix), total_procs);
      ACLEAR(sigs);
      return NULL;
   }

   vhdl_island_ctx_t *ctx = xcalloc(sizeof(vhdl_island_ctx_t));
   ctx->block = block;
   ctx->scope_prefix = scope_prefix;
   ctx->sigs = sigs.items;
   ctx->n_sigs = sigs.count;
   ctx->n_block_stmts = nstmts;
   ctx->stmt_fused = xcalloc_array(MAX(1, nstmts), sizeof(bool));

   u32_list_t ext_reads = AINIT;

   // Mark signals referenced by delayed/stable/quiet/transaction attributes
   for (int i = 0; i < nstmts; i++)
      mark_ineligible_attr_expr(ctx, &sigs, tree_stmt(block, i), 0);

   // Scan PSL declarations, child T_BLOCK port maps, and T_PSL_DIRECT
   for (int i = 0; i < ndecls; i++) {
      tree_t d = tree_decl(block, i);
      if (tree_kind(d) == T_PSL_DECL)
         scan_psl_refs(ctx, tree_psl(d), &ext_reads, &sigs, 0);
   }
   for (int i = 0; i < nstmts; i++) {
      tree_t s = tree_stmt(block, i);
      if (tree_kind(s) == T_BLOCK)
         scan_child_block_uses(ctx, s, &ext_reads, &sigs, 0);
      else if (tree_kind(s) == T_PSL_DIRECT)
         scan_unfused_refs(ctx, s, false, &ext_reads, false, &sigs, 0);
   }

   proc_list_t procs = AINIT;
   for (int i = 0; i < nstmts; i++) {
      tree_t s = tree_stmt(block, i);
      if (tree_kind(s) != T_PROCESS)
         continue;
      const unsigned before = procs.count;
      analyze_process(ctx, s, i, two_state, &procs, &ext_reads);
      if (procs.count == before) {
         scan_unfused_refs(ctx, s, false, &ext_reads, true, &sigs, 0);
         if (two_state)
            mark_9state_drivers(ctx, &sigs, s, 0);
      }
   }

   // Iteratively resolve multi-driver conflicts (allowing pairwise-disjoint
   // constant-bit/slice writers of the same vector signal)
   int *driver_count LOCAL = xcalloc_array(ctx->n_sigs, sizeof(int));
   bool changed = true;
   while (changed) {
      changed = false;
      memset(driver_count, 0, ctx->n_sigs * sizeof(int));
      for (unsigned i = 0; i < procs.count; i++) {
         if (procs.items[i].rejected)
            continue;
         for (uint32_t j = 0; j < procs.items[i].n_writes; j++)
            driver_count[procs.items[i].writes[j]]++;
      }
      for (unsigned i = 0; i < procs.count; i++) {
         vhdl_iproc_t *p = &(procs.items[i]);
         if (p->rejected)
            continue;
         bool conflict = false;
         for (uint32_t j = 0; j < p->n_writes; j++) {
            const uint32_t sid = p->writes[j];
            if (sigs.items[sid].has_ext_driver || sigs.items[sid].ineligible) {
               conflict = true;
               break;
            }
            if (driver_count[sid] > 1) {
               if (!sigs.items[sid].is_vector || sigs.items[sid].is_array2d) {
                  conflict = true;
                  break;
               }
               for (unsigned k = 0; k < procs.count; k++) {
                  if (k == i || procs.items[k].rejected)
                     continue;
                  bool k_writes = false;
                  for (uint32_t kw = 0; kw < procs.items[k].n_writes; kw++) {
                     if (procs.items[k].writes[kw] == sid) {
                        k_writes = true;
                        break;
                     }
                  }
                  if (k_writes
                      && procs_have_write_conflict(p, &(procs.items[k]), sid)) {
                     conflict = true;
                     break;
                  }
               }
               if (conflict)
                  break;
            }
         }
         bool read_9state = false;
         for (uint32_t j = 0; j < p->n_reads && !conflict; j++) {
            const uint32_t rsid = p->reads[j];
            if (sigs.items[rsid].ineligible)
               conflict = true;
            else if (two_state && sigs.items[rsid].has_9state_driver) {
               conflict = true;
               read_9state = true;
            }
         }
         if (conflict) {
            p->rejected = true;
            changed = true;
            if (read_9state && p->kind == VIPROC_COMB) {
               for (uint32_t j = 0; j < p->n_writes; j++)
                  sigs.items[p->writes[j]].has_9state_driver = true;
            }
         }
      }
   }

   // Tarjan SCC on remaining comb processes to reject cycles & topo-sort DAG
   u32_list_t comb_idxs = AINIT;
   for (unsigned i = 0; i < procs.count; i++) {
      if (!procs.items[i].rejected && procs.items[i].kind == VIPROC_COMB)
         APUSH(comb_idxs, i);
   }

   u32_list_t topo_comb_procs = AINIT;
   if (comb_idxs.count > 0) {
      const uint32_t nc = comb_idxs.count;
      u32_list_t *writers_comb LOCAL = xcalloc_array(ctx->n_sigs,
                                                     sizeof(u32_list_t));
      for (uint32_t c = 0; c < nc; c++) {
         const vhdl_iproc_t *p = &(procs.items[comb_idxs.items[c]]);
         for (uint32_t j = 0; j < p->n_writes; j++)
            APUSH(writers_comb[p->writes[j]], c);
      }

      bool **adj LOCAL = xcalloc_array(nc, sizeof(bool *));
      for (uint32_t c = 0; c < nc; c++)
         adj[c] = xcalloc_array(nc, sizeof(bool));

      for (uint32_t c = 0; c < nc; c++) {
         const vhdl_iproc_t *p = &(procs.items[comb_idxs.items[c]]);
         for (uint32_t j = 0; j < p->n_reads; j++) {
            const u32_list_t *wl = &(writers_comb[p->reads[j]]);
            for (unsigned wi = 0; wi < wl->count; wi++)
               adj[c][wl->items[wi]] = true;  // c depends on w: visit w first
         }
      }
      for (uint32_t s = 0; s < ctx->n_sigs; s++)
         ACLEAR(writers_comb[s]);

      scc_state_t st = {
         .index    = xcalloc_array(nc, sizeof(uint32_t)),
         .lowlink  = xcalloc_array(nc, sizeof(uint32_t)),
         .on_stack = xcalloc_array(nc, sizeof(bool)),
         .stack    = xmalloc_array(nc, sizeof(uint32_t)),
         .adj      = adj,
         .n        = nc,
         .in_cycle = xcalloc_array(nc, sizeof(bool)),
         .topo     = xmalloc_array(nc, sizeof(uint32_t)),
      };
      for (uint32_t c = 0; c < nc; c++) {
         if (st.index[c] == 0)
            tarjan_dfs(&st, c);
      }
      for (uint32_t c = 0; c < nc; c++) {
         if (st.in_cycle[c])
            procs.items[comb_idxs.items[c]].rejected = true;
      }
      for (uint32_t i = 0; i < st.topo_n; i++) {
         uint32_t c = st.topo[i];
         if (!procs.items[comb_idxs.items[c]].rejected)
            APUSH(topo_comb_procs, comb_idxs.items[c]);
      }

      for (uint32_t c = 0; c < nc; c++)
         free(adj[c]);
      free(st.index);
      free(st.lowlink);
      free(st.on_stack);
      free(st.stack);
      free(st.in_cycle);
      free(st.topo);
   }
   ACLEAR(comb_idxs);

   // Record reads of rejected processes as ext_reads so boundary signals stay visible
   for (unsigned i = 0; i < procs.count; i++) {
      if (procs.items[i].rejected)
         scan_unfused_refs(ctx, procs.items[i].stmt, false, &ext_reads,
                           false, &sigs, 0);
   }

   // Record unique driver for accepted processes
   for (unsigned i = 0; i < procs.count; i++) {
      const vhdl_iproc_t *p = &(procs.items[i]);
      if (p->rejected)
         continue;
      for (uint32_t j = 0; j < p->n_writes; j++)
         ctx->sigs[p->writes[j]].driver_proc = (int)i;
   }

   // Count surviving accepted processes
   uint32_t n_live = 0;
   for (unsigned i = 0; i < procs.count; i++) {
      if (!procs.items[i].rejected)
         n_live++;
   }

   if (n_live == 0) {
      if (opt_get_int(OPT_DUMP_ISLANDS) && total_procs > 0)
         printf("VHDL_ISLAND_ELAB: %s islands=0 fused_procs=0/%u "
                "sigs=%u state_bytes=0 two_state=0\n",
                istr(scope_prefix), total_procs, ctx->n_sigs);
      ACLEAR(topo_comb_procs);
      ctx->procs = procs.items;
      ctx->n_procs = procs.count;
      ctx->ext_read_sigs = ext_reads.items;
      ctx->n_ext_read_sigs = ext_reads.count;
      return ctx;
   }

   // Union-Find over live processes connected by shared internal signals
   // (Wait: also connect processes if signal is NOT ext_read/port/child_read,
   //  AND group same-clock FFs when connected or keep each connected component together!)
   uint32_t *uf_parent LOCAL = xmalloc_array(procs.count, sizeof(uint32_t));
   for (unsigned i = 0; i < procs.count; i++)
      uf_parent[i] = i;

   int *sig_first_user LOCAL = xmalloc_array(ctx->n_sigs, sizeof(int));
   for (uint32_t i = 0; i < ctx->n_sigs; i++)
      sig_first_user[i] = -1;

   for (unsigned i = 0; i < procs.count; i++) {
      const vhdl_iproc_t *p = &(procs.items[i]);
      if (p->rejected)
         continue;
      for (uint32_t j = 0; j < p->n_writes; j++) {
         const uint32_t sid = p->writes[j];
         if (sig_first_user[sid] < 0)
            sig_first_user[sid] = (int)i;
         else
            union_nodes(uf_parent, (uint32_t)sig_first_user[sid], i);
      }
      for (uint32_t j = 0; j < p->n_reads; j++) {
         const uint32_t sid = p->reads[j];
         // Merge producer & internal consumers into one island if driven locally
         if (ctx->sigs[sid].driver_proc >= 0) {
            if (sig_first_user[sid] < 0)
               sig_first_user[sid] = (int)i;
            else
               union_nodes(uf_parent, (uint32_t)sig_first_user[sid], i);
         }
      }
   }

   // Coalesce same-clock/reset-domain FF processes in this block into one island
   // to eliminate small-island boundary & domain dispatch churn.
   for (unsigned i = 0; i < procs.count; i++) {
      const vhdl_iproc_t *pi = &(procs.items[i]);
      if (pi->rejected || pi->kind != VIPROC_FF)
         continue;
      for (unsigned j = i + 1; j < procs.count; j++) {
         const vhdl_iproc_t *pj = &(procs.items[j]);
         if (pj->rejected || pj->kind != VIPROC_FF)
            continue;
         if (pi->clk_sig == pj->clk_sig && pi->rst_sig == pj->rst_sig
             && pi->clk_posedge == pj->clk_posedge
             && pi->rst_posedge == pj->rst_posedge
             && pi->edge_flags == pj->edge_flags) {
            union_nodes(uf_parent, i, j);
            break;
         }
      }
   }

   // Prune unprofitable 1-process combinational-only islands (zero internal
   // signals: every input & output crosses the island boundary, so native NVC
   // fast-driver execution avoids watch/sync/deposit churn).
   uint32_t *root_comb_cnt LOCAL = xcalloc_array(procs.count, sizeof(uint32_t));
   uint32_t *root_ff_cnt LOCAL = xcalloc_array(procs.count, sizeof(uint32_t));
   for (unsigned i = 0; i < procs.count; i++) {
      if (procs.items[i].rejected)
         continue;
      uint32_t r = find_root(uf_parent, i);
      if (procs.items[i].kind == VIPROC_FF)
         root_ff_cnt[r]++;
      else
         root_comb_cnt[r]++;
   }
   for (unsigned i = 0; i < procs.count; i++) {
      vhdl_iproc_t *p = &(procs.items[i]);
      if (p->rejected)
         continue;
      uint32_t r = find_root(uf_parent, i);
      if (root_ff_cnt[r] == 0 && root_comb_cnt[r] == 1) {
         p->rejected = true;
         n_live--;
         for (uint32_t j = 0; j < p->n_writes; j++)
            ctx->sigs[p->writes[j]].driver_proc = -1;
         scan_unfused_refs(ctx, p->stmt, false, &ext_reads, false, &sigs, 0);
      }
   }

   if (n_live == 0) {
      if (opt_get_int(OPT_DUMP_ISLANDS) && total_procs > 0)
         printf("VHDL_ISLAND_ELAB: %s islands=0 fused_procs=0/%u "
                "sigs=%u state_bytes=0 two_state=0\n",
                istr(scope_prefix), total_procs, ctx->n_sigs);
      ACLEAR(topo_comb_procs);
      ctx->procs = procs.items;
      ctx->n_procs = procs.count;
      ctx->ext_read_sigs = ext_reads.items;
      ctx->n_ext_read_sigs = ext_reads.count;
      return ctx;
   }

   // Map UF roots to island indices
   int *root_to_isl LOCAL = xmalloc_array(procs.count, sizeof(int));
   for (unsigned i = 0; i < procs.count; i++)
      root_to_isl[i] = -1;

   island_list_t islands = AINIT;
   for (unsigned i = 0; i < procs.count; i++) {
      vhdl_iproc_t *p = &(procs.items[i]);
      if (p->rejected)
         continue;
      uint32_t r = find_root(uf_parent, i);
      if (root_to_isl[r] < 0) {
         vhdl_island_t isl = {
            .id        = islands.count,
            .two_state = two_state,
         };
         root_to_isl[r] = (int)islands.count;
         APUSH(islands, isl);
      }
      p->island_idx = root_to_isl[r];
      ctx->stmt_fused[p->block_stmt_idx] = true;
   }

   // Populate per-island signals, topo-sorted comb procs, and FF domains
   int32_t *gid_to_loc LOCAL = xmalloc_array(ctx->n_sigs, sizeof(int32_t));
   for (uint32_t i = 0; i < ctx->n_sigs; i++)
      gid_to_loc[i] = -1;

   enum {
      LOC_COMB_READ    = 1u << 0,
      LOC_COMB_WRITTEN = 1u << 1,
      LOC_FF_READ      = 1u << 2,
      LOC_FF_WRITTEN   = 1u << 3,
      LOC_USED_EDGE    = 1u << 4,
   };

   for (uint32_t ii = 0; ii < islands.count; ii++) {
      vhdl_island_t *isl = &(islands.items[ii]);
      u32_list_t isl_sig_ids = AINIT;
      u32_list_t isl_comb = AINIT;
      A(vhdl_idom_t) doms = AINIT;

      // Add comb procs in global topo order
      for (unsigned k = 0; k < topo_comb_procs.count; k++) {
         uint32_t pi = topo_comb_procs.items[k];
         if (procs.items[pi].island_idx == (int)ii)
            APUSH(isl_comb, pi);
      }

      // Collect signals touched by processes in this island
      for (unsigned i = 0; i < procs.count; i++) {
         const vhdl_iproc_t *p = &(procs.items[i]);
         if (p->island_idx != (int)ii)
            continue;
         for (uint32_t j = 0; j < p->n_reads; j++)
            push_unique_u32(&isl_sig_ids, p->reads[j]);
         for (uint32_t j = 0; j < p->n_writes; j++)
            push_unique_u32(&isl_sig_ids, p->writes[j]);
         if (p->kind == VIPROC_FF) {
            push_unique_u32(&isl_sig_ids, p->clk_sig);
            if (p->rst_sig >= 0)
               push_unique_u32(&isl_sig_ids, (uint32_t)p->rst_sig);
         }
      }

      vhdl_isl_sig_t *isl_sigs = xcalloc_array(isl_sig_ids.count,
                                               sizeof(vhdl_isl_sig_t));
      uint8_t *loc_flags LOCAL = xcalloc_array(isl_sig_ids.count,
                                               sizeof(uint8_t));

      // Compute per-island state offsets
      uint32_t off = 0;
      for (unsigned j = 0; j < isl_sig_ids.count; j++) {
         const uint32_t gid = isl_sig_ids.items[j];
         const vhdl_isig_t *s = &(ctx->sigs[gid]);
         gid_to_loc[gid] = (int32_t)j;
         isl_sigs[j].sig_id = gid;
         isl_sigs[j].a_off = off;
         isl_sigs[j].b_off = UINT32_MAX;
         if (s->two_state) {
            const uint32_t nwords = (s->width + 63u) / 64u;
            off += nwords * s->nelems * 8u;
         }
         else {
            const uint32_t nbytes = s->width * s->nelems * s->byte_size;
            off += ALIGN_UP(nbytes, 8u);
         }
      }
      isl->state_bytes = off;

      for (unsigned i = 0; i < procs.count; i++) {
         vhdl_iproc_t *p = &(procs.items[i]);
         if (p->island_idx != (int)ii)
            continue;
         for (uint32_t j = 0; j < p->n_reads; j++) {
            const int32_t loc = gid_to_loc[p->reads[j]];
            assert(loc >= 0);
            loc_flags[loc] |= (p->kind == VIPROC_COMB)
               ? LOC_COMB_READ : LOC_FF_READ;
         }
         for (uint32_t j = 0; j < p->n_writes; j++) {
            const int32_t loc = gid_to_loc[p->writes[j]];
            assert(loc >= 0);
            loc_flags[loc] |= (p->kind == VIPROC_COMB)
               ? LOC_COMB_WRITTEN : LOC_FF_WRITTEN;
         }

         if (p->kind == VIPROC_FF) {
            const int32_t clk_l = gid_to_loc[p->clk_sig];
            const int32_t rst_l = (p->rst_sig >= 0)
               ? gid_to_loc[p->rst_sig] : -1;
            assert(clk_l >= 0);
            loc_flags[clk_l] |= LOC_USED_EDGE;
            if (rst_l >= 0)
               loc_flags[rst_l] |= LOC_USED_EDGE;

            int found = -1;
            for (unsigned d = 0; d < doms.count; d++) {
               const vhdl_idom_t *dm = &(doms.items[d]);
               if (dm->clk_isl_sig == (uint32_t)clk_l
                   && dm->rst_isl_sig == rst_l
                   && dm->clk_posedge == p->clk_posedge
                   && dm->rst_posedge == p->rst_posedge
                   && dm->edge_flags == p->edge_flags) {
                  found = (int)d;
                  break;
               }
            }
            if (found < 0) {
               vhdl_idom_t dm = {
                  .clk_isl_sig = (uint32_t)clk_l,
                  .rst_isl_sig = rst_l,
                  .clk_posedge = p->clk_posedge,
                  .rst_posedge = p->rst_posedge,
                  .edge_flags  = p->edge_flags,
                  .fn_name     = ident_sprintf("%s.$vhdl_island_ff_%u_%u",
                                               istr(scope_prefix), ii,
                                               (unsigned)doms.count),
               };
               found = (int)doms.count;
               APUSH(doms, dm);
            }
            p->dom_idx = found;
            vhdl_idom_t *dm = &(doms.items[found]);

            // Append proc_idx and write_isl_sigs
            dm->proc_idxs = xrealloc_array(dm->proc_idxs, dm->n_procs + 1,
                                           sizeof(uint32_t));
            dm->proc_idxs[dm->n_procs++] = i;

            for (uint32_t j = 0; j < p->n_writes; j++) {
               const uint32_t w_isl = (uint32_t)gid_to_loc[p->writes[j]];
               bool seen = false;
               for (uint32_t k = 0; k < dm->n_write_sigs; k++) {
                  if (dm->write_isl_sigs[k] == w_isl) {
                     seen = true;
                     break;
                  }
               }
               if (!seen) {
                  dm->write_isl_sigs = xrealloc_array(dm->write_isl_sigs,
                                                      dm->n_write_sigs + 1,
                                                      sizeof(uint32_t));
                  dm->write_isl_sigs[dm->n_write_sigs++] = w_isl;
               }
            }
         }
      }

      // Compute runtime boundary flags for each signal in this island
      for (unsigned j = 0; j < isl_sig_ids.count; j++) {
         const uint32_t gid = isl_sig_ids.items[j];
         const vhdl_isig_t *s = &(ctx->sigs[gid]);
         const uint8_t lf = loc_flags[j];
         uint32_t f = s->two_state ? RT_ISIG_TWO_STATE : RT_ISIG_STD_LOGIC9;
         if (s->elem_kind == VISIG_STD_LOGIC)
            f |= RT_ISIG_STD_LOGIC_ELEM;
         if (lf & LOC_COMB_READ)
            f |= RT_ISIG_COMB_READ;
         if (lf & LOC_COMB_WRITTEN)
            f |= RT_ISIG_COMB_WRITTEN;
         if (lf & LOC_FF_WRITTEN)
            f |= RT_ISIG_FF_WRITTEN;

         const bool is_driven_here = !!(lf & (LOC_COMB_WRITTEN | LOC_FF_WRITTEN));
         const bool edge_here = !!(lf & LOC_USED_EDGE);
         if (!is_driven_here || edge_here)
            f |= RT_ISIG_IN;

         const bool is_port_out = s->is_port
            && (s->port_mode == PORT_OUT || s->port_mode == PORT_BUFFER
                || s->port_mode == PORT_INOUT);
         if (is_driven_here
             && (is_port_out || s->child_read || s->ext_read || edge_here))
            f |= RT_ISIG_OUT;

         isl_sigs[j].rt_flags = f;
         gid_to_loc[gid] = -1;
      }

      isl->sigs = isl_sigs;
      isl->n_sigs = isl_sig_ids.count;
      ACLEAR(isl_sig_ids);
      isl->comb_procs = isl_comb.items;
      isl->n_comb_procs = isl_comb.count;
      if (isl->n_comb_procs > 0)
         isl->eval_name = ident_sprintf("%s.$vhdl_island_comb_%u",
                                        istr(scope_prefix), ii);
      isl->doms = doms.items;
      isl->n_doms = doms.count;
   }

   ACLEAR(topo_comb_procs);

   ctx->procs = procs.items;
   ctx->n_procs = procs.count;
   ctx->islands = islands.items;
   ctx->n_islands = islands.count;
   ctx->ext_read_sigs = ext_reads.items;
   ctx->n_ext_read_sigs = ext_reads.count;

   if (opt_get_int(OPT_DUMP_ISLANDS)) {
      uint32_t total_state = 0, two_cnt = 0;
      for (uint32_t i = 0; i < ctx->n_islands; i++) {
         total_state += ctx->islands[i].state_bytes;
         for (uint32_t j = 0; j < ctx->islands[i].n_sigs; j++) {
            if (ctx->sigs[ctx->islands[i].sigs[j].sig_id].two_state)
               two_cnt++;
         }
      }
      printf("VHDL_ISLAND_ELAB: %s islands=%u fused_procs=%u/%u "
             "sigs=%u state_bytes=%u two_state=%u\n",
             istr(scope_prefix), ctx->n_islands, n_live, total_procs,
             ctx->n_sigs, total_state, two_cnt);
   }

   return ctx;
}

void vhdl_island_ctx_free(vhdl_island_ctx_t *ctx)
{
   if (ctx == NULL)
      return;

   for (uint32_t i = 0; i < ctx->n_procs; i++) {
      free(ctx->procs[i].reads);
      free(ctx->procs[i].writes);
      free(ctx->procs[i].write_ranges);
   }
   free(ctx->procs);

   for (uint32_t i = 0; i < ctx->n_islands; i++) {
      vhdl_island_t *isl = &(ctx->islands[i]);
      free(isl->sigs);
      free(isl->comb_procs);
      for (uint32_t d = 0; d < isl->n_doms; d++) {
         free(isl->doms[d].proc_idxs);
         free(isl->doms[d].write_isl_sigs);
      }
      free(isl->doms);
   }
   free(ctx->islands);
   free(ctx->sigs);
   free(ctx->ext_read_sigs);
   free(ctx->stmt_fused);
   free(ctx);
}

bool vhdl_island_stmt_fused(const vhdl_island_ctx_t *ctx, int stmt_idx)
{
   if (ctx == NULL || stmt_idx < 0 || stmt_idx >= ctx->n_block_stmts)
      return false;
   return ctx->stmt_fused[stmt_idx];
}
