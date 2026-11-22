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
#include "rt/crv.h"

#include <assert.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

// Values are held as 128-bit integers during propagation so that the
// result of any 64-bit operation can be represented exactly before it is
// wrapped to the width of its node.
typedef __int128 i128;

typedef struct { i128 lo, hi; } ivl_t;     // Empty if lo > hi
typedef struct { i128 lo, hi; } range_t;

#define BIG ((i128)1 << 100)
#define MAX_SIZE (1 << 20)
#define MAX_SIZE_CHOICES 1024
#define MAX_NODES (1 << 24)
#define MAX_BIND 16
#define SMALL_HULL 4096
#define TABU_MIN 2048

#define VEC_PUSH(v, n, cap, x) do {                                     \
      if ((n) == (cap)) {                                               \
         (cap) = (cap) ? (cap) * 2 : 16;                                \
         (v) = xrealloc((v), (size_t)(cap) * sizeof(*(v)));             \
      }                                                                 \
      (v)[(n)++] = (x);                                                 \
   } while (0)

#define NEW(T, n) ((T *)xmalloc(((n) ? (size_t)(n) : 1) * sizeof(T)))

////////////////////////////////////////////////////////////////////////////////
// Random numbers

static __thread uint64_t rng_s[4];
static __thread bool rng_ready;

static uint64_t splitmix(uint64_t *x)
{
   uint64_t z = (*x += UINT64_C(0x9e3779b97f4a7c15));
   z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
   z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
   return z ^ (z >> 31);
}

void crv_seed(uint64_t seed)
{
   for (int i = 0; i < 4; i++)
      rng_s[i] = splitmix(&seed);
   rng_ready = true;
}

static inline uint64_t rotl(uint64_t x, int k)
{
   return (x << k) | (x >> (64 - k));
}

static uint64_t rnd64(void)
{
   if (unlikely(!rng_ready))
      crv_seed(UINT64_C(0x5eed5eed12345678));

   const uint64_t result = rotl(rng_s[1] * 5, 7) * 9;
   const uint64_t t = rng_s[1] << 17;

   rng_s[2] ^= rng_s[0];
   rng_s[3] ^= rng_s[1];
   rng_s[1] ^= rng_s[2];
   rng_s[0] ^= rng_s[3];
   rng_s[2] ^= t;
   rng_s[3] = rotl(rng_s[3], 45);

   return result;
}

#define MAX_SEED_DEPTH 16

typedef struct {
   uint64_t s[4];
   bool     ready;
   bool     active;
} seed_save_t;

static __thread seed_save_t seed_stack[MAX_SEED_DEPTH];
static __thread int seed_depth;

void crv_push_seed(uint64_t seed)
{
   // The random stream of an object replaces the global one while its
   // constraints are solved

   assert(seed_depth < MAX_SEED_DEPTH);
   seed_save_t *e = &(seed_stack[seed_depth++]);

   e->active = seed != 0;
   if (e->active) {
      memcpy(e->s, rng_s, sizeof(rng_s));
      e->ready = rng_ready;
      crv_seed(seed);
   }
}

uint64_t crv_pop_seed(void)
{
   assert(seed_depth > 0);
   seed_save_t *e = &(seed_stack[--seed_depth]);

   if (!e->active)
      return 0;

   const uint64_t next = rnd64() | 1;
   memcpy(rng_s, e->s, sizeof(rng_s));
   rng_ready = e->ready;
   return next;
}

static uint64_t rnd_below(uint64_t n)
{
   if ((n & (n - 1)) == 0)
      return rnd64() & (n - 1);

   const uint64_t lim = -n % n;
   uint64_t r;
   do {
      r = rnd64();
   } while (r < lim);
   return r % n;
}

// Uniform in [0, n) for 0 < n <= 2^64
static i128 rnd_i128(i128 n)
{
   if (n >= ((i128)1 << 64))
      return rnd64();
   return rnd_below((uint64_t)n);
}

static double rnd_dbl(void)
{
   return (double)(rnd64() >> 11) * (1.0 / 9007199254740992.0);
}

////////////////////////////////////////////////////////////////////////////////
// Types and values

static inline uint64_t wmask(unsigned w)
{
   return w >= 64 ? ~UINT64_C(0) : (UINT64_C(1) << w) - 1;
}

static inline i128 tmin(unsigned w, bool s)
{
   return s ? -((i128)1 << (w - 1)) : 0;
}

static inline i128 tmax(unsigned w, bool s)
{
   return s ? ((i128)1 << (w - 1)) - 1 : ((i128)1 << w) - 1;
}

// Wrap to the stored representation of a w-bit value
static inline int64_t norm(i128 v, unsigned w, bool s)
{
   uint64_t u = (uint64_t)v & wmask(w);
   if (s && w < 64 && ((u >> (w - 1)) & 1))
      u |= ~wmask(w);
   return (int64_t)u;
}

static inline i128 mathv(int64_t v, bool s)
{
   return s ? (i128)v : (i128)(uint64_t)v;
}

static inline i128 nrm(i128 v, unsigned w, bool s)
{
   return mathv(norm(v, w, s), s);
}

static inline int64_t st(i128 v)
{
   return (int64_t)(uint64_t)v;
}

static inline unsigned clampw(unsigned w)
{
   return w < 1 ? 1 : w > 64 ? 64 : w;
}

static inline ivl_t ivl(i128 lo, i128 hi)
{
   return (ivl_t){ lo, hi };
}

static inline bool iv_empty(ivl_t a) { return a.lo > a.hi; }
static inline bool iv_pt(ivl_t a) { return a.lo == a.hi; }
static inline bool can0(ivl_t a) { return a.lo <= 0 && a.hi >= 0; }
static inline bool can1(ivl_t a) { return a.lo != 0 || a.hi != 0; }
static inline bool is_true(ivl_t a) { return !iv_empty(a) && !can0(a); }
static inline bool is_false(ivl_t a) { return a.lo == 0 && a.hi == 0; }

static inline ivl_t isect(ivl_t a, ivl_t b)
{
   return ivl(a.lo > b.lo ? a.lo : b.lo, a.hi < b.hi ? a.hi : b.hi);
}

static inline ivl_t hull(ivl_t a, ivl_t b)
{
   if (iv_empty(a))
      return b;
   else if (iv_empty(b))
      return a;
   return ivl(a.lo < b.lo ? a.lo : b.lo, a.hi > b.hi ? a.hi : b.hi);
}

// Map an unbounded interval back into the range of a w-bit type
static ivl_t wrap(ivl_t r, unsigned w, bool s)
{
   const i128 tl = tmin(w, s), th = tmax(w, s);
   if (iv_empty(r) || (r.lo >= tl && r.hi <= th))
      return r;
   else if (r.hi - r.lo >= th - tl)
      return ivl(tl, th);

   const i128 a = nrm(r.lo, w, s), b = nrm(r.hi, w, s);
   return a <= b ? ivl(a, b) : ivl(tl, th);
}

static ivl_t iv_mul(ivl_t a, ivl_t b)
{
   const i128 c[4][2] = { { a.lo, b.lo }, { a.lo, b.hi },
                          { a.hi, b.lo }, { a.hi, b.hi } };
   ivl_t r = ivl(BIG, -BIG);
   for (int i = 0; i < 4; i++) {
      i128 p;
      if (__builtin_mul_overflow(c[i][0], c[i][1], &p) || p > BIG || p < -BIG)
         return ivl(-BIG, BIG);
      if (p < r.lo) r.lo = p;
      if (p > r.hi) r.hi = p;
   }
   return r;
}

static i128 fdiv(i128 a, i128 b)
{
   i128 q = a / b;
   if ((a % b != 0) && ((a < 0) != (b < 0)))
      q--;
   return q;
}

static i128 cdiv(i128 a, i128 b)
{
   i128 q = a / b;
   if ((a % b != 0) && ((a < 0) == (b < 0)))
      q++;
   return q;
}

// Values x such that c * x is in r
static ivl_t iv_divc(ivl_t r, i128 c)
{
   if (c > 0)
      return ivl(cdiv(r.lo, c), fdiv(r.hi, c));
   else
      return ivl(cdiv(r.hi, c), fdiv(r.lo, c));
}

static i128 pow2mask(i128 x)
{
   i128 m = 0;
   while (m < x)
      m = (m << 1) | 1;
   return m;
}

////////////////////////////////////////////////////////////////////////////////
// The graph

typedef enum {
   N_CONST, N_VAR, N_BIN, N_UN, N_CAST, N_COND, N_ASIZE, N_AELEM, N_ASUM,
   N_INSIDE, N_DIST, N_INDEX, N_FAIL,
} nkind_t;

typedef struct {
   uint8_t kind, op, width;
   bool    sgn;
   bool    inst;     // Contains an index or array element to substitute
   int32_t a, b, c;
   int32_t aux;
   int64_t val;
} node_t;

typedef struct {
   unsigned  width;
   bool      sgn;
   int64_t   cur;
   uint64_t *rc;
   int64_t  *dom;
   unsigned  ndom, cdom;
} cvar_t;

typedef struct {
   unsigned  width;
   bool      sgn, size_rand, elems_rand;
   unsigned  len;
   int64_t  *init;
   unsigned  ninit;
} carr_t;

typedef struct {
   crv_node_t lo, hi;      // hi is -1 for a single value
} item_t;

typedef struct {
   item_t   *items;
   unsigned  n, cap;
   range_t  *rr;
   unsigned  nrr;
   bool      allconst, prep;
} cset_t;

typedef struct {
   int64_t lo, hi, weight;
   bool    per_value;
} drange_t;

typedef struct {
   drange_t *r;
   unsigned  n, cap;
   bool      sgn, prep;
   range_t  *mr;           // Merged ranges with non-zero weight
   unsigned  nmr;
   range_t  *pr;           // The same ranges for picking
   double   *unit;         // Weight of each value
   unsigned  npr;
} cdist_t;

typedef enum { C_REQ, C_FOREACH, C_UNIQUE } ckind_t;

typedef struct {
   ckind_t     kind;
   crv_node_t  root;
   int         soft;       // Index among soft constraints or -1
   unsigned    nsoft;      // Soft indices of a foreach, one per element
   crv_array_t arr;
   crv_node_t  idx;
   unsigned    first, count;
   bool        dead;       // Soft constraint discarded by disable soft
   int         line;       // Source line for NVC_CRV_DEBUG
} ccons_t;

typedef struct {
   bool        array;
   int         before;
   crv_var_t   after;
} edge_t;

struct _crv {
   node_t   *nodes;
   unsigned  nnodes, cnodes;
   cvar_t   *vars;
   unsigned  nvars, cvars;
   carr_t   *arrays;
   unsigned  narrays, carrays;
   cset_t   *sets;
   unsigned  nsets, csets;
   cdist_t  *dists;
   unsigned  ndists, cdists;
   ccons_t  *cons;
   unsigned  ncons, ccons;
   crv_node_t *uniq;
   unsigned  nuniq, cuniq;
   edge_t   *edges;
   unsigned  nedges, cedges;
   int       nindex;
   int       nsoft;
   bool      greedy;   // Soft constraints are kept one at a time
};

// The first n entries are the values, remaining ones first, and the next n
// are the index of each value
//
// A variable wider than 16 bits has n == 0 and instead remembers the values
// used in this cycle in an open addressing hash table of value + 1
typedef struct {
   uint32_t n, left;
   uint64_t nused, cap;
   uint64_t *used;
   uint32_t vals[];
} randc_t;

static inline uint64_t randc_key(uint64_t x, unsigned width)
{
   return (width < 64 ? x & ((UINT64_C(1) << width) - 1) : x) + 1;
}

static inline uint64_t randc_hash(uint64_t k)
{
   k ^= k >> 33;
   k *= UINT64_C(0xff51afd7ed558ccd);
   k ^= k >> 33;
   return k;
}

static bool randc_used(const randc_t *rc, uint64_t key)
{
   if (rc->cap == 0 || key == 0)
      return false;

   const uint64_t mask = rc->cap - 1;
   for (uint64_t i = randc_hash(key) & mask;; i = (i + 1) & mask) {
      if (rc->used[i] == key)
         return true;
      else if (rc->used[i] == 0)
         return false;
   }
}

static void randc_clear(randc_t *rc)
{
   if (rc->cap > 0)
      memset(rc->used, 0, rc->cap * sizeof(uint64_t));
   rc->nused = 0;
}

static void randc_insert(randc_t *rc, uint64_t key)
{
   if (key == 0 || randc_used(rc, key))
      return;

   if (rc->nused * 2 >= rc->cap) {
      uint64_t *old = rc->used, ocap = rc->cap;
      rc->cap = ocap ? ocap * 2 : 64;
      rc->used = xcalloc(rc->cap * sizeof(uint64_t));
      for (uint64_t i = 0; i < ocap; i++) {
         if (old[i] != 0) {
            uint64_t j = randc_hash(old[i]) & (rc->cap - 1);
            while (rc->used[j] != 0)
               j = (j + 1) & (rc->cap - 1);
            rc->used[j] = old[i];
         }
      }
      free(old);
   }

   uint64_t i = randc_hash(key) & (rc->cap - 1);
   while (rc->used[i] != 0)
      i = (i + 1) & (rc->cap - 1);
   rc->used[i] = key;
   rc->nused++;
}

crv_t *crv_new(void)
{
   return xcalloc(sizeof(crv_t));
}

void crv_free(crv_t *c)
{
   for (unsigned i = 0; i < c->nvars; i++)
      free(c->vars[i].dom);
   for (unsigned i = 0; i < c->narrays; i++)
      free(c->arrays[i].init);
   for (unsigned i = 0; i < c->nsets; i++) {
      free(c->sets[i].items);
      free(c->sets[i].rr);
   }
   for (unsigned i = 0; i < c->ndists; i++) {
      free(c->dists[i].r);
      free(c->dists[i].mr);
      free(c->dists[i].pr);
      free(c->dists[i].unit);
   }
   free(c->nodes);
   free(c->vars);
   free(c->arrays);
   free(c->sets);
   free(c->dists);
   free(c->cons);
   free(c->uniq);
   free(c->edges);
   free(c);
}

static crv_node_t add_node(crv_t *c, node_t n)
{
   VEC_PUSH(c->nodes, c->nnodes, c->cnodes, n);
   return c->nnodes - 1;
}

static crv_node_t mk_const(crv_t *c, i128 v, unsigned w, bool s)
{
   node_t n = { .kind = N_CONST, .width = w, .sgn = s,
                .val = norm(v, w, s) };
   return add_node(c, n);
}

static crv_node_t mk_fail(crv_t *c)
{
   node_t n = { .kind = N_FAIL, .width = 1 };
   return add_node(c, n);
}

crv_var_t crv_var(crv_t *c, unsigned width, bool is_signed, int64_t current)
{
   width = clampw(width);
   cvar_t v = { .width = width, .sgn = is_signed,
                .cur = norm(mathv(current, is_signed), width, is_signed) };
   VEC_PUSH(c->vars, c->nvars, c->cvars, v);
   return c->nvars - 1;
}

void crv_var_randc(crv_t *c, crv_var_t v, uint64_t *state)
{
   cvar_t *cv = &c->vars[v];
   if (state == NULL)
      return;

   if (cv->width > 16) {
      if (*state == 0)
         *state = (uint64_t)(uintptr_t)xcalloc(sizeof(randc_t));
      cv->rc = state;
      return;
   }

   const uint32_t n = UINT32_C(1) << cv->width;
   if (*state == 0) {
      randc_t *rc = xmalloc(sizeof(randc_t) + 2 * n * sizeof(uint32_t));
      rc->n = rc->left = n;
      for (uint32_t i = 0; i < n; i++)
         rc->vals[i] = rc->vals[n + i] = i;
      *state = (uint64_t)(uintptr_t)rc;
   }
   cv->rc = state;
}

void crv_var_domain(crv_t *c, crv_var_t v, int64_t value)
{
   cvar_t *cv = &c->vars[v];
   VEC_PUSH(cv->dom, cv->ndom, cv->cdom,
            norm(mathv(value, cv->sgn), cv->width, cv->sgn));
}

crv_array_t crv_array(crv_t *c, unsigned elem_width, bool is_signed,
                      unsigned current_size, bool size_rand, bool elems_rand)
{
   carr_t a = { .width = clampw(elem_width), .sgn = is_signed,
                .size_rand = size_rand, .elems_rand = elems_rand,
                .len = current_size, .ninit = current_size };
   a.init = xcalloc((current_size ? current_size : 1) * sizeof(int64_t));
   VEC_PUSH(c->arrays, c->narrays, c->carrays, a);
   return c->narrays - 1;
}

void crv_array_init(crv_t *c, crv_array_t a, unsigned index, int64_t value)
{
   carr_t *ar = &c->arrays[a];
   if (index >= ar->ninit) {
      if (index >= (unsigned)MAX_SIZE * 4)
         return;
      ar->init = xrealloc(ar->init, (index + 1) * sizeof(int64_t));
      memset(ar->init + ar->ninit, 0, (index + 1 - ar->ninit) * 8);
      ar->ninit = index + 1;
   }
   ar->init[index] = norm(mathv(value, ar->sgn), ar->width, ar->sgn);
}

bool crv_array_resizable(crv_t *c, crv_array_t a)
{
   return c->arrays[a].size_rand;
}

crv_node_t crv_const(crv_t *c, int64_t value, unsigned width, bool is_signed)
{
   width = clampw(width);
   return mk_const(c, mathv(value, is_signed), width, is_signed);
}

crv_node_t crv_var_ref(crv_t *c, crv_var_t v)
{
   node_t n = { .kind = N_VAR, .width = c->vars[v].width,
                .sgn = c->vars[v].sgn, .aux = v };
   return add_node(c, n);
}

crv_node_t crv_binary(crv_t *c, crv_binary_t op, crv_node_t l, crv_node_t r,
                      unsigned width, bool is_signed)
{
   node_t n = { .kind = N_BIN, .op = op, .width = clampw(width),
                .sgn = is_signed, .a = l, .b = r,
                .inst = c->nodes[l].inst || c->nodes[r].inst };
   if (op >= CRV_EQ)
      n.sgn = false;
   return add_node(c, n);
}

crv_node_t crv_unary(crv_t *c, crv_unary_t op, crv_node_t arg)
{
   const node_t *a = &c->nodes[arg];
   node_t n = { .kind = N_UN, .op = op, .a = arg, .inst = a->inst };
   if (op == CRV_NEG || op == CRV_INV) {
      n.width = a->width;
      n.sgn = a->sgn;
   }
   else if (op == CRV_POPCNT) {
      n.width = 32;
      n.sgn = true;
   }
   else
      n.width = 1;
   return add_node(c, n);
}

crv_node_t crv_cast(crv_t *c, crv_node_t arg, unsigned width, bool is_signed)
{
   node_t n = { .kind = N_CAST, .width = clampw(width), .sgn = is_signed,
                .a = arg, .inst = c->nodes[arg].inst };
   return add_node(c, n);
}

crv_node_t crv_cond(crv_t *c, crv_node_t test, crv_node_t t, crv_node_t f)
{
   node_t n = { .kind = N_COND, .width = c->nodes[t].width,
                .sgn = c->nodes[t].sgn, .a = test, .b = t, .c = f,
                .inst = c->nodes[test].inst || c->nodes[t].inst
                || c->nodes[f].inst };
   return add_node(c, n);
}

crv_node_t crv_array_size(crv_t *c, crv_array_t a)
{
   node_t n = { .kind = N_ASIZE, .width = 32, .sgn = true, .aux = a };
   return add_node(c, n);
}

crv_node_t crv_array_elem(crv_t *c, crv_array_t a, crv_node_t index)
{
   node_t n = { .kind = N_AELEM, .width = c->arrays[a].width,
                .sgn = c->arrays[a].sgn, .a = index, .aux = a,
                .inst = true };
   return add_node(c, n);
}

crv_node_t crv_array_sum(crv_t *c, crv_array_t a, unsigned width,
                         bool is_signed)
{
   node_t n = { .kind = N_ASUM, .width = clampw(width), .sgn = is_signed,
                .aux = a };
   return add_node(c, n);
}

crv_set_t crv_set_new(crv_t *c)
{
   cset_t s = { 0 };
   VEC_PUSH(c->sets, c->nsets, c->csets, s);
   return c->nsets - 1;
}

