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
#include <stdlib.h>
#include <string.h>

#define MAXV 72      // The widest vector: a divider of 65 bit operands, plus one

typedef struct {
   unsigned n;
   blit_t   b[MAXV];
} bv_t;

typedef struct {
   int32_t off;
   uint8_t w;
   bool    sgn, done;
   blit_t  fail;
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
   uint8_t  *in_bit;
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
      b->in_bit = xrealloc_array(b->in_bit, b->cin, sizeof(uint8_t));
   }

   const unsigned g = new_gate(b, INPUT, b->nin);
   b->in_gate[b->nin] = g;
   b->in_var[b->nin] = var;
   b->in_bit[b->nin] = bit;
   b->nin++;
   return g << 1;
}

static blit_t constant(bool v)
{
   return v ? BLIT_TRUE : BLIT_FALSE;
}

////////////////////////////////////////////////////////////////////////////////
// Vectors

static void bv_const(bv_t *v, uint64_t val, unsigned w)
{
   v->n = w;
   for (unsigned i = 0; i < w; i++)
      v->b[i] = constant(i < 64 && ((val >> i) & 1));
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
   assert(w <= MAXV);
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
   for (unsigned i = 0; i < x->n; i++)
      out->b[i] = lit_not(x->b[i]);
   out->n = x->n;
}

// x - y over w bits: the carry out is set when x >= y (unsigned)
static void bv_sub(blast_t *b, const bv_t *x, const bv_t *y, unsigned w,
                   bv_t *out, blit_t *cout)
{
   bv_t ny;
   bv_not(y, &ny);
   bv_add(b, x, &ny, BLIT_TRUE, w, out, cout);
}

static void bv_neg(blast_t *b, const bv_t *x, unsigned w, bv_t *out)
{
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
   bv_t fx = *x, fy = *y;
   fx.b[x->n - 1] = lit_not(fx.b[x->n - 1]);
   fy.b[y->n - 1] = lit_not(fy.b[y->n - 1]);
   return bv_ult(b, &fx, &fy);
}

// Compare the mathematical values of two numbers
static void bv_cmp(blast_t *b, const bv_t *x, bool xs, const bv_t *y, bool ys,
                   blit_t *eq, blit_t *lt)
{
   const unsigned w = MAX(x->n, y->n) + (xs != ys ? 1 : 0);
   bv_t ex = *x, ey = *y;
   bv_resize(&ex, w, xs);
   bv_resize(&ey, w, ys);
   *eq = bv_eq(b, &ex, &ey);
   *lt = (xs || ys) ? bv_slt(b, &ex, &ey) : bv_ult(b, &ex, &ey);
}

// Compare with a constant, which may be outside the range of the type
static blit_t bv_le_const(blast_t *b, const bv_t *x, bool xs, i128 k)
{
   const unsigned w = x->n;
   const i128 lo = xs ? -((i128)1 << (w - 1)) : 0;
   const i128 hi = xs ? ((i128)1 << (w - 1)) - 1 : ((i128)1 << w) - 1;
   if (k >= hi)
      return BLIT_TRUE;
   else if (k < lo)
      return BLIT_FALSE;

   bv_t kv;
   bv_const(&kv, (uint64_t)k, w);
   const blit_t lt = xs ? bv_slt(b, x, &kv) : bv_ult(b, x, &kv);
   return blast_or(b, lt, bv_eq(b, x, &kv));
}

static blit_t bv_ge_const(blast_t *b, const bv_t *x, bool xs, i128 k)
{
   const unsigned w = x->n;
   const i128 lo = xs ? -((i128)1 << (w - 1)) : 0;
   const i128 hi = xs ? ((i128)1 << (w - 1)) - 1 : ((i128)1 << w) - 1;
   if (k <= lo)
      return BLIT_TRUE;
   else if (k > hi)
      return BLIT_FALSE;

   bv_t kv;
   bv_const(&kv, (uint64_t)k, w);
   return lit_not(xs ? bv_slt(b, x, &kv) : bv_ult(b, x, &kv));
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

   bv_t acc;
   bv_const(&acc, 0, w);
   for (unsigned i = 0; i < w; i++) {
      if (y->b[i] == BLIT_FALSE)
         continue;

      bv_t row, sum;
      row.n = w - i;
      bool zero = true;
      for (unsigned j = 0; j < row.n; j++) {
         row.b[j] = blast_and(b, y->b[i], x->b[j]);
         zero &= row.b[j] == BLIT_FALSE;
      }
      if (zero)
         continue;

      bv_t hi;
      hi.n = w - i;
      memcpy(hi.b, acc.b + i, (w - i) * sizeof(blit_t));
      bv_add(b, &hi, &row, BLIT_FALSE, w - i, &sum, NULL);
      memcpy(acc.b + i, sum.b, (w - i) * sizeof(blit_t));
   }
   *out = acc;
}

