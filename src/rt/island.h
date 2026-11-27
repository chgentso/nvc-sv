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
   RT_ISIG_ACT            = (1u << 19),   // Change byte read by VHDL comb
   RT_ISIG_SPEC_A         = (1u << 20),   // Island runs its Mode A variant
   RT_ISIG_SPEC_MIX       = (1u << 21),   // A direct target differs in layout
   RT_ISIG_SPEC_HELD      = (1u << 22),   // Mode A: kept as Mode B bytes
   RT_ISIG_SPEC_ROWF      = (1u << 23),   // Mode A layout: row clean bytes
   RT_ISIG_SPEC_ROWS      = (1u << 24),   // Mode A: some rows may be held
   RT_ISIG_MIX_MEMO       = (1u << 25),   // mix_memo is valid
   RT_ISIG_TOUCH_LOG      = (1u << 26),   // SV memory: element write log
} rt_island_sig_flags_t;

// Speculative Mode A clean tracking (NVC_ISLAND_SPEC).  The Mode A state of
// an island has one "held" byte per island signal at
// RT_ISLAND_SPEC_HELD_OFF(hdr_bytes): a packed signal whose value is not all
// '0'/'1' is held, its exact Mode B bytes kept at its x_off in the same
// state half (rt_island_bind_sig_a) and its packed words unused.  The Mode A
// functions test the held byte before every load or store of a packed
// signal and return RT_ISLAND_DEOPT_UNCLEAN when it is set.  A 2D signal
// with RT_ISIG_SPEC_ROWF has one byte per row after its packed rows (in each
// state half, rounded up to 8 bytes): a set byte means the row is held (its
// Mode B bytes at x_off + row * width); loads of the row and partial stores
// into it return RT_ISLAND_DEOPT_ROW, a store of the whole row clears it.
// A sparse RAM keeps its row bytes after its packed rows in its own area.
// A flip-flop function storing a whole value into a held signal writes
// its packed next value and sets the signal's request byte (at
// RT_ISLAND_SPEC_REQ_OFF): the commit packs the signal again.

#define RT_ISLAND_SPEC_HELD_OFF(hdr)   ALIGN_UP((hdr), 8u)
#define RT_ISLAND_SPEC_REQ_OFF(hdr, n) (RT_ISLAND_SPEC_HELD_OFF(hdr) \
                                        + ALIGN_UP((n), 8u))
#define RT_ISLAND_SPEC_ROWF_BYTES(n)   ALIGN_UP((n), 8u)

// Process activity of VHDL island combinational functions: the state of a
// VHDL island with combinational processes starts with one change byte per
// island signal, then a byte that makes every process run, then two 64-bit
// counters (processes run, processes visited) that the comb function only
// updates when NVC_ISLAND_ACT_STATS was set when it was lowered.  A process
// runs when the run-all byte or the change byte of a signal it reads is
// set; a process that reads a signal without RT_ISIG_ACT always runs.  The
// runtime sets the change byte wherever it writes a new value into the
// island state (island_act_mark) and clears them after each call.
#define RT_ISLAND_ACT_ALL(nsigs)    (nsigs)
#define RT_ISLAND_ACT_CNT(nsigs)    ALIGN_UP((nsigs) + 1u, 8u)
#define RT_ISLAND_ACT_BYTES(nsigs)  (RT_ISLAND_ACT_CNT(nsigs) + 16u)

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
#define RT_IFF_ACT          (1u << 6)
#define RT_IFF_TOUCH_LOG    (1u << 7)
#define RT_IFF_HAS_NOTIFY   (RT_IFF_DIRECT_LINK | RT_IFF_SYNC_OUT)

