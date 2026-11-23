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
#include "rt/crv-priv.h"
#include "rt/crv-blast.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Tests of the bit-blaster.  Random graphs are built through the API of the
// solver and every node is compared with the value that crv_eval gives on
// every assignment (graphs of up to 16 bits) or on random ones (wide
// operators and graphs built from chunks), and for the chunked 128 bit
// operators with the arithmetic of the host.

////////////////////////////////////////////////////////////////////////////////
// Random numbers and values

typedef struct { uint64_t s; } rng_t;

static uint64_t rng_next(rng_t *r)
{
   uint64_t z = (r->s += UINT64_C(0x9e3779b97f4a7c15));
   z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
   z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
   return z ^ (z >> 31);
}

static unsigned rng_below(rng_t *r, unsigned n)
{
   return rng_next(r) % n;
}

static inline uint64_t tmask(unsigned w)
{
   return w >= 64 ? ~UINT64_C(0) : (UINT64_C(1) << w) - 1;
}

// The representation of a w-bit value
static int64_t rep(uint64_t v, unsigned w, bool s)
{
   v &= tmask(w);
   if (s && w < 64 && ((v >> (w - 1)) & 1))
      v |= ~tmask(w);
   return (int64_t)v;
}

////////////////////////////////////////////////////////////////////////////////
// Graph generator

#define MAX_VARS 24
#define MAX_NODES 400
#define MAX_ARRAYS 2

typedef struct { crv_node_t id; unsigned w; bool s; } gv_t;

typedef struct {
   rng_t        r;
   crv_t       *c;
   unsigned     nsc, na, nv;
   uint8_t      width[MAX_VARS];
   bool         sgn[MAX_VARS];
   bool         fixed[MAX_VARS];
   int64_t      value[MAX_VARS];
   int          ebase[MAX_ARRAYS];
   unsigned     sizes[MAX_ARRAYS];
   crv_array_t  arr[MAX_ARRAYS];
   gv_t         list[MAX_NODES];
   unsigned     nlist;
   unsigned     maxw;          // Widest value a node may have
   unsigned     bits;          // Bits of the free variables
   bool         heavy;         // Multiplication and division allowed
   bool         mixed;         // Operands of different types allowed
} gen_t;

static unsigned gen_below(gen_t *g, unsigned n)
{
   return rng_below(&g->r, n);
}

static gv_t gen_note(gen_t *g, crv_node_t id, unsigned w, bool s)
{
   gv_t v = { id, w, s };
   if (g->nlist < MAX_NODES)
      g->list[g->nlist++] = v;
   return v;
}

static gv_t gen_cast(gen_t *g, gv_t a, unsigned w, bool s)
{
   if (a.w == w && a.s == s)
      return a;
   return gen_note(g, crv_cast(g->c, a.id, w, s), w, s);
}

static gv_t gen_val(gen_t *g, int depth);

static gv_t gen_leaf(gen_t *g)
{
   crv_t *c = g->c;
   const unsigned k = gen_below(g, 10);
   if (k < 5 && g->nsc > 0) {
      const unsigned v = gen_below(g, g->nsc);
      return gen_note(g, crv_var_ref(c, v), g->width[v], g->sgn[v]);
   }
   else if (k == 5 && g->na > 0) {
      const unsigned a = gen_below(g, g->na);
      switch (gen_below(g, 3)) {
      case 0:
         return gen_note(g, crv_array_size(c, g->arr[a]), 32, true);
      case 1:
         {
            const unsigned w = 1 + gen_below(g, g->maxw);
            const bool s = gen_below(g, 2);
            return gen_note(g, crv_array_sum(c, g->arr[a], w, s), w, s);
         }
      default:
         {
            // Index a variable or a constant, possibly out of range
            const int v = g->ebase[a];
            gv_t i;
            if (g->nsc > 0 && gen_below(g, 2) == 0) {
               const unsigned x = gen_below(g, g->nsc);
               i = gen_note(g, crv_var_ref(c, x), g->width[x], g->sgn[x]);
            }
            else {
               const unsigned w = 1 + gen_below(g, 4);
               const bool s = gen_below(g, 2);
               i = gen_note(g, crv_const(c, rep(rng_next(&g->r), w, s), w, s),
                            w, s);
            }
            return gen_note(g, crv_array_elem(c, g->arr[a], i.id),
                            g->width[v], g->sgn[v]);
         }
      }
   }
   else {
      const unsigned w = 1 + gen_below(g, g->maxw);
      const bool s = gen_below(g, 3) == 0;
      uint64_t x = rng_next(&g->r);
      if (gen_below(g, 4) == 0)
         x = gen_below(g, 3) == 0 ? ~UINT64_C(0) : gen_below(g, 4);
      return gen_note(g, crv_const(c, rep(x, w, s), w, s), w, s);
   }
}

