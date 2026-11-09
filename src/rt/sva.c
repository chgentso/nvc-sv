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
#include "rt/model.h"
#include "rt/sva.h"
#include "vlog/vlog-node.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
   int     op;
   int     a, b;
   int     leaf;
   int64_t lo, hi;    // Negative hi is unbounded
} sva_node_t;

// Values wider than 64 bits are held in a pool and referred to by index
typedef struct {
   uint64_t *w;
   unsigned  n, cap;
} wpool_t;

#define RES_INLINE 56

typedef struct {
   int64_t   start;
   unsigned  len;
   uint8_t  *ends;    // Indexed by end tick - start + 1
   bool      open;    // More matches may be found with later ticks
   uint8_t   small[RES_INLINE];
} seqres_t;

typedef enum { TRI_FALSE, TRI_TRUE, TRI_PENDING } tri_t;

typedef struct {
   int      op;
   int      a, b, c;
   int      width;
   int      flags;
   int64_t  value;
} xnode_t;

typedef struct {
   int  kind;
   int  local;
   int  expr;
   int  width;
   int  flags;
} item_t;

typedef struct {
   int       call;
   int64_t   start;
   int64_t   v[SVA_MAX_LOCALS];
   uint16_t  unk;
} event_t;

struct _sva {
   sva_kind_t  kind;
   event_t    *events;
   unsigned    nevents, maxevents;
   int64_t     cur_start;
   xnode_t    *xnodes;
   int         nx, maxx;
   item_t     *items;
   int         nitems, maxitems;
   int         nlocals, nslots;
   bool        lmode;
   unsigned    local_unk;
   int64_t     cur_slot[SVA_MAX_SLOTS];
   uint64_t    cur_unk;
   int64_t    *shist;
   uint64_t   *uhist;
   sva_node_t *nodes;
   int         nnodes, maxnodes;
   int         root;
   uint64_t   *hist;
   int64_t     hbase;      // Tick number of hist[0]
   int64_t     nticks;
   unsigned    hcap;
   int64_t    *live;
   unsigned    nlive, maxlive;
   unsigned    passes;     // Non-vacuous successes at the last tick
   unsigned    vpasses;    // Vacuous successes at the last tick
   unsigned    fails;      // Failures at the last tick
   unsigned    atype;      // Assertion type bit of $assertcontrol
   unsigned    dtype;      // Directive type bit of $assertcontrol
   bool        has_pass;   // Has a pass statement
   bool        off;        // No new attempts are started
   bool        locked;     // Status cannot be changed
   bool        pass_nv;    // Pass statement runs on non-vacuous success
   bool        pass_vac;   // Pass statement runs on vacuous success
   bool        fail_on;    // Fail statement runs on failure
   char       *label;
   char       *path;      // Instance containing the assertion
   bool        idle;       // The last tick left nothing pending or reported
   bool        eot;    // Evaluating the attempts left at the end of the trace
   int64_t     probe_t;    // Tick whose leaves are being looked for or -1
   uint64_t    needed;     // Leaves evaluated by the attempts at probe_t
   uint64_t    needed_slots;   // Slots read by the attempts at probe_t
   int64_t     slots_t;    // Tick for which needed_slots is valid
   int64_t     needed_t;   // Tick for which needed is valid
   int64_t     last_true[SVA_MAX_LEAVES];   // Latest tick where a leaf held
   bool        wide;       // Some local variable or slot is wider than 64 bits
   int         lw[SVA_MAX_LOCALS];   // Width of a wide local variable or zero
   int         sw[SVA_MAX_SLOTS];    // Width of a wide slot or zero
   wpool_t     tpool;      // Wide values of local variables
   wpool_t     ppool;      // Wide values of the slots at each tick
   int         fhandle[SVA_MAX_FCALLS];   // Function called by SVA_X_CALL
   void       *fctx[SVA_MAX_FCALLS];
   struct {
      int64_t tick;
      int     idx;
      int64_t args[SVA_MAX_FARGS];
      int64_t result;
   }           memo[16];
   unsigned    nmemo;
};

typedef struct {
   int       ctype;
   unsigned  atype, dtype;
   char     *label;    // Hierarchical name or NULL for all assertions
   char     *base;     // Scope of the call
   int       levels;
} control_t;

static sva_t **all_svas = NULL;
static unsigned nsvas = 0, maxsvas = 0;
static control_t *controls = NULL;
static unsigned ncontrols = 0, maxcontrols = 0;

static void sva_reset_control(sva_t *s)
{
   s->off = s->locked = false;
   s->pass_nv = s->pass_vac = s->fail_on = true;
}

static unsigned control_fields(int ctype)
{
   // The parts of the status of an assertion which a control type sets

   switch (ctype) {
   case SVA_CTRL_LOCK:
   case SVA_CTRL_UNLOCK:
      return 1;
   case SVA_CTRL_ON:
   case SVA_CTRL_OFF:
   case SVA_CTRL_KILL:
      return 2;
   case SVA_CTRL_PASS_ON:
   case SVA_CTRL_PASS_OFF:
      return 4 | 8;
   case SVA_CTRL_FAIL_ON:
   case SVA_CTRL_FAIL_OFF:
      return 16;
   case SVA_CTRL_NONVACUOUS_ON:
      return 4;
   case SVA_CTRL_VACUOUS_OFF:
      return 8;
   default:
      return 0;
   }
}

static bool sva_match_name(const sva_t *s, const control_t *c,
                           const char *cand)
{
   // The name selects the assertion or the scope and the instances below
   // it down to the given number of levels (20.12)

   const size_t len = strlen(cand);

   if (s->label != NULL && s->path != NULL) {
      const size_t plen = strlen(s->path);
      if (plen + 1 + strlen(s->label) == len && strncmp(cand, s->path, plen) == 0
          && cand[plen] == '.' && strcmp(cand + plen + 1, s->label) == 0)
         return true;
   }

   if (s->path == NULL || strncmp(s->path, cand, len) != 0
       || (s->path[len] != '\0' && s->path[len] != '.'))
      return false;

   int depth = 0;
   for (const char *p = s->path + len; *p; p++) {
      if (*p == '.')
         depth++;
   }

   return c->levels <= 0 || depth < c->levels;
}

static bool sva_matches(const sva_t *s, const control_t *c)
{
   if (c->label == NULL)
      return true;

   // The name is relative to the scope of the call or any scope which
   // encloses it, and may start with the name of one of them
   char *base LOCAL = xstrdup(c->base ?: "");
   const char *dot = strchr(c->label, '.');
   const size_t first = dot ? (size_t)(dot - c->label) : strlen(c->label);

   for (;;) {
      LOCAL_TEXT_BUF tb = tb_new();
      if (base[0] != '\0') {
         tb_cat(tb, base);
         tb_append(tb, '.');
      }
      tb_cat(tb, c->label);
      if (sva_match_name(s, c, tb_get(tb)))
         return true;

      const char *last = strrchr(base, '.');
      last = last ? last + 1 : base;
      if (base[0] != '\0' && strlen(last) == first
          && strncmp(last, c->label, first) == 0) {
         LOCAL_TEXT_BUF own = tb_new();
         tb_cat(own, base);
         if (dot != NULL)
            tb_cat(own, dot);
         if (sva_match_name(s, c, tb_get(own)))
            return true;
      }

      char *cut = strrchr(base, '.');
      if (cut == NULL)
         break;
      *cut = '\0';
   }

   return false;
}

static void sva_apply_control(sva_t *s, const control_t *c)
{
   if (!(s->atype & c->atype) || !(s->dtype & c->dtype))
      return;
   else if (!sva_matches(s, c))
      return;
   else if (s->locked && c->ctype != SVA_CTRL_UNLOCK)
      return;

   switch (c->ctype) {
   case SVA_CTRL_LOCK:
      s->locked = true;
      break;
   case SVA_CTRL_UNLOCK:
      s->locked = false;
      break;
   case SVA_CTRL_ON:
      s->off = false;
      break;
   case SVA_CTRL_OFF:
      s->off = true;
      break;
   case SVA_CTRL_KILL:
      s->off = true;
      s->nlive = 0;
      break;
   case SVA_CTRL_PASS_ON:
      s->pass_nv = s->pass_vac = true;
      break;
   case SVA_CTRL_PASS_OFF:
      s->pass_nv = s->pass_vac = false;
      break;
   case SVA_CTRL_FAIL_ON:
      s->fail_on = true;
      break;
   case SVA_CTRL_FAIL_OFF:
      s->fail_on = false;
      break;
   case SVA_CTRL_NONVACUOUS_ON:
      s->pass_nv = true;
      break;
   case SVA_CTRL_VACUOUS_OFF:
      s->pass_vac = false;
      break;
   }
}

static bool control_covers(const control_t *later, const control_t *c)
{
   // The later control sets everything the earlier one did for every
   // assertion the earlier one applied to

   if ((control_fields(c->ctype) & ~control_fields(later->ctype)) != 0)
      return false;
   else if ((c->atype & ~later->atype) != 0 || (c->dtype & ~later->dtype) != 0)
      return false;
   else if (later->label == NULL)
      return true;
   else
      return c->label != NULL && strcmp(c->label, later->label) == 0
         && c->levels == later->levels
         && strcmp(c->base ?: "", later->base ?: "") == 0;
}

static char *sva_scope_path(void)
{
   rt_model_t *m = get_model_or_null();
   rt_scope_t *scope = m != NULL ? get_active_scope(m) : NULL;
   if (scope == NULL)
      return NULL;

   LOCAL_TEXT_BUF tb = tb_new();
   get_verilog_path_name(scope, tb);
   return xstrdup(tb_get(tb));
}

void sva_control(int ctype, unsigned atype, unsigned dtype, int levels,
                 const char *label)
{
   // Change the status of the matching assertions with $assertcontrol and
   // remember it for the assertions which have not been created yet

   if (control_fields(ctype) == 0)
      return;

   control_t c = {
      .ctype = ctype,
      .atype = atype,
      .dtype = dtype,
      .label = label ? xstrdup(label) : NULL,
      .base = label ? sva_scope_path() : NULL,
      .levels = levels,
   };

   for (unsigned i = 0; i < nsvas; i++)
      sva_apply_control(all_svas[i], &c);

   // Drop the earlier controls which this one overrides, but not across
   // a lock which may have kept an assertion from changing
   unsigned lock = 0;
   for (unsigned i = ncontrols; i > 0; i--) {
      if (control_fields(controls[i - 1].ctype) == 1) {
         lock = i;
         break;
      }
   }

   unsigned keep = lock;
   for (unsigned i = lock; i < ncontrols; i++) {
      if (control_covers(&c, &controls[i])) {
         free(controls[i].label);
         free(controls[i].base);
      }
      else
         controls[keep++] = controls[i];
   }
   ncontrols = keep;

   if (ncontrols == maxcontrols) {
      maxcontrols = MAX(8, maxcontrols * 2);
      controls = xrealloc_array(controls, maxcontrols, sizeof(control_t));
   }
   controls[ncontrols++] = c;
}

static void sva_replay_controls(sva_t *s)
{
   sva_reset_control(s);
   for (unsigned i = 0; i < ncontrols; i++)
      sva_apply_control(s, &controls[i]);
}

void sva_set_label(sva_t *s, const char *label)
{
   free(s->label);
   s->label = xstrdup(label);
   sva_replay_controls(s);
}

sva_t *sva_new(sva_kind_t kind, unsigned flags)
{
   sva_t *s = xcalloc(sizeof(sva_t));
   if (nsvas == maxsvas) {
      maxsvas = MAX(16, maxsvas * 2);
      all_svas = xrealloc_array(all_svas, maxsvas, sizeof(sva_t *));
   }
   all_svas[nsvas++] = s;
   s->kind = kind;
   s->path = sva_scope_path();
   s->root = -1;
   s->probe_t = s->needed_t = s->slots_t = -1;
   for (int i = 0; i < 16; i++)
      s->memo[i].tick = INT64_MIN;
   for (int i = 0; i < SVA_MAX_LEAVES; i++)
      s->last_true[i] = -1;

   s->atype = kind == SVA_EXPECT ? SVA_ATYPE_EXPECT : SVA_ATYPE_CONCURRENT;
   if (kind == SVA_COVER_PROPERTY || kind == SVA_COVER_SEQUENCE)
      s->dtype = SVA_DTYPE_COVER;
   else if (flags & SVA_F_ASSUME)
      s->dtype = SVA_DTYPE_ASSUME;
   else
      s->dtype = SVA_DTYPE_ASSERT;
   s->has_pass = !!(flags & SVA_F_PASS);

   sva_replay_controls(s);
   return s;
}

