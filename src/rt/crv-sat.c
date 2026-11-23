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
#include "rt/crv-sat.h"

#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

////////////////////////////////////////////////////////////////////////////////
// CDCL SAT core
//
// Two watched literals with blocker literals, first-UIP conflict analysis
// with recursive clause minimization, VSIDS in a binary heap with random
// decision noise, random initial polarities with phase saving, Luby restarts,
// and LBD-based clause database reduction.  Every limit is a conflict count,
// never wall-clock time, so behaviour depends only on the problem and seed.

#define VAL_FALSE 0
#define VAL_TRUE  1
#define VAL_UNDEF 2

#define NO_CLAUSE  UINT32_MAX
#define CLA_HDR    3
#define CLA_LEARNT (UINT32_C(1) << 30)
#define CLA_DEL    (UINT32_C(1) << 31)
#define CLA_LOCKED (UINT32_C(1) << 29)
#define CLA_LBD_M  ((UINT32_C(1) << 29) - 1)

typedef struct {
   uint32_t cref;
   uint32_t blocker;
} watch_t;

typedef struct {
   watch_t *items;
   unsigned n, cap;
} wlist_t;

struct _sat {
   unsigned  nvars, cvars;
   bool      ok;

   // Per variable
   uint8_t  *val;       // VAL_FALSE, VAL_TRUE, VAL_UNDEF
   uint8_t  *phase;     // Saved polarity (0 or 1)
   uint32_t *vlevel;    // Decision level when assigned
   uint32_t *reason;    // Clause cref or NO_CLAUSE
   double   *act;       // VSIDS activity
   uint8_t  *seen;      // Conflict analysis / minimization scratch
   int32_t  *hpos;      // Index in VSIDS heap or -1

   // VSIDS binary max-heap
   uint32_t *heap;
   unsigned  heap_n;
   double    var_inc;

   // Assignment trail
   uint32_t *trail;
   unsigned  trail_n, qhead;
   unsigned *trail_lim;
   unsigned  level, clim;

   // Clause arena: [size, flags|lbd, activity, lits[0..size-1]]
   uint32_t *pool;
   uint32_t  pool_n, pool_cap, wasted;

   // Watch lists indexed by literal (2 * var + sign)
   wlist_t  *watches;

   // Learned clauses
   uint32_t *learnts;
   unsigned  nlearnts, clearnts;
   unsigned  learnt_limit, learnt_inc;

   // Conflict analysis scratch
   uint32_t *an_lits;
   unsigned  an_n, an_cap;
   uint32_t *an_clear;
   unsigned  an_clear_n, an_clear_cap;
   uint32_t *min_stk;
   unsigned  min_cap;
   uint32_t *lbd_mark;
   unsigned  lbd_cap;
   uint32_t  lbd_stamp;

   // Budget and counters
   uint64_t  conflicts;
   uint64_t  budget;
   uint32_t  age;
};

static inline uint32_t lit_neg(uint32_t l) { return l ^ 1; }
static inline unsigned lit_var(uint32_t l) { return l >> 1; }

static inline bool lit_true(const sat_t *s, uint32_t l)
{
   return s->val[l >> 1] == ((l & 1) ^ 1);
}

static inline bool lit_false(const sat_t *s, uint32_t l)
{
   return s->val[l >> 1] == (l & 1);
}

static inline bool lit_undef(const sat_t *s, uint32_t l)
{
   return s->val[l >> 1] == VAL_UNDEF;
}

////////////////////////////////////////////////////////////////////////////////
// VSIDS heap

static inline bool heap_gt(const sat_t *s, unsigned a, unsigned b)
{
   return s->act[a] > s->act[b];
}

static void heap_up(sat_t *s, unsigned i)
{
   const uint32_t v = s->heap[i];
   while (i > 0) {
      const unsigned p = (i - 1) >> 1;
      const uint32_t pv = s->heap[p];
      if (!heap_gt(s, v, pv))
         break;
      s->heap[i] = pv;
      s->hpos[pv] = i;
      i = p;
   }
   s->heap[i] = v;
   s->hpos[v] = i;
}

static void heap_down(sat_t *s, unsigned i)
{
   const uint32_t v = s->heap[i];
   const unsigned n = s->heap_n;
   for (;;) {
      unsigned c = (i << 1) + 1;
      if (c >= n)
         break;
      if (c + 1 < n && heap_gt(s, s->heap[c + 1], s->heap[c]))
         c++;
      if (!heap_gt(s, s->heap[c], v))
         break;
      const uint32_t cv = s->heap[c];
      s->heap[i] = cv;
      s->hpos[cv] = i;
      i = c;
   }
   s->heap[i] = v;
   s->hpos[v] = i;
}

static void heap_insert(sat_t *s, unsigned v)
{
   if (s->hpos[v] >= 0)
      return;
   const unsigned i = s->heap_n++;
   s->heap[i] = v;
   s->hpos[v] = i;
   heap_up(s, i);
}

