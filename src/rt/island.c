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
#include "option.h"
#include "rt/island.h"
#include "rt/model.h"
#include "rt/structs.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct _rt_super_island {
   uint32_t      id;
   rt_island_t **islands;
   uint32_t      n_islands;
   uint32_t      min_dirty_pos;
   uint32_t      dirty_count;
   bool          has_dirty;
   bool          has_dirty_inputs;
};

struct _rt_super_domain {
   rt_island_mgr_t     *mgr;
   rt_signal_t         *clk_sig;
   int32_t              clk_sig_offset;
   rt_signal_t         *rst_sig;
   int32_t              rst_sig_offset;
   bool                 clk_posedge;
   bool                 rst_posedge;
   uint8_t              edge_flags;
   uint8_t              prev_clk;
   uint8_t              prev_rst;
   bool                 in_sample_q;
   bool                 in_commit_q;
   rt_watch_t          *clk_watch;
   rt_watch_t          *rst_watch;
   rt_island_domain_t **doms;
   uint32_t             n_doms;
   rt_super_island_t  **clusters;
   uint32_t             n_clusters;
   rt_island_t        **solo_islands;
   uint32_t             n_solo_islands;
};

struct _rt_island_mgr {
   rt_model_t             *model;
   hash_t                 *sig_map;
   A(rt_island_t *)        islands;
   A(rt_island_t *)        dirty_q;
   A(rt_island_domain_t *) ff_sample_q;
   A(rt_island_domain_t *) ff_commit_q;
   A(rt_super_island_t *)  clusters;
   A(rt_super_island_t *)  dirty_clusters;
   A(rt_super_domain_t *)  super_domains;
   A(rt_super_domain_t *)  super_sample_q;
   A(rt_super_domain_t *)  super_commit_q;
   uint32_t                eval_epoch;
   bool                    hier_built;
};

static inline bool island_is_hier_mode(void)
{
   return opt_get_int(OPT_RTL_ISLANDS) == RTL_ISLANDS_HIER;
}

static inline uint64_t island_top_mask(uint32_t width)
{
   return (width & 63u) ? ((UINT64_C(1) << (width & 63u)) - 1u) : ~UINT64_C(0);
}