// Element write log of a SystemVerilog island memory (RT_ISIG_TOUCH_LOG):
// the flip-flop functions append the index of each element they store to a
// log that follows the planes of the signal in the next-state half, so the
// commit compares and copies only those elements instead of the whole
// memory.  The log is a count followed by RT_ISLAND_TOUCH_CAP entries and a
// spare slot; a count above RT_ISLAND_TOUCH_CAP means the log overflowed
// (or a store was not to a single element) and the commit compares the
// whole signal.  The commit resets the count.
#define RT_ISLAND_TOUCH_CAP        16u
#define RT_ISLAND_TOUCH_MIN_ELEMS  16u
#define RT_ISLAND_TOUCH_BYTES      ((RT_ISLAND_TOUCH_CAP + 2u) * 8u)

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
#define RT_IDIR_ACT         (1u << 2)   // Set the change byte (flags >> 8)
#define RT_IDIR_MIX         (1u << 3)   // With STD_LOGIC9: Mode A <-> Mode B
#define RT_IDIR_IDX_SHIFT   8u

typedef struct {
   const uint8_t     *src_a;
   uint8_t           *dst_a;
   union {
      const uint8_t                *src_b;
      const struct _rt_island_sig  *mix_src;   // RT_IDIR_MIX
   };
   union {
      uint8_t                      *dst_b;
      struct _rt_island_sig        *mix_dst;   // RT_IDIR_MIX
   };
   struct _rt_island *dst_isl;
   uint32_t           count;    // STD_LOGIC9: total bytes; 2/4-state: nwords * nelems
   uint32_t           flags;    // RT_IDIR_*, RT_IDIR_ACT index in bits 8+
} rt_island_direct_desc_t;

typedef struct {
   const uint8_t *src;
   uint8_t       *dst;
   uint32_t       nbytes;
   uint32_t       sig_idx;
} rt_island_in_desc_t;

typedef struct {
   const uint8_t  *src;
   const uint8_t  *cur;
   const uint64_t *word;   // Packed one-word output with a memo (Mode A)
   uint32_t        nbytes;
   uint32_t        sig_idx;
} rt_island_ext_out_desc_t;

typedef struct _rt_island_spec rt_island_spec_t;
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

typedef struct _rt_island_act {
   uint32_t off;      // Byte offset of the signal value, 8-byte aligned
   uint32_t nwords;   // Value size in 64-bit words including padding
   uint32_t idx;      // Island signal index (change byte)
} rt_island_act_t;

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
   uint64_t                mix_memo;   // Packed source word last unpacked
                                       // into this direct target
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
   bool                      spec_held_w;   // Writes a held signal
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
   rt_island_spec_t      *spec;     // Speculative Mode A variant or NULL
   bool                   spec_a;   // Running the Mode A variant now
   bool                   spec_held;   // Mode A with held signals
   bool                   act_on;       // Comb function tests change bytes
   bool                   direct_act;   // A direct comb desc has RT_IDIR_ACT
} rt_island_t;

// Speculative Mode A (NVC_ISLAND_SPEC): a VHDL island lowered in Mode B
// can also carry a packed two-state variant of its functions.  The variant
// returns one of these codes instead of computing a value that two-state
// storage cannot hold (the comb function returns 0 and a flip-flop function
// a mask with bit 63 set when it completed).  It writes nothing visible
// before it returns a code: the caller converts the state back with
// rt_island_spec_unpack and runs the Mode B function instead.
typedef enum {
   RT_ISLAND_DEOPT_NONE    = 0,
   RT_ISLAND_DEOPT_LITERAL = 1,   // std_logic literal other than '0'/'1'
   RT_ISLAND_DEOPT_RANGE   = 2,   // failing range check or zero divisor
   RT_ISLAND_DEOPT_CONV    = 3,   // integer conversion out of range
   RT_ISLAND_DEOPT_REPORT  = 4,   // report statement or failing assertion
   RT_ISLAND_DEOPT_STATE   = 5,   // (runtime) state not all '0'/'1'
   RT_ISLAND_DEOPT_INPUT   = 6,   // (runtime) input value not '0'/'1'
   RT_ISLAND_DEOPT_PUT     = 7,   // (runtime) force, deposit or release
   RT_ISLAND_DEOPT_STRESS  = 8,   // (runtime) NVC_ISLAND_SPEC_STRESS
   RT_ISLAND_DEOPT_UNCLEAN = 9,   // load or store of a held signal
   RT_ISLAND_DEOPT_ROW     = 10,  // load or partial store of a held row
   RT_ISLAND_DEOPT_NOVAR   = 11,  // Mode A function could not be lowered
   RT_ISLAND_DEOPT_COUNT
} rt_island_deopt_t;

