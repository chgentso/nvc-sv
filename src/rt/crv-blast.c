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
#include "rt/crv-blast.h"
#include "rt/crv-priv.h"

#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#define MAXV 72      // Inline capacity; wider vectors spill to the heap

typedef struct {
   unsigned n, cap;
   blit_t  *b;
   blit_t   inl[MAXV];
} bv_t;

typedef struct {
   int32_t  off;
   uint16_t w;
   bool     sgn, done;
   blit_t   fail;
} enode_t;

typedef struct {
   int32_t off;               // In the pool, -1 if not made yet
} invec_t;

struct _blast {
   const crv_t *c;
   blast_shape_t shape;

   // The graph: gate g has fanins l0[g] and l1[g].  Gate 0 is the constant
   // false and an input has l0 == INPUT and l1 the index of the input.
   blit_t   *l0, *l1;
   unsigned  ng, cg;
   uint32_t *tab;             // Hash of the fanins: gate + 1
   unsigned  tmask;
   unsigned *in_gate;         // Gate of each input
   int      *in_var;
   uint16_t *in_bit;
   uint8_t  *in_sig;          // Place in the first operand that has the bit
   unsigned  nin, cin;

   enode_t  *memo;
   unsigned  nmemo;
   blit_t   *pool;
   unsigned  npool, cpool;
   invec_t  *invec;

   blast_op_t *ops;
   unsigned   nops, cops;
   unsigned   nonlinear;

   uint64_t *sim;
   unsigned  nsim;

   unsigned  limit;           // Most gates to make, 0 for no limit
   bool      over;
};

#define INPUT UINT32_MAX

static inline blit_t lit_not(blit_t a) { return a ^ 1; }
static inline bool is_const(blit_t a) { return a < 2; }

blit_t blast_not(blit_t a)
{
   return a ^ 1;
}

////////////////////////////////////////////////////////////////////////////////
// And-inverter graph

static void tab_rehash(blast_t *b, unsigned size)
{
   free(b->tab);
   b->tab = xcalloc_array(size, sizeof(uint32_t));
   b->tmask = size - 1;

   for (unsigned g = 1; g < b->ng; g++) {
      if (b->l0[g] == INPUT)
         continue;

      unsigned h = (b->l0[g] * 0x9e3779b1u ^ b->l1[g] * 0x85ebca6bu);
      h ^= h >> 15;
      h *= 0xc2b2ae35u;
      h ^= h >> 13;
      for (h &= b->tmask; b->tab[h] != 0; h = (h + 1) & b->tmask)
         ;
      b->tab[h] = g + 1;
   }
}

static unsigned new_gate(blast_t *b, blit_t l0, blit_t l1)
{
   if (b->ng == b->cg) {
      b->cg = b->cg ? b->cg * 2 : 1024;
      b->l0 = xrealloc_array(b->l0, b->cg, sizeof(blit_t));
      b->l1 = xrealloc_array(b->l1, b->cg, sizeof(blit_t));
   }

   b->l0[b->ng] = l0;
   b->l1[b->ng] = l1;
   return b->ng++;
}

blit_t blast_and(blast_t *b, blit_t x, blit_t y)
{
   if (x > y) {
      const blit_t t = x;
      x = y;
      y = t;
   }

   if (x == BLIT_FALSE)
      return BLIT_FALSE;
   else if (x == BLIT_TRUE || x == y)
      return y;
   else if (x == (y ^ 1))
      return BLIT_FALSE;

   unsigned h = (x * 0x9e3779b1u ^ y * 0x85ebca6bu);
   h ^= h >> 15;
   h *= 0xc2b2ae35u;
   h ^= h >> 13;
   for (h &= b->tmask; b->tab[h] != 0; h = (h + 1) & b->tmask) {
      const unsigned g = b->tab[h] - 1;
      if (b->l0[g] == x && b->l1[g] == y)
         return g << 1;
   }

   if (b->limit != 0 && b->ng >= b->limit) {
      // Too big: everything after this is meaningless and discarded
      b->over = true;
      return BLIT_FALSE;
   }

   const unsigned g = new_gate(b, x, y);
   b->tab[h] = g + 1;

   if (b->ng * 2 > b->tmask + 1)
      tab_rehash(b, (b->tmask + 1) * 2);

   return g << 1;
}

blit_t blast_or(blast_t *b, blit_t x, blit_t y)
{
   return lit_not(blast_and(b, lit_not(x), lit_not(y)));
}

static blit_t g_xor(blast_t *b, blit_t x, blit_t y)
{
   if (x == y)
      return BLIT_FALSE;
   else if (x == (y ^ 1))
      return BLIT_TRUE;
   else if (x == BLIT_FALSE)
      return y;
   else if (x == BLIT_TRUE)
      return lit_not(y);
   else if (y == BLIT_FALSE)
      return x;
   else if (y == BLIT_TRUE)
      return lit_not(x);

   return blast_or(b, blast_and(b, x, lit_not(y)), blast_and(b, lit_not(x), y));
}

static blit_t g_mux(blast_t *b, blit_t s, blit_t t, blit_t f)
{
   if (t == f)
      return t;
   else if (s == BLIT_TRUE)
      return t;
   else if (s == BLIT_FALSE)
      return f;
   else if (t == BLIT_TRUE)
      return blast_or(b, s, f);
   else if (t == BLIT_FALSE)
      return blast_and(b, lit_not(s), f);
   else if (f == BLIT_TRUE)
      return blast_or(b, lit_not(s), t);
   else if (f == BLIT_FALSE)
      return blast_and(b, s, t);

   return blast_or(b, blast_and(b, s, t), blast_and(b, lit_not(s), f));
}

// Majority of three
static blit_t g_maj(blast_t *b, blit_t x, blit_t y, blit_t z)
{
   return blast_or(b, blast_and(b, x, y),
                   blast_and(b, z, blast_or(b, x, y)));
}

static blit_t new_input(blast_t *b, int var, unsigned bit)
{
   if (b->nin == b->cin) {
      b->cin = b->cin ? b->cin * 2 : 64;
      b->in_gate = xrealloc_array(b->in_gate, b->cin, sizeof(unsigned));
      b->in_var = xrealloc_array(b->in_var, b->cin, sizeof(int));
      b->in_bit = xrealloc_array(b->in_bit, b->cin, sizeof(uint16_t));
      b->in_sig = xrealloc_array(b->in_sig, b->cin, sizeof(uint8_t));
   }

   const unsigned g = new_gate(b, INPUT, b->nin);
   b->in_gate[b->nin] = g;
   b->in_var[b->nin] = var;
   b->in_bit[b->nin] = bit;
   b->in_sig[b->nin] = 255;
   b->nin++;
   return g << 1;
}

static blit_t constant(bool v)
{
   return v ? BLIT_TRUE : BLIT_FALSE;
}

////////////////////////////////////////////////////////////////////////////////
// Vectors

static inline void bv_init(bv_t *v)
{
   v->n = 0;
   v->cap = MAXV;
   v->b = v->inl;
}

static inline void bv_free(bv_t *v)
{
   if (v->b != v->inl)
      free(v->b);
}

static void bv_reserve(bv_t *v, unsigned need)
{
   if (need <= v->cap)
      return;
   unsigned nc = v->cap * 2;
   while (nc < need)
      nc *= 2;
   if (v->b == v->inl) {
      blit_t *p = xmalloc_array(nc, sizeof(blit_t));
      if (v->n > 0)
         memcpy(p, v->inl, v->n * sizeof(blit_t));
      v->b = p;
   }
   else
      v->b = xrealloc_array(v->b, nc, sizeof(blit_t));
   v->cap = nc;
}

static void bv_copy(bv_t *dst, const bv_t *src)
{
   bv_reserve(dst, src->n);
   dst->n = src->n;
   if (src->n > 0)
      memcpy(dst->b, src->b, src->n * sizeof(blit_t));
}

static void bv_const(bv_t *v, uint64_t val, unsigned w)
{
   bv_reserve(v, w);
   v->n = w;
   for (unsigned i = 0; i < w; i++)
      v->b[i] = constant(i < 64 && ((val >> i) & 1));
}

static void bv_const_wide(bv_t *v, const uint64_t *words, unsigned nwords,
                          unsigned w)
{
   bv_reserve(v, w);
   v->n = w;
   for (unsigned i = 0; i < w; i++) {
      const unsigned wi = i / 64;
      v->b[i] = constant(wi < nwords && ((words[wi] >> (i % 64)) & 1));
   }
}

static void bv_const_i128(bv_t *v, i128 k, unsigned w)
{
   bv_reserve(v, w);
   v->n = w;
   const uint64_t lo = (uint64_t)k;
   const uint64_t hi = (uint64_t)((unsigned __int128)k >> 64);
   const blit_t sign = constant(k < 0);
   for (unsigned i = 0; i < w; i++) {
      if (i < 64)
         v->b[i] = constant((lo >> i) & 1);
      else if (i < 128)
         v->b[i] = constant((hi >> (i - 64)) & 1);
      else
         v->b[i] = sign;
   }
}

// Bit i of the value extended past the top
static inline blit_t bv_ext(const bv_t *v, unsigned i, bool sgn)
{
   if (i < v->n)
      return v->b[i];
   else if (sgn && v->n > 0)
      return v->b[v->n - 1];
   else
      return BLIT_FALSE;
}

static void bv_resize(bv_t *v, unsigned w, bool sgn)
{
   bv_reserve(v, w);
   for (unsigned i = v->n; i < w; i++)
      v->b[i] = bv_ext(v, i, sgn);
   v->n = w;
}


// Bits that are not constant: position of the highest one that is
static unsigned bv_vbits(const bv_t *v)
{
   for (unsigned i = v->n; i > 0; i--) {
      if (!is_const(v->b[i - 1]))
         return i;
   }
   return 0;
}

static blit_t bv_any(blast_t *b, const blit_t *x, unsigned n)
{
   // Balanced so that a wide reduction stays shallow
   if (n == 0)
      return BLIT_FALSE;
   else if (n == 1)
      return x[0];

   const unsigned h = n / 2;
   return blast_or(b, bv_any(b, x, h), bv_any(b, x + h, n - h));
}

