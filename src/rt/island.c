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
#include "hash.h"
#include "jit/jit.h"
#include "rt/island.h"
#include "rt/model.h"
#include "rt/structs.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct _rt_island_mgr {
   rt_model_t             *model;
   hash_t                 *sig_map;
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

static inline void island_mark_in_dirty(rt_island_t *isl, uint32_t in_idx)
{
   if (likely(isl->n_in_sigs <= 64))
      isl->dirty_in_mask |= (UINT64_C(1) << in_idx);
   else
      isl->dirty_in_words[in_idx >> 6] |= (UINT64_C(1) << (in_idx & 63u));
}

static inline void island_clear_in_dirty(rt_island_t *isl)
{
   isl->dirty_in_mask = 0;
   if (unlikely(isl->dirty_in_words != NULL))
      memset(isl->dirty_in_words, 0, isl->dirty_in_nwords * sizeof(uint64_t));
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

static void island_add_obs_sig(rt_island_t *isl, uint32_t sig_idx)
{
   for (uint32_t i = 0; i < isl->n_obs_sigs; i++) {
      if (isl->obs_sigs[i] == sig_idx)
         return;
   }
   if (isl->n_obs_sigs == isl->cap_obs_sigs) {
      isl->cap_obs_sigs = MAX(isl->cap_obs_sigs * 2u, 8u);
      isl->obs_sigs = xrealloc_array(isl->obs_sigs, isl->cap_obs_sigs,
                                     sizeof(uint32_t));
   }
   isl->obs_sigs[isl->n_obs_sigs++] = sig_idx;
}

static void island_input_watch_cb(uint64_t now, rt_signal_t *signal,
                                  rt_watch_t *watch, void *user)
{
   rt_island_sig_t *s = user;
   rt_island_t *isl = s->isl;
   island_mark_in_dirty(isl, s->in_idx);
   if (s->flags & RT_ISIG_COMB_READ)
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
   mgr->sig_map = hash_new(64);
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
      for (uint32_t f = 0; f < isl->n_forces; f++)
         free(isl->forces[f].mask);
      free(isl->forces);
      free(isl->state);
      free(isl->sigs);
      free(isl->in_sigs);
      free(isl->dirty_in_words);
      free(isl->out_sigs);
      free(isl->obs_sigs);
      free(isl->scratch);
      free(isl);
   }
   hash_free(mgr->sig_map);
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
   s->isl = isl;
   s->sig_idx = idx;
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
   isl->sigs[sig_idx].flags |= RT_ISIG_FF_WRITTEN;
}

