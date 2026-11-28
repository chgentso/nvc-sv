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
#include "mir/mir-node.h"
#include "mir/mir-unit.h"
#include "option.h"
#include "rt/assert.h"
#include "rt/island.h"
#include "thread.h"
#include "tree.h"
#include "type.h"
#include "vcode.h"
#include "vhdl/vhdl-island-lower.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
   VIVAL_NONE = 0,
   VIVAL_VEC,      // Mode A: MIR_TYPE_VEC2; Mode B: pointer to uint8_t[width]
   VIVAL_BIT,      // Scalar std_logic/bit/boolean: Mode A: VEC2(1), Mode B: u8 (0..8)
   VIVAL_INT,      // MIR_TYPE_INT (i64 or narrower integer/enum)
} vival_kind_t;

typedef struct {
   vival_kind_t kind;
   mir_value_t  val;
   uint32_t     width;
   bool         is_signed;
   bool         is_std;     // true if IEEE std_ulogic element
   bool         is_bool;
   range_kind_t dir;
   int64_t      left;
   int64_t      right;
} vi_val_t;

typedef struct {
   int          path[VHDL_ISIG_MAX_DEPTH];
   uint8_t      path_len;
   type_t       type;
   bool         is_array2d;
   bool         is_vec;
   bool         is_std;
   bool         is_bool;
   bool         is_signed;
   bool         is_int;
   uint32_t     width;
   uint32_t     nelems;
   range_kind_t dir;
   int64_t      left;
   int64_t      right;
   range_kind_t outer_dir;
   int64_t      outer_left;
   int64_t      outer_right;
   mir_value_t  storage;
} vi_rec_leaf_t;

typedef struct vi_local {
   tree_t         decl;
   bool           is_rec;
   type_t         rec_type;
   bool           is_array2d;
   bool           is_vec;
   bool           is_std;
   bool           is_bool;
   bool           is_signed;
   bool           is_int;
   uint32_t       width;
   uint32_t       nelems;
   range_kind_t   dir;
   int64_t        left;
   int64_t        right;
   range_kind_t   outer_dir;
   int64_t        outer_left;
   int64_t        outer_right;
   mir_value_t    storage;    // Stack alloc pointer or param value
   bool           by_val;     // True if param is passed as direct vi_val_t
   vi_val_t       pval;
   bool           has_const_int;
   int64_t        const_int;
   // Flattened leaves for local record variables / record params
   vi_rec_leaf_t *rec_leaves;
   uint32_t       n_rec_leaves;
} vi_local_t;

typedef struct vi_scope {
   struct vi_scope *parent;
   A(vi_local_t)    locals;
   mir_block_t      ret_bb;
   mir_value_t      ret_slot;
   type_t           ret_type;
   bool             has_ret_val;
   bool             ret_is_vec;
   bool             ret_ptr_indirect;
   uint32_t         ret_w;
   bool             ret_signed;
   range_kind_t     ret_dir;
   int64_t          ret_left;
   int64_t          ret_right;
   mir_block_t      loop_next_bb;
   mir_block_t      loop_exit_bb;
   struct vi_local *ret_rec;   // Record result of an inlined function
} vi_scope_t;

typedef struct {
   mir_unit_t              *mu;
   const vhdl_island_ctx_t *ctx;
   const vhdl_island_t     *isl;
   mir_value_t              state_ptr;
   bool                     two_state;
   bool                     is_ff;
   uint32_t                 ff_stride;
   const vhdl_idom_t       *ff_dom;
   mir_value_t              ff_dirty_var;
   vi_scope_t              *scope;
   mir_block_t              const_bb;  // Entry block: shared tables
   mir_block_t              body_bb;   // First block after const_bb
   mir_value_t              lut_not;
   mir_value_t              lut_and;
   mir_value_t              lut_or;
   mir_value_t              lut_xor;
   struct {
      tree_t      agg;
      mir_value_t ptr;
   } rom_cache[16];
   unsigned                 n_rom_cache;
   uint32_t                *act_rd;    // island signals read by the process
   uint32_t                 n_act_rd;
   uint32_t                 cap_act_rd;
   bool                     act_track;
   const loc_t             *loc;           // Of the current statement
   unsigned                 inline_depth;  // Inlined subprogram bodies
   bool                     failed;
   const char              *reason;    // NVC_DUMP_ISLANDS: first failure
   // Speculative Mode A variant of a Mode B island (NVC_ISLAND_SPEC): a
   // value two-state storage cannot hold makes the function return a
   // deopt code (rt_island_deopt_t) instead
   bool                     spec;
   bool                     spec_checks;   // Held bytes may be set ($ah)
   mir_value_t              spec_var;
   // Signals the current combinational process reads and also writes:
   // reads load the copy taken when the process started, the value of the
   // current delta cycle (see vi_emit_self_shadow)
   const vhdl_isl_sig_t   **shadow_sig;
   mir_value_t             *shadow_ptr;
   uint32_t                 n_shadow;
   // Signals of a combinational loop: copied when the island function
   // starts, read by the processes that run after one of their writers
   const vhdl_isl_sig_t   **entry_sig;
   mir_value_t             *entry_ptr;
   uint32_t                 n_entry;
   // Speculative Mode A: signals the current combinational process that
   // may warn writes, copied when it starts and stored back by a deopt
   // inside it (see vi_emit_restore_copies)
   const vhdl_isl_sig_t   **restore_sig;
   mir_value_t             *restore_ptr;
   uint32_t                *restore_bytes;
   uint32_t                 n_restore;
   mir_value_t              restore_cond;   // The copies were taken
   // Loop passes lowered one by one so far (see vi_loop_wants_unroll)
   uint32_t                 unrolled;
   struct vi_const_snap    *case_snap;
} vi_lower_t;

static void vi_generic_reason(vi_lower_t *l, const char *what, tree_t t)
{
   if (l->reason != NULL || t == NULL || !opt_get_int(OPT_DUMP_ISLANDS))
      return;
   const tree_kind_t k = tree_kind(t);
   const unsigned line = tree_loc(t)->first_line;
   if (k == T_FCALL || k == T_PCALL)
      l->reason = istr(ident_sprintf("lower %s %s %s line %u", what,
                                     tree_kind_str(k), istr(tree_ident(t)),
                                     line));
   else
      l->reason = istr(ident_sprintf("lower %s %s line %u", what,
                                     tree_kind_str(k), line));
}

static bool fn_name_is(const char *rest, const char *target)
{
   const size_t len = strlen(target);
   return strncmp(rest, target, len) == 0
      && (rest[len] == '(' || rest[len] == '[' || rest[len] == '\0');
}

static const uint8_t STD_NOT_TBL[9] = { 0, 1, 3, 2, 1, 1, 3, 2, 1 };

static const uint8_t STD_AND_TBL[81] = {
   0,0,2,0,0,0,2,0,0,
   0,1,2,1,1,1,2,1,1,
   2,2,2,2,2,2,2,2,2,
   0,1,2,3,1,1,2,3,1,
   0,1,2,1,1,1,2,1,1,
   0,1,2,1,1,1,2,1,1,
   2,2,2,2,2,2,2,2,2,
   0,1,2,3,1,1,2,3,1,
   0,1,2,1,1,1,2,1,1,
};

static const uint8_t STD_OR_TBL[81] = {
   0,0,0,3,0,0,0,3,0,
   0,1,1,3,1,1,1,3,1,
   0,1,2,3,1,1,2,3,1,
   3,3,3,3,3,3,3,3,3,
   0,1,1,3,1,1,1,3,1,
   0,1,1,3,1,1,1,3,1,
   0,1,2,3,1,1,2,3,1,
   3,3,3,3,3,3,3,3,3,
   0,1,1,3,1,1,1,3,1,
};

static const uint8_t STD_XOR_TBL[81] = {
   0,0,0,0,0,0,0,0,0,
   0,1,1,1,1,1,1,1,1,
   0,1,2,3,1,1,2,3,1,
   0,1,3,2,1,1,3,2,1,
   0,1,1,1,1,1,1,1,1,
   0,1,1,1,1,1,1,1,1,
   0,1,2,3,1,1,2,3,1,
   0,1,3,2,1,1,3,2,1,
   0,1,1,1,1,1,1,1,1,
};

static inline mir_type_t vi_u8_type(mir_unit_t *mu)
{
   return mir_int_type(mu, 0, 255);
}

static inline mir_type_t vi_i64_type(mir_unit_t *mu)
{
   return mir_int_type(mu, INT64_MIN, INT64_MAX);
}

static inline mir_value_t vi_off(mir_unit_t *mu, int64_t v)
{
   return mir_const(mu, mir_offset_type(mu), v);
}

static inline mir_value_t vi_i64(mir_unit_t *mu, int64_t v)
{
   return mir_const(mu, vi_i64_type(mu), v);
}

static inline mir_value_t vi_u8(mir_unit_t *mu, uint8_t v)
{
   return mir_const(mu, vi_u8_type(mu), v);
}

// A bit/boolean array element is a byte holding 0 or 1; the MIR logical
// operators only take booleans
static inline mir_value_t vi_u8_to_bool(mir_unit_t *mu, mir_value_t v)
{
   return mir_build_cmp(mu, MIR_CMP_NEQ, v, vi_u8(mu, 0));
}

static inline mir_value_t vi_u8_from_bool(mir_unit_t *mu, mir_value_t b)
{
   return mir_build_select(mu, vi_u8_type(mu), b, vi_u8(mu, 1), vi_u8(mu, 0));
}

static mir_value_t vi_to_off(mir_unit_t *mu, mir_value_t v)
{
   if (mir_is_offset(mu, v))
      return v;
   return mir_build_cast(mu, mir_offset_type(mu), v);
}

static mir_value_t vi_to_i64(mir_unit_t *mu, mir_value_t v)
{
   if (mir_equals(mir_get_type(mu, v), vi_i64_type(mu)))
      return v;
   return mir_build_cast(mu, vi_i64_type(mu), v);
}

static mir_value_t vi_const_vec2(mir_unit_t *mu, int width, bool is_signed,
                                 const uint64_t *words)
{
   mir_type_t vt = mir_vec2_type(mu, width, is_signed);
   if (width <= 64) {
      uint64_t w0 = words != NULL ? words[0] : 0;
      if (width < 64)
         w0 &= (UINT64_C(1) << width) - 1u;
      return mir_const_vec(mu, vt, w0, 0);
   }

   mir_value_t acc = mir_const_vec(mu, mir_vec2_type(mu, 64, false),
                                   words != NULL ? words[0] : 0, 0);
   acc = mir_build_cast(mu, vt, acc);

   const int nw = (width + 63) / 64;
   for (int i = 1; i < nw; i++) {
      uint64_t wi = words != NULL ? words[i] : 0;
      if (wi == 0)
         continue;
      int cw = MIN(64, width - i * 64);
      if (cw < 64)
         wi &= (UINT64_C(1) << cw) - 1u;
      mir_value_t chunk = mir_const_vec(mu, mir_vec2_type(mu, cw, false), wi, 0);
      int msb_pos = width - i * 64 - cw;
      acc = mir_build_insert(mu, chunk, acc, vi_off(mu, msb_pos));
   }
   return acc;
}

static mir_value_t vi_const_vec2_rep(mir_unit_t *mu, int width, bool is_signed,
                                     int bit_val)
{
   if (bit_val == 0)
      return vi_const_vec2(mu, width, is_signed, NULL);

   const int nw = (width + 63) / 64;
   uint64_t *w LOCAL = xmalloc_array(nw, sizeof(uint64_t));
   for (int i = 0; i < nw; i++)
      w[i] = ~UINT64_C(0);
   return vi_const_vec2(mu, width, is_signed, w);
}

static inline uint8_t std_enum_to_2bit(unsigned pos)
{
   // 0='U',1='X',2='0',3='1',4='Z',5='W',6='L',7='H',8='-'
   return (pos == 3 || pos == 7) ? 1u : 0u;
}

static vi_local_t *vi_find_local(vi_lower_t *l, tree_t decl);
static bool vi_eval_type_bounds(vi_lower_t *l, type_t t,
                                range_kind_t *dir_out, int64_t *left_out,
                                int64_t *right_out, uint32_t *width_out);

static bool vi_eval_const_int_r(vi_lower_t *l, tree_t expr, int64_t *out,
                                int depth)
{
   if (expr == NULL || depth > 16)
      return false;

   if (vhdl_island_eval_const_int_ctx(l->ctx, expr, out))
      return true;

   switch (tree_kind(expr)) {
   case T_REF:
      {
         tree_t d = tree_ref(expr);
         if (d == NULL)
            return false;
         vi_local_t *loc = vi_find_local(l, d);
         if (loc != NULL && loc->has_const_int) {
            *out = loc->const_int;
            return true;
         }
         if (tree_kind(d) == T_CONST_DECL && tree_has_value(d))
            return vi_eval_const_int_r(l, tree_value(d), out, depth + 1);
         return false;
      }

   case T_QUALIFIED:
   case T_TYPE_CONV:
      return vi_eval_const_int_r(l, tree_value(expr), out, depth + 1);

   case T_ATTR_REF:
      {
         const attr_kind_t ak = tree_subkind(expr);
         if (ak != ATTR_LENGTH && ak != ATTR_LEFT && ak != ATTR_RIGHT
             && ak != ATTR_LOW && ak != ATTR_HIGH)
            return false;
         int64_t dim = 1;
         if (tree_params(expr) > 0) {
            if (tree_params(expr) != 1
                || !vi_eval_const_int_r(l, tree_value(tree_param(expr, 0)),
                                        &dim, depth + 1)
                || dim != 1)
               return false;
         }
         tree_t nm = tree_name(expr);
         range_kind_t dir = RANGE_DOWNTO;
         int64_t lv = 0, rv = 0;
         uint32_t w = 0;
         bool have = false;
         if (tree_kind(nm) == T_REF) {
            vi_local_t *loc = vi_find_local(l, tree_ref(nm));
            if (loc != NULL && loc->is_vec) {
               dir = loc->dir;
               lv = loc->left;
               rv = loc->right;
               w = loc->width;
               have = true;
            }
         }
         if (!have && nm != NULL && tree_type(nm) != NULL
             && vi_eval_type_bounds(l, tree_type(nm), &dir, &lv, &rv, &w))
            have = true;
         if (!have)
            return false;
         switch (ak) {
         case ATTR_LENGTH: *out = (int64_t)w; return true;
         case ATTR_LEFT:   *out = lv; return true;
         case ATTR_RIGHT:  *out = rv; return true;
         case ATTR_LOW:    *out = (dir == RANGE_TO) ? lv : rv; return true;
         case ATTR_HIGH:   *out = (dir == RANGE_TO) ? rv : lv; return true;
         default:          return false;
         }
      }

   case T_FCALL:
      {
         tree_t decl = tree_ref(expr);
         const subprogram_kind_t sk = tree_subkind(decl);
         if (tree_params(expr) == 1) {
            int64_t a;
            if (!vi_eval_const_int_r(l, tree_value(tree_param(expr, 0)),
                                     &a, depth + 1))
               return false;
            switch (sk) {
            case S_IDENTITY: *out = a; return true;
            case S_NEGATE:   if (a == INT64_MIN) return false; *out = -a; return true;
            case S_ABS:      if (a == INT64_MIN) return false; *out = (a < 0) ? -a : a; return true;
            default:         return false;
            }
         }
         else if (tree_params(expr) == 2) {
            int64_t a, b;
            if (!vi_eval_const_int_r(l, tree_value(tree_param(expr, 0)),
                                     &a, depth + 1)
                || !vi_eval_const_int_r(l, tree_value(tree_param(expr, 1)),
                                        &b, depth + 1))
               return false;
            switch (sk) {
            case S_ADD:     *out = a + b; return true;
            case S_SUB:     *out = a - b; return true;
            case S_MUL:     *out = a * b; return true;
            case S_DIV:     if (b == 0 || (a == INT64_MIN && b == -1)) return false; *out = a / b; return true;
            case S_REM:     if (b == 0 || (a == INT64_MIN && b == -1)) return false; *out = a % b; return true;
            case S_MOD:
               if (b == 0 || (a == INT64_MIN && b == -1))
                  return false;
               {
                  int64_t r = a % b;
                  if (r != 0 && ((r < 0) != (b < 0)))
                     r += b;
                  *out = r;
               }
               return true;
            case S_EXP:
               if (b < 0 || b > 62)
                  return false;
               {
                  int64_t acc = 1;
                  for (int64_t i = 0; i < b; i++) {
                     if (a != 0 && llabs(acc) > INT64_MAX / llabs(a))
                        return false;
                     acc *= a;
                  }
                  *out = acc;
               }
               return true;
            case S_MINIMUM: *out = MIN(a, b); return true;
            case S_MAXIMUM: *out = MAX(a, b); return true;
            default:        return false;
            }
         }
         return false;
      }

   default:
      return false;
   }
}

static bool vi_eval_const_int(vi_lower_t *l, tree_t expr, int64_t *out)
{
   return vi_eval_const_int_r(l, expr, out, 0);
}

static bool vi_eval_type_bounds(vi_lower_t *l, type_t t,
                                range_kind_t *dir_out, int64_t *left_out,
                                int64_t *right_out, uint32_t *width_out)
{
   if (t == NULL || type_is_unconstrained(t) || !type_is_array(t)
       || dimension_of(t) < 1)
      return false;
   tree_t r = range_of(t, 0);
   if (r == NULL || tree_kind(r) != T_RANGE)
      return false;
   range_kind_t rk = tree_subkind(r);
   if (rk == RANGE_EXPR && tree_has_value(r)) {
      tree_t re = tree_value(r);
      if (tree_kind(re) != T_ATTR_REF || tree_params(re) > 0)
         return false;
      const attr_kind_t ak = tree_subkind(re);
      if (ak != ATTR_RANGE && ak != ATTR_REVERSE_RANGE)
         return false;
      tree_t nm = tree_name(re);
      range_kind_t bdir = RANGE_DOWNTO;
      int64_t bl = 0, br = 0;
      uint32_t bw = 0;
      bool have = false;
      if (tree_kind(nm) == T_REF) {
         vi_local_t *loc = vi_find_local(l, tree_ref(nm));
         if (loc != NULL && loc->is_vec && loc->width > 0) {
            bdir = loc->dir;
            bl = loc->left;
            br = loc->right;
            bw = loc->width;
            have = true;
         }
      }
      if (!have && nm != NULL && tree_type(nm) != NULL
          && vi_eval_type_bounds(l, tree_type(nm), &bdir, &bl, &br, &bw))
         have = true;
      if (!have || bw == 0 || bw > 65536)
         return false;
      if (ak == ATTR_RANGE) {
         *dir_out = bdir;
         *left_out = bl;
         *right_out = br;
      }
      else {
         *dir_out = (bdir == RANGE_TO) ? RANGE_DOWNTO : RANGE_TO;
         *left_out = br;
         *right_out = bl;
      }
      *width_out = bw;
      return true;
   }
   if (rk != RANGE_TO && rk != RANGE_DOWNTO)
      return false;
   int64_t lv, rr;
   if (!vi_eval_const_int(l, tree_left(r), &lv)
       || !vi_eval_const_int(l, tree_right(r), &rr))
      return false;
   int64_t len = (rk == RANGE_TO) ? (rr - lv + 1) : (lv - rr + 1);
   if (len <= 0 || len > 65536)
      return false;
   *dir_out = rk;
   *left_out = lv;
   *right_out = rr;
   *width_out = (uint32_t)len;
   return true;
}

static bool vi_type_is_std_logic(type_t t)
{
   type_t base = type_base_recur(t);
   well_known_t wk = is_well_known(type_ident(base));
   return wk == W_IEEE_ULOGIC || wk == W_IEEE_LOGIC;
}

static bool vi_type_is_bool(type_t t)
{
   type_t base = type_base_recur(t);
   return is_well_known(type_ident(base)) == W_STD_BOOL;
}

static bool vi_type_is_int_or_user_enum(type_t t)
{
   if (t == NULL)
      return false;
   if (type_is_integer(t))
      return true;
   if (type_is_enum(t)) {
      type_t base = type_base_recur(t);
      well_known_t wk = is_well_known(type_ident(base));
      return wk != W_IEEE_ULOGIC && wk != W_IEEE_LOGIC
         && wk != W_STD_BIT && wk != W_STD_BOOL;
   }
   return false;
}

static bool vi_type_is_signed_vec(type_t t)
{
   const char *n = istr(type_ident(type_base_recur(t)));
   return strcmp(n, "IEEE.NUMERIC_STD.SIGNED") == 0
      || strcmp(n, "IEEE.NUMERIC_STD.UNRESOLVED_SIGNED") == 0;
}

// The tables shared by the processes of a unit (std_logic operator tables,
// constant ROMs) are built in the entry block, which dominates every other
// block: a table first needed inside a branch is used again in other
// branches and processes.  The entry block jumps to the body only when the
// unit is complete (vi_unit_close).
static void vi_unit_open(vi_lower_t *l)
{
   l->const_bb = mir_get_cursor(l->mu, NULL);
   l->body_bb = mir_add_block(l->mu);
   mir_set_cursor(l->mu, l->body_bb, MIR_APPEND);
}

static void vi_unit_close(vi_lower_t *l)
{
   mir_set_cursor(l->mu, l->const_bb, MIR_APPEND);
   mir_build_jump(l->mu, l->body_bb);
}

static mir_block_t vi_const_begin(vi_lower_t *l, unsigned *pos)
{
   mir_block_t bb = mir_get_cursor(l->mu, pos);
   mir_set_cursor(l->mu, l->const_bb, MIR_APPEND);
   return bb;
}

static void vi_const_end(vi_lower_t *l, mir_block_t bb, unsigned pos)
{
   mir_set_cursor(l->mu, bb, pos);
}

static mir_value_t vi_get_std_lut(vi_lower_t *l, int kind)
{
   mir_value_t *slot = NULL;
   const uint8_t *tbl = NULL;
   int len = 81;
   switch (kind) {
   case 0: slot = &l->lut_not; tbl = STD_NOT_TBL; len = 9; break;
   case 1: slot = &l->lut_and; tbl = STD_AND_TBL; break;
   case 2: slot = &l->lut_or;  tbl = STD_OR_TBL;  break;
   case 3: slot = &l->lut_xor; tbl = STD_XOR_TBL; break;
   default: should_not_reach_here();
   }
   if (!mir_is_null(*slot))
      return *slot;

   mir_unit_t *mu = l->mu;
   unsigned pos;
   mir_block_t bb = vi_const_begin(l, &pos);
   mir_value_t vals[81];
   for (int i = 0; i < len; i++)
      vals[i] = vi_u8(mu, tbl[i]);
   mir_value_t ca = mir_const_array(mu, mir_carray_type(mu, len, vi_u8_type(mu)),
                                    vals, len);
   *slot = mir_build_address_of(mu, ca);
   vi_const_end(l, bb, pos);
   return *slot;
}

static mir_value_t vi_std_scalar_not(vi_lower_t *l, mir_value_t a)
{
   mir_value_t tbl = vi_get_std_lut(l, 0);
   mir_value_t idx = vi_to_off(l->mu, a);
   return mir_build_load(l->mu, mir_build_array_ref(l->mu, tbl, idx));
}

static mir_value_t vi_std_scalar_bin(vi_lower_t *l, int tbl_kind,
                                     mir_value_t a, mir_value_t b, bool neg)
{
   mir_unit_t *mu = l->mu;
   mir_value_t tbl = vi_get_std_lut(l, tbl_kind);
   mir_value_t idx = mir_build_add(mu, mir_offset_type(mu),
                                   mir_build_mul(mu, mir_offset_type(mu),
                                                 vi_to_off(mu, a), vi_off(mu, 9)),
                                   vi_to_off(mu, b));
   mir_value_t res = mir_build_load(mu, mir_build_array_ref(mu, tbl, idx));
   return neg ? vi_std_scalar_not(l, res) : res;
}

static void vi_scope_clear(vi_scope_t *sc)
{
   for (unsigned i = 0; i < sc->locals.count; i++)
      free(sc->locals.items[i].rec_leaves);
   ACLEAR(sc->locals);
}

// An integer variable whose value may be known (see vi_const_forget)
static bool vi_is_const_var(const vi_local_t *loc)
{
   return loc->is_int && !loc->by_val && loc->decl != NULL
      && tree_kind(loc->decl) == T_VAR_DECL;
}

static vi_local_t *vi_find_local(vi_lower_t *l, tree_t decl)
{
   for (vi_scope_t *s = l->scope; s != NULL; s = s->parent) {
      for (unsigned i = 0; i < s->locals.count; i++) {
         if (s->locals.items[i].decl == decl)
            return &(s->locals.items[i]);
      }
   }
   return NULL;
}

static mir_value_t vi_to_vec2_1(vi_lower_t *l, vi_val_t v);
static mir_value_t vi_modeb_alloc_vec(vi_lower_t *l, uint32_t width);

typedef A(vi_rec_leaf_t) vi_rec_leaf_list_t;

// A local 2D array is a byte array (one byte per element) in Mode B.  In
// Mode A it has the layout of a 2D signal in the island state: one row of
// ceil(width / 64) words per element, accessed with island load and store on
// the local buffer.  Whole-array copies with a signal are then plain copies.
static uint32_t vi_array2d_bytes(const vi_lower_t *l, uint32_t nelems,
                                 uint32_t width)
{
   if (l->two_state)
      return nelems * 8u * ((width + 63u) / 64u);
   return nelems * width;
}

static mir_value_t vi_alloc_array2d_local(vi_lower_t *l, uint32_t nelems,
                                          uint32_t width, ident_t name)
{
   (void)name;
   return vi_modeb_alloc_vec(l, vi_array2d_bytes(l, nelems, width));
}

// A wide Mode A vector variable is a pointer to its words and a variable has
// no pointer until it is first stored, and an element assignment to a
// narrow one keeps the bits it does not write, which concatenation and
// comparison read as part of the value.  So a variable that is read before
// it is assigned or assigned element by element (a function local or record
// field with no initial value, a process variable) must start as zero, the
// 2-state image of the default value.
static mir_value_t vi_add_vec2_var(vi_lower_t *l, uint32_t width,
                                   bool is_signed, ident_t name)
{
   mir_unit_t *mu = l->mu;
   mir_value_t var = mir_add_var(mu, mir_vec2_type(mu, (int)width, is_signed),
                                 MIR_NULL_STAMP, name, MIR_VAR_TEMP);
   mir_build_store(mu, var, vi_const_vec2(mu, (int)width, is_signed, NULL));
   return var;
}

static bool vi_collect_rec_local_leaves(vi_lower_t *l, type_t type,
                                        int *path, uint8_t depth,
                                        ident_t name_hint,
                                        vi_rec_leaf_list_t *out)
{
   if (out->count >= 512)
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
         path[depth] = i;
         if (!vi_collect_rec_local_leaves(l, tree_type(f), path,
                                          (uint8_t)(depth + 1), name_hint, out))
            return false;
      }
      return true;
   }

   mir_unit_t *mu = l->mu;
   vi_rec_leaf_t lf = {
      .path_len = depth,
      .type     = type,
      .nelems   = 1,
   };
   if (depth > 0)
      memcpy(lf.path, path, depth * sizeof(int));

   if (!type_is_array(type)) {
      if (vi_type_is_int_or_user_enum(type)) {
         lf.is_int = true;
         lf.width = 1;
         lf.storage = mir_add_var(mu, vi_i64_type(mu), MIR_NULL_STAMP,
                                  ident_uniq(istr(name_hint)), MIR_VAR_TEMP);
         APUSH(*out, lf);
         return true;
      }
      if (type_is_enum(type)) {
         lf.is_std = vi_type_is_std_logic(type);
         lf.is_bool = vi_type_is_bool(type);
         lf.width = 1;
         mir_type_t st = l->two_state
            ? mir_vec2_type(mu, 1, false) : vi_u8_type(mu);
         lf.storage = mir_add_var(mu, st, MIR_NULL_STAMP,
                                  ident_uniq(istr(name_hint)), MIR_VAR_TEMP);
         APUSH(*out, lf);
         return true;
      }
      return false;
   }

   if (dimension_of(type) != 1)
      return false;
   type_t elem = type_elem(type);
   if (!type_is_array(elem) && !type_is_record(elem)) {
      if (!type_is_enum(elem))
         return false;
      lf.is_vec = true;
      lf.is_std = vi_type_is_std_logic(elem);
      lf.is_signed = vi_type_is_signed_vec(type);
      if (!vi_eval_type_bounds(l, type, &lf.dir, &lf.left, &lf.right, &lf.width)
          || lf.width == 0 || lf.width > VHDL_ISLAND_MAX_WIDTH)
         return false;
      if (l->two_state)
         lf.storage = vi_add_vec2_var(l, lf.width, lf.is_signed,
                                      ident_uniq(istr(name_hint)));
      else
         lf.storage = vi_modeb_alloc_vec(l, lf.width);
      APUSH(*out, lf);
      return true;
   }

   if (type_is_record(elem)) {
      if (depth >= VHDL_ISIG_MAX_DEPTH)
         return false;
      range_kind_t adir;
      int64_t aleft, aright;
      uint32_t nelems;
      if (!vi_eval_type_bounds(l, type, &adir, &aleft, &aright, &nelems)
          || nelems == 0 || nelems > 64)
         return false;
      for (uint32_t e = 0; e < nelems; e++) {
         path[depth] = -(int)(e + 1);
         if (!vi_collect_rec_local_leaves(l, elem, path, (uint8_t)(depth + 1),
                                          name_hint, out))
            return false;
      }
      return true;
   }

   if (type_is_array(elem) && !type_is_record(elem) && dimension_of(elem) == 1) {
      if (!type_is_enum(type_elem(elem)))
         return false;
      if (!vi_eval_type_bounds(l, elem, &lf.dir, &lf.left, &lf.right, &lf.width)
          || lf.width == 0 || lf.width > VHDL_ISLAND_MAX_WIDTH)
         return false;
      if (!vi_eval_type_bounds(l, type, &lf.outer_dir, &lf.outer_left,
                               &lf.outer_right, &lf.nelems)
          || lf.nelems == 0
          || (uint64_t)lf.width * lf.nelems > 4096u)
         return false;
      lf.is_vec = true;
      lf.is_array2d = true;
      lf.is_std = vi_type_is_std_logic(type_elem(elem));
      lf.is_signed = vi_type_is_signed_vec(elem);
      lf.storage = vi_alloc_array2d_local(l, lf.nelems, lf.width, name_hint);
      APUSH(*out, lf);
      return true;
   }

   return false;
}

static bool vi_extract_local_rec_path(vi_lower_t *l, tree_t expr,
                                      vi_local_t **loc_out,
                                      int *path_out, uint8_t *len_out)
{
   int rev_path[VHDL_ISIG_MAX_DEPTH];
   uint8_t n = 0;
   tree_t cur = expr;

   while (cur != NULL) {
      switch (tree_kind(cur)) {
      case T_QUALIFIED:
      case T_TYPE_CONV:
         cur = tree_value(cur);
         break;
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
      case T_ARRAY_REF:
         {
            if (n >= VHDL_ISIG_MAX_DEPTH || tree_params(cur) != 1
                || !type_is_record(tree_type(cur)))
               return false;
            tree_t arr_e = tree_value(cur);
            type_t arr_t = tree_type(arr_e);
            if (!type_is_array(arr_t) || dimension_of(arr_t) != 1)
               return false;
            range_kind_t adir;
            int64_t aleft, aright, idx;
            uint32_t nelems;
            if (!vi_eval_type_bounds(l, arr_t, &adir, &aleft, &aright, &nelems)
                || nelems == 0 || nelems > 64
                || !vi_eval_const_int(l, tree_value(tree_param(cur, 0)), &idx))
               return false;
            int64_t elem = (adir == RANGE_TO) ? (idx - aleft) : (aleft - idx);
            if (elem < 0 || elem >= (int64_t)nelems)
               return false;
            rev_path[n++] = -(int)(elem + 1);
            cur = arr_e;
         }
         break;
      case T_REF:
         {
            vi_local_t *loc = vi_find_local(l, tree_ref(cur));
            if (loc == NULL || !loc->is_rec)
               return false;
            *loc_out = loc;
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

static const vi_rec_leaf_t *vi_find_rec_leaf(const vi_local_t *loc,
                                             const int *path, uint8_t plen)
{
   for (uint32_t i = 0; i < loc->n_rec_leaves; i++) {
      const vi_rec_leaf_t *lf = &(loc->rec_leaves[i]);
      if (lf->path_len != plen)
         continue;
      if (plen == 0 || memcmp(lf->path, path, plen * sizeof(int)) == 0)
         return lf;
   }
   return NULL;
}

static uint32_t vi_collect_rec_prefix_leaves(const vi_local_t *loc,
                                             const int *path, uint8_t plen,
                                             const vi_rec_leaf_t **out,
                                             uint32_t max_out)
{
   uint32_t n = 0;
   for (uint32_t i = 0; i < loc->n_rec_leaves; i++) {
      const vi_rec_leaf_t *lf = &(loc->rec_leaves[i]);
      if (lf->path_len < plen)
         continue;
      if (plen > 0 && memcmp(lf->path, path, plen * sizeof(int)) != 0)
         continue;
      if (n < max_out)
         out[n] = lf;
      n++;
   }
   return n;
}

static bool vi_resolve_local_leaf(vi_lower_t *l, tree_t expr,
                                  vi_rec_leaf_t *leaf_out)
{
   if (tree_kind(expr) == T_REF) {
      vi_local_t *loc = vi_find_local(l, tree_ref(expr));
      if (loc == NULL || loc->is_rec || loc->by_val)
         return false;
      *leaf_out = (vi_rec_leaf_t){
         .is_array2d  = loc->is_array2d,
         .is_vec      = loc->is_vec,
         .is_std      = loc->is_std,
         .is_bool     = loc->is_bool,
         .is_signed   = loc->is_signed,
         .is_int      = loc->is_int,
         .width       = loc->width,
         .nelems      = loc->nelems,
         .dir         = loc->dir,
         .left        = loc->left,
         .right       = loc->right,
         .outer_dir   = loc->outer_dir,
         .outer_left  = loc->outer_left,
         .outer_right = loc->outer_right,
         .storage     = loc->storage,
      };
      return true;
   }

   vi_local_t *loc = NULL;
   int path[VHDL_ISIG_MAX_DEPTH];
   uint8_t plen = 0;
   if (!vi_extract_local_rec_path(l, expr, &loc, path, &plen))
      return false;
   const vi_rec_leaf_t *lf = vi_find_rec_leaf(loc, path, plen);
   if (lf == NULL)
      return false;
   *leaf_out = *lf;
   return true;
}

static bool vi_load_rec_leaf(vi_lower_t *l, const vi_rec_leaf_t *lf,
                             mir_value_t elem_idx, vi_val_t *out)
{
   mir_unit_t *mu = l->mu;
   if (lf->is_array2d) {
      if (l->two_state) {
         mir_value_t row = mir_build_island_load(
            mu, mir_vec2_type(mu, (int)lf->width, lf->is_signed),
            lf->storage, vi_off(mu, 0), vi_off(mu, -1), elem_idx);
         *out = (vi_val_t){
            .kind      = VIVAL_VEC,
            .val       = row,
            .width     = lf->width,
            .is_signed = lf->is_signed,
            .is_std    = lf->is_std,
            .dir       = lf->dir,
            .left      = lf->left,
            .right     = lf->right,
         };
         return true;
      }
      mir_value_t off = mir_build_mul(mu, mir_offset_type(mu),
                                      elem_idx, vi_off(mu, lf->width));
      *out = (vi_val_t){
         .kind      = VIVAL_VEC,
         .val       = mir_build_array_ref(mu, lf->storage, off),
         .width     = lf->width,
         .is_signed = lf->is_signed,
         .is_std    = lf->is_std,
         .dir       = lf->dir,
         .left      = lf->left,
         .right     = lf->right,
      };
      return true;
   }
   if (lf->is_vec) {
      *out = (vi_val_t){
         .kind      = VIVAL_VEC,
         .val       = l->two_state ? mir_build_load(mu, lf->storage) : lf->storage,
         .width     = lf->width,
         .is_signed = lf->is_signed,
         .is_std    = lf->is_std,
         .dir       = lf->dir,
         .left      = lf->left,
         .right     = lf->right,
      };
      return true;
   }
   if (lf->is_int) {
      *out = (vi_val_t){
         .kind = VIVAL_INT,
         .val  = mir_build_load(mu, lf->storage),
      };
      return true;
   }
   *out = (vi_val_t){
      .kind    = VIVAL_BIT,
      .val     = mir_build_load(mu, lf->storage),
      .width   = 1,
      .is_std  = lf->is_std,
      .is_bool = lf->is_bool,
   };
   return true;
}

static bool vi_store_rec_leaf(vi_lower_t *l, const vi_rec_leaf_t *lf,
                              mir_value_t elem_idx, vi_val_t rv)
{
   mir_unit_t *mu = l->mu;
   if (lf->is_array2d) {
      if (rv.kind != VIVAL_VEC || rv.width != lf->width)
         return false;
      if (l->two_state) {
         mir_value_t row = (rv.is_signed != lf->is_signed)
            ? mir_build_cast(mu, mir_vec2_type(mu, (int)lf->width,
                                               lf->is_signed), rv.val)
            : rv.val;
         mir_build_island_store(mu, lf->storage, vi_off(mu, 0),
                                vi_off(mu, -1), elem_idx, row);
         return true;
      }
      mir_value_t off = mir_build_mul(mu, mir_offset_type(mu),
                                      elem_idx, vi_off(mu, lf->width));
      mir_build_copy(mu, mir_build_array_ref(mu, lf->storage, off),
                     rv.val, vi_off(mu, lf->width));
      return true;
   }
   if (lf->is_vec) {
      if (rv.kind != VIVAL_VEC || rv.width != lf->width)
         return false;
      if (l->two_state) {
         mir_value_t v = (rv.is_signed != lf->is_signed)
            ? mir_build_cast(mu, mir_vec2_type(mu, (int)lf->width, lf->is_signed),
                             rv.val)
            : rv.val;
         mir_build_store(mu, lf->storage, v);
      }
      else
         mir_build_copy(mu, lf->storage, rv.val, vi_off(mu, lf->width));
      return true;
   }
   if (lf->is_int) {
      if (rv.kind != VIVAL_INT)
         return false;
      mir_build_store(mu, lf->storage, vi_to_i64(mu, rv.val));
      return true;
   }
   if (rv.kind != VIVAL_BIT)
      return false;
   mir_value_t v = l->two_state
      ? vi_to_vec2_1(l, rv)
      : mir_build_cast(mu, vi_u8_type(mu), rv.val);
   mir_build_store(mu, lf->storage, v);
   return true;
}

static bool vi_assign_rec_local(vi_lower_t *l, vi_local_t *dloc,
                                int *dpath, uint8_t dlen,
                                type_t rec_type, tree_t rhs_e);

// Position of T'LEFT, the default value of scalar type T
static bool vi_type_default_pos(vi_lower_t *l, type_t t, int64_t *pos)
{
   if (!type_is_scalar(t))
      return false;
   tree_t r = range_of(t, 0);
   if (r == NULL || tree_kind(r) != T_RANGE)
      return false;
   tree_t left = tree_left(r);
   if (tree_kind(left) == T_REF && tree_has_ref(left)
       && tree_kind(tree_ref(left)) == T_ENUM_LIT) {
      *pos = tree_pos(tree_ref(left));
      return true;
   }
   return vi_eval_const_int(l, left, pos);
}

// A subprogram variable declared without an initial value starts as
// T'LEFT of its type, element by element for an array (LRM 14.4.2.5),
// and an inlined call must give the storage of LF of type T that value
// rather than whatever the stack held.  Mode A stores 0, the 2-state image
// of the default; the speculative variant cannot hold a std_ulogic default
// such as 'U', see vi_check_spec_defaults.
static bool vi_init_default_leaf(vi_lower_t *l, const vi_rec_leaf_t *lf,
                                 type_t t)
{
   mir_unit_t *mu = l->mu;
   type_t st = t;
   while (type_is_array(st))
      st = type_elem(st);
   int64_t pos = 0;
   if (!vi_type_default_pos(l, st, &pos))
      return false;

   if (lf->is_array2d) {
      const uint32_t bytes = vi_array2d_bytes(l, lf->nelems, lf->width);
      mir_build_set(mu, lf->storage,
                    vi_u8(mu, l->two_state ? 0 : (uint8_t)pos),
                    vi_off(mu, bytes));
      return true;
   }
   if (lf->is_vec) {
      if (l->two_state) {
         uint8_t b = lf->is_std ? std_enum_to_2bit(pos) : (uint8_t)(pos & 1);
         mir_build_store(mu, lf->storage,
                         vi_const_vec2_rep(mu, (int)lf->width, lf->is_signed,
                                           b));
      }
      else
         mir_build_set(mu, lf->storage, vi_u8(mu, (uint8_t)pos),
                       vi_off(mu, lf->width));
      return true;
   }
   if (lf->is_int) {
      mir_build_store(mu, lf->storage, vi_i64(mu, pos));
      return true;
   }
   if (l->two_state) {
      uint8_t b = lf->is_std ? std_enum_to_2bit(pos) : (uint8_t)(pos & 1);
      mir_build_store(mu, lf->storage,
                      mir_const_vec(mu, mir_vec2_type(mu, 1, false), b, 0));
   }
   else
      mir_build_store(mu, lf->storage, vi_u8(mu, (uint8_t)pos));
   return true;
}

// Definite assignment of a subprogram variable without an initial value
// (the speculative variant only, which cannot hold a 'U' default): true
// when every path through the body assigns each element of the variable
// before anything reads any part of it.  Positions are the elements of
// each leaf (one per scalar or 2D-array leaf).
typedef struct {
   vi_lower_t       *l;
   const vi_local_t *loc;
   uint32_t          npos;
   uint32_t         *leaf_off;   // First position of each record leaf
   bool              read_early;
} vi_da_t;

static uint32_t vi_da_leaf_npos(const vi_rec_leaf_t *lf)
{
   return (lf->is_vec && !lf->is_array2d) ? lf->width : 1;
}

static void vi_da_ref_cb(tree_t t, void *ctx)
{
   vi_da_t *da = ctx;
   if (tree_has_ref(t) && tree_ref(t) == da->loc->decl)
      da->read_early = true;
}

// A read of any part of the variable while some position is unassigned
static void vi_da_read(vi_da_t *da, tree_t expr, const bool *asg)
{
   if (expr == NULL)
      return;
   for (uint32_t i = 0; i < da->npos; i++) {
      if (!asg[i]) {
         tree_visit_only(expr, vi_da_ref_cb, da, T_REF);
         return;
      }
   }
}

// Positions *LO, *N that TARGET overwrites completely, or false
static bool vi_da_target(vi_da_t *da, tree_t target, uint32_t *lo,
                         uint32_t *n)
{
   const vi_local_t *loc = da->loc;
   tree_t base = target;
   tree_t sel = NULL;
   if (tree_kind(target) == T_ARRAY_REF && type_is_record(tree_type(target)))
      ;   // An element of an array of records: a record path
   else if (tree_kind(target) == T_ARRAY_REF
            || tree_kind(target) == T_ARRAY_SLICE) {
      sel = target;
      base = tree_value(target);
   }

   uint32_t blo = 0, bn = da->npos;
   const vi_rec_leaf_t *leaf = NULL;
   if (tree_kind(base) == T_REF) {
      if (tree_ref(base) != loc->decl)
         return false;
   }
   else {
      vi_local_t *rl = NULL;
      int path[VHDL_ISIG_MAX_DEPTH];
      uint8_t plen = 0;
      if (!loc->is_rec || !vi_extract_local_rec_path(da->l, base, &rl, path,
                                                     &plen)
          || rl->decl != loc->decl)
         return false;
      const vi_rec_leaf_t *lv[512];
      const uint32_t nl = vi_collect_rec_prefix_leaves(loc, path, plen,
                                                       lv, 512);
      if (nl == 0 || nl > 512)
         return false;
      // Leaves of a prefix are consecutive
      const uint32_t first = lv[0] - loc->rec_leaves;
      const uint32_t last = lv[nl - 1] - loc->rec_leaves;
      blo = da->leaf_off[first];
      bn = da->leaf_off[last] + vi_da_leaf_npos(lv[nl - 1]) - blo;
      if (nl == 1)
         leaf = lv[0];
   }

   if (sel == NULL) {
      *lo = blo;
      *n = bn;
      return true;
   }

   // An element or slice of a vector
   range_kind_t dir;
   int64_t left;
   uint32_t width;
   if (leaf != NULL) {
      if (!leaf->is_vec || leaf->is_array2d)
         return false;
      dir = leaf->dir;
      left = leaf->left;
      width = leaf->width;
   }
   else if (!loc->is_rec && loc->is_vec && !loc->is_array2d) {
      dir = loc->dir;
      left = loc->left;
      width = loc->width;
   }
   else
      return false;

   int64_t il, ir;
   if (tree_kind(sel) == T_ARRAY_REF) {
      if (tree_params(sel) != 1
          || !vi_eval_const_int(da->l, tree_value(tree_param(sel, 0)), &il))
         return false;
      ir = il;
   }
   else {
      tree_t rg = tree_range(sel, 0);
      if (tree_subkind(rg) != dir
          || !vi_eval_const_int(da->l, tree_left(rg), &il)
          || !vi_eval_const_int(da->l, tree_right(rg), &ir))
         return false;
   }
   const int64_t p0 = (dir == RANGE_TO) ? il - left : left - il;
   const int64_t p1 = (dir == RANGE_TO) ? ir - left : left - ir;
   if (p0 < 0 || p1 < p0 || p1 >= width)
      return false;
   *lo = blo + (uint32_t)p0;
   *n = (uint32_t)(p1 - p0 + 1);
   return true;
}

// Reads in the index expressions of TARGET (the variable itself is written)
static void vi_da_target_reads(vi_da_t *da, tree_t target, const bool *asg)
{
   for (tree_t t = target; ; ) {
      switch (tree_kind(t)) {
      case T_ARRAY_REF:
         for (int i = 0; i < tree_params(t); i++)
            vi_da_read(da, tree_value(tree_param(t, i)), asg);
         t = tree_value(t);
         continue;
      case T_ARRAY_SLICE:
         {
            tree_t rg = tree_range(t, 0);
            if (tree_subkind(rg) == RANGE_EXPR)
               vi_da_read(da, tree_value(rg), asg);
            else {
               vi_da_read(da, tree_left(rg), asg);
               vi_da_read(da, tree_right(rg), asg);
            }
            t = tree_value(t);
            continue;
         }
      case T_RECORD_REF:
         t = tree_value(t);
         continue;
      case T_REF:
         return;
      default:
         vi_da_read(da, t, asg);
         return;
      }
   }
}

static bool vi_da_stmts(vi_da_t *da, tree_t owner, bool *asg);

static void vi_da_and(vi_da_t *da, bool *acc, const bool *b)
{
   for (uint32_t i = 0; i < da->npos; i++)
      acc[i] = acc[i] && b[i];
}

// Analyses STMT with positions ASG assigned on entry; returns false when
// the statement does not complete normally (a return), so what follows is
// unreachable on that path
static bool vi_da_stmt(vi_da_t *da, tree_t stmt, bool *asg)
{
   const size_t sz = MAX(1u, da->npos) * sizeof(bool);
   switch (tree_kind(stmt)) {
   case T_VAR_ASSIGN:
   case T_SIGNAL_ASSIGN:
      {
         if (tree_kind(stmt) == T_VAR_ASSIGN)
            vi_da_read(da, tree_value(stmt), asg);
         else {
            for (int i = 0; i < tree_waveforms(stmt); i++) {
               tree_t w = tree_waveform(stmt, i);
               if (tree_has_value(w))
                  vi_da_read(da, tree_value(w), asg);
               if (tree_has_delay(w))
                  vi_da_read(da, tree_delay(w), asg);
            }
            vi_da_read(da, tree_target(stmt), asg);
            return true;
         }
         tree_t target = tree_target(stmt);
         vi_da_target_reads(da, target, asg);
         uint32_t lo, n;
         if (vi_da_target(da, target, &lo, &n))
            memset(asg + lo, true, n);
         return true;
      }
   case T_RETURN:
      if (tree_has_value(stmt))
         vi_da_read(da, tree_value(stmt), asg);
      return false;
   case T_NULL:
      return true;
   case T_ASSERT:
   case T_REPORT:
      if (tree_has_value(stmt))
         vi_da_read(da, tree_value(stmt), asg);
      if (tree_has_message(stmt))
         vi_da_read(da, tree_message(stmt), asg);
      if (tree_has_severity(stmt))
         vi_da_read(da, tree_severity(stmt), asg);
      return true;
   case T_SEQUENCE:
      return vi_da_stmts(da, stmt, asg);
   case T_IF:
      {
         bool *acc LOCAL = xmalloc(sz);
         bool *br LOCAL = xmalloc(sz);
         memset(acc, true, sz);
         bool any = false, has_else = false;
         for (int i = 0; i < tree_conds(stmt); i++) {
            tree_t c = tree_cond(stmt, i);
            if (tree_has_value(c))
               vi_da_read(da, tree_value(c), asg);
            else
               has_else = true;
            memcpy(br, asg, sz);
            if (vi_da_stmts(da, c, br)) {
               vi_da_and(da, acc, br);
               any = true;
            }
         }
         if (!has_else) {
            vi_da_and(da, acc, asg);
            any = true;
         }
         if (!any)
            return false;
         memcpy(asg, acc, sz);
         return true;
      }
   case T_CASE:
      {
         vi_da_read(da, tree_value(stmt), asg);
         bool *acc LOCAL = xmalloc(sz);
         bool *br LOCAL = xmalloc(sz);
         memset(acc, true, sz);
         bool any = false;
         // A case statement covers every value of its expression
         for (int i = 0; i < tree_stmts(stmt); i++) {
            memcpy(br, asg, sz);
            if (vi_da_stmts(da, tree_stmt(stmt, i), br)) {
               vi_da_and(da, acc, br);
               any = true;
            }
         }
         if (tree_stmts(stmt) == 0)
            return true;
         if (!any)
            return false;
         memcpy(asg, acc, sz);
         return true;
      }
   case T_FOR:
      {
         tree_t rg = tree_range(stmt, 0);
         if (tree_subkind(rg) == RANGE_EXPR)
            vi_da_read(da, tree_value(rg), asg);
         else {
            vi_da_read(da, tree_left(rg), asg);
            vi_da_read(da, tree_right(rg), asg);
         }
         // The body may run zero times: it assigns nothing for what
         // follows, except a loop that assigns an element on every pass
         // (as in r(i) := ...), which covers the elements its parameter
         // selects
         bool *br LOCAL = xmalloc(sz);
         memcpy(br, asg, sz);
         const bool plain = tree_visit_only(stmt, NULL, NULL, T_EXIT) == 0
            && tree_visit_only(stmt, NULL, NULL, T_NEXT) == 0
            && tree_visit_only(stmt, NULL, NULL, T_RETURN) == 0;
         vi_da_stmts(da, stmt, br);   // Reads in the body
         int64_t lv, rv;
         const range_kind_t rk = tree_subkind(rg);
         if (!plain || (rk != RANGE_TO && rk != RANGE_DOWNTO)
             || !vi_eval_const_int(da->l, tree_left(rg), &lv)
             || !vi_eval_const_int(da->l, tree_right(rg), &rv))
            return true;
         const int64_t lo = MIN(lv, rv), hi = MAX(lv, rv);
         if ((rk == RANGE_TO) ? lv > rv : lv < rv)
            return true;
         if (hi - lo >= VHDL_ISLAND_MAX_LOOP_ITERS)
            return true;
         // Evaluate the targets with the loop parameter as a constant
         vi_local_t pl = { .decl = tree_decl(stmt, 0), .has_const_int = true };
         APUSH(da->l->scope->locals, pl);
         const unsigned pidx = da->l->scope->locals.count - 1;
         for (int64_t v = lo; v <= hi; v++) {
            da->l->scope->locals.items[pidx].const_int = v;
            for (int i = 0; i < tree_stmts(stmt); i++) {
               tree_t s = tree_stmt(stmt, i);
               uint32_t plo, pn;
               if (tree_kind(s) == T_VAR_ASSIGN
                   && vi_da_target(da, tree_target(s), &plo, &pn))
                  memset(asg + plo, true, pn);
            }
         }
         da->l->scope->locals.count--;
         return true;
      }
   case T_EXIT:
   case T_NEXT:
      if (tree_has_value(stmt))
         vi_da_read(da, tree_value(stmt), asg);
      return true;
   case T_PCALL:
      for (int i = 0; i < tree_params(stmt); i++)
         vi_da_read(da, tree_value(tree_param(stmt, i)), asg);
      return true;
   default:
      da->read_early = true;
      return true;
   }
}

static bool vi_da_stmts(vi_da_t *da, tree_t owner, bool *asg)
{
   for (int i = 0; i < tree_stmts(owner); i++) {
      if (!vi_da_stmt(da, tree_stmt(owner, i), asg))
         return false;
   }
   return true;
}

// LOC, a variable of subprogram BODY without an initial value, bound in the
// current scope: whether the body assigns all of it before reading it
static bool vi_def_assigned(vi_lower_t *l, tree_t body, const vi_local_t *loc)
{
   // A copy: the loop analysis adds to the scope that holds LOC
   const vi_local_t copy = *loc;
   vi_da_t da = { .l = l, .loc = &copy };
   uint32_t *offs LOCAL = NULL;
   if (loc->is_rec) {
      offs = xmalloc_array(MAX(1u, loc->n_rec_leaves), sizeof(uint32_t));
      for (uint32_t i = 0; i < loc->n_rec_leaves; i++) {
         offs[i] = da.npos;
         da.npos += vi_da_leaf_npos(&(loc->rec_leaves[i]));
      }
      da.leaf_off = offs;
   }
   else
      da.npos = (loc->is_vec && !loc->is_array2d) ? loc->width : 1;
   if (da.npos == 0 || da.npos > 65536)
      return false;
   bool *asg LOCAL = xcalloc_array(da.npos, sizeof(bool));
   vi_da_stmts(&da, body, asg);
   return !da.read_early;
}

static bool vi_local_has_std(const vi_local_t *loc)
{
   if (!loc->is_rec)
      return loc->is_std;
   for (uint32_t i = 0; i < loc->n_rec_leaves; i++) {
      if (loc->rec_leaves[i].is_std)
         return true;
   }
   return false;
}

static bool vi_init_default_local(vi_lower_t *l, const vi_local_t *loc)
{
   if (loc->is_rec) {
      for (uint32_t i = 0; i < loc->n_rec_leaves; i++) {
         const vi_rec_leaf_t *lf = &(loc->rec_leaves[i]);
         if (!vi_init_default_leaf(l, lf, lf->type))
            return false;
      }
      return true;
   }
   const vi_rec_leaf_t lf = {
      .is_array2d = loc->is_array2d,
      .is_vec     = loc->is_vec,
      .is_std     = loc->is_std,
      .is_bool    = loc->is_bool,
      .is_signed  = loc->is_signed,
      .is_int     = loc->is_int,
      .width      = loc->width,
      .nelems     = loc->nelems,
      .storage    = loc->storage,
   };
   return vi_init_default_leaf(l, &lf, tree_type(loc->decl));
}

// Speculative variant: the 2-state zero that stands for the 'U' default of
// a std_ulogic variable of subprogram BODY must never be read, so every
// such variable in SC must be assigned before it is read
static bool vi_check_spec_defaults(vi_lower_t *l, tree_t body,
                                   const vi_scope_t *sc)
{
   if (!l->spec)
      return true;
   for (unsigned i = 0; i < sc->locals.count; i++) {
      const vi_local_t *loc = &(sc->locals.items[i]);
      if (tree_kind(loc->decl) != T_VAR_DECL || tree_has_value(loc->decl)
          || !vi_local_has_std(loc))
         continue;
      if (!vi_def_assigned(l, body, loc)) {
         if (l->reason == NULL && opt_get_int(OPT_DUMP_ISLANDS))
            l->reason = istr(ident_sprintf(
               "spec: %s may be read before assignment (default 'U')",
               istr(tree_ident(loc->decl))));
         return false;
      }
   }
   return true;
}

static mir_value_t vi_to_bool(vi_lower_t *l, vi_val_t v)
{
   mir_unit_t *mu = l->mu;
   if (v.kind == VIVAL_BIT) {
      if (l->two_state)
         return mir_build_test(mu, v.val);
      if (v.is_std)
         return mir_build_cmp(mu, MIR_CMP_EQ, v.val, vi_u8(mu, 3));
      return mir_build_cmp(mu, MIR_CMP_NEQ,
                           mir_build_cast(mu, vi_u8_type(mu), v.val),
                           vi_u8(mu, 0));
   }
   if (v.kind == VIVAL_INT)
      return mir_build_cmp(mu, MIR_CMP_NEQ, vi_to_i64(mu, v.val), vi_i64(mu, 0));
   if (l->reason == NULL)
      l->reason = "lower: condition is neither bit nor integer";
   l->failed = true;
   return mir_const(mu, mir_bool_type(mu), 0);
}

static mir_value_t vi_cast_vec2(mir_unit_t *mu, mir_type_t vt, mir_value_t v)
{
   int64_t cval;
   if (mir_get_const(mu, v, &cval)) {
      const int w = mir_get_size(mu, vt);
      const bool sg = mir_get_signed(mu, vt);
      if (w <= 64) {
         const uint64_t mask = (w == 64)
            ? ~UINT64_C(0) : ((UINT64_C(1) << w) - 1u);
         return mir_const_vec(mu, vt, (uint64_t)cval & mask, 0);
      }
      const int nw = (w + 63) / 64;
      uint64_t *words LOCAL = xcalloc_array(nw, sizeof(uint64_t));
      words[0] = (uint64_t)cval;
      if (cval < 0) {
         for (int i = 1; i < nw; i++)
            words[i] = ~UINT64_C(0);
      }
      return vi_const_vec2(mu, w, sg, words);
   }
   return mir_build_cast(mu, vt, v);
}

// The two-state vector V as a MIR vector of its own width with signedness
// SG.  The signedness of the MIR type of V's value may differ from V's own
// flag (a slice or SIGNED()/UNSIGNED() keeps the bits of its operand), and
// MIR casts, comparisons and shifts take the signedness from the type.
static mir_value_t vi_vec2_as(vi_lower_t *l, const vi_val_t *v, bool sg)
{
   mir_unit_t *mu = l->mu;
   mir_type_t t = mir_get_type(mu, v->val);
   if (mir_get_signed(mu, t) == sg)
      return v->val;
   return mir_build_cast(mu, mir_vec2_type(mu, (int)v->width, sg), v->val);
}

static vi_val_t vi_from_bool_bit(vi_lower_t *l, mir_value_t bval, bool is_bool,
                                 bool is_std)
{
   mir_unit_t *mu = l->mu;
   if (l->two_state) {
      mir_value_t v1 = vi_cast_vec2(mu, mir_vec2_type(mu, 1, false), bval);
      return (vi_val_t){
         .kind = VIVAL_BIT, .val = v1, .width = 1,
         .is_std = is_std, .is_bool = is_bool,
      };
   }
   mir_value_t u8;
   if (is_std)
      u8 = mir_build_select(mu, vi_u8_type(mu), bval, vi_u8(mu, 3), vi_u8(mu, 2));
   else
      u8 = mir_build_select(mu, vi_u8_type(mu), bval, vi_u8(mu, 1), vi_u8(mu, 0));
   return (vi_val_t){
      .kind = VIVAL_BIT, .val = u8, .width = 1,
      .is_std = is_std, .is_bool = is_bool,
   };
}

static mir_value_t vi_to_vec2_1(vi_lower_t *l, vi_val_t v)
{
   mir_unit_t *mu = l->mu;
   if (v.kind == VIVAL_VEC && v.width == 1)
      return v.val;
   if (v.kind == VIVAL_BIT) {
      mir_type_t vt = mir_get_type(mu, v.val);
      if (!mir_is_null(vt) && mir_get_class(mu, vt) == MIR_TYPE_VEC2)
         return v.val;
      return vi_cast_vec2(mu, mir_vec2_type(mu, 1, false), v.val);
   }
   return vi_cast_vec2(mu, mir_vec2_type(mu, 1, false), v.val);
}

static mir_value_t vi_modeb_alloc_vec(vi_lower_t *l, uint32_t width)
{
   return mir_build_alloc(l->mu, vi_u8_type(l->mu), MIR_NULL_STAMP,
                          vi_off(l->mu, MAX(1u, width)));
}

static mir_value_t vi_state_ptr_at(vi_lower_t *l, uint32_t byte_off)
{
   return mir_build_array_ref(l->mu, l->state_ptr, vi_off(l->mu, byte_off));
}

// Base pointer and offset to read ISIG from: the island state, or the
// shadow copy when the current process also writes the signal
static mir_value_t vi_read_base(vi_lower_t *l, const vhdl_isl_sig_t *isig,
                                uint32_t *off)
{
   for (uint32_t i = 0; i < l->n_shadow; i++) {
      if (l->shadow_sig[i] == isig) {
         *off = 0;
         return l->shadow_ptr[i];
      }
   }
   *off = isig->a_off;
   return l->state_ptr;
}

static mir_value_t vi_read_ptr(vi_lower_t *l, const vhdl_isl_sig_t *isig)
{
   uint32_t off;
   mir_value_t base = vi_read_base(l, isig, &off);
   return mir_build_array_ref(l->mu, base, vi_off(l->mu, off));
}

static bool vi_lower_expr(vi_lower_t *l, tree_t expr, vi_val_t *out);
static bool vi_lower_stmt(vi_lower_t *l, tree_t stmt);
static bool vi_lower_inline_func_inner(vi_lower_t *l, tree_t fcall,
                                       tree_t func, vi_val_t *out,
                                       vi_local_t *ret_rec);
static bool vi_lower_inline_proc_inner(vi_lower_t *l, tree_t pcall);

// The code of a statement or expression carries its location, so that the
// stack trace of a report from a function the island calls (such as an
// IEEE metavalue warning) names the line and with it the fused process
// (see find_island_proc_loc in jit/jit-core.c), as with the process
// unfused.  The body of an inlined subprogram keeps the location of the
// call.
static const loc_t *vi_push_loc(vi_lower_t *l, tree_t t)
{
   const loc_t *old = l->loc;
   if (l->inline_depth == 0) {
      l->loc = tree_loc(t);
      mir_set_loc(l->mu, l->loc);
   }
   return old;
}

static void vi_pop_loc(vi_lower_t *l, const loc_t *old)
{
   if (l->inline_depth == 0 && old != NULL) {
      l->loc = old;
      mir_set_loc(l->mu, old);
   }
}

// True if evaluating EXPR cannot report anything (no function call that may
// warn, no index or range check): it can be evaluated when the LRM would
// skip it, as the right operand of a short-circuit operator
static bool vi_expr_quiet(tree_t expr, int depth)
{
   if (depth > 8)
      return false;
   switch (tree_kind(expr)) {
   case T_LITERAL:
   case T_REF:
      return true;
   case T_QUALIFIED:
      return vi_expr_quiet(tree_value(expr), depth + 1);
   case T_RECORD_REF:
      return vi_expr_quiet(tree_value(expr), depth + 1);
   case T_FCALL:
      {
         if (!tree_has_ref(expr))
            return false;
         switch (tree_subkind(tree_ref(expr))) {
         case S_SCALAR_EQ: case S_SCALAR_NEQ: case S_SCALAR_LT:
         case S_SCALAR_LE: case S_SCALAR_GT: case S_SCALAR_GE:
         case S_SCALAR_AND: case S_SCALAR_OR: case S_SCALAR_XOR:
         case S_SCALAR_NAND: case S_SCALAR_NOR: case S_SCALAR_XNOR:
         case S_SCALAR_NOT:
            break;
         default:
            return false;
         }
         const int np = tree_params(expr);
         for (int i = 0; i < np; i++) {
            if (!vi_expr_quiet(tree_value(tree_param(expr, i)), depth + 1))
               return false;
         }
         return true;
      }
   default:
      return false;
   }
}

static bool vi_lower_inline_func(vi_lower_t *l, tree_t fcall, tree_t func,
                                 vi_val_t *out, vi_local_t *ret_rec)
{
   l->inline_depth++;
   const bool ok = vi_lower_inline_func_inner(l, fcall, func, out, ret_rec);
   l->inline_depth--;
   return ok;
}

static bool vi_lower_inline_proc(vi_lower_t *l, tree_t pcall)
{
   l->inline_depth++;
   const bool ok = vi_lower_inline_proc_inner(l, pcall);
   l->inline_depth--;
   return ok;
}

// Speculative Mode A: the constant B as a value that MIR does not fold, for
// a branch whose untaken side must stay reachable in the CFG
static mir_value_t vi_spec_opaque_bool(vi_lower_t *l, bool b)
{
   mir_unit_t *mu = l->mu;
   if (mir_is_null(l->spec_var))
      l->spec_var = mir_add_var(mu, mir_offset_type(mu), MIR_NULL_STAMP,
                                ident_uniq("spec"), MIR_VAR_TEMP);
   mir_build_store(mu, l->spec_var, vi_off(mu, b));
   return mir_build_cmp(mu, MIR_CMP_NEQ, mir_build_load(mu, l->spec_var),
                        vi_off(mu, 0));
}

// Speculative Mode A: return deopt code KIND, after storing back the
// signals the current process assigned when it copied them
// (vi_emit_restore_copies)
static void vi_emit_deopt_return(vi_lower_t *l, rt_island_deopt_t kind)
{
   mir_unit_t *mu = l->mu;
   if (l->n_restore > 0) {
      mir_block_t copy_bb = mir_add_block(mu);
      mir_block_t ret_bb = mir_add_block(mu);
      mir_build_cond(mu, l->restore_cond, copy_bb, ret_bb);
      mir_set_cursor(mu, copy_bb, MIR_APPEND);
      for (uint32_t i = 0; i < l->n_restore; i++)
         mir_build_copy(mu, vi_state_ptr_at(l, l->restore_sig[i]->a_off),
                        l->restore_ptr[i], vi_off(mu, l->restore_bytes[i]));
      mir_build_jump(mu, ret_bb);
      mir_set_cursor(mu, ret_bb, MIR_APPEND);
   }
   mir_build_return(mu, vi_off(mu, kind));
}

// Speculative Mode A: return deopt code KIND from the function when OK is
// false.  The function has written nothing the caller keeps at that point:
// all values reach the state through stores that follow their computation.
static void vi_emit_deopt_unless(vi_lower_t *l, mir_value_t ok,
                                 rt_island_deopt_t kind)
{
   assert(l->spec);
   mir_unit_t *mu = l->mu;
   int64_t cval;
   if (mir_get_const(mu, ok, &cval) && cval != 0)
      return;

   mir_block_t deopt_bb = mir_add_block(mu);
   mir_block_t cont_bb = mir_add_block(mu);
   if (mir_get_const(mu, ok, &cval)) {
      // Keep the code after an unconditional stub reachable in the CFG so
      // the values it uses stay dominated by their definitions
      ok = vi_spec_opaque_bool(l, false);
   }
   mir_build_cond(mu, ok, cont_bb, deopt_bb);

   mir_set_cursor(mu, deopt_bb, MIR_APPEND);
   vi_emit_deopt_return(l, kind);

   mir_set_cursor(mu, cont_bb, MIR_APPEND);
}

static inline void vi_emit_deopt(vi_lower_t *l, rt_island_deopt_t kind)
{
   vi_emit_deopt_unless(l, mir_const(l->mu, mir_bool_type(l->mu), 0), kind);
}

// Speculative Mode A: deopt unless the i64 value V is in [LO, HI]
static void vi_spec_deopt_range(vi_lower_t *l, mir_value_t v, int64_t lo,
                                int64_t hi, rt_island_deopt_t kind)
{
   mir_unit_t *mu = l->mu;
   mir_type_t it = vi_i64_type(mu);
   v = mir_build_cast(mu, it, v);
   mir_value_t ok = mir_build_and(
      mu, mir_build_cmp(mu, MIR_CMP_GEQ, v, mir_const(mu, it, lo)),
      mir_build_cmp(mu, MIR_CMP_LEQ, v, mir_const(mu, it, hi)));
   vi_emit_deopt_unless(l, ok, kind);
}

// Speculative Mode A: deopt unless the integer V fits a WIDTH-bit vector of
// the given signedness, which is when the IEEE conversion to the vector is
// exact and silent
static void vi_spec_deopt_int_fit(vi_lower_t *l, mir_value_t v, uint32_t width,
                                  bool is_signed)
{
   if (width >= 64 && is_signed)
      return;
   else if (width >= 64)
      vi_spec_deopt_range(l, v, 0, INT64_MAX, RT_ISLAND_DEOPT_CONV);
   else if (is_signed)
      vi_spec_deopt_range(l, v, -(INT64_C(1) << (width - 1)),
                          (INT64_C(1) << (width - 1)) - 1,
                          RT_ISLAND_DEOPT_CONV);
   else
      vi_spec_deopt_range(l, v, 0, (INT64_C(1) << width) - 1,
                          RT_ISLAND_DEOPT_CONV);
}

// Speculative Mode A: a relational operator between a vector of WIDTH bits
// and an integer constant that does not fit the vector.  The IEEE numeric
// package (all versions) return before any conversion and without a warning
// once the vector has no metavalue, which is always the case in Mode A, so
// the result is the mathematical comparison and the vector can be ignored.  Returns false if IV is not such a constant.
static bool vi_spec_fold_int_cmp(vi_lower_t *l, mir_vec_op_t op, bool int_left,
                                 vi_val_t iv, uint32_t width, bool is_signed,
                                 bool *res)
{
   int64_t c;
   if (!mir_get_const(l->mu, vi_to_i64(l->mu, iv.val), &c))
      return false;

   bool above;   // the integer is greater than any vector value
   if (is_signed && width < 64)
      above = (c > (INT64_C(1) << (width - 1)) - 1);
   else if (!is_signed && width < 63)
      above = (c > (INT64_C(1) << width) - 1);
   else
      return false;

   bool below = false;
   if (is_signed)
      below = (c < -(INT64_C(1) << (width - 1)));
   else if (c < 0)
      return false;   // Not a NATURAL

   if (!above && !below)
      return false;

   // Normalise to "vector OP integer"
   if (int_left) {
      switch (op) {
      case MIR_VEC_LT:  op = MIR_VEC_GT;  break;
      case MIR_VEC_LEQ: op = MIR_VEC_GEQ; break;
      case MIR_VEC_GT:  op = MIR_VEC_LT;  break;
      case MIR_VEC_GEQ: op = MIR_VEC_LEQ; break;
      default: break;
      }
   }

   switch (op) {
   case MIR_VEC_LT:
   case MIR_VEC_LEQ:      *res = above; return true;
   case MIR_VEC_GT:
   case MIR_VEC_GEQ:      *res = below; return true;
   case MIR_VEC_CASE_EQ:  *res = false; return true;
   case MIR_VEC_CASE_NEQ: *res = true;  return true;
   default:               return false;
   }
}

// Speculative Mode A: deopt unless the integer result IV of FCALL is in
// the range of its type, where the IEEE function fails or warns otherwise
static void vi_spec_deopt_result(vi_lower_t *l, tree_t fcall, mir_value_t iv)
{
   type_t rt = tree_type(fcall);
   tree_t r = type_is_integer(rt) ? range_of(rt, 0) : NULL;
   int64_t lo = INT32_MIN, hi = INT32_MAX;
   if (r != NULL)
      folded_bounds(r, &lo, &hi);
   vi_spec_deopt_range(l, iv, lo, hi, RT_ISLAND_DEOPT_CONV);
}

// Speculative Mode A: Mode B reports the division by zero
static void vi_spec_deopt_zero(vi_lower_t *l, mir_value_t divisor)
{
   mir_unit_t *mu = l->mu;
   vi_emit_deopt_unless(l, mir_build_cmp(mu, MIR_CMP_NEQ,
                                         vi_to_i64(mu, divisor), vi_i64(mu, 0)),
                        RT_ISLAND_DEOPT_RANGE);
}

// Speculative Mode A clean tracking (see RT_ISLAND_SPEC_HELD_OFF in
// rt/island.h): every load or store of a packed signal first tests its held
// byte, and every access to a row of a 2D signal its row clean byte, so the
// variant never computes with nor overwrites part of a value that is not
// all '0'/'1'.  The runtime then runs the island in Mode B, which propagates
// the metavalue exactly.

static void vi_spec_check_held(vi_lower_t *l, const vhdl_isl_sig_t *isig)
{
   if (!l->spec_checks || !(isig->rt_flags & RT_ISIG_TWO_STATE))
      return;
   mir_unit_t *mu = l->mu;
   const uint32_t idx = (uint32_t)(isig - l->isl->sigs);
   assert(idx < l->isl->n_sigs);
   const uint32_t off = RT_ISLAND_SPEC_HELD_OFF(l->isl->hdr_bytes) + idx;
   mir_value_t h = mir_build_load(mu, vi_state_ptr_at(l, off));
   vi_emit_deopt_unless(l, mir_build_cmp(mu, MIR_CMP_EQ, h, vi_u8(mu, 0)),
                        RT_ISLAND_DEOPT_UNCLEAN);
}

// A flip-flop function stores a whole value into the packed signal ISIG:
// when it is held, set its request byte so the commit packs it again (see
// RT_ISLAND_SPEC_REQ_OFF)
static void vi_spec_store_held(vi_lower_t *l, const vhdl_isl_sig_t *isig)
{
   if (!l->spec_checks || !(isig->rt_flags & RT_ISIG_TWO_STATE))
      return;
   const uint32_t idx = (uint32_t)(isig - l->isl->sigs);
   // Only for a signal no other domain writes: a deopt of the domain then
   // drops exactly the requests of its call
   unsigned nw = 0;
   for (uint32_t d = 0; d < l->isl->n_doms; d++) {
      for (uint32_t w = 0; w < l->isl->doms[d].n_write_sigs; w++)
         nw += (l->isl->doms[d].write_isl_sigs[w] == idx);
   }
   if (nw != 1 || (isig->rt_flags & RT_ISIG_COMB_WRITTEN)) {
      vi_spec_check_held(l, isig);
      return;
   }
   mir_unit_t *mu = l->mu;
   const uint32_t hdr = l->isl->hdr_bytes;
   mir_value_t h = mir_build_load(
      mu, vi_state_ptr_at(l, RT_ISLAND_SPEC_HELD_OFF(hdr) + idx));
   mir_build_store(
      mu, vi_state_ptr_at(l, RT_ISLAND_SPEC_REQ_OFF(hdr, l->isl->n_sigs) + idx),
      h);
}

// Byte offset of the row clean bytes of ISIG in the state half at HALF_OFF
static uint32_t vi_spec_rowf_off(const vi_lower_t *l, const vhdl_isig_t *s,
                                 const vhdl_isl_sig_t *isig, uint32_t half_off)
{
   (void)l;
   const uint32_t nwords = (s->width + 63u) / 64u;
   return isig->a_off + half_off + nwords * s->nelems * 8u;
}

static inline bool vi_spec_has_rowf(const vi_lower_t *l,
                                    const vhdl_isl_sig_t *isig)
{
   return l->spec_checks && (isig->rt_flags & RT_ISIG_SPEC_ROWF);
}

static mir_value_t vi_spec_rowf_ptr(vi_lower_t *l, const vhdl_isig_t *s,
                                    const vhdl_isl_sig_t *isig,
                                    uint32_t half_off, mir_value_t row)
{
   mir_unit_t *mu = l->mu;
   mir_value_t off = mir_build_add(mu, mir_offset_type(mu),
                                   vi_off(mu, vi_spec_rowf_off(l, s, isig,
                                                               half_off)),
                                   vi_to_off(mu, row));
   return mir_build_array_ref(mu, l->state_ptr, off);
}

// Deopt when row ROW of ISIG in the state half at HALF_OFF is held
static void vi_spec_check_row(vi_lower_t *l, const vhdl_isig_t *s,
                              const vhdl_isl_sig_t *isig, uint32_t half_off,
                              mir_value_t row)
{
   if (!vi_spec_has_rowf(l, isig))
      return;
   mir_unit_t *mu = l->mu;
   mir_value_t f = mir_build_load(mu, vi_spec_rowf_ptr(l, s, isig, half_off,
                                                       row));
   vi_emit_deopt_unless(l, mir_build_cmp(mu, MIR_CMP_EQ, f, vi_u8(mu, 0)),
                        RT_ISLAND_DEOPT_ROW);
}

// A store of the whole of row ROW: the row is clean
static void vi_spec_clear_row(vi_lower_t *l, const vhdl_isig_t *s,
                              const vhdl_isl_sig_t *isig, uint32_t half_off,
                              mir_value_t row)
{
   if (!vi_spec_has_rowf(l, isig))
      return;
   mir_build_store(l->mu, vi_spec_rowf_ptr(l, s, isig, half_off, row),
                   vi_u8(l->mu, 0));
}

// A whole-array load (CLEAR false: deopt when any row is held) or store
// (CLEAR true: every row is clean) of the 2D signal ISIG
static void vi_spec_all_rows(vi_lower_t *l, const vhdl_isig_t *s,
                             const vhdl_isl_sig_t *isig, uint32_t half_off,
                             bool clear)
{
   if (!vi_spec_has_rowf(l, isig))
      return;
   mir_unit_t *mu = l->mu;
   mir_type_t t64 = mir_vec2_type(mu, 64, false);
   const uint32_t base = vi_spec_rowf_off(l, s, isig, half_off);
   const uint32_t nw = RT_ISLAND_SPEC_ROWF_BYTES(s->nelems) / 8u;

   mir_value_t k_v = mir_add_var(mu, mir_offset_type(mu), MIR_NULL_STAMP,
                                 ident_uniq("rowf"), MIR_VAR_TEMP);
   mir_build_store(mu, k_v, vi_off(mu, 0));
   mir_block_t hdr = mir_add_block(mu);
   mir_block_t body = mir_add_block(mu);
   mir_block_t done = mir_add_block(mu);
   mir_build_jump(mu, hdr);

   mir_set_cursor(mu, hdr, MIR_APPEND);
   mir_value_t k = mir_build_load(mu, k_v);
   mir_build_cond(mu, mir_build_cmp(mu, MIR_CMP_LT, k, vi_off(mu, nw)),
                  body, done);

   mir_set_cursor(mu, body, MIR_APPEND);
   if (clear)
      mir_build_island_store(mu, l->state_ptr, vi_off(mu, base),
                             vi_off(mu, -1), k, mir_const_vec(mu, t64, 0, 0));
   else {
      mir_value_t w = mir_build_island_load(mu, t64, l->state_ptr,
                                            vi_off(mu, base), vi_off(mu, -1),
                                            k);
      vi_emit_deopt_unless(l, mir_build_cmp(mu, MIR_CMP_EQ,
                                            mir_build_cast(mu, vi_i64_type(mu),
                                                           w),
                                            vi_i64(mu, 0)),
                           RT_ISLAND_DEOPT_ROW);
   }
   mir_build_store(mu, k_v, mir_build_add(mu, mir_offset_type(mu), k,
                                          vi_off(mu, 1)));
   mir_build_jump(mu, hdr);

   mir_set_cursor(mu, done, MIR_APPEND);
}

// True if BYTES (std_logic positions) holds a value other than '0' and '1'
static bool vi_bytes_non01(const uint8_t *bytes, size_t n)
{
   for (size_t i = 0; i < n; i++) {
      if (bytes[i] != 2 && bytes[i] != 3)
         return true;
   }
   return false;
}

static bool vi_lower_const_vec_eval(vi_lower_t *l, tree_t expr, type_t hint_t,
                                    uint8_t *bytes_out, uint32_t width);

// Speculative Mode A: a case choice with a std_logic value other than '0'
// and '1' cannot match the selector, which only holds '0' and '1'
static bool vi_spec_choice_non01(vi_lower_t *l, tree_t choice, vi_val_t sel)
{
   if (!sel.is_std)
      return false;
   if (sel.kind == VIVAL_BIT) {
      if (tree_kind(choice) != T_REF || !tree_has_ref(choice)
          || tree_kind(tree_ref(choice)) != T_ENUM_LIT)
         return false;
      const unsigned pos = tree_pos(tree_ref(choice));
      return pos != 2 && pos != 3;
   }
   else if (sel.kind == VIVAL_VEC && sel.width > 0) {
      uint8_t *bytes LOCAL = xmalloc_array(sel.width, 1);
      return vi_lower_const_vec_eval(l, choice, tree_type(choice), bytes,
                                     sel.width)
         && vi_bytes_non01(bytes, sel.width);
   }
   return false;
}

// Record that the combinational process being lowered reads ISIG: the
// process is skipped when none of the signals it reads changed
static void vi_note_read(vi_lower_t *l, const vhdl_isl_sig_t *isig)
{
   if (!l->act_track)
      return;
   const uint32_t idx = (uint32_t)(isig - l->isl->sigs);
   assert(idx < l->isl->n_sigs);
   for (uint32_t i = 0; i < l->n_act_rd; i++) {
      if (l->act_rd[i] == idx)
         return;
   }
   if (l->n_act_rd == l->cap_act_rd) {
      l->cap_act_rd = MAX(l->cap_act_rd * 2u, 16u);
      l->act_rd = xrealloc_array(l->act_rd, l->cap_act_rd, sizeof(uint32_t));
   }
   l->act_rd[l->n_act_rd++] = idx;
}

static mir_value_t vi_compute_elem_pos(vi_lower_t *l, vi_val_t base,
                                       mir_value_t idx_val, uint32_t elem_w)
{
   mir_unit_t *mu = l->mu;
   mir_value_t idx = vi_to_i64(mu, idx_val);
   mir_value_t pos;
   if (base.dir == RANGE_TO)
      pos = mir_build_sub(mu, vi_i64_type(mu), idx, vi_i64(mu, base.left));
   else
      pos = mir_build_sub(mu, vi_i64_type(mu), vi_i64(mu, base.left), idx);
   pos = vi_to_off(mu, pos);
   if (elem_w > 0) {
      // Clamp out-of-range index cleanly in runtime
      mir_value_t ok_lo = mir_build_cmp(mu, MIR_CMP_GEQ, pos, vi_off(mu, 0));
      mir_value_t ok_hi = mir_build_cmp(mu, MIR_CMP_LT, pos, vi_off(mu, elem_w));
      mir_value_t in_bounds = mir_build_and(mu, ok_lo, ok_hi);
      pos = mir_build_select(mu, mir_offset_type(mu), in_bounds, pos,
                             vi_off(mu, 0));
   }
   return pos;
}

static const vhdl_isig_t *vi_resolve_expr_sig(vi_lower_t *l, tree_t expr,
                                             const vhdl_isl_sig_t **isig_out)
{
   tree_t decl = NULL;
   int path[VHDL_ISIG_MAX_DEPTH];
   uint8_t plen = 0;
   if (!vhdl_island_extract_path_ctx(l->ctx, expr, &decl, path, &plen))
      return NULL;
   int gid = vhdl_island_lookup_leaf(l->ctx, decl, path, plen);
   if (gid < 0)
      return NULL;
   const vhdl_isl_sig_t *isig = vhdl_island_find_sig(l->isl, (uint32_t)gid);
   if (isig == NULL)
      return NULL;
   if (isig_out != NULL)
      *isig_out = isig;
   return &(l->ctx->sigs[gid]);
}

static bool vi_lower_sig_load(vi_lower_t *l, const vhdl_isig_t *s,
                              const vhdl_isl_sig_t *isig,
                              mir_value_t elem_idx, vi_val_t *out)
{
   mir_unit_t *mu = l->mu;
   vi_note_read(l, isig);
   uint32_t rd_off;
   mir_value_t rd_base = vi_read_base(l, isig, &rd_off);
   if (s->elem_kind == VISIG_ENUM || s->elem_kind == VISIG_INT) {
      mir_value_t iv;
      if (s->byte_size == 1 && !s->is_signed) {
         mir_value_t ptr = mir_build_array_ref(mu, rd_base, vi_off(mu, rd_off));
         mir_value_t raw = mir_build_load(mu, ptr);
         iv = mir_build_cast(mu, vi_i64_type(mu), raw);
      }
      else {
         mir_type_t vt = mir_vec2_type(mu, (int)s->byte_size * 8, s->is_signed);
         mir_value_t raw = mir_build_island_load(mu, vt, rd_base,
                                                 vi_off(mu, rd_off),
                                                 vi_off(mu, -1), vi_off(mu, 0));
         iv = mir_build_cast(mu, vi_i64_type(mu), raw);
      }
      *out = (vi_val_t){
         .kind      = VIVAL_INT,
         .val       = iv,
         .width     = 1,
         .is_signed = s->is_signed,
      };
      return true;
   }

   if (l->two_state) {
      vi_spec_check_held(l, isig);
      if (s->is_array2d)
         vi_spec_check_row(l, s, isig, 0, elem_idx);
      mir_value_t r = mir_build_island_load(
         mu, mir_vec2_type(mu, s->width, s->is_signed),
         rd_base, vi_off(mu, rd_off), vi_off(mu, -1), elem_idx);
      *out = (vi_val_t){
         .kind      = s->is_vector ? VIVAL_VEC : VIVAL_BIT,
         .val       = r,
         .width     = s->width,
         .is_signed = s->is_signed,
         .is_std    = (s->elem_kind == VISIG_STD_LOGIC),
         .is_bool   = (s->elem_kind == VISIG_BOOL),
         .dir       = s->dir,
         .left      = s->left,
         .right     = s->right,
      };
      return true;
   }

   if (!s->is_vector && !s->is_array2d) {
      mir_value_t ptr = mir_build_array_ref(mu, rd_base, vi_off(mu, rd_off));
      mir_value_t b = mir_build_load(mu, ptr);
      *out = (vi_val_t){
         .kind    = VIVAL_BIT,
         .val     = b,
         .width   = 1,
         .is_std  = (s->elem_kind == VISIG_STD_LOGIC),
         .is_bool = (s->elem_kind == VISIG_BOOL),
      };
      return true;
   }

   mir_value_t byte_off = vi_off(mu, rd_off);
   if (s->is_array2d) {
      mir_value_t mul = mir_build_mul(mu, mir_offset_type(mu), elem_idx,
                                      vi_off(mu, s->width));
      byte_off = mir_build_add(mu, mir_offset_type(mu), byte_off, mul);
   }
   mir_value_t ptr = mir_build_array_ref(mu, rd_base, byte_off);
   *out = (vi_val_t){
      .kind      = VIVAL_VEC,
      .val       = ptr,
      .width     = s->width,
      .is_signed = s->is_signed,
      .is_std    = (s->elem_kind == VISIG_STD_LOGIC),
      .dir       = s->dir,
      .left      = s->left,
      .right     = s->right,
   };
   return true;
}

static bool vi_lower_const_vec_eval(vi_lower_t *l, tree_t expr, type_t hint_t,
                                    uint8_t *bytes_out, uint32_t width)
{
   switch (tree_kind(expr)) {
   case T_STRING:
      if ((uint32_t)tree_chars(expr) != width)
         return false;
      for (uint32_t i = 0; i < width; i++) {
         tree_t ch = tree_char(expr, i);
         if (tree_kind(ch) != T_REF || tree_kind(tree_ref(ch)) != T_ENUM_LIT)
            return false;
         bytes_out[i] = (uint8_t)tree_pos(tree_ref(ch));
      }
      return true;

   case T_REF:
      {
         tree_t d = tree_ref(expr);
         if (tree_kind(d) == T_CONST_DECL && tree_has_value(d))
            return vi_lower_const_vec_eval(l, tree_value(d), tree_type(d),
                                           bytes_out, width);
         return false;
      }

   case T_QUALIFIED:
   case T_TYPE_CONV:
      return vi_lower_const_vec_eval(l, tree_value(expr), tree_type(expr),
                                     bytes_out, width);

   case T_AGGREGATE:
      {
         range_kind_t dir = RANGE_DOWNTO;
         int64_t left = (int64_t)width - 1, right = 0;
         uint32_t w = width;
         if (hint_t != NULL && type_is_array(hint_t) && !type_is_unconstrained(hint_t))
            vi_eval_type_bounds(l, hint_t, &dir, &left, &right, &w);
         if (w != width)
            return false;

         uint8_t fill = 0;
         bool has_others = false;
         const int na = tree_assocs(expr);
         for (int i = 0; i < na; i++) {
            tree_t a = tree_assoc(expr, i);
            if (tree_subkind(a) == A_OTHERS) {
               tree_t v = tree_value(a);
               if (tree_kind(v) == T_REF && tree_kind(tree_ref(v)) == T_ENUM_LIT) {
                  fill = (uint8_t)tree_pos(tree_ref(v));
                  has_others = true;
               }
               else
                  return false;
            }
         }
         if (has_others)
            memset(bytes_out, fill, width);

         for (int i = 0; i < na; i++) {
            tree_t a = tree_assoc(expr, i);
            switch (tree_subkind(a)) {
            case A_OTHERS:
               break;
            case A_POS:
               {
                  tree_t v = tree_value(a);
                  if (tree_kind(v) == T_REF
                      && tree_kind(tree_ref(v)) == T_ENUM_LIT
                      && (uint32_t)i < width)
                     bytes_out[i] = (uint8_t)tree_pos(tree_ref(v));
                  else
                     return false;
               }
               break;
            case A_NAMED:
               {
                  int64_t k;
                  tree_t v = tree_value(a);
                  if (!vi_eval_const_int(l, tree_name(a), &k)
                      || tree_kind(v) != T_REF
                      || tree_kind(tree_ref(v)) != T_ENUM_LIT)
                     return false;
                  int64_t pos = (dir == RANGE_TO) ? (k - left) : (left - k);
                  if (pos < 0 || pos >= width)
                     return false;
                  bytes_out[pos] = (uint8_t)tree_pos(tree_ref(v));
               }
               break;
            case A_RANGE:
               {
                  tree_t rg = tree_range(a, 0);
                  int64_t rl, rr;
                  tree_t v = tree_value(a);
                  if (!vi_eval_const_int(l, tree_left(rg), &rl)
                      || !vi_eval_const_int(l, tree_right(rg), &rr)
                      || tree_kind(v) != T_REF
                      || tree_kind(tree_ref(v)) != T_ENUM_LIT)
                     return false;
                  uint8_t bv = (uint8_t)tree_pos(tree_ref(v));
                  int64_t p0 = (dir == RANGE_TO) ? (rl - left) : (left - rl);
                  int64_t p1 = (dir == RANGE_TO) ? (rr - left) : (left - rr);
                  if (p0 > p1) { int64_t tmp = p0; p0 = p1; p1 = tmp; }
                  if (p0 < 0 || p1 >= width)
                     return false;
                  for (int64_t p = p0; p <= p1; p++)
                     bytes_out[p] = bv;
               }
               break;
            default:
               return false;
            }
         }
         return true;
      }

   default:
      return false;
   }
}

static void vi_pack_bytes_to_words(const uint8_t *bytes, uint32_t width,
                                   bool is_std, uint64_t *words_out)
{
   const uint32_t nw = (width + 63u) / 64u;
   memset(words_out, 0, nw * sizeof(uint64_t));
   for (uint32_t i = 0; i < width; i++) {
      const uint8_t b = is_std ? std_enum_to_2bit(bytes[i]) : (bytes[i] & 1u);
      if (b) {
         const uint32_t lsb = width - 1u - i;
         words_out[lsb / 64u] |= UINT64_C(1) << (lsb & 63u);
      }
   }
}

static void vi_check_int_bounds(vi_lower_t *l, type_t target_type,
                                mir_value_t val64, tree_t actual_expr,
                                tree_t hint_decl)
{
   if (target_type == NULL || !type_is_integer(target_type))
      return;
   tree_t fr = range_of(target_type, 0);
   int64_t f_lo, f_hi;
   if (fr == NULL || !folded_bounds(fr, &f_lo, &f_hi))
      return;

   if (actual_expr != NULL) {
      int64_t cv;
      if (vi_eval_const_int(l, actual_expr, &cv)
          && cv >= f_lo && cv <= f_hi)
         return;

      type_t at = tree_type(actual_expr);
      if (at != NULL && type_is_integer(at)) {
         tree_t ar = range_of(at, 0);
         int64_t a_lo, a_hi;
         if (ar != NULL && folded_bounds(ar, &a_lo, &a_hi)
             && a_lo >= f_lo && a_hi <= f_hi)
            return;
      }
   }

   if (l->spec) {
      // Mode B reports the failure
      vi_spec_deopt_range(l, val64, f_lo, f_hi, RT_ISLAND_DEOPT_RANGE);
      return;
   }

   mir_unit_t *mu = l->mu;
   mir_type_t it = vi_i64_type(mu);
   mir_value_t left = mir_const(mu, it, f_lo);
   mir_value_t right = mir_const(mu, it, f_hi);
   mir_value_t dir = mir_const(mu, mir_bool_type(mu), RANGE_TO);
   tree_t loc_tree = (actual_expr != NULL) ? actual_expr : hint_decl;
   mir_value_t locus = mir_build_debug_locus(mu, tree_to_object(loc_tree));
   mir_value_t hint = (hint_decl != NULL)
      ? mir_build_debug_locus(mu, tree_to_object(hint_decl)) : locus;
   mir_build_range_check(mu, val64, left, right, dir, locus, hint);
}

// The range check of a scalar conversion such as T'VAL, as lower.c makes
// it: against the base type, then the subtype
static bool vi_check_scalar_bounds(vi_lower_t *l, type_t type,
                                   mir_value_t val64, tree_t where)
{
   tree_t r = range_of(type, 0);
   int64_t low, high, left, right;
   if (r == NULL || tree_kind(r) != T_RANGE
       || !folded_bounds(r, &low, &high)
       || !folded_int(tree_left(r), &left)
       || !folded_int(tree_right(r), &right))
      return false;
   int64_t cv;
   if (mir_get_const(l->mu, val64, &cv) && cv >= low && cv <= high)
      return true;
   if (type_kind(type) == T_SUBTYPE
       && !vi_check_scalar_bounds(l, type_base(type), val64, where))
      return false;
   if (l->spec) {
      vi_spec_deopt_range(l, val64, low, high, RT_ISLAND_DEOPT_RANGE);
      return true;
   }
   mir_unit_t *mu = l->mu;
   mir_type_t it = vi_i64_type(mu);
   mir_value_t locus = mir_build_debug_locus(mu, tree_to_object(where));
   mir_build_range_check(mu, val64, mir_const(mu, it, left),
                         mir_const(mu, it, right),
                         mir_const(mu, mir_bool_type(mu), tree_subkind(r)),
                         locus, locus);
   return true;
}

// Mode A: turn a packed vector into the byte-per-element form the IEEE
// image functions take.  Only 0 and 1 exist in Mode A, so std_logic elements
// become '0' and '1' and bit elements the same positions of BIT.
static bool vi_vec2_to_bytes(vi_lower_t *l, vi_val_t *v)
{
   if (v->kind != VIVAL_VEC)
      return false;

   mir_unit_t *mu = l->mu;
   mir_value_t buf = mir_build_alloc(mu, vi_u8_type(mu), MIR_NULL_STAMP,
                                     vi_off(mu, v->width));
   mir_value_t idx_v = mir_add_var(mu, mir_offset_type(mu), MIR_NULL_STAMP,
                                   ident_uniq("v2b"), MIR_VAR_TEMP);
   mir_build_store(mu, idx_v, vi_off(mu, 0));
   mir_block_t hdr = mir_add_block(mu);
   mir_block_t body = mir_add_block(mu);
   mir_block_t done = mir_add_block(mu);
   mir_build_jump(mu, hdr);

   mir_set_cursor(mu, hdr, MIR_APPEND);
   mir_value_t i = mir_build_load(mu, idx_v);
   mir_build_cond(mu, mir_build_cmp(mu, MIR_CMP_LT, i, vi_off(mu, v->width)),
                  body, done);

   mir_set_cursor(mu, body, MIR_APPEND);
   mir_value_t bit = mir_build_extract(mu, mir_vec2_type(mu, 1, false),
                                       v->val, i);
   mir_value_t b = mir_build_select(mu, vi_u8_type(mu), mir_build_test(mu, bit),
                                    vi_u8(mu, v->is_std ? 3 : 1),
                                    vi_u8(mu, v->is_std ? 2 : 0));
   mir_build_store(mu, mir_build_array_ref(mu, buf, i), b);
   mir_build_store(mu, idx_v, mir_build_add(mu, mir_offset_type(mu),
                                            i, vi_off(mu, 1)));
   mir_build_jump(mu, hdr);

   mir_set_cursor(mu, done, MIR_APPEND);
   v->val = buf;
   return true;
}

static bool vi_modeb_call_ieee(vi_lower_t *l, tree_t decl,
                               const vi_val_t *pvals, int np,
                               mir_type_t mret, mir_value_t *ret_out)
{
   mir_unit_t *mu = l->mu;
   const char *base_s = istr(tree_ident2(decl));
   const char *paren = strchr(base_s, '(');
   if (paren == NULL)
      return false;
   const char *dot = paren;
   while (dot > base_s && *dot != '.')
      dot--;
   if (*dot != '.')
      return false;
   ident_t pkg_id = ident_new_n(base_s, (size_t)(dot - base_s));
   mir_value_t pkg_ctx = mir_build_link_package(mu, pkg_id);

   SCOPED_A(mir_value_t) args = AINIT;
   APUSH(args, pkg_ctx);
   for (int i = 0; i < np; i++) {
      const vi_val_t pv = pvals[i];
      tree_t fport = tree_port(decl, i);
      type_t pt = tree_type(fport);
      if (type_is_array(pt)) {
         if (pv.kind != VIVAL_VEC) {
            ACLEAR(args);
            return false;
         }
         mir_dim_t dim = {
            .left  = vi_off(mu, pv.left),
            .right = vi_off(mu, pv.right),
            .dir   = mir_const(mu, mir_bool_type(mu),
                               pv.dir == RANGE_DOWNTO ? 1 : 0),
         };
         APUSH(args, mir_build_wrap(mu, pv.val, &dim, 1));
      }
      else if (vi_type_is_bool(pt)) {
         APUSH(args, vi_to_bool(l, pv));
      }
      else if (vi_type_is_std_logic(pt) || type_is_enum(pt)) {
         APUSH(args, mir_build_cast(mu, vi_u8_type(mu), pv.val));
      }
      else {
         APUSH(args, mir_build_cast(mu, mir_int_type(mu, INT32_MIN, INT32_MAX),
                                    vi_to_i64(mu, pv.val)));
      }
   }

   *ret_out = mir_build_fcall(mu, tree_ident2(decl), mret, MIR_NULL_STAMP,
                              args.items, args.count);
   ACLEAR(args);
   return true;
}

typedef enum { VIM_EQ, VIM_NEQ, VIM_LT, VIM_LE, VIM_GT, VIM_GE } vi_match_op_t;

// Mode A matching comparison.  There are no metavalues in Mode A, so STD_MATCH
// and the matching operators compare exactly like "=" and the ordering
// operators.  BOOL_RESULT selects a boolean result (STD_MATCH) instead of a
// bit or std_ulogic one (the matching operators).
static bool vi_lower_match_cmp(vi_lower_t *l, vi_match_op_t op, vi_val_t a,
                               vi_val_t b, bool bool_result, bool is_std,
                               vi_val_t *out)
{
   mir_unit_t *mu = l->mu;
   mir_value_t c;
   if (a.kind == VIVAL_BIT && b.kind == VIVAL_BIT) {
      const mir_cmp_t cmp = (op == VIM_EQ) ? MIR_CMP_EQ
         : (op == VIM_NEQ) ? MIR_CMP_NEQ
         : (op == VIM_LT) ? MIR_CMP_LT
         : (op == VIM_LE) ? MIR_CMP_LEQ
         : (op == VIM_GT) ? MIR_CMP_GT : MIR_CMP_GEQ;
      c = mir_build_cmp(mu, cmp,
                        mir_build_cast(mu, vi_u8_type(mu), vi_to_vec2_1(l, a)),
                        mir_build_cast(mu, vi_u8_type(mu), vi_to_vec2_1(l, b)));
   }
   else if (a.kind == VIVAL_VEC && b.kind == VIVAL_VEC
            && (op == VIM_EQ || op == VIM_NEQ)) {
      if (a.width != b.width)
         c = mir_const(mu, mir_bool_type(mu), op == VIM_NEQ);
      else
         c = mir_build_binary(mu, (op == VIM_EQ)
                              ? MIR_VEC_CASE_EQ : MIR_VEC_CASE_NEQ,
                              mir_vec2_type(mu, (int)a.width, false),
                              a.val, b.val);
   }
   else
      return false;

   *out = bool_result ? vi_from_bool_bit(l, c, true, false)
      : vi_from_bool_bit(l, c, false, is_std);
   return true;
}

static bool vi_match_op_from_name(const char *fn, vi_match_op_t *op)
{
   if (fn_name_is(fn, "\"?=\""))        *op = VIM_EQ;
   else if (fn_name_is(fn, "\"?/=\"")) *op = VIM_NEQ;
   else if (fn_name_is(fn, "\"?<\""))  *op = VIM_LT;
   else if (fn_name_is(fn, "\"?<=\"")) *op = VIM_LE;
   else if (fn_name_is(fn, "\"?>\""))  *op = VIM_GT;
   else if (fn_name_is(fn, "\"?>=\"")) *op = VIM_GE;
   else
      return false;
   return true;
}

static bool vi_lower_std_fcall(vi_lower_t *l, tree_t fcall, tree_t decl,
                               const char *pkg, const char *fn, vi_val_t *out)
{
   mir_unit_t *mu = l->mu;
   const int np = tree_params(fcall);
   type_t rtype = tree_type(fcall);
   const bool ret_signed = vi_type_is_signed_vec(rtype);

   if (!l->two_state) {
      // Mode B: inline scalar 1-bit ops and zero-loop RESIZE/SHIFT; delegate
      // vector bitwise and NUMERIC_STD arithmetic/compare/conv to IEEE fcall
      // so NVC's AVX2 SIMD jit-intrin.c fast-paths execute directly.
      if (np == 1 && fn_name_is(fn, "\"??\"")) {
         vi_val_t a;
         if (!vi_lower_expr(l, tree_value(tree_param(fcall, 0)), &a)
             || a.kind != VIVAL_BIT)
            return false;
         mir_value_t av = mir_build_cast(mu, vi_u8_type(mu), a.val);
         mir_value_t is1 = mir_build_or(mu,
                                        mir_build_cmp(mu, MIR_CMP_EQ, av, vi_u8(mu, 3)),
                                        mir_build_cmp(mu, MIR_CMP_EQ, av, vi_u8(mu, 7)));
         *out = vi_from_bool_bit(l, is1, true, false);
         return true;
      }

      const bool is_std1164 = (strcmp(pkg, "IEEE.STD_LOGIC_1164.") == 0);
      const bool is_num_pkg = (strcmp(pkg, "IEEE.NUMERIC_STD.") == 0
                               || strcmp(pkg, "IEEE.NUMERIC_STD_UNSIGNED.") == 0
                               || strcmp(pkg, "IEEE.STD_LOGIC_UNSIGNED.") == 0
                               || strcmp(pkg, "IEEE.STD_LOGIC_SIGNED.") == 0
                               || strcmp(pkg, "IEEE.STD_LOGIC_ARITH.") == 0);

      // Lower all arguments once
      vi_val_t *pvals LOCAL = xcalloc_array(MAX(1, np), sizeof(vi_val_t));
      vi_val_t first_vec = { 0 };
      const int nports = tree_ports(decl);
      for (int i = 0; i < np; i++) {
         tree_t pe = tree_value(tree_param(fcall, i));
         if (!vi_lower_expr(l, pe, &pvals[i]))
            return false;
         if (pvals[i].kind == VIVAL_INT && i < nports) {
            tree_t fp = tree_port(decl, i);
            vi_check_int_bounds(l, tree_type(fp), vi_to_i64(mu, pvals[i].val),
                                pe, fp);
         }
         if (pvals[i].kind == VIVAL_VEC && first_vec.width == 0)
            first_vec = pvals[i];
      }

      if (np == 1 && fn_name_is(fn, "IS_X") && pvals[0].kind == VIVAL_BIT) {
         mir_value_t av = mir_build_cast(mu, vi_u8_type(mu), pvals[0].val);
         mir_value_t ok01 = mir_build_or(
            mu,
            mir_build_or(mu,
                         mir_build_cmp(mu, MIR_CMP_EQ, av, vi_u8(mu, 2)),
                         mir_build_cmp(mu, MIR_CMP_EQ, av, vi_u8(mu, 3))),
            mir_build_or(mu,
                         mir_build_cmp(mu, MIR_CMP_EQ, av, vi_u8(mu, 6)),
                         mir_build_cmp(mu, MIR_CMP_EQ, av, vi_u8(mu, 7))));
         *out = vi_from_bool_bit(l, mir_build_not(mu, ok01), true, false);
         return true;
      }

      if (np == 1 && fn_name_is(fn, "IS_X") && pvals[0].kind == VIVAL_VEC) {
         const uint32_t w = pvals[0].width;
         if (w <= 8) {
            mir_value_t acc = mir_const(mu, mir_bool_type(mu), 0);
            for (uint32_t k = 0; k < w; k++) {
               mir_value_t av = mir_build_load(
                  mu, mir_build_array_ref(mu, pvals[0].val, vi_off(mu, k)));
               mir_value_t ok01 = mir_build_or(
                  mu,
                  mir_build_or(mu,
                               mir_build_cmp(mu, MIR_CMP_EQ, av, vi_u8(mu, 2)),
                               mir_build_cmp(mu, MIR_CMP_EQ, av, vi_u8(mu, 3))),
                  mir_build_or(mu,
                               mir_build_cmp(mu, MIR_CMP_EQ, av, vi_u8(mu, 6)),
                               mir_build_cmp(mu, MIR_CMP_EQ, av, vi_u8(mu, 7))));
               acc = mir_build_or(mu, acc, mir_build_not(mu, ok01));
            }
            *out = vi_from_bool_bit(l, acc, true, false);
            return true;
         }
         mir_value_t idx_v = mir_add_var(mu, mir_offset_type(mu), MIR_NULL_STAMP,
                                         ident_uniq("isx_i"), MIR_VAR_TEMP);
         mir_value_t res_v = mir_add_var(mu, mir_bool_type(mu), MIR_NULL_STAMP,
                                         ident_uniq("isx_r"), MIR_VAR_TEMP);
         mir_build_store(mu, idx_v, vi_off(mu, 0));
         mir_build_store(mu, res_v, mir_const(mu, mir_bool_type(mu), 0));
         mir_block_t hdr  = mir_add_block(mu);
         mir_block_t body = mir_add_block(mu);
         mir_block_t hit  = mir_add_block(mu);
         mir_block_t step = mir_add_block(mu);
         mir_block_t done = mir_add_block(mu);
         mir_build_jump(mu, hdr);

         mir_set_cursor(mu, hdr, MIR_APPEND);
         mir_value_t cur = mir_build_load(mu, idx_v);
         mir_build_cond(mu, mir_build_cmp(mu, MIR_CMP_LT, cur, vi_off(mu, w)),
                        body, done);

         mir_set_cursor(mu, body, MIR_APPEND);
         mir_value_t av = mir_build_load(
            mu, mir_build_array_ref(mu, pvals[0].val, cur));
         mir_value_t ok01 = mir_build_or(
            mu,
            mir_build_or(mu,
                         mir_build_cmp(mu, MIR_CMP_EQ, av, vi_u8(mu, 2)),
                         mir_build_cmp(mu, MIR_CMP_EQ, av, vi_u8(mu, 3))),
            mir_build_or(mu,
                         mir_build_cmp(mu, MIR_CMP_EQ, av, vi_u8(mu, 6)),
                         mir_build_cmp(mu, MIR_CMP_EQ, av, vi_u8(mu, 7))));
         mir_build_cond(mu, ok01, step, hit);

         mir_set_cursor(mu, hit, MIR_APPEND);
         mir_build_store(mu, res_v, mir_const(mu, mir_bool_type(mu), 1));
         mir_build_jump(mu, done);

         mir_set_cursor(mu, step, MIR_APPEND);
         mir_build_store(mu, idx_v,
                         mir_build_add(mu, mir_offset_type(mu), cur, vi_off(mu, 1)));
         mir_build_jump(mu, hdr);

         mir_set_cursor(mu, done, MIR_APPEND);
         *out = vi_from_bool_bit(l, mir_build_load(mu, res_v), true, false);
         return true;
      }

      if (is_std1164) {
         if (np == 1 && fn_name_is(fn, "\"not\"") && pvals[0].kind == VIVAL_BIT) {
            *out = pvals[0];
            out->val = vi_std_scalar_not(l, pvals[0].val);
            return true;
         }
         if (np == 2 && pvals[0].kind == VIVAL_BIT && pvals[1].kind == VIVAL_BIT
             && (fn_name_is(fn, "\"and\"") || fn_name_is(fn, "\"nand\"")
                 || fn_name_is(fn, "\"or\"") || fn_name_is(fn, "\"nor\"")
                 || fn_name_is(fn, "\"xor\"") || fn_name_is(fn, "\"xnor\""))) {
            const bool neg = fn_name_is(fn, "\"nand\"") || fn_name_is(fn, "\"nor\"")
               || fn_name_is(fn, "\"xnor\"");
            const int tbl_kind = (fn_name_is(fn, "\"and\"") || fn_name_is(fn, "\"nand\""))
               ? 1 : ((fn_name_is(fn, "\"or\"") || fn_name_is(fn, "\"nor\"")) ? 2 : 3);
            *out = pvals[0];
            out->val = vi_std_scalar_bin(l, tbl_kind, pvals[0].val, pvals[1].val, neg);
            return true;
         }
      }

      if (is_num_pkg && np == 2) {
         const vi_val_t a = pvals[0];
         const vi_val_t b = pvals[1];
         tree_t e1 = tree_value(tree_param(fcall, 1));

         if (fn_name_is(fn, "RESIZE") && a.kind == VIVAL_VEC && a.width >= 1) {
            int64_t sz;
            if (vi_eval_const_int(l, e1, &sz)
                && sz >= 1 && sz <= VHDL_ISLAND_MAX_WIDTH) {
               const uint32_t nw = (uint32_t)sz;
               const bool sg = a.is_signed || ret_signed;
               mir_value_t dst = vi_modeb_alloc_vec(l, nw);
               if (!sg) {
                  if (nw <= a.width) {
                     mir_value_t src = (nw == a.width)
                        ? a.val
                        : mir_build_array_ref(mu, a.val, vi_off(mu, a.width - nw));
                     mir_build_copy(mu, dst, src, vi_off(mu, nw));
                  }
                  else {
                     const uint32_t pad = nw - a.width;
                     mir_build_set(mu, dst, vi_u8(mu, 2), vi_off(mu, pad));
                     mir_build_copy(mu, mir_build_array_ref(mu, dst, vi_off(mu, pad)),
                                    a.val, vi_off(mu, a.width));
                  }
               }
               else {
                  mir_value_t sbyte = mir_build_load(mu, a.val);
                  if (nw == 1) {
                     mir_build_store(mu, dst, sbyte);
                  }
                  else if (nw <= a.width) {
                     mir_build_store(mu, dst, sbyte);
                     const uint32_t low = nw - 1;
                     mir_build_copy(mu, mir_build_array_ref(mu, dst, vi_off(mu, 1)),
                                    mir_build_array_ref(mu, a.val, vi_off(mu, a.width - low)),
                                    vi_off(mu, low));
                  }
                  else {
                     const uint32_t pad = nw - a.width;
                     mir_build_set(mu, dst, sbyte, vi_off(mu, pad));
                     mir_build_copy(mu, mir_build_array_ref(mu, dst, vi_off(mu, pad)),
                                    a.val, vi_off(mu, a.width));
                  }
               }
               *out = (vi_val_t){
                  .kind      = VIVAL_VEC,
                  .val       = dst,
                  .width     = nw,
                  .is_signed = sg,
                  .is_std    = a.is_std,
                  .dir       = RANGE_DOWNTO,
                  .left      = (int64_t)nw - 1,
                  .right     = 0,
               };
               return true;
            }
         }

         if ((fn_name_is(fn, "SHIFT_LEFT") || fn_name_is(fn, "SHIFT_RIGHT")
              || fn_name_is(fn, "\"sll\"") || fn_name_is(fn, "\"srl\""))
             && a.kind == VIVAL_VEC && a.width >= 1 && b.kind == VIVAL_INT) {
            const uint32_t w = a.width;
            const bool is_shift_l = fn_name_is(fn, "SHIFT_LEFT");
            const bool is_shift_r = fn_name_is(fn, "SHIFT_RIGHT");
            const bool is_sll = fn_name_is(fn, "\"sll\"");
            const bool arith_r = is_shift_r && a.is_signed;
            mir_value_t fill = arith_r ? mir_build_load(mu, a.val) : vi_u8(mu, 2);
            mir_value_t dst = vi_modeb_alloc_vec(l, w);
            mir_value_t amt64 = vi_to_i64(mu, b.val);
            mir_block_t bb_done = mir_add_block(mu);

            if (is_shift_l || is_shift_r) {
               mir_block_t bb_ovf = mir_add_block(mu);
               mir_block_t bb_in = mir_add_block(mu);
               mir_value_t ovf = mir_build_cmp(mu, MIR_CMP_GEQ, amt64, vi_i64(mu, w));
               mir_build_cond(mu, ovf, bb_ovf, bb_in);

               mir_set_cursor(mu, bb_ovf, MIR_APPEND);
               mir_build_set(mu, dst, fill, vi_off(mu, w));
               mir_build_jump(mu, bb_done);

               mir_set_cursor(mu, bb_in, MIR_APPEND);
               mir_value_t c = vi_to_off(mu, amt64);
               mir_value_t rem = mir_build_sub(mu, mir_offset_type(mu),
                                               vi_off(mu, w), c);
               if (is_shift_l) {
                  mir_build_copy(mu, dst, mir_build_array_ref(mu, a.val, c), rem);
                  mir_build_set(mu, mir_build_array_ref(mu, dst, rem), fill, c);
               }
               else {
                  mir_build_set(mu, dst, fill, c);
                  mir_build_copy(mu, mir_build_array_ref(mu, dst, c), a.val, rem);
               }
               mir_build_jump(mu, bb_done);
            }
            else {
               mir_value_t neg = mir_build_cmp(mu, MIR_CMP_LT, amt64, vi_i64(mu, 0));
               mir_value_t abs64 = mir_build_select(
                  mu, vi_i64_type(mu), neg,
                  mir_build_neg(mu, vi_i64_type(mu), amt64), amt64);
               mir_value_t go_left = is_sll ? mir_build_not(mu, neg) : neg;
               mir_block_t bb_ovf = mir_add_block(mu);
               mir_block_t bb_in = mir_add_block(mu);
               mir_block_t bb_l = mir_add_block(mu);
               mir_block_t bb_r = mir_add_block(mu);
               mir_build_cond(mu, mir_build_cmp(mu, MIR_CMP_GEQ, abs64, vi_i64(mu, w)),
                              bb_ovf, bb_in);

               mir_set_cursor(mu, bb_ovf, MIR_APPEND);
               mir_build_set(mu, dst, vi_u8(mu, 2), vi_off(mu, w));
               mir_build_jump(mu, bb_done);

               mir_set_cursor(mu, bb_in, MIR_APPEND);
               mir_value_t c = vi_to_off(mu, abs64);
               mir_value_t rem = mir_build_sub(mu, mir_offset_type(mu),
                                               vi_off(mu, w), c);
               mir_build_cond(mu, go_left, bb_l, bb_r);

               mir_set_cursor(mu, bb_l, MIR_APPEND);
               mir_build_copy(mu, dst, mir_build_array_ref(mu, a.val, c), rem);
               mir_build_set(mu, mir_build_array_ref(mu, dst, rem),
                             vi_u8(mu, 2), c);
               mir_build_jump(mu, bb_done);

               mir_set_cursor(mu, bb_r, MIR_APPEND);
               mir_build_set(mu, dst, vi_u8(mu, 2), c);
               mir_build_copy(mu, mir_build_array_ref(mu, dst, c), a.val, rem);
               mir_build_jump(mu, bb_done);
            }

            mir_set_cursor(mu, bb_done, MIR_APPEND);
            *out = (vi_val_t){
               .kind      = VIVAL_VEC,
               .val       = dst,
               .width     = w,
               .is_signed = a.is_signed,
               .is_std    = a.is_std,
               .dir       = RANGE_DOWNTO,
               .left      = (int64_t)w - 1,
               .right     = 0,
            };
            return true;
         }
      }

      if (type_is_array(rtype)) {
         mir_type_t mret = mir_uarray_type(mu, 1, vi_u8_type(mu));
         mir_value_t res_ua = MIR_NULL_VALUE;
         if (!vi_modeb_call_ieee(l, decl, pvals, np, mret, &res_ua))
            return false;

         range_kind_t rdir = RANGE_DOWNTO;
         int64_t rl = 0, rr = 0;
         uint32_t rw = 0;
         if (!vi_eval_type_bounds(l, rtype, &rdir, &rl, &rr, &rw)) {
            if (fn_name_is(fn, "\"*\"") && np == 2) {
               uint32_t w0 = pvals[0].kind == VIVAL_VEC ? pvals[0].width : first_vec.width;
               uint32_t w1 = pvals[1].kind == VIVAL_VEC ? pvals[1].width : first_vec.width;
               rw = w0 + w1;
            }
            else if ((fn_name_is(fn, "RESIZE")
                      || fn_name_is(fn, "TO_UNSIGNED")
                      || fn_name_is(fn, "TO_SIGNED")
                      || fn_name_is(fn, "CONV_STD_LOGIC_VECTOR")
                      || fn_name_is(fn, "CONV_UNSIGNED")
                      || fn_name_is(fn, "CONV_SIGNED"))
                     && np == 2) {
               int64_t sz;
               if (!vi_eval_const_int(l, tree_value(tree_param(fcall, 1)), &sz)
                   || sz <= 0)
                  return false;
               rw = (uint32_t)sz;
            }
            else if ((fn_name_is(fn, "\"+\"") || fn_name_is(fn, "\"-\"")
                      || fn_name_is(fn, "MINIMUM") || fn_name_is(fn, "MAXIMUM"))
                     && np == 2) {
               uint32_t w0 = pvals[0].kind == VIVAL_VEC ? pvals[0].width : 0;
               uint32_t w1 = pvals[1].kind == VIVAL_VEC ? pvals[1].width : 0;
               rw = MAX(w0, w1);
               if (rw == 0)
                  rw = first_vec.width;
            }
            else if (first_vec.width > 0) {
               rw = first_vec.width;
            }
            else
               return false;
            rdir = RANGE_DOWNTO;
            rl = (int64_t)rw - 1;
            rr = 0;
         }

         mir_value_t uptr = mir_build_unwrap(mu, res_ua);
         mir_value_t vec_ptr = uptr;
         if (fn_name_is(fn, "TO_01") || fn_name_is(fn, "RESIZE")
             || fn_name_is(fn, "SHIFT_LEFT") || fn_name_is(fn, "SHIFT_RIGHT")) {
            vec_ptr = vi_modeb_alloc_vec(l, rw);
            mir_build_copy(mu, vec_ptr, uptr, vi_off(mu, rw));
         }
         *out = (vi_val_t){
            .kind      = VIVAL_VEC,
            .val       = vec_ptr,
            .width     = rw,
            .is_signed = ret_signed,
            .is_std    = true,
            .dir       = rdir,
            .left      = rl,
            .right     = rr,
         };
         return true;
      }
      else if (vi_type_is_bool(rtype)) {
         mir_value_t r = MIR_NULL_VALUE;
         if (!vi_modeb_call_ieee(l, decl, pvals, np, mir_bool_type(mu), &r))
            return false;
         *out = vi_from_bool_bit(l, r, true, false);
         return true;
      }
      else if (type_is_integer(rtype)) {
         mir_value_t r = MIR_NULL_VALUE;
         if (!vi_modeb_call_ieee(l, decl, pvals, np,
                                 mir_int_type(mu, INT32_MIN, INT32_MAX), &r))
            return false;
         *out = (vi_val_t){ .kind = VIVAL_INT, .val = vi_to_i64(mu, r) };
         return true;
      }
      else if (type_is_enum(rtype)) {
         mir_value_t r = MIR_NULL_VALUE;
         if (!vi_modeb_call_ieee(l, decl, pvals, np, vi_u8_type(mu), &r))
            return false;
         *out = (vi_val_t){
            .kind = VIVAL_BIT, .val = r, .width = 1,
            .is_std = vi_type_is_std_logic(rtype),
         };
         return true;
      }
      return false;
   }

   // Mode A (2-state VEC2): direct MIR lowering with zero external calls
   if (strcmp(pkg, "IEEE.STD_LOGIC_1164.") == 0) {
      if (np == 1) {
         vi_val_t a;
         if (!vi_lower_expr(l, tree_value(tree_param(fcall, 0)), &a))
            return false;
         if (fn_name_is(fn, "\"??\"")) {
            if (a.kind != VIVAL_BIT)
               return false;
            *out = vi_from_bool_bit(l, vi_to_bool(l, a), true, false);
            return true;
         }
         if (fn_name_is(fn, "IS_X")) {
            // Mode A holds only '0' and '1'
            if (a.kind != VIVAL_BIT && a.kind != VIVAL_VEC)
               return false;
            *out = vi_from_bool_bit(l, mir_const(mu, mir_bool_type(mu), 0),
                                    true, false);
            return true;
         }
         if (fn_name_is(fn, "\"not\"")) {
            if (a.kind == VIVAL_BIT) {
               mir_value_t v1 = mir_build_unary(mu, MIR_VEC_BIT_NOT,
                                                mir_vec2_type(mu, 1, false),
                                                vi_to_vec2_1(l, a));
               *out = a;
               out->val = v1;
               return true;
            }
            if (a.kind == VIVAL_VEC) {
               *out = a;
               out->val = mir_build_unary(mu, MIR_VEC_BIT_NOT,
                                          mir_vec2_type(mu, a.width, a.is_signed),
                                          a.val);
               return true;
            }
         }
         if (fn_name_is(fn, "TO_STDLOGICVECTOR") || fn_name_is(fn, "TO_STDULOGICVECTOR")
             || fn_name_is(fn, "TO_BITVECTOR") || fn_name_is(fn, "TO_X01")
             || fn_name_is(fn, "TO_01") || fn_name_is(fn, "TO_BIT")) {
            *out = a;
            if (fn_name_is(fn, "TO_BIT"))
               out->is_std = false;
            else if (fn_name_is(fn, "TO_STDLOGICVECTOR")
                     || fn_name_is(fn, "TO_STDULOGICVECTOR"))
               out->is_std = true;
            return true;
         }
      }
      else if (np == 2) {
         vi_val_t a, b;
         if (!vi_lower_expr(l, tree_value(tree_param(fcall, 0)), &a)
             || !vi_lower_expr(l, tree_value(tree_param(fcall, 1)), &b))
            return false;
         vi_match_op_t mop;
         if (vi_match_op_from_name(fn, &mop))
            return vi_lower_match_cmp(l, mop, a, b, false, true, out);
         if (fn_name_is(fn, "STD_MATCH"))
            return vi_lower_match_cmp(l, VIM_EQ, a, b, true, false, out);
         bool neg = false;
         mir_vec_op_t op;
         if (fn_name_is(fn, "\"and\""))        op = MIR_VEC_BIT_AND;
         else if (fn_name_is(fn, "\"nand\"")) { op = MIR_VEC_BIT_AND; neg = true; }
         else if (fn_name_is(fn, "\"or\""))   op = MIR_VEC_BIT_OR;
         else if (fn_name_is(fn, "\"nor\""))  { op = MIR_VEC_BIT_OR; neg = true; }
         else if (fn_name_is(fn, "\"xor\""))  op = MIR_VEC_BIT_XOR;
         else if (fn_name_is(fn, "\"xnor\"")) { op = MIR_VEC_BIT_XOR; neg = true; }
         else return false;

         if (a.kind == VIVAL_BIT && b.kind == VIVAL_BIT) {
            mir_type_t t1 = mir_vec2_type(mu, 1, false);
            mir_value_t r = mir_build_binary(mu, op, t1, vi_to_vec2_1(l, a),
                                             vi_to_vec2_1(l, b));
            if (neg)
               r = mir_build_unary(mu, MIR_VEC_BIT_NOT, t1, r);
            *out = a;
            out->val = r;
            return true;
         }
         if (a.kind == VIVAL_VEC && b.kind == VIVAL_VEC && a.width == b.width) {
            mir_type_t tv = mir_vec2_type(mu, a.width, a.is_signed);
            mir_value_t r = mir_build_binary(mu, op, tv, a.val, b.val);
            if (neg)
               r = mir_build_unary(mu, MIR_VEC_BIT_NOT, tv, r);
            *out = a;
            out->val = r;
            return true;
         }
         if ((a.kind == VIVAL_VEC && b.kind == VIVAL_BIT)
             || (a.kind == VIVAL_BIT && b.kind == VIVAL_VEC)) {
            vi_val_t v = (a.kind == VIVAL_VEC) ? a : b;
            vi_val_t s = (a.kind == VIVAL_BIT) ? a : b;
            mir_type_t tv = mir_vec2_type(mu, v.width, v.is_signed);
            mir_value_t sb = mir_build_test(mu, vi_to_vec2_1(l, s));
            mir_value_t splat = mir_build_select(
               mu, tv, sb,
               vi_const_vec2_rep(mu, (int)v.width, v.is_signed, 1),
               vi_const_vec2_rep(mu, (int)v.width, v.is_signed, 0));
            mir_value_t r = mir_build_binary(mu, op, tv, v.val, splat);
            if (neg)
               r = mir_build_unary(mu, MIR_VEC_BIT_NOT, tv, r);
            *out = v;
            out->val = r;
            return true;
         }
      }
      return false;
   }

   // NUMERIC_STD / NUMERIC_STD_UNSIGNED / STD_LOGIC_UNSIGNED / STD_LOGIC_SIGNED
   const bool pkg_signed = (strcmp(pkg, "IEEE.STD_LOGIC_SIGNED.") == 0);
   if (np == 1) {
      vi_val_t a;
      if (!vi_lower_expr(l, tree_value(tree_param(fcall, 0)), &a))
         return false;
      if (fn_name_is(fn, "\"not\"")) {
         if (a.kind != VIVAL_VEC)
            return false;
         mir_type_t tv = mir_vec2_type(mu, a.width, a.is_signed);
         *out = a;
         out->val = mir_build_unary(mu, MIR_VEC_BIT_NOT, tv, a.val);
         return true;
      }
      if (fn_name_is(fn, "IS_X")) {
         if (a.kind != VIVAL_VEC)
            return false;
         *out = vi_from_bool_bit(l, mir_const(mu, mir_bool_type(mu), 0),
                                 true, false);
         return true;
      }
      if (fn_name_is(fn, "TO_INTEGER")) {
         if (a.kind != VIVAL_VEC || (l->spec && a.width > 63))
            return false;
         const bool sg = a.is_signed || pkg_signed;
         mir_value_t src = vi_vec2_as(l, &a, sg);
         if (a.width > 64)
            src = mir_build_cast(mu, mir_vec2_type(mu, 64, sg), src);
         mir_value_t iv = mir_build_cast(mu, vi_i64_type(mu), src);
         if (l->spec)
            vi_spec_deopt_result(l, fcall, iv);
         *out = (vi_val_t){ .kind = VIVAL_INT, .val = iv };
         return true;
      }
      if (fn_name_is(fn, "UNSIGNED") || fn_name_is(fn, "SIGNED")
          || fn_name_is(fn, "CONV_INTEGER")) {
         if (fn_name_is(fn, "CONV_INTEGER")) {
            if (a.kind != VIVAL_VEC || (l->spec && a.width > 63))
               return false;
            mir_value_t src = vi_vec2_as(l, &a, pkg_signed);
            if (a.width > 64)
               src = mir_build_cast(mu, mir_vec2_type(mu, 64, pkg_signed), src);
            mir_value_t iv = mir_build_cast(mu, vi_i64_type(mu), src);
            if (l->spec)
               vi_spec_deopt_result(l, fcall, iv);
            *out = (vi_val_t){ .kind = VIVAL_INT, .val = iv };
            return true;
         }
         *out = a;
         out->is_signed = fn_name_is(fn, "SIGNED");
         if (a.kind == VIVAL_VEC)
            out->val = vi_vec2_as(l, &a, out->is_signed);
         return true;
      }
      if (fn_name_is(fn, "TO_01")) {
         *out = a;
         return true;
      }
      if (fn_name_is(fn, "\"-\"")) {
         if (a.kind != VIVAL_VEC)
            return false;
         mir_type_t tv = mir_vec2_type(mu, a.width, true);
         mir_value_t zero = vi_const_vec2(mu, a.width, true, NULL);
         mir_value_t av = mir_build_cast(mu, tv, a.val);
         *out = a;
         out->is_signed = true;
         out->val = mir_build_binary(mu, MIR_VEC_SUB, tv, zero, av);
         return true;
      }
      return false;
   }

   if (np == 2) {
      tree_t e0 = tree_value(tree_param(fcall, 0));
      tree_t e1 = tree_value(tree_param(fcall, 1));
      vi_val_t a, b;
      if (!vi_lower_expr(l, e0, &a) || !vi_lower_expr(l, e1, &b))
         return false;
      if (fn_name_is(fn, "STD_MATCH"))
         return vi_lower_match_cmp(l, VIM_EQ, a, b, true, false, out);
      const int nports = tree_ports(decl);
      if (a.kind == VIVAL_INT && nports >= 1) {
         tree_t fp = tree_port(decl, 0);
         vi_check_int_bounds(l, tree_type(fp), vi_to_i64(mu, a.val), e0, fp);
      }
      if (b.kind == VIVAL_INT && nports >= 2) {
         tree_t fp = tree_port(decl, 1);
         vi_check_int_bounds(l, tree_type(fp), vi_to_i64(mu, b.val), e1, fp);
      }

      if (fn_name_is(fn, "TO_UNSIGNED") || fn_name_is(fn, "TO_SIGNED")
          || fn_name_is(fn, "CONV_UNSIGNED") || fn_name_is(fn, "CONV_SIGNED")
          || fn_name_is(fn, "CONV_STD_LOGIC_VECTOR")) {
         int64_t sz;
         if (!vi_eval_const_int(l, e1, &sz)
             || sz <= 0 || sz > VHDL_ISLAND_MAX_WIDTH)
            return false;
         const bool sg = fn_name_is(fn, "TO_SIGNED") || fn_name_is(fn, "CONV_SIGNED");
         mir_type_t tv = mir_vec2_type(mu, (int)sz, sg);
         mir_value_t v;
         if (l->spec && a.kind == VIVAL_INT
             && (fn_name_is(fn, "TO_UNSIGNED") || fn_name_is(fn, "TO_SIGNED")))
            vi_spec_deopt_int_fit(l, vi_to_i64(mu, a.val), (uint32_t)sz, sg);
         if (a.kind == VIVAL_INT)
            v = vi_cast_vec2(mu, tv, vi_to_i64(mu, a.val));
         else if (a.kind == VIVAL_VEC)
            v = mir_build_cast(mu, tv, a.val);
         else
            return false;
         *out = (vi_val_t){
            .kind      = VIVAL_VEC,
            .val       = v,
            .width     = (uint32_t)sz,
            .is_signed = sg,
            .is_std    = true,
            .dir       = RANGE_DOWNTO,
            .left      = sz - 1,
            .right     = 0,
         };
         return true;
      }

      if (fn_name_is(fn, "RESIZE")) {
         if (a.kind != VIVAL_VEC)
            return false;
         int64_t sz;
         if (!vi_eval_const_int(l, e1, &sz)
             || sz <= 0 || sz > VHDL_ISLAND_MAX_WIDTH)
            return false;
         const uint32_t nw = (uint32_t)sz;
         const bool sg = a.is_signed || ret_signed;
         mir_type_t tv = mir_vec2_type(mu, (int)nw, sg);
         mir_value_t src = vi_vec2_as(l, &a, sg);
         mir_value_t res = mir_build_cast(mu, tv, src);
         if (sg && nw < a.width) {
            // Preserve sign bit at MSB (pos 0) per IEEE numeric_std
            // RESIZE(SIGNED): also for a new size of one bit, which keeps
            // only the sign bit
            mir_value_t sbit = mir_build_extract(mu, mir_vec2_type(mu, 1, false),
                                                 src, vi_off(mu, 0));
            res = mir_build_insert(mu, sbit, res, vi_off(mu, 0));
         }
         *out = (vi_val_t){
            .kind      = VIVAL_VEC,
            .val       = res,
            .width     = nw,
            .is_signed = sg,
            .is_std    = a.is_std,
            .dir       = RANGE_DOWNTO,
            .left      = (int64_t)nw - 1,
            .right     = 0,
         };
         return true;
      }

      if (fn_name_is(fn, "SHIFT_LEFT") || fn_name_is(fn, "SHIFT_RIGHT")
          || fn_name_is(fn, "ROTATE_LEFT") || fn_name_is(fn, "ROTATE_RIGHT")
          || fn_name_is(fn, "\"sll\"") || fn_name_is(fn, "\"srl\"")
          || fn_name_is(fn, "\"sla\"") || fn_name_is(fn, "\"sra\"")
          || fn_name_is(fn, "\"rol\"") || fn_name_is(fn, "\"ror\"")) {
         if (a.kind != VIVAL_VEC || b.kind != VIVAL_INT)
            return false;
         mir_value_t amt = vi_to_i64(mu, b.val);
         const uint32_t w = a.width;
         const bool sg = a.is_signed;
         mir_type_t tv = mir_vec2_type(mu, (int)w, sg);
         a.val = vi_vec2_as(l, &a, sg);

         const bool is_left = fn_name_is(fn, "SHIFT_LEFT") || fn_name_is(fn, "\"sll\"")
            || fn_name_is(fn, "\"sla\"");
         const bool is_right = fn_name_is(fn, "SHIFT_RIGHT") || fn_name_is(fn, "\"srl\"")
            || fn_name_is(fn, "\"sra\"");

         if (is_left || is_right) {
            mir_value_t neg_amt = mir_build_cmp(mu, MIR_CMP_LT, amt, vi_i64(mu, 0));
            mir_value_t abs_amt = mir_build_select(mu, vi_i64_type(mu), neg_amt,
                                                   mir_build_neg(mu, vi_i64_type(mu), amt),
                                                   amt);
            mir_value_t clamped = mir_build_cmp(mu, MIR_CMP_GEQ, abs_amt, vi_i64(mu, w));
            mir_value_t safe_amt = mir_build_select(mu, vi_i64_type(mu), clamped,
                                                    vi_i64(mu, 0), abs_amt);
            mir_value_t shv = vi_cast_vec2(mu, tv, safe_amt);
            mir_vec_op_t op = is_left ? MIR_VEC_SLL
               : (sg ? MIR_VEC_SRA : MIR_VEC_SRL);
            mir_value_t shifted = mir_build_binary(mu, op, tv, a.val, shv);
            mir_value_t ovf = vi_const_vec2_rep(mu, (int)w, sg, 0);
            if (is_right && sg) {
               mir_value_t sbit = mir_build_extract(mu, mir_vec2_type(mu, 1, false),
                                                    a.val, vi_off(mu, 0));
               mir_value_t all1 = vi_const_vec2_rep(mu, (int)w, sg, 1);
               ovf = mir_build_select(mu, tv, mir_build_test(mu, sbit), all1, ovf);
            }
            *out = a;
            out->val = mir_build_select(mu, tv, clamped, ovf, shifted);
            return true;
         }

         // Rotate
         const bool rot_left = fn_name_is(fn, "ROTATE_LEFT") || fn_name_is(fn, "\"rol\"");
         mir_value_t modw = mir_build_rem(mu, vi_i64_type(mu), amt, vi_i64(mu, w));
         mir_value_t neg = mir_build_cmp(mu, MIR_CMP_LT, modw, vi_i64(mu, 0));
         mir_value_t k = mir_build_select(mu, vi_i64_type(mu), neg,
                                          mir_build_add(mu, vi_i64_type(mu), modw,
                                                        vi_i64(mu, w)),
                                          modw);
         if (!rot_left)
            k = mir_build_sub(mu, vi_i64_type(mu), vi_i64(mu, w), k);
         k = mir_build_rem(mu, vi_i64_type(mu), k, vi_i64(mu, w));
         mir_value_t wk = mir_build_sub(mu, vi_i64_type(mu), vi_i64(mu, w), k);
         mir_value_t lo = mir_build_binary(mu, MIR_VEC_SLL, tv, a.val,
                                           vi_cast_vec2(mu, tv, k));
         mir_value_t hi = mir_build_binary(mu, MIR_VEC_SRL, tv, a.val,
                                           vi_cast_vec2(mu, tv, wk));
         mir_value_t rot = mir_build_binary(mu, MIR_VEC_BIT_OR, tv, lo, hi);
         *out = a;
         out->val = mir_build_select(mu, tv,
                                     mir_build_cmp(mu, MIR_CMP_EQ, k, vi_i64(mu, 0)),
                                     a.val, rot);
         return true;
      }

      // Bitwise vector/mixed in numeric_std
      if (fn_name_is(fn, "\"and\"") || fn_name_is(fn, "\"nand\"")
          || fn_name_is(fn, "\"or\"") || fn_name_is(fn, "\"nor\"")
          || fn_name_is(fn, "\"xor\"") || fn_name_is(fn, "\"xnor\"")) {
         if (a.kind != VIVAL_VEC || b.kind != VIVAL_VEC || a.width != b.width)
            return false;
         bool neg = false;
         mir_vec_op_t op;
         if (fn_name_is(fn, "\"and\""))        op = MIR_VEC_BIT_AND;
         else if (fn_name_is(fn, "\"nand\"")) { op = MIR_VEC_BIT_AND; neg = true; }
         else if (fn_name_is(fn, "\"or\""))   op = MIR_VEC_BIT_OR;
         else if (fn_name_is(fn, "\"nor\""))  { op = MIR_VEC_BIT_OR; neg = true; }
         else if (fn_name_is(fn, "\"xor\""))  op = MIR_VEC_BIT_XOR;
         else                                 { op = MIR_VEC_BIT_XOR; neg = true; }
         mir_type_t tv = mir_vec2_type(mu, a.width, a.is_signed);
         mir_value_t r = mir_build_binary(mu, op, tv, a.val, b.val);
         if (neg)
            r = mir_build_unary(mu, MIR_VEC_BIT_NOT, tv, r);
         *out = a;
         out->val = r;
         return true;
      }

      // Binary arithmetic & relational ops: +, -, *, =, /=, <, <=, >, >=
      bool is_arith = false, is_mul = false, is_rel = false;
      mir_vec_op_t op;
      if (fn_name_is(fn, "\"+\""))       { op = MIR_VEC_ADD; is_arith = true; }
      else if (fn_name_is(fn, "\"-\""))  { op = MIR_VEC_SUB; is_arith = true; }
      else if (fn_name_is(fn, "\"*\""))  { op = MIR_VEC_MUL; is_arith = true; is_mul = true; }
      else if (fn_name_is(fn, "\"=\""))  { op = MIR_VEC_CASE_EQ; is_rel = true; }
      else if (fn_name_is(fn, "\"/=\"")) { op = MIR_VEC_CASE_NEQ; is_rel = true; }
      else if (fn_name_is(fn, "\"<\""))  { op = MIR_VEC_LT; is_rel = true; }
      else if (fn_name_is(fn, "\"<=\"")) { op = MIR_VEC_LEQ; is_rel = true; }
      else if (fn_name_is(fn, "\">\""))  { op = MIR_VEC_GT; is_rel = true; }
      else if (fn_name_is(fn, "\">=\"")) { op = MIR_VEC_GEQ; is_rel = true; }
      else if (fn_name_is(fn, "MINIMUM") || fn_name_is(fn, "MAXIMUM")) {
         is_rel = false;
         op = MIR_VEC_LT;
      }
      else
         return false;

      uint32_t wa = (a.kind == VIVAL_VEC) ? a.width : 0;
      uint32_t wb = (b.kind == VIVAL_VEC) ? b.width : 0;
      if (wa == 0 && wb == 0)
         return false;

      const bool sg = pkg_signed || ret_signed || a.is_signed || b.is_signed;
      if (l->spec) {
         // Unsigned with signed operands extend differently in the IEEE
         // packages; an integer operand that does not fit the width of the
         // vector one is converted with a truncation warning or compared
         // as a mathematical value
         if (wa > 0 && wb > 0 && a.is_signed != b.is_signed)
            return false;
         if (is_rel && strcmp(pkg, "IEEE.NUMERIC_STD.") == 0
             && (a.kind == VIVAL_INT) != (b.kind == VIVAL_INT)) {
            const bool int_left = (a.kind == VIVAL_INT);
            const vi_val_t iv = int_left ? a : b;
            const uint32_t wv = int_left ? wb : wa;
            bool res;
            if (wv > 0 && vi_spec_fold_int_cmp(l, op, int_left, iv, wv, sg,
                                               &res)) {
               *out = vi_from_bool_bit(l, mir_const(mu, mir_bool_type(mu), res),
                                       true, false);
               return true;
            }
         }
         if (a.kind == VIVAL_INT && wb > 0)
            vi_spec_deopt_int_fit(l, vi_to_i64(mu, a.val), wb, sg);
         if (b.kind == VIVAL_INT && wa > 0)
            vi_spec_deopt_int_fit(l, vi_to_i64(mu, b.val), wa, sg);
      }
      uint32_t w;
      if (is_mul)
         w = (wa > 0 && wb > 0) ? (wa + wb) : (MAX(wa, wb) * 2u);
      else
         w = MAX(wa, wb);

      mir_type_t tv = mir_vec2_type(mu, (int)w, sg);
      mir_value_t va, vb;
      if (a.kind == VIVAL_VEC) {
         mir_value_t src = vi_vec2_as(l, &a, sg);
         va = (a.width == w) ? src : mir_build_cast(mu, tv, src);
      }
      else if (a.kind == VIVAL_INT)
         va = vi_cast_vec2(mu, tv, vi_to_i64(mu, a.val));
      else if (a.kind == VIVAL_BIT)
         va = mir_build_cast(mu, tv, vi_to_vec2_1(l, a));
      else
         return false;

      if (b.kind == VIVAL_VEC) {
         mir_value_t src = vi_vec2_as(l, &b, sg);
         vb = (b.width == w) ? src : mir_build_cast(mu, tv, src);
      }
      else if (b.kind == VIVAL_INT)
         vb = vi_cast_vec2(mu, tv, vi_to_i64(mu, b.val));
      else if (b.kind == VIVAL_BIT)
         vb = mir_build_cast(mu, tv, vi_to_vec2_1(l, b));
      else
         return false;

      if (fn_name_is(fn, "MINIMUM") || fn_name_is(fn, "MAXIMUM")) {
         mir_vec_op_t cmp_op = fn_name_is(fn, "MINIMUM") ? MIR_VEC_LT : MIR_VEC_GT;
         mir_value_t c = mir_build_binary(mu, cmp_op, tv, va, vb);
         *out = (vi_val_t){
            .kind      = VIVAL_VEC,
            .val       = mir_build_select(mu, tv, mir_build_test(mu, c), va, vb),
            .width     = w,
            .is_signed = sg,
            .is_std    = true,
            .dir       = RANGE_DOWNTO,
            .left      = (int64_t)w - 1,
            .right     = 0,
         };
         return true;
      }

      mir_value_t res = mir_build_binary(mu, op, tv, va, vb);
      if (is_rel) {
         *out = (vi_val_t){
            .kind = VIVAL_BIT, .val = res, .width = 1, .is_bool = true,
         };
      }
      else {
         *out = (vi_val_t){
            .kind      = VIVAL_VEC,
            .val       = res,
            .width     = w,
            .is_signed = sg,
            .is_std    = true,
            .dir       = RANGE_DOWNTO,
            .left      = (int64_t)w - 1,
            .right     = 0,
         };
      }
      (void)is_arith;
      return true;
   }

   return false;
}

// Inlines a call FCALL of FUNC: the result is *OUT, or for a function
// returning a record the leaves of RET_REC, a record local of that type
static bool vi_lower_inline_func_inner(vi_lower_t *l, tree_t fcall,
                                       tree_t func, vi_val_t *out,
                                       vi_local_t *ret_rec)
{
   func = vhdl_island_resolve_func_body(func);
   if (func == NULL || (tree_flags(func) & TREE_F_IMPURE))
      return false;

   mir_unit_t *mu = l->mu;
   const int np = tree_params(fcall);
   if (np != tree_ports(func))
      return false;

   // Evaluate arguments in caller scope first
   vi_val_t *arg_vals LOCAL = xcalloc_array(MAX(1, np), sizeof(vi_val_t));
   bool *arg_is_rec LOCAL = xcalloc_array(MAX(1, np), sizeof(bool));
   vi_local_t *arg_rec_locs LOCAL = xcalloc_array(MAX(1, np), sizeof(vi_local_t));
   bool *arg_has_cint LOCAL = xcalloc_array(MAX(1, np), sizeof(bool));
   int64_t *arg_cints LOCAL = xcalloc_array(MAX(1, np), sizeof(int64_t));
   for (int i = 0; i < np; i++) {
      tree_t fp = tree_port(func, i);
      tree_t pe = tree_value(tree_param(fcall, i));
      type_t fpt = tree_type(fp);
      if (type_is_record(fpt)) {
         arg_is_rec[i] = true;
         vi_rec_leaf_list_t leaves = AINIT;
         int rpath[VHDL_ISIG_MAX_DEPTH];
         if (!vi_collect_rec_local_leaves(l, fpt, rpath, 0, tree_ident(fp), &leaves)) {
            ACLEAR(leaves);
            for (int k = 0; k < i; k++)
               free(arg_rec_locs[k].rec_leaves);
            return false;
         }
         arg_rec_locs[i] = (vi_local_t){
            .decl         = fp,
            .is_rec       = true,
            .rec_type     = fpt,
            .rec_leaves   = leaves.items,
            .n_rec_leaves = leaves.count,
         };
         if (!vi_assign_rec_local(l, &arg_rec_locs[i], rpath, 0, fpt, pe)) {
            for (int k = 0; k <= i; k++)
               free(arg_rec_locs[k].rec_leaves);
            return false;
         }
         continue;
      }
      if (!vi_lower_expr(l, pe, &arg_vals[i])) {
         for (int k = 0; k < i; k++)
            free(arg_rec_locs[k].rec_leaves);
         return false;
      }
      if (arg_vals[i].kind == VIVAL_INT) {
         vi_check_int_bounds(l, fpt, vi_to_i64(mu, arg_vals[i].val), pe, fp);
         int64_t cv = 0;
         if (vi_eval_const_int(l, pe, &cv)
             || mir_get_const(mu, arg_vals[i].val, &cv)) {
            arg_has_cint[i] = true;
            arg_cints[i] = cv;
         }
      }
   }

   vi_scope_t child = {
      .parent   = l->scope,
      .ret_bb   = mir_add_block(mu),
      .ret_type = tree_type(fcall),
      .ret_rec  = ret_rec,
   };

   // Bind formal parameters in callee scope first so return type & local decl
   // bounds can resolve formal integer constants and array attributes.
   for (int i = 0; i < np; i++) {
      if (arg_is_rec[i]) {
         APUSH(child.locals, arg_rec_locs[i]);
         continue;
      }
      tree_t fp = tree_port(func, i);
      vi_local_t loc = {
         .decl          = fp,
         .is_vec        = (arg_vals[i].kind == VIVAL_VEC),
         .is_std        = arg_vals[i].is_std,
         .is_bool       = arg_vals[i].is_bool,
         .is_signed     = arg_vals[i].is_signed,
         .is_int        = (arg_vals[i].kind == VIVAL_INT),
         .width         = arg_vals[i].width,
         .dir           = arg_vals[i].dir,
         .left          = arg_vals[i].left,
         .right         = arg_vals[i].right,
         .by_val        = true,
         .pval          = arg_vals[i],
         .has_const_int = arg_has_cint[i],
         .const_int     = arg_cints[i],
      };
      APUSH(child.locals, loc);
   }

   vi_scope_t *prev_scope = l->scope;
   l->scope = &child;

   // Allocate return slot if function returns a value
   type_t rtype = tree_type(fcall);
   range_kind_t rdir = RANGE_DOWNTO;
   int64_t rl = 0, rr = 0;
   uint32_t rw = 0;
   if (type_is_record(rtype)) {
      if (ret_rec == NULL) {
         l->scope = prev_scope;
         vi_scope_clear(&child);
         return false;
      }
   }
   else if (type_is_array(rtype)) {
      child.ret_is_vec = true;
      child.ret_signed = vi_type_is_signed_vec(rtype);
      if (vi_eval_type_bounds(l, rtype, &rdir, &rl, &rr, &rw)) {
         child.ret_w = rw;
         child.ret_dir = rdir;
         child.ret_left = rl;
         child.ret_right = rr;
         if (l->two_state) {
            mir_type_t vt = mir_vec2_type(mu, (int)rw, child.ret_signed);
            child.ret_slot = mir_add_var(mu, vt, MIR_NULL_STAMP,
                                         ident_uniq("fnret"), MIR_VAR_TEMP);
         }
         else
            child.ret_slot = vi_modeb_alloc_vec(l, rw);
      }
      else if (!l->two_state) {
         // Unconstrained Mode B return: hold returned buffer pointer in a
         // function-scoped var so branched returns always dominate ret_bb.
         child.ret_ptr_indirect = true;
         child.ret_slot = mir_add_var(mu, mir_pointer_type(mu, vi_u8_type(mu)),
                                      MIR_NULL_STAMP, ident_uniq("fnretp"),
                                      MIR_VAR_TEMP);
      }
   }
   else if (vi_type_is_int_or_user_enum(rtype)) {
      child.ret_slot = mir_add_var(mu, vi_i64_type(mu), MIR_NULL_STAMP,
                                   ident_uniq("fnret"), MIR_VAR_TEMP);
   }
   else if (vi_type_is_bool(rtype) || type_is_enum(rtype)) {
      mir_type_t st = l->two_state
         ? mir_vec2_type(mu, 1, false) : vi_u8_type(mu);
      child.ret_slot = mir_add_var(mu, st, MIR_NULL_STAMP,
                                   ident_uniq("fnret"), MIR_VAR_TEMP);
   }
   else {
      l->scope = prev_scope;
      vi_scope_clear(&child);
      return false;
   }

   for (int i = 0; i < tree_decls(func); i++) {
      tree_t d = tree_decl(func, i);
      if (tree_kind(d) == T_VAR_DECL || tree_kind(d) == T_CONST_DECL) {
         type_t vt = tree_type(d);
         vi_local_t loc = { .decl = d };
         if (type_is_record(vt)) {
            vi_rec_leaf_list_t leaves = AINIT;
            int rpath[VHDL_ISIG_MAX_DEPTH];
            if (!vi_collect_rec_local_leaves(l, vt, rpath, 0, tree_ident(d), &leaves)) {
               ACLEAR(leaves);
               l->scope = prev_scope;
               vi_scope_clear(&child);
               return false;
            }
            loc.is_rec = true;
            loc.rec_type = vt;
            loc.rec_leaves = leaves.items;
            loc.n_rec_leaves = leaves.count;
            if (tree_has_value(d)
                ? !vi_assign_rec_local(l, &loc, rpath, 0, vt, tree_value(d))
                : !vi_init_default_local(l, &loc)) {
               free(loc.rec_leaves);
               l->scope = prev_scope;
               vi_scope_clear(&child);
               return false;
            }
            APUSH(child.locals, loc);
            continue;
         }
         if (type_is_array(vt)) {
            loc.is_vec = true;
            loc.is_std = vi_type_is_std_logic(type_elem(vt));
            loc.is_signed = vi_type_is_signed_vec(vt);
            if (!vi_eval_type_bounds(l, vt, &loc.dir, &loc.left,
                                     &loc.right, &loc.width)) {
               l->scope = prev_scope;
               vi_scope_clear(&child);
               return false;
            }
            if (l->two_state)
               loc.storage = vi_add_vec2_var(l, loc.width, loc.is_signed,
                                             tree_ident(d));
            else
               loc.storage = vi_modeb_alloc_vec(l, loc.width);
         }
         else if (vi_type_is_int_or_user_enum(vt)) {
            loc.is_int = true;
            loc.storage = mir_add_var(mu, vi_i64_type(mu), MIR_NULL_STAMP,
                                      tree_ident(d), MIR_VAR_TEMP);
            if (tree_kind(d) == T_CONST_DECL && tree_has_value(d)) {
               int64_t cv = 0;
               if (vi_eval_const_int(l, tree_value(d), &cv)) {
                  loc.has_const_int = true;
                  loc.const_int = cv;
               }
            }
         }
         else if (type_is_enum(vt)) {
            loc.is_std = vi_type_is_std_logic(vt);
            loc.is_bool = vi_type_is_bool(vt);
            loc.width = 1;
            mir_type_t st = l->two_state
               ? mir_vec2_type(mu, 1, false) : vi_u8_type(mu);
            loc.storage = mir_add_var(mu, st, MIR_NULL_STAMP,
                                      tree_ident(d), MIR_VAR_TEMP);
         }
         else {
            l->scope = prev_scope;
            vi_scope_clear(&child);
            return false;
         }

         if (tree_has_value(d)) {
            vi_val_t initv;
            if (!vi_lower_expr(l, tree_value(d), &initv)) {
               l->scope = prev_scope;
               vi_scope_clear(&child);
               return false;
            }
            if (l->two_state || !loc.is_vec)
               mir_build_store(mu, loc.storage, initv.val);
            else
               mir_build_copy(mu, loc.storage, initv.val, vi_off(mu, loc.width));
         }
         else if (!vi_init_default_local(l, &loc)) {
            l->scope = prev_scope;
            vi_scope_clear(&child);
            return false;
         }
         APUSH(child.locals, loc);
      }
   }

   if (!vi_check_spec_defaults(l, func, &child)) {
      l->scope = prev_scope;
      vi_scope_clear(&child);
      return false;
   }

   for (int i = 0; i < tree_stmts(func); i++) {
      if (!vi_lower_stmt(l, tree_stmt(func, i))) {
         l->scope = prev_scope;
         vi_scope_clear(&child);
         return false;
      }
   }

   unsigned pos = 0;
   mir_block_t cur = mir_get_cursor(mu, &pos);
   if (!mir_block_finished(mu, cur))
      mir_build_jump(mu, child.ret_bb);

   mir_set_cursor(mu, child.ret_bb, MIR_APPEND);
   l->scope = prev_scope;

   if (ret_rec != NULL) {
      const bool ok = child.has_ret_val;
      vi_scope_clear(&child);
      return ok;
   }

   if (!child.has_ret_val || mir_is_null(child.ret_slot)) {
      vi_scope_clear(&child);
      return false;
   }

   if (child.ret_is_vec) {
      *out = (vi_val_t){
         .kind      = VIVAL_VEC,
         .val       = (l->two_state || child.ret_ptr_indirect)
                      ? mir_build_load(mu, child.ret_slot) : child.ret_slot,
         .width     = child.ret_w,
         .is_signed = child.ret_signed,
         .is_std    = vi_type_is_std_logic(type_elem(rtype)),
         .dir       = child.ret_dir,
         .left      = child.ret_left,
         .right     = child.ret_right,
      };
   }
   else if (vi_type_is_int_or_user_enum(rtype)) {
      *out = (vi_val_t){
         .kind = VIVAL_INT,
         .val  = mir_build_load(mu, child.ret_slot),
      };
   }
   else {
      *out = (vi_val_t){
         .kind    = VIVAL_BIT,
         .val     = mir_build_load(mu, child.ret_slot),
         .width   = 1,
         .is_std  = vi_type_is_std_logic(rtype),
         .is_bool = vi_type_is_bool(rtype),
      };
   }

   vi_scope_clear(&child);
   return true;
}

static bool vi_lower_builtin_fcall(vi_lower_t *l, tree_t fcall, tree_t decl,
                                   subprogram_kind_t sk, vi_val_t *out)
{
   mir_unit_t *mu = l->mu;
   const int np = tree_params(fcall);

   if (np == 1) {
      vi_val_t a;
      if (!vi_lower_expr(l, tree_value(tree_param(fcall, 0)), &a))
         return false;

      switch (sk) {
      case S_IDENTITY:
         *out = a;
         return true;
      case S_NEGATE:
         if (a.kind != VIVAL_INT)
            return false;
         *out = (vi_val_t){
            .kind = VIVAL_INT,
            .val  = mir_build_neg(mu, vi_i64_type(mu), vi_to_i64(mu, a.val)),
         };
         return true;
      case S_ABS:
         if (a.kind != VIVAL_INT)
            return false;
         *out = (vi_val_t){
            .kind = VIVAL_INT,
            .val  = mir_build_abs(mu, vi_i64_type(mu), vi_to_i64(mu, a.val)),
         };
         return true;
      case S_SCALAR_NOT:
         {
            mir_value_t ab = vi_to_bool(l, a);
            mir_value_t nb = mir_build_not(mu, ab);
            *out = vi_from_bool_bit(l, nb, a.is_bool, a.is_std);
            return true;
         }
      case S_IEEE_NOT:
         if (l->two_state) {
            mir_type_t t1 = mir_vec2_type(mu, 1, false);
            *out = a;
            out->val = mir_build_unary(mu, MIR_VEC_BIT_NOT, t1, vi_to_vec2_1(l, a));
         }
         else {
            *out = a;
            out->val = vi_std_scalar_not(l, a.val);
         }
         return true;
      case S_ARRAY_NOT:
         if (a.kind != VIVAL_VEC)
            return false;
         if (l->two_state) {
            *out = a;
            out->val = mir_build_unary(mu, MIR_VEC_BIT_NOT,
                                       mir_vec2_type(mu, a.width, a.is_signed),
                                       a.val);
            return true;
         }
         else {
            mir_value_t dst = vi_modeb_alloc_vec(l, a.width);
            mir_value_t idx_v = mir_add_var(mu, mir_offset_type(mu),
                                            MIR_NULL_STAMP, ident_uniq("ni"),
                                            MIR_VAR_TEMP);
            mir_build_store(mu, idx_v, vi_off(mu, 0));
            mir_block_t hdr = mir_add_block(mu);
            mir_block_t body = mir_add_block(mu);
            mir_block_t done = mir_add_block(mu);
            mir_build_jump(mu, hdr);
            mir_set_cursor(mu, hdr, MIR_APPEND);
            mir_value_t i = mir_build_load(mu, idx_v);
            mir_build_cond(mu, mir_build_cmp(mu, MIR_CMP_LT, i, vi_off(mu, a.width)),
                           body, done);
            mir_set_cursor(mu, body, MIR_APPEND);
            mir_value_t elem = mir_build_load(mu, mir_build_array_ref(mu, a.val, i));
            mir_value_t res = a.is_std ? vi_std_scalar_not(l, elem)
               : vi_u8_from_bool(mu, mir_build_not(mu, vi_u8_to_bool(mu, elem)));
            mir_build_store(mu, mir_build_array_ref(mu, dst, i), res);
            mir_build_store(mu, idx_v, mir_build_add(mu, mir_offset_type(mu),
                                                     i, vi_off(mu, 1)));
            mir_build_jump(mu, hdr);
            mir_set_cursor(mu, done, MIR_APPEND);
            *out = a;
            out->val = dst;
            return true;
         }
      case S_REDUCE_AND:
      case S_REDUCE_OR:
      case S_REDUCE_NAND:
      case S_REDUCE_NOR:
      case S_REDUCE_XOR:
      case S_REDUCE_XNOR:
         if (a.kind != VIVAL_VEC || !l->two_state)
            return false;
         {
            const uint32_t w = a.width;
            mir_type_t t1 = mir_vec2_type(mu, 1, false);
            mir_type_t tv = mir_vec2_type(mu, (int)w, a.is_signed);
            mir_value_t b = MIR_NULL_VALUE;
            if (sk == S_REDUCE_OR || sk == S_REDUCE_NOR) {
               mir_value_t eq0 = mir_build_binary(mu, MIR_VEC_CASE_EQ, tv, a.val,
                                                  vi_const_vec2(mu, (int)w, a.is_signed, NULL));
               b = mir_build_unary(mu, MIR_VEC_BIT_NOT, t1, eq0);
            }
            else if (sk == S_REDUCE_AND || sk == S_REDUCE_NAND) {
               b = mir_build_binary(mu, MIR_VEC_CASE_EQ, tv, a.val,
                                    vi_const_vec2_rep(mu, (int)w, a.is_signed, 1));
            }
            else {
               // XOR reduction
               b = mir_build_extract(mu, t1, a.val, vi_off(mu, 0));
               for (uint32_t i = 1; i < w; i++) {
                  mir_value_t bi = mir_build_extract(mu, t1, a.val, vi_off(mu, i));
                  b = mir_build_binary(mu, MIR_VEC_BIT_XOR, t1, b, bi);
               }
            }
            if (sk == S_REDUCE_NAND || sk == S_REDUCE_NOR || sk == S_REDUCE_XNOR)
               b = mir_build_unary(mu, MIR_VEC_BIT_NOT, t1, b);
            *out = (vi_val_t){
               .kind = VIVAL_BIT, .val = b, .width = 1, .is_std = a.is_std,
            };
            return true;
         }
      default:
         return false;
      }
   }

   if (np == 2) {
      // Boolean and bit AND/OR (and NAND/NOR) evaluate their right operand
      // only when the left one does not decide the result (LRM 9.2.2).  An
      // operand that cannot report anything is evaluated anyway, which
      // needs no branch.
      if (sk == S_SCALAR_AND || sk == S_SCALAR_OR
          || ((sk == S_SCALAR_NAND || sk == S_SCALAR_NOR)
              && !vi_expr_quiet(tree_value(tree_param(fcall, 1)), 0))) {
         const bool is_and = (sk == S_SCALAR_AND || sk == S_SCALAR_NAND);
         const bool neg = (sk == S_SCALAR_NAND || sk == S_SCALAR_NOR);
         vi_val_t a, b;
         if (!vi_lower_expr(l, tree_value(tree_param(fcall, 0)), &a))
            return false;
         mir_value_t ab = vi_to_bool(l, a), r;
         tree_t rhs = tree_value(tree_param(fcall, 1));
         if (vi_expr_quiet(rhs, 0)) {
            if (!vi_lower_expr(l, rhs, &b))
               return false;
            mir_value_t bb = vi_to_bool(l, b);
            r = is_and ? mir_build_and(mu, ab, bb) : mir_build_or(mu, ab, bb);
         }
         else {
            mir_type_t t_bool = mir_bool_type(mu);
            mir_value_t var = mir_add_var(mu, t_bool, MIR_NULL_STAMP,
                                          ident_uniq("sc"), MIR_VAR_TEMP);
            mir_build_store(mu, var, ab);
            mir_block_t rhs_bb = mir_add_block(mu);
            mir_block_t join_bb = mir_add_block(mu);
            if (is_and)
               mir_build_cond(mu, ab, rhs_bb, join_bb);
            else
               mir_build_cond(mu, ab, join_bb, rhs_bb);
            mir_set_cursor(mu, rhs_bb, MIR_APPEND);
            if (!vi_lower_expr(l, rhs, &b))
               return false;
            mir_build_store(mu, var, vi_to_bool(l, b));
            mir_build_jump(mu, join_bb);
            mir_set_cursor(mu, join_bb, MIR_APPEND);
            if (l->loc != NULL)
               mir_set_loc(mu, l->loc);
            r = mir_build_load(mu, var);
         }
         if (neg)
            r = mir_build_not(mu, r);
         *out = vi_from_bool_bit(l, r, a.is_bool, a.is_std);
         return true;
      }

      vi_val_t a, b;
      if (!vi_lower_expr(l, tree_value(tree_param(fcall, 0)), &a)
          || !vi_lower_expr(l, tree_value(tree_param(fcall, 1)), &b))
         return false;

      // Scalar integer & enum arithmetic/relational
      switch (sk) {
      case S_ADD:
         *out = (vi_val_t){
            .kind = VIVAL_INT,
            .val  = mir_build_add(mu, vi_i64_type(mu), vi_to_i64(mu, a.val),
                                  vi_to_i64(mu, b.val)),
         };
         return true;
      case S_SUB:
         *out = (vi_val_t){
            .kind = VIVAL_INT,
            .val  = mir_build_sub(mu, vi_i64_type(mu), vi_to_i64(mu, a.val),
                                  vi_to_i64(mu, b.val)),
         };
         return true;
      case S_MUL:
         *out = (vi_val_t){
            .kind = VIVAL_INT,
            .val  = mir_build_mul(mu, vi_i64_type(mu), vi_to_i64(mu, a.val),
                                  vi_to_i64(mu, b.val)),
         };
         return true;
      case S_DIV:
         if (l->spec)
            vi_spec_deopt_zero(l, b.val);
         *out = (vi_val_t){
            .kind = VIVAL_INT,
            .val  = mir_build_div(mu, vi_i64_type(mu), vi_to_i64(mu, a.val),
                                  vi_to_i64(mu, b.val)),
         };
         return true;
      case S_REM:
         if (l->spec)
            vi_spec_deopt_zero(l, b.val);
         *out = (vi_val_t){
            .kind = VIVAL_INT,
            .val  = mir_build_rem(mu, vi_i64_type(mu), vi_to_i64(mu, a.val),
                                  vi_to_i64(mu, b.val)),
         };
         return true;
      case S_MOD:
         if (l->spec)
            vi_spec_deopt_zero(l, b.val);
         *out = (vi_val_t){
            .kind = VIVAL_INT,
            .val  = mir_build_mod(mu, vi_i64_type(mu), vi_to_i64(mu, a.val),
                                  vi_to_i64(mu, b.val)),
         };
         return true;
      case S_EXP:
         *out = (vi_val_t){
            .kind = VIVAL_INT,
            .val  = mir_build_exp(mu, vi_i64_type(mu), vi_to_i64(mu, a.val),
                                  vi_to_i64(mu, b.val)),
         };
         return true;

      case S_MINIMUM:
      case S_MAXIMUM:
         if (a.kind == VIVAL_INT && b.kind == VIVAL_INT) {
            mir_value_t ai = vi_to_i64(mu, a.val), bi = vi_to_i64(mu, b.val);
            mir_cmp_t cmp = (sk == S_MINIMUM) ? MIR_CMP_LT : MIR_CMP_GT;
            *out = (vi_val_t){
               .kind = VIVAL_INT,
               .val  = mir_build_select(mu, vi_i64_type(mu),
                                        mir_build_cmp(mu, cmp, ai, bi), ai, bi),
            };
            return true;
         }
         return false;

      case S_SCALAR_XOR:
      case S_SCALAR_NAND:
      case S_SCALAR_NOR:
      case S_SCALAR_XNOR:
         {
            mir_value_t ab = vi_to_bool(l, a), bb = vi_to_bool(l, b), r;
            if (sk == S_SCALAR_XOR)       r = mir_build_xor(mu, ab, bb);
            else if (sk == S_SCALAR_NAND) r = mir_build_not(mu, mir_build_and(mu, ab, bb));
            else if (sk == S_SCALAR_NOR)  r = mir_build_not(mu, mir_build_or(mu, ab, bb));
            else                          r = mir_build_not(mu, mir_build_xor(mu, ab, bb));
            *out = vi_from_bool_bit(l, r, a.is_bool, a.is_std);
            return true;
         }

      case S_IEEE_AND:
      case S_IEEE_NAND:
      case S_IEEE_OR:
      case S_IEEE_NOR:
      case S_IEEE_XOR:
      case S_IEEE_XNOR:
         if (l->two_state) {
            mir_type_t t1 = mir_vec2_type(mu, 1, false);
            bool neg = (sk == S_IEEE_NAND || sk == S_IEEE_NOR || sk == S_IEEE_XNOR);
            mir_vec_op_t op = (sk == S_IEEE_AND || sk == S_IEEE_NAND) ? MIR_VEC_BIT_AND
               : ((sk == S_IEEE_OR || sk == S_IEEE_NOR) ? MIR_VEC_BIT_OR : MIR_VEC_BIT_XOR);
            mir_value_t r = mir_build_binary(mu, op, t1, vi_to_vec2_1(l, a),
                                             vi_to_vec2_1(l, b));
            if (neg)
               r = mir_build_unary(mu, MIR_VEC_BIT_NOT, t1, r);
            *out = a;
            out->val = r;
         }
         else {
            bool neg = (sk == S_IEEE_NAND || sk == S_IEEE_NOR || sk == S_IEEE_XNOR);
            int tbl_kind = (sk == S_IEEE_AND || sk == S_IEEE_NAND) ? 1
               : ((sk == S_IEEE_OR || sk == S_IEEE_NOR) ? 2 : 3);
            *out = a;
            out->val = vi_std_scalar_bin(l, tbl_kind, a.val, b.val, neg);
         }
         return true;

      case S_SCALAR_EQ:
      case S_SCALAR_NEQ:
      case S_SCALAR_LT:
      case S_SCALAR_LE:
      case S_SCALAR_GT:
      case S_SCALAR_GE:
         {
            mir_cmp_t cmp = (sk == S_SCALAR_EQ) ? MIR_CMP_EQ
               : (sk == S_SCALAR_NEQ) ? MIR_CMP_NEQ
               : (sk == S_SCALAR_LT) ? MIR_CMP_LT
               : (sk == S_SCALAR_LE) ? MIR_CMP_LEQ
               : (sk == S_SCALAR_GT) ? MIR_CMP_GT : MIR_CMP_GEQ;
            mir_value_t av, bv;
            if (a.kind == VIVAL_BIT && l->two_state) {
               av = mir_build_cast(mu, vi_u8_type(mu), vi_to_vec2_1(l, a));
               bv = mir_build_cast(mu, vi_u8_type(mu), vi_to_vec2_1(l, b));
            }
            else if (a.kind == VIVAL_BIT) {
               av = mir_build_cast(mu, vi_u8_type(mu), a.val);
               bv = mir_build_cast(mu, vi_u8_type(mu), b.val);
            }
            else {
               av = vi_to_i64(mu, a.val);
               bv = vi_to_i64(mu, b.val);
            }
            *out = vi_from_bool_bit(l, mir_build_cmp(mu, cmp, av, bv), true, false);
            return true;
         }

      case S_ARRAY_AND:
      case S_ARRAY_NAND:
      case S_ARRAY_OR:
      case S_ARRAY_NOR:
      case S_ARRAY_XOR:
      case S_ARRAY_XNOR:
         if (a.kind != VIVAL_VEC || b.kind != VIVAL_VEC || a.width != b.width)
            return false;
         if (l->two_state) {
            bool neg = (sk == S_ARRAY_NAND || sk == S_ARRAY_NOR || sk == S_ARRAY_XNOR);
            mir_vec_op_t op = (sk == S_ARRAY_AND || sk == S_ARRAY_NAND) ? MIR_VEC_BIT_AND
               : ((sk == S_ARRAY_OR || sk == S_ARRAY_NOR) ? MIR_VEC_BIT_OR : MIR_VEC_BIT_XOR);
            mir_type_t tv = mir_vec2_type(mu, (int)a.width, a.is_signed);
            mir_value_t r = mir_build_binary(mu, op, tv, a.val, b.val);
            if (neg)
               r = mir_build_unary(mu, MIR_VEC_BIT_NOT, tv, r);
            *out = a;
            out->val = r;
            return true;
         }
         else {
            bool neg = (sk == S_ARRAY_NAND || sk == S_ARRAY_NOR || sk == S_ARRAY_XNOR);
            int tbl_kind = (sk == S_ARRAY_AND || sk == S_ARRAY_NAND) ? 1
               : ((sk == S_ARRAY_OR || sk == S_ARRAY_NOR) ? 2 : 3);
            mir_value_t dst = vi_modeb_alloc_vec(l, a.width);
            mir_value_t idx_v = mir_add_var(mu, mir_offset_type(mu),
                                            MIR_NULL_STAMP, ident_uniq("bi"),
                                            MIR_VAR_TEMP);
            mir_build_store(mu, idx_v, vi_off(mu, 0));
            mir_block_t hdr = mir_add_block(mu);
            mir_block_t body = mir_add_block(mu);
            mir_block_t done = mir_add_block(mu);
            mir_build_jump(mu, hdr);
            mir_set_cursor(mu, hdr, MIR_APPEND);
            mir_value_t i = mir_build_load(mu, idx_v);
            mir_build_cond(mu, mir_build_cmp(mu, MIR_CMP_LT, i, vi_off(mu, a.width)),
                           body, done);
            mir_set_cursor(mu, body, MIR_APPEND);
            mir_value_t ea = mir_build_load(mu, mir_build_array_ref(mu, a.val, i));
            mir_value_t eb = mir_build_load(mu, mir_build_array_ref(mu, b.val, i));
            mir_value_t r;
            if (a.is_std)
               r = vi_std_scalar_bin(l, tbl_kind, ea, eb, neg);
            else {
               mir_value_t ba = vi_u8_to_bool(mu, ea);
               mir_value_t bb = vi_u8_to_bool(mu, eb);
               mir_value_t rb;
               if (tbl_kind == 1)      rb = mir_build_and(mu, ba, bb);
               else if (tbl_kind == 2) rb = mir_build_or(mu, ba, bb);
               else                    rb = mir_build_xor(mu, ba, bb);
               if (neg)
                  rb = mir_build_not(mu, rb);
               r = vi_u8_from_bool(mu, rb);
            }
            mir_build_store(mu, mir_build_array_ref(mu, dst, i), r);
            mir_build_store(mu, idx_v, mir_build_add(mu, mir_offset_type(mu),
                                                     i, vi_off(mu, 1)));
            mir_build_jump(mu, hdr);
            mir_set_cursor(mu, done, MIR_APPEND);
            *out = a;
            out->val = dst;
            return true;
         }

      case S_ARRAY_EQ:
      case S_ARRAY_NEQ:
         if (a.kind != VIVAL_VEC || b.kind != VIVAL_VEC)
            return false;
         if (a.width != b.width) {
            *out = vi_from_bool_bit(l, mir_const(mu, mir_bool_type(mu),
                                                 sk == S_ARRAY_NEQ), true, false);
            return true;
         }
         if (l->two_state) {
            mir_type_t tv = mir_vec2_type(mu, (int)a.width, false);
            mir_value_t cmp = mir_build_binary(mu,
                                               (sk == S_ARRAY_EQ)
                                               ? MIR_VEC_CASE_EQ : MIR_VEC_CASE_NEQ,
                                               tv, a.val, b.val);
            *out = (vi_val_t){
               .kind = VIVAL_BIT, .val = cmp, .width = 1, .is_bool = true,
            };
            return true;
         }
         else {
            mir_value_t eq_var = mir_add_var(mu, mir_bool_type(mu),
                                             MIR_NULL_STAMP, ident_uniq("aeq"),
                                             MIR_VAR_TEMP);
            mir_build_store(mu, eq_var, mir_const(mu, mir_bool_type(mu), 1));
            mir_value_t idx_v = mir_add_var(mu, mir_offset_type(mu),
                                            MIR_NULL_STAMP, ident_uniq("ci"),
                                            MIR_VAR_TEMP);
            mir_build_store(mu, idx_v, vi_off(mu, 0));
            mir_block_t hdr = mir_add_block(mu);
            mir_block_t body = mir_add_block(mu);
            mir_block_t mism = mir_add_block(mu);
            mir_block_t next_b = mir_add_block(mu);
            mir_block_t done = mir_add_block(mu);
            mir_build_jump(mu, hdr);
            mir_set_cursor(mu, hdr, MIR_APPEND);
            mir_value_t i = mir_build_load(mu, idx_v);
            mir_build_cond(mu, mir_build_cmp(mu, MIR_CMP_LT, i, vi_off(mu, a.width)),
                           body, done);
            mir_set_cursor(mu, body, MIR_APPEND);
            mir_value_t ea = mir_build_load(mu, mir_build_array_ref(mu, a.val, i));
            mir_value_t eb = mir_build_load(mu, mir_build_array_ref(mu, b.val, i));
            mir_build_cond(mu, mir_build_cmp(mu, MIR_CMP_EQ, ea, eb), next_b, mism);
            mir_set_cursor(mu, mism, MIR_APPEND);
            mir_build_store(mu, eq_var, mir_const(mu, mir_bool_type(mu), 0));
            mir_build_jump(mu, done);
            mir_set_cursor(mu, next_b, MIR_APPEND);
            mir_build_store(mu, idx_v, mir_build_add(mu, mir_offset_type(mu),
                                                     i, vi_off(mu, 1)));
            mir_build_jump(mu, hdr);
            mir_set_cursor(mu, done, MIR_APPEND);
            mir_value_t eqv = mir_build_load(mu, eq_var);
            if (sk == S_ARRAY_NEQ)
               eqv = mir_build_not(mu, eqv);
            *out = vi_from_bool_bit(l, eqv, true, false);
            return true;
         }

      case S_MATCH_EQ:
      case S_MATCH_NEQ:
      case S_MATCH_LT:
      case S_MATCH_LE:
      case S_MATCH_GT:
      case S_MATCH_GE:
         if (!l->two_state)
            return false;
         return vi_lower_match_cmp(l, (sk == S_MATCH_EQ) ? VIM_EQ
                                   : (sk == S_MATCH_NEQ) ? VIM_NEQ
                                   : (sk == S_MATCH_LT) ? VIM_LT
                                   : (sk == S_MATCH_LE) ? VIM_LE
                                   : (sk == S_MATCH_GT) ? VIM_GT : VIM_GE,
                                   a, b, false, false, out);

      case S_ARRAY_LT:
      case S_ARRAY_LE:
      case S_ARRAY_GT:
      case S_ARRAY_GE:
         if (a.kind != VIVAL_VEC || b.kind != VIVAL_VEC || a.width != b.width)
            return false;
         if (!l->two_state)
            return false;
         {
            mir_vec_op_t op = (sk == S_ARRAY_LT) ? MIR_VEC_LT
               : (sk == S_ARRAY_LE) ? MIR_VEC_LEQ
               : (sk == S_ARRAY_GT) ? MIR_VEC_GT : MIR_VEC_GEQ;
            mir_type_t tv = mir_vec2_type(mu, (int)a.width, false);
            mir_value_t va = mir_build_cast(mu, tv, a.val);
            mir_value_t vb = mir_build_cast(mu, tv, b.val);
            *out = (vi_val_t){
               .kind = VIVAL_BIT, .val = mir_build_binary(mu, op, tv, va, vb),
               .width = 1, .is_bool = true,
            };
            return true;
         }

      case S_SLL:
      case S_SRL:
      case S_SLA:
      case S_SRA:
      case S_ROL:
      case S_ROR:
         if (a.kind != VIVAL_VEC || b.kind != VIVAL_INT || !l->two_state)
            return false;
         // Delegate bit_vector shift/rotate via same 2-state helper names
         {
            const char *fn_alias = (sk == S_SLL) ? "\"sll\""
               : (sk == S_SRL) ? "\"srl\""
               : (sk == S_SLA) ? "\"sla\""
               : (sk == S_SRA) ? "\"sra\""
               : (sk == S_ROL) ? "\"rol\"" : "\"ror\"";
            return vi_lower_std_fcall(l, fcall, decl, "IEEE.NUMERIC_STD.",
                                      fn_alias, out);
         }

      default:
         return false;
      }
   }

   return false;
}

static bool vi_lower_const_array_ref(vi_lower_t *l, tree_t base_e,
                                     vi_val_t idx_val, tree_t idx_expr,
                                     vi_val_t *out)
{
   tree_t cur = base_e;
   type_t arr_t = (cur != NULL) ? tree_type(cur) : NULL;
   while (cur != NULL) {
      if (tree_kind(cur) == T_QUALIFIED || tree_kind(cur) == T_TYPE_CONV) {
         if (arr_t == NULL || type_is_unconstrained(arr_t))
            arr_t = tree_type(cur);
         cur = tree_value(cur);
      }
      else if (tree_kind(cur) == T_REF
               && tree_kind(tree_ref(cur)) == T_CONST_DECL
               && tree_has_value(tree_ref(cur))) {
         tree_t cd = tree_ref(cur);
         if (arr_t == NULL || type_is_unconstrained(arr_t))
            arr_t = tree_type(cd);
         cur = tree_value(cd);
      }
      else
         break;
   }
   if (cur == NULL || tree_kind(cur) != T_AGGREGATE
       || arr_t == NULL || !type_is_array(arr_t) || dimension_of(arr_t) != 1)
      return false;

   range_kind_t odir = RANGE_TO;
   int64_t oleft = 0, oright = 0;
   uint32_t nelems = 0;
   if (!vi_eval_type_bounds(l, arr_t, &odir, &oleft, &oright, &nelems)
       || nelems == 0)
      return false;

   mir_unit_t *mu = l->mu;
   type_t elem_t = type_elem(arr_t);

   // Case 1: Constant 2D array of vectors (e.g. ROM / S-box tables)
   if (type_is_array(elem_t) && dimension_of(elem_t) == 1
       && !type_is_array(type_elem(elem_t))
       && !type_is_record(type_elem(elem_t))) {
      range_kind_t idir = RANGE_DOWNTO;
      int64_t ileft = 0, iright = 0;
      uint32_t width = 0;
      if (!vi_eval_type_bounds(l, elem_t, &idir, &ileft, &iright, &width)
          || width == 0 || width > VHDL_ISLAND_MAX_WIDTH
          || (uint64_t)nelems * width > 65536u)
         return false;
      const uint32_t nw = (width + 63u) / 64u;

      const bool is_std = vi_type_is_std_logic(type_elem(elem_t));
      const bool is_sg = vi_type_is_signed_vec(elem_t);

      // Check cache for already-emitted ROM table
      mir_value_t tbl_ptr = MIR_NULL_VALUE;
      for (unsigned ci = 0; ci < l->n_rom_cache; ci++) {
         if (l->rom_cache[ci].agg == cur) {
            tbl_ptr = l->rom_cache[ci].ptr;
            break;
         }
      }

      if (mir_is_null(tbl_ptr)) {
         const size_t total = (size_t)nelems * width;
         uint8_t *tbl LOCAL = xcalloc_array(total, 1);
         uint8_t *row LOCAL = xcalloc_array(width, 1);
         bool *filled LOCAL = xcalloc_array(nelems, sizeof(bool));
         bool has_others = false;

         const int na = tree_assocs(cur);
         for (int i = 0; i < na; i++) {
            tree_t a = tree_assoc(cur, i);
            if (tree_subkind(a) == A_OTHERS) {
               if (!vi_lower_const_vec_eval(l, tree_value(a), elem_t, row, width))
                  return false;
               for (uint32_t p = 0; p < nelems; p++)
                  memcpy(tbl + (size_t)p * width, row, width);
               has_others = true;
               break;
            }
         }
         for (int i = 0; i < na; i++) {
            tree_t a = tree_assoc(cur, i);
            switch (tree_subkind(a)) {
            case A_OTHERS:
               break;
            case A_POS:
               if ((uint32_t)i >= nelems
                   || !vi_lower_const_vec_eval(l, tree_value(a), elem_t,
                                               tbl + (size_t)i * width, width))
                  return false;
               filled[i] = true;
               break;
            case A_NAMED:
               {
                  int64_t k = 0;
                  if (!vi_eval_const_int(l, tree_name(a), &k))
                     return false;
                  int64_t pos = (odir == RANGE_TO) ? (k - oleft) : (oleft - k);
                  if (pos < 0 || pos >= (int64_t)nelems
                      || !vi_lower_const_vec_eval(l, tree_value(a), elem_t,
                                                  tbl + (size_t)pos * width, width))
                     return false;
                  filled[pos] = true;
               }
               break;
            case A_RANGE:
               {
                  tree_t rg = tree_range(a, 0);
                  int64_t rl = 0, rr = 0;
                  if (!vi_eval_const_int(l, tree_left(rg), &rl)
                      || !vi_eval_const_int(l, tree_right(rg), &rr)
                      || !vi_lower_const_vec_eval(l, tree_value(a), elem_t,
                                                  row, width))
                     return false;
                  int64_t p0 = (odir == RANGE_TO) ? (rl - oleft) : (oleft - rl);
                  int64_t p1 = (odir == RANGE_TO) ? (rr - oleft) : (oleft - rr);
                  if (p0 > p1) { int64_t tmp = p0; p0 = p1; p1 = tmp; }
                  if (p0 < 0 || p1 >= (int64_t)nelems)
                     return false;
                  for (int64_t p = p0; p <= p1; p++) {
                     memcpy(tbl + (size_t)p * width, row, width);
                     filled[p] = true;
                  }
               }
               break;
            default:
               return false;
            }
         }
         if (!has_others) {
            for (uint32_t p = 0; p < nelems; p++) {
               if (!filled[p])
                  return false;
            }
         }

         // Constant index fast path: emit single row directly without table
         int64_t cidx = 0;
         if (!(l->two_state && width > 64)
             && vi_eval_const_int(l, idx_expr, &cidx)) {
            int64_t pos = (odir == RANGE_TO) ? (cidx - oleft) : (oleft - cidx);
            if (pos < 0 || pos >= (int64_t)nelems)
               return false;
            const uint8_t *rp = tbl + (size_t)pos * width;
            if (l->spec && is_std && vi_bytes_non01(rp, width))
               vi_emit_deopt(l, RT_ISLAND_DEOPT_LITERAL);
            if (l->two_state) {
               uint64_t w0 = 0;
               vi_pack_bytes_to_words(rp, width, is_std, &w0);
               *out = (vi_val_t){
                  .kind = VIVAL_VEC,
                  .val = vi_const_vec2(mu, (int)width, is_sg, &w0),
                  .width = width, .is_signed = is_sg, .is_std = is_std,
                  .dir = idir, .left = ileft, .right = iright,
               };
               return true;
            }
            mir_value_t *vals LOCAL = xmalloc_array(width, sizeof(mir_value_t));
            for (uint32_t b = 0; b < width; b++)
               vals[b] = vi_u8(mu, rp[b]);
            mir_value_t ca = mir_const_array(
               mu, mir_carray_type(mu, (int)width, vi_u8_type(mu)), vals, width);
            *out = (vi_val_t){
               .kind = VIVAL_VEC, .val = mir_build_address_of(mu, ca),
               .width = width, .is_signed = is_sg, .is_std = is_std,
               .dir = idir, .left = ileft, .right = iright,
            };
            return true;
         }

         if (l->spec && is_std && vi_bytes_non01(tbl, (size_t)nelems * width)) {
            // A row read at a dynamic index can be any of them
            if (l->reason == NULL)
               l->reason = "spec: constant table with metavalues";
            l->failed = true;
            return false;
         }
         unsigned cpos;
         mir_block_t cbb = vi_const_begin(l, &cpos);
         if (l->two_state) {
            // ceil(width / 64) i64 words per element, least significant
            // word first
            const size_t nvals = (size_t)nelems * nw;
            mir_value_t *vals LOCAL = xmalloc_array(nvals, sizeof(mir_value_t));
            uint64_t *words LOCAL = xcalloc_array(nw, sizeof(uint64_t));
            for (uint32_t p = 0; p < nelems; p++) {
               vi_pack_bytes_to_words(tbl + (size_t)p * width, width, is_std,
                                      words);
               for (uint32_t k = 0; k < nw; k++)
                  vals[(size_t)p * nw + k] = vi_i64(mu, (int64_t)words[k]);
            }
            mir_value_t ca = mir_const_array(
               mu, mir_carray_type(mu, (int)nvals, vi_i64_type(mu)),
               vals, nvals);
            tbl_ptr = mir_build_address_of(mu, ca);
         }
         else {
            mir_value_t *vals LOCAL = xmalloc_array(total, sizeof(mir_value_t));
            for (size_t k = 0; k < total; k++)
               vals[k] = vi_u8(mu, tbl[k]);
            mir_value_t ca = mir_const_array(
               mu, mir_carray_type(mu, (int)total, vi_u8_type(mu)),
               vals, total);
            tbl_ptr = mir_build_address_of(mu, ca);
         }
         vi_const_end(l, cbb, cpos);

         if (l->n_rom_cache < ARRAY_LEN(l->rom_cache)) {
            l->rom_cache[l->n_rom_cache].agg = cur;
            l->rom_cache[l->n_rom_cache].ptr = tbl_ptr;
            l->n_rom_cache++;
         }
      }

      vi_val_t fake_outer = { .dir = odir, .left = oleft, .right = oright };
      mir_value_t elem_idx = vi_compute_elem_pos(l, fake_outer, idx_val.val, nelems);
      if (l->two_state && width > 64) {
         // Assemble the row from its words, least significant word first
         mir_value_t base = mir_build_mul(mu, mir_offset_type(mu), elem_idx,
                                          vi_off(mu, nw));
         mir_value_t v = vi_cast_vec2(
            mu, mir_vec2_type(mu, (int)width, is_sg),
            mir_build_load(mu, mir_build_array_ref(mu, tbl_ptr, base)));
         for (uint32_t k = 1; k < nw; k++) {
            const int cw = MIN(64, (int)width - (int)(k * 64));
            mir_value_t at = mir_build_add(mu, mir_offset_type(mu), base,
                                           vi_off(mu, k));
            mir_value_t chunk = vi_cast_vec2(
               mu, mir_vec2_type(mu, cw, false),
               mir_build_load(mu, mir_build_array_ref(mu, tbl_ptr, at)));
            v = mir_build_insert(mu, chunk, v,
                                 vi_off(mu, (int)width - (int)(k * 64) - cw));
         }
         *out = (vi_val_t){
            .kind = VIVAL_VEC, .val = v, .width = width,
            .is_signed = is_sg, .is_std = is_std,
            .dir = idir, .left = ileft, .right = iright,
         };
         return true;
      }
      if (l->two_state) {
         mir_value_t raw = mir_build_load(
            mu, mir_build_array_ref(mu, tbl_ptr, elem_idx));
         mir_value_t v = mir_build_cast(
            mu, mir_vec2_type(mu, (int)width, is_sg), raw);
         *out = (vi_val_t){
            .kind = VIVAL_VEC, .val = v, .width = width,
            .is_signed = is_sg, .is_std = is_std,
            .dir = idir, .left = ileft, .right = iright,
         };
         return true;
      }
      mir_value_t byte_off = mir_build_mul(mu, mir_offset_type(mu),
                                           elem_idx, vi_off(mu, width));
      *out = (vi_val_t){
         .kind = VIVAL_VEC,
         .val  = mir_build_array_ref(mu, tbl_ptr, byte_off),
         .width = width, .is_signed = is_sg, .is_std = is_std,
         .dir = idir, .left = ileft, .right = iright,
      };
      return true;
   }

   // Case 2: Constant 1D array of scalar int / user enum
   if (!type_is_array(elem_t) && !type_is_record(elem_t)
       && vi_type_is_int_or_user_enum(elem_t) && nelems <= 4096u) {
      int64_t *tbl LOCAL = xcalloc_array(nelems, sizeof(int64_t));
      bool *filled LOCAL = xcalloc_array(nelems, sizeof(bool));
      bool has_others = false;
      const int na = tree_assocs(cur);
      for (int i = 0; i < na; i++) {
         tree_t a = tree_assoc(cur, i);
         if (tree_subkind(a) == A_OTHERS) {
            int64_t ov = 0;
            if (!vi_eval_const_int(l, tree_value(a), &ov))
               return false;
            for (uint32_t p = 0; p < nelems; p++)
               tbl[p] = ov;
            has_others = true;
            break;
         }
      }
      for (int i = 0; i < na; i++) {
         tree_t a = tree_assoc(cur, i);
         switch (tree_subkind(a)) {
         case A_OTHERS:
            break;
         case A_POS:
            {
               int64_t ev = 0;
               if ((uint32_t)i >= nelems || !vi_eval_const_int(l, tree_value(a), &ev))
                  return false;
               tbl[i] = ev;
               filled[i] = true;
            }
            break;
         case A_NAMED:
            {
               int64_t k = 0, ev = 0;
               if (!vi_eval_const_int(l, tree_name(a), &k)
                   || !vi_eval_const_int(l, tree_value(a), &ev))
                  return false;
               int64_t pos = (odir == RANGE_TO) ? (k - oleft) : (oleft - k);
               if (pos < 0 || pos >= (int64_t)nelems)
                  return false;
               tbl[pos] = ev;
               filled[pos] = true;
            }
            break;
         case A_RANGE:
            {
               tree_t rg = tree_range(a, 0);
               int64_t rl = 0, rr = 0, ev = 0;
               if (!vi_eval_const_int(l, tree_left(rg), &rl)
                   || !vi_eval_const_int(l, tree_right(rg), &rr)
                   || !vi_eval_const_int(l, tree_value(a), &ev))
                  return false;
               int64_t p0 = (odir == RANGE_TO) ? (rl - oleft) : (oleft - rl);
               int64_t p1 = (odir == RANGE_TO) ? (rr - oleft) : (oleft - rr);
               if (p0 > p1) { int64_t tmp = p0; p0 = p1; p1 = tmp; }
               if (p0 < 0 || p1 >= (int64_t)nelems)
                  return false;
               for (int64_t p = p0; p <= p1; p++) {
                  tbl[p] = ev;
                  filled[p] = true;
               }
            }
            break;
         default:
            return false;
         }
      }
      if (!has_others) {
         for (uint32_t p = 0; p < nelems; p++) {
            if (!filled[p])
               return false;
         }
      }

      mir_value_t tbl_ptr = MIR_NULL_VALUE;
      for (unsigned ci = 0; ci < l->n_rom_cache; ci++) {
         if (l->rom_cache[ci].agg == cur) {
            tbl_ptr = l->rom_cache[ci].ptr;
            break;
         }
      }
      if (mir_is_null(tbl_ptr)) {
         unsigned cpos;
         mir_block_t cbb = vi_const_begin(l, &cpos);
         mir_value_t *vals LOCAL = xmalloc_array(nelems, sizeof(mir_value_t));
         for (uint32_t p = 0; p < nelems; p++)
            vals[p] = vi_i64(mu, tbl[p]);
         mir_value_t ca = mir_const_array(
            mu, mir_carray_type(mu, (int)nelems, vi_i64_type(mu)), vals, nelems);
         tbl_ptr = mir_build_address_of(mu, ca);
         vi_const_end(l, cbb, cpos);
         if (l->n_rom_cache < ARRAY_LEN(l->rom_cache)) {
            l->rom_cache[l->n_rom_cache].agg = cur;
            l->rom_cache[l->n_rom_cache].ptr = tbl_ptr;
            l->n_rom_cache++;
         }
      }

      vi_val_t fake_outer = { .dir = odir, .left = oleft, .right = oright };
      mir_value_t elem_idx = vi_compute_elem_pos(l, fake_outer, idx_val.val, nelems);
      *out = (vi_val_t){
         .kind = VIVAL_INT,
         .val  = mir_build_load(mu, mir_build_array_ref(mu, tbl_ptr, elem_idx)),
      };
      return true;
   }

   return false;
}

static bool vi_lower_expr_inner(vi_lower_t *l, tree_t expr, vi_val_t *out);

static bool vi_lower_expr(vi_lower_t *l, tree_t expr, vi_val_t *out)
{
   const loc_t *old_loc = expr ? vi_push_loc(l, expr) : NULL;
   const bool ok = vi_lower_expr_inner(l, expr, out);
   vi_pop_loc(l, old_loc);
   if (!ok)
      vi_generic_reason(l, "expr", expr);
   return ok;
}

static bool vi_lower_expr_inner(vi_lower_t *l, tree_t expr, vi_val_t *out)
{
   if (expr == NULL || l->failed)
      return false;
   mir_unit_t *mu = l->mu;

   switch (tree_kind(expr)) {
   case T_LITERAL:
      if (tree_subkind(expr) == L_INT) {
         *out = (vi_val_t){ .kind = VIVAL_INT, .val = vi_i64(mu, tree_ival(expr)) };
         return true;
      }
      return false;

   case T_STRING:
      {
         const uint32_t n = (uint32_t)tree_chars(expr);
         if (n == 0)
            return false;
         type_t t = tree_type(expr);
         const bool is_std = vi_type_is_std_logic(type_elem(t));
         const bool is_sg = vi_type_is_signed_vec(t);
         uint8_t *bytes LOCAL = xmalloc_array(n, 1);
         if (!vi_lower_const_vec_eval(l, expr, t, bytes, n))
            return false;
         if (l->spec && is_std && vi_bytes_non01(bytes, n))
            vi_emit_deopt(l, RT_ISLAND_DEOPT_LITERAL);
         if (l->two_state) {
            const uint32_t nw = (n + 63u) / 64u;
            uint64_t *w LOCAL = xmalloc_array(nw, sizeof(uint64_t));
            vi_pack_bytes_to_words(bytes, n, is_std, w);
            *out = (vi_val_t){
               .kind = VIVAL_VEC, .val = vi_const_vec2(mu, (int)n, is_sg, w),
               .width = n, .is_signed = is_sg, .is_std = is_std,
               .dir = RANGE_DOWNTO, .left = (int64_t)n - 1, .right = 0,
            };
            return true;
         }
         mir_value_t *vals LOCAL = xmalloc_array(n, sizeof(mir_value_t));
         for (uint32_t i = 0; i < n; i++)
            vals[i] = vi_u8(mu, bytes[i]);
         mir_value_t ca = mir_const_array(mu, mir_carray_type(mu, (int)n, vi_u8_type(mu)),
                                          vals, n);
         *out = (vi_val_t){
            .kind = VIVAL_VEC, .val = mir_build_address_of(mu, ca),
            .width = n, .is_signed = is_sg, .is_std = is_std,
            .dir = RANGE_DOWNTO, .left = (int64_t)n - 1, .right = 0,
         };
         return true;
      }

   case T_REF:
      {
         tree_t d = tree_ref(expr);
         if (d == NULL)
            return false;
         switch (tree_kind(d)) {
         case T_ENUM_LIT:
            {
               type_t et = tree_type(d);
               if (vi_type_is_std_logic(et) || is_well_known(type_ident(type_base_recur(et))) == W_STD_BIT
                   || vi_type_is_bool(et)) {
                  const bool is_std = vi_type_is_std_logic(et);
                  const bool is_bool = vi_type_is_bool(et);
                  unsigned pos = tree_pos(d);
                  if (l->spec && is_std && pos != 2 && pos != 3)
                     vi_emit_deopt(l, RT_ISLAND_DEOPT_LITERAL);
                  if (l->two_state) {
                     uint8_t b = is_std ? std_enum_to_2bit(pos) : (uint8_t)(pos & 1u);
                     *out = (vi_val_t){
                        .kind = VIVAL_BIT,
                        .val  = mir_const_vec(mu, mir_vec2_type(mu, 1, false), b, 0),
                        .width = 1, .is_std = is_std, .is_bool = is_bool,
                     };
                  }
                  else {
                     *out = (vi_val_t){
                        .kind = VIVAL_BIT, .val = vi_u8(mu, (uint8_t)pos),
                        .width = 1, .is_std = is_std, .is_bool = is_bool,
                     };
                  }
               }
               else {
                  *out = (vi_val_t){
                     .kind = VIVAL_INT, .val = vi_i64(mu, tree_pos(d)),
                  };
               }
               return true;
            }
         case T_PORT_DECL:
         case T_SIGNAL_DECL:
            {
               const vhdl_isl_sig_t *isig = NULL;
               const vhdl_isig_t *s = vi_resolve_expr_sig(l, expr, &isig);
               if (s == NULL || s->is_array2d)
                  return false;
               return vi_lower_sig_load(l, s, isig, vi_off(mu, 0), out);
            }
         case T_VAR_DECL:
         case T_PARAM_DECL:
            {
               vi_local_t *loc = vi_find_local(l, d);
               if (loc != NULL) {
                  if (loc->by_val) {
                     *out = loc->pval;
                     return true;
                  }
                  if (loc->is_vec) {
                     *out = (vi_val_t){
                        .kind      = VIVAL_VEC,
                        .val       = l->two_state ? mir_build_load(mu, loc->storage) : loc->storage,
                        .width     = loc->width,
                        .is_signed = loc->is_signed,
                        .is_std    = loc->is_std,
                        .dir       = loc->dir,
                        .left      = loc->left,
                        .right     = loc->right,
                     };
                     return true;
                  }
                  if (loc->is_std || loc->is_bool || loc->width == 1) {
                     *out = (vi_val_t){
                        .kind    = VIVAL_BIT,
                        .val     = mir_build_load(mu, loc->storage),
                        .width   = 1,
                        .is_std  = loc->is_std,
                        .is_bool = loc->is_bool,
                     };
                     return true;
                  }
                  if (loc->has_const_int && vi_is_const_var(loc)) {
                     *out = (vi_val_t){
                        .kind = VIVAL_INT, .val = vi_i64(mu, loc->const_int),
                     };
                     return true;
                  }
                  *out = (vi_val_t){
                     .kind = VIVAL_INT, .val = mir_build_load(mu, loc->storage),
                  };
                  return true;
               }
               // Fallback: parameter of T_FOR loop might be in local var map
               return false;
            }
         case T_CONST_DECL:
         case T_GENERIC_DECL:
            {
               vi_local_t *loc = vi_find_local(l, d);
               if (loc != NULL && !mir_is_null(loc->storage)) {
                  if (loc->is_vec) {
                     *out = (vi_val_t){
                        .kind      = VIVAL_VEC,
                        .val       = l->two_state ? mir_build_load(mu, loc->storage) : loc->storage,
                        .width     = loc->width,
                        .is_signed = loc->is_signed,
                        .is_std    = loc->is_std,
                        .dir       = loc->dir,
                        .left      = loc->left,
                        .right     = loc->right,
                     };
                     return true;
                  }
                  if (loc->is_std || loc->is_bool || loc->width == 1) {
                     *out = (vi_val_t){
                        .kind    = VIVAL_BIT,
                        .val     = mir_build_load(mu, loc->storage),
                        .width   = 1,
                        .is_std  = loc->is_std,
                        .is_bool = loc->is_bool,
                     };
                     return true;
                  }
                  *out = (vi_val_t){
                     .kind = VIVAL_INT, .val = mir_build_load(mu, loc->storage),
                  };
                  return true;
               }
               if (vi_type_is_int_or_user_enum(tree_type(d))) {
                  int64_t cval;
                  if (vi_eval_const_int(l, expr, &cval)) {
                     *out = (vi_val_t){ .kind = VIVAL_INT, .val = vi_i64(mu, cval) };
                     return true;
                  }
               }
               if (tree_kind(d) == T_GENERIC_DECL) {
                  if (l->ctx != NULL) {
                     tree_t eblk = l->ctx->eval_block != NULL
                        ? l->ctx->eval_block : l->ctx->block;
                     const tree_t *eancs = l->ctx->eval_block != NULL
                        ? l->ctx->eval_ancestors : l->ctx->ancestors;
                     const int n_eancs = l->ctx->eval_block != NULL
                        ? l->ctx->n_eval_ancestors : l->ctx->n_ancestors;
                     for (int ai = -1; ai < n_eancs; ai++) {
                        tree_t gb = (ai < 0) ? eblk : eancs[ai];
                        if (gb == NULL)
                           continue;
                        const int ng = tree_generics(gb);
                        for (int gi = 0; gi < ng; gi++) {
                           if (tree_generic(gb, gi) == d) {
                              tree_t v = vhdl_island_generic_value(gb, gi);
                              return v != NULL && vi_lower_expr(l, v, out);
                           }
                        }
                     }
                  }
                  return false;
               }
               if (tree_has_value(d))
                  return vi_lower_expr(l, tree_value(d), out);
               return false;
            }
         default:
            return false;
         }
      }

   case T_RECORD_REF:
      {
         const vhdl_isl_sig_t *isig = NULL;
         const vhdl_isig_t *s = vi_resolve_expr_sig(l, expr, &isig);
         if (s != NULL && !s->is_array2d)
            return vi_lower_sig_load(l, s, isig, vi_off(mu, 0), out);

         // Flattened local record variable field reference: e.g. v.field / v.a.b
         {
            vi_local_t *rloc = NULL;
            int rpath[VHDL_ISIG_MAX_DEPTH];
            uint8_t rlen = 0;
            if (vi_extract_local_rec_path(l, expr, &rloc, rpath, &rlen)) {
               const vi_rec_leaf_t *lf = vi_find_rec_leaf(rloc, rpath, rlen);
               if (lf != NULL && !lf->is_array2d)
                  return vi_load_rec_leaf(l, lf, vi_off(mu, 0), out);
               return false;
            }
         }

         // Constant record field reference: e.g. my_const_rec.field
         tree_t base_e = tree_value(expr);
         tree_t field = tree_ref(expr);
         if (tree_kind(field) == T_FIELD_DECL) {
            tree_t rec_val = base_e;
            while (rec_val != NULL) {
               if (tree_kind(rec_val) == T_REF
                   && tree_kind(tree_ref(rec_val)) == T_CONST_DECL
                   && tree_has_value(tree_ref(rec_val)))
                  rec_val = tree_value(tree_ref(rec_val));
               else if (tree_kind(rec_val) == T_QUALIFIED
                        || tree_kind(rec_val) == T_TYPE_CONV)
                  rec_val = tree_value(rec_val);
               else
                  break;
            }
            if (rec_val != NULL && tree_kind(rec_val) == T_AGGREGATE) {
               const unsigned fpos = tree_pos(field);
               tree_t fval = NULL, others_val = NULL;
               const int na = tree_assocs(rec_val);
               for (int i = 0; i < na; i++) {
                  tree_t a = tree_assoc(rec_val, i);
                  switch (tree_subkind(a)) {
                  case A_POS:
                     if ((unsigned)i == fpos)
                        fval = tree_value(a);
                     break;
                  case A_NAMED:
                     if (tree_kind(tree_name(a)) == T_REF
                         && tree_ref(tree_name(a)) == field)
                        fval = tree_value(a);
                     break;
                  case A_OTHERS:
                     others_val = tree_value(a);
                     break;
                  default:
                     break;
                  }
               }
               if (fval == NULL)
                  fval = others_val;
               if (fval != NULL)
                  return vi_lower_expr(l, fval, out);
            }
         }
         return false;
      }

   case T_ARRAY_REF:
      {
         if (tree_params(expr) != 1)
            return false;
         vi_val_t iv;
         if (!vi_lower_expr(l, tree_value(tree_param(expr, 0)), &iv)
             || iv.kind != VIVAL_INT)
            return false;

         tree_t base_e = tree_value(expr);
         const vhdl_isl_sig_t *isig = NULL;
         const vhdl_isig_t *s = vi_resolve_expr_sig(l, base_e, &isig);
         if (s != NULL && s->is_array2d) {
            vi_val_t fake_outer = {
               .dir = s->outer_dir, .left = s->outer_left, .right = s->outer_right,
            };
            mir_value_t elem_idx = vi_compute_elem_pos(l, fake_outer, iv.val, s->nelems);
            return vi_lower_sig_load(l, s, isig, elem_idx, out);
         }

         if (s == NULL) {
            vi_rec_leaf_t llf;
            if (vi_resolve_local_leaf(l, base_e, &llf) && llf.is_array2d) {
               vi_val_t fake_outer = {
                  .dir = llf.outer_dir,
                  .left = llf.outer_left,
                  .right = llf.outer_right,
               };
               mir_value_t elem_idx = vi_compute_elem_pos(l, fake_outer, iv.val,
                                                          llf.nelems);
               return vi_load_rec_leaf(l, &llf, elem_idx, out);
            }
            if (vi_lower_const_array_ref(l, base_e, iv,
                                         tree_value(tree_param(expr, 0)), out))
               return true;
         }

         vi_val_t bv;
         if (!vi_lower_expr(l, base_e, &bv) || bv.kind != VIVAL_VEC)
            return false;
         mir_value_t pos = vi_compute_elem_pos(l, bv, iv.val, bv.width);
         if (l->two_state) {
            mir_value_t bit = mir_build_extract(mu, mir_vec2_type(mu, 1, false),
                                                bv.val, pos);
            *out = (vi_val_t){
               .kind = VIVAL_BIT, .val = bit, .width = 1, .is_std = bv.is_std,
            };
            return true;
         }
         mir_value_t b = mir_build_load(mu, mir_build_array_ref(mu, bv.val, pos));
         *out = (vi_val_t){
            .kind = VIVAL_BIT, .val = b, .width = 1, .is_std = bv.is_std,
         };
         return true;
      }

   case T_ARRAY_SLICE:
      {
         tree_t rg = tree_range(expr, 0);
         range_kind_t sdir = tree_subkind(rg);
         if (sdir != RANGE_TO && sdir != RANGE_DOWNTO)
            return false;
         int64_t sl = 0, sr = 0;
         const bool const_bounds =
            vi_eval_const_int(l, tree_left(rg), &sl)
            && vi_eval_const_int(l, tree_right(rg), &sr);

         if (const_bounds) {
            int64_t sw = (sdir == RANGE_TO) ? (sr - sl + 1) : (sl - sr + 1);
            if (sw <= 0 || sw > VHDL_ISLAND_MAX_WIDTH)
               return false;

            vi_val_t bv;
            if (!vi_lower_expr(l, tree_value(expr), &bv) || bv.kind != VIVAL_VEC)
               return false;
            if (sdir != bv.dir)
               return false;
            int64_t pos = (bv.dir == RANGE_TO) ? (sl - bv.left) : (bv.left - sl);
            if (pos < 0 || pos + sw > bv.width)
               return false;

            if (l->two_state) {
               mir_value_t sub = mir_build_extract(
                  mu, mir_vec2_type(mu, (int)sw, bv.is_signed), bv.val,
                  vi_off(mu, pos));
               *out = (vi_val_t){
                  .kind = VIVAL_VEC, .val = sub, .width = (uint32_t)sw,
                  .is_signed = bv.is_signed, .is_std = bv.is_std,
                  .dir = sdir, .left = sl, .right = sr,
               };
               return true;
            }
            mir_value_t sub_ptr = mir_build_array_ref(mu, bv.val, vi_off(mu, pos));
            *out = (vi_val_t){
               .kind = VIVAL_VEC, .val = sub_ptr, .width = (uint32_t)sw,
               .is_signed = bv.is_signed, .is_std = bv.is_std,
               .dir = sdir, .left = sl, .right = sr,
            };
            return true;
         }

         uint32_t sw = 0;
         if (!vhdl_island_const_slice_width(l->ctx, rg, &sw))
            return false;
         vi_val_t bv, lv;
         if (!vi_lower_expr(l, tree_value(expr), &bv) || bv.kind != VIVAL_VEC)
            return false;
         if (sdir != bv.dir || sw > bv.width)
            return false;
         if (!vi_lower_expr(l, tree_left(rg), &lv) || lv.kind != VIVAL_INT)
            return false;
         mir_value_t pos = vi_compute_elem_pos(l, bv, lv.val,
                                               bv.width - sw + 1u);
         if (l->two_state) {
            mir_value_t sub = mir_build_extract(
               mu, mir_vec2_type(mu, (int)sw, bv.is_signed), bv.val, pos);
            *out = (vi_val_t){
               .kind = VIVAL_VEC, .val = sub, .width = sw,
               .is_signed = bv.is_signed, .is_std = bv.is_std,
               .dir = sdir,
               .left = (sdir == RANGE_TO) ? 0 : (int64_t)sw - 1,
               .right = (sdir == RANGE_TO) ? (int64_t)sw - 1 : 0,
            };
            return true;
         }
         mir_value_t sub_ptr = mir_build_array_ref(mu, bv.val, pos);
         *out = (vi_val_t){
            .kind = VIVAL_VEC, .val = sub_ptr, .width = sw,
            .is_signed = bv.is_signed, .is_std = bv.is_std,
            .dir = sdir,
            .left = (sdir == RANGE_TO) ? 0 : (int64_t)sw - 1,
            .right = (sdir == RANGE_TO) ? (int64_t)sw - 1 : 0,
         };
         return true;
      }

   case T_QUALIFIED:
   case T_TYPE_CONV:
      {
         if (!vi_lower_expr(l, tree_value(expr), out))
            return false;
         type_t tt = tree_type(expr);
         if (out->kind == VIVAL_VEC) {
            out->is_signed = vi_type_is_signed_vec(tt);
            out->is_std = vi_type_is_std_logic(type_elem(tt));
            range_kind_t d; int64_t lv, rv; uint32_t w;
            if (vi_eval_type_bounds(l, tt, &d, &lv, &rv, &w) && w == out->width) {
               out->dir = d;
               out->left = lv;
               out->right = rv;
            }
            if (l->two_state)
               out->val = mir_build_cast(mu, mir_vec2_type(mu, (int)out->width,
                                                           out->is_signed),
                                         out->val);
         }
         return true;
      }

   case T_COND_EXPR:
      {
         type_t rt = tree_type(expr);
         if (type_is_array(rt)) {
            range_kind_t rd; int64_t rl, rr; uint32_t rw;
            if (!vi_eval_type_bounds(l, rt, &rd, &rl, &rr, &rw))
               return false;
            const bool rsg = vi_type_is_signed_vec(rt);
            const bool rstd = vi_type_is_std_logic(type_elem(rt));
            mir_value_t slot = l->two_state
               ? mir_add_var(mu, mir_vec2_type(mu, (int)rw, rsg), MIR_NULL_STAMP,
                             ident_uniq("ce"), MIR_VAR_TEMP)
               : vi_modeb_alloc_vec(l, rw);
            mir_block_t exit_bb = mir_add_block(mu);
            const int nc = tree_conds(expr);
            for (int i = 0; i < nc; i++) {
               tree_t br = tree_cond(expr, i);
               mir_block_t next_bb = (i + 1 < nc) ? mir_add_block(mu) : exit_bb;
               if (tree_has_value(br)) {
                  vi_val_t cv;
                  if (!vi_lower_expr(l, tree_value(br), &cv))
                     return false;
                  mir_block_t tbb = mir_add_block(mu);
                  mir_build_cond(mu, vi_to_bool(l, cv), tbb, next_bb);
                  mir_set_cursor(mu, tbb, MIR_APPEND);
               }
               vi_val_t vv;
               if (!vi_lower_expr(l, tree_value(tree_stmt(br, 0)), &vv)
                   || vv.width != rw)
                  return false;
               if (l->two_state)
                  mir_build_store(mu, slot, vv.val);
               else
                  mir_build_copy(mu, slot, vv.val, vi_off(mu, rw));
               mir_build_jump(mu, exit_bb);
               if (i + 1 < nc)
                  mir_set_cursor(mu, next_bb, MIR_APPEND);
            }
            mir_set_cursor(mu, exit_bb, MIR_APPEND);
            *out = (vi_val_t){
               .kind = VIVAL_VEC,
               .val  = l->two_state ? mir_build_load(mu, slot) : slot,
               .width = rw, .is_signed = rsg, .is_std = rstd,
               .dir = rd, .left = rl, .right = rr,
            };
            return true;
         }
         const bool is_int_like = vi_type_is_int_or_user_enum(rt);
         mir_type_t st = is_int_like ? vi_i64_type(mu)
            : (l->two_state ? mir_vec2_type(mu, 1, false) : vi_u8_type(mu));
         mir_value_t slot = mir_add_var(mu, st, MIR_NULL_STAMP,
                                        ident_uniq("ces"), MIR_VAR_TEMP);
         mir_block_t exit_bb = mir_add_block(mu);
         const int nc = tree_conds(expr);
         for (int i = 0; i < nc; i++) {
            tree_t br = tree_cond(expr, i);
            mir_block_t next_bb = (i + 1 < nc) ? mir_add_block(mu) : exit_bb;
            if (tree_has_value(br)) {
               vi_val_t cv;
               if (!vi_lower_expr(l, tree_value(br), &cv))
                  return false;
               mir_block_t tbb = mir_add_block(mu);
               mir_build_cond(mu, vi_to_bool(l, cv), tbb, next_bb);
               mir_set_cursor(mu, tbb, MIR_APPEND);
            }
            vi_val_t vv;
            if (!vi_lower_expr(l, tree_value(tree_stmt(br, 0)), &vv))
               return false;
            mir_build_store(mu, slot, is_int_like ? vi_to_i64(mu, vv.val)
                            : (l->two_state ? vi_to_vec2_1(l, vv) : vv.val));
            mir_build_jump(mu, exit_bb);
            if (i + 1 < nc)
               mir_set_cursor(mu, next_bb, MIR_APPEND);
         }
         mir_set_cursor(mu, exit_bb, MIR_APPEND);
         *out = (vi_val_t){
            .kind    = is_int_like ? VIVAL_INT : VIVAL_BIT,
            .val     = mir_build_load(mu, slot),
            .width   = 1,
            .is_std  = vi_type_is_std_logic(rt),
            .is_bool = vi_type_is_bool(rt),
         };
         return true;
      }

   case T_AGGREGATE:
      {
         type_t t = tree_type(expr);
         if (!type_is_array(t))
            return false;
         const int na = tree_assocs(expr);
         bool has_concat = false;
         bool all_pos = (na > 0);
         for (int i = 0; i < na; i++) {
            const assoc_kind_t ak = tree_subkind(tree_assoc(expr, i));
            if (ak == A_CONCAT)
               has_concat = true;
            if (ak != A_POS)
               all_pos = false;
         }
         const bool is_concat = has_concat || (all_pos && type_is_unconstrained(t));
         const bool is_std = vi_type_is_std_logic(type_elem(t));
         const bool is_sg = vi_type_is_signed_vec(t);

         if (is_concat) {
            vi_val_t *parts LOCAL = xcalloc_array(na, sizeof(vi_val_t));
            uint32_t tot = 0;
            for (int i = 0; i < na; i++) {
               if (!vi_lower_expr(l, tree_value(tree_assoc(expr, i)), &parts[i]))
                  return false;
               uint32_t pw = (parts[i].kind == VIVAL_VEC) ? parts[i].width : 1;
               if (parts[i].kind != VIVAL_VEC && parts[i].kind != VIVAL_BIT)
                  return false;
               tot += pw;
            }
            if (tot == 0 || tot > VHDL_ISLAND_MAX_WIDTH)
               return false;
            if (l->two_state) {
               mir_value_t acc = vi_const_vec2(mu, (int)tot, is_sg, NULL);
               uint32_t pos = 0;
               for (int i = 0; i < na; i++) {
                  mir_value_t pv = (parts[i].kind == VIVAL_VEC)
                     ? parts[i].val : vi_to_vec2_1(l, parts[i]);
                  uint32_t pw = (parts[i].kind == VIVAL_VEC) ? parts[i].width : 1;
                  acc = mir_build_insert(mu, pv, acc, vi_off(mu, pos));
                  pos += pw;
               }
               *out = (vi_val_t){
                  .kind = VIVAL_VEC, .val = acc, .width = tot,
                  .is_signed = is_sg, .is_std = is_std,
                  .dir = RANGE_DOWNTO, .left = (int64_t)tot - 1, .right = 0,
               };
               return true;
            }
            mir_value_t buf = vi_modeb_alloc_vec(l, tot);
            uint32_t pos = 0;
            for (int i = 0; i < na; i++) {
               if (parts[i].kind == VIVAL_VEC) {
                  mir_build_copy(mu, mir_build_array_ref(mu, buf, vi_off(mu, pos)),
                                 parts[i].val, vi_off(mu, parts[i].width));
                  pos += parts[i].width;
               }
               else {
                  mir_build_store(mu, mir_build_array_ref(mu, buf, vi_off(mu, pos)),
                                  parts[i].val);
                  pos += 1;
               }
            }
            *out = (vi_val_t){
               .kind = VIVAL_VEC, .val = buf, .width = tot,
               .is_signed = is_sg, .is_std = is_std,
               .dir = RANGE_DOWNTO, .left = (int64_t)tot - 1, .right = 0,
            };
            return true;
         }

         range_kind_t dir = RANGE_DOWNTO;
         int64_t left = 0, right = 0;
         uint32_t w = 0;
         if (!vi_eval_type_bounds(l, t, &dir, &left, &right, &w)) {
            // (others => X) for a slice with dynamic bounds and a constant
            // width, as in v(i * 8 + 7 downto i * 8) := (others => '0'):
            // only the width matters
            tree_t rg = (na == 1 && tree_subkind(tree_assoc(expr, 0)) == A_OTHERS
                         && !type_is_unconstrained(t) && dimension_of(t) == 1)
               ? range_of(t, 0) : NULL;
            if (rg == NULL || !vhdl_island_const_slice_width(l->ctx, rg, &w))
               return false;
            dir = tree_subkind(rg);
            left = dir == RANGE_DOWNTO ? (int64_t)w - 1 : 0;
            right = dir == RANGE_DOWNTO ? 0 : (int64_t)w - 1;
         }

         if (l->two_state) {
            mir_type_t tv = mir_vec2_type(mu, (int)w, is_sg);
            mir_value_t acc = vi_const_vec2(mu, (int)w, is_sg, NULL);
            for (int i = 0; i < na; i++) {
               tree_t a = tree_assoc(expr, i);
               if (tree_subkind(a) == A_OTHERS) {
                  vi_val_t ov;
                  if (!vi_lower_expr(l, tree_value(a), &ov))
                     return false;
                  int64_t cv;
                  mir_value_t ov1 = vi_to_vec2_1(l, ov);
                  uint64_t ab = 0, bb = 0;
                  if (mir_get_const(mu, ov1, &cv)) {
                     acc = vi_const_vec2_rep(mu, (int)w, is_sg, (cv & 1) ? 1 : 0);
                  }
                  else {
                     (void)ab; (void)bb;
                     mir_value_t all1 = vi_const_vec2_rep(mu, (int)w, is_sg, 1);
                     acc = mir_build_select(mu, tv, mir_build_test(mu, ov1),
                                            all1, acc);
                  }
                  break;
               }
            }
            for (int i = 0; i < na; i++) {
               tree_t a = tree_assoc(expr, i);
               if (tree_subkind(a) == A_OTHERS)
                  continue;
               vi_val_t ev;
               if (!vi_lower_expr(l, tree_value(a), &ev))
                  return false;
               mir_value_t ev1 = vi_to_vec2_1(l, ev);
               if (tree_subkind(a) == A_POS) {
                  acc = mir_build_insert(mu, ev1, acc, vi_off(mu, i));
               }
               else if (tree_subkind(a) == A_NAMED) {
                  int64_t k;
                  if (!vi_eval_const_int(l, tree_name(a), &k))
                     return false;
                  int64_t pos = (dir == RANGE_TO) ? (k - left) : (left - k);
                  if (pos < 0 || pos >= w)
                     return false;
                  acc = mir_build_insert(mu, ev1, acc, vi_off(mu, pos));
               }
               else if (tree_subkind(a) == A_RANGE) {
                  tree_t rg = tree_range(a, 0);
                  int64_t rl, rr;
                  if (!vi_eval_const_int(l, tree_left(rg), &rl)
                      || !vi_eval_const_int(l, tree_right(rg), &rr))
                     return false;
                  int64_t p0 = (dir == RANGE_TO) ? (rl - left) : (left - rl);
                  int64_t p1 = (dir == RANGE_TO) ? (rr - left) : (left - rr);
                  if (p0 > p1) { int64_t tmp = p0; p0 = p1; p1 = tmp; }
                  if (p0 < 0 || p1 >= w)
                     return false;
                  for (int64_t p = p0; p <= p1; p++)
                     acc = mir_build_insert(mu, ev1, acc, vi_off(mu, p));
               }
            }
            *out = (vi_val_t){
               .kind = VIVAL_VEC, .val = acc, .width = w,
               .is_signed = is_sg, .is_std = is_std,
               .dir = dir, .left = left, .right = right,
            };
            return true;
         }

         mir_value_t buf = vi_modeb_alloc_vec(l, w);
         for (int i = 0; i < na; i++) {
            tree_t a = tree_assoc(expr, i);
            if (tree_subkind(a) == A_OTHERS) {
               vi_val_t ov;
               if (!vi_lower_expr(l, tree_value(a), &ov) || ov.kind != VIVAL_BIT)
                  return false;
               mir_build_set(mu, buf, mir_build_cast(mu, vi_u8_type(mu), ov.val),
                             vi_off(mu, w));
               break;
            }
         }
         for (int i = 0; i < na; i++) {
            tree_t a = tree_assoc(expr, i);
            if (tree_subkind(a) == A_OTHERS)
               continue;
            vi_val_t ev;
            if (!vi_lower_expr(l, tree_value(a), &ev) || ev.kind != VIVAL_BIT)
               return false;
            mir_value_t ev8 = mir_build_cast(mu, vi_u8_type(mu), ev.val);
            if (tree_subkind(a) == A_POS) {
               mir_build_store(mu, mir_build_array_ref(mu, buf, vi_off(mu, i)), ev8);
            }
            else if (tree_subkind(a) == A_NAMED) {
               int64_t k;
               if (!vi_eval_const_int(l, tree_name(a), &k))
                  return false;
               int64_t pos = (dir == RANGE_TO) ? (k - left) : (left - k);
               if (pos < 0 || pos >= w)
                  return false;
               mir_build_store(mu, mir_build_array_ref(mu, buf, vi_off(mu, pos)), ev8);
            }
            else if (tree_subkind(a) == A_RANGE) {
               tree_t rg = tree_range(a, 0);
               int64_t rl, rr;
               if (!vi_eval_const_int(l, tree_left(rg), &rl)
                   || !vi_eval_const_int(l, tree_right(rg), &rr))
                  return false;
               int64_t p0 = (dir == RANGE_TO) ? (rl - left) : (left - rl);
               int64_t p1 = (dir == RANGE_TO) ? (rr - left) : (left - rr);
               if (p0 > p1) { int64_t tmp = p0; p0 = p1; p1 = tmp; }
               if (p0 < 0 || p1 >= w)
                  return false;
               mir_build_set(mu, mir_build_array_ref(mu, buf, vi_off(mu, p0)),
                             ev8, vi_off(mu, p1 - p0 + 1));
            }
         }
         *out = (vi_val_t){
            .kind = VIVAL_VEC, .val = buf, .width = w,
            .is_signed = is_sg, .is_std = is_std,
            .dir = dir, .left = left, .right = right,
         };
         return true;
      }

   case T_ATTR_REF:
      {
         int64_t cv;
         if (vi_eval_const_int(l, expr, &cv)) {
            *out = (vi_val_t){ .kind = VIVAL_INT, .val = vi_i64(mu, cv) };
            return true;
         }
         const attr_kind_t ak = tree_subkind(expr);
         tree_t base = tree_name(expr);
         if (ak == ATTR_POS || ak == ATTR_VAL) {
            if (tree_params(expr) != 1)
               return false;
            vi_val_t av;
            if (!vi_lower_expr(l, tree_value(tree_param(expr, 0)), &av))
               return false;
            if (ak == ATTR_POS) {
               if (av.kind == VIVAL_INT)
                  *out = av;
               else if (av.kind == VIVAL_BIT && !l->two_state)
                  *out = (vi_val_t){ .kind = VIVAL_INT, .val = vi_to_i64(mu, av.val) };
               else if (av.kind == VIVAL_BIT && l->two_state && av.is_std) {
                  mir_value_t b = vi_to_bool(l, av);
                  *out = (vi_val_t){
                     .kind = VIVAL_INT,
                     .val  = mir_build_select(mu, vi_i64_type(mu), b,
                                              vi_i64(mu, 3), vi_i64(mu, 2)),
                  };
               }
               else
                  *out = (vi_val_t){ .kind = VIVAL_INT, .val = vi_to_i64(mu, av.val) };
               return true;
            }
            // T'VAL(X) of an integer or a user enumeration, both held as
            // their position
            type_t rt = tree_type(expr);
            if (av.kind != VIVAL_INT || !vi_type_is_int_or_user_enum(rt))
               return false;
            mir_value_t v = vi_to_i64(mu, av.val);
            if (!vi_check_scalar_bounds(l, rt, v, expr))
               return false;
            *out = (vi_val_t){ .kind = VIVAL_INT, .val = v };
            return true;
         }
         if (tree_kind(base) == T_REF) {
            vi_local_t *loc = vi_find_local(l, tree_ref(base));
            if (loc != NULL && loc->is_vec) {
               switch (ak) {
               case ATTR_LENGTH: cv = loc->width; break;
               case ATTR_LEFT:   cv = loc->left; break;
               case ATTR_RIGHT:  cv = loc->right; break;
               case ATTR_LOW:    cv = (loc->dir == RANGE_TO) ? loc->left : loc->right; break;
               case ATTR_HIGH:   cv = (loc->dir == RANGE_TO) ? loc->right : loc->left; break;
               case ATTR_ASCENDING: cv = (loc->dir == RANGE_TO) ? 1 : 0; break;
               default: return false;
               }
               if (ak == ATTR_ASCENDING)
                  *out = vi_from_bool_bit(l, mir_const(mu, mir_bool_type(mu), cv), true, false);
               else
                  *out = (vi_val_t){ .kind = VIVAL_INT, .val = vi_i64(mu, cv) };
               return true;
            }
         }
         return false;
      }

   case T_FCALL:
      {
         tree_t decl = tree_ref(expr);
         const subprogram_kind_t sk = tree_subkind(decl);
         if (sk != S_USER && sk != S_IEEE_MISC && sk != S_IEEE_LOGICAL) {
            if (sk == S_RECORD_EQ || sk == S_RECORD_NEQ) {
               // Leaf-by-leaf record comparison (signals and/or record locals)
               if (tree_params(expr) != 2)
                  return false;
               tree_t e0 = tree_value(tree_param(expr, 0));
               tree_t e1 = tree_value(tree_param(expr, 1));
               uint32_t *ids0 = NULL, *ids1 = NULL;
               uint32_t n0 = 0, n1 = 0;
               const vi_rec_leaf_t *lf0[512], *lf1[512];
               bool e0_loc = false, e1_loc = false;
               vi_local_t *loc0 = NULL, *loc1 = NULL;
               int p0[VHDL_ISIG_MAX_DEPTH], p1[VHDL_ISIG_MAX_DEPTH];
               uint8_t pl0 = 0, pl1 = 0;

               if (vi_extract_local_rec_path(l, e0, &loc0, p0, &pl0)) {
                  e0_loc = true;
                  n0 = vi_collect_rec_prefix_leaves(loc0, p0, pl0, lf0, 512);
                  if (n0 > 512)
                     return false;
               }
               else if (!vhdl_island_collect_expr_leaves(l->ctx, e0, &ids0, &n0)) {
                  free(ids0);
                  return false;
               }

               if (vi_extract_local_rec_path(l, e1, &loc1, p1, &pl1)) {
                  e1_loc = true;
                  n1 = vi_collect_rec_prefix_leaves(loc1, p1, pl1, lf1, 512);
                  if (n1 > 512) {
                     free(ids0);
                     return false;
                  }
               }
               else if (!vhdl_island_collect_expr_leaves(l->ctx, e1, &ids1, &n1)) {
                  free(ids0);
                  free(ids1);
                  return false;
               }

               if (n0 != n1 || n0 == 0) {
                  free(ids0);
                  free(ids1);
                  return false;
               }

               mir_value_t acc = mir_const(mu, mir_bool_type(mu), 1);
               for (uint32_t i = 0; i < n0; i++) {
                  const bool a2d0 = e0_loc ? lf0[i]->is_array2d
                     : l->ctx->sigs[ids0[i]].is_array2d;
                  const bool a2d1 = e1_loc ? lf1[i]->is_array2d
                     : l->ctx->sigs[ids1[i]].is_array2d;
                  const uint32_t w0 = e0_loc ? lf0[i]->width
                     : l->ctx->sigs[ids0[i]].width;
                  const uint32_t w1 = e1_loc ? lf1[i]->width
                     : l->ctx->sigs[ids1[i]].width;
                  if (w0 != w1 || a2d0 != a2d1) {
                     free(ids0);
                     free(ids1);
                     return false;
                  }
                  const uint32_t nrows = a2d0
                     ? (e0_loc ? lf0[i]->nelems : l->ctx->sigs[ids0[i]].nelems) : 1u;
                  if (a2d0) {
                     const uint32_t nr1 = e1_loc ? lf1[i]->nelems
                        : l->ctx->sigs[ids1[i]].nelems;
                     if (nrows != nr1) {
                        free(ids0);
                        free(ids1);
                        return false;
                     }
                  }
                  const vhdl_isig_t *s0 = e0_loc ? NULL : &(l->ctx->sigs[ids0[i]]);
                  const vhdl_isig_t *s1 = e1_loc ? NULL : &(l->ctx->sigs[ids1[i]]);
                  const vhdl_isl_sig_t *is0 = e0_loc ? NULL
                     : vhdl_island_find_sig(l->isl, ids0[i]);
                  const vhdl_isl_sig_t *is1 = e1_loc ? NULL
                     : vhdl_island_find_sig(l->isl, ids1[i]);
                  if ((!e0_loc && is0 == NULL) || (!e1_loc && is1 == NULL)) {
                     free(ids0);
                     free(ids1);
                     return false;
                  }

                  for (uint32_t row = 0; row < nrows; row++) {
                     vi_val_t v0, v1;
                     bool ok0 = e0_loc
                        ? vi_load_rec_leaf(l, lf0[i], vi_off(mu, row), &v0)
                        : vi_lower_sig_load(l, s0, is0, vi_off(mu, row), &v0);
                     bool ok1 = e1_loc
                        ? vi_load_rec_leaf(l, lf1[i], vi_off(mu, row), &v1)
                        : vi_lower_sig_load(l, s1, is1, vi_off(mu, row), &v1);
                     if (!ok0 || !ok1 || v0.kind != v1.kind) {
                        free(ids0);
                        free(ids1);
                        return false;
                     }
                     mir_value_t eq;
                     if (v0.kind == VIVAL_VEC) {
                        if (l->two_state) {
                           mir_value_t c = mir_build_binary(mu, MIR_VEC_CASE_EQ,
                                                            mir_vec2_type(mu, (int)v0.width, false),
                                                            v0.val, v1.val);
                           eq = mir_build_test(mu, c);
                        }
                        else {
                           eq = mir_const(mu, mir_bool_type(mu), 1);
                           for (uint32_t k = 0; k < v0.width; k++) {
                              mir_value_t b0 = mir_build_load(mu, mir_build_array_ref(mu, v0.val, vi_off(mu, k)));
                              mir_value_t b1 = mir_build_load(mu, mir_build_array_ref(mu, v1.val, vi_off(mu, k)));
                              eq = mir_build_and(mu, eq, mir_build_cmp(mu, MIR_CMP_EQ, b0, b1));
                           }
                        }
                     }
                     else if (v0.kind == VIVAL_INT) {
                        eq = mir_build_cmp(mu, MIR_CMP_EQ,
                                           vi_to_i64(mu, v0.val),
                                           vi_to_i64(mu, v1.val));
                     }
                     else {
                        if (l->two_state)
                           eq = mir_build_cmp(mu, MIR_CMP_EQ,
                                              mir_build_cast(mu, vi_u8_type(mu), vi_to_vec2_1(l, v0)),
                                              mir_build_cast(mu, vi_u8_type(mu), vi_to_vec2_1(l, v1)));
                        else
                           eq = mir_build_cmp(mu, MIR_CMP_EQ, v0.val, v1.val);
                     }
                     acc = mir_build_and(mu, acc, eq);
                  }
               }
               free(ids0);
               free(ids1);
               if (sk == S_RECORD_NEQ)
                  acc = mir_build_not(mu, acc);
               *out = vi_from_bool_bit(l, acc, true, false);
               return true;
            }
            return vi_lower_builtin_fcall(l, expr, decl, sk, out);
         }

         const char *s = istr(tree_ident2(decl));
         static const char *const pkgs[] = {
            "IEEE.STD_LOGIC_1164.",
            "IEEE.NUMERIC_STD.",
            "IEEE.NUMERIC_STD_UNSIGNED.",
            "IEEE.STD_LOGIC_UNSIGNED.",
            "IEEE.STD_LOGIC_SIGNED.",
            "IEEE.STD_LOGIC_ARITH.",
         };
         for (size_t i = 0; i < ARRAY_LEN(pkgs); i++) {
            size_t plen = strlen(pkgs[i]);
            if (strncmp(s, pkgs[i], plen) == 0)
               return vi_lower_std_fcall(l, expr, decl, pkgs[i], s + plen, out);
         }
         return vi_lower_inline_func(l, expr, decl, out, NULL);
      }

   default:
      return false;
   }
}

static void vi_mark_ff_dirty(vi_lower_t *l, const vhdl_isl_sig_t *isig)
{
   if (!l->is_ff || l->ff_dom == NULL || mir_is_null(l->ff_dirty_var))
      return;
   const uint32_t isl_sig_idx = (uint32_t)(isig - l->isl->sigs);
   uint32_t bit = 62u;
   for (uint32_t w = 0; w < l->ff_dom->n_write_sigs; w++) {
      if (l->ff_dom->write_isl_sigs[w] == isl_sig_idx) {
         bit = (w < 62u) ? w : 62u;
         break;
      }
   }
   mir_unit_t *mu = l->mu;
   mir_type_t t64 = mir_vec2_type(mu, 64, false);
   mir_value_t cur = mir_build_load(mu, l->ff_dirty_var);
   mir_value_t upd = mir_build_binary(mu, MIR_VEC_BIT_OR, t64, cur,
                                      mir_const_vec(mu, t64, UINT64_C(1) << bit, 0));
   mir_build_store(mu, l->ff_dirty_var, upd);
}

// Speculative Mode A: a store of the whole of row ELEM_IDX of the sparse RAM
// ISIG appends the packed row to the inline log, which the runtime copies
// to the rows at commit (and clears the row clean byte, see
// island_commit_sparse_entries in rt/island.c).  A full log, which Mode B
// spills to the runtime, returns a deopt code instead: the variant never
// calls out, so the runtime can drop the entries of a call that deopts
// (the function saves the log position in the pad word on entry).
static bool vi_emit_sparse_ram_write_a(vi_lower_t *l, const vhdl_isig_t *s,
                                       const vhdl_isl_sig_t *isig,
                                       mir_value_t elem_idx, bool is_full,
                                       mir_value_t sub_off, vi_val_t val)
{
   if (!(isig->rt_flags & RT_ISIG_TWO_STATE)) {
      l->reason = "spec: sparse RAM of non-logic elements";
      return false;
   }

   vi_spec_check_held(l, isig);
   vi_mark_ff_dirty(l, isig);

   mir_unit_t *mu = l->mu;
   mir_type_t t_off = mir_offset_type(mu);
   mir_type_t t_i64 = vi_i64_type(mu);
   mir_type_t t_v64 = mir_vec2_type(mu, 64, false);
   mir_type_t t_row = mir_vec2_type(mu, (int)s->width, false);
   const uint32_t log_off = l->isl->sparse_log_off;
   const uint32_t isl_sig_idx = (uint32_t)(isig - l->isl->sigs);
   const uint32_t len = ((s->width + 63u) / 64u) * 8u;
   const uint32_t ent_base = log_off + offsetof(rt_island_sparse_log_t, entries);
   const uint32_t data_base = log_off + offsetof(rt_island_sparse_log_t, data);

   mir_value_t byte_off = mir_build_mul(mu, t_off, vi_to_off(mu, elem_idx),
                                        vi_off(mu, len));
   mir_value_t key = mir_build_add(
      mu, t_i64,
      mir_build_mul(mu, t_i64, vi_to_i64(mu, byte_off),
                    vi_i64(mu, INT64_C(1) << 32)),
      vi_i64(mu, (int64_t)isl_sig_idx));

   mir_value_t part = (val.kind == VIVAL_VEC) ? val.val : vi_to_vec2_1(l, val);
   if (val.is_signed)
      part = mir_build_cast(mu, mir_vec2_type(mu, (int)(val.kind == VIVAL_VEC
                                                       ? val.width : 1),
                                               false), part);
   mir_value_t v = part;
   if (!is_full) {
      // A partial store merges into the latest pending store of the row
      // (the log holds whole rows), or else into the row, which must be
      // clean
      mir_value_t base_v = mir_add_var(mu, mir_pointer_type(mu, vi_u8_type(mu)),
                                       MIR_NULL_STAMP, ident_uniq("srow"),
                                       MIR_VAR_TEMP);
      mir_value_t k_v = mir_add_var(mu, t_off, MIR_NULL_STAMP,
                                    ident_uniq("slog"), MIR_VAR_TEMP);
      mir_build_store(mu, base_v, mir_build_array_ref(
                         mu, l->state_ptr,
                         mir_build_add(mu, t_off, vi_off(mu, isig->a_off),
                                       byte_off)));
      mir_build_store(mu, k_v, mir_build_cast(
                         mu, t_off, mir_build_island_load(
                            mu, t_v64, l->state_ptr,
                            vi_off(mu, log_off
                                   + offsetof(rt_island_sparse_log_t, count)),
                            vi_off(mu, -1), vi_off(mu, 0))));
      mir_block_t hdr = mir_add_block(mu);
      mir_block_t body = mir_add_block(mu);
      mir_block_t hit = mir_add_block(mu);
      mir_block_t next = mir_add_block(mu);
      mir_block_t miss = mir_add_block(mu);
      mir_block_t done = mir_add_block(mu);
      mir_build_jump(mu, hdr);

      mir_set_cursor(mu, hdr, MIR_APPEND);
      mir_value_t k = mir_build_load(mu, k_v);
      mir_build_cond(mu, mir_build_cmp(mu, MIR_CMP_GT, k, vi_off(mu, 0)),
                     body, miss);

      mir_set_cursor(mu, body, MIR_APPEND);
      mir_value_t km1 = mir_build_sub(mu, t_off, k, vi_off(mu, 1));
      mir_build_store(mu, k_v, km1);
      mir_value_t e2 = mir_build_mul(mu, t_off, km1, vi_off(mu, 2));
      mir_value_t w0 = mir_build_cast(
         mu, t_i64, mir_build_island_load(mu, t_v64, l->state_ptr,
                                          vi_off(mu, ent_base),
                                          vi_off(mu, -1), e2));
      mir_build_cond(mu, mir_build_cmp(mu, MIR_CMP_EQ, w0, key), hit, next);

      mir_set_cursor(mu, hit, MIR_APPEND);
      mir_value_t w1 = mir_build_cast(
         mu, t_i64, mir_build_island_load(mu, t_v64, l->state_ptr,
                                          vi_off(mu, ent_base + 8u),
                                          vi_off(mu, -1), e2));
      mir_value_t doff = mir_build_div(mu, t_i64, w1,
                                       vi_i64(mu, INT64_C(1) << 32));
      mir_build_store(mu, base_v, mir_build_array_ref(
                         mu, l->state_ptr,
                         mir_build_add(mu, t_off, vi_off(mu, data_base),
                                       vi_to_off(mu, doff))));
      mir_build_jump(mu, done);

      mir_set_cursor(mu, next, MIR_APPEND);
      mir_build_jump(mu, hdr);

      mir_set_cursor(mu, miss, MIR_APPEND);
      vi_spec_check_row(l, s, isig, 0, elem_idx);
      mir_build_jump(mu, done);

      mir_set_cursor(mu, done, MIR_APPEND);
      mir_value_t cur = mir_build_island_load(mu, t_row,
                                              mir_build_load(mu, base_v),
                                              vi_off(mu, 0), vi_off(mu, -1),
                                              vi_off(mu, 0));
      v = mir_build_insert(mu, part, cur, sub_off);
   }

   mir_value_t cnt = mir_build_cast(
      mu, t_off, mir_build_island_load(
         mu, t_v64, l->state_ptr,
         vi_off(mu, log_off + offsetof(rt_island_sparse_log_t, count)),
         vi_off(mu, -1), vi_off(mu, 0)));
   mir_value_t dused = mir_build_cast(
      mu, t_off, mir_build_island_load(
         mu, t_v64, l->state_ptr,
         vi_off(mu, log_off + offsetof(rt_island_sparse_log_t, data_used)),
         vi_off(mu, -1), vi_off(mu, 0)));
   mir_value_t next_dused = mir_build_add(mu, t_off, dused, vi_off(mu, len));

   mir_value_t ok_cnt = mir_build_cmp(mu, MIR_CMP_LT, cnt,
                                      vi_off(mu, RT_ISLAND_SPARSE_INLINE_CAP));
   mir_value_t ok_data = mir_build_cmp(mu, MIR_CMP_LEQ, next_dused,
                                       vi_off(mu, RT_ISLAND_SPARSE_INLINE_DATA));
   vi_emit_deopt_unless(l, mir_build_and(mu, ok_cnt, ok_data),
                        RT_ISLAND_DEOPT_RANGE);

   mir_value_t e2 = mir_build_mul(mu, t_off, cnt, vi_off(mu, 2));
   mir_value_t w1_i = mir_build_add(
      mu, t_i64,
      mir_build_mul(mu, t_i64, vi_to_i64(mu, dused),
                    vi_i64(mu, INT64_C(1) << 32)),
      vi_i64(mu, (int64_t)len));
   mir_build_island_store(mu, l->state_ptr, vi_off(mu, ent_base),
                          vi_off(mu, -1), e2, mir_build_cast(mu, t_v64, key));
   mir_build_island_store(mu, l->state_ptr, vi_off(mu, ent_base + 8u),
                          vi_off(mu, -1), e2, mir_build_cast(mu, t_v64, w1_i));

   mir_value_t dst_ptr = mir_build_array_ref(
      mu, l->state_ptr, mir_build_add(mu, t_off, vi_off(mu, data_base), dused));
   mir_build_island_store(mu, dst_ptr, vi_off(mu, 0), vi_off(mu, -1),
                          vi_off(mu, 0), v);

   mir_build_island_store(
      mu, l->state_ptr,
      vi_off(mu, log_off + offsetof(rt_island_sparse_log_t, count)),
      vi_off(mu, -1), vi_off(mu, 0),
      mir_build_cast(mu, t_v64, mir_build_add(mu, t_off, cnt, vi_off(mu, 1))));
   mir_build_island_store(
      mu, l->state_ptr,
      vi_off(mu, log_off + offsetof(rt_island_sparse_log_t, data_used)),
      vi_off(mu, -1), vi_off(mu, 0), mir_build_cast(mu, t_v64, next_dused));
   return true;
}

// Speculative Mode A flip-flop function entry: save the log position (count
// in the low half of the pad word, data used in the high half)
static void vi_spec_sparse_mark(vi_lower_t *l)
{
   if (!l->spec || l->isl->sparse_log_off == 0)
      return;
   mir_unit_t *mu = l->mu;
   mir_type_t t_v64 = mir_vec2_type(mu, 64, false);
   mir_type_t t_i64 = vi_i64_type(mu);
   const uint32_t log_off = l->isl->sparse_log_off;
   mir_value_t cnt = mir_build_cast(
      mu, t_i64, mir_build_island_load(
         mu, t_v64, l->state_ptr,
         vi_off(mu, log_off + offsetof(rt_island_sparse_log_t, count)),
         vi_off(mu, -1), vi_off(mu, 0)));
   mir_value_t dused = mir_build_cast(
      mu, t_i64, mir_build_island_load(
         mu, t_v64, l->state_ptr,
         vi_off(mu, log_off + offsetof(rt_island_sparse_log_t, data_used)),
         vi_off(mu, -1), vi_off(mu, 0)));
   mir_value_t mark = mir_build_add(
      mu, t_i64, cnt,
      mir_build_mul(mu, t_i64, dused, vi_i64(mu, INT64_C(1) << 32)));
   mir_build_island_store(
      mu, l->state_ptr,
      vi_off(mu, log_off + offsetof(rt_island_sparse_log_t, pad)),
      vi_off(mu, -1), vi_off(mu, 0), mir_build_cast(mu, t_v64, mark));
}

static bool vi_emit_sparse_ram_write(vi_lower_t *l, const vhdl_isig_t *s,
                                     const vhdl_isl_sig_t *isig,
                                     mir_value_t elem_idx,
                                     mir_value_t sub_off, uint32_t len,
                                     vi_val_t val)
{
   if (l->spec && l->is_ff && l->isl->sparse_log_off != 0) {
      int64_t c = -1;
      const bool full = mir_get_const(l->mu, sub_off, &c) && c == 0
         && len == s->width;
      return vi_emit_sparse_ram_write_a(l, s, isig, elem_idx, full, sub_off,
                                        val);
   }
   if (!l->is_ff || l->two_state || l->isl->sparse_log_off == 0 || len == 0)
      return false;

   vi_mark_ff_dirty(l, isig);

   mir_unit_t *mu = l->mu;
   mir_type_t t_off = mir_offset_type(mu);
   mir_type_t t_i64 = vi_i64_type(mu);
   mir_type_t t_v64 = mir_vec2_type(mu, 64, false);
   const uint32_t log_off = l->isl->sparse_log_off;
   const uint32_t isl_sig_idx = (uint32_t)(isig - l->isl->sigs);

   mir_value_t byte_off = mir_build_add(
      mu, t_off,
      mir_build_mul(mu, t_off, elem_idx, vi_off(mu, s->width)),
      sub_off);

   mir_value_t cnt_v = mir_build_island_load(
      mu, t_v64, l->state_ptr,
      vi_off(mu, log_off + offsetof(rt_island_sparse_log_t, count)),
      vi_off(mu, -1), vi_off(mu, 0));
   mir_value_t cnt = mir_build_cast(mu, t_off, cnt_v);

   mir_value_t dused_v = mir_build_island_load(
      mu, t_v64, l->state_ptr,
      vi_off(mu, log_off + offsetof(rt_island_sparse_log_t, data_used)),
      vi_off(mu, -1), vi_off(mu, 0));
   mir_value_t dused = mir_build_cast(mu, t_off, dused_v);
   mir_value_t next_dused = mir_build_add(mu, t_off, dused, vi_off(mu, len));

   mir_value_t ok_cnt = mir_build_cmp(mu, MIR_CMP_LT, cnt,
                                      vi_off(mu, RT_ISLAND_SPARSE_INLINE_CAP));
   mir_value_t ok_data = mir_build_cmp(mu, MIR_CMP_LEQ, next_dused,
                                       vi_off(mu, RT_ISLAND_SPARSE_INLINE_DATA));
   mir_value_t can_inline = mir_build_and(mu, ok_cnt, ok_data);

   mir_block_t fast_bb = mir_add_block(mu);
   mir_block_t slow_bb = mir_add_block(mu);
   mir_block_t done_bb = mir_add_block(mu);
   mir_build_cond(mu, can_inline, fast_bb, slow_bb);

   mir_set_cursor(mu, fast_bb, MIR_APPEND);
   mir_value_t e2 = mir_build_mul(mu, t_off, cnt, vi_off(mu, 2));
   mir_value_t w0_i = mir_build_add(
      mu, t_i64,
      mir_build_mul(mu, t_i64, vi_to_i64(mu, byte_off),
                    vi_i64(mu, INT64_C(1) << 32)),
      vi_i64(mu, (int64_t)isl_sig_idx));
   mir_value_t w1_i = mir_build_add(
      mu, t_i64,
      mir_build_mul(mu, t_i64, vi_to_i64(mu, dused),
                    vi_i64(mu, INT64_C(1) << 32)),
      vi_i64(mu, (int64_t)len));

   const uint32_t ent_base = log_off + offsetof(rt_island_sparse_log_t, entries);
   mir_build_island_store(mu, l->state_ptr, vi_off(mu, ent_base),
                          vi_off(mu, -1), e2, mir_build_cast(mu, t_v64, w0_i));
   mir_build_island_store(mu, l->state_ptr, vi_off(mu, ent_base + 8u),
                          vi_off(mu, -1), e2, mir_build_cast(mu, t_v64, w1_i));

   mir_value_t dst_off = mir_build_add(
      mu, t_off,
      vi_off(mu, log_off + offsetof(rt_island_sparse_log_t, data)), dused);
   mir_value_t dst_ptr = mir_build_array_ref(mu, l->state_ptr, dst_off);
   if (val.kind == VIVAL_VEC)
      mir_build_copy(mu, dst_ptr, val.val, vi_off(mu, len));
   else
      mir_build_store(mu, dst_ptr, mir_build_cast(mu, vi_u8_type(mu), val.val));

   mir_value_t next_cnt = mir_build_add(mu, t_off, cnt, vi_off(mu, 1));
   mir_build_island_store(
      mu, l->state_ptr,
      vi_off(mu, log_off + offsetof(rt_island_sparse_log_t, count)),
      vi_off(mu, -1), vi_off(mu, 0), mir_build_cast(mu, t_v64, next_cnt));
   mir_build_island_store(
      mu, l->state_ptr,
      vi_off(mu, log_off + offsetof(rt_island_sparse_log_t, data_used)),
      vi_off(mu, -1), vi_off(mu, 0), mir_build_cast(mu, t_v64, next_dused));
   mir_build_jump(mu, done_bb);

   mir_set_cursor(mu, slow_bb, MIR_APPEND);
   mir_value_t src_ptr = val.val;
   if (val.kind != VIVAL_VEC) {
      src_ptr = vi_modeb_alloc_vec(l, 1);
      mir_build_store(mu, src_ptr, mir_build_cast(mu, vi_u8_type(mu), val.val));
   }
   mir_value_t isl_v = mir_build_island_load(
      mu, t_v64, l->state_ptr,
      vi_off(mu, log_off + offsetof(rt_island_sparse_log_t, isl_ptr)),
      vi_off(mu, -1), vi_off(mu, 0));
   mir_value_t fargs[5] = {
      mir_build_cast(mu, t_off, isl_v),
      vi_off(mu, isl_sig_idx),
      byte_off,
      vi_off(mu, len),
      src_ptr,
   };
   mir_value_t fret = mir_build_fcall(mu, ident_new("__sv_island_sparse_write"),
                                      t_off, MIR_NULL_STAMP, fargs, 5);
   // Keep call live through MIR DCE (helper returns 0)
   if (!mir_is_null(l->ff_dirty_var)) {
      mir_value_t cur_dm = mir_build_load(mu, l->ff_dirty_var);
      mir_value_t upd_dm = mir_build_binary(mu, MIR_VEC_BIT_OR, t_v64, cur_dm,
                                            mir_build_cast(mu, t_v64, fret));
      mir_build_store(mu, l->ff_dirty_var, upd_dm);
   }
   mir_build_jump(mu, done_bb);

   mir_set_cursor(mu, done_bb, MIR_APPEND);
   return true;
}

static bool vi_assign_leaf_sig(vi_lower_t *l, const vhdl_isig_t *s,
                               const vhdl_isl_sig_t *isig,
                               mir_value_t elem_idx, int64_t pos_off,
                               uint32_t sub_w, bool is_full, vi_val_t val)
{
   mir_unit_t *mu = l->mu;
   if (s->is_sparse_ram)
      return vi_emit_sparse_ram_write(l, s, isig, elem_idx,
                                      vi_off(mu, is_full ? 0 : pos_off),
                                      sub_w, val);
   const uint32_t dst_off = isig->a_off + (l->is_ff ? l->ff_stride : 0u);
   vi_mark_ff_dirty(l, isig);

   if (s->elem_kind == VISIG_ENUM || s->elem_kind == VISIG_INT) {
      if (val.kind != VIVAL_INT)
         return false;
      if (s->byte_size == 1 && !s->is_signed) {
         mir_value_t ptr = vi_state_ptr_at(l, dst_off);
         mir_value_t v = mir_build_cast(mu, vi_u8_type(mu),
                                        vi_to_i64(mu, val.val));
         mir_build_store(mu, ptr, v);
         return true;
      }
      mir_type_t vt = mir_vec2_type(mu, (int)s->byte_size * 8, s->is_signed);
      mir_value_t v = vi_cast_vec2(mu, vt, vi_to_i64(mu, val.val));
      mir_build_island_store(mu, l->state_ptr, vi_off(mu, dst_off),
                             vi_off(mu, -1), vi_off(mu, 0), v);
      return true;
   }

   if (l->two_state) {
      const uint32_t half = l->is_ff ? l->ff_stride : 0u;
      if (l->is_ff && is_full && sub_w == s->width && !s->is_array2d)
         vi_spec_store_held(l, isig);
      else
         vi_spec_check_held(l, isig);
      if (is_full && sub_w == s->width) {
         if (s->is_array2d)
            vi_spec_clear_row(l, s, isig, half, elem_idx);
         mir_value_t v = (val.kind == VIVAL_VEC) ? val.val : vi_to_vec2_1(l, val);
         if (val.is_signed != s->is_signed)
            v = mir_build_cast(mu, mir_vec2_type(mu, (int)s->width, s->is_signed), v);
         mir_build_island_store(mu, l->state_ptr, vi_off(mu, dst_off),
                                vi_off(mu, -1), elem_idx, v);
         return true;
      }
      if (s->is_array2d)
         vi_spec_check_row(l, s, isig, half, elem_idx);
      mir_value_t cur = mir_build_island_load(
         mu, mir_vec2_type(mu, (int)s->width, s->is_signed),
         l->state_ptr, vi_off(mu, dst_off), vi_off(mu, -1), elem_idx);
      mir_value_t part = (val.kind == VIVAL_VEC) ? val.val : vi_to_vec2_1(l, val);
      mir_value_t updated = mir_build_insert(mu, part, cur, vi_off(mu, pos_off));
      mir_build_island_store(mu, l->state_ptr, vi_off(mu, dst_off),
                             vi_off(mu, -1), elem_idx, updated);
      return true;
   }

   // Mode B (9-state flat byte array)
   mir_value_t base = vi_state_ptr_at(l, dst_off);
   if (!s->is_vector && !s->is_array2d) {
      mir_build_store(mu, base, mir_build_cast(mu, vi_u8_type(mu), val.val));
      return true;
   }

   mir_value_t off = vi_off(mu, is_full ? 0 : pos_off);
   if (s->is_array2d) {
      mir_value_t mul = mir_build_mul(mu, mir_offset_type(mu), elem_idx,
                                      vi_off(mu, s->width));
      off = mir_build_add(mu, mir_offset_type(mu), mul, off);
   }

   mir_value_t dst = mir_build_array_ref(mu, base, off);
   if (val.kind == VIVAL_VEC)
      mir_build_copy(mu, dst, val.val, vi_off(mu, sub_w));
   else
      mir_build_store(mu, dst, mir_build_cast(mu, vi_u8_type(mu), val.val));
   return true;
}

static bool vi_assign_leaf_sig_dyn_pos(vi_lower_t *l, const vhdl_isig_t *s,
                                       const vhdl_isl_sig_t *isig,
                                       mir_value_t elem_idx, mir_value_t dyn_pos,
                                       vi_val_t val)
{
   if (s->is_sparse_ram)
      return vi_emit_sparse_ram_write(l, s, isig, elem_idx, dyn_pos, 1u, val);
   mir_unit_t *mu = l->mu;
   const uint32_t dst_off = isig->a_off + (l->is_ff ? l->ff_stride : 0u);
   vi_mark_ff_dirty(l, isig);
   if (l->two_state) {
      vi_spec_check_held(l, isig);
      if (s->is_array2d)
         vi_spec_check_row(l, s, isig, l->is_ff ? l->ff_stride : 0u,
                           elem_idx);
      mir_value_t cur = mir_build_island_load(
         mu, mir_vec2_type(mu, (int)s->width, s->is_signed),
         l->state_ptr, vi_off(mu, dst_off), vi_off(mu, -1), elem_idx);
      mir_value_t updated = mir_build_insert(mu, vi_to_vec2_1(l, val), cur, dyn_pos);
      mir_build_island_store(mu, l->state_ptr, vi_off(mu, dst_off),
                             vi_off(mu, -1), elem_idx, updated);
      return true;
   }
   mir_value_t base = vi_state_ptr_at(l, dst_off);
   mir_value_t off = dyn_pos;
   if (s->is_array2d) {
      mir_value_t mul = mir_build_mul(mu, mir_offset_type(mu), elem_idx,
                                      vi_off(mu, s->width));
      off = mir_build_add(mu, mir_offset_type(mu), mul, off);
   }
   mir_build_store(mu, mir_build_array_ref(mu, base, off),
                   mir_build_cast(mu, vi_u8_type(mu), val.val));
   return true;
}

static bool vi_assign_leaf_sig_dyn_slice(vi_lower_t *l, const vhdl_isig_t *s,
                                         const vhdl_isl_sig_t *isig,
                                         mir_value_t elem_idx,
                                         mir_value_t dyn_pos,
                                         uint32_t sub_w, vi_val_t val)
{
   if (val.kind != VIVAL_VEC)
      return false;
   if (s->is_sparse_ram)
      return vi_emit_sparse_ram_write(l, s, isig, elem_idx, dyn_pos, sub_w, val);
   mir_unit_t *mu = l->mu;
   const uint32_t dst_off = isig->a_off + (l->is_ff ? l->ff_stride : 0u);
   vi_mark_ff_dirty(l, isig);
   if (l->two_state) {
      vi_spec_check_held(l, isig);
      if (s->is_array2d)
         vi_spec_check_row(l, s, isig, l->is_ff ? l->ff_stride : 0u,
                           elem_idx);
      mir_value_t cur = mir_build_island_load(
         mu, mir_vec2_type(mu, (int)s->width, s->is_signed),
         l->state_ptr, vi_off(mu, dst_off), vi_off(mu, -1), elem_idx);
      mir_value_t part = val.is_signed
         ? mir_build_cast(mu, mir_vec2_type(mu, (int)sub_w, false), val.val)
         : val.val;
      mir_build_island_store(mu, l->state_ptr, vi_off(mu, dst_off),
                             vi_off(mu, -1), elem_idx,
                             mir_build_insert(mu, part, cur, dyn_pos));
      return true;
   }
   mir_value_t base = vi_state_ptr_at(l, dst_off);
   mir_value_t off = dyn_pos;
   if (s->is_array2d) {
      mir_value_t mul = mir_build_mul(mu, mir_offset_type(mu), elem_idx,
                                      vi_off(mu, s->width));
      off = mir_build_add(mu, mir_offset_type(mu), mul, off);
   }
   mir_build_copy(mu, mir_build_array_ref(mu, base, off),
                  val.val, vi_off(mu, sub_w));
   return true;
}

static bool vi_copy_array2d_sig(vi_lower_t *l, const vhdl_isig_t *ds,
                                const vhdl_isl_sig_t *dis,
                                const vhdl_isig_t *ss,
                                const vhdl_isl_sig_t *sis)
{
   if (!ds->is_array2d || !ss->is_array2d
       || ds->nelems != ss->nelems || ds->width != ss->width)
      return false;

   mir_unit_t *mu = l->mu;
   vi_note_read(l, sis);
   if (!l->two_state) {
      const uint32_t dst_off = dis->a_off + (l->is_ff ? l->ff_stride : 0u);
      vi_mark_ff_dirty(l, dis);
      mir_build_copy(mu, vi_state_ptr_at(l, dst_off),
                     vi_read_ptr(l, sis),
                     vi_off(mu, (int64_t)ds->nelems * ds->width));
      return true;
   }

   mir_value_t idx_v = mir_add_var(mu, mir_offset_type(mu), MIR_NULL_STAMP,
                                   ident_uniq("a2c"), MIR_VAR_TEMP);
   mir_build_store(mu, idx_v, vi_off(mu, 0));
   mir_block_t hdr = mir_add_block(mu);
   mir_block_t body = mir_add_block(mu);
   mir_block_t done = mir_add_block(mu);
   mir_build_jump(mu, hdr);

   mir_set_cursor(mu, hdr, MIR_APPEND);
   mir_value_t r = mir_build_load(mu, idx_v);
   mir_build_cond(mu, mir_build_cmp(mu, MIR_CMP_LT, r, vi_off(mu, ds->nelems)),
                  body, done);

   mir_set_cursor(mu, body, MIR_APPEND);
   vi_val_t rv;
   if (!vi_lower_sig_load(l, ss, sis, r, &rv)
       || !vi_assign_leaf_sig(l, ds, dis, r, 0, ds->width, true, rv))
      return false;
   mir_build_store(mu, idx_v, mir_build_add(mu, mir_offset_type(mu),
                                            r, vi_off(mu, 1)));
   mir_build_jump(mu, hdr);

   mir_set_cursor(mu, done, MIR_APPEND);
   return true;
}

static bool vi_assign_array2d_sig(vi_lower_t *l, const vhdl_isig_t *s,
                                  const vhdl_isl_sig_t *isig, tree_t val_e)
{
   if (s->is_sparse_ram)
      return false;
   while (val_e != NULL) {
      if (tree_kind(val_e) == T_REF
          && tree_kind(tree_ref(val_e)) == T_CONST_DECL
          && tree_has_value(tree_ref(val_e)))
         val_e = tree_value(tree_ref(val_e));
      else if (tree_kind(val_e) == T_QUALIFIED
               || tree_kind(val_e) == T_TYPE_CONV)
         val_e = tree_value(val_e);
      else
         break;
   }
   if (val_e == NULL)
      return false;

   const vhdl_isl_sig_t *sis = NULL;
   const vhdl_isig_t *ss = vi_resolve_expr_sig(l, val_e, &sis);
   if (ss != NULL && ss->is_array2d)
      return vi_copy_array2d_sig(l, s, isig, ss, sis);

   {
      vi_rec_leaf_t slf;
      if (vi_resolve_local_leaf(l, val_e, &slf)
          && slf.is_array2d && slf.nelems == s->nelems && slf.width == s->width) {
         const uint32_t dst_off = isig->a_off + (l->is_ff ? l->ff_stride : 0u);
         vi_mark_ff_dirty(l, isig);
         vi_spec_check_held(l, isig);
         vi_spec_all_rows(l, s, isig, l->is_ff ? l->ff_stride : 0u, true);
         mir_build_copy(l->mu, vi_state_ptr_at(l, dst_off), slf.storage,
                        vi_off(l->mu, vi_array2d_bytes(l, s->nelems, s->width)));
         return true;
      }
   }

   if (tree_kind(val_e) != T_AGGREGATE)
      return false;

   mir_unit_t *mu = l->mu;
   const int na = tree_assocs(val_e);
   bool *filled LOCAL = xcalloc_array(s->nelems, sizeof(bool));
   bool has_others = false;

   for (int i = 0; i < na; i++) {
      tree_t a = tree_assoc(val_e, i);
      if (tree_subkind(a) == A_OTHERS) {
         vi_val_t ov;
         if (!vi_lower_expr(l, tree_value(a), &ov)
             || ov.kind != VIVAL_VEC || ov.width != s->width)
            return false;

         mir_value_t idx_v = mir_add_var(mu, mir_offset_type(mu),
                                         MIR_NULL_STAMP, ident_uniq("a2r"),
                                         MIR_VAR_TEMP);
         mir_build_store(mu, idx_v, vi_off(mu, 0));
         mir_block_t hdr = mir_add_block(mu);
         mir_block_t body = mir_add_block(mu);
         mir_block_t done = mir_add_block(mu);
         mir_build_jump(mu, hdr);

         mir_set_cursor(mu, hdr, MIR_APPEND);
         mir_value_t r = mir_build_load(mu, idx_v);
         mir_build_cond(mu, mir_build_cmp(mu, MIR_CMP_LT, r,
                                          vi_off(mu, s->nelems)),
                        body, done);

         mir_set_cursor(mu, body, MIR_APPEND);
         if (!vi_assign_leaf_sig(l, s, isig, r, 0, s->width, true, ov))
            return false;
         mir_build_store(mu, idx_v, mir_build_add(mu, mir_offset_type(mu),
                                                  r, vi_off(mu, 1)));
         mir_build_jump(mu, hdr);

         mir_set_cursor(mu, done, MIR_APPEND);
         has_others = true;
         break;
      }
   }

   for (int i = 0; i < na; i++) {
      tree_t a = tree_assoc(val_e, i);
      if (tree_subkind(a) == A_OTHERS)
         continue;
      vi_val_t ev;
      if (!vi_lower_expr(l, tree_value(a), &ev)
          || ev.kind != VIVAL_VEC || ev.width != s->width)
         return false;
      switch (tree_subkind(a)) {
      case A_POS:
         if ((uint32_t)i >= s->nelems)
            return false;
         if (!vi_assign_leaf_sig(l, s, isig, vi_off(mu, i), 0, s->width,
                                 true, ev))
            return false;
         filled[i] = true;
         break;
      case A_NAMED:
         {
            int64_t k = 0;
            if (!vi_eval_const_int(l, tree_name(a), &k))
               return false;
            int64_t pos = (s->outer_dir == RANGE_TO)
               ? (k - s->outer_left) : (s->outer_left - k);
            if (pos < 0 || pos >= (int64_t)s->nelems)
               return false;
            if (!vi_assign_leaf_sig(l, s, isig, vi_off(mu, pos), 0, s->width,
                                    true, ev))
               return false;
            filled[pos] = true;
         }
         break;
      case A_RANGE:
         {
            tree_t rg = tree_range(a, 0);
            int64_t rl = 0, rr = 0;
            if (!vi_eval_const_int(l, tree_left(rg), &rl)
                || !vi_eval_const_int(l, tree_right(rg), &rr))
               return false;
            int64_t p0 = (s->outer_dir == RANGE_TO)
               ? (rl - s->outer_left) : (s->outer_left - rl);
            int64_t p1 = (s->outer_dir == RANGE_TO)
               ? (rr - s->outer_left) : (s->outer_left - rr);
            if (p0 > p1) { int64_t tmp = p0; p0 = p1; p1 = tmp; }
            if (p0 < 0 || p1 >= (int64_t)s->nelems)
               return false;
            for (int64_t p = p0; p <= p1; p++) {
               if (!vi_assign_leaf_sig(l, s, isig, vi_off(mu, p), 0, s->width,
                                       true, ev))
                  return false;
               filled[p] = true;
            }
         }
         break;
      default:
         return false;
      }
   }

   if (!has_others) {
      for (uint32_t p = 0; p < s->nelems; p++) {
         if (!filled[p])
            return false;
      }
   }
   return true;
}

static bool vi_assign_array2d_local(vi_lower_t *l, const vi_rec_leaf_t *lf,
                                    tree_t val_e)
{
   if (!lf->is_array2d)
      return false;

   while (val_e != NULL) {
      if (tree_kind(val_e) == T_REF
          && tree_kind(tree_ref(val_e)) == T_CONST_DECL
          && tree_has_value(tree_ref(val_e)))
         val_e = tree_value(tree_ref(val_e));
      else if (tree_kind(val_e) == T_QUALIFIED
               || tree_kind(val_e) == T_TYPE_CONV)
         val_e = tree_value(val_e);
      else
         break;
   }
   if (val_e == NULL)
      return false;

   mir_unit_t *mu = l->mu;
   const vhdl_isl_sig_t *sis = NULL;
   const vhdl_isig_t *ss = vi_resolve_expr_sig(l, val_e, &sis);
   if (ss != NULL && ss->is_array2d) {
      if (ss->nelems != lf->nelems || ss->width != lf->width)
         return false;
      vi_note_read(l, sis);
      vi_spec_check_held(l, sis);
      vi_spec_all_rows(l, ss, sis, 0, false);
      mir_build_copy(mu, lf->storage, vi_read_ptr(l, sis),
                     vi_off(mu, vi_array2d_bytes(l, lf->nelems, lf->width)));
      return true;
   }

   vi_rec_leaf_t slf;
   if (vi_resolve_local_leaf(l, val_e, &slf) && slf.is_array2d) {
      if (slf.nelems != lf->nelems || slf.width != lf->width)
         return false;
      if (!mir_equals(slf.storage, lf->storage))
         mir_build_copy(mu, lf->storage, slf.storage,
                        vi_off(mu, vi_array2d_bytes(l, lf->nelems, lf->width)));
      return true;
   }

   if (tree_kind(val_e) != T_AGGREGATE)
      return false;

   const int na = tree_assocs(val_e);
   bool *filled LOCAL = xcalloc_array(lf->nelems, sizeof(bool));
   bool has_others = false;

   for (int i = 0; i < na; i++) {
      tree_t a = tree_assoc(val_e, i);
      if (tree_subkind(a) == A_OTHERS) {
         vi_val_t ov;
         if (!vi_lower_expr(l, tree_value(a), &ov)
             || ov.kind != VIVAL_VEC || ov.width != lf->width)
            return false;
         mir_value_t idx_v = mir_add_var(mu, mir_offset_type(mu),
                                         MIR_NULL_STAMP, ident_uniq("l2r"),
                                         MIR_VAR_TEMP);
         mir_build_store(mu, idx_v, vi_off(mu, 0));
         mir_block_t hdr = mir_add_block(mu);
         mir_block_t body = mir_add_block(mu);
         mir_block_t done = mir_add_block(mu);
         mir_build_jump(mu, hdr);

         mir_set_cursor(mu, hdr, MIR_APPEND);
         mir_value_t r = mir_build_load(mu, idx_v);
         mir_build_cond(mu, mir_build_cmp(mu, MIR_CMP_LT, r,
                                          vi_off(mu, lf->nelems)),
                        body, done);

         mir_set_cursor(mu, body, MIR_APPEND);
         if (!vi_store_rec_leaf(l, lf, r, ov))
            return false;
         mir_build_store(mu, idx_v, mir_build_add(mu, mir_offset_type(mu),
                                                  r, vi_off(mu, 1)));
         mir_build_jump(mu, hdr);

         mir_set_cursor(mu, done, MIR_APPEND);
         has_others = true;
         break;
      }
   }

   for (int i = 0; i < na; i++) {
      tree_t a = tree_assoc(val_e, i);
      if (tree_subkind(a) == A_OTHERS)
         continue;
      vi_val_t ev;
      if (!vi_lower_expr(l, tree_value(a), &ev)
          || ev.kind != VIVAL_VEC || ev.width != lf->width)
         return false;
      switch (tree_subkind(a)) {
      case A_POS:
         if ((uint32_t)i >= lf->nelems
             || !vi_store_rec_leaf(l, lf, vi_off(mu, i), ev))
            return false;
         filled[i] = true;
         break;
      case A_NAMED:
         {
            int64_t k = 0;
            if (!vi_eval_const_int(l, tree_name(a), &k))
               return false;
            int64_t pos = (lf->outer_dir == RANGE_TO)
               ? (k - lf->outer_left) : (lf->outer_left - k);
            if (pos < 0 || pos >= (int64_t)lf->nelems
                || !vi_store_rec_leaf(l, lf, vi_off(mu, pos), ev))
               return false;
            filled[pos] = true;
         }
         break;
      case A_RANGE:
         {
            tree_t rg = tree_range(a, 0);
            int64_t rl = 0, rr = 0;
            if (!vi_eval_const_int(l, tree_left(rg), &rl)
                || !vi_eval_const_int(l, tree_right(rg), &rr))
               return false;
            int64_t p0 = (lf->outer_dir == RANGE_TO)
               ? (rl - lf->outer_left) : (lf->outer_left - rl);
            int64_t p1 = (lf->outer_dir == RANGE_TO)
               ? (rr - lf->outer_left) : (lf->outer_left - rr);
            if (p0 > p1) { int64_t tmp = p0; p0 = p1; p1 = tmp; }
            if (p0 < 0 || p1 >= (int64_t)lf->nelems)
               return false;
            for (int64_t p = p0; p <= p1; p++) {
               if (!vi_store_rec_leaf(l, lf, vi_off(mu, p), ev))
                  return false;
               filled[p] = true;
            }
         }
         break;
      default:
         return false;
      }
   }

   if (!has_others) {
      for (uint32_t p = 0; p < lf->nelems; p++) {
         if (!filled[p])
            return false;
      }
   }
   return true;
}

static bool vi_copy_rec_local(vi_lower_t *l, const vi_local_t *dloc,
                              const int *dpath, uint8_t dlen,
                              const vi_local_t *sloc, const int *spath,
                              uint8_t slen)
{
   mir_unit_t *mu = l->mu;
   const vi_rec_leaf_t *d_leaves[512];
   const vi_rec_leaf_t *s_leaves[512];
   uint32_t dn = vi_collect_rec_prefix_leaves(dloc, dpath, dlen,
                                              d_leaves, 512);
   uint32_t sn = vi_collect_rec_prefix_leaves(sloc, spath, slen,
                                              s_leaves, 512);
   if (dn == 0 || dn > 512 || sn != dn)
      return false;
   for (uint32_t i = 0; i < dn; i++) {
      const vi_rec_leaf_t *dlf = d_leaves[i];
      const vi_rec_leaf_t *slf = s_leaves[i];
      if (dlf->width != slf->width || dlf->is_array2d != slf->is_array2d)
         return false;
      if (dlf->is_array2d) {
         if (dlf->nelems != slf->nelems)
            return false;
         mir_build_copy(mu, dlf->storage, slf->storage,
                        vi_off(mu, vi_array2d_bytes(l, dlf->nelems,
                                                    dlf->width)));
         continue;
      }
      vi_val_t v;
      if (!vi_load_rec_leaf(l, slf, vi_off(mu, 0), &v)
          || !vi_store_rec_leaf(l, dlf, vi_off(mu, 0), v))
         return false;
   }
   return true;
}

// RHS_E, a call of a user function that returns a record: inlines it with
// the result in *TMP, a new record local whose leaves the caller frees.
// The result has storage of its own: the function may read the variable
// or signal the caller assigns it to, through its parameters.
static bool vi_call_rec_func(vi_lower_t *l, tree_t rhs_e, vi_local_t *tmp)
{
   if (tree_kind(rhs_e) != T_FCALL || !tree_has_ref(rhs_e))
      return false;
   tree_t decl = tree_ref(rhs_e);
   type_t rt = tree_type(rhs_e);
   if (tree_subkind(decl) != S_USER || !type_is_record(rt))
      return false;
   vi_rec_leaf_list_t leaves = AINIT;
   int rpath[VHDL_ISIG_MAX_DEPTH];
   if (!vi_collect_rec_local_leaves(l, rt, rpath, 0, tree_ident(decl),
                                    &leaves)) {
      ACLEAR(leaves);
      return false;
   }
   *tmp = (vi_local_t){
      .decl         = decl,
      .is_rec       = true,
      .rec_type     = rt,
      .rec_leaves   = leaves.items,
      .n_rec_leaves = leaves.count,
   };
   vi_val_t unused;
   if (!vi_lower_inline_func(l, rhs_e, decl, &unused, tmp)) {
      free(tmp->rec_leaves);
      return false;
   }
   return true;
}

static bool vi_assign_rec_local(vi_lower_t *l, vi_local_t *dloc,
                                int *dpath, uint8_t dlen,
                                type_t rec_type, tree_t rhs_e)
{
   while (rhs_e != NULL) {
      if (tree_kind(rhs_e) == T_REF
          && tree_kind(tree_ref(rhs_e)) == T_CONST_DECL
          && tree_has_value(tree_ref(rhs_e)))
         rhs_e = tree_value(tree_ref(rhs_e));
      else if (tree_kind(rhs_e) == T_QUALIFIED
               || tree_kind(rhs_e) == T_TYPE_CONV)
         rhs_e = tree_value(rhs_e);
      else
         break;
   }
   if (rhs_e == NULL)
      return false;

   mir_unit_t *mu = l->mu;

   if (type_is_record(rec_type)) {
      if (tree_kind(rhs_e) == T_COND_EXPR) {
         mir_block_t exit_bb = mir_add_block(mu);
         const int nc = tree_conds(rhs_e);
         for (int i = 0; i < nc; i++) {
            tree_t br = tree_cond(rhs_e, i);
            mir_block_t next_bb = (i + 1 < nc) ? mir_add_block(mu) : exit_bb;
            if (tree_has_value(br)) {
               vi_val_t cv;
               if (!vi_lower_expr(l, tree_value(br), &cv))
                  return false;
               mir_block_t tbb = mir_add_block(mu);
               mir_build_cond(mu, vi_to_bool(l, cv), tbb, next_bb);
               mir_set_cursor(mu, tbb, MIR_APPEND);
            }
            if (!vi_assign_rec_local(l, dloc, dpath, dlen, rec_type,
                                     tree_value(tree_stmt(br, 0))))
               return false;
            mir_build_jump(mu, exit_bb);
            if (i + 1 < nc)
               mir_set_cursor(mu, next_bb, MIR_APPEND);
         }
         mir_set_cursor(mu, exit_bb, MIR_APPEND);
         return true;
      }

      // Copy from record signal: v := r or v.sub := r.sub
      if (vhdl_island_is_sig_root(rhs_e)) {
         const vi_rec_leaf_t *d_leaves[512];
         uint32_t dn = vi_collect_rec_prefix_leaves(dloc, dpath, dlen,
                                                    d_leaves, 512);
         if (dn == 0 || dn > 512)
            return false;
         uint32_t *s_ids = NULL;
         uint32_t sn = 0;
         if (!vhdl_island_collect_expr_leaves(l->ctx, rhs_e, &s_ids, &sn)
             || sn != dn) {
            free(s_ids);
            return false;
         }
         for (uint32_t i = 0; i < dn; i++) {
            const vi_rec_leaf_t *dlf = d_leaves[i];
            const vhdl_isig_t *ss = &(l->ctx->sigs[s_ids[i]]);
            const vhdl_isl_sig_t *sis = vhdl_island_find_sig(l->isl, s_ids[i]);
            if (sis == NULL || dlf->width != ss->width
                || dlf->is_array2d != ss->is_array2d) {
               free(s_ids);
               return false;
            }
            if (dlf->is_array2d) {
               if (dlf->nelems != ss->nelems) {
                  free(s_ids);
                  return false;
               }
               vi_note_read(l, sis);
               vi_spec_check_held(l, sis);
               vi_spec_all_rows(l, ss, sis, 0, false);
               mir_build_copy(mu, dlf->storage, vi_read_ptr(l, sis),
                              vi_off(mu, vi_array2d_bytes(l, dlf->nelems,
                                                          dlf->width)));
               continue;
            }
            vi_val_t v;
            if (!vi_lower_sig_load(l, ss, sis, vi_off(mu, 0), &v)
                || !vi_store_rec_leaf(l, dlf, vi_off(mu, 0), v)) {
               free(s_ids);
               return false;
            }
         }
         free(s_ids);
         return true;
      }

      // Copy from another record local variable: v2 := v1 or v2.sub := v1.sub
      vi_local_t *sloc = NULL;
      int spath[VHDL_ISIG_MAX_DEPTH];
      uint8_t slen = 0;
      if (vi_extract_local_rec_path(l, rhs_e, &sloc, spath, &slen)) {
         if (sloc == dloc && slen == dlen
             && (dlen == 0 || memcmp(spath, dpath, dlen * sizeof(int)) == 0))
            return true;
         return vi_copy_rec_local(l, dloc, dpath, dlen, sloc, spath, slen);
      }

      // Call of a function returning a record: v := f(x)
      if (tree_kind(rhs_e) == T_FCALL) {
         vi_local_t tmp;
         if (!vi_call_rec_func(l, rhs_e, &tmp))
            return false;
         const bool ok = vi_copy_rec_local(l, dloc, dpath, dlen, &tmp,
                                           NULL, 0);
         free(tmp.rec_leaves);
         return ok;
      }

      // Record aggregate / constant record initializer
      if (dlen >= VHDL_ISIG_MAX_DEPTH || tree_kind(rhs_e) != T_AGGREGATE)
         return false;
      type_t base = type_base_recur(rec_type);
      const int nfields = type_fields(base);
      const int na = tree_assocs(rhs_e);
      tree_t others_val = NULL;
      for (int i = 0; i < na; i++) {
         tree_t a = tree_assoc(rhs_e, i);
         if (tree_subkind(a) == A_OTHERS)
            others_val = tree_value(a);
      }
      for (int f = 0; f < nfields; f++) {
         tree_t fdecl = type_field(base, f);
         tree_t fval = NULL;
         for (int i = 0; i < na; i++) {
            tree_t a = tree_assoc(rhs_e, i);
            switch (tree_subkind(a)) {
            case A_POS:
               if (i == f)
                  fval = tree_value(a);
               break;
            case A_NAMED:
               if (tree_kind(tree_name(a)) == T_REF
                   && tree_ref(tree_name(a)) == fdecl)
                  fval = tree_value(a);
               break;
            default:
               break;
            }
         }
         if (fval == NULL)
            fval = others_val;
         if (fval == NULL)
            return false;
         dpath[dlen] = f;
         if (!vi_assign_rec_local(l, dloc, dpath, (uint8_t)(dlen + 1),
                                  tree_type(fdecl), fval))
            return false;
      }
      return true;
   }

   // Array of records field inside a record aggregate
   if (type_is_array(rec_type) && dimension_of(rec_type) == 1
       && type_is_record(type_elem(rec_type))) {
      if (dlen >= VHDL_ISIG_MAX_DEPTH || tree_kind(rhs_e) != T_AGGREGATE)
         return false;
      range_kind_t adir;
      int64_t aleft, aright;
      uint32_t nelems;
      if (!vi_eval_type_bounds(l, rec_type, &adir, &aleft, &aright, &nelems)
          || nelems == 0 || nelems > 64)
         return false;
      type_t elem = type_elem(rec_type);
      const int na = tree_assocs(rhs_e);
      tree_t others_val = NULL;
      for (int i = 0; i < na; i++) {
         if (tree_subkind(tree_assoc(rhs_e, i)) == A_OTHERS)
            others_val = tree_value(tree_assoc(rhs_e, i));
      }
      for (uint32_t e = 0; e < nelems; e++) {
         int64_t idx = (adir == RANGE_TO) ? (aleft + (int64_t)e) : (aleft - (int64_t)e);
         tree_t eval = NULL;
         for (int i = 0; i < na; i++) {
            tree_t a = tree_assoc(rhs_e, i);
            switch (tree_subkind(a)) {
            case A_POS:
               if ((uint32_t)i == e)
                  eval = tree_value(a);
               break;
            case A_NAMED:
               {
                  int64_t k = 0;
                  if (vi_eval_const_int(l, tree_name(a), &k) && k == idx)
                     eval = tree_value(a);
               }
               break;
            case A_RANGE:
               {
                  tree_t rg = tree_range(a, 0);
                  int64_t rl = 0, rr = 0;
                  if (vi_eval_const_int(l, tree_left(rg), &rl)
                      && vi_eval_const_int(l, tree_right(rg), &rr)) {
                     int64_t lo = MIN(rl, rr), hi = MAX(rl, rr);
                     if (idx >= lo && idx <= hi)
                        eval = tree_value(a);
                  }
               }
               break;
            default:
               break;
            }
         }
         if (eval == NULL)
            eval = others_val;
         if (eval == NULL)
            return false;
         dpath[dlen] = -(int)(e + 1);
         if (!vi_assign_rec_local(l, dloc, dpath, (uint8_t)(dlen + 1),
                                  elem, eval))
            return false;
      }
      return true;
   }

   const vi_rec_leaf_t *lf = vi_find_rec_leaf(dloc, dpath, dlen);
   if (lf == NULL)
      return false;
   if (lf->is_array2d)
      return vi_assign_array2d_local(l, lf, rhs_e);

   vi_val_t rv;
   if (!vi_lower_expr(l, rhs_e, &rv))
      return false;
   return vi_store_rec_leaf(l, lf, vi_off(mu, 0), rv);
}

static bool vi_copy_rec_local_to_sig(vi_lower_t *l,
                                     const uint32_t *d_ids, uint32_t dn,
                                     const vi_local_t *sloc,
                                     const int *spath, uint8_t slen)
{
   const vi_rec_leaf_t *s_leaves[512];
   uint32_t sn = vi_collect_rec_prefix_leaves(sloc, spath, slen, s_leaves, 512);
   if (sn == 0 || sn > 512 || sn != dn)
      return false;

   mir_unit_t *mu = l->mu;
   for (uint32_t i = 0; i < dn; i++) {
      const vhdl_isig_t *ds = &(l->ctx->sigs[d_ids[i]]);
      const vhdl_isl_sig_t *dis = vhdl_island_find_sig(l->isl, d_ids[i]);
      const vi_rec_leaf_t *slf = s_leaves[i];
      if (dis == NULL || ds->width != slf->width
          || ds->is_array2d != slf->is_array2d)
         return false;
      if (ds->is_array2d) {
         if (ds->nelems != slf->nelems)
            return false;
         const uint32_t dst_off = dis->a_off + (l->is_ff ? l->ff_stride : 0u);
         vi_mark_ff_dirty(l, dis);
         vi_spec_check_held(l, dis);
         vi_spec_all_rows(l, ds, dis, l->is_ff ? l->ff_stride : 0u, true);
         mir_build_copy(mu, vi_state_ptr_at(l, dst_off), slf->storage,
                        vi_off(mu, vi_array2d_bytes(l, ds->nelems, ds->width)));
         continue;
      }
      vi_val_t v;
      if (!vi_load_rec_leaf(l, slf, vi_off(mu, 0), &v)
          || !vi_assign_leaf_sig(l, ds, dis, vi_off(mu, 0), 0, ds->width,
                                 true, v))
         return false;
   }
   return true;
}

static bool vi_assign_record_expr(vi_lower_t *l, tree_t target_decl,
                                  int *path, uint8_t depth, type_t rec_type,
                                  tree_t rhs_e)
{
   while (rhs_e != NULL) {
      if (tree_kind(rhs_e) == T_REF
          && tree_kind(tree_ref(rhs_e)) == T_CONST_DECL
          && tree_has_value(tree_ref(rhs_e)))
         rhs_e = tree_value(tree_ref(rhs_e));
      else if (tree_kind(rhs_e) == T_QUALIFIED
               || tree_kind(rhs_e) == T_TYPE_CONV)
         rhs_e = tree_value(rhs_e);
      else
         break;
   }
   if (rhs_e == NULL)
      return false;

   if (type_is_record(rec_type)) {
      vi_local_t *sloc = NULL;
      int spath[VHDL_ISIG_MAX_DEPTH];
      uint8_t slen = 0;
      if (vi_extract_local_rec_path(l, rhs_e, &sloc, spath, &slen)) {
         A(uint32_t) d_ids = AINIT;
         for (uint32_t i = 0; i < l->ctx->n_sigs; i++) {
            const vhdl_isig_t *s = &(l->ctx->sigs[i]);
            if (s->decl == target_decl && s->path_len >= depth
                && (depth == 0 || memcmp(s->field_path, path,
                                         depth * sizeof(int)) == 0))
               APUSH(d_ids, i);
         }
         bool ok = vi_copy_rec_local_to_sig(l, d_ids.items, d_ids.count,
                                            sloc, spath, slen);
         ACLEAR(d_ids);
         return ok;
      }
      if (tree_kind(rhs_e) == T_FCALL) {
         vi_local_t tmp;
         if (!vi_call_rec_func(l, rhs_e, &tmp))
            return false;
         A(uint32_t) d_ids = AINIT;
         for (uint32_t i = 0; i < l->ctx->n_sigs; i++) {
            const vhdl_isig_t *s = &(l->ctx->sigs[i]);
            if (s->decl == target_decl && s->path_len >= depth
                && (depth == 0 || memcmp(s->field_path, path,
                                         depth * sizeof(int)) == 0))
               APUSH(d_ids, i);
         }
         bool ok = vi_copy_rec_local_to_sig(l, d_ids.items, d_ids.count,
                                            &tmp, NULL, 0);
         ACLEAR(d_ids);
         free(tmp.rec_leaves);
         return ok;
      }
      if (vhdl_island_is_sig_root(rhs_e)) {
         A(uint32_t) d_ids = AINIT;
         for (uint32_t i = 0; i < l->ctx->n_sigs; i++) {
            const vhdl_isig_t *s = &(l->ctx->sigs[i]);
            if (s->decl == target_decl && s->path_len >= depth
                && (depth == 0 || memcmp(s->field_path, path,
                                         depth * sizeof(int)) == 0))
               APUSH(d_ids, i);
         }
         uint32_t *s_ids = NULL;
         uint32_t sn = 0;
         if (!vhdl_island_collect_expr_leaves(l->ctx, rhs_e, &s_ids, &sn)
             || sn != d_ids.count) {
            ACLEAR(d_ids);
            free(s_ids);
            return false;
         }
         bool ok = true;
         for (uint32_t i = 0; i < sn && ok; i++) {
            const vhdl_isig_t *ds = &(l->ctx->sigs[d_ids.items[i]]);
            const vhdl_isig_t *ss = &(l->ctx->sigs[s_ids[i]]);
            const vhdl_isl_sig_t *dis = vhdl_island_find_sig(l->isl, d_ids.items[i]);
            const vhdl_isl_sig_t *sis = vhdl_island_find_sig(l->isl, s_ids[i]);
            if (dis == NULL || sis == NULL
                || ds->width != ss->width || ds->is_array2d != ss->is_array2d)
               ok = false;
            else if (ds->is_array2d)
               ok = vi_copy_array2d_sig(l, ds, dis, ss, sis);
            else {
               vi_val_t v;
               ok = vi_lower_sig_load(l, ss, sis, vi_off(l->mu, 0), &v)
                  && vi_assign_leaf_sig(l, ds, dis, vi_off(l->mu, 0), 0,
                                        ds->width, true, v);
            }
         }
         ACLEAR(d_ids);
         free(s_ids);
         return ok;
      }
      if (depth >= VHDL_ISIG_MAX_DEPTH || tree_kind(rhs_e) != T_AGGREGATE)
         return false;
      type_t base = type_base_recur(rec_type);
      const int nfields = type_fields(base);
      const int na = tree_assocs(rhs_e);
      tree_t others_val = NULL;
      for (int i = 0; i < na; i++) {
         tree_t a = tree_assoc(rhs_e, i);
         if (tree_subkind(a) == A_OTHERS)
            others_val = tree_value(a);
      }
      for (int f = 0; f < nfields; f++) {
         tree_t fdecl = type_field(base, f);
         tree_t fval = NULL;
         for (int i = 0; i < na; i++) {
            tree_t a = tree_assoc(rhs_e, i);
            switch (tree_subkind(a)) {
            case A_POS:
               if (i == f)
                  fval = tree_value(a);
               break;
            case A_NAMED:
               if (tree_kind(tree_name(a)) == T_REF
                   && tree_ref(tree_name(a)) == fdecl)
                  fval = tree_value(a);
               break;
            default:
               break;
            }
         }
         if (fval == NULL)
            fval = others_val;
         if (fval == NULL)
            return false;
         path[depth] = f;
         if (!vi_assign_record_expr(l, target_decl, path, (uint8_t)(depth + 1),
                                    tree_type(fdecl), fval))
            return false;
      }
      return true;
   }

   int gid = vhdl_island_lookup_leaf(l->ctx, target_decl, path, depth);
   if (gid < 0)
      return false;
   const vhdl_isig_t *ds = &(l->ctx->sigs[gid]);
   const vhdl_isl_sig_t *dis = vhdl_island_find_sig(l->isl, (uint32_t)gid);
   if (dis == NULL)
      return false;
   if (ds->is_array2d)
      return vi_assign_array2d_sig(l, ds, dis, rhs_e);

   vi_val_t rv;
   if (!vi_lower_expr(l, rhs_e, &rv))
      return false;
   if (ds->is_vector && (rv.kind != VIVAL_VEC || rv.width != ds->width))
      return false;
   if (!ds->is_vector && ds->elem_kind != VISIG_ENUM
       && ds->elem_kind != VISIG_INT && rv.kind != VIVAL_BIT)
      return false;
   if ((ds->elem_kind == VISIG_ENUM || ds->elem_kind == VISIG_INT)
       && rv.kind != VIVAL_INT)
      return false;
   return vi_assign_leaf_sig(l, ds, dis, vi_off(l->mu, 0), 0, ds->width,
                             true, rv);
}

static bool vi_lower_sig_assign(vi_lower_t *l, tree_t stmt)
{
   tree_t target = tree_target(stmt);
   tree_t val_e = tree_value(tree_waveform(stmt, 0));
   mir_unit_t *mu = l->mu;

   // Whole/partial record signal assignment: r_dst <= r_src or const/aggregate
   if (type_is_record(tree_type(target))) {
      uint32_t *d_ids = NULL;
      uint32_t dn = 0;
      if (!vhdl_island_collect_expr_leaves(l->ctx, target, &d_ids, &dn)) {
         free(d_ids);
         return false;
      }
      if (vhdl_island_is_sig_root(val_e)) {
         uint32_t *s_ids = NULL;
         uint32_t sn = 0;
         if (!vhdl_island_collect_expr_leaves(l->ctx, val_e, &s_ids, &sn)
             || sn != dn) {
            free(d_ids);
            free(s_ids);
            return false;
         }
         for (uint32_t i = 0; i < dn; i++) {
            const vhdl_isig_t *ds = &(l->ctx->sigs[d_ids[i]]);
            const vhdl_isig_t *ss = &(l->ctx->sigs[s_ids[i]]);
            const vhdl_isl_sig_t *dis = vhdl_island_find_sig(l->isl, d_ids[i]);
            const vhdl_isl_sig_t *sis = vhdl_island_find_sig(l->isl, s_ids[i]);
            if (dis == NULL || sis == NULL
                || ds->width != ss->width || ds->is_array2d != ss->is_array2d) {
               free(d_ids);
               free(s_ids);
               return false;
            }
            if (ds->is_array2d) {
               if (!vi_copy_array2d_sig(l, ds, dis, ss, sis)) {
                  free(d_ids);
                  free(s_ids);
                  return false;
               }
               continue;
            }
            vi_val_t v;
            if (!vi_lower_sig_load(l, ss, sis, vi_off(mu, 0), &v)
                || !vi_assign_leaf_sig(l, ds, dis, vi_off(mu, 0), 0, ds->width,
                                       true, v)) {
               free(d_ids);
               free(s_ids);
               return false;
            }
         }
         free(d_ids);
         free(s_ids);
         return true;
      }
      {
         vi_local_t *sloc = NULL;
         int spath[VHDL_ISIG_MAX_DEPTH];
         uint8_t slen = 0;
         if (vi_extract_local_rec_path(l, val_e, &sloc, spath, &slen)) {
            bool ok = vi_copy_rec_local_to_sig(l, d_ids, dn, sloc, spath, slen);
            free(d_ids);
            return ok;
         }
      }
      free(d_ids);
      tree_t tdecl = NULL;
      int tpath[VHDL_ISIG_MAX_DEPTH];
      uint8_t tlen = 0;
      if (vhdl_island_extract_path_ctx(l->ctx, target, &tdecl, tpath, &tlen))
         return vi_assign_record_expr(l, tdecl, tpath, tlen, tree_type(target),
                                      val_e);
      return false;
   }

   {
      const vhdl_isl_sig_t *t_isig = NULL;
      const vhdl_isig_t *t_sig = vi_resolve_expr_sig(l, target, &t_isig);
      if (t_sig != NULL && t_sig->is_array2d)
         return vi_assign_array2d_sig(l, t_sig, t_isig, val_e);
   }

   vi_val_t rv;
   if (!vi_lower_expr(l, val_e, &rv))
      return false;

   if (tree_kind(target) == T_ARRAY_SLICE) {
      tree_t rg = tree_range(target, 0);
      range_kind_t sdir = tree_subkind(rg);
      if (sdir != RANGE_TO && sdir != RANGE_DOWNTO)
         return false;
      int64_t sl = 0, sr = 0;
      const bool const_bounds =
         vi_eval_const_int(l, tree_left(rg), &sl)
         && vi_eval_const_int(l, tree_right(rg), &sr);
      uint32_t sw = 0;
      if (const_bounds) {
         int64_t sw64 = (sdir == RANGE_TO) ? (sr - sl + 1) : (sl - sr + 1);
         if (sw64 <= 0 || sw64 > VHDL_ISLAND_MAX_WIDTH)
            return false;
         sw = (uint32_t)sw64;
      }
      else if (!vhdl_island_const_slice_width(l->ctx, rg, &sw))
         return false;

      tree_t base = tree_value(target);
      const vhdl_isl_sig_t *isig = NULL;
      const vhdl_isig_t *s = vi_resolve_expr_sig(l, base, &isig);
      mir_value_t elem_idx = vi_off(mu, 0);
      if (s == NULL && tree_kind(base) == T_ARRAY_REF && tree_params(base) == 1) {
         vi_val_t eiv;
         if (!vi_lower_expr(l, tree_value(tree_param(base, 0)), &eiv)
             || eiv.kind != VIVAL_INT)
            return false;
         s = vi_resolve_expr_sig(l, tree_value(base), &isig);
         if (s == NULL || !s->is_array2d)
            return false;
         vi_val_t fake_outer = {
            .dir = s->outer_dir, .left = s->outer_left, .right = s->outer_right,
         };
         elem_idx = vi_compute_elem_pos(l, fake_outer, eiv.val, s->nelems);
      }
      else if (s == NULL || s->is_array2d)
         return false;
      if (sdir != s->dir || rv.kind != VIVAL_VEC || sw != rv.width || sw > s->width)
         return false;
      if (const_bounds) {
         int64_t pos = (s->dir == RANGE_TO) ? (sl - s->left) : (s->left - sl);
         if (pos < 0 || pos + (int64_t)sw > s->width)
            return false;
         return vi_assign_leaf_sig(l, s, isig, elem_idx, pos, sw, false, rv);
      }
      vi_val_t lv;
      if (!vi_lower_expr(l, tree_left(rg), &lv) || lv.kind != VIVAL_INT)
         return false;
      vi_val_t fake = { .dir = s->dir, .left = s->left, .right = s->right };
      mir_value_t pos = vi_compute_elem_pos(l, fake, lv.val, s->width - sw + 1u);
      return vi_assign_leaf_sig_dyn_slice(l, s, isig, elem_idx, pos, sw, rv);
   }

   if (tree_kind(target) == T_ARRAY_REF) {
      if (tree_params(target) != 1)
         return false;
      vi_val_t iv;
      if (!vi_lower_expr(l, tree_value(tree_param(target, 0)), &iv)
          || iv.kind != VIVAL_INT)
         return false;
      tree_t base = tree_value(target);
      const vhdl_isl_sig_t *isig = NULL;
      const vhdl_isig_t *s = vi_resolve_expr_sig(l, base, &isig);
      if (s == NULL && tree_kind(base) == T_ARRAY_REF && tree_params(base) == 1) {
         vi_val_t eiv;
         if (!vi_lower_expr(l, tree_value(tree_param(base, 0)), &eiv)
             || eiv.kind != VIVAL_INT)
            return false;
         s = vi_resolve_expr_sig(l, tree_value(base), &isig);
         if (s == NULL || !s->is_array2d || rv.kind != VIVAL_BIT)
            return false;
         vi_val_t fake_outer = {
            .dir = s->outer_dir, .left = s->outer_left, .right = s->outer_right,
         };
         mir_value_t elem_idx = vi_compute_elem_pos(l, fake_outer, eiv.val, s->nelems);
         vi_val_t fake = { .dir = s->dir, .left = s->left, .right = s->right };
         mir_value_t pos = vi_compute_elem_pos(l, fake, iv.val, s->width);
         return vi_assign_leaf_sig_dyn_pos(l, s, isig, elem_idx, pos, rv);
      }
      if (s == NULL)
         return false;
      if (s->is_array2d) {
         if (rv.kind != VIVAL_VEC || rv.width != s->width)
            return false;
         vi_val_t fake_outer = {
            .dir = s->outer_dir, .left = s->outer_left, .right = s->outer_right,
         };
         mir_value_t elem_idx = vi_compute_elem_pos(l, fake_outer, iv.val, s->nelems);
         return vi_assign_leaf_sig(l, s, isig, elem_idx, 0, s->width, true, rv);
      }
      if (!s->is_vector || rv.kind != VIVAL_BIT)
         return false;
      vi_val_t fake = { .dir = s->dir, .left = s->left, .right = s->right };
      mir_value_t pos = vi_compute_elem_pos(l, fake, iv.val, s->width);
      return vi_assign_leaf_sig_dyn_pos(l, s, isig, vi_off(mu, 0), pos, rv);
   }

   const vhdl_isl_sig_t *isig = NULL;
   const vhdl_isig_t *s = vi_resolve_expr_sig(l, target, &isig);
   if (s == NULL || s->is_array2d)
      return false;
   if (s->is_vector && (rv.kind != VIVAL_VEC || rv.width != s->width))
      return false;
   if ((s->elem_kind == VISIG_ENUM || s->elem_kind == VISIG_INT)
       && rv.kind != VIVAL_INT)
      return false;
   if (!s->is_vector && s->elem_kind != VISIG_ENUM && s->elem_kind != VISIG_INT
       && rv.kind != VIVAL_BIT)
      return false;
   return vi_assign_leaf_sig(l, s, isig, vi_off(mu, 0), 0, s->width, true, rv);
}

static bool vi_lower_var_assign(vi_lower_t *l, tree_t stmt)
{
   mir_unit_t *mu = l->mu;
   tree_t target = tree_target(stmt);
   tree_t val_e = tree_value(stmt);

   // Whole/sub-record local variable assignment: v := r / v := v2 / v := agg
   if (type_is_record(tree_type(target))) {
      vi_local_t *dloc = NULL;
      int dpath[VHDL_ISIG_MAX_DEPTH];
      uint8_t dlen = 0;
      if (!vi_extract_local_rec_path(l, target, &dloc, dpath, &dlen))
         return false;
      return vi_assign_rec_local(l, dloc, dpath, dlen, tree_type(target), val_e);
   }

   // Whole 2D local array / record 2D-array field assignment
   {
      vi_rec_leaf_t tlf;
      if (vi_resolve_local_leaf(l, target, &tlf) && tlf.is_array2d)
         return vi_assign_array2d_local(l, &tlf, val_e);
   }

   vi_val_t rv;
   if (!vi_lower_expr(l, val_e, &rv))
      return false;

   if (tree_kind(target) == T_ARRAY_SLICE) {
      tree_t rg = tree_range(target, 0);
      range_kind_t sdir = tree_subkind(rg);
      if (sdir != RANGE_TO && sdir != RANGE_DOWNTO)
         return false;
      int64_t sl = 0, sr = 0;
      const bool const_bounds =
         vi_eval_const_int(l, tree_left(rg), &sl)
         && vi_eval_const_int(l, tree_right(rg), &sr);
      tree_t base = tree_value(target);
      vi_rec_leaf_t lf;
      mir_value_t elem_off = vi_off(mu, 0);
      mir_value_t row_idx = vi_off(mu, 0);
      bool is_2d_row = false;
      if (vi_resolve_local_leaf(l, base, &lf)) {
         if (!lf.is_vec || lf.is_array2d)
            return false;
      }
      else if (tree_kind(base) == T_ARRAY_REF && tree_params(base) == 1
               && vi_resolve_local_leaf(l, tree_value(base), &lf)
               && lf.is_array2d) {
         vi_val_t eiv;
         if (!vi_lower_expr(l, tree_value(tree_param(base, 0)), &eiv)
             || eiv.kind != VIVAL_INT)
            return false;
         vi_val_t fake_outer = {
            .dir = lf.outer_dir, .left = lf.outer_left, .right = lf.outer_right,
         };
         mir_value_t eidx = vi_compute_elem_pos(l, fake_outer, eiv.val, lf.nelems);
         elem_off = mir_build_mul(mu, mir_offset_type(mu),
                                  eidx, vi_off(mu, lf.width));
         row_idx = eidx;
         is_2d_row = true;
      }
      else
         return false;

      uint32_t sw = 0;
      if (const_bounds) {
         int64_t sw64 = (sdir == RANGE_TO) ? (sr - sl + 1) : (sl - sr + 1);
         if (sw64 <= 0 || sw64 > VHDL_ISLAND_MAX_WIDTH)
            return false;
         sw = (uint32_t)sw64;
      }
      else if (!vhdl_island_const_slice_width(l->ctx, rg, &sw))
         return false;
      if (sdir != lf.dir || rv.kind != VIVAL_VEC || sw != rv.width
          || sw > lf.width)
         return false;
      if (const_bounds) {
         int64_t pos = (lf.dir == RANGE_TO) ? (sl - lf.left) : (lf.left - sl);
         if (pos < 0 || pos + (int64_t)sw > lf.width)
            return false;
         if (l->two_state && is_2d_row) {
            mir_value_t cur = mir_build_island_load(
               mu, mir_vec2_type(mu, (int)lf.width, lf.is_signed), lf.storage,
               vi_off(mu, 0), vi_off(mu, -1), row_idx);
            mir_value_t upd = mir_build_insert(mu, rv.val, cur,
                                               vi_off(mu, pos));
            mir_build_island_store(mu, lf.storage, vi_off(mu, 0),
                                   vi_off(mu, -1), row_idx, upd);
         }
         else if (l->two_state) {
            mir_value_t cur = mir_build_load(mu, lf.storage);
            mir_value_t upd = mir_build_insert(mu, rv.val, cur, vi_off(mu, pos));
            mir_build_store(mu, lf.storage, upd);
         }
         else {
            mir_value_t off = is_2d_row
               ? mir_build_add(mu, mir_offset_type(mu), elem_off, vi_off(mu, pos))
               : vi_off(mu, pos);
            mir_build_copy(mu, mir_build_array_ref(mu, lf.storage, off),
                           rv.val, vi_off(mu, sw));
         }
         return true;
      }
      vi_val_t lv;
      if (!vi_lower_expr(l, tree_left(rg), &lv) || lv.kind != VIVAL_INT)
         return false;
      vi_val_t fake = { .dir = lf.dir, .left = lf.left, .right = lf.right };
      mir_value_t pos = vi_compute_elem_pos(l, fake, lv.val, lf.width - sw + 1u);
      if (l->two_state) {
         mir_value_t part = rv.is_signed
            ? mir_build_cast(mu, mir_vec2_type(mu, (int)sw, false), rv.val)
            : rv.val;
         if (is_2d_row) {
            mir_value_t cur = mir_build_island_load(
               mu, mir_vec2_type(mu, (int)lf.width, lf.is_signed), lf.storage,
               vi_off(mu, 0), vi_off(mu, -1), row_idx);
            mir_build_island_store(mu, lf.storage, vi_off(mu, 0),
                                   vi_off(mu, -1), row_idx,
                                   mir_build_insert(mu, part, cur, pos));
         }
         else {
            mir_value_t cur = mir_build_load(mu, lf.storage);
            mir_build_store(mu, lf.storage,
                            mir_build_insert(mu, part, cur, pos));
         }
         return true;
      }
      mir_value_t off = is_2d_row
         ? mir_build_add(mu, mir_offset_type(mu), elem_off, pos) : pos;
      mir_build_copy(mu, mir_build_array_ref(mu, lf.storage, off),
                     rv.val, vi_off(mu, sw));
      return true;
   }

   if (tree_kind(target) == T_ARRAY_REF) {
      if (tree_params(target) != 1)
         return false;
      vi_val_t iv;
      if (!vi_lower_expr(l, tree_value(tree_param(target, 0)), &iv)
          || iv.kind != VIVAL_INT)
         return false;
      tree_t base = tree_value(target);
      vi_rec_leaf_t lf;
      if (vi_resolve_local_leaf(l, base, &lf)) {
         if (lf.is_array2d) {
            vi_val_t fake_outer = {
               .dir = lf.outer_dir, .left = lf.outer_left, .right = lf.outer_right,
            };
            mir_value_t eidx = vi_compute_elem_pos(l, fake_outer, iv.val, lf.nelems);
            return vi_store_rec_leaf(l, &lf, eidx, rv);
         }
         if (!lf.is_vec || rv.kind != VIVAL_BIT)
            return false;
         vi_val_t fake = { .dir = lf.dir, .left = lf.left, .right = lf.right };
         mir_value_t pos = vi_compute_elem_pos(l, fake, iv.val, lf.width);
         if (l->two_state) {
            mir_value_t cur = mir_build_load(mu, lf.storage);
            mir_value_t upd = mir_build_insert(mu, vi_to_vec2_1(l, rv), cur, pos);
            mir_build_store(mu, lf.storage, upd);
         }
         else
            mir_build_store(mu, mir_build_array_ref(mu, lf.storage, pos),
                            mir_build_cast(mu, vi_u8_type(mu), rv.val));
         return true;
      }
      if (tree_kind(base) == T_ARRAY_REF && tree_params(base) == 1
          && vi_resolve_local_leaf(l, tree_value(base), &lf) && lf.is_array2d
          && rv.kind == VIVAL_BIT) {
         vi_val_t eiv;
         if (!vi_lower_expr(l, tree_value(tree_param(base, 0)), &eiv)
             || eiv.kind != VIVAL_INT)
            return false;
         vi_val_t fake_outer = {
            .dir = lf.outer_dir, .left = lf.outer_left, .right = lf.outer_right,
         };
         mir_value_t eidx = vi_compute_elem_pos(l, fake_outer, eiv.val, lf.nelems);
         vi_val_t fake = { .dir = lf.dir, .left = lf.left, .right = lf.right };
         mir_value_t pos = vi_compute_elem_pos(l, fake, iv.val, lf.width);
         mir_value_t off = mir_build_add(
            mu, mir_offset_type(mu),
            mir_build_mul(mu, mir_offset_type(mu), eidx, vi_off(mu, lf.width)),
            pos);
         if (l->two_state) {
            mir_value_t cur = mir_build_island_load(
               mu, mir_vec2_type(mu, (int)lf.width, lf.is_signed), lf.storage,
               vi_off(mu, 0), vi_off(mu, -1), eidx);
            mir_build_island_store(
               mu, lf.storage, vi_off(mu, 0), vi_off(mu, -1), eidx,
               mir_build_insert(mu, vi_to_vec2_1(l, rv), cur, pos));
            return true;
         }
         mir_build_store(mu, mir_build_array_ref(mu, lf.storage, off),
                         mir_build_cast(mu, vi_u8_type(mu), rv.val));
         return true;
      }
      return false;
   }

   vi_rec_leaf_t lf;
   if (!vi_resolve_local_leaf(l, target, &lf) || lf.is_array2d)
      return false;
   if (tree_kind(target) == T_REF && tree_kind(tree_ref(target)) == T_VAR_DECL
       && type_is_integer(tree_type(tree_ref(target))))
      vhdl_island_var_def_set((vhdl_island_ctx_t *)l->ctx, tree_ref(target),
                              val_e);
   if (tree_kind(target) == T_REF) {
      vi_local_t *loc = vi_find_local(l, tree_ref(target));
      if (loc != NULL && vi_is_const_var(loc)) {
         int64_t cv = 0;
         loc->has_const_int = vi_eval_const_int(l, val_e, &cv)
            || (rv.kind == VIVAL_INT && mir_get_const(mu, rv.val, &cv));
         loc->const_int = cv;
      }
   }
   return vi_store_rec_leaf(l, &lf, vi_off(mu, 0), rv);
}

typedef struct {
   mir_value_t ptr;
   mir_value_t len;
} vi_str_t;

// VAL_E'IMAGE, or TO_STRING(VAL_E) when TO_STRING: like 'IMAGE without the
// apostrophes around a character literal
static bool vi_lower_image(vi_lower_t *l, tree_t val_e, bool to_string,
                           vi_str_t *out)
{
   mir_unit_t *mu = l->mu;
   vi_val_t v;
   if (!vi_lower_expr(l, val_e, &v))
      return false;
   type_t base = type_base_recur(tree_type(val_e));
   ident_t func = ident_prefix(type_ident(base), ident_new("image"), '$');
   mir_value_t arg;
   bool has_chars = false;
   if (type_is_enum(base)) {
      const unsigned nlits = type_enum_literals(base);
      for (unsigned i = 0; i < nlits && !has_chars; i++)
         has_chars = ident_char(tree_ident(type_enum_literal(base, i)), 0)
            == '\'';
      mir_type_t at = mir_int_type(mu, 0, (int64_t)nlits - 1);
      well_known_t wk = is_well_known(type_ident(base));
      if (l->two_state && (wk == W_IEEE_ULOGIC || wk == W_IEEE_LOGIC)) {
         mir_value_t b = vi_to_bool(l, v);
         arg = mir_build_select(mu, at, b, mir_const(mu, at, 3),
                                mir_const(mu, at, 2));
      }
      else if (l->two_state && (wk == W_STD_BIT || wk == W_STD_BOOL)) {
         mir_value_t b = vi_to_bool(l, v);
         arg = mir_build_select(mu, at, b, mir_const(mu, at, 1),
                                mir_const(mu, at, 0));
      }
      else
         arg = mir_build_cast(mu, at, v.val);
   }
   else if (type_is_integer(base)) {
      int64_t lo = INT64_MIN, hi = INT64_MAX;
      if (type_dims(base) > 0)
         folded_bounds(type_dim(base, 0), &lo, &hi);
      mir_type_t at = mir_int_type(mu, lo, hi);
      arg = mir_build_cast(mu, at, vi_to_i64(mu, v.val));
   }
   else
      return false;
   mir_type_t str_t = mir_uarray_type(mu, 1, vi_u8_type(mu));
   mir_value_t uarr = mir_build_fcall(mu, func, str_t, MIR_NULL_STAMP,
                                      &arg, 1);
   out->ptr = mir_build_unwrap(mu, uarr);
   out->len = mir_build_uarray_len(mu, uarr, 0);

   if (to_string && has_chars) {
      // 'IMAGE of a character literal is the literal with its apostrophes
      mir_value_t first = mir_build_load(mu, out->ptr);
      mir_value_t quoted = mir_build_cmp(mu, MIR_CMP_EQ, first,
                                         vi_u8(mu, '\''));
      mir_type_t t_off = mir_offset_type(mu);
      out->ptr = mir_build_select(
         mu, mir_get_type(mu, out->ptr), quoted,
         mir_build_array_ref(mu, out->ptr, vi_off(mu, 1)), out->ptr);
      out->len = mir_build_select(
         mu, t_off, quoted, mir_build_sub(mu, t_off, out->len, vi_off(mu, 2)),
         out->len);
   }
   return true;
}

static bool vi_lower_str_expr(vi_lower_t *l, tree_t expr, vi_str_t *out)
{
   mir_unit_t *mu = l->mu;
   if (expr == NULL)
      return false;

   switch (tree_kind(expr)) {
   case T_STRING:
      {
         const int nch = tree_chars(expr);
         const int alen = MAX(1, nch);
         mir_value_t *vals LOCAL = xmalloc_array(alen, sizeof(mir_value_t));
         if (nch == 0)
            vals[0] = vi_u8(mu, 0);
         else {
            for (int i = 0; i < nch; i++) {
               tree_t ch = tree_char(expr, i);
               if (tree_kind(ch) != T_REF || tree_kind(tree_ref(ch)) != T_ENUM_LIT)
                  return false;
               vals[i] = vi_u8(mu, (uint8_t)tree_pos(tree_ref(ch)));
            }
         }
         mir_value_t ca = mir_const_array(
            mu, mir_carray_type(mu, alen, vi_u8_type(mu)), vals, alen);
         out->ptr = mir_build_address_of(mu, ca);
         out->len = vi_off(mu, nch);
         return true;
      }

   case T_AGGREGATE:
      {
         const int na = tree_assocs(expr);
         if (na < 1)
            return false;
         vi_str_t *parts LOCAL = xcalloc_array(na, sizeof(vi_str_t));
         mir_value_t tot = vi_off(mu, 0);
         for (int i = 0; i < na; i++) {
            tree_t a = tree_assoc(expr, i);
            const assoc_kind_t ak = tree_subkind(a);
            tree_t v = tree_value(a);
            if (ak == A_CONCAT || (ak == A_POS && type_is_array(tree_type(v)))) {
               if (!vi_lower_str_expr(l, v, &parts[i]))
                  return false;
            }
            else if (ak == A_POS) {
               vi_val_t ch;
               if (!vi_lower_expr(l, v, &ch))
                  return false;
               mir_value_t b = mir_build_alloc(mu, vi_u8_type(mu),
                                               MIR_NULL_STAMP, vi_off(mu, 1));
               mir_build_store(mu, b, mir_build_cast(mu, vi_u8_type(mu), ch.val));
               parts[i].ptr = b;
               parts[i].len = vi_off(mu, 1);
            }
            else
               return false;
            tot = mir_build_add(mu, mir_offset_type(mu), tot, parts[i].len);
         }
         mir_value_t buf = mir_build_alloc(mu, vi_u8_type(mu),
                                           MIR_NULL_STAMP, tot);
         mir_value_t cur = vi_off(mu, 0);
         for (int i = 0; i < na; i++) {
            mir_build_copy(mu, mir_build_array_ref(mu, buf, cur),
                           parts[i].ptr, parts[i].len);
            cur = mir_build_add(mu, mir_offset_type(mu), cur, parts[i].len);
         }
         out->ptr = buf;
         out->len = tot;
         return true;
      }

   case T_QUALIFIED:
   case T_TYPE_CONV:
      return vi_lower_str_expr(l, tree_value(expr), out);

   case T_REF:
      {
         tree_t d = tree_ref(expr);
         if (d == NULL)
            return false;
         if (tree_kind(d) == T_CONST_DECL && tree_has_value(d))
            return vi_lower_str_expr(l, tree_value(d), out);
         if (tree_kind(d) == T_GENERIC_DECL && l->ctx != NULL) {
            tree_t eblk = l->ctx->eval_block != NULL
               ? l->ctx->eval_block : l->ctx->block;
            const tree_t *eancs = l->ctx->eval_block != NULL
               ? l->ctx->eval_ancestors : l->ctx->ancestors;
            const int n_eancs = l->ctx->eval_block != NULL
               ? l->ctx->n_eval_ancestors : l->ctx->n_ancestors;
            for (int ai = -1; ai < n_eancs; ai++) {
               tree_t gb = (ai < 0) ? eblk : eancs[ai];
               if (gb == NULL)
                  continue;
               const int ng = tree_generics(gb);
               for (int gi = 0; gi < ng; gi++) {
                  if (tree_generic(gb, gi) == d) {
                     tree_t v = vhdl_island_generic_value(gb, gi);
                     return v != NULL && vi_lower_str_expr(l, v, out);
                  }
               }
            }
         }
         return false;
      }

   case T_ATTR_REF:
      if (tree_subkind(expr) == ATTR_IMAGE && tree_params(expr) == 1)
         return vi_lower_image(l, tree_value(tree_param(expr, 0)), false, out);
      return false;

   case T_FCALL:
      {
         tree_t decl = tree_ref(expr);
         const subprogram_kind_t sk = tree_subkind(decl);
         if (sk == S_TO_STRING && tree_params(expr) == 1)
            return vi_lower_image(l, tree_value(tree_param(expr, 0)), true,
                                  out);
         if (sk == S_CONCAT && tree_params(expr) >= 1) {
            const int np = tree_params(expr);
            vi_str_t *parts LOCAL = xcalloc_array(np, sizeof(vi_str_t));
            mir_value_t tot = vi_off(mu, 0);
            for (int i = 0; i < np; i++) {
               tree_t ai = tree_value(tree_param(expr, i));
               if (type_is_array(tree_type(ai))) {
                  if (!vi_lower_str_expr(l, ai, &parts[i]))
                     return false;
               }
               else {
                  vi_val_t ch;
                  if (!vi_lower_expr(l, ai, &ch))
                     return false;
                  mir_value_t b = mir_build_alloc(mu, vi_u8_type(mu),
                                                  MIR_NULL_STAMP, vi_off(mu, 1));
                  mir_build_store(mu, b, mir_build_cast(mu, vi_u8_type(mu), ch.val));
                  parts[i].ptr = b;
                  parts[i].len = vi_off(mu, 1);
               }
               tot = mir_build_add(mu, mir_offset_type(mu), tot, parts[i].len);
            }
            mir_value_t buf = mir_build_alloc(mu, vi_u8_type(mu),
                                              MIR_NULL_STAMP, tot);
            mir_value_t cur = vi_off(mu, 0);
            for (int i = 0; i < np; i++) {
               mir_build_copy(mu, mir_build_array_ref(mu, buf, cur),
                              parts[i].ptr, parts[i].len);
               cur = mir_build_add(mu, mir_offset_type(mu), cur, parts[i].len);
            }
            out->ptr = buf;
            out->len = tot;
            return true;
         }
         if (tree_params(expr) == 1) {
            vi_val_t av;
            if (!vi_lower_expr(l, tree_value(tree_param(expr, 0)), &av))
               return false;
            if (l->two_state && !vi_vec2_to_bytes(l, &av))
               return false;
            mir_type_t str_t = mir_uarray_type(mu, 1, vi_u8_type(mu));
            mir_value_t uarr;
            if (!vi_modeb_call_ieee(l, decl, &av, 1, str_t, &uarr))
               return false;
            out->ptr = mir_build_unwrap(mu, uarr);
            out->len = mir_build_uarray_len(mu, uarr, 0);
            return true;
         }
         return false;
      }

   default:
      return false;
   }
}

static bool vi_bind_process_locals(vi_lower_t *l, tree_t proc, vi_scope_t *sc,
                                   bool subprog);

static bool vi_lower_inline_proc_inner(vi_lower_t *l, tree_t pcall)
{
   if (!tree_has_ref(pcall))
      return false;
   tree_t proc = vhdl_island_resolve_proc_body(tree_ref(pcall));
   if (proc == NULL)
      return false;

   mir_unit_t *mu = l->mu;
   const int np = tree_params(pcall);
   if (np != tree_ports(proc))
      return false;

   vi_local_t *param_locs LOCAL = xcalloc_array(MAX(1, np), sizeof(vi_local_t));
   for (int i = 0; i < np; i++) {
      tree_t fp = tree_port(proc, i);
      tree_t pm = tree_param(pcall, i);
      if (tree_subkind(pm) != P_POS) {
         for (int k = 0; k < i; k++)
            free(param_locs[k].rec_leaves);
         return false;
      }
      tree_t pe = tree_value(pm);
      type_t fpt = tree_type(fp);
      const port_mode_t mode = tree_subkind(fp);

      if (mode == PORT_IN) {
         if (type_is_record(fpt)) {
            vi_rec_leaf_list_t leaves = AINIT;
            int rpath[VHDL_ISIG_MAX_DEPTH];
            if (!vi_collect_rec_local_leaves(l, fpt, rpath, 0, tree_ident(fp),
                                             &leaves)) {
               ACLEAR(leaves);
               for (int k = 0; k < i; k++)
                  free(param_locs[k].rec_leaves);
               return false;
            }
            param_locs[i] = (vi_local_t){
               .decl         = fp,
               .is_rec       = true,
               .rec_type     = fpt,
               .rec_leaves   = leaves.items,
               .n_rec_leaves = leaves.count,
            };
            if (!vi_assign_rec_local(l, &param_locs[i], rpath, 0, fpt, pe)) {
               for (int k = 0; k <= i; k++)
                  free(param_locs[k].rec_leaves);
               return false;
            }
         }
         else {
            vi_val_t av;
            if (!vi_lower_expr(l, pe, &av)) {
               for (int k = 0; k < i; k++)
                  free(param_locs[k].rec_leaves);
               return false;
            }
            bool has_cint = false;
            int64_t cint = 0;
            if (av.kind == VIVAL_INT) {
               vi_check_int_bounds(l, fpt, vi_to_i64(mu, av.val), pe, fp);
               if (vi_eval_const_int(l, pe, &cint)
                   || mir_get_const(mu, av.val, &cint))
                  has_cint = true;
            }
            param_locs[i] = (vi_local_t){
               .decl          = fp,
               .is_vec        = (av.kind == VIVAL_VEC),
               .is_std        = av.is_std,
               .is_bool       = av.is_bool,
               .is_signed     = av.is_signed,
               .is_int        = (av.kind == VIVAL_INT),
               .width         = av.width,
               .dir           = av.dir,
               .left          = av.left,
               .right         = av.right,
               .by_val        = true,
               .pval          = av,
               .has_const_int = has_cint,
               .const_int     = cint,
            };
         }
      }
      else if (mode == PORT_OUT || mode == PORT_INOUT) {
         if (type_is_record(fpt)) {
            vi_local_t *aloc = NULL;
            int apath[VHDL_ISIG_MAX_DEPTH];
            uint8_t alen = 0;
            if (!vi_extract_local_rec_path(l, pe, &aloc, apath, &alen)) {
               for (int k = 0; k < i; k++)
                  free(param_locs[k].rec_leaves);
               return false;
            }
            const vi_rec_leaf_t *matched[512];
            uint32_t nm = vi_collect_rec_prefix_leaves(aloc, apath, alen,
                                                       matched, 512);
            if (nm == 0 || nm > 512) {
               for (int k = 0; k < i; k++)
                  free(param_locs[k].rec_leaves);
               return false;
            }
            vi_rec_leaf_t *shifted = xmalloc_array(nm, sizeof(vi_rec_leaf_t));
            for (uint32_t m = 0; m < nm; m++) {
               shifted[m] = *matched[m];
               shifted[m].path_len = (uint8_t)(matched[m]->path_len - alen);
               if (shifted[m].path_len > 0)
                  memcpy(shifted[m].path, matched[m]->path + alen,
                         shifted[m].path_len * sizeof(int));
            }
            param_locs[i] = (vi_local_t){
               .decl         = fp,
               .is_rec       = true,
               .rec_type     = fpt,
               .rec_leaves   = shifted,
               .n_rec_leaves = nm,
            };
         }
         else {
            vi_rec_leaf_t lf;
            if (!vi_resolve_local_leaf(l, pe, &lf) || lf.is_array2d) {
               for (int k = 0; k < i; k++)
                  free(param_locs[k].rec_leaves);
               return false;
            }
            param_locs[i] = (vi_local_t){
               .decl      = fp,
               .is_vec    = lf.is_vec,
               .is_std    = lf.is_std,
               .is_bool   = lf.is_bool,
               .is_signed = lf.is_signed,
               .is_int    = lf.is_int,
               .width     = lf.width,
               .dir       = lf.dir,
               .left      = lf.left,
               .right     = lf.right,
               .storage   = lf.storage,
            };
         }
      }
      else {
         for (int k = 0; k < i; k++)
            free(param_locs[k].rec_leaves);
         return false;
      }
   }

   vi_scope_t child = {
      .parent = l->scope,
      .ret_bb = mir_add_block(mu),
   };
   for (int i = 0; i < np; i++)
      APUSH(child.locals, param_locs[i]);

   vi_scope_t *prev_scope = l->scope;
   l->scope = &child;

   if (!vi_bind_process_locals(l, proc, &child, true)
       || !vi_check_spec_defaults(l, proc, &child)) {
      l->scope = prev_scope;
      vi_scope_clear(&child);
      return false;
   }

   for (int i = 0; i < tree_stmts(proc); i++) {
      unsigned pos = 0;
      if (mir_block_finished(mu, mir_get_cursor(mu, &pos)))
         break;
      if (!vi_lower_stmt(l, tree_stmt(proc, i))) {
         l->scope = prev_scope;
         vi_scope_clear(&child);
         return false;
      }
   }

   unsigned pos = 0;
   if (!mir_block_finished(mu, mir_get_cursor(mu, &pos)))
      mir_build_jump(mu, child.ret_bb);

   mir_set_cursor(mu, child.ret_bb, MIR_APPEND);
   l->scope = prev_scope;
   vi_scope_clear(&child);
   return true;
}

// Integer variables with a value known while lowering: an assignment of a
// constant sets has_const_int on the variable, which holds until the next
// assignment along straight-line code.  Statements that branch or repeat
// forget what their bodies assign (see vi_const_forget), so a value never
// reaches code that a different path could reach with another value.  This
// gives constant bounds to loops and slices whose bounds come from such
// variables, as in a loop that doubles a stride on every pass.

typedef struct {
   tree_t  *decls;
   unsigned n;
   unsigned cap;
} vi_decl_set_t;

static void vi_assigned_cb(tree_t t, void *ctx)
{
   vi_decl_set_t *set = ctx;
   tree_t target = tree_target(t);
   while (tree_kind(target) == T_ARRAY_REF || tree_kind(target) == T_ARRAY_SLICE
          || tree_kind(target) == T_RECORD_REF)
      target = tree_value(target);
   if (tree_kind(target) != T_REF || !tree_has_ref(target))
      return;
   if (set->n == set->cap) {
      set->cap = MAX(set->cap * 2, 8u);
      set->decls = xrealloc_array(set->decls, set->cap, sizeof(tree_t));
   }
   set->decls[set->n++] = tree_ref(target);
}

// Variables STMT may assign; *ALL when it calls a procedure, which may
// assign any variable through a parameter
static void vi_collect_assigned(tree_t stmt, vi_decl_set_t *set, bool *all)
{
   tree_visit_only(stmt, vi_assigned_cb, set, T_VAR_ASSIGN);
   *all = tree_visit_only(stmt, NULL, NULL, T_PCALL) > 0;
}

static bool vi_decl_set_has(const vi_decl_set_t *set, tree_t decl)
{
   for (unsigned i = 0; i < set->n; i++) {
      if (set->decls[i] == decl)
         return true;
   }
   return false;
}

// Forget the known values of the variables STMT may assign
static void vi_const_forget(vi_lower_t *l, tree_t stmt)
{
   vi_decl_set_t set = { NULL, 0, 0 };
   bool all = false;
   vi_collect_assigned(stmt, &set, &all);
   for (vi_scope_t *sc = l->scope; sc != NULL; sc = sc->parent) {
      for (unsigned i = 0; i < sc->locals.count; i++) {
         vi_local_t *loc = &(sc->locals.items[i]);
         if (vi_is_const_var(loc) && (all || vi_decl_set_has(&set, loc->decl)))
            loc->has_const_int = false;
      }
   }
   free(set.decls);
}

static void vi_const_forget_all(vi_lower_t *l)
{
   for (vi_scope_t *sc = l->scope; sc != NULL; sc = sc->parent) {
      for (unsigned i = 0; i < sc->locals.count; i++) {
         vi_local_t *loc = &(sc->locals.items[i]);
         if (vi_is_const_var(loc))
            loc->has_const_int = false;
      }
   }
}

typedef struct {
   vi_local_t *loc;
   bool        has;
   int64_t     val;
} vi_const_save_t;

typedef struct vi_const_snap {
   vi_const_save_t *items;
   uint32_t         count;
   uint32_t         limit;
} vi_const_snap_t;

// The known values on entry to a branching statement, restored for each
// branch
static void vi_const_snapshot(vi_lower_t *l, vi_const_snap_t *snap)
{
   for (vi_scope_t *sc = l->scope; sc != NULL; sc = sc->parent) {
      for (unsigned i = 0; i < sc->locals.count; i++) {
         vi_local_t *loc = &(sc->locals.items[i]);
         if (vi_is_const_var(loc)) {
            vi_const_save_t sv = { loc, loc->has_const_int, loc->const_int };
            APUSH(*snap, sv);
         }
      }
   }
}

static void vi_const_restore(const vi_const_snap_t *snap)
{
   for (unsigned i = 0; i < snap->count; i++) {
      snap->items[i].loc->has_const_int = snap->items[i].has;
      snap->items[i].loc->const_int = snap->items[i].val;
   }
}

typedef struct {
   const vi_decl_set_t *vars;
   tree_t               param;
   bool                 hit;
} vi_unroll_scan_t;

static void vi_unroll_ref_cb(tree_t t, void *ctx)
{
   vi_unroll_scan_t *us = ctx;
   if (!tree_has_ref(t))
      return;
   tree_t d = tree_ref(t);
   if (d == us->param || vi_decl_set_has(us->vars, d))
      us->hit = true;
}

static void vi_unroll_range(vi_unroll_scan_t *us, tree_t rg)
{
   if (tree_subkind(rg) == RANGE_EXPR)
      tree_visit_only(tree_value(rg), vi_unroll_ref_cb, us, T_REF);
   else {
      tree_visit_only(tree_left(rg), vi_unroll_ref_cb, us, T_REF);
      tree_visit_only(tree_right(rg), vi_unroll_ref_cb, us, T_REF);
   }
}

typedef struct {
   tree_t           loop;
   vi_unroll_scan_t loops;    // Inner loop ranges: variables or parameter
   vi_unroll_scan_t slices;   // Slice ranges: variables
} vi_unroll_ctx_t;

static void vi_unroll_for_cb(tree_t t, void *ctx)
{
   vi_unroll_ctx_t *uc = ctx;
   if (t != uc->loop)
      vi_unroll_range(&uc->loops, tree_range(t, 0));
}

static void vi_unroll_slice_cb(tree_t t, void *ctx)
{
   vi_unroll_ctx_t *uc = ctx;
   vi_unroll_range(&uc->slices, tree_range(t, 0));
}

// Whether loop STMT of NITERS passes is lowered pass by pass, with its
// parameter and the variables its body assigns known in each pass: when
// the body has an inner loop whose bounds or a slice whose bounds depend
// on them, which a single body with a dynamic parameter cannot lower
#define VI_UNROLL_MAX_ITERS  64
#define VI_UNROLL_MAX_TOTAL  2048

static bool vi_loop_wants_unroll(vi_lower_t *l, tree_t stmt, int64_t niters)
{
   if (niters > VI_UNROLL_MAX_ITERS
       || l->unrolled + niters > VI_UNROLL_MAX_TOTAL)
      return false;
   // A pass that ends early has no straight-line successor
   if (tree_visit_only(stmt, NULL, NULL, T_EXIT) > 0
       || tree_visit_only(stmt, NULL, NULL, T_NEXT) > 0
       || tree_visit_only(stmt, NULL, NULL, T_RETURN) > 0)
      return false;

   vi_decl_set_t set = { NULL, 0, 0 };
   bool all = false;
   vi_collect_assigned(stmt, &set, &all);
   vi_unroll_ctx_t uc = {
      .loop   = stmt,
      .loops  = { .vars = &set, .param = tree_decl(stmt, 0) },
      .slices = { .vars = &set, .param = NULL },
   };
   tree_visit_only(stmt, vi_unroll_for_cb, &uc, T_FOR);
   tree_visit_only(stmt, vi_unroll_slice_cb, &uc, T_ARRAY_SLICE);
   free(set.decls);
   return uc.loops.hit || uc.slices.hit;
}

static bool vi_lower_stmt_inner(vi_lower_t *l, tree_t stmt);

static bool vi_lower_stmt(vi_lower_t *l, tree_t stmt)
{
   const loc_t *old_loc = vi_push_loc(l, stmt);
   bool ok;
   if (tree_kind(stmt) == T_CASE) {
      // Each alternative starts from the values known before the case
      vi_const_snap_t snap = AINIT, *prev = l->case_snap;
      vi_const_snapshot(l, &snap);
      l->case_snap = &snap;
      ok = vi_lower_stmt_inner(l, stmt);
      l->case_snap = prev;
      vi_const_restore(&snap);
      ACLEAR(snap);
      vi_const_forget(l, stmt);
   }
   else
      ok = vi_lower_stmt_inner(l, stmt);
   if (!ok)
      vi_generic_reason(l, "stmt", stmt);
   vi_pop_loc(l, old_loc);
   return ok;
}

static bool vi_lower_stmt_inner(vi_lower_t *l, tree_t stmt)
{
   if (stmt == NULL || l->failed)
      return false;
   mir_unit_t *mu = l->mu;

   switch (tree_kind(stmt)) {
   case T_NULL:
   case T_DUMMY_DRIVER:
      return true;

   case T_RETURN:
      if (mir_is_null(l->scope->ret_bb))
         return false;
      if (tree_has_value(stmt) && l->scope->ret_rec != NULL) {
         int rpath[VHDL_ISIG_MAX_DEPTH];
         if (!vi_assign_rec_local(l, l->scope->ret_rec, rpath, 0,
                                  l->scope->ret_type, tree_value(stmt)))
            return false;
         l->scope->has_ret_val = true;
      }
      else if (tree_has_value(stmt)) {
         vi_val_t rv;
         if (!vi_lower_expr(l, tree_value(stmt), &rv))
            return false;
         if (l->scope->ret_is_vec) {
            if (rv.kind != VIVAL_VEC)
               return false;
            if (l->scope->has_ret_val && rv.width != l->scope->ret_w)
               return false;
            if (l->scope->ret_ptr_indirect) {
               l->scope->ret_w = rv.width;
               l->scope->ret_dir = rv.dir;
               l->scope->ret_left = rv.left;
               l->scope->ret_right = rv.right;
               mir_value_t buf = vi_modeb_alloc_vec(l, rv.width);
               mir_build_copy(mu, buf, rv.val, vi_off(mu, rv.width));
               mir_build_store(mu, l->scope->ret_slot, buf);
            }
            else {
               if (mir_is_null(l->scope->ret_slot)) {
                  l->scope->ret_w = rv.width;
                  l->scope->ret_dir = rv.dir;
                  l->scope->ret_left = rv.left;
                  l->scope->ret_right = rv.right;
                  if (l->two_state) {
                     mir_type_t vt = mir_vec2_type(mu, (int)rv.width, l->scope->ret_signed);
                     l->scope->ret_slot = mir_add_var(mu, vt, MIR_NULL_STAMP,
                                                      ident_uniq("fnret"), MIR_VAR_TEMP);
                  }
                  else
                     l->scope->ret_slot = vi_modeb_alloc_vec(l, rv.width);
               }
               if (rv.width != l->scope->ret_w)
                  return false;
               if (l->two_state) {
                  mir_value_t v = (rv.is_signed != l->scope->ret_signed)
                     ? mir_build_cast(mu, mir_vec2_type(mu, (int)l->scope->ret_w,
                                                        l->scope->ret_signed),
                                      rv.val)
                     : rv.val;
                  mir_build_store(mu, l->scope->ret_slot, v);
               }
               else
                  mir_build_copy(mu, l->scope->ret_slot, rv.val, vi_off(mu, rv.width));
            }
         }
         else if (vi_type_is_int_or_user_enum(l->scope->ret_type)) {
            if (rv.kind != VIVAL_INT)
               return false;
            mir_build_store(mu, l->scope->ret_slot, vi_to_i64(mu, rv.val));
         }
         else {
            if (rv.kind != VIVAL_BIT)
               return false;
            mir_build_store(mu, l->scope->ret_slot,
                            l->two_state ? vi_to_vec2_1(l, rv) : rv.val);
         }
         l->scope->has_ret_val = true;
      }
      mir_build_jump(mu, l->scope->ret_bb);
      return true;

   case T_SIGNAL_ASSIGN:
      return vi_lower_sig_assign(l, stmt);

   case T_VAR_ASSIGN:
      return vi_lower_var_assign(l, stmt);

   case T_PCALL:
      {
         const bool ok = vi_lower_inline_proc(l, stmt);
         vi_const_forget_all(l);   // Through out and inout parameters
         return ok;
      }

   case T_IF:
      {
         const int nc = tree_conds(stmt);
         mir_block_t exit_bb = mir_add_block(mu);
         vi_const_snap_t snap = AINIT;
         vi_const_snapshot(l, &snap);
         for (int i = 0; i < nc; i++) {
            tree_t br = tree_cond(stmt, i);
            vi_const_restore(&snap);
            int64_t cval = 0;
            bool const_true = false;
            if (tree_has_value(br)
                && vi_eval_const_int(l, tree_value(br), &cval)) {
               if (cval == 0)
                  continue;
               const_true = true;
            }
            mir_block_t next_bb = (!const_true && i + 1 < nc)
               ? mir_add_block(mu) : exit_bb;
            if (tree_has_value(br) && !const_true) {
               vi_val_t cv;
               if (!vi_lower_expr(l, tree_value(br), &cv))
                  return false;
               mir_block_t tbb = mir_add_block(mu);
               mir_build_cond(mu, vi_to_bool(l, cv), tbb, next_bb);
               mir_set_cursor(mu, tbb, MIR_APPEND);
            }
            for (int j = 0; j < tree_stmts(br); j++) {
               unsigned pos = 0;
               if (mir_block_finished(mu, mir_get_cursor(mu, &pos)))
                  break;
               if (!vi_lower_stmt(l, tree_stmt(br, j))) {
                  ACLEAR(snap);
                  return false;
               }
            }
            unsigned pos = 0;
            if (!mir_block_finished(mu, mir_get_cursor(mu, &pos)))
               mir_build_jump(mu, exit_bb);
            if (const_true)
               break;
            if (i + 1 < nc)
               mir_set_cursor(mu, next_bb, MIR_APPEND);
         }
         unsigned pos = 0;
         if (!mir_block_finished(mu, mir_get_cursor(mu, &pos)))
            mir_build_jump(mu, exit_bb);
         mir_set_cursor(mu, exit_bb, MIR_APPEND);
         vhdl_island_var_defs_clear((vhdl_island_ctx_t *)l->ctx);
         vi_const_restore(&snap);
         ACLEAR(snap);
         vi_const_forget(l, stmt);
         return true;
      }

   case T_CASE:
      {
         vi_val_t sel;
         if (!vi_lower_expr(l, tree_value(stmt), &sel))
            return false;

         const int nalts = tree_stmts(stmt);
         mir_block_t exit_bb = mir_add_block(mu);

         for (int i = 0; i < nalts; i++) {
            tree_t alt = tree_stmt(stmt, i);
            const int nch = tree_choices(alt);
            bool has_others = false;
            for (int j = 0; j < nch; j++) {
               tree_t ch = tree_choice(alt, j);
               if (tree_ranges(ch) == 0 && !tree_has_name(ch)) {
                  has_others = true;
                  break;
               }
            }
            mir_block_t body_bb = mir_add_block(mu);
            mir_block_t next_bb = (i + 1 < nalts && !has_others)
               ? mir_add_block(mu) : exit_bb;

            if (has_others) {
               mir_build_jump(mu, body_bb);
            }
            else {
               mir_value_t any = mir_const(mu, mir_bool_type(mu), 0);
               for (int j = 0; j < nch; j++) {
                  tree_t ch = tree_choice(alt, j);
                  if (tree_ranges(ch) == 0 && tree_has_name(ch)) {
                     if (l->spec && vi_spec_choice_non01(l, tree_name(ch), sel)) {
                        // Never matches a '0'/'1' selector
                        any = mir_build_or(mu, any, vi_spec_opaque_bool(l, false));
                        continue;
                     }
                     vi_val_t cv;
                     if (!vi_lower_expr(l, tree_name(ch), &cv))
                        return false;
                     mir_value_t m = MIR_NULL_VALUE;
                     if (sel.kind == VIVAL_INT && cv.kind == VIVAL_INT)
                        m = mir_build_cmp(mu, MIR_CMP_EQ, vi_to_i64(mu, sel.val),
                                          vi_to_i64(mu, cv.val));
                     else if (sel.kind == VIVAL_BIT && cv.kind == VIVAL_BIT) {
                        if (l->two_state) {
                           if (sel.is_std) {
                              tree_t cn = tree_name(ch);
                              if (tree_kind(cn) == T_REF
                                  && tree_kind(tree_ref(cn)) == T_ENUM_LIT) {
                                 unsigned p = tree_pos(tree_ref(cn));
                                 if (p == 0 || p == 1) // 'U'/'X' unreachable in 2-state
                                    m = mir_const(mu, mir_bool_type(mu), 0);
                              }
                           }
                           if (mir_is_null(m))
                              m = mir_build_cmp(mu, MIR_CMP_EQ,
                                                mir_build_cast(mu, vi_u8_type(mu), vi_to_vec2_1(l, sel)),
                                                mir_build_cast(mu, vi_u8_type(mu), vi_to_vec2_1(l, cv)));
                        }
                        else
                           m = mir_build_cmp(mu, MIR_CMP_EQ, sel.val, cv.val);
                     }
                     else if (sel.kind == VIVAL_VEC && cv.kind == VIVAL_VEC
                              && sel.width == cv.width) {
                        if (l->two_state) {
                           mir_value_t c = mir_build_binary(mu, MIR_VEC_CASE_EQ,
                                                            mir_vec2_type(mu, (int)sel.width, false),
                                                            sel.val, cv.val);
                           m = mir_build_test(mu, c);
                        }
                        else {
                           m = mir_const(mu, mir_bool_type(mu), 1);
                           for (uint32_t k = 0; k < sel.width; k++) {
                              mir_value_t e0 = mir_build_load(mu, mir_build_array_ref(mu, sel.val, vi_off(mu, k)));
                              mir_value_t e1 = mir_build_load(mu, mir_build_array_ref(mu, cv.val, vi_off(mu, k)));
                              m = mir_build_and(mu, m, mir_build_cmp(mu, MIR_CMP_EQ, e0, e1));
                           }
                        }
                     }
                     else
                        return false;
                     any = mir_build_or(mu, any, m);
                  }
                  else if (tree_ranges(ch) > 0 && sel.kind == VIVAL_INT) {
                     tree_t rg = tree_range(ch, 0);
                     int64_t rl, rr;
                     if (!vi_eval_const_int(l, tree_left(rg), &rl)
                         || !vi_eval_const_int(l, tree_right(rg), &rr))
                        return false;
                     int64_t lo = MIN(rl, rr), hi = MAX(rl, rr);
                     mir_value_t sv = vi_to_i64(mu, sel.val);
                     mir_value_t in = mir_build_and(
                        mu,
                        mir_build_cmp(mu, MIR_CMP_GEQ, sv, vi_i64(mu, lo)),
                        mir_build_cmp(mu, MIR_CMP_LEQ, sv, vi_i64(mu, hi)));
                     any = mir_build_or(mu, any, in);
                  }
                  else
                     return false;
               }
               mir_build_cond(mu, any, body_bb, next_bb);
            }

            mir_set_cursor(mu, body_bb, MIR_APPEND);
            vi_const_restore(l->case_snap);
            for (int j = 0; j < tree_stmts(alt); j++) {
               unsigned pos = 0;
               if (mir_block_finished(mu, mir_get_cursor(mu, &pos)))
                  break;
               if (!vi_lower_stmt(l, tree_stmt(alt, j)))
                  return false;
            }
            unsigned pos = 0;
            if (!mir_block_finished(mu, mir_get_cursor(mu, &pos)))
               mir_build_jump(mu, exit_bb);

            if (has_others || i + 1 >= nalts)
               break;
            mir_set_cursor(mu, next_bb, MIR_APPEND);
         }
         mir_set_cursor(mu, exit_bb, MIR_APPEND);
         return true;
      }

   case T_SEQUENCE:
      for (int j = 0; j < tree_stmts(stmt); j++) {
         unsigned pos = 0;
         if (mir_block_finished(mu, mir_get_cursor(mu, &pos)))
            break;
         if (!vi_lower_stmt(l, tree_stmt(stmt, j)))
            return false;
      }
      return true;

   case T_ASSERT:
      {
         vi_val_t cv;
         if (!vi_lower_expr(l, tree_value(stmt), &cv))
            return false;
         mir_value_t cond = vi_to_bool(l, cv);
         int64_t cval = 0;
         if (mir_get_const(mu, cond, &cval) && cval != 0)
            return true;

         mir_block_t fail_bb = mir_add_block(mu);
         mir_block_t cont_bb = mir_add_block(mu);
         mir_build_cond(mu, cond, cont_bb, fail_bb);

         mir_set_cursor(mu, fail_bb, MIR_APPEND);
         if (l->spec) {
            // Mode B reports the failure
            vi_emit_deopt_return(l, RT_ISLAND_DEOPT_REPORT);
            mir_set_cursor(mu, cont_bb, MIR_APPEND);
            return true;
         }
         mir_value_t msg_ptr = MIR_NULL_VALUE, msg_len = MIR_NULL_VALUE;
         if (tree_has_message(stmt)) {
            vi_str_t ms;
            if (!vi_lower_str_expr(l, tree_message(stmt), &ms))
               return false;
            msg_ptr = ms.ptr;
            msg_len = ms.len;
         }
         mir_type_t sev_t = mir_int_type(mu, 0, 3);
         mir_value_t sev;
         if (tree_has_severity(stmt)) {
            vi_val_t sv;
            if (!vi_lower_expr(l, tree_severity(stmt), &sv))
               return false;
            sev = mir_build_cast(mu, sev_t, sv.val);
         }
         else
            sev = mir_const(mu, sev_t, SEVERITY_ERROR);
         mir_value_t locus = mir_build_debug_locus(
            mu, tree_to_object(tree_value(stmt)));
         mir_build_assert(mu, mir_const(mu, mir_bool_type(mu), 0),
                          msg_ptr, msg_len, sev, locus,
                          MIR_NULL_VALUE, MIR_NULL_VALUE);
         mir_build_jump(mu, cont_bb);

         mir_set_cursor(mu, cont_bb, MIR_APPEND);
         return true;
      }

   case T_REPORT:
      if (l->spec) {
         vi_emit_deopt(l, RT_ISLAND_DEOPT_REPORT);   // Mode B reports
         return true;
      }
      {
         vi_str_t ms;
         if (tree_has_message(stmt)) {
            if (!vi_lower_str_expr(l, tree_message(stmt), &ms))
               return false;
         }
         else {
            mir_value_t z = vi_u8(mu, 0);
            mir_value_t ca = mir_const_array(
               mu, mir_carray_type(mu, 1, vi_u8_type(mu)), &z, 1);
            ms.ptr = mir_build_address_of(mu, ca);
            ms.len = vi_off(mu, 0);
         }
         mir_type_t sev_t = mir_int_type(mu, 0, 3);
         mir_value_t sev;
         if (tree_has_severity(stmt)) {
            vi_val_t sv;
            if (!vi_lower_expr(l, tree_severity(stmt), &sv))
               return false;
            sev = mir_build_cast(mu, sev_t, sv.val);
         }
         else
            sev = mir_const(mu, sev_t, SEVERITY_NOTE);
         mir_value_t locus = mir_build_debug_locus(mu, tree_to_object(stmt));
         mir_build_report(mu, ms.ptr, ms.len, sev, locus);
         return true;
      }

   case T_EXIT:
   case T_NEXT:
      {
         mir_block_t target_bb = MIR_NULL_BLOCK;
         for (vi_scope_t *sc = l->scope; sc != NULL; sc = sc->parent) {
            mir_block_t bb = (tree_kind(stmt) == T_EXIT)
               ? sc->loop_exit_bb : sc->loop_next_bb;
            if (!mir_is_null(bb)) {
               target_bb = bb;
               break;
            }
         }
         if (mir_is_null(target_bb))
            return false;
         if (tree_has_value(stmt)) {
            vi_val_t cv;
            if (!vi_lower_expr(l, tree_value(stmt), &cv))
               return false;
            mir_block_t cont_bb = mir_add_block(mu);
            mir_build_cond(mu, vi_to_bool(l, cv), target_bb, cont_bb);
            mir_set_cursor(mu, cont_bb, MIR_APPEND);
         }
         else
            mir_build_jump(mu, target_bb);
         return true;
      }

   case T_FOR:
      {
         tree_t rg = tree_range(stmt, 0);
         int64_t lv = 0, rv = 0;
         range_kind_t rk = tree_subkind(rg);
         if (rk == RANGE_EXPR && tree_has_value(rg)) {
            tree_t re = tree_value(rg);
            if (tree_kind(re) != T_ATTR_REF)
               return false;
            const attr_kind_t ak = tree_subkind(re);
            tree_t nm = tree_name(re);
            if (tree_kind(nm) != T_REF)
               return false;
            vi_local_t *ploc = vi_find_local(l, tree_ref(nm));
            if (ploc == NULL || !ploc->is_vec || ploc->width == 0)
               return false;
            if (ak == ATTR_RANGE) {
               rk = ploc->dir;
               lv = ploc->left;
               rv = ploc->right;
            }
            else if (ak == ATTR_REVERSE_RANGE) {
               rk = (ploc->dir == RANGE_TO) ? RANGE_DOWNTO : RANGE_TO;
               lv = ploc->right;
               rv = ploc->left;
            }
            else
               return false;
         }
         else if (!vi_eval_const_int(l, tree_left(rg), &lv)
                  || !vi_eval_const_int(l, tree_right(rg), &rv))
            return false;
         if ((rk == RANGE_TO && lv > rv) || (rk == RANGE_DOWNTO && lv < rv))
            return true;
         const int64_t niters = (rk == RANGE_TO) ? (rv - lv + 1) : (lv - rv + 1);
         if (niters > VHDL_ISLAND_MAX_LOOP_ITERS)
            return false;

         tree_t loop_var = tree_decl(stmt, 0);
         mir_value_t lvar = mir_add_var(mu, vi_i64_type(mu), MIR_NULL_STAMP,
                                        tree_ident(loop_var), MIR_VAR_TEMP);

         if (vi_loop_wants_unroll(l, stmt, niters)) {
            // One copy of the body per pass, straight-line: the parameter
            // and the variables the body assigns are known in each pass
            l->unrolled += niters;
            vi_scope_t us = { .parent = l->scope, .ret_bb = l->scope->ret_bb,
                              .ret_slot = l->scope->ret_slot,
                              .ret_type = l->scope->ret_type,
                              .has_ret_val = l->scope->has_ret_val,
                              .ret_is_vec = l->scope->ret_is_vec,
                              .ret_ptr_indirect = l->scope->ret_ptr_indirect,
                              .ret_signed = l->scope->ret_signed,
                              .ret_w = l->scope->ret_w,
                              .ret_dir = l->scope->ret_dir,
                              .ret_left = l->scope->ret_left,
                              .ret_right = l->scope->ret_right,
                              .loop_next_bb = MIR_NULL_BLOCK,
                              .loop_exit_bb = MIR_NULL_BLOCK,
                              .ret_rec = l->scope->ret_rec };
            vi_local_t ploc = { .decl = loop_var, .storage = lvar,
                                .has_const_int = true };
            APUSH(us.locals, ploc);
            l->scope = &us;
            const int64_t step = (rk == RANGE_TO) ? 1 : -1;
            bool ok = true;
            for (int64_t k = 0; k < niters && ok; k++) {
               const int64_t pv = lv + k * step;
               us.locals.items[0].const_int = pv;
               mir_build_store(mu, lvar, vi_i64(mu, pv));
               vhdl_island_var_defs_clear((vhdl_island_ctx_t *)l->ctx);
               for (int j = 0; j < tree_stmts(stmt) && ok; j++)
                  ok = vi_lower_stmt(l, tree_stmt(stmt, j));
            }
            vhdl_island_var_defs_clear((vhdl_island_ctx_t *)l->ctx);
            l->scope = us.parent;
            vi_scope_clear(&us);
            return ok;
         }

         mir_build_store(mu, lvar, vi_i64(mu, lv));
         // The body runs with values from earlier passes
         vi_const_forget(l, stmt);

         mir_block_t hdr = mir_add_block(mu);
         mir_block_t body = mir_add_block(mu);
         mir_block_t step_bb = mir_add_block(mu);
         mir_block_t exit_bb = mir_add_block(mu);

         vi_scope_t ls = { .parent = l->scope, .ret_bb = l->scope->ret_bb,
                           .ret_slot = l->scope->ret_slot,
                           .ret_type = l->scope->ret_type,
                           .has_ret_val = l->scope->has_ret_val,
                           .ret_is_vec = l->scope->ret_is_vec,
                           .ret_ptr_indirect = l->scope->ret_ptr_indirect,
                           .ret_signed = l->scope->ret_signed,
                           .ret_w = l->scope->ret_w,
                           .ret_dir = l->scope->ret_dir,
                           .ret_left = l->scope->ret_left,
                           .ret_right = l->scope->ret_right,
                           .loop_next_bb = step_bb,
                           .loop_exit_bb = exit_bb,
                           .ret_rec = l->scope->ret_rec };
         vi_local_t loc = { .decl = loop_var, .storage = lvar };
         APUSH(ls.locals, loc);
         l->scope = &ls;

         mir_build_jump(mu, hdr);

         mir_set_cursor(mu, hdr, MIR_APPEND);
         mir_value_t cur = mir_build_load(mu, lvar);
         mir_value_t cond = (rk == RANGE_TO)
            ? mir_build_cmp(mu, MIR_CMP_LEQ, cur, vi_i64(mu, rv))
            : mir_build_cmp(mu, MIR_CMP_GEQ, cur, vi_i64(mu, rv));
         mir_build_cond(mu, cond, body, exit_bb);

         mir_set_cursor(mu, body, MIR_APPEND);
         vhdl_island_var_defs_clear((vhdl_island_ctx_t *)l->ctx);
         for (int j = 0; j < tree_stmts(stmt); j++) {
            unsigned pos = 0;
            if (mir_block_finished(mu, mir_get_cursor(mu, &pos)))
               break;
            if (!vi_lower_stmt(l, tree_stmt(stmt, j))) {
               vhdl_island_var_defs_clear((vhdl_island_ctx_t *)l->ctx);
               l->scope = ls.parent;
               vi_scope_clear(&ls);
               return false;
            }
         }
         vhdl_island_var_defs_clear((vhdl_island_ctx_t *)l->ctx);
         unsigned pos = 0;
         if (!mir_block_finished(mu, mir_get_cursor(mu, &pos)))
            mir_build_jump(mu, step_bb);

         mir_set_cursor(mu, step_bb, MIR_APPEND);
         mir_value_t step = (rk == RANGE_TO) ? vi_i64(mu, 1) : vi_i64(mu, -1);
         mir_build_store(mu, lvar,
                         mir_build_add(mu, vi_i64_type(mu),
                                       mir_build_load(mu, lvar), step));
         mir_build_jump(mu, hdr);

         // A return in the body sets the result of the enclosing function
         l->scope = ls.parent;
         l->scope->has_ret_val |= ls.has_ret_val;
         l->scope->ret_slot = ls.ret_slot;
         l->scope->ret_w = ls.ret_w;
         l->scope->ret_dir = ls.ret_dir;
         l->scope->ret_left = ls.ret_left;
         l->scope->ret_right = ls.ret_right;
         vi_scope_clear(&ls);
         vi_const_forget(l, stmt);
         mir_set_cursor(mu, exit_bb, MIR_APPEND);
         return true;
      }

   default:
      return false;
   }
}

// SUBPROG: the declarations of an inlined procedure, whose variables start
// with their default value; a fused process assigns each variable before it
// reads it (see check_process_decls)
static bool vi_bind_process_locals(vi_lower_t *l, tree_t proc, vi_scope_t *sc,
                                   bool subprog)
{
   mir_unit_t *mu = l->mu;
   const int nd = tree_decls(proc);
   for (int i = 0; i < nd; i++) {
      tree_t d = tree_decl(proc, i);
      if (tree_kind(d) != T_VAR_DECL && tree_kind(d) != T_CONST_DECL)
         continue;
      type_t vt = tree_type(d);
      vi_local_t loc = { .decl = d };
      if (type_is_record(vt)) {
         vi_rec_leaf_list_t leaves = AINIT;
         int rpath[VHDL_ISIG_MAX_DEPTH];
         if (!vi_collect_rec_local_leaves(l, vt, rpath, 0, tree_ident(d), &leaves)) {
            ACLEAR(leaves);
            return false;
         }
         loc.is_rec = true;
         loc.rec_type = vt;
         loc.rec_leaves = leaves.items;
         loc.n_rec_leaves = leaves.count;
         if (tree_has_value(d)
             ? !vi_assign_rec_local(l, &loc, rpath, 0, vt, tree_value(d))
             : (subprog && !vi_init_default_local(l, &loc))) {
            free(loc.rec_leaves);
            return false;
         }
         APUSH(sc->locals, loc);
         continue;
      }
      if (type_is_array(vt)) {
         if (dimension_of(vt) != 1)
            return false;
         type_t elem = type_elem(vt);
         if (type_is_array(elem) && !type_is_record(elem) && dimension_of(elem) == 1) {
            if (!type_is_enum(type_elem(elem)))
               return false;
            loc.is_vec = true;
            loc.is_array2d = true;
            loc.is_std = vi_type_is_std_logic(type_elem(elem));
            loc.is_signed = vi_type_is_signed_vec(elem);
            if (!vi_eval_type_bounds(l, elem, &loc.dir, &loc.left,
                                     &loc.right, &loc.width)
                || !vi_eval_type_bounds(l, vt, &loc.outer_dir, &loc.outer_left,
                                        &loc.outer_right, &loc.nelems)
                || loc.width == 0 || loc.nelems == 0
                || (uint64_t)loc.width * loc.nelems > 4096u)
               return false;
            loc.storage = vi_alloc_array2d_local(l, loc.nelems, loc.width,
                                                 tree_ident(d));
            if (tree_has_value(d)) {
               vi_rec_leaf_t lf = {
                  .is_array2d  = true,
                  .is_vec      = true,
                  .is_std      = loc.is_std,
                  .is_signed   = loc.is_signed,
                  .width       = loc.width,
                  .nelems      = loc.nelems,
                  .dir         = loc.dir,
                  .left        = loc.left,
                  .right       = loc.right,
                  .outer_dir   = loc.outer_dir,
                  .outer_left  = loc.outer_left,
                  .outer_right = loc.outer_right,
                  .storage     = loc.storage,
               };
               if (!vi_assign_array2d_local(l, &lf, tree_value(d)))
                  return false;
            }
            else if (subprog && !vi_init_default_local(l, &loc))
               return false;
            APUSH(sc->locals, loc);
            continue;
         }
         if (type_is_record(elem)) {
            vi_rec_leaf_list_t leaves = AINIT;
            int rpath[VHDL_ISIG_MAX_DEPTH];
            if (!vi_collect_rec_local_leaves(l, vt, rpath, 0, tree_ident(d), &leaves)) {
               ACLEAR(leaves);
               return false;
            }
            loc.is_rec = true;
            loc.rec_type = vt;
            loc.rec_leaves = leaves.items;
            loc.n_rec_leaves = leaves.count;
            if (tree_has_value(d)
                ? !vi_assign_rec_local(l, &loc, rpath, 0, vt, tree_value(d))
                : (subprog && !vi_init_default_local(l, &loc))) {
               free(loc.rec_leaves);
               return false;
            }
            APUSH(sc->locals, loc);
            continue;
         }
         loc.is_vec = true;
         loc.is_std = vi_type_is_std_logic(elem);
         loc.is_signed = vi_type_is_signed_vec(vt);
         if (!vi_eval_type_bounds(l, vt, &loc.dir, &loc.left,
                                  &loc.right, &loc.width))
            return false;
         if (l->two_state)
            loc.storage = vi_add_vec2_var(l, loc.width, loc.is_signed,
                                          tree_ident(d));
         else
            loc.storage = vi_modeb_alloc_vec(l, loc.width);
      }
      else if (vi_type_is_int_or_user_enum(vt)) {
         loc.is_int = true;
         loc.storage = mir_add_var(mu, vi_i64_type(mu), MIR_NULL_STAMP,
                                   tree_ident(d), MIR_VAR_TEMP);
         if (tree_kind(d) == T_CONST_DECL && tree_has_value(d)) {
            int64_t cv = 0;
            if (vi_eval_const_int(l, tree_value(d), &cv)) {
               loc.has_const_int = true;
               loc.const_int = cv;
            }
         }
      }
      else if (type_is_enum(vt)) {
         loc.is_std = vi_type_is_std_logic(vt);
         loc.is_bool = vi_type_is_bool(vt);
         loc.width = 1;
         mir_type_t st = l->two_state
            ? mir_vec2_type(mu, 1, false) : vi_u8_type(mu);
         loc.storage = mir_add_var(mu, st, MIR_NULL_STAMP,
                                   tree_ident(d), MIR_VAR_TEMP);
      }
      else
         return false;

      if (tree_has_value(d)) {
         vi_val_t initv;
         if (!vi_lower_expr(l, tree_value(d), &initv))
            return false;
         if (l->two_state || !loc.is_vec)
            mir_build_store(mu, loc.storage, initv.val);
         else
            mir_build_copy(mu, loc.storage, initv.val, vi_off(mu, loc.width));
      }
      else if (subprog && !vi_init_default_local(l, &loc))
         return false;
      APUSH(sc->locals, loc);
   }
   return true;
}

static void vi_proc_done(vi_lower_t *l, const vhdl_island_ctx_t *ctx,
                         uint32_t pi, bool ok)
{
   vhdl_iproc_t *p = &(((vhdl_island_ctx_t *)ctx)->procs[pi]);
   if (!ok && l->spec)
      p->a_block = l->reason ? l->reason : "spec: unsupported construct";
   else if (!ok)
      p->reason = l->reason ? l->reason : "lower: unsupported construct";
   l->reason = NULL;
}

static void vi_act_count(vi_lower_t *l, uint32_t off, uint32_t n)
{
   mir_unit_t *mu = l->mu;
   mir_type_t t_v64 = mir_vec2_type(mu, 64, false);
   mir_value_t cur = mir_build_island_load(mu, t_v64, l->state_ptr,
                                           vi_off(mu, off), vi_off(mu, -1),
                                           vi_off(mu, 0));
   mir_value_t upd = mir_build_add(mu, vi_i64_type(mu),
                                   mir_build_cast(mu, vi_i64_type(mu), cur),
                                   vi_i64(mu, n));
   mir_build_island_store(mu, l->state_ptr, vi_off(mu, off), vi_off(mu, -1),
                          vi_off(mu, 0), mir_build_cast(mu, t_v64, upd));
}

static bool vi_act_add_read(vi_lower_t *l, uint32_t idx)
{
   if (!(l->isl->sigs[idx].rt_flags & RT_ISIG_ACT))
      return false;
   for (uint32_t i = 0; i < l->n_act_rd; i++) {
      if (l->act_rd[i] == idx)
         return true;
   }
   if (l->n_act_rd == l->cap_act_rd) {
      l->cap_act_rd = MAX(l->cap_act_rd * 2u, 16u);
      l->act_rd = xrealloc_array(l->act_rd, l->cap_act_rd, sizeof(uint32_t));
   }
   l->act_rd[l->n_act_rd++] = idx;
   return true;
}

// Branch to BODY_BB when the run-all byte or the change byte of a signal
// the process reads is set.  The read set is the union of what the
// analysis found and what the lowering of the body loaded; a process that
// reads a signal without a change byte always runs.
// A process that runs only when its sensitivity list changed, see
// vi_emit_report_guard.  A Mode A variant has no metavalue warnings
// (vhdl_island_new_a_variant keeps report processes out of it).
static bool vi_proc_guarded(const vi_lower_t *l, const vhdl_iproc_t *p)
{
   return p->has_report || (p->has_warn && !l->two_state);
}

static void vi_emit_act_test(vi_lower_t *l, const vhdl_iproc_t *p,
                             mir_block_t body_bb, mir_block_t next_bb)
{
   mir_unit_t *mu = l->mu;
   const vhdl_island_t *isl = l->isl;

   bool always = false;
   for (uint32_t i = 0; i < l->n_act_rd && !always; i++)
      always = !(isl->sigs[l->act_rd[i]].rt_flags & RT_ISIG_ACT);
   for (uint32_t i = 0; i < p->n_reads && !always; i++) {
      const vhdl_isl_sig_t *isig = vhdl_island_find_sig(isl, p->reads[i]);
      always = isig == NULL
         || !vi_act_add_read(l, (uint32_t)(isig - isl->sigs));
   }
   // The sensitivity list of a guarded process may name signals that it
   // does not read (vi_emit_report_guard).  So may the one of a process
   // that may warn in a speculative Mode A variant: an event on such a
   // signal must run it there too, so that a metavalue it loads deopts
   // and Mode B runs it with its guard and warns.
   const bool evt = vi_proc_guarded(l, p) || (l->spec && p->has_warn);
   for (uint32_t i = 0; evt && i < p->n_evt_sigs && !always; i++) {
      const vhdl_isl_sig_t *isig = vhdl_island_find_sig(isl, p->evt_sigs[i]);
      always = isig == NULL
         || !vi_act_add_read(l, (uint32_t)(isig - isl->sigs));
   }

   if (always) {
      mir_build_jump(mu, body_bb);
      return;
   }

   mir_value_t zero = vi_u8(mu, 0);
   mir_value_t any = mir_build_cmp(
      mu, MIR_CMP_NEQ,
      mir_build_load(mu, vi_state_ptr_at(l, RT_ISLAND_ACT_ALL(isl->n_sigs))),
      zero);
   for (uint32_t i = 0; i < l->n_act_rd; i++) {
      mir_value_t chg = mir_build_load(mu, vi_state_ptr_at(l, l->act_rd[i]));
      any = mir_build_or(mu, any, mir_build_cmp(mu, MIR_CMP_NEQ, chg, zero));
   }
   mir_build_cond(mu, any, body_bb, next_bb);
}

// Speculative Mode A (vi_lower_spec): SPEC 1 is the variant that assumes
// no held signal or row ("$a"), 2 the one that tests the held and row clean
// bytes ("$ah"), which the runtime calls while some are set
static ident_t vi_spec_name(ident_t name, int spec)
{
   return spec == 2 ? ident_sprintf("%sh", istr(name)) : name;
}

// Bytes of island state holding signal S in the layout being lowered
static uint32_t vi_sig_state_bytes(const vi_lower_t *l, const vhdl_isig_t *s)
{
   const bool packed = l->two_state
      ? (s->elem_kind != VISIG_ENUM && s->elem_kind != VISIG_INT)
      : s->two_state;
   if (packed)
      return (s->width + 63u) / 64u * s->nelems * 8u;
   return s->width * s->nelems * s->byte_size;
}

// Largest copy of self-read signals one process may take on every run
#define VI_MAX_SHADOW_BYTES 4096

// A VHDL process that reads a signal it also assigns sees the value of the
// current delta cycle, not the one it assigned.  Island signal assignments
// store straight into the state, so copy the signals process P both reads
// and writes when it starts and read them from the copy.  A changed value
// makes the island run again in the next delta (island_check_feedback in
// rt/island.c), where P sees it, as with the process unfused.
static void vi_add_shadow(vi_lower_t *l, const vhdl_isl_sig_t *isig,
                          mir_value_t copy)
{
   l->shadow_sig = xrealloc_array(l->shadow_sig, l->n_shadow + 1,
                                  sizeof(const vhdl_isl_sig_t *));
   l->shadow_ptr = xrealloc_array(l->shadow_ptr, l->n_shadow + 1,
                                  sizeof(mir_value_t));
   l->shadow_sig[l->n_shadow] = isig;
   l->shadow_ptr[l->n_shadow] = copy;
   l->n_shadow++;
}

static bool vi_emit_self_shadow(vi_lower_t *l, const vhdl_iproc_t *p)
{
   l->n_shadow = 0;
   uint32_t total = 0;
   for (uint32_t i = 0; i < p->n_reads; i++) {
      const vhdl_isl_sig_t *isig = vhdl_island_find_sig(l->isl, p->reads[i]);
      bool in_entry = false;
      for (uint32_t j = 0; j < l->n_entry && !in_entry; j++) {
         if (l->entry_sig[j] == isig) {
            vi_add_shadow(l, isig, l->entry_ptr[j]);
            in_entry = true;
         }
      }
      if (in_entry)
         continue;

      bool written = false;
      for (uint32_t j = 0; j < p->n_writes && !written; j++)
         written = p->writes[j] == p->reads[i];
      if (!written)
         continue;

      const vhdl_isig_t *s = &(l->ctx->sigs[p->reads[i]]);
      if (isig == NULL || s->is_sparse_ram) {
         l->reason = "comb: process reads a sparse RAM it also writes";
         return false;
      }

      const uint32_t bytes = vi_sig_state_bytes(l, s);
      total += bytes;
      if (total > VI_MAX_SHADOW_BYTES) {
         l->reason = "comb: signals the process reads and writes are too large";
         return false;
      }

      mir_unit_t *mu = l->mu;
      mir_value_t copy = vi_modeb_alloc_vec(l, ALIGN_UP(bytes, 8u));
      mir_build_copy(mu, copy, vi_state_ptr_at(l, isig->a_off),
                     vi_off(mu, bytes));
      vi_add_shadow(l, isig, copy);
   }
   return true;
}

// A combinational process that may warn runs in Mode B only when a signal
// of its sensitivity list changed (vi_emit_report_guard).  When its
// speculative Mode A variant deopts inside the body, the Mode B call that
// replaces it decides with that guard, so the signals the process already
// assigned must get back their values: copy them when it starts, and a
// deopt stub stores the copies back (vi_emit_deopt_return).  Only while
// its run flag is set: with the flag clear (after a run in Mode A, or when
// Mode B runs it without the guard) Mode B runs it anyway.
static bool vi_emit_restore_copies(vi_lower_t *l, const vhdl_iproc_t *p)
{
   mir_unit_t *mu = l->mu;
   l->n_restore = 0;
   uint32_t total = 0;
   for (uint32_t j = 0; j < p->n_writes; j++) {
      const vhdl_isl_sig_t *isig = vhdl_island_find_sig(l->isl, p->writes[j]);
      if (isig == NULL)
         continue;
      const vhdl_isig_t *s = &(l->ctx->sigs[p->writes[j]]);
      const uint32_t bytes = vi_sig_state_bytes(l, s);
      total += bytes;
      if (s->is_sparse_ram || total > VI_MAX_SHADOW_BYTES) {
         l->reason = "spec: signals a process that may warn writes are too "
            "large to copy";
         return false;
      }

      const uint32_t n = l->n_restore + 1;
      l->restore_sig = xrealloc_array(l->restore_sig, n,
                                      sizeof(const vhdl_isl_sig_t *));
      l->restore_ptr = xrealloc_array(l->restore_ptr, n, sizeof(mir_value_t));
      l->restore_bytes = xrealloc_array(l->restore_bytes, n,
                                        sizeof(uint32_t));
      l->restore_sig[l->n_restore] = isig;
      l->restore_ptr[l->n_restore] = vi_modeb_alloc_vec(l, ALIGN_UP(bytes, 8u));
      l->restore_bytes[l->n_restore] = bytes;
      l->n_restore = n;
   }
   if (l->n_restore == 0)
      return true;

   mir_value_t ran = mir_build_load(mu, vi_state_ptr_at(l, p->evt_flag_off));
   l->restore_cond = mir_build_cmp(mu, MIR_CMP_NEQ, ran, vi_u8(mu, 0));
   mir_block_t copy_bb = mir_add_block(mu);
   mir_block_t join_bb = mir_add_block(mu);
   mir_build_cond(mu, l->restore_cond, copy_bb, join_bb);
   mir_set_cursor(mu, copy_bb, MIR_APPEND);
   for (uint32_t i = 0; i < l->n_restore; i++)
      mir_build_copy(mu, l->restore_ptr[i],
                     vi_state_ptr_at(l, l->restore_sig[i]->a_off),
                     vi_off(mu, l->restore_bytes[i]));
   mir_build_jump(mu, join_bb);
   mir_set_cursor(mu, join_bb, MIR_APPEND);
   return true;
}

// Processes run readers first, so outside a combinational loop no process
// runs after the writer of a signal it reads.  Inside a loop some do: copy
// those signals when the island function starts, the values of the current
// delta cycle, and read them from the copy.
static bool vi_emit_entry_shadow(vi_lower_t *l, int *failed_proc_out)
{
   const vhdl_island_t *isl = l->isl;
   bool *written LOCAL = xcalloc_array(MAX(1u, isl->n_sigs), sizeof(bool));
   bool *need LOCAL = xcalloc_array(MAX(1u, isl->n_sigs), sizeof(bool));
   uint32_t total = 0;
   for (uint32_t k = isl->n_comb_procs; k-- > 0; ) {
      const vhdl_iproc_t *p = &(l->ctx->procs[isl->comb_procs[k]]);
      for (uint32_t i = 0; i < p->n_reads; i++) {
         const vhdl_isl_sig_t *isig = vhdl_island_find_sig(isl, p->reads[i]);
         if (isig == NULL)
            continue;
         const uint32_t idx = (uint32_t)(isig - isl->sigs);
         if (!written[idx] || need[idx])
            continue;
         const vhdl_isig_t *s = &(l->ctx->sigs[p->reads[i]]);
         total += vi_sig_state_bytes(l, s);
         if (s->is_sparse_ram || total > VI_MAX_SHADOW_BYTES) {
            l->reason = "comb: loop through signals too large to copy";
            *failed_proc_out = (int)isl->comb_procs[k];
            return false;
         }
         need[idx] = true;
      }
      for (uint32_t j = 0; j < p->n_writes; j++) {
         const vhdl_isl_sig_t *isig = vhdl_island_find_sig(isl, p->writes[j]);
         if (isig != NULL)
            written[isig - isl->sigs] = true;
      }
   }

   mir_unit_t *mu = l->mu;
   for (uint32_t j = 0; j < isl->n_sigs; j++) {
      if (!need[j])
         continue;
      const vhdl_isl_sig_t *isig = &(isl->sigs[j]);
      const uint32_t bytes =
         vi_sig_state_bytes(l, &(l->ctx->sigs[isig->sig_id]));
      mir_value_t copy = vi_modeb_alloc_vec(l, ALIGN_UP(bytes, 8u));
      mir_build_copy(mu, copy, vi_state_ptr_at(l, isig->a_off),
                     vi_off(mu, bytes));
      l->entry_sig = xrealloc_array(l->entry_sig, l->n_entry + 1,
                                    sizeof(const vhdl_isl_sig_t *));
      l->entry_ptr = xrealloc_array(l->entry_ptr, l->n_entry + 1,
                                    sizeof(mir_value_t));
      l->entry_sig[l->n_entry] = isig;
      l->entry_ptr[l->n_entry] = copy;
      l->n_entry++;
   }
   return true;
}

// OR into ANY whether the NWORDS 64-bit words at A_OFF of A_BASE and B_OFF
// of the state differ, and copy them to B_OFF
static mir_value_t vi_emit_words_differ_at(vi_lower_t *l, mir_value_t any,
                                           mir_value_t a_base, uint32_t a_off,
                                           uint32_t b_off, uint32_t nwords)
{
   mir_unit_t *mu = l->mu;
   mir_type_t t_v64 = mir_vec2_type(mu, 64, false);

   if (nwords <= 4) {
      for (uint32_t w = 0; w < nwords; w++) {
         mir_value_t a = mir_build_island_load(mu, t_v64, a_base,
                                               vi_off(mu, a_off + 8u * w),
                                               vi_off(mu, -1), vi_off(mu, 0));
         mir_value_t b = mir_build_island_load(mu, t_v64, l->state_ptr,
                                               vi_off(mu, b_off + 8u * w),
                                               vi_off(mu, -1), vi_off(mu, 0));
         any = mir_build_or(mu, any, mir_build_cmp(mu, MIR_CMP_NEQ, a, b));
         mir_build_island_store(mu, l->state_ptr, vi_off(mu, b_off + 8u * w),
                                vi_off(mu, -1), vi_off(mu, 0), a);
      }
      return any;
   }

   mir_type_t t_off = mir_offset_type(mu);
   mir_value_t idx_v = mir_add_var(mu, t_off, MIR_NULL_STAMP,
                                   ident_uniq("evw"), MIR_VAR_TEMP);
   mir_value_t any_v = mir_add_var(mu, mir_bool_type(mu), MIR_NULL_STAMP,
                                   ident_uniq("evd"), MIR_VAR_TEMP);
   mir_build_store(mu, idx_v, vi_off(mu, 0));
   mir_build_store(mu, any_v, any);

   mir_block_t hdr = mir_add_block(mu);
   mir_block_t body = mir_add_block(mu);
   mir_block_t done = mir_add_block(mu);
   mir_build_jump(mu, hdr);

   mir_set_cursor(mu, hdr, MIR_APPEND);
   mir_value_t i = mir_build_load(mu, idx_v);
   mir_build_cond(mu, mir_build_cmp(mu, MIR_CMP_LT, i, vi_off(mu, nwords)),
                  body, done);

   mir_set_cursor(mu, body, MIR_APPEND);
   mir_value_t a = mir_build_island_load(mu, t_v64, a_base,
                                         vi_off(mu, a_off), vi_off(mu, -1), i);
   mir_value_t b = mir_build_island_load(mu, t_v64, l->state_ptr,
                                         vi_off(mu, b_off), vi_off(mu, -1), i);
   mir_build_store(mu, any_v,
                   mir_build_or(mu, mir_build_load(mu, any_v),
                                mir_build_cmp(mu, MIR_CMP_NEQ, a, b)));
   mir_build_island_store(mu, l->state_ptr, vi_off(mu, b_off), vi_off(mu, -1),
                          i, a);
   mir_build_store(mu, idx_v, mir_build_add(mu, t_off, i, vi_off(mu, 1)));
   mir_build_jump(mu, hdr);

   mir_set_cursor(mu, done, MIR_APPEND);
   return mir_build_load(mu, any_v);
}

// OR into ANY whether the NBYTES bytes at A_OFF of A_BASE and B_OFF of the
// state differ, for a long slice that does not start on a word, and copy
// them to B_OFF
static mir_value_t vi_emit_bytes_differ_at(vi_lower_t *l, mir_value_t any,
                                           mir_value_t a_base, uint32_t a_off,
                                           uint32_t b_off, uint32_t nbytes)
{
   mir_unit_t *mu = l->mu;
   mir_type_t t_off = mir_offset_type(mu);
   mir_value_t idx_v = mir_add_var(mu, t_off, MIR_NULL_STAMP,
                                   ident_uniq("evb"), MIR_VAR_TEMP);
   mir_value_t any_v = mir_add_var(mu, mir_bool_type(mu), MIR_NULL_STAMP,
                                   ident_uniq("evd"), MIR_VAR_TEMP);
   mir_build_store(mu, idx_v, vi_off(mu, 0));
   mir_build_store(mu, any_v, any);

   mir_block_t hdr = mir_add_block(mu);
   mir_block_t body = mir_add_block(mu);
   mir_block_t done = mir_add_block(mu);
   mir_build_jump(mu, hdr);

   mir_set_cursor(mu, hdr, MIR_APPEND);
   mir_value_t i = mir_build_load(mu, idx_v);
   mir_build_cond(mu, mir_build_cmp(mu, MIR_CMP_LT, i, vi_off(mu, nbytes)),
                  body, done);

   mir_set_cursor(mu, body, MIR_APPEND);
   mir_value_t a = mir_build_load(
      mu, mir_build_array_ref(mu, a_base,
                              mir_build_add(mu, t_off, vi_off(mu, a_off), i)));
   mir_value_t b_ptr = mir_build_array_ref(
      mu, l->state_ptr, mir_build_add(mu, t_off, vi_off(mu, b_off), i));
   mir_value_t b = mir_build_load(mu, b_ptr);
   mir_build_store(mu, any_v,
                   mir_build_or(mu, mir_build_load(mu, any_v),
                                mir_build_cmp(mu, MIR_CMP_NEQ, a, b)));
   mir_build_store(mu, b_ptr, a);
   mir_build_store(mu, idx_v, mir_build_add(mu, t_off, i, vi_off(mu, 1)));
   mir_build_jump(mu, hdr);

   mir_set_cursor(mu, done, MIR_APPEND);
   return mir_build_load(mu, any_v);
}

// True when the change byte of ISIG is set or every signal counts as
// changed (RT_ISLAND_ACT_ALL), or the null value if ISIG has no change byte
static mir_value_t vi_guard_changed(vi_lower_t *l, const vhdl_isl_sig_t *isig)
{
   if (!l->isl->has_act || !(isig->rt_flags & RT_ISIG_ACT))
      return MIR_NULL_VALUE;
   mir_unit_t *mu = l->mu;
   const uint32_t idx = (uint32_t)(isig - l->isl->sigs);
   mir_value_t chg = mir_build_load(mu, vi_state_ptr_at(l, idx));
   mir_value_t all = mir_build_load(
      mu, vi_state_ptr_at(l, RT_ISLAND_ACT_ALL(l->isl->n_sigs)));
   return mir_build_or(mu, mir_build_cmp(mu, MIR_CMP_NEQ, chg, vi_u8(mu, 0)),
                       mir_build_cmp(mu, MIR_CMP_NEQ, all, vi_u8(mu, 0)));
}

// Signals of a sensitivity list compared under one test of their change
// bytes (vi_emit_report_guard), and the length above which a signal has a
// test of its own
#define VI_GUARD_GROUP      8
#define VI_GUARD_GATE_BYTES 16

// Where the process reads ISIG from (at byte PART_OFF): a signal that a
// process inside a combinational loop may already have assigned in this
// call is read from its copy taken when the island function started (see
// vi_emit_entry_shadow), which holds the value of the current delta cycle
static mir_value_t vi_guard_base(vi_lower_t *l, const vhdl_isl_sig_t *isig,
                                 uint32_t part_off, uint32_t *off)
{
   for (uint32_t j = 0; j < l->n_entry; j++) {
      if (l->entry_sig[j] == isig) {
         *off = part_off;
         return l->entry_ptr[j];
      }
   }
   *off = isig->a_off + part_off;
   return l->state_ptr;
}

// OR into ANY whether sensitivity signal I of P differs from its copy, and
// update the copy
static mir_value_t vi_emit_guard_cmp(vi_lower_t *l, const vhdl_iproc_t *p,
                                     uint32_t i, mir_value_t any)
{
   mir_unit_t *mu = l->mu;
   const vhdl_isl_sig_t *isig = vhdl_island_find_sig(l->isl, p->evt_sigs[i]);
   const uint32_t nbytes = p->evt_copy_bytes[i];
   uint32_t src;
   mir_value_t base = vi_guard_base(l, isig, p->evt_part_off[i], &src);

   if (src % 8 == 0 && nbytes % 8 == 0)
      return vi_emit_words_differ_at(l, any, base, src, p->evt_copy_off[i],
                                     nbytes / 8);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
   else if (nbytes <= 64 && nbytes > 2) {
      // A slice that does not start or end on a word: compare and copy
      // it a word at a time, each word assembled from the one or two
      // words that hold its bytes.  Every word loaded holds a byte of the
      // slice, so it lies inside the state or the entry copy, both of
      // which end on a word
      mir_type_t t_v64 = mir_vec2_type(mu, 64, false);
      const uint32_t sh = (src % 8) * 8;
      for (uint32_t c = 0; c * 8 < nbytes; c++) {
         const uint32_t w = src / 8 + c;
         const uint32_t len = MIN(8u, nbytes - c * 8);
         mir_value_t a = mir_build_island_load(mu, t_v64, base,
                                               vi_off(mu, 8u * w),
                                               vi_off(mu, -1), vi_off(mu, 0));
         if (sh > 0) {
            a = mir_build_binary(mu, MIR_VEC_SRL, t_v64, a,
                                 mir_const_vec(mu, t_v64, sh, 0));
            if (src % 8 + len > 8) {
               mir_value_t hi = mir_build_island_load(
                  mu, t_v64, base, vi_off(mu, 8u * (w + 1)), vi_off(mu, -1),
                  vi_off(mu, 0));
               hi = mir_build_binary(mu, MIR_VEC_SLL, t_v64, hi,
                                     mir_const_vec(mu, t_v64, 64 - sh, 0));
               a = mir_build_binary(mu, MIR_VEC_BIT_OR, t_v64, a, hi);
            }
         }
         if (len < 8) {
            const uint64_t mask = (UINT64_C(1) << (8 * len)) - 1;
            a = mir_build_binary(mu, MIR_VEC_BIT_AND, t_v64, a,
                                 mir_const_vec(mu, t_v64, mask, 0));
         }
         const uint32_t b_off = p->evt_copy_off[i] + 8u * c;
         mir_value_t b = mir_build_island_load(mu, t_v64, l->state_ptr,
                                               vi_off(mu, b_off),
                                               vi_off(mu, -1), vi_off(mu, 0));
         any = mir_build_or(mu, any, mir_build_cmp(mu, MIR_CMP_NEQ, a, b));
         mir_build_island_store(mu, l->state_ptr, vi_off(mu, b_off),
                                vi_off(mu, -1), vi_off(mu, 0), a);
      }
      return any;
   }
#endif
   else if (nbytes <= 64) {
      // An element or a short slice
      for (uint32_t k = 0; k < nbytes; k++) {
         mir_value_t a = mir_build_load(
            mu, mir_build_array_ref(mu, base, vi_off(mu, src + k)));
         mir_value_t b_ptr = vi_state_ptr_at(l, p->evt_copy_off[i] + k);
         mir_value_t b = mir_build_load(mu, b_ptr);
         any = mir_build_or(mu, any, mir_build_cmp(mu, MIR_CMP_NEQ, a, b));
         mir_build_store(mu, b_ptr, a);
      }
      return any;
   }
   else
      return vi_emit_bytes_differ_at(l, any, base, src, p->evt_copy_off[i],
                                     nbytes);
}

// OR into ANY whether one of the N sensitivity signals GROUP of P, which
// have change bytes, differs from its copy, comparing them only when one
// of their change bytes is set or FIRST
static mir_value_t vi_emit_guard_group(vi_lower_t *l, const vhdl_iproc_t *p,
                                       const uint32_t *group, uint32_t n_group,
                                       mir_value_t first, mir_value_t any,
                                       mir_value_t *any_v)
{
   mir_unit_t *mu = l->mu;

   uint32_t nbytes = 0;
   for (uint32_t k = 0; k < n_group; k++)
      nbytes += p->evt_copy_bytes[group[k]];
   if (nbytes <= VI_GUARD_GATE_BYTES) {
      // Comparing a word or two costs less than a test that may mispredict
      for (uint32_t k = 0; k < n_group; k++)
         any = vi_emit_guard_cmp(l, p, group[k], any);
      return any;
   }

   // A signal whose change byte is clear has not changed since the
   // previous call, so its copy still holds that value
   mir_value_t zero = vi_u8(mu, 0);
   mir_value_t cond = mir_build_or(
      mu, first,   // No copy taken yet
      mir_build_cmp(mu, MIR_CMP_NEQ, mir_build_load(
         mu, vi_state_ptr_at(l, RT_ISLAND_ACT_ALL(l->isl->n_sigs))), zero));
   for (uint32_t k = 0; k < n_group; k++) {
      const vhdl_isl_sig_t *gs =
         vhdl_island_find_sig(l->isl, p->evt_sigs[group[k]]);
      const uint32_t idx = (uint32_t)(gs - l->isl->sigs);
      mir_value_t chg = mir_build_load(mu, vi_state_ptr_at(l, idx));
      cond = mir_build_or(mu, cond, mir_build_cmp(mu, MIR_CMP_NEQ, chg, zero));
   }

   if (mir_is_null(*any_v))
      *any_v = mir_add_var(mu, mir_bool_type(mu), MIR_NULL_STAMP,
                           ident_uniq("evany"), MIR_VAR_TEMP);
   mir_build_store(mu, *any_v, any);
   mir_block_t cmp_bb = mir_add_block(mu);
   mir_block_t next_bb = mir_add_block(mu);
   mir_build_cond(mu, cond, cmp_bb, next_bb);
   mir_set_cursor(mu, cmp_bb, MIR_APPEND);

   for (uint32_t k = 0; k < n_group; k++)
      any = vi_emit_guard_cmp(l, p, group[k], any);

   mir_build_store(mu, *any_v, any);
   mir_build_jump(mu, next_bb);
   mir_set_cursor(mu, next_bb, MIR_APPEND);
   return mir_build_load(mu, *any_v);
}

// A combinational process with report or assert statements, or calls that
// may warn, must run exactly when it would run unfused: on its first call
// and after a signal of its sensitivity list changed.  The change bytes are
// not enough for that (an input sync marks its signal without comparing,
// and forces or the first call mark every signal), so compare each signal
// with its copy, then branch to SKIP_BB or fall through to the process.
// Each compared signal is copied in the same pass whether the process runs
// or not: the copies always hold the values of the previous call, which are
// the values of the last run unless one of them changed, and a signal that
// is not compared has not changed since the previous call.
static bool vi_emit_report_guard(vi_lower_t *l, const vhdl_iproc_t *p,
                                 mir_block_t skip_bb)
{
   mir_unit_t *mu = l->mu;

   mir_block_t body_bb = MIR_NULL_BLOCK;
   if (!p->has_report && p->warn_ieee_only && l->isl->has_act) {
      // The process can only warn through IEEE package warnings: when they
      // are off for the whole run, running it more often than unfused does
      // not change its outputs, and the activity test is enough
      body_bb = mir_add_block(mu);
      mir_block_t guard_bb = mir_add_block(mu);
      mir_value_t nowarn = mir_build_load(
         mu, vi_state_ptr_at(l, RT_ISLAND_ACT_NOWARN(l->isl->n_sigs)));
      mir_build_cond(mu, mir_build_cmp(mu, MIR_CMP_NEQ, nowarn, vi_u8(mu, 0)),
                     body_bb, guard_bb);
      mir_set_cursor(mu, guard_bb, MIR_APPEND);
   }

   mir_value_t ran = mir_build_load(mu, vi_state_ptr_at(l, p->evt_flag_off));
   mir_value_t first = mir_build_cmp(mu, MIR_CMP_EQ, ran, vi_u8(mu, 0));
   mir_value_t any = first, any_v = MIR_NULL_VALUE;
   // Signals with a change byte are compared in groups under one test of
   // their change bytes
   uint32_t group[VI_GUARD_GROUP];
   uint32_t n_group = 0;
   for (uint32_t i = 0; i <= p->n_evt_sigs; i++) {
      if (i < p->n_evt_sigs) {
         const vhdl_isl_sig_t *isig =
            vhdl_island_find_sig(l->isl, p->evt_sigs[i]);
         const uint32_t nbytes = p->evt_copy_bytes[i];
         if (isig == NULL && !p->has_report) {
            // Read only by a branch that is never taken and by no other
            // process of the island: the island does not see its events
            l->reason = "comb: process that may warn is sensitive to a "
               "signal outside its island";
            return false;
         }
         if (isig != NULL && !p->has_report && nbytes == 0) {
            // A process that only calls functions that may warn reads a
            // sparse RAM, which is not copied: run it when the change byte
            // of the signal is set (an input sync may set it without a
            // change), or always without one
            mir_value_t chg = vi_guard_changed(l, isig);
            if (mir_is_null(chg))
               any = mir_const(mu, mir_bool_type(mu), 1);
            else
               any = mir_build_or(mu, any, chg);
            continue;
         }
         if (isig == NULL || nbytes == 0) {
            l->reason = "comb: report process sensitive to a sparse RAM";
            return false;
         }
         if (!l->isl->has_act || !(isig->rt_flags & RT_ISIG_ACT)) {
            any = vi_emit_guard_cmp(l, p, i, any);
            continue;
         }
         if (nbytes > VI_GUARD_GATE_BYTES) {
            // A long signal is compared on its own change byte
            any = vi_emit_guard_group(l, p, &i, 1, first, any, &any_v);
            continue;
         }
         group[n_group++] = i;
         if (n_group < VI_GUARD_GROUP)
            continue;
      }
      if (n_group == 0)
         continue;

      any = vi_emit_guard_group(l, p, group, n_group, first, any, &any_v);
      n_group = 0;
   }

   mir_block_t run_bb = mir_add_block(mu);
   mir_build_cond(mu, any, run_bb, skip_bb);

   mir_set_cursor(mu, run_bb, MIR_APPEND);
   mir_build_store(mu, vi_state_ptr_at(l, p->evt_flag_off), vi_u8(mu, 1));
   if (!mir_is_null(body_bb)) {
      mir_build_jump(mu, body_bb);
      mir_set_cursor(mu, body_bb, MIR_APPEND);
   }
   return true;
}

// NAME, when not NULL, replaces the function name (vi_lazy_build)
static mir_unit_t *vi_build_comb_unit(mir_context_t *mc,
                                      const vhdl_island_ctx_t *ctx,
                                      const vhdl_island_t *isl, int spec,
                                      int *failed_proc_out, ident_t name)
{
   mir_unit_t *mu = mir_unit_new(mc,
                                 name ?: vi_spec_name(isl->eval_name, spec),
                                 tree_to_object(ctx->block),
                                 MIR_UNIT_FUNCTION, NULL);
   mir_type_t t_off = mir_offset_type(mu);
   mir_type_t t_ptr = mir_pointer_type(mu, vi_u8_type(mu));
   mir_set_result(mu, t_off);
   mir_add_param(mu, t_ptr, MIR_NULL_STAMP, ident_new("context"));
   mir_value_t state = mir_add_param(mu, t_ptr, MIR_NULL_STAMP, ident_new("state"));

   vi_lower_t l = {
      .mu        = mu,
      .ctx       = ctx,
      .isl       = isl,
      .state_ptr = state,
      .two_state = isl->two_state,
      .spec      = spec != 0,
      .spec_checks = spec == 2,
      .spec_var  = MIR_NULL_VALUE,
      .is_ff     = false,
      .lut_not   = MIR_NULL_VALUE,
      .lut_and   = MIR_NULL_VALUE,
      .lut_or    = MIR_NULL_VALUE,
      .lut_xor   = MIR_NULL_VALUE,
   };

   // With process activity (isl->has_act) each process is guarded by a
   // test of the change bytes of the island signals it reads, which the
   // runtime sets before every call (island_act_prepare in rt/island.c), so
   // a delta cycle only runs the processes whose inputs changed.
   const bool act = isl->has_act;
   const bool act_stats = act && getenv("NVC_ISLAND_ACT_STATS") != NULL;
   const uint32_t act_cnt = RT_ISLAND_ACT_CNT(isl->n_sigs);
   vi_unit_open(&l);
   if (act_stats)
      vi_act_count(&l, act_cnt + 8u, isl->n_comb_procs);

   if (!vi_emit_entry_shadow(&l, failed_proc_out)) {
      vi_proc_done(&l, ctx, (uint32_t)*failed_proc_out, false);
      free(l.entry_sig);
      free(l.entry_ptr);
      mir_unit_free(mu);
      return NULL;
   }

   // Readers before writers: comb_procs is in topological order, so going
   // backwards every process reads the signal values of the current delta
   // cycle, as a VHDL signal assignment only takes effect in the next one.
   // The runtime runs the island again when such a signal changed (see
   // island_check_feedback in rt/island.c).
   for (uint32_t k = isl->n_comb_procs; k-- > 0; ) {
      uint32_t pi = isl->comb_procs[k];
      const vhdl_iproc_t *p = &(ctx->procs[pi]);
      vhdl_island_var_defs_clear((vhdl_island_ctx_t *)ctx);
      vhdl_island_set_eval_scope((vhdl_island_ctx_t *)ctx, p->owner_block,
                                 p->owner_ancestors, p->n_owner_ancestors);
      vi_scope_t sc = { .ret_bb = MIR_NULL_BLOCK };
      l.scope = &sc;

      mir_block_t test_bb = MIR_NULL_BLOCK, body_bb = MIR_NULL_BLOCK;
      mir_block_t next_bb = MIR_NULL_BLOCK;
      const bool guard = vi_proc_guarded(&l, p);
      if (act || guard)
         next_bb = mir_add_block(mu);
      if (act) {
         test_bb = mir_add_block(mu);
         body_bb = mir_add_block(mu);
         mir_build_jump(mu, test_bb);
         mir_set_cursor(mu, body_bb, MIR_APPEND);

         l.n_act_rd = 0;
         l.act_track = true;
      }

      const bool restore = !guard && l.spec && p->has_warn;
      bool ok = (!guard || vi_emit_report_guard(&l, p, next_bb))
         && vi_emit_self_shadow(&l, p)
         && (!restore || vi_emit_restore_copies(&l, p))
         && vi_bind_process_locals(&l, p->stmt, &sc, false);
      const int ns = tree_stmts(p->stmt) - 1;
      for (int i = 0; i < ns && ok; i++)
         ok = vi_lower_stmt(&l, tree_stmt(p->stmt, i));
      l.n_restore = 0;

      if (ok && !l.failed && !guard && l.spec && p->has_warn) {
         // A speculative Mode A variant runs the process without its Mode
         // B guard and copies: clear the run flag so that Mode B runs it
         // and takes new copies on its next call (vi_emit_report_guard).
         // Only once the process completed: on a deopt in its body the
         // Mode B call that replaces this one must decide with the flag
         // and copies of the last run, or it runs the process on a
         // non-event with the metavalue that caused the deopt and warns.
         mir_build_store(mu, vi_state_ptr_at(&l, p->evt_flag_off),
                         vi_u8(mu, 0));
      }

      l.n_shadow = 0;
      l.act_track = false;
      vi_scope_clear(&sc);
      l.scope = NULL;

      vi_proc_done(&l, ctx, pi, ok && !l.failed);
      vhdl_island_set_eval_scope((vhdl_island_ctx_t *)ctx, ctx->block,
                                 ctx->ancestors, ctx->n_ancestors);
      if (!ok || l.failed) {
         *failed_proc_out = (int)pi;
         free(l.act_rd);
         free(l.shadow_sig);
         free(l.shadow_ptr);
         free(l.entry_sig);
         free(l.entry_ptr);
         free(l.restore_sig);
         free(l.restore_ptr);
         free(l.restore_bytes);
         mir_unit_free(mu);
         return NULL;
      }

      if (act) {
         if (act_stats)
            vi_act_count(&l, act_cnt, 1);
         mir_build_jump(mu, next_bb);
         mir_set_cursor(mu, test_bb, MIR_APPEND);
         vi_emit_act_test(&l, p, body_bb, next_bb);
         mir_set_cursor(mu, next_bb, MIR_APPEND);
      }
      else if (guard) {
         mir_build_jump(mu, next_bb);
         mir_set_cursor(mu, next_bb, MIR_APPEND);
      }
   }

   free(l.act_rd);
   free(l.shadow_sig);
   free(l.shadow_ptr);
   free(l.entry_sig);
   free(l.entry_ptr);
   free(l.restore_sig);
   free(l.restore_ptr);
   free(l.restore_bytes);
   mir_build_return(mu, vi_off(mu, 0));
   vi_unit_close(&l);
   return mu;
}

static mir_unit_t *vi_build_ff_unit(mir_context_t *mc,
                                    const vhdl_island_ctx_t *ctx,
                                    const vhdl_island_t *isl,
                                    const vhdl_idom_t *dm, int spec,
                                    int *failed_proc_out, ident_t name)
{
   mir_unit_t *mu = mir_unit_new(mc, name ?: vi_spec_name(dm->fn_name, spec),
                                 tree_to_object(ctx->block),
                                 MIR_UNIT_FUNCTION, NULL);
   mir_type_t t_off = mir_offset_type(mu);
   mir_type_t t_ptr = mir_pointer_type(mu, vi_u8_type(mu));
   mir_set_result(mu, t_off);
   mir_add_param(mu, t_ptr, MIR_NULL_STAMP, ident_new("context"));
   mir_value_t state = mir_add_param(mu, t_ptr, MIR_NULL_STAMP, ident_new("state"));

   mir_type_t t_dm = mir_vec2_type(mu, 64, false);
   mir_value_t ff_dirty_var = mir_add_var(mu, t_dm, MIR_NULL_STAMP,
                                          ident_uniq("ffdm"), MIR_VAR_TEMP);
   mir_build_store(mu, ff_dirty_var,
                   mir_const_vec(mu, t_dm, UINT64_C(1) << 63, 0));

   vi_lower_t l = {
      .mu           = mu,
      .ctx          = ctx,
      .isl          = isl,
      .state_ptr    = state,
      .two_state    = isl->two_state,
      .spec         = spec != 0,
      .spec_checks  = spec == 2,
      .spec_var     = MIR_NULL_VALUE,
      .is_ff        = true,
      .ff_stride    = RT_ISLAND_STRIDE(isl->state_bytes),
      .ff_dom       = dm,
      .ff_dirty_var = ff_dirty_var,
      .lut_not      = MIR_NULL_VALUE,
      .lut_and      = MIR_NULL_VALUE,
      .lut_or       = MIR_NULL_VALUE,
      .lut_xor      = MIR_NULL_VALUE,
   };

   vi_unit_open(&l);
   vi_spec_sparse_mark(&l);

   if (dm->rst_isl_sig >= 0) {
      const vhdl_isl_sig_t *ris = &(isl->sigs[dm->rst_isl_sig]);
      const vhdl_isig_t *rs = &(ctx->sigs[ris->sig_id]);
      vi_scope_t sc0 = { .ret_bb = MIR_NULL_BLOCK };
      l.scope = &sc0;
      vi_val_t rv;
      if (!vi_lower_sig_load(&l, rs, ris, vi_off(mu, 0), &rv)) {
         mir_unit_free(mu);
         return NULL;
      }
      mir_value_t rst_active;
      if (l.two_state) {
         mir_value_t b = vi_to_bool(&l, rv);
         rst_active = dm->rst_posedge ? b : mir_build_not(mu, b);
      }
      else {
         uint8_t target = (rs->elem_kind == VISIG_STD_LOGIC)
            ? (dm->rst_posedge ? 3u : 2u) : (dm->rst_posedge ? 1u : 0u);
         rst_active = mir_build_cmp(mu, MIR_CMP_EQ, rv.val, vi_u8(mu, target));
      }

      mir_block_t rst_bb = mir_add_block(mu);
      mir_block_t clk_bb = mir_add_block(mu);
      mir_block_t done_bb = mir_add_block(mu);
      mir_build_cond(mu, rst_active, rst_bb, clk_bb);

      // Reset branch
      mir_set_cursor(mu, rst_bb, MIR_APPEND);
      for (uint32_t k = 0; k < dm->n_procs; k++) {
         uint32_t pi = dm->proc_idxs[k];
         const vhdl_iproc_t *p = &(ctx->procs[pi]);
         vhdl_island_var_defs_clear((vhdl_island_ctx_t *)ctx);
         vhdl_island_set_eval_scope((vhdl_island_ctx_t *)ctx, p->owner_block,
                                    p->owner_ancestors, p->n_owner_ancestors);
         vi_scope_t sc = { .ret_bb = MIR_NULL_BLOCK };
         l.scope = &sc;
         bool ok = vi_bind_process_locals(&l, p->stmt, &sc, false);
         for (int j = 0; j < tree_stmts(p->rst_branch) && ok; j++)
            ok = vi_lower_stmt(&l, tree_stmt(p->rst_branch, j));
         vi_scope_clear(&sc);
         l.scope = NULL;
         vi_proc_done(&l, ctx, pi, ok && !l.failed);
         vhdl_island_set_eval_scope((vhdl_island_ctx_t *)ctx, ctx->block,
                                    ctx->ancestors, ctx->n_ancestors);
         if (!ok || l.failed) {
            *failed_proc_out = (int)pi;
            mir_unit_free(mu);
            return NULL;
         }
      }
      mir_build_jump(mu, done_bb);

      // Clock branch
      mir_set_cursor(mu, clk_bb, MIR_APPEND);
      for (uint32_t k = 0; k < dm->n_procs; k++) {
         uint32_t pi = dm->proc_idxs[k];
         const vhdl_iproc_t *p = &(ctx->procs[pi]);
         vhdl_island_var_defs_clear((vhdl_island_ctx_t *)ctx);
         vhdl_island_set_eval_scope((vhdl_island_ctx_t *)ctx, p->owner_block,
                                    p->owner_ancestors, p->n_owner_ancestors);
         vi_scope_t sc = { .ret_bb = MIR_NULL_BLOCK };
         l.scope = &sc;
         bool ok = vi_bind_process_locals(&l, p->stmt, &sc, false);
         for (int j = 0; j < tree_stmts(p->clk_branch) && ok; j++)
            ok = vi_lower_stmt(&l, tree_stmt(p->clk_branch, j));
         vi_scope_clear(&sc);
         l.scope = NULL;
         vi_proc_done(&l, ctx, pi, ok && !l.failed);
         vhdl_island_set_eval_scope((vhdl_island_ctx_t *)ctx, ctx->block,
                                    ctx->ancestors, ctx->n_ancestors);
         if (!ok || l.failed) {
            *failed_proc_out = (int)pi;
            mir_unit_free(mu);
            return NULL;
         }
      }
      mir_build_jump(mu, done_bb);
      mir_set_cursor(mu, done_bb, MIR_APPEND);
   }
   else {
      for (uint32_t k = 0; k < dm->n_procs; k++) {
         uint32_t pi = dm->proc_idxs[k];
         const vhdl_iproc_t *p = &(ctx->procs[pi]);
         vhdl_island_var_defs_clear((vhdl_island_ctx_t *)ctx);
         vhdl_island_set_eval_scope((vhdl_island_ctx_t *)ctx, p->owner_block,
                                    p->owner_ancestors, p->n_owner_ancestors);
         vi_scope_t sc = { .ret_bb = MIR_NULL_BLOCK };
         l.scope = &sc;
         bool ok = vi_bind_process_locals(&l, p->stmt, &sc, false);
         for (int j = 0; j < tree_stmts(p->clk_branch) && ok; j++)
            ok = vi_lower_stmt(&l, tree_stmt(p->clk_branch, j));
         vi_scope_clear(&sc);
         l.scope = NULL;
         vi_proc_done(&l, ctx, pi, ok && !l.failed);
         vhdl_island_set_eval_scope((vhdl_island_ctx_t *)ctx, ctx->block,
                                    ctx->ancestors, ctx->n_ancestors);
         if (!ok || l.failed) {
            *failed_proc_out = (int)pi;
            mir_unit_free(mu);
            return NULL;
         }
      }
   }

   mir_build_return(mu, mir_build_cast(mu, t_off,
                                       mir_build_load(mu, ff_dirty_var)));
   vi_unit_close(&l);
   return mu;
}

static void vi_reject_failed(vhdl_island_ctx_t *ctx, uint32_t isl_idx,
                             int fail_proc)
{
   if (fail_proc >= 0 && (uint32_t)fail_proc < ctx->n_procs) {
      ctx->procs[fail_proc].rejected = true;
      return;
   }
   for (uint32_t i = 0; i < ctx->n_procs; i++) {
      if (ctx->procs[i].island_idx == (int)isl_idx) {
         ctx->procs[i].rejected = true;
         if (ctx->procs[i].reason == NULL)
            ctx->procs[i].reason = "lower: island clock/reset signal load failed";
      }
   }
}

// Lower every island of CTX, retrying with the failing processes ejected.
// With PUT false the units are discarded (used for the dump-only Mode A
// trial, which must not register units under the Mode B names).
static void vi_lower_ctx(mir_context_t *mc, vhdl_island_ctx_t *ctx, bool put)
{
   const uint32_t max_retries = ctx->n_procs + 1;
   A(mir_unit_t *) built = AINIT;
   for (uint32_t attempt = 0; attempt <= max_retries; attempt++) {
      bool need_rebuild = false;
      for (uint32_t ii = 0; ii < ctx->n_islands; ii++) {
         const vhdl_island_t *isl = &(ctx->islands[ii]);
         if (isl->n_sigs == 0)
            continue;

         int failed_proc = -1;
         if (isl->n_comb_procs > 0
             && (!put || mir_get_shape(mc, isl->eval_name) == NULL)) {
            mir_unit_t *cmu = vi_build_comb_unit(mc, ctx, isl, 0,
                                                 &failed_proc, NULL);
            if (cmu == NULL) {
               vi_reject_failed(ctx, ii, failed_proc);
               need_rebuild = true;
               break;
            }
            APUSH(built, cmu);
         }

         for (uint32_t d = 0; d < isl->n_doms; d++) {
            if (put && mir_get_shape(mc, isl->doms[d].fn_name) != NULL)
               continue;
            mir_unit_t *fmu = vi_build_ff_unit(mc, ctx, isl, &(isl->doms[d]),
                                               0, &failed_proc, NULL);
            if (fmu == NULL) {
               vi_reject_failed(ctx, ii, failed_proc);
               need_rebuild = true;
               break;
            }
            APUSH(built, fmu);
         }

         if (need_rebuild)
            break;
      }

      if (!need_rebuild)
         break;

      for (unsigned u = 0; u < built.count; u++)
         mir_unit_free(built.items[u]);
      built.count = 0;

      vhdl_island_rebuild(ctx);
      if (ctx->n_islands == 0)
         break;
   }

   for (unsigned u = 0; u < built.count; u++) {
      if (put) {
         mir_optimise(built.items[u], MIR_PASS_O1);
         mir_put_unit(mc, built.items[u]);
      }
      else
         mir_unit_free(built.items[u]);
   }
   ACLEAR(built);
}

// A Mode A function of an island that is lowered the first time the
// runtime calls it (see vi_lower_spec)
typedef struct {
   vhdl_island_ctx_t   *ctx;
   const vhdl_island_t *a;
   int                  dom;    // Flip-flop domain or -1 for the comb unit
   int                  spec;
} vi_lazy_t;

static hash_t     *vi_lazy_map;    // Function name to vi_lazy_t
static nvc_lock_t  vi_lazy_lock;

// Body of a Mode A function whose lowering failed: give up Mode A for the
// island before it computes anything
static void vi_build_novar_stub(mir_unit_t *mu)
{
   mir_type_t t_ptr = mir_pointer_type(mu, vi_u8_type(mu));
   mir_set_result(mu, mir_offset_type(mu));
   mir_add_param(mu, t_ptr, MIR_NULL_STAMP, ident_new("context"));
   mir_add_param(mu, t_ptr, MIR_NULL_STAMP, ident_new("state"));
   mir_build_return(mu, vi_off(mu, RT_ISLAND_DEOPT_NOVAR));
}

static void vi_lazy_build(mir_unit_t *mu, object_t *obj)
{
   SCOPED_LOCK(vi_lazy_lock);
   (void)obj;

   ident_t name = mir_get_unit_name(mu);
   vi_lazy_t *lz = hash_get(vi_lazy_map, name);
   assert(lz != NULL);

   mir_context_t *mc = mir_get_context(mu);
   int failed_proc = -1;
   // Built under another name: the deferred one is still registered
   ident_t tmp_name = ident_prefix(name, ident_new("lazy"), '$');
   mir_unit_t *real = lz->dom < 0
      ? vi_build_comb_unit(mc, lz->ctx, lz->a, lz->spec, &failed_proc,
                           tmp_name)
      : vi_build_ff_unit(mc, lz->ctx, lz->a, &(lz->a->doms[lz->dom]),
                         lz->spec, &failed_proc, tmp_name);
   if (real != NULL) {
      mir_optimise(real, MIR_PASS_O1);
      mir_unit_move(mu, real);
   }
   else
      vi_build_novar_stub(mu);

   hash_put(vi_lazy_map, name, NULL);
   vhdl_island_ctx_free(lz->ctx);
   free(lz);
}

// Defer the Mode A function NAME of variant A of an island of CTX
static void vi_lazy_defer(mir_context_t *mc, vhdl_island_ctx_t *ctx,
                          const vhdl_island_t *a, int dom, int spec,
                          ident_t name)
{
   SCOPED_LOCK(vi_lazy_lock);

   if (vi_lazy_map == NULL)
      vi_lazy_map = hash_new(256);
   else if (hash_get(vi_lazy_map, name) != NULL)
      return;   // Same unit as an earlier island (still deferred)

   vi_lazy_t *lz = xmalloc(sizeof(vi_lazy_t));
   lz->ctx  = ctx;
   lz->a    = a;
   lz->dom  = dom;
   lz->spec = spec;
   hash_put(vi_lazy_map, name, lz);

   atomic_add(&(ctx->refs), 1);
   mir_defer(mc, name, NULL, MIR_UNIT_FUNCTION, vi_lazy_build,
             tree_to_object(ctx->block));
}

// Speculative Mode A (NVC_ISLAND_SPEC): lower each island of the final
// Mode B CTX a second time with a packed two-state layout.  The processes
// and the island set are the ones of Mode B; an island whose Mode A
// lowering fails keeps no variant and its processes stay fused.
//
// The Mode A functions are lowered when the runtime first calls them: most
// islands never call the "$ah" variant and some never get promoted, and
// lowering every function up front was a large part of the start-up time.
// A function whose lowering then fails returns RT_ISLAND_DEOPT_NOVAR, which
// pins its island in Mode B.  NVC_DUMP_ISLANDS lowers them all now to report
// which islands have no variant and why.
static void vi_lower_spec(mir_context_t *mc, vhdl_island_ctx_t *ctx)
{
   const bool lazy = !opt_get_int(OPT_DUMP_ISLANDS);

   for (uint32_t ii = 0; ii < ctx->n_islands; ii++) {
      vhdl_island_t *isl = &(ctx->islands[ii]);
      if (isl->n_sigs == 0)
         continue;

      vhdl_island_t *a = vhdl_island_new_a_variant(ctx, isl, &isl->a_reason);
      if (a == NULL)
         continue;

      if (lazy) {
         for (int spec = 1; spec <= 2; spec++) {
            if (a->n_comb_procs > 0)
               vi_lazy_defer(mc, ctx, a, -1, spec,
                             vi_spec_name(a->eval_name, spec));
            for (uint32_t d = 0; d < a->n_doms; d++)
               vi_lazy_defer(mc, ctx, a, d, spec,
                             vi_spec_name(a->doms[d].fn_name, spec));
         }
         isl->a_variant = a;
         continue;
      }

      A(mir_unit_t *) built = AINIT;
      int failed_proc = -1;
      bool ok = true;
      for (int spec = 1; spec <= 2 && ok; spec++) {
         if (a->n_comb_procs > 0
             && mir_get_shape(mc, vi_spec_name(a->eval_name, spec)) == NULL) {
            mir_unit_t *cmu = vi_build_comb_unit(mc, ctx, a, spec,
                                                 &failed_proc, NULL);
            if (cmu != NULL)
               APUSH(built, cmu);
            else
               ok = false;
         }
         for (uint32_t d = 0; ok && d < a->n_doms; d++) {
            ident_t name = vi_spec_name(a->doms[d].fn_name, spec);
            if (mir_get_shape(mc, name) != NULL)
               continue;
            mir_unit_t *fmu = vi_build_ff_unit(mc, ctx, a, &(a->doms[d]),
                                               spec, &failed_proc, NULL);
            if (fmu != NULL)
               APUSH(built, fmu);
            else
               ok = false;
         }
      }

      for (unsigned u = 0; u < built.count; u++) {
         if (ok) {
            mir_optimise(built.items[u], MIR_PASS_O1);
            mir_put_unit(mc, built.items[u]);
         }
         else
            mir_unit_free(built.items[u]);
      }
      ACLEAR(built);

      if (ok)
         isl->a_variant = a;
      else {
         isl->a_reason = (failed_proc >= 0 && ctx->procs[failed_proc].a_block)
            ? ctx->procs[failed_proc].a_block : "spec: lowering failed";
         vhdl_island_free_a_variant(a);
      }
   }
}

vhdl_island_ctx_t *vhdl_island_lower_prepare(mir_context_t *mc, tree_t block,
                                             const tree_t *ancestors,
                                             int n_ancestors,
                                             ident_t scope_prefix,
                                             cover_data_t *cover)
{
   if (opt_get_int(OPT_RTL_ISLANDS) == RTL_ISLANDS_OFF)
      return NULL;
   if (cover != NULL
       && cover_enabled(cover, COVER_MASK_STMT | COVER_MASK_BRANCH
                        | COVER_MASK_EXPRESSION | COVER_MASK_TOGGLE
                        | COVER_MASK_STATE))
      return NULL;

   vhdl_island_ctx_t *ctx = vhdl_island_analyze(block, ancestors, n_ancestors,
                                                scope_prefix);
   if (ctx == NULL)
      return NULL;
   if (ctx->n_islands == 0) {
      vhdl_island_dump_summary(ctx);
      return ctx;
   }

   // NVC_DUMP_ISLANDS in Mode B: also analyse and lower the block as Mode A
   // (discarding the result) so the dump can say which processes would not
   // be Mode A and why.
   vhdl_island_ctx_t *trial = NULL;
   const bool spec = opt_get_int(OPT_ISLAND_SPEC) && !ctx->two_state;
   const bool want_trial =
      opt_get_int(OPT_DUMP_ISLANDS) && !ctx->two_state && !spec;
   if (want_trial) {
      trial = vhdl_island_analyze_mode(block, ancestors, n_ancestors,
                                       scope_prefix, true);
      if (trial != NULL && trial->n_islands > 0)
         vi_lower_ctx(mc, trial, false);
   }

   vi_lower_ctx(mc, ctx, true);

   if (spec)
      vi_lower_spec(mc, ctx);

   if (want_trial) {
      vhdl_island_mark_a_blocks(ctx, trial);
      vhdl_island_ctx_free(trial);
   }

   vhdl_island_dump_summary(ctx);
   return ctx;
}

// Register the speculative Mode A variant of ISL with island HANDLE
static void vi_emit_variant_a_init(const vhdl_island_ctx_t *ctx,
                                   const vhdl_island_t *isl,
                                   vcode_reg_t handle, int ctx_upref)
{
   const vhdl_island_t *a = isl->a_variant;
   vcode_type_t voffset = vtype_offset();
   vcode_type_t vptr = vtype_pointer(vtype_opaque());

   vcode_reg_t clos, clos_h;
   if (a->eval_name != NULL) {
      vcode_reg_t ctx_reg = emit_context_upref(ctx_upref);
      clos = emit_closure(a->eval_name, voffset, &ctx_reg, 1);
      ctx_reg = emit_context_upref(ctx_upref);
      clos_h = emit_closure(vi_spec_name(a->eval_name, 2), voffset,
                            &ctx_reg, 1);
   }
   else
      clos = clos_h = emit_null(vptr);

   vcode_reg_t v_args[7] = {
      handle,
      clos,
      clos_h,
      emit_const(voffset, a->state_bytes),
      emit_const(voffset, a->hdr_bytes),
      emit_const(voffset, a->sparse_log_off),
      emit_const(voffset, a->total_state_bytes),
   };
   emit_fcall(ident_new("__sv_island_set_variant_a"), voffset,
              VCODE_INVALID_STAMP, v_args, 7);

   ident_t bind_a_fn = ident_new("__sv_island_bind_sig_a");
   for (uint32_t j = 0; j < a->n_sigs; j++) {
      const vhdl_isig_t *s = &(ctx->sigs[a->sigs[j].sig_id]);
      vcode_reg_t b_args[6] = {
         handle,
         emit_const(voffset, j),
         emit_const(voffset, a->sigs[j].a_off),
         emit_const(voffset, a->sigs[j].rt_flags),
         emit_const(voffset, s->width * s->nelems * s->byte_size),
         emit_const(voffset, a->sigs[j].x_off),
      };
      emit_fcall(bind_a_fn, voffset, VCODE_INVALID_STAMP, b_args, 6);
   }

   ident_t add_dom_a_fn = ident_new("__sv_island_add_domain_a");
   for (uint32_t d = 0; d < a->n_doms; d++) {
      vcode_reg_t ctx_reg = emit_context_upref(ctx_upref);
      vcode_reg_t ff_clos = emit_closure(a->doms[d].fn_name, voffset,
                                         &ctx_reg, 1);
      ctx_reg = emit_context_upref(ctx_upref);
      vcode_reg_t ff_clos_h = emit_closure(vi_spec_name(a->doms[d].fn_name, 2),
                                           voffset, &ctx_reg, 1);
      vcode_reg_t d_args[4] = {
         handle, emit_const(voffset, d), ff_clos, ff_clos_h
      };
      emit_fcall(add_dom_a_fn, voffset, VCODE_INVALID_STAMP, d_args, 4);
   }
}

void vhdl_island_lower_emit_vcode_init(const vhdl_island_ctx_t *ctx,
                                       lower_unit_t *lu,
                                       vhdl_island_sig_lookup_fn_t get_sig_fn,
                                       int ctx_upref)
{
   if (ctx == NULL)
      return;

   vcode_type_t voffset = vtype_offset();
   vcode_type_t vptr = vtype_pointer(vtype_opaque());

   ident_t begin_fn = ident_new("__sv_island_begin");
   ident_t bind_fn = ident_new("__sv_island_bind_sig");
   ident_t bind_gen_fn = ident_new("__sv_island_bind_gen_sig");
   ident_t add_dom_fn = ident_new("__sv_island_add_domain");
   ident_t add_dom_w_fn = ident_new("__sv_island_add_domain_write");
   ident_t finish_fn = ident_new("__sv_island_finish");

   for (uint32_t ii = 0; ii < ctx->n_islands; ii++) {
      const vhdl_island_t *isl = &(ctx->islands[ii]);
      if (isl->n_sigs == 0)
         continue;

      vcode_reg_t clos;
      if (isl->eval_name != NULL) {
         vcode_reg_t ctx_reg = emit_context_upref(ctx_upref);
         clos = emit_closure(isl->eval_name, voffset, &ctx_reg, 1);
      }
      else
         clos = emit_null(vptr);

      vcode_reg_t begin_args[4] = {
         clos,
         emit_const(voffset, isl->state_bytes),
         emit_const(voffset, isl->n_sigs),
         emit_const(voffset, isl->n_doms),
      };
      vcode_reg_t handle = emit_fcall(begin_fn, vptr, VCODE_INVALID_STAMP,
                                      begin_args, 4);

      if (isl->sparse_log_off > 0) {
         ident_t set_sparse_fn = ident_new("__sv_island_set_sparse");
         vcode_reg_t sp_args[3] = {
            handle,
            emit_const(voffset, isl->sparse_log_off),
            emit_const(voffset, isl->total_state_bytes),
         };
         emit_fcall(set_sparse_fn, voffset, VCODE_INVALID_STAMP, sp_args, 3);
      }

      for (uint32_t j = 0; j < isl->n_sigs; j++) {
         const vhdl_isl_sig_t *isig = &(isl->sigs[j]);
         const vhdl_isig_t *s = &(ctx->sigs[isig->sig_id]);
         if (s->owner_block != NULL) {
            vcode_reg_t bind_gen_args[9] = {
               handle,
               emit_const(voffset, j),
               emit_const(voffset, (int64_t)(intptr_t)s->owner_block),
               emit_const(voffset, (int64_t)(intptr_t)s->decl),
               emit_const(voffset, isig->a_off),
               emit_const(voffset, (int64_t)(uint32_t)isig->b_off),
               emit_const(voffset, s->width),
               emit_const(voffset, s->nelems),
               emit_const(voffset, isig->rt_flags),
            };
            emit_fcall(bind_gen_fn, voffset, VCODE_INVALID_STAMP,
                       bind_gen_args, 9);
         }
         else {
            vcode_reg_t sig_reg = get_sig_fn(lu, s->decl, s->field_path, s->path_len);
            vcode_reg_t bind_args[8] = {
               handle,
               emit_const(voffset, j),
               sig_reg,
               emit_const(voffset, isig->a_off),
               emit_const(voffset, (int64_t)(uint32_t)isig->b_off),
               emit_const(voffset, s->width),
               emit_const(voffset, s->nelems),
               emit_const(voffset, isig->rt_flags),
            };
            emit_fcall(bind_fn, voffset, VCODE_INVALID_STAMP, bind_args, 8);
         }
      }

      for (uint32_t d = 0; d < isl->n_doms; d++) {
         const vhdl_idom_t *dm = &(isl->doms[d]);
         vcode_reg_t ctx_reg = emit_context_upref(ctx_upref);
         vcode_reg_t ff_clos = emit_closure(dm->fn_name, voffset, &ctx_reg, 1);
         vcode_reg_t dom_args[7] = {
            handle,
            emit_const(voffset, d),
            ff_clos,
            emit_const(voffset, dm->clk_isl_sig),
            emit_const(voffset, dm->rst_isl_sig),
            emit_const(voffset, dm->edge_flags),
            emit_const(voffset, dm->n_write_sigs),
         };
         emit_fcall(add_dom_fn, voffset, VCODE_INVALID_STAMP, dom_args, 7);

         for (uint32_t w = 0; w < dm->n_write_sigs; w++) {
            vcode_reg_t w_args[3] = {
               handle,
               emit_const(voffset, d),
               emit_const(voffset, dm->write_isl_sigs[w]),
            };
            emit_fcall(add_dom_w_fn, voffset, VCODE_INVALID_STAMP, w_args, 3);
         }
      }

      if (isl->a_variant != NULL)
         vi_emit_variant_a_init(ctx, isl, handle, ctx_upref);

      emit_fcall(finish_fn, voffset, VCODE_INVALID_STAMP, &handle, 1);
   }
}