static inline void island_mark_dirty(rt_island_t *isl)
{
   if (isl->dirty)
      return;

   isl->dirty = true;
   if (isl->cluster != NULL) {
      rt_super_island_t *cl = isl->cluster;
      cl->dirty_count++;
      if (isl->topo_pos < cl->min_dirty_pos)
         cl->min_dirty_pos = isl->topo_pos;
      if (!cl->has_dirty) {
         cl->has_dirty = true;
         APUSH(isl->mgr->dirty_clusters, cl);
      }
   }
   else if (!isl->in_dirty_list) {
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

static inline uint8_t island_read_last_sv_bit(const rt_signal_t *sig,
                                              int32_t sig_offset)
{
   const uint8_t *last_base = sig->shared.data + sig->shared.size;
   if (sig->shared.flags & SIG_F_PACKED) {
      const uint32_t sig_w = sig->bit_width;
      const uint32_t sig_nw = (sig_w + 63u) / 64u;
      const bool sig_is4 = !!(sig->shared.flags & SIG_F_PACKED4);
      const uint64_t *src_a = (const uint64_t *)last_base;
      const uint64_t *src_b = sig_is4 ? src_a + sig_nw : NULL;
      const uint32_t sig_bit = sig_w - 1u - (uint32_t)sig_offset;
      const uint64_t a = (src_a[sig_bit / 64u] >> (sig_bit & 63u)) & 1u;
      const uint64_t b = src_b != NULL
         ? ((src_b[sig_bit / 64u] >> (sig_bit & 63u)) & 1u) : 0;
      return (uint8_t)((b << 1) | a);
   }

   const uint8_t raw = last_base[sig_offset];
   if (sig->shared.flags & SIG_F_STD_LOGIC)
      return std_logic_to_sv(raw);
   return raw & 3u;
}

static inline uint8_t island_initial_prev_sv_bit(const rt_model_t *m,
                                                 const rt_signal_t *sig,
                                                 int32_t sig_offset)
{
   if (sig == NULL)
      return 3u;
   if (rt_model_has_init_event(m, sig))
      return island_read_last_sv_bit(sig, sig_offset);
   return island_read_sv_bit(sig, sig_offset);
}

static inline bool island_edge_match(uint8_t old_v, uint8_t new_v, bool posedge)
{
   if (posedge)
      return (old_v == 0 && new_v != 0) || (old_v != 1 && new_v == 1);
   else
      return (old_v == 1 && new_v != 1) || (old_v != 0 && new_v == 0);
}

static inline bool island_sig_is_std(const rt_signal_t *sig, uint32_t sig_flags)
{
   return !!(sig->shared.flags & SIG_F_STD_LOGIC)
      || !!(sig_flags & RT_ISIG_STD_LOGIC_ELEM);
}

static inline uint8_t island_read_raw_bit(const rt_signal_t *sig,
                                          int32_t sig_offset)
{
   return sig->shared.data[sig_offset];
}

static inline uint8_t island_read_last_raw_bit(const rt_signal_t *sig,
                                               int32_t sig_offset)
{
   return sig->shared.data[sig->shared.size + sig_offset];
}

static inline uint8_t island_initial_prev_domain_bit(const rt_model_t *m,
                                                     const rt_signal_t *sig,
                                                     int32_t sig_offset,
                                                     uint8_t edge_flags)
{
   if (!(edge_flags & (RT_IEDGE_VHDL | RT_IEDGE_VHDL_EVENT
                       | RT_IEDGE_RST_LEVEL)))
      return island_initial_prev_sv_bit(m, sig, sig_offset);
   if (sig == NULL)
      return 0u;
   if (rt_model_has_init_event(m, sig))
      return island_read_last_raw_bit(sig, sig_offset);
   return island_read_raw_bit(sig, sig_offset);
}

static inline bool island_vhdl_clk_match(uint8_t old_v, uint8_t new_v,
                                         uint8_t edge_flags, bool is_std)
{
   const bool posedge = !!(edge_flags & RT_IEDGE_CLK_POSEDGE);
   if (edge_flags & RT_IEDGE_VHDL) {
      if (is_std) {
         if (posedge)
            return (old_v == 2u || old_v == 6u) && (new_v == 3u || new_v == 7u);
         else
            return (old_v == 3u || old_v == 7u) && (new_v == 2u || new_v == 6u);
      }
      else {
         if (posedge)
            return (old_v & 1u) == 0u && (new_v & 1u) == 1u;
         else
            return (old_v & 1u) == 1u && (new_v & 1u) == 0u;
      }
   }
   else {
      const uint8_t target = is_std ? (posedge ? 3u : 2u) : (posedge ? 1u : 0u);
      return old_v != new_v && new_v == target;
   }
}

static inline bool island_vhdl_rst_match(uint8_t cur, bool rst_posedge,
                                         bool is_std, bool two_state)
{
   if (is_std) {
      if (two_state) {
         const uint8_t sv = std_logic_to_sv(cur);
         const uint8_t bit = (sv == 1u) ? 1u : 0u;
         return rst_posedge ? (bit == 1u) : (bit == 0u);
      }
      const uint8_t target = rst_posedge ? 3u : 2u;
      return cur == target;
   }
   return rst_posedge ? ((cur & 1u) == 1u) : ((cur & 1u) == 0u);
}

__attribute__((noinline))
static void island_input_watch_cb(uint64_t now, rt_signal_t *signal,
                                  rt_watch_t *watch, void *user);

static inline bool island_wakeable_is_input_watch(const rt_wakeable_t *w)
{
   if (w == NULL || w->kind != W_WATCH)
      return false;
   const rt_watch_t *watch = container_of(w, rt_watch_t, wakeable);
   return watch->fn == island_input_watch_cb;
}

static inline bool island_pending_has_ext_observer(void *pending)
{
   if (pending == NULL)
      return false;
   if (pointer_tag(pending) == 1) {
      const rt_wakeable_t *w = untag_pointer(pending, rt_wakeable_t);
      return !island_wakeable_is_input_watch(w);
   }
   const rt_pending_t *p = untag_pointer(pending, rt_pending_t);
   for (unsigned j = 0; j < p->count; j++) {
      if (p->wake[j] != NULL && !island_wakeable_is_input_watch(p->wake[j]))
         return true;
   }
   return false;
}

static inline bool island_sig_observed(const rt_signal_t *sig)
{
   if (sig->shared.flags & (SIG_F_POLL_WATCHED | SIG_F_EXT_READ))
      return true;
   const rt_nexus_t *nx = &(sig->nexus);
   for (uint32_t i = 0; i < sig->n_nexus; i++, nx = nx->chain) {
      if (island_pending_has_ext_observer(nx->pending))
         return true;
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
   isl->has_dirty_inputs = true;
   if (isl->cluster != NULL)
      isl->cluster->has_dirty_inputs = true;
   if (s->flags & RT_ISIG_COMB_READ)
      island_mark_dirty(isl);
}

static void island_super_clk_watch_cb(uint64_t now, rt_signal_t *signal,
                                      rt_watch_t *watch, void *user)
{
   rt_super_domain_t *sdom = user;
   const bool is_vhdl = !!(sdom->edge_flags
                           & (RT_IEDGE_VHDL | RT_IEDGE_VHDL_EVENT));
   bool hit;
   if (is_vhdl) {
      const uint8_t cur = island_read_raw_bit(sdom->clk_sig,
                                              sdom->clk_sig_offset);
      const rt_island_domain_t *d0 = sdom->doms[0];
      const rt_island_sig_t *cs0 = &(d0->isl->sigs[d0->clk_sig_idx]);
      const bool is_std = island_sig_is_std(sdom->clk_sig, cs0->flags);
      hit = island_vhdl_clk_match(sdom->prev_clk, cur, sdom->edge_flags,
                                  is_std);
      sdom->prev_clk = cur;
   }
   else {
      const uint8_t cur = island_read_sv_bit(sdom->clk_sig,
                                             sdom->clk_sig_offset);
      hit = island_edge_match(sdom->prev_clk, cur, sdom->clk_posedge);
      sdom->prev_clk = cur;
   }
   if (hit && !sdom->in_sample_q) {
      sdom->in_sample_q = true;
      APUSH(sdom->mgr->super_sample_q, sdom);
   }
}

static void island_super_rst_watch_cb(uint64_t now, rt_signal_t *signal,
                                      rt_watch_t *watch, void *user)
{
   rt_super_domain_t *sdom = user;
   bool hit;
   if (sdom->edge_flags & RT_IEDGE_RST_LEVEL) {
      const uint8_t cur = island_read_raw_bit(sdom->rst_sig,
                                              sdom->rst_sig_offset);
      const rt_island_domain_t *d0 = sdom->doms[0];
      const rt_island_sig_t *rs0 = &(d0->isl->sigs[d0->rst_sig_idx]);
      const bool is_std = island_sig_is_std(sdom->rst_sig, rs0->flags);
      const bool two_state = !!(rs0->flags & RT_ISIG_TWO_STATE);
      hit = island_vhdl_rst_match(cur, sdom->rst_posedge, is_std, two_state);
      sdom->prev_rst = cur;
   }
   else {
      const uint8_t cur = island_read_sv_bit(sdom->rst_sig,
                                             sdom->rst_sig_offset);
      hit = island_edge_match(sdom->prev_rst, cur, sdom->rst_posedge);
      sdom->prev_rst = cur;
   }
   if (hit && !sdom->in_sample_q) {
      sdom->in_sample_q = true;
      APUSH(sdom->mgr->super_sample_q, sdom);
   }
}

static void island_clk_watch_cb(uint64_t now, rt_signal_t *signal,
                                rt_watch_t *watch, void *user)
{
   rt_island_domain_t *dom = user;
   const rt_island_sig_t *cs = &(dom->isl->sigs[dom->clk_sig_idx]);
   const bool is_vhdl = !!(dom->edge_flags
                           & (RT_IEDGE_VHDL | RT_IEDGE_VHDL_EVENT));
   bool hit;
   if (is_vhdl) {
      const uint8_t cur = island_read_raw_bit(cs->sig, cs->sig_offset);
      const bool is_std = island_sig_is_std(cs->sig, cs->flags);
      hit = island_vhdl_clk_match(dom->prev_clk, cur, dom->edge_flags, is_std);
      dom->prev_clk = cur;
   }
   else {
      const uint8_t cur = island_read_sv_bit(cs->sig, cs->sig_offset);
      hit = island_edge_match(dom->prev_clk, cur, dom->clk_posedge);
      dom->prev_clk = cur;
   }
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
   bool hit;
   if (dom->edge_flags & RT_IEDGE_RST_LEVEL) {
      const uint8_t cur = island_read_raw_bit(rs->sig, rs->sig_offset);
      const bool is_std = island_sig_is_std(rs->sig, rs->flags);
      const bool two_state = !!(rs->flags & RT_ISIG_TWO_STATE);
      hit = island_vhdl_rst_match(cur, dom->rst_posedge, is_std, two_state);
      dom->prev_rst = cur;
   }
   else {
      const uint8_t cur = island_read_sv_bit(rs->sig, rs->sig_offset);
      hit = island_edge_match(dom->prev_rst, cur, dom->rst_posedge);
      dom->prev_rst = cur;
   }
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

   for (unsigned i = 0; i < mgr->clusters.count; i++) {
      rt_super_island_t *cl = mgr->clusters.items[i];
      free(cl->islands);
      free(cl);
   }
   for (unsigned i = 0; i < mgr->super_domains.count; i++) {
      rt_super_domain_t *sd = mgr->super_domains.items[i];
      free(sd->doms);
      free(sd->clusters);
      free(sd->solo_islands);
      free(sd);
   }
   for (unsigned i = 0; i < mgr->islands.count; i++) {
      rt_island_t *isl = mgr->islands.items[i];
      for (uint32_t d = 0; d < isl->ndomains; d++)
         free(isl->domains[d].write_sigs);
      free(isl->domains);
      for (uint32_t f = 0; f < isl->n_forces; f++)
         free(isl->forces[f].mask);
      free(isl->forces);
      for (uint32_t s = 0; s < isl->nsigs; s++)
         free(isl->sigs[s].direct_targets);
      free(isl->state);
      free(isl->sigs);
      free(isl->in_sigs);
      free(isl->dirty_in_words);
      free(isl->out_sigs);
      free(isl->ext_out_sigs);
      free(isl->direct_comb_outs);
      free(isl->obs_sigs);
      free(isl->scratch);
      free(isl);
   }
   hash_free(mgr->sig_map);
   ACLEAR(mgr->islands);
   ACLEAR(mgr->dirty_q);
   ACLEAR(mgr->ff_sample_q);
   ACLEAR(mgr->ff_commit_q);
   ACLEAR(mgr->clusters);
   ACLEAR(mgr->dirty_clusters);
   ACLEAR(mgr->super_domains);
   ACLEAR(mgr->super_sample_q);
   ACLEAR(mgr->super_commit_q);
   free(mgr);
}

static void island_init_state_defaults(rt_island_t *isl)
{
   memset(isl->state, 0, isl->state_bytes);

   for (uint32_t i = 0; i < isl->nsigs; i++) {
      const rt_island_sig_t *s = &(isl->sigs[i]);
      if (s->width == 0 || s->nelems == 0
          || (s->flags & (RT_ISIG_TWO_STATE | RT_ISIG_STD_LOGIC9)))
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
   if (flags & RT_ISIG_STD_LOGIC9)
      assert(a_off + ALIGN_UP(width * nelems, 8u) <= isl->state_bytes);
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
   dom->clk_posedge = !!(edge_flags & RT_IEDGE_CLK_POSEDGE);
   dom->rst_posedge = !!(edge_flags & RT_IEDGE_RST_POSEDGE);
   dom->edge_flags = (uint8_t)edge_flags;
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
   const uint32_t nelems = s->nelems;

   if (s->flags & RT_ISIG_STD_LOGIC9) {
      assert(!(sig->shared.flags & SIG_F_PACKED));
      memcpy(isl->state + s->a_off, sig->shared.data + s->sig_offset,
             (size_t)width * nelems);
      return;
   }

   const uint32_t nwords = s->nwords;
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
   const bool is_std = island_sig_is_std(sig, s->flags);

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
   const uint32_t nelems = s->nelems;

   if (s->flags & RT_ISIG_STD_LOGIC9) {
      assert(!(sig->shared.flags & SIG_F_PACKED));
      const uint32_t total = width * nelems;
      const uint8_t *src = isl->state + s->a_off;
      const uint8_t *cur = sig->shared.data + s->sig_offset;
      if (memcmp(cur, src, total) != 0)
         deposit_signal(m, sig, src, s->sig_offset, total);
      return;
   }

   const uint32_t nwords = s->nwords;
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

   const bool is_std = island_sig_is_std(sig, s->flags);

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
   if (s->flags & RT_ISIG_STD_LOGIC9) {
      assert(!(sig->shared.flags & SIG_F_PACKED));
      const uint32_t sig_off = (uint32_t)s->sig_offset + local_off;
      const uint8_t *src = isl->state + s->a_off + local_off;
      if (memcmp(sig->shared.data + sig_off, src, local_cnt) != 0)
         deposit_signal(m, sig, src, (int)sig_off, (int)local_cnt);
      return;
   }

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

   const bool is_std = island_sig_is_std(sig, s->flags);
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
   const uint32_t nelems = s->nelems;

   if (s->flags & RT_ISIG_STD_LOGIC9) {
      assert(!(sig->shared.flags & SIG_F_PACKED));
      const uint32_t total = width * nelems;
      uint8_t *dst = sig->shared.data + s->sig_offset;
      memcpy(dst, isl->state + s->a_off, total);
      if (update_last)
         memcpy(sig->shared.data + sig->shared.size + s->sig_offset,
                dst, total);
      return;
   }

   const uint32_t nwords = s->nwords;
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

   const bool is_std = island_sig_is_std(sig, s->flags);
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
   if (s->flags & RT_ISIG_STD_LOGIC9) {
      assert(!(sig->shared.flags & SIG_F_PACKED));
      uint8_t *dst = sig->shared.data + s->sig_offset + local_off;
      memcpy(dst, isl->state + s->a_off + local_off, local_cnt);
      if (update_last)
         memcpy(sig->shared.data + sig->shared.size
                + s->sig_offset + local_off, dst, local_cnt);
      return;
   }

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

   const bool is_std = island_sig_is_std(sig, s->flags);
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

   rt_island_force_t *f = &(isl->forces[isl->n_forces++]);
   f->sig_idx = s->sig_idx;
   if (s->flags & RT_ISIG_STD_LOGIC9) {
      const uint32_t total = s->width * s->nelems;
      const uint32_t mask_words = (total + 63u) / 64u;
      const uint32_t val_words = (total + 7u) / 8u;
      uint64_t *mem = xcalloc_array(mask_words + val_words, sizeof(uint64_t));
      f->mask = mem;
      f->a = mem + mask_words;
      f->b = NULL;
   }
   else {
      const uint32_t total_words = s->nwords * s->nelems;
      uint64_t *mem = xcalloc_array(total_words * 3u, sizeof(uint64_t));
      f->mask = mem;
      f->a = mem + total_words;
      f->b = mem + total_words * 2u;
   }
   isl->has_forces = true;
   return f;
}

static void island_apply_force_entry(uint8_t *base, const rt_island_sig_t *s,
                                     const rt_island_force_t *f)
{
   if (s->flags & RT_ISIG_STD_LOGIC9) {
      const uint32_t total = s->width * s->nelems;
      uint8_t *dst = base + s->a_off;
      const uint8_t *fa = (const uint8_t *)f->a;
      for (uint32_t k = 0; k < total; k++) {
         if (f->mask[k >> 6] & (UINT64_C(1) << (k & 63u)))
            dst[k] = fa[k];
      }
      return;
   }

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
   if (s->flags & RT_ISIG_STD_LOGIC9) {
      memcpy(dst_base + s->a_off, src_base + s->a_off,
             (size_t)s->width * s->nelems);
      return;
   }
   const size_t bytes = (size_t)s->nwords * s->nelems * sizeof(uint64_t);
   memcpy(dst_base + s->a_off, src_base + s->a_off, bytes);
   if (!(s->flags & RT_ISIG_TWO_STATE))
      memcpy(dst_base + s->b_off, src_base + s->b_off, bytes);
}

static inline bool island_commit_sig_buf(rt_island_t *isl,
                                         const rt_island_sig_t *s)
{
   if (s->flags & RT_ISIG_STD_LOGIC9) {
      const size_t total = (size_t)s->width * s->nelems;
      uint8_t *dst = isl->state + s->a_off;
      const uint8_t *src = isl->next_state + s->a_off;
      if (memcmp(dst, src, total) != 0) {
         memcpy(dst, src, total);
         return true;
      }
      return false;
   }

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

static inline bool island_copy_direct_sig(const rt_island_sig_t *src_s,
                                          rt_island_sig_t *dst_s)
{
   const rt_island_t *src_isl = src_s->isl;
   rt_island_t *dst_isl = dst_s->isl;
   if (src_s->flags & RT_ISIG_STD_LOGIC9) {
      const size_t total = (size_t)src_s->width * src_s->nelems;
      const uint8_t *src = src_isl->state + src_s->a_off;
      uint8_t *dst = dst_isl->state + dst_s->a_off;
      if (memcmp(dst, src, total) != 0) {
         memcpy(dst, src, total);
         return true;
      }
      return false;
   }

   const uint32_t total_words = src_s->nwords * src_s->nelems;
   const uint64_t *sa = (const uint64_t *)(src_isl->state + src_s->a_off);
   uint64_t *da = (uint64_t *)(dst_isl->state + dst_s->a_off);
   bool changed = false;

   if (src_s->flags & RT_ISIG_TWO_STATE) {
      for (uint32_t i = 0; i < total_words; i++) {
         if (da[i] != sa[i]) {
            da[i] = sa[i];
            changed = true;
         }
      }
      return changed;
   }

   const uint64_t *sb = (const uint64_t *)(src_isl->state + src_s->b_off);
   uint64_t *db = (uint64_t *)(dst_isl->state + dst_s->b_off);
   for (uint32_t i = 0; i < total_words; i++) {
      if (da[i] != sa[i] || db[i] != sb[i]) {
         da[i] = sa[i];
         db[i] = sb[i];
         changed = true;
      }
   }
   return changed;
}

typedef A(rt_island_sig_t *) island_sig_ptr_list_t;

static bool island_check_direct_sig(const rt_island_sig_t *src_s,
                                    island_sig_ptr_list_t *consumers)
{
   consumers->count = 0;
   if (src_s->sig == NULL || src_s->next_owner != NULL
       || (src_s->flags & RT_ISIG_OBSERVED))
      return false;

   const rt_signal_t *sig = src_s->sig;
   if (sig->shared.flags & (SIG_F_POLL_WATCHED | SIG_F_EXT_READ))
      return false;

   const rt_nexus_t *nx = &(sig->nexus);
   for (uint32_t i = 0; i < sig->n_nexus; i++, nx = nx->chain) {
      if (nx->n_sources != 0 || nx->outputs != NULL)
         return false;
      if (nx->pending == NULL)
         continue;

      if (pointer_tag(nx->pending) == 1) {
         rt_wakeable_t *w = untag_pointer(nx->pending, rt_wakeable_t);
         if (w->kind != W_WATCH)
            return false;
         rt_watch_t *watch = container_of(w, rt_watch_t, wakeable);
         if (watch->fn != island_input_watch_cb)
            return false;
         rt_island_sig_t *dst_s = watch->user_data;
         if (dst_s->isl == src_s->isl
             || dst_s->sig_offset != src_s->sig_offset
             || dst_s->width != src_s->width
             || dst_s->nelems != src_s->nelems
             || ((src_s->flags ^ dst_s->flags)
                 & (RT_ISIG_TWO_STATE | RT_ISIG_STD_LOGIC9)) != 0)
            return false;

         bool seen = false;
         for (unsigned k = 0; k < consumers->count; k++) {
            if (consumers->items[k] == dst_s) {
               seen = true;
               break;
            }
         }
         if (!seen)
            APUSH(*consumers, dst_s);
      }
      else {
         const rt_pending_t *p = untag_pointer(nx->pending, rt_pending_t);
         for (unsigned j = 0; j < p->count; j++) {
            rt_wakeable_t *w = p->wake[j];
            if (w == NULL)
               continue;
            if (w->kind != W_WATCH)
               return false;
            rt_watch_t *watch = container_of(w, rt_watch_t, wakeable);
            if (watch->fn != island_input_watch_cb)
               return false;
            rt_island_sig_t *dst_s = watch->user_data;
            if (dst_s->isl == src_s->isl
                || dst_s->sig_offset != src_s->sig_offset
                || dst_s->width != src_s->width
                || dst_s->nelems != src_s->nelems
                || ((src_s->flags ^ dst_s->flags)
                 & (RT_ISIG_TWO_STATE | RT_ISIG_STD_LOGIC9)) != 0)
               return false;

            bool seen = false;
            for (unsigned k = 0; k < consumers->count; k++) {
               if (consumers->items[k] == dst_s) {
                  seen = true;
                  break;
               }
            }
            if (!seen)
               APUSH(*consumers, dst_s);
         }
      }
   }

   return consumers->count > 0;
}

typedef struct {
   rt_island_sig_t  *src_s;
   rt_island_sig_t **targets;
   uint32_t          n_targets;
   bool              rejected;
} hier_cand_t;

typedef struct {
   uint32_t dst_isl;
   uint32_t cand_idx;
} hier_comb_edge_t;

typedef struct {
   uint32_t node;
   uint32_t edge_idx;
} hier_dfs_frame_t;

static uint32_t hier_uf_find(uint32_t *parent, uint32_t x)
{
   while (parent[x] != x) {
      parent[x] = parent[parent[x]];
      x = parent[x];
   }
   return x;
}

static void hier_uf_union(uint32_t *parent, uint32_t *sz,
                          uint32_t a, uint32_t b)
{
   uint32_t ra = hier_uf_find(parent, a);
   uint32_t rb = hier_uf_find(parent, b);
   if (ra == rb)
      return;
   if (sz[ra] < sz[rb]) {
      uint32_t tmp = ra;
      ra = rb;
      rb = tmp;
   }
   parent[rb] = ra;
   sz[ra] += sz[rb];
}

static uint32_t *hier_topo_rank = NULL;

static int hier_cmp_island_topo(const void *a, const void *b)
{
   const rt_island_t *ia = *(const rt_island_t *const *)a;
   const rt_island_t *ib = *(const rt_island_t *const *)b;
   const uint32_t ra = hier_topo_rank[ia->isl_id];
   const uint32_t rb = hier_topo_rank[ib->isl_id];
   if (ra < rb) return -1;
   if (ra > rb) return 1;
   return 0;
}

typedef struct {
   rt_signal_t *clk_sig;
   int32_t      clk_sig_offset;
   rt_signal_t *rst_sig;
   int32_t      rst_sig_offset;
   bool         clk_posedge;
   bool         rst_posedge;
   uint8_t      edge_flags;
} sdom_key_t;

static uint32_t sdom_key_hash(const void *key)
{
   const sdom_key_t *k = key;
   uint32_t h = mix_bits_64((uintptr_t)k->clk_sig);
   h ^= mix_bits_64((uint64_t)(uint32_t)k->clk_sig_offset);
   h ^= mix_bits_64((uintptr_t)k->rst_sig);
   h ^= mix_bits_64((uint64_t)(uint32_t)k->rst_sig_offset);
   h ^= (k->clk_posedge ? 1u : 0u) | (k->rst_posedge ? 2u : 0u)
      | ((uint32_t)k->edge_flags << 2);
   return h;
}

static bool sdom_key_cmp(const void *a, const void *b)
{
   const sdom_key_t *ka = a, *kb = b;
   return ka->clk_sig == kb->clk_sig
      && ka->clk_sig_offset == kb->clk_sig_offset
      && ka->rst_sig == kb->rst_sig
      && ka->rst_sig_offset == kb->rst_sig_offset
      && ka->clk_posedge == kb->clk_posedge
      && ka->rst_posedge == kb->rst_posedge
      && ka->edge_flags == kb->edge_flags;
}

static void island_build_hier(rt_island_mgr_t *mgr)
{
   const uint32_t N = mgr->islands.count;
   if (N == 0)
      return;

   for (uint32_t i = 0; i < N; i++)
      mgr->islands.items[i]->isl_id = i;

   // 1. Discover candidate clean inter-island direct wires
   A(hier_cand_t) cands = AINIT;
   island_sig_ptr_list_t tmp_cons = AINIT;

   for (uint32_t i = 0; i < N; i++) {
      rt_island_t *isl = mgr->islands.items[i];
      for (uint32_t j = 0; j < isl->nsigs; j++) {
         rt_island_sig_t *src_s = &(isl->sigs[j]);
         if (!(src_s->flags & (RT_ISIG_COMB_WRITTEN | RT_ISIG_FF_WRITTEN)))
            continue;
         if (!island_check_direct_sig(src_s, &tmp_cons))
            continue;

         hier_cand_t c = {
            .src_s     = src_s,
            .targets   = xmalloc_array(tmp_cons.count,
                                       sizeof(rt_island_sig_t *)),
            .n_targets = tmp_cons.count,
            .rejected  = false,
         };
         memcpy(c.targets, tmp_cons.items,
                tmp_cons.count * sizeof(rt_island_sig_t *));
         APUSH(cands, c);
      }
   }
   ACLEAR(tmp_cons);

   // 2. Build combinational dependency graph & break any cycles via DFS
   uint32_t *comb_deg LOCAL = xcalloc_array(N, sizeof(uint32_t));
   for (unsigned c = 0; c < cands.count; c++) {
      const hier_cand_t *hc = &(cands.items[c]);
      if (!(hc->src_s->flags & RT_ISIG_COMB_WRITTEN))
         continue;
      const uint32_t u = hc->src_s->isl->isl_id;
      for (uint32_t t = 0; t < hc->n_targets; t++) {
         if (hc->targets[t]->flags & RT_ISIG_COMB_READ)
            comb_deg[u]++;
      }
   }

   hier_comb_edge_t **comb_adj LOCAL = xcalloc_array(N,
                                                     sizeof(hier_comb_edge_t *));
   uint32_t *comb_pos LOCAL = xcalloc_array(N, sizeof(uint32_t));
   for (uint32_t u = 0; u < N; u++) {
      if (comb_deg[u] > 0)
         comb_adj[u] = xmalloc_array(comb_deg[u], sizeof(hier_comb_edge_t));
   }

   for (unsigned c = 0; c < cands.count; c++) {
      const hier_cand_t *hc = &(cands.items[c]);
      if (!(hc->src_s->flags & RT_ISIG_COMB_WRITTEN))
         continue;
      const uint32_t u = hc->src_s->isl->isl_id;
      for (uint32_t t = 0; t < hc->n_targets; t++) {
         if (hc->targets[t]->flags & RT_ISIG_COMB_READ) {
            comb_adj[u][comb_pos[u]++] = (hier_comb_edge_t){
               .dst_isl  = hc->targets[t]->isl->isl_id,
               .cand_idx = c,
            };
         }
      }
   }

   uint8_t *color LOCAL = xcalloc_array(N, sizeof(uint8_t));
   uint32_t *post_order LOCAL = xmalloc_array(N, sizeof(uint32_t));
   uint32_t post_n = 0;
   A(hier_dfs_frame_t) dfs_stk = AINIT;

   for (uint32_t start = 0; start < N; start++) {
      if (color[start] != 0)
         continue;
      color[start] = 1;
      APUSH(dfs_stk, ((hier_dfs_frame_t){ .node = start, .edge_idx = 0 }));

      while (dfs_stk.count > 0) {
         hier_dfs_frame_t *top = &(dfs_stk.items[dfs_stk.count - 1]);
         const uint32_t u = top->node;
         if (top->edge_idx < comb_deg[u]) {
            const hier_comb_edge_t e = comb_adj[u][top->edge_idx++];
            const uint32_t v = e.dst_isl;
            if (color[v] == 1)
               cands.items[e.cand_idx].rejected = true;
            else if (color[v] == 0) {
               color[v] = 1;
               APUSH(dfs_stk,
                     ((hier_dfs_frame_t){ .node = v, .edge_idx = 0 }));
            }
         }
         else {
            color[u] = 2;
            post_order[post_n++] = u;
            APOP(dfs_stk);
         }
      }
   }
   ACLEAR(dfs_stk);
   for (uint32_t u = 0; u < N; u++)
      free(comb_adj[u]);

   uint32_t *topo_rank LOCAL = xmalloc_array(N, sizeof(uint32_t));
   for (uint32_t k = 0; k < post_n; k++)
      topo_rank[post_order[post_n - 1u - k]] = k;

   // 3. Commit RT_ISIG_DIRECT_LINK on accepted candidates & Union-Find clusters
   uint32_t *uf_parent LOCAL = xmalloc_array(N, sizeof(uint32_t));
   uint32_t *uf_sz LOCAL = xmalloc_array(N, sizeof(uint32_t));
   for (uint32_t i = 0; i < N; i++) {
      uf_parent[i] = i;
      uf_sz[i] = 1;
   }

   for (unsigned c = 0; c < cands.count; c++) {
      hier_cand_t *hc = &(cands.items[c]);
      if (hc->rejected) {
         free(hc->targets);
         continue;
      }

      rt_island_sig_t *src_s = hc->src_s;
      src_s->flags |= RT_ISIG_DIRECT_LINK;
      src_s->direct_targets = hc->targets;
      src_s->n_direct_targets = hc->n_targets;

      const uint32_t u = src_s->isl->isl_id;
      for (uint32_t t = 0; t < hc->n_targets; t++) {
         rt_island_sig_t *dst_s = hc->targets[t];
         dst_s->flags |= RT_ISIG_DIRECT_LINK;
         dst_s->direct_src = src_s;
         if (dst_s->watch != NULL) {
            watch_free(mgr->model, dst_s->watch);
            dst_s->watch = NULL;
         }
         hier_uf_union(uf_parent, uf_sz, u, dst_s->isl->isl_id);
      }
   }
   ACLEAR(cands);

   // Build per-island ext_out_sigs and direct_comb_outs
   for (uint32_t i = 0; i < N; i++) {
      rt_island_t *isl = mgr->islands.items[i];
      uint32_t n_ext = 0, n_dir = 0;
      for (uint32_t j = 0; j < isl->n_out_sigs; j++) {
         if (!(isl->sigs[isl->out_sigs[j]].flags & RT_ISIG_DIRECT_LINK))
            n_ext++;
      }
      for (uint32_t j = 0; j < isl->nsigs; j++) {
         const uint32_t f = isl->sigs[j].flags;
         if ((f & (RT_ISIG_COMB_WRITTEN | RT_ISIG_DIRECT_LINK))
             == (RT_ISIG_COMB_WRITTEN | RT_ISIG_DIRECT_LINK))
            n_dir++;
      }

      isl->n_ext_out_sigs = n_ext;
      if (n_ext > 0) {
         isl->ext_out_sigs = xmalloc_array(n_ext, sizeof(uint32_t));
         uint32_t p = 0;
         for (uint32_t j = 0; j < isl->n_out_sigs; j++) {
            const uint32_t sidx = isl->out_sigs[j];
            if (!(isl->sigs[sidx].flags & RT_ISIG_DIRECT_LINK))
               isl->ext_out_sigs[p++] = sidx;
         }
      }

      isl->n_direct_comb_outs = n_dir;
      if (n_dir > 0) {
         isl->direct_comb_outs = xmalloc_array(n_dir, sizeof(uint32_t));
         uint32_t p = 0;
         for (uint32_t j = 0; j < isl->nsigs; j++) {
            const uint32_t f = isl->sigs[j].flags;
            if ((f & (RT_ISIG_COMB_WRITTEN | RT_ISIG_DIRECT_LINK))
                == (RT_ISIG_COMB_WRITTEN | RT_ISIG_DIRECT_LINK))
               isl->direct_comb_outs[p++] = j;
         }
      }
   }

   // 4. Create multi-island super-island clusters and topologically sort
   rt_super_island_t **root_to_cl LOCAL =
      xcalloc_array(N, sizeof(rt_super_island_t *));
   for (uint32_t i = 0; i < N; i++) {
      const uint32_t r = hier_uf_find(uf_parent, i);
      if (uf_sz[r] < 2)
         continue;
      if (root_to_cl[r] == NULL) {
         rt_super_island_t *cl = xcalloc(sizeof(rt_super_island_t));
         cl->id = mgr->clusters.count;
         cl->islands = xmalloc_array(uf_sz[r], sizeof(rt_island_t *));
         cl->min_dirty_pos = UINT32_MAX;
         root_to_cl[r] = cl;
         APUSH(mgr->clusters, cl);
      }
      rt_super_island_t *cl = root_to_cl[r];
      rt_island_t *isl = mgr->islands.items[i];
      cl->islands[cl->n_islands++] = isl;
      isl->cluster = cl;
   }

   hier_topo_rank = topo_rank;
   for (unsigned c = 0; c < mgr->clusters.count; c++) {
      rt_super_island_t *cl = mgr->clusters.items[c];
      qsort(cl->islands, cl->n_islands, sizeof(rt_island_t *),
            hier_cmp_island_topo);
      for (uint32_t k = 0; k < cl->n_islands; k++)
         cl->islands[k]->topo_pos = k;
   }
   hier_topo_rank = NULL;

   // 5. Group clock/reset domains across islands into rt_super_domain_t
   typedef struct {
      sdom_key_t              key;
      A(rt_island_domain_t *) doms;
   } sdom_group_t;

   ghash_t *sdom_map = ghash_new(64, sdom_key_hash, sdom_key_cmp);
   A(sdom_group_t *) groups = AINIT;

   for (uint32_t i = 0; i < N; i++) {
      rt_island_t *isl = mgr->islands.items[i];
      for (uint32_t d = 0; d < isl->ndomains; d++) {
         rt_island_domain_t *dom = &(isl->domains[d]);
         if (dom->ff_handle == JIT_HANDLE_INVALID)
            continue;

         const rt_island_sig_t *cs = &(isl->sigs[dom->clk_sig_idx]);
         const rt_island_sig_t *rs = dom->rst_sig_idx >= 0
            ? &(isl->sigs[dom->rst_sig_idx]) : NULL;
         sdom_key_t k = {
            .clk_sig        = cs->sig,
            .clk_sig_offset = cs->sig_offset,
            .rst_sig        = rs != NULL ? rs->sig : NULL,
            .rst_sig_offset = rs != NULL ? rs->sig_offset : 0,
            .clk_posedge    = dom->clk_posedge,
            .rst_posedge    = dom->rst_posedge,
            .edge_flags     = dom->edge_flags,
         };

         sdom_group_t *g = ghash_get(sdom_map, &k);
         if (g == NULL) {
            g = xcalloc(sizeof(sdom_group_t));
            g->key = k;
            ghash_put(sdom_map, &(g->key), g);
            APUSH(groups, g);
         }
         APUSH(g->doms, dom);
      }
   }

   for (unsigned gi = 0; gi < groups.count; gi++) {
      sdom_group_t *g = groups.items[gi];
      if (g->doms.count >= 2) {
         rt_super_domain_t *sd = xcalloc(sizeof(rt_super_domain_t));
         sd->mgr            = mgr;
         sd->clk_sig        = g->key.clk_sig;
         sd->clk_sig_offset = g->key.clk_sig_offset;
         sd->rst_sig        = g->key.rst_sig;
         sd->rst_sig_offset = g->key.rst_sig_offset;
         sd->clk_posedge    = g->key.clk_posedge;
         sd->rst_posedge    = g->key.rst_posedge;
         sd->edge_flags     = g->key.edge_flags;
         sd->prev_clk       = g->doms.items[0]->prev_clk;
         sd->prev_rst       = g->doms.items[0]->prev_rst;
         sd->n_doms         = g->doms.count;
         sd->doms           = xmalloc_array(sd->n_doms,
                                            sizeof(rt_island_domain_t *));

         A(rt_super_island_t *) cls = AINIT;
         A(rt_island_t *) solos = AINIT;

         for (uint32_t j = 0; j < sd->n_doms; j++) {
            rt_island_domain_t *dom = g->doms.items[j];
            sd->doms[j] = dom;
            dom->super_dom = sd;

            if (dom->clk_watch != NULL) {
               watch_free(mgr->model, dom->clk_watch);
               dom->clk_watch = NULL;
            }
            if (dom->rst_watch != NULL) {
               watch_free(mgr->model, dom->rst_watch);
               dom->rst_watch = NULL;
            }

            if (dom->isl->cluster != NULL) {
               bool seen = false;
               for (unsigned k = 0; k < cls.count; k++) {
                  if (cls.items[k] == dom->isl->cluster) {
                     seen = true;
                     break;
                  }
               }
               if (!seen)
                  APUSH(cls, dom->isl->cluster);
            }
            else {
               bool seen = false;
               for (unsigned k = 0; k < solos.count; k++) {
                  if (solos.items[k] == dom->isl) {
                     seen = true;
                     break;
                  }
               }
               if (!seen)
                  APUSH(solos, dom->isl);
            }
         }

         sd->clusters = cls.items;
         sd->n_clusters = cls.count;
         sd->solo_islands = solos.items;
         sd->n_solo_islands = solos.count;

         sd->clk_watch = watch_new(mgr->model, island_super_clk_watch_cb,
                                   sd, WATCH_EVENT, 1);
         if (sd->clk_sig->shared.flags & SIG_F_PACKED)
            model_set_event_cb(mgr->model, sd->clk_sig, 0, 1, sd->clk_watch);
         else
            model_set_event_cb(mgr->model, sd->clk_sig, sd->clk_sig_offset, 1,
                               sd->clk_watch);

         if (sd->rst_sig != NULL) {
            sd->rst_watch = watch_new(mgr->model, island_super_rst_watch_cb,
                                      sd, WATCH_EVENT, 1);
            if (sd->rst_sig->shared.flags & SIG_F_PACKED)
               model_set_event_cb(mgr->model, sd->rst_sig, 0, 1,
                                  sd->rst_watch);
            else
               model_set_event_cb(mgr->model, sd->rst_sig, sd->rst_sig_offset,
                                  1, sd->rst_watch);
         }

         APUSH(mgr->super_domains, sd);
      }

      ACLEAR(g->doms);
      free(g);
   }
   ACLEAR(groups);
   ghash_free(sdom_map);

   if (opt_get_int(OPT_DUMP_ISLANDS)) {
      uint32_t n_direct = 0;
      for (uint32_t i = 0; i < N; i++) {
         const rt_island_t *isl = mgr->islands.items[i];
         for (uint32_t j = 0; j < isl->nsigs; j++) {
            if ((isl->sigs[j].flags & RT_ISIG_DIRECT_LINK)
                && isl->sigs[j].n_direct_targets > 0)
               n_direct++;
         }
      }
      printf("ISLAND_HIER: islands=%u super_islands=%u direct_wires=%u "
             "super_domains=%u\n",
             N, mgr->clusters.count, n_direct, mgr->super_domains.count);
   }
}

void rt_island_reset_all(rt_island_mgr_t *mgr)
{
   if (mgr == NULL)
      return;

   mgr->dirty_q.count = 0;
   mgr->ff_sample_q.count = 0;
   mgr->ff_commit_q.count = 0;
   mgr->dirty_clusters.count = 0;
   mgr->super_sample_q.count = 0;
   mgr->super_commit_q.count = 0;

   for (unsigned i = 0; i < mgr->clusters.count; i++) {
      rt_super_island_t *cl = mgr->clusters.items[i];
      cl->has_dirty = false;
      cl->has_dirty_inputs = false;
      cl->dirty_count = 0;
      cl->min_dirty_pos = UINT32_MAX;
   }

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
         dom->prev_clk = island_initial_prev_domain_bit(mgr->model, cs->sig,
                                                        cs->sig_offset,
                                                        dom->edge_flags);
         if (dom->rst_sig_idx >= 0) {
            const rt_island_sig_t *rs = &(isl->sigs[dom->rst_sig_idx]);
            dom->prev_rst = island_initial_prev_domain_bit(mgr->model, rs->sig,
                                                           rs->sig_offset,
                                                           dom->edge_flags);
         }
         else
            dom->prev_rst = (dom->edge_flags & RT_IEDGE_VHDL) ? 0u : 3u;
         dom->in_sample_q = false;
         dom->in_commit_q = false;
      }

      isl->dirty = false;
      isl->in_dirty_list = false;
      isl->has_dirty_inputs = false;
   }

   if (island_is_hier_mode()) {
      if (!mgr->hier_built) {
         island_build_hier(mgr);
         mgr->hier_built = true;
      }
      else {
         for (unsigned i = 0; i < mgr->super_domains.count; i++) {
            rt_super_domain_t *sd = mgr->super_domains.items[i];
            sd->prev_clk = island_initial_prev_domain_bit(mgr->model, sd->clk_sig,
                                                          sd->clk_sig_offset,
                                                          sd->edge_flags);
            sd->prev_rst = island_initial_prev_domain_bit(mgr->model, sd->rst_sig,
                                                          sd->rst_sig_offset,
                                                          sd->edge_flags);
            sd->in_sample_q = false;
            sd->in_commit_q = false;
         }
      }
   }

   for (unsigned i = 0; i < mgr->islands.count; i++) {
      rt_island_t *isl = mgr->islands.items[i];
      const bool no_comb = isl->eval_handle == JIT_HANDLE_INVALID;
      for (uint32_t j = 0; j < isl->nsigs; j++) {
         rt_island_sig_t *s = &(isl->sigs[j]);
         if (!(s->flags & (RT_ISIG_TWO_STATE | RT_ISIG_STD_LOGIC9))
             || !(s->flags & (RT_ISIG_FF_WRITTEN | RT_ISIG_COMB_WRITTEN)))
            continue;
         if (s->flags & RT_ISIG_DIRECT_LINK) {
            for (uint32_t t = 0; t < s->n_direct_targets; t++)
               island_copy_direct_sig(s, s->direct_targets[t]);
         }
         else if ((s->flags & RT_ISIG_TWO_STATE)
                  && s->sig != NULL
                  && (s->flags & (RT_ISIG_OUT | RT_ISIG_OBSERVED))
                  && (!(s->flags & RT_ISIG_COMB_WRITTEN) || no_comb)) {
            island_sync_out_sig(mgr->model, isl, s);
         }
      }
      for (uint32_t d = 0; d < isl->ndomains; d++) {
         rt_island_domain_t *dom = &(isl->domains[d]);
         if (dom->rst_sig_idx >= 0 && (dom->edge_flags & RT_IEDGE_RST_LEVEL)) {
            const rt_island_sig_t *rs = &(isl->sigs[dom->rst_sig_idx]);
            if (rs->sig != NULL) {
               const uint8_t cur = island_read_raw_bit(rs->sig, rs->sig_offset);
               const bool is_std = island_sig_is_std(rs->sig, rs->flags);
               const bool two_state = !!(rs->flags & RT_ISIG_TWO_STATE);
               if (island_vhdl_rst_match(cur, dom->rst_posedge,
                                         is_std, two_state)) {
                  if (dom->super_dom != NULL) {
                     if (!dom->super_dom->in_sample_q) {
                        dom->super_dom->in_sample_q = true;
                        APUSH(mgr->super_sample_q, dom->super_dom);
                     }
                  }
                  else if (!dom->in_sample_q) {
                     dom->in_sample_q = true;
                     APUSH(mgr->ff_sample_q, dom);
                  }
               }
            }
         }
      }
      if (!no_comb)
         island_mark_dirty(isl);
   }
}

bool rt_island_has_dirty(const rt_island_mgr_t *mgr)
{
   return mgr != NULL
      && (mgr->dirty_q.count > 0 || mgr->dirty_clusters.count > 0);
}

static bool island_call_comb(jit_t *jit, rt_model_t *m, rt_island_t *isl,
                             tlab_t *tlab)
{
   jit_scalar_t p1 = { .pointer = isl->inst_ctx };
   jit_scalar_t p2 = { .pointer = isl->state };
   jit_scalar_t result = { .integer = 0 };
   tlab_t null_tlab;
   tlab_t *use_tlab = tlab;
   if (use_tlab == NULL) {
      null_tlab = jit_null_tlab(jit);
      use_tlab = &null_tlab;
   }
   const bool ok = jit_fastcall(jit, isl->eval_handle, &result,
                                p1, p2, use_tlab);
   if (tlab != NULL)
      tlab_reset(tlab);
   if (!ok)
      return false;

   if (unlikely(isl->has_forces))
      island_apply_forces_comb(isl);
   return true;
}

static bool island_eval_cluster(jit_t *jit, rt_model_t *m,
                                rt_super_island_t *cl, tlab_t *tlab)
{
   bool ok = true;
   for (uint32_t k = cl->min_dirty_pos;
        k < cl->n_islands && cl->dirty_count > 0; k++) {
      rt_island_t *isl = cl->islands[k];
      if (!isl->dirty)
         continue;
      cl->dirty_count--;
      isl->dirty = false;
      isl->has_dirty_inputs = false;

      island_sync_dirty_inputs(isl);

      if (isl->eval_handle == JIT_HANDLE_INVALID)
         continue;

      if (!island_call_comb(jit, m, isl, tlab)) {
         ok = false;
         break;
      }

      for (uint32_t j = 0; j < isl->n_direct_comb_outs; j++) {
         const rt_island_sig_t *src_s = &(isl->sigs[isl->direct_comb_outs[j]]);
         if (unlikely(!(src_s->flags & RT_ISIG_DIRECT_LINK)))
            continue;
         for (uint32_t t = 0; t < src_s->n_direct_targets; t++) {
            rt_island_sig_t *dst_s = src_s->direct_targets[t];
            if (island_copy_direct_sig(src_s, dst_s)
                && (dst_s->flags & RT_ISIG_COMB_READ)
                && dst_s->isl->eval_handle != JIT_HANDLE_INVALID)
               island_mark_dirty(dst_s->isl);
         }
      }

      for (uint32_t j = 0; j < isl->n_ext_out_sigs; j++)
         island_sync_out_sig(m, isl, &(isl->sigs[isl->ext_out_sigs[j]]));
      if (unlikely(isl->n_obs_sigs > 0)) {
         for (uint32_t j = 0; j < isl->n_obs_sigs; j++)
            island_sync_out_sig(m, isl, &(isl->sigs[isl->obs_sigs[j]]));
      }
   }

   if (ok) {
      cl->has_dirty = false;
      cl->has_dirty_inputs = false;
      cl->min_dirty_pos = UINT32_MAX;
      cl->dirty_count = 0;
   }
   return ok;
}

static bool island_eval_dirty_clusters(jit_t *jit, rt_model_t *m,
                                       rt_island_mgr_t *mgr, tlab_t *tlab)
{
   if (mgr->dirty_clusters.count == 0)
      return true;

   unsigned n = mgr->dirty_clusters.count;
   bool ok = true;
   for (unsigned i = 0; i < n; i++) {
      rt_super_island_t *cl = mgr->dirty_clusters.items[i];
      if (!cl->has_dirty)
         continue;
      if (!island_eval_cluster(jit, m, cl, tlab)) {
         ok = false;
         break;
      }
   }

   if (mgr->dirty_clusters.count > n) {
      memmove(mgr->dirty_clusters.items, mgr->dirty_clusters.items + n,
              (mgr->dirty_clusters.count - n) * sizeof(rt_super_island_t *));
      mgr->dirty_clusters.count -= n;
   }
   else
      mgr->dirty_clusters.count = 0;

   return ok;
}

bool rt_island_eval_dirty(jit_t *jit, rt_model_t *m, rt_island_mgr_t *mgr,
                          tlab_t *tlab)
{
   if (mgr == NULL || (mgr->dirty_q.count == 0 && mgr->dirty_clusters.count == 0))
      return true;

   bool ok = true;
   if (mgr->dirty_q.count > 0) {
      unsigned n = mgr->dirty_q.count;
      for (unsigned i = 0; i < n; i++) {
         rt_island_t *isl = mgr->dirty_q.items[i];
         isl->in_dirty_list = false;
         if (!isl->dirty)
            continue;
         isl->dirty = false;
         isl->has_dirty_inputs = false;

         island_sync_dirty_inputs(isl);

         if (isl->eval_handle == JIT_HANDLE_INVALID)
            continue;

         if (!island_call_comb(jit, m, isl, tlab)) {
            ok = false;
            break;
         }

         if (isl->n_direct_comb_outs > 0) {
            for (uint32_t j = 0; j < isl->n_direct_comb_outs; j++) {
               const rt_island_sig_t *src_s =
                  &(isl->sigs[isl->direct_comb_outs[j]]);
               if (unlikely(!(src_s->flags & RT_ISIG_DIRECT_LINK)))
                  continue;
               for (uint32_t t = 0; t < src_s->n_direct_targets; t++) {
                  rt_island_sig_t *dst_s = src_s->direct_targets[t];
                  if (island_copy_direct_sig(src_s, dst_s)
                      && (dst_s->flags & RT_ISIG_COMB_READ)
                      && dst_s->isl->eval_handle != JIT_HANDLE_INVALID)
                     island_mark_dirty(dst_s->isl);
               }
            }
            for (uint32_t j = 0; j < isl->n_ext_out_sigs; j++)
               island_sync_out_sig(m, isl, &(isl->sigs[isl->ext_out_sigs[j]]));
         }
         else {
            for (uint32_t j = 0; j < isl->n_out_sigs; j++)
               island_sync_out_sig(m, isl, &(isl->sigs[isl->out_sigs[j]]));
         }
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
   }

   if (ok && mgr->dirty_clusters.count > 0)
      ok = island_eval_dirty_clusters(jit, m, mgr, tlab);

   return ok;
}

bool rt_island_has_ff_sample(const rt_island_mgr_t *mgr)
{
   return mgr != NULL
      && (mgr->ff_sample_q.count > 0 || mgr->super_sample_q.count > 0);
}

static bool island_sample_domain(jit_t *jit, rt_island_domain_t *dom,
                                 tlab_t *tlab)
{
   rt_island_t *isl = dom->isl;
   if (isl->has_dirty_inputs) {
      island_sync_dirty_inputs(isl);
      isl->has_dirty_inputs = false;
   }

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
   const bool ok = jit_fastcall(jit, dom->ff_handle, &result,
                                p1, p2, use_tlab);
   if (tlab != NULL)
      tlab_reset(tlab);
   return ok;
}

bool rt_island_eval_ff(jit_t *jit, rt_model_t *m, rt_island_mgr_t *mgr,
                       tlab_t *tlab)
{
   if (mgr == NULL
       || (mgr->ff_sample_q.count == 0 && mgr->super_sample_q.count == 0))
      return true;

   bool ok = true;
   if (mgr->ff_sample_q.count > 0) {
      unsigned n = mgr->ff_sample_q.count;
      for (unsigned i = 0; i < n; i++) {
         rt_island_domain_t *dom = mgr->ff_sample_q.items[i];
         dom->in_sample_q = false;

         rt_island_t *isl = dom->isl;
         if (isl->cluster != NULL && isl->cluster->has_dirty) {
            if (!island_eval_cluster(jit, m, isl->cluster, tlab)) {
               ok = false;
               break;
            }
         }

         if (!island_sample_domain(jit, dom, tlab)) {
            ok = false;
            break;
         }

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
   }

   if (ok && mgr->super_sample_q.count > 0) {
      unsigned n = mgr->super_sample_q.count;
      for (unsigned i = 0; i < n; i++) {
         rt_super_domain_t *sd = mgr->super_sample_q.items[i];
         sd->in_sample_q = false;

         for (uint32_t c = 0; c < sd->n_clusters; c++) {
            rt_super_island_t *cl = sd->clusters[c];
            if (cl->has_dirty && !island_eval_cluster(jit, m, cl, tlab)) {
               ok = false;
               break;
            }
            if (cl->has_dirty_inputs) {
               for (uint32_t k = 0; k < cl->n_islands; k++) {
                  rt_island_t *isl = cl->islands[k];
                  if (isl->has_dirty_inputs) {
                     island_sync_dirty_inputs(isl);
                     isl->has_dirty_inputs = false;
                  }
               }
               cl->has_dirty_inputs = false;
            }
         }
         if (!ok)
            break;

         for (uint32_t s = 0; s < sd->n_solo_islands; s++) {
            rt_island_t *isl = sd->solo_islands[s];
            if (isl->has_dirty_inputs) {
               island_sync_dirty_inputs(isl);
               isl->has_dirty_inputs = false;
            }
            if (isl->dirty && isl->eval_handle != JIT_HANDLE_INVALID) {
               isl->dirty = false;
               if (!island_call_comb(jit, m, isl, tlab)) {
                  ok = false;
                  break;
               }
               for (uint32_t j = 0; j < isl->n_out_sigs; j++)
                  island_sync_out_sig(m, isl, &(isl->sigs[isl->out_sigs[j]]));
               if (unlikely(isl->n_obs_sigs > 0)) {
                  for (uint32_t j = 0; j < isl->n_obs_sigs; j++)
                     island_sync_out_sig(m, isl,
                                         &(isl->sigs[isl->obs_sigs[j]]));
               }
            }
         }
         if (!ok)
            break;

         for (uint32_t d = 0; d < sd->n_doms; d++) {
            if (!island_sample_domain(jit, sd->doms[d], tlab)) {
               ok = false;
               break;
            }
         }
         if (!ok)
            break;

         if (!sd->in_commit_q) {
            sd->in_commit_q = true;
            APUSH(mgr->super_commit_q, sd);
         }
      }

      if (mgr->super_sample_q.count > n) {
         memmove(mgr->super_sample_q.items, mgr->super_sample_q.items + n,
                 (mgr->super_sample_q.count - n) * sizeof(rt_super_domain_t *));
         mgr->super_sample_q.count -= n;
      }
      else
         mgr->super_sample_q.count = 0;
   }

   return ok;
}

bool rt_island_has_ff_commits(const rt_island_mgr_t *mgr)
{
   return mgr != NULL
      && (mgr->ff_commit_q.count > 0 || mgr->super_commit_q.count > 0);
}

static void island_commit_domain(rt_model_t *m, rt_island_domain_t *dom)
{
   rt_island_t *isl = dom->isl;
   bool comb_need_eval = false;
   for (uint32_t j = 0; j < dom->n_write_sigs; j++) {
      rt_island_sig_t *s = &(isl->sigs[dom->write_sigs[j]]);
      if (unlikely(isl->has_forces)) {
         const rt_island_force_t *f = island_find_force(isl, s->sig_idx);
         if (f != NULL)
            island_apply_force_entry(isl->next_state, s, f);
      }

      if (!island_commit_sig_buf(isl, s))
         continue;

      if (s->flags & RT_ISIG_COMB_READ)
         comb_need_eval = true;

      if (s->flags & RT_ISIG_DIRECT_LINK) {
         for (uint32_t t = 0; t < s->n_direct_targets; t++) {
            rt_island_sig_t *dst_s = s->direct_targets[t];
            if (island_copy_direct_sig(s, dst_s)
                && (dst_s->flags & RT_ISIG_COMB_READ)
                && dst_s->isl->eval_handle != JIT_HANDLE_INVALID)
               island_mark_dirty(dst_s->isl);
         }
      }
      else if (s->flags & (RT_ISIG_OUT | RT_ISIG_OBSERVED))
         island_sync_out_sig(m, isl, s);
   }

   if (comb_need_eval && isl->eval_handle != JIT_HANDLE_INVALID)
      island_mark_dirty(isl);
}

void rt_island_commit_ff(rt_model_t *m, rt_island_mgr_t *mgr, tlab_t *tlab)
{
   if (mgr == NULL
       || (mgr->ff_commit_q.count == 0 && mgr->super_commit_q.count == 0))
      return;

   for (unsigned i = 0; i < mgr->ff_commit_q.count; i++) {
      rt_island_domain_t *dom = mgr->ff_commit_q.items[i];
      dom->in_commit_q = false;
      island_commit_domain(m, dom);
   }
   mgr->ff_commit_q.count = 0;

   for (unsigned i = 0; i < mgr->super_commit_q.count; i++) {
      rt_super_domain_t *sd = mgr->super_commit_q.items[i];
      sd->in_commit_q = false;
      for (uint32_t d = 0; d < sd->n_doms; d++)
         island_commit_domain(m, sd->doms[d]);
   }
   mgr->super_commit_q.count = 0;

   if (mgr->dirty_clusters.count > 0)
      island_eval_dirty_clusters(rt_island_model_jit(m), m, mgr, tlab);
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
   if (s->flags & RT_ISIG_STD_LOGIC9) {
      const uint8_t *src = isl->state + s->a_off;
      for (uint32_t e = 0; e < nelems; e++) {
         uint64_t *ea = a_out != NULL ? a_out + e * nwords : NULL;
         uint64_t *eb = b_out != NULL ? b_out + e * nwords : NULL;
         if (ea != NULL) memset(ea, 0, nwords * sizeof(uint64_t));
         if (eb != NULL) memset(eb, 0, nwords * sizeof(uint64_t));
         for (uint32_t i = 0; i < s->width; i++) {
            const uint8_t sv = std_logic_to_sv(src[e * s->width + i]);
            const uint32_t bit = s->width - 1u - i;
            if (ea != NULL && (sv & 1u))
               ea[bit / 64u] |= UINT64_C(1) << (bit & 63u);
            if (eb != NULL && (sv & 2u))
               eb[bit / 64u] |= UINT64_C(1) << (bit & 63u);
         }
      }
      if (width_out != NULL)
         *width_out = s->width;
      if (two_state_out != NULL)
         *two_state_out = false;
      return true;
   }

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

static void island_break_direct_link(rt_model_t *m, rt_island_sig_t *src_s)
{
   if (!(src_s->flags & RT_ISIG_DIRECT_LINK))
      return;

   src_s->flags &= ~RT_ISIG_DIRECT_LINK;
   src_s->flags |= RT_ISIG_OBSERVED;
   island_sync_sig_raw(src_s->isl, src_s, true);
   if (src_s->flags & RT_ISIG_COMB_WRITTEN)
      island_add_obs_sig(src_s->isl, src_s->sig_idx);

   for (uint32_t t = 0; t < src_s->n_direct_targets; t++) {
      rt_island_sig_t *dst_s = src_s->direct_targets[t];
      island_copy_direct_sig(src_s, dst_s);
      dst_s->flags &= ~RT_ISIG_DIRECT_LINK;
      if (dst_s->watch == NULL && dst_s->sig != NULL) {
         dst_s->watch = watch_new(m, island_input_watch_cb, dst_s,
                                  WATCH_EVENT, 1);
         if (dst_s->sig->shared.flags & SIG_F_PACKED)
            model_set_event_cb(m, dst_s->sig, 0, 1, dst_s->watch);
         else
            model_set_event_cb(m, dst_s->sig, dst_s->sig_offset,
                               (int)(dst_s->width * dst_s->nelems),
                               dst_s->watch);
      }
      if ((dst_s->flags & RT_ISIG_COMB_READ)
          && dst_s->isl->eval_handle != JIT_HANDLE_INVALID)
         island_mark_dirty(dst_s->isl);
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
      if (s->flags & RT_ISIG_DIRECT_LINK)
         island_break_direct_link(m, s);
      else if (!(s->flags & RT_ISIG_OBSERVED)) {
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
   for (uint32_t j = 0; j < isl->n_in_sigs; j++) {
      rt_island_sig_t *s = &(isl->sigs[isl->in_sigs[j]]);
      if (s->flags & RT_ISIG_DIRECT_LINK)
         island_copy_direct_sig(s->direct_src, s);
      else
         island_sync_in_sig(isl, s);
   }
   island_clear_in_dirty(isl);
   isl->has_dirty_inputs = false;

   if (isl->eval_handle != JIT_HANDLE_INVALID)
      island_call_comb(rt_island_model_jit(m), m, isl, NULL);

   if (isl->n_direct_comb_outs > 0) {
      for (uint32_t j = 0; j < isl->n_direct_comb_outs; j++) {
         const rt_island_sig_t *src_s = &(isl->sigs[isl->direct_comb_outs[j]]);
         if (unlikely(!(src_s->flags & RT_ISIG_DIRECT_LINK)))
            continue;
         for (uint32_t t = 0; t < src_s->n_direct_targets; t++) {
            rt_island_sig_t *dst_s = src_s->direct_targets[t];
            if (island_copy_direct_sig(src_s, dst_s)
                && (dst_s->flags & RT_ISIG_COMB_READ)
                && dst_s->isl->eval_handle != JIT_HANDLE_INVALID) {
               island_mark_dirty(dst_s->isl);
               rt_island_request_delta(m);
            }
         }
      }
      for (uint32_t j = 0; j < isl->n_ext_out_sigs; j++)
         island_sync_out_sig(m, isl, &(isl->sigs[isl->ext_out_sigs[j]]));
   }
   else {
      for (uint32_t j = 0; j < isl->n_out_sigs; j++)
         island_sync_out_sig(m, isl, &(isl->sigs[isl->out_sigs[j]]));
   }
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
   rt_island_force_t *f = NULL;
   if (is_force)
      f = island_get_or_add_force(isl, s);
   else if (unlikely(isl->has_forces))
      f = (rt_island_force_t *)island_find_force(isl, s->sig_idx);

   if (s->flags & RT_ISIG_STD_LOGIC9) {
      uint8_t *dst = isl->state + s->a_off;
      uint8_t *ndst = isl->next_state != NULL ? isl->next_state + s->a_off : NULL;
      uint8_t *fa = f != NULL ? (uint8_t *)f->a : NULL;
      for (uint32_t k = 0; k < local_cnt; k++) {
         const uint32_t pos = local_off + k;
         uint8_t val = 0;
         if (is_packed_src) {
            const uint32_t src_bit = total_src_w - 1u - k;
            const uint8_t a = (a_words[src_bit / 64u] >> (src_bit & 63u)) & 1u;
            const uint8_t b = b_words != NULL
               ? ((b_words[src_bit / 64u] >> (src_bit & 63u)) & 1u) : 0u;
            val = sv_to_std_logic((uint8_t)(a | (b << 1)));
         }
         else if (bytes != NULL)
            val = bytes[k];

         const uint64_t mbit = UINT64_C(1) << (pos & 63u);
         if (is_force) {
            f->mask[pos >> 6] |= mbit;
            fa[pos] = val;
            dst[pos] = val;
            if (ndst != NULL)
               ndst[pos] = val;
         }
         else {
            if (f != NULL && (f->mask[pos >> 6] & mbit))
               continue;
            dst[pos] = val;
         }
      }
      return;
   }

   const bool two_state = !!(s->flags & RT_ISIG_TWO_STATE);
   uint64_t *ap = (uint64_t *)(isl->state + s->a_off);
   uint64_t *bp = two_state ? NULL : (uint64_t *)(isl->state + s->b_off);
   uint64_t *nap = isl->next_state != NULL
      ? (uint64_t *)(isl->next_state + s->a_off) : NULL;
   uint64_t *nbp = (nap != NULL && !two_state)
      ? (uint64_t *)(isl->next_state + s->b_off) : NULL;

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
      if (s->flags & RT_ISIG_DIRECT_LINK)
         island_break_direct_link(m, s);
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
      if (s->flags & RT_ISIG_DIRECT_LINK)
         island_break_direct_link(m, s);
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
      if (s->flags & RT_ISIG_DIRECT_LINK)
         island_break_direct_link(m, s);
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
      if (s->flags & RT_ISIG_STD_LOGIC9) {
         for (uint32_t k = r_start - s_start; k < r_end - s_start; k++)
            f->mask[k >> 6] &= ~(UINT64_C(1) << (k & 63u));
         const uint32_t mask_words = (s->width * s->nelems + 63u) / 64u;
         bool any = false;
         for (uint32_t w = 0; w < mask_words && !any; w++)
            any = (f->mask[w] != 0);
         if (!any) {
            free(f->mask);
            if ((uint32_t)fidx + 1u < isl->n_forces)
               isl->forces[fidx] = isl->forces[isl->n_forces - 1u];
            isl->n_forces--;
            isl->has_forces = (isl->n_forces > 0);
         }
      }
      else {
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
      }

      if ((s->flags & RT_ISIG_COMB_WRITTEN)
          && isl->eval_handle != JIT_HANDLE_INVALID) {
         island_eval_comb_now(m, isl);
         if (s->flags & RT_ISIG_DIRECT_LINK) {
            for (uint32_t t = 0; t < s->n_direct_targets; t++) {
               rt_island_sig_t *dst_s = s->direct_targets[t];
               if (island_copy_direct_sig(s, dst_s)
                   && (dst_s->flags & RT_ISIG_COMB_READ)
                   && dst_s->isl->eval_handle != JIT_HANDLE_INVALID) {
                  island_mark_dirty(dst_s->isl);
                  rt_island_request_delta(m);
               }
            }
         }
         else if (!(s->flags & (RT_ISIG_OUT | RT_ISIG_OBSERVED)))
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

void rt_island_report_forces(rt_island_mgr_t *mgr, rt_signal_t *sig,
                             uint32_t offset, uint32_t count,
                             void (*fn)(uint32_t, uint32_t, void *),
                             void *arg)
{
   if (mgr == NULL || mgr->sig_map == NULL || sig == NULL || count == 0)
      return;

   for (rt_island_sig_t *s = hash_get(mgr->sig_map, sig);
        s != NULL; s = s->next_owner) {
      rt_island_t *isl = s->isl;
      if (!isl->has_forces)
         continue;

      const rt_island_force_t *f = island_find_force(isl, s->sig_idx);
      if (f == NULL)
         continue;

      const uint32_t s_start = (uint32_t)s->sig_offset;
      const uint32_t s_end = s_start + s->width * s->nelems;
      const uint32_t r_start = MAX(offset, s_start);
      const uint32_t r_end = MIN(offset + count, s_end);
      if (r_start >= r_end)
         continue;

      uint32_t run_start = 0, run_len = 0;
      for (uint32_t pos = r_start; pos < r_end; pos++) {
         const uint32_t k = pos - s_start;
         bool masked;
         if (s->flags & RT_ISIG_STD_LOGIC9)
            masked = !!(f->mask[k >> 6] & (UINT64_C(1) << (k & 63u)));
         else {
            const uint32_t e = k / s->width;
            const uint32_t lsb = s->width - 1u - (k % s->width);
            const uint32_t widx = e * s->nwords + (lsb / 64u);
            const uint64_t bit = UINT64_C(1) << (lsb & 63u);
            masked = !!(f->mask[widx] & bit);
         }

         if (masked) {
            if (run_len == 0)
               run_start = pos - offset;
            run_len++;
         }
         else if (run_len > 0) {
            (*fn)(run_start, run_len, arg);
            run_len = 0;
         }
      }
      if (run_len > 0)
         (*fn)(run_start, run_len, arg);
   }
}
