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
#include "array.h"
#include "jit/jit.h"
#include "rt/island.h"
#include "rt/model.h"
#include "rt/structs.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct _rt_island_mgr {
   rt_model_t    *model;
   A(rt_island_t *) islands;
   A(rt_island_t *) dirty_q;
   uint32_t        eval_epoch;
};

static inline uint64_t island_top_mask(uint32_t width)
{
   return (width & 63u) ? ((UINT64_C(1) << (width & 63u)) - 1u) : ~UINT64_C(0);
}

static void island_mark_dirty(rt_island_t *isl)
{
   if (isl->dirty)
      return;

   isl->dirty = true;
   if (!isl->in_dirty_list) {
      APUSH(isl->mgr->dirty_q, isl);
      isl->in_dirty_list = true;
   }
}

static void island_input_watch_cb(uint64_t now, rt_signal_t *signal,
                                  rt_watch_t *watch, void *user)
{
   rt_island_t *isl = user;
   island_mark_dirty(isl);
}

rt_island_mgr_t *rt_island_mgr_new(rt_model_t *m)
{
   rt_island_mgr_t *mgr = xcalloc(sizeof(rt_island_mgr_t));
   mgr->model = m;
   return mgr;
}

void rt_island_mgr_free(rt_island_mgr_t *mgr)
{
   if (mgr == NULL)
      return;

   for (unsigned i = 0; i < mgr->islands.count; i++) {
      rt_island_t *isl = mgr->islands.items[i];
      free(isl->state);
      free(isl->sigs);
      free(isl->in_sigs);
      free(isl->sync_sigs);
      free(isl->scratch);
      free(isl);
   }
   ACLEAR(mgr->islands);
   ACLEAR(mgr->dirty_q);
   free(mgr);
}

static void island_init_state_defaults(rt_island_t *isl)
{
   memset(isl->state, 0, isl->state_bytes);

   for (uint32_t i = 0; i < isl->nsigs; i++) {
      const rt_island_sig_t *s = &(isl->sigs[i]);
      if (s->width == 0 || s->nelems == 0 || (s->flags & RT_ISIG_TWO_STATE))
         continue;

      const uint64_t top_mask = island_top_mask(s->width);
      const bool is_net = !!(s->flags & RT_ISIG_IS_NET);
      uint64_t *ap = (uint64_t *)(isl->state + s->a_off);
      uint64_t *bp = (uint64_t *)(isl->state + s->b_off);

      // SV X is (a=1, b=1); Z (unconnected wire default) is (a=0, b=1)
      for (uint32_t e = 0; e < s->nelems; e++) {
         const uint32_t base = e * s->nwords;
         for (uint32_t w = 0; w < s->nwords; w++) {
            const uint64_t mask = (w == s->nwords - 1) ? top_mask : ~UINT64_C(0);
            ap[base + w] = is_net ? 0 : mask;
            bp[base + w] = mask;
         }
      }
   }
}

void *rt_island_begin(rt_model_t *m, rt_island_mgr_t **mgr_p,
                      jit_handle_t eval_handle, void *inst_ctx,
                      uint32_t state_bytes, uint32_t nsigs)
{
   if (*mgr_p == NULL)
      *mgr_p = rt_island_mgr_new(m);

   rt_island_t *isl = xcalloc(sizeof(rt_island_t));
   isl->model = m;
   isl->mgr = *mgr_p;
   isl->eval_handle = eval_handle;
   isl->inst_ctx = inst_ctx;
   isl->state_bytes = ALIGN_UP(MAX(64u, state_bytes), 64u);
   isl->nsigs = nsigs;

   void *buf = NULL;
   if (posix_memalign(&buf, 64, isl->state_bytes) != 0)
      fatal("failed to allocate %u bytes for island state", isl->state_bytes);
   isl->state = buf;
   memset(isl->state, 0, isl->state_bytes);

   if (nsigs > 0)
      isl->sigs = xcalloc_array(nsigs, sizeof(rt_island_sig_t));

   APUSH(isl->mgr->islands, isl);
   return isl;
}

void rt_island_bind_sig(void *handle, uint32_t idx, sig_shared_t *ss,
                        int32_t sig_offset, uint32_t a_off, uint32_t b_off,
                        uint32_t width, uint32_t nelems, uint32_t flags)
{
   rt_island_t *isl = handle;
   assert(idx < isl->nsigs);

   rt_signal_t *sig = ss != NULL ? container_of(ss, rt_signal_t, shared) : NULL;
   rt_island_sig_t *s = &(isl->sigs[idx]);
   s->sig = sig;
   s->sig_offset = sig_offset;
   s->a_off = a_off;
   s->b_off = b_off;
   s->width = width;
   s->nelems = nelems;
   s->nwords = (width + 63u) / 64u;
   s->flags = flags;
}

