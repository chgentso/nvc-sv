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

#include "test_util.h"
#include "rt/crv.h"
#include "rt/crv-bdd.h"
#include "rt/crv-sat.h"

#include <inttypes.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// Tests of the constraint solver through its C API: the graph that the
// generated code builds is built here by hand and the answers are checked
// against values computed in this file, never by the solver's own
// evaluator.

////////////////////////////////////////////////////////////////////////////////
// Reference arithmetic

static inline uint64_t tmask(unsigned w)
{
   return w >= 64 ? ~UINT64_C(0) : (UINT64_C(1) << w) - 1;
}

// Wrap to w bits and sign or zero extend: the representation of a value
static int64_t wrapv(int64_t v, unsigned w, bool s)
{
   uint64_t u = (uint64_t)v & tmask(w);
   if (s && w < 64 && ((u >> (w - 1)) & 1))
      u |= ~tmask(w);
   return (int64_t)u;
}

static __int128 widen(int64_t v, bool s)
{
   return s ? (__int128)v : (__int128)(uint64_t)v;
}

static int64_t ref_shift(crv_binary_t op, unsigned w, bool s, int64_t a,
                         uint64_t n)
{
   const uint64_t ua = (uint64_t)a;
   if (n >= 64)
      return (op == CRV_ASR && s && a < 0) ? -1 : 0;
   else if (op == CRV_SHL)
      return wrapv(ua << n, w, s);
   else if (op == CRV_ASR && s)
      return wrapv(a >> n, w, s);
   else
      return wrapv((ua & tmask(w)) >> n, w, s);
}

// Result of a binary operator on operands of the result width and
// signedness.  Returns false for division by zero.
static bool ref_bin(crv_binary_t op, unsigned w, bool s, int64_t a, int64_t b,
                    int64_t *r)
{
   const uint64_t ua = (uint64_t)a, ub = (uint64_t)b;
   switch (op) {
   case CRV_ADD: *r = wrapv(ua + ub, w, s); return true;
   case CRV_SUB: *r = wrapv(ua - ub, w, s); return true;
   case CRV_MUL: *r = wrapv(ua * ub, w, s); return true;
   case CRV_AND: *r = wrapv(ua & ub, w, s); return true;
   case CRV_OR:  *r = wrapv(ua | ub, w, s); return true;
   case CRV_XOR: *r = wrapv(ua ^ ub, w, s); return true;
   case CRV_DIV:
   case CRV_MOD:
      if (b == 0)
         return false;
      else {
         const __int128 x = widen(a, s), y = widen(b, s);
         *r = wrapv(op == CRV_DIV ? x / y : x % y, w, s);
         return true;
      }
   case CRV_SHL:
   case CRV_SHR:
   case CRV_ASR:
      *r = ref_shift(op, w, s, a, ub & tmask(w));
      return true;
   default:
      ck_abort_msg("not an arithmetic operator");
   }
}

////////////////////////////////////////////////////////////////////////////////
// Test random numbers: not the solver's, so that a test does not depend on
// how many numbers the solver drew

typedef struct { uint64_t s; } trng_t;

static uint64_t trng_next(trng_t *r)
{
   uint64_t z = (r->s += UINT64_C(0x9e3779b97f4a7c15));
   z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
   z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
   return z ^ (z >> 31);
}

static unsigned trng_below(trng_t *r, unsigned n)
{
   return trng_next(r) % n;
}

////////////////////////////////////////////////////////////////////////////////
// Domains and simple constraints

START_TEST(test_domain)
{
   // An enum has only its members as values
   static const int64_t members[] = { 3, 7, 200 };
   int seen[3] = { 0 };

   crv_seed(1);
   for (int i = 0; i < 300; i++) {
      crv_t *c = crv_new();
      crv_var_t x = crv_var(c, 8, false, 3);
      for (int k = 0; k < 3; k++)
         crv_var_domain(c, x, members[k]);
      ck_assert(crv_solve(c));

      const int64_t v = crv_value(c, x);
      int k;
      for (k = 0; k < 3 && members[k] != v; k++)
         ;
      ck_assert_msg(k < 3, "value %"PRIi64" is not a member", v);
      seen[k]++;
      crv_free(c);
   }

   for (int k = 0; k < 3; k++)
      ck_assert_msg(seen[k] > 50, "member %d seen %d of 300", k, seen[k]);
}
END_TEST

START_TEST(test_fixed)
{
   // A variable that is not being randomized keeps its value
   crv_seed(2);
   for (int i = 0; i < 20; i++) {
      crv_t *c = crv_new();
      crv_var_t x = crv_var(c, 16, true, -1234);
      crv_var_domain(c, x, -1234);
      crv_var_t y = crv_var(c, 8, false, 0);
      crv_require(c, crv_binary(c, CRV_LT, crv_var_ref(c, x),
                                crv_const(c, 0, 16, true), 1, false),
                  false, 1);
      crv_require(c, crv_binary(c, CRV_EQ, crv_var_ref(c, y),
                                crv_const(c, 77, 8, false), 1, false),
                  false, 2);
      ck_assert(crv_solve(c));
      ck_assert_int_eq(crv_value(c, x), -1234);
      ck_assert_int_eq(crv_value(c, y), 77);
      crv_free(c);
   }
}
END_TEST

START_TEST(test_unsat)
{
   // Failure changes nothing
   crv_t *c = crv_new();
   crv_var_t x = crv_var(c, 8, false, 42);
   crv_require(c, crv_binary(c, CRV_GT, crv_var_ref(c, x),
                             crv_const(c, 200, 8, false), 1, false), false, 1);
   crv_require(c, crv_binary(c, CRV_LT, crv_var_ref(c, x),
                             crv_const(c, 100, 8, false), 1, false), false, 2);
   ck_assert_int_eq(crv_solve2(c), CRV_UNSAT);
   ck_assert(!crv_solve(c));
   ck_assert_int_eq(crv_value(c, x), 42);
   crv_free(c);

   c = crv_new();
   x = crv_var(c, 8, false, 42);
   crv_require(c, crv_binary(c, CRV_GT, crv_var_ref(c, x),
                             crv_const(c, 200, 8, false), 1, false), false, 1);
   ck_assert_int_eq(crv_solve2(c), CRV_SAT);
   ck_assert_int_gt(crv_value(c, x), 200);
   crv_free(c);
}
END_TEST

START_TEST(test_no_constraint)
{
   // Every value of a narrow variable shows up, and of a signed one both signs
   int seen[16] = { 0 }, neg = 0, pos = 0;
   crv_seed(3);
   for (int i = 0; i < 1000; i++) {
      crv_t *c = crv_new();
      crv_var_t x = crv_var(c, 4, false, 0);
      crv_var_t y = crv_var(c, 12, true, 0);
      ck_assert(crv_solve(c));
      seen[crv_value(c, x)]++;
      const int64_t v = crv_value(c, y);
      ck_assert(v >= -2048 && v <= 2047);
      neg += v < 0;
      pos += v >= 0;
      crv_free(c);
   }

   for (int k = 0; k < 16; k++)
      ck_assert_msg(seen[k] > 20, "value %d seen %d of 1000", k, seen[k]);
   ck_assert(neg > 300 && pos > 300);
}
END_TEST

////////////////////////////////////////////////////////////////////////////////
// Operators

typedef struct {
   crv_binary_t op;
   const char  *name;
} binop_t;

static const binop_t binops[] = {
   { CRV_ADD, "add" }, { CRV_SUB, "sub" }, { CRV_MUL, "mul" },
   { CRV_AND, "and" }, { CRV_OR, "or" }, { CRV_XOR, "xor" },
   { CRV_SHL, "shl" }, { CRV_SHR, "shr" }, { CRV_ASR, "asr" },
   { CRV_DIV, "div" }, { CRV_MOD, "mod" },
};

// (x op y) == k where k is the result for chosen operands: the solver must
// find some pair with the same result, which the reference arithmetic here
// then confirms
static void check_binop(const binop_t *bo, unsigned w, bool s, trng_t *rng)
{
   int ncheck = 0;
   for (int trial = 0; trial < 12; trial++) {
      const int64_t a0 = wrapv(trng_next(rng), w, s);
      int64_t b0 = wrapv(trng_next(rng), w, s);
      if ((bo->op == CRV_SHL || bo->op == CRV_SHR || bo->op == CRV_ASR)
          && s)
         b0 = trng_below(rng, w + 2);   // Shift amounts of either type
      int64_t k;
      if (!ref_bin(bo->op, w, s, a0, b0, &k))
         continue;

      crv_t *c = crv_new();
      crv_var_t x = crv_var(c, w, s, 0), y = crv_var(c, w, s, 0);
      crv_node_t e = crv_binary(c, bo->op, crv_var_ref(c, x),
                                crv_var_ref(c, y), w, s);
      crv_require(c, crv_binary(c, CRV_EQ, e, crv_const(c, k, w, s), 1, false),
                  false, 1);
      ck_assert_msg(crv_solve(c), "%s w=%u s=%d: no solution for %"PRIi64,
                    bo->name, w, s, k);

      int64_t r;
      ck_assert(ref_bin(bo->op, w, s, crv_value(c, x), crv_value(c, y), &r));
      ck_assert_msg(r == k, "%s w=%u s=%d: x=%"PRIi64" y=%"PRIi64" gives "
                    "%"PRIi64" not %"PRIi64, bo->name, w, s, crv_value(c, x),
                    crv_value(c, y), r, k);
      crv_free(c);
      ncheck++;
   }
   ck_assert_msg(ncheck > 0, "%s w=%u s=%d: no case", bo->name, w, s);
}

START_TEST(test_binary)
{
   const binop_t *bo = &binops[_i / 6];
   const unsigned widths[3] = { 3, 6, 8 };
   const unsigned w = widths[(_i % 6) / 2];
   const bool s = _i % 2;

   crv_seed(10 + _i);
   trng_t rng = { 100 + _i };
   check_binop(bo, w, s, &rng);
}
END_TEST

START_TEST(test_compare)
{
   // Each comparison on signed and unsigned operands of the same width
   static const crv_binary_t ops[] = { CRV_EQ, CRV_NE, CRV_LT, CRV_LE,
                                       CRV_GT, CRV_GE };
   const crv_binary_t op = ops[_i / 2];
   const bool s = _i % 2;
   trng_t rng = { 7 };

   crv_seed(20 + _i);
   for (int trial = 0; trial < 20; trial++) {
      const int64_t k = wrapv(trng_next(&rng), 6, s);
      crv_t *c = crv_new();
      crv_var_t x = crv_var(c, 6, s, 0);
      crv_require(c, crv_binary(c, op, crv_var_ref(c, x),
                                crv_const(c, k, 6, s), 1, false), false, 1);
      const bool want = (op == CRV_EQ) ? true
         : (op == CRV_NE) ? true
         : (op == CRV_LT) ? widen(k, s) > (s ? -32 : 0)
         : (op == CRV_GT) ? widen(k, s) < (s ? 31 : 63)
         : true;
      const bool ok = crv_solve(c);
      ck_assert_int_eq(ok, want);
      if (ok) {
         const __int128 v = widen(crv_value(c, x), s), kk = widen(k, s);
         const bool holds = op == CRV_EQ ? v == kk : op == CRV_NE ? v != kk
            : op == CRV_LT ? v < kk : op == CRV_LE ? v <= kk
            : op == CRV_GT ? v > kk : v >= kk;
         ck_assert(holds);
      }
      crv_free(c);
   }
}
END_TEST

START_TEST(test_unary)
{
   trng_t rng = { 11 };
   crv_seed(30);
   for (int trial = 0; trial < 60; trial++) {
      const unsigned w = 4 + trng_below(&rng, 9);
      const crv_unary_t op = (crv_unary_t)trng_below(&rng, 7);
      const int64_t a0 = wrapv(trng_next(&rng), w, false);
      int64_t k;
      unsigned rw = w;
      bool rs = false;
      const uint64_t u = (uint64_t)a0 & tmask(w);

      switch (op) {
      case CRV_NEG: k = wrapv(-u, w, false); break;
      case CRV_INV: k = wrapv(~u, w, false); break;
      case CRV_LOG_NOT: k = a0 == 0; rw = 1; break;
      case CRV_REDUCE_AND: k = u == tmask(w); rw = 1; break;
      case CRV_REDUCE_OR: k = u != 0; rw = 1; break;
      case CRV_REDUCE_XOR: k = __builtin_popcountll(u) & 1; rw = 1; break;
      case CRV_POPCNT: k = __builtin_popcountll(u); rw = 32; rs = true; break;
      default: ck_abort_msg("bad op");
      }

      crv_t *c = crv_new();
      crv_var_t x = crv_var(c, w, false, 0);
      crv_node_t e = crv_unary(c, op, crv_var_ref(c, x));
      crv_require(c, crv_binary(c, CRV_EQ, e, crv_const(c, k, rw, rs), 1,
                                false), false, 1);
      ck_assert_msg(crv_solve(c), "unary %d w=%u: no solution for %"PRIi64,
                    op, w, k);

      const uint64_t v = (uint64_t)crv_value(c, x) & tmask(w);
      int64_t r;
      switch (op) {
      case CRV_NEG: r = wrapv(-v, w, false); break;
      case CRV_INV: r = wrapv(~v, w, false); break;
      case CRV_LOG_NOT: r = v == 0; break;
      case CRV_REDUCE_AND: r = v == tmask(w); break;
      case CRV_REDUCE_OR: r = v != 0; break;
      case CRV_REDUCE_XOR: r = __builtin_popcountll(v) & 1; break;
      default: r = __builtin_popcountll(v); break;
      }
      ck_assert_msg(r == k, "unary %d w=%u: x=%"PRIu64" gives %"PRIi64
                    " not %"PRIi64, op, w, v, r, k);
      crv_free(c);
   }
}
END_TEST

START_TEST(test_popcnt_signed)
{
   // A signed variable whose domain holds negative numbers and zero has
   // values with a count of one or more: these were refuted
   for (unsigned w = 1; w <= 5; w++) {
      crv_t *c = crv_new();
      crv_seed(31 + w);
      crv_var_t x = crv_var(c, w, true, 0);
      crv_node_t n = crv_unary(c, CRV_POPCNT, crv_var_ref(c, x));
      n = crv_unary(c, CRV_POPCNT, n);
      crv_require(c, crv_unary(c, CRV_REDUCE_OR, n), false, 1);
      ck_assert(crv_solve(c));
      ck_assert_int_ne(crv_value(c, x), 0);
      crv_free(c);

      c = crv_new();
      x = crv_var(c, w, true, 0);
      n = crv_unary(c, CRV_POPCNT, crv_var_ref(c, x));
      crv_require(c, crv_binary(c, CRV_EQ, n, crv_const(c, w, 32, true), 1,
                                false), false, 1);
      ck_assert(crv_solve(c));
      ck_assert_int_eq(crv_value(c, x), -1);
      crv_free(c);
   }
}
END_TEST

START_TEST(test_cast)
{
   // Narrowing wraps, widening extends by the signedness of the source
   crv_seed(31);
   for (int i = 0; i < 40; i++) {
      crv_t *c = crv_new();
      crv_var_t x = crv_var(c, 4, true, 0);       // -8 to 7
      crv_node_t wide = crv_cast(c, crv_var_ref(c, x), 12, false);
      crv_require(c, crv_binary(c, CRV_GT, wide, crv_const(c, 4000, 12, false),
                                1, false), false, 1);
      ck_assert(crv_solve(c));
      const int64_t v = crv_value(c, x);
      ck_assert_msg(v < 0, "sign extension to 12 bits gives %"PRIi64, v);
      crv_free(c);

      c = crv_new();
      x = crv_var(c, 12, false, 0);
      crv_node_t narrow = crv_cast(c, crv_var_ref(c, x), 4, false);
      crv_require(c, crv_binary(c, CRV_EQ, narrow, crv_const(c, 9, 4, false),
                                1, false), false, 1);
      ck_assert(crv_solve(c));
      ck_assert_int_eq(crv_value(c, x) & 15, 9);
      crv_free(c);
   }
}
END_TEST

START_TEST(test_logic)
{
   // (a && b) || c, a -> b, and ?: on three 1 bit variables: all the
   // solutions found must satisfy them and every solution must turn up
   int seen[8] = { 0 };
   crv_seed(32);
   for (int i = 0; i < 400; i++) {
      crv_t *c = crv_new();
      crv_var_t v[3];
      for (int k = 0; k < 3; k++)
         v[k] = crv_var(c, 1, false, 0);
      crv_node_t a = crv_var_ref(c, v[0]), b = crv_var_ref(c, v[1]),
         d = crv_var_ref(c, v[2]);
      crv_node_t ab = crv_binary(c, CRV_LOG_AND, a, b, 1, false);
      crv_require(c, crv_binary(c, CRV_LOG_OR, ab, d, 1, false), false, 1);
      crv_require(c, crv_binary(c, CRV_IMPLIES, a, b, 1, false), false, 2);
      crv_require(c, crv_cond(c, d, a, crv_unary(c, CRV_LOG_NOT, b)), false, 3);
      ck_assert(crv_solve(c));

      const int x = crv_value(c, v[0]), y = crv_value(c, v[1]),
         z = crv_value(c, v[2]);
      ck_assert((x && y) || z);
      ck_assert(!x || y);
      ck_assert(z ? x : !y);
      seen[x | (y << 1) | (z << 2)]++;
      crv_free(c);
   }

   // x y z: 1 1 1 is the only one with d set; with d clear need x&&y and !y
   int n = 0;
   for (int k = 0; k < 8; k++)
      n += seen[k] > 0;
   ck_assert_msg(n == 1, "%d distinct solutions, expected 1", n);
   ck_assert_int_eq(seen[7], 400);
}
END_TEST

////////////////////////////////////////////////////////////////////////////////
// Sets, distributions, ordering, soft constraints and unique

START_TEST(test_inside)
{
   // inside {1, [10:12], y} where y is a variable
   int seen[16] = { 0 };
   crv_seed(40);
   for (int i = 0; i < 600; i++) {
      crv_t *c = crv_new();
      crv_var_t x = crv_var(c, 4, false, 0), y = crv_var(c, 4, false, 0);
      crv_set_t s = crv_set_new(c);
      crv_set_value(c, s, crv_const(c, 1, 4, false));
      crv_set_range(c, s, crv_const(c, 10, 4, false),
                    crv_const(c, 12, 4, false));
      crv_set_value(c, s, crv_var_ref(c, y));
      crv_require(c, crv_inside(c, crv_var_ref(c, x), s), false, 1);
      crv_require(c, crv_binary(c, CRV_EQ, crv_var_ref(c, y),
                                crv_const(c, 5, 4, false), 1, false), false, 2);
      ck_assert(crv_solve(c));
      const int64_t v = crv_value(c, x);
      ck_assert_msg(v == 1 || v == 5 || (v >= 10 && v <= 12), "x=%"PRIi64, v);
      seen[v]++;
      crv_free(c);
   }

   const int members[] = { 1, 5, 10, 11, 12 };
   for (int k = 0; k < 5; k++)
      ck_assert_msg(seen[members[k]] > 60, "value %d seen %d of 600",
                    members[k], seen[members[k]]);
}
END_TEST

START_TEST(test_dist)
{
   // x dist { 0 := 1, [1:3] := 1, [8:11] :/ 4, 15 := 0 }: 1/8 for 0, each of
   // 1..3 has 1/8, and 8..11 share 4/8
   static const double expect[16] = {
      1.0 / 8, 1.0 / 8, 1.0 / 8, 1.0 / 8, 0, 0, 0, 0,
      0.5 / 4, 0.5 / 4, 0.5 / 4, 0.5 / 4, 0, 0, 0, 0
   };
   int seen[16] = { 0 };
   const int n = 4000;

   crv_seed(41);
   for (int i = 0; i < n; i++) {
      crv_t *c = crv_new();
      crv_var_t x = crv_var(c, 4, false, 0);
      crv_dist_t d = crv_dist_new(c);
      crv_dist_range(c, d, 0, 0, 1, true);
      crv_dist_range(c, d, 1, 3, 1, true);
      crv_dist_range(c, d, 8, 11, 4, false);
      crv_dist_range(c, d, 15, 15, 0, true);
      crv_require(c, crv_dist(c, crv_var_ref(c, x), d), false, 1);
      ck_assert(crv_solve(c));
      seen[crv_value(c, x)]++;
      crv_free(c);
   }

   for (int k = 0; k < 16; k++) {
      if (expect[k] == 0)
         ck_assert_msg(seen[k] == 0, "value %d has no weight but seen %d",
                       k, seen[k]);
      else {
         const double f = (double)seen[k] / n;
         ck_assert_msg(f > expect[k] * 0.8 && f < expect[k] * 1.2,
                       "value %d: %.3f expected %.3f", k, f, expect[k]);
      }
   }
}
END_TEST

