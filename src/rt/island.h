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

#ifndef _RT_ISLAND_H
#define _RT_ISLAND_H

#include "prim.h"
#include "rt/defs.h"
#include "rt/model.h"

#include <stdint.h>

typedef enum {
   RT_ISIG_IN           = (1u << 0),
   RT_ISIG_OUT          = (1u << 1),
   RT_ISIG_TWO_STATE    = (1u << 2),
   RT_ISIG_IS_NET       = (1u << 3),
   RT_ISIG_COMB_WRITTEN = (1u << 4),
   RT_ISIG_COMB_READ    = (1u << 5),
   RT_ISIG_FF_WRITTEN   = (1u << 6),
   RT_ISIG_OBSERVED     = (1u << 7),
   RT_ISIG_DIRECT_LINK  = (1u << 8),
} rt_island_sig_flags_t;

typedef struct _rt_super_island rt_super_island_t;
typedef struct _rt_super_domain rt_super_domain_t;

#define RT_ISLAND_STRIDE(bytes) ALIGN_UP(MAX(64u, (bytes)), 64u)

typedef struct {
   uint32_t  sig_idx;
   uint64_t *mask;
   uint64_t *a;
   uint64_t *b;
} rt_island_force_t;

typedef struct _rt_island_sig {
   rt_signal_t            *sig;
   struct _rt_island      *isl;
   uint32_t                sig_idx;
   uint32_t                in_idx;
   struct _rt_island_sig  *next_owner;
   struct _rt_island_sig **direct_targets;
   struct _rt_island_sig  *direct_src;
   uint32_t                n_direct_targets;
   int32_t                 sig_offset;
   uint32_t                a_off;
   uint32_t                b_off;
   uint32_t                width;
   uint32_t                nelems;
   uint32_t                nwords;
   uint32_t                flags;
   rt_watch_t             *watch;
} rt_island_sig_t;

typedef struct {
   struct _rt_island *isl;
   rt_super_domain_t *super_dom;
   uint32_t           dom_idx;
   jit_handle_t       ff_handle;
   uint32_t           clk_sig_idx;
   int32_t            rst_sig_idx;
   bool               clk_posedge;
   bool               rst_posedge;
   uint8_t            prev_clk;
   uint8_t            prev_rst;
   bool               in_sample_q;
   bool               in_commit_q;
   uint32_t          *write_sigs;
   uint32_t           n_write_sigs;
   uint32_t           cap_write_sigs;
   rt_watch_t        *clk_watch;
   rt_watch_t        *rst_watch;
} rt_island_domain_t;

typedef struct _rt_island {
   rt_model_t          *model;
   struct _rt_island_mgr *mgr;
   rt_super_island_t   *cluster;
   uint32_t             isl_id;
   uint32_t             topo_pos;
   uint8_t             *state;
   uint8_t             *next_state;
   uint32_t             state_bytes;
   jit_handle_t         eval_handle;
   void                *inst_ctx;
   rt_island_sig_t     *sigs;
   uint32_t             nsigs;
   rt_island_domain_t  *domains;
   uint32_t             ndomains;
   uint32_t            *in_sigs;
   uint32_t             n_in_sigs;
   uint64_t             dirty_in_mask;
   uint64_t            *dirty_in_words;
   uint32_t             dirty_in_nwords;
   uint32_t            *out_sigs;
   uint32_t             n_out_sigs;
   uint32_t            *ext_out_sigs;
   uint32_t             n_ext_out_sigs;
   uint32_t            *direct_comb_outs;
   uint32_t             n_direct_comb_outs;
   uint32_t            *obs_sigs;
   uint32_t             n_obs_sigs;
   uint32_t             cap_obs_sigs;
   rt_island_force_t   *forces;
   uint32_t             n_forces;
   uint32_t             cap_forces;
   uint64_t            *scratch;
   uint32_t             scratch_words;
   uint32_t             dirty_gen;
   bool                 dirty;
   bool                 in_dirty_list;
   bool                 has_dirty_inputs;
   bool                 initialized;
   bool                 has_forces;
} rt_island_t;