void crv_set_value(crv_t *c, crv_set_t s, crv_node_t value)
{
   cset_t *cs = &c->sets[s];
   item_t it = { value, -1 };
   VEC_PUSH(cs->items, cs->n, cs->cap, it);
   cs->prep = false;
}

void crv_set_range(crv_t *c, crv_set_t s, crv_node_t lo, crv_node_t hi)
{
   cset_t *cs = &c->sets[s];
   item_t it = { lo, hi };
   VEC_PUSH(cs->items, cs->n, cs->cap, it);
   cs->prep = false;
}

crv_node_t crv_inside(crv_t *c, crv_node_t arg, crv_set_t s)
{
   node_t n = { .kind = N_INSIDE, .width = 1, .a = arg, .aux = s,
                .inst = c->nodes[arg].inst };
   const cset_t *cs = &c->sets[s];
   for (unsigned i = 0; i < cs->n; i++) {
      if (c->nodes[cs->items[i].lo].inst
          || (cs->items[i].hi >= 0 && c->nodes[cs->items[i].hi].inst))
         n.inst = true;
   }
   return add_node(c, n);
}

crv_dist_t crv_dist_new(crv_t *c)
{
   cdist_t d = { 0 };
   VEC_PUSH(c->dists, c->ndists, c->cdists, d);
   return c->ndists - 1;
}

void crv_dist_range(crv_t *c, crv_dist_t d, int64_t lo, int64_t hi,
                    int64_t weight, bool per_value)
{
   cdist_t *cd = &c->dists[d];
   drange_t r = { lo, hi, weight, per_value };
   VEC_PUSH(cd->r, cd->n, cd->cap, r);
   cd->prep = false;
}

crv_node_t crv_dist(crv_t *c, crv_node_t arg, crv_dist_t d)
{
   node_t n = { .kind = N_DIST, .width = 1, .a = arg, .aux = d,
                .inst = c->nodes[arg].inst };
   c->dists[d].sgn = c->nodes[arg].sgn;
   c->dists[d].prep = false;
   return add_node(c, n);
}

void crv_require(crv_t *c, crv_node_t test, bool soft, int line)
{
   ccons_t k = { .kind = C_REQ, .root = test,
                 .soft = soft ? c->nsoft++ : -1, .line = line };
   VEC_PUSH(c->cons, c->ncons, c->ccons, k);
}

void crv_unique(crv_t *c, const crv_node_t *nodes, unsigned count)
{
   ccons_t k = { .kind = C_UNIQUE, .soft = -1, .first = c->nuniq,
                 .count = count };
   for (unsigned i = 0; i < count; i++)
      VEC_PUSH(c->uniq, c->nuniq, c->cuniq, nodes[i]);
   VEC_PUSH(c->cons, c->ncons, c->ccons, k);
}

crv_node_t crv_index(crv_t *c)
{
   node_t n = { .kind = N_INDEX, .width = 32, .sgn = true,
                .aux = c->nindex++, .inst = true };
   return add_node(c, n);
}

void crv_foreach(crv_t *c, crv_array_t a, crv_node_t index, crv_node_t body,
                 bool soft)
{
   ccons_t k = { .kind = C_FOREACH, .root = body, .arr = a, .idx = index,
                 .soft = -1, .nsoft = 1 };
   if (soft) {
      // Each element is a separate soft constraint
      const carr_t *ar = &c->arrays[a];
      k.soft = c->nsoft;
      k.nsoft = ar->size_rand || ar->len == 0 ? 1 : ar->len;
      c->nsoft += k.nsoft;
      c->greedy |= k.nsoft > 1;
   }
   VEC_PUSH(c->cons, c->ncons, c->ccons, k);
}

void crv_solve_before(crv_t *c, crv_var_t before, crv_var_t after)
{
   edge_t e = { false, before, after };
   VEC_PUSH(c->edges, c->nedges, c->cedges, e);
}

void crv_array_solve_before(crv_t *c, crv_array_t before, crv_var_t after)
{
   edge_t e = { true, before, after };
   VEC_PUSH(c->edges, c->nedges, c->cedges, e);
}

int64_t crv_value(crv_t *c, crv_var_t v)
{
   return c->vars[v].cur;
}

unsigned crv_array_len(crv_t *c, crv_array_t a)
{
   return c->arrays[a].len;
}

int64_t crv_array_value(crv_t *c, crv_array_t a, unsigned index)
{
   const carr_t *ar = &c->arrays[a];
   return index < ar->ninit && index < ar->len ? ar->init[index] : 0;
}

////////////////////////////////////////////////////////////////////////////////
// Concrete evaluation

typedef struct {
   const crv_t    *c;
   const int64_t  *vals;
   int             nsc;
   const int      *ebase;
   const unsigned *sizes;
   bool            fail;
} ectx_t;

static int64_t ev_bin(const node_t *nd, const node_t *an, const node_t *bn,
                      int64_t l, int64_t r, bool *fail)
{
   const unsigned w = nd->width;
   const bool s = nd->sgn;
   const uint64_t ul = (uint64_t)l, ur = (uint64_t)r;

   switch ((crv_binary_t)nd->op) {
   case CRV_ADD: return norm(ul + ur, w, s);
   case CRV_SUB: return norm(ul - ur, w, s);
   case CRV_MUL: return norm(ul * ur, w, s);
   case CRV_AND: return norm(ul & ur, w, s);
   case CRV_OR: return norm(ul | ur, w, s);
   case CRV_XOR: return norm(ul ^ ur, w, s);
   case CRV_DIV:
   case CRV_MOD:
      {
         const i128 x = mathv(l, an->sgn), y = mathv(r, bn->sgn);
         if (y == 0) {
            *fail = true;
            return 0;
         }
         return norm(nd->op == CRV_DIV ? x / y : x % y, w, s);
      }
   case CRV_SHL:
   case CRV_SHR:
   case CRV_ASR:
      {
         const uint64_t cnt = ur & wmask(bn->width);
         if (cnt >= 64) {
            if (nd->op == CRV_ASR && s && l < 0)
               return norm(-1, w, s);
            return 0;
         }
         else if (nd->op == CRV_SHL)
            return norm(ul << cnt, w, s);
         else if (nd->op == CRV_ASR && s)
            return norm(l >> cnt, w, s);
         else
            return norm((ul & wmask(w)) >> cnt, w, s);
      }
   case CRV_EQ: return mathv(l, an->sgn) == mathv(r, bn->sgn);
   case CRV_NE: return mathv(l, an->sgn) != mathv(r, bn->sgn);
   case CRV_LT: return mathv(l, an->sgn) < mathv(r, bn->sgn);
   case CRV_LE: return mathv(l, an->sgn) <= mathv(r, bn->sgn);
   case CRV_GT: return mathv(l, an->sgn) > mathv(r, bn->sgn);
   case CRV_GE: return mathv(l, an->sgn) >= mathv(r, bn->sgn);
   default: should_not_reach_here();
   }
}

static int64_t ev_un(const node_t *nd, const node_t *an, int64_t v)
{
   const uint64_t u = (uint64_t)v;
   switch ((crv_unary_t)nd->op) {
   case CRV_NEG: return norm(-u, nd->width, nd->sgn);
   case CRV_INV: return norm(~u, nd->width, nd->sgn);
   case CRV_LOG_NOT: return v == 0;
   case CRV_REDUCE_AND: return (u & wmask(an->width)) == wmask(an->width);
   case CRV_REDUCE_OR: return (u & wmask(an->width)) != 0;
   case CRV_REDUCE_XOR:
      return __builtin_popcountll(u & wmask(an->width)) & 1;
   case CRV_POPCNT:
      return __builtin_popcountll(u & wmask(an->width));
   default: should_not_reach_here();
   }
}

static int64_t ev(ectx_t *E, int n)
{
   const node_t *nodes = E->c->nodes;
   const node_t *nd = &nodes[n];

   switch ((nkind_t)nd->kind) {
   case N_CONST:
      return nd->val;
   case N_VAR:
      return E->vals[nd->aux];
   case N_ASIZE:
      return E->vals[E->nsc + nd->aux];
   case N_BIN:
      {
         switch ((crv_binary_t)nd->op) {
         case CRV_LOG_AND:
         case CRV_LOG_OR:
         case CRV_IMPLIES:
            {
               // A failed operand is X: X || 1 and X -> 1 are 1, X && 0 is 0
               const crv_binary_t op = nd->op;
               const bool saved = E->fail;
               E->fail = false;

               int64_t l = ev(E, nd->a);
               const bool lx = E->fail;
               E->fail = false;

               const int64_t r = ev(E, nd->b);
               const bool rx = E->fail;
               E->fail = saved;

               if (op == CRV_IMPLIES)
                  l = (l == 0);

               if (op == CRV_LOG_AND) {
                  if ((!lx && l == 0) || (!rx && r == 0))
                     return 0;
               }
               else if ((!lx && l != 0) || (!rx && r != 0))
                  return 1;

               if (lx || rx) {
                  E->fail = true;
                  return 0;
               }

               return op == CRV_LOG_AND;
            }
         default:
            break;
         }

         const int64_t l = ev(E, nd->a);
         if (E->fail)
            return 0;
         switch ((crv_binary_t)nd->op) {
         default:
            {
               const int64_t r = ev(E, nd->b);
               if (E->fail)
                  return 0;
               return ev_bin(nd, &nodes[nd->a], &nodes[nd->b], l, r,
                             &E->fail);
            }
         }
      }
   case N_UN:
      {
         const int64_t v = ev(E, nd->a);
         return E->fail ? 0 : ev_un(nd, &nodes[nd->a], v);
      }
   case N_CAST:
      {
         const int64_t v = ev(E, nd->a);
         return norm(mathv(v, nodes[nd->a].sgn), nd->width, nd->sgn);
      }
   case N_COND:
      {
         const int64_t t = ev(E, nd->a);
         if (E->fail)
            return 0;
         return ev(E, t ? nd->b : nd->c);
      }
   case N_AELEM:
      {
         const int64_t i = ev(E, nd->a);
         if (E->fail)
            return 0;
         const i128 x = mathv(i, nodes[nd->a].sgn);
         if (x < 0 || x >= E->sizes[nd->aux]) {
            E->fail = true;
            return 0;
         }
         return E->vals[E->ebase[nd->aux] + (int)x];
      }
   case N_ASUM:
      {
         uint64_t sum = 0;
         for (unsigned i = 0; i < E->sizes[nd->aux]; i++)
            sum += (uint64_t)E->vals[E->ebase[nd->aux] + i];
         return norm(sum, nd->width, nd->sgn);
      }
   case N_INSIDE:
      {
         const cset_t *cs = &E->c->sets[nd->aux];
         const i128 x = mathv(ev(E, nd->a), nodes[nd->a].sgn);
         if (E->fail)
            return 0;
         for (unsigned i = 0; i < cs->n; i++) {
            const item_t it = cs->items[i];
            const i128 lo = mathv(ev(E, it.lo), nodes[it.lo].sgn);
            if (E->fail)
               return 0;
            else if (it.hi < 0) {
               if (x == lo)
                  return 1;
            }
            else {
               const i128 hi = mathv(ev(E, it.hi), nodes[it.hi].sgn);
               if (E->fail)
                  return 0;
               else if (x >= lo && x <= hi)
                  return 1;
            }
         }
         return 0;
      }
   case N_DIST:
      {
         const cdist_t *cd = &E->c->dists[nd->aux];
         const i128 x = mathv(ev(E, nd->a), nodes[nd->a].sgn);
         if (E->fail)
            return 0;
         for (unsigned i = 0; i < cd->nmr; i++) {
            if (x >= cd->mr[i].lo && x <= cd->mr[i].hi)
               return 1;
         }
         return 0;
      }
   default:
      E->fail = true;
      return 0;
   }
}

////////////////////////////////////////////////////////////////////////////////
// Sets and distributions

static int range_cmp(const void *a, const void *b)
{
   const range_t *x = a, *y = b;
   return x->lo < y->lo ? -1 : x->lo > y->lo ? 1 : 0;
}

// Sort and merge overlapping or adjacent ranges
static unsigned rng_merge(range_t *r, unsigned n)
{
   if (n < 2)
      return n;

   qsort(r, n, sizeof(range_t), range_cmp);

   unsigned k = 0;
   for (unsigned i = 1; i < n; i++) {
      if (r[i].lo <= r[k].hi + 1) {
         if (r[i].hi > r[k].hi)
            r[k].hi = r[i].hi;
      }
      else
         r[++k] = r[i];
   }
   return k + 1;
}

static void set_prep(crv_t *c, cset_t *cs)
{
   if (cs->prep)
      return;

   free(cs->rr);
   cs->rr = NULL;
   cs->nrr = 0;
   cs->allconst = true;
   for (unsigned i = 0; i < cs->n; i++) {
      if (c->nodes[cs->items[i].lo].kind != N_CONST
          || (cs->items[i].hi >= 0
              && c->nodes[cs->items[i].hi].kind != N_CONST))
         cs->allconst = false;
   }

   if (cs->allconst) {
      cs->rr = NEW(range_t, cs->n);
      for (unsigned i = 0; i < cs->n; i++) {
         const node_t *lo = &c->nodes[cs->items[i].lo];
         range_t r = { mathv(lo->val, lo->sgn), 0 };
         if (cs->items[i].hi < 0)
            r.hi = r.lo;
         else {
            const node_t *hi = &c->nodes[cs->items[i].hi];
            r.hi = mathv(hi->val, hi->sgn);
         }
         if (r.lo <= r.hi)
            cs->rr[cs->nrr++] = r;
      }
      cs->nrr = rng_merge(cs->rr, cs->nrr);
   }
   cs->prep = true;
}

static void dist_prep(cdist_t *cd)
{
   if (cd->prep)
      return;

   free(cd->mr);
   free(cd->pr);
   free(cd->unit);
   cd->mr = NEW(range_t, cd->n);
   cd->pr = NEW(range_t, cd->n);
   cd->unit = NEW(double, cd->n);
   cd->nmr = cd->npr = 0;

   for (unsigned i = 0; i < cd->n; i++) {
      const drange_t *d = &cd->r[i];
      range_t r = { mathv(d->lo, cd->sgn), mathv(d->hi, cd->sgn) };
      if (d->weight <= 0 || r.lo > r.hi)
         continue;
      cd->mr[cd->nmr++] = r;
      cd->pr[cd->npr] = r;
      cd->unit[cd->npr++] = d->per_value
         ? (double)d->weight : (double)d->weight / (double)(r.hi - r.lo + 1);
   }
   cd->nmr = rng_merge(cd->mr, cd->nmr);
   cd->prep = true;
}

// Is any value of [lo, hi] in the sorted disjoint ranges
static bool rng_overlaps(const range_t *r, unsigned n, i128 lo, i128 hi)
{
   unsigned a = 0, b = n;
   while (a < b) {
      const unsigned m = (a + b) / 2;
      if (r[m].hi >= lo)
         b = m;
      else
         a = m + 1;
   }
   return a < n && r[a].lo <= hi;
}

static bool rng_covers(const range_t *r, unsigned n, i128 lo, i128 hi)
{
   unsigned a = 0, b = n;
   while (a < b) {
      const unsigned m = (a + b) / 2;
      if (r[m].hi >= lo)
         b = m;
      else
         a = m + 1;
   }
   return a < n && r[a].lo <= lo && r[a].hi >= hi;
}

// Smallest value >= v in the ranges or false
static bool rng_next(const range_t *r, unsigned n, i128 v, i128 *out)
{
   unsigned a = 0, b = n;
   while (a < b) {
      const unsigned m = (a + b) / 2;
      if (r[m].hi >= v)
         b = m;
      else
         a = m + 1;
   }
   if (a == n)
      return false;
   *out = r[a].lo > v ? r[a].lo : v;
   return true;
}

static bool rng_prev(const range_t *r, unsigned n, i128 v, i128 *out)
{
   unsigned a = 0, b = n;
   while (a < b) {
      const unsigned m = (a + b) / 2;
      if (r[m].lo > v)
         b = m;
      else
         a = m + 1;
   }
   if (a == 0)
      return false;
   *out = r[a - 1].hi < v ? r[a - 1].hi : v;
   return true;
}

////////////////////////////////////////////////////////////////////////////////
// Arena

typedef struct _chunk {
   struct _chunk *next;
   size_t         used, cap;
   char           data[] __attribute__((aligned(16)));
} chunk_t;

typedef struct { chunk_t *head; } arena_t;
typedef struct { chunk_t *head; size_t used; } amark_t;

static void *arena_alloc(arena_t *a, size_t sz)
{
   sz = (sz + 15) & ~(size_t)15;
   if (a->head == NULL || a->head->used + sz > a->head->cap) {
      const size_t cap = sz > 16384 ? sz : 16384;
      chunk_t *ch = xmalloc(sizeof(chunk_t) + cap);
      ch->next = a->head;
      ch->used = 0;
      ch->cap = cap;
      a->head = ch;
   }
   void *p = a->head->data + a->head->used;
   a->head->used += sz;
   return p;
}

static amark_t arena_mark(const arena_t *a)
{
   return (amark_t){ a->head, a->head ? a->head->used : 0 };
}

static void arena_reset(arena_t *a, amark_t m)
{
   while (a->head != m.head) {
      chunk_t *ch = a->head;
      a->head = ch->next;
      free(ch);
   }
   if (a->head)
      a->head->used = m.used;
}

////////////////////////////////////////////////////////////////////////////////
// Domains: a set of values as a window onto a sorted list of ranges

typedef struct {
   i128           lo, hi;        // Empty if lo > hi
   const range_t *rs;            // NULL if the interval has no holes
   int            i0, i1;
} dom_t;

static inline bool dom_empty(const dom_t *d) { return d->lo > d->hi; }

static inline void dom_set_empty(dom_t *d)
{
   d->lo = 1;
   d->hi = 0;
}

static void dom_norm(dom_t *d)
{
   if (d->lo > d->hi || d->rs == NULL)
      return;

   int a = d->i0, b = d->i1 + 1;
   while (a < b) {
      const int m = (a + b) / 2;
      if (d->rs[m].hi >= d->lo)
         b = m;
      else
         a = m + 1;
   }
   const int i = a;
   if (i > d->i1) {
      dom_set_empty(d);
      return;
   }

   a = i;
   b = d->i1 + 1;
   while (a < b) {
      const int m = (a + b) / 2;
      if (d->rs[m].lo > d->hi)
         b = m;
      else
         a = m + 1;
   }
   const int j = a - 1;
   if (j < i) {
      dom_set_empty(d);
      return;
   }

   if (d->lo < d->rs[i].lo) d->lo = d->rs[i].lo;
   if (d->hi > d->rs[j].hi) d->hi = d->rs[j].hi;
   d->i0 = i;
   d->i1 = j;
}

static dom_t dom_interval(i128 lo, i128 hi)
{
   return (dom_t){ lo, hi, NULL, 0, 0 };
}

static dom_t dom_ranges(const range_t *rs, int n)
{
   if (n == 0)
      return dom_interval(1, 0);
   else if (n == 1)
      return dom_interval(rs[0].lo, rs[0].hi);
   return (dom_t){ rs[0].lo, rs[n - 1].hi, rs, 0, n - 1 };
}

static inline int dom_nr(const dom_t *d)
{
   return d->rs ? d->i1 - d->i0 + 1 : 1;
}

static range_t dom_range(const dom_t *d, int k)
{
   if (d->rs == NULL)
      return (range_t){ d->lo, d->hi };

   range_t r = d->rs[d->i0 + k];
   if (k == 0 && r.lo < d->lo) r.lo = d->lo;
   if (d->i0 + k == d->i1 && r.hi > d->hi) r.hi = d->hi;
   return r;
}

// Number of values in the domain that are in [a, b]
static i128 dom_count(const dom_t *d, i128 a, i128 b)
{
   if (a < d->lo) a = d->lo;
   if (b > d->hi) b = d->hi;
   if (a > b)
      return 0;
   else if (d->rs == NULL)
      return b - a + 1;

   i128 n = 0;
   for (int k = d->i0; k <= d->i1; k++) {
      if (d->rs[k].lo > b)
         break;
      const i128 lo = d->rs[k].lo > a ? d->rs[k].lo : a;
      const i128 hi = d->rs[k].hi < b ? d->rs[k].hi : b;
      if (lo <= hi)
         n += hi - lo + 1;
   }
   return n;
}