static sva_call_fn_t  call_fn = NULL;
static void          *call_jit = NULL;

void sva_set_caller(sva_call_fn_t fn, void *jit)
{
   call_fn = fn;
   call_jit = jit;
}

void sva_set_callfn(sva_t *s, int idx, int handle, void *ctx)
{
   assert(idx >= 0 && idx < SVA_MAX_FCALLS);
   s->fhandle[idx] = handle;
   s->fctx[idx] = ctx;
}

void sva_free(sva_t *s)
{
   for (unsigned i = 0; i < nsvas; i++) {
      if (all_svas[i] == s) {
         all_svas[i] = all_svas[--nsvas];
         break;
      }
   }

   free(s->label);
   free(s->path);
   free(s->nodes);
   free(s->xnodes);
   free(s->events);
   free(s->items);
   free(s->shist);
   free(s->uhist);
   free(s->hist);
   free(s->live);
   free(s->tpool.w);
   free(s->ppool.w);
   free(s);
}

int sva_node(sva_t *s, int op, int a, int b, int leaf, int64_t lo, int64_t hi)
{
   if (s->nnodes == s->maxnodes) {
      s->maxnodes = MAX(16, s->maxnodes * 2);
      s->nodes = xrealloc_array(s->nodes, s->maxnodes, sizeof(sva_node_t));
   }

   sva_node_t *n = &(s->nodes[s->nnodes]);
   n->op = op;
   n->a = a;
   n->b = b;
   n->leaf = leaf;
   n->lo = lo;
   n->hi = hi;
   return s->nnodes++;
}

void sva_config(sva_t *s, int nlocals, int nslots, unsigned unknown)
{
   assert(nlocals <= SVA_MAX_LOCALS && nslots <= SVA_MAX_SLOTS);
   s->lmode = true;
   s->nlocals = nlocals;
   s->nslots = nslots;
   s->local_unk = unknown;
}

int sva_expr(sva_t *s, int op, int a, int b, int c, int width, int flags,
             int64_t value)
{
   if (s->nx == s->maxx) {
      s->maxx = MAX(16, s->maxx * 2);
      s->xnodes = xrealloc_array(s->xnodes, s->maxx, sizeof(xnode_t));
   }

   if (width > 64)
      s->wide = true;

   xnode_t *x = &(s->xnodes[s->nx]);
   x->op = op;
   x->a = a;
   x->b = b;
   x->c = c;
   x->width = width;
   x->flags = flags;
   x->value = value;
   return s->nx++;
}

int sva_item(sva_t *s, int kind, int local, int expr, int width, int flags)
{
   if (s->nitems == s->maxitems) {
      s->maxitems = MAX(8, s->maxitems * 2);
      s->items = xrealloc_array(s->items, s->maxitems, sizeof(item_t));
   }

   item_t *it = &(s->items[s->nitems]);
   it->kind = kind;
   it->local = local;
   it->expr = expr;
   it->width = width;
   it->flags = flags;
   return s->nitems++;
}

void sva_slot(sva_t *s, int slot, int64_t value, bool unknown)
{
   assert(slot < SVA_MAX_SLOTS);
   s->cur_slot[slot] = value;
   if (unknown)
      s->cur_unk |= UINT64_C(1) << slot;
   else
      s->cur_unk &= ~(UINT64_C(1) << slot);
}

static int64_t wpool_add(wpool_t *p, const uint64_t *w)
{
   if (p->n == 0) {
      // The first entry is zero and stands for a slot not yet sampled
      if (p->w == NULL) {
         p->cap = 64;
         p->w = xcalloc_array(p->cap, SVA_WORDS * sizeof(uint64_t));
      }

      memset(p->w, 0, SVA_WORDS * sizeof(uint64_t));
      p->n = 1;
   }

   if (p->n == p->cap) {
      p->cap *= 2;
      p->w = xrealloc_array(p->w, p->cap * SVA_WORDS, sizeof(uint64_t));
   }

   memcpy(p->w + (size_t)p->n * SVA_WORDS, w, SVA_WORDS * sizeof(uint64_t));
   return p->n++;
}

static const uint64_t *wpool_get(const wpool_t *p, int64_t h)
{
   assert(h >= 0 && h < p->n);
   return p->w + (size_t)h * SVA_WORDS;
}

static void wmask(uint64_t *w, int width)
{
   for (int i = 0; i < SVA_WORDS; i++) {
      const int lo = i * 64;
      if (lo >= width)
         w[i] = 0;
      else if (lo + 64 > width)
         w[i] &= (UINT64_C(1) << (width - lo)) - 1;
   }
}

void sva_localw(sva_t *s, int local, int width)
{
   assert(local < SVA_MAX_LOCALS && width <= SVA_WORDS * 64);
   s->lw[local] = width;
   s->wide = true;
}

void sva_slotw(sva_t *s, int slot, int width, const uint64_t *bits,
               bool unknown)
{
   assert(slot < SVA_MAX_SLOTS && width <= SVA_WORDS * 64);

   uint64_t w[SVA_WORDS] = {};
   memcpy(w, bits, ((width + 63) / 64) * sizeof(uint64_t));
   wmask(w, width);

   s->wide = true;
   s->sw[slot] = width;
   s->cur_slot[slot] = wpool_add(&s->ppool, w);
   if (unknown)
      s->cur_unk |= UINT64_C(1) << slot;
   else
      s->cur_unk &= ~(UINT64_C(1) << slot);
}

void sva_set_root(sva_t *s, int node)
{
   s->root = node;
}

static bool sva_leaf(const sva_t *s, int leaf, int64_t t)
{
   if (t == s->probe_t)
      ((sva_t *)s)->needed |= UINT64_C(1) << leaf;

   return (s->hist[t - s->hbase] >> leaf) & 1;
}

static bool leaf_in_range(const sva_t *s, int leaf, int64_t from, int64_t to)
{
   // Some tick from FROM to TO, which has been seen, where the leaf holds

   if (from > to)
      return false;
   else if (to == s->nticks - 1 && s->probe_t < 0)
      return s->last_true[leaf] >= from;

   for (int64_t t = from; t <= to; t++) {
      if (sva_leaf(s, leaf, t))
         return true;
   }

   return false;
}

static void res_init(const sva_t *s, seqres_t *r, int64_t start)
{
   r->start = start;
   r->len = start <= s->nticks ? s->nticks - start + 1 : 0;
   if (r->len == 0)
      r->ends = NULL;
   else if (r->len <= RES_INLINE) {
      r->ends = r->small;
      memset(r->small, 0, r->len);
   }
   else
      r->ends = xcalloc(r->len);
   r->open = false;
}

static void res_free(seqres_t *r)
{
   if (r->ends != r->small)
      free(r->ends);
   r->ends = NULL;
}

static void res_move(seqres_t *dst, seqres_t *src)
{
   *dst = *src;
   if (src->ends == src->small)
      dst->ends = dst->small;
   src->ends = NULL;
}

static void res_set(seqres_t *r, int64_t end)
{
   const int64_t idx = end - r->start + 1;
   if (idx >= 0 && idx < r->len)
      r->ends[idx] = 1;
}

static bool res_get(const seqres_t *r, int64_t end)
{
   const int64_t idx = end - r->start + 1;
   return idx >= 0 && idx < r->len && r->ends[idx];
}

static bool res_any(const seqres_t *r)
{
   for (unsigned i = 0; i < r->len; i++) {
      if (r->ends[i])
         return true;
   }

   return false;
}

static void res_merge(seqres_t *dst, const seqres_t *src)
{
   for (unsigned i = 0; i < src->len; i++) {
      if (src->ends[i])
         res_set(dst, src->start - 1 + i);
   }
}

static bool is_seq(const sva_t *s, int id)
{
   const sva_node_t *n = &(s->nodes[id]);
   switch (n->op) {
   case SVA_LEAF:
   case SVA_DLEAF:
   case V_SVA_MATCH:
   case V_SVA_DELAY:
   case V_SVA_REPEAT:
   case V_SVA_GOTO:
   case V_SVA_NONCONS:
   case V_SVA_INTERSECT:
   case V_SVA_WITHIN:
   case V_SVA_THROUGHOUT:
   case V_SVA_FIRST_MATCH:
      return true;
   case V_SVA_AND:
   case V_SVA_OR:
      return is_seq(s, n->a) && is_seq(s, n->b);
   default:
      return false;
   }
}

static void eval_seq(const sva_t *s, int id, int64_t start, seqres_t *out);
static tri_t eval_prop(const sva_t *s, int id, int64_t start, bool *nv);

static bool in_range(int64_t k, int64_t lo, int64_t hi)
{
   return k >= lo && (hi < 0 || k <= hi);
}

static void seq_delay(const sva_t *s, const sva_node_t *n, int64_t start,
                      seqres_t *out)
{
   seqres_t ra = {};
   int64_t first = start - 1, last = start - 1;   // Candidate end ticks
   bool leading = n->a < 0;

   if (leading)
      first = last = start;
   else {
      eval_seq(s, n->a, start, &ra);
      if (ra.open)
         out->open = true;
      last = ra.start - 2 + ra.len;
   }

   // Ticks where the second operand can start; each is evaluated once
   // even if the windows after several ends of the first overlap
   const sva_node_t *nb = &(s->nodes[n->b]);
   int64_t covered = start - 1;

   for (int64_t e1 = first; e1 <= last; e1++) {
      if (!leading && !res_get(&ra, e1))
         continue;

      if (n->hi < 0 || e1 + n->hi >= s->nticks)
         out->open = true;

      const int64_t from = MAX(MAX(e1 + n->lo, start), covered + 1);
      const int64_t to = n->hi < 0 ? s->nticks - 1
         : MIN(e1 + n->hi, s->nticks - 1);

      for (int64_t s2 = from; s2 <= to; s2++) {
         if (nb->op == SVA_LEAF) {
            if (sva_leaf(s, nb->leaf, s2))
               res_set(out, s2);
            continue;
         }

         seqres_t rb;
         eval_seq(s, n->b, s2, &rb);
         res_merge(out, &rb);
         out->open |= rb.open;
         res_free(&rb);
      }

      covered = MAX(covered, to);
   }

   res_free(&ra);
}

static void seq_repeat(const sva_t *s, const sva_node_t *n, int64_t start,
                       seqres_t *out)
{
   seqres_t prev, next;
   res_init(s, &prev, start);
   if (prev.len > 0)
      res_set(&prev, start - 1);

   if (n->lo == 0 && out->len > 0)
      res_set(out, start - 1);

   for (int64_t k = 1; (n->hi < 0 || k <= n->hi) && k <= s->nticks + 2; k++) {
      res_init(s, &next, start);

      for (unsigned i = 0; i < prev.len; i++) {
         if (!prev.ends[i])
            continue;

         const int64_t s2 = start + i;   // End tick + 1
         if (s2 >= s->nticks)
            out->open = true;
         else {
            seqres_t ra;
            eval_seq(s, n->a, s2, &ra);
            res_merge(&next, &ra);
            out->open |= ra.open;
            res_free(&ra);
         }
      }

      if (k >= n->lo)
         res_merge(out, &next);

      res_free(&prev);
      res_move(&prev, &next);

      if (!res_any(&prev))
         break;
   }

   res_free(&prev);
}

static void seq_goto(const sva_t *s, const sva_node_t *n, int64_t start,
                     bool trailing, seqres_t *out)
{
   const int leaf = s->nodes[n->a].leaf;

   // Positions of the occurrences of the expression
   int64_t count = 0, prev = start - 1;
   for (int64_t t = start; t < s->nticks; t++) {
      if (!sva_leaf(s, leaf, t))
         continue;

      count++;

      if (!trailing && in_range(count, n->lo, n->hi))
         res_set(out, t);

      if (trailing) {
         // Ticks after the previous occurrence until this one
         if (in_range(count - 1, n->lo, n->hi)) {
            for (int64_t e = prev; e < t; e++)
               res_set(out, e);
         }
      }

      prev = t;
   }

   if (trailing && in_range(count, n->lo, n->hi)) {
      for (int64_t e = prev; e < s->nticks; e++)
         res_set(out, e);
   }

   out->open = n->hi < 0 || (trailing ? count <= n->hi : count < n->hi);
}