START_TEST(test_solve_before)
{
   // x < y over 4 bits, solve x before y: x is uniform over 0..14 so x == 0
   // is as likely as x == 14
   int seen[16] = { 0 };
   const int n = 3000;

   crv_seed(42);
   for (int i = 0; i < n; i++) {
      crv_t *c = crv_new();
      crv_var_t x = crv_var(c, 4, false, 0), y = crv_var(c, 4, false, 0);
      crv_require(c, crv_binary(c, CRV_LT, crv_var_ref(c, x),
                                crv_var_ref(c, y), 1, false), false, 1);
      crv_solve_before(c, x, y);
      ck_assert(crv_solve(c));
      ck_assert(crv_value(c, x) < crv_value(c, y));
      seen[crv_value(c, x)]++;
      crv_free(c);
   }

   ck_assert_int_eq(seen[15], 0);
   for (int k = 0; k < 15; k++) {
      const double f = (double)seen[k] / n;
      ck_assert_msg(f > 1.0 / 15 * 0.65 && f < 1.0 / 15 * 1.35,
                    "x=%d: %.3f expected %.3f", k, f, 1.0 / 15);
   }
}
END_TEST

START_TEST(test_soft)
{
   // A later soft constraint has the higher priority
   crv_seed(43);
   for (int i = 0; i < 40; i++) {
      crv_t *c = crv_new();
      crv_var_t x = crv_var(c, 8, false, 0);
      crv_node_t xv = crv_var_ref(c, x);
      crv_require(c, crv_binary(c, CRV_GE, xv, crv_const(c, 10, 8, false), 1,
                                false), false, 1);
      crv_require(c, crv_binary(c, CRV_LT, xv, crv_const(c, 50, 8, false), 1,
                                false), true, 2);
      crv_require(c, crv_binary(c, CRV_GE, xv, crv_const(c, 100, 8, false), 1,
                                false), true, 3);
      crv_require(c, crv_binary(c, CRV_EQ, crv_binary(
                                   c, CRV_AND, xv, crv_const(c, 1, 8, false),
                                   8, false), crv_const(c, 1, 8, false), 1,
                                false), true, 4);
      ck_assert(crv_solve(c));
      const int64_t v = crv_value(c, x);
      ck_assert_msg(v >= 100 && (v & 1) == 1, "x=%"PRIi64, v);
      crv_free(c);
   }

   // A soft constraint that cannot hold with the hard ones is dropped
   crv_t *c = crv_new();
   crv_var_t x = crv_var(c, 8, false, 0);
   crv_require(c, crv_binary(c, CRV_LT, crv_var_ref(c, x),
                             crv_const(c, 5, 8, false), 1, false), false, 1);
   crv_require(c, crv_binary(c, CRV_GT, crv_var_ref(c, x),
                             crv_const(c, 50, 8, false), 1, false), true, 2);
   ck_assert(crv_solve(c));
   ck_assert(crv_value(c, x) < 5);
   crv_free(c);
}
END_TEST

// The soft constraints kept are the ones taken one at a time from the
// highest priority, whichever way they are found
START_TEST(test_soft_many)
{
   static const crv_engine_t engines[] = { CRV_ENGINE_DFS, CRV_ENGINE_AUTO };
   const crv_engine_t saved = crv_engine();
   trng_t rng = { 91 };

   for (size_t e = 0; e < ARRAY_LEN(engines); e++) {
      crv_set_engine(engines[e]);
      crv_seed(95 + e);

      for (int trial = 0; trial < 120; trial++) {
         const int n = 1 + trng_below(&rng, 24);
         int kind[24], a[24], b[24];
         bool sat[24][256];
         crv_t *c = crv_new();
         crv_var_t x = crv_var(c, 8, false, 0);
         crv_node_t xv = crv_var_ref(c, x);

         const int hlo = trng_below(&rng, 60), hhi = 200 + trng_below(&rng, 56);
         crv_require(c, crv_binary(c, CRV_GE, xv, crv_const(c, hlo, 8, false),
                                   1, false), false, 1);
         crv_require(c, crv_binary(c, CRV_LE, xv, crv_const(c, hhi, 8, false),
                                   1, false), false, 1);

         for (int i = 0; i < n; i++) {
            kind[i] = trng_below(&rng, 3);
            a[i] = trng_below(&rng, 256);
            b[i] = trng_below(&rng, 256);
            crv_node_t k;
            switch (kind[i]) {
            case 0:
               k = crv_binary(c, CRV_NE, xv, crv_const(c, a[i], 8, false), 1,
                              false);
               break;
            case 1:
               k = crv_binary(c, CRV_LT, xv, crv_const(c, a[i], 8, false), 1,
                              false);
               break;
            default:
               a[i] &= 0x0f;
               b[i] &= 0xf0;
               k = crv_binary(
                  c, CRV_EQ, crv_binary(c, CRV_AND, xv,
                                        crv_const(c, 0xf0, 8, false), 8, false),
                  crv_const(c, b[i], 8, false), 1, false);
               break;
            }
            crv_require(c, k, true, 10 + i);
            for (int v = 0; v < 256; v++)
               sat[i][v] = kind[i] == 0 ? v != a[i] : kind[i] == 1 ? v < a[i]
                  : (v & 0xf0) == b[i];
         }

         // Greedy from the last one
         bool kept[24] = { false };
         bool ok[256];
         for (int v = 0; v < 256; v++)
            ok[v] = v >= hlo && v <= hhi;
         for (int i = n - 1; i >= 0; i--) {
            int both = 0;
            for (int v = 0; v < 256; v++)
               both += ok[v] && sat[i][v];
            if (both > 0) {
               kept[i] = true;
               for (int v = 0; v < 256; v++)
                  ok[v] = ok[v] && sat[i][v];
            }
         }

         ck_assert_msg(crv_solve(c), "engine %zu trial %d", e, trial);
         const int v = crv_value(c, x);
         ck_assert(v >= hlo && v <= hhi);
         for (int i = 0; i < n; i++)
            ck_assert_msg(sat[i][v] == kept[i], "engine %zu trial %d: soft "
                          "constraint %d of %d is %d, expected %d", e, trial,
                          i, n, sat[i][v], kept[i]);
         crv_free(c);
      }
   }

   crv_set_engine(saved);
}
END_TEST

START_TEST(test_unique)
{
   // Three 2 bit variables all different: 24 ordered solutions
   int seen[64] = { 0 }, nseen = 0;
   crv_seed(44);
   for (int i = 0; i < 1200; i++) {
      crv_t *c = crv_new();
      crv_var_t v[3];
      crv_node_t n[3];
      for (int k = 0; k < 3; k++) {
         v[k] = crv_var(c, 2, false, 0);
         n[k] = crv_var_ref(c, v[k]);
      }
      crv_unique(c, n, 3);
      ck_assert(crv_solve(c));
      const int a = crv_value(c, v[0]), b = crv_value(c, v[1]),
         d = crv_value(c, v[2]);
      ck_assert(a != b && a != d && b != d);
      if (seen[a | (b << 2) | (d << 4)]++ == 0)
         nseen++;
      crv_free(c);
   }
   ck_assert_int_eq(nseen, 24);

   // Five of them cannot be
   crv_t *c = crv_new();
   crv_node_t n[5];
   for (int k = 0; k < 5; k++)
      n[k] = crv_var_ref(c, crv_var(c, 2, false, 0));
   crv_unique(c, n, 5);
   ck_assert(!crv_solve(c));
   crv_free(c);
}
END_TEST

// Large unique sets are propagated by the values that have been taken:
// a set built from a permutation always has a solution, and one with one
// member too many for its values has none
START_TEST(test_unique_large)
{
   static const crv_engine_t engines[] = { CRV_ENGINE_DFS, CRV_ENGINE_AUTO };
   const crv_engine_t saved = crv_engine();
   trng_t rng = { 77 };

   for (size_t e = 0; e < ARRAY_LEN(engines); e++) {
      crv_set_engine(engines[e]);
      crv_seed(81 + e);

      for (int trial = 0; trial < 150; trial++) {
         const int n = 8 + trng_below(&rng, 40);
         crv_t *c = crv_new();
         crv_var_t v[48];
         crv_node_t nodes[48];
         int64_t lo[48], hi[48];
         int perm[48];
         for (int i = 0; i < n; i++)
            perm[i] = i * 2;
         for (int i = n - 1; i > 0; i--) {
            const int j = trng_below(&rng, i + 1);
            const int t = perm[i];
            perm[i] = perm[j];
            perm[j] = t;
         }

         for (int i = 0; i < n; i++) {
            // A range round the value of a hidden solution
            const int slack = trial % 3 == 0 ? 0 : trng_below(&rng, 6);
            lo[i] = MAX(0, perm[i] - (int)trng_below(&rng, slack + 1));
            hi[i] = perm[i] + trng_below(&rng, slack + 1);
            v[i] = crv_var(c, 8, false, 0);
            nodes[i] = crv_var_ref(c, v[i]);
            crv_node_t in = crv_binary(
               c, CRV_LOG_AND,
               crv_binary(c, CRV_GE, nodes[i], crv_const(c, lo[i], 8, false),
                          1, false),
               crv_binary(c, CRV_LE, nodes[i], crv_const(c, hi[i], 8, false),
                          1, false), 1, false);
            crv_require(c, in, false, 1);
         }
         crv_unique(c, nodes, n);

         ck_assert_msg(crv_solve(c), "engine %zu trial %d n %d: no solution",
                       e, trial, n);
         for (int i = 0; i < n; i++) {
            const int64_t x = crv_value(c, v[i]);
            ck_assert(x >= lo[i] && x <= hi[i]);
            for (int j = 0; j < i; j++)
               ck_assert_int_ne(x, crv_value(c, v[j]));
         }
         crv_free(c);
      }

      // Nine members and eight values
      crv_t *c = crv_new();
      crv_node_t nodes[9];
      for (int i = 0; i < 9; i++) {
         crv_var_t x = crv_var(c, 8, false, 0);
         nodes[i] = crv_var_ref(c, x);
         crv_require(c, crv_binary(c, CRV_LT, nodes[i],
                                   crv_const(c, 8, 8, false), 1, false),
                     false, 1);
      }
      crv_unique(c, nodes, 9);
      ck_assert_int_eq(crv_solve2(c), CRV_UNSAT);
      crv_free(c);
   }

   crv_set_engine(saved);
}
END_TEST

// Variables that no constraint links are solved on their own: every
// constraint holds, a variable nothing mentions is free, and a solve before
// keeps its variables together
START_TEST(test_components)
{
   const crv_engine_t saved = crv_engine();
   crv_set_engine(CRV_ENGINE_AUTO);
   crv_seed(101);
   int loose_seen[4] = { 0 };

   for (int i = 0; i < 300; i++) {
      crv_t *c = crv_new();
      crv_var_t x = crv_var(c, 8, false, 0), y = crv_var(c, 8, false, 0);
      crv_var_t a = crv_var(c, 6, false, 0), b = crv_var(c, 6, false, 0);
      crv_var_t d = crv_var(c, 6, false, 0), loose = crv_var(c, 2, false, 0);
      crv_var_t p = crv_var(c, 4, false, 0), q = crv_var(c, 4, false, 0);
      crv_require(c, crv_binary(c, CRV_LT, crv_var_ref(c, x),
                                crv_var_ref(c, y), 1, false), false, 1);
      crv_node_t sum = crv_binary(
         c, CRV_ADD, crv_binary(c, CRV_ADD, crv_var_ref(c, a),
                                crv_var_ref(c, b), 6, false),
         crv_var_ref(c, d), 6, false);
      crv_require(c, crv_binary(c, CRV_EQ, sum, crv_const(c, 33, 6, false), 1,
                                false), false, 2);
      crv_require(c, crv_binary(c, CRV_NE, crv_var_ref(c, p),
                                crv_var_ref(c, q), 1, false), false, 3);
      crv_solve_before(c, p, q);
      ck_assert(crv_solve(c));

      ck_assert(crv_value(c, x) < crv_value(c, y));
      ck_assert_int_eq((crv_value(c, a) + crv_value(c, b) + crv_value(c, d))
                       & 63, 33);
      ck_assert(crv_value(c, p) != crv_value(c, q));
      loose_seen[crv_value(c, loose)]++;
      crv_free(c);
   }

   for (int k = 0; k < 4; k++)
      ck_assert_msg(loose_seen[k] > 30, "free value %d seen %d of 300", k,
                    loose_seen[k]);
   crv_set_engine(saved);
}
END_TEST

START_TEST(test_randc)
{
   // Every value of a 4 bit variable once before any repeats, also with a
   // constraint that removes some values and with another random variable
   uint64_t state = 0;
   crv_seed(45);
   for (int cycle = 0; cycle < 3; cycle++) {
      bool used[16] = { false };
      for (int i = 0; i < 16; i++) {
         crv_t *c = crv_new();
         crv_var_t x = crv_var(c, 4, false, 0);
         crv_var_randc(c, x, &state);
         crv_var_t y = crv_var(c, 8, false, 0);
         crv_require(c, crv_binary(c, CRV_LT, crv_var_ref(c, y),
                                   crv_const(c, 100, 8, false), 1, false),
                     false, 1);
         ck_assert(crv_solve(c));
         const int v = crv_value(c, x);
         ck_assert_msg(!used[v], "cycle %d: %d repeated at step %d", cycle, v, i);
         used[v] = true;
         crv_free(c);
      }
   }
}
END_TEST

START_TEST(test_array_unregistered)
{
   // An array that was never registered fails the solve instead of crashing
   crv_t *c = crv_new();
   crv_var_t x = crv_var(c, 8, false, 0);
   crv_node_t e = crv_array_elem(c, -1, crv_const(c, 0, 32, true));
   crv_require(c, crv_binary(c, CRV_EQ, crv_var_ref(c, x),
                             crv_cast(c, e, 8, false), 1, false), false, 1);
   crv_array_init(c, -1, 0, 5);
   crv_foreach(c, -1, crv_index(c), crv_const(c, 1, 1, false), false);
   ck_assert_int_eq(crv_solve2(c), CRV_UNSAT);
   ck_assert_uint_eq(crv_array_len(c, -1), 0);
   ck_assert(!crv_array_resizable(c, -1));
   crv_free(c);

   // The same through the interface of generated code
   int64_t a[8] = { 0 };
   const int64_t g = crv_generated(CRV_OP_BEGIN, a);
   int64_t b[8] = { g, -1, 0 };
   crv_generated(CRV_OP_ARRAY_ELEM, b);
   int64_t d[8] = { g };
   ck_assert_int_eq(crv_generated(CRV_OP_SOLVE, d), 0);
   crv_generated(CRV_OP_END, d);
}
END_TEST

static void *seeded_object_thread(void *arg)
{
   // Value drawn for an object seeded with 77: its solver is created
   // before (arg == 0) or after the object's stream is selected
   int64_t a[8] = { 0 };
   int64_t g = 0;
   if (!arg)
      g = crv_generated(CRV_OP_BEGIN, a);

   int64_t s[8] = { 77 };
   crv_generated(CRV_OP_SEED_BEGIN, s);
   if (arg)
      g = crv_generated(CRV_OP_BEGIN, a);

   int64_t v[8] = { g, 1, 1, 0, CRV_FLAGS(32, false), 1 };
   const int64_t x = crv_generated(CRV_OP_VAR, v);
   int64_t d[8] = { g };
   if (!crv_generated(CRV_OP_SOLVE, d))
      return (void *)(intptr_t)-1;
   int64_t r[8] = { g, x };
   const int64_t val = crv_generated(CRV_OP_VALUE, r);
   crv_generated(CRV_OP_SEED_END, a);
   crv_generated(CRV_OP_END, d);
   return (void *)(intptr_t)(val + 2);
}

START_TEST(test_seed_before_begin)
{
   // The first solver may be created while the stream of an object is
   // selected without replacing that stream
   void *first, *second;
   pthread_t t;
   ck_assert_int_eq(pthread_create(&t, NULL, seeded_object_thread, NULL), 0);
   pthread_join(t, &first);
   ck_assert_int_eq(pthread_create(&t, NULL, seeded_object_thread, (void *)1), 0);
   pthread_join(t, &second);
   ck_assert_ptr_ne(first, (void *)(intptr_t)-1);
   ck_assert_ptr_eq(first, second);
}
END_TEST

START_TEST(test_randc_trial)
{
   // A trial solve followed by the final one takes one value from the cycle
   // for the pair, so the cycle stays a permutation
   uint64_t state = 0;
   crv_seed(46);
   for (int cycle = 0; cycle < 4; cycle++) {
      bool used[16] = { false };
      for (int i = 0; i < 16; i++) {
         crv_t *c = crv_new();
         crv_var_t x = crv_var(c, 4, false, 0);
         crv_var_randc(c, x, &state);
         crv_var_t y = crv_var(c, 8, false, 0);
         crv_require(c, crv_binary(c, CRV_LT, crv_var_ref(c, y),
                                   crv_const(c, 100, 8, false), 1, false),
                     false, 1);

         // Trial draws leave the cycle alone and may repeat a value
         crv_trial(c, true);
         ck_assert(crv_solve(c));
         ck_assert(crv_solve(c));
         ck_assert(crv_solve(c));

         // The final draw is taken
         ck_assert(crv_solve(c));
         const int v = crv_value(c, x);
         crv_randc_commit(c);
         ck_assert_msg(!used[v], "cycle %d: %d repeated at step %d", cycle, v, i);
         used[v] = true;
         crv_free(c);
      }
   }

   // Without the commit the trial leaves the cycle where it was
   uint64_t state2 = 0;
   for (int i = 0; i < 40; i++) {
      crv_t *c = crv_new();
      crv_var_t x = crv_var(c, 4, false, 0);
      crv_var_randc(c, x, &state2);
      crv_trial(c, true);
      ck_assert(crv_solve(c));
      crv_free(c);
   }
   bool used[16] = { false };
   for (int i = 0; i < 16; i++) {
      crv_t *c = crv_new();
      crv_var_t x = crv_var(c, 4, false, 0);
      crv_var_randc(c, x, &state2);
      ck_assert(crv_solve(c));
      const int v = crv_value(c, x);
      ck_assert(!used[v]);
      used[v] = true;
      crv_free(c);
   }
}
END_TEST

START_TEST(test_seed)
{
   // The same seed gives the same values and push/pop leaves the stream
   uint64_t first[8], second[8];
   for (int run = 0; run < 2; run++) {
      crv_seed(99);
      for (int i = 0; i < 8; i++) {
         crv_t *c = crv_new();
         crv_var_t x = crv_var(c, 32, false, 0);
         ck_assert(crv_solve(c));
         (run ? second : first)[i] = crv_value(c, x);
         crv_free(c);
      }
   }
   ck_assert_mem_eq(first, second, sizeof(first));

   // An object's own stream does not disturb the global one
   uint64_t third[8];
   crv_seed(99);
   for (int i = 0; i < 8; i++) {
      if (i == 3) {
         crv_push_seed(1234);
         crv_t *c = crv_new();
         (void)crv_var(c, 32, false, 0);
         ck_assert(crv_solve(c));
         crv_free(c);
         ck_assert_uint_ne(crv_pop_seed(), 0);
      }
      crv_t *c = crv_new();
      crv_var_t x = crv_var(c, 32, false, 0);
      ck_assert(crv_solve(c));
      third[i] = crv_value(c, x);
      crv_free(c);
   }
   ck_assert_mem_eq(first, third, sizeof(first));
}
END_TEST

