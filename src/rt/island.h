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
#include "jit/jit.h"
#include "rt/defs.h"
#include "rt/model.h"

#include <stdint.h>

typedef enum {
   RT_ISIG_IN             = (1u << 0),
   RT_ISIG_OUT            = (1u << 1),
   RT_ISIG_TWO_STATE      = (1u << 2),
   RT_ISIG_IS_NET         = (1u << 3),
   RT_ISIG_COMB_WRITTEN   = (1u << 4),
   RT_ISIG_COMB_READ      = (1u << 5),
   RT_ISIG_FF_WRITTEN     = (1u << 6),
   RT_ISIG_OBSERVED       = (1u << 7),
   RT_ISIG_DIRECT_LINK    = (1u << 8),
   RT_ISIG_STD_LOGIC9     = (1u << 9),
   RT_ISIG_STD_LOGIC_ELEM = (1u << 10),
   RT_ISIG_VHDL_SCHED     = (1u << 11),
   RT_ISIG_VHDL           = (1u << 12),
   RT_ISIG_SOLE_OWNER     = (1u << 13),
   RT_ISIG_LAST_OUT_VALID = (1u << 14),
   RT_ISIG_PRUNED         = (1u << 15),
   RT_ISIG_SPARSE_RAM     = (1u << 16),
   RT_ISIG_PROC_COMB      = (1u << 17),   // Written by always_comb/always @*
   RT_ISIG_RESET_LATE     = (1u << 18),   // Not settled before time zero
} rt_island_sig_flags_t;

#define RT_IEDGE_CLK_POSEDGE  (1u << 0)
#define RT_IEDGE_RST_POSEDGE  (1u << 1)
#define RT_IEDGE_VHDL         (1u << 2)
#define RT_IEDGE_VHDL_EVENT   (1u << 3)
#define RT_IEDGE_RST_LEVEL    (1u << 4)

#define RT_IFF_STD_LOGIC9   (1u << 0)
#define RT_IFF_TWO_STATE    (1u << 1)
#define RT_IFF_COMB_READ    (1u << 2)
#define RT_IFF_DIRECT_LINK  (1u << 3)
#define RT_IFF_SYNC_OUT     (1u << 4)
#define RT_IFF_SPARSE_RAM   (1u << 5)
#define RT_IFF_HAS_NOTIFY   (RT_IFF_DIRECT_LINK | RT_IFF_SYNC_OUT)

#define RT_ISLAND_SPARSE_INLINE_CAP   16u
#define RT_ISLAND_SPARSE_INLINE_DATA  1024u

typedef struct {
   uint64_t sig_and_off;   // low32 = sig_idx, high32 = byte_off
   uint64_t len_and_data;  // low32 = len,     high32 = data_off
} rt_island_sparse_entry_t;

typedef struct {
   uint64_t                 count;
   uint64_t                 data_used;
   uint64_t                 isl_ptr;
   uint64_t                 pad;
   rt_island_sparse_entry_t entries[RT_ISLAND_SPARSE_INLINE_CAP];
   uint8_t                  data[RT_ISLAND_SPARSE_INLINE_DATA];
} rt_island_sparse_log_t;

typedef struct {
   uint32_t a_off;
   uint32_t count;      // STD_LOGIC9: total bytes; 2/4-state: nwords * nelems
   uint32_t sig_idx;
   uint16_t flags;      // RT_IFF_*
   uint16_t b_delta;    // 4-state: (b_off - a_off) >> 3 if < 0xFFFF, else 0xFFFF
} rt_island_ff_desc_t;

#define RT_IDIR_STD_LOGIC9  (1u << 0)
#define RT_IDIR_TWO_STATE   (1u << 1)

typedef struct {
   const uint8_t     *src_a;
   uint8_t           *dst_a;
   const uint8_t     *src_b;
   uint8_t           *dst_b;
   struct _rt_island *dst_isl;
   uint32_t           count;    // STD_LOGIC9: total bytes; 2/4-state: nwords * nelems
   uint32_t           flags;    // RT_IDIR_*
} rt_island_direct_desc_t;