void rt_island_finish(void *handle)
{
   rt_island_t *isl = handle;
   rt_model_t *m = isl->model;

   uint32_t n_in = 0, n_sync = 0, max_words = 1;
   for (uint32_t i = 0; i < isl->nsigs; i++) {
      const rt_island_sig_t *s = &(isl->sigs[i]);
      if (s->sig == NULL)
         continue;
      if (s->nwords > max_words)
         max_words = s->nwords;
      if (s->flags & RT_ISIG_IN)
         n_in++;
      if (s->flags & RT_ISIG_COMB_WRITTEN)
         n_sync++;
   }

   isl->n_in_sigs = n_in;
   if (n_in > 0)
      isl->in_sigs = xmalloc_array(n_in, sizeof(uint32_t));
   isl->n_sync_sigs = n_sync;
   if (n_sync > 0)
      isl->sync_sigs = xmalloc_array(n_sync, sizeof(uint32_t));

   isl->scratch_words = max_words;
   isl->scratch = xcalloc_array(max_words * 2u, sizeof(uint64_t));

   uint32_t in_pos = 0, sync_pos = 0;
   for (uint32_t i = 0; i < isl->nsigs; i++) {
      rt_island_sig_t *s = &(isl->sigs[i]);
      if (s->sig == NULL)
         continue;

      if (s->flags & RT_ISIG_IN) {
         isl->in_sigs[in_pos++] = i;
         s->watch = watch_new(m, island_input_watch_cb, isl, WATCH_EVENT, 1);
         if (s->sig->shared.flags & SIG_F_PACKED)
            model_set_event_cb(m, s->sig, 0, 1, s->watch);
         else
            model_set_event_cb(m, s->sig, s->sig_offset,
                               (int)(s->width * s->nelems), s->watch);
      }
      if (s->flags & RT_ISIG_COMB_WRITTEN)
         isl->sync_sigs[sync_pos++] = i;
   }

   island_init_state_defaults(isl);
   isl->initialized = true;
}

static inline uint8_t std_logic_to_sv(uint8_t v)
{
   // _U,_X,_0,_1,_Z,_W,_L,_H,-
   static const uint8_t map[9] = { 3, 3, 0, 1, 2, 3, 0, 1, 3 };
   return v < 9 ? map[v] : 3;
}

static inline uint8_t sv_to_std_logic(uint8_t v)
{
   static const uint8_t map[4] = { 2, 3, 4, 1 };
   return map[v & 3];
}