static blit_t bv_all(blast_t *b, const blit_t *x, unsigned n)
{
   if (n == 0)
      return BLIT_TRUE;
   else if (n == 1)
      return x[0];

   const unsigned h = n / 2;
   return blast_and(b, bv_all(b, x, h), bv_all(b, x + h, n - h));
}

static blit_t bv_parity(blast_t *b, const blit_t *x, unsigned n)
{
   if (n == 0)
      return BLIT_FALSE;
   else if (n == 1)
      return x[0];

   const unsigned h = n / 2;
   return g_xor(b, bv_parity(b, x, h), bv_parity(b, x + h, n - h));
}

// out = x + y + cin over w bits, carry out in *cout
static void bv_add(blast_t *b, const bv_t *x, const bv_t *y, blit_t cin,
                   unsigned w, bv_t *out, blit_t *cout)
{
   bv_reserve(out, w);
   blit_t carry = cin;
   for (unsigned i = 0; i < w; i++) {
      const blit_t xi = x->b[i], yi = y->b[i];
      const blit_t p = g_xor(b, xi, yi);
      out->b[i] = g_xor(b, p, carry);
      carry = g_mux(b, p, carry, xi);
   }
   out->n = w;
   if (cout != NULL)
      *cout = carry;
}

static void bv_not(const bv_t *x, bv_t *out)
{
   bv_reserve(out, x->n);
   for (unsigned i = 0; i < x->n; i++)
      out->b[i] = lit_not(x->b[i]);
   out->n = x->n;
}

// x - y over w bits: the carry out is set when x >= y (unsigned)
static void bv_sub(blast_t *b, const bv_t *x, const bv_t *y, unsigned w,
                   bv_t *out, blit_t *cout)
{
   bv_t ny;
   bv_init(&ny);
   bv_not(y, &ny);
   bv_add(b, x, &ny, BLIT_TRUE, w, out, cout);
   bv_free(&ny);
}

static void bv_neg(blast_t *b, const bv_t *x, unsigned w, bv_t *out)
{
   bv_reserve(out, w);
   blit_t seen = BLIT_FALSE;
   for (unsigned i = 0; i < w; i++) {
      out->b[i] = g_xor(b, x->b[i], seen);
      seen = blast_or(b, seen, x->b[i]);
   }
   out->n = w;
}

static void bv_mux(blast_t *b, blit_t s, const bv_t *t, const bv_t *f,
                   bv_t *out)
{
   assert(t->n == f->n);
   bv_reserve(out, t->n);
   for (unsigned i = 0; i < t->n; i++)
      out->b[i] = g_mux(b, s, t->b[i], f->b[i]);
   out->n = t->n;
}

static blit_t bv_eq(blast_t *b, const bv_t *x, const bv_t *y)
{
   assert(x->n == y->n);
   blit_t eq = BLIT_TRUE;
   for (unsigned i = 0; i < x->n; i++)
      eq = blast_and(b, eq, lit_not(g_xor(b, x->b[i], y->b[i])));
   return eq;
}

static blit_t bv_ult(blast_t *b, const bv_t *x, const bv_t *y)
{
   assert(x->n == y->n);
   blit_t lt = BLIT_FALSE;
   for (unsigned i = 0; i < x->n; i++)
      lt = g_maj(b, lit_not(x->b[i]), y->b[i], lt);
   return lt;
}

static blit_t bv_slt(blast_t *b, const bv_t *x, const bv_t *y)
{
   assert(x->n == y->n && x->n > 0);
   bv_t fx, fy;
   bv_init(&fx);
   bv_init(&fy);
   bv_copy(&fx, x);
   bv_copy(&fy, y);
   fx.b[x->n - 1] = lit_not(fx.b[x->n - 1]);
   fy.b[y->n - 1] = lit_not(fy.b[y->n - 1]);
   const blit_t r = bv_ult(b, &fx, &fy);
   bv_free(&fx);
   bv_free(&fy);
   return r;
}

// Compare the mathematical values of two numbers
static void bv_cmp(blast_t *b, const bv_t *x, bool xs, const bv_t *y, bool ys,
                   blit_t *eq, blit_t *lt)
{
   const unsigned w = MAX(x->n, y->n) + (xs != ys ? 1 : 0);
   bv_t ex, ey;
   bv_init(&ex);
   bv_init(&ey);
   bv_copy(&ex, x);
   bv_copy(&ey, y);
   bv_resize(&ex, w, xs);
   bv_resize(&ey, w, ys);
   *eq = bv_eq(b, &ex, &ey);
   *lt = (xs || ys) ? bv_slt(b, &ex, &ey) : bv_ult(b, &ex, &ey);
   bv_free(&ex);
   bv_free(&ey);
}

// Compare with a constant, which may be outside the range of the type
static blit_t bv_le_const(blast_t *b, const bv_t *x, bool xs, i128 k)
{
   const unsigned w = x->n;
   if (w <= 64) {
      const i128 lo = xs ? -((i128)1 << (w - 1)) : 0;
      const i128 hi = xs ? ((i128)1 << (w - 1)) - 1 : ((i128)1 << w) - 1;
      if (k >= hi)
         return BLIT_TRUE;
      else if (k < lo)
         return BLIT_FALSE;
   }
   else if (!xs && k < 0)
      return BLIT_FALSE;

   bv_t kv;
   bv_init(&kv);
   bv_const_i128(&kv, k, w);
   const blit_t r = lit_not(xs ? bv_slt(b, &kv, x) : bv_ult(b, &kv, x));
   bv_free(&kv);
   return r;
}

static blit_t bv_ge_const(blast_t *b, const bv_t *x, bool xs, i128 k)
{
   const unsigned w = x->n;
   if (w <= 64) {
      const i128 lo = xs ? -((i128)1 << (w - 1)) : 0;
      const i128 hi = xs ? ((i128)1 << (w - 1)) - 1 : ((i128)1 << w) - 1;
      if (k <= lo)
         return BLIT_TRUE;
      else if (k > hi)
         return BLIT_FALSE;
   }
   else if (!xs && k <= 0)
      return BLIT_TRUE;

   bv_t kv;
   bv_init(&kv);
   bv_const_i128(&kv, k, w);
   const blit_t r = lit_not(xs ? bv_slt(b, x, &kv) : bv_ult(b, x, &kv));
   bv_free(&kv);
   return r;
}

// Array multiplier: the low w bits of x * y.  The operand with the most
// constant bits drives the rows.
static void bv_mul(blast_t *b, const bv_t *x, const bv_t *y, unsigned w,
                   bv_t *out)
{
   unsigned cx = 0, cy = 0;
   for (unsigned i = 0; i < w; i++) {
      cx += is_const(x->b[i]);
      cy += is_const(y->b[i]);
   }
   if (cx > cy) {
      const bv_t *t = x;
      x = y;
      y = t;
   }

   bv_t acc, row, sum, hi;
   bv_init(&acc);
   bv_init(&row);
   bv_init(&sum);
   bv_init(&hi);
   bv_const(&acc, 0, w);
   for (unsigned i = 0; i < w; i++) {
      if (y->b[i] == BLIT_FALSE)
         continue;

      const unsigned remw = w - i;
      bv_reserve(&row, remw);
      row.n = remw;
      bool zero = true;
      for (unsigned j = 0; j < remw; j++) {
         row.b[j] = blast_and(b, y->b[i], x->b[j]);
         zero &= row.b[j] == BLIT_FALSE;
      }
      if (zero)
         continue;

      bv_reserve(&hi, remw);
      hi.n = remw;
      memcpy(hi.b, acc.b + i, remw * sizeof(blit_t));
      bv_add(b, &hi, &row, BLIT_FALSE, remw, &sum, NULL);
      memcpy(acc.b + i, sum.b, remw * sizeof(blit_t));
   }
   bv_copy(out, &acc);
   bv_free(&acc);
   bv_free(&row);
   bv_free(&sum);
   bv_free(&hi);
}

// Restoring divider on unsigned values of w bits
static void bv_udiv(blast_t *b, const bv_t *n, const bv_t *d, unsigned w,
                    bv_t *q, bv_t *r)
{
   // The remainder register has one more bit than the operands
   bv_t rem, dd, diff;
   bv_init(&rem);
   bv_init(&dd);
   bv_init(&diff);
   bv_const(&rem, 0, w + 1);
   bv_reserve(&dd, w + 1);
   dd.n = w + 1;
   for (unsigned i = 0; i < w; i++)
      dd.b[i] = d->b[i];
   dd.b[w] = BLIT_FALSE;

   bv_reserve(q, w);
   q->n = w;
   for (int i = w - 1; i >= 0; i--) {
      // rem = (rem << 1) | n[i]
      for (int j = w; j > 0; j--)
         rem.b[j] = rem.b[j - 1];
      rem.b[0] = n->b[i];

      blit_t ge;
      bv_sub(b, &rem, &dd, w + 1, &diff, &ge);
      q->b[i] = ge;
      bv_mux(b, ge, &diff, &rem, &rem);
   }

   bv_reserve(r, w);
   r->n = w;
   for (unsigned i = 0; i < w; i++)
      r->b[i] = rem.b[i];
   bv_free(&rem);
   bv_free(&dd);
   bv_free(&diff);
}