typedef struct {
   const uint8_t *src;
   uint8_t       *dst;
   uint32_t       nbytes;
   uint32_t       sig_idx;
} rt_island_in_desc_t;

typedef struct {
   const uint8_t *src;
   const uint8_t *cur;
   uint32_t       nbytes;
   uint32_t       sig_idx;
} rt_island_ext_out_desc_t;

typedef struct _rt_super_island rt_super_island_t;
typedef struct _rt_super_domain rt_super_domain_t;

#define RT_ISLAND_STRIDE(bytes) ALIGN_UP(MAX(64u, (bytes)), 64u)

typedef struct {
   uint32_t  sig_idx;
   uint64_t *mask;
   uint64_t *a;
   uint64_t *b;
   uint64_t *saved_a;
   uint64_t *saved_b;
} rt_island_force_t;

typedef struct _rt_island_alias {
   rt_signal_t             *sig;
   struct _rt_island_sig   *src_s;
   int32_t                  sig_offset;
   struct _rt_island_alias *next;
} rt_island_alias_t;

typedef struct _rt_island_sig {
   rt_signal_t            *sig;
   struct _rt_island      *isl;
   uint32_t                sig_idx;
   uint32_t                in_idx;
   struct _rt_island_sig  *next_owner;
   struct _rt_island_sig **direct_targets;
   struct _rt_island_sig  *direct_src;
   uint32_t                n_direct_targets;
   rt_island_alias_t      *direct_aliases;
   uint32_t                n_direct_aliases;
   int32_t                 sig_offset;
   uint32_t                a_off;
   uint32_t                b_off;
   uint32_t                width;
   uint32_t                nelems;
   uint32_t                nwords;
   uint32_t                flags;
   uint8_t                 elem_size;
   rt_watch_t             *watch;
   rt_watch_t             *ext_write_watch;
   uint64_t                last_out_a;
   uint64_t                last_out_b;
   uint64_t               *last_out_words;
   uint64_t                gen_key_hash;
} rt_island_sig_t;

typedef struct _rt_island_domain {
   struct _rt_island        *isl;
   rt_super_domain_t        *super_dom;
   struct _rt_island_domain *ff_batch_next;
   uint32_t                  dom_idx;
   jit_handle_t              ff_handle;
   void                     *ff_func;
   jit_fast_entry_fn_t       ff_fn;
   uint32_t                  clk_sig_idx;
   int32_t                   rst_sig_idx;
   bool                      clk_posedge;
   bool                      rst_posedge;
   uint8_t                   edge_flags;
   uint8_t                   prev_clk;
   uint8_t                   prev_rst;
   bool                      in_sample_q;
   bool                      in_commit_q;
   bool                      needs_presync;
   bool                      reset_clk_edge;
   bool                      reset_rst_edge;
   uint32_t                 *write_sigs;
   rt_island_ff_desc_t      *ff_descs;
   uint32_t                  n_write_sigs;
   uint32_t                  cap_write_sigs;
   uint64_t                  ff_dirty_mask;
   rt_watch_t               *clk_watch;
   rt_watch_t               *rst_watch;
} rt_island_domain_t;