static void seq_and(const sva_t *s, const sva_node_t *n, int64_t start,
                    seqres_t *out)
{
   seqres_t ra, rb;
   eval_seq(s, n->a, start, &ra);
   eval_seq(s, n->b, start, &rb);

   for (unsigned i = 0; i < ra.len; i++) {
      if (!ra.ends[i])
         continue;

      for (unsigned j = 0; j < rb.len; j++) {
         if (rb.ends[j])
            res_set(out, start - 1 + MAX(i, j));
      }
   }

   const bool any_a = res_any(&ra), any_b = res_any(&rb);
   out->open = (ra.open && (rb.open || any_b)) || (rb.open && (ra.open || any_a));

   res_free(&ra);
   res_free(&rb);
}

static void seq_within(const sva_t *s, const sva_node_t *n, int64_t start,
                       seqres_t *out)
{
   seqres_t rb;
   eval_seq(s, n->b, start, &rb);

   for (unsigned i = 0; i < rb.len; i++) {
      if (!rb.ends[i])
         continue;

      const int64_t eb = start - 1 + i;
      bool found = false;
      for (int64_t s2 = start; s2 <= eb && !found; s2++) {
         seqres_t ra;
         eval_seq(s, n->a, s2, &ra);
         for (int64_t e = s2 - 1; e <= eb && !found; e++)
            found = res_get(&ra, e);
         res_free(&ra);
      }

      if (found)
         res_set(out, eb);
   }

   out->open = rb.open;
   res_free(&rb);
}

static void eval_seq(const sva_t *s, int id, int64_t start, seqres_t *out)
{
   const sva_node_t *n = &(s->nodes[id]);

   res_init(s, out, start);

   if (start > s->nticks) {
      out->open = true;
      return;
   }

   switch (n->op) {
   case SVA_LEAF:
      if (start < s->nticks) {
         if (sva_leaf(s, n->leaf, start))
            res_set(out, start);
      }
      else
         out->open = true;
      break;

   case V_SVA_DELAY:
      seq_delay(s, n, start, out);
      break;

   case V_SVA_REPEAT:
      seq_repeat(s, n, start, out);
      break;

   case V_SVA_GOTO:
      seq_goto(s, n, start, false, out);
      break;

   case V_SVA_NONCONS:
      seq_goto(s, n, start, true, out);
      break;

   case V_SVA_AND:
      seq_and(s, n, start, out);
      break;

   case V_SVA_OR:
      {
         seqres_t ra, rb;
         eval_seq(s, n->a, start, &ra);
         eval_seq(s, n->b, start, &rb);
         res_merge(out, &ra);
         res_merge(out, &rb);
         out->open = ra.open || rb.open;
         res_free(&ra);
         res_free(&rb);
      }
      break;

   case V_SVA_INTERSECT:
      {
         seqres_t ra, rb;
         eval_seq(s, n->a, start, &ra);
         eval_seq(s, n->b, start, &rb);
         for (unsigned i = 0; i < ra.len && i < rb.len; i++) {
            if (ra.ends[i] && rb.ends[i])
               out->ends[i] = 1;
         }
         out->open = ra.open && rb.open;
         res_free(&ra);
         res_free(&rb);
      }
      break;

   case V_SVA_WITHIN:
      seq_within(s, n, start, out);
      break;

   case V_SVA_THROUGHOUT:
      {
         const int leaf = s->nodes[n->a].leaf;
         int64_t fail = s->nticks;
         for (int64_t t = start; t < s->nticks; t++) {
            if (!sva_leaf(s, leaf, t)) {
               fail = t;
               break;
            }
         }

         seqres_t rb;
         eval_seq(s, n->b, start, &rb);
         for (unsigned i = 0; i < rb.len; i++) {
            if (rb.ends[i] && start - 1 + i < fail)
               res_set(out, start - 1 + i);
         }
         out->open = rb.open && fail == s->nticks;
         res_free(&rb);
      }
      break;

   case V_SVA_FIRST_MATCH:
      {
         seqres_t ra;
         eval_seq(s, n->a, start, &ra);
         bool found = false;
         for (unsigned i = 0; i < ra.len && !found; i++) {
            if (ra.ends[i]) {
               res_set(out, start - 1 + i);
               found = true;
            }
         }
         out->open = ra.open && !found;
         res_free(&ra);
      }
      break;

   default:
      fatal_trace("unhandled sequence operator %d", n->op);
   }
}

static tri_t at_end(const sva_t *s, bool strong)
{
   // An attempt that needs more ticks fails if its property is strong
   // when the trace ends

   if (!s->eot)
      return TRI_PENDING;

   return strong ? TRI_FALSE : TRI_TRUE;
}

static tri_t tri_not(tri_t t)
{
   return t == TRI_PENDING ? t : (t == TRI_TRUE ? TRI_FALSE : TRI_TRUE);
}

static tri_t seq_as_property(const sva_t *s, int id, int64_t start,
                             bool strong)
{
   seqres_t r;
   eval_seq(s, id, start, &r);

   tri_t result;
   bool matched = false;
   for (unsigned i = 1; i < r.len && !matched; i++)
      matched = r.ends[i];

   if (matched)
      result = TRI_TRUE;
   else if (r.open)
      result = at_end(s, strong);
   else
      result = TRI_FALSE;

   res_free(&r);
   return result;
}

static tri_t prop_implication(const sva_t *s, const sva_node_t *n,
                              int64_t start, bool overlap, bool *nv)
{
   const sva_node_t *na = &(s->nodes[n->a]);
   if (na->op == SVA_LEAF && start < s->nticks) {
      // The antecedent matches at the first tick or not at all
      if (!sva_leaf(s, na->leaf, start)) {
         *nv = false;
         return TRI_TRUE;
      }

      const int64_t from = overlap ? start : start + 1;

      tri_t r;
      bool nv2 = true;
      const sva_node_t *nb = &(s->nodes[n->b]);
      if (nb->op == V_SVA_DELAY && nb->a < 0
          && s->nodes[nb->b].op == SVA_LEAF) {
         // A delayed leaf is decided by the newest tick
         const int64_t last = nb->hi < 0 ? s->nticks - 1
            : MIN(from + nb->hi, s->nticks - 1);
         const bool open = nb->hi < 0 || from + nb->hi >= s->nticks;

         if (leaf_in_range(s, s->nodes[nb->b].leaf, from + nb->lo, last))
            r = TRI_TRUE;
         else if (open)
            r = at_end(s, false);
         else
            r = TRI_FALSE;
      }
      else
         r = eval_prop(s, n->b, from, &nv2);

      *nv = nv2;

      if (r == TRI_FALSE)
         return TRI_FALSE;
      else if (r == TRI_PENDING && !s->eot)
         return TRI_PENDING;
      else
         return TRI_TRUE;
   }

   seqres_t ra;
   eval_seq(s, n->a, start, &ra);

   bool any = false, pending = false, fail = false;
   for (unsigned i = 1; i < ra.len && !fail; i++) {
      if (!ra.ends[i])
         continue;

      bool nv2 = false;
      const int64_t e = start - 1 + i;
      switch (eval_prop(s, n->b, overlap ? e : e + 1, &nv2)) {
      case TRI_FALSE: fail = true; break;
      case TRI_PENDING: pending = true; break;
      default: break;
      }

      any |= nv2;
   }

   const bool open = ra.open;
   res_free(&ra);

   *nv = any;

   if (fail)
      return TRI_FALSE;
   else if (!s->eot && (open || pending))
      return TRI_PENDING;
   else
      return TRI_TRUE;
}

static tri_t prop_followed_by(const sva_t *s, const sva_node_t *n,
                              int64_t start, bool overlap, bool *nv)
{
   seqres_t ra;
   eval_seq(s, n->a, start, &ra);

   bool pending = false, found = false;
   for (unsigned i = 1; i < ra.len && !found; i++) {
      if (!ra.ends[i])
         continue;

      bool nv2 = false;
      const int64_t e = start - 1 + i;
      switch (eval_prop(s, n->b, overlap ? e : e + 1, &nv2)) {
      case TRI_TRUE: found = true; break;
      case TRI_PENDING: pending = true; break;
      default: break;
      }
   }

   const bool open = ra.open;
   res_free(&ra);

   *nv = true;

   if (found)
      return TRI_TRUE;
   else if (!s->eot && (open || pending))
      return TRI_PENDING;
   else
      return TRI_FALSE;
}

static tri_t prop_until(const sva_t *s, const sva_node_t *n, int64_t start,
                        bool with, bool strong, bool *nv)
{
   *nv = true;

   for (int64_t k = start; ; k++) {
      if (k >= s->nticks)
         return at_end(s, strong);

      bool nv2;
      const tri_t r2 = eval_prop(s, n->b, k, &nv2);
      const tri_t r1 = eval_prop(s, n->a, k, &nv2);

      if (r2 == TRI_TRUE) {
         if (!with)
            return TRI_TRUE;
         else
            return r1;
      }
      else if (r2 == TRI_PENDING)
         return TRI_PENDING;
      else if (r1 == TRI_FALSE)
         return TRI_FALSE;
      else if (r1 == TRI_PENDING)
         return TRI_PENDING;
   }
}

static tri_t eval_prop(const sva_t *s, int id, int64_t start, bool *nv)
{
   const sva_node_t *n = &(s->nodes[id]);
   *nv = true;

   if (is_seq(s, id))
      return seq_as_property(s, id, start, false);

   switch (n->op) {
   case V_SVA_STRONG:
   case V_SVA_WEAK:
      return seq_as_property(s, n->a, start, n->op == V_SVA_STRONG);

   case V_SVA_NOT:
      {
         const tri_t r = eval_prop(s, n->a, start, nv);
         *nv = true;
         return tri_not(r);
      }

   case V_SVA_AND:
   case V_SVA_OR:
      {
         bool nv1, nv2;
         const tri_t r1 = eval_prop(s, n->a, start, &nv1);
         const tri_t r2 = eval_prop(s, n->b, start, &nv2);
         *nv = nv1 || nv2;
         if (n->op == V_SVA_AND) {
            if (r1 == TRI_FALSE || r2 == TRI_FALSE)
               return TRI_FALSE;
            else if (r1 == TRI_TRUE && r2 == TRI_TRUE)
               return TRI_TRUE;
            else
               return TRI_PENDING;
         }
         else {
            if (r1 == TRI_TRUE || r2 == TRI_TRUE)
               return TRI_TRUE;
            else if (r1 == TRI_FALSE && r2 == TRI_FALSE)
               return TRI_FALSE;
            else
               return TRI_PENDING;
         }
      }

   case V_SVA_IMPL_OVER:
      return prop_implication(s, n, start, true, nv);
   case V_SVA_IMPL_NON:
      return prop_implication(s, n, start, false, nv);
   case V_SVA_FOLLOW_OVER:
      return prop_followed_by(s, n, start, true, nv);
   case V_SVA_FOLLOW_NON:
      return prop_followed_by(s, n, start, false, nv);

   case V_SVA_IF:
      {
         if (start >= s->nticks)
            return at_end(s, false);

         if (sva_leaf(s, n->leaf, start))
            return eval_prop(s, n->a, start, nv);
         else if (n->b >= 0)
            return eval_prop(s, n->b, start, nv);
         else {
            *nv = false;
            return TRI_TRUE;
         }
      }

   case V_SVA_ACCEPT_ON:
   case V_SVA_SYNC_ACCEPT_ON:
   case V_SVA_REJECT_ON:
   case V_SVA_SYNC_REJECT_ON:
      {
         // The abort condition ends the attempt at the first tick where it
         // holds, unless the property was decided before that
         const bool accept =
            n->op == V_SVA_ACCEPT_ON || n->op == V_SVA_SYNC_ACCEPT_ON;

         int64_t abort = -1;
         for (int64_t k = start; k < s->nticks; k++) {
            if (sva_leaf(s, n->leaf, k)) {
               abort = k;
               break;
            }
         }

         if (abort < 0)
            return eval_prop(s, n->a, start, nv);
         else if (abort == start)
            return accept ? TRI_TRUE : TRI_FALSE;

         sva_t *w = (sva_t *)s;
         const int64_t saved = w->nticks;
         w->nticks = abort;
         const tri_t r = eval_prop(s, n->a, start, nv);
         w->nticks = saved;

         *nv = true;
         if (r == TRI_PENDING)
            return accept ? TRI_TRUE : TRI_FALSE;
         else
            return r;
      }

   case V_SVA_NEXTTIME:
   case V_SVA_S_NEXTTIME:
      {
         const bool strong = n->op == V_SVA_S_NEXTTIME;
         bool pending = false;
         for (int64_t k = n->lo; n->hi < 0 || k <= n->hi; k++) {
            if (start + k >= s->nticks) {
               pending = true;
               break;
            }

            bool nv2;
            switch (eval_prop(s, n->a, start + k, &nv2)) {
            case TRI_FALSE: return TRI_FALSE;
            case TRI_PENDING: pending = true; break;
            default: break;
            }
         }

         return pending ? at_end(s, strong) : TRI_TRUE;
      }

   case V_SVA_ALWAYS:
   case V_SVA_S_ALWAYS:
      {
         const bool strong = n->op == V_SVA_S_ALWAYS;
         bool pending = false;
         for (int64_t k = n->lo; n->hi < 0 || k <= n->hi; k++) {
            if (start + k >= s->nticks) {
               pending = true;
               break;
            }

            bool nv2;
            switch (eval_prop(s, n->a, start + k, &nv2)) {
            case TRI_FALSE: return TRI_FALSE;
            case TRI_PENDING: pending = true; break;
            default: break;
            }
         }

         return pending ? at_end(s, strong) : TRI_TRUE;
      }

   case V_SVA_EVENTUALLY:
   case V_SVA_S_EVENTUALLY:
      {
         const bool strong = n->op == V_SVA_S_EVENTUALLY;
         bool pending = false;
         for (int64_t k = n->lo; n->hi < 0 || k <= n->hi; k++) {
            if (start + k >= s->nticks) {
               pending = true;
               break;
            }

            bool nv2;
            switch (eval_prop(s, n->a, start + k, &nv2)) {
            case TRI_TRUE: return TRI_TRUE;
            case TRI_PENDING: pending = true; break;
            default: break;
            }
         }

         return pending ? at_end(s, strong) : TRI_FALSE;
      }

   case V_SVA_UNTIL:
   case V_SVA_S_UNTIL:
      return prop_until(s, n, start, false, n->op == V_SVA_S_UNTIL, nv);
   case V_SVA_UNTIL_WITH:
   case V_SVA_S_UNTIL_WITH:
      return prop_until(s, n, start, true, n->op == V_SVA_S_UNTIL_WITH, nv);

   case V_SVA_IMPLIES:
   case V_SVA_IFF:
      {
         bool nv2;
         const tri_t r1 = eval_prop(s, n->a, start, &nv2);
         const tri_t r2 = eval_prop(s, n->b, start, &nv2);
         if (n->op == V_SVA_IMPLIES) {
            *nv = r1 == TRI_TRUE;
            const tri_t nr1 = tri_not(r1);
            if (nr1 == TRI_TRUE || r2 == TRI_TRUE)
               return TRI_TRUE;
            else if (nr1 == TRI_FALSE && r2 == TRI_FALSE)
               return TRI_FALSE;
            else
               return TRI_PENDING;
         }
         else if (r1 == TRI_PENDING || r2 == TRI_PENDING)
            return TRI_PENDING;
         else
            return r1 == r2 ? TRI_TRUE : TRI_FALSE;
      }

   default:
      fatal_trace("unhandled property operator %d", n->op);
   }
}