static unsigned heap_pop(sat_t *s)
{
   const unsigned top = s->heap[0];
   s->hpos[top] = -1;
   s->heap_n--;
   if (s->heap_n > 0) {
      s->heap[0] = s->heap[s->heap_n];
      s->hpos[s->heap[0]] = 0;
      heap_down(s, 0);
   }
   return top;
}

static void var_bump(sat_t *s, unsigned v)
{
   s->act[v] += s->var_inc;
   if (unlikely(s->act[v] > 1e100)) {
      for (unsigned i = 0; i < s->nvars; i++)
         s->act[i] *= 1e-100;
      s->var_inc *= 1e-100;
   }
   if (s->hpos[v] >= 0)
      heap_up(s, (unsigned)s->hpos[v]);
}

////////////////////////////////////////////////////////////////////////////////
// Variables, watches, clauses

static void grow_vars(sat_t *s, unsigned need)
{
   if (need <= s->cvars)
      return;

   unsigned cap = s->cvars ? s->cvars * 2 : 64;
   while (cap < need)
      cap *= 2;

   s->val = xrealloc_array(s->val, cap, sizeof(uint8_t));
   s->phase = xrealloc_array(s->phase, cap, sizeof(uint8_t));
   s->vlevel = xrealloc_array(s->vlevel, cap, sizeof(uint32_t));
   s->reason = xrealloc_array(s->reason, cap, sizeof(uint32_t));
   s->act = xrealloc_array(s->act, cap, sizeof(double));
   s->seen = xrealloc_array(s->seen, cap, sizeof(uint8_t));
   s->hpos = xrealloc_array(s->hpos, cap, sizeof(int32_t));
   s->heap = xrealloc_array(s->heap, cap, sizeof(uint32_t));
   s->trail = xrealloc_array(s->trail, cap, sizeof(uint32_t));
   s->watches = xrealloc_array(s->watches, (size_t)cap * 2, sizeof(wlist_t));

   memset(s->watches + (size_t)s->cvars * 2, 0,
          (size_t)(cap - s->cvars) * 2 * sizeof(wlist_t));
   s->cvars = cap;
}

static void watch_push(wlist_t *wl, uint32_t cref, uint32_t blocker)
{
   if (wl->n == wl->cap) {
      wl->cap = wl->cap ? wl->cap * 2 : 4;
      wl->items = xrealloc_array(wl->items, wl->cap, sizeof(watch_t));
   }
   wl->items[wl->n++] = (watch_t){ cref, blocker };
}

static uint32_t clause_alloc(sat_t *s, const uint32_t *lits, unsigned n,
                             bool learnt, unsigned lbd)
{
   const uint32_t words = CLA_HDR + n;
   if (s->pool_n + words > s->pool_cap) {
      uint32_t cap = s->pool_cap ? s->pool_cap * 2 : 4096;
      while (cap < s->pool_n + words)
         cap *= 2;
      s->pool = xrealloc_array(s->pool, cap, sizeof(uint32_t));
      s->pool_cap = cap;
   }

   const uint32_t cr = s->pool_n;
   s->pool_n += words;
   s->pool[cr + 0] = n;
   s->pool[cr + 1] = (learnt ? CLA_LEARNT : 0) | (lbd & CLA_LBD_M);
   s->pool[cr + 2] = ++s->age;
   memcpy(s->pool + cr + CLA_HDR, lits, n * sizeof(uint32_t));

   watch_push(&s->watches[lits[0]], cr, lits[1]);
   watch_push(&s->watches[lits[1]], cr, lits[0]);
   return cr;
}

sat_t *sat_new(unsigned nvars)
{
   sat_t *s = xcalloc(sizeof(sat_t));
   s->ok = true;
   s->var_inc = 1.0;
   s->learnt_limit = 2000;
   s->learnt_inc = 500;
   s->budget = 50000;

   if (nvars > 0) {
      grow_vars(s, nvars);
      for (unsigned v = 0; v < nvars; v++) {
         s->val[v] = VAL_UNDEF;
         s->phase[v] = 0;
         s->vlevel[v] = 0;
         s->reason[v] = NO_CLAUSE;
         s->act[v] = 0.0;
         s->seen[v] = 0;
         s->hpos[v] = -1;
      }
      s->nvars = nvars;
   }
   return s;
}

void sat_free(sat_t *s)
{
   if (s == NULL)
      return;
   for (unsigned i = 0; i < s->nvars * 2; i++)
      free(s->watches[i].items);
   free(s->watches);
   free(s->val);
   free(s->phase);
   free(s->vlevel);
   free(s->reason);
   free(s->act);
   free(s->seen);
   free(s->hpos);
   free(s->heap);
   free(s->trail);
   free(s->trail_lim);
   free(s->pool);
   free(s->learnts);
   free(s->an_lits);
   free(s->an_clear);
   free(s->min_stk);
   free(s->lbd_mark);
   free(s);
}