static gv_t gen_bool(gen_t *g, int depth)
{
   crv_t *c = g->c;
   switch (depth <= 0 ? gen_below(g, 3) : gen_below(g, 11)) {
   case 0: case 1: case 2:
      {
         static const crv_binary_t ops[] = { CRV_EQ, CRV_NE, CRV_LT, CRV_LE,
                                             CRV_GT, CRV_GE };
         gv_t a = gen_val(g, depth - 1), b = gen_val(g, depth - 1);
         if (!g->mixed || gen_below(g, 4) != 0) {
            const unsigned w = a.w > b.w ? a.w : b.w;
            const bool s = a.s && b.s;
            a = gen_cast(g, a, w, s);
            b = gen_cast(g, b, w, s);
         }
         return gen_note(g, crv_binary(c, ops[gen_below(g, 6)], a.id, b.id, 1,
                                       false), 1, false);
      }
   case 3:
      {
         gv_t a = gen_val(g, depth - 1);
         crv_set_t set = crv_set_new(c);
         const int n = 1 + gen_below(g, 3);
         for (int i = 0; i < n; i++) {
            gv_t lo = gen_val(g, 0);
            if (gen_below(g, 2))
               crv_set_value(c, set, lo.id);
            else {
               gv_t hi = gen_val(g, 0);
               crv_set_range(c, set, lo.id, hi.id);
            }
         }
         return gen_note(g, crv_inside(c, a.id, set), 1, false);
      }
   case 4:
      {
         gv_t a = gen_val(g, depth - 1);
         crv_dist_t d = crv_dist_new(c);
         const int n = 1 + gen_below(g, 4);
         for (int i = 0; i < n; i++) {
            const int64_t x = rep(rng_next(&g->r), a.w, a.s);
            const int64_t y = rep(rng_next(&g->r), a.w, a.s);
            const bool sw = a.s ? x > y : (uint64_t)x > (uint64_t)y;
            crv_dist_range(c, d, sw ? y : x, sw ? x : y, gen_below(g, 4), 1);
         }
         return gen_note(g, crv_dist(c, a.id, d), 1, false);
      }
   case 5:
      {
         gv_t a = gen_bool(g, depth - 1);
         return gen_note(g, crv_unary(c, CRV_LOG_NOT, a.id), 1, false);
      }
   case 6: case 7: case 8:
      {
         static const crv_binary_t ops[] = { CRV_LOG_AND, CRV_LOG_OR,
                                             CRV_IMPLIES };
         gv_t a = gen_bool(g, depth - 1), b = gen_bool(g, depth - 1);
         return gen_note(g, crv_binary(c, ops[gen_below(g, 3)], a.id, b.id, 1,
                                       false), 1, false);
      }
   default:
      {
         static const crv_unary_t ops[] = { CRV_REDUCE_AND, CRV_REDUCE_OR,
                                            CRV_REDUCE_XOR, CRV_LOG_NOT };
         gv_t a = gen_val(g, depth - 1);
         return gen_note(g, crv_unary(c, ops[gen_below(g, 4)], a.id), 1,
                         false);
      }
   }
}

static gv_t gen_val(gen_t *g, int depth)
{
   crv_t *c = g->c;
   if (depth <= 0)
      return gen_leaf(g);

   switch (gen_below(g, 12)) {
   case 0: case 1:
      return gen_leaf(g);
   case 2:
      {
         static const crv_unary_t ops[] = { CRV_NEG, CRV_INV, CRV_POPCNT };
         gv_t a = gen_val(g, depth - 1);
         const crv_unary_t op = ops[gen_below(g, 3)];
         return gen_note(g, crv_unary(c, op, a.id),
                         op == CRV_POPCNT ? 32 : a.w,
                         op == CRV_POPCNT ? true : a.s);
      }
   case 3:
      {
         gv_t a = gen_val(g, depth - 1);
         return gen_cast(g, a, 1 + gen_below(g, g->maxw), gen_below(g, 2));
      }
   case 4:
      {
         gv_t t = gen_bool(g, depth - 1);
         gv_t a = gen_val(g, depth - 1), b = gen_val(g, depth - 1);
         const unsigned w = a.w > b.w ? a.w : b.w;
         const bool s = a.s && b.s;
         a = gen_cast(g, a, w, s);
         b = gen_cast(g, b, w, s);
         return gen_note(g, crv_cond(c, t.id, a.id, b.id), w, s);
      }
   default:
      {
         static const crv_binary_t ops[] = {
            CRV_ADD, CRV_SUB, CRV_MUL, CRV_AND, CRV_OR, CRV_XOR, CRV_SHL,
            CRV_SHR, CRV_ASR, CRV_DIV, CRV_MOD, CRV_ADD, CRV_AND, CRV_SUB,
         };
         crv_binary_t op = ops[gen_below(g, ARRAY_LEN(ops))];
         if (!g->heavy && (op == CRV_MUL || op == CRV_DIV || op == CRV_MOD))
            op = CRV_ADD;

         gv_t a = gen_val(g, depth - 1), b = gen_val(g, depth - 1);
         const unsigned w = 1 + gen_below(g, g->maxw);
         const bool s = gen_below(g, 2);
         const bool raw = g->mixed && gen_below(g, 5) == 0;
         if (op == CRV_SHL || op == CRV_SHR || op == CRV_ASR) {
            if (!raw)
               a = gen_cast(g, a, w, s);
         }
         else if (!raw) {
            a = gen_cast(g, a, w, s);
            b = gen_cast(g, b, w, s);
         }
         return gen_note(g, crv_binary(c, op, a.id, b.id, w, s), w, s);
      }
   }
}

// Variables and arrays: widths such that the free bits are at most maxbits
static void gen_setup(gen_t *g, crv_t *c, unsigned maxbits, unsigned maxw)
{
   g->c = c;
   g->maxw = maxw;

   g->nsc = 1 + gen_below(g, 4);
   unsigned budget = maxbits;
   for (unsigned i = 0; i < g->nsc; i++) {
      unsigned w = 1 + gen_below(g, maxw < 8 ? maxw : 8);
      if (maxw > 8 && gen_below(g, 3) == 0)
         w = 1 + gen_below(g, maxw);
      const unsigned left = budget - (g->nsc - 1 - i);
      if (w > left)
         w = left;
      budget -= w;
      g->width[i] = w;
      g->sgn[i] = gen_below(g, 3) == 0;
      crv_var(c, w, g->sgn[i], 0);
   }

   g->na = budget > 4 ? gen_below(g, MAX_ARRAYS + 1) : 0;
   unsigned nelem = 0;
   g->nv = g->nsc + g->na;
   for (unsigned a = 0; a < g->na; a++) {
      g->sizes[a] = 1 + gen_below(g, 3);
      unsigned w = 1 + gen_below(g, 4);
      if (w * g->sizes[a] > budget) {
         g->sizes[a] = 1;
         w = budget > 0 ? (budget < w ? budget : w) : 1;
      }
      budget -= (w * g->sizes[a] > budget) ? budget : w * g->sizes[a];
      const bool s = gen_below(g, 2);
      g->arr[a] = crv_array(c, w, s, g->sizes[a], false, true);
      g->width[g->nsc + a] = 32;
      g->sgn[g->nsc + a] = true;
      g->fixed[g->nsc + a] = true;
      g->value[g->nsc + a] = g->sizes[a];
      g->ebase[a] = g->nsc + g->na + nelem;
      nelem += g->sizes[a];
      g->bits += 0;
      for (unsigned i = 0; i < g->sizes[a]; i++) {
         const unsigned v = g->nsc + g->na + nelem - g->sizes[a] + i;
         g->width[v] = w;
         g->sgn[v] = s;
         g->fixed[v] = false;
      }
   }
   g->nv += nelem;

   g->bits = 0;
   for (unsigned v = 0; v < g->nv; v++) {
      if (!g->fixed[v])
         g->bits += g->width[v];
   }
}