////////////////////////////////////////////////////////////////////////////////
// Arrays

START_TEST(test_array_fixed)
{
   // Elements of a fixed array are constrained like variables
   crv_seed(50);
   for (int i = 0; i < 60; i++) {
      crv_t *c = crv_new();
      crv_array_t a = crv_array(c, 8, false, 6, false, true);
      crv_node_t idx = crv_index(c);
      crv_node_t body = crv_binary(c, CRV_LT, crv_array_elem(c, a, idx),
                                   crv_const(c, 20, 8, false), 1, false);
      crv_foreach(c, a, idx, body, false);
      ck_assert(crv_solve(c));
      ck_assert_int_eq(crv_array_len(c, a), 6);
      for (unsigned k = 0; k < 6; k++)
         ck_assert(crv_array_value(c, a, k) < 20);
      crv_free(c);
   }
}
END_TEST

START_TEST(test_array_dynamic)
{
   // A dynamic array of random size 2 to 5 with sorted elements
   int sizes[8] = { 0 };
   crv_seed(51);
   for (int i = 0; i < 400; i++) {
      crv_t *c = crv_new();
      crv_array_t a = crv_array(c, 8, false, 0, true, true);
      crv_node_t size = crv_array_size(c, a);
      crv_require(c, crv_binary(c, CRV_GE, size, crv_const(c, 2, 32, true),
                                1, false), false, 1);
      crv_require(c, crv_binary(c, CRV_LE, size, crv_const(c, 5, 32, true),
                                1, false), false, 2);

      crv_node_t idx = crv_index(c);
      crv_node_t next = crv_binary(c, CRV_ADD, idx, crv_const(c, 1, 32, true),
                                   32, true);
      crv_node_t last = crv_binary(c, CRV_SUB, size, crv_const(c, 1, 32, true),
                                   32, true);
      crv_node_t more = crv_binary(c, CRV_LT, idx, last, 1, false);
      crv_node_t ordered = crv_binary(
         c, CRV_LT, crv_array_elem(c, a, idx),
         crv_array_elem(c, a, next), 1, false);
      crv_foreach(c, a, idx, crv_binary(c, CRV_IMPLIES, more, ordered, 1,
                                        false), false);
      ck_assert(crv_solve(c));

      const unsigned n = crv_array_len(c, a);
      ck_assert_msg(n >= 2 && n <= 5, "size %u", n);
      sizes[n]++;
      for (unsigned k = 1; k < n; k++)
         ck_assert(crv_array_value(c, a, k - 1) < crv_array_value(c, a, k));
      crv_free(c);
   }

   for (int k = 2; k <= 5; k++)
      ck_assert_msg(sizes[k] > 40, "size %d seen %d of 400", k, sizes[k]);
}
END_TEST

START_TEST(test_array_sum)
{
   // The sum of a fixed array is a constraint on all of its elements
   crv_seed(52);
   for (int i = 0; i < 60; i++) {
      crv_t *c = crv_new();
      crv_array_t a = crv_array(c, 8, false, 5, false, true);
      crv_node_t sum = crv_array_sum(c, a, 16, false);
      crv_require(c, crv_binary(c, CRV_EQ, sum, crv_const(c, 1000, 16, false),
                                1, false), false, 1);
      ck_assert(crv_solve(c));
      unsigned total = 0;
      for (unsigned k = 0; k < 5; k++)
         total += crv_array_value(c, a, k);
      ck_assert_int_eq(total, 1000);
      crv_free(c);
   }
}
END_TEST

// The elements decide which sizes are possible: with 8 bit elements below 50
// a sum of 200 needs five of them.  Sizes that have no solution for the
// elements are not tried again
START_TEST(test_array_size_elements)
{
   const crv_engine_t saved = crv_engine();
   crv_set_engine(CRV_ENGINE_AUTO);
   crv_seed(66);
   for (int i = 0; i < 300; i++) {
      crv_t *c = crv_new();
      crv_array_t a = crv_array(c, 8, false, 0, true, true);
      crv_node_t size = crv_array_size(c, a);
      crv_require(c, crv_binary(c, CRV_GE, size, crv_const(c, 1, 32, true),
                                1, false), false, 1);
      crv_require(c, crv_binary(c, CRV_LE, size, crv_const(c, 10, 32, true),
                                1, false), false, 2);
      crv_require(c, crv_binary(c, CRV_EQ, crv_array_sum(c, a, 8, false),
                                crv_const(c, 200, 8, false), 1, false),
                  false, 3);
      crv_node_t idx = crv_index(c);
      crv_foreach(c, a, idx, crv_binary(
                     c, CRV_LT, crv_array_elem(c, a, idx),
                     crv_const(c, 50, 8, false), 1, false), false);
      ck_assert_msg(crv_solve(c), "call %d: no solution", i);

      unsigned sum = 0;
      for (unsigned k = 0; k < crv_array_len(c, a); k++) {
         ck_assert(crv_array_value(c, a, k) < 50);
         sum += crv_array_value(c, a, k);
      }
      ck_assert_int_eq(sum & 255, 200);
      crv_free(c);
   }

   crv_set_engine(saved);
}
END_TEST

// A long chain of additions, as the sum of a method with a clause is lowered:
// propagated by its terms, in every engine
START_TEST(test_sum_chain)
{
   static const crv_engine_t engines[] = { CRV_ENGINE_DFS, CRV_ENGINE_AUTO };
   const crv_engine_t saved = crv_engine();

   for (size_t e = 0; e < ARRAY_LEN(engines); e++) {
      crv_set_engine(engines[e]);
      crv_seed(61 + e);

      for (int trial = 0; trial < 60; trial++) {
         const int n = 16 + trial % 9;
         const bool sgn = trial % 3 == 0;
         const int64_t target = sgn ? (trial % 5) * 50 - 100
            : 40 + trial * 37 % (n * 200);
         crv_t *c = crv_new();
         crv_var_t v[32];
         crv_node_t sum = 0;
         for (int i = 0; i < n; i++) {
            v[i] = crv_var(c, 8, sgn, 0);
            crv_node_t t = crv_cast(c, crv_var_ref(c, v[i]), 32, true);
            sum = i == 0 ? t : crv_binary(c, CRV_ADD, sum, t, 32, true);
         }

         const crv_binary_t rel = trial % 4 == 0 ? CRV_EQ
            : trial % 4 == 1 ? CRV_LE : trial % 4 == 2 ? CRV_GE : CRV_EQ;
         crv_require(c, crv_binary(c, rel, sum,
                                   crv_const(c, target, 32, true), 1, false),
                     false, 1);
         if (trial % 7 == 0)
            crv_require(c, crv_binary(c, CRV_NE, crv_var_ref(c, v[3]),
                                      crv_var_ref(c, v[4]), 1, false),
                        false, 2);

         ck_assert_msg(crv_solve(c), "engine %zu trial %d: no solution", e,
                       trial);
         int64_t total = 0;
         for (int i = 0; i < n; i++)
            total += crv_value(c, v[i]);
         ck_assert(rel == CRV_EQ ? total == target
                   : rel == CRV_LE ? total <= target : total >= target);
         crv_free(c);
      }

      // Out of reach in both directions
      crv_t *c = crv_new();
      crv_node_t sum = 0;
      for (int i = 0; i < 20; i++) {
         crv_node_t t = crv_cast(c, crv_var_ref(c, crv_var(c, 8, false, 0)),
                                 32, true);
         sum = i == 0 ? t : crv_binary(c, CRV_ADD, sum, t, 32, true);
      }
      crv_require(c, crv_binary(c, CRV_GT, sum, crv_const(c, 20 * 255, 32, true),
                                1, false), false, 1);
      ck_assert_int_eq(crv_solve2(c), CRV_UNSAT);
      crv_free(c);
   }

   crv_set_engine(saved);
}
END_TEST

////////////////////////////////////////////////////////////////////////////////
// Problems described apart from the solver
//
// A problem is a set of decision fields and constraints over them held as
// a tree in this file, which is evaluated here with the semantics of the
// language and also turned into the solver's graph.  The solutions are
// found by trying every assignment, so the problem must have at most 16
// decision bits.  From the solutions the distribution that the language
// asks for is worked out: uniform over the solutions, uniform over the
// extendable values of earlier solve before levels and uniform given them,
// the values of a dist by their weight (IEEE 1800 18.5.4) among the feasible
// ones, and the soft
// constraints kept from the highest priority.

typedef enum { X_CONST, X_VAR, X_BIN, X_UN, X_CAST, X_COND, X_INSIDE,
               X_DIST } xkind_t;

#define MAX_ITEMS 4

typedef struct ex {
   xkind_t       kind;
   crv_binary_t  bop;
   crv_unary_t   uop;
   unsigned      w;
   bool          s;
   int64_t       val;
   int           var;
   struct ex    *a, *b, *c;
   int           nitems;
   int64_t       lo[MAX_ITEMS], hi[MAX_ITEMS];    // inside and dist ranges
   int64_t       weight[MAX_ITEMS];
   bool          per_value[MAX_ITEMS];
} ex_t;

static ex_t pool[512];
static int  npool;

static ex_t *newex(xkind_t kind, unsigned w, bool s)
{
   ck_assert_int_lt(npool, (int)ARRAY_LEN(pool));
   ex_t *e = &pool[npool++];
   memset(e, 0, sizeof(*e));
   e->kind = kind;
   e->w = w;
   e->s = s;
   return e;
}

#define MAX_FIELDS 8

typedef struct {
   unsigned  w;
   bool      s;
} field_t;

typedef struct problem {
   const char *name;
   int         nfields;
   field_t     f[MAX_FIELDS];
   int         ncons;
   ex_t       *cons[8];
   bool        soft[8];
   int         nedges, before[4], after[4];
   int         nuniq;
   ex_t       *uniq[4][4];
   int         uniq_n[4];
   bool        array;       // Field 0 is the size of an array held in 1..3
   bool        solvable;
} problem_t;

static ex_t *xconst(int64_t v, unsigned w, bool s)
{
   ex_t *e = newex(X_CONST, w, s);
   e->val = wrapv(v, w, s);
   return e;
}

static ex_t *xvar(const problem_t *p, int i)
{
   ex_t *e = newex(X_VAR, p->f[i].w, p->f[i].s);
   e->var = i;
   return e;
}

static ex_t *xcast(ex_t *a, unsigned w, bool s)
{
   if (a->w == w && a->s == s)
      return a;
   ex_t *e = newex(X_CAST, w, s);
   e->a = a;
   return e;
}

// Operands are converted to the type of the result, but not the amount of a
// shift
static ex_t *xbin(crv_binary_t op, unsigned w, bool s, ex_t *a, ex_t *b)
{
   ex_t *e = newex(X_BIN, w, s);
   e->bop = op;
   switch (op) {
   case CRV_SHL: case CRV_SHR: case CRV_ASR:
      e->a = xcast(a, w, s);
      e->b = b;
      break;
   case CRV_LOG_AND: case CRV_LOG_OR: case CRV_IMPLIES:
      e->a = a;
      e->b = b;
      break;
   case CRV_EQ: case CRV_NE: case CRV_LT: case CRV_LE: case CRV_GT: case CRV_GE:
      {
         // Both sides have the width of the wider and are signed if both are
         const unsigned cw = a->w > b->w ? a->w : b->w;
         const bool cs = a->s && b->s;
         e->a = xcast(a, cw, cs);
         e->b = xcast(b, cw, cs);
         e->w = 1;
         e->s = false;
         break;
      }
   default:
      e->a = xcast(a, w, s);
      e->b = xcast(b, w, s);
      break;
   }
   return e;
}

static ex_t *xun(crv_unary_t op, ex_t *a)
{
   unsigned w = a->w;
   bool s = a->s;
   switch (op) {
   case CRV_LOG_NOT: case CRV_REDUCE_AND: case CRV_REDUCE_OR:
   case CRV_REDUCE_XOR:
      w = 1;
      s = false;
      break;
   case CRV_POPCNT:
      w = 32;
      s = true;
      break;
   default:
      break;
   }
   ex_t *e = newex(X_UN, w, s);
   e->uop = op;
   e->a = a;
   return e;
}

static ex_t *xcond(ex_t *t, ex_t *a, ex_t *b)
{
   const unsigned w = a->w > b->w ? a->w : b->w;
   const bool s = a->s && b->s;
   ex_t *e = newex(X_COND, w, s);
   e->a = t;
   e->b = xcast(a, w, s);
   e->c = xcast(b, w, s);
   return e;
}

static ex_t *xinside(ex_t *a, int n, const int64_t *lo, const int64_t *hi)
{
   ex_t *e = newex(X_INSIDE, 1, false);
   e->a = a;
   e->nitems = n;
   for (int i = 0; i < n; i++) {
      e->lo[i] = lo[i];
      e->hi[i] = hi ? hi[i] : lo[i];
   }
   return e;
}

static ex_t *xdist(ex_t *a, int n, const int64_t *lo, const int64_t *hi,
                   const int64_t *weight, const bool *per_value)
{
   ex_t *e = newex(X_DIST, 1, false);
   e->a = a;
   e->nitems = n;
   for (int i = 0; i < n; i++) {
      e->lo[i] = lo[i];
      e->hi[i] = hi[i];
      e->weight[i] = weight[i];
      e->per_value[i] = per_value[i];
   }
   return e;
}

static ex_t *xnum(crv_binary_t op, ex_t *a, int64_t k)
{
   return xbin(op, 1, false, a, xconst(k, a->w, a->s));
}

typedef struct {
   int64_t v;
   bool    fail;
} xval_t;

// Value of a node, in the representation of its type.  A failed operand
// (division by zero) is unknown: it does not stop a one in an or, or a
// false antecedent of an implication, or a zero in an and.
static xval_t xeval(const ex_t *e, const int64_t *a)
{
   xval_t r = { 0, false };
   switch (e->kind) {
   case X_CONST:
      r.v = e->val;
      return r;
   case X_VAR:
      r.v = a[e->var];
      return r;
   case X_CAST:
      {
         const xval_t x = xeval(e->a, a);
         r.v = wrapv(widen(x.v, e->a->s), e->w, e->s);
         r.fail = x.fail;
         return r;
      }
   case X_UN:
      {
         const xval_t x = xeval(e->a, a);
         const uint64_t u = (uint64_t)x.v & tmask(e->a->w);
         r.fail = x.fail;
         switch (e->uop) {
         case CRV_NEG: r.v = wrapv(-(uint64_t)x.v, e->w, e->s); break;
         case CRV_INV: r.v = wrapv(~(uint64_t)x.v, e->w, e->s); break;
         case CRV_LOG_NOT: r.v = x.v == 0; break;
         case CRV_REDUCE_AND: r.v = u == tmask(e->a->w); break;
         case CRV_REDUCE_OR: r.v = u != 0; break;
         case CRV_REDUCE_XOR: r.v = __builtin_popcountll(u) & 1; break;
         default: r.v = __builtin_popcountll(u); break;
         }
         return r;
      }
   case X_COND:
      {
         const xval_t t = xeval(e->a, a);
         if (t.fail) {
            r.fail = true;
            return r;
         }
         return xeval(t.v ? e->b : e->c, a);
      }
   case X_INSIDE:
      {
         const xval_t x = xeval(e->a, a);
         if (x.fail) {
            r.fail = true;
            return r;
         }
         const __int128 m = widen(x.v, e->a->s);
         for (int i = 0; i < e->nitems; i++) {
            if (m >= e->lo[i] && m <= e->hi[i])
               r.v = 1;
         }
         return r;
      }
   case X_DIST:
      {
         const xval_t x = xeval(e->a, a);
         if (x.fail) {
            r.fail = true;
            return r;
         }
         const __int128 m = widen(x.v, e->a->s);
         for (int i = 0; i < e->nitems; i++) {
            if (e->weight[i] > 0 && m >= e->lo[i] && m <= e->hi[i])
               r.v = 1;
         }
         return r;
      }
   case X_BIN:
      break;
   }

   if (e->bop == CRV_LOG_AND || e->bop == CRV_LOG_OR
       || e->bop == CRV_IMPLIES) {
      const xval_t l = xeval(e->a, a), x = xeval(e->b, a);
      bool lv = l.v != 0;
      if (e->bop == CRV_IMPLIES)
         lv = !lv;
      const bool rv = x.v != 0;
      if (e->bop == CRV_LOG_AND) {
         if ((!l.fail && !lv) || (!x.fail && !rv))
            return r;
         r.fail = l.fail || x.fail;
         r.v = !r.fail;
         return r;
      }
      else {
         if ((!l.fail && lv) || (!x.fail && rv)) {
            r.v = 1;
            return r;
         }
         r.fail = l.fail || x.fail;
         return r;
      }
   }

   const xval_t l = xeval(e->a, a), x = xeval(e->b, a);
   if (l.fail || x.fail) {
      r.fail = true;
      return r;
   }

   switch (e->bop) {
   case CRV_EQ: r.v = widen(l.v, e->a->s) == widen(x.v, e->b->s); return r;
   case CRV_NE: r.v = widen(l.v, e->a->s) != widen(x.v, e->b->s); return r;
   case CRV_LT: r.v = widen(l.v, e->a->s) < widen(x.v, e->b->s); return r;
   case CRV_LE: r.v = widen(l.v, e->a->s) <= widen(x.v, e->b->s); return r;
   case CRV_GT: r.v = widen(l.v, e->a->s) > widen(x.v, e->b->s); return r;
   case CRV_GE: r.v = widen(l.v, e->a->s) >= widen(x.v, e->b->s); return r;
   case CRV_SHL:
   case CRV_SHR:
   case CRV_ASR:
      // The amount is unsigned and of its own width
      r.v = ref_shift(e->bop, e->w, e->s, l.v,
                      (uint64_t)x.v & tmask(e->b->w));
      return r;
   default:
      {
         int64_t out;
         if (!ref_bin(e->bop, e->w, e->s, l.v, x.v, &out)) {
            r.fail = true;
            return r;
         }
         r.v = out;
         return r;
      }
   }
}

static bool xtrue(const ex_t *e, const int64_t *a)
{
   const xval_t v = xeval(e, a);
   return !v.fail && v.v != 0;
}

// Build the solver's graph for a node
typedef struct {
   crv_var_t    var[MAX_FIELDS];
   crv_array_t  arr;
} handles_t;

static crv_node_t xbuild(crv_t *c, const handles_t *h, const ex_t *e)
{
   switch (e->kind) {
   case X_CONST:
      return crv_const(c, e->val, e->w, e->s);
   case X_VAR:
      return crv_var_ref(c, h->var[e->var]);
   case X_CAST:
      return crv_cast(c, xbuild(c, h, e->a), e->w, e->s);
   case X_UN:
      return crv_unary(c, e->uop, xbuild(c, h, e->a));
   case X_COND:
      {
         crv_node_t t = xbuild(c, h, e->a);
         crv_node_t x = xbuild(c, h, e->b), y = xbuild(c, h, e->c);
         return crv_cond(c, t, x, y);
      }
   case X_BIN:
      {
         crv_node_t l = xbuild(c, h, e->a), r = xbuild(c, h, e->b);
         return crv_binary(c, e->bop, l, r, e->w, e->s);
      }
   case X_INSIDE:
      {
         crv_node_t arg = xbuild(c, h, e->a);
         crv_set_t set = crv_set_new(c);
         for (int i = 0; i < e->nitems; i++) {
            crv_node_t lo = crv_const(c, e->lo[i], e->a->w, e->a->s);
            if (e->lo[i] == e->hi[i])
               crv_set_value(c, set, lo);
            else
               crv_set_range(c, set, lo, crv_const(c, e->hi[i], e->a->w,
                                                   e->a->s));
         }
         return crv_inside(c, arg, set);
      }
   case X_DIST:
      {
         crv_node_t arg = xbuild(c, h, e->a);
         crv_dist_t d = crv_dist_new(c);
         for (int i = 0; i < e->nitems; i++)
            crv_dist_range(c, d, e->lo[i], e->hi[i], e->weight[i],
                           e->per_value[i]);
         return crv_dist(c, arg, d);
      }
   }
   should_not_reach_here();
}