unsigned sat_new_var(sat_t *s)
{
   const unsigned v = s->nvars;
   grow_vars(s, v + 1);
   s->val[v] = VAL_UNDEF;
   s->phase[v] = 0;
   s->vlevel[v] = 0;
   s->reason[v] = NO_CLAUSE;
   s->act[v] = 0.0;
   s->seen[v] = 0;
   s->hpos[v] = -1;
   s->nvars = v + 1;
   return v;
}

void sat_set_budget(sat_t *s, uint64_t conflicts)
{
   s->budget = conflicts;
}

uint64_t sat_conflicts(const sat_t *s)
{
   return s->conflicts;
}

bool sat_value(const sat_t *s, unsigned var)
{
   assert(var < s->nvars);
   return s->val[var] == VAL_TRUE;
}

////////////////////////////////////////////////////////////////////////////////
// Propagation and backtracking

static inline void enqueue(sat_t *s, uint32_t lit, uint32_t reason)
{
   const unsigned v = lit_var(lit);
   assert(s->val[v] == VAL_UNDEF);
   s->val[v] = (lit & 1) ? VAL_FALSE : VAL_TRUE;
   s->vlevel[v] = s->level;
   s->reason[v] = reason;
   s->trail[s->trail_n++] = lit;
}

static void new_level(sat_t *s)
{
   if (s->level == s->clim) {
      s->clim = s->clim ? s->clim * 2 : 64;
      s->trail_lim = xrealloc_array(s->trail_lim, s->clim, sizeof(unsigned));
   }
   s->trail_lim[s->level++] = s->trail_n;
}

static void backtrack(sat_t *s, unsigned target)
{
   if (s->level <= target)
      return;

   const unsigned stop = s->trail_lim[target];
   while (s->trail_n > stop) {
      const uint32_t lit = s->trail[--s->trail_n];
      const unsigned v = lit_var(lit);
      s->phase[v] = s->val[v];
      s->val[v] = VAL_UNDEF;
      s->reason[v] = NO_CLAUSE;
      heap_insert(s, v);
   }
   s->qhead = stop;
   s->level = target;
}

static uint32_t propagate(sat_t *s)
{
   while (s->qhead < s->trail_n) {
      const uint32_t p = s->trail[s->qhead++];
      const uint32_t flit = lit_neg(p);
      wlist_t *wl = &s->watches[flit];
      watch_t *ws = wl->items;
      const unsigned n = wl->n;
      unsigned i = 0, j = 0;

      while (i < n) {
         watch_t w = ws[i++];
         if (lit_true(s, w.blocker)) {
            ws[j++] = w;
            continue;
         }

         uint32_t *cl = s->pool + w.cref;
         const unsigned sz = cl[0];
         uint32_t *lits = cl + CLA_HDR;
         if (lits[0] == flit) {
            lits[0] = lits[1];
            lits[1] = flit;
         }

         const uint32_t first = lits[0];
         if (lit_true(s, first)) {
            w.blocker = first;
            ws[j++] = w;
            continue;
         }

         bool moved = false;
         for (unsigned k = 2; k < sz; k++) {
            if (!lit_false(s, lits[k])) {
               lits[1] = lits[k];
               lits[k] = flit;
               watch_push(&s->watches[lits[1]], w.cref, first);
               moved = true;
               break;
            }
         }
         if (moved)
            continue;

         w.blocker = first;
         ws[j++] = w;

         if (lit_false(s, first)) {
            while (i < n)
               ws[j++] = ws[i++];
            wl->n = j;
            s->qhead = s->trail_n;
            return w.cref;
         }
         enqueue(s, first, w.cref);
      }
      wl->n = j;
   }
   return NO_CLAUSE;
}

bool sat_add_clause(sat_t *s, const uint32_t *lits, unsigned n)
{
   assert(s->level == 0);
   if (!s->ok)
      return false;

   uint32_t buf[16];
   uint32_t *tmp = n <= 16 ? buf : xmalloc_array(n, sizeof(uint32_t));
   unsigned m = 0;

   for (unsigned i = 0; i < n; i++) {
      const uint32_t l = lits[i];
      assert(lit_var(l) < s->nvars);
      if (lit_true(s, l)) {
         if (tmp != buf) free(tmp);
         return true;
      }
      if (lit_false(s, l))
         continue;

      bool dup = false;
      for (unsigned k = 0; k < m; k++) {
         if (tmp[k] == l) {
            dup = true;
            break;
         }
         if (tmp[k] == lit_neg(l)) {
            if (tmp != buf) free(tmp);
            return true;   // Tautology
         }
      }
      if (!dup)
         tmp[m++] = l;
   }

   if (m == 0)
      s->ok = false;
   else if (m == 1) {
      enqueue(s, tmp[0], NO_CLAUSE);
      if (propagate(s) != NO_CLAUSE)
         s->ok = false;
   }
   else
      clause_alloc(s, tmp, m, false, 0);

   if (tmp != buf)
      free(tmp);
   return s->ok;
}

