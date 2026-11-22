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

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
      {
         const uint64_t n = ub & tmask(w);
         if (n >= 64)
            *r = (op == CRV_ASR && s && a < 0) ? -1 : 0;
         else if (op == CRV_SHL)
            *r = wrapv(ua << n, w, s);
         else if (op == CRV_ASR && s)
            *r = wrapv(a >> n, w, s);
         else
            *r = wrapv((ua & tmask(w)) >> n, w, s);
         return true;
      }
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
   ck_assert(!crv_solve(c));
   ck_assert_int_eq(crv_value(c, x), 42);
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

////////////////////////////////////////////////////////////////////////////////
// Engine selection and statistics

START_TEST(test_engine_default)
{
   unsetenv("NVC_CRV_ENGINE");
   unsetenv("NVC_CRV_DECOMPOSE");
   ck_assert_int_eq(crv_engine(), CRV_ENGINE_AUTO);
   ck_assert(crv_decompose_enabled());
}
END_TEST

START_TEST(test_engine_env)
{
   static const struct {
      const char *name;
      crv_engine_t engine;
   } cases[] = {
      { "legacy", CRV_ENGINE_LEGACY }, { "dfs", CRV_ENGINE_DFS },
      { "reject", CRV_ENGINE_REJECT }, { "bdd", CRV_ENGINE_BDD },
      { "sat", CRV_ENGINE_SAT }, { "auto", CRV_ENGINE_AUTO },
   };

   // Each case in its own process: the setting is read once
   const int i = _i;
   setenv("NVC_CRV_ENGINE", cases[i].name, 1);
   ck_assert_int_eq(crv_engine(), cases[i].engine);
   ck_assert_str_eq(crv_engine_name(cases[i].engine), cases[i].name);

   // Whatever is chosen the answers are those of the search of release 0.18
   crv_seed(60);
   crv_t *c = crv_new();
   crv_var_t x = crv_var(c, 16, false, 0);
   crv_require(c, crv_binary(c, CRV_GT, crv_var_ref(c, x),
                             crv_const(c, 60000, 16, false), 1, false),
               false, 1);
   ck_assert(crv_solve(c));
   const int64_t v = crv_value(c, x);
   ck_assert(v > 60000);
   crv_free(c);

   // The same seed: identical to what the legacy choice gives
   crv_set_engine(CRV_ENGINE_LEGACY);
   crv_seed(60);
   c = crv_new();
   x = crv_var(c, 16, false, 0);
   crv_require(c, crv_binary(c, CRV_GT, crv_var_ref(c, x),
                             crv_const(c, 60000, 16, false), 1, false),
               false, 1);
   ck_assert(crv_solve(c));
   ck_assert_int_eq(crv_value(c, x), v);
   crv_free(c);
}
END_TEST

START_TEST(test_engine_unknown)
{
   setenv("NVC_CRV_ENGINE", "no-such-engine", 1);
   ck_assert_int_eq(crv_engine(), CRV_ENGINE_AUTO);
}
END_TEST

START_TEST(test_engine_available)
{
   ck_assert(crv_engine_available(CRV_ENGINE_LEGACY));
   ck_assert(crv_engine_available(CRV_ENGINE_DFS));
   ck_assert(crv_engine_available(CRV_ENGINE_AUTO));
   ck_assert(!crv_engine_available(CRV_ENGINE_REJECT));
   ck_assert(!crv_engine_available(CRV_ENGINE_BDD));
   ck_assert(!crv_engine_available(CRV_ENGINE_SAT));
}
END_TEST

START_TEST(test_decompose_env)
{
   setenv("NVC_CRV_DECOMPOSE", "0", 1);
   ck_assert(!crv_decompose_enabled());
   crv_set_decompose(true);
   ck_assert(crv_decompose_enabled());
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
   ck_assert_int_eq(st->stage[CRV_STAGE_DFS], 1);
   ck_assert_int_eq(st->top, CRV_STAGE_DFS);
   crv_free(c);
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

START_TEST(test_stats_decompose)
{
   // A 32 bit value constrained through two slices is split into fields
   // unless that is off
   for (int pass = 0; pass < 2; pass++) {
      crv_set_decompose(pass == 0);
      crv_t *c = crv_new();
      crv_var_t x = crv_var(c, 32, false, 0);
      crv_node_t xv = crv_var_ref(c, x);
      crv_node_t hi = crv_binary(c, CRV_SHR, xv, crv_const(c, 16, 32, false),
                                 32, false);
      crv_require(c, crv_binary(c, CRV_EQ, hi, crv_const(c, 0x1234, 32, false),
                                1, false), false, 1);
      crv_node_t lo = crv_binary(c, CRV_AND, xv,
                                 crv_const(c, 0xff, 32, false), 32, false);
      crv_require(c, crv_binary(c, CRV_EQ, lo, crv_const(c, 0x5a, 32, false),
                                1, false), false, 2);
      const crv_call_stat_t *st = solved_stats(c, true);
      ck_assert_int_eq(st->decomposed, pass == 0);
      ck_assert_int_eq(crv_value(c, x) >> 16, 0x1234);
      ck_assert_int_eq(crv_value(c, x) & 0xff, 0x5a);
      crv_free(c);
   }
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

      // Site, nine counters, two groups of six stages, three declines and
      // two times
      int cols = 1;
      for (const char *p = line; *p; p++)
         cols += *p == '\t';
      ck_assert_int_eq(cols, 27);
      n++;
   }
   fclose(f);
   unlink(path);
   ck_assert_int_eq(n, 2);
}
END_TEST

Suite *get_crv_tests(void)
{
   Suite *s = suite_create("crv");

   TCase *tc = tcase_create("Core");
   tcase_set_timeout(tc, 30.0);
   tcase_add_test(tc, test_domain);
   tcase_add_test(tc, test_fixed);
   tcase_add_test(tc, test_unsat);
   tcase_add_test(tc, test_no_constraint);
   tcase_add_loop_test(tc, test_binary, 0, ARRAY_LEN(binops) * 6);
   tcase_add_loop_test(tc, test_compare, 0, 12);
   tcase_add_test(tc, test_unary);
   tcase_add_test(tc, test_cast);
   tcase_add_test(tc, test_logic);
   tcase_add_test(tc, test_inside);
   tcase_add_test(tc, test_dist);
   tcase_add_test(tc, test_solve_before);
   tcase_add_test(tc, test_soft);
   tcase_add_test(tc, test_unique);
   tcase_add_test(tc, test_randc);
   tcase_add_test(tc, test_seed);
   tcase_add_test(tc, test_array_fixed);
   tcase_add_test(tc, test_array_dynamic);
   tcase_add_test(tc, test_array_sum);
   tcase_add_test(tc, test_engine_default);
   tcase_add_loop_test(tc, test_engine_env, 0, 6);
   tcase_add_test(tc, test_engine_unknown);
   tcase_add_test(tc, test_engine_available);
   tcase_add_test(tc, test_decompose_env);
   tcase_add_test(tc, test_stats_none);
   tcase_add_test(tc, test_stats_linked);
   tcase_add_test(tc, test_stats_independent);
   tcase_add_test(tc, test_stats_order);
   tcase_add_test(tc, test_stats_fail);
   tcase_add_test(tc, test_stats_soft);
   tcase_add_test(tc, test_stats_decompose);
   tcase_add_test(tc, test_stats_site);
   suite_add_tcase(s, tc);

   return s;
}