typedef struct {
   int64_t  v;
   bool     unk;
} xval_t;

typedef struct {
   int64_t  v[SVA_MAX_LOCALS];
   uint16_t unk;
   uint16_t chg;
} lvals_t;

typedef struct {
   int64_t  end;
   lvals_t  l;
} lmatch_t;

typedef struct {
   lmatch_t *m;
   unsigned  n, cap;
   bool      open;
} lres_t;

static uint64_t xmask(int w)
{
   return w >= 64 ? ~UINT64_C(0) : (UINT64_C(1) << w) - 1;
}

static int64_t xext(int64_t v, int w, bool sign)
{
   v &= xmask(w);
   if (sign && w < 64 && ((v >> (w - 1)) & 1))
      v |= ~xmask(w);
   return v;
}


typedef struct {
   uint64_t w[SVA_WORDS];
   bool     unk;
} wval_t;

static int wnwords(int width)
{
   return (width + 63) / 64;
}

static bool wbit(const uint64_t *w, int i)
{
   return (w[i / 64] >> (i % 64)) & 1;
}

static void wext(uint64_t *w, int from, int to, bool sign)
{
   // Change the width of a value with only the low FROM bits set

   if (to > from && sign && from > 0 && wbit(w, from - 1)) {
      for (int i = from; i < to; i++)
         w[i / 64] |= UINT64_C(1) << (i % 64);
   }

   wmask(w, to);
}

static bool wzero(const uint64_t *w, int width)
{
   for (int i = 0; i < wnwords(width); i++) {
      if (w[i] != 0)
         return false;
   }

   return true;
}

static int wcmp(const uint64_t *a, const uint64_t *b, int width, bool sign)
{
   if (sign && width > 0) {
      const bool na = wbit(a, width - 1), nb = wbit(b, width - 1);
      if (na != nb)
         return na ? -1 : 1;
   }

   for (int i = wnwords(width) - 1; i >= 0; i--) {
      if (a[i] != b[i])
         return a[i] < b[i] ? -1 : 1;
   }

   return 0;
}

static void wshl(uint64_t *out, const uint64_t *a, uint64_t amt, int width)
{
   memset(out, 0, SVA_WORDS * sizeof(uint64_t));
   if (amt >= width)
      return;

   const int ws = amt / 64, bs = amt % 64;
   for (int i = wnwords(width) - 1; i >= ws; i--) {
      out[i] = a[i - ws] << bs;
      if (bs > 0 && i - ws - 1 >= 0)
         out[i] |= a[i - ws - 1] >> (64 - bs);
   }

   wmask(out, width);
}

static void wshr(uint64_t *out, const uint64_t *a, uint64_t amt, int width,
                 bool fill)
{
   memset(out, 0, SVA_WORDS * sizeof(uint64_t));
   const int n = wnwords(width);
   if (amt >= width) {
      if (fill) {
         for (int i = 0; i < n; i++)
            out[i] = ~UINT64_C(0);
         wmask(out, width);
      }
      return;
   }

   const int ws = amt / 64, bs = amt % 64;
   for (int i = 0; i + ws < n; i++) {
      out[i] = a[i + ws] >> bs;
      if (bs > 0 && i + ws + 1 < n)
         out[i] |= a[i + ws + 1] << (64 - bs);
   }

   if (fill) {
      for (int i = width - amt; i < width; i++)
         out[i / 64] |= UINT64_C(1) << (i % 64);
   }

   wmask(out, width);
}

static uint64_t wamount(const uint64_t *w, int width)
{
   for (int i = 1; i < wnwords(width); i++) {
      if (w[i] != 0)
         return UINT64_MAX;
   }

   return w[0];
}

static void wmul(uint64_t *r, const uint64_t *a, const uint64_t *b,
                 int width)
{
   uint64_t t[SVA_WORDS] = {};
   const int n = wnwords(width);
   for (int i = 0; i < n; i++) {
      unsigned __int128 carry = 0;
      for (int j = 0; i + j < n; j++) {
         const unsigned __int128 p =
            (unsigned __int128)a[i] * b[j] + t[i + j] + carry;
         t[i + j] = (uint64_t)p;
         carry = p >> 64;
      }
   }

   memcpy(r, t, sizeof(t));
   wmask(r, width);
}

static void wneg(uint64_t *w, int width)
{
   uint64_t carry = 1;
   for (int i = 0; i < SVA_WORDS; i++) {
      const uint64_t t = ~w[i] + carry;
      carry = carry && t == 0;
      w[i] = t;
   }

   wmask(w, width);
}

static void wsub(uint64_t *a, const uint64_t *b, int width)
{
   uint64_t borrow = 0;
   for (int i = 0; i < wnwords(width); i++) {
      const uint64_t t = a[i] - b[i];
      const uint64_t b1 = a[i] < b[i];
      const uint64_t r = t - borrow;
      borrow = b1 | (t < borrow);
      a[i] = r;
   }

   wmask(a, width);
}

static void wdivmod(uint64_t *r, const uint64_t *a, const uint64_t *b,
                    int width, bool sign, bool mod)
{
   uint64_t x[SVA_WORDS], y[SVA_WORDS], q[SVA_WORDS] = {}, rem[SVA_WORDS] = {};
   memcpy(x, a, sizeof(x));
   memcpy(y, b, sizeof(y));

   const bool na = sign && wbit(x, width - 1);
   const bool nb = sign && wbit(y, width - 1);
   if (na)
      wneg(x, width);
   if (nb)
      wneg(y, width);

   for (int i = width - 1; i >= 0; i--) {
      const bool top = wbit(rem, width - 1);
      uint64_t sh[SVA_WORDS];
      wshl(sh, rem, 1, width);
      memcpy(rem, sh, sizeof(rem));
      if (wbit(x, i))
         rem[0] |= 1;

      if (top || wcmp(rem, y, width, false) >= 0) {
         wsub(rem, y, width);
         q[i / 64] |= UINT64_C(1) << (i % 64);
      }
   }

   memcpy(r, mod ? rem : q, SVA_WORDS * sizeof(uint64_t));
   if (mod ? na : na != nb)
      wneg(r, width);
}

static xval_t xeval(const sva_t *s, int id, int64_t tick, const lvals_t *lv);

static xval_t xcall(const sva_t *s, const xnode_t *x, int64_t tick,
                    const lvals_t *lv)
{
   // A call of a function with arguments which depend on local variables

   sva_t *w = (sva_t *)s;

   const int idx = x->value & 0xff;
   const int nargs = (x->value >> 8) & 0xf;
   const int sbits = (x->value >> 12) & 0x3f;
   const int ids[SVA_MAX_FARGS] = {
      x->a, x->b, x->c,
      (x->value >> 20) & 0x3fff,
      (x->value >> 34) & 0x3fff,
      (x->value >> 48) & 0x3fff,
   };

   int64_t args[SVA_MAX_FARGS] = {};
   bool unk = false;
   for (int i = 0; i < nargs; i++) {
      const xval_t a = xeval(s, ids[i], tick, lv);
      args[i] = xext(a.v, s->xnodes[ids[i]].width, (sbits >> i) & 1);
      unk |= a.unk;
   }

   xval_t r = { 0, unk };
   if (unk || tick == s->probe_t || call_fn == NULL)
      return r.unk = true, r;

   for (int i = 0; i < 16; i++) {
      if (w->memo[i].tick == tick && w->memo[i].idx == idx
          && memcmp(w->memo[i].args, args, sizeof(args)) == 0) {
         r.v = w->memo[i].result;
         return r;
      }
   }

   int64_t result = 0;
   if (!call_fn(call_jit, s->fhandle[idx], s->fctx[idx], args, nargs, &result)) {
      r.unk = true;
      return r;
   }

   const unsigned slot = w->nmemo++ % 16;
   w->memo[slot].tick = tick;
   w->memo[slot].idx = idx;
   memcpy(w->memo[slot].args, args, sizeof(args));
   w->memo[slot].result = result;

   r.v = result;
   return r;
}

static void weval(const sva_t *s, int id, int64_t tick, const lvals_t *lv,
                  wval_t *r);

static void weval_ext(const sva_t *s, int id, int64_t tick, const lvals_t *lv,
                      int width, bool sign, wval_t *r)
{
   // Evaluate a node and extend or truncate it to WIDTH bits

   weval(s, id, tick, lv, r);
   wext(r->w, s->xnodes[id].width, width, sign);
}