////////////////////////////////////////////////////////////////////////////////
// First-UIP conflict analysis and clause minimization

static void an_push_lit(sat_t *s, uint32_t l)
{
   if (s->an_n == s->an_cap) {
      s->an_cap = s->an_cap ? s->an_cap * 2 : 32;
      s->an_lits = xrealloc_array(s->an_lits, s->an_cap, sizeof(uint32_t));
   }
   s->an_lits[s->an_n++] = l;
}

static void an_mark(sat_t *s, unsigned v, uint8_t tag)
{
   if (s->seen[v] == 0) {
      if (s->an_clear_n == s->an_clear_cap) {
         s->an_clear_cap = s->an_clear_cap ? s->an_clear_cap * 2 : 64;
         s->an_clear = xrealloc_array(s->an_clear, s->an_clear_cap,
                                      sizeof(uint32_t));
      }
      s->an_clear[s->an_clear_n++] = v;
   }
   s->seen[v] = tag;
}

// Recursive clause minimization: is literal l implied by the already-marked
// literals (seen == 1) and level-0 units?  Tag 2 = redundant, 3 = poisoned.
static bool lit_redundant(sat_t *s, uint32_t lit)
{
   unsigned sp = 0;
   if (s->min_cap < 32) {
      s->min_cap = 32;
      s->min_stk = xrealloc_array(s->min_stk, s->min_cap, sizeof(uint32_t));
   }
   s->min_stk[sp++] = lit;
   const unsigned top = s->an_clear_n;

   while (sp > 0) {
      const uint32_t cur = s->min_stk[--sp];
      const uint32_t r = s->reason[lit_var(cur)];
      assert(r != NO_CLAUSE);

      const uint32_t *cl = s->pool + r;
      const unsigned sz = cl[0];
      const uint32_t *lits = cl + CLA_HDR;
      for (unsigned i = 1; i < sz; i++) {
         const uint32_t q = lits[i];
         const unsigned qv = lit_var(q);
         if (s->vlevel[qv] == 0 || s->seen[qv] == 1 || s->seen[qv] == 2)
            continue;
         if (s->reason[qv] == NO_CLAUSE || s->seen[qv] == 3) {
            // Undo tentative marks as poisoned
            for (unsigned k = top; k < s->an_clear_n; k++)
               s->seen[s->an_clear[k]] = 3;
            return false;
         }
         an_mark(s, qv, 2);
         if (sp == s->min_cap) {
            s->min_cap *= 2;
            s->min_stk = xrealloc_array(s->min_stk, s->min_cap,
                                        sizeof(uint32_t));
         }
         s->min_stk[sp++] = q;
      }
   }
   return true;
}

static unsigned compute_lbd(sat_t *s, const uint32_t *lits, unsigned n)
{
   if (s->level + 1 > s->lbd_cap) {
      const unsigned cap = MAX(s->level + 1, s->lbd_cap ? s->lbd_cap * 2 : 64);
      s->lbd_mark = xrealloc_array(s->lbd_mark, cap, sizeof(uint32_t));
      memset(s->lbd_mark + s->lbd_cap, 0,
             (cap - s->lbd_cap) * sizeof(uint32_t));
      s->lbd_cap = cap;
   }
   const uint32_t st = ++s->lbd_stamp;
   if (unlikely(st == 0)) {
      memset(s->lbd_mark, 0, s->lbd_cap * sizeof(uint32_t));
      s->lbd_stamp = 1;
   }
   unsigned lbd = 0;
   for (unsigned i = 0; i < n; i++) {
      const unsigned lv = s->vlevel[lit_var(lits[i])];
      if (s->lbd_mark[lv] != s->lbd_stamp) {
         s->lbd_mark[lv] = s->lbd_stamp;
         lbd++;
      }
   }
   return lbd;
}