// Division and modulo of the mathematical values, truncating towards zero.
// The operands may differ in width and signedness.  The result has w bits.
static void bv_divmod(blast_t *b, const bv_t *x, bool xs, const bv_t *y,
                      bool ys, bool want_mod, unsigned w, bv_t *out)
{
   const bool sg = xs || ys;
   const unsigned ww = MAX(x->n, y->n) + (sg ? 1 : 0);
   bv_t ex, ey, q, r;
   bv_init(&ex);
   bv_init(&ey);
   bv_init(&q);
   bv_init(&r);
   bv_copy(&ex, x);
   bv_copy(&ey, y);
   bv_resize(&ex, ww, xs);
   bv_resize(&ey, ww, ys);

   if (!sg) {
      bv_udiv(b, &ex, &ey, ww, &q, &r);
      bv_t *res = want_mod ? &r : &q;
      bv_resize(res, w, false);
      bv_copy(out, res);
      out->n = w;
      bv_free(&ex);
      bv_free(&ey);
      bv_free(&q);
      bv_free(&r);
      return;
   }

   const blit_t sx = ex.b[ww - 1], sy = ey.b[ww - 1];
   bv_t nx, ny, ax, ay, res, nres;
   bv_init(&nx);
   bv_init(&ny);
   bv_init(&ax);
   bv_init(&ay);
   bv_init(&res);
   bv_init(&nres);
   bv_neg(b, &ex, ww, &nx);
   bv_neg(b, &ey, ww, &ny);
   bv_mux(b, sx, &nx, &ex, &ax);
   bv_mux(b, sy, &ny, &ey, &ay);

   bv_udiv(b, &ax, &ay, ww, &q, &r);

   if (want_mod) {
      bv_neg(b, &r, ww, &nres);
      bv_mux(b, sx, &nres, &r, &res);
   }
   else {
      bv_neg(b, &q, ww, &nres);
      bv_mux(b, g_xor(b, sx, sy), &nres, &q, &res);
   }

   bv_resize(&res, w, true);
   bv_copy(out, &res);
   out->n = w;
   bv_free(&nx);
   bv_free(&ny);
   bv_free(&ax);
   bv_free(&ay);
   bv_free(&res);
   bv_free(&nres);
   bv_free(&ex);
   bv_free(&ey);
   bv_free(&q);
   bv_free(&r);
}

// Barrel shifter on the sw-bit value x: by the count held in the low bits
// of cnt (cnt->n bits, taken as unsigned).  kind: 0 left, 1 logical right,
// 2 arithmetic right.
static void bv_shift(blast_t *b, const bv_t *x, const bv_t *cnt, int kind,
                     unsigned w, bv_t *out)
{
   const unsigned sw = x->n;
   assert(sw >= w && sw > 0);
   unsigned stages = 0;
   while ((1u << stages) < sw)
      stages++;

   bv_t cur, sh;
   bv_init(&cur);
   bv_init(&sh);
   bv_copy(&cur, x);
   bv_reserve(&sh, sw);
   sh.n = sw;

   for (unsigned k = 0; k < stages; k++) {
      const blit_t sel = k < cnt->n ? cnt->b[k] : BLIT_FALSE;
      if (sel == BLIT_FALSE)
         continue;

      const unsigned by = 1u << k;
      for (unsigned i = 0; i < sw; i++) {
         if (kind == 0)
            sh.b[i] = i >= by ? cur.b[i - by] : BLIT_FALSE;
         else if (i + by < sw)
            sh.b[i] = cur.b[i + by];
         else
            sh.b[i] = kind == 2 ? x->b[sw - 1] : BLIT_FALSE;
      }
      for (unsigned i = 0; i < sw; i++)
         cur.b[i] = g_mux(b, sel, sh.b[i], cur.b[i]);
   }

   // A count of (1 << stages) or more shifts everything out
   blit_t big = BLIT_FALSE;
   for (unsigned k = stages; k < cnt->n; k++)
      big = blast_or(b, big, cnt->b[k]);

   const blit_t fill = kind == 2 ? x->b[sw - 1] : BLIT_FALSE;
   bv_reserve(out, w);
   out->n = w;
   for (unsigned i = 0; i < w; i++)
      out->b[i] = g_mux(b, big, fill, cur.b[i]);
   bv_free(&cur);
   bv_free(&sh);
}

// Number of set bits among n literals, as a vector of the width needed
static void bv_popcount(blast_t *b, const blit_t *x, unsigned n, bv_t *out)
{
   if (n == 1) {
      bv_reserve(out, 1);
      out->n = 1;
      out->b[0] = x[0];
      return;
   }

   const unsigned h = n / 2;
   bv_t l, r;
   bv_init(&l);
   bv_init(&r);
   bv_popcount(b, x, h, &l);
   bv_popcount(b, x + h, n - h, &r);

   const unsigned w = MAX(l.n, r.n) + 1;
   bv_resize(&l, w, false);
   bv_resize(&r, w, false);
   bv_add(b, &l, &r, BLIT_FALSE, w, out, NULL);

   // The top bit is only needed if it can be set
   while (out->n > 1 && out->b[out->n - 1] == BLIT_FALSE)
      out->n--;
   bv_free(&l);
   bv_free(&r);
}

////////////////////////////////////////////////////////////////////////////////
// Nodes

static const enode_t *get_node(blast_t *b, crv_node_t n);

static void memo_grow(blast_t *b, unsigned n)
{
   if (n < b->nmemo)
      return;

   const unsigned nn = MAX(n + 1, b->nmemo * 2);
   b->memo = xrealloc_array(b->memo, nn, sizeof(enode_t));
   memset(b->memo + b->nmemo, 0, (nn - b->nmemo) * sizeof(enode_t));
   b->nmemo = nn;
}

static void store(blast_t *b, crv_node_t n, const bv_t *v, blit_t fail)
{
   const node_t *nd = &(b->c->nodes[n]);
   const unsigned w = nd->width;

   if (b->npool + w > b->cpool) {
      b->cpool = MAX(b->cpool * 2, b->npool + w + 4096);
      b->pool = xrealloc_array(b->pool, b->cpool, sizeof(blit_t));
   }

   memo_grow(b, n);
   enode_t *e = &(b->memo[n]);
   e->off = b->npool;
   e->w = w;
   e->sgn = nd->sgn;
   e->fail = fail;
   e->done = true;

   for (unsigned i = 0; i < w; i++)
      b->pool[b->npool++] = i < v->n ? v->b[i] : BLIT_FALSE;
}

// The value of an encoded node as a vector of w bits, extended by the
// signedness of the node
static void fetch(blast_t *b, crv_node_t n, unsigned w, bv_t *out)
{
   const enode_t *e = get_node(b, n);
   bv_reserve(out, MAX(e->w, w));
   out->n = e->w;
   memcpy(out->b, b->pool + e->off, e->w * sizeof(blit_t));
   bv_resize(out, w, e->sgn);
}

static blit_t fail_of(blast_t *b, crv_node_t n)
{
   return get_node(b, n)->fail;
}

static blit_t truth(blast_t *b, crv_node_t n)
{
   const enode_t *e = get_node(b, n);
   return bv_any(b, b->pool + e->off, e->w);
}

// Bits of an input variable
static void input_vec(blast_t *b, int v, bv_t *out)
{
   const blast_shape_t *s = &(b->shape);
   assert(v >= 0 && (unsigned)v < s->nv);

   const unsigned w = s->width[v];
   if (b->invec[v].off < 0) {
      if (b->npool + w > b->cpool) {
         b->cpool = MAX(b->cpool * 2, b->npool + w + 4096);
         b->pool = xrealloc_array(b->pool, b->cpool, sizeof(blit_t));
      }

      b->invec[v].off = b->npool;
      for (unsigned i = 0; i < w; i++) {
         if (s->fixed != NULL && s->fixed[v]) {
            bool bit = false;
            if (w <= 64)
               bit = ((uint64_t)s->value[v] >> i) & 1;
            else if (s->wvalue != NULL && s->wvalue[v] != NULL)
               bit = (s->wvalue[v][i / 64] >> (i % 64)) & 1;
            b->pool[b->npool++] = constant(bit);
         }
         else
            b->pool[b->npool++] = new_input(b, v, i);
      }
   }

   bv_reserve(out, w);
   out->n = w;
   memcpy(out->b, b->pool + b->invec[v].off, w * sizeof(blit_t));
}

// Record where the inputs that are bits of an operand are in it: bits of
// two operands at the same place meet in the gates of the operation
static void note_sig(blast_t *b, const bv_t *x)
{
   for (unsigned k = 0; k < x->n; k++) {
      const unsigned g = x->b[k] >> 1;
      if (g != 0 && b->l0[g] == INPUT && b->in_sig[b->l1[g]] == 255)
         b->in_sig[b->l1[g]] = MIN(k, 254);
   }
}

// Operands that both have variable bits: a comparison with a constant says
// nothing about how two variables line up
static void note_sigs(blast_t *b, const bv_t *x, const bv_t *y)
{
   if (bv_vbits(x) > 0 && bv_vbits(y) > 0) {
      note_sig(b, x);
      note_sig(b, y);
   }
}

// Inputs that some bit of a vector depends on: a sign extension counts as
// the one bit it repeats
static unsigned bv_support(const blast_t *b, const bv_t *x)
{
   uint8_t *seen = xcalloc(b->ng);
   unsigned *stack = xmalloc_array(b->ng + 1, sizeof(unsigned));
   unsigned n = 0;

   for (unsigned i = 0; i < x->n; i++) {
      unsigned sp = 0;
      if (x->b[i] >> 1 != 0 && !seen[x->b[i] >> 1]) {
         seen[x->b[i] >> 1] = 1;
         stack[sp++] = x->b[i] >> 1;
      }
      while (sp > 0) {
         const unsigned g = stack[--sp];
         if (b->l0[g] == INPUT) {
            n++;
            continue;
         }
         const blit_t l[2] = { b->l0[g], b->l1[g] };
         for (int k = 0; k < 2; k++) {
            if (l[k] >> 1 != 0 && !seen[l[k] >> 1]) {
               seen[l[k] >> 1] = 1;
               stack[sp++] = l[k] >> 1;
            }
         }
      }
   }

   free(stack);
   free(seen);
   return n;
}