static void island_sync_in_sig(rt_island_t *isl, const rt_island_sig_t *s)
{
   rt_signal_t *sig = s->sig;
   const uint32_t width = s->width;
   const uint32_t nwords = s->nwords;
   const uint32_t nelems = s->nelems;
   const bool two_state = !!(s->flags & RT_ISIG_TWO_STATE);
   uint64_t *dst_a = (uint64_t *)(isl->state + s->a_off);
   uint64_t *dst_b = two_state ? NULL : (uint64_t *)(isl->state + s->b_off);

   if (sig->shared.flags & SIG_F_PACKED) {
      const uint32_t sig_w = sig->bit_width;
      const uint32_t sig_nw = (sig_w + 63u) / 64u;
      const bool sig_is4 = !!(sig->shared.flags & SIG_F_PACKED4);
      const uint64_t *src_a = (const uint64_t *)sig->shared.data;
      const uint64_t *src_b = sig_is4 ? src_a + sig_nw : NULL;

      if (nelems == 1 && s->sig_offset == 0 && width == sig_w) {
         const uint64_t top_mask = island_top_mask(width);
         for (uint32_t w = 0; w < nwords; w++) {
            const uint64_t msk = (w == nwords - 1) ? top_mask : ~UINT64_C(0);
            uint64_t a = src_a[w] & msk;
            uint64_t b = (src_b != NULL) ? (src_b[w] & msk) : 0;
            if (two_state) {
               a &= ~b;
               dst_a[w] = a;
            }
            else {
               dst_a[w] = a;
               dst_b[w] = b;
            }
         }
         return;
      }

      // Sliced / collapsed packed signal (msb_off = sig_offset .. width*nelems)
      for (uint32_t e = 0; e < nelems; e++) {
         uint64_t *ea = dst_a + e * nwords;
         uint64_t *eb = dst_b != NULL ? dst_b + e * nwords : NULL;
         memset(ea, 0, nwords * sizeof(uint64_t));
         if (eb != NULL)
            memset(eb, 0, nwords * sizeof(uint64_t));

         for (uint32_t i = 0; i < width; i++) {
            const uint32_t msb_bit = (uint32_t)s->sig_offset + e * width + i;
            const uint32_t sig_bit = sig_w - 1u - msb_bit;
            const uint32_t dst_bit = width - 1u - i;
            const uint64_t a = (src_a[sig_bit / 64u] >> (sig_bit & 63u)) & 1u;
            const uint64_t b = src_b != NULL
               ? ((src_b[sig_bit / 64u] >> (sig_bit & 63u)) & 1u) : 0;
            const uint64_t aval = two_state ? (a & ~b) : a;
            if (aval)
               ea[dst_bit / 64u] |= UINT64_C(1) << (dst_bit & 63u);
            if (eb != NULL && b)
               eb[dst_bit / 64u] |= UINT64_C(1) << (dst_bit & 63u);
         }
      }
      return;
   }

   const uint8_t *bytes = sig->shared.data + s->sig_offset;
   const bool is_std = !!(sig->shared.flags & SIG_F_STD_LOGIC);

   for (uint32_t e = 0; e < nelems; e++) {
      uint64_t *ea = dst_a + e * nwords;
      uint64_t *eb = dst_b != NULL ? dst_b + e * nwords : NULL;
      memset(ea, 0, nwords * sizeof(uint64_t));
      if (eb != NULL)
         memset(eb, 0, nwords * sizeof(uint64_t));

      const uint8_t *ebytes = bytes + e * width;
      for (uint32_t i = 0; i < width; i++) {
         const uint8_t raw = is_std ? std_logic_to_sv(ebytes[i]) : ebytes[i];
         const uint32_t bit = width - 1u - i;
         const uint64_t a = raw & 1u;
         const uint64_t b = (raw >> 1) & 1u;
         const uint64_t aval = two_state ? (a & ~b) : a;
         if (aval)
            ea[bit / 64u] |= UINT64_C(1) << (bit & 63u);
         if (eb != NULL && b)
            eb[bit / 64u] |= UINT64_C(1) << (bit & 63u);
      }
   }
}

static void island_sync_out_sig(rt_model_t *m, rt_island_t *isl,
                                const rt_island_sig_t *s)
{
   rt_signal_t *sig = s->sig;
   const bool required = (s->flags & RT_ISIG_OUT) != 0;
   const bool observed = sig->nexus.pending != NULL
      || (sig->shared.flags & SIG_F_POLL_WATCHED) != 0;
   if (!required && !observed)
      return;

   const uint32_t width = s->width;
   const uint32_t nwords = s->nwords;
   const uint32_t nelems = s->nelems;
   const bool two_state = !!(s->flags & RT_ISIG_TWO_STATE);
   const uint64_t *src_a = (const uint64_t *)(isl->state + s->a_off);
   const uint64_t *src_b = two_state
      ? NULL : (const uint64_t *)(isl->state + s->b_off);
   const uint64_t top_mask = island_top_mask(width);

   if (sig->shared.flags & SIG_F_PACKED) {
      const uint32_t sig_w = sig->bit_width;
      const uint32_t sig_nw = (sig_w + 63u) / 64u;
      const bool sig_is4 = !!(sig->shared.flags & SIG_F_PACKED4);
      const uint64_t *cur_a = (const uint64_t *)sig->shared.data;
      const uint64_t *cur_b = sig_is4 ? cur_a + sig_nw : NULL;

      if (nelems == 1 && s->sig_offset == 0 && width == sig_w) {
         bool changed = false;
         for (uint32_t w = 0; w < nwords && !changed; w++) {
            const uint64_t msk = (w == nwords - 1) ? top_mask : ~UINT64_C(0);
            const uint64_t na = src_a[w] & msk;
            const uint64_t nb = (sig_is4 && src_b != NULL) ? (src_b[w] & msk) : 0;
            if ((cur_a[w] & msk) != na)
               changed = true;
            else if (sig_is4 && ((cur_b[w] & msk) != nb))
               changed = true;
         }
         if (!changed)
            return;

         if (nwords == 1) {
            const uint64_t a0 = src_a[0] & top_mask;
            const uint64_t b0 = src_b != NULL ? (src_b[0] & top_mask) : 0;
            rt_island_deposit_packed(m, sig, 0, width, a0, b0,
                                     PACK_SRC_SCALAR
                                     | (two_state ? PACK_SRC_VEC2 : 0));
         }
         else {
            rt_island_deposit_packed(m, sig, 0, width,
                                     (uint64_t)(uintptr_t)src_a,
                                     (uint64_t)(uintptr_t)src_b,
                                     two_state ? PACK_SRC_VEC2 : 0);
         }
         return;
      }

      for (uint32_t e = 0; e < nelems; e++) {
         const uint64_t *ea = src_a + e * nwords;
         const uint64_t *eb = src_b != NULL ? src_b + e * nwords : NULL;
         const uint32_t msb_off = (uint32_t)s->sig_offset + e * width;
         if (nwords == 1) {
            rt_island_deposit_packed(m, sig, msb_off, width,
                                     ea[0] & top_mask,
                                     eb != NULL ? (eb[0] & top_mask) : 0,
                                     PACK_SRC_SCALAR
                                     | (two_state ? PACK_SRC_VEC2 : 0));
         }
         else {
            rt_island_deposit_packed(m, sig, msb_off, width,
                                     (uint64_t)(uintptr_t)ea,
                                     (uint64_t)(uintptr_t)eb,
                                     two_state ? PACK_SRC_VEC2 : 0);
         }
      }
      return;
   }

   const uint32_t total = width * nelems;
   uint8_t stack_buf[64];
   uint8_t *heap_buf = NULL;
   uint8_t *out_bytes = stack_buf;
   if (total > sizeof(stack_buf)) {
      heap_buf = xmalloc(total);
      out_bytes = heap_buf;
   }

   const bool is_std = !!(sig->shared.flags & SIG_F_STD_LOGIC);
   for (uint32_t e = 0; e < nelems; e++) {
      const uint64_t *ea = src_a + e * nwords;
      const uint64_t *eb = src_b != NULL ? src_b + e * nwords : NULL;
      for (uint32_t i = 0; i < width; i++) {
         const uint32_t bit = width - 1u - i;
         const uint8_t a = (ea[bit / 64u] >> (bit & 63u)) & 1u;
         const uint8_t b = (eb != NULL)
            ? ((eb[bit / 64u] >> (bit & 63u)) & 1u) : 0;
         const uint8_t sv = (uint8_t)(a | (b << 1));
         out_bytes[e * width + i] = is_std ? sv_to_std_logic(sv) : sv;
      }
   }

   const uint8_t *cur = sig->shared.data + s->sig_offset;
   if (memcmp(cur, out_bytes, total) != 0)
      deposit_signal(m, sig, out_bytes, s->sig_offset, total);

   free(heap_buf);
}