static void weval(const sva_t *s, int id, int64_t tick, const lvals_t *lv,
                  wval_t *r)
{
   const xnode_t *x = &(s->xnodes[id]);
   const bool sign = !!(x->flags & SVA_XF_SIGNED);

   memset(r, 0, sizeof(wval_t));

   switch (x->op) {
   case SVA_X_CONST:
      r->w[0] = x->value & xmask(x->width);
      r->unk = !!(x->flags & SVA_XF_UNKNOWN);
      return;

   case SVA_X_SLOT:
      {
         const int64_t idx = tick - s->hbase;
         if (tick == s->probe_t)
            ((sva_t *)s)->needed_slots |= UINT64_C(1) << x->value;

         const int64_t raw = s->shist[idx * s->nslots + x->value];
         if (x->width > 64)
            memcpy(r->w, wpool_get(&s->ppool, raw), sizeof(r->w));
         else
            r->w[0] = raw & xmask(x->width);
         r->unk = (s->uhist[idx] >> x->value) & 1;
         return;
      }

   case SVA_X_CALL:
      {
         const xval_t c = xcall(s, x, tick, lv);
         r->w[0] = c.v & xmask(x->width);
         r->unk = c.unk;
         return;
      }

   case SVA_X_LOCAL:
      if (s->lw[x->value] > 64)
         memcpy(r->w, wpool_get(&s->tpool, lv->v[x->value]), sizeof(r->w));
      else
         r->w[0] = lv->v[x->value] & xmask(x->width);
      r->unk = (lv->unk >> x->value) & 1;
      return;

   case SVA_X_COND:
      {
         wval_t c;
         weval(s, x->a, tick, lv, &c);
         if (c.unk)
            r->unk = true;
         else {
            const int pick = wzero(c.w, s->xnodes[x->a].width) ? x->c : x->b;
            weval_ext(s, pick, tick, lv, x->width, sign, r);
         }
         return;
      }

   case SVA_X_CAST:
      weval_ext(s, x->a, tick, lv, x->width, sign, r);
      return;

   case SVA_X_PARTSEL:
      {
         wval_t a;
         weval(s, x->a, tick, lv, &a);
         wshr(r->w, a.w, x->value, s->xnodes[x->a].width, false);
         wmask(r->w, x->width);
         r->unk = a.unk;
         return;
      }

   case SVA_X_BITSEL:
      {
         wval_t a, i;
         weval(s, x->a, tick, lv, &a);
         weval(s, x->b, tick, lv, &i);
         const uint64_t idx = wamount(i.w, s->xnodes[x->b].width);
         if (a.unk || i.unk || idx >= s->xnodes[x->a].width)
            r->unk = true;
         else
            r->w[0] = wbit(a.w, idx);
         return;
      }

   case SVA_X_NEG:
   case SVA_X_BITNEG:
      {
         weval_ext(s, x->a, tick, lv, x->width, sign, r);
         if (r->unk)
            return;

         if (x->op == SVA_X_BITNEG) {
            for (int i = 0; i < SVA_WORDS; i++)
               r->w[i] = ~r->w[i];
         }
         else {
            uint64_t carry = 1;
            for (int i = 0; i < SVA_WORDS; i++) {
               r->w[i] = ~r->w[i] + carry;
               carry = carry && r->w[i] == 0;
            }
         }

         wmask(r->w, x->width);
         return;
      }

   case SVA_X_LNOT:
   case SVA_X_RAND:
   case SVA_X_ROR:
   case SVA_X_RXOR:
   case SVA_X_RNAND:
   case SVA_X_RNOR:
   case SVA_X_RXNOR:
      {
         wval_t a;
         weval(s, x->a, tick, lv, &a);
         if (a.unk) {
            r->unk = true;
            return;
         }

         const int wa = s->xnodes[x->a].width;
         bool all = true, any = false;
         int ones = 0;
         for (int i = 0; i < wa; i++) {
            const bool b = wbit(a.w, i);
            all &= b;
            any |= b;
            ones += b;
         }

         switch (x->op) {
         case SVA_X_LNOT:  r->w[0] = !any; break;
         case SVA_X_RAND:  r->w[0] = all; break;
         case SVA_X_ROR:   r->w[0] = any; break;
         case SVA_X_RXOR:  r->w[0] = ones & 1; break;
         case SVA_X_RNAND: r->w[0] = !all; break;
         case SVA_X_RNOR:  r->w[0] = !any; break;
         default:          r->w[0] = !(ones & 1); break;
         }

         return;
      }

   default:
      break;
   }

   const int wa = s->xnodes[x->a].width, wb = s->xnodes[x->b].width;

   wval_t a, b;
   weval(s, x->a, tick, lv, &a);
   weval(s, x->b, tick, lv, &b);

   if (x->op == SVA_X_LAND || x->op == SVA_X_LOR) {
      const bool a0 = !a.unk && wzero(a.w, wa), b0 = !b.unk && wzero(b.w, wb);
      const bool a1 = !a.unk && !a0, b1 = !b.unk && !b0;
      if (x->op == SVA_X_LAND) {
         if (a0 || b0)
            r->w[0] = 0;
         else if (a1 && b1)
            r->w[0] = 1;
         else
            r->unk = true;
      }
      else {
         if (a1 || b1)
            r->w[0] = 1;
         else if (a0 && b0)
            r->w[0] = 0;
         else
            r->unk = true;
      }
      return;
   }

   const bool shift = x->op == SVA_X_SHL || x->op == SVA_X_SHR
      || x->op == SVA_X_ASHR;
   const bool relation = x->op >= SVA_X_EQ && x->op <= SVA_X_GE;
   const int opw = relation ? MAX(wa, wb) : shift ? x->width
      : MAX(x->width, MAX(wa, wb));

   if (shift) {
      wext(a.w, wa, opw, sign);
      wmask(b.w, wb);
   }
   else {
      wext(a.w, wa, opw, sign);
      wext(b.w, wb, opw, sign);
   }

   if (x->op == SVA_X_CEQ || x->op == SVA_X_CNE) {
      bool eq;
      if (a.unk || b.unk)
         eq = a.unk && b.unk;
      else
         eq = wcmp(a.w, b.w, opw, false) == 0;
      r->w[0] = x->op == SVA_X_CEQ ? eq : !eq;
      return;
   }

   if (a.unk || b.unk) {
      r->unk = true;
      return;
   }

   switch (x->op) {
   case SVA_X_ADD:
   case SVA_X_SUB:
      {
         uint64_t carry = x->op == SVA_X_SUB;
         for (int i = 0; i < SVA_WORDS; i++) {
            const uint64_t y = x->op == SVA_X_SUB ? ~b.w[i] : b.w[i];
            const uint64_t t = a.w[i] + y;
            const uint64_t c1 = t < a.w[i];
            r->w[i] = t + carry;
            carry = c1 | (r->w[i] < t);
         }
      }
      break;
   case SVA_X_MUL:  wmul(r->w, a.w, b.w, opw); break;
   case SVA_X_DIV:
   case SVA_X_MOD:
      if (wzero(b.w, opw))
         r->unk = true;
      else
         wdivmod(r->w, a.w, b.w, opw, sign, x->op == SVA_X_MOD);
      break;
   case SVA_X_POW:
      {
         const uint64_t n = wamount(b.w, opw);
         if ((sign && opw > 0 && wbit(b.w, opw - 1)) || n > 4096)
            r->unk = true;
         else {
            uint64_t acc[SVA_WORDS] = { 1 }, base[SVA_WORDS];
            memcpy(base, a.w, sizeof(base));
            for (uint64_t e = n; e != 0; e >>= 1) {
               if (e & 1)
                  wmul(acc, acc, base, opw);
               wmul(base, base, base, opw);
            }
            memcpy(r->w, acc, sizeof(acc));
         }
      }
      break;
   case SVA_X_AND:  for (int i = 0; i < SVA_WORDS; i++) r->w[i] = a.w[i] & b.w[i]; break;
   case SVA_X_OR:   for (int i = 0; i < SVA_WORDS; i++) r->w[i] = a.w[i] | b.w[i]; break;
   case SVA_X_XOR:  for (int i = 0; i < SVA_WORDS; i++) r->w[i] = a.w[i] ^ b.w[i]; break;
   case SVA_X_XNOR: for (int i = 0; i < SVA_WORDS; i++) r->w[i] = ~(a.w[i] ^ b.w[i]); break;
   case SVA_X_NAND: for (int i = 0; i < SVA_WORDS; i++) r->w[i] = ~(a.w[i] & b.w[i]); break;
   case SVA_X_NOR:  for (int i = 0; i < SVA_WORDS; i++) r->w[i] = ~(a.w[i] | b.w[i]); break;
   case SVA_X_SHL:  wshl(r->w, a.w, wamount(b.w, wb), opw); break;
   case SVA_X_SHR:
      wmask(a.w, wa);
      wshr(r->w, a.w, wamount(b.w, wb), opw, false);
      break;
   case SVA_X_ASHR:
      if (sign)
         wshr(r->w, a.w, wamount(b.w, wb), opw, opw > 0 && wbit(a.w, opw - 1));
      else {
         wmask(a.w, wa);
         wshr(r->w, a.w, wamount(b.w, wb), opw, false);
      }
      break;
   case SVA_X_EQ:   r->w[0] = wcmp(a.w, b.w, opw, false) == 0; break;
   case SVA_X_NE:   r->w[0] = wcmp(a.w, b.w, opw, false) != 0; break;
   case SVA_X_LT:   r->w[0] = wcmp(a.w, b.w, opw, sign) < 0; break;
   case SVA_X_GT:   r->w[0] = wcmp(a.w, b.w, opw, sign) > 0; break;
   case SVA_X_LE:   r->w[0] = wcmp(a.w, b.w, opw, sign) <= 0; break;
   case SVA_X_GE:   r->w[0] = wcmp(a.w, b.w, opw, sign) >= 0; break;
   default:
      fatal_trace("unhandled wide expression operator %d", x->op);
   }

   wmask(r->w, x->width);
}

static xval_t xeval(const sva_t *s, int id, int64_t tick, const lvals_t *lv);