// Assignment: one value per field in its own representation.  An array
// problem has the size in field 0 and the elements in the others, the ones
// past the size being zero.
#define ARRAY_MAX 3

static bool hard_holds(const problem_t *p, const int64_t *a)
{
   if (p->array) {
      for (int k = 0; k < ARRAY_MAX; k++) {
         if (k < a[0] && a[1 + k] == 0)
            return false;
         else if (k >= a[0] && a[1 + k] != 0)
            return false;       // Not a canonical assignment
      }
   }

   for (int i = 0; i < p->ncons; i++) {
      if (!p->soft[i] && !xtrue(p->cons[i], a))
         return false;
   }

   for (int u = 0; u < p->nuniq; u++) {
      for (int i = 0; i < p->uniq_n[u]; i++) {
         for (int j = i + 1; j < p->uniq_n[u]; j++) {
            if (xeval(p->uniq[u][i], a).v == xeval(p->uniq[u][j], a).v)
               return false;
         }
      }
   }

   return true;
}

static void build_problem(crv_t *c, const problem_t *p, handles_t *h)
{
   if (p->array) {
      h->arr = crv_array(c, p->f[1].w, false, 0, true, true);
      crv_require(c, crv_binary(c, CRV_LE, crv_array_size(c, h->arr),
                                crv_const(c, ARRAY_MAX, 32, true), 1, false),
                  false, 100);
      crv_node_t idx = crv_index(c);
      crv_node_t body = crv_binary(
         c, CRV_NE, crv_array_elem(c, h->arr, idx),
         crv_const(c, 0, p->f[1].w, false), 1, false);
      crv_foreach(c, h->arr, idx, body, false);
      return;
   }

   for (int i = 0; i < p->nfields; i++)
      h->var[i] = crv_var(c, p->f[i].w, p->f[i].s, 0);

   for (int i = 0; i < p->ncons; i++)
      crv_require(c, xbuild(c, h, p->cons[i]), p->soft[i], i + 1);

   for (int u = 0; u < p->nuniq; u++) {
      crv_node_t n[4];
      for (int i = 0; i < p->uniq_n[u]; i++)
         n[i] = xbuild(c, h, p->uniq[u][i]);
      crv_unique(c, n, p->uniq_n[u]);
   }

   for (int i = 0; i < p->nedges; i++)
      crv_solve_before(c, h->var[p->before[i]], h->var[p->after[i]]);
}

static void read_problem(crv_t *c, const problem_t *p, const handles_t *h,
                         int64_t *a)
{
   if (p->array) {
      const unsigned n = crv_array_len(c, h->arr);
      a[0] = n;
      for (int k = 0; k < ARRAY_MAX; k++)
         a[1 + k] = k < (int)n ? crv_array_value(c, h->arr, k) : 0;
      return;
   }

   for (int i = 0; i < p->nfields; i++)
      a[i] = crv_value(c, h->var[i]);
}

////////////////////////////////////////////////////////////////////////////////
// Enumeration and the distribution the language asks for

typedef struct {
   const problem_t *p;
   int       nbits;
   int       nsol;           // Solutions of the hard constraints and the kept
                             // soft ones
   int64_t  *sol;            // nsol * nfields
   int32_t  *index;          // key -> solution + 1
   double   *prob;
} model_t;

static uint32_t model_key(const problem_t *p, const int64_t *a)
{
   uint32_t key = 0;
   for (int i = 0; i < p->nfields; i++)
      key = (key << p->f[i].w) | (uint32_t)((uint64_t)a[i] & tmask(p->f[i].w));
   return key;
}

static void field_value(const problem_t *p, int i, uint64_t raw, int64_t *a)
{
   a[i] = wrapv(raw, p->f[i].w, p->f[i].s);
}

// A solve before level for each field and the blocks of fields solved
// together, in order
typedef struct {
   unsigned fields;           // Mask of fields
   int      dist;             // Index of the dist node or -1
} block_t;

static int field_level(const problem_t *p, int f, int *deepest)
{
   // The level of a field that is only ever after another is one more than
   // the highest of those before it
   int lvl = 0;
   for (int i = 0; i < p->nedges; i++) {
      if (p->after[i] == f) {
         const int b = field_level(p, p->before[i], deepest) + 1;
         if (b > lvl)
            lvl = b;
      }
   }
   if (lvl > *deepest)
      *deepest = lvl;
   return lvl;
}

static int make_blocks(const problem_t *p, block_t *bl)
{
   if (p->array) {
      // The size is chosen first, among the sizes the elements can have,
      // then the elements: the two phases of the solver
      bl[0] = (block_t){ 1u, -1 };
      bl[1] = (block_t){ ((1u << p->nfields) - 1) & ~1u, -1 };
      return 2;
   }

   int deepest = 0, level[MAX_FIELDS];
   bool mentioned[MAX_FIELDS] = { false };
   for (int i = 0; i < p->nedges; i++)
      mentioned[p->before[i]] = mentioned[p->after[i]] = true;
   for (int f = 0; f < p->nfields; f++)
      level[f] = field_level(p, f, &deepest);
   for (int f = 0; f < p->nfields; f++) {
      // Fields in no solve before are solved with the last
      if (!mentioned[f])
         level[f] = deepest;
   }

   // A dist at the root of a constraint
   int distf[MAX_FIELDS];
   for (int f = 0; f < p->nfields; f++)
      distf[f] = -1;
   for (int i = 0; i < p->ncons; i++) {
      const ex_t *e = p->cons[i];
      if (!p->soft[i] && e->kind == X_DIST && e->a->kind == X_VAR)
         distf[e->a->var] = i;
   }

   int n = 0;
   for (int l = 0; l <= deepest; l++) {
      for (int f = 0; f < p->nfields; f++) {
         if (level[f] == l && distf[f] >= 0)
            bl[n++] = (block_t){ 1u << f, distf[f] };
      }

      unsigned mask = 0;
      for (int f = 0; f < p->nfields; f++) {
         if (level[f] == l && distf[f] < 0)
            mask |= 1u << f;
      }
      if (mask)
         bl[n++] = (block_t){ mask, -1 };
   }
   return n;
}

static const model_t *sort_model;
static unsigned sort_mask;

static int sort_cmp(const void *x, const void *y)
{
   const int a = *(const int *)x, b = *(const int *)y;
   const int nf = sort_model->p->nfields;
   for (int f = 0; f < nf; f++) {
      if (!(sort_mask & (1u << f)))
         continue;
      const int64_t u = sort_model->sol[a * nf + f];
      const int64_t v = sort_model->sol[b * nf + f];
      if (u != v)
         return u < v ? -1 : 1;
   }
   return 0;
}

static bool same_group(const model_t *m, unsigned mask, int a, int b)
{
   const int nf = m->p->nfields;
   for (int f = 0; f < nf; f++) {
      if ((mask & (1u << f)) && m->sol[a * nf + f] != m->sol[b * nf + f])
         return false;
   }
   return true;
}

// Weight of one value of a dist: the first range that holds it
static double dist_value_weight(const ex_t *d, int64_t v)
{
   for (int r = 0; r < d->nitems; r++) {
      if (v >= d->lo[r] && v <= d->hi[r])
         return d->per_value[r] ? (double)d->weight[r]
            : (double)d->weight[r] / (double)(d->hi[r] - d->lo[r] + 1);
   }
   return 0.0;
}

static void spread(const model_t *m, const block_t *bl, int nbl, int b,
                   int *idx, int n, double mass)
{
   if (b == nbl) {
      ck_assert_int_eq(n, 1);
      m->prob[idx[0]] = mass;
      return;
   }

   sort_model = m;
   sort_mask = bl[b].fields;
   qsort(idx, n, sizeof(int), sort_cmp);

   const int nf = m->p->nfields;
   int ngroups = 0;
   for (int i = 0; i < n; i++) {
      if (i == 0 || !same_group(m, bl[b].fields, idx[i - 1], idx[i]))
         ngroups++;
   }

   if (bl[b].dist < 0) {
      for (int i = 0; i < n; ) {
         int j = i + 1;
         while (j < n && same_group(m, bl[b].fields, idx[i], idx[j]))
            j++;
         spread(m, bl, nbl, b + 1, idx + i, j - i, mass / ngroups);
         i = j;
      }
      return;
   }

   // IEEE 1800 18.5.4: every value of a range has the weight of := or the
   // weight of :/ shared over the declared size of the range.  Other
   // constraints remove values and the choice is weighted over the values
   // that are left (those that can be extended), then uniform over the rest
   const ex_t *d = m->p->cons[bl[b].dist];
   const int fld = d->a->var;
   double total = 0;
   for (int i = 0; i < n; ) {
      int j = i + 1;
      while (j < n && same_group(m, bl[b].fields, idx[i], idx[j]))
         j++;
      total += dist_value_weight(d, widen(m->sol[idx[i] * nf + fld],
                                          m->p->f[fld].s));
      i = j;
   }

   for (int i = 0; i < n; ) {
      int j = i + 1;
      while (j < n && same_group(m, bl[b].fields, idx[i], idx[j]))
         j++;
      const double w = dist_value_weight(d, widen(m->sol[idx[i] * nf + fld],
                                                  m->p->f[fld].s));
      if (w > 0)
         spread(m, bl, nbl, b + 1, idx + i, j - i, mass * (w / total));
      i = j;
   }
}

static void model_build(model_t *m, const problem_t *p)
{
   memset(m, 0, sizeof(*m));
   m->p = p;
   const int nf = p->nfields;
   for (int i = 0; i < nf; i++)
      m->nbits += p->f[i].w;
   ck_assert_msg(m->nbits <= 16, "%s: %d decision bits", p->name, m->nbits);

   // Every assignment of the fields
   const uint32_t total = UINT32_C(1) << (m->nbits & 31);
   int64_t *hard = xmalloc_array(total, nf * sizeof(int64_t));
   int nhard = 0;
   for (uint32_t key = 0; key < total; key++) {
      int64_t a[MAX_FIELDS];
      uint32_t k = key;
      for (int i = nf - 1; i >= 0; i--) {
         field_value(p, i, k & tmask(p->f[i].w), a);
         k >>= p->f[i].w;
      }
      if (hard_holds(p, a)) {
         memcpy(hard + (size_t)nhard * nf, a, nf * sizeof(int64_t));
         nhard++;
      }
   }

   // Soft constraints from the highest priority, the last one: each is
   // kept if some solution of everything kept so far satisfies it
   bool *alive = xmalloc_array(nhard ? nhard : 1, 1);
   memset(alive, 1, nhard ? nhard : 1);
   for (int i = p->ncons - 1; i >= 0; i--) {
      if (!p->soft[i])
         continue;

      int n = 0;
      for (int k = 0; k < nhard; k++)
         n += alive[k] && xtrue(p->cons[i], hard + (size_t)k * nf);
      if (n > 0) {
         for (int k = 0; k < nhard; k++)
            alive[k] = alive[k] && xtrue(p->cons[i], hard + (size_t)k * nf);
      }
   }

   m->sol = xmalloc_array(nhard ? nhard : 1, nf * sizeof(int64_t));
   for (int k = 0; k < nhard; k++) {
      if (alive[k])
         memcpy(m->sol + (size_t)m->nsol++ * nf, hard + (size_t)k * nf,
                nf * sizeof(int64_t));
   }
   free(hard);
   free(alive);

   m->index = xcalloc_array(total, sizeof(int32_t));
   for (int k = 0; k < m->nsol; k++)
      m->index[model_key(p, m->sol + (size_t)k * nf)] = k + 1;

   m->prob = xcalloc_array(m->nsol > 0 ? m->nsol : 1, sizeof(double));
   if (m->nsol > 0) {
      block_t bl[2 * MAX_FIELDS];
      const int nbl = make_blocks(p, bl);
      int *idx = xmalloc_array(m->nsol, sizeof(int));
      for (int k = 0; k < m->nsol; k++)
         idx[k] = k;
      spread(m, bl, nbl, 0, idx, m->nsol, 1.0);
      free(idx);
   }
}

static void model_free(model_t *m)
{
   free(m->sol);
   free(m->index);
   free(m->prob);
}

////////////////////////////////////////////////////////////////////////////////
// The problems

static problem_t def_base(const char *name)
{
   npool = 0;
   problem_t p = { .name = name };
   return p;
}

static void add_field(problem_t *p, unsigned w, bool s)
{
   p->f[p->nfields++] = (field_t){ w, s };
}

static void add_cons(problem_t *p, ex_t *e, bool soft)
{
   p->cons[p->ncons] = e;
   p->soft[p->ncons++] = soft;
}

static problem_t def_xlty(unsigned w)
{
   problem_t p = def_base(w == 4 ? "x<y 4" : w == 3 ? "x<y 3" : "x<y 5");
   add_field(&p, w, false);
   add_field(&p, w, false);
   add_cons(&p, xbin(CRV_LT, 1, false, xvar(&p, 0), xvar(&p, 1)), false);
   return p;
}

static problem_t def_sum(int64_t total)
{
   problem_t p = def_base(total == 10 ? "x+y+z==10" : "x+y+z==20");
   for (int i = 0; i < 3; i++)
      add_field(&p, 4, false);
   ex_t *sum = xbin(CRV_ADD, 6, false, xvar(&p, 0), xvar(&p, 1));
   sum = xbin(CRV_ADD, 6, false, sum, xvar(&p, 2));
   add_cons(&p, xbin(CRV_EQ, 1, false, sum, xconst(total, 6, false)), false);
   return p;
}

static problem_t def_impl(bool ordered)
{
   problem_t p = def_base(ordered ? "s->d==0, s before d" : "s->d==0");
   add_field(&p, 1, false);
   add_field(&p, 8, false);
   add_cons(&p, xbin(CRV_IMPLIES, 1, false, xvar(&p, 0),
                     xnum(CRV_EQ, xvar(&p, 1), 0)), false);
   if (ordered) {
      p.before[0] = 0;
      p.after[0] = 1;
      p.nedges = 1;
   }
   return p;
}

static problem_t def_modes(void)
{
   problem_t p = def_base("if/else modes");
   add_field(&p, 2, false);
   add_field(&p, 8, false);
   ex_t *m = xvar(&p, 0), *len = xvar(&p, 1);
   ex_t *a0 = xbin(CRV_IMPLIES, 1, false, xnum(CRV_EQ, m, 0),
                   xnum(CRV_LT, len, 10));
   const int64_t lo[] = { 100 }, hi[] = { 110 };
   ex_t *a1 = xbin(CRV_IMPLIES, 1, false, xnum(CRV_EQ, m, 1),
                   xinside(len, 1, lo, hi));
   ex_t *a2 = xbin(CRV_IMPLIES, 1, false, xnum(CRV_GE, m, 2),
                   xnum(CRV_EQ, len, 255));
   add_cons(&p, a0, false);
   add_cons(&p, a1, false);
   add_cons(&p, a2, false);
   return p;
}

static problem_t def_andz(void)
{
   problem_t p = def_base("(x&y)==0");
   add_field(&p, 4, false);
   add_field(&p, 4, false);
   ex_t *a = xbin(CRV_AND, 4, false, xvar(&p, 0), xvar(&p, 1));
   add_cons(&p, xnum(CRV_EQ, a, 0), false);
   return p;
}

static problem_t def_mul(void)
{
   problem_t p = def_base("x*y==12 mod 16");
   add_field(&p, 4, false);
   add_field(&p, 4, false);
   ex_t *a = xbin(CRV_MUL, 4, false, xvar(&p, 0), xvar(&p, 1));
   add_cons(&p, xnum(CRV_EQ, a, 12), false);
   return p;
}

static problem_t def_uniq(void)
{
   problem_t p = def_base("unique of three 2 bit");
   for (int i = 0; i < 3; i++)
      add_field(&p, 2, false);
   p.nuniq = 1;
   p.uniq_n[0] = 3;
   for (int i = 0; i < 3; i++)
      p.uniq[0][i] = xvar(&p, i);
   return p;
}

static problem_t def_before(void)
{
   problem_t p = def_base("a<b, a before b");
   add_field(&p, 4, false);
   add_field(&p, 4, false);
   add_cons(&p, xbin(CRV_LT, 1, false, xvar(&p, 0), xvar(&p, 1)), false);
   p.before[0] = 0;
   p.after[0] = 1;
   p.nedges = 1;
   return p;
}

static problem_t def_chain(void)
{
   problem_t p = def_base("a<b<c, a before b before c");
   for (int i = 0; i < 3; i++)
      add_field(&p, 3, false);
   add_cons(&p, xbin(CRV_LT, 1, false, xvar(&p, 0), xvar(&p, 1)), false);
   add_cons(&p, xbin(CRV_LT, 1, false, xvar(&p, 1), xvar(&p, 2)), false);
   p.before[0] = 0;
   p.after[0] = 1;
   p.before[1] = 1;
   p.after[1] = 2;
   p.nedges = 2;
   return p;
}

static problem_t def_dist(int variant)
{
   static const char *const names[] = {
      "dist x!=y", "dist, bucket infeasible", "dist, :/ over a split range",
      "dist, values with different extensions", "two dists"
   };
   problem_t p = def_base(names[variant]);
   add_field(&p, 3, false);
   add_field(&p, 3, false);
   ex_t *x = xvar(&p, 0);
   if (variant == 0) {
      const int64_t lo[] = { 0, 1, 4 }, hi[] = { 0, 3, 7 };
      const int64_t w[] = { 1, 6, 1 };
      const bool pv[] = { true, false, true };
      add_cons(&p, xdist(x, 3, lo, hi, w, pv), false);
      add_cons(&p, xbin(CRV_NE, 1, false, x, xvar(&p, 1)), false);
   }
   else if (variant == 1) {
      const int64_t lo[] = { 0, 1, 4 }, hi[] = { 0, 3, 7 };
      const int64_t w[] = { 1, 6, 3 };
      const bool pv[] = { false, false, false };
      add_cons(&p, xdist(x, 3, lo, hi, w, pv), false);
      add_cons(&p, xnum(CRV_LT, x, 4), false);
   }
   else if (variant == 3) {
      // y <= 2x: the values of x can be extended in 1, 3, 5 and 7 ways, and
      // are chosen by their weights alone
      p.f[0].w = 2;
      const int64_t lo[] = { 0, 1, 3 }, hi[] = { 0, 2, 3 };
      const int64_t w[] = { 1, 4, 1 };
      const bool pv[] = { true, false, true };
      add_cons(&p, xdist(x, 3, lo, hi, w, pv), false);
      add_cons(&p, xbin(CRV_LE, 1, false, xvar(&p, 1),
                        xbin(CRV_MUL, 3, false, x, xconst(2, 3, false))),
               false);
   }
   else if (variant == 4) {
      // Each variable by its weights in turn, x first
      p.f[0].w = 2;
      const int64_t lx[] = { 0, 1 }, hx[] = { 0, 1 }, wx[] = { 1, 3 };
      const bool px[] = { true, true };
      add_cons(&p, xdist(x, 2, lx, hx, wx, px), false);
      const int64_t ly[] = { 0, 2 }, hy[] = { 1, 3 }, wy[] = { 1, 6 };
      const bool py[] = { true, false };
      add_cons(&p, xdist(xvar(&p, 1), 2, ly, hy, wy, py), false);
      add_cons(&p, xnum(CRV_NE, xbin(CRV_ADD, 4, false, x, xvar(&p, 1)), 3),
               false);
   }
   else {
      // The second bucket loses its low values to a constraint
      const int64_t lo[] = { 0, 2, 6 }, hi[] = { 1, 5, 7 };
      const int64_t w[] = { 4, 8, 4 };
      const bool pv[] = { false, false, false };
      add_cons(&p, xdist(x, 3, lo, hi, w, pv), false);
      add_cons(&p, xnum(CRV_NE, x, 2), false);
      add_cons(&p, xnum(CRV_NE, x, 3), false);
   }
   return p;
}