typedef struct _rt_island_mgr rt_island_mgr_t;

rt_island_mgr_t *rt_island_mgr_new(rt_model_t *m);
void rt_island_mgr_free(rt_island_mgr_t *mgr);
void rt_island_reset_all(rt_island_mgr_t *mgr);
bool rt_island_has_dirty(const rt_island_mgr_t *mgr);
bool rt_island_eval_dirty(jit_t *jit, rt_model_t *m, rt_island_mgr_t *mgr,
                          tlab_t *tlab);
bool rt_island_has_ff_sample(const rt_island_mgr_t *mgr);
bool rt_island_eval_ff(jit_t *jit, rt_model_t *m, rt_island_mgr_t *mgr,
                       tlab_t *tlab);
bool rt_island_has_ff_commits(const rt_island_mgr_t *mgr);
void rt_island_commit_ff(rt_model_t *m, rt_island_mgr_t *mgr, tlab_t *tlab);

bool rt_island_read_sig(rt_island_mgr_t *mgr, rt_signal_t *sig,
                        uint64_t *a_out, uint64_t *b_out,
                        uint32_t *width_out, bool *two_state_out);
void rt_island_sync_to_signal(rt_model_t *m, rt_signal_t *sig);
void rt_island_sync_slice_to_signal(rt_model_t *m, rt_signal_t *sig,
                                    uint32_t offset, uint32_t count);
void rt_island_on_observer_attached(rt_model_t *m, rt_signal_t *sig);
bool rt_island_vpi_put(rt_model_t *m, rt_signal_t *sig,
                       const uint64_t *a_val, const uint64_t *b_val,
                       uint32_t width, bool is_force);
void rt_island_vpi_release(rt_model_t *m, rt_signal_t *sig);
bool rt_island_put_bytes(rt_model_t *m, rt_signal_t *sig,
                         const uint8_t *bytes, uint32_t offset, uint32_t count,
                         bool is_force);
bool rt_island_put_packed(rt_model_t *m, rt_signal_t *sig,
                          uint32_t msb_off, uint32_t count,
                          uint64_t a_val, uint64_t b_val, uint32_t src_flags,
                          bool is_force);
void rt_island_vpi_release_slice(rt_model_t *m, rt_signal_t *sig,
                                 uint32_t offset, uint32_t count);
void rt_island_report_forces(rt_island_mgr_t *mgr, rt_signal_t *sig,
                             uint32_t offset, uint32_t count,
                             void (*fn)(uint32_t, uint32_t, void *),
                             void *arg);

void *rt_island_begin(rt_model_t *m, rt_island_mgr_t **mgr_p,
                      jit_handle_t eval_handle, void *inst_ctx,
                      uint32_t state_bytes, uint32_t nsigs,
                      uint32_t ndomains);
void rt_island_bind_sig(void *handle, uint32_t idx, sig_shared_t *ss,
                        int32_t sig_offset, uint32_t a_off, uint32_t b_off,
                        uint32_t width, uint32_t nelems, uint32_t flags);
void rt_island_add_domain(void *handle, uint32_t dom_idx,
                          jit_handle_t ff_handle, void *inst_ctx,
                          uint32_t clk_sig_idx, int32_t rst_sig_idx,
                          uint32_t edge_flags, uint32_t n_write_sigs);
void rt_island_add_domain_write(void *handle, uint32_t dom_idx,
                                uint32_t sig_idx);
void rt_island_finish(void *handle);

// Runtime hooks provided by model.c
void rt_island_deposit_packed(rt_model_t *m, rt_signal_t *s, uint32_t msb_off,
                              uint32_t count, uint64_t a_val, uint64_t b_val,
                              uint32_t src_flags);
void rt_island_request_delta(rt_model_t *m);
jit_t *rt_island_model_jit(rt_model_t *m);
rt_island_mgr_t *rt_island_model_mgr(rt_model_t *m);

#endif  // _RT_ISLAND_H