static i128 dom_nth(const dom_t *d, i128 a, i128 b, i128 n)
{
   if (a < d->lo) a = d->lo;
   if (b > d->hi) b = d->hi;
   if (d->rs == NULL)
      return a + n;

   for (int k = d->i0; k <= d->i1; k++) {
      const i128 lo = d->rs[k].lo > a ? d->rs[k].lo : a;
      const i128 hi = d->rs[k].hi < b ? d->rs[k].hi : b;
      if (lo > hi)
         continue;
      else if (n < hi - lo + 1)
         return lo + n;
      n -= hi - lo + 1;
   }
   return d->lo;
}

static bool dom_has(const dom_t *d, i128 v)
{
   if (v < d->lo || v > d->hi)
      return false;
   else if (d->rs == NULL)
      return true;

   int a = d->i0, b = d->i1 + 1;
   while (a < b) {
      const int m = (a + b) / 2;
      if (d->rs[m].hi >= v)
         b = m;
      else
         a = m + 1;
   }
   return a <= d->i1 && d->rs[a].lo <= v;
}

static bool dom_clip(dom_t *d, i128 a, i128 b)
{
   if (a <= d->lo && b >= d->hi)
      return false;
   if (a > d->lo) d->lo = a;
   if (b < d->hi) d->hi = b;
   dom_norm(d);
   return true;
}

static bool dom_remove(dom_t *d, i128 v, arena_t *ar)
{
   if (v < d->lo || v > d->hi)
      return false;
   else if (d->lo == d->hi) {
      dom_set_empty(d);
      return true;
   }
   else if (v == d->lo) {
      d->lo = v + 1;
      dom_norm(d);
      return true;
   }
   else if (v == d->hi) {
      d->hi = v - 1;
      dom_norm(d);
      return true;
   }
   else if (d->rs == NULL) {
      range_t *r = arena_alloc(ar, 2 * sizeof(range_t));
      r[0] = (range_t){ d->lo, v - 1 };
      r[1] = (range_t){ v + 1, d->hi };
      d->rs = r;
      d->i0 = 0;
      d->i1 = 1;
      return true;
   }
   else if (!dom_has(d, v))
      return false;

   const int nr = dom_nr(d);
   range_t *r = arena_alloc(ar, (nr + 1) * sizeof(range_t));
   int n = 0;
   for (int k = 0; k < nr; k++) {
      const range_t x = dom_range(d, k);
      if (v >= x.lo && v <= x.hi) {
         if (x.lo < v) r[n++] = (range_t){ x.lo, v - 1 };
         if (v < x.hi) r[n++] = (range_t){ v + 1, x.hi };
      }
      else
         r[n++] = x;
   }
   d->rs = r;
   d->i0 = 0;
   d->i1 = n - 1;
   return true;
}

// Intersect with, or subtract, a sorted list of disjoint ranges
static int rng_op(const dom_t *d, const range_t *rr, int n, bool inter,
                  range_t *out)
{
   const int nr = dom_nr(d);
   int m = 0, j = 0;

   if (inter) {
      for (int i = 0; i < nr && j < n; ) {
         const range_t x = dom_range(d, i);
         const range_t y = rr[j];
         const i128 lo = x.lo > y.lo ? x.lo : y.lo;
         const i128 hi = x.hi < y.hi ? x.hi : y.hi;
         if (lo <= hi)
            out[m++] = (range_t){ lo, hi };
         if (x.hi < y.hi)
            i++;
         else
            j++;
      }
   }
   else {
      for (int i = 0; i < nr; i++) {
         const range_t x = dom_range(d, i);
         while (j < n && rr[j].hi < x.lo)
            j++;
         i128 cur = x.lo;
         for (int k = j; k < n && rr[k].lo <= x.hi; k++) {
            if (rr[k].lo > cur)
               out[m++] = (range_t){ cur, rr[k].lo - 1 };
            if (rr[k].hi + 1 > cur)
               cur = rr[k].hi + 1;
            if (cur > x.hi)
               break;
         }
         if (cur <= x.hi)
            out[m++] = (range_t){ cur, x.hi };
      }
   }
   return m;
}

////////////////////////////////////////////////////////////////////////////////
// The problem being solved

typedef struct {
   dom_t     dom;
   unsigned  width;
   bool      sgn, size;
   int       dist, pop;
   int      *gd;
   int       ngd;
   i128     *tabu;
   int       ntabu, ctabu;
   randc_t  *rc;
   unsigned  level;
   int64_t   cur;
   bool      split, derived;
} pvar_t;

typedef struct {
   ckind_t kind;
   int     root;
   int     soft;
   int     unstart, unn;
   int     line;
} pcons_t;

typedef struct {
   int     var;
   dom_t   old;
} trail_t;

// Debugging switches: read once, not on every call
static struct {
   int         level;      // NVC_CRV_DEBUG: 0 off, 1 report, 2 list constraints
   const char *skip;       // NVC_CRV_SKIP
   int64_t     budget;     // NVC_CRV_BUDGET: work limit of a search, for tests
   bool        read;
} dbg;

static void read_debug_env(void)
{
   const char *d = getenv("NVC_CRV_DEBUG");
   dbg.level = d == NULL ? 0 : !strcmp(d, "2") ? 2 : 1;
   dbg.skip = getenv("NVC_CRV_SKIP");
   dbg.budget = getenv("NVC_CRV_BUDGET") ? atoll(getenv("NVC_CRV_BUDGET")) : 0;
   dbg.read = true;
}

static inline int debug_level(void)
{
   if (unlikely(!dbg.read))
      read_debug_env();
   return dbg.level;
}

#define MAX_FIELDS 32

// A variable which is split into fields at the boundaries of the bit
// slices of it that are used in constraints
typedef struct dec_s {
   int      var, f0, nf;
   unsigned width;
   uint8_t  pos[MAX_FIELDS + 1];
} dec_t;

typedef struct {
   crv_t    *c;
   int       nsc, na, nv;
   int      *ebase;
   unsigned *sizes;
   pvar_t   *vars;
   pcons_t  *cons;
   int       ncons, ccons;
   int      *un;
   uint8_t  *uproc;
   int       nun, cun;
   int      *vl, nvl, cvl;
   int      *vstart;
   int      *vmark;
   int      *wstart, *wlist;
   int       nn;
   ivl_t    *iv;
   uint32_t *stamp, pass;
   uint64_t *pend;
   int       qn;
   int       curc;
   trail_t  *trail;
   int       ntrail, ctrail;
   arena_t   ar, tmp;
   int64_t   work, budget;
   uint64_t  changed;
   struct tabu_s { int var, level; } *tb;
   int       ntb, ctb;
   bool      oob;
   int64_t  *vals;
   unsigned  base_nodes, base_sets;
   struct dec_s *dec;
   int       ndec;
} prob_t;

typedef struct {
   crv_t          *c;
   const int      *ebase;
   const unsigned *sizes;
   int             nsc;
   int             nb;
   int             bidx[MAX_BIND];
   int64_t         bval[MAX_BIND];
   bool            toobig;
} ictx_t;

typedef void (*visit_fn)(void *ctx, const node_t *nd, int n);

static void visit(const crv_t *c, int n, visit_fn fn, void *ctx)
{
   const node_t *nd = &c->nodes[n];
   fn(ctx, nd, n);

   switch ((nkind_t)nd->kind) {
   case N_BIN:
      visit(c, nd->a, fn, ctx);
      visit(c, nd->b, fn, ctx);
      break;
   case N_UN:
   case N_CAST:
   case N_AELEM:
   case N_DIST:
      visit(c, nd->a, fn, ctx);
      break;
   case N_COND:
      visit(c, nd->a, fn, ctx);
      visit(c, nd->b, fn, ctx);
      visit(c, nd->c, fn, ctx);
      break;
   case N_INSIDE:
      {
         visit(c, nd->a, fn, ctx);
         const cset_t *cs = &c->sets[nd->aux];
         for (unsigned i = 0; i < cs->n; i++) {
            visit(c, cs->items[i].lo, fn, ctx);
            if (cs->items[i].hi >= 0)
               visit(c, cs->items[i].hi, fn, ctx);
         }
      }
      break;
   default:
      break;
   }
}

typedef struct {
   bool     *vars;
   unsigned  count;
   bool      found;
} mention_t;

static void collect_vars_cb(void *ctx, const node_t *nd, int n)
{
   mention_t *m = ctx;
   if (nd->kind == N_VAR && nd->aux >= 0 && nd->aux < m->count)
      m->vars[nd->aux] = true;
}

static void mention_vars_cb(void *ctx, const node_t *nd, int n)
{
   mention_t *m = ctx;
   if (nd->kind == N_VAR && nd->aux >= 0 && nd->aux < m->count
       && m->vars[nd->aux])
      m->found = true;
}

void crv_disable_soft(crv_t *c, crv_node_t target)
{
   // Discard the soft constraints added so far which refer to a variable

   if (c->nvars == 0)
      return;

   mention_t m = { .vars = xcalloc(c->nvars), .count = c->nvars };
   visit(c, target, collect_vars_cb, &m);

   for (unsigned i = 0; i < c->ncons; i++) {
      ccons_t *cc = &(c->cons[i]);
      if (cc->soft < 0 || cc->dead)
         continue;

      m.found = false;
      visit(c, cc->root, mention_vars_cb, &m);
      cc->dead |= m.found;
   }

   free(m.vars);
}

static int inst(ictx_t *I, int n)
{
   crv_t *c = I->c;
   if (!c->nodes[n].inst)
      return n;
   else if (c->nnodes > MAX_NODES) {
      I->toobig = true;
      return n;
   }

   node_t nd = c->nodes[n];
   nd.inst = false;

   switch ((nkind_t)nd.kind) {
   case N_INDEX:
      for (int k = 0; k < I->nb; k++) {
         if (I->bidx[k] == nd.aux)
            return mk_const(c, I->bval[k], 32, true);
      }
      return mk_const(c, 0, 32, true);

   case N_AELEM:
      {
         const int a = inst(I, nd.a);
         const node_t *an = &c->nodes[a];
         if (an->kind == N_CONST) {
            const i128 i = mathv(an->val, an->sgn);
            if (i < 0 || i >= I->sizes[nd.aux])
               return mk_fail(c);
            node_t v = { .kind = N_VAR, .width = nd.width, .sgn = nd.sgn,
                         .aux = I->ebase[nd.aux] + (int)i };
            return add_node(c, v);
         }
         nd.a = a;
         return add_node(c, nd);
      }

   case N_BIN:
      {
         nd.a = inst(I, nd.a);
         nd.b = inst(I, nd.b);
         const bool fold = c->nodes[nd.a].kind == N_CONST
            && c->nodes[nd.b].kind == N_CONST;
         const int m = add_node(c, nd);
         if (!fold)
            return m;

         ectx_t E = { .c = c };
         const int64_t v = ev(&E, m);
         return E.fail ? mk_fail(c) : mk_const(c, mathv(v, nd.sgn),
                                               nd.width, nd.sgn);
      }

   case N_UN:
   case N_CAST:
      {
         nd.a = inst(I, nd.a);
         const bool fold = c->nodes[nd.a].kind == N_CONST;
         const int m = add_node(c, nd);
         if (!fold)
            return m;

         ectx_t E = { .c = c };
         const int64_t v = ev(&E, m);
         return mk_const(c, mathv(v, nd.sgn), nd.width, nd.sgn);
      }

   case N_COND:
      {
         const int t = inst(I, nd.a);
         if (c->nodes[t].kind == N_CONST)
            return inst(I, c->nodes[t].val != 0 ? nd.b : nd.c);
         nd.a = t;
         nd.b = inst(I, nd.b);
         nd.c = inst(I, nd.c);
         return add_node(c, nd);
      }

   case N_INSIDE:
      {
         nd.a = inst(I, nd.a);
         cset_t ns = { 0 };
         const unsigned cnt = c->sets[nd.aux].n;
         for (unsigned i = 0; i < cnt; i++) {
            item_t it = c->sets[nd.aux].items[i];
            it.lo = inst(I, it.lo);
            if (it.hi >= 0)
               it.hi = inst(I, it.hi);
            VEC_PUSH(ns.items, ns.n, ns.cap, it);
         }
         VEC_PUSH(c->sets, c->nsets, c->csets, ns);
         nd.aux = c->nsets - 1;
         return add_node(c, nd);
      }

   case N_DIST:
      nd.a = inst(I, nd.a);
      return add_node(c, nd);

   default:
      return n;
   }
}

typedef struct {
   int ids[MAX_BIND];
   int n;
} idxlist_t;

static void idx_cb(void *ctx, const node_t *nd, int n)
{
   (void)n;
   idxlist_t *l = ctx;
   if (nd->kind != N_INDEX || l->n == MAX_BIND)
      return;
   for (int i = 0; i < l->n; i++) {
      if (l->ids[i] == nd->aux)
         return;
   }
   l->ids[l->n++] = nd->aux;
}

typedef struct {
   bool bad;
   const crv_t *c;
} crv_rsize_t;

static void rsize_cb(void *ctx, const node_t *nd, int n)
{
   (void)n;
   crv_rsize_t *r = ctx;
   if ((nd->kind == N_AELEM || nd->kind == N_ASUM)
       && r->c->arrays[nd->aux].size_rand)
      r->bad = true;
}

static void add_pcons(prob_t *P, ckind_t kind, int root, int soft, int line)
{
   pcons_t pc = { .kind = kind, .root = root, .soft = soft, .line = line };
   VEC_PUSH(P->cons, P->ncons, P->ccons, pc);
}

static void expand_foreach(prob_t *P, ictx_t *I, const ccons_t *f,
                           const bool *active)
{
   crv_t *c = P->c;

   idxlist_t l = { .n = 0 };
   visit(c, f->root, idx_cb, &l);

   int own = c->nodes[f->idx].aux;
   int ids[MAX_BIND];
   int arrs[MAX_BIND];
   int nd = 0, soft = f->soft;
   ids[nd] = own;
   arrs[nd++] = f->arr;

   for (int i = 0; i < l.n; i++) {
      if (l.ids[i] == own)
         continue;
      for (unsigned k = 0; k < c->ncons; k++) {
         const ccons_t *o = &c->cons[k];
         if (o->kind == C_FOREACH && c->nodes[o->idx].aux == l.ids[i]
             && nd < MAX_BIND) {
            ids[nd] = l.ids[i];
            arrs[nd++] = o->arr;
            if (soft < 0)
               soft = o->soft;
            break;
         }
      }
   }

   for (int i = 0; i < nd; i++) {
      if (P->sizes[arrs[i]] == 0)
         return;
   }

   unsigned cnt[MAX_BIND] = { 0 };
   for (;;) {
      for (int i = 0; i < nd; i++) {
         I->bidx[i] = ids[i];
         I->bval[i] = cnt[i];
      }
      I->nb = nd;

      int sid = soft;
      if (sid >= 0 && sid == f->soft && nd == 1)
         sid += MIN(cnt[0], f->nsoft - 1);

      if (sid < 0 || active[sid])
         add_pcons(P, C_REQ, inst(I, f->root), sid, f->line);
      if (I->toobig)
         return;

      int i = 0;
      while (i < nd && ++cnt[i] == P->sizes[arrs[i]])
         cnt[i++] = 0;
      if (i == nd)
         break;
   }
}

typedef struct {
   prob_t *P;
   int     ci;
} vctx_t;

static void add_var(vctx_t *V, int v)
{
   prob_t *P = V->P;
   if (P->vmark[v] != V->ci + 1) {
      P->vmark[v] = V->ci + 1;
      VEC_PUSH(P->vl, P->nvl, P->cvl, v);
   }
}

static void vars_cb(void *ctx, const node_t *nd, int n)
{
   (void)n;
   vctx_t *V = ctx;
   const prob_t *P = V->P;

   switch ((nkind_t)nd->kind) {
   case N_VAR:
      add_var(V, nd->aux);
      break;
   case N_ASIZE:
      add_var(V, P->nsc + nd->aux);
      break;
   case N_ASUM:
   case N_AELEM:
      if (P->ebase[nd->aux] >= 0) {
         for (unsigned i = 0; i < P->sizes[nd->aux]; i++)
            add_var(V, P->ebase[nd->aux] + i);
      }
      break;
   default:
      break;
   }
}

static dom_t type_dom(unsigned w, bool s)
{
   return dom_interval(tmin(w, s), tmax(w, s));
}

static int i64_cmp(const void *a, const void *b)
{
   const int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;
   return x < y ? -1 : x > y ? 1 : 0;
}

static dom_t scalar_dom(prob_t *P, const cvar_t *cv)
{
   dom_t d = type_dom(cv->width, cv->sgn);
   if (cv->ndom == 0)
      return d;

   int64_t *tmp = NEW(int64_t, cv->ndom);
   memcpy(tmp, cv->dom, cv->ndom * sizeof(int64_t));
   if (cv->sgn)
      qsort(tmp, cv->ndom, sizeof(int64_t), i64_cmp);
   else {
      // Unsigned values compare as such
      for (unsigned i = 0; i < cv->ndom; i++)
         tmp[i] ^= INT64_MIN;
      qsort(tmp, cv->ndom, sizeof(int64_t), i64_cmp);
      for (unsigned i = 0; i < cv->ndom; i++)
         tmp[i] ^= INT64_MIN;
   }

   range_t *rs = arena_alloc(&P->ar, cv->ndom * sizeof(range_t));
   for (unsigned i = 0; i < cv->ndom; i++)
      rs[i] = (range_t){ mathv(tmp[i], cv->sgn), mathv(tmp[i], cv->sgn) };
   free(tmp);

   const unsigned n = rng_merge(rs, cv->ndom);
   return dom_ranges(rs, n);
}

static void prob_free(prob_t *P)
{
   crv_t *c = P->c;
   for (unsigned i = P->base_sets; i < c->nsets; i++) {
      free(c->sets[i].items);
      free(c->sets[i].rr);
   }
   c->nsets = P->base_sets;
   c->nnodes = P->base_nodes;

   free(P->dec);
   free(P->ebase);
   free(P->sizes);
   free(P->cons);
   free(P->un);
   free(P->vl);
   free(P->vstart);
   free(P->vmark);
   free(P->wstart);
   free(P->wlist);
   free(P->iv);
   free(P->stamp);
   free(P->pend);
   free(P->uproc);
   for (int v = 0; v < P->nv; v++) {
      free(P->vars[v].tabu);
      free(P->vars[v].gd);
   }
   free(P->tb);
   free(P->vars);
   free(P->trail);
   free(P->vals);
   arena_reset(&P->ar, (amark_t){ NULL, 0 });
   arena_reset(&P->tmp, (amark_t){ NULL, 0 });
   free(P);
}

////////////////////////////////////////////////////////////////////////////////
// Splitting variables into bit fields
//
// Bounds on a variable cannot express that some of its bits are fixed, so
// the constraints on several slices of one variable take a long chain of
// propagation steps, each moving one bound past a single field, and a random
// value is almost never a solution.  Instead each slice becomes a sum of
// independent variables for the fields.

static bool is_const_tree(const crv_t *c, int n)
{
   const node_t *nd = &c->nodes[n];
   switch ((nkind_t)nd->kind) {
   case N_CONST:
      return true;
   case N_BIN:
      return is_const_tree(c, nd->a) && is_const_tree(c, nd->b);
   case N_UN:
   case N_CAST:
      return is_const_tree(c, nd->a);
   default:
      return false;
   }
}

static bool const_of(const crv_t *c, int n, int64_t *val)
{
   if (!is_const_tree(c, n))
      return false;

   ectx_t E = { .c = c };
   const int64_t v = ev(&E, n);
   if (E.fail)
      return false;

   *val = mathv(v, c->nodes[n].sgn);
   return true;
}

static bool slice_of(const crv_t *c, int n, int nsc, int *var, unsigned *lsb,
                     unsigned *w)
{
   const node_t *cn = &c->nodes[n];
   if (cn->kind != N_CAST || cn->sgn)
      return false;

   const node_t *sh = &c->nodes[cn->a];
   if (sh->kind != N_BIN || sh->op != CRV_SHR)
      return false;

   const node_t *xv = &c->nodes[sh->a];
   int64_t k;
   if (xv->kind != N_VAR || xv->aux >= nsc || xv->sgn || xv->width > 64
       || !const_of(c, sh->b, &k) || k < 0 || k >= xv->width)
      return false;

   *var = xv->aux;
   *lsb = k;
   *w = MIN(cn->width, xv->width - (unsigned)k);
   return true;
}