////////////////////////////////////////////////////////////////////////////////
// Comparison with crv_eval

typedef struct {
   blast_t        *b;
   crv_t          *c;
   gen_t          *g;
   int64_t        *vals;       // lanes * nv
   unsigned        nodes;
   uint64_t        checked;
   unsigned        gates;
} chk_t;

static void shape_of(const gen_t *g, blast_shape_t *s)
{
   *s = (blast_shape_t){
      .nv = g->nv, .width = g->width, .sgn = g->sgn, .nsc = g->nsc,
      .ebase = g->ebase, .sizes = g->sizes, .fixed = g->fixed,
      .value = g->value,
   };
}

static void prepare(crv_t *c)
{
   for (unsigned i = 0; i < c->ndists; i++)
      crv_dist_prepare(&c->dists[i]);
}

// Compare every node on the assignments in vals
static void compare(chk_t *k, unsigned lanes, const char *what, uint64_t seed)
{
   blast_sim(k->b, k->vals, lanes);

   const gen_t *g = k->g;
   for (unsigned i = 0; i < g->nlist; i++) {
      const gv_t n = g->list[i];
      const uint64_t fl = blast_lit(k->b, blast_fail(k->b, n.id));
      uint64_t bits[64];
      for (unsigned j = 0; j < n.w; j++)
         bits[j] = blast_lit(k->b, blast_bit(k->b, n.id, j));
      const uint64_t holds = blast_lit(k->b, blast_holds(k->b, n.id));

      for (unsigned l = 0; l < lanes; l++) {
         const int64_t *vals = k->vals + (size_t)l * g->nv;
         bool fail;
         const int64_t v = crv_eval(k->c, n.id, vals, g->nsc, g->ebase,
                                    g->sizes, &fail);
         if (fail != !!((fl >> l) & 1))
            ck_abort_msg("%s seed %"PRIu64" node %d (kind %d op %d w %u): "
                         "undefined %d, circuit %d", what, seed, n.id,
                         k->c->nodes[n.id].kind, k->c->nodes[n.id].op, n.w,
                         fail, (int)((fl >> l) & 1));
         if (fail)
            continue;

         if (!!((holds >> l) & 1) != (v != 0))
            ck_abort_msg("%s seed %"PRIu64" node %d: holds mismatch",
                         what, seed, n.id);

         for (unsigned j = 0; j < n.w; j++) {
            if (((bits[j] >> l) & 1) != (((uint64_t)v >> j) & 1))
               ck_abort_msg("%s seed %"PRIu64" node %d (kind %d op %d w %u "
                            "%s): bit %u of value 0x%"PRIx64" is %d in the "
                            "circuit, lane %u", what, seed, n.id,
                            k->c->nodes[n.id].kind, k->c->nodes[n.id].op,
                            n.w, n.s ? "signed" : "unsigned", j,
                            (uint64_t)v, (int)((bits[j] >> l) & 1), l);
         }
         k->checked++;
      }
   }
}

// Value of variable v for the pattern x of the free bits
static void decode(const gen_t *g, uint64_t x, int64_t *vals)
{
   unsigned pos = 0;
   for (unsigned v = 0; v < g->nv; v++) {
      if (g->fixed[v])
         vals[v] = g->value[v];
      else {
         vals[v] = rep(x >> pos, g->width[v], g->sgn[v]);
         pos += g->width[v];
      }
   }
}

static void blast_all(chk_t *k, gen_t *g)
{
   blast_shape_t shape;
   shape_of(g, &shape);
   k->b = blast_new(k->c, &shape);
   k->g = g;
   for (unsigned i = 0; i < g->nlist; i++)
      (void)blast_holds(k->b, g->list[i].id);
   prepare(k->c);
   k->gates = blast_stats(k->b).gates;
}

static uint64_t exhaustive_seeds(unsigned n)
{
   const char *e = getenv("NVC_CRV_LONG");
   return e != NULL && *e && strcmp(e, "0") != 0 ? n * 20 : n;
}