static void note_op(blast_t *b, crv_node_t n, const node_t *nd,
                    const bv_t *x, const bv_t *y)
{
   const unsigned vx = bv_vbits(x), vy = bv_vbits(y);
   if (vx == 0 || vy == 0)
      return;

   if (b->nops == b->cops) {
      b->cops = b->cops ? b->cops * 2 : 64;
      b->ops = xrealloc_array(b->ops, b->cops, sizeof(blast_op_t));
   }
   b->ops[b->nops++] = (blast_op_t){ n, nd->op, vx, vy };

   if (nd->op == CRV_MUL || nd->op == CRV_DIV || nd->op == CRV_MOD) {
      const unsigned sx = bv_support(b, x), sy = bv_support(b, y);
      b->nonlinear = MAX(b->nonlinear, MIN(MIN(sx, sy), MIN(vx, vy)));
   }
}

static void encode_bin(blast_t *b, crv_node_t n, const node_t *nd)
{
   const crv_binary_t op = nd->op;
   const unsigned w = nd->width;
   bv_t out;
   bv_init(&out);
   bv_reserve(&out, w);
   out.n = w;

   if (op == CRV_LOG_AND || op == CRV_LOG_OR || op == CRV_IMPLIES) {
      // A failed operand is unknown: unknown or 1 and unknown implies 1
      // are 1 and unknown and 0 is 0
      get_node(b, nd->a);
      get_node(b, nd->b);
      blit_t lt = truth(b, nd->a), rt = truth(b, nd->b);
      const blit_t lf = fail_of(b, nd->a), rf = fail_of(b, nd->b);
      if (op == CRV_IMPLIES)
         lt = lit_not(lt);

      blit_t val, fail;
      if (op == CRV_LOG_AND) {
         const blit_t z0 = blast_and(b, lit_not(lf), lit_not(lt));
         const blit_t z1 = blast_and(b, lit_not(rf), lit_not(rt));
         const blit_t ret0 = blast_or(b, z0, z1);
         fail = blast_and(b, lit_not(ret0), blast_or(b, lf, rf));
         val = blast_and(b, blast_and(b, lt, rt),
                         blast_and(b, lit_not(lf), lit_not(rf)));
      }
      else {
         const blit_t o0 = blast_and(b, lit_not(lf), lt);
         const blit_t o1 = blast_and(b, lit_not(rf), rt);
         const blit_t ret1 = blast_or(b, o0, o1);
         fail = blast_and(b, lit_not(ret1), blast_or(b, lf, rf));
         val = ret1;
      }

      out.b[0] = val;
      for (unsigned i = 1; i < w; i++)
         out.b[i] = BLIT_FALSE;
      store(b, n, &out, fail);
      bv_free(&out);
      return;
   }

   get_node(b, nd->a);
   get_node(b, nd->b);
   const node_t *an = &(b->c->nodes[nd->a]);
   const node_t *bn = &(b->c->nodes[nd->b]);
   blit_t fail = blast_or(b, fail_of(b, nd->a), fail_of(b, nd->b));

   switch (op) {
   case CRV_ADD:
   case CRV_SUB:
   case CRV_MUL:
   case CRV_AND:
   case CRV_OR:
   case CRV_XOR:
      {
         bv_t x, y;
         bv_init(&x);
         bv_init(&y);
         fetch(b, nd->a, w, &x);
         fetch(b, nd->b, w, &y);
         note_sigs(b, &x, &y);
         if (op == CRV_ADD)
            bv_add(b, &x, &y, BLIT_FALSE, w, &out, NULL);
         else if (op == CRV_SUB)
            bv_sub(b, &x, &y, w, &out, NULL);
         else if (op == CRV_MUL) {
            note_op(b, n, nd, &x, &y);
            bv_mul(b, &x, &y, w, &out);
         }
         else {
            for (unsigned i = 0; i < w; i++)
               out.b[i] = op == CRV_AND ? blast_and(b, x.b[i], y.b[i])
                  : op == CRV_OR ? blast_or(b, x.b[i], y.b[i])
                  : g_xor(b, x.b[i], y.b[i]);
         }
         if (op == CRV_ADD || op == CRV_SUB)
            note_op(b, n, nd, &x, &y);
         bv_free(&x);
         bv_free(&y);
      }
      break;

   case CRV_DIV:
   case CRV_MOD:
      {
         bv_t x, y;
         bv_init(&x);
         bv_init(&y);
         fetch(b, nd->a, an->width, &x);
         fetch(b, nd->b, bn->width, &y);
         note_sigs(b, &x, &y);
         note_op(b, n, nd, &x, &y);
         bv_divmod(b, &x, an->sgn, &y, bn->sgn, op == CRV_MOD, w, &out);
         fail = blast_or(b, fail, lit_not(bv_any(b, y.b, y.n)));
         bv_free(&x);
         bv_free(&y);
      }
      break;

   case CRV_SHL:
   case CRV_SHR:
   case CRV_ASR:
      {
         bv_t x, cnt;
         bv_init(&x);
         bv_init(&cnt);
         const unsigned sw = (w <= 64 && an->width <= 64) ? 64
            : MAX(w, an->width);
         fetch(b, nd->a, sw, &x);

         // The value of a node is its representation in sw bits
         if (op != CRV_SHL && !(op == CRV_ASR && nd->sgn)) {
            for (unsigned i = w; i < sw; i++)
               x.b[i] = BLIT_FALSE;
         }

         // The count is the raw bits of the operand
         const enode_t *be = get_node(b, nd->b);
         bv_reserve(&cnt, be->w);
         cnt.n = be->w;
         memcpy(cnt.b, b->pool + be->off, be->w * sizeof(blit_t));

         bv_shift(b, &x, &cnt, op == CRV_SHL ? 0
                  : op == CRV_ASR && nd->sgn ? 2 : 1, w, &out);
         bv_free(&x);
         bv_free(&cnt);
      }
      break;

   case CRV_EQ:
   case CRV_NE:
   case CRV_LT:
   case CRV_LE:
   case CRV_GT:
   case CRV_GE:
      {
         bv_t x, y;
         bv_init(&x);
         bv_init(&y);
         fetch(b, nd->a, an->width, &x);
         fetch(b, nd->b, bn->width, &y);
         note_sigs(b, &x, &y);
         note_op(b, n, nd, &x, &y);

         blit_t eq, lt;
         bv_cmp(b, &x, an->sgn, &y, bn->sgn, &eq, &lt);
         blit_t r;
         switch (op) {
         case CRV_EQ: r = eq; break;
         case CRV_NE: r = lit_not(eq); break;
         case CRV_LT: r = lt; break;
         case CRV_LE: r = blast_or(b, lt, eq); break;
         case CRV_GT: r = lit_not(blast_or(b, lt, eq)); break;
         default: r = lit_not(lt); break;
         }
         out.b[0] = r;
         for (unsigned i = 1; i < w; i++)
            out.b[i] = BLIT_FALSE;
         bv_free(&x);
         bv_free(&y);
      }
      break;

   default:
      should_not_reach_here();
   }

   store(b, n, &out, fail);
   bv_free(&out);
}

static void encode_un(blast_t *b, crv_node_t n, const node_t *nd)
{
   const unsigned w = nd->width;
   get_node(b, nd->a);
   const node_t *an = &(b->c->nodes[nd->a]);
   const blit_t fail = fail_of(b, nd->a);

   bv_t x, out;
   bv_init(&x);
   bv_init(&out);
   bv_reserve(&out, w);
   out.n = w;
   switch ((crv_unary_t)nd->op) {
   case CRV_NEG:
      fetch(b, nd->a, w, &x);
      bv_neg(b, &x, w, &out);
      break;
   case CRV_INV:
      fetch(b, nd->a, w, &x);
      bv_not(&x, &out);
      out.n = w;
      break;
   case CRV_LOG_NOT:
   case CRV_REDUCE_AND:
   case CRV_REDUCE_OR:
   case CRV_REDUCE_XOR:
      {
         const enode_t *e = get_node(b, nd->a);
         const blit_t *bits = b->pool + e->off;
         const unsigned wa = an->width;
         blit_t r;
         if (nd->op == CRV_LOG_NOT)
            r = lit_not(bv_any(b, bits, wa));
         else if (nd->op == CRV_REDUCE_AND)
            r = bv_all(b, bits, wa);
         else if (nd->op == CRV_REDUCE_OR)
            r = bv_any(b, bits, wa);
         else
            r = bv_parity(b, bits, wa);
         out.b[0] = r;
         for (unsigned i = 1; i < w; i++)
            out.b[i] = BLIT_FALSE;
      }
      break;
   case CRV_POPCNT:
      {
         const enode_t *e = get_node(b, nd->a);
         bv_popcount(b, b->pool + e->off, an->width, &out);
         bv_resize(&out, w, false);
         out.n = w;
      }
      break;
   default:
      should_not_reach_here();
   }

   store(b, n, &out, fail);
   bv_free(&x);
   bv_free(&out);
}

static void encode_inside(blast_t *b, crv_node_t n, const node_t *nd)
{
   const cset_t *cs = &(b->c->sets[nd->aux]);
   const node_t *an = &(b->c->nodes[nd->a]);

   get_node(b, nd->a);
   bv_t x;
   bv_init(&x);
   fetch(b, nd->a, an->width, &x);

   // The value is related to the bounds that are not constants: a group
   // of variables for the order of the stages after the blaster
   if (b->nops == b->cops) {
      b->cops = b->cops ? b->cops * 2 : 64;
      b->ops = xrealloc_array(b->ops, b->cops, sizeof(blast_op_t));
   }
   b->ops[b->nops++] = (blast_op_t){ n, BLAST_OP_INSIDE, 1, 1 };

   // The items are looked at in order and the search stops at a match, so
   // an undefined item only counts if nothing before it matched
   blit_t found = BLIT_FALSE, failed = fail_of(b, nd->a);
   for (unsigned i = 0; i < cs->n; i++) {
      const item_t it = cs->items[i];
      get_node(b, it.lo);
      const node_t *ln = &(b->c->nodes[it.lo]);
      bv_t lo;
      bv_init(&lo);
      fetch(b, it.lo, ln->width, &lo);
      note_sigs(b, &x, &lo);

      blit_t ifail = fail_of(b, it.lo), match;
      blit_t eq, lt;
      bv_cmp(b, &x, an->sgn, &lo, ln->sgn, &eq, &lt);
      bv_free(&lo);
      if (it.hi < 0)
         match = eq;
      else {
         get_node(b, it.hi);
         const node_t *hn = &(b->c->nodes[it.hi]);
         bv_t hi;
         bv_init(&hi);
         fetch(b, it.hi, hn->width, &hi);
         note_sigs(b, &x, &hi);
         ifail = blast_or(b, ifail, fail_of(b, it.hi));

         blit_t heq, hlt;
         bv_cmp(b, &x, an->sgn, &hi, hn->sgn, &heq, &hlt);
         bv_free(&hi);
         match = blast_and(b, lit_not(lt), blast_or(b, hlt, heq));
      }

      const blit_t alive = blast_and(b, lit_not(found), lit_not(failed));
      failed = blast_or(b, failed, blast_and(b, alive, ifail));
      found = blast_or(b, found,
                       blast_and(b, blast_and(b, alive, lit_not(ifail)),
                                 match));
   }
   bv_free(&x);

   bv_t out;
   bv_init(&out);
   bv_reserve(&out, nd->width);
   out.n = nd->width;
   out.b[0] = found;
   for (unsigned i = 1; i < out.n; i++)
      out.b[i] = BLIT_FALSE;
   store(b, n, &out, failed);
   bv_free(&out);
}