typedef struct _rt_island {
   rt_model_t            *model;
   struct _rt_island_mgr *mgr;
   rt_scope_t            *root_scope;
   rt_super_island_t     *cluster;
   uint32_t               isl_id;
   uint32_t               topo_pos;
   uint8_t               *state;
   uint8_t               *next_state;
   uint32_t               state_bytes;
   uint32_t               total_state_bytes;
   uint32_t               sparse_log_off;
   rt_island_sparse_entry_t *sparse_ovf_entries;
   uint32_t               n_sparse_ovf;
   uint32_t               cap_sparse_ovf;
   uint8_t               *sparse_ovf_data;
   uint32_t               sparse_ovf_data_used;
   uint32_t               cap_sparse_ovf_data;
   jit_handle_t           eval_handle;
   void                  *eval_func;
   jit_fast_entry_fn_t    eval_fn;
   void                  *inst_ctx;
   rt_island_sig_t       *sigs;
   uint32_t               nsigs;
   rt_island_domain_t    *domains;
   uint32_t               ndomains;
   uint32_t                *in_sigs;
   rt_island_in_desc_t     *in_descs;
   uint32_t                 n_in_sigs;
   uint64_t                 dirty_in_mask;
   uint64_t                *dirty_in_words;
   uint32_t                 dirty_in_nwords;
   uint32_t                *out_sigs;
   uint32_t                 n_out_sigs;
   uint32_t                 *ext_out_sigs;
   uint32_t                  n_ext_out_sigs;
   rt_island_ext_out_desc_t *ext_out_descs;
   uint32_t                  n_ext_out_descs;
   uint32_t                *direct_comb_outs;
   uint32_t                 n_direct_comb_outs;
   rt_island_direct_desc_t *direct_comb_descs;
   uint32_t                 n_direct_comb_descs;
   uint32_t              *obs_sigs;
   uint32_t               n_obs_sigs;
   uint32_t               cap_obs_sigs;
   rt_island_force_t     *forces;
   uint32_t               n_forces;
   uint32_t               cap_forces;
   uint64_t              *scratch;
   uint32_t               scratch_words;
   uint32_t               dirty_gen;
   rt_island_domain_t    *ff_pending_head;
   rt_island_domain_t    *ff_pending_tail;
   struct _rt_island    **comb_deps;
   uint32_t               n_comb_deps;
   uint32_t               mt_wave_tag;
   uint32_t               pending_vhdl_ff;
   uint32_t              *fb_sigs;
   uint32_t               n_fb_sigs;
   uint8_t               *fb_shadow;
   bool                   dirty;
   bool                   in_dirty_list;
   bool                   in_defer_dirty;
   bool                   in_defer_direct;
   bool                   mt_fb_changed;
   bool                   mt_defer_direct;
   bool                   has_dirty_inputs;
   bool                   in_cl_dirty_in;
   bool                   initialized;
   bool                   has_forces;
   bool                   has_reset_late;
   bool                   need_sync_out;
   bool                   mt_eval_on_main;
   bool                   mt_ff_unsafe;
   bool                   is_vhdl;
} rt_island_t;

typedef struct _rt_island_mgr rt_island_mgr_t;
typedef struct _sva sva_t;

typedef struct {
   sva_t     *sva;
   uint64_t   leaves;
   int        disable;
   rt_proc_t *proc;
} rt_sva_batch_item_t;

rt_island_mgr_t *rt_island_mgr_new(rt_model_t *m);
void rt_island_mgr_free(rt_island_mgr_t *mgr);
void rt_island_reset_all(rt_island_mgr_t *mgr);
bool rt_island_eval_sv_reset(jit_t *jit, rt_model_t *m, rt_island_mgr_t *mgr,
                             tlab_t *tlab, bool first_pass, bool *progress);
void rt_island_mark_sv_reset_edges(rt_island_mgr_t *mgr);
bool rt_island_has_dirty(const rt_island_mgr_t *mgr);
bool rt_island_has_sv_dirty(const rt_island_mgr_t *mgr);
bool rt_island_eval_dirty(jit_t *jit, rt_model_t *m, rt_island_mgr_t *mgr,
                          tlab_t *tlab);
bool rt_island_eval_sv_dirty(jit_t *jit, rt_model_t *m, rt_island_mgr_t *mgr,
                             tlab_t *tlab);
bool rt_island_has_ff_sample(const rt_island_mgr_t *mgr);
bool rt_island_eval_ff(jit_t *jit, rt_model_t *m, rt_island_mgr_t *mgr,
                       tlab_t *tlab);