void rt_island_finish(void *handle)
{
   rt_island_t *isl = handle;
   rt_model_t *m = isl->model;

   uint32_t n_in = 0, n_out = 0, max_words = 1;
   for (uint32_t i = 0; i < isl->nsigs; i++) {
      const rt_island_sig_t *s = &(isl->sigs[i]);
      if (s->sig == NULL)
         continue;
      if (s->nwords > max_words)
         max_words = s->nwords;
      if (s->flags & RT_ISIG_IN)
         n_in++;
      if ((s->flags & (RT_ISIG_COMB_WRITTEN | RT_ISIG_OUT))
          == (RT_ISIG_COMB_WRITTEN | RT_ISIG_OUT))
         n_out++;
   }

   isl->n_in_sigs = n_in;
   if (n_in > 0) {
      isl->in_sigs = xmalloc_array(n_in, sizeof(uint32_t));
      if (n_in > 64) {
         isl->dirty_in_nwords = (n_in + 63u) / 64u;
         isl->dirty_in_words = xcalloc_array(isl->dirty_in_nwords,
                                             sizeof(uint64_t));
      }
   }
   isl->n_out_sigs = n_out;
   if (n_out > 0)
      isl->out_sigs = xmalloc_array(n_out, sizeof(uint32_t));

   isl->scratch_words = max_words;
   isl->scratch = xcalloc_array(max_words * 2u, sizeof(uint64_t));

   uint32_t in_pos = 0, out_pos = 0;
   for (uint32_t i = 0; i < isl->nsigs; i++) {
      rt_island_sig_t *s = &(isl->sigs[i]);
      if (s->sig == NULL)
         continue;

      if (s->flags & RT_ISIG_IN) {
         s->in_idx = in_pos;
         isl->in_sigs[in_pos++] = i;
         s->watch = watch_new(m, island_input_watch_cb, s, WATCH_EVENT, 1);
         if (s->sig->shared.flags & SIG_F_PACKED)
            model_set_event_cb(m, s->sig, 0, 1, s->watch);
         else
            model_set_event_cb(m, s->sig, s->sig_offset,
                               (int)(s->width * s->nelems), s->watch);
      }
      if ((s->flags & (RT_ISIG_COMB_WRITTEN | RT_ISIG_OUT))
          == (RT_ISIG_COMB_WRITTEN | RT_ISIG_OUT))
         isl->out_sigs[out_pos++] = i;

      if (s->flags & (RT_ISIG_COMB_WRITTEN | RT_ISIG_FF_WRITTEN)) {
         s->sig->shared.flags |= SIG_F_ISLAND;
         s->sig->shared.flags &= ~(SIG_F_FAST_NOOP | SIG_F_FAST_DIRECT);

         rt_island_sig_t *prev = hash_get(isl->mgr->sig_map, s->sig);
         s->next_owner = prev;
         hash_put(isl->mgr->sig_map, s->sig, s);

         if (island_sig_observed(s->sig)) {
            s->flags |= RT_ISIG_OBSERVED;
            if ((s->flags & RT_ISIG_COMB_WRITTEN)
                && !(s->flags & RT_ISIG_OUT))
               island_add_obs_sig(isl, i);
         }
      }
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

   if (nelems == 1 && width == 1) {
      const uint8_t raw = is_std ? std_logic_to_sv(bytes[0]) : (bytes[0] & 3u);
      const uint64_t a = raw & 1u;
      const uint64_t b = (raw >> 1) & 1u;
      dst_a[0] = two_state ? (a & ~b) : a;
      if (dst_b != NULL)
         dst_b[0] = b;
      return;
   }

   if (nelems == 1 && width <= 64) {
      uint64_t a = 0, b = 0;
      if (is_std) {
         for (uint32_t i = 0; i < width; i++) {
            const uint8_t raw = std_logic_to_sv(bytes[i]);
            a = (a << 1) | (raw & 1u);
            b = (b << 1) | ((raw >> 1) & 1u);
         }
      }
      else {
         for (uint32_t i = 0; i < width; i++) {
            const uint8_t raw = bytes[i];
            a = (a << 1) | (raw & 1u);
            b = (b << 1) | ((raw >> 1) & 1u);
         }
      }
      dst_a[0] = two_state ? (a & ~b) : a;
      if (dst_b != NULL)
         dst_b[0] = b;
      return;
   }

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

static void island_sync_dirty_inputs(rt_island_t *isl)
{
   if (likely(isl->n_in_sigs <= 64)) {
      uint64_t msk = isl->dirty_in_mask;
      if (msk == 0)
         return;
      isl->dirty_in_mask = 0;
      while (msk != 0) {
         const unsigned idx = (unsigned)__builtin_ctzll(msk);
         msk &= msk - 1u;
         island_sync_in_sig(isl, &(isl->sigs[isl->in_sigs[idx]]));
      }
   }
   else if (isl->dirty_in_words != NULL) {
      for (uint32_t w = 0; w < isl->dirty_in_nwords; w++) {
         uint64_t msk = isl->dirty_in_words[w];
         if (msk == 0)
            continue;
         isl->dirty_in_words[w] = 0;
         const uint32_t base = w << 6;
         while (msk != 0) {
            const unsigned idx = (unsigned)__builtin_ctzll(msk);
            msk &= msk - 1u;
            island_sync_in_sig(isl, &(isl->sigs[isl->in_sigs[base + idx]]));
         }
      }
   }
}

static void island_sync_out_sig(rt_model_t *m, rt_island_t *isl,
                                const rt_island_sig_t *s)
{
   rt_signal_t *sig = s->sig;
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

   const bool is_std = !!(sig->shared.flags & SIG_F_STD_LOGIC);

   if (nelems == 1 && width == 1) {
      const uint8_t a = (uint8_t)(src_a[0] & 1u);
      const uint8_t b = src_b != NULL ? (uint8_t)(src_b[0] & 1u) : 0u;
      const uint8_t sv = (uint8_t)(a | (b << 1));
      const uint8_t out_b = is_std ? sv_to_std_logic(sv) : sv;
      if (sig->shared.data[s->sig_offset] != out_b)
         deposit_signal(m, sig, &out_b, s->sig_offset, 1);
      return;
   }

   if (nelems == 1 && width <= 64) {
      uint8_t out_bytes[64];
      const uint64_t a0 = src_a[0];
      const uint64_t b0 = src_b != NULL ? src_b[0] : 0u;
      if (b0 == 0) {
         const uint8_t base = is_std ? 2u : 0u;
         for (uint32_t i = 0; i < width; i++)
            out_bytes[i] = (uint8_t)(base + ((a0 >> (width - 1u - i)) & 1u));
      }
      else if (is_std) {
         for (uint32_t i = 0; i < width; i++) {
            const uint32_t shift = width - 1u - i;
            const uint8_t sv = (uint8_t)(((a0 >> shift) & 1u)
                                         | (((b0 >> shift) & 1u) << 1));
            out_bytes[i] = sv_to_std_logic(sv);
         }
      }
      else {
         for (uint32_t i = 0; i < width; i++) {
            const uint32_t shift = width - 1u - i;
            out_bytes[i] = (uint8_t)(((a0 >> shift) & 1u)
                                     | (((b0 >> shift) & 1u) << 1));
         }
      }
      const uint8_t *cur = sig->shared.data + s->sig_offset;
      if (memcmp(cur, out_bytes, width) != 0)
         deposit_signal(m, sig, out_bytes, s->sig_offset, width);
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

static void island_sync_out_slice(rt_model_t *m, rt_island_t *isl,
                                  const rt_island_sig_t *s,
                                  uint32_t local_off, uint32_t local_cnt)
{
   if (local_cnt == 0)
      return;
   if (local_off == 0 && local_cnt >= s->width * s->nelems) {
      island_sync_out_sig(m, isl, s);
      return;
   }

   rt_signal_t *sig = s->sig;
   const uint32_t width = s->width;
   const uint32_t nwords = s->nwords;
   const bool two_state = !!(s->flags & RT_ISIG_TWO_STATE);
   const uint64_t *src_a = (const uint64_t *)(isl->state + s->a_off);
   const uint64_t *src_b = two_state
      ? NULL : (const uint64_t *)(isl->state + s->b_off);

   if (sig->shared.flags & SIG_F_PACKED) {
      if (local_cnt <= 64) {
         uint64_t a = 0, b = 0;
         for (uint32_t k = 0; k < local_cnt; k++) {
            const uint32_t pos = local_off + k;
            const uint32_t e = pos / width;
            const uint32_t bit = width - 1u - (pos % width);
            const uint64_t *ea = src_a + e * nwords;
            const uint64_t *eb = src_b != NULL ? src_b + e * nwords : NULL;
            const uint64_t ba = (ea[bit / 64u] >> (bit & 63u)) & 1u;
            const uint64_t bb = eb != NULL
               ? ((eb[bit / 64u] >> (bit & 63u)) & 1u) : 0;
            a = (a << 1) | ba;
            b = (b << 1) | bb;
         }
         rt_island_deposit_packed(m, sig, (uint32_t)s->sig_offset + local_off,
                                  local_cnt, a, b,
                                  PACK_SRC_SCALAR
                                  | (two_state ? PACK_SRC_VEC2 : 0));
      }
      else {
         const uint32_t out_nw = (local_cnt + 63u) / 64u;
         uint64_t *wa = xcalloc_array(out_nw * 2u, sizeof(uint64_t));
         uint64_t *wb = wa + out_nw;
         for (uint32_t k = 0; k < local_cnt; k++) {
            const uint32_t pos = local_off + k;
            const uint32_t e = pos / width;
            const uint32_t bit = width - 1u - (pos % width);
            const uint64_t *ea = src_a + e * nwords;
            const uint64_t *eb = src_b != NULL ? src_b + e * nwords : NULL;
            const uint64_t ba = (ea[bit / 64u] >> (bit & 63u)) & 1u;
            const uint64_t bb = eb != NULL
               ? ((eb[bit / 64u] >> (bit & 63u)) & 1u) : 0;
            const uint32_t dst_bit = local_cnt - 1u - k;
            if (ba)
               wa[dst_bit / 64u] |= UINT64_C(1) << (dst_bit & 63u);
            if (bb)
               wb[dst_bit / 64u] |= UINT64_C(1) << (dst_bit & 63u);
         }
         rt_island_deposit_packed(m, sig, (uint32_t)s->sig_offset + local_off,
                                  local_cnt, (uint64_t)(uintptr_t)wa,
                                  (uint64_t)(uintptr_t)wb,
                                  two_state ? PACK_SRC_VEC2 : 0);
         free(wa);
      }
      return;
   }

   const bool is_std = !!(sig->shared.flags & SIG_F_STD_LOGIC);
   uint8_t stack_buf[64];
   uint8_t *heap_buf = NULL;
   uint8_t *out_bytes = stack_buf;
   if (local_cnt > sizeof(stack_buf)) {
      heap_buf = xmalloc(local_cnt);
      out_bytes = heap_buf;
   }

   for (uint32_t k = 0; k < local_cnt; k++) {
      const uint32_t pos = local_off + k;
      const uint32_t e = pos / width;
      const uint32_t bit = width - 1u - (pos % width);
      const uint64_t *ea = src_a + e * nwords;
      const uint64_t *eb = src_b != NULL ? src_b + e * nwords : NULL;
      const uint8_t a = (ea[bit / 64u] >> (bit & 63u)) & 1u;
      const uint8_t b = eb != NULL
         ? ((eb[bit / 64u] >> (bit & 63u)) & 1u) : 0;
      const uint8_t sv = (uint8_t)(a | (b << 1));
      out_bytes[k] = is_std ? sv_to_std_logic(sv) : sv;
   }

   const uint32_t sig_off = (uint32_t)s->sig_offset + local_off;
   if (memcmp(sig->shared.data + sig_off, out_bytes, local_cnt) != 0)
      deposit_signal(m, sig, out_bytes, (int)sig_off, (int)local_cnt);

   free(heap_buf);
}

static void island_sync_sig_raw(rt_island_t *isl, const rt_island_sig_t *s,
                                bool update_last)
{
   rt_signal_t *sig = s->sig;
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
      uint64_t *eff_a = (uint64_t *)sig->shared.data;
      uint64_t *eff_b = sig_is4 ? eff_a + sig_nw : NULL;
      uint64_t *last_a = (uint64_t *)(sig->shared.data + sig->shared.size);
      uint64_t *last_b = sig_is4 ? last_a + sig_nw : NULL;

      if (nelems == 1 && s->sig_offset == 0 && width == sig_w) {
         for (uint32_t w = 0; w < nwords; w++) {
            const uint64_t msk = (w == nwords - 1) ? top_mask : ~UINT64_C(0);
            const uint64_t na = src_a[w] & msk;
            const uint64_t nb = (sig_is4 && src_b != NULL)
               ? (src_b[w] & msk) : 0;
            eff_a[w] = na;
            if (eff_b != NULL)
               eff_b[w] = nb;
            if (update_last) {
               last_a[w] = na;
               if (last_b != NULL)
                  last_b[w] = nb;
            }
         }
         return;
      }

      for (uint32_t e = 0; e < nelems; e++) {
         const uint64_t *ea = src_a + e * nwords;
         const uint64_t *eb = src_b != NULL ? src_b + e * nwords : NULL;
         for (uint32_t i = 0; i < width; i++) {
            const uint32_t msb_bit = (uint32_t)s->sig_offset + e * width + i;
            const uint32_t sig_bit = sig_w - 1u - msb_bit;
            const uint32_t src_bit = width - 1u - i;
            const uint64_t a = (ea[src_bit / 64u] >> (src_bit & 63u)) & 1u;
            const uint64_t b = eb != NULL
               ? ((eb[src_bit / 64u] >> (src_bit & 63u)) & 1u) : 0;
            const uint64_t bit = UINT64_C(1) << (sig_bit & 63u);
            const uint32_t widx = sig_bit / 64u;
            eff_a[widx] = (eff_a[widx] & ~bit) | (a ? bit : 0);
            if (eff_b != NULL)
               eff_b[widx] = (eff_b[widx] & ~bit) | (b ? bit : 0);
            if (update_last) {
               last_a[widx] = (last_a[widx] & ~bit) | (a ? bit : 0);
               if (last_b != NULL)
                  last_b[widx] = (last_b[widx] & ~bit) | (b ? bit : 0);
            }
         }
      }
      return;
   }

   const bool is_std = !!(sig->shared.flags & SIG_F_STD_LOGIC);
   uint8_t *dst = sig->shared.data + s->sig_offset;
   uint8_t *last_dst = sig->shared.data + sig->shared.size + s->sig_offset;

   for (uint32_t e = 0; e < nelems; e++) {
      const uint64_t *ea = src_a + e * nwords;
      const uint64_t *eb = src_b != NULL ? src_b + e * nwords : NULL;
      for (uint32_t i = 0; i < width; i++) {
         const uint32_t bit = width - 1u - i;
         const uint8_t a = (ea[bit / 64u] >> (bit & 63u)) & 1u;
         const uint8_t b = (eb != NULL)
            ? ((eb[bit / 64u] >> (bit & 63u)) & 1u) : 0;
         const uint8_t sv = (uint8_t)(a | (b << 1));
         dst[e * width + i] = is_std ? sv_to_std_logic(sv) : sv;
      }
   }

   if (update_last)
      memcpy(last_dst, dst, width * nelems);
}

static void island_sync_sig_raw_slice(rt_island_t *isl,
                                      const rt_island_sig_t *s,
                                      uint32_t local_off, uint32_t local_cnt,
                                      bool update_last)
{
   if (local_cnt == 0)
      return;
   if (local_off == 0 && local_cnt >= s->width * s->nelems) {
      island_sync_sig_raw(isl, s, update_last);
      return;
   }

   rt_signal_t *sig = s->sig;
   const uint32_t width = s->width;
   const uint32_t nwords = s->nwords;
   const bool two_state = !!(s->flags & RT_ISIG_TWO_STATE);
   const uint64_t *src_a = (const uint64_t *)(isl->state + s->a_off);
   const uint64_t *src_b = two_state
      ? NULL : (const uint64_t *)(isl->state + s->b_off);

   if (sig->shared.flags & SIG_F_PACKED) {
      const uint32_t sig_w = sig->bit_width;
      const uint32_t sig_nw = (sig_w + 63u) / 64u;
      const bool sig_is4 = !!(sig->shared.flags & SIG_F_PACKED4);
      uint64_t *eff_a = (uint64_t *)sig->shared.data;
      uint64_t *eff_b = sig_is4 ? eff_a + sig_nw : NULL;
      uint64_t *last_a = (uint64_t *)(sig->shared.data + sig->shared.size);
      uint64_t *last_b = sig_is4 ? last_a + sig_nw : NULL;

      for (uint32_t k = 0; k < local_cnt; k++) {
         const uint32_t pos = local_off + k;
         const uint32_t e = pos / width;
         const uint32_t src_bit = width - 1u - (pos % width);
         const uint32_t msb_bit = (uint32_t)s->sig_offset + pos;
         const uint32_t sig_bit = sig_w - 1u - msb_bit;
         const uint64_t *ea = src_a + e * nwords;
         const uint64_t *eb = src_b != NULL ? src_b + e * nwords : NULL;
         const uint64_t a = (ea[src_bit / 64u] >> (src_bit & 63u)) & 1u;
         const uint64_t b = eb != NULL
            ? ((eb[src_bit / 64u] >> (src_bit & 63u)) & 1u) : 0;
         const uint64_t bit = UINT64_C(1) << (sig_bit & 63u);
         const uint32_t widx = sig_bit / 64u;
         eff_a[widx] = (eff_a[widx] & ~bit) | (a ? bit : 0);
         if (eff_b != NULL)
            eff_b[widx] = (eff_b[widx] & ~bit) | (b ? bit : 0);
         if (update_last) {
            last_a[widx] = (last_a[widx] & ~bit) | (a ? bit : 0);
            if (last_b != NULL)
               last_b[widx] = (last_b[widx] & ~bit) | (b ? bit : 0);
         }
      }
      return;
   }

   const bool is_std = !!(sig->shared.flags & SIG_F_STD_LOGIC);
   uint8_t *dst = sig->shared.data + s->sig_offset;
   uint8_t *last_dst = sig->shared.data + sig->shared.size + s->sig_offset;

   for (uint32_t k = 0; k < local_cnt; k++) {
      const uint32_t pos = local_off + k;
      const uint32_t e = pos / width;
      const uint32_t bit = width - 1u - (pos % width);
      const uint64_t *ea = src_a + e * nwords;
      const uint64_t *eb = src_b != NULL ? src_b + e * nwords : NULL;
      const uint8_t a = (ea[bit / 64u] >> (bit & 63u)) & 1u;
      const uint8_t b = eb != NULL
         ? ((eb[bit / 64u] >> (bit & 63u)) & 1u) : 0;
      const uint8_t sv = (uint8_t)(a | (b << 1));
      dst[pos] = is_std ? sv_to_std_logic(sv) : sv;
   }

   if (update_last)
      memcpy(last_dst + local_off, dst + local_off, local_cnt);
}

static inline const rt_island_force_t *island_find_force(const rt_island_t *isl,
                                                         uint32_t sig_idx)
{
   for (uint32_t i = 0; i < isl->n_forces; i++) {
      if (isl->forces[i].sig_idx == sig_idx)
         return &(isl->forces[i]);
   }
   return NULL;
}

static rt_island_force_t *island_get_or_add_force(rt_island_t *isl,
                                                  const rt_island_sig_t *s)
{
   for (uint32_t i = 0; i < isl->n_forces; i++) {
      if (isl->forces[i].sig_idx == s->sig_idx)
         return &(isl->forces[i]);
   }

   if (isl->n_forces == isl->cap_forces) {
      isl->cap_forces = MAX(isl->cap_forces * 2u, 4u);
      isl->forces = xrealloc_array(isl->forces, isl->cap_forces,
                                   sizeof(rt_island_force_t));
   }

   const uint32_t total_words = s->nwords * s->nelems;
   uint64_t *mem = xcalloc_array(total_words * 3u, sizeof(uint64_t));
   rt_island_force_t *f = &(isl->forces[isl->n_forces++]);
   f->sig_idx = s->sig_idx;
   f->mask = mem;
   f->a = mem + total_words;
   f->b = mem + total_words * 2u;
   isl->has_forces = true;
   return f;
}

static void island_apply_force_entry(uint8_t *base, const rt_island_sig_t *s,
                                     const rt_island_force_t *f)
{
   const uint32_t total_words = s->nwords * s->nelems;
   uint64_t *ap = (uint64_t *)(base + s->a_off);
   for (uint32_t w = 0; w < total_words; w++) {
      const uint64_t m = f->mask[w];
      if (m)
         ap[w] = (ap[w] & ~m) | (f->a[w] & m);
   }
   if (!(s->flags & RT_ISIG_TWO_STATE)) {
      uint64_t *bp = (uint64_t *)(base + s->b_off);
      for (uint32_t w = 0; w < total_words; w++) {
         const uint64_t m = f->mask[w];
         if (m)
            bp[w] = (bp[w] & ~m) | (f->b[w] & m);
      }
   }
}

static void island_apply_forces_comb(rt_island_t *isl)
{
   for (uint32_t i = 0; i < isl->n_forces; i++) {
      const rt_island_force_t *f = &(isl->forces[i]);
      const rt_island_sig_t *s = &(isl->sigs[f->sig_idx]);
      island_apply_force_entry(isl->state, s, f);
   }
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
      for (uint32_t f = 0; f < isl->n_forces; f++)
         free(isl->forces[f].mask);
      isl->n_forces = 0;
      isl->has_forces = false;

      island_init_state_defaults(isl);
      for (uint32_t j = 0; j < isl->nsigs; j++) {
         if (isl->sigs[j].sig != NULL)
            island_sync_in_sig(isl, &(isl->sigs[j]));
      }
      if (isl->next_state != NULL)
         memcpy(isl->next_state, isl->state, isl->state_bytes);

      island_clear_in_dirty(isl);

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

      island_sync_dirty_inputs(isl);

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

      if (unlikely(isl->has_forces))
         island_apply_forces_comb(isl);

      for (uint32_t j = 0; j < isl->n_out_sigs; j++)
         island_sync_out_sig(m, isl, &(isl->sigs[isl->out_sigs[j]]));
      if (unlikely(isl->n_obs_sigs > 0)) {
         for (uint32_t j = 0; j < isl->n_obs_sigs; j++)
            island_sync_out_sig(m, isl, &(isl->sigs[isl->obs_sigs[j]]));
      }
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
      island_sync_dirty_inputs(isl);

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
         if (unlikely(isl->has_forces)) {
            const rt_island_force_t *f = island_find_force(isl, s->sig_idx);
            if (f != NULL)
               island_apply_force_entry(isl->next_state, s, f);
         }

         if (!island_commit_sig_buf(isl, s))
            continue;

         if (s->flags & RT_ISIG_COMB_READ)
            comb_need_eval = true;

         if (s->flags & (RT_ISIG_OUT | RT_ISIG_OBSERVED))
            island_sync_out_sig(m, isl, s);
      }

      if (comb_need_eval && isl->eval_handle != JIT_HANDLE_INVALID)
         island_mark_dirty(isl);
   }

   mgr->ff_commit_q.count = 0;
}

bool rt_island_read_sig(rt_island_mgr_t *mgr, rt_signal_t *sig,
                        uint64_t *a_out, uint64_t *b_out,
                        uint32_t *width_out, bool *two_state_out)
{
   if (mgr == NULL || mgr->sig_map == NULL || sig == NULL)
      return false;

   const rt_island_sig_t *s = hash_get(mgr->sig_map, sig);
   if (s == NULL)
      return false;

   rt_island_t *isl = s->isl;
   const uint32_t nwords = s->nwords;
   const uint32_t nelems = s->nelems;
   const bool two_state = !!(s->flags & RT_ISIG_TWO_STATE);
   const uint64_t *src_a = (const uint64_t *)(isl->state + s->a_off);
   const uint64_t *src_b = two_state
      ? NULL : (const uint64_t *)(isl->state + s->b_off);
   const uint64_t top_mask = island_top_mask(s->width);

   for (uint32_t e = 0; e < nelems; e++) {
      for (uint32_t w = 0; w < nwords; w++) {
         const uint64_t msk = (w == nwords - 1) ? top_mask : ~UINT64_C(0);
         const uint32_t idx = e * nwords + w;
         if (a_out != NULL)
            a_out[idx] = src_a[idx] & msk;
         if (b_out != NULL)
            b_out[idx] = src_b != NULL ? (src_b[idx] & msk) : 0;
      }
   }

   if (width_out != NULL)
      *width_out = s->width;
   if (two_state_out != NULL)
      *two_state_out = two_state;
   return true;
}

void rt_island_sync_to_signal(rt_model_t *m, rt_signal_t *sig)
{
   rt_island_mgr_t *mgr = rt_island_model_mgr(m);
   if (mgr == NULL || mgr->sig_map == NULL || sig == NULL)
      return;

   rt_island_sig_t *s = hash_get(mgr->sig_map, sig);
   for (; s != NULL; s = s->next_owner)
      island_sync_sig_raw(s->isl, s, false);
}

void rt_island_sync_slice_to_signal(rt_model_t *m, rt_signal_t *sig,
                                    uint32_t offset, uint32_t count)
{
   if (count == 0)
      return;

   rt_island_mgr_t *mgr = rt_island_model_mgr(m);
   if (mgr == NULL || mgr->sig_map == NULL || sig == NULL)
      return;

   rt_island_sig_t *s = hash_get(mgr->sig_map, sig);
   for (; s != NULL; s = s->next_owner) {
      const uint32_t s_start = (uint32_t)s->sig_offset;
      const uint32_t s_end = s_start + s->width * s->nelems;
      const uint32_t r_start = MAX(offset, s_start);
      const uint32_t r_end = MIN(offset + count, s_end);
      if (r_start < r_end)
         island_sync_sig_raw_slice(s->isl, s, r_start - s_start,
                                   r_end - r_start, false);
   }
}

void rt_island_on_observer_attached(rt_model_t *m, rt_signal_t *sig)
{
   rt_island_mgr_t *mgr = rt_island_model_mgr(m);
   if (mgr == NULL || mgr->sig_map == NULL || sig == NULL)
      return;

   sig->shared.flags &= ~(SIG_F_FAST_NOOP | SIG_F_FAST_DIRECT);

   rt_island_sig_t *s = hash_get(mgr->sig_map, sig);
   for (; s != NULL; s = s->next_owner) {
      if (!(s->flags & RT_ISIG_OBSERVED)) {
         if (!(s->flags & RT_ISIG_OUT))
            island_sync_sig_raw(s->isl, s, true);
         s->flags |= RT_ISIG_OBSERVED;
         if ((s->flags & RT_ISIG_COMB_WRITTEN) && !(s->flags & RT_ISIG_OUT))
            island_add_obs_sig(s->isl, s->sig_idx);
      }
   }
}

static void island_eval_comb_now(rt_model_t *m, rt_island_t *isl)
{
   for (uint32_t j = 0; j < isl->n_in_sigs; j++)
      island_sync_in_sig(isl, &(isl->sigs[isl->in_sigs[j]]));
   island_clear_in_dirty(isl);

   if (isl->eval_handle != JIT_HANDLE_INVALID) {
      jit_t *jit = rt_island_model_jit(m);
      jit_scalar_t p1 = { .pointer = isl->inst_ctx };
      jit_scalar_t p2 = { .pointer = isl->state };
      jit_scalar_t result = { .integer = 0 };
      tlab_t null_tlab = jit_null_tlab(jit);
      jit_fastcall(jit, isl->eval_handle, &result, p1, p2, &null_tlab);

      if (unlikely(isl->has_forces))
         island_apply_forces_comb(isl);
   }

   for (uint32_t j = 0; j < isl->n_out_sigs; j++)
      island_sync_out_sig(m, isl, &(isl->sigs[isl->out_sigs[j]]));
   if (unlikely(isl->n_obs_sigs > 0)) {
      for (uint32_t j = 0; j < isl->n_obs_sigs; j++)
         island_sync_out_sig(m, isl, &(isl->sigs[isl->obs_sigs[j]]));
   }

   isl->dirty = false;
}

static void island_write_bits_slice(rt_island_t *isl, rt_island_sig_t *s,
                                    uint32_t local_off, uint32_t local_cnt,
                                    const uint8_t *bytes,
                                    const uint64_t *a_words,
                                    const uint64_t *b_words,
                                    bool is_packed_src, uint32_t total_src_w,
                                    bool is_force)
{
   const bool two_state = !!(s->flags & RT_ISIG_TWO_STATE);
   uint64_t *ap = (uint64_t *)(isl->state + s->a_off);
   uint64_t *bp = two_state ? NULL : (uint64_t *)(isl->state + s->b_off);
   uint64_t *nap = isl->next_state != NULL
      ? (uint64_t *)(isl->next_state + s->a_off) : NULL;
   uint64_t *nbp = (nap != NULL && !two_state)
      ? (uint64_t *)(isl->next_state + s->b_off) : NULL;

   rt_island_force_t *f = NULL;
   if (is_force)
      f = island_get_or_add_force(isl, s);
   else if (unlikely(isl->has_forces))
      f = (rt_island_force_t *)island_find_force(isl, s->sig_idx);

   for (uint32_t k = 0; k < local_cnt; k++) {
      const uint32_t pos = local_off + k;
      const uint32_t e = pos / s->width;
      const uint32_t bit_msb = pos % s->width;
      const uint32_t lsb = s->width - 1u - bit_msb;
      const uint32_t widx = e * s->nwords + (lsb / 64u);
      const uint64_t bit = UINT64_C(1) << (lsb & 63u);

      uint64_t a = 0, b = 0;
      if (is_packed_src) {
         const uint32_t src_bit = total_src_w - 1u - k;
         a = (a_words[src_bit / 64u] >> (src_bit & 63u)) & 1u;
         b = b_words != NULL
            ? ((b_words[src_bit / 64u] >> (src_bit & 63u)) & 1u) : 0;
      }
      else if (bytes != NULL) {
         const uint8_t v = bytes[k];
         a = v & 1u;
         b = (v >> 1) & 1u;
      }

      if (two_state) {
         a &= ~b;
         b = 0;
      }

      if (is_force) {
         f->mask[widx] |= bit;
         f->a[widx] = (f->a[widx] & ~bit) | (a ? bit : 0);
         f->b[widx] = (f->b[widx] & ~bit) | (b ? bit : 0);
         ap[widx] = (ap[widx] & ~bit) | (a ? bit : 0);
         if (bp != NULL)
            bp[widx] = (bp[widx] & ~bit) | (b ? bit : 0);
         if (nap != NULL)
            nap[widx] = (nap[widx] & ~bit) | (a ? bit : 0);
         if (nbp != NULL)
            nbp[widx] = (nbp[widx] & ~bit) | (b ? bit : 0);
      }
      else {
         if (f != NULL && (f->mask[widx] & bit))
            continue;
         ap[widx] = (ap[widx] & ~bit) | (a ? bit : 0);
         if (bp != NULL)
            bp[widx] = (bp[widx] & ~bit) | (b ? bit : 0);
      }
   }
}

bool rt_island_vpi_put(rt_model_t *m, rt_signal_t *sig,
                       const uint64_t *a_val, const uint64_t *b_val,
                       uint32_t width, bool is_force)
{
   rt_island_mgr_t *mgr = rt_island_model_mgr(m);
   if (mgr == NULL || mgr->sig_map == NULL || sig == NULL)
      return false;

   rt_island_sig_t *s = hash_get(mgr->sig_map, sig);
   if (s == NULL)
      return false;

   for (; s != NULL; s = s->next_owner) {
      const uint32_t total = s->width * s->nelems;
      const uint32_t cnt = MIN(width, total);
      island_write_bits_slice(s->isl, s, 0, cnt, NULL, a_val, b_val,
                              true, width, is_force);
      island_sync_out_slice(m, s->isl, s, 0, cnt);

      if (is_force || !(s->flags & RT_ISIG_COMB_WRITTEN)) {
         if ((s->flags & RT_ISIG_COMB_READ)
             && s->isl->eval_handle != JIT_HANDLE_INVALID) {
            island_mark_dirty(s->isl);
            rt_island_request_delta(m);
         }
      }
   }
   return true;
}

bool rt_island_put_bytes(rt_model_t *m, rt_signal_t *sig,
                         const uint8_t *bytes, uint32_t offset, uint32_t count,
                         bool is_force)
{
   rt_island_mgr_t *mgr = rt_island_model_mgr(m);
   if (mgr == NULL || mgr->sig_map == NULL || sig == NULL)
      return false;

   rt_island_sig_t *s = hash_get(mgr->sig_map, sig);
   if (s == NULL)
      return false;

   for (; s != NULL; s = s->next_owner) {
      const uint32_t s_start = (uint32_t)s->sig_offset;
      const uint32_t s_end = s_start + s->width * s->nelems;
      const uint32_t r_start = MAX(offset, s_start);
      const uint32_t r_end = MIN(offset + count, s_end);
      if (r_start >= r_end)
         continue;

      island_write_bits_slice(s->isl, s, r_start - s_start, r_end - r_start,
                              bytes + (r_start - offset), NULL, NULL,
                              false, 0, is_force);
      island_sync_out_slice(m, s->isl, s, r_start - s_start, r_end - r_start);

      if (is_force || !(s->flags & RT_ISIG_COMB_WRITTEN)) {
         if ((s->flags & RT_ISIG_COMB_READ)
             && s->isl->eval_handle != JIT_HANDLE_INVALID) {
            island_mark_dirty(s->isl);
            rt_island_request_delta(m);
         }
      }
   }
   return true;
}

bool rt_island_put_packed(rt_model_t *m, rt_signal_t *sig,
                          uint32_t msb_off, uint32_t count,
                          uint64_t a_val, uint64_t b_val, uint32_t src_flags,
                          bool is_force)
{
   rt_island_mgr_t *mgr = rt_island_model_mgr(m);
   if (mgr == NULL || mgr->sig_map == NULL || sig == NULL)
      return false;

   rt_island_sig_t *s = hash_get(mgr->sig_map, sig);
   if (s == NULL)
      return false;

   const uint64_t *src_a = (src_flags & PACK_SRC_SCALAR)
      ? &a_val : (const uint64_t *)(uintptr_t)a_val;
   const uint64_t *src_b = (src_flags & PACK_SRC_VEC2) ? NULL
      : ((src_flags & PACK_SRC_SCALAR)
         ? &b_val : (const uint64_t *)(uintptr_t)b_val);

   for (; s != NULL; s = s->next_owner) {
      const uint32_t s_start = (uint32_t)s->sig_offset;
      const uint32_t s_end = s_start + s->width * s->nelems;
      const uint32_t r_start = MAX(msb_off, s_start);
      const uint32_t r_end = MIN(msb_off + count, s_end);
      if (r_start >= r_end)
         continue;

      // For exact alignment with r_start == msb_off:
      const uint32_t skip = r_start - msb_off;
      if (skip == 0) {
         island_write_bits_slice(s->isl, s, r_start - s_start, r_end - r_start,
                                 NULL, src_a, src_b, true, count, is_force);
      }
      else {
         uint8_t stack_b[64];
         uint8_t *heap_b = NULL;
         const uint32_t sub_cnt = r_end - r_start;
         uint8_t *buf = sub_cnt <= sizeof(stack_b)
            ? stack_b : (heap_b = xmalloc(sub_cnt));
         for (uint32_t k = 0; k < sub_cnt; k++) {
            const uint32_t src_bit = count - 1u - (skip + k);
            const uint64_t a = (src_a[src_bit / 64u] >> (src_bit & 63u)) & 1u;
            const uint64_t b = src_b != NULL
               ? ((src_b[src_bit / 64u] >> (src_bit & 63u)) & 1u) : 0;
            buf[k] = (uint8_t)(a | (b << 1));
         }
         island_write_bits_slice(s->isl, s, r_start - s_start, sub_cnt,
                                 buf, NULL, NULL, false, 0, is_force);
         free(heap_b);
      }
      island_sync_out_slice(m, s->isl, s, r_start - s_start, r_end - r_start);

      if (is_force || !(s->flags & RT_ISIG_COMB_WRITTEN)) {
         if ((s->flags & RT_ISIG_COMB_READ)
             && s->isl->eval_handle != JIT_HANDLE_INVALID) {
            island_mark_dirty(s->isl);
            rt_island_request_delta(m);
         }
      }
   }
   return true;
}

void rt_island_vpi_release_slice(rt_model_t *m, rt_signal_t *sig,
                                 uint32_t offset, uint32_t count)
{
   rt_island_mgr_t *mgr = rt_island_model_mgr(m);
   if (mgr == NULL || mgr->sig_map == NULL || sig == NULL)
      return;

   rt_island_sig_t *s = hash_get(mgr->sig_map, sig);
   for (; s != NULL; s = s->next_owner) {
      rt_island_t *isl = s->isl;
      if (!isl->has_forces)
         continue;

      const uint32_t s_start = (uint32_t)s->sig_offset;
      const uint32_t s_end = s_start + s->width * s->nelems;
      const uint32_t r_start = MAX(offset, s_start);
      const uint32_t r_end = MIN(offset + count, s_end);
      if (r_start >= r_end)
         continue;

      int fidx = -1;
      for (uint32_t i = 0; i < isl->n_forces; i++) {
         if (isl->forces[i].sig_idx == s->sig_idx) {
            fidx = (int)i;
            break;
         }
      }
      if (fidx < 0)
         continue;

      rt_island_force_t *f = &(isl->forces[fidx]);
      for (uint32_t k = r_start - s_start; k < r_end - s_start; k++) {
         const uint32_t e = k / s->width;
         const uint32_t lsb = s->width - 1u - (k % s->width);
         const uint32_t widx = e * s->nwords + (lsb / 64u);
         const uint64_t bit = UINT64_C(1) << (lsb & 63u);
         f->mask[widx] &= ~bit;
      }

      const uint32_t total_words = s->nwords * s->nelems;
      bool any = false;
      for (uint32_t w = 0; w < total_words && !any; w++)
         any = (f->mask[w] != 0);

      if (!any) {
         free(f->mask);
         if ((uint32_t)fidx + 1u < isl->n_forces)
            isl->forces[fidx] = isl->forces[isl->n_forces - 1u];
         isl->n_forces--;
         isl->has_forces = (isl->n_forces > 0);
      }

      if ((s->flags & RT_ISIG_COMB_WRITTEN)
          && isl->eval_handle != JIT_HANDLE_INVALID) {
         island_eval_comb_now(m, isl);
         if (!(s->flags & (RT_ISIG_OUT | RT_ISIG_OBSERVED)))
            island_sync_sig_raw(isl, s, true);
      }
      else if ((s->flags & RT_ISIG_COMB_READ)
               && isl->eval_handle != JIT_HANDLE_INVALID) {
         island_mark_dirty(isl);
         rt_island_request_delta(m);
      }
   }
}

void rt_island_vpi_release(rt_model_t *m, rt_signal_t *sig)
{
   if (sig == NULL)
      return;
   rt_island_vpi_release_slice(m, sig, 0, signal_width(sig));
}