static xval_t xeval_narrow(const sva_t *s, int id, int64_t tick,
                           const lvals_t *lv)
{
   const xnode_t *x = &(s->xnodes[id]);
   const bool sign = !!(x->flags & SVA_XF_SIGNED);
   xval_t r = { 0, false };

   switch (x->op) {
   case SVA_X_CONST:
      r.v = x->value & xmask(x->width);
      r.unk = !!(x->flags & SVA_XF_UNKNOWN);
      return r;

   case SVA_X_SLOT:
      {
         const int64_t idx = tick - s->hbase;
         if (tick == s->probe_t)
            ((sva_t *)s)->needed_slots |= UINT64_C(1) << x->value;
         r.v = s->shist[idx * s->nslots + x->value] & xmask(x->width);
         r.unk = (s->uhist[idx] >> x->value) & 1;
         return r;
      }

   case SVA_X_CALL:
      r = xcall(s, x, tick, lv);
      r.v &= xmask(x->width);
      return r;

   case SVA_X_LOCAL:
      r.v = lv->v[x->value] & xmask(x->width);
      r.unk = (lv->unk >> x->value) & 1;
      return r;

   case SVA_X_COND:
      {
         const xval_t c = xeval(s, x->a, tick, lv);
         if (c.unk)
            r.unk = true;
         else {
            const int pick = c.v != 0 ? x->b : x->c;
            r = xeval(s, pick, tick, lv);
            r.v = xext(r.v, s->xnodes[pick].width, sign) & xmask(x->width);
         }
         return r;
      }

   case SVA_X_CAST:
      {
         const xval_t a = xeval(s, x->a, tick, lv);
         r.unk = a.unk;
         r.v = xext(a.v, s->xnodes[x->a].width, sign) & xmask(x->width);
         return r;
      }

   case SVA_X_PARTSEL:
      {
         const xval_t a = xeval(s, x->a, tick, lv);
         r.unk = a.unk;
         r.v = ((uint64_t)(a.v & xmask(s->xnodes[x->a].width)) >> x->value)
            & xmask(x->width);
         return r;
      }

   case SVA_X_BITSEL:
      {
         const xval_t a = xeval(s, x->a, tick, lv);
         const xval_t i = xeval(s, x->b, tick, lv);
         const int wa = s->xnodes[x->a].width;
         const uint64_t idx = i.v & xmask(s->xnodes[x->b].width);
         if (a.unk || i.unk || idx >= wa)
            r.unk = true;
         else
            r.v = ((uint64_t)a.v >> idx) & 1;
         return r;
      }

   case SVA_X_NEG:
   case SVA_X_BITNEG:
   case SVA_X_LNOT:
   case SVA_X_RAND:
   case SVA_X_ROR:
   case SVA_X_RXOR:
   case SVA_X_RNAND:
   case SVA_X_RNOR:
   case SVA_X_RXNOR:
      {
         const xval_t a = xeval(s, x->a, tick, lv);
         const int wa = s->xnodes[x->a].width;
         if (a.unk) {
            r.unk = true;
            return r;
         }

         const uint64_t raw = a.v & xmask(wa);
         switch (x->op) {
         case SVA_X_NEG:    r.v = -xext(a.v, wa, sign); break;
         case SVA_X_BITNEG: r.v = ~xext(a.v, wa, sign); break;
         case SVA_X_LNOT:   r.v = raw == 0; break;
         case SVA_X_RAND:   r.v = raw == xmask(wa); break;
         case SVA_X_ROR:    r.v = raw != 0; break;
         case SVA_X_RXOR:   r.v = __builtin_popcountll(raw) & 1; break;
         case SVA_X_RNAND:  r.v = raw != xmask(wa); break;
         case SVA_X_RNOR:   r.v = raw == 0; break;
         default:           r.v = !(__builtin_popcountll(raw) & 1); break;
         }

         r.v &= xmask(x->width);
         return r;
      }

   default:
      break;
   }

   const xval_t a = xeval(s, x->a, tick, lv);
   const xval_t b = xeval(s, x->b, tick, lv);
   const int wa = s->xnodes[x->a].width, wb = s->xnodes[x->b].width;

   if (x->op == SVA_X_LAND || x->op == SVA_X_LOR) {
      const bool a0 = !a.unk && (a.v & xmask(wa)) == 0;
      const bool b0 = !b.unk && (b.v & xmask(wb)) == 0;
      const bool a1 = !a.unk && !a0, b1 = !b.unk && !b0;
      if (x->op == SVA_X_LAND) {
         if (a0 || b0)
            r.v = 0;
         else if (a1 && b1)
            r.v = 1;
         else
            r.unk = true;
      }
      else {
         if (a1 || b1)
            r.v = 1;
         else if (a0 && b0)
            r.v = 0;
         else
            r.unk = true;
      }
      return r;
   }

   if (x->op == SVA_X_CEQ || x->op == SVA_X_CNE) {
      bool eq;
      if (a.unk || b.unk)
         eq = a.unk && b.unk;
      else
         eq = xext(a.v, wa, sign) == xext(b.v, wb, sign);
      r.v = x->op == SVA_X_CEQ ? eq : !eq;
      return r;
   }

   if (a.unk || b.unk) {
      r.unk = true;
      return r;
   }

   const int64_t l = xext(a.v, wa, sign);
   const int64_t rr = x->op == SVA_X_SHL || x->op == SVA_X_SHR
      || x->op == SVA_X_ASHR ? (int64_t)(b.v & xmask(wb)) : xext(b.v, wb, sign);

   switch (x->op) {
   case SVA_X_ADD:  r.v = (uint64_t)l + (uint64_t)rr; break;
   case SVA_X_SUB:  r.v = (uint64_t)l - (uint64_t)rr; break;
   case SVA_X_MUL:  r.v = (uint64_t)l * (uint64_t)rr; break;
   case SVA_X_DIV:
   case SVA_X_MOD:
      if (rr == 0)
         r.unk = true;
      else if (sign) {
         if (l == INT64_MIN && rr == -1)
            r.v = x->op == SVA_X_DIV ? l : 0;
         else
            r.v = x->op == SVA_X_DIV ? l / rr : l % rr;
      }
      else
         r.v = x->op == SVA_X_DIV ? (uint64_t)l / (uint64_t)rr
            : (uint64_t)l % (uint64_t)rr;
      break;
   case SVA_X_POW:
      {
         uint64_t acc = 1;
         if (sign && rr < 0)
            r.unk = true;
         else if (rr > 4096)
            r.unk = true;
         else {
            for (int64_t i = 0; i < rr; i++)
               acc *= (uint64_t)l;
            r.v = acc;
         }
      }
      break;
   case SVA_X_AND:  r.v = l & rr; break;
   case SVA_X_OR:   r.v = l | rr; break;
   case SVA_X_XOR:  r.v = l ^ rr; break;
   case SVA_X_XNOR: r.v = ~(l ^ rr); break;
   case SVA_X_NAND: r.v = ~(l & rr); break;
   case SVA_X_NOR:  r.v = ~(l | rr); break;
   case SVA_X_SHL:  r.v = rr >= 64 ? 0 : (uint64_t)l << rr; break;
   case SVA_X_SHR:
      r.v = rr >= 64 ? 0 : (uint64_t)(l & xmask(wa)) >> rr;
      break;
   case SVA_X_ASHR:
      if (sign)
         r.v = l >> MIN(rr, 63);
      else
         r.v = rr >= 64 ? 0 : (uint64_t)(l & xmask(wa)) >> rr;
      break;
   case SVA_X_EQ:   r.v = l == rr; break;
   case SVA_X_NE:   r.v = l != rr; break;
   case SVA_X_LT:   r.v = sign ? l < rr : (uint64_t)l < (uint64_t)rr; break;
   case SVA_X_GT:   r.v = sign ? l > rr : (uint64_t)l > (uint64_t)rr; break;
   case SVA_X_LE:   r.v = sign ? l <= rr : (uint64_t)l <= (uint64_t)rr; break;
   case SVA_X_GE:   r.v = sign ? l >= rr : (uint64_t)l >= (uint64_t)rr; break;
   default:
      fatal_trace("unhandled expression operator %d", x->op);
   }

   r.v &= xmask(x->width);
   return r;
}

static xval_t xeval(const sva_t *s, int id, int64_t tick, const lvals_t *lv)
{
   if (!s->wide)
      return xeval_narrow(s, id, tick, lv);

   wval_t w;
   weval(s, id, tick, lv, &w);
   return (xval_t){ .v = w.w[0], .unk = w.unk };
}

static bool xtrue(const sva_t *s, int id, int64_t tick, const lvals_t *lv)
{
   if (s->wide) {
      wval_t w;
      weval(s, id, tick, lv, &w);
      return !w.unk && !wzero(w.w, s->xnodes[id].width);
   }

   const xval_t r = xeval(s, id, tick, lv);
   return !r.unk && r.v != 0;
}

static bool lvals_same(const sva_t *s, const lvals_t *a, const lvals_t *b)
{
   // The values of the local variables are equal

   if (a->unk != b->unk)
      return false;

   for (int i = 0; i < s->nlocals; i++) {
      if (s->lw[i] > 64) {
         if (memcmp(wpool_get(&s->tpool, a->v[i]),
                    wpool_get(&s->tpool, b->v[i]),
                    sizeof(uint64_t) * wnwords(s->lw[i])) != 0)
            return false;
      }
      else if (a->v[i] != b->v[i])
         return false;
   }

   return true;
}

static void lvals_init(const sva_t *s, lvals_t *lv)
{
   memset(lv, 0, sizeof(lvals_t));
   lv->unk = s->local_unk;

   if (s->wide) {
      // Index zero of the pool is a zero value
      uint64_t zero[SVA_WORDS] = {};
      if (s->tpool.n == 0)
         (void)wpool_add(&((sva_t *)s)->tpool, zero);
   }
}

static void record_call(const sva_t *s, int call, const lvals_t *lv)
{
   sva_t *w = (sva_t *)s;

   for (unsigned i = 0; i < w->nevents; i++) {
      const event_t *e = &(w->events[i]);
      if (e->call != call || e->start != w->cur_start)
         continue;

      lvals_t other = { .unk = e->unk };
      memcpy(other.v, e->v, sizeof(other.v));
      if (lvals_same(s, &other, lv))
         return;
   }

   if (w->nevents == w->maxevents) {
      w->maxevents = MAX(8, w->maxevents * 2);
      w->events = xrealloc_array(w->events, w->maxevents, sizeof(event_t));
   }

   event_t *e = &(w->events[w->nevents++]);
   e->call = call;
   e->start = w->cur_start;
   e->unk = lv->unk;
   memcpy(e->v, lv->v, sizeof(e->v));
}

static void apply_items(const sva_t *s, const sva_node_t *n, int64_t tick,
                        lvals_t *lv)
{
   for (int i = 0; i < n->lo; i++) {
      const item_t *it = &(s->items[n->b + i]);

      if (it->kind == SVA_ITEM_CALL) {
         if (tick == s->nticks - 1 && s->probe_t < 0 && !s->eot)
            record_call(s, it->local, lv);
         continue;
      }

      assert(it->kind == SVA_ITEM_ASSIGN);

      if (s->wide) {
         wval_t w;
         weval(s, it->expr, tick, lv, &w);
         wext(w.w, s->xnodes[it->expr].width, it->width,
              !!(it->flags & SVA_XF_SIGNED));
         if (s->lw[it->local] > 64)
            lv->v[it->local] = wpool_add(&((sva_t *)s)->tpool, w.w);
         else
            lv->v[it->local] = w.w[0];

         if (w.unk)
            lv->unk |= 1 << it->local;
         else
            lv->unk &= ~(1 << it->local);
         lv->chg |= 1 << it->local;
         continue;
      }

      const xval_t r = xeval(s, it->expr, tick, lv);
      const int64_t v =
         xext(r.v, s->xnodes[it->expr].width, !!(it->flags & SVA_XF_SIGNED));
      lv->v[it->local] = v & xmask(it->width);
      if (r.unk)
         lv->unk |= 1 << it->local;
      else
         lv->unk &= ~(1 << it->local);
      lv->chg |= 1 << it->local;
   }
}

static void lres_free(lres_t *r)
{
   free(r->m);
   r->m = NULL;
   r->n = r->cap = 0;
}

static bool lvals_equal(const sva_t *s, const lvals_t *a, const lvals_t *b)
{
   return a->chg == b->chg && lvals_same(s, a, b);
}

static void lres_add(const sva_t *s, lres_t *r, int64_t end, const lvals_t *lv)
{
   for (unsigned i = 0; i < r->n; i++) {
      if (r->m[i].end == end && lvals_equal(s, &(r->m[i].l), lv))
         return;
   }

   if (r->n == r->cap) {
      r->cap = MAX(8, r->cap * 2);
      r->m = xrealloc_array(r->m, r->cap, sizeof(lmatch_t));
   }

   r->m[r->n].end = end;
   r->m[r->n].l = *lv;
   r->n++;
}

static void lvals_merge(const sva_t *s, lvals_t *out, const lvals_t *a,
                        const lvals_t *b, const lvals_t *in)
{
   *out = *in;
   for (int i = 0; i < s->nlocals; i++) {
      const lvals_t *from = NULL;
      if ((b->chg & ~in->chg) & (1 << i))
         from = b;
      else if ((a->chg & ~in->chg) & (1 << i))
         from = a;

      if (from != NULL) {
         out->v[i] = from->v[i];
         out->unk = (out->unk & ~(1 << i)) | (from->unk & (1 << i));
         out->chg |= 1 << i;
      }
   }
}

static void leval_seq(const sva_t *s, int id, int64_t start,
                      const lvals_t *in, lres_t *out);

static bool lleaf(const sva_t *s, const sva_node_t *n, int64_t t,
                  const lvals_t *lv)
{
   if (n->op == SVA_DLEAF)
      return xtrue(s, n->leaf, t, lv);
   else
      return sva_leaf(s, n->leaf, t);
}

static void lseq_delay(const sva_t *s, const sva_node_t *n, int64_t start,
                       const lvals_t *in, lres_t *out)
{
   lres_t ra = {};
   if (n->a < 0)
      lres_add(s, &ra, start, in);
   else
      leval_seq(s, n->a, start, in, &ra);

   if (ra.open)
      out->open = true;

   for (unsigned i = 0; i < ra.n; i++) {
      const int64_t e1 = ra.m[i].end;
      const lvals_t lv = ra.m[i].l;

      if (n->hi < 0 || e1 + n->hi >= s->nticks)
         out->open = true;

      const int64_t from = MAX(e1 + n->lo, start);
      const int64_t to = n->hi < 0 ? s->nticks - 1
         : MIN(e1 + n->hi, s->nticks - 1);

      for (int64_t s2 = from; s2 <= to; s2++) {
         lres_t rb = {};
         leval_seq(s, n->b, s2, &lv, &rb);
         for (unsigned j = 0; j < rb.n; j++)
            lres_add(s, out, rb.m[j].end, &(rb.m[j].l));
         out->open |= rb.open;
         lres_free(&rb);
      }
   }

   lres_free(&ra);
}

static void lseq_repeat(const sva_t *s, const sva_node_t *n, int64_t start,
                        const lvals_t *in, lres_t *out)
{
   lres_t prev = {}, next;
   lres_add(s, &prev, start - 1, in);

   if (n->lo == 0)
      lres_add(s, out, start - 1, in);

   for (int64_t k = 1; (n->hi < 0 || k <= n->hi) && k <= s->nticks + 2; k++) {
      next = (lres_t){};

      for (unsigned i = 0; i < prev.n; i++) {
         const int64_t s2 = prev.m[i].end + 1;
         if (s2 >= s->nticks)
            out->open = true;
         else {
            lres_t ra = {};
            leval_seq(s, n->a, s2, &(prev.m[i].l), &ra);
            for (unsigned j = 0; j < ra.n; j++)
               lres_add(s, &next, ra.m[j].end, &(ra.m[j].l));
            out->open |= ra.open;
            lres_free(&ra);
         }
      }

      if (k >= n->lo) {
         for (unsigned j = 0; j < next.n; j++)
            lres_add(s, out, next.m[j].end, &(next.m[j].l));
      }

      lres_free(&prev);
      prev = next;

      if (prev.n == 0)
         break;
   }

   lres_free(&prev);
}