// Restoring divider on unsigned values of w bits
static void bv_udiv(blast_t *b, const bv_t *n, const bv_t *d, unsigned w,
                    bv_t *q, bv_t *r)
{
   // The remainder register has one more bit than the operands
   bv_t rem, dd;
   bv_const(&rem, 0, w + 1);
   dd.n = w + 1;
   memcpy(dd.b, d->b, w * sizeof(blit_t));
   dd.b[w] = BLIT_FALSE;

   q->n = w;
   for (int i = w - 1; i >= 0; i--) {
      // rem = (rem << 1) | n[i]
      for (int j = w; j > 0; j--)
         rem.b[j] = rem.b[j - 1];
      rem.b[0] = n->b[i];

      bv_t diff;
      blit_t ge;
      bv_sub(b, &rem, &dd, w + 1, &diff, &ge);
      q->b[i] = ge;
      bv_mux(b, ge, &diff, &rem, &rem);
   }

   r->n = w;
   memcpy(r->b, rem.b, w * sizeof(blit_t));
}

// Division and modulo of the mathematical values, truncating towards zero.
// The operands may differ in width and signedness.  The result has w bits.
static void bv_divmod(blast_t *b, const bv_t *x, bool xs, const bv_t *y,
                      bool ys, bool want_mod, unsigned w, bv_t *out)
{
   const bool sg = xs || ys;
   const unsigned ww = MAX(x->n, y->n) + (sg ? 1 : 0);
   bv_t ex = *x, ey = *y;
   bv_resize(&ex, ww, xs);
   bv_resize(&ey, ww, ys);

   bv_t q, r;
   if (!sg) {
      bv_udiv(b, &ex, &ey, ww, &q, &r);
      bv_t *res = want_mod ? &r : &q;
      bv_resize(res, w, false);
      *out = *res;
      out->n = w;
      return;
   }

   const blit_t sx = ex.b[ww - 1], sy = ey.b[ww - 1];
   bv_t nx, ny, ax, ay;
   bv_neg(b, &ex, ww, &nx);
   bv_neg(b, &ey, ww, &ny);
   bv_mux(b, sx, &nx, &ex, &ax);
   bv_mux(b, sy, &ny, &ey, &ay);

   bv_udiv(b, &ax, &ay, ww, &q, &r);

   bv_t res, nres;
   if (want_mod) {
      bv_neg(b, &r, ww, &nres);
      bv_mux(b, sx, &nres, &r, &res);
   }
   else {
      bv_neg(b, &q, ww, &nres);
      bv_mux(b, g_xor(b, sx, sy), &nres, &q, &res);
   }

   bv_resize(&res, w, true);
   *out = res;
   out->n = w;
}

// Barrel shifter on the 64 bit value x: by the count held in the low bits
// of cnt (cnt->n bits, taken as unsigned).  kind: 0 left, 1 logical right,
// 2 arithmetic right.
static void bv_shift(blast_t *b, const bv_t *x, const bv_t *cnt, int kind,
                     unsigned w, bv_t *out)
{
   assert(x->n == 64);

   bv_t cur = *x;
   for (unsigned k = 0; k < 6; k++) {
      const blit_t sel = k < cnt->n ? cnt->b[k] : BLIT_FALSE;
      if (sel == BLIT_FALSE)
         continue;

      const unsigned by = 1u << k;
      bv_t sh;
      sh.n = 64;
      for (unsigned i = 0; i < 64; i++) {
         if (kind == 0)
            sh.b[i] = i >= by ? cur.b[i - by] : BLIT_FALSE;
         else if (i + by < 64)
            sh.b[i] = cur.b[i + by];
         else
            sh.b[i] = kind == 2 ? x->b[63] : BLIT_FALSE;
      }
      for (unsigned i = 0; i < 64; i++)
         cur.b[i] = g_mux(b, sel, sh.b[i], cur.b[i]);
   }

   // A count of 64 or more shifts everything out
   blit_t big = BLIT_FALSE;
   for (unsigned k = 6; k < cnt->n; k++)
      big = blast_or(b, big, cnt->b[k]);

   const blit_t fill = kind == 2 ? x->b[63] : BLIT_FALSE;
   out->n = w;
   for (unsigned i = 0; i < w; i++)
      out->b[i] = g_mux(b, big, fill, cur.b[i]);
}

