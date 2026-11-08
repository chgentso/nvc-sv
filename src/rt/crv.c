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

void crv_require(crv_t *c, crv_node_t test, bool soft)
{
   ccons_t k = { .kind = C_REQ, .root = test,
                 .soft = soft ? c->nsoft++ : -1 };
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
} rsize_t;

static void rsize_cb(void *ctx, const node_t *nd, int n)
{
   (void)n;
   rsize_t *r = ctx;
   if ((nd->kind == N_AELEM || nd->kind == N_ASUM)
       && r->c->arrays[nd->aux].size_rand)
      r->bad = true;
}

static void add_pcons(prob_t *P, ckind_t kind, int root, int soft)
{
   pcons_t pc = { .kind = kind, .root = root, .soft = soft };
   VEC_PUSH(P->cons, P->ncons, P->ccons, pc);
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

      if (!ok)
         break;
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

static unsigned ilog2_128(i128 n)
{
   unsigned r = 0;
   while (n > 1) {
      n >>= 1;
      r++;
   }
   return r;
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


static bool solve_once(crv_t *c, const bool *active, sol_t *out)
{
   unsigned *sizes = NEW(unsigned, c->narrays);
   bool rs = false;
   for (unsigned a = 0; a < c->narrays; a++) {
      sizes[a] = c->arrays[a].len;
      rs |= c->arrays[a].size_rand;
   }

   for (int att = 0; att < (rs ? 4 : 1); att++) {
      if (rs) {
         prob_t *P1 = prob_build(c, 1, sizes, active);
         const sres_t r = solve_prob(P1);
         if (r == S_SAT) {
            for (unsigned a = 0; a < c->narrays; a++) {
               if (c->arrays[a].size_rand)
                  sizes[a] = P1->vals[P1->nsc + a];
            }
         }
         prob_free(P1);
         if (r == S_UNSAT)
            break;
         else if (r == S_GIVEUP)
            continue;
      }

      prob_t *P2 = prob_build(c, 2, sizes, active);
      const sres_t r = solve_prob(P2);
      if (r == S_SAT) {
         out->vals = P2->vals;
         out->ebase = P2->ebase;
         out->sizes = sizes;
         P2->vals = NULL;
         P2->ebase = NULL;
         prob_free(P2);
         return true;
      }
      prob_free(P2);
      if (!rs && r == S_UNSAT)
         break;
   }

   free(sizes);
   return false;
}

