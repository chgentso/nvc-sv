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
   rt_model_t             *model;
   A(rt_island_t *)        islands;
   A(rt_island_t *)        dirty_q;
   A(rt_island_domain_t *) ff_sample_q;
   A(rt_island_domain_t *) ff_commit_q;
   uint32_t                eval_epoch;
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

static inline uint8_t island_read_sv_bit(const rt_signal_t *sig,
                                         int32_t sig_offset)
{
   if (sig->shared.flags & SIG_F_PACKED) {
      const uint32_t sig_w = sig->bit_width;
      const uint32_t sig_nw = (sig_w + 63u) / 64u;
      const bool sig_is4 = !!(sig->shared.flags & SIG_F_PACKED4);
      const uint64_t *src_a = (const uint64_t *)sig->shared.data;
      const uint64_t *src_b = sig_is4 ? src_a + sig_nw : NULL;
      const uint32_t sig_bit = sig_w - 1u - (uint32_t)sig_offset;
      const uint64_t a = (src_a[sig_bit / 64u] >> (sig_bit & 63u)) & 1u;
      const uint64_t b = src_b != NULL
         ? ((src_b[sig_bit / 64u] >> (sig_bit & 63u)) & 1u) : 0;
      return (uint8_t)((b << 1) | a);
   }

   const uint8_t raw = sig->shared.data[sig_offset];
   if (sig->shared.flags & SIG_F_STD_LOGIC)
      return std_logic_to_sv(raw);
   return raw & 3u;
}

static inline bool island_edge_match(uint8_t old_v, uint8_t new_v, bool posedge)
{
   if (posedge)
      return (old_v == 0 && new_v != 0) || (old_v != 1 && new_v == 1);
   else
      return (old_v == 1 && new_v != 1) || (old_v != 0 && new_v == 0);
}

static inline bool island_sig_observed(const rt_signal_t *sig)
{
   if (sig->nexus.pending != NULL
       || (sig->shared.flags & SIG_F_POLL_WATCHED) != 0)
      return true;
   if (unlikely(sig->n_nexus > 1)) {
      for (const rt_nexus_t *nx = sig->nexus.chain; nx != NULL; nx = nx->chain) {
         if (nx->pending != NULL)
            return true;
      }
   }
   return false;
}

static void island_input_watch_cb(uint64_t now, rt_signal_t *signal,
                                  rt_watch_t *watch, void *user)
{
   rt_island_t *isl = user;
   island_mark_dirty(isl);
}

static void island_clk_watch_cb(uint64_t now, rt_signal_t *signal,
                                rt_watch_t *watch, void *user)
{
   rt_island_domain_t *dom = user;
   const rt_island_sig_t *cs = &(dom->isl->sigs[dom->clk_sig_idx]);
   const uint8_t cur = island_read_sv_bit(cs->sig, cs->sig_offset);
   const bool hit = island_edge_match(dom->prev_clk, cur, dom->clk_posedge);
   dom->prev_clk = cur;
   if (hit && !dom->in_sample_q) {
      dom->in_sample_q = true;
      APUSH(dom->isl->mgr->ff_sample_q, dom);
   }
}

static void island_rst_watch_cb(uint64_t now, rt_signal_t *signal,
                                rt_watch_t *watch, void *user)
{
   rt_island_domain_t *dom = user;
   const rt_island_sig_t *rs = &(dom->isl->sigs[dom->rst_sig_idx]);
   const uint8_t cur = island_read_sv_bit(rs->sig, rs->sig_offset);
   const bool hit = island_edge_match(dom->prev_rst, cur, dom->rst_posedge);
   dom->prev_rst = cur;
   if (hit && !dom->in_sample_q) {
      dom->in_sample_q = true;
      APUSH(dom->isl->mgr->ff_sample_q, dom);
   }
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
      for (uint32_t d = 0; d < isl->ndomains; d++)
         free(isl->domains[d].write_sigs);
      free(isl->domains);
      free(isl->state);
      free(isl->sigs);
      free(isl->in_sigs);
      free(isl->sync_sigs);
      free(isl->scratch);
      free(isl);
   }
   ACLEAR(mgr->islands);
   ACLEAR(mgr->dirty_q);
   ACLEAR(mgr->ff_sample_q);
   ACLEAR(mgr->ff_commit_q);
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

   if (isl->next_state != NULL)
      memcpy(isl->next_state, isl->state, isl->state_bytes);
}