static void encode_dist(blast_t *b, crv_node_t n, const node_t *nd)
{
   cdist_t *cd = (cdist_t *)&(b->c->dists[nd->aux]);
   crv_dist_prepare(cd);

   const node_t *an = &(b->c->nodes[nd->a]);
   get_node(b, nd->a);
   bv_t x;
   bv_init(&x);
   fetch(b, nd->a, an->width, &x);

   blit_t in = BLIT_FALSE;
   for (unsigned i = 0; i < cd->nmr; i++) {
      const blit_t ge = bv_ge_const(b, &x, an->sgn, cd->mr[i].lo);
      const blit_t le = bv_le_const(b, &x, an->sgn, cd->mr[i].hi);
      in = blast_or(b, in, blast_and(b, ge, le));
   }
   bv_free(&x);

   bv_t out;
   bv_init(&out);
   bv_reserve(&out, nd->width);
   out.n = nd->width;
   out.b[0] = in;
   for (unsigned i = 1; i < out.n; i++)
      out.b[i] = BLIT_FALSE;
   store(b, n, &out, fail_of(b, nd->a));
   bv_free(&out);
}

static void encode_elem(blast_t *b, crv_node_t n, const node_t *nd)
{
   const blast_shape_t *s = &(b->shape);
   const unsigned w = nd->width;
   bv_t out;
   bv_init(&out);

   if (s->ebase[nd->aux] < 0) {
      // The elements are not variables of this problem
      bv_const(&out, 0, w);
      store(b, n, &out, BLIT_TRUE);
      bv_free(&out);
      return;
   }

   const node_t *an = &(b->c->nodes[nd->a]);
   get_node(b, nd->a);
   bv_t idx;
   bv_init(&idx);
   fetch(b, nd->a, an->width, &idx);

   const unsigned size = s->sizes[nd->aux];
   blit_t any = BLIT_FALSE;
   bv_const(&out, 0, w);
   for (unsigned k = 0; k < size; k++) {
      // Index equal to k: a negative index never is
      blit_t sel;
      if (an->sgn ? (an->width <= 64 && k >= ((uint64_t)1 << (an->width - 1)))
          : (an->width < 64 && k >= ((uint64_t)1 << an->width)))
         sel = BLIT_FALSE;
      else {
         bv_t kv;
         bv_init(&kv);
         bv_const(&kv, k, an->width);
         sel = bv_eq(b, &idx, &kv);
         bv_free(&kv);
      }

      if (sel == BLIT_FALSE)
         continue;

      any = blast_or(b, any, sel);

      bv_t ev;
      bv_init(&ev);
      input_vec(b, s->ebase[nd->aux] + k, &ev);
      bv_resize(&ev, w, s->sgn[s->ebase[nd->aux] + k]);
      for (unsigned i = 0; i < w; i++)
         out.b[i] = blast_or(b, out.b[i], blast_and(b, sel, ev.b[i]));
      bv_free(&ev);
   }

   store(b, n, &out, blast_or(b, fail_of(b, nd->a), lit_not(any)));
   bv_free(&idx);
   bv_free(&out);
}

static void encode_sum(blast_t *b, crv_node_t n, const node_t *nd)
{
   const blast_shape_t *s = &(b->shape);
   const unsigned w = nd->width;
   bv_t acc;
   bv_init(&acc);
   bv_const(&acc, 0, w);

   if (s->ebase[nd->aux] < 0) {
      store(b, n, &acc, BLIT_TRUE);
      bv_free(&acc);
      return;
   }

   bv_t ev, sum;
   bv_init(&ev);
   bv_init(&sum);
   for (unsigned k = 0; k < s->sizes[nd->aux]; k++) {
      const int v = s->ebase[nd->aux] + k;
      input_vec(b, v, &ev);
      bv_resize(&ev, w, s->sgn[v]);
      bv_add(b, &acc, &ev, BLIT_FALSE, w, &sum, NULL);
      bv_copy(&acc, &sum);
   }
   bv_free(&ev);
   bv_free(&sum);

   store(b, n, &acc, BLIT_FALSE);
   bv_free(&acc);
}

static const enode_t *get_node(blast_t *b, crv_node_t n)
{
   memo_grow(b, n);
   if (b->memo[n].done)
      return &(b->memo[n]);

   const crv_t *c = b->c;
   const node_t *nd = &(c->nodes[n]);
   const blast_shape_t *s = &(b->shape);
   const unsigned w = nd->width;
   bv_t out;
   bv_init(&out);

   switch ((nkind_t)nd->kind) {
   case N_CONST:
      if (w <= 64)
         bv_const(&out, (uint64_t)nd->val, w);
      else
         bv_const_wide(&out, c->wpool + nd->val, (w + 63) / 64, w);
      store(b, n, &out, BLIT_FALSE);
      break;
   case N_VAR:
      {
         input_vec(b, nd->aux, &out);
         bv_resize(&out, w, s->sgn[nd->aux]);
         store(b, n, &out, BLIT_FALSE);
      }
      break;
   case N_ASIZE:
      {
         const int v = s->nsc + nd->aux;
         input_vec(b, v, &out);
         bv_resize(&out, w, s->sgn[v]);
         store(b, n, &out, BLIT_FALSE);
      }
      break;
   case N_BIN:
      get_node(b, nd->a);
      get_node(b, nd->b);
      encode_bin(b, n, nd);
      break;
   case N_UN:
      get_node(b, nd->a);
      encode_un(b, n, nd);
      break;
   case N_CAST:
      {
         get_node(b, nd->a);
         fetch(b, nd->a, w, &out);
         store(b, n, &out, fail_of(b, nd->a));
      }
      break;
   case N_COND:
      {
         get_node(b, nd->a);
         get_node(b, nd->b);
         get_node(b, nd->c);
         const blit_t t = truth(b, nd->a);
         bv_t x, y;
         bv_init(&x);
         bv_init(&y);
         fetch(b, nd->b, w, &x);
         fetch(b, nd->c, w, &y);
         bv_mux(b, t, &x, &y, &out);
         bv_free(&x);
         bv_free(&y);
         const blit_t bf = fail_of(b, nd->b), cf = fail_of(b, nd->c);
         const blit_t fail = blast_or(b, fail_of(b, nd->a),
                                      g_mux(b, t, bf, cf));
         store(b, n, &out, fail);
      }
      break;
   case N_AELEM:
      get_node(b, nd->a);
      encode_elem(b, n, nd);
      break;
   case N_ASUM:
      encode_sum(b, n, nd);
      break;
   case N_INSIDE:
      encode_inside(b, n, nd);
      break;
   case N_DIST:
      get_node(b, nd->a);
      encode_dist(b, n, nd);
      break;
   default:
      bv_const(&out, 0, w);
      store(b, n, &out, BLIT_TRUE);
      break;
   }

   bv_free(&out);
   return &(b->memo[n]);
}

////////////////////////////////////////////////////////////////////////////////
// Interface

blast_t *blast_new(const crv_t *c, const blast_shape_t *shape)
{
   blast_t *b = xcalloc(sizeof(blast_t));
   b->c = c;
   b->shape = *shape;

   // Gate 0 is the constant false
   new_gate(b, 0, 0);
   tab_rehash(b, 4096);

   b->invec = xmalloc_array(shape->nv ? shape->nv : 1, sizeof(invec_t));
   for (unsigned i = 0; i < shape->nv; i++)
      b->invec[i].off = -1;

   return b;
}

void blast_free(blast_t *b)
{
   free(b->l0);
   free(b->l1);
   free(b->tab);
   free(b->in_gate);
   free(b->in_var);
   free(b->in_bit);
   free(b->in_sig);
   free(b->memo);
   free(b->pool);
   free(b->invec);
   free(b->ops);
   free(b->sim);
   free(b);
}

unsigned blast_width(blast_t *b, crv_node_t n)
{
   return get_node(b, n)->w;
}

blit_t blast_bit(blast_t *b, crv_node_t n, unsigned i)
{
   const enode_t *e = get_node(b, n);
   return i < e->w ? b->pool[e->off + i] : BLIT_FALSE;
}

blit_t blast_fail(blast_t *b, crv_node_t n)
{
   return get_node(b, n)->fail;
}

blit_t blast_holds(blast_t *b, crv_node_t n)
{
   const blit_t t = truth(b, n);
   return blast_and(b, t, lit_not(fail_of(b, n)));
}