// Number of set bits among n literals, as a vector of the width needed
static void bv_popcount(blast_t *b, const blit_t *x, unsigned n, bv_t *out)
{
   if (n == 1) {
      out->n = 1;
      out->b[0] = x[0];
      return;
   }

   const unsigned h = n / 2;
   bv_t l, r;
   bv_popcount(b, x, h, &l);
   bv_popcount(b, x + h, n - h, &r);

   const unsigned w = MAX(l.n, r.n) + 1;
   bv_resize(&l, w, false);
   bv_resize(&r, w, false);
   bv_add(b, &l, &r, BLIT_FALSE, w, out, NULL);

   // The top bit is only needed if it can be set
   while (out->n > 1 && out->b[out->n - 1] == BLIT_FALSE)
      out->n--;
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
         if (s->fixed != NULL && s->fixed[v])
            b->pool[b->npool++] = constant((s->value[v] >> i) & 1);
         else
            b->pool[b->npool++] = new_input(b, v, i);
      }
   }

   out->n = w;
   memcpy(out->b, b->pool + b->invec[v].off, w * sizeof(blit_t));
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

   if (nd->op == CRV_MUL || nd->op == CRV_DIV || nd->op == CRV_MOD)
      b->nonlinear = MAX(b->nonlinear, MIN(vx, vy));
}

static void encode_bin(blast_t *b, crv_node_t n, const node_t *nd)
{
   const crv_binary_t op = nd->op;
   const unsigned w = nd->width;
   bv_t out = { .n = w };

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
         fetch(b, nd->a, w, &x);
         fetch(b, nd->b, w, &y);
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
      }
      break;

   case CRV_DIV:
   case CRV_MOD:
      {
         bv_t x, y;
         fetch(b, nd->a, an->width, &x);
         fetch(b, nd->b, bn->width, &y);
         note_op(b, n, nd, &x, &y);
         bv_divmod(b, &x, an->sgn, &y, bn->sgn, op == CRV_MOD, w, &out);
         fail = blast_or(b, fail, lit_not(bv_any(b, y.b, y.n)));
      }
      break;

   case CRV_SHL:
   case CRV_SHR:
   case CRV_ASR:
      {
         bv_t x, cnt;
         fetch(b, nd->a, 64, &x);

         // The value of a node is its representation in 64 bits
         if (op != CRV_SHL && !(op == CRV_ASR && nd->sgn)) {
            for (unsigned i = w; i < 64; i++)
               x.b[i] = BLIT_FALSE;
         }

         // The count is the raw bits of the operand
         const enode_t *be = get_node(b, nd->b);
         cnt.n = be->w;
         memcpy(cnt.b, b->pool + be->off, be->w * sizeof(blit_t));

         bv_shift(b, &x, &cnt, op == CRV_SHL ? 0
                  : op == CRV_ASR && nd->sgn ? 2 : 1, w, &out);
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
         fetch(b, nd->a, an->width, &x);
         fetch(b, nd->b, bn->width, &y);
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
      }
      break;

   default:
      should_not_reach_here();
   }

   store(b, n, &out, fail);
}

static void encode_un(blast_t *b, crv_node_t n, const node_t *nd)
{
   const unsigned w = nd->width;
   get_node(b, nd->a);
   const node_t *an = &(b->c->nodes[nd->a]);
   const blit_t fail = fail_of(b, nd->a);

   bv_t x, out = { .n = w };
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
}

static void encode_inside(blast_t *b, crv_node_t n, const node_t *nd)
{
   const cset_t *cs = &(b->c->sets[nd->aux]);
   const node_t *an = &(b->c->nodes[nd->a]);

   get_node(b, nd->a);
   bv_t x;
   fetch(b, nd->a, an->width, &x);

   // The items are looked at in order and the search stops at a match, so
   // an undefined item only counts if nothing before it matched
   blit_t found = BLIT_FALSE, failed = fail_of(b, nd->a);
   for (unsigned i = 0; i < cs->n; i++) {
      const item_t it = cs->items[i];
      get_node(b, it.lo);
      const node_t *ln = &(b->c->nodes[it.lo]);
      bv_t lo;
      fetch(b, it.lo, ln->width, &lo);

      blit_t ifail = fail_of(b, it.lo), match;
      blit_t eq, lt;
      bv_cmp(b, &x, an->sgn, &lo, ln->sgn, &eq, &lt);
      if (it.hi < 0)
         match = eq;
      else {
         get_node(b, it.hi);
         const node_t *hn = &(b->c->nodes[it.hi]);
         bv_t hi;
         fetch(b, it.hi, hn->width, &hi);
         ifail = blast_or(b, ifail, fail_of(b, it.hi));

         blit_t heq, hlt;
         bv_cmp(b, &x, an->sgn, &hi, hn->sgn, &heq, &hlt);
         match = blast_and(b, lit_not(lt), blast_or(b, hlt, heq));
      }

      const blit_t alive = blast_and(b, lit_not(found), lit_not(failed));
      failed = blast_or(b, failed, blast_and(b, alive, ifail));
      found = blast_or(b, found,
                       blast_and(b, blast_and(b, alive, lit_not(ifail)),
                                 match));
   }

   bv_t out = { .n = nd->width };
   out.b[0] = found;
   for (unsigned i = 1; i < out.n; i++)
      out.b[i] = BLIT_FALSE;
   store(b, n, &out, failed);
}