static bool mask_of(const crv_t *c, int n, int nsc, int *var,
                    uint64_t *mask)
{
   // A variable ANDed with a constant: the bits in the mask are separate
   // fields of the variable
   const node_t *nd = &c->nodes[n];
   if (nd->kind != N_BIN || nd->op != CRV_AND || nd->sgn)
      return false;

   int v = nd->a, m = nd->b;
   if (c->nodes[v].kind != N_VAR)
      v = nd->b, m = nd->a;

   const node_t *xv = &c->nodes[v];
   int64_t k;
   if (xv->kind != N_VAR || xv->aux >= nsc || xv->sgn || xv->width > 64
       || xv->width != nd->width || !const_of(c, m, &k))
      return false;

   *var = xv->aux;
   *mask = (uint64_t)k & (xv->width == 64 ? ~UINT64_C(0)
                          : (UINT64_C(1) << xv->width) - 1);
   return true;
}

typedef struct {
   const prob_t *P;
   uint64_t     *cut;
   bool         *bad;
   int          *count;
} dcol_t;

static void dcol_bad(dcol_t *D, int n)
{
   const crv_t *c = D->P->c;
   const node_t *an = &c->nodes[n];
   if (an->kind == N_CAST)
      an = &c->nodes[an->a];
   if (an->kind == N_VAR && an->aux < D->P->nsc)
      D->bad[an->aux] = true;
}

static void dcol_cb(void *ctx, const node_t *nd, int n)
{
   dcol_t *D = ctx;
   const crv_t *c = D->P->c;

   int var;
   unsigned lsb, w;
   if (slice_of(c, n, D->P->nsc, &var, &lsb, &w)) {
      const unsigned width = c->nodes[c->nodes[nd->a].a].width;
      D->cut[var] |= UINT64_C(1) << lsb;
      if (lsb + w < width)
         D->cut[var] |= UINT64_C(1) << (lsb + w);
      D->count[var]++;
      return;
   }

   uint64_t mask;
   if (mask_of(c, n, D->P->nsc, &var, &mask)) {
      const unsigned width = c->vars[var].width;
      for (unsigned b = 1; b < width; b++) {
         if (((mask >> b) & 1) != ((mask >> (b - 1)) & 1))
            D->cut[var] |= UINT64_C(1) << b;
      }
      D->count[var]++;
      return;
   }

   switch ((nkind_t)nd->kind) {
   case N_INSIDE:
   case N_DIST:
      dcol_bad(D, nd->a);
      break;
   case N_UN:
      if (nd->op == CRV_POPCNT)
         dcol_bad(D, nd->a);
      break;
   case N_BIN:
      // Shifted by an amount which is not known
      if ((nd->op == CRV_SHR || nd->op == CRV_ASR || nd->op == CRV_SHL)
          && !is_const_tree(c, nd->b))
         dcol_bad(D, nd->a);
      break;
   default:
      break;
   }
}

typedef struct {
   prob_t *P;
   int    *memo;
   int     n0;
} drw_t;

static const dec_t *dec_for(const drw_t *R, int var)
{
   for (int i = 0; i < R->P->ndec; i++) {
      if (R->P->dec[i].var == var)
         return &R->P->dec[i];
   }
   return NULL;
}

static int drw_fields(drw_t *R, const dec_t *d, unsigned lsb, unsigned w,
                      unsigned own)
{
   crv_t *c = R->P->c;
   int result = -1;
   for (int i = 0; i < d->nf; i++) {
      if (d->pos[i] < lsb || d->pos[i + 1] > lsb + w)
         continue;

      const unsigned fw = d->pos[i + 1] - d->pos[i];
      node_t fn = { .kind = N_VAR, .width = fw, .aux = d->f0 + i };
      int t = add_node(c, fn);
      if (d->pos[i] > lsb) {
         node_t sn = { .kind = N_BIN, .op = CRV_SHL, .width = own,
                       .a = t, .b = mk_const(c, d->pos[i] - lsb, 32, true) };
         t = add_node(c, sn);
      }
      else if (fw != own && w == fw) {
         node_t cn = { .kind = N_CAST, .width = own, .a = t };
         t = add_node(c, cn);
      }

      if (result < 0)
         result = t;
      else {
         node_t an = { .kind = N_BIN, .op = CRV_ADD, .width = own,
                       .a = result, .b = t };
         result = add_node(c, an);
      }
   }
   return result;
}

static int drw_masked(drw_t *R, const dec_t *d, uint64_t mask, unsigned own)
{
   // The fields of the variable inside the mask: each is wholly in or out
   crv_t *c = R->P->c;
   int result = -1;
   for (int i = 0; i < d->nf; i++) {
      if (!((mask >> d->pos[i]) & 1))
         continue;

      const unsigned fw = d->pos[i + 1] - d->pos[i];
      node_t fn = { .kind = N_VAR, .width = fw, .aux = d->f0 + i };
      int t = add_node(c, fn);
      if (fw != own) {
         node_t cn = { .kind = N_CAST, .width = own, .a = t };
         t = add_node(c, cn);
      }
      if (d->pos[i] > 0) {
         node_t sn = { .kind = N_BIN, .op = CRV_SHL, .width = own,
                       .a = t, .b = mk_const(c, d->pos[i], 32, true) };
         t = add_node(c, sn);
      }

      if (result < 0)
         result = t;
      else {
         node_t an = { .kind = N_BIN, .op = CRV_ADD, .width = own,
                       .a = result, .b = t };
         result = add_node(c, an);
      }
   }

   return result >= 0 ? result : mk_const(c, 0, own, false);
}

static int drw(drw_t *R, int n)
{
   crv_t *c = R->P->c;
   if (n >= R->n0)
      return n;
   else if (R->memo[n] >= 0)
      return R->memo[n];

   node_t nd = c->nodes[n];
   int res = n, var;
   unsigned lsb, w;
   const dec_t *d;

   uint64_t mask;
   if (slice_of(c, n, R->P->nsc, &var, &lsb, &w)
       && (d = dec_for(R, var)) != NULL)
      res = drw_fields(R, d, lsb, w, nd.width);
   else if (mask_of(c, n, R->P->nsc, &var, &mask)
            && (d = dec_for(R, var)) != NULL)
      res = drw_masked(R, d, mask, nd.width);
   else {
      switch ((nkind_t)nd.kind) {
      case N_VAR:
         if (nd.aux < R->P->nsc && (d = dec_for(R, nd.aux)) != NULL)
            res = drw_fields(R, d, 0, d->width, nd.width);
         break;
      case N_BIN:
         {
            const int a = drw(R, nd.a), b = drw(R, nd.b);
            if (a != nd.a || b != nd.b) {
               nd.a = a;
               nd.b = b;
               res = add_node(c, nd);
            }
         }
         break;
      case N_UN:
      case N_CAST:
      case N_DIST:
      case N_AELEM:
         {
            const int a = drw(R, nd.a);
            if (a != nd.a) {
               nd.a = a;
               res = add_node(c, nd);
            }
         }
         break;
      case N_COND:
         {
            const int a = drw(R, nd.a), b = drw(R, nd.b), cc = drw(R, nd.c);
            if (a != nd.a || b != nd.b || cc != nd.c) {
               nd.a = a;
               nd.b = b;
               nd.c = cc;
               res = add_node(c, nd);
            }
         }
         break;
      case N_INSIDE:
         {
            bool changed = false;
            const int a = drw(R, nd.a);
            changed |= a != nd.a;
            cset_t ns = { 0 };
            const unsigned cnt = c->sets[nd.aux].n;
            for (unsigned i = 0; i < cnt; i++) {
               item_t it = c->sets[nd.aux].items[i];
               const int lo = drw(R, it.lo);
               const int hi = it.hi >= 0 ? drw(R, it.hi) : -1;
               changed |= lo != it.lo || hi != it.hi;
               it.lo = lo;
               it.hi = hi;
               VEC_PUSH(ns.items, ns.n, ns.cap, it);
            }
            if (changed) {
               VEC_PUSH(c->sets, c->nsets, c->csets, ns);
               nd.a = a;
               nd.aux = c->nsets - 1;
               res = add_node(c, nd);
            }
            else
               free(ns.items);
         }
         break;
      default:
         break;
      }
   }

   R->memo[n] = res;
   return res;
}

static void decompose(prob_t *P)
{
   crv_t *c = P->c;
   if (P->nsc == 0)
      return;

   dcol_t D = { .P = P, .cut = xcalloc(P->nsc * sizeof(uint64_t)),
                .bad = xcalloc(P->nsc), .count = xcalloc(P->nsc * sizeof(int)) };

   for (int ci = 0; ci < P->ncons; ci++) {
      const pcons_t *pc = &P->cons[ci];
      if (pc->kind == C_REQ)
         visit(c, pc->root, dcol_cb, &D);
      else {
         for (int i = 0; i < pc->unn; i++) {
            const int n = P->un[pc->unstart + i];
            dcol_bad(&D, n);
            visit(c, n, dcol_cb, &D);
         }
      }
   }

   int nf = 0;
   for (int v = 0; v < P->nsc; v++) {
      const cvar_t *cv = &c->vars[v];
      const pvar_t *pv = &P->vars[v];
      if (D.count[v] == 0 || D.bad[v] || cv->sgn || cv->width < 12
          || cv->width > 64 || cv->rc || cv->ndom > 0 || pv->dist >= 0
          || pv->ngd > 0 || pv->pop >= 0)
         continue;

      const uint64_t cut = D.cut[v] | 1;
      const int n = __builtin_popcountll(cut);
      if (n < 2 || n > MAX_FIELDS)
         continue;

      dec_t d = { .var = v, .nf = n, .width = cv->width, .f0 = P->nv + nf };
      int k = 0;
      for (unsigned b = 0; b < cv->width; b++) {
         if (cut & (UINT64_C(1) << b))
            d.pos[k++] = b;
      }
      d.pos[n] = cv->width;
      nf += n;
      P->dec = xrealloc(P->dec, (P->ndec + 1) * sizeof(dec_t));
      P->dec[P->ndec++] = d;
   }

   free(D.cut);
   free(D.bad);
   free(D.count);

   if (P->ndec == 0)
      return;

   P->vars = xrealloc(P->vars, (P->nv + nf) * sizeof(pvar_t));
   P->vals = xrealloc(P->vals, (P->nv + nf) * sizeof(int64_t));
   for (int i = 0; i < P->ndec; i++) {
      const dec_t *d = &P->dec[i];
      P->vars[d->var].derived = true;
      for (int k = 0; k < d->nf; k++) {
         const unsigned fw = d->pos[k + 1] - d->pos[k];
         P->vars[d->f0 + k] = (pvar_t){
            .dom = dom_interval(0, ((i128)1 << fw) - 1), .width = fw,
            .dist = -1, .pop = -1,
         };
      }
   }
   P->nv += nf;

   drw_t R = { .P = P, .n0 = c->nnodes };
   R.memo = NEW(int, c->nnodes);
   memset(R.memo, -1, c->nnodes * sizeof(int));

   for (int ci = 0; ci < P->ncons; ci++) {
      pcons_t *pc = &P->cons[ci];
      if (pc->kind == C_REQ)
         pc->root = drw(&R, pc->root);
      else {
         for (int i = 0; i < pc->unn; i++)
            P->un[pc->unstart + i] = drw(&R, P->un[pc->unstart + i]);
      }
   }

   for (int v = 0; v < P->nsc; v++) {
      pvar_t *pv = &P->vars[v];
      for (int i = 0; i < pv->ngd; i++)
         pv->gd[i * 2] = drw(&R, pv->gd[i * 2]);
   }

   free(R.memo);
}


////////////////////////////////////////////////////////////////////////////////
// Domains from implications
//
// Constraints like (s == A) -> (x == 1); (s == B) -> (x == 2); ... where the
// antecedents between them cover every value s can take say that x is one of
// the consequent values, but a bound on x cannot express that and nothing is
// known about x until s is picked.  If x is searched first with a wide
// domain it is almost never one of the values and the search thrashes, as
// with the bounds of a variable constrained to lie below x.  Add the
// consequence as a constraint of its own: it is implied by the others so it
// does not change the solutions.

#define MAX_IMPL     512
#define MAX_COVER    4096
#define MAX_SPANS    64

typedef struct {
   int     var, xnode;       // Variable tested and the node of the consequent
   int     xvar;
   int     line;
   range_t ant[MAX_SPANS], cons[MAX_SPANS];
   int     nant, ncons;
   bool    done;
} impl_t;

static bool value_of(const prob_t *P, int n, i128 *out)
{
   if (!is_const_tree(P->c, n))
      return false;

   ectx_t E = { .c = P->c };
   const int64_t v = ev(&E, n);
   if (E.fail)
      return false;

   *out = mathv(v, P->c->nodes[n].sgn);
   return true;
}

static int pred_var(const prob_t *P, int n, int *xnode)
{
   // A variable which a cast keeps whole

   *xnode = n;
   for (;;) {
      const node_t *nd = &P->c->nodes[n];
      if (nd->kind == N_VAR)
         return nd->aux < P->nsc ? nd->aux : -1;
      else if (nd->kind != N_CAST)
         return -1;

      n = nd->a;
      // Walk to the variable then check what it can hold fits
      const node_t *an = &P->c->nodes[n];
      const node_t *vn = an;
      while (vn->kind == N_CAST)
         vn = &P->c->nodes[vn->a];
      if (vn->kind != N_VAR || vn->aux >= P->nsc)
         return -1;

      const dom_t *d = &P->vars[vn->aux].dom;
      if (d->lo < tmin(nd->width, nd->sgn) || d->hi > tmax(nd->width, nd->sgn))
         return -1;
   }
}

static bool pred_spans(const prob_t *P, int n, int *var, int *xnode,
                       range_t *spans, int *ns)
{
   // Values of a variable for which a one bit node is true

   const crv_t *c = P->c;
   const node_t *nd = &c->nodes[n];

   *ns = 0;
   if (nd->kind == N_BIN && nd->op == CRV_EQ) {
      i128 val;
      int v = pred_var(P, nd->a, xnode);
      if (v >= 0 && value_of(P, nd->b, &val)) {
         *var = v;
         spans[(*ns)++] = (range_t){ val, val };
         return true;
      }

      v = pred_var(P, nd->b, xnode);
      if (v >= 0 && value_of(P, nd->a, &val)) {
         *var = v;
         spans[(*ns)++] = (range_t){ val, val };
         return true;
      }
      return false;
   }
   else if (nd->kind == N_INSIDE) {
      const int v = pred_var(P, nd->a, xnode);
      if (v < 0)
         return false;

      const cset_t *cs = &c->sets[nd->aux];
      for (unsigned i = 0; i < cs->n; i++) {
         i128 lo, hi;
         if (*ns == MAX_SPANS || !value_of(P, cs->items[i].lo, &lo)
             || (cs->items[i].hi >= 0 && !value_of(P, cs->items[i].hi, &hi)))
            return false;
         if (cs->items[i].hi < 0)
            hi = lo;
         spans[(*ns)++] = (range_t){ lo, hi };
      }

      *var = v;
      return *ns > 0;
   }

   return false;
}

static bool in_spans(const range_t *r, int n, i128 v)
{
   for (int i = 0; i < n; i++) {
      if (v >= r[i].lo && v <= r[i].hi)
         return true;
   }
   return false;
}

static void derive_domains(prob_t *P)
{
   crv_t *c = P->c;
   const int n0 = P->ncons;

   impl_t *imps = NULL;
   int nimp = 0;

   for (int ci = 0; ci < n0 && nimp < MAX_IMPL; ci++) {
      const pcons_t *pc = &P->cons[ci];
      if (pc->kind != C_REQ || pc->soft >= 0)
         continue;

      const node_t *nd = &c->nodes[pc->root];
      if (nd->kind != N_BIN || nd->op != CRV_IMPLIES)
         continue;

      impl_t im = { .line = pc->line };
      int dummy;
      if (!pred_spans(P, nd->a, &im.var, &dummy, im.ant, &im.nant)
          || !pred_spans(P, nd->b, &im.xvar, &im.xnode, im.cons, &im.ncons)
          || im.var == im.xvar)
         continue;

      if (imps == NULL)
         imps = xmalloc_array(MAX_IMPL, sizeof(impl_t));
      imps[nimp++] = im;
   }

   for (int i = 0; i < nimp; i++) {
      if (imps[i].done)
         continue;

      // The implications between this pair of variables
      int group[MAX_IMPL], ng = 0;
      for (int j = i; j < nimp; j++) {
         if (imps[j].var == imps[i].var && imps[j].xvar == imps[i].xvar) {
            imps[j].done = true;
            group[ng++] = j;
         }
      }

      const dom_t *sd = &P->vars[imps[i].var].dom;
      if (dom_count(sd, sd->lo, sd->hi) > MAX_COVER)
         continue;

      // Every value of the tested variable needs exactly one antecedent
      bool used[MAX_IMPL] = { false }, ok = true;
      for (int k = 0; k < dom_nr(sd) && ok; k++) {
         const range_t r = dom_range(sd, k);
         for (i128 v = r.lo; v <= r.hi && ok; v++) {
            int match = -1;
            for (int g = 0; g < ng; g++) {
               if (in_spans(imps[group[g]].ant, imps[group[g]].nant, v)) {
                  if (match >= 0)
                     ok = false;
                  match = g;
               }
            }
            if (match < 0)
               ok = false;
            else
               used[match] = true;
         }
      }

      if (!ok)
         continue;

      range_t all[MAX_SPANS * 8];
      int nall = 0;
      for (int g = 0; g < ng && ok; g++) {
         if (!used[g])
            continue;
         if (nall + imps[group[g]].ncons > MAX_SPANS * 8)
            ok = false;
         else {
            memcpy(all + nall, imps[group[g]].cons,
                   imps[group[g]].ncons * sizeof(range_t));
            nall += imps[group[g]].ncons;
         }
      }

      if (!ok)
         continue;

      nall = rng_merge(all, nall);

      // Nothing to learn if x can already only take these values
      const dom_t *xd = &P->vars[imps[i].xvar].dom;
      bool useful = false;
      for (int k = 0; k < dom_nr(xd) && !useful; k++) {
         const range_t r = dom_range(xd, k);
         useful = !rng_covers(all, nall, r.lo, r.hi);
      }

      if (!useful)
         continue;

      // Adding nodes may move them
      const unsigned xwidth = c->nodes[imps[i].xnode].width;
      const bool xsgn = c->nodes[imps[i].xnode].sgn;
      crv_set_t set = crv_set_new(c);
      for (int k = 0; k < nall; k++) {
         crv_node_t lo = mk_const(c, all[k].lo, xwidth, xsgn);
         if (all[k].lo == all[k].hi)
            crv_set_value(c, set, lo);
         else
            crv_set_range(c, set, lo, mk_const(c, all[k].hi, xwidth,
                                               xsgn));
      }

      const int root = crv_inside(c, imps[i].xnode, set);
      add_pcons(P, C_REQ, root, -1, imps[i].line);
   }

   free(imps);
}