START_TEST(test_exhaustive)
{
   // Random graphs of up to 16 bits: every assignment
   const uint64_t base = (uint64_t)_i * 1000;
   uint64_t checked = 0;
   const uint64_t n = exhaustive_seeds(60);
   for (uint64_t seed = base; seed < base + n; seed++) {
      crv_t *c = crv_new();
      gen_t *g = xcalloc(sizeof(gen_t));
      g->r.s = seed;
      g->heavy = true;
      g->mixed = true;
      gen_setup(g, c, 16, 12);

      const int roots = 1 + gen_below(g, 3);
      for (int i = 0; i < roots; i++) {
         if (gen_below(g, 3) == 0)
            gen_val(g, 3);
         else
            gen_bool(g, 3);
      }

      chk_t k = { .c = c };
      blast_all(&k, g);
      k.vals = xmalloc_array(64 * g->nv, sizeof(int64_t));

      // NVC_CRV_BLAST_SMT2=<prefix> writes the check of the circuit for an
      // SMT solver, which should find it unsatisfiable
      const char *smt = getenv("NVC_CRV_BLAST_SMT2");
      if (smt != NULL && *smt) {
         char *name = xasprintf("%s.%"PRIu64".smt2", smt, seed);
         FILE *f = fopen(name, "w");
         if (f != NULL) {
            crv_node_t *roots = xmalloc_array(g->nlist, sizeof(crv_node_t));
            for (unsigned i = 0; i < g->nlist; i++)
               roots[i] = g->list[i].id;
            blast_dump_smt2(k.b, f, roots, g->nlist);
            free(roots);
            fclose(f);
         }
         free(name);
      }

      const uint64_t total = UINT64_C(1) << g->bits;
      for (uint64_t x = 0; x < total; x += 64) {
         const unsigned lanes = total - x < 64 ? total - x : 64;
         for (unsigned l = 0; l < lanes; l++)
            decode(g, x + l, k.vals + (size_t)l * g->nv);
         compare(&k, lanes, "exhaustive", seed);
      }

      checked += k.checked;
      free(k.vals);
      blast_free(k.b);
      free(g);
      crv_free(c);
   }
   ck_assert_uint_gt(checked, 0);
}
END_TEST

START_TEST(test_random_wide)
{
   // Wide operators: 10^5 random assignments, a few with extreme values
   const uint64_t base = (uint64_t)_i * 1000 + 500000;
   uint64_t checked = 0;
   const uint64_t n = exhaustive_seeds(12);
   for (uint64_t seed = base; seed < base + n; seed++) {
      crv_t *c = crv_new();
      gen_t *g = xcalloc(sizeof(gen_t));
      g->r.s = seed;
      g->heavy = gen_below(g, 2);
      g->mixed = true;

      // Up to 64 bit variables, sized such that a graph is not huge
      g->c = c;
      g->maxw = 64;
      g->nsc = 2 + gen_below(g, 3);
      for (unsigned i = 0; i < g->nsc; i++) {
         static const unsigned ws[] = { 17, 24, 32, 33, 48, 63, 64, 64 };
         g->width[i] = ws[gen_below(g, ARRAY_LEN(ws))];
         g->sgn[i] = gen_below(g, 2);
         crv_var(c, g->width[i], g->sgn[i], 0);
      }
      g->nv = g->nsc;
      g->na = 0;

      const int roots = 1 + gen_below(g, 3);
      for (int i = 0; i < roots; i++) {
         if (gen_below(g, 2) == 0)
            gen_val(g, 2);
         else
            gen_bool(g, 2);
      }

      chk_t k = { .c = c };
      blast_all(&k, g);
      k.vals = xmalloc_array(64 * g->nv, sizeof(int64_t));

      for (unsigned batch = 0; batch < 1600; batch++) {
         for (unsigned l = 0; l < 64; l++) {
            for (unsigned v = 0; v < g->nv; v++) {
               uint64_t x = rng_next(&g->r);
               switch (gen_below(g, 8)) {
               case 0: x = 0; break;
               case 1: x = ~UINT64_C(0); break;
               case 2: x = UINT64_C(1) << gen_below(g, 64); break;
               case 3: x = (UINT64_C(1) << gen_below(g, 64)) - 1; break;
               default: break;
               }
               k.vals[(size_t)l * g->nv + v] = rep(x, g->width[v], g->sgn[v]);
            }
         }
         compare(&k, 64, "random", seed);
      }

      checked += k.checked;
      free(k.vals);
      blast_free(k.b);
      free(g);
      crv_free(c);
   }
   ck_assert_uint_gt(checked, 0);
}
END_TEST

////////////////////////////////////////////////////////////////////////////////
// Every operator at wide widths

typedef struct {
   const char *name;
   bool        un;
   int         op;
} wop_t;

static const wop_t wops[] = {
   { "add", false, CRV_ADD }, { "sub", false, CRV_SUB },
   { "mul", false, CRV_MUL }, { "div", false, CRV_DIV },
   { "mod", false, CRV_MOD }, { "and", false, CRV_AND },
   { "or", false, CRV_OR }, { "xor", false, CRV_XOR },
   { "shl", false, CRV_SHL }, { "shr", false, CRV_SHR },
   { "asr", false, CRV_ASR }, { "eq", false, CRV_EQ },
   { "ne", false, CRV_NE }, { "lt", false, CRV_LT }, { "le", false, CRV_LE },
   { "gt", false, CRV_GT }, { "ge", false, CRV_GE },
   { "land", false, CRV_LOG_AND }, { "lor", false, CRV_LOG_OR },
   { "implies", false, CRV_IMPLIES },
   { "neg", true, CRV_NEG }, { "inv", true, CRV_INV },
   { "lnot", true, CRV_LOG_NOT }, { "rand", true, CRV_REDUCE_AND },
   { "ror", true, CRV_REDUCE_OR }, { "rxor", true, CRV_REDUCE_XOR },
   { "popcnt", true, CRV_POPCNT },
};