static problem_t def_cond(void)
{
   problem_t p = def_base("$countones(x)==2 ? y<4 : y>=12");
   add_field(&p, 4, false);
   add_field(&p, 4, false);
   ex_t *two = xnum(CRV_EQ, xun(CRV_POPCNT, xvar(&p, 0)), 2);
   add_cons(&p, xcond(two, xnum(CRV_LT, xvar(&p, 1), 4),
                      xnum(CRV_GE, xvar(&p, 1), 12)), false);
   return p;
}

static problem_t def_soft(void)
{
   problem_t p = def_base("soft conflict");
   add_field(&p, 4, false);
   add_field(&p, 4, false);
   add_cons(&p, xbin(CRV_LT, 1, false, xvar(&p, 0), xvar(&p, 1)), false);
   add_cons(&p, xnum(CRV_GT, xvar(&p, 0), 10), true);
   add_cons(&p, xnum(CRV_LT, xvar(&p, 1), 8), true);
   return p;
}

static problem_t def_soft_ok(void)
{
   problem_t p = def_base("soft compatible");
   add_field(&p, 4, false);
   add_field(&p, 4, false);
   add_cons(&p, xbin(CRV_NE, 1, false, xvar(&p, 0), xvar(&p, 1)), false);
   add_cons(&p, xnum(CRV_GT, xvar(&p, 0), 9), true);
   add_cons(&p, xnum(CRV_LT, xvar(&p, 1), 6), true);
   return p;
}

static problem_t def_array(void)
{
   problem_t p = def_base("array size 0 to 3, elements nonzero");
   p.array = true;
   add_field(&p, 2, false);
   for (int i = 0; i < ARRAY_MAX; i++)
      add_field(&p, 2, false);
   return p;
}

// total is computed from the others: the case for defined variable
// elimination, without which nearly every sample is rejected
static problem_t def_defined(void)
{
   problem_t p = def_base("total == a + b, a != b");
   add_field(&p, 8, false);
   add_field(&p, 3, false);
   add_field(&p, 3, false);
   ex_t *sum = xbin(CRV_ADD, 8, false, xvar(&p, 1), xvar(&p, 2));
   add_cons(&p, xbin(CRV_EQ, 1, false, xvar(&p, 0), sum), false);
   add_cons(&p, xbin(CRV_NE, 1, false, xvar(&p, 1), xvar(&p, 2)), false);
   return p;
}

// c is in no solve before: it is chosen with b, after a
static problem_t def_unmentioned(void)
{
   problem_t p = def_base("a<b, c<=a, a before b");
   for (int i = 0; i < 3; i++)
      add_field(&p, 3, false);
   add_cons(&p, xbin(CRV_LT, 1, false, xvar(&p, 0), xvar(&p, 1)), false);
   add_cons(&p, xbin(CRV_LE, 1, false, xvar(&p, 2), xvar(&p, 0)), false);
   p.before[0] = 0;
   p.after[0] = 1;
   p.nedges = 1;
   return p;
}

// The two defined by one equality are in different levels, so neither is
// computed from the other
static problem_t def_defined_levels(void)
{
   problem_t p = def_base("x == y + 1, x before y, z != y");
   add_field(&p, 4, false);
   add_field(&p, 4, false);
   add_field(&p, 2, false);
   ex_t *sum = xbin(CRV_ADD, 4, false, xvar(&p, 1), xconst(1, 4, false));
   add_cons(&p, xbin(CRV_EQ, 1, false, xvar(&p, 0), sum), false);
   add_cons(&p, xbin(CRV_NE, 1, false, xvar(&p, 2), xvar(&p, 1)), false);
   p.before[0] = 0;
   p.after[0] = 1;
   p.nedges = 1;
   return p;
}

typedef struct {
   problem_t (*make)(int arg);
   int arg;
} entry_t;

static problem_t mk_xlty(int a) { return def_xlty(a); }
static problem_t mk_sum(int a) { return def_sum(a); }
static problem_t mk_impl(int a) { return def_impl(a); }
static problem_t mk_modes(int a) { (void)a; return def_modes(); }
static problem_t mk_andz(int a) { (void)a; return def_andz(); }
static problem_t mk_mul(int a) { (void)a; return def_mul(); }
static problem_t mk_uniq(int a) { (void)a; return def_uniq(); }
static problem_t mk_before(int a) { (void)a; return def_before(); }
static problem_t mk_chain(int a) { (void)a; return def_chain(); }
static problem_t mk_dist(int a) { return def_dist(a); }
static problem_t mk_cond(int a) { (void)a; return def_cond(); }
static problem_t mk_soft(int a) { (void)a; return def_soft(); }
static problem_t mk_soft_ok(int a) { (void)a; return def_soft_ok(); }
static problem_t mk_array(int a) { (void)a; return def_array(); }
static problem_t mk_defined(int a) { (void)a; return def_defined(); }
static problem_t mk_unm(int a) { (void)a; return def_unmentioned(); }
static problem_t mk_deflev(int a) { (void)a; return def_defined_levels(); }

static const entry_t problems[] = {
   { mk_xlty, 4 }, { mk_xlty, 3 }, { mk_xlty, 5 },
   { mk_sum, 10 }, { mk_sum, 20 },
   { mk_impl, 0 }, { mk_impl, 1 },
   { mk_modes, 0 }, { mk_andz, 0 }, { mk_mul, 0 }, { mk_uniq, 0 },
   { mk_before, 0 }, { mk_chain, 0 },
   { mk_dist, 0 }, { mk_dist, 1 }, { mk_dist, 2 },
   { mk_soft, 0 }, { mk_soft_ok, 0 }, { mk_array, 0 }, { mk_cond, 0 },
   { mk_dist, 3 }, { mk_dist, 4 }, { mk_defined, 0 }, { mk_unm, 0 },
   { mk_deflev, 0 },
};

#define NPROBLEMS ((int)ARRAY_LEN(problems))

////////////////////////////////////////////////////////////////////////////////
// Statistics of a run

// Upper bound of the chi-square distribution with df degrees of freedom at
// p = 1e-4 by the Wilson-Hilferty approximation
static double chi2_bound(int df)
{
   const double z = 3.7190165;     // Normal quantile for 1 - 1e-4
   const double t = 2.0 / (9.0 * df);
   const double u = 1.0 - t + z * sqrt(t);
   return df * u * u * u;
}

typedef struct {
   int      nsol;
   long     n;
   double   chi2;
   int      df;
   double   bound;
   double   ratio;           // Largest count over the smallest of those seen
   int      unreached;       // Solutions with a probability that never came
   long     fails;           // crv_solve returned false
   long     invalid;         // Not a solution of the kept constraints
} result_t;

// Draw n samples from a sampler and compare with the distribution
typedef void (*sampler_t)(const model_t *, void *ctx, int64_t *a, bool *ok);

static result_t run_samples(const model_t *m, long n, sampler_t draw,
                            void *ctx)
{
   result_t r = { .nsol = m->nsol, .n = n };
   long *count = xcalloc_array(m->nsol > 0 ? m->nsol : 1, sizeof(long));
   for (long i = 0; i < n; i++) {
      int64_t a[MAX_FIELDS] = { 0 };
      bool ok = true;
      draw(m, ctx, a, &ok);
      if (!ok) {
         r.fails++;
         continue;
      }

      const int k = m->index[model_key(m->p, a)] - 1;
      if (k < 0)
         r.invalid++;
      else
         count[k]++;
   }

   // Cells with an expected count below 5 are pooled into one
   long ok_n = n - r.fails - r.invalid;
   double pooled_e = 0, pooled_o = 0;
   int cells = 0;
   long cmin = -1, cmax = 0;
   for (int k = 0; k < m->nsol; k++) {
      const double e = m->prob[k] * (double)ok_n;
      if (count[k] == 0 && e > 0)
         r.unreached++;
      if (count[k] > 0) {
         if (cmin < 0 || count[k] < cmin)
            cmin = count[k];
         if (count[k] > cmax)
            cmax = count[k];
      }

      if (e < 5) {
         pooled_e += e;
         pooled_o += (double)count[k];
      }
      else {
         const double d = (double)count[k] - e;
         r.chi2 += d * d / e;
         cells++;
      }
   }
   if (pooled_e >= 5) {
      const double d = pooled_o - pooled_e;
      r.chi2 += d * d / pooled_e;
      cells++;
   }
   r.df = cells > 1 ? cells - 1 : 1;
   r.bound = chi2_bound(r.df);
   r.ratio = cmin > 0 ? (double)cmax / (double)cmin : 0;
   free(count);
   return r;
}

// The solver, forced to one engine
typedef struct {
   crv_engine_t engine;
   handles_t    h;
   crv_result_t result;     // Of the last draw
} solver_ctx_t;

static void solver_draw(const model_t *m, void *ctx, int64_t *a, bool *ok)
{
   solver_ctx_t *sc = ctx;
   crv_set_engine(sc->engine);
   crv_t *c = crv_new();
   build_problem(c, m->p, &sc->h);
   sc->result = crv_solve2(c);
   *ok = sc->result == CRV_SAT;
   if (*ok)
      read_problem(c, m->p, &sc->h, a);
   crv_free(c);
}

// What an exact sampler draws: the distribution of the model itself
typedef struct { trng_t rng; double *cum; } ideal_ctx_t;

static void ideal_draw(const model_t *m, void *ctx, int64_t *a, bool *ok)
{
   ideal_ctx_t *ic = ctx;
   const double u = (double)(trng_next(&ic->rng) >> 11) / 9007199254740992.0;
   int k = 0;
   while (k < m->nsol - 1 && ic->cum[k] <= u)
      k++;
   memcpy(a, m->sol + (size_t)k * m->p->nfields,
          m->p->nfields * sizeof(int64_t));
   *ok = true;
}

static long samples_for(const model_t *m)
{
   return m->nsol * 50L > 20000 ? m->nsol * 50L : 20000;
}

static const uint64_t seeds[3] = { 0x1234, 0xabcd5678, 0x9e3779b9 };

static bool long_mode(void)
{
   const char *e = getenv("NVC_CRV_LONG");
   return e != NULL && *e && strcmp(e, "0") != 0;
}

static void print_result(const char *engine, const problem_t *p,
                         uint64_t seed, const result_t *r)
{
   printf("%-8s %-38s seed %-10"PRIx64" sols %5d n %6ld chi2/df %7.2f "
          "(df %4d bound %7.1f) %s max/min %6.2f unreached %4d fails %ld\n",
          engine, p->name, seed, r->nsol, r->n,
          r->chi2 / r->df, r->df, r->bound,
          r->chi2 <= r->bound ? "pass" : "BIASED", r->ratio, r->unreached,
          r->fails);
}

static void ideal_model(const model_t *m, ideal_ctx_t *ic)
{
   ic->cum = xmalloc_array(m->nsol, sizeof(double));
   double acc = 0;
   for (int k = 0; k < m->nsol; k++) {
      acc += m->prob[k];
      ic->cum[k] = acc;
   }
}

////////////////////////////////////////////////////////////////////////////////
// Tests of the oracle and the harness

START_TEST(test_model_counts)
{
   // Solution counts worked out by hand
   static const struct { int idx; int nsol; } cases[] = {
      { 0, 120 }, { 1, 28 }, { 2, 496 }, { 3, 66 },
      { 5, 257 },    // s=0 with any d, or s=1 with d=0
      { 7, 23 },     // 10 + 11 + 1 + 1
      { 8, 81 },     // x & y == 0: 3^4
      { 10, 24 },    // 4 * 3 * 2
      { 16, 28 },    // x < y < 8
      { 12, 56 },    // a<b<c over 3 bits
      { 18, 1 + 3 + 9 + 27 },
      { 19, 64 },    // 6 values with two ones and y<4, 10 and y>=12
   };

   for (size_t i = 0; i < ARRAY_LEN(cases); i++) {
      problem_t p = problems[cases[i].idx].make(problems[cases[i].idx].arg);
      model_t m;
      model_build(&m, &p);
      ck_assert_msg(m.nsol == cases[i].nsol, "%s: %d solutions, expected %d",
                    p.name, m.nsol, cases[i].nsol);
      model_free(&m);
   }
}
END_TEST

START_TEST(test_model_sum_one)
{
   // The probabilities add up to one
   const problem_t p = problems[_i].make(problems[_i].arg);
   model_t m;
   model_build(&m, &p);
   ck_assert_int_gt(m.nsol, 0);
   double sum = 0;
   for (int k = 0; k < m.nsol; k++) {
      ck_assert(m.prob[k] > 0);
      sum += m.prob[k];
   }
   ck_assert_msg(fabs(sum - 1.0) < 1e-9, "%s: probabilities add up to %g",
                 p.name, sum);
   model_free(&m);
}
END_TEST

START_TEST(test_model_levels)
{
   // Hand worked values of the expectation: s -> d == 0 with s before d gives
   // each s half and d uniform given s
   problem_t p = def_impl(true);
   model_t m;
   model_build(&m, &p);
   double ps1 = 0, ps0 = 0;
   for (int k = 0; k < m.nsol; k++) {
      if (m.sol[k * 2] == 1)
         ps1 += m.prob[k];
      else {
         ps0 += m.prob[k];
         ck_assert_msg(fabs(m.prob[k] - 0.5 / 256) < 1e-12, "p %g", m.prob[k]);
      }
   }
   ck_assert(fabs(ps1 - 0.5) < 1e-12 && fabs(ps0 - 0.5) < 1e-12);
   model_free(&m);

   // Without the order it is uniform over the 257 solutions
   p = def_impl(false);
   model_build(&m, &p);
   for (int k = 0; k < m.nsol; k++)
      ck_assert(fabs(m.prob[k] - 1.0 / 257) < 1e-12);
   model_free(&m);

   // a<b with a before b: a uniform over 0..14 and b uniform above it
   p = def_before();
   model_build(&m, &p);
   for (int k = 0; k < m.nsol; k++) {
      const int a = m.sol[k * 2], b = m.sol[k * 2 + 1];
      ck_assert(fabs(m.prob[k] - 1.0 / 15 / (15 - a)) < 1e-12);
      ck_assert(b > a);
   }
   model_free(&m);

   // Buckets: the infeasible one has no share, the others 1:6 as :/ weights
   p = def_dist(1);
   model_build(&m, &p);
   double p0 = 0;
   for (int k = 0; k < m.nsol; k++) {
      if (m.sol[k * 2] == 0)
         p0 += m.prob[k];
   }
   ck_assert_msg(fabs(p0 - 1.0 / 7) < 1e-12, "bucket %g", p0);
   model_free(&m);
}
END_TEST

START_TEST(test_chi2_bound)
{
   // The bound for 119 degrees of freedom is the 185 of the plan and the
   // known quantiles for a few others are close
   ck_assert(fabs(chi2_bound(119) - 185.2) < 1.0);
   ck_assert(fabs(chi2_bound(65) - 116.0) < 1.5);
   ck_assert(fabs(chi2_bound(1) - 15.14) < 1.5);
}
END_TEST

START_TEST(test_ideal_sampler)
{
   // An exact sampler of the model passes the test for every problem and
   // seed: this checks the model, the pooling and the bound together
   const problem_t p = problems[_i].make(problems[_i].arg);
   model_t m;
   model_build(&m, &p);
   ideal_ctx_t ic;
   ideal_model(&m, &ic);

   const int nseeds = long_mode() ? 3 : 1;
   for (int s = 0; s < nseeds; s++) {
      ic.rng = (trng_t){ seeds[s] + 77 * _i };
      const result_t r = run_samples(&m, samples_for(&m), ideal_draw, &ic);
      ck_assert_msg(r.chi2 <= r.bound, "%s: ideal sampler chi2 %.1f above "
                    "%.1f (df %d)", p.name, r.chi2, r.bound, r.df);
      ck_assert_int_eq(r.unreached, 0);
   }
   free(ic.cum);
   model_free(&m);
}
END_TEST

START_TEST(test_harness_detects_bias)
{
   // The first solution found by a deterministic search repeated, or the
   // search on x<y, are far from uniform and the test says so
   const problem_t p = def_xlty(4);
   model_t m;
   model_build(&m, &p);

   solver_ctx_t sc = { .engine = CRV_ENGINE_DFS };
   crv_seed(seeds[0]);
   const result_t r = run_samples(&m, samples_for(&m), solver_draw, &sc);
   ck_assert_int_eq(r.fails, 0);
   ck_assert_int_eq(r.invalid, 0);
   ck_assert_msg(r.chi2 > 4 * r.bound, "chi2 %.1f of %d degrees is not "
                 "biased: bound %.1f", r.chi2, r.df, r.bound);
   model_free(&m);
}
END_TEST

// An engine that answers exactly must pass for every problem it takes
START_TEST(test_exact_stages)
{
   static const crv_engine_t exact[] = { CRV_ENGINE_REJECT, CRV_ENGINE_BDD };
   const problem_t p = problems[_i].make(problems[_i].arg);
   model_t m;
   model_build(&m, &p);

   const int nseeds = long_mode() ? 3 : 1;
   for (size_t e = 0; e < ARRAY_LEN(exact); e++) {
      if (!crv_engine_available(exact[e]))
         continue;

      for (int s = 0; s < nseeds; s++) {
         solver_ctx_t sc = { .engine = exact[e] };
         crv_seed(seeds[s]);
         const result_t r = run_samples(&m, samples_for(&m), solver_draw, &sc);
         ck_assert_int_eq(r.invalid, 0);
         // The bdd stage takes every problem of the harness
         if (exact[e] == CRV_ENGINE_BDD)
            ck_assert_msg(r.fails == 0, "%s: bdd declined %ld draws", p.name,
                          r.fails);
         ck_assert_msg(r.chi2 <= r.bound, "%s with %s seed %d: chi2 %.1f "
                       "above %.1f", p.name, crv_engine_name(exact[e]), s,
                       r.chi2, r.bound);
      }
   }
   model_free(&m);
}
END_TEST

// Every stage gives only solutions, whatever its distribution
START_TEST(test_sound_all_engines)
{
   static const crv_engine_t all[] = { CRV_ENGINE_DFS,
                                       CRV_ENGINE_REJECT, CRV_ENGINE_BDD,
                                       CRV_ENGINE_SAT, CRV_ENGINE_AUTO };
   const problem_t p = problems[_i].make(problems[_i].arg);
   model_t m;
   model_build(&m, &p);

   for (size_t e = 0; e < ARRAY_LEN(all); e++) {
      if (!crv_engine_available(all[e]))
         continue;

      solver_ctx_t sc = { .engine = all[e] };
      crv_seed(seeds[1]);
      const result_t r = run_samples(&m, 600, solver_draw, &sc);
      ck_assert_msg(r.invalid == 0 && r.fails == 0,
                    "%s with %s: %ld invalid %ld failed", p.name,
                    crv_engine_name(all[e]), r.invalid, r.fails);
   }
   model_free(&m);
}
END_TEST

// The bias report: not asserted, printed in long mode or when asked for
START_TEST(test_bias_report)
{
   static const crv_engine_t all[] = { CRV_ENGINE_DFS,
                                       CRV_ENGINE_REJECT, CRV_ENGINE_BDD,
                                       CRV_ENGINE_SAT, CRV_ENGINE_AUTO };
   const bool report = long_mode() || getenv("NVC_CRV_REPORT") != NULL;
   if (!report)
      return;

   const int nseeds = long_mode() ? 3 : 1;
   const problem_t p = problems[_i].make(problems[_i].arg);
   model_t m;
   model_build(&m, &p);

   for (size_t e = 0; e < ARRAY_LEN(all); e++) {
      if (!crv_engine_available(all[e]))
         continue;

      for (int s = 0; s < nseeds; s++) {
         solver_ctx_t sc = { .engine = all[e] };
         crv_seed(seeds[s]);
         const result_t r = run_samples(&m, samples_for(&m), solver_draw, &sc);
         print_result(crv_engine_name(all[e]), &p, seeds[s], &r);
      }
   }
   fflush(stdout);
   model_free(&m);
}
END_TEST

