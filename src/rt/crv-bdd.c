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
#include "rt/crv-bdd.h"

#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define TERM_VAR 0x7fffffffu
#define NO_NODE  UINT32_MAX

typedef struct {
   uint32_t a, b, r;     // a holds the operation in its top bits
} centry_t;

typedef enum { OP_AND, OP_OR } bop_t;

struct _bdd {
   // Node n is (var[n], lo[n], hi[n]) and chained from the table slot of its
   // hash through next[n]; notc[n] is its negation once known, else 0
   uint32_t *var, *lo, *hi, *next, *notc;
   unsigned  nn, cap, max;
   uint32_t *heads;
   unsigned  hsize, hcap;
   centry_t *comp;
   unsigned  csize, ccap;
   uint64_t *cnt;         // nl limbs per node
   uint8_t  *cdone;
   unsigned  ncnt, nl;
   unsigned *vidx;        // Index of a variable among those counted
   unsigned  nvidx;
   unsigned  nvars;
   bool      over, busy;
   unsigned  cused;       // Counts made in the build
};

static __thread bdd_t *manager;

static void grow_nodes(bdd_t *b)
{
   unsigned cap = b->cap ? b->cap * 2 : 256;
   if (cap > b->max)
      cap = b->max;
   assert(cap > b->cap);

   b->var = xrealloc_array(b->var, cap, sizeof(uint32_t));
   b->lo = xrealloc_array(b->lo, cap, sizeof(uint32_t));
   b->hi = xrealloc_array(b->hi, cap, sizeof(uint32_t));
   b->next = xrealloc_array(b->next, cap, sizeof(uint32_t));
   b->notc = xrealloc_array(b->notc, cap, sizeof(uint32_t));
   b->cap = cap;
}

static inline unsigned hash3(uint32_t v, uint32_t l, uint32_t h)
{
   uint32_t x = v * 0x9e3779b1u ^ l * 0x85ebca6bu ^ h * 0xc2b2ae35u;
   x ^= x >> 15;
   x *= 0x2c1b3c6du;
   x ^= x >> 12;
   return x;
}

static void rehash(bdd_t *b, unsigned size)
{
   if (size > b->hcap) {
      b->heads = xrealloc_array(b->heads, size, sizeof(uint32_t));
      b->hcap = size;
      }
   b->hsize = size;
   memset(b->heads, 0, size * sizeof(uint32_t));
   for (unsigned n = 2; n < b->nn; n++) {
      const unsigned h = hash3(b->var[n], b->lo[n], b->hi[n]) & (size - 1);
      b->next[n] = b->heads[h];
      b->heads[h] = n;
   }
}

static void comp_size(bdd_t *b, unsigned size)
{
   if (size > b->ccap) {
      b->comp = xrealloc_array(b->comp, size, sizeof(centry_t));
      b->ccap = size;
      }
   b->csize = size;
   memset(b->comp, 0xff, size * sizeof(centry_t));
}

bdd_t *bdd_open(unsigned nvars, unsigned max_nodes)
{
   if (manager == NULL)
      manager = xcalloc(sizeof(bdd_t));

   bdd_t *b = manager;
   assert(!b->busy);
   b->busy = true;
   b->over = false;
   b->nvars = nvars;
   b->max = max_nodes < 4 ? 4 : max_nodes;
   b->nn = 2;
   b->cused = 0;

   if (b->cap == 0)
      grow_nodes(b);

   for (unsigned i = 0; i < 2; i++) {
      b->var[i] = TERM_VAR;
      b->lo[i] = b->hi[i] = i;
      b->next[i] = 0;
      b->notc[i] = 1 - i;
   }

   rehash(b, 256);
   comp_size(b, 256);
   return b;
}

void bdd_close(bdd_t *b)
{
   b->busy = false;
}

bool bdd_over(const bdd_t *b)
{
   return b->over;
}

unsigned bdd_nodes(const bdd_t *b)
{
   return b->nn;
}

size_t bdd_bytes(const bdd_t *b)
{
   // The memory the build needs, not what an earlier larger one left
   return (size_t)b->nn * (5 * sizeof(uint32_t))
      + (size_t)b->hsize * sizeof(uint32_t)
      + (size_t)b->csize * sizeof(centry_t)
      + (size_t)b->cused * (b->nl * sizeof(uint64_t) + 1);
}

unsigned bdd_top(const bdd_t *b, bnode_t f)
{
   return b->var[f];
}