static void analyze(sat_t *s, uint32_t confl, unsigned *bt_level, unsigned *lbd)
{
   s->an_n = 0;
   s->an_clear_n = 0;
   an_push_lit(s, 0);   // Slot for asserting UIP literal

   unsigned path_c = 0;
   unsigned index = s->trail_n;
   uint32_t p = NO_CLAUSE;

   do {
      uint32_t *cl = s->pool + confl;
      const unsigned sz = cl[0];
      const uint32_t *lits = cl + CLA_HDR;
      if (cl[1] & CLA_LEARNT) {
         cl[2] = ++s->age;
         const unsigned nlbd = compute_lbd(s, lits, sz);
         if (nlbd < (cl[1] & CLA_LBD_M))
            cl[1] = (cl[1] & ~CLA_LBD_M) | (nlbd & CLA_LBD_M);
      }

      for (unsigned i = (p == NO_CLAUSE ? 0 : 1); i < sz; i++) {
         const uint32_t q = lits[i];
         const unsigned qv = lit_var(q);
         if (!s->seen[qv] && s->vlevel[qv] > 0) {
            var_bump(s, qv);
            an_mark(s, qv, 1);
            if (s->vlevel[qv] == s->level)
               path_c++;
            else
               an_push_lit(s, q);
         }
      }

      do {
         p = s->trail[--index];
      } while (!s->seen[lit_var(p)]);

      confl = s->reason[lit_var(p)];
      s->seen[lit_var(p)] = 0;
      path_c--;
   } while (path_c > 0);

   s->an_lits[0] = lit_neg(p);

   // Minimize learned clause
   unsigned j = 1;
   for (unsigned i = 1; i < s->an_n; i++) {
      const uint32_t l = s->an_lits[i];
      if (s->reason[lit_var(l)] == NO_CLAUSE || !lit_redundant(s, l))
         s->an_lits[j++] = l;
   }
   s->an_n = j;

   for (unsigned i = 0; i < s->an_clear_n; i++)
      s->seen[s->an_clear[i]] = 0;
   s->an_clear_n = 0;

   if (s->an_n == 1) {
      *bt_level = 0;
      *lbd = 1;
   }
   else {
      unsigned max_i = 1;
      unsigned max_lv = s->vlevel[lit_var(s->an_lits[1])];
      for (unsigned i = 2; i < s->an_n; i++) {
         const unsigned lv = s->vlevel[lit_var(s->an_lits[i])];
         if (lv > max_lv) {
            max_lv = lv;
            max_i = i;
         }
      }
      const uint32_t t = s->an_lits[1];
      s->an_lits[1] = s->an_lits[max_i];
      s->an_lits[max_i] = t;
      *bt_level = max_lv;
      *lbd = compute_lbd(s, s->an_lits, s->an_n);
   }
}

////////////////////////////////////////////////////////////////////////////////
// Clause database reduction and compaction

static const uint32_t *cmp_pool;

static int cmp_learnt(const void *a, const void *b)
{
   const uint32_t ca = *(const uint32_t *)a, cb = *(const uint32_t *)b;
   const uint32_t *pa = cmp_pool + ca, *pb = cmp_pool + cb;
   const uint32_t la = pa[1] & CLA_LBD_M, lb = pb[1] & CLA_LBD_M;
   if (la != lb)
      return la > lb ? -1 : 1;   // Higher LBD first (deleted first)
   if (pa[2] != pb[2])
      return pa[2] < pb[2] ? -1 : 1;   // Older first
   return pa[0] > pb[0] ? -1 : pa[0] < pb[0];
}

static void compact_pool(sat_t *s)
{
   const uint32_t live = s->pool_n - s->wasted;
   const uint32_t ncap = MAX(4096u, live * 2);
   uint32_t *npool = xmalloc_array(ncap, sizeof(uint32_t));
   uint32_t npos = 0;

   for (uint32_t cr = 0; cr < s->pool_n; ) {
      const uint32_t sz = s->pool[cr];
      const uint32_t fl = s->pool[cr + 1];
      const uint32_t words = CLA_HDR + sz;
      if (!(fl & CLA_DEL)) {
         memcpy(npool + npos, s->pool + cr, words * sizeof(uint32_t));
         s->pool[cr + 1] = npos;   // Forwarding offset
         npos += words;
      }
      cr += words;
   }

   for (unsigned l = 0; l < s->nvars * 2; l++) {
      wlist_t *wl = &s->watches[l];
      for (unsigned i = 0; i < wl->n; i++)
         wl->items[i].cref = s->pool[wl->items[i].cref + 1];
   }
   for (unsigned v = 0; v < s->nvars; v++) {
      if (s->reason[v] != NO_CLAUSE)
         s->reason[v] = s->pool[s->reason[v] + 1];
   }
   for (unsigned i = 0; i < s->nlearnts; i++)
      s->learnts[i] = s->pool[s->learnts[i] + 1];

   free(s->pool);
   s->pool = npool;
   s->pool_n = npos;
   s->pool_cap = ncap;
   s->wasted = 0;
}