////////////////////////////////////////////////////////////////////////////////
// Differential fuzzer
//
// Random small problems: two to five fields of one to six bits, constraints
// built from every operator, some soft, some with an order or a unique set.
// The oracle lists their solutions by enumeration, so for each problem the
// solver must find a solution exactly when one exists, the solution must be
// one of the kept ones, and an unsatisfiable problem must be reported.  The
// seed of every problem is printed with a failure and can be replayed with
// NVC_CRV_FUZZ_SEED, together with a listing of the constraints.

typedef struct {
   trng_t r;
   const problem_t *p;
} gen_t;

static ex_t *gen_val(gen_t *g, int depth);

static unsigned gen_below(gen_t *g, unsigned n)
{
   return trng_below(&g->r, n);
}

static ex_t *gen_leaf(gen_t *g)
{
   if (gen_below(g, 4) < 3 && g->p->nfields > 0)
      return xvar(g->p, gen_below(g, g->p->nfields));

   const unsigned w = 1 + gen_below(g, 8);
   return xconst(trng_next(&g->r), w, gen_below(g, 3) == 0);
}

static ex_t *gen_bool(gen_t *g, int depth)
{
   switch (depth <= 0 ? gen_below(g, 3) : gen_below(g, 9)) {
   case 0: case 1: case 2:
      {
         static const crv_binary_t ops[] = { CRV_EQ, CRV_NE, CRV_LT, CRV_LE,
                                             CRV_GT, CRV_GE };
         ex_t *a = gen_val(g, depth - 1), *b = gen_val(g, depth - 1);
         return xbin(ops[gen_below(g, 6)], 1, false, a, b);
      }
   case 3:
      {
         ex_t *a = gen_val(g, depth - 1);
         const int n = 1 + gen_below(g, 3);
         int64_t lo[3], hi[3];
         for (int i = 0; i < n; i++) {
            const int64_t x = wrapv(trng_next(&g->r), a->w, a->s);
            const int64_t y = wrapv(trng_next(&g->r), a->w, a->s);
            const __int128 mx = widen(x, a->s), my = widen(y, a->s);
            lo[i] = mx < my ? x : y;
            hi[i] = mx < my ? y : x;
            if (gen_below(g, 2) == 0)
               hi[i] = lo[i];
         }
         return xinside(a, n, lo, hi);
      }
   case 4:
      return xnum(CRV_NE, gen_val(g, depth - 1), 0);
   case 5:
      return xun(CRV_LOG_NOT, gen_bool(g, depth - 1));
   case 6: case 7:
      {
         static const crv_binary_t ops[] = { CRV_LOG_AND, CRV_LOG_OR,
                                             CRV_IMPLIES };
         ex_t *a = gen_bool(g, depth - 1), *b = gen_bool(g, depth - 1);
         return xbin(ops[gen_below(g, 3)], 1, false, a, b);
      }
   default:
      {
         static const crv_unary_t ops[] = { CRV_REDUCE_AND, CRV_REDUCE_OR,
                                            CRV_REDUCE_XOR };
         return xun(ops[gen_below(g, 3)], gen_val(g, depth - 1));
      }
   }
}

static ex_t *gen_val(gen_t *g, int depth)
{
   if (depth <= 0)
      return gen_leaf(g);

   switch (gen_below(g, 12)) {
   case 0: case 1:
      return gen_leaf(g);
   case 2:
      {
         static const crv_unary_t ops[] = { CRV_NEG, CRV_INV, CRV_POPCNT };
         return xun(ops[gen_below(g, 3)], gen_val(g, depth - 1));
      }
   case 3:
      return xcast(gen_val(g, depth - 1), 1 + gen_below(g, 12),
                   gen_below(g, 2));
   case 4:
      {
         ex_t *t = gen_bool(g, depth - 1);
         ex_t *a = gen_val(g, depth - 1), *b = gen_val(g, depth - 1);
         return xcond(t, a, b);
      }
   default:
      {
         static const crv_binary_t ops[] = {
            CRV_ADD, CRV_SUB, CRV_MUL, CRV_AND, CRV_OR, CRV_XOR, CRV_SHL,
            CRV_SHR, CRV_ASR, CRV_DIV, CRV_MOD, CRV_ADD, CRV_AND,
         };
         ex_t *a = gen_val(g, depth - 1), *b = gen_val(g, depth - 1);
         unsigned w = a->w > b->w ? a->w : b->w;
         if (gen_below(g, 4) == 0 && w < 12)
            w += 1 + gen_below(g, 4);
         const bool sg = gen_below(g, 3) == 0 ? gen_below(g, 2)
            : (a->s && b->s);
         return xbin(ops[gen_below(g, ARRAY_LEN(ops))], w, sg, a, b);
      }
   }
}

static void xprint(const ex_t *e, FILE *f)
{
   static const char *const bops[] = {
      "+", "-", "*", "/", "%", "&", "|", "^", "<<", ">>", ">>>", "==", "!=",
      "<", "<=", ">", ">=", "&&", "||", "->"
   };
   static const char *const uops[] = {
      "-", "~", "!", "&", "|", "^", "$countones"
   };
   switch (e->kind) {
   case X_CONST: fprintf(f, "%u'%sd%"PRId64, e->w, e->s ? "s" : "", e->val);
      break;
   case X_VAR: fprintf(f, "f%d", e->var); break;
   case X_CAST: fprintf(f, "%u'%s(", e->w, e->s ? "s" : "");
      xprint(e->a, f);
      fprintf(f, ")");
      break;
   case X_UN: fprintf(f, "%s(", uops[e->uop]);
      xprint(e->a, f);
      fprintf(f, ")");
      break;
   case X_COND: fprintf(f, "(");
      xprint(e->a, f);
      fprintf(f, " ? ");
      xprint(e->b, f);
      fprintf(f, " : ");
      xprint(e->c, f);
      fprintf(f, ")");
      break;
   case X_BIN: fprintf(f, "(");
      xprint(e->a, f);
      fprintf(f, " %s[%u%s] ", bops[e->bop], e->w, e->s ? "s" : "");
      xprint(e->b, f);
      fprintf(f, ")");
      break;
   case X_INSIDE: xprint(e->a, f);
      fprintf(f, " inside {");
      for (int i = 0; i < e->nitems; i++)
         fprintf(f, "%s[%"PRId64":%"PRId64"]", i ? ", " : "", e->lo[i],
                 e->hi[i]);
      fprintf(f, "}");
      break;
   case X_DIST: xprint(e->a, f);
      fprintf(f, " dist {...}");
      break;
   }
}

static void print_problem(const problem_t *p, FILE *f)
{
   for (int i = 0; i < p->nfields; i++)
      fprintf(f, "  rand bit %s [%u] f%d\n", p->f[i].s ? "signed" : "",
              p->f[i].w, i);
   for (int i = 0; i < p->ncons; i++) {
      fprintf(f, "  constraint %s ", p->soft[i] ? "soft" : "hard");
      xprint(p->cons[i], f);
      fprintf(f, "\n");
   }
   for (int i = 0; i < p->nedges; i++)
      fprintf(f, "  solve f%d before f%d\n", p->before[i], p->after[i]);
   for (int u = 0; u < p->nuniq; u++) {
      fprintf(f, "  unique {");
      for (int i = 0; i < p->uniq_n[u]; i++) {
         xprint(p->uniq[u][i], f);
         fprintf(f, ", ");
      }
      fprintf(f, "}\n");
   }
}

static problem_t fuzz_problem(uint64_t seed)
{
   problem_t p = def_base("fuzz");
   gen_t g = { .r = { seed }, .p = &p };

   // Widths such that the enumeration stays small
   const int nf = 2 + gen_below(&g, 4);
   int budget = 13;
   for (int i = 0; i < nf; i++) {
      unsigned w = 1 + gen_below(&g, 6);
      const int left = budget - (nf - 1 - i);
      if ((int)w > left)
         w = left;
      budget -= w;
      add_field(&p, w, gen_below(&g, 3) == 0);
   }

   const int ncons = 1 + gen_below(&g, 3);
   for (int i = 0; i < ncons; i++)
      add_cons(&p, gen_bool(&g, 3), i > 0 && gen_below(&g, 4) == 0);

   if (gen_below(&g, 6) == 0 && p.nfields > 1) {
      p.before[0] = gen_below(&g, p.nfields);
      p.after[0] = (p.before[0] + 1 + gen_below(&g, p.nfields - 1))
         % p.nfields;
      p.nedges = 1;
   }

   if (gen_below(&g, 8) == 0 && p.nfields > 1) {
      // Unique over two or three fields of the same width and signedness
      int n = 0;
      for (int i = 0; i < p.nfields && n < 3; i++) {
         if (n == 0 || (p.f[i].w == p.uniq[0][0]->w
                        && p.f[i].s == p.uniq[0][0]->s))
            p.uniq[0][n++] = xvar(&p, i);
      }
      if (n > 1) {
         p.nuniq = 1;
         p.uniq_n[0] = n;
      }
   }

   return p;
}

typedef struct {
   int  problems, with_solutions, unsat, draws;
   int  incomplete;    // Solutions exist and the solver said there are none
   int  invalid;       // An answer which breaks a hard constraint
   int  soft_lost;     // A soft constraint dropped that could be kept
   int  false_sat;     // Unsatisfiable and the solver gave an answer
   int  declined;      // A stage that cannot prove anything gave up
} fuzz_stats_t;

static bool fuzz_run_one(uint64_t seed, const crv_engine_t *engines,
                         int nengines, fuzz_stats_t *st, bool verbose)
{
   problem_t p = fuzz_problem(seed);
   model_t m;
   model_build(&m, &p);

   st->problems++;
   if (m.nsol > 0)
      st->with_solutions++;
   else
      st->unsat++;

   bool good = true;
   for (int e = 0; e < nengines; e++) {
      for (int draw = 0; draw < 3; draw++) {
         solver_ctx_t sc = { .engine = engines[e] };
         int64_t a[MAX_FIELDS] = { 0 };
         bool ok;
         crv_seed(seed ^ (draw * UINT64_C(0x9e3779b97f4a7c15)));
         solver_draw(&m, &sc, a, &ok);
         st->draws++;

         // The rejection stage on its own finds a solution or gives up: not
         // finding one is not a fault, and a soft constraint can be lost
         // because a trial was given up
         const bool partial = engines[e] == CRV_ENGINE_REJECT;
         const char *what = NULL;
         if (partial && !ok && m.nsol > 0 && sc.result == CRV_GIVEUP)
            st->declined++;
         else if (partial && ok && m.nsol > 0
                  && m.index[model_key(&p, a)] == 0 && hard_holds(&p, a))
            st->declined++;
         else if (ok && m.nsol == 0) {
            st->false_sat++;
            what = "a solution for an unsatisfiable problem";
         }
         else if (!ok && m.nsol > 0) {
            st->incomplete++;
            what = sc.result == CRV_GIVEUP
               ? "no solution for a satisfiable one (the search gave up)"
               : "no solution for a satisfiable one (said to be unsatisfiable)";
         }
         else if (ok && m.index[model_key(&p, a)] == 0) {
            if (hard_holds(&p, a)) {
               // A solution, but a soft constraint was dropped that could
               // have been kept
               st->soft_lost++;
               what = "a solution that drops a soft constraint it could keep";
            }
            else {
               st->invalid++;
               what = "a value that is not a solution";
            }
         }

         if (what != NULL) {
            // Every engine that fails is named, the problem is listed once
            if (verbose) {
               printf("fuzz seed %"PRIu64" engine %s draw %d: %s "
                      "(%d solutions)\n", seed, crv_engine_name(engines[e]),
                      draw, what, m.nsol);
               if (good)
                  print_problem(&p, stdout);
               fflush(stdout);
            }
            good = false;
         }
      }
   }

   model_free(&m);
   return good;
}