START_TEST(test_wide_ops)
{
   // One operator on 17, 32, 33 and 64 bit operands, unsigned and signed,
   // also with a mixed pair: 10^5 random assignments, a quarter of them
   // with edge values and small shift counts
   static const unsigned widths[] = { 17, 32, 33, 64 };
   const wop_t *op = &wops[_i % ARRAY_LEN(wops)];
   const unsigned w = widths[(_i / ARRAY_LEN(wops)) % ARRAY_LEN(widths)];
   const unsigned mode = _i / ARRAY_LEN(wops) / ARRAY_LEN(widths);   // 0..2

   const bool s = mode == 1;
   const bool bs = mode == 2 ? !s : s;
   const unsigned bw = mode == 2 ? (w > 20 ? w - 9 : w + 5) : w;

   crv_t *c = crv_new();
   crv_node_t x = crv_var_ref(c, crv_var(c, w, s, 0));
   crv_node_t y = crv_var_ref(c, crv_var(c, bw, bs, 0));

   crv_node_t n;
   if (op->un)
      n = crv_unary(c, op->op, x);
   else if (op->op >= CRV_EQ) {
      // Comparisons of unlike operands are exact
      n = crv_binary(c, op->op, x, y, 1, false);
   }
   else if (mode == 2 && op->op != CRV_SHL && op->op != CRV_SHR
            && op->op != CRV_ASR)
      n = crv_binary(c, op->op, x, y, w, s);    // Operands not cast
   else
      n = crv_binary(c, op->op, x, mode == 2 ? y : crv_cast(c, y, w, s), w, s);

   uint8_t width[2] = { w, bw };
   bool sgn[2] = { s, bs };
   blast_shape_t shape = { .nv = 2, .width = width, .sgn = sgn, .nsc = 2 };
   blast_t *b = blast_new(c, &shape);
   (void)blast_holds(b, n);
   const unsigned nw = blast_width(b, n);

   rng_t r = { 1000 + (uint64_t)_i };
   int64_t vals[64 * 2];
   for (unsigned batch = 0; batch < 1600; batch++) {
      for (unsigned l = 0; l < 64; l++) {
         for (unsigned v = 0; v < 2; v++) {
            uint64_t x = rng_next(&r);
            switch (rng_below(&r, 8)) {
            case 0: x = 0; break;
            case 1: x = ~UINT64_C(0); break;
            case 2: x = UINT64_C(1) << rng_below(&r, 64); break;
            case 3: x = (UINT64_C(1) << rng_below(&r, 64)) - 1; break;
            case 4: x = rng_below(&r, 80); break;      // Small counts
            default: break;
            }
            vals[l * 2 + v] = rep(x, width[v], sgn[v]);
         }
      }

      blast_sim(b, vals, 64);
      const uint64_t fl = blast_lit(b, blast_fail(b, n));
      uint64_t bits[64];
      for (unsigned j = 0; j < nw; j++)
         bits[j] = blast_lit(b, blast_bit(b, n, j));

      for (unsigned l = 0; l < 64; l++) {
         bool fail;
         const int64_t v = crv_eval(c, n, vals + l * 2, 2, NULL, NULL, &fail);
         if (fail != !!((fl >> l) & 1))
            ck_abort_msg("%s %u: undefined", op->name, w);
         else if (fail)
            continue;

         for (unsigned j = 0; j < nw; j++) {
            if (((bits[j] >> l) & 1) != (((uint64_t)v >> j) & 1))
               ck_abort_msg("%s %u mode %u: bit %u of 0x%"PRIx64" a 0x%"PRIx64
                            " b 0x%"PRIx64, op->name, w, mode, j,
                            (uint64_t)v, (uint64_t)vals[l * 2],
                            (uint64_t)vals[l * 2 + 1]);
         }
      }
   }

   blast_free(b);
   crv_free(c);
}
END_TEST

////////////////////////////////////////////////////////////////////////////////
// Chunked 128 bit operators

typedef struct {
   crv_t *c;
   crv_node_t v[4];     // a0 a1 b0 b1
} wide_t;

static crv_node_t u64(crv_t *c, crv_binary_t op, crv_node_t a, crv_node_t b)
{
   return crv_binary(c, op, a, b, 64, false);
}

static crv_node_t bit64(crv_t *c, crv_binary_t op, crv_node_t a, crv_node_t b)
{
   return crv_cast(c, crv_binary(c, op, a, b, 1, false), 64, false);
}

static crv_node_t k64(crv_t *c, uint64_t k)
{
   return crv_const(c, (int64_t)k, 64, false);
}