void rt_island_reset_all(rt_island_mgr_t *mgr)
{
   if (mgr == NULL)
      return;

   mgr->dirty_q.count = 0;

   for (unsigned i = 0; i < mgr->islands.count; i++) {
      rt_island_t *isl = mgr->islands.items[i];
      island_init_state_defaults(isl);
      for (uint32_t j = 0; j < isl->nsigs; j++) {
         if (isl->sigs[j].sig != NULL)
            island_sync_in_sig(isl, &(isl->sigs[j]));
      }
      isl->dirty = false;
      isl->in_dirty_list = false;
      island_mark_dirty(isl);
   }
}

bool rt_island_eval_dirty(jit_t *jit, rt_model_t *m, rt_island_mgr_t *mgr)
{
   if (mgr == NULL || mgr->dirty_q.count == 0)
      return true;

   unsigned n = mgr->dirty_q.count;
   bool ok = true;
   for (unsigned i = 0; i < n; i++) {
      rt_island_t *isl = mgr->dirty_q.items[i];
      isl->in_dirty_list = false;
      if (!isl->dirty)
         continue;
      isl->dirty = false;

      for (uint32_t j = 0; j < isl->n_in_sigs; j++)
         island_sync_in_sig(isl, &(isl->sigs[isl->in_sigs[j]]));

      jit_scalar_t p1 = { .pointer = isl->inst_ctx };
      jit_scalar_t p2 = { .pointer = isl->state };
      jit_scalar_t result = { .integer = 0 };
      tlab_t tlab = jit_null_tlab(jit);
      if (!jit_fastcall(jit, isl->eval_handle, &result, p1, p2, &tlab)) {
         ok = false;
         break;
      }

      for (uint32_t j = 0; j < isl->n_sync_sigs; j++)
         island_sync_out_sig(m, isl, &(isl->sigs[isl->sync_sigs[j]]));
   }

   if (mgr->dirty_q.count > n) {
      memmove(mgr->dirty_q.items, mgr->dirty_q.items + n,
              (mgr->dirty_q.count - n) * sizeof(rt_island_t *));
      mgr->dirty_q.count -= n;
   }
   else
      mgr->dirty_q.count = 0;

   return ok;
}