blit_t blast_unique(blast_t *b, const crv_node_t *nodes, unsigned count)
{
   blit_t ok = BLIT_TRUE;
   for (unsigned i = 0; i < count; i++) {
      get_node(b, nodes[i]);
      ok = blast_and(b, ok, lit_not(fail_of(b, nodes[i])));
   }

   for (unsigned i = 0; i < count; i++) {
      const node_t *in = &(b->c->nodes[nodes[i]]);
      bv_t x;
      bv_init(&x);
      fetch(b, nodes[i], in->width, &x);
      for (unsigned j = i + 1; j < count; j++) {
         const node_t *jn = &(b->c->nodes[nodes[j]]);
         bv_t y;
         bv_init(&y);
         fetch(b, nodes[j], jn->width, &y);
         blit_t eq, lt;
         bv_cmp(b, &x, in->sgn, &y, jn->sgn, &eq, &lt);
         ok = blast_and(b, ok, lit_not(eq));
         bv_free(&y);
      }
      bv_free(&x);
   }

   return ok;
}

blit_t blast_var_range(blast_t *b, int v, __int128 lo, __int128 hi)
{
   const unsigned w = b->shape.width[v];
   if (w > 64)
      return BLIT_TRUE;
   const bool sgn = b->shape.sgn[v];
   const i128 tlo = sgn ? -((i128)1 << (w - 1)) : 0;
   const i128 thi = sgn ? ((i128)1 << (w - 1)) - 1 : ((i128)1 << w) - 1;
   if (lo <= tlo && hi >= thi)
      return BLIT_TRUE;
   else if (lo > hi || hi < tlo || lo > thi)
      return BLIT_FALSE;

   bv_t x;
   bv_init(&x);
   input_vec(b, v, &x);
   blit_t res;
   if (lo == hi) {
      bv_t kv;
      bv_init(&kv);
      bv_const(&kv, (uint64_t)lo, x.n);
      res = bv_eq(b, &x, &kv);
      bv_free(&kv);
   }
   else {
      const blit_t ge = bv_ge_const(b, &x, sgn, lo);
      const blit_t le = bv_le_const(b, &x, sgn, hi);
      res = blast_and(b, ge, le);
   }
   bv_free(&x);
   return res;
}

blit_t blast_var_eq(blast_t *b, int v, uint64_t val)
{
   bv_t x, kv;
   bv_init(&x);
   bv_init(&kv);
   input_vec(b, v, &x);
   bv_const(&kv, val, x.n);
   const blit_t r = bv_eq(b, &x, &kv);
   bv_free(&x);
   bv_free(&kv);
   return r;
}

void blast_set_limit(blast_t *b, unsigned gates)
{
   b->limit = gates;
}

bool blast_overflow(const blast_t *b)
{
   return b->over;
}

unsigned blast_gate_count(const blast_t *b)
{
   return b->ng;
}

int blast_gate_input(const blast_t *b, unsigned g)
{
   assert(g < b->ng);
   return b->l0[g] == INPUT ? (int)b->l1[g] : -1;
}

void blast_gate_fanins(const blast_t *b, unsigned g, blit_t *l0, blit_t *l1)
{
   assert(g > 0 && g < b->ng && b->l0[g] != INPUT);
   *l0 = b->l0[g];
   *l1 = b->l1[g];
}

unsigned blast_input_sig(const blast_t *b, unsigned i)
{
   assert(i < b->nin);
   return b->in_sig[i];
}

unsigned blast_input_gate(const blast_t *b, unsigned i)
{
   assert(i < b->nin);
   return b->in_gate[i];
}

unsigned blast_input_count(const blast_t *b)
{
   return b->nin;
}

void blast_input_var(const blast_t *b, unsigned i, int *var, unsigned *bit)
{
   assert(i < b->nin);
   *var = b->in_var[i];
   *bit = b->in_bit[i];
}

// Depth first walk of the cones of the outputs, the first fanin first.
// Calls visit once for each gate, a gate before the gates it uses.
static unsigned walk(const blast_t *b, const blit_t *outs, unsigned nouts,
                     unsigned *order)
{
   uint8_t *seen = xcalloc(b->ng);
   unsigned *gs = xmalloc_array(b->ng + 1, sizeof(unsigned));
   uint8_t *ks = xmalloc_array(b->ng + 1, sizeof(uint8_t));
   unsigned n = 0, ngates = 0;

   for (unsigned o = 0; o < nouts; o++) {
      unsigned sp = 0;
      if (outs[o] >> 1 == 0 || seen[outs[o] >> 1])
         continue;

      gs[sp] = outs[o] >> 1;
      ks[sp++] = 0;
      seen[outs[o] >> 1] = 1;
      while (sp > 0) {
         const unsigned g = gs[sp - 1];
         if (b->l0[g] == INPUT) {
            if (order != NULL)
               order[n] = b->l1[g];
            n++;
            sp--;
            continue;
         }

         if (ks[sp - 1] == 0)
            ngates++;

         if (ks[sp - 1] == 2) {
            sp--;
            continue;
         }

         const blit_t l = ks[sp - 1]++ == 0 ? b->l0[g] : b->l1[g];
         if (l >> 1 != 0 && !seen[l >> 1]) {
            seen[l >> 1] = 1;
            gs[sp] = l >> 1;
            ks[sp++] = 0;
         }
      }
   }

   free(ks);
   free(gs);
   free(seen);
   return order != NULL ? n : ngates;
}

unsigned blast_fanin_order(const blast_t *b, const blit_t *outs,
                           unsigned nouts, unsigned *order)
{
   return walk(b, outs, nouts, order);
}

unsigned blast_cone_gates(const blast_t *b, const blit_t *outs,
                          unsigned nouts)
{
   return walk(b, outs, nouts, NULL);
}

blast_stats_t blast_stats(const blast_t *b)
{
   blast_stats_t s = { .inputs = b->nin, .gates = b->ng - 1 - b->nin,
                       .nonlinear = b->nonlinear, .nops = b->nops };
   return s;
}

const blast_op_t *blast_ops(const blast_t *b, unsigned *count)
{
   *count = b->nops;
   return b->ops;
}

void blast_sim(blast_t *b, const int64_t *vals, unsigned lanes)
{
   assert(lanes >= 1 && lanes <= 64);

   if (b->nsim < b->ng) {
      b->sim = xrealloc_array(b->sim, b->ng, sizeof(uint64_t));
      b->nsim = b->ng;
   }

   uint64_t *sim = b->sim;
   sim[0] = 0;

   for (unsigned g = 1; g < b->ng; g++) {
      if (b->l0[g] == INPUT) {
         const unsigned i = b->l1[g];
         const int v = b->in_var[i];
         const unsigned bit = b->in_bit[i];
         uint64_t word = 0;
         for (unsigned l = 0; l < lanes; l++)
            word |= (((uint64_t)vals[(size_t)l * b->shape.nv + v] >> bit) & 1)
               << l;
         sim[g] = word;
      }
      else {
         const blit_t x = b->l0[g], y = b->l1[g];
         const uint64_t xv = sim[x >> 1] ^ (0 - (uint64_t)(x & 1));
         const uint64_t yv = sim[y >> 1] ^ (0 - (uint64_t)(y & 1));
         sim[g] = xv & yv;
      }
   }
}

uint64_t blast_lit(const blast_t *b, blit_t lit)
{
   return b->sim[lit >> 1] ^ (0 - (uint64_t)(lit & 1));
}

////////////////////////////////////////////////////////////////////////////////
// SMT-LIB 2 dump
//
// The word-level meaning of each node written as bit-vector terms next to
// the gates, so that an SMT solver can check that they agree.  The
// terms are written from the definition of the operators, not from the
// encoders.

typedef struct {
   blast_t *b;
   FILE    *f;
   uint8_t *done;
} smt_t;

static char *smt_bin(i128 v, unsigned w)
{
   char *s = xmalloc(w + 3);
   s[0] = '#';
   s[1] = 'b';
   for (unsigned i = 0; i < w; i++)
      s[2 + i] = ((v >> (w - 1 - i)) & 1) ? '1' : '0';
   s[w + 2] = 0;
   return s;
}

static char *smt_resize(char *term, unsigned from, unsigned w, bool sgn)
{
   char *r;
   if (w == from)
      return term;
   else if (w > from)
      r = xasprintf("((_ %s_extend %u) %s)", sgn ? "sign" : "zero", w - from,
                    term);
   else
      r = xasprintf("((_ extract %u 0) %s)", w - 1, term);
   free(term);
   return r;
}

// The value of node n extended or cut to w bits
static char *smt_ext(smt_t *S, crv_node_t n, unsigned w)
{
   const node_t *nd = &(S->b->c->nodes[n]);
   return smt_resize(xasprintf("n%d", n), nd->width, w, nd->sgn);
}

static char *smt_var(smt_t *S, int v, unsigned w, bool sgn)
{
   const blast_shape_t *sh = &(S->b->shape);
   char *t;
   if (sh->fixed != NULL && sh->fixed[v])
      t = smt_bin(sh->value[v], sh->width[v]);
   else
      t = xasprintf("v%d", v);
   return smt_resize(t, sh->width[v], w, sgn);
}

static char *smt_truth(smt_t *S, crv_node_t n)
{
   return xasprintf("(distinct n%d (_ bv0 %u))", n,
                    S->b->c->nodes[n].width);
}

// Comparison of the mathematical values of two nodes: = lt le
static char *smt_cmp(smt_t *S, const char *op, crv_node_t x, crv_node_t y)
{
   const node_t *xn = &(S->b->c->nodes[x]), *yn = &(S->b->c->nodes[y]);
   const unsigned w = MAX(xn->width, yn->width) + (xn->sgn != yn->sgn ? 1 : 0);
   const bool sg = xn->sgn || yn->sgn;
   char *a = smt_ext(S, x, w), *b = smt_ext(S, y, w);
   char *r = xasprintf("(%s%s %s %s)",
                       !strcmp(op, "=") ? "" : sg ? "bvs" : "bvu", op, a, b);
   free(a);
   free(b);
   return r;
}

static char *smt_bit(char *cond, unsigned w)
{
   char *r = xasprintf("(ite %s (_ bv1 %u) (_ bv0 %u))", cond, w, w);
   free(cond);
   return r;
}

static void smt_emit(smt_t *S, crv_node_t n, char *val, char *fail)
{
   fprintf(S->f, "(define-fun n%d () (_ BitVec %u) %s)\n", n,
           S->b->c->nodes[n].width, val);
   fprintf(S->f, "(define-fun f%d () Bool %s)\n", n, fail);
   free(val);
   free(fail);
}