static bnode_t mk(bdd_t *b, uint32_t v, bnode_t lo, bnode_t hi)
{
   if (lo == hi)
      return lo;

   unsigned h = hash3(v, lo, hi) & (b->hsize - 1);
   for (uint32_t n = b->heads[h]; n != 0; n = b->next[n]) {
      if (b->var[n] == v && b->lo[n] == lo && b->hi[n] == hi)
         return n;
   }

   if (b->nn >= b->max) {
      b->over = true;
      return BDD_FALSE;
   }

   if (b->nn == b->cap)
      grow_nodes(b);

   if (b->nn > 2 * b->hsize) {
      rehash(b, b->hsize * 2);
      h = hash3(v, lo, hi) & (b->hsize - 1);
   }

   if (b->nn > 4 * b->csize && b->csize < (1u << 14))
      comp_size(b, b->csize * 2);

   const unsigned n = b->nn++;
   b->var[n] = v;
   b->lo[n] = lo;
   b->hi[n] = hi;
   b->notc[n] = 0;
   b->next[n] = b->heads[h];
   b->heads[h] = n;
   return n;
}

bnode_t bdd_var(bdd_t *b, unsigned i)
{
   assert(i < b->nvars);
   return mk(b, i, BDD_FALSE, BDD_TRUE);
}

bnode_t bdd_not(bdd_t *b, bnode_t f)
{
   if (f < 2)
      return 1 - f;
   else if (b->notc[f] != 0)
      return b->notc[f];
   else if (b->over)
      return BDD_FALSE;

   const bnode_t l = bdd_not(b, b->lo[f]);
   const bnode_t h = bdd_not(b, b->hi[f]);
   const bnode_t r = mk(b, b->var[f], l, h);
   if (!b->over) {
      b->notc[f] = r;
      b->notc[r] = f;
   }
   return r;
}

static bnode_t apply(bdd_t *b, bop_t op, bnode_t f, bnode_t g)
{
   if (op == OP_AND) {
      if (f == BDD_FALSE || g == BDD_FALSE)
         return BDD_FALSE;
      else if (f == BDD_TRUE || f == g)
         return g;
      else if (g == BDD_TRUE)
         return f;
   }
   else {
      if (f == BDD_TRUE || g == BDD_TRUE)
         return BDD_TRUE;
      else if (f == BDD_FALSE || f == g)
         return g;
      else if (g == BDD_FALSE)
         return f;
   }

   if (b->over)
      return BDD_FALSE;

   if (f > g) {
      const bnode_t t = f;
      f = g;
      g = t;
   }

   const unsigned slot = hash3(f, g, op) & (b->csize - 1);
   centry_t *ce = &(b->comp[slot]);
   if (ce->a == (f | ((uint32_t)op << 31)) && ce->b == g)
      return ce->r;

   const uint32_t vf = b->var[f], vg = b->var[g];
   const uint32_t v = vf < vg ? vf : vg;
   const bnode_t f0 = vf == v ? b->lo[f] : f, f1 = vf == v ? b->hi[f] : f;
   const bnode_t g0 = vg == v ? b->lo[g] : g, g1 = vg == v ? b->hi[g] : g;

   const bnode_t l = apply(b, op, f0, g0);
   const bnode_t h = apply(b, op, f1, g1);
   const bnode_t r = mk(b, v, l, h);

   if (!b->over) {
      // The table may have been resized
      ce = &(b->comp[hash3(f, g, op) & (b->csize - 1)]);
      ce->a = f | ((uint32_t)op << 31);
      ce->b = g;
      ce->r = r;
   }
   return r;
}

bnode_t bdd_and(bdd_t *b, bnode_t f, bnode_t g)
{
   return apply(b, OP_AND, f, g);
}

bnode_t bdd_or(bdd_t *b, bnode_t f, bnode_t g)
{
   return apply(b, OP_OR, f, g);
}

static bnode_t exists_rec(bdd_t *b, bnode_t f, const uint8_t *quant,
                          uint32_t *memo)
{
   if (f < 2)
      return f;
   else if (memo[f] != NO_NODE)
      return memo[f];

   const bnode_t l = exists_rec(b, b->lo[f], quant, memo);
   const bnode_t h = exists_rec(b, b->hi[f], quant, memo);
   const bnode_t r = quant[b->var[f]] ? apply(b, OP_OR, l, h)
      : mk(b, b->var[f], l, h);
   memo[f] = r;
   return r;
}