static void encode_dist(blast_t *b, crv_node_t n, const node_t *nd)
{
   cdist_t *cd = (cdist_t *)&(b->c->dists[nd->aux]);
   crv_dist_prepare(cd);

   const node_t *an = &(b->c->nodes[nd->a]);
   get_node(b, nd->a);
   bv_t x;
   fetch(b, nd->a, an->width, &x);

   blit_t in = BLIT_FALSE;
   for (unsigned i = 0; i < cd->nmr; i++) {
      const blit_t ge = bv_ge_const(b, &x, an->sgn, cd->mr[i].lo);
      const blit_t le = bv_le_const(b, &x, an->sgn, cd->mr[i].hi);
      in = blast_or(b, in, blast_and(b, ge, le));
   }

   bv_t out = { .n = nd->width };
   out.b[0] = in;
   for (unsigned i = 1; i < out.n; i++)
      out.b[i] = BLIT_FALSE;
   store(b, n, &out, fail_of(b, nd->a));
}

static void encode_elem(blast_t *b, crv_node_t n, const node_t *nd)
{
   const blast_shape_t *s = &(b->shape);
   const unsigned w = nd->width;
   bv_t out = { .n = w };

   if (s->ebase[nd->aux] < 0) {
      // The elements are not variables of this problem
      bv_const(&out, 0, w);
      store(b, n, &out, BLIT_TRUE);
      return;
   }

   const node_t *an = &(b->c->nodes[nd->a]);
   get_node(b, nd->a);
   bv_t idx;
   fetch(b, nd->a, an->width, &idx);

   const unsigned size = s->sizes[nd->aux];
   blit_t any = BLIT_FALSE;
   bv_const(&out, 0, w);
   for (unsigned k = 0; k < size; k++) {
      // Index equal to k: a negative index never is
      blit_t sel;
      if (an->sgn ? (k >= ((uint64_t)1 << (an->width - 1)))
          : (an->width < 64 && k >= ((uint64_t)1 << an->width)))
         sel = BLIT_FALSE;
      else {
         bv_t kv;
         bv_const(&kv, k, an->width);
         sel = bv_eq(b, &idx, &kv);
      }

      if (sel == BLIT_FALSE)
         continue;

      any = blast_or(b, any, sel);

      bv_t ev;
      input_vec(b, s->ebase[nd->aux] + k, &ev);
      bv_resize(&ev, w, s->sgn[s->ebase[nd->aux] + k]);
      for (unsigned i = 0; i < w; i++)
         out.b[i] = blast_or(b, out.b[i], blast_and(b, sel, ev.b[i]));
   }

   store(b, n, &out, blast_or(b, fail_of(b, nd->a), lit_not(any)));
}

static void encode_sum(blast_t *b, crv_node_t n, const node_t *nd)
{
   const blast_shape_t *s = &(b->shape);
   const unsigned w = nd->width;
   bv_t acc;
   bv_const(&acc, 0, w);

   if (s->ebase[nd->aux] < 0) {
      store(b, n, &acc, BLIT_TRUE);
      return;
   }

   for (unsigned k = 0; k < s->sizes[nd->aux]; k++) {
      bv_t ev, sum;
      const int v = s->ebase[nd->aux] + k;
      input_vec(b, v, &ev);
      bv_resize(&ev, w, s->sgn[v]);
      bv_add(b, &acc, &ev, BLIT_FALSE, w, &sum, NULL);
      acc = sum;
   }

   store(b, n, &acc, BLIT_FALSE);
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
   bv_t out = { .n = w };

   switch ((nkind_t)nd->kind) {
   case N_CONST:
      bv_const(&out, (uint64_t)nd->val, w);
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
         fetch(b, nd->b, w, &x);
         fetch(b, nd->c, w, &y);
         bv_mux(b, t, &x, &y, &out);
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
      fetch(b, nodes[i], in->width, &x);
      for (unsigned j = i + 1; j < count; j++) {
         const node_t *jn = &(b->c->nodes[nodes[j]]);
         bv_t y;
         fetch(b, nodes[j], jn->width, &y);
         blit_t eq, lt;
         bv_cmp(b, &x, in->sgn, &y, jn->sgn, &eq, &lt);
         ok = blast_and(b, ok, lit_not(eq));
      }
   }

   return ok;
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