static void smt_node(smt_t *S, crv_node_t n);

static void smt_children(smt_t *S, const node_t *nd)
{
   const crv_t *c = S->b->c;
   switch ((nkind_t)nd->kind) {
   case N_BIN:
      smt_node(S, nd->a);
      smt_node(S, nd->b);
      break;
   case N_UN:
   case N_CAST:
   case N_AELEM:
   case N_DIST:
      smt_node(S, nd->a);
      break;
   case N_COND:
      smt_node(S, nd->a);
      smt_node(S, nd->b);
      smt_node(S, nd->c);
      break;
   case N_INSIDE:
      {
         smt_node(S, nd->a);
         const cset_t *cs = &(c->sets[nd->aux]);
         for (unsigned i = 0; i < cs->n; i++) {
            smt_node(S, cs->items[i].lo);
            if (cs->items[i].hi >= 0)
               smt_node(S, cs->items[i].hi);
         }
      }
      break;
   default:
      break;
   }
}

static char *smt_unary(smt_t *S, const node_t *nd)
{
   const crv_t *c = S->b->c;
   const node_t *an = &(c->nodes[nd->a]);
   const unsigned w = nd->width, wa = an->width;

   switch ((crv_unary_t)nd->op) {
   case CRV_NEG:
   case CRV_INV:
      {
         char *x = smt_ext(S, nd->a, w);
         char *r = xasprintf("(%s %s)", nd->op == CRV_NEG ? "bvneg" : "bvnot",
                             x);
         free(x);
         return r;
      }
   case CRV_LOG_NOT:
      return smt_bit(xasprintf("(= n%d (_ bv0 %u))", nd->a, wa), w);
   case CRV_REDUCE_AND:
      return smt_bit(xasprintf("(= n%d (bvnot (_ bv0 %u)))", nd->a, wa), w);
   case CRV_REDUCE_OR:
      return smt_bit(xasprintf("(distinct n%d (_ bv0 %u))", nd->a, wa), w);
   case CRV_REDUCE_XOR:
      {
         char *t = xasprintf("((_ extract 0 0) n%d)", nd->a);
         for (unsigned i = 1; i < wa; i++) {
            char *u = xasprintf("(bvxor %s ((_ extract %u %u) n%d))", t, i, i,
                                nd->a);
            free(t);
            t = u;
         }
         char *r = smt_bit(xasprintf("(= %s #b1)", t), w);
         free(t);
         return r;
      }
   case CRV_POPCNT:
      {
         char *t = xasprintf("((_ zero_extend %u) ((_ extract 0 0) n%d))",
                             w - 1, nd->a);
         for (unsigned i = 1; i < wa; i++) {
            char *u = xasprintf("(bvadd %s ((_ zero_extend %u) ((_ extract "
                                "%u %u) n%d)))", t, w - 1, i, i, nd->a);
            free(t);
            t = u;
         }
         return t;
      }
   }

   should_not_reach_here();
}

static char *smt_binary(smt_t *S, const node_t *nd, char **fail)
{
   const crv_t *c = S->b->c;
   const node_t *an = &(c->nodes[nd->a]), *bn = &(c->nodes[nd->b]);
   const crv_binary_t op = nd->op;
   const unsigned w = nd->width;

   *fail = xasprintf("(or f%d f%d)", nd->a, nd->b);

   switch (op) {
   case CRV_ADD: case CRV_SUB: case CRV_MUL: case CRV_AND: case CRV_OR:
   case CRV_XOR:
      {
         char *x = smt_ext(S, nd->a, w), *y = smt_ext(S, nd->b, w);
         char *r = xasprintf("(%s %s %s)", op == CRV_ADD ? "bvadd"
                             : op == CRV_SUB ? "bvsub" : op == CRV_MUL
                             ? "bvmul" : op == CRV_AND ? "bvand"
                             : op == CRV_OR ? "bvor" : "bvxor", x, y);
         free(x);
         free(y);
         return r;
      }
   case CRV_DIV:
   case CRV_MOD:
      {
         const bool sg = an->sgn || bn->sgn;
         const unsigned ww = MAX(an->width, bn->width) + (sg ? 1 : 0);
         char *x = smt_ext(S, nd->a, ww), *y = smt_ext(S, nd->b, ww);
         char *q = xasprintf("(%s %s %s)", sg ? (op == CRV_DIV ? "bvsdiv"
                                                 : "bvsrem")
                             : (op == CRV_DIV ? "bvudiv" : "bvurem"), x, y);
         free(x);
         free(y);
         free(*fail);
         *fail = xasprintf("(or f%d f%d (= n%d (_ bv0 %u)))", nd->a, nd->b,
                           nd->b, bn->width);
         return smt_resize(q, ww, w, sg);
      }
   case CRV_SHL:
   case CRV_SHR:
   case CRV_ASR:
      {
         const bool arith = op == CRV_ASR && nd->sgn;
         char *x = smt_ext(S, nd->a, 64);
         if (op != CRV_SHL && !arith && w < 64) {
            char *m = smt_bin(((i128)1 << w) - 1, 64);
            char *u = xasprintf("(bvand %s %s)", x, m);
            free(x);
            free(m);
            x = u;
         }

         // The amount is the raw bits of the operand
         char *y = smt_resize(xasprintf("n%d", nd->b), bn->width, 64, false);
         char *r = xasprintf("(%s %s %s)", op == CRV_SHL ? "bvshl"
                             : arith ? "bvashr" : "bvlshr", x, y);
         free(x);
         free(y);
         return smt_resize(r, 64, w, false);
      }
   case CRV_EQ: case CRV_NE: case CRV_LT: case CRV_LE: case CRV_GT:
   case CRV_GE:
      {
         char *t;
         switch (op) {
         case CRV_EQ: t = smt_cmp(S, "=", nd->a, nd->b); break;
         case CRV_LT: t = smt_cmp(S, "lt", nd->a, nd->b); break;
         case CRV_LE: t = smt_cmp(S, "le", nd->a, nd->b); break;
         case CRV_NE:
            {
               char *e = smt_cmp(S, "=", nd->a, nd->b);
               t = xasprintf("(not %s)", e);
               free(e);
            }
            break;
         case CRV_GT:
            {
               char *e = smt_cmp(S, "le", nd->a, nd->b);
               t = xasprintf("(not %s)", e);
               free(e);
            }
            break;
         default:
            {
               char *e = smt_cmp(S, "lt", nd->a, nd->b);
               t = xasprintf("(not %s)", e);
               free(e);
            }
            break;
         }
         return smt_bit(t, w);
      }
   default:
      {
         // Logical operators: an undefined operand is unknown
         char *l = op == CRV_IMPLIES
            ? xasprintf("(= n%d (_ bv0 %u))", nd->a, an->width)
            : smt_truth(S, nd->a);
         char *r = smt_truth(S, nd->b);
         char *val, *ret;
         free(*fail);
         if (op == CRV_LOG_AND) {
            val = xasprintf("(and %s %s (not f%d) (not f%d))", l, r, nd->a,
                            nd->b);
            ret = xasprintf("(or (and (not f%d) (not %s)) (and (not f%d) "
                            "(not %s)))", nd->a, l, nd->b, r);
         }
         else {
            val = xasprintf("(or (and (not f%d) %s) (and (not f%d) %s))",
                            nd->a, l, nd->b, r);
            ret = xasprintf("%s", val);
         }
         *fail = xasprintf("(and (not %s) (or f%d f%d))", ret, nd->a, nd->b);
         free(ret);
         free(l);
         free(r);
         return smt_bit(val, w);
      }
   }
}

static void smt_inside(smt_t *S, crv_node_t n, const node_t *nd)
{
   const crv_t *c = S->b->c;
   const cset_t *cs = &(c->sets[nd->aux]);
   FILE *f = S->f;

   char *found = xasprintf("false"), *failed = xasprintf("f%d", nd->a);
   for (unsigned i = 0; i < cs->n; i++) {
      const item_t it = cs->items[i];
      char *ifail, *match;
      if (it.hi < 0) {
         ifail = xasprintf("f%d", it.lo);
         match = smt_cmp(S, "=", nd->a, it.lo);
      }
      else {
         ifail = xasprintf("(or f%d f%d)", it.lo, it.hi);
         char *lt = smt_cmp(S, "lt", nd->a, it.lo);
         char *le = smt_cmp(S, "le", nd->a, it.hi);
         match = xasprintf("(and (not %s) %s)", lt, le);
         free(lt);
         free(le);
      }

      char *alive = xasprintf("(and (not %s) (not %s))", found, failed);
      fprintf(f, "(define-fun t%d_%u () Bool (or %s (and %s %s)))\n", n, i,
              failed, alive, ifail);
      fprintf(f, "(define-fun s%d_%u () Bool (or %s (and %s (not %s) %s)))\n",
              n, i, found, alive, ifail, match);
      free(alive);
      free(ifail);
      free(match);
      free(found);
      free(failed);
      found = xasprintf("s%d_%u", n, i);
      failed = xasprintf("t%d_%u", n, i);
   }

   smt_emit(S, n, smt_bit(found, nd->width), failed);
}

static void smt_dist(smt_t *S, crv_node_t n, const node_t *nd)
{
   const crv_t *c = S->b->c;
   cdist_t *cd = (cdist_t *)&(c->dists[nd->aux]);
   crv_dist_prepare(cd);

   const node_t *an = &(c->nodes[nd->a]);
   const unsigned w = an->width + 1;
   const i128 lo = an->sgn ? -((i128)1 << (an->width - 1)) : 0;
   const i128 hi = an->sgn ? ((i128)1 << (an->width - 1)) - 1
      : ((i128)1 << an->width) - 1;

   char *x = smt_ext(S, nd->a, w);
   char *any = xasprintf("false");
   for (unsigned i = 0; i < cd->nmr; i++) {
      i128 l = cd->mr[i].lo, h = cd->mr[i].hi;
      if (l > hi || h < lo)
         continue;
      if (l < lo)
         l = lo;
      if (h > hi)
         h = hi;
      char *lc = smt_bin(l, w), *hc = smt_bin(h, w);
      char *u = xasprintf("(or %s (and (bvsle %s %s) (bvsle %s %s)))", any,
                          lc, x, x, hc);
      free(lc);
      free(hc);
      free(any);
      any = u;
   }

   free(x);
   smt_emit(S, n, smt_bit(any, nd->width), xasprintf("f%d", nd->a));
}