// Mode 1 solves for the sizes of arrays with a random size, ignoring the
// elements of those, mode 2 has all sizes known
static prob_t *prob_build(crv_t *c, int mode, const unsigned *sizes,
                          const bool *active)
{
   prob_t *P = xcalloc(sizeof(prob_t));
   P->c = c;
   P->nsc = c->nvars;
   P->na = c->narrays;
   P->curc = -1;
   P->pass = 1;

   P->ebase = NEW(int, c->narrays);
   P->sizes = NEW(unsigned, c->narrays);
   int nelem = 0;
   for (unsigned a = 0; a < c->narrays; a++) {
      const carr_t *ar = &c->arrays[a];
      P->sizes[a] = mode == 2 ? sizes[a] : ar->len;
      if (mode == 2 || !ar->size_rand) {
         P->ebase[a] = P->nsc + P->na + nelem;
         nelem += P->sizes[a];
      }
      else
         P->ebase[a] = -1;
   }

   P->nv = P->nsc + P->na + nelem;
   P->vars = NEW(pvar_t, P->nv);
   P->vals = NEW(int64_t, P->nv);

   for (int v = 0; v < P->nsc; v++) {
      const cvar_t *cv = &c->vars[v];
      pvar_t *pv = &P->vars[v];
      *pv = (pvar_t){ .dom = scalar_dom(P, cv), .width = cv->width,
                      .sgn = cv->sgn, .dist = -1, .pop = -1, .cur = cv->cur };
      if (cv->rc)
         pv->rc = (randc_t *)(uintptr_t)*cv->rc;
   }

   for (unsigned a = 0; a < c->narrays; a++) {
      const carr_t *ar = &c->arrays[a];
      pvar_t *pv = &P->vars[P->nsc + a];
      *pv = (pvar_t){ .width = 32, .sgn = true, .dist = -1, .pop = -1,
                      .cur = ar->len, .size = mode == 1 && ar->size_rand };
      if (mode == 2 || !ar->size_rand)
         pv->dom = dom_interval(P->sizes[a], P->sizes[a]);
      else
         pv->dom = dom_interval(0, MAX_SIZE);

      if (P->ebase[a] < 0)
         continue;

      for (unsigned i = 0; i < P->sizes[a]; i++) {
         pvar_t *ev = &P->vars[P->ebase[a] + i];
         const int64_t init = i < ar->ninit ? ar->init[i] : 0;
         *ev = (pvar_t){ .width = ar->width, .sgn = ar->sgn, .dist = -1, .pop = -1,
                         .cur = init };
         if (ar->elems_rand)
            ev->dom = type_dom(ar->width, ar->sgn);
         else
            ev->dom = dom_interval(mathv(init, ar->sgn),
                                   mathv(init, ar->sgn));
      }
   }

   ictx_t I = { .c = c, .ebase = P->ebase, .sizes = P->sizes,
                .nsc = P->nsc };
   P->base_nodes = c->nnodes;
   P->base_sets = c->nsets;

   for (unsigned k = 0; k < c->ncons; k++) {
      const ccons_t *cc = &c->cons[k];
      if (cc->dead)
         continue;

      if (cc->kind == C_FOREACH) {
         if (mode == 2)
            expand_foreach(P, &I, cc, active);
         continue;
      }

      if (cc->soft >= 0 && !active[cc->soft])
         continue;

      bool skip = false;
      if (mode == 1) {
         crv_rsize_t rs = { false, c };
         if (cc->kind == C_REQ)
            visit(c, cc->root, rsize_cb, &rs);
         else {
            for (unsigned i = 0; i < cc->count; i++)
               visit(c, c->uniq[cc->first + i], rsize_cb, &rs);
         }
         skip = rs.bad;
      }
      if (skip)
         continue;

      I.nb = 0;
      if (cc->kind == C_REQ)
         add_pcons(P, C_REQ, inst(&I, cc->root), cc->soft, cc->line);
      else {
         add_pcons(P, C_UNIQUE, -1, cc->soft, cc->line);
         P->cons[P->ncons - 1].unstart = P->nun;
         P->cons[P->ncons - 1].unn = cc->count;
         for (unsigned i = 0; i < cc->count; i++)
            VEC_PUSH(P->un, P->nun, P->cun, inst(&I, c->uniq[cc->first + i]));
      }
   }

   if (I.toobig) {
      // Too large: leave no constraints satisfiable
      P->ncons = 0;
      add_pcons(P, C_REQ, mk_fail(c), -1, 0);
      }
   derive_domains(P);

   // Weighted choice applies to a distribution at the root of a constraint
   for (int ci = 0; ci < P->ncons; ci++) {
      const pcons_t *pc = &P->cons[ci];
      if (pc->kind != C_REQ || pc->soft >= 0)
         continue;
      const node_t *nd = &c->nodes[pc->root];
      const node_t *an = &c->nodes[nd->a];
      if (nd->kind == N_DIST && an->kind == N_VAR
          && P->vars[an->aux].dist < 0)
         P->vars[an->aux].dist = nd->aux;
      else if (nd->kind == N_BIN && nd->op == CRV_IMPLIES
               && c->nodes[nd->b].kind == N_DIST) {
         const node_t *dn = &c->nodes[nd->b];
         const node_t *dv = &c->nodes[dn->a];
         if (dv->kind == N_VAR) {
            pvar_t *pv = &P->vars[dv->aux];
            pv->gd = xrealloc(pv->gd, (pv->ngd + 1) * 2 * sizeof(int));
            pv->gd[pv->ngd * 2] = nd->a;
            pv->gd[pv->ngd * 2 + 1] = dn->aux;
            pv->ngd++;
         }
      }
   }

   // A variable counted by a constraint takes values with a suitable count
   for (unsigned n = 0; n < c->nnodes; n++) {
      const node_t *nd = &c->nodes[n];
      if (nd->kind != N_UN || nd->op != CRV_POPCNT)
         continue;

      const node_t *an = &c->nodes[nd->a];
      if (an->kind == N_CAST && !an->sgn)
         an = &c->nodes[an->a];

      if (an->kind == N_VAR && (unsigned)an->aux < (unsigned)P->nv
          && !P->vars[an->aux].sgn && P->vars[an->aux].pop < 0
          && P->vars[an->aux].width <= 64)
         P->vars[an->aux].pop = n;
   }

   if (crv_decompose_enabled())
      decompose(P);
   P->nn = c->nnodes;

   // Prepare sets and distributions now the nodes are final
   for (unsigned i = 0; i < c->nsets; i++)
      set_prep(c, &c->sets[i]);
   for (unsigned i = 0; i < c->ndists; i++)
      dist_prep(&c->dists[i]);

   P->vmark = xcalloc((P->nv ? P->nv : 1) * sizeof(int));
   P->vstart = NEW(int, P->ncons + 1);
   for (int ci = 0; ci < P->ncons; ci++) {
      const pcons_t *pc = &P->cons[ci];
      vctx_t V = { P, ci };
      P->vstart[ci] = P->nvl;
      if (pc->kind == C_REQ)
         visit(c, pc->root, vars_cb, &V);
      else {
         for (int i = 0; i < pc->unn; i++)
            visit(c, P->un[pc->unstart + i], vars_cb, &V);
      }
   }
   P->vstart[P->ncons] = P->nvl;

   P->wstart = xcalloc((P->nv + 1) * sizeof(int));
   for (int i = 0; i < P->nvl; i++)
      P->wstart[P->vl[i] + 1]++;
   for (int v = 0; v < P->nv; v++)
      P->wstart[v + 1] += P->wstart[v];
   P->wlist = NEW(int, P->nvl);
   int *fill = NEW(int, P->nv);
   memcpy(fill, P->wstart, P->nv * sizeof(int));
   for (int ci = 0; ci < P->ncons; ci++) {
      for (int i = P->vstart[ci]; i < P->vstart[ci + 1]; i++)
         P->wlist[fill[P->vl[i]]++] = ci;
   }
   free(fill);

   // A size that nothing constrains keeps its value
   for (unsigned a = 0; a < c->narrays; a++) {
      const int v = P->nsc + a;
      if (P->vars[v].size && P->wstart[v] == P->wstart[v + 1]) {
         P->vars[v].dom = dom_interval(P->sizes[a], P->sizes[a]);
         P->vars[v].size = false;
      }
   }

   P->iv = NEW(ivl_t, P->nn);
   P->stamp = xcalloc((P->nn ? P->nn : 1) * sizeof(uint32_t));
   P->uproc = xcalloc(P->nun ? P->nun : 1);
   P->pend = xcalloc(((P->ncons + 63) / 64 + 1) * sizeof(uint64_t));

   P->budget = 800000 + 80 * ((int64_t)P->nn + P->nv);
   for (int ci = 0; ci < P->ncons; ci++)
      P->budget += 24 * (int64_t)P->cons[ci].unn * P->cons[ci].unn;
   (void)debug_level();
   if (dbg.budget > 0)
      P->budget = dbg.budget;

   return P;
}


////////////////////////////////////////////////////////////////////////////////
// Propagation

static inline ivl_t dom_iv(const dom_t *d)
{
   return ivl(d->lo, d->hi);
}

static void enqueue(prob_t *P, int ci)
{
   const uint64_t bit = UINT64_C(1) << (ci & 63);
   if (!(P->pend[ci >> 6] & bit)) {
      P->pend[ci >> 6] |= bit;
      P->qn++;
   }
}

// Find a queued constraint scanning up from, or down from, a position
static int next_pending(const prob_t *P, int pos, bool up)
{
   if (pos < 0 || pos >= P->ncons)
      return -1;

   int w = pos >> 6;
   if (up) {
      uint64_t bits = P->pend[w] & (~UINT64_C(0) << (pos & 63));
      for (const int nw = (P->ncons + 63) / 64; ; ) {
         if (bits)
            return (w << 6) + __builtin_ctzll(bits);
         else if (++w >= nw)
            return -1;
         bits = P->pend[w];
      }
   }
   else {
      uint64_t bits = P->pend[w];
      if ((pos & 63) != 63)
         bits &= (UINT64_C(1) << ((pos & 63) + 1)) - 1;
      for (;;) {
         if (bits)
            return (w << 6) + 63 - __builtin_clzll(bits);
         else if (--w < 0)
            return -1;
         bits = P->pend[w];
      }
   }
}

static void notify(prob_t *P, int v)
{
   for (int i = P->wstart[v]; i < P->wstart[v + 1]; i++) {
      if (P->wlist[i] != P->curc)
         enqueue(P, P->wlist[i]);
   }
}

static void commit_dom(prob_t *P, int v, dom_t nd)
{
   const trail_t t = { v, P->vars[v].dom };
   VEC_PUSH(P->trail, P->ntrail, P->ctrail, t);
   P->vars[v].dom = nd;
   P->changed++;
   notify(P, v);
}

static void undo_to(prob_t *P, int mark)
{
   while (P->ntrail > mark) {
      const trail_t *t = &P->trail[--P->ntrail];
      if (t->var < 0)
         P->uproc[-2 - t->var] = 0;
      else
         P->vars[t->var].dom = t->old;
   }
}

static bool var_clip(prob_t *P, int v, i128 lo, i128 hi)
{
   dom_t d = P->vars[v].dom;
   if (!dom_clip(&d, lo, hi))
      return true;
   else if (dom_empty(&d))
      return false;

   commit_dom(P, v, d);
   return true;
}

static bool var_remove(prob_t *P, int v, i128 val)
{
   dom_t d = P->vars[v].dom;
   P->work += dom_nr(&d) / 4;
   if (!dom_remove(&d, val, &P->ar))
      return true;
   else if (dom_empty(&d))
      return false;

   commit_dom(P, v, d);
   return true;
}

static bool var_apply(prob_t *P, int v, const range_t *rr, int n, bool inter)
{
   dom_t d = P->vars[v].dom;
   if (n == 0)
      return !inter;

   const amark_t m = arena_mark(&P->ar);
   const int nr = dom_nr(&d);
   range_t *out = arena_alloc(&P->ar, (nr + n + 1) * sizeof(range_t));
   const int k = rng_op(&d, rr, n, inter, out);
   P->work += (nr + n) / 8;

   i128 cnt = 0;
   for (int i = 0; i < k; i++)
      cnt += out[i].hi - out[i].lo + 1;

   if (k == 0) {
      arena_reset(&P->ar, m);
      return false;
   }
   else if (cnt == dom_count(&d, d.lo, d.hi)) {
      arena_reset(&P->ar, m);
      return true;
   }

   commit_dom(P, v, dom_ranges(out, k));
   return true;
}

static const dom_t *node_dom(prob_t *P, const node_t *nd)
{
   if (nd->kind == N_VAR)
      return &P->vars[nd->aux].dom;
   else if (nd->kind == N_ASIZE)
      return &P->vars[P->nsc + nd->aux].dom;
   else
      return NULL;
}

static int node_var(const prob_t *P, const node_t *nd)
{
   if (nd->kind == N_VAR)
      return nd->aux;
   else if (nd->kind == N_ASIZE)
      return P->nsc + nd->aux;
   else
      return -1;
}

static ivl_t fwd(prob_t *P, int n);
static bool bwd(prob_t *P, int n, ivl_t r);

static ivl_t div_corners(ivl_t a, ivl_t b)
{
   ivl_t r = ivl(BIG, -BIG);
   const i128 x[2] = { a.lo, a.hi }, y[2] = { b.lo, b.hi };
   for (int i = 0; i < 2; i++) {
      for (int j = 0; j < 2; j++) {
         const i128 q = x[i] / y[j];
         if (q < r.lo) r.lo = q;
         if (q > r.hi) r.hi = q;
      }
   }
   return r;
}

static ivl_t fwd_shift(const node_t *nd, ivl_t a, ivl_t b)
{
   const unsigned w = nd->width;
   const bool s = nd->sgn;

   i128 cmin = b.lo, cmax = b.hi;
   if (b.lo < 0) {
      cmin = 0;
      cmax = 64;
   }
   if (cmin > 64) cmin = 64;
   if (cmax > 64) cmax = 64;

   const bool zero = cmax >= 64;
   const i128 sign = nd->op == CRV_ASR && s;
   if (cmin >= 64) {
      if (sign)
         return ivl(a.lo < 0 ? -1 : 0, a.hi < 0 ? -1 : 0);
      return ivl(0, 0);
   }

   const int c1 = (int)cmin, c2 = zero ? 63 : (int)cmax;
   ivl_t r;
   switch ((crv_binary_t)nd->op) {
   case CRV_SHL:
      r = iv_mul(a, ivl((i128)1 << c1, (i128)1 << c2));
      if (zero)
         r = hull(r, ivl(0, 0));
      return wrap(r, w, s);
   case CRV_SHR:
      if (a.lo >= 0)
         r = ivl(zero ? 0 : a.lo >> c2, a.hi >> c1);
      else
         r = ivl(0, (i128)wmask(w) >> c1);
      return wrap(r, w, s);
   default:
      {
         const i128 l1 = a.lo >> c1, l2 = a.lo >> c2;
         const i128 h1 = a.hi >> c1, h2 = a.hi >> c2;
         r = ivl(l1 < l2 ? l1 : l2, h1 > h2 ? h1 : h2);
         if (zero) {
            if (sign)
               r = hull(r, ivl(a.lo < 0 ? -1 : 0, a.hi < 0 ? -1 : 0));
            else
               r = hull(r, ivl(0, 0));
         }
         return r;
      }
   }
}

static ivl_t fwd_bool(crv_binary_t op, ivl_t a, ivl_t b)
{
   ivl_t r = ivl(1, 0);
   ivl_t bb = ivl(1, 0);
   if (!iv_empty(b))
      bb = is_false(b) ? ivl(0, 0) : !can0(b) ? ivl(1, 1) : ivl(0, 1);

   if (iv_empty(a)) {
      // An operand that is always X: X && 0 is 0, X || 1 and X -> 1 are 1
      if (iv_empty(b))
         return r;
      else if (op == CRV_LOG_AND)
         return can0(b) ? ivl(0, 0) : r;
      else
         return can1(b) ? ivl(1, 1) : r;
   }

   switch (op) {
   case CRV_LOG_AND:
      if (can0(a)) r = hull(r, ivl(0, 0));
      if (can1(a)) r = hull(r, bb);
      break;
   case CRV_LOG_OR:
      if (can1(a)) r = hull(r, ivl(1, 1));
      if (can0(a)) r = hull(r, bb);
      break;
   default:
      if (can0(a)) r = hull(r, ivl(1, 1));
      if (can1(a)) r = hull(r, bb);
      break;
   }
   return r;
}

static ivl_t fwd_bin(prob_t *P, const node_t *nd, ivl_t a, ivl_t b)
{
   const crv_binary_t op = nd->op;
   const unsigned w = nd->width;
   const bool s = nd->sgn;

   if (op == CRV_LOG_AND || op == CRV_LOG_OR || op == CRV_IMPLIES)
      return fwd_bool(op, a, b);
   else if (iv_empty(a) || iv_empty(b))
      return ivl(1, 0);

   if (op >= CRV_EQ && op <= CRV_GE) {
      // The same variable on both sides
      const int va = node_var(P, &P->c->nodes[nd->a]);
      if (va >= 0 && va == node_var(P, &P->c->nodes[nd->b]))
         return op == CRV_EQ || op == CRV_LE || op == CRV_GE
            ? ivl(1, 1) : ivl(0, 0);
   }

   if (iv_pt(a) && iv_pt(b)) {
      bool fail = false;
      const node_t *nodes = P->c->nodes;
      const int64_t v = ev_bin(nd, &nodes[nd->a], &nodes[nd->b],
                               st(a.lo), st(b.lo), &fail);
      if (fail)
         return ivl(1, 0);
      const i128 m = mathv(v, s);
      return ivl(m, m);
   }

   switch (op) {
   case CRV_ADD: return wrap(ivl(a.lo + b.lo, a.hi + b.hi), w, s);
   case CRV_SUB: return wrap(ivl(a.lo - b.hi, a.hi - b.lo), w, s);
   case CRV_MUL: return wrap(iv_mul(a, b), w, s);
   case CRV_DIV:
      {
         ivl_t r = ivl(1, 0);
         if (b.lo <= -1)
            r = hull(r, div_corners(a, ivl(b.lo, b.hi < -1 ? b.hi : -1)));
         if (b.hi >= 1)
            r = hull(r, div_corners(a, ivl(b.lo > 1 ? b.lo : 1, b.hi)));
         return wrap(r, w, s);
      }
   case CRV_MOD:
      {
         if (is_false(b))
            return ivl(1, 0);
         const i128 ml = b.lo < 0 ? -b.lo : b.lo;
         const i128 mh = b.hi < 0 ? -b.hi : b.hi;
         const i128 m = (ml > mh ? ml : mh) - 1;
         const i128 lo = a.lo < 0 ? (-m > a.lo ? -m : a.lo) : 0;
         const i128 hi = a.hi > 0 ? (m < a.hi ? m : a.hi) : 0;
         return wrap(ivl(lo, hi), w, s);
      }
   case CRV_AND:
      if (a.lo >= 0 && b.lo >= 0)
         return ivl(0, a.hi < b.hi ? a.hi : b.hi);
      else if (a.lo >= 0)
         return ivl(0, a.hi);
      else if (b.lo >= 0)
         return ivl(0, b.hi);
      return ivl(tmin(w, s), tmax(w, s));
   case CRV_OR:
      if (a.lo >= 0 && b.lo >= 0)
         return ivl(a.lo > b.lo ? a.lo : b.lo,
                    pow2mask(a.hi > b.hi ? a.hi : b.hi));
      return ivl(tmin(w, s), tmax(w, s));
   case CRV_XOR:
      if (a.lo >= 0 && b.lo >= 0)
         return ivl(0, pow2mask(a.hi > b.hi ? a.hi : b.hi));
      return ivl(tmin(w, s), tmax(w, s));
   case CRV_SHL:
   case CRV_SHR:
   case CRV_ASR:
      return fwd_shift(nd, a, b);
   case CRV_EQ:
      return a.hi < b.lo || b.hi < a.lo ? ivl(0, 0) : ivl(0, 1);
   case CRV_NE:
      return a.hi < b.lo || b.hi < a.lo ? ivl(1, 1) : ivl(0, 1);
   case CRV_LT:
      return a.hi < b.lo ? ivl(1, 1) : a.lo >= b.hi ? ivl(0, 0) : ivl(0, 1);
   case CRV_LE:
      return a.hi <= b.lo ? ivl(1, 1) : a.lo > b.hi ? ivl(0, 0) : ivl(0, 1);
   case CRV_GT:
      return a.lo > b.hi ? ivl(1, 1) : a.hi <= b.lo ? ivl(0, 0) : ivl(0, 1);
   case CRV_GE:
      return a.lo >= b.hi ? ivl(1, 1) : a.hi < b.lo ? ivl(0, 0) : ivl(0, 1);
   default:
      should_not_reach_here();
   }
}

// Ranges that the argument of an inside or dist may take, or definitely
// does take if "definite", for sets with items that are not constant these
// are built in the arena
static int node_ranges(prob_t *P, const node_t *nd, bool definite,
                       const range_t **out)
{
   if (nd->kind == N_DIST) {
      const cdist_t *cd = &P->c->dists[nd->aux];
      *out = cd->mr;
      return cd->nmr;
   }

   const cset_t *cs = &P->c->sets[nd->aux];
   if (cs->allconst) {
      *out = cs->rr;
      return cs->nrr;
   }

   range_t *r = arena_alloc(&P->tmp, (cs->n + 1) * sizeof(range_t));
   unsigned n = 0;
   for (unsigned i = 0; i < cs->n; i++) {
      const ivl_t lo = P->iv[cs->items[i].lo];
      const ivl_t hi = cs->items[i].hi >= 0 ? P->iv[cs->items[i].hi] : lo;
      if (iv_empty(lo) || iv_empty(hi))
         continue;
      else if (definite) {
         if (iv_pt(lo) && iv_pt(hi) && lo.lo <= hi.lo)
            r[n++] = (range_t){ lo.lo, hi.lo };
      }
      else if (lo.lo <= hi.hi)
         r[n++] = (range_t){ lo.lo, hi.hi };
   }
   *out = r;
   return rng_merge(r, n);
}