bnode_t bdd_exists_set(bdd_t *b, bnode_t f, const uint8_t *quant)
{
   if (f < 2 || b->over)
      return f;

   uint32_t *memo = xmalloc_array(b->nn, sizeof(uint32_t));
   memset(memo, 0xff, b->nn * sizeof(uint32_t));
   const bnode_t r = exists_rec(b, f, quant, memo);
   free(memo);
   return b->over ? BDD_FALSE : r;
}

static bnode_t cofactor_rec(bdd_t *b, bnode_t f, const uint8_t *fix,
                            const uint8_t *val, uint32_t *memo)
{
   if (f < 2)
      return f;
   else if (memo[f] != NO_NODE)
      return memo[f];

   bnode_t r;
   if (fix[b->var[f]])
      r = cofactor_rec(b, val[b->var[f]] ? b->hi[f] : b->lo[f], fix, val,
                       memo);
   else {
      const bnode_t l = cofactor_rec(b, b->lo[f], fix, val, memo);
      const bnode_t h = cofactor_rec(b, b->hi[f], fix, val, memo);
      r = mk(b, b->var[f], l, h);
   }
   memo[f] = r;
   return r;
}

bnode_t bdd_cofactor(bdd_t *b, bnode_t f, const uint8_t *fix,
                     const uint8_t *val)
{
   if (f < 2 || b->over)
      return f;

   uint32_t *memo = xmalloc_array(b->nn, sizeof(uint32_t));
   memset(memo, 0xff, b->nn * sizeof(uint32_t));
   const bnode_t r = cofactor_rec(b, f, fix, val, memo);
   free(memo);
   return b->over ? BDD_FALSE : r;
}

typedef struct {
   uint32_t k;
   __int128 rem;
   bnode_t  node;
} cmp_memo_t;

typedef struct {
   bdd_t          *b;
   unsigned        w, n;
   unsigned        pos[64];           // Variable of each bit, by position
   unsigned        sig[64];           // Significance of the bit
   __int128        w0[64], w1[64];    // Value of the bit when 0 and when 1
   __int128        rest[65];          // Most the bits from k on can add
   uint64_t        mask[65];          // Bits of the numbers they can add
   bool            ge;
   cmp_memo_t     *memo;
   unsigned        nmemo, cmemo;
} cmp_t;

static bnode_t cmp_rec(cmp_t *C, unsigned k, __int128 rem)
{
   // ge: the number is >= c, that is the bits left add at least rem
   if (C->ge) {
      if (rem <= 0)
         return BDD_TRUE;
      else if (rem > C->rest[k])
         return BDD_FALSE;
   }
   else {
      if (rem < 0)
         return BDD_FALSE;
      else if (rem >= C->rest[k])
         return BDD_TRUE;
   }

   if (C->b->over)
      return BDD_FALSE;

   // The bits left add the numbers that are a part of mask: two values of
   // rem that compare the same way with all of them are the same, so take
   // the one that can be added.  This keeps the number of states at a
   // position small in any order of the bits
   const uint64_t M = C->mask[k];
   uint64_t t = (uint64_t)rem;
   if (C->ge) {
      // The least number of M that is >= t
      for (uint64_t x; (x = t & ~M) != 0; ) {
         const unsigned h = 63 - __builtin_clzll(x);
         t = ((t >> h) + 1) << h;
      }
   }
   else {
      // The greatest number of M that is <= t
      const uint64_t x = t & ~M;
      if (x != 0) {
         const unsigned h = 63 - __builtin_clzll(x);
         t = ((h == 63 ? 0 : (t >> (h + 1)) << (h + 1))) | (M & ((UINT64_C(1) << h) - 1));
      }
   }
   rem = t;

   // The number of different values of rem at a position is small
   for (unsigned i = 0; i < C->nmemo; i++) {
      const cmp_memo_t *m = &C->memo[i];
      if (m->k == k && m->rem == rem)
         return m->node;
   }

   const bnode_t lo = cmp_rec(C, k + 1, rem - C->w0[k]);
   const bnode_t hi = cmp_rec(C, k + 1, rem - C->w1[k]);
   const bnode_t r = mk(C->b, C->pos[k], lo, hi);

   if (C->nmemo == C->cmemo) {
      C->cmemo = C->cmemo ? C->cmemo * 2 : 64;
      C->memo = xrealloc_array(C->memo, C->cmemo, sizeof(cmp_memo_t));
   }
   C->memo[C->nmemo++] = (cmp_memo_t){ k, rem, r };
   return r;
}