static void reduce_db(sat_t *s)
{
   assert(s->level == 0);

   for (unsigned i = 0; i < s->trail_n; i++) {
      const uint32_t r = s->reason[lit_var(s->trail[i])];
      if (r != NO_CLAUSE)
         s->pool[r + 1] |= CLA_LOCKED;
   }

   cmp_pool = s->pool;
   qsort(s->learnts, s->nlearnts, sizeof(uint32_t), cmp_learnt);

   const unsigned cut = s->nlearnts / 2;
   unsigned kept = 0;
   for (unsigned i = 0; i < s->nlearnts; i++) {
      const uint32_t cr = s->learnts[i];
      uint32_t *cl = s->pool + cr;
      const bool keep = (i >= cut) || (cl[1] & CLA_LOCKED)
         || cl[0] <= 2 || (cl[1] & CLA_LBD_M) <= 2;
      if (keep)
         s->learnts[kept++] = cr;
      else {
         cl[1] |= CLA_DEL;
         s->wasted += CLA_HDR + cl[0];
      }
   }
   s->nlearnts = kept;

   for (unsigned i = 0; i < s->trail_n; i++) {
      const uint32_t r = s->reason[lit_var(s->trail[i])];
      if (r != NO_CLAUSE)
         s->pool[r + 1] &= ~CLA_LOCKED;
   }

   // Remove deleted clauses from watch lists
   for (unsigned l = 0; l < s->nvars * 2; l++) {
      wlist_t *wl = &s->watches[l];
      unsigned j = 0;
      for (unsigned i = 0; i < wl->n; i++) {
         if (!(s->pool[wl->items[i].cref + 1] & CLA_DEL))
            wl->items[j++] = wl->items[i];
      }
      wl->n = j;
   }

   if (s->pool_n > 131072 && s->wasted * 2 > s->pool_n)
      compact_pool(s);

   s->learnt_limit += s->learnt_inc;
   s->learnt_inc += 200;
}

////////////////////////////////////////////////////////////////////////////////
// Luby restart schedule and search

static uint64_t luby(unsigned i)
{
   unsigned size = 1, seq = 0;
   while (size < i + 1) {
      seq++;
      size = 2 * size + 1;
   }
   while (size - 1 != i) {
      size = (size - 1) >> 1;
      seq--;
      i = i % size;
   }
   return UINT64_C(1) << seq;
}

static unsigned pick_branch(sat_t *s, sat_rng_fn_t rng, void *rng_ctx,
                            bool *pol)
{
   if (rng != NULL && s->nvars > 0 && (rng(rng_ctx) & 63) == 0) {
      const unsigned start = (unsigned)(rng(rng_ctx) % s->nvars);
      for (unsigned step = 0; step < 32 && step < s->nvars; step++) {
         const unsigned v = (start + step) % s->nvars;
         if (s->val[v] == VAL_UNDEF) {
            *pol = (rng(rng_ctx) & 1) != 0;
            return v;
         }
      }
   }

   while (s->heap_n > 0) {
      const unsigned v = heap_pop(s);
      if (s->val[v] == VAL_UNDEF) {
         *pol = s->phase[v] != 0;
         return v;
      }
   }
   return UINT_MAX;
}