void *rt_island_begin(rt_model_t *m, rt_island_mgr_t **mgr_p,
                      jit_handle_t eval_handle, void *inst_ctx,
                      uint32_t state_bytes, uint32_t nsigs, uint32_t ndomains)
{
   if (*mgr_p == NULL)
      *mgr_p = rt_island_mgr_new(m);

   rt_island_t *isl = xcalloc(sizeof(rt_island_t));
   isl->model = m;
   isl->mgr = *mgr_p;
   isl->eval_handle = eval_handle;
   isl->inst_ctx = inst_ctx;
   isl->state_bytes = RT_ISLAND_STRIDE(state_bytes);
   isl->nsigs = nsigs;
   isl->ndomains = ndomains;

   const size_t total_bytes = ndomains > 0
      ? (size_t)isl->state_bytes * 2u : (size_t)isl->state_bytes;
   void *buf = NULL;
   if (posix_memalign(&buf, 64, total_bytes) != 0)
      fatal("failed to allocate %zu bytes for island state", total_bytes);
   isl->state = buf;
   isl->next_state = ndomains > 0 ? isl->state + isl->state_bytes : NULL;
   memset(isl->state, 0, total_bytes);

   if (nsigs > 0)
      isl->sigs = xcalloc_array(nsigs, sizeof(rt_island_sig_t));
   if (ndomains > 0) {
      isl->domains = xcalloc_array(ndomains, sizeof(rt_island_domain_t));
      for (uint32_t d = 0; d < ndomains; d++) {
         isl->domains[d].isl = isl;
         isl->domains[d].dom_idx = d;
         isl->domains[d].ff_handle = JIT_HANDLE_INVALID;
         isl->domains[d].rst_sig_idx = -1;
      }
   }

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

void rt_island_add_domain(void *handle, uint32_t dom_idx,
                          jit_handle_t ff_handle, void *inst_ctx,
                          uint32_t clk_sig_idx, int32_t rst_sig_idx,
                          uint32_t edge_flags, uint32_t n_write_sigs)
{
   rt_island_t *isl = handle;
   assert(dom_idx < isl->ndomains);
   assert(clk_sig_idx < isl->nsigs);
   assert(rst_sig_idx < 0 || (uint32_t)rst_sig_idx < isl->nsigs);

   if (isl->inst_ctx == NULL)
      isl->inst_ctx = inst_ctx;

   rt_island_domain_t *dom = &(isl->domains[dom_idx]);
   dom->ff_handle = ff_handle;
   dom->clk_sig_idx = clk_sig_idx;
   dom->rst_sig_idx = rst_sig_idx;
   dom->clk_posedge = !!(edge_flags & 1u);
   dom->rst_posedge = !!(edge_flags & 2u);
   dom->cap_write_sigs = n_write_sigs;
   dom->n_write_sigs = 0;
   if (n_write_sigs > 0)
      dom->write_sigs = xmalloc_array(n_write_sigs, sizeof(uint32_t));
}

void rt_island_add_domain_write(void *handle, uint32_t dom_idx,
                                uint32_t sig_idx)
{
   rt_island_t *isl = handle;
   assert(dom_idx < isl->ndomains);
   assert(sig_idx < isl->nsigs);

   rt_island_domain_t *dom = &(isl->domains[dom_idx]);
   assert(dom->n_write_sigs < dom->cap_write_sigs);
   dom->write_sigs[dom->n_write_sigs++] = sig_idx;
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

   for (uint32_t d = 0; d < isl->ndomains; d++) {
      rt_island_domain_t *dom = &(isl->domains[d]);
      if (dom->ff_handle == JIT_HANDLE_INVALID)
         continue;

      const rt_island_sig_t *cs = &(isl->sigs[dom->clk_sig_idx]);
      assert(cs->sig != NULL);
      dom->clk_watch = watch_new(m, island_clk_watch_cb, dom, WATCH_EVENT, 1);
      if (cs->sig->shared.flags & SIG_F_PACKED)
         model_set_event_cb(m, cs->sig, 0, 1, dom->clk_watch);
      else
         model_set_event_cb(m, cs->sig, cs->sig_offset, 1, dom->clk_watch);

      if (dom->rst_sig_idx >= 0) {
         const rt_island_sig_t *rs = &(isl->sigs[dom->rst_sig_idx]);
         assert(rs->sig != NULL);
         dom->rst_watch = watch_new(m, island_rst_watch_cb, dom,
                                    WATCH_EVENT, 1);
         if (rs->sig->shared.flags & SIG_F_PACKED)
            model_set_event_cb(m, rs->sig, 0, 1, dom->rst_watch);
         else
            model_set_event_cb(m, rs->sig, rs->sig_offset, 1, dom->rst_watch);
      }
   }

   island_init_state_defaults(isl);
   isl->initialized = true;
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
   if (!required && !island_sig_observed(sig))
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

static inline void island_copy_sig_buf(uint8_t *dst_base,
                                       const uint8_t *src_base,
                                       const rt_island_sig_t *s)
{
   const size_t bytes = (size_t)s->nwords * s->nelems * sizeof(uint64_t);
   memcpy(dst_base + s->a_off, src_base + s->a_off, bytes);
   if (!(s->flags & RT_ISIG_TWO_STATE))
      memcpy(dst_base + s->b_off, src_base + s->b_off, bytes);
}

static inline bool island_commit_sig_buf(rt_island_t *isl,
                                         const rt_island_sig_t *s)
{
   const uint32_t total_words = s->nwords * s->nelems;
   uint64_t *dst_a = (uint64_t *)(isl->state + s->a_off);
   const uint64_t *src_a = (const uint64_t *)(isl->next_state + s->a_off);
   bool changed = false;

   if (s->flags & RT_ISIG_TWO_STATE) {
      for (uint32_t i = 0; i < total_words; i++) {
         if (dst_a[i] != src_a[i]) {
            dst_a[i] = src_a[i];
            changed = true;
         }
      }
      return changed;
   }

   uint64_t *dst_b = (uint64_t *)(isl->state + s->b_off);
   const uint64_t *src_b = (const uint64_t *)(isl->next_state + s->b_off);
   for (uint32_t i = 0; i < total_words; i++) {
      if (dst_a[i] != src_a[i] || dst_b[i] != src_b[i]) {
         dst_a[i] = src_a[i];
         dst_b[i] = src_b[i];
         changed = true;
      }
   }
   return changed;
}

void rt_island_reset_all(rt_island_mgr_t *mgr)
{
   if (mgr == NULL)
      return;

   mgr->dirty_q.count = 0;
   mgr->ff_sample_q.count = 0;
   mgr->ff_commit_q.count = 0;

   for (unsigned i = 0; i < mgr->islands.count; i++) {
      rt_island_t *isl = mgr->islands.items[i];
      island_init_state_defaults(isl);
      for (uint32_t j = 0; j < isl->nsigs; j++) {
         if (isl->sigs[j].sig != NULL)
            island_sync_in_sig(isl, &(isl->sigs[j]));
      }
      if (isl->next_state != NULL)
         memcpy(isl->next_state, isl->state, isl->state_bytes);

      for (uint32_t d = 0; d < isl->ndomains; d++) {
         rt_island_domain_t *dom = &(isl->domains[d]);
         const rt_island_sig_t *cs = &(isl->sigs[dom->clk_sig_idx]);
         dom->prev_clk = cs->sig != NULL
            ? island_read_sv_bit(cs->sig, cs->sig_offset) : 3u;
         if (dom->rst_sig_idx >= 0) {
            const rt_island_sig_t *rs = &(isl->sigs[dom->rst_sig_idx]);
            dom->prev_rst = rs->sig != NULL
               ? island_read_sv_bit(rs->sig, rs->sig_offset) : 3u;
         }
         else
            dom->prev_rst = 3u;
         dom->in_sample_q = false;
         dom->in_commit_q = false;
      }

      isl->dirty = false;
      isl->in_dirty_list = false;
      if (isl->eval_handle != JIT_HANDLE_INVALID)
         island_mark_dirty(isl);
   }
}

bool rt_island_has_dirty(const rt_island_mgr_t *mgr)
{
   return mgr != NULL && mgr->dirty_q.count > 0;
}

bool rt_island_eval_dirty(jit_t *jit, rt_model_t *m, rt_island_mgr_t *mgr,
                          tlab_t *tlab)
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

      if (isl->eval_handle == JIT_HANDLE_INVALID)
         continue;

      jit_scalar_t p1 = { .pointer = isl->inst_ctx };
      jit_scalar_t p2 = { .pointer = isl->state };
      jit_scalar_t result = { .integer = 0 };
      tlab_t null_tlab;
      tlab_t *use_tlab = tlab;
      if (use_tlab == NULL) {
         null_tlab = jit_null_tlab(jit);
         use_tlab = &null_tlab;
      }
      if (!jit_fastcall(jit, isl->eval_handle, &result, p1, p2, use_tlab)) {
         if (tlab != NULL)
            tlab_reset(tlab);
         ok = false;
         break;
      }
      if (tlab != NULL)
         tlab_reset(tlab);

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

bool rt_island_has_ff_sample(const rt_island_mgr_t *mgr)
{
   return mgr != NULL && mgr->ff_sample_q.count > 0;
}

bool rt_island_eval_ff(jit_t *jit, rt_model_t *m, rt_island_mgr_t *mgr,
                       tlab_t *tlab)
{
   if (mgr == NULL || mgr->ff_sample_q.count == 0)
      return true;

   unsigned n = mgr->ff_sample_q.count;
   bool ok = true;
   for (unsigned i = 0; i < n; i++) {
      rt_island_domain_t *dom = mgr->ff_sample_q.items[i];
      dom->in_sample_q = false;

      rt_island_t *isl = dom->isl;
      for (uint32_t j = 0; j < dom->n_write_sigs; j++)
         island_copy_sig_buf(isl->next_state, isl->state,
                             &(isl->sigs[dom->write_sigs[j]]));

      jit_scalar_t p1 = { .pointer = isl->inst_ctx };
      jit_scalar_t p2 = { .pointer = isl->state };
      jit_scalar_t result = { .integer = 0 };
      tlab_t null_tlab;
      tlab_t *use_tlab = tlab;
      if (use_tlab == NULL) {
         null_tlab = jit_null_tlab(jit);
         use_tlab = &null_tlab;
      }
      if (!jit_fastcall(jit, dom->ff_handle, &result, p1, p2, use_tlab)) {
         if (tlab != NULL)
            tlab_reset(tlab);
         ok = false;
         break;
      }
      if (tlab != NULL)
         tlab_reset(tlab);

      if (!dom->in_commit_q) {
         dom->in_commit_q = true;
         APUSH(mgr->ff_commit_q, dom);
      }
   }

   if (mgr->ff_sample_q.count > n) {
      memmove(mgr->ff_sample_q.items, mgr->ff_sample_q.items + n,
              (mgr->ff_sample_q.count - n) * sizeof(rt_island_domain_t *));
      mgr->ff_sample_q.count -= n;
   }
   else
      mgr->ff_sample_q.count = 0;

   return ok;
}

bool rt_island_has_ff_commits(const rt_island_mgr_t *mgr)
{
   return mgr != NULL && mgr->ff_commit_q.count > 0;
}

void rt_island_commit_ff(rt_model_t *m, rt_island_mgr_t *mgr)
{
   if (mgr == NULL || mgr->ff_commit_q.count == 0)
      return;

   const unsigned n = mgr->ff_commit_q.count;
   for (unsigned i = 0; i < n; i++) {
      rt_island_domain_t *dom = mgr->ff_commit_q.items[i];
      dom->in_commit_q = false;

      rt_island_t *isl = dom->isl;
      bool comb_need_eval = false;
      for (uint32_t j = 0; j < dom->n_write_sigs; j++) {
         const rt_island_sig_t *s = &(isl->sigs[dom->write_sigs[j]]);
         if (!island_commit_sig_buf(isl, s))
            continue;

         if (s->flags & RT_ISIG_COMB_READ)
            comb_need_eval = true;

         if ((s->flags & RT_ISIG_OUT)
             || (s->sig != NULL && island_sig_observed(s->sig)))
            island_sync_out_sig(m, isl, s);
      }

      if (comb_need_eval && isl->eval_handle != JIT_HANDLE_INVALID)
         island_mark_dirty(isl);
   }

   mgr->ff_commit_q.count = 0;
}