bnode_t bdd_cmp_const(bdd_t *b, const unsigned *pos, unsigned w,
                      bool inv_msb, uint64_t c, bool ge)
{
   assert(w >= 1 && w <= 64);

   // The bits by position in the order, the first at the top
   unsigned sig[64];
   for (unsigned i = 0; i < w; i++)
      sig[i] = i;
   for (unsigned i = 1; i < w; i++) {
      const unsigned x = sig[i];
      unsigned j = i;
      for (; j > 0 && pos[sig[j - 1]] > pos[x]; j--)
         sig[j] = sig[j - 1];
      sig[j] = x;
   }

   cmp_t C = { .b = b, .w = w, .ge = ge };
   for (unsigned k = 0; k < w; k++) {
      const unsigned i = sig[k];
      const __int128 v = (__int128)1 << i;
      C.pos[k] = pos[i];
      C.w0[k] = (inv_msb && i == w - 1) ? v : 0;
      C.w1[k] = (inv_msb && i == w - 1) ? 0 : v;
   }

   C.rest[w] = 0;
   C.mask[w] = 0;
   for (unsigned k = w; k-- > 0; ) {
      C.rest[k] = C.rest[k + 1] + (C.w0[k] > C.w1[k] ? C.w0[k] : C.w1[k]);
      C.mask[k] = C.mask[k + 1] | ((uint64_t)1 << sig[k]);
   }

   const bnode_t r = cmp_rec(&C, 0, (__int128)c);
   free(C.memo);
   return r;
}

////////////////////////////////////////////////////////////////////////////////
// Counting

bnum_t bnum_zero(unsigned nl)
{
   return (bnum_t){ .nl = nl };
}

unsigned bnum_bitlen(const bnum_t *a)
{
   for (unsigned i = a->nl; i > 0; i--) {
      if (a->w[i - 1] != 0)
         return (i - 1) * 64 + 64 - __builtin_clzll(a->w[i - 1]);
   }
   return 0;
}

bool bnum_is_zero(const bnum_t *a)
{
   return bnum_bitlen(a) == 0;
}

int bnum_cmp(const bnum_t *a, const bnum_t *b)
{
   for (unsigned i = a->nl; i > 0; i--) {
      if (a->w[i - 1] != b->w[i - 1])
         return a->w[i - 1] < b->w[i - 1] ? -1 : 1;
   }
   return 0;
}

void bnum_sub1(bnum_t *a)
{
   for (unsigned i = 0; i < a->nl; i++) {
      if (a->w[i]-- != 0)
         break;
   }
}

unsigned __int128 bnum_u128(const bnum_t *a)
{
   assert(bnum_bitlen(a) <= 128);
   unsigned __int128 x = a->w[0];
   if (a->nl > 1)
      x |= (unsigned __int128)a->w[1] << 64;
   return x;
}

static void num_add(uint64_t *a, const uint64_t *b, unsigned nl)
{
   unsigned carry = 0;
   for (unsigned i = 0; i < nl; i++) {
      const uint64_t s = a[i] + b[i];
      const unsigned c1 = s < a[i];
      const uint64_t t = s + carry;
      carry = c1 | (t < s);
      a[i] = t;
   }
}

static void num_sub(uint64_t *a, const uint64_t *b, unsigned nl)
{
   unsigned borrow = 0;
   for (unsigned i = 0; i < nl; i++) {
      const uint64_t d = a[i] - b[i];
      const unsigned b1 = a[i] < b[i];
      const uint64_t t = d - borrow;
      borrow = b1 | (d < borrow);
      a[i] = t;
   }
}

static int num_cmp(const uint64_t *a, const uint64_t *b, unsigned nl)
{
   for (unsigned i = nl; i > 0; i--) {
      if (a[i - 1] != b[i - 1])
         return a[i - 1] < b[i - 1] ? -1 : 1;
   }
   return 0;
}

// out = a << k, a and out may be the same
static void num_shl(uint64_t *out, const uint64_t *a, unsigned k, unsigned nl)
{
   const unsigned limbs = k / 64, bits = k % 64;
   for (unsigned i = nl; i-- > 0; ) {
      uint64_t x = 0;
      if (i >= limbs) {
         x = a[i - limbs] << bits;
         if (bits != 0 && i > limbs)
            x |= a[i - limbs - 1] >> (64 - bits);
      }
      out[i] = x;
   }
}