static ivl_t fwd_member(prob_t *P, const node_t *nd, ivl_t x)
{
   if (iv_empty(x))
      return x;

   const amark_t m = arena_mark(&P->tmp);
   const range_t *rr;
   int n = node_ranges(P, nd, false, &rr);
   ivl_t r;
   if (!rng_overlaps(rr, n, x.lo, x.hi))
      r = ivl(0, 0);
   else {
      n = node_ranges(P, nd, true, &rr);
      r = rng_covers(rr, n, x.lo, x.hi) ? ivl(1, 1) : ivl(0, 1);
   }
   arena_reset(&P->tmp, m);
   return r;
}

static ivl_t fwd(prob_t *P, int n)
{
   if (P->stamp[n] == P->pass)
      return P->iv[n];

   const node_t *nd = &P->c->nodes[n];
   ivl_t r;
   P->work++;

   switch ((nkind_t)nd->kind) {
   case N_CONST:
      r = ivl(mathv(nd->val, nd->sgn), mathv(nd->val, nd->sgn));
      break;
   case N_VAR:
      r = dom_iv(&P->vars[nd->aux].dom);
      break;
   case N_ASIZE:
      r = dom_iv(&P->vars[P->nsc + nd->aux].dom);
      break;
   case N_BIN:
      {
         const ivl_t a = fwd(P, nd->a);
         const ivl_t b = fwd(P, nd->b);
         r = fwd_bin(P, nd, a, b);
      }
      break;
   case N_UN:
      {
         const ivl_t a = fwd(P, nd->a);
         const node_t *an = &P->c->nodes[nd->a];
         if (iv_empty(a))
            r = a;
         else if (iv_pt(a)) {
            const i128 m = mathv(ev_un(nd, an, st(a.lo)), nd->sgn);
            r = ivl(m, m);
         }
         else {
            switch ((crv_unary_t)nd->op) {
            case CRV_NEG:
               r = wrap(ivl(-a.hi, -a.lo), nd->width, nd->sgn);
               break;
            case CRV_INV:
               if (nd->sgn)
                  r = ivl(-a.hi - 1, -a.lo - 1);
               else {
                  const i128 mx = tmax(nd->width, false);
                  r = ivl(mx - a.hi, mx - a.lo);
               }
               break;
            case CRV_LOG_NOT:
               r = is_false(a) ? ivl(1, 1) : !can0(a) ? ivl(0, 0) : ivl(0, 1);
               break;
            case CRV_REDUCE_OR:
               r = !can0(a) ? ivl(1, 1) : ivl(0, 1);
               break;
            case CRV_POPCNT:
               {
                  const node_t *an = &P->c->nodes[nd->a];
                  // A negative value has its sign bit set
                  r = ivl(a.lo > 0 || a.hi < 0 ? 1 : 0,
                          a.lo >= 0 && a.hi == 0 ? 0 : an->width);
               }
               break;
            default:
               r = ivl(0, 1);
               break;
            }
         }
      }
      break;
   case N_CAST:
      {
         const ivl_t a = fwd(P, nd->a);
         r = wrap(a, nd->width, nd->sgn);
      }
      break;
   case N_COND:
      {
         const ivl_t t = fwd(P, nd->a);
         const ivl_t x = fwd(P, nd->b);
         const ivl_t y = fwd(P, nd->c);
         r = ivl(1, 0);
         if (!iv_empty(t)) {
            if (can1(t)) r = hull(r, x);
            if (can0(t)) r = hull(r, y);
         }
      }
      break;
   case N_AELEM:
      {
         const ivl_t x = fwd(P, nd->a);
         const i128 size = P->sizes[nd->aux];
         const i128 lo = x.lo < 0 ? 0 : x.lo;
         const i128 hi = x.hi >= size ? size - 1 : x.hi;
         r = ivl(1, 0);
         if (!iv_empty(x) && lo <= hi) {
            const int base = P->ebase[nd->aux];
            for (i128 k = lo; k <= hi; k++)
               r = hull(r, dom_iv(&P->vars[base + (int)k].dom));
            P->work += (int64_t)(hi - lo) / 8;
         }
      }
      break;
   case N_ASUM:
      {
         const int base = P->ebase[nd->aux];
         const unsigned size = P->sizes[nd->aux];
         i128 lo = 0, hi = 0;
         for (unsigned i = 0; i < size; i++) {
            lo += P->vars[base + i].dom.lo;
            hi += P->vars[base + i].dom.hi;
         }
         P->work += size / 8;
         r = wrap(ivl(lo, hi), nd->width, nd->sgn);
      }
      break;
   case N_INSIDE:
      {
         const cset_t *cs = &P->c->sets[nd->aux];
         const ivl_t x = fwd(P, nd->a);
         if (!cs->allconst) {
            for (unsigned i = 0; i < cs->n; i++) {
               fwd(P, cs->items[i].lo);
               if (cs->items[i].hi >= 0)
                  fwd(P, cs->items[i].hi);
            }
         }
         r = fwd_member(P, nd, x);
      }
      break;
   case N_DIST:
      r = fwd_member(P, nd, fwd(P, nd->a));
      break;
   default:
      r = ivl(1, 0);
      break;
   }

   P->iv[n] = r;
   P->stamp[n] = P->pass;
   return r;
}

static bool bwd_true(prob_t *P, int n)
{
   const ivl_t x = P->iv[n];
   if (iv_empty(x) || is_false(x))
      return false;
   else if (x.lo == 0)
      return bwd(P, n, ivl(1, x.hi));
   else if (x.hi == 0)
      return bwd(P, n, ivl(x.lo, -1));
   return true;
}

static bool bwd_false(prob_t *P, int n)
{
   return bwd(P, n, ivl(0, 0));
}

// Remove a value from the possible values of a node
static bool prune(prob_t *P, int n, i128 v)
{
   const node_t *nd = &P->c->nodes[n];
   const int var = node_var(P, nd);
   if (var >= 0) {
      if (!var_remove(P, var, v))
         return false;
      P->iv[n] = dom_iv(&P->vars[var].dom);
      return true;
   }

   const ivl_t x = P->iv[n];
   if (x.lo == v)
      return bwd(P, n, ivl(v + 1, BIG));
   else if (x.hi == v)
      return bwd(P, n, ivl(-BIG, v - 1));
   return true;
}

static bool enforce_rel(prob_t *P, crv_binary_t op, int a, int b)
{
   switch (op) {
   case CRV_EQ:
      {
         const ivl_t x = isect(P->iv[a], P->iv[b]);
         return bwd(P, a, x) && bwd(P, b, x);
      }
   case CRV_NE:
      if (iv_pt(P->iv[b]) && !prune(P, a, P->iv[b].lo))
         return false;
      if (iv_pt(P->iv[a]))
         return prune(P, b, P->iv[a].lo);
      return true;
   case CRV_LT:
      return bwd(P, a, ivl(-BIG, P->iv[b].hi - 1))
         && bwd(P, b, ivl(P->iv[a].lo + 1, BIG));
   case CRV_LE:
      return bwd(P, a, ivl(-BIG, P->iv[b].hi))
         && bwd(P, b, ivl(P->iv[a].lo, BIG));
   case CRV_GT:
      return bwd(P, a, ivl(P->iv[b].lo + 1, BIG))
         && bwd(P, b, ivl(-BIG, P->iv[a].hi - 1));
   case CRV_GE:
      return bwd(P, a, ivl(P->iv[b].lo, BIG))
         && bwd(P, b, ivl(-BIG, P->iv[a].hi));
   default:
      should_not_reach_here();
   }
}

static crv_binary_t negate_rel(crv_binary_t op)
{
   switch (op) {
   case CRV_EQ: return CRV_NE;
   case CRV_NE: return CRV_EQ;
   case CRV_LT: return CRV_GE;
   case CRV_GE: return CRV_LT;
   case CRV_LE: return CRV_GT;
   default: return CRV_LE;
   }
}

typedef struct {
   range_t r[32];
   int     n, var;
   bool    ok;
} orset_t;

// Collect the values a variable can take for a disjunction of simple
// tests of the same variable to be true
static void or_collect(prob_t *P, int n, orset_t *U)
{
   const node_t *nd = &P->c->nodes[n];
   if (!U->ok)
      return;
   else if (nd->kind == N_BIN && nd->op == CRV_LOG_OR) {
      or_collect(P, nd->a, U);
      or_collect(P, nd->b, U);
      return;
   }

   int var = -1;
   range_t rg = { -BIG, BIG };
   if (nd->kind == N_BIN && nd->op >= CRV_EQ && nd->op <= CRV_GE
       && nd->op != CRV_NE) {
      const node_t *an = &P->c->nodes[nd->a];
      const node_t *bn = &P->c->nodes[nd->b];
      crv_binary_t op = nd->op;
      ivl_t o = ivl(0, 0);
      if ((var = node_var(P, an)) >= 0)
         o = P->iv[nd->b];
      else if ((var = node_var(P, bn)) >= 0) {
         o = P->iv[nd->a];
         op = op == CRV_LT ? CRV_GT : op == CRV_GT ? CRV_LT
            : op == CRV_LE ? CRV_GE : op == CRV_GE ? CRV_LE : op;
      }
      if (var >= 0) {
         switch (op) {
         case CRV_EQ: rg = (range_t){ o.lo, o.hi }; break;
         case CRV_LT: rg.hi = o.hi - 1; break;
         case CRV_LE: rg.hi = o.hi; break;
         case CRV_GT: rg.lo = o.lo + 1; break;
         default: rg.lo = o.lo; break;
         }
         if (iv_empty(o))
            var = -1;
      }
   }
   else if (nd->kind == N_INSIDE || nd->kind == N_DIST) {
      var = node_var(P, &P->c->nodes[nd->a]);
      if (var >= 0) {
         const range_t *rr;
         const int k = node_ranges(P, nd, false, &rr);
         if (U->n + k > 32)
            var = -1;
         else {
            for (int i = 0; i < k; i++)
               U->r[U->n++] = rr[i];
            if (U->var >= 0 && U->var != var)
               U->ok = false;
            U->var = var;
            return;
         }
      }
   }

   if (var < 0 || U->n == 32 || (U->var >= 0 && U->var != var)) {
      U->ok = false;
      return;
   }
   U->var = var;
   U->r[U->n++] = rg;
}

static bool bwd_or_union(prob_t *P, int n)
{
   orset_t U = { .var = -1, .ok = true };
   const amark_t m = arena_mark(&P->tmp);
   or_collect(P, n, &U);
   bool ok = true;
   if (U.ok && U.var >= 0) {
      const int k = rng_merge(U.r, U.n);
      ok = var_apply(P, U.var, U.r, k, true);
   }
   arena_reset(&P->tmp, m);
   return ok;
}

// Trim a to the nearest values with a % c == k
static bool trim_mod(prob_t *P, int a, i128 c, i128 k)
{
   const ivl_t x = P->iv[a];
   if (c < 0)
      c = -c;
   if (c == 0 || x.lo < 0)
      return true;
   else if (k < 0 || k >= c)
      return false;

   const i128 lo = x.lo + (k - x.lo % c + c) % c;
   const i128 hi = x.hi - (x.hi % c - k + c) % c;
   return bwd(P, a, ivl(lo, hi));
}

static bool bwd_arith(prob_t *P, const node_t *nd, ivl_t r)
{
   const int a = nd->a, b = nd->b;
   const ivl_t ia = P->iv[a], ib = P->iv[b];
   const unsigned w = nd->width;
   const bool s = nd->sgn;
   const i128 tl = tmin(w, s), th = tmax(w, s);

   switch ((crv_binary_t)nd->op) {
   case CRV_ADD:
      return bwd(P, a, wrap(ivl(r.lo - ib.hi, r.hi - ib.lo), w, s))
         && bwd(P, b, wrap(ivl(r.lo - P->iv[a].hi, r.hi - P->iv[a].lo),
                           w, s));
   case CRV_SUB:
      return bwd(P, a, wrap(ivl(r.lo + ib.lo, r.hi + ib.hi), w, s))
         && bwd(P, b, wrap(ivl(P->iv[a].lo - r.hi, P->iv[a].hi - r.lo),
                           w, s));
   case CRV_MUL:
      {
         const ivl_t raw = iv_mul(ia, ib);
         if (raw.lo < tl || raw.hi > th)
            return true;
         else if (iv_pt(ib) && ib.lo != 0)
            return bwd(P, a, iv_divc(r, ib.lo));
         else if (iv_pt(ia) && ia.lo != 0)
            return bwd(P, b, iv_divc(r, ia.lo));
         return true;
      }
   case CRV_DIV:
      if (iv_pt(ib) && ib.lo > 0) {
         const i128 c = ib.lo;
         i128 lo, hi;
         if (__builtin_mul_overflow(r.lo, c, &lo)
             || __builtin_mul_overflow(r.hi, c, &hi))
            return true;
         if (r.lo <= 0) lo -= c - 1;
         if (r.hi >= 0) hi += c - 1;
         return bwd(P, a, ivl(lo, hi));
      }
      return true;
   case CRV_MOD:
      if (iv_pt(ib) && iv_pt(r))
         return trim_mod(P, a, ib.lo, r.lo);
      return true;
   case CRV_AND:
      if (iv_pt(r)) {
         // Only bits in the mask can be set
         if (iv_pt(ib) && ib.lo >= 0 && (r.lo & ~ib.lo) != 0)
            return false;
         else if (iv_pt(ia) && ia.lo >= 0 && (r.lo & ~ia.lo) != 0)
            return false;
         else if (iv_pt(ib) && ib.lo > 0 && ((ib.lo + 1) & ib.lo) == 0)
            return trim_mod(P, a, ib.lo + 1, r.lo);
         else if (iv_pt(ia) && ia.lo > 0 && ((ia.lo + 1) & ia.lo) == 0)
            return trim_mod(P, b, ia.lo + 1, r.lo);
      }
      if (ia.lo >= 0 && ib.lo >= 0)
         return bwd(P, a, ivl(r.lo, BIG)) && bwd(P, b, ivl(r.lo, BIG));
      return true;
   case CRV_OR:
      if (ia.lo >= 0 && ib.lo >= 0)
         return bwd(P, a, ivl(-BIG, r.hi)) && bwd(P, b, ivl(-BIG, r.hi));
      return true;
   case CRV_XOR:
      if (iv_pt(r) && (iv_pt(ib) || iv_pt(ia))) {
         const i128 x = nrm(r.lo ^ (iv_pt(ib) ? ib.lo : ia.lo), w, s);
         return bwd(P, iv_pt(ib) ? a : b, ivl(x, x));
      }
      return true;
   case CRV_SHL:
      if (iv_pt(ib) && ib.lo >= 0 && ib.lo < 63) {
         const i128 m = (i128)1 << (int)ib.lo;
         const ivl_t raw = iv_mul(ia, ivl(m, m));
         if (raw.lo >= tl && raw.hi <= th)
            return bwd(P, a, iv_divc(r, m));
      }
      return true;
   case CRV_SHR:
   case CRV_ASR:
      if (iv_pt(ib) && ib.lo >= 0 && ib.lo < 63
          && (nd->op == CRV_ASR || ia.lo >= 0)) {
         const i128 m = (i128)1 << (int)ib.lo;
         return bwd(P, a, ivl(r.lo * m, r.hi * m + (m - 1)));
      }
      return true;
   default:
      return true;
   }
}

static bool bwd_member(prob_t *P, const node_t *nd, ivl_t r)
{
   const int a = nd->a;
   const node_t *an = &P->c->nodes[a];
   const int var = node_var(P, an);
   const amark_t m = arena_mark(&P->tmp);
   const range_t *rr;
   int n;
   bool ok;

   if (is_true(r) || (r.lo == 1 && r.hi == 1)) {
      n = node_ranges(P, nd, false, &rr);
      if (var >= 0) {
         ok = var_apply(P, var, rr, n, true);
         if (ok)
            P->iv[a] = dom_iv(&P->vars[var].dom);
      }
      else {
         const ivl_t x = P->iv[a];
         i128 lo, hi;
         if (rng_next(rr, n, x.lo, &lo) && rng_prev(rr, n, x.hi, &hi))
            ok = bwd(P, a, ivl(lo, hi));
         else
            ok = false;
      }
   }
   else {
      n = node_ranges(P, nd, true, &rr);
      if (var >= 0) {
         ok = var_apply(P, var, rr, n, false);
         if (ok)
            P->iv[a] = dom_iv(&P->vars[var].dom);
      }
      else {
         // Trim from the ends only
         ivl_t x = P->iv[a];
         for (int i = 0; i < n; i++) {
            if (rr[i].lo <= x.lo && rr[i].hi >= x.lo)
               x.lo = rr[i].hi + 1;
         }
         for (int i = n - 1; i >= 0; i--) {
            if (rr[i].lo <= x.hi && rr[i].hi >= x.hi)
               x.hi = rr[i].lo - 1;
         }
         ok = bwd(P, a, x);
      }
   }

   arena_reset(&P->tmp, m);
   return ok;
}