START_TEST(test_chunked128)
{
   // 128 bit values held as two 64 bit chunks, the graph that the lowering
   // makes for wide operators.  Results are checked against the host's
   // 128 bit arithmetic as well as crv_eval.
   crv_t *c = crv_new();
   crv_node_t a0 = crv_var_ref(c, crv_var(c, 64, false, 0));
   crv_node_t a1 = crv_var_ref(c, crv_var(c, 64, false, 0));
   crv_node_t b0 = crv_var_ref(c, crv_var(c, 64, false, 0));
   crv_node_t b1 = crv_var_ref(c, crv_var(c, 64, false, 0));

   crv_node_t out[12];
   unsigned no = 0;

   // Sum
   crv_node_t sum0 = u64(c, CRV_ADD, a0, b0);
   crv_node_t carry = bit64(c, CRV_LT, sum0, a0);
   crv_node_t sum1 = u64(c, CRV_ADD, u64(c, CRV_ADD, a1, b1), carry);
   out[no++] = sum0;
   out[no++] = sum1;

   // Difference
   crv_node_t dif0 = u64(c, CRV_SUB, a0, b0);
   crv_node_t borrow = bit64(c, CRV_LT, a0, b0);
   crv_node_t dif1 = u64(c, CRV_SUB, u64(c, CRV_SUB, a1, b1), borrow);
   out[no++] = dif0;
   out[no++] = dif1;

   // Unsigned less than and equality
   crv_node_t hlt = bit64(c, CRV_LT, a1, b1);
   crv_node_t heq = bit64(c, CRV_EQ, a1, b1);
   crv_node_t llt = bit64(c, CRV_LT, a0, b0);
   crv_node_t lt = u64(c, CRV_OR, hlt, u64(c, CRV_AND, heq, llt));
   crv_node_t leq = bit64(c, CRV_EQ, a0, b0);
   crv_node_t eq = u64(c, CRV_AND, heq, leq);
   out[no++] = lt;
   out[no++] = eq;

   // Low 128 bits of the product, the high half of a0 * b0 from 32 bit
   // limbs
   crv_node_t m32 = k64(c, 0xffffffff);
   crv_node_t sh32 = crv_const(c, 32, 7, false);
   crv_node_t l1 = u64(c, CRV_AND, a0, m32);
   crv_node_t h1 = crv_binary(c, CRV_SHR, a0, sh32, 64, false);
   crv_node_t l2 = u64(c, CRV_AND, b0, m32);
   crv_node_t h2 = crv_binary(c, CRV_SHR, b0, sh32, 64, false);
   crv_node_t t = u64(c, CRV_MUL, l1, l2);
   crv_node_t u = u64(c, CRV_ADD, u64(c, CRV_MUL, h1, l2),
                      crv_binary(c, CRV_SHR, t, sh32, 64, false));
   crv_node_t w = u64(c, CRV_ADD, u64(c, CRV_MUL, l1, h2),
                      u64(c, CRV_AND, u, m32));
   crv_node_t mhi = u64(c, CRV_ADD,
                        u64(c, CRV_ADD, u64(c, CRV_MUL, h1, h2),
                            crv_binary(c, CRV_SHR, u, sh32, 64, false)),
                        crv_binary(c, CRV_SHR, w, sh32, 64, false));
   crv_node_t prod0 = u64(c, CRV_MUL, a0, b0);
   crv_node_t prod1 = u64(c, CRV_ADD, mhi,
                          u64(c, CRV_ADD, u64(c, CRV_MUL, a1, b0),
                              u64(c, CRV_MUL, a0, b1)));
   out[no++] = prod0;
   out[no++] = prod1;

   // Bitwise and a shift of the whole by a constant of 40 bits:
   // hi = (a1 << 40) | (a0 >> 24), lo = a0 << 40
   crv_node_t sh40 = crv_const(c, 40, 7, false), sh24 = crv_const(c, 24, 7, false);
   crv_node_t shl0 = crv_binary(c, CRV_SHL, a0, sh40, 64, false);
   crv_node_t shl1 = u64(c, CRV_OR, crv_binary(c, CRV_SHL, a1, sh40, 64, false),
                         crv_binary(c, CRV_SHR, a0, sh24, 64, false));
   out[no++] = shl0;
   out[no++] = shl1;

   uint8_t width[4] = { 64, 64, 64, 64 };
   bool sgn[4] = { false };
   blast_shape_t shape = { .nv = 4, .width = width, .sgn = sgn, .nsc = 4 };
   blast_t *b = blast_new(c, &shape);
   for (unsigned i = 0; i < no; i++)
      (void)blast_holds(b, out[i]);

   rng_t r = { 128 };
   int64_t vals[64 * 4];
   uint64_t checked = 0;
   for (unsigned batch = 0; batch < 1600; batch++) {
      for (unsigned l = 0; l < 64; l++) {
         for (unsigned v = 0; v < 4; v++) {
            uint64_t x = rng_next(&r);
            switch (rng_below(&r, 6)) {
            case 0: x = 0; break;
            case 1: x = ~UINT64_C(0); break;
            case 2: x = UINT64_C(1) << rng_below(&r, 64); break;
            default: break;
            }
            vals[l * 4 + v] = (int64_t)x;
         }
      }

      blast_sim(b, vals, 64);
      for (unsigned l = 0; l < 64; l++) {
         const unsigned __int128 A = ((unsigned __int128)(uint64_t)vals[l * 4 + 1] << 64)
            | (uint64_t)vals[l * 4];
         const unsigned __int128 B = ((unsigned __int128)(uint64_t)vals[l * 4 + 3] << 64)
            | (uint64_t)vals[l * 4 + 2];
         const unsigned __int128 S = A + B, D = A - B, P = A * B,
            SH = A << 40;
         const uint64_t want[12] = {
            (uint64_t)S, (uint64_t)(S >> 64), (uint64_t)D, (uint64_t)(D >> 64),
            A < B, A == B, (uint64_t)P, (uint64_t)(P >> 64),
            (uint64_t)SH, (uint64_t)(SH >> 64),
         };

         for (unsigned i = 0; i < no; i++) {
            uint64_t got = 0;
            for (unsigned j = 0; j < 64; j++)
               got |= ((blast_lit(b, blast_bit(b, out[i], j)) >> l) & 1) << j;

            bool fail;
            const int64_t v = crv_eval(c, out[i], vals + l * 4, 4, NULL, NULL,
                                       &fail);
            ck_assert(!fail);
            ck_assert_msg(got == (uint64_t)v, "output %u circuit 0x%"PRIx64
                          " crv_eval 0x%"PRIx64, i, got, (uint64_t)v);
            ck_assert_msg(got == want[i], "output %u circuit 0x%"PRIx64
                          " host 0x%"PRIx64, i, got, want[i]);
            checked++;
         }
      }
   }

   ck_assert_uint_gt(checked, 0);
   blast_free(b);
   crv_free(c);
}
END_TEST

////////////////////////////////////////////////////////////////////////////////
// Unique sets and the facts about the circuit

START_TEST(test_unique)
{
   // Distinct values of three 3 bit variables, one of them a sum
   crv_t *c = crv_new();
   crv_node_t x = crv_var_ref(c, crv_var(c, 3, false, 0));
   crv_node_t y = crv_var_ref(c, crv_var(c, 3, false, 0));
   crv_node_t z = crv_var_ref(c, crv_var(c, 3, false, 0));
   crv_node_t s = crv_binary(c, CRV_ADD, x, z, 3, false);
   const crv_node_t set[] = { x, y, s };

   uint8_t width[3] = { 3, 3, 3 };
   bool sgn[3] = { false };
   blast_shape_t shape = { .nv = 3, .width = width, .sgn = sgn, .nsc = 3 };
   blast_t *b = blast_new(c, &shape);
   const blit_t ok = blast_unique(b, set, 3);

   int64_t vals[64 * 3];
   for (unsigned l = 0; l < 64; l++) {
      vals[l * 3] = l & 7;
      vals[l * 3 + 1] = (l >> 3) & 7;
      vals[l * 3 + 2] = (l * 5) & 7;
   }
   blast_sim(b, vals, 64);
   for (unsigned l = 0; l < 64; l++) {
      const int xv = vals[l * 3], yv = vals[l * 3 + 1], zv = vals[l * 3 + 2];
      const int sv = (xv + zv) & 7;
      const bool want = xv != yv && xv != sv && yv != sv;
      ck_assert_int_eq((blast_lit(b, ok) >> l) & 1, want);
   }

   blast_free(b);
   crv_free(c);
}
END_TEST