struct _rt_island_spec {
   jit_handle_t  eval_handle;     // Mode A comb function
   jit_handle_t *ff_handles;      // Mode A flip-flop function per domain
   jit_handle_t  eval_handle_h;   // The same testing held bytes ("$ah")
   jit_handle_t *ff_handles_h;
   bool          checks;          // Running the "$ah" functions
   uint32_t      state_bytes;     // Mode A state size (one half)
   uint32_t      stride;          // RT_ISLAND_STRIDE(state_bytes)
   uint32_t      hdr_bytes;       // Leading bytes copied unchanged
   uint32_t     *a_off;           // Mode A offset per island signal
   uint32_t     *a_flags;         // Mode A RT_ISIG_* flags per signal
   uint32_t     *b_bytes;         // Mode B storage bytes per signal
   uint8_t      *buf;             // NVC_ISLAND_SPEC_FORCE=A: state + next
   bool          force_a;
   uint64_t      calls_a;
   uint64_t      deopts[RT_ISLAND_DEOPT_COUNT];
   // Run-time switching between the variants on the one live state (see
   // "Speculative Mode A at run time" in island.c)
   jit_handle_t  eval_handle_b;   // Mode B comb function
   jit_handle_t *ff_handles_b;    // Mode B flip-flop function per domain
   uint32_t     *b_off;           // Mode B offset per island signal
   uint32_t      stride_b;        // Mode B state half
   bool          pinned;          // Stays Mode B for the rest of the run
   uint8_t       pin_reason;      // rt_island_deopt_t
   uint32_t      n_deopt;         // Deopts so far (back-off)
   uint32_t      n_report;        // Report deopts so far
   uint32_t      check_ival;      // Promotion check interval (ticks)
   uint64_t      next_check;      // Tick of the next promotion check
   uint64_t      tick_promoted;   // Tick of the last promotion
   uint64_t      ticks_a;         // Ticks spent in Mode A
   uint64_t      promotions;
   uint64_t      failed_checks;
   // Clean tracking (RT_ISLAND_SPEC_HELD_OFF)
   uint32_t     *x_off;           // Mode B bytes of a held packed signal
   uint32_t      sparse_log_off;  // Mode A sparse log (0: no sparse RAM)
   uint32_t      sparse_log_off_b;
   uint32_t      total_bytes;     // Mode A state including sparse RAMs
   uint32_t      total_bytes_b;   // Mode B state including sparse RAMs
   uint32_t      n_unclean;       // Unclean read deopts so far (back-off)
   uint32_t      n_held;          // Held signals now (Mode A)
   uint32_t     *held_list;       // Their indices (island_spec_rebuild)
   uint32_t      n_held_list;
   uint32_t      n_held_rows;     // Held rows at the last promotion
   uint64_t      holds;           // Signals held after promotion
   uint64_t      unholds;
   // Cost of promotion (island_spec_try_promote, island_spec_demote)
   uint64_t      unclean_since;   // Tick+1 of the first check with held
                                  // values since the last clean one
   uint32_t      n_quick;         // Successive deopts soon after promotion
   bool          cost_pinned;     // Retried only every ISLAND_SPEC_COST_RETRY
   uint64_t      cost_pins;
   uint64_t      hold_waits;      // Checks that waited before holding
};

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
void rt_island_set_variant_a(void *handle, jit_handle_t eval_handle,
                             jit_handle_t eval_handle_h,
                             uint32_t state_bytes, uint32_t hdr_bytes,
                             uint32_t sparse_log_off, uint32_t total_bytes);
void rt_island_bind_sig_a(void *handle, uint32_t idx, uint32_t a_off,
                          uint32_t a_flags, uint32_t b_bytes, uint32_t x_off);
void rt_island_add_domain_a(void *handle, uint32_t dom_idx,
                            jit_handle_t ff_handle, jit_handle_t ff_handle_h);
bool rt_island_spec_pack(const rt_island_t *isl, const uint8_t *b_state,
                         uint8_t *a_state);
void rt_island_spec_unpack(const rt_island_t *isl, const uint8_t *a_state,
                           uint8_t *b_state);

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