static bool bwd(prob_t *P, int n, ivl_t r)
{
   const ivl_t cur = P->iv[n];
   const node_t *nd = &P->c->nodes[n];
   r = isect(r, cur);
   if (iv_empty(r))
      return false;

   const int var = node_var(P, nd);
   if (var >= 0) {
      if (!var_clip(P, var, r.lo, r.hi))
         return false;
      P->iv[n] = dom_iv(&P->vars[var].dom);
      return true;
   }
   else if (r.lo == cur.lo && r.hi == cur.hi)
      return true;

   P->iv[n] = r;
   P->work++;

   switch ((nkind_t)nd->kind) {
   case N_BIN:
      switch ((crv_binary_t)nd->op) {
      case CRV_LOG_AND:
         if (r.lo == 1)
            return bwd_true(P, nd->a) && bwd_true(P, nd->b);
         else if (r.hi == 0) {
            if (is_true(P->iv[nd->a]))
               return bwd_false(P, nd->b);
            else if (is_true(P->iv[nd->b]))
               return bwd_false(P, nd->a);
         }
         return true;
      case CRV_LOG_OR:
         if (r.hi == 0)
            return bwd_false(P, nd->a) && bwd_false(P, nd->b);
         else if (r.lo == 1) {
            if (is_false(P->iv[nd->a]))
               return bwd_true(P, nd->b);
            else if (is_false(P->iv[nd->b]))
               return bwd_true(P, nd->a);
            return bwd_or_union(P, n);
         }
         return true;
      case CRV_IMPLIES:
         if (r.hi == 0)
            return bwd_true(P, nd->a) && bwd_false(P, nd->b);
         else if (r.lo == 1) {
            if (is_true(P->iv[nd->a]))
               return bwd_true(P, nd->b);
            else if (is_false(P->iv[nd->b]))
               return bwd_false(P, nd->a);
         }
         return true;
      case CRV_EQ: case CRV_NE: case CRV_LT:
      case CRV_LE: case CRV_GT: case CRV_GE:
         if (r.lo == 1)
            return enforce_rel(P, nd->op, nd->a, nd->b);
         else if (r.hi == 0)
            return enforce_rel(P, negate_rel(nd->op), nd->a, nd->b);
         return true;
      default:
         return bwd_arith(P, nd, r);
      }

   case N_UN:
      switch ((crv_unary_t)nd->op) {
      case CRV_NEG:
         return bwd(P, nd->a, wrap(ivl(-r.hi, -r.lo), nd->width, nd->sgn));
      case CRV_INV:
         if (nd->sgn)
            return bwd(P, nd->a, ivl(-r.hi - 1, -r.lo - 1));
         else {
            const i128 mx = tmax(nd->width, false);
            return bwd(P, nd->a, ivl(mx - r.hi, mx - r.lo));
         }
      case CRV_LOG_NOT:
         if (r.lo == 1)
            return bwd_false(P, nd->a);
         else if (r.hi == 0)
            return bwd_true(P, nd->a);
         return true;
      case CRV_REDUCE_OR:
         if (r.lo == 1)
            return bwd_true(P, nd->a);
         else if (r.hi == 0)
            return bwd_false(P, nd->a);
         return true;
      case CRV_REDUCE_AND:
         if (r.lo == 1) {
            const node_t *an = &P->c->nodes[nd->a];
            const i128 ones = an->sgn ? -1 : (i128)wmask(an->width);
            return bwd(P, nd->a, ivl(ones, ones));
         }
         return true;
      case CRV_POPCNT:
         {
            const node_t *an = &P->c->nodes[nd->a];
            if (r.hi <= 0)
               return bwd(P, nd->a, ivl(0, 0));
            else if (r.lo >= an->width) {
               const i128 ones = an->sgn ? -1 : (i128)wmask(an->width);
               return bwd(P, nd->a, ivl(ones, ones));
            }
            return true;
         }
      default:
         return true;
      }

   case N_CAST:
      {
         const ivl_t a = P->iv[nd->a];
         if (a.lo >= tmin(nd->width, nd->sgn)
             && a.hi <= tmax(nd->width, nd->sgn))
            return bwd(P, nd->a, r);
         else if (iv_pt(r) && a.lo >= 0 && nd->width < 64)
            return trim_mod(P, nd->a, (i128)1 << nd->width,
                            r.lo & (i128)wmask(nd->width));
         else if (nd->width <= 64) {
            // The operand is one of the values congruent to the result
            const i128 m = (i128)1 << nd->width;
            const i128 rl = MAX(r.lo, tmin(nd->width, nd->sgn));
            const i128 rh = MIN(r.hi, tmax(nd->width, nd->sgn));
            if (rl > rh)
               return false;

            const i128 kmin = cdiv(a.lo - rh, m), kmax = fdiv(a.hi - rl, m);
            if (kmin > kmax)
               return false;

            const i128 lo = MAX(a.lo, rl + kmin * m);
            const i128 hi = MIN(a.hi, rh + kmax * m);
            return lo <= hi && bwd(P, nd->a, ivl(lo, hi));
         }
         return true;
      }

   case N_COND:
      {
         const ivl_t t = P->iv[nd->a];
         if (is_true(t))
            return bwd(P, nd->b, r);
         else if (is_false(t))
            return bwd(P, nd->c, r);

         const bool okb = !iv_empty(isect(P->iv[nd->b], r));
         const bool okc = !iv_empty(isect(P->iv[nd->c], r));
         if (!okb && !okc)
            return false;
         else if (!okb)
            return bwd_false(P, nd->a) && bwd(P, nd->c, r);
         else if (!okc)
            return bwd_true(P, nd->a) && bwd(P, nd->b, r);
         return true;
      }

   case N_INSIDE:
   case N_DIST:
      if (r.lo == 1 || r.hi == 0)
         return bwd_member(P, nd, r);
      return true;

   case N_ASUM:
      {
         const int base = P->ebase[nd->aux];
         const unsigned size = P->sizes[nd->aux];
         i128 lo = 0, hi = 0;
         for (unsigned i = 0; i < size; i++) {
            lo += P->vars[base + i].dom.lo;
            hi += P->vars[base + i].dom.hi;
         }
         if (lo < tmin(nd->width, nd->sgn) || hi > tmax(nd->width, nd->sgn))
            return true;

         P->work += size / 8;
         for (unsigned i = 0; i < size; i++) {
            const dom_t *d = &P->vars[base + i].dom;
            if (!var_clip(P, base + i, r.lo - (hi - d->hi),
                          r.hi - (lo - d->lo)))
               return false;
         }
         return true;
      }

   case N_AELEM:
      {
         const i128 size = P->sizes[nd->aux];
         const int base = P->ebase[nd->aux];
         if (!bwd(P, nd->a, ivl(0, size - 1)))
            return false;

         ivl_t x = P->iv[nd->a];
         while (x.lo <= x.hi
                && iv_empty(isect(dom_iv(&P->vars[base + (int)x.lo].dom), r)))
            x.lo++;
         while (x.lo <= x.hi
                && iv_empty(isect(dom_iv(&P->vars[base + (int)x.hi].dom), r)))
            x.hi--;
         P->work += (int64_t)(P->iv[nd->a].hi - P->iv[nd->a].lo) / 8;
         if (!bwd(P, nd->a, x))
            return false;
         if (iv_pt(x) && !var_clip(P, base + (int)x.lo, r.lo, r.hi))
            return false;
         return true;
      }

   default:
      return true;
   }
}

static int i128_cmp(const void *a, const void *b)
{
   const i128 x = *(const i128 *)a, y = *(const i128 *)b;
   return x < y ? -1 : x > y ? 1 : 0;
}

static bool in_sorted(const i128 *v, int n, i128 x)
{
   int a = 0, b = n;
   while (a < b) {
      const int m = (a + b) / 2;
      if (v[m] < x)
         a = m + 1;
      else
         b = m;
   }
   return a < n && v[a] == x;
}

static bool prop_unique(prob_t *P, const pcons_t *pc)
{
   const int *nodes = P->un + pc->unstart;
   const int cnt = pc->unn;
   if (cnt < 2)
      return true;

   const amark_t mark = arena_mark(&P->tmp);
   i128 *pts = arena_alloc(&P->tmp, cnt * sizeof(i128));
   bool ok = true;

   for (int it = 0; it < 8 && ok; it++) {
      const uint64_t ch0 = P->changed;
      int np = 0;
      ivl_t h = ivl(BIG, -BIG);

      P->pass++;
      for (int i = 0; i < cnt; i++) {
         const ivl_t x = fwd(P, nodes[i]);
         if (iv_empty(x)) {
            ok = false;
            break;
         }
         h = hull(h, x);
         if (iv_pt(x))
            pts[np++] = x.lo;
      }
      if (!ok)
         break;

      qsort(pts, np, sizeof(i128), i128_cmp);
      for (int i = 1; i < np; i++) {
         if (pts[i] == pts[i - 1])
            ok = false;
      }
      P->work += cnt / 4;

      const i128 span = h.hi - h.lo + 1;
      if (!ok || span < cnt) {
         ok = false;
         break;
      }

      if (span <= SMALL_HULL) {
         // Pigeonhole: count the distinct values that can be used
         const amark_t m2 = arena_mark(&P->tmp);
         int32_t *diff = arena_alloc(&P->tmp, ((size_t)span + 1) * 4);
         memset(diff, 0, ((size_t)span + 1) * 4);
         for (int i = 0; i < cnt; i++) {
            const dom_t *d = node_dom(P, &P->c->nodes[nodes[i]]);
            if (d != NULL && d->rs != NULL) {
               for (int k = 0; k < dom_nr(d); k++) {
                  const range_t r = dom_range(d, k);
                  diff[(size_t)(r.lo - h.lo)]++;
                  diff[(size_t)(r.hi - h.lo) + 1]--;
               }
            }
            else {
               const ivl_t x = P->iv[nodes[i]];
               diff[(size_t)(x.lo - h.lo)]++;
               diff[(size_t)(x.hi - h.lo) + 1]--;
            }
         }
         int distinct = 0, run = 0;
         for (size_t i = 0; i < (size_t)span; i++) {
            run += diff[i];
            distinct += run > 0;
         }
         arena_reset(&P->tmp, m2);
         P->work += (int64_t)span / 8;
         if (distinct < cnt) {
            ok = false;
            break;
         }
      }

      if (np == 0)
         break;

      // Values which have only now been assigned are removed from the
      // others in full, anything else just from the ends
      range_t *pr = arena_alloc(&P->tmp, np * sizeof(range_t));
      int nnew = 0;
      for (int i = 0; i < cnt; i++) {
         uint8_t *done = &P->uproc[pc->unstart + i];
         if (!*done && iv_pt(P->iv[nodes[i]])) {
            pr[nnew++] = (range_t){ P->iv[nodes[i]].lo, P->iv[nodes[i]].lo };
            *done = 1;
            const trail_t t = { -2 - (pc->unstart + i), { 0 } };
            VEC_PUSH(P->trail, P->ntrail, P->ctrail, t);
         }
      }
      const int npr = rng_merge(pr, nnew);

      for (int i = 0; i < cnt && ok; i++) {
         const int n = nodes[i];
         if (iv_pt(P->iv[n]))
            continue;

         for (;;) {
            const ivl_t x = P->iv[n];
            if (iv_pt(x))
               break;
            else if (in_sorted(pts, np, x.lo))
               ok = prune(P, n, x.lo);
            else if (in_sorted(pts, np, x.hi))
               ok = prune(P, n, x.hi);
            else
               break;
            if (!ok)
               break;
         }

         const int var = node_var(P, &P->c->nodes[n]);
         if (ok && var >= 0 && !iv_pt(P->iv[n])) {
            const dom_t *d = &P->vars[var].dom;
            if (d->hi - d->lo < SMALL_HULL) {
               // Only the values within the domain
               int a = 0, b = npr;
               while (a < b) {
                  const int m = (a + b) / 2;
                  if (pr[m].hi >= d->lo)
                     b = m;
                  else
                     a = m + 1;
               }
               int e = a;
               while (e < npr && pr[e].lo <= d->hi)
                  e++;
               if (e > a)
                  ok = var_apply(P, var, pr + a, e - a, false);
               if (ok)
                  P->iv[n] = dom_iv(&P->vars[var].dom);
            }
         }
      }

      if (P->changed == ch0)
         break;
   }

   arena_reset(&P->tmp, mark);
   return ok;
}

static bool prop_cons(prob_t *P, const pcons_t *pc)
{
   if (pc->kind == C_UNIQUE)
      return prop_unique(P, pc);

   for (int it = 0; it < 3; it++) {
      const uint64_t ch0 = P->changed;
      P->pass++;
      const ivl_t r = fwd(P, pc->root);
      if (iv_empty(r) || is_false(r))
         return false;
      else if (!can0(r))
         return true;
      else if (!bwd_true(P, pc->root))
         return false;
      else if (P->changed == ch0)
         break;
   }
   return true;
}

// Sweeping up then down the constraints reaches the fixpoint of a chain of
// bounds in two passes where a queue would take a number proportional to
// the length of the chain
static bool propagate(prob_t *P)
{
   bool ok = true, up = true;
   int pos = 0;

   while (P->qn > 0) {
      const int ci = next_pending(P, pos, up);
      if (ci < 0) {
         up = !up;
         pos = up ? 0 : P->ncons - 1;
         continue;
      }

      P->pend[ci >> 6] &= ~(UINT64_C(1) << (ci & 63));
      P->qn--;
      pos = up ? ci + 1 : ci - 1;

      P->curc = ci;
      ok = prop_cons(P, &P->cons[ci]);
      P->curc = -1;

      if (!ok) {
         if (debug_level())
            fprintf(stderr, "crv: constraint %d of %d (line %d) failed "
                    "(oob=%d)\n", ci, P->ncons, P->cons[ci].line, P->oob);
         break;
      }
      else if (P->work > P->budget) {
         P->oob = true;
         ok = false;
         break;
      }
   }

   if (P->qn > 0) {
      memset(P->pend, 0, ((P->ncons + 63) / 64) * sizeof(uint64_t));
      P->qn = 0;
   }
   return ok;
}

////////////////////////////////////////////////////////////////////////////////
// Search

typedef enum { S_SAT, S_UNSAT, S_GIVEUP } sres_t;

typedef enum { F_VALUE, F_HALF1, F_HALF2 } fkind_t;

typedef struct {
   int     var;
   i128    val, lo, hi;
   int     trail;
   int     pos;
   fkind_t kind;
} frame_t;

typedef struct {
   uint32_t a, b, c;
   uint64_t r;
   int      var;
} okey_t;

static int okey_cmp(const void *x, const void *y)
{
   const okey_t *a = x, *b = y;
   if (a->a != b->a) return a->a < b->a ? -1 : 1;
   if (a->b != b->b) return a->b < b->b ? -1 : 1;
   if (a->c != b->c) return a->c < b->c ? -1 : 1;
   return a->r < b->r ? -1 : a->r > b->r ? 1 : 0;
}

static unsigned ilog2_128(i128 n)
{
   unsigned r = 0;
   while (n > 1) {
      n >>= 1;
      r++;
   }
   return r;
}

static void compute_levels(prob_t *P)
{
   const crv_t *c = P->c;
   for (int round = 0; round < 16; round++) {
      bool changed = false;
      for (unsigned i = 0; i < c->nedges; i++) {
         const edge_t *e = &c->edges[i];
         unsigned bl = 0;
         if (!e->array)
            bl = P->vars[e->before].level;
         else {
            bl = P->vars[P->nsc + e->before].level;
            const int base = P->ebase[e->before];
            for (int k = 0; base >= 0 && k < (int)P->sizes[e->before]; k++) {
               if (P->vars[base + k].level > bl)
                  bl = P->vars[base + k].level;
            }
         }
         if (P->vars[e->after].level < bl + 1) {
            P->vars[e->after].level = bl + 1;
            changed = true;
         }
      }
      if (!changed)
         break;
   }

   for (int i = 0; i < P->ndec; i++) {
      const dec_t *d = &P->dec[i];
      for (int k = 0; k < d->nf; k++)
         P->vars[d->f0 + k].level = P->vars[d->var].level;
   }
}

static int build_order(prob_t *P, int attempt, int **out)
{
   okey_t *keys = NEW(okey_t, P->nv);
   int n = 0;
   for (int v = 0; v < P->nv; v++) {
      const pvar_t *pv = &P->vars[v];
      if (pv->dom.lo == pv->dom.hi || pv->derived)
         continue;

      okey_t *k = &keys[n++];
      k->var = v;
      k->a = pv->rc ? 0 : 1;
      k->b = pv->size ? 1 << 20 : pv->level;
      k->c = pv->dist >= 0 || pv->ngd > 0 ? 0 : pv->size ? 1 : 2;
      if (attempt > 0)
         k->c = k->c * 128 + ilog2_128(dom_count(&pv->dom, pv->dom.lo,
                                                  pv->dom.hi));
      k->r = rnd64();
   }

   qsort(keys, n, sizeof(okey_t), okey_cmp);

   int *ord = NEW(int, n);
   for (int i = 0; i < n; i++)
      ord[i] = keys[i].var;
   free(keys);

   *out = ord;
   return n;
}

static i128 pick_randc(prob_t *P, const pvar_t *pv)
{
   randc_t *rc = pv->rc;
   const dom_t *d = &pv->dom;
   uint32_t cnt = 0;

   if (rc->n == 0) {
      const i128 total = dom_count(d, d->lo, d->hi);
      if (total <= 4096) {
         i128 *unused = NEW(i128, (size_t)total);
         int nunused = 0;
         P->work += (int)total / 8 + 1;
         for (int i = 0; i < (int)total; i++) {
            const i128 v = dom_nth(d, d->lo, d->hi, i);
            if (!randc_used(rc, randc_key((uint64_t)v, pv->width)))
               unused[nunused++] = v;
         }
         i128 result;
         if (nunused > 0)
            result = unused[rnd_below(nunused)];
         else {
            randc_clear(rc);
            result = dom_nth(d, d->lo, d->hi, rnd_i128(total));
         }
         free(unused);
         return result;
      }

      i128 v = 0;
      for (int tries = 0; tries < 32; tries++) {
         v = dom_nth(d, d->lo, d->hi, rnd_i128(total));
         if (!randc_used(rc, randc_key((uint64_t)v, pv->width)))
            break;
      }
      return v;
   }

   if (rc->left > 0 && dom_count(d, d->lo, d->hi) == rc->n)
      return nrm(rc->vals[rnd_below(rc->left)], pv->width, pv->sgn);

   P->work += rc->left / 8 + 1;
   for (uint32_t i = 0; i < rc->left; i++) {
      if (dom_has(d, nrm(rc->vals[i], pv->width, pv->sgn)))
         cnt++;
   }

   if (cnt == 0)
      return dom_nth(d, d->lo, d->hi, rnd_i128(dom_count(d, d->lo, d->hi)));

   uint32_t k = rnd_below(cnt);
   for (uint32_t i = 0; i < rc->left; i++) {
      const i128 v = nrm(rc->vals[i], pv->width, pv->sgn);
      if (dom_has(d, v) && k-- == 0)
         return v;
   }
   return d->lo;
}

static bool pick_dist(prob_t *P, const pvar_t *pv, int dist, i128 *out)
{
   const cdist_t *cd = &P->c->dists[dist];
   const dom_t *d = &pv->dom;
   double total = 0.0;

   P->work += cd->npr / 4 + 1;
   for (unsigned i = 0; i < cd->npr; i++)
      total += cd->unit[i] * (double)dom_count(d, cd->pr[i].lo, cd->pr[i].hi);

   if (total <= 0.0)
      return false;

   double r = rnd_dbl() * total;
   int last = -1;
   for (unsigned i = 0; i < cd->npr; i++) {
      const i128 cnt = dom_count(d, cd->pr[i].lo, cd->pr[i].hi);
      if (cnt == 0)
         continue;
      last = i;
      const double w = cd->unit[i] * (double)cnt;
      if (r < w)
         break;
      r -= w;
   }

   const i128 cnt = dom_count(d, cd->pr[last].lo, cd->pr[last].hi);
   *out = dom_nth(d, cd->pr[last].lo, cd->pr[last].hi, rnd_i128(cnt));
   return true;
}

// Values refuted for a variable in a domain too big to remove them from
static void tabu_trunc(prob_t *P, int level)
{
   while (P->ntb > 0 && P->tb[P->ntb - 1].level > level)
      P->vars[P->tb[--P->ntb].var].ntabu--;
}

static void tabu_add(prob_t *P, int v, i128 val, int level)
{
   pvar_t *pv = &P->vars[v];
   if (pv->ntabu == pv->ctabu) {
      pv->ctabu = pv->ctabu ? pv->ctabu * 2 : 8;
      pv->tabu = xrealloc(pv->tabu, pv->ctabu * sizeof(i128));
   }
   pv->tabu[pv->ntabu++] = val;

   const struct tabu_s t = { v, level };
   VEC_PUSH(P->tb, P->ntb, P->ctb, t);
}

static bool is_tabu(const pvar_t *pv, i128 x)
{
   for (int i = 0; i < pv->ntabu; i++) {
      if (pv->tabu[i] == x)
         return true;
   }
   return false;
}

static bool pick_popcount(prob_t *P, const pvar_t *pv, i128 *out)
{
   // A value with a number of one bits in the range allowed for the count

   const ivl_t r = P->iv[pv->pop];
   const unsigned width = pv->width;
   const i128 lo = MAX(r.lo, 0), hi = MIN(r.hi, (i128)width);
   if (lo > hi)
      return false;

   for (int attempt = 0; attempt < 8; attempt++) {
      const unsigned k = lo + rnd_below((uint64_t)(hi - lo + 1));

      uint8_t pos[64];
      for (unsigned i = 0; i < width; i++)
         pos[i] = i;

      uint64_t bits = 0;
      for (unsigned i = 0; i < k; i++) {
         const unsigned j = i + rnd_below(width - i);
         const uint8_t t = pos[i];
         pos[i] = pos[j];
         pos[j] = t;
         bits |= UINT64_C(1) << pos[i];
      }

      const i128 x = nrm(bits, width, false);
      if (dom_has(&pv->dom, x)) {
         *out = x;
         return true;
      }
   }

   return false;
}

typedef struct {
   prob_t *P;
   bool    ok;
} gctx_t;

static void guard_cb(void *ctx, const node_t *nd, int n)
{
   gctx_t *g = ctx;
   int var = -1;
   switch ((nkind_t)nd->kind) {
   case N_VAR:
      var = nd->aux;
      break;
   case N_ASIZE:
      var = g->P->nsc + nd->aux;
      break;
   case N_AELEM:
   case N_INDEX:
      g->ok = false;
      return;
   default:
      return;
   }

   const dom_t *d = &g->P->vars[var].dom;
   if (d->lo != d->hi)
      g->ok = false;
   else
      g->P->vals[var] = st(d->lo);
}

// Whether a guard is true now that the variables it reads are all known
static bool guard_true(prob_t *P, int node)
{
   gctx_t g = { P, true };
   visit(P->c, node, guard_cb, &g);
   if (!g.ok)
      return false;

   ectx_t E = { .c = P->c, .vals = P->vals, .nsc = P->nsc,
                .ebase = P->ebase, .sizes = P->sizes };
   const int64_t v = ev(&E, node);
   return !E.fail && v != 0;
}