START_TEST(test_stats)
{
   // The nonlinear width counts only operands with variable bits
   crv_t *c = crv_new();
   crv_node_t x = crv_var_ref(c, crv_var(c, 16, false, 0));
   crv_node_t y = crv_var_ref(c, crv_var(c, 12, false, 0));
   crv_node_t k = crv_const(c, 1234, 16, false);

   uint8_t width[2] = { 16, 12 };
   bool sgn[2] = { false };
   blast_shape_t shape = { .nv = 2, .width = width, .sgn = sgn, .nsc = 2 };
   blast_t *b = blast_new(c, &shape);

   (void)blast_bit(b, crv_binary(c, CRV_MUL, x, k, 16, false), 0);
   ck_assert_uint_eq(blast_stats(b).nonlinear, 0);
   (void)blast_bit(b, crv_binary(c, CRV_ADD, x, y, 16, false), 0);
   ck_assert_uint_eq(blast_stats(b).nonlinear, 0);
   ck_assert_uint_eq(blast_stats(b).nops, 1);

   crv_node_t m = crv_binary(c, CRV_MUL, x, crv_cast(c, y, 16, false), 16,
                             false);
   (void)blast_bit(b, m, 0);
   ck_assert_uint_eq(blast_stats(b).nonlinear, 12);
   ck_assert_uint_eq(blast_stats(b).nops, 2);

   unsigned n;
   const blast_op_t *ops = blast_ops(b, &n);
   ck_assert_uint_eq(n, 2);
   ck_assert_int_eq(ops[1].op, CRV_MUL);
   ck_assert_uint_eq(ops[1].wa, 16);
   ck_assert_uint_eq(ops[1].wb, 12);

   // Inputs in the order of first use from an output, each once
   crv_node_t sum = crv_binary(c, CRV_ADD, crv_cast(c, x, 12, false), y, 12,
                               false);
   blit_t out = blast_bit(b, sum, 11);
   unsigned order[28];
   const unsigned nin = blast_fanin_order(b, &out, 1, order);
   ck_assert_uint_eq(nin, 24);
   bool seen[28] = { false };
   for (unsigned i = 0; i < nin; i++) {
      ck_assert(!seen[order[i]]);
      seen[order[i]] = true;
      int var;
      unsigned bit;
      blast_input_var(b, order[i], &var, &bit);
      ck_assert_int_lt(var, 2);
      ck_assert_uint_lt(bit, 12);
   }

   blast_free(b);
   crv_free(c);
}
END_TEST

////////////////////////////////////////////////////////////////////////////////
// Gate counts of each operator

static unsigned gates_of(crv_binary_t op, unsigned w, bool s, bool konst,
                         bool un, crv_unary_t uop)
{
   crv_t *c = crv_new();
   crv_node_t x = crv_var_ref(c, crv_var(c, w, s, 0));
   const bool shift = op == CRV_SHL || op == CRV_SHR || op == CRV_ASR;
   crv_node_t y = konst ? crv_const(c, shift ? 5 : 0x2545f4914f6cdd1dULL, w, s)
      : crv_var_ref(c, crv_var(c, w, s, 0));

   uint8_t width[2] = { w, w };
   bool sgn[2] = { s, s };
   blast_shape_t shape = { .nv = konst ? 1 : 2, .width = width, .sgn = sgn,
                           .nsc = konst ? 1 : 2 };
   blast_t *b = blast_new(c, &shape);

   crv_node_t n;
   if (un)
      n = crv_unary(c, uop, x);
   else
      n = crv_binary(c, op, x, y, op >= CRV_EQ ? 1 : w, s);
   blit_t outs[64];
   const unsigned nb = blast_width(b, n);
   for (unsigned i = 0; i < nb; i++)
      outs[i] = blast_bit(b, n, i);
   blit_t f = blast_fail(b, n);
   unsigned cone = blast_cone_gates(b, outs, nb);
   (void)f;
   blast_free(b);
   crv_free(c);
   return cone;
}