static void smt_elem(smt_t *S, crv_node_t n, const node_t *nd)
{
   const crv_t *c = S->b->c;
   const blast_shape_t *sh = &(S->b->shape);
   const node_t *an = &(c->nodes[nd->a]);
   const unsigned w = nd->width, iw = an->width + 1;
   const unsigned size = sh->sizes[nd->aux];

   char *idx = smt_ext(S, nd->a, iw);
   char *val = xasprintf("(_ bv0 %u)", w);
   char *any = xasprintf("false");
   if (sh->ebase[nd->aux] >= 0) {
      for (int k = size - 1; k >= 0; k--) {
         if (iw <= 64 && (uint64_t)k >= ((uint64_t)1 << (iw - 1)))
            continue;      // No index of this type is k

         char *kc = smt_bin(k, iw);
         char *e = smt_var(S, sh->ebase[nd->aux] + k, w,
                           sh->sgn[sh->ebase[nd->aux] + k]);
         char *u = xasprintf("(ite (= %s %s) %s %s)", idx, kc, e, val);
         char *a = xasprintf("(or (= %s %s) %s)", idx, kc, any);
         free(kc);
         free(e);
         free(val);
         free(any);
         val = u;
         any = a;
      }
   }
   free(idx);

   char *fail = xasprintf("(or f%d (not %s))", nd->a, any);
   free(any);
   smt_emit(S, n, val, fail);
}

static void smt_node(smt_t *S, crv_node_t n)
{
   if (S->done[n])
      return;
   S->done[n] = 1;

   const crv_t *c = S->b->c;
   const node_t *nd = &(c->nodes[n]);
   const blast_shape_t *sh = &(S->b->shape);
   const unsigned w = nd->width;

   smt_children(S, nd);

   switch ((nkind_t)nd->kind) {
   case N_CONST:
      smt_emit(S, n, smt_bin((i128)nd->val, w), xasprintf("false"));
      break;
   case N_VAR:
      smt_emit(S, n, smt_var(S, nd->aux, w, sh->sgn[nd->aux]),
               xasprintf("false"));
      break;
   case N_ASIZE:
      smt_emit(S, n, smt_var(S, sh->nsc + nd->aux, w, true),
               xasprintf("false"));
      break;
   case N_CAST:
      smt_emit(S, n, smt_ext(S, nd->a, w), xasprintf("f%d", nd->a));
      break;
   case N_UN:
      smt_emit(S, n, smt_unary(S, nd), xasprintf("f%d", nd->a));
      break;
   case N_BIN:
      {
         char *fail;
         char *val = smt_binary(S, nd, &fail);
         smt_emit(S, n, val, fail);
      }
      break;
   case N_COND:
      {
         char *t = smt_truth(S, nd->a);
         char *x = smt_ext(S, nd->b, w), *y = smt_ext(S, nd->c, w);
         char *val = xasprintf("(ite %s %s %s)", t, x, y);
         char *fail = xasprintf("(or f%d (ite %s f%d f%d))", nd->a, t, nd->b,
                                nd->c);
         free(t);
         free(x);
         free(y);
         smt_emit(S, n, val, fail);
      }
      break;
   case N_INSIDE:
      smt_inside(S, n, nd);
      break;
   case N_DIST:
      smt_dist(S, n, nd);
      break;
   case N_AELEM:
      smt_elem(S, n, nd);
      break;
   case N_ASUM:
      {
         char *val = xasprintf("(_ bv0 %u)", w);
         if (sh->ebase[nd->aux] >= 0) {
            for (unsigned k = 0; k < sh->sizes[nd->aux]; k++) {
               const int v = sh->ebase[nd->aux] + k;
               char *e = smt_var(S, v, w, sh->sgn[v]);
               char *u = xasprintf("(bvadd %s %s)", val, e);
               free(e);
               free(val);
               val = u;
            }
            smt_emit(S, n, val, xasprintf("false"));
         }
         else
            smt_emit(S, n, val, xasprintf("true"));
      }
      break;
   default:
      smt_emit(S, n, xasprintf("(_ bv0 %u)", w), xasprintf("true"));
      break;
   }
}

static char *smt_lit(blit_t l)
{
   if (l >> 1 == 0)
      return xasprintf("%s", (l & 1) ? "true" : "false");
   return (l & 1) ? xasprintf("(not g%u)", l >> 1) : xasprintf("g%u", l >> 1);
}

// The gates in the cones of the literals, defined in order
static void smt_gates(blast_t *b, FILE *f, const blit_t *lits, unsigned n)
{
   uint8_t *mark = xcalloc(b->ng);
   unsigned *stack = xmalloc_array(b->ng + 1, sizeof(unsigned));
   for (unsigned i = 0; i < n; i++) {
      unsigned sp = 0;
      const unsigned g0 = lits[i] >> 1;
      if (g0 == 0 || mark[g0])
         continue;
      mark[g0] = 1;
      stack[sp++] = g0;
      while (sp > 0) {
         const unsigned g = stack[--sp];
         if (b->l0[g] == INPUT)
            continue;
         const blit_t fi[2] = { b->l0[g], b->l1[g] };
         for (int k = 0; k < 2; k++) {
            if (fi[k] >> 1 != 0 && !mark[fi[k] >> 1]) {
               mark[fi[k] >> 1] = 1;
               stack[sp++] = fi[k] >> 1;
            }
         }
      }
   }

   for (unsigned g = 1; g < b->ng; g++) {
      if (!mark[g])
         continue;

      if (b->l0[g] == INPUT) {
         const unsigned in = b->l1[g];
         fprintf(f, "(define-fun g%u () Bool (= ((_ extract %u %u) v%d) "
                 "#b1))\n", g, b->in_bit[in], b->in_bit[in], b->in_var[in]);
      }
      else {
         char *x = smt_lit(b->l0[g]), *y = smt_lit(b->l1[g]);
         fprintf(f, "(define-fun g%u () Bool (and %s %s))\n", g, x, y);
         free(x);
         free(y);
      }
   }

   free(stack);
   free(mark);
}

void blast_dump_problem_smt2(blast_t *b, FILE *f, const crv_node_t *holds,
                             unsigned nholds, const crv_node_t *defs,
                             unsigned ndefs, const char *extra)
{
   fprintf(f, "(set-logic QF_BV)\n");

   const blast_shape_t *sh = &(b->shape);
   for (unsigned v = 0; v < sh->nv; v++) {
      if (sh->fixed == NULL || !sh->fixed[v])
         fprintf(f, "(declare-const v%u (_ BitVec %u))\n", v, sh->width[v]);
   }

   smt_t S = { b, f, xcalloc(b->nmemo > b->c->nnodes ? b->nmemo
                             : b->c->nnodes) };
   for (unsigned i = 0; i < ndefs; i++)
      smt_node(&S, defs[i]);
   for (unsigned i = 0; i < nholds; i++) {
      smt_node(&S, holds[i]);
      fprintf(f, "(assert (and (not f%d) (distinct n%d (_ bv0 %u))))\n",
              holds[i], holds[i], b->c->nodes[holds[i]].width);
   }

   if (extra != NULL)
      fputs(extra, f);
   fprintf(f, "(check-sat)\n");
   free(S.done);
}

void blast_dump_smt2(blast_t *b, FILE *f, const crv_node_t *roots,
                     unsigned count)
{
   fprintf(f, "; Bit-blaster check: unsatisfiable if the gates compute what "
           "the nodes do\n(set-logic QF_BV)\n");

   const blast_shape_t *sh = &(b->shape);
   for (unsigned v = 0; v < sh->nv; v++) {
      if (sh->fixed == NULL || !sh->fixed[v])
         fprintf(f, "(declare-const v%u (_ BitVec %u))\n", v, sh->width[v]);
   }

   smt_t S = { b, f, xcalloc(b->nmemo > b->c->nnodes ? b->nmemo
                             : b->c->nnodes) };
   for (unsigned i = 0; i < count; i++)
      smt_node(&S, roots[i]);

   // Literals of the circuit for each root
   size_t capl = 0;
   for (unsigned i = 0; i < count; i++)
      capl += blast_width(b, roots[i]) + 1;
   blit_t *lits = xmalloc_array(capl ? capl : 1, sizeof(blit_t));
   unsigned nl = 0;
   for (unsigned i = 0; i < count; i++) {
      const unsigned w = blast_width(b, roots[i]);
      for (unsigned j = 0; j < w; j++)
         lits[nl++] = blast_bit(b, roots[i], j);
      lits[nl++] = blast_fail(b, roots[i]);
   }
   smt_gates(b, f, lits, nl);

   fprintf(f, "(assert (or false");
   for (unsigned i = 0; i < count; i++) {
      const unsigned w = blast_width(b, roots[i]);
      char *cat = NULL;
      for (unsigned j = w; j > 0; j--) {
         char *l = smt_lit(blast_bit(b, roots[i], j - 1));
         char *bit = xasprintf("(ite %s #b1 #b0)", l);
         free(l);
         if (cat == NULL)
            cat = bit;
         else {
            char *u = xasprintf("(concat %s %s)", cat, bit);
            free(cat);
            free(bit);
            cat = u;
         }
      }
      char *fl = smt_lit(blast_fail(b, roots[i]));
      fprintf(f, "\n  (xor f%d %s) (and (not f%d) (distinct n%d %s))",
              roots[i], fl, roots[i], roots[i], cat);
      free(fl);
      free(cat);
   }
   fprintf(f, "))\n(check-sat)\n");

   free(lits);
   free(S.done);
}