static i128 pick_val(prob_t *P, int v)
{
   const pvar_t *pv = &P->vars[v];
   const dom_t *d = &pv->dom;

   P->work += dom_nr(d) / 8;
   if (pv->rc)
      return pick_randc(P, pv);

   i128 x;
   if (pv->dist >= 0 && pick_dist(P, pv, pv->dist, &x))
      return x;

   for (int i = 0; i < pv->ngd; i++) {
      if (guard_true(P, pv->gd[i * 2]) && pick_dist(P, pv, pv->gd[i * 2 + 1], &x))
         return x;
   }

   if (pv->pop >= 0 && pick_popcount(P, pv, &x))
      return x;

   i128 n = dom_count(d, d->lo, d->hi);
   if (pv->size && n > MAX_SIZE_CHOICES)
      n = 32;
   return dom_nth(d, d->lo, d->hi, rnd_i128(n));
}

static bool check_all(prob_t *P)
{
   for (int v = 0; v < P->nv; v++)
      P->vals[v] = st(P->vars[v].dom.lo);

   for (int i = 0; i < P->ndec; i++) {
      const dec_t *d = &P->dec[i];
      uint64_t x = 0;
      for (int k = 0; k < d->nf; k++)
         x |= (uint64_t)P->vals[d->f0 + k] << d->pos[k];
      P->vals[d->var] = x;
   }

   ectx_t E = { .c = P->c, .vals = P->vals, .nsc = P->nsc,
                .ebase = P->ebase, .sizes = P->sizes };

   for (int ci = 0; ci < P->ncons; ci++) {
      const pcons_t *pc = &P->cons[ci];
      E.fail = false;
      if (pc->kind == C_REQ) {
         const int64_t v = ev(&E, pc->root);
         if (E.fail || v == 0)
            return false;
      }
      else {
         const amark_t m = arena_mark(&P->tmp);
         i128 *tmp = arena_alloc(&P->tmp, (pc->unn + 1) * sizeof(i128));
         bool ok = true;
         for (int i = 0; i < pc->unn; i++) {
            const int n = P->un[pc->unstart + i];
            tmp[i] = mathv(ev(&E, n), P->c->nodes[n].sgn);
            if (E.fail)
               ok = false;
         }
         if (ok) {
            qsort(tmp, pc->unn, sizeof(i128), i128_cmp);
            for (int i = 1; i < pc->unn; i++) {
               if (tmp[i] == tmp[i - 1])
                  ok = false;
            }
         }
         arena_reset(&P->tmp, m);
         if (!ok)
            return false;
      }
   }
   return true;
}

// Values of a variable whose domain is wider than this are bisected after
// a conflict rather than picked from at random, as with only a few of the
// bits constrained a random value is very unlikely to be a solution
#define SPLIT_MIN 64

static bool splittable(const pvar_t *pv)
{
   return !pv->rc && !pv->size && pv->dist < 0 && pv->ngd == 0 && pv->pop < 0;
}

static sres_t dfs(prob_t *P, int attempt, int limit)
{
   int *ord;
   const int nord = build_order(P, attempt, &ord);
   int cfr = nord + 1;
   frame_t *fr = NEW(frame_t, cfr);
   int depth = 0, pos = 0, fails = 0;
   sres_t res = S_GIVEUP;
   bool ok = true;

   for (;;) {
      if (P->work > P->budget) {
         P->oob = true;
         break;
      }

      int v = -1;
      while (pos < nord) {
         const dom_t *d = &P->vars[ord[pos]].dom;
         if (d->lo != d->hi) {
            v = ord[pos];
            break;
         }
         pos++;
      }

      if (v < 0) {
         if (check_all(P)) {
            res = S_SAT;
            break;
         }
         ok = false;
      }
      else {
         if (depth == cfr) {
            cfr *= 2;
            fr = xrealloc(fr, cfr * sizeof(frame_t));
         }

         const pvar_t *pv = &P->vars[v];
         const i128 n = dom_count(&pv->dom, pv->dom.lo, pv->dom.hi);
         if (pv->split && n > SPLIT_MIN) {
            // Pick one half in proportion to the number of values in it
            const i128 nl = n / 2;
            const i128 m = dom_nth(&pv->dom, pv->dom.lo, pv->dom.hi, nl - 1);
            const i128 lo = pv->dom.lo, hi = pv->dom.hi;
            const bool left = rnd_i128(n) < nl;
            frame_t f = { .var = v, .trail = P->ntrail, .pos = pos,
                          .kind = F_HALF1 };
            if (left) {
               f.lo = m + 1;
               f.hi = hi;
            }
            else {
               f.lo = lo;
               f.hi = m;
            }
            fr[depth++] = f;
            ok = left ? var_clip(P, v, lo, m) && propagate(P)
               : var_clip(P, v, m + 1, hi) && propagate(P);
         }
         else {
            i128 x = pick_val(P, v);
            if (pv->ntabu > 0
                && (i128)pv->ntabu * 64 > n) {
               for (int k = 0; k < 8 && is_tabu(pv, x); k++)
                  x = pick_val(P, v);
            }
            fr[depth++] = (frame_t){ .var = v, .val = x, .trail = P->ntrail,
                                     .pos = pos, .kind = F_VALUE };
            ok = var_clip(P, v, x, x) && propagate(P);
         }
      }

      while (!ok) {
         if (P->oob)
            goto done;
         else if (depth == 0) {
            res = S_UNSAT;
            goto done;
         }

         const frame_t f = fr[--depth];
         undo_to(P, f.trail);
         pos = f.pos;
         if (++fails > limit)
            goto done;

         P->work++;
         tabu_trunc(P, depth);

         if (f.kind == F_HALF1) {
            fr[depth++] = (frame_t){ .var = f.var, .trail = f.trail,
                                     .pos = f.pos, .kind = F_HALF2 };
            ok = var_clip(P, f.var, f.lo, f.hi) && propagate(P);
            continue;
         }
         else if (f.kind == F_HALF2)
            continue;

         pvar_t *fv = &P->vars[f.var];
         const dom_t *fd = &fv->dom;
         const i128 fn = dom_count(fd, fd->lo, fd->hi);
         if (fn > SPLIT_MIN && splittable(fv))
            fv->split = true;

         if (fn > TABU_MIN) {
            tabu_add(P, f.var, f.val, depth);
            ok = true;
         }
         else
            ok = var_remove(P, f.var, f.val) && propagate(P);
      }
   }

 done:
   tabu_trunc(P, -1);
   free(ord);
   free(fr);
   return res;
}

////////////////////////////////////////////////////////////////////////////////
// Statistics
//
// Components are the sets of variables linked by a constraint, a unique
// set or a solve before edge.  A component with one free variable cannot
// be sampled unevenly by the order of the search, only those with more
// than one can.  This only counts: it does not use the random generator or
// change the problem.

static __thread crv_call_stat_t *cur_stat;


static int uf_find(int *p, int x)
{
   while (p[x] != x) {
      p[x] = p[p[x]];
      x = p[x];
   }
   return x;
}

static void uf_union(int *p, int a, int b)
{
   a = uf_find(p, a);
   b = uf_find(p, b);
   if (a != b)
      p[a] = b;
}

static void stats_components(const prob_t *P, crv_call_stat_t *out)
{
   const crv_t *c = P->c;
   const int nv = P->nv;

   int nfields = 0;
   for (int i = 0; i < P->ndec; i++)
      nfields += P->dec[i].nf;
   const int norig = nv - nfields;

   int *uf = NEW(int, nv);
   bool *used = xcalloc(nv ? nv : 1);
   for (int i = 0; i < nv; i++)
      uf[i] = i;

   for (int ci = 0; ci < P->ncons; ci++) {
      int first = -1;
      for (int i = P->vstart[ci]; i < P->vstart[ci + 1]; i++) {
         const int v = P->vl[i];
         used[v] = true;
         if (first < 0)
            first = v;
         else
            uf_union(uf, first, v);
      }
   }

   for (int i = 0; i < P->ndec; i++) {
      const dec_t *d = &P->dec[i];
      for (int k = 0; k < d->nf; k++)
         uf_union(uf, d->var, d->f0 + k);
      used[d->var] = true;
   }

   for (unsigned e = 0; e < c->nedges; e++) {
      const int b = c->edges[e].array ? P->nsc + c->edges[e].before
         : c->edges[e].before;
      const int a = c->edges[e].after;
      if (a < nv && b < nv) {
         used[a] = used[b] = true;
         uf_union(uf, a, b);
      }
   }

   int *nfree = xcalloc((nv ? nv : 1) * sizeof(int));
   bool *comp = xcalloc(nv ? nv : 1);
   crv_call_stat_t s = { 0 };

   for (int v = 0; v < norig; v++) {
      const bool is_free = P->vars[v].dom.lo != P->vars[v].dom.hi;
      if (used[v]) {
         const int r = uf_find(uf, v);
         comp[r] = true;
         nfree[r] += is_free;
      }
      else if (is_free)
         s.loose++;
   }

   for (int v = 0; v < nv; v++) {
      if (!comp[v])
         continue;
      else if (nfree[v] == 0)
         s.stage[CRV_STAGE_NONE]++;
      else {
         s.comps++;
         s.stage[CRV_STAGE_DFS]++;
         s.freevars += nfree[v];
         if ((uint32_t)nfree[v] > s.maxfree)
            s.maxfree = nfree[v];
      }
   }

   free(nfree);
   free(comp);
   free(used);
   free(uf);

   // Several problems are solved for one call (array sizes, soft
   // constraints): keep the largest of each count
#define MAXOF(f) do { if (s.f > out->f) out->f = s.f; } while (0)
   MAXOF(comps);
   MAXOF(loose);
   MAXOF(freevars);
   MAXOF(maxfree);
   for (int i = 0; i < CRV_STAGE_COUNT; i++)
      MAXOF(stage[i]);
#undef MAXOF
   out->decomposed |= P->ndec > 0;
}

static sres_t solve_prob(prob_t *P)
{
   for (int ci = 0; ci < P->ncons; ci++)
      enqueue(P, ci);

   if (!propagate(P))
      return P->oob ? S_GIVEUP : S_UNSAT;

   if (cur_stat != NULL)
      stats_components(P, cur_stat);

   compute_levels(P);

   const int root_trail = P->ntrail;
   const amark_t am = arena_mark(&P->ar);
   int limit = 12;

   for (int attempt = 0; ; attempt++) {
      const sres_t r = dfs(P, attempt, limit);
      if (r != S_GIVEUP || P->oob)
         return r;

      undo_to(P, root_trail);
      arena_reset(&P->ar, am);
      if (limit < 100000)
         limit *= 2;
   }
}

typedef struct {
   unsigned *sizes;
   int      *ebase;
   int64_t  *vals;
} sol_t;

static void sol_free(sol_t *s)
{
   free(s->sizes);
   free(s->ebase);
   free(s->vals);
   *s = (sol_t){ 0 };
}


static prob_t *prob_build_timed(crv_t *c, int mode, const unsigned *sizes,
                                const bool *active)
{
   if (cur_stat == NULL)
      return prob_build(c, mode, sizes, active);

   const uint64_t t0 = get_timestamp_ns();
   prob_t *P = prob_build(c, mode, sizes, active);
   cur_stat->build_ns += get_timestamp_ns() - t0;
   return P;
}

static sres_t solve_prob_timed(prob_t *P)
{
   if (cur_stat == NULL)
      return solve_prob(P);

   const uint64_t t0 = get_timestamp_ns();
   const sres_t r = solve_prob(P);
   cur_stat->solve_ns += get_timestamp_ns() - t0;
   cur_stat->giveup = r == S_GIVEUP;
   return r;
}

static sres_t solve_once(crv_t *c, const bool *active, sol_t *out)
{
   unsigned *sizes = NEW(unsigned, c->narrays);
   bool rs = false, gave_up = false;
   for (unsigned a = 0; a < c->narrays; a++) {
      sizes[a] = c->arrays[a].len;
      rs |= c->arrays[a].size_rand;
   }

   for (int att = 0; att < (rs ? 4 : 1); att++) {
      if (rs) {
         prob_t *P1 = prob_build_timed(c, 1, sizes, active);
         const sres_t r = solve_prob_timed(P1);
         if (debug_level() && r != S_SAT)
            fprintf(stderr, "crv: size problem result %d work %"PRIi64" budget "
                    "%"PRIi64" oob %d nv %d ncons %d\n", r, P1->work,
                    P1->budget, P1->oob, P1->nv, P1->ncons);
         if (r == S_SAT) {
            for (unsigned a = 0; a < c->narrays; a++) {
               if (c->arrays[a].size_rand)
                  sizes[a] = P1->vals[P1->nsc + a];
            }
         }
         prob_free(P1);
         if (r == S_UNSAT)
            break;
         else if (r == S_GIVEUP) {
            gave_up = true;
            continue;
         }
      }

      prob_t *P2 = prob_build_timed(c, 2, sizes, active);
      const sres_t r = solve_prob_timed(P2);
      if (debug_level() && r != S_SAT)
         fprintf(stderr, "crv: value problem result %d (rs=%d)\n", r, rs);
      if (r == S_SAT) {
         out->vals = P2->vals;
         out->ebase = P2->ebase;
         out->sizes = sizes;
         P2->vals = NULL;
         P2->ebase = NULL;
         prob_free(P2);
         return S_SAT;
      }
      prob_free(P2);
      gave_up |= r == S_GIVEUP;
      if (!rs && r == S_UNSAT)
         break;
   }

   free(sizes);
   return gave_up ? S_GIVEUP : S_UNSAT;
}

static void commit(crv_t *c, const sol_t *s)
{
   for (unsigned v = 0; v < c->nvars; v++) {
      cvar_t *cv = &c->vars[v];
      cv->cur = s->vals[v];

      if (cv->rc) {
         randc_t *rc = (randc_t *)(uintptr_t)*cv->rc;
         if (rc->n == 0) {
            randc_insert(rc, randc_key((uint64_t)cv->cur, cv->width));
            if (cv->width < 64 && rc->nused >= UINT64_C(1) << cv->width) {
               randc_clear(rc);
            }
            continue;
         }

         const uint32_t x = (uint64_t)cv->cur & wmask(cv->width);
         uint32_t *pos = rc->vals + rc->n;
         if (pos[x] >= rc->left)
            rc->left = rc->n;

         const uint32_t last = rc->left - 1, y = rc->vals[last];
         rc->vals[pos[x]] = y;
         pos[y] = pos[x];
         rc->vals[last] = x;
         pos[x] = last;
         rc->left--;
      }
   }

   for (unsigned a = 0; a < c->narrays; a++) {
      carr_t *ar = &c->arrays[a];
      const unsigned n = s->sizes[a];
      int64_t *init = xcalloc((n ? n : 1) * sizeof(int64_t));
      for (unsigned i = 0; i < n; i++)
         init[i] = s->vals[s->ebase[a] + i];
      free(ar->init);
      ar->init = init;
      ar->ninit = n;
      ar->len = n;
   }
}


static void debug_node(const crv_t *c, int n, int depth)
{
   if (n < 0 || depth > 8) {
      fprintf(stderr, "_");
      return;
   }

   const node_t *nd = &c->nodes[n];
   switch (nd->kind) {
   case N_CONST:
      fprintf(stderr, "%"PRIi64, nd->val);
      return;
   case N_VAR:
      fprintf(stderr, "v%d", nd->aux);
      return;
   default:
      break;
   }

   if (nd->kind == N_INSIDE) {
      const cset_t *cs = &c->sets[nd->aux];
      fprintf(stderr, "(inside ");
      debug_node(c, nd->a, depth + 1);
      for (unsigned i = 0; i < cs->n; i++) {
         fprintf(stderr, " [");
         debug_node(c, cs->items[i].lo, depth + 1);
         fprintf(stderr, ":");
         debug_node(c, cs->items[i].hi, depth + 1);
         fprintf(stderr, "]");
      }
      fprintf(stderr, ")");
      return;
   }

   fprintf(stderr, "(k%d.%d ", nd->kind, nd->op);
   debug_node(c, nd->a, depth + 1);
   fprintf(stderr, " ");
   debug_node(c, nd->b, depth + 1);
   if (nd->kind == N_COND || nd->kind == N_INSIDE) {
      fprintf(stderr, " ");
      debug_node(c, nd->c, depth + 1);
   }
   fprintf(stderr, ")");
}

static crv_result_t solve_all(crv_t *c);

crv_result_t crv_solve2(crv_t *c)
{
   // The engine is read here so that a bad setting is reported once
   // however the solver is reached
   (void)crv_engine();

   if (!crv_stats_enabled())
      return solve_all(c);

   crv_call_stat_t st = { .soft = c->nsoft > 0 };
   crv_call_stat_t *saved = cur_stat;
   cur_stat = &st;
   const crv_result_t r = solve_all(c);
   cur_stat = saved;

   st.ok = r == CRV_SAT;
   st.giveup = r == CRV_GIVEUP;
   st.top = st.comps + st.loose > 0 ? CRV_STAGE_DFS : CRV_STAGE_NONE;
   crv_stats_record(&st);
   return r;
}

bool crv_solve(crv_t *c)
{
   return crv_solve2(c) == CRV_SAT;
}

static crv_result_t solve_all(crv_t *c)
{
   if (debug_level() == 2) {
      for (unsigned i = 0; i < c->ncons; i++) {
         fprintf(stderr, "crv: constraint %u line %d: ", i, c->cons[i].line);
         debug_node(c, c->cons[i].root, 0);
         fprintf(stderr, "\n");
      }
   }

   (void)debug_level();
   if (dbg.skip != NULL) {
      // Debugging aid: ignore the constraints with these indices
      for (const char *p = dbg.skip; *p; ) {
         char *end;
         const long i = strtol(p, &end, 10);
         if (end == p)
            break;
         if (i >= 0 && i < (long)c->ncons)
            c->cons[i].dead = true;
         p = *end ? end + 1 : end;
      }
   }

   const int nsoft = c->nsoft;
   bool *active = NEW(bool, nsoft);
   sol_t sol = { 0 };

   for (int i = 0; i < nsoft; i++)
      active[i] = true;

   sres_t res = solve_once(c, active, &sol);
   bool ok = res == S_SAT;

   if (!ok && nsoft > 0) {
      for (int i = 0; i < nsoft; i++)
         active[i] = false;

      // Check the hard constraints alone first so that an unsatisfiable
      // set fails after one more attempt
      res = solve_once(c, active, &sol);
      ok = res == S_SAT;

      // Keep each soft constraint in turn from the highest priority if the
      // others still allow it
      for (int k = nsoft - 1; ok && k >= 0; k--) {
         active[k] = true;

         sol_t next = { 0 };
         if (solve_once(c, active, &next) == S_SAT) {
            sol_free(&sol);
            sol = next;
         }
         else
            active[k] = false;
      }
   }

   if (!ok && debug_level()) {
      // Report a minimal unsatisfiable set of the constraints by removing
      // those which are not needed to make the problem unsatisfiable
      bool *was = NEW(bool, c->ncons);
      for (unsigned i = 0; i < c->ncons; i++) {
         was[i] = c->cons[i].dead;
         if (was[i])
            continue;

         c->cons[i].dead = true;
         sol_t t = { 0 };
         if (solve_once(c, active, &t) == S_SAT) {
            sol_free(&t);
            c->cons[i].dead = false;   // Needed to be unsatisfiable
         }
      }

      for (unsigned i = 0; i < c->ncons; i++) {
         if (!was[i] && !c->cons[i].dead)
         {
            fprintf(stderr, "crv: unsatisfiable core includes the constraint "
                    "on line %d: ", c->cons[i].line);
            debug_node(c, c->cons[i].root, 0);
            fprintf(stderr, "\n");
         }
         c->cons[i].dead = was[i];
      }
      free(was);

      for (unsigned v = 0; v < c->nvars; v++) {
         const cvar_t *cv = &c->vars[v];
         fprintf(stderr, "crv: var %u width %u cur %"PRIi64" domain size %u\n",
                 v, cv->width, cv->cur, cv->ndom);
      }
   }

   if (ok)
      commit(c, &sol);

   sol_free(&sol);
   free(active);
   return ok ? CRV_SAT : res == S_GIVEUP ? CRV_GIVEUP : CRV_UNSAT;
}