static double now_seconds(void)
{
   struct timespec ts;
   clock_gettime(CLOCK_MONOTONIC, &ts);
   return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static int fuzz_engines(crv_engine_t *engines)
{
   // Every stage that exists is checked against the oracle
   static const crv_engine_t all[] = { CRV_ENGINE_DFS,
                                       CRV_ENGINE_REJECT, CRV_ENGINE_BDD,
                                       CRV_ENGINE_SAT, CRV_ENGINE_AUTO };
   int n = 0;
   for (size_t i = 0; i < ARRAY_LEN(all); i++) {
      if (crv_engine_available(all[i]))
         engines[n++] = all[i];
   }
   return n;
}

// The comparison of the bits of a variable with a constant, for every width
// and constant up to 6 bits, signed or not, with the bits in any order
START_TEST(test_bdd_cmp_const)
{
   for (unsigned w = 1; w <= 6; w++) {
      for (int order = 0; order < 3; order++) {
         // The bits of the variable are spread among 2 w positions: least
         // significant first, most significant first, or mixed
         unsigned pos[6];
         for (unsigned i = 0; i < w; i++) {
            const unsigned k = order == 0 ? i : order == 1 ? w - 1 - i
               : (i * 5) % w;
            pos[i] = 2 * k + (k & 1);
         }
         if (order == 2) {
            // (i * 5) % w is a permutation only when 5 and w are coprime
            bool seen[6] = { false };
            bool perm = true;
            for (unsigned i = 0; i < w; i++) {
               const unsigned k = (i * 5) % w;
               perm &= !seen[k];
               seen[k] = true;
            }
            if (!perm)
               continue;
         }

         for (int sgn = 0; sgn < 2; sgn++) {
            for (int ge = 0; ge < 2; ge++) {
               for (unsigned cc = 0; cc < (1u << w); cc++) {
                  bdd_t *b = bdd_open(2 * w, 100000);
                  const bnode_t f = bdd_cmp_const(b, pos, w, sgn, cc, ge);
                  ck_assert(!bdd_over(b));
                  uint8_t fix[16], bits[16];
                  memset(fix, 1, sizeof(fix));
                  for (unsigned x = 0; x < (1u << w); x++) {
                     memset(bits, 0, sizeof(bits));
                     for (unsigned i = 0; i < w; i++)
                        bits[pos[i]] = (x >> i) & 1;
                     const bnode_t r = bdd_cofactor(b, f, fix, bits);
                     // Signed: the pattern with the top bit inverted compares
                     const unsigned u = sgn ? x ^ (1u << (w - 1)) : x;
                     const bool want = ge ? u >= cc : u <= cc;
                     ck_assert_msg(r == (want ? BDD_TRUE : BDD_FALSE),
                                   "w %u order %d sgn %d ge %d c %u x %u", w,
                                   order, sgn, ge, cc, x);
                  }
                  bdd_close(b);
               }
            }
         }
      }
   }
}
END_TEST

// Wide variables in a scrambled order of bits (a slice of a variable
// that a constraint mentions comes before the rest): the comparison stays
// small
START_TEST(test_bdd_cmp_scrambled)
{
   trng_t rng = { 4242 };
   for (unsigned w = 8; w <= 64; w *= 2) {
      for (int trial = 0; trial < 30; trial++) {
         unsigned pos[64];
         for (unsigned i = 0; i < w; i++)
            pos[i] = i;
         for (unsigned i = w - 1; i > 0; i--) {
            const unsigned j = trng_below(&rng, i + 1);
            const unsigned t = pos[i];
            pos[i] = pos[j];
            pos[j] = t;
         }
         const uint64_t c = trng_next(&rng) & tmask(w);
         const bool ge = trial & 1, sgn = trial & 2;
         bdd_t *b = bdd_open(w, 100000);
         const bnode_t f = bdd_cmp_const(b, pos, w, sgn, c, ge);
         ck_assert(!bdd_over(b));
         ck_assert_msg(bdd_nodes(b) < 4 * w * w, "w %u: %u nodes", w,
                       bdd_nodes(b));
         uint8_t fix[64], bits[64];
         memset(fix, 1, sizeof(fix));
         for (int k = 0; k < 100; k++) {
            const uint64_t x = trng_next(&rng) & tmask(w);
            for (unsigned i = 0; i < w; i++)
               bits[pos[i]] = (x >> i) & 1;
            const uint64_t u = sgn ? x ^ (UINT64_C(1) << (w - 1)) : x;
            const bool want = ge ? u >= c : u <= c;
            ck_assert(bdd_cofactor(b, f, fix, bits)
                      == (want ? BDD_TRUE : BDD_FALSE));
         }
         bdd_close(b);
      }
   }
}
END_TEST

// A product of two 8 bit values needs a BDD of more than the budget: the
// stage gives up and the next one answers exactly as it does without the
// stage; wider products are not tried
START_TEST(test_bdd_budget)
{
   const crv_engine_t saved = crv_engine();
   const unsigned nodes = crv_bdd_nodes();
   crv_set_engine(CRV_ENGINE_AUTO);

   int64_t got[3][2] = { { 0 } };
   for (int pass = 0; pass < 3; pass++) {
      // pass 0: the default budget, 1: no bdd stage, 2: a smaller budget
      crv_set_bdd_nodes(pass == 0 ? 20000 : pass == 1 ? 0 : 2000);
      crv_t *c = crv_new();
      crv_var_t x = crv_var(c, 8, false, 0), y = crv_var(c, 8, false, 0);
      crv_require(c, crv_binary(c, CRV_EQ, crv_binary(
                                   c, CRV_MUL, crv_var_ref(c, x),
                                   crv_var_ref(c, y), 16, false),
                                crv_const(c, 60000, 16, false), 1, false),
                  false, 1);
      crv_stats_force(true);
      crv_seed(77);
      const crv_result_t r = crv_solve2(c);
      const crv_call_stat_t *st = crv_stats_last();
      ck_assert_int_eq(r, CRV_SAT);
      ck_assert_int_eq(crv_value(c, x) * crv_value(c, y), 60000);
      ck_assert_int_eq(st->top, CRV_STAGE_DFS);
      got[pass][0] = crv_value(c, x);
      got[pass][1] = crv_value(c, y);
      if (pass == 1)
         ck_assert_int_eq(st->bdd_declined[CRV_BDD_BUDGET], 0);
      else {
         ck_assert_int_eq(st->bdd_declined[CRV_BDD_BUDGET], 1);
         ck_assert_int_eq(st->declined[1], 1);
         ck_assert_msg(st->bdd_bytes < (1u << 20), "%u bytes", st->bdd_bytes);
      }
      crv_free(c);
   }

   // The same values whatever the stage that gave up did
   for (int pass = 1; pass < 3; pass++) {
      ck_assert_int_eq(got[pass][0], got[0][0]);
      ck_assert_int_eq(got[pass][1], got[0][1]);
   }

   // Wider products are not tried at all
   crv_set_bdd_nodes(nodes);
   crv_t *c = crv_new();
   crv_var_t x = crv_var(c, 12, false, 0), y = crv_var(c, 12, false, 0);
   crv_require(c, crv_binary(c, CRV_EQ, crv_binary(
                                c, CRV_MUL, crv_var_ref(c, x),
                                crv_var_ref(c, y), 24, false),
                             crv_const(c, 1000000, 24, false), 1, false),
               false, 1);
   crv_stats_force(true);
   ck_assert_int_eq(crv_solve2(c), CRV_SAT);
   ck_assert_int_eq(crv_stats_last()->bdd_declined[CRV_BDD_NONLINEAR], 1);
   ck_assert_int_eq(crv_value(c, x) * crv_value(c, y), 1000000);
   crv_free(c);

   crv_set_engine(saved);
   crv_set_bdd_nodes(nodes);
}
END_TEST

// Values for a seed depend on the problem and the seed alone: not on how
// large the node limit is when the problem fits in it, and not on the build
// (the golden values below are the same in the release and the ASan builds)
static void bdd_draws(const problem_t *p, uint64_t seed, int n, int64_t *out)
{
   model_t m = { .p = p };
   solver_ctx_t sc = { .engine = CRV_ENGINE_BDD };
   crv_seed(seed);
   for (int i = 0; i < n; i++) {
      bool ok;
      solver_draw(&m, &sc, out + i * p->nfields, &ok);
      ck_assert(ok);
   }
}

START_TEST(test_bdd_seed_stable)
{
   const unsigned nodes = crv_bdd_nodes();
   const crv_engine_t saved = crv_engine();
   const unsigned budgets[] = { 2000, 20000, 400000 };
   for (int pi = 0; pi < NPROBLEMS; pi++) {
      const problem_t p = problems[pi].make(problems[pi].arg);
      int64_t base[4 * MAX_FIELDS], other[4 * MAX_FIELDS];
      crv_set_bdd_nodes(budgets[1]);
      bdd_draws(&p, 0x51ed, 4, base);
      for (size_t b = 0; b < ARRAY_LEN(budgets); b += 2) {
         crv_set_bdd_nodes(budgets[b]);
         bdd_draws(&p, 0x51ed, 4, other);
         ck_assert_msg(!memcmp(base, other, sizeof(int64_t) * 4 * p.nfields),
                       "%s: values change with the node limit %u", p.name,
                       budgets[b]);
      }
   }

   // Values of fixed seeds
   static const struct {
      int      problem;
      int64_t  v[8];
   } golden[] = {
      { 0, { 3, 8, 2, 4, 10, 12, 8, 10 } },
      { 11, { 0, 4, 12, 14, 14, 15, 6, 13 } },
      { 13, { 1, 6, 3, 7, 6, 7, 4, 1 } },
      { 14, { 1, 6, 3, 3, 0, 5, 0, 3 } },
      { 15, { 1, 6, 5, 3, 1, 5, 0, 0 } },
      { 20, { 0, 0, 3, 6, 1, 1, 1, 2 } },
      { 21, { 0, 2, 1, 1, 1, 3, 0, 2 } },
   };
   const bool print_golden = getenv("NVC_CRV_PRINT_GOLDEN") != NULL;
   crv_set_bdd_nodes(nodes);
   for (size_t i = 0; i < ARRAY_LEN(golden); i++) {
      const problem_t p = problems[golden[i].problem].make(
         problems[golden[i].problem].arg);
      int64_t got[4 * MAX_FIELDS];
      bdd_draws(&p, 0x51ed, 4, got);
      if (print_golden) {
         for (int k = 0; k < 4 * p.nfields; k++)
            printf("%"PRIi64", ", got[k]);
         printf("\n");
         continue;
      }
      for (int k = 0; k < 4 * p.nfields; k++)
         ck_assert_msg(got[k] == golden[i].v[k], "%s: value %d is %"PRIi64
                       ", expected %"PRIi64, p.name, k, got[k],
                       golden[i].v[k]);
   }
   crv_set_engine(saved);
}
END_TEST

// The BDD of the hard constraints counts the solutions and unranking from 0
// to the count lists each of them once: the stage is exact if this holds,
// whatever the random numbers do
// Returns false if the stage declined the problem: a product of two big
// expressions or a long sum is for the search
static bool check_bdd_bijection(const problem_t *p, const char *what,
                                bool may_decline)
{
   if (p->array)
      return true;

   const int nf = p->nfields;
   int nbits = 0;
   for (int i = 0; i < nf; i++)
      nbits += p->f[i].w;
   ck_assert_int_le(nbits, 16);
   const uint32_t total = UINT32_C(1) << nbits;

   // Every solution of the hard constraints
   uint8_t *hard = xcalloc(total);
   uint64_t nhard = 0;
   for (uint32_t key = 0; key < total; key++) {
      int64_t a[MAX_FIELDS];
      uint32_t k = key;
      for (int i = nf - 1; i >= 0; i--) {
         field_value(p, i, k & tmask(p->f[i].w), a);
         k >>= p->f[i].w;
      }
      if (hard_holds(p, a)) {
         hard[model_key(p, a)] = 1;
         nhard++;
      }
   }

   crv_t *c = crv_new();
   handles_t h;
   build_problem(c, p, &h);
   int64_t *vals = xmalloc_array(nhard + 1, nf * sizeof(int64_t));
   uint64_t count = 0;
   if (!crv_bdd_enumerate(c, nhard, &count, vals)) {
      ck_assert_msg(may_decline, "%s: the stage declined", what);
      free(vals);
      free(hard);
      crv_free(c);
      return false;
   }
   ck_assert_msg(count == nhard, "%s: BDD counts %"PRIu64" solutions, "
                 "enumeration %"PRIu64, what, count, nhard);

   uint8_t *seen = xcalloc(total);
   for (uint64_t r = 0; r < count; r++) {
      const uint32_t key = model_key(p, vals + r * nf);
      ck_assert_msg(hard[key], "%s: unrank %"PRIu64" is not a solution", what, r);
      ck_assert_msg(!seen[key], "%s: unrank %"PRIu64" repeats a solution", what,
                    r);
      seen[key] = 1;
   }

   free(seen);
   free(vals);
   free(hard);
   crv_free(c);
   return true;
}

START_TEST(test_bdd_bijection_problems)
{
   const problem_t p = problems[_i].make(problems[_i].arg);
   check_bdd_bijection(&p, p.name, false);
}
END_TEST

START_TEST(test_bdd_bijection_fuzz)
{
   const uint64_t base = getenv("NVC_CRV_FUZZ_SEED")
      ? strtoull(getenv("NVC_CRV_FUZZ_SEED"), NULL, 0) : 1000;
   const int n = long_mode() ? 20000 : 600;
   int declined = 0;
   for (int i = 0; i < n; i++) {
      const problem_t p = fuzz_problem(base + i);
      char what[64];
      snprintf(what, sizeof(what), "fuzz seed %"PRIu64, base + i);
      if (!check_bdd_bijection(&p, what, true))
         declined++;
   }
   ck_assert_msg(declined * 100 <= n, "%d of %d problems declined", declined,
                 n);
}
END_TEST

START_TEST(test_fuzz)
{
   crv_engine_t engines[6];
   const int nengines = fuzz_engines(engines);

   const char *replay = getenv("NVC_CRV_FUZZ_SEED");
   const bool lng = long_mode();
   const double limit = lng ? (getenv("NVC_CRV_FUZZ_SECONDS")
                               ? atof(getenv("NVC_CRV_FUZZ_SECONDS")) : 600.0)
      : 3.0;

   // A fixed number of problems from fixed seeds, as many as fit in the
   // time in the long mode
   const uint64_t base = replay ? strtoull(replay, NULL, 0) : 1000;
   fuzz_stats_t st = { 0 };
   const double start = now_seconds();
   int bad = 0;
   for (uint64_t i = 0; replay ? i < 1 : (lng || i < 1500); i++) {
      if (!fuzz_run_one(base + i, engines, nengines, &st, lng || replay))
         bad++;
      if (now_seconds() - start > limit && !replay)
         break;
   }

   if (lng || replay)
      printf("fuzz: %d problems (%d with solutions, %d unsatisfiable), %d "
             "draws; %d without an answer, %d soft constraints dropped that "
             "could be kept, %d invalid, %d answered although "
             "unsatisfiable; the rejection stage gave up %d times\n",
             st.problems, st.with_solutions, st.unsat,
             st.draws, st.incomplete, st.soft_lost, st.invalid, st.false_sat,
             st.declined);

   // The answers the solver gives are always right; whether it finds one
   // when there is one is reported, and bounded
   ck_assert_int_eq(st.invalid, 0);
   ck_assert_int_eq(st.false_sat, 0);
   ck_assert_msg((st.incomplete + st.soft_lost) * 50 <= st.draws,
                 "%d + %d of %d draws without an answer or with a soft "
                 "constraint lost", st.incomplete, st.soft_lost, st.draws);
   (void)bad;
}
END_TEST

// Problems the fuzzer found that the solver gave up on although they have
// solutions: the bounds of $countones of a signed value that can be
// negative were wrong, so the constraint was refuted at the root
START_TEST(test_fuzz_fixed)
{
   static const uint64_t seeds[] = { 1214, 1512, 1520 };
   crv_engine_t engines[6];
   const int nengines = fuzz_engines(engines);

   for (size_t i = 0; i < ARRAY_LEN(seeds); i++) {
      fuzz_stats_t st = { 0 };
      ck_assert(fuzz_run_one(seeds[i], engines, nengines, &st, true));
      ck_assert_int_eq(st.incomplete, 0);
   }
}
END_TEST

////////////////////////////////////////////////////////////////////////////////
// Engine selection and statistics

START_TEST(test_engine_default)
{
   unsetenv("NVC_CRV_ENGINE");
   ck_assert_int_eq(crv_engine(), CRV_ENGINE_AUTO);
}
END_TEST

START_TEST(test_engine_env)
{
   static const struct {
      const char *name;
      crv_engine_t engine;
   } cases[] = {
      { "dfs", CRV_ENGINE_DFS },
      { "reject", CRV_ENGINE_REJECT }, { "bdd", CRV_ENGINE_BDD },
      { "sat", CRV_ENGINE_SAT }, { "auto", CRV_ENGINE_AUTO },
   };

   // Each case in its own process: the setting is read once
   const int i = _i;
   setenv("NVC_CRV_ENGINE", cases[i].name, 1);
   ck_assert_int_eq(crv_engine(), cases[i].engine);
   ck_assert_str_eq(crv_engine_name(cases[i].engine), cases[i].name);

   crv_seed(60);
   crv_t *c = crv_new();
   crv_var_t x = crv_var(c, 16, false, 0);
   crv_require(c, crv_binary(c, CRV_GT, crv_var_ref(c, x),
                             crv_const(c, 60000, 16, false), 1, false),
               false, 1);
   ck_assert(crv_solve(c));
   ck_assert_int_gt(crv_value(c, x), 60000);
   crv_free(c);
}
END_TEST

START_TEST(test_engine_unknown)
{
   setenv("NVC_CRV_ENGINE", "no-such-engine", 1);
   ck_assert_int_eq(crv_engine(), CRV_ENGINE_AUTO);
}
END_TEST

START_TEST(test_engine_legacy)
{
   // The engine of release 0.18.0 is gone: a note and the default
   setenv("NVC_CRV_ENGINE", "legacy", 1);
   ck_assert_int_eq(crv_engine(), CRV_ENGINE_AUTO);
}
END_TEST

START_TEST(test_engine_available)
{
   ck_assert(crv_engine_available(CRV_ENGINE_DFS));
   ck_assert(crv_engine_available(CRV_ENGINE_AUTO));
   ck_assert(crv_engine_available(CRV_ENGINE_REJECT));
   ck_assert(crv_engine_available(CRV_ENGINE_BDD));
   ck_assert(crv_engine_available(CRV_ENGINE_SAT));
}
END_TEST

static const crv_call_stat_t *solved_stats(crv_t *c, bool expect)
{
   crv_stats_force(true);
   ck_assert_int_eq(crv_solve(c), expect);
   return crv_stats_last();
}

START_TEST(test_stats_none)
{
   // Nothing is free: no component has a variable to choose
   crv_t *c = crv_new();
   crv_var_t x = crv_var(c, 8, false, 5);
   crv_var_domain(c, x, 5);
   crv_require(c, crv_binary(c, CRV_EQ, crv_var_ref(c, x),
                             crv_const(c, 5, 8, false), 1, false), false, 1);
   const crv_call_stat_t *st = solved_stats(c, true);
   ck_assert_int_eq(st->comps, 0);
   ck_assert_int_eq(st->loose, 0);
   ck_assert_int_eq(st->maxfree, 0);
   ck_assert_int_eq(st->top, CRV_STAGE_NONE);
   ck_assert_int_eq(st->stage[CRV_STAGE_NONE], 1);
   ck_assert(st->ok);
   crv_free(c);
}
END_TEST

START_TEST(test_stats_linked)
{
   // x < y links two free variables
   crv_t *c = crv_new();
   crv_var_t x = crv_var(c, 4, false, 0), y = crv_var(c, 4, false, 0);
   crv_require(c, crv_binary(c, CRV_LT, crv_var_ref(c, x), crv_var_ref(c, y),
                             1, false), false, 1);
   const crv_call_stat_t *st = solved_stats(c, true);
   ck_assert_int_eq(st->comps, 1);
   ck_assert_int_eq(st->freevars, 2);
   ck_assert_int_eq(st->maxfree, 2);
   ck_assert_int_eq(st->stage[CRV_STAGE_REJECT], 1);
   ck_assert_int_eq(st->stage[CRV_STAGE_DFS], 0);
   ck_assert_int_eq(st->top, CRV_STAGE_REJECT);
   crv_free(c);
}
END_TEST

START_TEST(test_stats_stage)
{
   // The stage that answers is the one the engine allows
   static const struct {
      crv_engine_t engine;
      crv_stage_t  stage;
   } cases[] = {
      { CRV_ENGINE_DFS, CRV_STAGE_DFS },
      { CRV_ENGINE_REJECT, CRV_STAGE_REJECT },
      { CRV_ENGINE_BDD, CRV_STAGE_BDD },
      { CRV_ENGINE_SAT, CRV_STAGE_SAT },
      { CRV_ENGINE_AUTO, CRV_STAGE_REJECT },
   };

   const crv_engine_t saved = crv_engine();
   for (size_t i = 0; i < ARRAY_LEN(cases); i++) {
      crv_set_engine(cases[i].engine);
      crv_t *c = crv_new();
      crv_var_t x = crv_var(c, 8, false, 0), y = crv_var(c, 8, false, 0);
      crv_require(c, crv_binary(c, CRV_LT, crv_var_ref(c, x),
                                crv_var_ref(c, y), 1, false), false, 1);
      const crv_call_stat_t *st = solved_stats(c, true);
      ck_assert_int_eq(st->top, cases[i].stage);
      ck_assert_int_eq(st->stage[cases[i].stage], 1);
      crv_free(c);
   }
   crv_set_engine(saved);
}
END_TEST

START_TEST(test_stats_decline)
{
   // A component with a randc variable is for the search: forced, the
   // rejection stage and the bdd stage give up and say why
   static const crv_engine_t engines[] = {
      CRV_ENGINE_AUTO, CRV_ENGINE_REJECT, CRV_ENGINE_BDD
   };
   const crv_engine_t saved = crv_engine();
   uint64_t state = 0;
   for (size_t pass = 0; pass < ARRAY_LEN(engines); pass++) {
      crv_set_engine(engines[pass]);
      crv_t *c = crv_new();
      crv_var_t x = crv_var(c, 4, false, 0), y = crv_var(c, 4, false, 0);
      crv_var_randc(c, x, &state);
      crv_require(c, crv_binary(c, CRV_LT, crv_var_ref(c, x),
                                crv_var_ref(c, y), 1, false), false, 1);
      crv_stats_force(true);
      const crv_result_t r = crv_solve2(c);
      const crv_call_stat_t *st = crv_stats_last();
      if (pass != 2)
         ck_assert_int_eq(st->declined[0], 1);
      if (pass != 1)
         ck_assert_int_eq(st->bdd_declined[CRV_BDD_RANDC], 1);
      if (pass == 0) {
         ck_assert_int_eq(r, CRV_SAT);
         ck_assert_int_eq(st->top, CRV_STAGE_DFS);
      }
      else
         ck_assert_int_eq(r, CRV_GIVEUP);
      crv_free(c);
   }
   crv_set_engine(saved);
}
END_TEST

START_TEST(test_soft_giveup)
{
   // The forced rejection stage cannot say that the hard constraints have no
   // solution: the soft constraints cannot all hold, the hard ones alone are
   // too tight to sample, and the call gives up instead of saying
   // unsatisfiable
   const crv_engine_t saved = crv_engine();
   crv_set_engine(CRV_ENGINE_REJECT);
   crv_t *c = crv_new();
   crv_var_t x = crv_var(c, 32, false, 0), y = crv_var(c, 32, false, 0);
   crv_require(c, crv_binary(c, CRV_EQ, crv_binary(
                                c, CRV_ADD, crv_var_ref(c, x),
                                crv_var_ref(c, y), 32, false),
                             crv_const(c, 0x12345678, 32, false), 1, false),
               false, 1);
   crv_require(c, crv_binary(c, CRV_LT, crv_var_ref(c, x),
                             crv_const(c, 5, 32, false), 1, false), true, 2);
   crv_require(c, crv_binary(c, CRV_GT, crv_var_ref(c, x),
                             crv_const(c, 10, 32, false), 1, false), true, 3);
   crv_seed(5);
   ck_assert_int_eq(crv_solve2(c), CRV_GIVEUP);
   crv_free(c);
   crv_set_engine(saved);
}
END_TEST

START_TEST(test_stats_defined_elements)
{
   // a[k] == a[k-1] + 1 defines each element by the one before: sampling the
   // first is enough, and the rejection stage answers (the chance that eight
   // independent bytes follow each other is 2^-56)
   const crv_engine_t saved = crv_engine();
   crv_set_engine(CRV_ENGINE_AUTO);
   crv_t *c = crv_new();
   crv_array_t a = crv_array(c, 8, false, 8, false, true);
   for (unsigned k = 1; k < 8; k++) {
      crv_node_t cur = crv_array_elem(c, a, crv_const(c, k, 32, true));
      crv_node_t prev = crv_array_elem(c, a, crv_const(c, k - 1, 32, true));
      crv_node_t next = crv_binary(c, CRV_ADD, prev, crv_const(c, 1, 8, false),
                                   8, false);
      crv_require(c, crv_binary(c, CRV_EQ, cur, next, 1, false), false, k);
   }

   crv_seed(11);
   const crv_call_stat_t *st = solved_stats(c, true);
   ck_assert_int_eq(st->stage[CRV_STAGE_REJECT], 1);
   ck_assert_int_eq(st->stage[CRV_STAGE_DFS], 0);
   for (unsigned k = 1; k < 8; k++)
      ck_assert_int_eq(crv_array_value(c, a, k),
                       (crv_array_value(c, a, k - 1) + 1) & 0xff);
   crv_free(c);
   crv_set_engine(saved);
}
END_TEST

START_TEST(test_stats_exhausted)
{
   // 16 samples of a variable that one value in 2^16 satisfies are all
   // rejected: the bdd stage answers, the search does without it, and a
   // forced rejection stage gives up
   static const crv_engine_t engines[] = {
      CRV_ENGINE_AUTO, CRV_ENGINE_REJECT, CRV_ENGINE_AUTO
   };
   const crv_engine_t saved = crv_engine();
   const unsigned nodes = crv_bdd_nodes();
   for (int pass = 0; pass < 3; pass++) {
      crv_set_engine(engines[pass]);
      crv_set_bdd_nodes(pass == 2 ? 0 : nodes);
      crv_t *c = crv_new();
      crv_var_t x = crv_var(c, 32, false, 0), y = crv_var(c, 32, false, 0);
      crv_require(c, crv_binary(c, CRV_EQ, crv_binary(
                                   c, CRV_ADD, crv_var_ref(c, x),
                                   crv_var_ref(c, y), 32, false),
                                crv_const(c, 0x12345678, 32, false), 1, false),
                  false, 1);
      crv_stats_force(true);
      crv_seed(5);
      const crv_result_t r = crv_solve2(c);
      const crv_call_stat_t *st = crv_stats_last();
      if (pass != 1) {
         ck_assert_int_eq(r, CRV_SAT);
         ck_assert_int_eq((crv_value(c, x) + crv_value(c, y)) & 0xffffffff,
                          0x12345678);
         ck_assert_int_eq(st->top, pass == 0 ? CRV_STAGE_BDD : CRV_STAGE_DFS);
      }
      else
         ck_assert_int_eq(r, CRV_GIVEUP);
      ck_assert_int_eq(st->declined[2], 1);
      crv_free(c);
   }
   crv_set_bdd_nodes(nodes);
   crv_set_engine(saved);
}
END_TEST

START_TEST(test_stats_independent)
{
   // Two constraints on different variables, one loose variable, one that
   // another constraint has already fixed
   crv_t *c = crv_new();
   crv_var_t x = crv_var(c, 4, false, 0), y = crv_var(c, 4, false, 0);
   crv_var_t z = crv_var(c, 4, false, 0), w = crv_var(c, 4, false, 0);
   (void)z;
   crv_require(c, crv_binary(c, CRV_LT, crv_var_ref(c, x),
                             crv_const(c, 5, 4, false), 1, false), false, 1);
   crv_require(c, crv_binary(c, CRV_GT, crv_var_ref(c, y),
                             crv_const(c, 3, 4, false), 1, false), false, 2);
   crv_require(c, crv_binary(c, CRV_EQ, crv_var_ref(c, w),
                             crv_const(c, 9, 4, false), 1, false), false, 3);
   const crv_call_stat_t *st = solved_stats(c, true);
   ck_assert_int_eq(st->comps, 2);
   ck_assert_int_eq(st->loose, 1);
   ck_assert_int_eq(st->maxfree, 1);
   ck_assert_int_eq(st->stage[CRV_STAGE_NONE], 1);
   crv_free(c);
}
END_TEST

START_TEST(test_stats_order)
{
   // A solve before edge links variables that no constraint does
   crv_t *c = crv_new();
   crv_var_t x = crv_var(c, 4, false, 0), y = crv_var(c, 4, false, 0);
   crv_solve_before(c, x, y);
   const crv_call_stat_t *st = solved_stats(c, true);
   ck_assert_int_eq(st->comps, 1);
   ck_assert_int_eq(st->maxfree, 2);
   crv_free(c);
}
END_TEST

START_TEST(test_stats_fail)
{
   crv_t *c = crv_new();
   crv_var_t x = crv_var(c, 4, false, 0), y = crv_var(c, 4, false, 0);
   crv_require(c, crv_binary(c, CRV_LT, crv_var_ref(c, x), crv_var_ref(c, y),
                             1, false), false, 1);
   crv_require(c, crv_binary(c, CRV_LT, crv_var_ref(c, y), crv_var_ref(c, x),
                             1, false), false, 2);
   const crv_call_stat_t *st = solved_stats(c, false);
   ck_assert(!st->ok);
   crv_free(c);
}
END_TEST

START_TEST(test_stats_soft)
{
   crv_t *c = crv_new();
   crv_var_t x = crv_var(c, 8, false, 0);
   crv_require(c, crv_binary(c, CRV_LT, crv_var_ref(c, x),
                             crv_const(c, 5, 8, false), 1, false), true, 1);
   const crv_call_stat_t *st = solved_stats(c, true);
   ck_assert(st->soft);
   crv_free(c);
}
END_TEST

START_TEST(test_stats_site)
{
   // Calls are summed per site and the table can be written to a file
   char path[] = "/tmp/nvc-crv-stats-XXXXXX";
   const int fd = mkstemp(path);
   ck_assert_int_ge(fd, 0);
   close(fd);
   setenv("NVC_CRV_STATS", path, 1);
   ck_assert(crv_stats_enabled());

   for (int i = 0; i < 5; i++) {
      crv_stats_site(i < 3 ? "a.sv:1:1" : "b.sv:2:2");
      crv_t *c = crv_new();
      crv_var_t x = crv_var(c, 4, false, 0), y = crv_var(c, 4, false, 0);
      crv_require(c, crv_binary(c, CRV_LT, crv_var_ref(c, x),
                                crv_var_ref(c, y), 1, false), false, 1);
      ck_assert(crv_solve(c));
      crv_free(c);
   }

   // Written when the process ends: do it by hand here
   crv_stats_write();

   FILE *f = fopen(path, "r");
   ck_assert_ptr_nonnull(f);
   char line[512];
   int n = 0;
   while (fgets(line, sizeof(line), f)) {
      char site[64];
      unsigned long calls, comps, loose, freevars, multi;
      ck_assert_int_eq(sscanf(line, "%63s\t%lu\t%lu\t%lu\t%lu\t%lu", site,
                              &calls, &comps, &loose, &freevars, &multi), 6);
      if (!strcmp(site, "a.sv:1:1"))
         ck_assert_int_eq(calls, 3);
      else if (!strcmp(site, "b.sv:2:2"))
         ck_assert_int_eq(calls, 2);
      else
         ck_abort_msg("unexpected site %s", site);
      ck_assert_int_eq(multi, calls);
      ck_assert_int_eq(freevars, calls * 2);

      // Site, nine counters, two groups of six stages, three declines, two
      // times, the declines of the bdd stage by reason, its two times and
      // its memory
      int cols = 1;
      for (const char *p = line; *p; p++)
         cols += *p == '\t';
      ck_assert_int_eq(cols, 27 + CRV_BDD_REASONS + 3);
      n++;
   }
   fclose(f);
   unlink(path);
   ck_assert_int_eq(n, 2);
}
END_TEST

static uint64_t test_sat_rng(void *ctx)
{
   return trng_next((trng_t *)ctx);
}

// Direct tests of the CDCL SAT core: satisfiable 3-SAT, pigeonhole UNSAT,
// conflict budget give-up, and deterministic seed replay
START_TEST(test_sat_core)
{
   // Pigeonhole PHP(n+1, n): n+1 pigeons in n holes is unsatisfiable
   for (unsigned holes = 3; holes <= 5; holes++) {
      const unsigned pigeons = holes + 1;
      sat_t *s = sat_new(pigeons * holes);
      // Every pigeon must be in at least one hole
      for (unsigned p = 0; p < pigeons; p++) {
         uint32_t cl[6];
         for (unsigned h = 0; h < holes; h++)
            cl[h] = (p * holes + h) << 1;
         ck_assert(sat_add_clause(s, cl, holes));
      }
      // At most one pigeon per hole
      for (unsigned h = 0; h < holes; h++) {
         for (unsigned p1 = 0; p1 < pigeons; p1++) {
            for (unsigned p2 = p1 + 1; p2 < pigeons; p2++) {
               uint32_t cl[2] = { ((p1 * holes + h) << 1) | 1,
                                  ((p2 * holes + h) << 1) | 1 };
               ck_assert(sat_add_clause(s, cl, 2));
            }
         }
      }
      trng_t rng = { 12345 + holes };
      ck_assert_int_eq(sat_solve(s, test_sat_rng, &rng), SAT_UNSAT);
      sat_free(s);
   }

   // Conflict budget gives up cleanly on PHP(7, 6) with budget = 5
   {
      const unsigned holes = 6, pigeons = 7;
      sat_t *s = sat_new(pigeons * holes);
      sat_set_budget(s, 5);
      for (unsigned p = 0; p < pigeons; p++) {
         uint32_t cl[6];
         for (unsigned h = 0; h < holes; h++)
            cl[h] = (p * holes + h) << 1;
         sat_add_clause(s, cl, holes);
      }
      for (unsigned h = 0; h < holes; h++) {
         for (unsigned p1 = 0; p1 < pigeons; p1++) {
            for (unsigned p2 = p1 + 1; p2 < pigeons; p2++) {
               uint32_t cl[2] = { ((p1 * holes + h) << 1) | 1,
                                  ((p2 * holes + h) << 1) | 1 };
               sat_add_clause(s, cl, 2);
            }
         }
      }
      trng_t rng = { 77 };
      ck_assert_int_eq(sat_solve(s, test_sat_rng, &rng), SAT_GIVEUP);
      sat_free(s);
   }

   // Satisfiable random 3-SAT with planted solution: model must satisfy all
   trng_t gen = { 9999 };
   const unsigned nv = 40, nc = 160;
   uint8_t planted[40];
   for (unsigned v = 0; v < nv; v++)
      planted[v] = trng_next(&gen) & 1;

   uint32_t clauses[160][3];
   sat_t *s = sat_new(nv);
   for (unsigned i = 0; i < nc; i++) {
      bool sat_planted = false;
      for (unsigned k = 0; k < 3; k++) {
         const unsigned v = trng_below(&gen, nv);
         unsigned neg = trng_next(&gen) & 1;
         if (k == 2 && !sat_planted)
            neg = planted[v] ? 0 : 1;
         if ((neg == 0) == (planted[v] == 1))
            sat_planted = true;
         clauses[i][k] = (v << 1) | neg;
      }
      ck_assert(sat_add_clause(s, clauses[i], 3));
   }
   trng_t rng = { 42 };
   ck_assert_int_eq(sat_solve(s, test_sat_rng, &rng), SAT_SAT);
   for (unsigned i = 0; i < nc; i++) {
      bool ok = false;
      for (unsigned k = 0; k < 3; k++) {
         const bool v = sat_value(s, clauses[i][k] >> 1);
         if (v == ((clauses[i][k] & 1) == 0))
            ok = true;
      }
      ck_assert(ok);
   }
   sat_free(s);
}
END_TEST

// SAT stage via the solver C API: modular multiplication, bit relations,
// proven UNSAT (not give-up), and seed stability
START_TEST(test_sat_api)
{
   const crv_engine_t saved = crv_engine();
   crv_set_engine(CRV_ENGINE_AUTO);

   // 16-bit product x * y == 30031, x > 1, y > 1: answered by SAT in auto
   crv_seed(31415);
   for (int i = 0; i < 10; i++) {
      crv_t *c = crv_new();
      crv_var_t x = crv_var(c, 16, false, 0), y = crv_var(c, 16, false, 0);
      crv_require(c, crv_binary(c, CRV_EQ, crv_binary(
                                   c, CRV_MUL, crv_var_ref(c, x),
                                   crv_var_ref(c, y), 16, false),
                                crv_const(c, 30031, 16, false), 1, false),
                  false, 1);
      crv_require(c, crv_binary(c, CRV_GT, crv_var_ref(c, x),
                                crv_const(c, 1, 16, false), 1, false),
                  false, 2);
      crv_require(c, crv_binary(c, CRV_GT, crv_var_ref(c, y),
                                crv_const(c, 1, 16, false), 1, false),
                  false, 3);
      crv_stats_force(true);
      ck_assert_int_eq(crv_solve2(c), CRV_SAT);
      const uint16_t xv = (uint16_t)crv_value(c, x);
      const uint16_t yv = (uint16_t)crv_value(c, y);
      ck_assert_int_eq((uint16_t)(xv * yv), 30031);
      ck_assert_int_gt(xv, 1);
      ck_assert_int_gt(yv, 1);
      ck_assert_int_eq(crv_stats_last()->top, CRV_STAGE_SAT);
      crv_free(c);
   }

   // Unsatisfiable product: 17 is prime, x, y in [2..16] -> proven CRV_UNSAT
   {
      crv_t *c = crv_new();
      crv_var_t x = crv_var(c, 8, false, 0), y = crv_var(c, 8, false, 0);
      crv_require(c, crv_binary(c, CRV_EQ, crv_binary(
                                   c, CRV_MUL, crv_var_ref(c, x),
                                   crv_var_ref(c, y), 8, false),
                                crv_const(c, 17, 8, false), 1, false),
                  false, 1);
      crv_require(c, crv_binary(c, CRV_GT, crv_var_ref(c, x),
                                crv_const(c, 1, 8, false), 1, false),
                  false, 2);
      crv_require(c, crv_binary(c, CRV_LT, crv_var_ref(c, x),
                                crv_const(c, 17, 8, false), 1, false),
                  false, 3);
      crv_require(c, crv_binary(c, CRV_GT, crv_var_ref(c, y),
                                crv_const(c, 1, 8, false), 1, false),
                  false, 4);
      crv_require(c, crv_binary(c, CRV_LT, crv_var_ref(c, y),
                                crv_const(c, 17, 8, false), 1, false),
                  false, 5);
      ck_assert_int_eq(crv_solve2(c), CRV_UNSAT);
      crv_set_engine(CRV_ENGINE_SAT);
      ck_assert_int_eq(crv_solve2(c), CRV_UNSAT);
      crv_free(c);
   }

   // Forced SAT seed stability: same seed gives identical sequence
   int64_t seq[2][4];
   for (int pass = 0; pass < 2; pass++) {
      crv_set_engine(CRV_ENGINE_SAT);
      crv_seed(0x5a75eed);
      for (int k = 0; k < 2; k++) {
         crv_t *c = crv_new();
         crv_var_t x = crv_var(c, 16, false, 0), y = crv_var(c, 16, false, 0);
         crv_require(c, crv_binary(c, CRV_EQ, crv_binary(
                                      c, CRV_MUL, crv_var_ref(c, x),
                                      crv_var_ref(c, y), 16, false),
                                   crv_const(c, 30031, 16, false), 1, false),
                     false, 1);
         ck_assert_int_eq(crv_solve2(c), CRV_SAT);
         seq[pass][k * 2] = crv_value(c, x);
         seq[pass][k * 2 + 1] = crv_value(c, y);
         crv_free(c);
      }
   }
   for (int i = 0; i < 4; i++)
      ck_assert_int_eq(seq[0][i], seq[1][i]);

   crv_set_engine(saved);
}
END_TEST

START_TEST(test_dist_guarded)
{
   crv_seed(0x90a123);

   // Guarded dist under a free guard variable: both branches weighted
   int m1_hi = 0, m1_n = 0, m0_lo = 0, m0_n = 0;
   for (int i = 0; i < 400; i++) {
      crv_t *c = crv_new();
      crv_var_t mode = crv_var(c, 1, false, 0);
      crv_var_t x = crv_var(c, 8, false, 0);
      crv_dist_t d1 = crv_dist_new(c);
      crv_dist_range(c, d1, 255, 255, 3, true);
      crv_dist_range(c, d1, 0, 0, 1, true);
      crv_dist_t d0 = crv_dist_new(c);
      crv_dist_range(c, d0, 0, 0, 3, true);
      crv_dist_range(c, d0, 255, 255, 1, true);
      crv_require(c, crv_binary(c, CRV_IMPLIES, crv_var_ref(c, mode),
                                crv_dist(c, crv_var_ref(c, x), d1), 1, false),
                  false, 1);
      crv_require(c, crv_binary(c, CRV_IMPLIES,
                                crv_unary(c, CRV_LOG_NOT, crv_var_ref(c, mode)),
                                crv_dist(c, crv_var_ref(c, x), d0), 1, false),
                  false, 2);
      ck_assert(crv_solve(c));
      const int64_t mv = crv_value(c, mode);
      const int64_t xv = crv_value(c, x);
      ck_assert(xv == 0 || xv == 255);
      if (mv) {
         m1_n++;
         if (xv == 255) m1_hi++;
      }
      else {
         m0_n++;
         if (xv == 0) m0_lo++;
      }
      crv_free(c);
   }
   ck_assert_int_gt(m1_n, 100);
   ck_assert_int_gt(m0_n, 100);
   ck_assert_int_gt(m1_hi * 2, m1_n);
   ck_assert_int_gt(m0_lo * 2, m0_n);

   // Guarded dist with non-dist else branch and free guard
   int s0 = 0, s1_5 = 0, s1_70 = 0;
   for (int i = 0; i < 200; i++) {
      crv_t *c = crv_new();
      crv_var_t sel = crv_var(c, 1, false, 0);
      crv_var_t x = crv_var(c, 32, false, 0);
      crv_dist_t d = crv_dist_new(c);
      crv_dist_range(c, d, 5, 5, 1, true);
      crv_dist_range(c, d, 70, 70, 1, true);
      crv_require(c, crv_binary(c, CRV_IMPLIES, crv_var_ref(c, sel),
                                crv_dist(c, crv_var_ref(c, x), d), 1, false),
                  false, 1);
      crv_require(c, crv_binary(c, CRV_IMPLIES,
                                crv_unary(c, CRV_LOG_NOT, crv_var_ref(c, sel)),
                                crv_binary(c, CRV_EQ, crv_var_ref(c, x),
                                           crv_const(c, 4242, 32, false),
                                           1, false),
                                1, false),
                  false, 2);
      ck_assert(crv_solve(c));
      if (crv_value(c, sel)) {
         const int64_t xv = crv_value(c, x);
         ck_assert(xv == 5 || xv == 70);
         if (xv == 5) s1_5++; else s1_70++;
      }
      else {
         ck_assert_int_eq(crv_value(c, x), 4242);
         s0++;
      }
      crv_free(c);
   }
   ck_assert_int_gt(s0, 0);
   ck_assert_int_gt(s1_5, 0);
   ck_assert_int_gt(s1_70, 0);
}
END_TEST

Suite *get_crv_tests(void)
{
   Suite *s = suite_create("crv");

   TCase *tc = tcase_create("Core");
   tcase_set_timeout(tc, long_mode() ? 3600.0 : 30.0);
   tcase_add_test(tc, test_domain);
   tcase_add_test(tc, test_fixed);
   tcase_add_test(tc, test_unsat);
   tcase_add_test(tc, test_no_constraint);
   tcase_add_loop_test(tc, test_binary, 0, ARRAY_LEN(binops) * 6);
   tcase_add_loop_test(tc, test_compare, 0, 12);
   tcase_add_test(tc, test_unary);
   tcase_add_test(tc, test_popcnt_signed);
   tcase_add_test(tc, test_cast);
   tcase_add_test(tc, test_logic);
   tcase_add_test(tc, test_inside);
   tcase_add_test(tc, test_dist);
   tcase_add_test(tc, test_solve_before);
   tcase_add_test(tc, test_soft);
   tcase_add_test(tc, test_soft_many);
   tcase_add_test(tc, test_unique);
   tcase_add_test(tc, test_unique_large);
   tcase_add_test(tc, test_components);
   tcase_add_test(tc, test_randc);
   tcase_add_test(tc, test_seed);
   tcase_add_test(tc, test_array_fixed);
   tcase_add_test(tc, test_array_dynamic);
   tcase_add_test(tc, test_array_sum);
   tcase_add_test(tc, test_array_size_elements);
   tcase_add_test(tc, test_sum_chain);
   tcase_add_test(tc, test_array_unregistered);
   tcase_add_test(tc, test_seed_before_begin);
   tcase_add_test(tc, test_randc_trial);
   tcase_add_test(tc, test_engine_default);
   tcase_add_loop_test(tc, test_engine_env, 0, 5);
   tcase_add_test(tc, test_engine_unknown);
   tcase_add_test(tc, test_engine_legacy);
   tcase_add_test(tc, test_engine_available);
   tcase_add_test(tc, test_stats_none);
   tcase_add_test(tc, test_stats_linked);
   tcase_add_test(tc, test_stats_independent);
   tcase_add_test(tc, test_stats_stage);
   tcase_add_test(tc, test_stats_decline);
   tcase_add_test(tc, test_stats_exhausted);
   tcase_add_test(tc, test_stats_defined_elements);
   tcase_add_test(tc, test_soft_giveup);
   tcase_add_test(tc, test_stats_order);
   tcase_add_test(tc, test_stats_fail);
   tcase_add_test(tc, test_stats_soft);
   tcase_add_test(tc, test_stats_site);
   tcase_add_test(tc, test_model_counts);
   tcase_add_loop_test(tc, test_model_sum_one, 0, NPROBLEMS);
   tcase_add_test(tc, test_model_levels);
   tcase_add_test(tc, test_chi2_bound);
   tcase_add_loop_test(tc, test_ideal_sampler, 0, NPROBLEMS);
   tcase_add_test(tc, test_harness_detects_bias);
   tcase_add_loop_test(tc, test_exact_stages, 0, NPROBLEMS);
   tcase_add_loop_test(tc, test_sound_all_engines, 0, NPROBLEMS);
   tcase_add_loop_test(tc, test_bias_report, 0, NPROBLEMS);
   tcase_add_test(tc, test_fuzz);
   tcase_add_loop_test(tc, test_bdd_bijection_problems, 0, NPROBLEMS);
   tcase_add_test(tc, test_bdd_bijection_fuzz);
   tcase_add_test(tc, test_bdd_cmp_const);
   tcase_add_test(tc, test_bdd_cmp_scrambled);
   tcase_add_test(tc, test_bdd_budget);
   tcase_add_test(tc, test_bdd_seed_stable);
   tcase_add_test(tc, test_sat_core);
   tcase_add_test(tc, test_sat_api);
   tcase_add_test(tc, test_dist_guarded);
   tcase_add_test(tc, test_fuzz_fixed);
   suite_add_tcase(s, tc);

   return s;
}