START_TEST(test_gate_counts)
{
   // Gates of every operator on operands of 8, 16, 32 and 64 bits: both
   // variable, then the right operand constant.  NVC_CRV_GATES=<file> writes
   // the table.
   static const struct { const char *name; crv_binary_t op; } bins[] = {
      { "add", CRV_ADD }, { "sub", CRV_SUB }, { "mul", CRV_MUL },
      { "div", CRV_DIV }, { "mod", CRV_MOD }, { "and", CRV_AND },
      { "or", CRV_OR }, { "xor", CRV_XOR }, { "shl", CRV_SHL },
      { "shr", CRV_SHR }, { "asr", CRV_ASR }, { "eq", CRV_EQ },
      { "ne", CRV_NE }, { "lt", CRV_LT }, { "le", CRV_LE },
      { "gt", CRV_GT }, { "ge", CRV_GE },
   };
   static const struct { const char *name; crv_unary_t op; } uns[] = {
      { "neg", CRV_NEG }, { "inv", CRV_INV }, { "lognot", CRV_LOG_NOT },
      { "redand", CRV_REDUCE_AND }, { "redor", CRV_REDUCE_OR },
      { "redxor", CRV_REDUCE_XOR }, { "popcnt", CRV_POPCNT },
   };
   static const unsigned widths[] = { 8, 16, 32, 64 };

   FILE *f = NULL;
   const char *path = getenv("NVC_CRV_GATES");
   if (path != NULL && *path)
      f = fopen(path, "w");
   if (f != NULL)
      fprintf(f, "# AND gates of the circuit of one operator on operands of "
              "the same width\n# operator, signedness, operand width, "
              "gates with two variable operands, gates with the second "
              "operand constant\n");

   for (size_t i = 0; i < ARRAY_LEN(bins); i++) {
      for (int s = 0; s < 2; s++) {
         for (size_t k = 0; k < ARRAY_LEN(widths); k++) {
            const unsigned vv = gates_of(bins[i].op, widths[k], s, false,
                                         false, 0);
            const unsigned vc = gates_of(bins[i].op, widths[k], s, true,
                                         false, 0);
            if (f != NULL)
               fprintf(f, "%-7s %-9s %2u %7u %7u\n", bins[i].name,
                       s ? "signed" : "unsigned", widths[k], vv, vc);

            // Sanity bounds: linear operators stay small, the others
            // quadratic
            const bool quad = bins[i].op == CRV_MUL || bins[i].op == CRV_DIV
               || bins[i].op == CRV_MOD;
            const unsigned w = widths[k];
            ck_assert_uint_gt(vv, 0);
            ck_assert_uint_le(vv, quad ? 40 * w * w : 40 * w * 7 + 200);
            ck_assert_uint_le(vc, vv + 6 * w);
         }
      }
   }

   for (size_t i = 0; i < ARRAY_LEN(uns); i++) {
      for (size_t k = 0; k < ARRAY_LEN(widths); k++) {
         const unsigned v = gates_of(0, widths[k], false, false, true,
                                     uns[i].op);
         if (f != NULL)
            fprintf(f, "%-7s %-9s %2u %7u %7s\n", uns[i].name, "unsigned",
                    widths[k], v, "-");
         ck_assert_uint_le(v, 20 * widths[k]);
      }
   }

   if (f != NULL)
      fclose(f);
}
END_TEST

START_TEST(test_dump_smt2)
{
   // The check of the circuit names the variables, defines the nodes at
   // word level and the gates, and asserts that they differ
   crv_t *c = crv_new();
   crv_node_t x = crv_var_ref(c, crv_var(c, 6, true, 0));
   crv_node_t y = crv_var_ref(c, crv_var(c, 6, true, 0));
   crv_node_t q = crv_binary(c, CRV_DIV, x, y, 6, true);

   uint8_t width[2] = { 6, 6 };
   bool sgn[2] = { true, true };
   blast_shape_t shape = { .nv = 2, .width = width, .sgn = sgn, .nsc = 2 };
   blast_t *b = blast_new(c, &shape);
   (void)blast_holds(b, q);

   char *buf = NULL;
   size_t len = 0;
   FILE *f = open_memstream(&buf, &len);
   blast_dump_smt2(b, f, &q, 1);
   fclose(f);

   ck_assert_ptr_nonnull(strstr(buf, "(declare-const v0 (_ BitVec 6))"));
   ck_assert_ptr_nonnull(strstr(buf, "bvsdiv"));
   ck_assert_ptr_nonnull(strstr(buf, "define-fun g"));
   ck_assert_ptr_nonnull(strstr(buf, "(check-sat)"));
   free(buf);

   // The problem itself
   f = open_memstream(&buf, &len);
   blast_dump_problem_smt2(b, f, &q, 1, NULL, 0, "(assert false)\n");
   fclose(f);
   ck_assert_ptr_nonnull(strstr(buf, "(assert (and (not f2)"));
   ck_assert_ptr_nonnull(strstr(buf, "(assert false)"));
   free(buf);

   blast_free(b);
   crv_free(c);

   // An index of 64 bits: no index is out of the range of the type by size
   c = crv_new();
   crv_var(c, 64, false, 0);
   crv_array_t a = crv_array(c, 8, false, 3, false, true);
   crv_node_t i = crv_var_ref(c, 0);
   crv_node_t e = crv_array_elem(c, a, i);
   uint8_t w2[5] = { 64, 32, 8, 8, 8 };
   bool s2[5] = { false, true, false, false, false };
   bool fixed[5] = { false, true };
   int64_t value[5] = { 0, 3 };
   int ebase[1] = { 2 };
   unsigned sizes[1] = { 3 };
   blast_shape_t shape2 = { .nv = 5, .width = w2, .sgn = s2, .nsc = 1,
                            .ebase = ebase, .sizes = sizes, .fixed = fixed,
                            .value = value };
   b = blast_new(c, &shape2);
   (void)blast_holds(b, e);
   f = open_memstream(&buf, &len);
   blast_dump_smt2(b, f, &e, 1);
   fclose(f);
   ck_assert_ptr_nonnull(strstr(buf, "(ite (= ((_ zero_extend 1) n0)"));
   free(buf);

   blast_free(b);
   crv_free(c);
}
END_TEST

Suite *get_blast_tests(void)
{
   Suite *s = suite_create("blast");

   TCase *tc = tcase_create("Core");
   tcase_set_timeout(tc, 3600.0);
   tcase_add_loop_test(tc, test_exhaustive, 0, 10);
   tcase_add_loop_test(tc, test_random_wide, 0, 6);
   tcase_add_loop_test(tc, test_wide_ops, 0, 27 * 4 * 3);
   tcase_add_test(tc, test_chunked128);
   tcase_add_test(tc, test_unique);
   tcase_add_test(tc, test_stats);
   tcase_add_test(tc, test_gate_counts);
   tcase_add_test(tc, test_dump_smt2);
   suite_add_tcase(s, tc);

   return s;
}