static void lseq_goto(const sva_t *s, const sva_node_t *n, int64_t start,
                      const lvals_t *in, bool trailing, lres_t *out)
{
   int64_t count = 0, prev = start - 1;
   for (int64_t t = start; t < s->nticks; t++) {
      if (!lleaf(s, &(s->nodes[n->a]), t, in))
         continue;

      count++;

      if (!trailing && in_range(count, n->lo, n->hi))
         lres_add(s, out, t, in);

      if (trailing && in_range(count - 1, n->lo, n->hi)) {
         for (int64_t e = prev; e < t; e++)
            lres_add(s, out, e, in);
      }

      prev = t;
   }

   if (trailing && in_range(count, n->lo, n->hi)) {
      for (int64_t e = prev; e < s->nticks; e++)
         lres_add(s, out, e, in);
   }

   out->open = n->hi < 0 || (trailing ? count <= n->hi : count < n->hi);
}

static void lseq_within(const sva_t *s, const sva_node_t *n, int64_t start,
                        const lvals_t *in, lres_t *out)
{
   lvals_t clean = *in;
   clean.chg = 0;

   lres_t rb = {};
   leval_seq(s, n->b, start, &clean, &rb);

   for (unsigned j = 0; j < rb.n; j++) {
      const int64_t eb = rb.m[j].end;
      for (int64_t s2 = start; s2 <= eb; s2++) {
         lres_t ra = {};
         leval_seq(s, n->a, s2, &clean, &ra);
         for (unsigned i = 0; i < ra.n; i++) {
            if (ra.m[i].end < s2 - 1 || ra.m[i].end > eb)
               continue;

            lvals_t lv;
            lvals_merge(s, &lv, &(ra.m[i].l), &(rb.m[j].l), &clean);
            lv.chg |= in->chg;
            lres_add(s, out, eb, &lv);
         }
         lres_free(&ra);
      }
   }

   out->open = rb.open;
   lres_free(&rb);
}

static void leval_seq(const sva_t *s, int id, int64_t start,
                      const lvals_t *in, lres_t *out)
{
   const sva_node_t *n = &(s->nodes[id]);

   out->open = false;

   if (start > s->nticks) {
      out->open = true;
      return;
   }

   switch (n->op) {
   case SVA_LEAF:
   case SVA_DLEAF:
      if (start >= s->nticks)
         out->open = true;
      else if (lleaf(s, n, start, in))
         lres_add(s, out, start, in);
      break;

   case V_SVA_MATCH:
      {
         lres_t ra = {};
         leval_seq(s, n->a, start, in, &ra);
         for (unsigned i = 0; i < ra.n; i++) {
            lvals_t lv = ra.m[i].l;
            apply_items(s, n, ra.m[i].end, &lv);
            lres_add(s, out, ra.m[i].end, &lv);
         }
         out->open = ra.open;
         lres_free(&ra);
      }
      break;

   case V_SVA_DELAY:
      lseq_delay(s, n, start, in, out);
      break;

   case V_SVA_REPEAT:
      lseq_repeat(s, n, start, in, out);
      break;

   case V_SVA_GOTO:
   case V_SVA_NONCONS:
      lseq_goto(s, n, start, in, n->op == V_SVA_NONCONS, out);
      break;

   case V_SVA_WITHIN:
      lseq_within(s, n, start, in, out);
      break;

   case V_SVA_OR:
      {
         lres_t ra = {}, rb = {};
         leval_seq(s, n->a, start, in, &ra);
         leval_seq(s, n->b, start, in, &rb);
         for (unsigned i = 0; i < ra.n; i++)
            lres_add(s, out, ra.m[i].end, &(ra.m[i].l));
         for (unsigned i = 0; i < rb.n; i++)
            lres_add(s, out, rb.m[i].end, &(rb.m[i].l));
         out->open = ra.open || rb.open;
         lres_free(&ra);
         lres_free(&rb);
      }
      break;

   case V_SVA_AND:
   case V_SVA_INTERSECT:
      {
         lvals_t clean = *in;
         clean.chg = 0;

         lres_t ra = {}, rb = {};
         leval_seq(s, n->a, start, &clean, &ra);
         leval_seq(s, n->b, start, &clean, &rb);

         const bool isect = n->op == V_SVA_INTERSECT;
         for (unsigned i = 0; i < ra.n; i++) {
            for (unsigned j = 0; j < rb.n; j++) {
               if (isect && ra.m[i].end != rb.m[j].end)
                  continue;

               lvals_t lv;
               lvals_merge(s, &lv, &(ra.m[i].l), &(rb.m[j].l), &clean);
               lv.chg |= in->chg;
               lres_add(s, out, MAX(ra.m[i].end, rb.m[j].end), &lv);
            }
         }

         if (isect)
            out->open = ra.open && rb.open;
         else
            out->open = (ra.open && (rb.open || rb.n > 0))
               || (rb.open && (ra.open || ra.n > 0));

         lres_free(&ra);
         lres_free(&rb);
      }
      break;

   case V_SVA_THROUGHOUT:
      {
         int64_t fail = s->nticks;
         for (int64_t t = start; t < s->nticks; t++) {
            if (!lleaf(s, &(s->nodes[n->a]), t, in)) {
               fail = t;
               break;
            }
         }

         lres_t rb = {};
         leval_seq(s, n->b, start, in, &rb);
         for (unsigned i = 0; i < rb.n; i++) {
            if (rb.m[i].end < fail)
               lres_add(s, out, rb.m[i].end, &(rb.m[i].l));
         }
         out->open = rb.open && fail == s->nticks;
         lres_free(&rb);
      }
      break;

   case V_SVA_FIRST_MATCH:
      {
         lres_t ra = {};
         const unsigned nevents = s->nevents;
         leval_seq(s, n->a, start, in, &ra);

         int64_t first = INT64_MAX;
         for (unsigned i = 0; i < ra.n; i++)
            first = MIN(first, ra.m[i].end);

         // Subroutines called by later matches are not run
         if (first < s->nticks - 1)
            ((sva_t *)s)->nevents = nevents;

         for (unsigned i = 0; i < ra.n; i++) {
            if (ra.m[i].end == first)
               lres_add(s, out, first, &(ra.m[i].l));
         }

         out->open = ra.open && ra.n == 0;
         lres_free(&ra);
      }
      break;

   default:
      fatal_trace("unhandled sequence operator %d with local variables",
                  n->op);
   }
}

static tri_t lseq_as_property(const sva_t *s, int id, int64_t start,
                              const lvals_t *in, bool strong)
{
   lres_t r = {};
   leval_seq(s, id, start, in, &r);

   bool matched = false;
   for (unsigned i = 0; i < r.n && !matched; i++)
      matched = r.m[i].end >= start;

   const bool open = r.open;
   lres_free(&r);

   if (matched)
      return TRI_TRUE;
   else if (open)
      return at_end(s, strong);
   else
      return TRI_FALSE;
}

static tri_t leval_prop(const sva_t *s, int id, int64_t start,
                        const lvals_t *in, bool *nv)
{
   const sva_node_t *n = &(s->nodes[id]);
   *nv = true;

   if (is_seq(s, id))
      return lseq_as_property(s, id, start, in, false);

   switch (n->op) {
   case V_SVA_STRONG:
   case V_SVA_WEAK:
      return lseq_as_property(s, n->a, start, in, n->op == V_SVA_STRONG);

   case V_SVA_NOT:
      {
         const tri_t r = leval_prop(s, n->a, start, in, nv);
         *nv = true;
         return tri_not(r);
      }

   case V_SVA_AND:
   case V_SVA_OR:
      {
         bool nv1, nv2;
         const tri_t r1 = leval_prop(s, n->a, start, in, &nv1);
         const tri_t r2 = leval_prop(s, n->b, start, in, &nv2);
         *nv = nv1 || nv2;
         if (n->op == V_SVA_AND) {
            if (r1 == TRI_FALSE || r2 == TRI_FALSE)
               return TRI_FALSE;
            else if (r1 == TRI_TRUE && r2 == TRI_TRUE)
               return TRI_TRUE;
            else
               return TRI_PENDING;
         }
         else {
            if (r1 == TRI_TRUE || r2 == TRI_TRUE)
               return TRI_TRUE;
            else if (r1 == TRI_FALSE && r2 == TRI_FALSE)
               return TRI_FALSE;
            else
               return TRI_PENDING;
         }
      }

   case V_SVA_IMPL_OVER:
   case V_SVA_IMPL_NON:
   case V_SVA_FOLLOW_OVER:
   case V_SVA_FOLLOW_NON:
      {
         const bool overlap =
            n->op == V_SVA_IMPL_OVER || n->op == V_SVA_FOLLOW_OVER;
         const bool impl = n->op == V_SVA_IMPL_OVER || n->op == V_SVA_IMPL_NON;

         lres_t ra = {};
         leval_seq(s, n->a, start, in, &ra);

         bool any = false, pending = false, decided = false;
         for (unsigned i = 0; i < ra.n && !decided; i++) {
            if (ra.m[i].end < start)
               continue;

            bool nv2 = false;
            const int64_t e = ra.m[i].end;
            switch (leval_prop(s, n->b, overlap ? e : e + 1, &(ra.m[i].l),
                               &nv2)) {
            case TRI_FALSE: decided = impl; break;
            case TRI_TRUE: decided = !impl; break;
            case TRI_PENDING: pending = true; break;
            }

            any |= nv2;
         }

         const bool open = ra.open;
         lres_free(&ra);

         if (impl) {
            *nv = any;
            if (decided)
               return TRI_FALSE;
            else if (!s->eot && (open || pending))
               return TRI_PENDING;
            else
               return TRI_TRUE;
         }
         else {
            *nv = true;
            if (decided)
               return TRI_TRUE;
            else if (!s->eot && (open || pending))
               return TRI_PENDING;
            else
               return TRI_FALSE;
         }
      }

   case V_SVA_IF:
      {
         if (start >= s->nticks)
            return at_end(s, false);

         const bool cond = n->lo > 0 ? xtrue(s, n->lo - 1, start, in)
            : sva_leaf(s, n->leaf, start);

         if (cond)
            return leval_prop(s, n->a, start, in, nv);
         else if (n->b >= 0)
            return leval_prop(s, n->b, start, in, nv);
         else {
            *nv = false;
            return TRI_TRUE;
         }
      }

   case V_SVA_ACCEPT_ON:
   case V_SVA_SYNC_ACCEPT_ON:
   case V_SVA_REJECT_ON:
   case V_SVA_SYNC_REJECT_ON:
      {
         const bool accept =
            n->op == V_SVA_ACCEPT_ON || n->op == V_SVA_SYNC_ACCEPT_ON;

         int64_t abort = -1;
         for (int64_t k = start; k < s->nticks; k++) {
            if (n->lo > 0 ? xtrue(s, n->lo - 1, k, in)
                : sva_leaf(s, n->leaf, k)) {
               abort = k;
               break;
            }
         }

         if (abort < 0)
            return leval_prop(s, n->a, start, in, nv);
         else if (abort == start)
            return accept ? TRI_TRUE : TRI_FALSE;

         sva_t *w = (sva_t *)s;
         const int64_t saved = w->nticks;
         w->nticks = abort;
         const tri_t r = leval_prop(s, n->a, start, in, nv);
         w->nticks = saved;

         *nv = true;
         if (r == TRI_PENDING)
            return accept ? TRI_TRUE : TRI_FALSE;
         else
            return r;
      }

   case V_SVA_NEXTTIME:
   case V_SVA_S_NEXTTIME:
   case V_SVA_ALWAYS:
   case V_SVA_S_ALWAYS:
   case V_SVA_EVENTUALLY:
   case V_SVA_S_EVENTUALLY:
      {
         const bool strong = n->op == V_SVA_S_NEXTTIME
            || n->op == V_SVA_S_ALWAYS || n->op == V_SVA_S_EVENTUALLY;
         const bool exists = n->op == V_SVA_EVENTUALLY
            || n->op == V_SVA_S_EVENTUALLY;
         const tri_t decisive = exists ? TRI_TRUE : TRI_FALSE;

         bool pending = false;
         for (int64_t k = n->lo; n->hi < 0 || k <= n->hi; k++) {
            if (start + k >= s->nticks) {
               pending = true;
               break;
            }

            bool nv2;
            const tri_t r = leval_prop(s, n->a, start + k, in, &nv2);
            if (r == decisive)
               return decisive;
            else if (r == TRI_PENDING)
               pending = true;
         }

         if (pending)
            return at_end(s, strong);
         else
            return exists ? TRI_FALSE : TRI_TRUE;
      }

   case V_SVA_UNTIL:
   case V_SVA_S_UNTIL:
   case V_SVA_UNTIL_WITH:
   case V_SVA_S_UNTIL_WITH:
      {
         const bool strong = n->op == V_SVA_S_UNTIL
            || n->op == V_SVA_S_UNTIL_WITH;
         const bool with = n->op == V_SVA_UNTIL_WITH
            || n->op == V_SVA_S_UNTIL_WITH;

         for (int64_t k = start; ; k++) {
            if (k >= s->nticks)
               return at_end(s, strong);

            bool nv2;
            const tri_t r2 = leval_prop(s, n->b, k, in, &nv2);
            const tri_t r1 = leval_prop(s, n->a, k, in, &nv2);

            if (r2 == TRI_TRUE)
               return with ? r1 : TRI_TRUE;
            else if (r2 == TRI_PENDING)
               return TRI_PENDING;
            else if (r1 == TRI_FALSE)
               return TRI_FALSE;
            else if (r1 == TRI_PENDING)
               return TRI_PENDING;
         }
      }

   case V_SVA_IMPLIES:
   case V_SVA_IFF:
      {
         bool nv2;
         const tri_t r1 = leval_prop(s, n->a, start, in, &nv2);
         const tri_t r2 = leval_prop(s, n->b, start, in, &nv2);
         if (n->op == V_SVA_IMPLIES) {
            *nv = r1 == TRI_TRUE;
            const tri_t nr1 = tri_not(r1);
            if (nr1 == TRI_TRUE || r2 == TRI_TRUE)
               return TRI_TRUE;
            else if (nr1 == TRI_FALSE && r2 == TRI_FALSE)
               return TRI_FALSE;
            else
               return TRI_PENDING;
         }
         else if (r1 == TRI_PENDING || r2 == TRI_PENDING)
            return TRI_PENDING;
         else
            return r1 == r2 ? TRI_TRUE : TRI_FALSE;
      }

   default:
      fatal_trace("unhandled property operator %d with local variables",
                  n->op);
   }
}