sat_res_t sat_solve(sat_t *s, sat_rng_fn_t rng, void *rng_ctx)
{
   if (!s->ok)
      return SAT_UNSAT;

   if (propagate(s) != NO_CLAUSE) {
      s->ok = false;
      return SAT_UNSAT;
   }

   // Initialize random polarities and a shuffled initial decision order
   s->heap_n = 0;
   uint32_t *order = xmalloc_array(s->nvars ? s->nvars : 1, sizeof(uint32_t));
   for (unsigned v = 0; v < s->nvars; v++) {
      order[v] = v;
      s->hpos[v] = -1;
      if (rng != NULL) {
         s->phase[v] = (uint8_t)(rng(rng_ctx) & 1);
         s->act[v] = (double)(rng(rng_ctx) & 0xffff) * 1e-6;
      }
   }
   if (rng != NULL && s->nvars > 1) {
      for (unsigned i = s->nvars - 1; i > 0; i--) {
         const unsigned j = (unsigned)(rng(rng_ctx) % (i + 1));
         const uint32_t t = order[i];
         order[i] = order[j];
         order[j] = t;
      }
   }
   for (unsigned i = 0; i < s->nvars; i++) {
      if (s->val[order[i]] == VAL_UNDEF)
         heap_insert(s, order[i]);
   }
   free(order);

   unsigned restart_idx = 0;
   uint64_t next_restart = luby(restart_idx) * 64;
   uint64_t since_restart = 0;

   for (;;) {
      const uint32_t confl = propagate(s);
      if (confl != NO_CLAUSE) {
         s->conflicts++;
         since_restart++;
         if (s->level == 0) {
            s->ok = false;
            return SAT_UNSAT;
         }
         if (s->budget > 0 && s->conflicts >= s->budget)
            return SAT_GIVEUP;

         unsigned bt_level, lbd;
         analyze(s, confl, &bt_level, &lbd);
         backtrack(s, bt_level);

         if (s->an_n == 1)
            enqueue(s, s->an_lits[0], NO_CLAUSE);
         else {
            const uint32_t cr = clause_alloc(s, s->an_lits, s->an_n, true, lbd);
            if (s->nlearnts == s->clearnts) {
               s->clearnts = s->clearnts ? s->clearnts * 2 : 256;
               s->learnts = xrealloc_array(s->learnts, s->clearnts,
                                           sizeof(uint32_t));
            }
            s->learnts[s->nlearnts++] = cr;
            enqueue(s, s->an_lits[0], cr);
         }

         s->var_inc /= 0.95;
      }
      else {
         if (since_restart >= next_restart) {
            backtrack(s, 0);
            since_restart = 0;
            restart_idx++;
            next_restart = luby(restart_idx) * 64;
            if (s->nlearnts >= s->learnt_limit)
               reduce_db(s);
            continue;
         }

         bool pol = false;
         const unsigned v = pick_branch(s, rng, rng_ctx, &pol);
         if (v == UINT_MAX)
            return SAT_SAT;

         new_level(s);
         enqueue(s, (v << 1) | (pol ? 0 : 1), NO_CLAUSE);
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
// Tseitin encoding from the bit-blaster AIG
//
// Root AND chains are flattened into unit literals first, propagating fixed
// bits before any clauses are built.  Patterns of the form
// ~AND(~AND(s, t), ~AND(~s, f)) = ITE(s, t, f) (which covers both XOR and
// MUX from crv-blast.c) are encoded as a single SAT variable in 4-6 clauses
// instead of 3 variables and 9 clauses.

#define SAT_LIT_FALSE (UINT32_MAX - 1)   // Even: SAT_LIT_FALSE ^ 1 == SAT_LIT_TRUE
#define SAT_LIT_TRUE  UINT32_MAX         // Odd

static bool detect_ite(const blast_t *b, unsigned g, blit_t *s, blit_t *t,
                       blit_t *f)
{
   blit_t l0, l1;
   blast_gate_fanins(b, g, &l0, &l1);
   if ((l0 & 1) == 0 || (l1 & 1) == 0)
      return false;

   const unsigned ga = l0 >> 1, gb = l1 >> 1;
   if (ga == 0 || gb == 0 || blast_gate_input(b, ga) >= 0
       || blast_gate_input(b, gb) >= 0)
      return false;

   blit_t a0, a1, b0, b1;
   blast_gate_fanins(b, ga, &a0, &a1);
   blast_gate_fanins(b, gb, &b0, &b1);

   if (a0 == (b0 ^ 1))      { *s = a0; *t = a1; *f = b1; return true; }
   else if (a0 == (b1 ^ 1)) { *s = a0; *t = a1; *f = b0; return true; }
   else if (a1 == (b0 ^ 1)) { *s = a1; *t = a0; *f = b1; return true; }
   else if (a1 == (b1 ^ 1)) { *s = a1; *t = a0; *f = b0; return true; }
   return false;
}

static inline uint32_t map_blit(const uint32_t *gmap, blit_t l)
{
   const uint32_t m = gmap[l >> 1];
   return m ^ (l & 1);
}

static bool emit_clause(sat_t *sat, const uint32_t *lits, unsigned n)
{
   uint32_t out[4];
   unsigned m = 0;
   for (unsigned i = 0; i < n; i++) {
      if (lits[i] == SAT_LIT_TRUE)
         return true;
      else if (lits[i] != SAT_LIT_FALSE)
         out[m++] = lits[i];
   }
   return sat_add_clause(sat, out, m);
}

sat_res_t sat_solve_aig(const blast_t *b, const blit_t *roots, unsigned nroots,
                        uint64_t conflict_budget, sat_rng_fn_t rng,
                        void *rng_ctx, uint8_t *input_vals)
{
   const unsigned ng = blast_gate_count(b);
   const unsigned nin = blast_input_count(b);

   int8_t *forced = xcalloc(ng ? ng : 1);
   blit_t *q = xmalloc_array((size_t)2 * ng + nroots + 1, sizeof(blit_t));
   unsigned qn = 0;

   // Step 1: Flatten top-level AND chains into forced gate polarities
   for (unsigned i = 0; i < nroots; i++)
      q[qn++] = roots[i];

   bool ok = true;
   for (unsigned qi = 0; qi < qn && ok; qi++) {
      const blit_t l = q[qi];
      if (l == BLIT_FALSE) {
         ok = false;
         break;
      }
      else if (l == BLIT_TRUE)
         continue;

      const unsigned g = l >> 1;
      const int8_t want = (l & 1) ? -1 : 1;
      if (forced[g] == -want) {
         ok = false;
         break;
      }
      else if (forced[g] == want)
         continue;

      forced[g] = want;
      if (want == 1 && blast_gate_input(b, g) < 0) {
         blit_t l0, l1;
         blast_gate_fanins(b, g, &l0, &l1);
         q[qn++] = l0;
         q[qn++] = l1;
      }
   }
   free(q);

   if (!ok) {
      free(forced);
      return SAT_UNSAT;
   }

   // Step 2: Mark the cone of needed gates, skipping hidden internals of ITE/XOR
   uint8_t *needed = xcalloc(ng ? ng : 1);
   unsigned *stk = xmalloc_array(ng + 1, sizeof(unsigned));
   unsigned sp = 0;
   for (unsigned g = 1; g < ng; g++) {
      if (forced[g] == -1 && blast_gate_input(b, g) < 0) {
         needed[g] = 1;
         stk[sp++] = g;
      }
   }

   while (sp > 0) {
      const unsigned g = stk[--sp];
      if (blast_gate_input(b, g) >= 0)
         continue;

      blit_t ch[3];
      unsigned nch;
      if (detect_ite(b, g, &ch[0], &ch[1], &ch[2]))
         nch = 3;
      else {
         blast_gate_fanins(b, g, &ch[0], &ch[1]);
         nch = 2;
      }

      for (unsigned k = 0; k < nch; k++) {
         const unsigned cg = ch[k] >> 1;
         if (cg > 0 && forced[cg] == 0 && !needed[cg]) {
            needed[cg] = 1;
            stk[sp++] = cg;
         }
      }
   }
   free(stk);

   // Step 3: Allocate SAT variables
   unsigned nsat = 0;
   for (unsigned g = 1; g < ng; g++) {
      if (forced[g] == 0 && needed[g])
         nsat++;
   }

   sat_t *sat = sat_new(nsat);
   sat_set_budget(sat, conflict_budget);

   uint32_t *gmap = xmalloc_array(ng ? ng : 1, sizeof(uint32_t));
   gmap[0] = SAT_LIT_FALSE;
   unsigned next_v = 0;
   for (unsigned g = 1; g < ng; g++) {
      if (forced[g] == 1)
         gmap[g] = SAT_LIT_TRUE;
      else if (forced[g] == -1)
         gmap[g] = SAT_LIT_FALSE;
      else if (needed[g])
         gmap[g] = (next_v++) << 1;
      else
         gmap[g] = SAT_LIT_FALSE;
   }
   assert(next_v == nsat);

   // Step 4: Emit Tseitin clauses
   for (unsigned g = 1; g < ng && ok; g++) {
      if (blast_gate_input(b, g) >= 0 || forced[g] == 1)
         continue;
      if (!needed[g] && forced[g] == 0)
         continue;

      const uint32_t gl = gmap[g];
      blit_t bs, bt, bf;
      if (detect_ite(b, g, &bs, &bt, &bf)) {
         // ~g <-> ITE(bs, bt, bf)
         const uint32_t sl = map_blit(gmap, bs);
         const uint32_t tl = map_blit(gmap, bt);
         const uint32_t fl = map_blit(gmap, bf);
         const uint32_t zl = gl ^ 1;
         const uint32_t c1[3] = { sl ^ 1, tl ^ 1, zl };
         const uint32_t c2[3] = { sl ^ 1, tl,     zl ^ 1 };
         const uint32_t c3[3] = { sl,     fl ^ 1, zl };
         const uint32_t c4[3] = { sl,     fl,     zl ^ 1 };
         ok = emit_clause(sat, c1, 3) && emit_clause(sat, c2, 3)
            && emit_clause(sat, c3, 3) && emit_clause(sat, c4, 3);
         if (ok && bt != (bf ^ 1)) {
            const uint32_t c5[3] = { tl ^ 1, fl ^ 1, zl };
            const uint32_t c6[3] = { tl,     fl,     zl ^ 1 };
            ok = emit_clause(sat, c5, 3) && emit_clause(sat, c6, 3);
         }
      }
      else {
         blit_t l0, l1;
         blast_gate_fanins(b, g, &l0, &l1);
         const uint32_t a = map_blit(gmap, l0);
         const uint32_t c = map_blit(gmap, l1);
         const uint32_t c1[2] = { gl ^ 1, a };
         const uint32_t c2[2] = { gl ^ 1, c };
         const uint32_t c3[3] = { gl, a ^ 1, c ^ 1 };
         ok = emit_clause(sat, c1, 2) && emit_clause(sat, c2, 2)
            && emit_clause(sat, c3, 3);
      }
   }

   sat_res_t res = ok ? sat_solve(sat, rng, rng_ctx) : SAT_UNSAT;
   if (res == SAT_SAT && input_vals != NULL) {
      for (unsigned i = 0; i < nin; i++) {
         const unsigned g = blast_input_gate(b, i);
         if (forced[g] == 1)
            input_vals[i] = 1;
         else if (forced[g] == -1)
            input_vals[i] = 0;
         else if (needed[g])
            input_vals[i] = sat_value(sat, gmap[g] >> 1) ? 1 : 0;
         else
            input_vals[i] = rng != NULL ? (uint8_t)(rng(rng_ctx) & 1) : 0;
      }
   }

   free(gmap);
   free(needed);
   free(forced);
   sat_free(sat);
   return res;
}