bool rt_island_has_ff_commits(const rt_island_mgr_t *mgr);
void rt_island_commit_ff(rt_model_t *m, rt_island_mgr_t *mgr, tlab_t *tlab);
uint32_t rt_island_eval_sva_batch(jit_t *jit, rt_model_t *m,
                                  rt_island_mgr_t **mgr_p,
                                  rt_sva_batch_item_t *items, uint32_t count,
                                  tlab_t *tlab);

bool rt_island_read_sig(rt_island_mgr_t *mgr, rt_signal_t *sig,
                        uint64_t *a_out, uint64_t *b_out,
                        uint32_t *width_out, bool *two_state_out);
void rt_island_sync_to_signal(rt_model_t *m, rt_signal_t *sig);
void rt_island_sync_if_unobserved(rt_model_t *m, rt_signal_t *sig);
void rt_island_sync_slice_to_signal(rt_model_t *m, rt_signal_t *sig,
                                    uint32_t offset, uint32_t count);
void rt_island_on_observer_attached(rt_model_t *m, rt_signal_t *sig);
bool rt_island_is_input_watch(const rt_watch_t *w);
void rt_island_input_watch_inline(rt_model_t *m, void *user, bool defer_comb);
void rt_island_promote_deferred(rt_island_mgr_t *mgr);
void rt_island_on_ext_deposit(rt_model_t *m, rt_signal_t *sig);
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
bool rt_island_vpi_release_slice(rt_model_t *m, rt_signal_t *sig,
                                 uint32_t offset, uint32_t count);
void rt_island_report_forces(rt_island_mgr_t *mgr, rt_signal_t *sig,
                             uint32_t offset, uint32_t count,
                             void (*fn)(uint32_t, uint32_t, void *),
                             void *arg);
bool rt_island_is_forced(rt_island_mgr_t *mgr, rt_signal_t *sig,
                         uint32_t offset, uint32_t count);

void *rt_island_begin(rt_model_t *m, rt_island_mgr_t **mgr_p,
                      jit_handle_t eval_handle, void *inst_ctx,
                      uint32_t state_bytes, uint32_t nsigs,
                      uint32_t ndomains);
void rt_island_set_sparse(void *handle, uint32_t sparse_log_off,
                          uint32_t total_state_bytes);
void rt_island_sparse_write(void *isl_ptr, uint32_t sig_idx,
                            uint32_t byte_off, uint32_t len,
                            const uint8_t *data);
void rt_island_bind_sig(void *handle, uint32_t idx, sig_shared_t *ss,
                        int32_t sig_offset, uint32_t a_off, uint32_t b_off,
                        uint32_t width, uint32_t nelems, uint32_t flags);
void rt_island_set_gen_key(void *handle, uint32_t idx, uint64_t key_hash);
void rt_island_attach_sig(void *handle, uint32_t idx, sig_shared_t *ss,
                          int32_t sig_offset);
void rt_island_apply_forces(void *handle);
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
void rt_island_deposit_unowned_packed(rt_model_t *m, rt_signal_t *s,
                                      uint32_t msb_off, uint32_t count,
                                      uint64_t a_val, uint64_t b_val,
                                      uint32_t src_flags);
void rt_island_force_unowned_packed(rt_model_t *m, rt_signal_t *s,
                                    uint32_t msb_off, uint32_t count,
                                    uint64_t a_val, uint64_t b_val,
                                    uint32_t src_flags);
void rt_island_force_unowned_bytes(rt_model_t *m, rt_signal_t *s,
                                   const void *values, int offset,
                                   size_t count);
void rt_island_request_delta(rt_model_t *m);
jit_t *rt_island_model_jit(rt_model_t *m);
rt_island_mgr_t *rt_island_model_mgr(rt_model_t *m);
rt_scope_t *rt_island_model_active_scope(rt_model_t *m);

#endif  // _RT_ISLAND_H