static void num_pow2(uint64_t *out, unsigned k, unsigned nl)
{
   for (unsigned i = 0; i < nl; i++)
      out[i] = 0;
   assert(k / 64 < nl);
   out[k / 64] = UINT64_C(1) << (k % 64);
}

static void count_rec(bdd_t *b, bnode_t f, unsigned nv)
{
   const unsigned nl = b->nl;
   if (b->cdone[f])
      return;

   const unsigned i = b->vidx[b->var[f]];
   assert(i < nv);
   uint64_t *total = b->cnt + (size_t)f * nl;
   for (unsigned k = 0; k < nl; k++)
      total[k] = 0;

   for (unsigned k = 0; k < 2; k++) {
      const bnode_t ch = k ? b->hi[f] : b->lo[f];
      uint64_t part[BNUM_MAX];
      if (ch == BDD_TRUE)
         num_pow2(part, nv - i - 1, nl);
      else if (ch == BDD_FALSE)
         continue;
      else {
         count_rec(b, ch, nv);
         num_shl(part, b->cnt + (size_t)ch * nl, b->vidx[b->var[ch]] - i - 1,
                 nl);
      }
      num_add(total, part, nl);
   }
   b->cdone[f] = 1;
}

bool bdd_count(bdd_t *b, bnode_t f, const unsigned *vars, unsigned nv,
               bnum_t *total)
{
   if (nv > 256 || b->over)
      return false;

   const unsigned nl = nv <= 126 ? 2 : BNUM_MAX;
   *total = bnum_zero(nl);

   if (f < 2) {
      if (f)
         num_pow2(total->w, nv, nl);
      return true;
   }

   if (b->nvidx < b->nvars) {
      b->vidx = xrealloc_array(b->vidx, b->nvars, sizeof(unsigned));
      b->nvidx = b->nvars;
   }
   for (unsigned i = 0; i < b->nvars; i++)
      b->vidx[i] = UINT_MAX;
   for (unsigned i = 0; i < nv; i++)
      b->vidx[vars[i]] = i;

   if (b->nl != nl)
      b->ncnt = 0;
   b->nl = nl;
   if (b->ncnt < b->nn) {
      b->cnt = xrealloc_array(b->cnt, (size_t)b->nn * nl, sizeof(uint64_t));
      b->cdone = xrealloc_array(b->cdone, b->nn, 1);
      b->ncnt = b->nn;
   }
   memset(b->cdone, 0, b->nn);
   if (b->nn > b->cused)
      b->cused = b->nn;

   count_rec(b, f, nv);
   num_shl(total->w, b->cnt + (size_t)f * nl, b->vidx[b->var[f]], nl);
   return true;
}

void bdd_unrank(bdd_t *b, bnode_t f, const unsigned *vars, unsigned nv,
                const bnum_t *rn, uint8_t *bits)
{
   const unsigned nl = rn->nl;
   assert(nl == b->nl || f < 2);

   uint64_t r[BNUM_MAX], half[BNUM_MAX];
   memcpy(r, rn->w, sizeof(r));

   unsigned p = 0;
   bnode_t cur = f;

   while (p < nv) {
      if (cur == BDD_TRUE) {
         // The rest are free: the bits of r from the top
         const unsigned t = nv - p;
         for (unsigned i = 0; i < t; i++) {
            const unsigned k = t - 1 - i;
            bits[vars[p + i]] = (r[k / 64] >> (k % 64)) & 1;
         }
         break;
      }

      assert(cur != BDD_FALSE);

      const unsigned v = b->vidx[b->var[cur]];
      const uint64_t *c = b->cnt + (size_t)cur * nl;
      while (p < v) {
         // A variable the function does not depend on: half of the
         // assignments each way
         num_shl(half, c, v - p - 1, nl);
         if (num_cmp(r, half, nl) >= 0) {
            num_sub(r, half, nl);
            bits[vars[p]] = 1;
         }
         else
            bits[vars[p]] = 0;
         p++;
      }

      const bnode_t l = b->lo[cur], h = b->hi[cur];
      if (l == BDD_TRUE)
         num_pow2(half, nv - p - 1, nl);
      else if (l == BDD_FALSE)
         memset(half, 0, sizeof(half));
      else
         num_shl(half, b->cnt + (size_t)l * nl, b->vidx[b->var[l]] - p - 1,
                 nl);

      if (num_cmp(r, half, nl) < 0) {
         bits[vars[p]] = 0;
         cur = l;
      }
      else {
         num_sub(r, half, nl);
         bits[vars[p]] = 1;
         cur = h;
      }
      p++;
   }
}