static tri_t eval_root(const sva_t *s, int64_t start, bool *nv)
{
   if (!s->lmode)
      return eval_prop(s, s->root, start, nv);

   lvals_t in;
   lvals_init(s, &in);
   return leval_prop(s, s->root, start, &in, nv);
}

static void sva_compact(sva_t *s)
{
   int64_t keep = s->nticks;
   for (unsigned i = 0; i < s->nlive; i++)
      keep = MIN(keep, s->live[i]);

   if (keep - s->hbase < 64)
      return;

   const int64_t drop = keep - s->hbase;
   memmove(s->hist, s->hist + drop, (s->nticks - keep) * sizeof(uint64_t));
   if (s->nslots > 0) {
      memmove(s->shist, s->shist + drop * s->nslots,
              (s->nticks - keep) * s->nslots * sizeof(int64_t));
      memmove(s->uhist, s->uhist + drop,
              (s->nticks - keep) * sizeof(uint64_t));
   }
   s->hbase = keep;

   if (s->wide && s->nslots > 0) {
      // Keep only the wide values of the slots in the ticks which remain
      wpool_t np = {};
      for (int64_t t = 0; t < s->nticks - keep; t++) {
         for (int j = 0; j < s->nslots; j++) {
            if (s->sw[j] > 64) {
               int64_t *h = &(s->shist[t * s->nslots + j]);
               *h = wpool_add(&np, wpool_get(&s->ppool, *h));
            }
         }
      }

      for (int j = 0; j < s->nslots; j++) {
         if (s->sw[j] > 64)
            s->cur_slot[j] = 0;
      }

      free(s->ppool.w);
      s->ppool = np;
   }
}

unsigned sva_finish(sva_t *s)
{
   // Attempts of strong properties which have not completed fail

   unsigned fails = 0;
   s->tpool.n = 0;
   if (s->root >= 0 && s->kind == SVA_ASSERT) {
      s->eot = true;

      for (unsigned i = 0; i < s->nlive; i++) {
         bool nv;
         if (eval_root(s, s->live[i], &nv) == TRI_FALSE)
            fails++;
      }

      s->eot = false;
   }

   s->nlive = 0;
   return s->fail_on ? fails : 0;
}

unsigned sva_nevents(const sva_t *s)
{
   return s->nevents;
}

int sva_event_call(const sva_t *s, unsigned k)
{
   return s->events[k].call;
}

int64_t sva_event_local(const sva_t *s, unsigned k, int local)
{
   return s->events[k].v[local];
}

unsigned sva_result(const sva_t *s, sva_result_t which)
{
   switch (which) {
   case SVA_RESULT_PASS_ACTIONS:
      return (s->pass_nv ? s->passes : 0) + (s->pass_vac ? s->vpasses : 0);
   case SVA_RESULT_FAIL_ACTIONS:
      return s->fail_on ? s->fails : 0;
   case SVA_RESULT_SUCCESSES:
      return s->passes + s->vpasses;
   default:
      return 0;
   }
}

static void sva_reserve(sva_t *s)
{
   if (s->nticks - s->hbase == s->hcap) {
      s->hcap = MAX(256, s->hcap * 2);
      s->hist = xrealloc_array(s->hist, s->hcap, sizeof(uint64_t));
      if (s->nslots > 0) {
         s->shist = xrealloc_array(s->shist, s->hcap * s->nslots,
                                   sizeof(int64_t));
         s->uhist = xrealloc_array(s->uhist, s->hcap, sizeof(uint64_t));
      }
   }
}

static void sva_probe_attempts(sva_t *s, int64_t t)
{
   for (unsigned i = 0; i <= s->nlive; i++) {
      const int64_t start = i < s->nlive ? s->live[i] : t;

      if (s->kind == SVA_COVER_SEQUENCE && s->lmode) {
         lvals_t in;
         lvals_init(s, &in);

         lres_t r = {};
         leval_seq(s, s->root, start, &in, &r);
         lres_free(&r);
      }
      else if (s->kind == SVA_COVER_SEQUENCE) {
         seqres_t r = {};
         eval_seq(s, s->root, start, &r);
         res_free(&r);
      }
      else {
         bool nv = true;
         (void)eval_root(s, start, &nv);
      }
   }
}

static void sva_probe_tick(sva_t *s, int64_t t, uint64_t leaves)
{
   s->hist[t - s->hbase] = leaves;
   if (s->nslots > 0) {
      memset(s->shist + (t - s->hbase) * s->nslots, 0,
             s->nslots * sizeof(int64_t));
      s->uhist[t - s->hbase] = 0;
   }

   sva_probe_attempts(s, t);
}

static void sva_probe(sva_t *s)
{
   // Find the leaves which the attempts evaluate at the next tick whatever
   // their values are so an expression with side effects is only evaluated
   // when an attempt reaches it

   assert(s->root >= 0);

   sva_reserve(s);
   s->tpool.n = 0;

   const int64_t t = s->nticks;
   s->probe_t = t;
   s->needed = 0;
   s->nticks = t + 1;

   for (int pass = 0; pass < 2; pass++)
      sva_probe_tick(s, t, pass ? ~UINT64_C(0) : 0);

   s->nticks = t;
   s->probe_t = -1;
   s->needed_t = t;
}

static void sva_probe_slots(sva_t *s, uint64_t leaves)
{
   // Find the slots which the attempts read at the next tick given the
   // values of the leaves

   assert(s->root >= 0);

   sva_reserve(s);
   s->tpool.n = 0;

   const int64_t t = s->nticks;
   s->probe_t = t;
   s->needed_slots = 0;
   s->nticks = t + 1;

   sva_probe_tick(s, t, leaves);

   s->nticks = t;
   s->probe_t = -1;
   s->slots_t = t;
}

bool sva_wanted(sva_t *s, int leaf)
{
   if (s->needed_t != s->nticks)
      sva_probe(s);

   return (s->needed >> leaf) & 1;
}

bool sva_slot_wanted(sva_t *s, int slot, uint64_t leaves)
{
   if (s->slots_t != s->nticks)
      sva_probe_slots(s, leaves);

   return (s->needed_slots >> slot) & 1;
}

unsigned sva_tick(sva_t *s, uint64_t leaves, int disable)
{
   // DISABLE is one if the disable condition is true at this tick and two
   // if it was only true at some time since the last tick

   assert(s->root >= 0);

   sva_reserve(s);
   s->tpool.n = 0;

   s->hist[s->nticks - s->hbase] = leaves;
   if (s->nslots > 0) {
      const int64_t idx = s->nticks - s->hbase;
      memcpy(s->shist + idx * s->nslots, s->cur_slot,
             s->nslots * sizeof(int64_t));
      s->uhist[idx] = s->cur_unk;
   }
   for (uint64_t m = leaves; m != 0; m &= m - 1)
      s->last_true[__builtin_ctzll(m)] = s->nticks;
   s->nticks++;

   s->passes = s->vpasses = s->fails = 0;
   s->nevents = 0;

   if (disable == 1) {
      // The attempts in progress are aborted and run no action (16.12)
      s->nlive = 0;
      s->idle = !s->off;
      sva_compact(s);
      return 0;
   }
   else if (disable == 2) {
      // The attempts which were in progress when the condition was true
      // are aborted but a new attempt starts at this tick
      s->nlive = 0;
   }

   if (s->nlive == s->maxlive) {
      s->maxlive = MAX(16, s->maxlive * 2);
      s->live = xrealloc_array(s->live, s->maxlive, sizeof(int64_t));
   }

   // An assertion switched off with $assertoff starts no new attempts but
   // those already in progress go on (20.12)
   if (!s->off && (s->kind != SVA_EXPECT || s->nticks == 1))
      s->live[s->nlive++] = s->nticks - 1;

   unsigned fails = 0, passes = 0, vpasses = 0, keep = 0;
   for (unsigned i = 0; i < s->nlive; i++) {
      const int64_t start = s->live[i];
      bool done = true;

      s->cur_start = start;

      switch (s->kind) {
      case SVA_COVER_SEQUENCE:
         {
            if (s->lmode) {
               lvals_t in;
               lvals_init(s, &in);

               lres_t r = {};
               leval_seq(s, s->root, start, &in, &r);
               for (unsigned j = 0; j < r.n; j++) {
                  if (r.m[j].end == s->nticks - 1)
                     passes++;
               }
               done = !r.open;
               lres_free(&r);
            }
            else {
               seqres_t r;
               eval_seq(s, s->root, start, &r);
               if (res_get(&r, s->nticks - 1))
                  passes++;
               done = !r.open;
               res_free(&r);
            }
         }
         break;

      default:
         {
            bool nv = true;
            switch (eval_root(s, start, &nv)) {
            case TRI_TRUE:
               // A vacuous success runs the pass statement of an assertion
               // (16.14.1) but is not counted by a cover (16.14.3)
               if (nv)
                  passes++;
               else if (s->kind != SVA_COVER_PROPERTY)
                  vpasses++;
               break;
            case TRI_FALSE:
               if (s->kind == SVA_ASSERT || s->kind == SVA_EXPECT)
                  fails++;
               break;
            case TRI_PENDING:
               done = false;
               break;
            }
         }
         break;
      }

      if (!done)
         s->live[keep++] = start;
   }

   s->nlive = keep;
   sva_compact(s);

   s->passes = passes;
   s->vpasses = vpasses;
   s->fails = fails;

   // A vacuous success only matters where it runs a statement.  The
   // process must not sleep while new attempts are switched off as it
   // would not see them switched back on.
   const bool vacuous_seen =
      vpasses > 0 && (s->has_pass || s->kind == SVA_EXPECT);
   s->idle = keep == 0 && passes == 0 && fails == 0 && !vacuous_seen
      && !s->off;
   return fails;
}

bool sva_idle(const sva_t *s)
{
   return s->idle;
}
