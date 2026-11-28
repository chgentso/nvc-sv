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
#include "ident.h"
#include "jit/jit.h"
#include "object.h"
#include "option.h"
#include "rt/island.h"
#include "rt/model.h"
#include "rt/mspace.h"
#include "rt/structs.h"
#include "rt/sva.h"
#include "thread.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __MINGW32__
#include <malloc.h>
#endif

#ifdef HAVE_PTHREAD
#include <pthread.h>
#elif defined __MINGW32__
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#error missing pthread or win32 thread support
#endif

#define RT_ISLAND_MT_FF_THRESHOLD    16u
#define RT_ISLAND_MT_DIRTY_THRESHOLD 8u
#define RT_ISLAND_MT_MIN_BATCH_NS    25000u
#define RT_SVA_MT_THRESHOLD          8u
#define RT_SVA_MT_MIN_BATCH_NS       12000u
#define RT_ISLAND_SPIN_ITERS         3000

STATIC_ASSERT(sizeof(rt_island_ff_desc_t) == 16);

typedef struct {
   uint32_t next_probe;
   uint32_t probes_done;
   bool     use_mt;
} rt_island_mt_gate_t;

static inline bool island_mt_gate_should_run(rt_island_mt_gate_t *g,
                                             bool *do_probe)
{
   if (g->next_probe == 0) {
      *do_probe = true;
      return false;
   }
   g->next_probe--;
   *do_probe = false;
   return g->use_mt;
}

static inline void island_mt_gate_record(rt_island_mt_gate_t *g,
                                         uint64_t dt_ns)
{
   g->use_mt = (dt_ns >= RT_ISLAND_MT_MIN_BATCH_NS);
   g->probes_done++;
   if (g->probes_done == 1)
      g->next_probe = 64u;
   else if (g->probes_done == 2)
      g->next_probe = 128u;
   else if (g->probes_done == 3)
      g->next_probe = 256u;
   else
      g->next_probe = 2048u;
}

#ifdef __MINGW32__
typedef CRITICAL_SECTION   island_mutex_t;
typedef CONDITION_VARIABLE island_cond_t;

static inline void island_mutex_init(island_mutex_t *m)
{
   InitializeCriticalSection(m);
}

static inline void island_mutex_destroy(island_mutex_t *m)
{
   DeleteCriticalSection(m);
}

static inline void island_mutex_lock(island_mutex_t *m)
{
   EnterCriticalSection(m);
}

static inline void island_mutex_unlock(island_mutex_t *m)
{
   LeaveCriticalSection(m);
}

static inline void island_cond_init(island_cond_t *c)
{
   InitializeConditionVariable(c);
}

static inline void island_cond_destroy(island_cond_t *c)
{
   (void)c;
}

static inline void island_cond_wait(island_cond_t *c, island_mutex_t *m)
{
   SleepConditionVariableCS(c, m, INFINITE);
}

static inline void island_cond_broadcast(island_cond_t *c)
{
   WakeAllConditionVariable(c);
}
#else
typedef pthread_mutex_t island_mutex_t;
typedef pthread_cond_t  island_cond_t;

static inline void island_mutex_init(island_mutex_t *m)
{
   pthread_mutex_init(m, NULL);
}

static inline void island_mutex_destroy(island_mutex_t *m)
{
   pthread_mutex_destroy(m);
}

static inline void island_mutex_lock(island_mutex_t *m)
{
   pthread_mutex_lock(m);
}

static inline void island_mutex_unlock(island_mutex_t *m)
{
   pthread_mutex_unlock(m);
}

static inline void island_cond_init(island_cond_t *c)
{
   pthread_cond_init(c, NULL);
}

static inline void island_cond_destroy(island_cond_t *c)
{
   pthread_cond_destroy(c);
}

static inline void island_cond_wait(island_cond_t *c, island_mutex_t *m)
{
   pthread_cond_wait(c, m);
}

static inline void island_cond_broadcast(island_cond_t *c)
{
   pthread_cond_broadcast(c);
}
#endif

typedef enum {
   ISLAND_TASK_NONE = 0,
   ISLAND_TASK_EVAL_FF,
   ISLAND_TASK_EVAL_DIRTY_CLUSTERS,
   ISLAND_TASK_EVAL_DIRTY_ISLANDS,
   ISLAND_TASK_EVAL_SVA,
} rt_island_task_kind_t;

typedef struct _rt_island_pool rt_island_pool_t;

typedef struct {
   nvc_thread_t     *thread;
   rt_island_pool_t *pool;
   tlab_t           *tlab;
   uint32_t          idx;
} __attribute__((aligned(64))) rt_island_worker_t;

struct _rt_island_pool {
   jit_t                *jit;
   rt_model_t           *model;
   rt_island_mgr_t      *mgr;
   uint32_t              nthreads;
   uint32_t              nworkers;
   rt_island_worker_t   *workers;
   island_mutex_t        park_mtx;
   island_cond_t         park_cv;
   uint64_t              epoch __attribute__((aligned(64)));
   bool                  stop;
   rt_island_task_kind_t task_kind;
   void *const          *items;
   uint32_t              n_items;
   uint32_t              chunk;
   uint32_t              n_parked __attribute__((aligned(64)));
   uint32_t              done_count __attribute__((aligned(64)));
   uint32_t              next_idx __attribute__((aligned(64)));
   uint32_t              failed __attribute__((aligned(64)));
};

struct _rt_super_island {
   uint32_t            id;
   rt_island_t       **islands;
   uint32_t            n_islands;
   rt_island_t       **dirty_in_list;
   uint32_t            n_dirty_in;
   uint32_t            min_dirty_pos;
   uint32_t            dirty_count;
   rt_super_island_t **comb_deps;
   uint32_t            n_comb_deps;
   uint32_t            mt_wave_tag;
   uint32_t            mt_sync_start;
   uint32_t            mt_sync_end;
   uint32_t            pending_vhdl_ff;
   bool                has_dirty;
   bool                has_dirty_inputs;
   bool                mt_eval_on_main;
   bool                mt_requeue;
   bool                is_vhdl;
};

typedef struct {
   void                *inst_ctx;
   uint8_t             *state;
   void                *ff_func;
   jit_fast_entry_fn_t  ff_fn;
   rt_island_domain_t  *dom;
   uint64_t             dirty_mask;
} rt_sdom_ff_entry_t;

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
   bool                 reset_clk_edge;
   bool                 reset_rst_edge;
   bool                 used_fast_sample;
   rt_watch_t          *clk_watch;
   rt_watch_t          *rst_watch;
   rt_island_domain_t **doms;
   rt_sdom_ff_entry_t  *ff_entries;
   uint32_t             n_doms;
   rt_super_island_t  **clusters;
   uint32_t             n_clusters;
   rt_island_t        **solo_islands;
   uint32_t             n_solo_islands;
};

// A change of island state made by a VHDL force or release that the island
// processes must only see from the next delta cycle on
typedef struct {
   rt_island_t *isl;
   uint32_t     a_off;
   uint32_t     b_off;
   uint32_t     bytes;
   bool         has_b;
   uint8_t     *buf;   // Changed bits then new values, for a then b
} island_defer_put_t;

struct _rt_island_mgr {
   rt_model_t             *model;
   hash_t                 *sig_map;
   hash_t                 *alias_map;
   A(rt_island_t *)        islands;
   A(rt_island_t *)        dirty_q;
   A(rt_island_t *)        defer_dirty_q;
   A(rt_island_t *)        defer_direct_q;
   A(island_defer_put_t)   defer_put_q;
   A(rt_island_domain_t *) ff_sample_q;
   A(rt_island_domain_t *) ff_commit_q;
   A(rt_super_island_t *)  clusters;
   A(rt_super_island_t *)  dirty_clusters;
   A(rt_super_domain_t *)  super_domains;
   A(rt_super_domain_t *)  super_sample_q;
   A(rt_super_domain_t *)  super_commit_q;
   rt_island_pool_t       *pool;
   A(rt_island_t *)        mt_ff_islands;
   A(rt_island_t *)        mt_ff_safe_q;
   A(rt_island_t *)        mt_isl_work_q;
   A(rt_island_t *)        mt_isl_wave_q;
   A(rt_island_t *)        mt_isl_rem_q;
   A(rt_island_t *)        mt_isl_safe_q;
   A(rt_super_island_t *)  mt_cl_work_q;
   A(rt_super_island_t *)  mt_cl_wave_q;
   A(rt_super_island_t *)  mt_cl_rem_q;
   A(rt_super_island_t *)  mt_cl_safe_q;
   A(void *)               mt_sva_ptrs;
   rt_island_mt_gate_t     mt_gate_cl;
   rt_island_mt_gate_t     mt_gate_isl;
   rt_island_mt_gate_t     mt_gate_ff;
   rt_island_mt_gate_t     mt_gate_sva;
   uint32_t                mt_wave_tag;
   uint32_t                eval_epoch;
   uint32_t                n_forced_islands;
   bool                    hier_built;
   bool                    reset_done;
   bool                    late_pending;
   bool                    pub_pending;   // Dormant RT_ISIG_DIRECT_PUB
   bool                    mt_deps_built;
   bool                    mt_has_comb_deps;
   bool                    fast;
   bool                    has_vhdl;
   struct island_act_info *act_info;   // Indexed by isl_id, see island_init_act
   uint32_t                n_act_info;
   bool                    spec_on;      // Speculative Mode A switching
   uint64_t                spec_tick;    // Delta cycles seen
   uint64_t                spec_next;    // Tick of the next promotion sweep
   bool                    spec_trace;   // NVC_ISLAND_SPEC_STATS=3
   uint32_t                spec_n_a;     // Islands in Mode A now
   uint32_t                spec_timeline[40];   // spec_n_a at tick 2^k
   uint32_t                spec_held_timeline[40];   // Of them holding
   uint32_t                n_spec_timeline;
};

// Direct-wire diagnostics: NVC_ISLAND_WIRE_STATS=1 prints, at the end of the
// run, why each island-written signal is not a direct inter-island wire and
// how many input wakeups / syncs it causes.  Level 2 also prints the return
// addresses of the callers that attached observers.  Diagnostics only: the
// reason codes are recorded on the side and never influence an island
// decision.

typedef enum {
   WR_NONE,
   WR_DIRECT,           // wired directly
   WR_NO_CONSUMER,      // nothing outside the island reads it
   WR_OBSERVED_PRE,     // observed before the build (wave/VPI/VHPI/force)
   WR_MULTI_OWNER,      // several islands write overlapping slices
   WR_SPARSE_RAM,
   WR_POLL,             // SIG_F_POLL_WATCHED
   WR_EXT_READ,         // read by an unfused process / testbench code
   WR_SRC_NEXUS,        // nexus has other sources (extra driver, resolved)
   WR_PACKED,           // packed signal representation
   WR_SRC_SLICE,        // src nexus not 1:1 with the island signal
   WR_PORT_KIND,        // output chain is not a plain port connection
   WR_PORT_DEPTH,       // port chain deeper than the search limit
   WR_DST_SOURCES,      // destination port has != 1 source
   WR_DST_FORCED,       // destination forced or inout
   WR_DST_SLICE,        // destination port narrower/wider than the source
   WR_DST_RESOLVED,     // destination port resolved
   WR_DST_PACKED,       // destination signal packed
   WR_DST_POLL_EXT,     // destination polled or read by unfused code
   WR_DST_ISLAND_OWNED, // destination port itself written by an island
   WR_DST_NO_READER,    // destination port has no island reader
   WR_READER_PROC,      // a process waits on the signal
   WR_READER_WATCH,     // foreign watch (wave/VPI/VHPI/...)
   WR_READER_SAME_ISL,  // reader is in the writer's island
   WR_READER_SIG,       // reader reads through a different signal object
   WR_READER_OFFSET,    // reader reads a sub-range
   WR_READER_SHAPE,     // reader width/elements/size differ
   WR_READER_REPR,      // two-state / 9-state representation differs
   WR_CYCLE,            // rejected to break a combinational loop
   WR_DIRECT_PUB,       // direct to island readers, published for the others
   WR_MAX
} island_wire_reason_t;

typedef enum {
   WB_NONE,
   WB_EXT_DEPOSIT,
   WB_OBSERVER,
   WB_VPI_PUT,
   WB_ALIAS,
   WB_MAX
} island_wire_cause_t;

static const char *const island_wr_names[WR_MAX] = {
   "unclassified",
   "DIRECT",
   "no-consumer (island-internal)",
   "observed-before-build",
   "multi-owner",
   "sparse-ram",
   "sv-poll-watched",
   "ext-read (unfused reader)",
   "src-nexus-has-sources",
   "packed-signal",
   "src-sliced",
   "port-chain-not-port",
   "port-chain-too-deep",
   "dst-port-sources!=1",
   "dst-port-forced/inout",
   "dst-port-sliced",
   "dst-port-resolved",
   "dst-port-packed",
   "dst-port-poll/ext-read",
   "dst-port-island-owned (forwarding)",
   "dst-port-no-island-reader",
   "reader-is-process",
   "reader-is-foreign-watch",
   "reader-same-island",
   "reader-other-signal-object",
   "reader-sub-range",
   "reader-shape-mismatch",
   "reader-repr-mismatch",
   "comb-cycle",
   "DIRECT+published",
};

static const char *const island_wb_names[WB_MAX] = {
   "", "ext-deposit", "observer-attached", "vpi/vhpi-put", "alias-break",
};

typedef enum {
   RK_NONE,
   RK_ISLAND,    // driven by an island signal (attributed to its class)
   RK_PROC,      // driven by a non-island process
   RK_UNDRIVEN,
   RK_OTHER,
   RK_MAX
} island_reader_kind_t;

typedef struct {
   rt_island_sig_t *s;
   rt_island_sig_t *writer;
   uint8_t          reason;
   uint8_t          cause;
   uint8_t          rd_kind;
   uint64_t         wake;
   uint64_t         sync_in;
   uint64_t         sync_out;   // out-sync routine calls (diff checks)
   uint64_t         deposit;    // deposits actually scheduled
   const rt_signal_t *other_sig;   // offending signal of the reject reason
   ident_t          other_proc;    // offending process of the reject reason
   void            *observer_ra;   // caller that attached the first observer
} island_wstat_t;

// Process activity state of a VHDL island that the hot paths shared with
// Verilog islands never touch (see island_init_act)
typedef struct island_act_info {
   rt_island_act_t *sigs;        // NVC_ISLAND_ACT_CHECK: signals compared
   uint32_t         n_sigs;
   uint8_t         *shadow;      // NVC_ISLAND_ACT_CHECK: state copy
   uint64_t         calls;       // NVC_ISLAND_ACT_STATS counters
   uint64_t         all_calls;
   uint64_t         fb_reruns;
   bool             all;         // Run every process on the next call
   bool             every;       // Run every process on every call
} island_act_info_t;

static inline island_act_info_t *island_act_info(rt_island_t *isl)
{
   assert(isl->act_on && isl->isl_id < isl->mgr->n_act_info);
   return &(isl->mgr->act_info[isl->isl_id]);
}

static bool                   island_wstats_on = false;
static int                    island_wstats_level = 0;
static bool                   island_act_enabled = true;
static bool                   island_act_stats = false;
static bool                   island_act_check = false;
static hash_t                *island_wstats = NULL;
static A(island_wstat_t *)    island_wstat_list = AINIT;
static island_wire_reason_t   island_wire_why = WR_NONE;
static const rt_signal_t     *island_wire_obj_sig = NULL;
static ident_t                island_wire_obj_proc = NULL;
static island_wire_cause_t    island_wb_cur = WB_NONE;
static void                  *island_wb_ra = NULL;
static uint64_t               island_wstat_clk_cb = 0;
static uint64_t               island_wstat_rst_cb = 0;
static uint64_t               island_wstat_sclk_cb = 0;
static uint64_t               island_wstat_srst_cb = 0;

static inline bool island_wr_no(island_wire_reason_t r)
{
   island_wire_why = r;
   return false;
}

static inline bool island_wr_yes(island_wire_reason_t r)
{
   island_wire_why = r;
   return true;
}

static island_wstat_t *island_wstat_get(const rt_island_sig_t *s, bool create)
{
   if (island_wstats == NULL)
      return NULL;
   island_wstat_t *e = hash_get(island_wstats, s);
   if (e == NULL && create) {
      e = xcalloc(sizeof(island_wstat_t));
      e->s = (rt_island_sig_t *)s;
      hash_put(island_wstats, s, e);
      APUSH(island_wstat_list, e);
   }
   return e;
}

static void island_wstat_note(const rt_island_sig_t *s,
                              island_wire_reason_t r)
{
   island_wstat_t *e = island_wstat_get(s, true);
   if (e != NULL) {
      e->reason = r;
      if (r != WR_DIRECT) {
         e->other_sig = island_wire_obj_sig;
         e->other_proc = island_wire_obj_proc;
      }
   }
}

static void island_wstat_cause(const rt_island_sig_t *s,
                               island_wire_cause_t c)
{
   island_wstat_t *e = island_wstat_get(s, true);
   if (e != NULL && e->cause == WB_NONE) {
      e->cause = c;
      if (c == WB_OBSERVER)
         e->observer_ra = island_wb_ra;
   }
}

static void island_wstat_count(const rt_island_sig_t *s, int which)
{
   island_wstat_t *e = island_wstat_get(s, false);
   if (e == NULL)
      return;
   uint64_t *p = which == 0 ? &e->wake : which == 1 ? &e->sync_in
      : which == 2 ? &e->sync_out : &e->deposit;
   __atomic_fetch_add(p, 1, __ATOMIC_RELAXED);
}

static void island_wstat_report(const rt_island_mgr_t *mgr);
static void island_wstat_free(void);

#define WSTAT_COUNT(s, w) do {                          \
      if (unlikely(island_wstats_on))                   \
         island_wstat_count((s), (w));                  \
   } while (0)

// Comb output in ext_out_sigs: not a direct wire, or one that is published
// too (RT_ISIG_DIRECT_PUB)
static inline bool island_sig_published(uint32_t flags)
{
   return !(flags & RT_ISIG_DIRECT_LINK) || (flags & RT_ISIG_DIRECT_PUB);
}

// NVC_ISLAND_DIRECT_PUB=0 turns RT_ISIG_DIRECT_PUB wires off (diagnosis)
static bool island_direct_pub_on = true;

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

static inline void island_clear_dirty(rt_island_t *isl)
{
   if (!isl->dirty)
      return;

   isl->dirty = false;
   if (isl->cluster != NULL) {
      assert(isl->cluster->dirty_count > 0);
      isl->cluster->dirty_count--;
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

bool rt_island_is_input_watch(const rt_watch_t *w)
{
   return w != NULL && w->fn == island_input_watch_cb;
}

static inline bool island_wakeable_is_input_watch(const rt_wakeable_t *w)
{
   if (w == NULL || w->kind != W_WATCH)
      return false;
   const rt_watch_t *watch = container_of(w, rt_watch_t, wakeable);
   return watch->fn == island_input_watch_cb;
}

static island_wire_reason_t island_wake_reason(const rt_wakeable_t *w)
{
   if (w->kind == W_PROC)
      island_wire_obj_proc = container_of(w, rt_proc_t, wakeable)->name;
   return w->kind == W_WATCH ? WR_READER_WATCH : WR_READER_PROC;
}

static inline bool island_pending_has_ext_observer(void *pending)
{
   if (pending == NULL)
      return false;
   if (pointer_tag(pending) == 1) {
      const rt_wakeable_t *w = untag_pointer(pending, rt_wakeable_t);
      if (island_wakeable_is_input_watch(w))
         return false;
      return island_wr_yes(island_wake_reason(w));
   }
   const rt_pending_t *p = untag_pointer(pending, rt_pending_t);
   for (unsigned j = 0; j < p->count; j++) {
      if (p->wake[j] != NULL && !island_wakeable_is_input_watch(p->wake[j]))
         return island_wr_yes(island_wake_reason(p->wake[j]));
   }
   return false;
}

static inline bool island_nexus_overlaps_sig(const rt_nexus_t *nx,
                                             const rt_island_sig_t *s)
{
   if (s->sig->shared.flags & SIG_F_PACKED)
      return true;
   const uint32_t s_lo = (uint32_t)s->sig_offset;
   const uint32_t s_hi = s_lo + s->width * s->nelems;
   const uint32_t n_sz = nx->size > 0 ? nx->size : 1u;
   const uint32_t n_lo = (uint32_t)(nx->offset / n_sz);
   const uint32_t n_hi = n_lo + nx->width;
   return n_hi > s_lo && n_lo < s_hi;
}

static bool island_sig_map_overlaps(const rt_island_mgr_t *mgr,
                                    const rt_signal_t *sig,
                                    uint32_t off, uint32_t width)
{
   for (rt_island_sig_t *own = hash_get(mgr->sig_map, sig);
        own != NULL; own = own->next_owner) {
      const uint32_t o_lo = (uint32_t)own->sig_offset;
      const uint32_t o_hi = o_lo + own->width * own->nelems;
      if (o_hi > off && o_lo < off + width)
         return true;
   }
   return false;
}

static island_wire_reason_t island_reader_mismatch(
   const rt_island_sig_t *src_s, const rt_island_sig_t *dst_s,
   const rt_signal_t *cur_sig, int32_t cur_off, bool exact_sig)
{
   if (dst_s->isl == src_s->isl)
      return WR_READER_SAME_ISL;
   if (exact_sig && dst_s->sig != cur_sig)
      return WR_READER_SIG;
   if (dst_s->sig_offset != cur_off)
      return WR_READER_OFFSET;
   if (dst_s->width != src_s->width
       || dst_s->nelems != src_s->nelems
       || dst_s->elem_size != src_s->elem_size)
      return WR_READER_SHAPE;
   if (((src_s->flags ^ dst_s->flags)
        & (RT_ISIG_TWO_STATE | RT_ISIG_STD_LOGIC9)) != 0)
      return WR_READER_REPR;
   return WR_NONE;
}

static bool island_pending_watch_matches(void *pending,
                                         const rt_island_sig_t *src_s,
                                         const rt_signal_t *cur_sig,
                                         uint32_t cur_off,
                                         bool *found_out)
{
   if (pending == NULL)
      return true;

   if (pointer_tag(pending) == 1) {
      const rt_wakeable_t *w = untag_pointer(pending, rt_wakeable_t);
      if (!island_wakeable_is_input_watch(w))
         return island_wr_no(island_wake_reason(w));
      const rt_watch_t *watch = container_of(w, rt_watch_t, wakeable);
      const rt_island_sig_t *dst_s = watch->user_data;
      island_wire_obj_sig = dst_s->sig;
      const island_wire_reason_t why =
         island_reader_mismatch(src_s, dst_s, cur_sig, (int32_t)cur_off, true);
      if (why != WR_NONE)
         return island_wr_no(why);
      *found_out = true;
      return true;
   }

   const rt_pending_t *p = untag_pointer(pending, rt_pending_t);
   for (unsigned j = 0; j < p->count; j++) {
      const rt_wakeable_t *w = p->wake[j];
      if (w == NULL)
         continue;
      if (!island_wakeable_is_input_watch(w))
         return island_wr_no(island_wake_reason(w));
      const rt_watch_t *watch = container_of(w, rt_watch_t, wakeable);
      const rt_island_sig_t *dst_s = watch->user_data;
      island_wire_obj_sig = dst_s->sig;
      const island_wire_reason_t why =
         island_reader_mismatch(src_s, dst_s, cur_sig, (int32_t)cur_off, true);
      if (why != WR_NONE)
         return island_wr_no(why);
      *found_out = true;
   }
   return true;
}

static bool island_nexus_port_observed(const rt_nexus_t *nx,
                                       const rt_island_sig_t *src_s,
                                       bool *found_watch,
                                       unsigned depth)
{
   if (nx->outputs == NULL)
      return false;
   if (depth >= 4)
      return island_wr_yes(WR_PORT_DEPTH);

   const rt_island_mgr_t *mgr = src_s->isl->mgr;
   const uint32_t total = src_s->width * src_s->nelems;
   if (nx->size != src_s->elem_size || nx->width != total)
      return island_wr_yes(WR_SRC_SLICE);

   for (const rt_source_t *o = nx->outputs; o != NULL; o = o->chain_output) {
      if (o->tag != SOURCE_PORT)
         return island_wr_yes(WR_PORT_KIND);
      const rt_nexus_t *dst_nx = o->u.port.output;
      if (dst_nx == NULL || dst_nx->signal == NULL)
         return island_wr_yes(WR_PORT_KIND);
      island_wire_obj_sig = dst_nx->signal;
      if (dst_nx->n_sources != 1)
         return island_wr_yes(WR_DST_SOURCES);
      if (dst_nx->flags & (NET_F_FORCED | NET_F_INOUT))
         return island_wr_yes(WR_DST_FORCED);
      if (dst_nx->width != nx->width || dst_nx->size != nx->size)
         return island_wr_yes(WR_DST_SLICE);
      const rt_signal_t *dst_sig = dst_nx->signal;
      if (dst_sig->resolution != NULL && !(dst_nx->flags & NET_F_R_IDENT))
         return island_wr_yes(WR_DST_RESOLVED);
      if (dst_sig->shared.flags & SIG_F_PACKED)
         return island_wr_yes(WR_DST_PACKED);
      if (dst_sig->shared.flags & (SIG_F_POLL_WATCHED | SIG_F_EXT_READ))
         return island_wr_yes(WR_DST_POLL_EXT);
      const uint32_t dst_off = (uint32_t)(dst_nx->offset / dst_nx->size);
      if (island_sig_map_overlaps(mgr, dst_sig, dst_off, dst_nx->width))
         return island_wr_yes(WR_DST_ISLAND_OWNED);

      bool local_found = false;
      if (!island_pending_watch_matches(dst_nx->pending, src_s,
                                        dst_sig, dst_off, &local_found))
         return true;   // reason set by the matcher

      bool sub_found = false;
      if (dst_nx->outputs != NULL) {
         if (island_nexus_port_observed(dst_nx, src_s, &sub_found, depth + 1))
            return true;
      }

      if (!local_found && !sub_found)
         return island_wr_yes(WR_DST_NO_READER);
      *found_watch = true;
   }
   return false;
}

static inline bool island_sig_observed(const rt_island_sig_t *s)
{
   const rt_signal_t *sig = s->sig;
   island_wire_obj_sig = NULL;
   island_wire_obj_proc = NULL;
   if (sig->shared.flags & SIG_F_POLL_WATCHED)
      return island_wr_yes(WR_POLL);
   if (sig->shared.flags & SIG_F_EXT_READ)
      return island_wr_yes(WR_EXT_READ);
   const uint32_t total = s->width * s->nelems;
   const rt_nexus_t *nx = &(sig->nexus);
   for (uint32_t i = 0; i < sig->n_nexus; i++, nx = nx->chain) {
      if (!island_nexus_overlaps_sig(nx, s))
         continue;
      if (nx->outputs != NULL) {
         if (sig->shared.flags & SIG_F_PACKED)
            return island_wr_yes(WR_PACKED);
         if (nx->size != s->elem_size
             || nx->width != total
             || (uint32_t)(nx->offset / nx->size) != (uint32_t)s->sig_offset)
            return island_wr_yes(WR_SRC_SLICE);
         bool found_port_watch = false;
         if (island_nexus_port_observed(nx, s, &found_port_watch, 0))
            return true;   // reason set by the port walk
         if (!found_port_watch)
            return island_wr_yes(WR_DST_NO_READER);
      }
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

static void island_refresh_ext_out_descs(rt_island_t *isl)
{
   free(isl->ext_out_descs);
   isl->ext_out_descs = NULL;
   isl->n_ext_out_descs = 0;
   if (isl->n_ext_out_sigs == 0)
      return;

   isl->ext_out_descs = xcalloc_array(isl->n_ext_out_sigs,
                                      sizeof(rt_island_ext_out_desc_t));
   for (uint32_t j = 0; j < isl->n_ext_out_sigs; j++) {
      const uint32_t sidx = isl->ext_out_sigs[j];
      const rt_island_sig_t *os = &(isl->sigs[sidx]);
      rt_island_ext_out_desc_t *ed = &(isl->ext_out_descs[j]);
      ed->sig_idx = sidx;
      if ((os->flags & (RT_ISIG_VHDL | RT_ISIG_STD_LOGIC9 | RT_ISIG_PRUNED))
          == (RT_ISIG_VHDL | RT_ISIG_STD_LOGIC9)
          && os->sig != NULL
          && !(os->sig->shared.flags & SIG_F_PACKED)) {
         const size_t elem_sz = os->elem_size;
         ed->src    = isl->state + os->a_off;
         ed->cur    = os->sig->shared.data + (size_t)os->sig_offset * elem_sz;
         ed->nbytes = os->width * os->nelems * os->elem_size;
      }
      else if ((os->flags & (RT_ISIG_VHDL | RT_ISIG_TWO_STATE
                             | RT_ISIG_STD_LOGIC9 | RT_ISIG_SOLE_OWNER
                             | RT_ISIG_PRUNED))
               == (RT_ISIG_VHDL | RT_ISIG_TWO_STATE | RT_ISIG_SOLE_OWNER)
               && os->nwords * os->nelems == 1
               && isl->mgr->fast && os->sig != NULL
               && !(os->sig->shared.flags & SIG_F_PACKED)) {
         // The static part of island_out_memo_ok holds
         ed->word = (const uint64_t *)(isl->state + os->a_off);
      }
   }
   isl->n_ext_out_descs = isl->n_ext_out_sigs;
}

static void island_add_ext_out_sig(rt_island_t *isl, uint32_t sig_idx)
{
   for (uint32_t i = 0; i < isl->n_ext_out_sigs; i++) {
      if (isl->ext_out_sigs[i] == sig_idx) {
         island_refresh_ext_out_descs(isl);
         return;
      }
   }
   isl->ext_out_sigs = xrealloc_array(isl->ext_out_sigs,
                                      isl->n_ext_out_sigs + 1u,
                                      sizeof(uint32_t));
   isl->ext_out_sigs[isl->n_ext_out_sigs++] = sig_idx;
   island_refresh_ext_out_descs(isl);
}

static void island_add_comb_flush_sig(rt_island_t *isl,
                                      const rt_island_sig_t *s)
{
   if (!(s->flags & RT_ISIG_COMB_WRITTEN))
      return;
   if (s->flags & RT_ISIG_OUT) {
      if (isl->mgr->hier_built)
         island_add_ext_out_sig(isl, s->sig_idx);
   }
   else
      island_add_obs_sig(isl, s->sig_idx);
}

void rt_island_input_watch_inline(rt_model_t *m, void *user, bool defer_comb)
{
   rt_island_sig_t *s = user;
   rt_island_t *isl = s->isl;
   WSTAT_COUNT(s, 0);
   island_mark_in_dirty(isl, s->in_idx);
   isl->has_dirty_inputs = true;
   if (isl->cluster != NULL) {
      rt_super_island_t *cl = isl->cluster;
      cl->has_dirty_inputs = true;
      if (!isl->in_cl_dirty_in) {
         isl->in_cl_dirty_in = true;
         cl->dirty_in_list[cl->n_dirty_in++] = isl;
      }
   }
   if (s->flags & RT_ISIG_COMB_READ) {
      if (unlikely(defer_comb)) {
         if (!isl->in_defer_dirty) {
            isl->in_defer_dirty = true;
            APUSH(isl->mgr->defer_dirty_q, isl);
         }
         rt_island_request_delta(m);
      }
      else
         island_mark_dirty(isl);
   }
}

static void island_apply_direct_outs(rt_island_t *isl);
static void island_apply_deferred_puts(rt_island_mgr_t *mgr);
static void island_spec_tick(rt_island_mgr_t *mgr);
static void island_spec_reset(rt_island_mgr_t *mgr);
static void island_spec_pin_sig(rt_island_mgr_t *mgr, rt_signal_t *sig,
                                rt_island_deopt_t why);
static void island_init_act(rt_island_t *isl);
static void island_act_report(rt_island_mgr_t *mgr);

static void island_activate_direct_pub(rt_island_mgr_t *mgr);

void rt_island_promote_deferred(rt_island_mgr_t *mgr)
{
   if (unlikely(mgr->pub_pending))
      island_activate_direct_pub(mgr);

   if (unlikely(mgr->defer_put_q.count > 0))
      island_apply_deferred_puts(mgr);

   if (unlikely(mgr->spec_on))
      island_spec_tick(mgr);

   if (unlikely(mgr->defer_direct_q.count > 0)) {
      // Direct comb wires out of VHDL islands deliver their value one delta
      // after the source island ran, like the signal they replace
      const unsigned nd = mgr->defer_direct_q.count;
      for (unsigned i = 0; i < nd; i++) {
         rt_island_t *isl = mgr->defer_direct_q.items[i];
         isl->in_defer_direct = false;
         island_apply_direct_outs(isl);
      }
      mgr->defer_direct_q.count = 0;
   }

   if (likely(mgr->defer_dirty_q.count == 0))
      return;
   const unsigned n = mgr->defer_dirty_q.count;
   for (unsigned i = 0; i < n; i++) {
      rt_island_t *isl = mgr->defer_dirty_q.items[i];
      isl->in_defer_dirty = false;
      island_mark_dirty(isl);
   }
   mgr->defer_dirty_q.count = 0;
}

static void island_input_watch_cb(uint64_t now, rt_signal_t *signal,
                                  rt_watch_t *watch, void *user)
{
   (void)now;
   (void)signal;
   (void)watch;
   rt_island_sig_t *s = user;
   rt_island_input_watch_inline(s->isl->model, user, false);
}

static inline bool island_sv_edge_armed(rt_island_mgr_t *mgr,
                                        const rt_signal_t *sig, uint64_t now,
                                        bool *reset_edge)
{
   // Verilog always @(posedge/negedge ...) processes enter their first
   // event control during delta 0 (now == 0, iteration == 0), so signal
   // updates delivered in the 0ms+0 signal-update phase (e.g. VPI
   // cbStartOfSimulation deposits) must not trigger SV edge domains,
   // whereas signals driven during model_reset() (init events or
   // rt_island_eval_sv_reset()) and delta-0 deposits (iteration >= 1) do.
   if (*reset_edge) {
      *reset_edge = false;
      return true;
   }
   if (likely(now > 0))
      return true;
   unsigned deltas = 0;
   (void)model_now(mgr->model, &deltas);
   if (deltas > 0)
      return true;
   return rt_model_has_init_event(mgr->model, sig);
}

static inline void island_sdom_enqueue_sample(rt_island_mgr_t *mgr,
                                              rt_super_domain_t *sdom)
{
   if (sdom->in_sample_q)
      return;
   sdom->in_sample_q = true;
   APUSH(mgr->super_sample_q, sdom);
   if (sdom->edge_flags & (RT_IEDGE_VHDL | RT_IEDGE_VHDL_EVENT)) {
      for (uint32_t c = 0; c < sdom->n_clusters; c++)
         sdom->clusters[c]->pending_vhdl_ff++;
      for (uint32_t s = 0; s < sdom->n_solo_islands; s++)
         sdom->solo_islands[s]->pending_vhdl_ff++;
   }
}

static inline void island_sdom_dequeue_sample(rt_super_domain_t *sdom)
{
   assert(sdom->in_sample_q);
   sdom->in_sample_q = false;
   if (sdom->edge_flags & (RT_IEDGE_VHDL | RT_IEDGE_VHDL_EVENT)) {
      for (uint32_t c = 0; c < sdom->n_clusters; c++) {
         assert(sdom->clusters[c]->pending_vhdl_ff > 0);
         sdom->clusters[c]->pending_vhdl_ff--;
      }
      for (uint32_t s = 0; s < sdom->n_solo_islands; s++) {
         assert(sdom->solo_islands[s]->pending_vhdl_ff > 0);
         sdom->solo_islands[s]->pending_vhdl_ff--;
      }
   }
}

static inline void island_dom_enqueue_sample(rt_island_mgr_t *mgr,
                                             rt_island_domain_t *dom)
{
   if (dom->in_sample_q)
      return;
   dom->in_sample_q = true;
   APUSH(mgr->ff_sample_q, dom);
   if (dom->edge_flags & (RT_IEDGE_VHDL | RT_IEDGE_VHDL_EVENT)) {
      rt_island_t *isl = dom->isl;
      isl->pending_vhdl_ff++;
      if (isl->cluster != NULL)
         isl->cluster->pending_vhdl_ff++;
   }
}

static inline void island_dom_dequeue_sample(rt_island_domain_t *dom)
{
   assert(dom->in_sample_q);
   dom->in_sample_q = false;
   if (dom->edge_flags & (RT_IEDGE_VHDL | RT_IEDGE_VHDL_EVENT)) {
      rt_island_t *isl = dom->isl;
      assert(isl->pending_vhdl_ff > 0);
      isl->pending_vhdl_ff--;
      if (isl->cluster != NULL) {
         assert(isl->cluster->pending_vhdl_ff > 0);
         isl->cluster->pending_vhdl_ff--;
      }
   }
}

static void island_super_clk_watch_cb(uint64_t now, rt_signal_t *signal,
                                      rt_watch_t *watch, void *user)
{
   if (unlikely(island_wstats_on))
      __atomic_fetch_add(&island_wstat_sclk_cb, 1, __ATOMIC_RELAXED);
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
      hit = island_sv_edge_armed(sdom->mgr, sdom->clk_sig, now,
                                 &sdom->reset_clk_edge)
         && island_edge_match(sdom->prev_clk, cur, sdom->clk_posedge);
      sdom->prev_clk = cur;
   }
   if (hit)
      island_sdom_enqueue_sample(sdom->mgr, sdom);
}

static void island_super_rst_watch_cb(uint64_t now, rt_signal_t *signal,
                                      rt_watch_t *watch, void *user)
{
   if (unlikely(island_wstats_on))
      __atomic_fetch_add(&island_wstat_srst_cb, 1, __ATOMIC_RELAXED);
   rt_super_domain_t *sdom = user;
   bool hit;
   if (sdom->edge_flags & RT_IEDGE_RST_LEVEL) {
      const uint8_t cur = island_read_raw_bit(sdom->rst_sig,
                                              sdom->rst_sig_offset);
      const rt_island_domain_t *d0 = sdom->doms[0];
      const rt_island_sig_t *rs0 = &(d0->isl->sigs[d0->rst_sig_idx]);
      const bool is_std = island_sig_is_std(sdom->rst_sig, rs0->flags);
      const bool two_state = (rs0->flags & (RT_ISIG_TWO_STATE | RT_ISIG_SPEC_A))
         == RT_ISIG_TWO_STATE;
      hit = island_vhdl_rst_match(cur, sdom->rst_posedge, is_std, two_state);
      sdom->prev_rst = cur;
   }
   else {
      const uint8_t cur = island_read_sv_bit(sdom->rst_sig,
                                             sdom->rst_sig_offset);
      hit = island_sv_edge_armed(sdom->mgr, sdom->rst_sig, now,
                                 &sdom->reset_rst_edge)
         && island_edge_match(sdom->prev_rst, cur, sdom->rst_posedge);
      sdom->prev_rst = cur;
   }
   if (hit)
      island_sdom_enqueue_sample(sdom->mgr, sdom);
}

static void island_clk_watch_cb(uint64_t now, rt_signal_t *signal,
                                rt_watch_t *watch, void *user)
{
   if (unlikely(island_wstats_on))
      __atomic_fetch_add(&island_wstat_clk_cb, 1, __ATOMIC_RELAXED);
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
      hit = island_sv_edge_armed(dom->isl->mgr, cs->sig, now,
                                 &dom->reset_clk_edge)
         && island_edge_match(dom->prev_clk, cur, dom->clk_posedge);
      dom->prev_clk = cur;
   }
   if (hit)
      island_dom_enqueue_sample(dom->isl->mgr, dom);
}

static void island_rst_watch_cb(uint64_t now, rt_signal_t *signal,
                                rt_watch_t *watch, void *user)
{
   if (unlikely(island_wstats_on))
      __atomic_fetch_add(&island_wstat_rst_cb, 1, __ATOMIC_RELAXED);
   rt_island_domain_t *dom = user;
   const rt_island_sig_t *rs = &(dom->isl->sigs[dom->rst_sig_idx]);
   bool hit;
   if (dom->edge_flags & RT_IEDGE_RST_LEVEL) {
      const uint8_t cur = island_read_raw_bit(rs->sig, rs->sig_offset);
      const bool is_std = island_sig_is_std(rs->sig, rs->flags);
      const bool two_state = (rs->flags & (RT_ISIG_TWO_STATE | RT_ISIG_SPEC_A))
         == RT_ISIG_TWO_STATE;
      hit = island_vhdl_rst_match(cur, dom->rst_posedge, is_std, two_state);
      dom->prev_rst = cur;
   }
   else {
      const uint8_t cur = island_read_sv_bit(rs->sig, rs->sig_offset);
      hit = island_sv_edge_armed(dom->isl->mgr, rs->sig, now,
                                 &dom->reset_rst_edge)
         && island_edge_match(dom->prev_rst, cur, dom->rst_posedge);
      dom->prev_rst = cur;
   }
   if (hit)
      island_dom_enqueue_sample(dom->isl->mgr, dom);
}

// The clock and reset watches of SV domains detect the edge when the
// signal changes, as the trigger of an always process does, instead of when
// their callback runs: a change and a change back in one region (two
// blocking assignments, or non-blocking ones that overwrite each other) is
// still an edge, and a change that is not an edge schedules no callback
// (and so no delta cycle).  The callback, run when it would otherwise,
// samples the domain if an edge was seen and the domain is armed.
bool rt_island_edge_watch_hit(rt_model_t *m, rt_watch_t *w)
{
   void *user = w->user_data;
   bool edge;
   if (w->fn == island_clk_watch_cb) {
      if (unlikely(island_wstats_on))
         __atomic_fetch_add(&island_wstat_clk_cb, 1, __ATOMIC_RELAXED);
      rt_island_domain_t *dom = user;
      const rt_island_sig_t *cs = &(dom->isl->sigs[dom->clk_sig_idx]);
      const uint8_t cur = island_read_sv_bit(cs->sig, cs->sig_offset);
      edge = island_edge_match(dom->prev_clk, cur, dom->clk_posedge);
      dom->prev_clk = cur;
   }
   else if (w->fn == island_rst_watch_cb) {
      if (unlikely(island_wstats_on))
         __atomic_fetch_add(&island_wstat_rst_cb, 1, __ATOMIC_RELAXED);
      rt_island_domain_t *dom = user;
      const rt_island_sig_t *rs = &(dom->isl->sigs[dom->rst_sig_idx]);
      const uint8_t cur = island_read_sv_bit(rs->sig, rs->sig_offset);
      edge = island_edge_match(dom->prev_rst, cur, dom->rst_posedge);
      dom->prev_rst = cur;
   }
   else if (w->fn == island_super_clk_watch_cb) {
      if (unlikely(island_wstats_on))
         __atomic_fetch_add(&island_wstat_sclk_cb, 1, __ATOMIC_RELAXED);
      rt_super_domain_t *sdom = user;
      const uint8_t cur = island_read_sv_bit(sdom->clk_sig,
                                             sdom->clk_sig_offset);
      edge = island_edge_match(sdom->prev_clk, cur, sdom->clk_posedge);
      sdom->prev_clk = cur;
   }
   else {
      assert(w->fn == island_super_rst_watch_cb);
      if (unlikely(island_wstats_on))
         __atomic_fetch_add(&island_wstat_srst_cb, 1, __ATOMIC_RELAXED);
      rt_super_domain_t *sdom = user;
      const uint8_t cur = island_read_sv_bit(sdom->rst_sig,
                                             sdom->rst_sig_offset);
      edge = island_edge_match(sdom->prev_rst, cur, sdom->rst_posedge);
      sdom->prev_rst = cur;
   }
   return edge;
}

void rt_island_edge_watch_fire(rt_model_t *m, rt_watch_t *w, bool edge)
{
   // Changes while the callback was pending need no test: the domain is
   // sampled anyway, and the next edge is tested against the value now
   const uint64_t now = model_now(m, NULL);
   if (w->fn == island_clk_watch_cb) {
      rt_island_domain_t *dom = w->user_data;
      const rt_island_sig_t *cs = &(dom->isl->sigs[dom->clk_sig_idx]);
      dom->prev_clk = island_read_sv_bit(cs->sig, cs->sig_offset);
      if (island_sv_edge_armed(dom->isl->mgr, cs->sig, now,
                               &dom->reset_clk_edge) && edge)
         island_dom_enqueue_sample(dom->isl->mgr, dom);
   }
   else if (w->fn == island_rst_watch_cb) {
      rt_island_domain_t *dom = w->user_data;
      const rt_island_sig_t *rs = &(dom->isl->sigs[dom->rst_sig_idx]);
      dom->prev_rst = island_read_sv_bit(rs->sig, rs->sig_offset);
      if (island_sv_edge_armed(dom->isl->mgr, rs->sig, now,
                               &dom->reset_rst_edge) && edge)
         island_dom_enqueue_sample(dom->isl->mgr, dom);
   }
   else if (w->fn == island_super_clk_watch_cb) {
      rt_super_domain_t *sdom = w->user_data;
      sdom->prev_clk = island_read_sv_bit(sdom->clk_sig, sdom->clk_sig_offset);
      if (island_sv_edge_armed(sdom->mgr, sdom->clk_sig, now,
                               &sdom->reset_clk_edge) && edge)
         island_sdom_enqueue_sample(sdom->mgr, sdom);
   }
   else {
      rt_super_domain_t *sdom = w->user_data;
      sdom->prev_rst = island_read_sv_bit(sdom->rst_sig, sdom->rst_sig_offset);
      if (island_sv_edge_armed(sdom->mgr, sdom->rst_sig, now,
                               &sdom->reset_rst_edge) && edge)
         island_sdom_enqueue_sample(sdom->mgr, sdom);
   }
}

rt_island_mgr_t *rt_island_mgr_new(rt_model_t *m)
{
   rt_island_mgr_t *mgr = xcalloc(sizeof(rt_island_mgr_t));
   mgr->model = m;
   mgr->sig_map = hash_new(64);
   mgr->alias_map = hash_new(64);
   mgr->fast = !!opt_get_int(OPT_ISLAND_FAST);
   {
      const char *ws = getenv("NVC_ISLAND_WIRE_STATS");
      island_wstats_level = ws != NULL ? atoi(ws) : 0;
      island_wstats_on = island_wstats_level > 0;
      if (island_wstats_on && island_wstats == NULL)
         island_wstats = hash_new(256);
      const char *act = getenv("NVC_ISLAND_ACT");
      island_act_enabled = act == NULL || atoi(act) != 0;
      // Off with simulation threads: island_eval_cluster_mt marks the
      // readers in the cluster directly
      const char *dpub = getenv("NVC_ISLAND_DIRECT_PUB");
      island_direct_pub_on = (dpub == NULL || atoi(dpub) != 0)
         && opt_get_int(OPT_SIM_THREADS) <= 1;
      island_act_stats = getenv("NVC_ISLAND_ACT_STATS") != NULL;
      const char *chk = getenv("NVC_ISLAND_ACT_CHECK");
#ifdef DEBUG
      island_act_check = chk == NULL || atoi(chk) != 0;
#else
      island_act_check = chk != NULL && atoi(chk) != 0;
#endif
   }
   return mgr;
}

static void island_pool_free(rt_island_pool_t *pool)
{
   if (pool == NULL)
      return;

   island_mutex_lock(&pool->park_mtx);
   atomic_store(&pool->stop, true);
   island_cond_broadcast(&pool->park_cv);
   island_mutex_unlock(&pool->park_mtx);

   for (uint32_t i = 0; i < pool->nworkers; i++) {
      if (pool->workers[i].thread != NULL)
         thread_join(pool->workers[i].thread);
   }

   for (uint32_t i = 0; i < pool->nworkers; i++) {
      if (pool->workers[i].tlab != NULL)
         tlab_release(pool->jit, pool->workers[i].tlab);
   }

   island_cond_destroy(&pool->park_cv);
   island_mutex_destroy(&pool->park_mtx);
   free(pool->workers);
   free(pool);
}

static void island_spec_free(rt_island_t *isl);
static void island_spec_report(rt_island_mgr_t *mgr);
static void island_spec_rt_report(rt_island_mgr_t *mgr);
static uint8_t *island_spec_tmp = NULL;   // Conversion scratch
static size_t island_spec_tmp_size = 0;

void rt_island_mgr_free(rt_island_mgr_t *mgr)
{
   if (mgr == NULL)
      return;

   if (island_wstats_on) {
      island_wstat_report(mgr);
      island_wstat_free();
   }

   if (island_act_stats)
      island_act_report(mgr);

   if (mgr->pool != NULL) {
      island_pool_free(mgr->pool);
      mgr->pool = NULL;
   }

   island_spec_report(mgr);
   island_spec_rt_report(mgr);
   free(island_spec_tmp);
   island_spec_tmp = NULL;
   island_spec_tmp_size = 0;

   for (unsigned i = 0; i < mgr->clusters.count; i++) {
      rt_super_island_t *cl = mgr->clusters.items[i];
      free(cl->comb_deps);
      free(cl->dirty_in_list);
      free(cl->islands);
      free(cl);
   }
   for (unsigned i = 0; i < mgr->super_domains.count; i++) {
      rt_super_domain_t *sd = mgr->super_domains.items[i];
      free(sd->doms);
      free(sd->ff_entries);
      free(sd->clusters);
      free(sd->solo_islands);
      free(sd);
   }
   for (unsigned i = 0; i < mgr->islands.count; i++) {
      rt_island_t *isl = mgr->islands.items[i];
      for (uint32_t d = 0; d < isl->ndomains; d++) {
         free(isl->domains[d].write_sigs);
         free(isl->domains[d].ff_descs);
         free(isl->domains[d].ff_runs);
         free(isl->domains[d].glitch_resets);
      }
      free(isl->domains);
      for (uint32_t f = 0; f < isl->n_forces; f++)
         free(isl->forces[f].mask);
      free(isl->forces);
      for (uint32_t s = 0; s < isl->nsigs; s++) {
         free(isl->sigs[s].direct_targets);
         free(isl->sigs[s].direct_aliases);
         free(isl->sigs[s].last_out_words);
      }
      free(isl->comb_deps);
#ifdef __MINGW32__
      _aligned_free(isl->state);
#else
      free(isl->state);
#endif
      free(isl->sigs);
      free(isl->in_sigs);
      free(isl->in_descs);
      free(isl->dirty_in_words);
      free(isl->out_sigs);
      free(isl->ext_out_sigs);
      free(isl->ext_out_descs);
      free(isl->direct_comb_outs);
      free(isl->direct_comb_descs);
      free(isl->obs_sigs);
      free(isl->scratch);
      free(isl->fb_sigs);
      free(isl->fb_shadow);
      free(isl->sparse_ovf_entries);
      free(isl->sparse_ovf_data);
      if (isl->spec != NULL) {
         free(isl->spec->b_off);
         free(isl->spec->ff_handles_b);
      }
      island_spec_free(isl);
      free(isl);
   }
   hash_free(mgr->sig_map);
   hash_free(mgr->alias_map);
   for (uint32_t i = 0; i < mgr->n_act_info; i++) {
      free(mgr->act_info[i].sigs);
      free(mgr->act_info[i].shadow);
   }
   free(mgr->act_info);
   ACLEAR(mgr->islands);
   ACLEAR(mgr->dirty_q);
   ACLEAR(mgr->defer_dirty_q);
   ACLEAR(mgr->defer_direct_q);
   for (unsigned i = 0; i < mgr->defer_put_q.count; i++)
      free(mgr->defer_put_q.items[i].buf);
   ACLEAR(mgr->defer_put_q);
   ACLEAR(mgr->ff_sample_q);
   ACLEAR(mgr->ff_commit_q);
   ACLEAR(mgr->clusters);
   ACLEAR(mgr->dirty_clusters);
   ACLEAR(mgr->super_domains);
   ACLEAR(mgr->super_sample_q);
   ACLEAR(mgr->super_commit_q);
   ACLEAR(mgr->mt_ff_islands);
   ACLEAR(mgr->mt_ff_safe_q);
   ACLEAR(mgr->mt_isl_work_q);
   ACLEAR(mgr->mt_isl_wave_q);
   ACLEAR(mgr->mt_isl_rem_q);
   ACLEAR(mgr->mt_isl_safe_q);
   ACLEAR(mgr->mt_cl_work_q);
   ACLEAR(mgr->mt_cl_wave_q);
   ACLEAR(mgr->mt_cl_rem_q);
   ACLEAR(mgr->mt_cl_safe_q);
   ACLEAR(mgr->mt_sva_ptrs);
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
   isl->root_scope = rt_island_model_active_scope(m);
   isl->isl_id = isl->mgr->islands.count;
   isl->eval_handle = eval_handle;
   isl->inst_ctx = inst_ctx;
   isl->reset_eval_handle = JIT_HANDLE_INVALID;
   isl->state_bytes = RT_ISLAND_STRIDE(state_bytes);
   isl->nsigs = nsigs;
   isl->ndomains = ndomains;

   const size_t total_bytes = ndomains > 0
      ? (size_t)isl->state_bytes * 2u : (size_t)isl->state_bytes;
   void *buf = NULL;
#ifdef __MINGW32__
   buf = _aligned_malloc(total_bytes, 64);
   if (buf == NULL)
      fatal("failed to allocate %zu bytes for island state", total_bytes);
#else
   if (posix_memalign(&buf, 64, total_bytes) != 0)
      fatal("failed to allocate %zu bytes for island state", total_bytes);
#endif
   isl->state = buf;
   isl->next_state = ndomains > 0 ? isl->state + isl->state_bytes : NULL;
   isl->total_state_bytes = (uint32_t)total_bytes;
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

_Static_assert(offsetof(rt_island_sparse_log_t, count) == 0, "sparse log count");
_Static_assert(offsetof(rt_island_sparse_log_t, data_used) == 8, "sparse log data_used");
_Static_assert(offsetof(rt_island_sparse_log_t, isl_ptr) == 16, "sparse log isl_ptr");
_Static_assert(offsetof(rt_island_sparse_log_t, entries) == 32, "sparse log entries");
_Static_assert(offsetof(rt_island_sparse_log_t, data) == 288, "sparse log data");
_Static_assert(sizeof(rt_island_sparse_entry_t) == 16, "sparse entry size");

void rt_island_set_reset_comb(void *handle, jit_handle_t reset_handle,
                              void *reset_ctx, bool skip_reset)
{
   rt_island_t *isl = handle;
   isl->reset_eval_handle = reset_handle;
   isl->reset_eval_ctx = reset_ctx;
   isl->skip_sv_reset = skip_reset;
}

void rt_island_set_sparse(void *handle, uint32_t sparse_log_off,
                          uint32_t total_state_bytes)
{
   rt_island_t *isl = handle;
   assert(isl != NULL);
   assert(sparse_log_off >= isl->total_state_bytes);
   assert(total_state_bytes >= sparse_log_off + sizeof(rt_island_sparse_log_t));

   const size_t aligned_total = ALIGN_UP((size_t)total_state_bytes, 64u);
   void *buf = NULL;
#ifdef __MINGW32__
   buf = _aligned_malloc(aligned_total, 64);
   if (buf == NULL)
      fatal("failed to allocate %zu bytes for sparse island state", aligned_total);
   _aligned_free(isl->state);
#else
   if (posix_memalign(&buf, 64, aligned_total) != 0)
      fatal("failed to allocate %zu bytes for sparse island state", aligned_total);
   free(isl->state);
#endif
   memset(buf, 0, aligned_total);
   isl->state = buf;
   isl->next_state = isl->ndomains > 0 ? isl->state + isl->state_bytes : NULL;
   isl->sparse_log_off = sparse_log_off;
   isl->total_state_bytes = (uint32_t)aligned_total;

   rt_island_sparse_log_t *log =
      (rt_island_sparse_log_t *)(isl->state + sparse_log_off);
   log->isl_ptr = (uint64_t)(uintptr_t)isl;
}

void rt_island_sparse_write(void *isl_ptr, uint32_t sig_idx,
                            uint32_t byte_off, uint32_t len,
                            const uint8_t *data)
{
   rt_island_t *isl = isl_ptr;
   assert(isl != NULL && sig_idx < isl->nsigs);

   if (isl->n_sparse_ovf == isl->cap_sparse_ovf) {
      isl->cap_sparse_ovf = MAX(isl->cap_sparse_ovf * 2u, 32u);
      isl->sparse_ovf_entries = xrealloc_array(isl->sparse_ovf_entries,
                                               isl->cap_sparse_ovf,
                                               sizeof(rt_island_sparse_entry_t));
   }

   const uint32_t need_data = isl->sparse_ovf_data_used + len;
   if (need_data > isl->cap_sparse_ovf_data) {
      uint32_t new_cap = MAX(isl->cap_sparse_ovf_data * 2u, 2048u);
      while (new_cap < need_data)
         new_cap *= 2u;
      isl->cap_sparse_ovf_data = new_cap;
      isl->sparse_ovf_data = xrealloc(isl->sparse_ovf_data, new_cap);
   }

   const uint32_t doff = isl->sparse_ovf_data_used;
   if (len > 0)
      memcpy(isl->sparse_ovf_data + doff, data, len);
   isl->sparse_ovf_data_used = need_data;

   rt_island_sparse_entry_t *e = &(isl->sparse_ovf_entries[isl->n_sparse_ovf++]);
   e->sig_and_off  = (uint64_t)sig_idx | ((uint64_t)byte_off << 32);
   e->len_and_data = (uint64_t)len     | ((uint64_t)doff << 32);
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
   if (flags & RT_ISIG_RESET_LATE)
      isl->has_reset_late = true;
   s->elem_size = (sig != NULL && !(sig->shared.flags & SIG_F_PACKED)
                   && sig->nexus.size > 0)
      ? (uint8_t)sig->nexus.size : 1u;
   if (flags & RT_ISIG_STD_LOGIC9)
      assert(a_off + ALIGN_UP(width * nelems * s->elem_size, 8u)
             <= isl->total_state_bytes);
}

static uint64_t island_gen_key_hash(const char *s)
{
   uint64_t h = UINT64_C(14695981039346656037);
   for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
      h ^= (uint64_t)(*p);
      h *= UINT64_C(1099511628211);
   }
   return h ?: 1u;
}

void rt_island_set_gen_key(void *handle, uint32_t idx, uint64_t key_hash)
{
   rt_island_t *isl = handle;
   assert(idx < isl->nsigs);
   isl->sigs[idx].gen_key_hash = key_hash;
}

void rt_island_attach_sig(void *handle, uint32_t idx, sig_shared_t *ss,
                          int32_t sig_offset)
{
   rt_island_t *isl = handle;
   if (ss == NULL)
      return;

   rt_signal_t *sig = container_of(ss, rt_signal_t, shared);

   if (idx == UINT32_MAX) {
      if (isl->root_scope == NULL || sig->parent == NULL || sig->where == NULL)
         return;
      const char *rname = istr(isl->root_scope->name);
      const size_t rlen = strlen(rname);
      const char *pname = istr(sig->parent->name);
      if (strncmp(pname, rname, rlen) != 0 || pname[rlen] != '.')
         return;
      ident_t sname = object_ident((object_t *)sig->where);
      if (sname == NULL)
         return;
      LOCAL_TEXT_BUF ktb = tb_new();
      tb_printf(ktb, "%s.%s", pname + rlen + 1, istr(sname));
      const uint64_t h = island_gen_key_hash(tb_get(ktb));
      uint32_t found = UINT32_MAX;
      for (uint32_t k = 0; k < isl->nsigs; k++) {
         if (isl->sigs[k].gen_key_hash == h && isl->sigs[k].sig == NULL) {
            found = k;
            break;
         }
      }
      if (found == UINT32_MAX)
         return;
      idx = found;
   }

   assert(idx < isl->nsigs);
   rt_island_sig_t *s = &(isl->sigs[idx]);
   s->sig = sig;
   s->sig_offset = sig_offset;
   s->elem_size = (!(sig->shared.flags & SIG_F_PACKED)
                   && sig->nexus.size > 0)
      ? (uint8_t)sig->nexus.size : 1u;

   if (s->flags & (RT_ISIG_COMB_WRITTEN | RT_ISIG_FF_WRITTEN)) {
      sig->shared.flags |= SIG_F_ISLAND;
      sig->shared.flags &= ~(SIG_F_FAST_NOOP | SIG_F_FAST_DIRECT);

      rt_island_sig_t *prev = hash_get(isl->mgr->sig_map, sig);
      s->next_owner = prev;
      hash_put(isl->mgr->sig_map, sig, s);
   }
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

////////////////////////////////////////////////////////////////////////////////
// Speculative Mode A variant of a VHDL island (NVC_ISLAND_SPEC)
//
// The island runs with its Mode B layout and functions.  The Mode A layout
// keeps the same leading bytes, stores std_logic, bit and boolean signals
// as packed two-state words ('0' = 0, '1' = 1) and other signals as the
// same bytes as Mode B.  rt_island_spec_pack converts one half of the state
// to Mode A and says whether every std_logic value was '0' or '1', the only
// case where the conversion is exact; rt_island_spec_unpack is the exact
// inverse for such a state.

static int island_spec_force = -1;

static bool island_spec_force_a(void)
{
   if (island_spec_force < 0) {
      const char *env = getenv("NVC_ISLAND_SPEC_FORCE");
      island_spec_force = env != NULL && (env[0] == 'A' || env[0] == 'a');
   }
   return island_spec_force;
}

void rt_island_set_variant_a(void *handle, jit_handle_t eval_handle,
                             jit_handle_t eval_handle_h,
                             uint32_t state_bytes, uint32_t hdr_bytes,
                             uint32_t sparse_log_off, uint32_t total_bytes)
{
   rt_island_t *isl = handle;
   assert(isl->spec == NULL);
   assert(hdr_bytes <= state_bytes);
   assert((sparse_log_off == 0) == (isl->sparse_log_off == 0));

   rt_island_spec_t *sp = xcalloc(sizeof(rt_island_spec_t));
   sp->eval_handle = eval_handle;
   sp->eval_handle_h = eval_handle_h;
   sp->state_bytes = state_bytes;
   sp->stride      = RT_ISLAND_STRIDE(state_bytes);
   sp->hdr_bytes   = hdr_bytes;
   sp->sparse_log_off = sparse_log_off;
   sp->sparse_log_off_b = isl->sparse_log_off;
   sp->total_bytes = sparse_log_off > 0 ? total_bytes : 0;
   assert(sparse_log_off == 0
          || (sparse_log_off >= isl->total_state_bytes
              && total_bytes >= sparse_log_off
              + sizeof(rt_island_sparse_log_t)));
   if (isl->nsigs > 0) {
      sp->a_off   = xcalloc_array(isl->nsigs, sizeof(uint32_t));
      sp->a_flags = xcalloc_array(isl->nsigs, sizeof(uint32_t));
      sp->b_bytes = xcalloc_array(isl->nsigs, sizeof(uint32_t));
      sp->x_off   = xcalloc_array(isl->nsigs, sizeof(uint32_t));
   }
   if (isl->ndomains > 0) {
      sp->ff_handles = xmalloc_array(isl->ndomains, sizeof(jit_handle_t));
      sp->ff_handles_h = xmalloc_array(isl->ndomains, sizeof(jit_handle_t));
      for (uint32_t d = 0; d < isl->ndomains; d++)
         sp->ff_handles[d] = sp->ff_handles_h[d] = JIT_HANDLE_INVALID;
   }
   isl->spec = sp;
}

void rt_island_bind_sig_a(void *handle, uint32_t idx, uint32_t a_off,
                          uint32_t a_flags, uint32_t b_bytes, uint32_t x_off)
{
   rt_island_t *isl = handle;
   rt_island_spec_t *sp = isl->spec;
   assert(sp != NULL && idx < isl->nsigs);
   sp->a_off[idx] = a_off;
   sp->a_flags[idx] = a_flags;
   sp->b_bytes[idx] = b_bytes;
   sp->x_off[idx] = x_off;
   assert(RT_ISLAND_SPEC_HELD_OFF(sp->hdr_bytes) + isl->nsigs <= sp->state_bytes);
   if (isl->sigs[idx].flags & RT_ISIG_SPARSE_RAM) {
      // After both halves, one copy
      assert(a_off >= sp->sparse_log_off + sizeof(rt_island_sparse_log_t));
      assert(a_off + b_bytes <= sp->total_bytes);
      assert(!(a_flags & RT_ISIG_TWO_STATE)
             || (x_off + b_bytes <= sp->total_bytes
                 && (a_flags & RT_ISIG_SPEC_ROWF)));
   }
   else if (a_flags & RT_ISIG_TWO_STATE) {
      const rt_island_sig_t *s = &(isl->sigs[idx]);
      uint32_t end = a_off + s->nwords * s->nelems * 8u;
      if (a_flags & RT_ISIG_SPEC_ROWF)
         end += RT_ISLAND_SPEC_ROWF_BYTES(s->nelems);
      assert(end <= sp->state_bytes);
      assert(x_off >= end && x_off + b_bytes <= sp->state_bytes);
      assert(b_bytes == s->width * s->nelems);
      (void)end;
   }
   else
      assert(a_off + b_bytes <= sp->state_bytes);
}

void rt_island_add_domain_a(void *handle, uint32_t dom_idx,
                            jit_handle_t ff_handle, jit_handle_t ff_handle_h)
{
   rt_island_t *isl = handle;
   assert(isl->spec != NULL && dom_idx < isl->ndomains);
   isl->spec->ff_handles[dom_idx] = ff_handle;
   isl->spec->ff_handles_h[dom_idx] = ff_handle_h;
}

// A signal that is neither an input nor written by the island, such as one
// used only as a flip-flop clock (sampled by the domain watch from the
// signal itself), has a copy in the state that is never updated nor loaded
static inline bool island_spec_sig_live(const rt_island_sig_t *s)
{
   return !!(s->flags & (RT_ISIG_IN | RT_ISIG_COMB_WRITTEN
                         | RT_ISIG_FF_WRITTEN));
}

// Convert the NELEMS elements of WIDTH bytes at SRC to packed words of NWORDS
// each, true if every byte was ZERO or ZERO + 1; a byte is a
// set bit iff it is ZERO + 1
static bool island_spec_pack_elems(uint64_t *words, const uint8_t *src,
                                   uint32_t nelems, uint32_t width,
                                   uint32_t nwords, uint8_t zero);

// Convert signal I of the island from the Mode B state B_STATE to the Mode A
// state A_STATE, true if every value was '0' or '1'
static bool island_spec_pack_sig(const rt_island_t *isl, uint32_t i,
                                 const uint8_t *b_state, uint8_t *a_state)
{
   const rt_island_spec_t *sp = isl->spec;
   const rt_island_sig_t *s = &(isl->sigs[i]);
   if (!island_spec_sig_live(s))
      return true;
   const uint8_t *src = b_state + s->a_off;
   uint8_t *dst = a_state + sp->a_off[i];
   if (!(sp->a_flags[i] & RT_ISIG_TWO_STATE)) {
      memcpy(dst, src, sp->b_bytes[i]);
      return true;
   }

   // Bit 0 of a packed element is its last byte ("lsb" of a downto vector),
   // as in the Mode A lowering
   const uint8_t zero = (sp->a_flags[i] & RT_ISIG_STD_LOGIC_ELEM) ? 2 : 0;
   return island_spec_pack_elems((uint64_t *)dst, src, s->nelems, s->width,
                                 s->nwords, zero);
}

// The exact inverse of island_spec_pack_sig for a '0'/'1' value
static void island_spec_unpack_sig(const rt_island_t *isl, uint32_t i,
                                   const uint8_t *a_state, uint8_t *b_state)
{
   const rt_island_spec_t *sp = isl->spec;
   const rt_island_sig_t *s = &(isl->sigs[i]);
   if (!island_spec_sig_live(s))
      return;
   uint8_t *dst = b_state + s->a_off;
   const uint8_t *src = a_state + sp->a_off[i];
   if (!(sp->a_flags[i] & RT_ISIG_TWO_STATE)) {
      memcpy(dst, src, sp->b_bytes[i]);
      return;
   }

   const uint8_t zero = (sp->a_flags[i] & RT_ISIG_STD_LOGIC_ELEM) ? 2 : 0;
   const uint64_t *words = (const uint64_t *)src;
   for (uint32_t e = 0; e < s->nelems; e++) {
      uint8_t *eb = dst + (size_t)e * s->width;
      const uint64_t *ew = words + (size_t)e * s->nwords;
      for (uint32_t k = 0; k < s->width; k++) {
         const uint32_t lsb = s->width - 1u - k;
         eb[k] = zero + (uint8_t)((ew[lsb / 64u] >> (lsb & 63u)) & 1u);
      }
   }
}

bool rt_island_spec_pack(const rt_island_t *isl, const uint8_t *b_state,
                         uint8_t *a_state)
{
   const rt_island_spec_t *sp = isl->spec;
   memset(a_state, 0, sp->state_bytes);
   memcpy(a_state, b_state, sp->hdr_bytes);

   bool clean = true;
   for (uint32_t i = 0; i < isl->nsigs; i++)
      clean &= island_spec_pack_sig(isl, i, b_state, a_state);
   return clean;
}

void rt_island_spec_unpack(const rt_island_t *isl, const uint8_t *a_state,
                           uint8_t *b_state)
{
   memcpy(b_state, a_state, isl->spec->hdr_bytes);
   for (uint32_t i = 0; i < isl->nsigs; i++)
      island_spec_unpack_sig(isl, i, a_state, b_state);
}

static void island_spec_setup(rt_island_t *isl)
{
   rt_island_spec_t *sp = isl->spec;
   if (sp == NULL || !island_spec_force_a() || sp->sparse_log_off > 0)
      return;

   // NVC_ISLAND_SPEC_FORCE=A (test hook): every call converts the state to
   // Mode A, runs the Mode A function and converts back, or runs the Mode B
   // function when the state is not all '0'/'1' or the Mode A function
   // returned a deopt code
   sp->force_a = true;
   const size_t nbytes = (size_t)sp->stride * 2u;
   sp->buf = xmalloc_array(nbytes / 8u, sizeof(uint64_t));
   memset(sp->buf, 0, nbytes);
}

static void island_spec_free(rt_island_t *isl)
{
   rt_island_spec_t *sp = isl->spec;
   if (sp == NULL)
      return;
   free(sp->ff_handles);
   free(sp->ff_handles_h);
   free(sp->a_off);
   free(sp->a_flags);
   free(sp->b_bytes);
   free(sp->x_off);
   free(sp->held_list);
   free(sp->buf);
   free(sp);
   isl->spec = NULL;
}

static void island_spec_report(rt_island_mgr_t *mgr)
{
   // NVC_ISLAND_SPEC_STATS=1 prints to stdout, an absolute path appends
   // to that file
   const char *env = getenv("NVC_ISLAND_SPEC_STATS");
   if (env == NULL || *env == '\0')
      return;

   FILE *f = stdout;
   if (env[0] == '/' && (f = fopen(env, "a")) == NULL)
      return;

   uint32_t n_a = 0;
   for (unsigned i = 0; i < mgr->islands.count; i++)
      n_a += (mgr->islands.items[i]->spec != NULL);
   fprintf(f, "ISLAND_SPEC: islands=%u a_variant=%u\n", mgr->islands.count,
           n_a);

   for (unsigned i = 0; i < mgr->islands.count; i++) {
      const rt_island_t *isl = mgr->islands.items[i];
      const rt_island_spec_t *sp = isl->spec;
      if (sp == NULL || !sp->force_a)
         continue;
      fprintf(f, "ISLAND_SPEC: island=%u calls_a=%"PRIu64" deopt literal=%"PRIu64
             " range=%"PRIu64" conv=%"PRIu64" report=%"PRIu64
             " state=%"PRIu64"\n", isl->isl_id, sp->calls_a,
             sp->deopts[RT_ISLAND_DEOPT_LITERAL],
             sp->deopts[RT_ISLAND_DEOPT_RANGE],
             sp->deopts[RT_ISLAND_DEOPT_CONV],
             sp->deopts[RT_ISLAND_DEOPT_REPORT],
             sp->deopts[RT_ISLAND_DEOPT_STATE]);
   }
   if (f != stdout)
      fclose(f);
   else
      fflush(stdout);
}

// Registers of an SV flip-flop domain that nothing outside the island state
// observes on a change (no direct link, output, activity byte, log or
// forces) are committed together: consecutive ones whose a and b planes are
// adjacent in the state form one run of words compared and copied at once,
// instead of one descriptor per register.  A run only combines registers
// with the same RT_IFF_COMB_READ so a change of the run wakes the
// combinational logic exactly when a change of one of its registers would.
// Offset in the next-state half of the overwritten NBA log of an SV island
// register (RT_ISIG_GLITCH_LOG), after its planes
static inline uint32_t island_glitch_log_off(const rt_island_sig_t *s)
{
   const uint32_t plane = s->nwords * s->nelems * 8u;
   return ((s->flags & RT_ISIG_TWO_STATE) ? s->a_off : s->b_off) + plane;
}

static bool island_ff_desc_mergeable(const rt_island_ff_desc_t *d)
{
   if (d->flags & (RT_IFF_STD_LOGIC9 | RT_IFF_SPARSE_RAM | RT_IFF_ACT
                   | RT_IFF_TOUCH_LOG | RT_IFF_HAS_NOTIFY))
      return false;
   return (d->flags & RT_IFF_TWO_STATE) || d->b_delta == d->count;
}

static int island_ff_desc_cmp(const void *a, const void *b)
{
   const uint32_t ao = ((const rt_island_ff_desc_t *)a)->a_off;
   const uint32_t bo = ((const rt_island_ff_desc_t *)b)->a_off;
   return (ao > bo) - (ao < bo);
}

static void island_merge_ff_runs(rt_island_domain_t *dom)
{
   if (dom->ff_runs == NULL)
      dom->ff_runs = xmalloc_array(MAX(dom->n_write_sigs, 1),
                                   sizeof(rt_island_ff_desc_t));

   // The other descriptors keep their order; the runs follow them, merged
   // in state order as the registers are written in statement order
   uint32_t n = 0, nm = 0;
   rt_island_ff_desc_t *merge LOCAL =
      xmalloc_array(MAX(dom->n_write_sigs, 1), sizeof(rt_island_ff_desc_t));
   for (uint32_t j = 0; j < dom->n_write_sigs; j++) {
      const rt_island_ff_desc_t *d = &(dom->ff_descs[j]);
      if (island_ff_desc_mergeable(d))
         merge[nm++] = *d;
      else
         dom->ff_runs[n++] = *d;
   }

   qsort(merge, nm, sizeof(rt_island_ff_desc_t), island_ff_desc_cmp);

   const uint32_t first_run = n;
   for (uint32_t j = 0; j < nm; j++) {
      const rt_island_ff_desc_t *d = &(merge[j]);
      const uint32_t nwords =
         (d->flags & RT_IFF_TWO_STATE) ? d->count : 2 * d->count;
      const uint16_t comb = d->flags & RT_IFF_COMB_READ;
      rt_island_ff_desc_t *prev = n > first_run ? &(dom->ff_runs[n - 1]) : NULL;
      if (prev != NULL && (prev->flags & RT_IFF_COMB_READ) == comb
          && prev->a_off + prev->count * 8u == d->a_off)
         prev->count += nwords;
      else {
         dom->ff_runs[n++] = (rt_island_ff_desc_t){
            .a_off   = d->a_off,
            .count   = nwords,
            .sig_idx = d->sig_idx,
            .flags   = RT_IFF_RUN | comb,
         };
      }
   }
   dom->n_ff_runs = n;
}

static void island_refresh_domain_descs(rt_island_t *isl)
{
   for (uint32_t d = 0; d < isl->ndomains; d++) {
      rt_island_domain_t *dom = &(isl->domains[d]);
      if (dom->n_write_sigs == 0)
         continue;
      if (dom->ff_descs == NULL)
         dom->ff_descs = xmalloc_array(dom->n_write_sigs,
                                       sizeof(rt_island_ff_desc_t));
      if (dom->glitch_resets == NULL)
         dom->glitch_resets = xmalloc_array(dom->n_write_sigs,
                                            sizeof(uint32_t));
      dom->n_glitch_resets = 0;
      for (uint32_t j = 0; j < dom->n_write_sigs; j++) {
         const uint32_t sidx = dom->write_sigs[j];
         const rt_island_sig_t *s = &(isl->sigs[sidx]);
         rt_island_ff_desc_t *fd = &(dom->ff_descs[j]);
         uint16_t fl = 0;
         if (s->flags & RT_ISIG_SPARSE_RAM)
            fl |= RT_IFF_SPARSE_RAM;
         else if (s->flags & RT_ISIG_STD_LOGIC9)
            fl |= RT_IFF_STD_LOGIC9;
         if (s->flags & RT_ISIG_TWO_STATE)
            fl |= RT_IFF_TWO_STATE;
         if (s->flags & (RT_ISIG_COMB_READ | RT_ISIG_COMB_WRITTEN))
            fl |= RT_IFF_COMB_READ;
         if (s->flags & RT_ISIG_ACT)
            fl |= RT_IFF_ACT;
         if (s->flags & RT_ISIG_DIRECT_LINK)
            fl |= RT_IFF_DIRECT_LINK;
         else if ((s->flags & (RT_ISIG_OUT | RT_ISIG_OBSERVED))
                  && !(s->flags & RT_ISIG_PRUNED))
            fl |= RT_IFF_SYNC_OUT;
         if ((s->flags & RT_ISIG_TOUCH_LOG)
             && !(fl & (RT_IFF_SPARSE_RAM | RT_IFF_STD_LOGIC9)))
            fl |= RT_IFF_TOUCH_LOG;
         // The overwritten NBA values of a register only matter to those
         // woken by its changes; the log of another is only reset
         if ((s->flags & RT_ISIG_GLITCH_LOG)
             && !(fl & (RT_IFF_SPARSE_RAM | RT_IFF_STD_LOGIC9))) {
            if (fl & RT_IFF_HAS_NOTIFY)
               fl |= RT_IFF_GLITCH;
            else
               dom->glitch_resets[dom->n_glitch_resets++] =
                  island_glitch_log_off(s);
         }

         fd->a_off   = s->a_off;
         fd->count   = (fl & RT_IFF_STD_LOGIC9)
            ? (s->width * s->nelems * s->elem_size)
            : (s->nwords * s->nelems);
         if (s->flags & RT_ISIG_SPEC_ROWF)   // Row clean bytes follow
            fd->count += RT_ISLAND_SPEC_ROWF_BYTES(s->nelems) / 8u;
         fd->sig_idx = sidx;
         fd->flags   = fl;
         if (!(fl & (RT_IFF_STD_LOGIC9 | RT_IFF_TWO_STATE))) {
            if (s->b_off >= s->a_off && ((s->b_off - s->a_off) & 7u) == 0
                && ((s->b_off - s->a_off) >> 3) < 0xFFFFu)
               fd->b_delta = (uint16_t)((s->b_off - s->a_off) >> 3);
            else
               fd->b_delta = 0xFFFFu;
         }
         else
            fd->b_delta = 0;
      }

      if (!isl->is_vhdl)
         island_merge_ff_runs(dom);
   }
}

static void island_spec_rt_setup(rt_island_t *isl);

void rt_island_finish(void *handle)
{
   rt_island_t *isl = handle;
   rt_model_t *m = isl->model;

   island_spec_setup(isl);
   island_spec_rt_setup(isl);

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
      isl->in_descs = xcalloc_array(n_in, sizeof(rt_island_in_desc_t));
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

   isl->is_vhdl = false;
   for (uint32_t i = 0; i < isl->nsigs; i++) {
      if (isl->sigs[i].flags & (RT_ISIG_VHDL | RT_ISIG_STD_LOGIC9
                                | RT_ISIG_VHDL_SCHED)) {
         isl->is_vhdl = true;
         break;
      }
   }
   const bool allow_inline = !isl->is_vhdl
      || (isl->mgr->fast && !opt_get_int(OPT_SHUFFLE_PROCS));

   uint32_t in_pos = 0, out_pos = 0;
   for (uint32_t i = 0; i < isl->nsigs; i++) {
      rt_island_sig_t *s = &(isl->sigs[i]);
      if (s->sig == NULL)
         continue;

      if (s->flags & RT_ISIG_IN) {
         s->in_idx = in_pos;
         isl->in_sigs[in_pos] = i;
         rt_island_in_desc_t *id = &(isl->in_descs[in_pos]);
         id->sig_idx = i;
         if (s->flags & RT_ISIG_STD_LOGIC9) {
            const size_t elem_sz = s->elem_size;
            id->src    = s->sig->shared.data + (size_t)s->sig_offset * elem_sz;
            id->dst    = isl->state + s->a_off;
            id->nbytes = s->width * s->nelems * s->elem_size;
         }
         in_pos++;
         s->watch = watch_new(m, island_input_watch_cb, s, WATCH_EVENT, 1);
         watch_set_inline_island(s->watch, allow_inline);
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
      }
   }

   for (uint32_t d = 0; d < isl->ndomains; d++) {
      rt_island_domain_t *dom = &(isl->domains[d]);
      if (dom->ff_handle == JIT_HANDLE_INVALID)
         continue;

      dom->needs_presync = false;
      for (uint32_t w = 0; w < dom->n_write_sigs; w++) {
         const uint32_t sidx = dom->write_sigs[w];
         const rt_island_sig_t *ws = &(isl->sigs[sidx]);
         if (ws->flags & (RT_ISIG_IN | RT_ISIG_COMB_WRITTEN)) {
            dom->needs_presync = true;
            break;
         }
         for (uint32_t d2 = 0; d2 < isl->ndomains && !dom->needs_presync; d2++) {
            if (d2 == d)
               continue;
            const rt_island_domain_t *od = &(isl->domains[d2]);
            for (uint32_t w2 = 0; w2 < od->n_write_sigs; w2++) {
               if (od->write_sigs[w2] == sidx) {
                  dom->needs_presync = true;
                  break;
               }
            }
         }
         if (dom->needs_presync)
            break;
      }

      const rt_island_sig_t *cs = &(isl->sigs[dom->clk_sig_idx]);
      assert(cs->sig != NULL);
      // An SV edge is detected when the clock changes (a VHDL process
      // tests the edge when it runs)
      const bool sv_edge = !(dom->edge_flags
                             & (RT_IEDGE_VHDL | RT_IEDGE_VHDL_EVENT));
      dom->clk_watch = watch_new(m, island_clk_watch_cb, dom, WATCH_EVENT, 1);
      watch_set_inline_edge(dom->clk_watch, sv_edge);
      if (cs->sig->shared.flags & SIG_F_PACKED)
         model_set_event_cb(m, cs->sig, 0, 1, dom->clk_watch);
      else
         model_set_event_cb(m, cs->sig, cs->sig_offset, 1, dom->clk_watch);

      if (dom->rst_sig_idx >= 0) {
         const rt_island_sig_t *rs = &(isl->sigs[dom->rst_sig_idx]);
         assert(rs->sig != NULL);
         dom->rst_watch = watch_new(m, island_rst_watch_cb, dom,
                                    WATCH_EVENT, 1);
         watch_set_inline_edge(dom->rst_watch, sv_edge
                               && !(dom->edge_flags & RT_IEDGE_RST_LEVEL));
         if (rs->sig->shared.flags & SIG_F_PACKED)
            model_set_event_cb(m, rs->sig, 0, 1, dom->rst_watch);
         else
            model_set_event_cb(m, rs->sig, rs->sig_offset, 1, dom->rst_watch);
      }
   }

   island_init_state_defaults(isl);
   island_refresh_domain_descs(isl);
   island_init_act(isl);
   isl->initialized = true;
}

static inline void island_copy_bytes(uint8_t * restrict dst,
                                     const uint8_t * restrict src, size_t n)
{
   if (n == 1) {
      *dst = *src;
      return;
   }
   if (n == 32) {
      uint64_t s0, s1, s2, s3;
      memcpy(&s0, src, 8);      memcpy(&s1, src + 8, 8);
      memcpy(&s2, src + 16, 8); memcpy(&s3, src + 24, 8);
      memcpy(dst, &s0, 8);      memcpy(dst + 8, &s1, 8);
      memcpy(dst + 16, &s2, 8); memcpy(dst + 24, &s3, 8);
      return;
   }
   if (n <= 8) {
      if (n == 4) {
         uint32_t s4;
         memcpy(&s4, src, 4);
         memcpy(dst, &s4, 4);
      }
      else if (n == 8) {
         uint64_t s8;
         memcpy(&s8, src, 8);
         memcpy(dst, &s8, 8);
      }
      else if (n == 2) {
         uint16_t s2;
         memcpy(&s2, src, 2);
         memcpy(dst, &s2, 2);
      }
      else if (n >= 5) {
         uint32_t s0, s1;
         memcpy(&s0, src, 4);
         memcpy(&s1, src + n - 4, 4);
         memcpy(dst, &s0, 4);
         memcpy(dst + n - 4, &s1, 4);
      }
      else if (n == 3) {
         uint16_t s0;
         memcpy(&s0, src, 2);
         memcpy(dst, &s0, 2);
         dst[2] = src[2];
      }
      return;
   }
   if (n <= 16) {
      uint64_t s0, s1;
      memcpy(&s0, src, 8);
      memcpy(&s1, src + n - 8, 8);
      memcpy(dst, &s0, 8);
      memcpy(dst + n - 8, &s1, 8);
      return;
   }
   if (n < 32) {
      uint64_t s0, s1, s2, s3;
      memcpy(&s0, src, 8);
      memcpy(&s1, src + 8, 8);
      memcpy(&s2, src + n - 16, 8);
      memcpy(&s3, src + n - 8, 8);
      memcpy(dst, &s0, 8);
      memcpy(dst + 8, &s1, 8);
      memcpy(dst + n - 16, &s2, 8);
      memcpy(dst + n - 8, &s3, 8);
      return;
   }
   memcpy(dst, src, n);
}

__attribute__((noinline))
static bool island_bytes_diff_other(const uint8_t *a, const uint8_t *b,
                                    size_t n)
{
   if (n <= 8) {
      if (n == 2) {
         uint16_t x, y;
         memcpy(&x, a, 2);
         memcpy(&y, b, 2);
         return x != y;
      }
      if (n >= 5) {
         uint32_t x0, x1, y0, y1;
         memcpy(&x0, a, 4);         memcpy(&x1, a + n - 4, 4);
         memcpy(&y0, b, 4);         memcpy(&y1, b + n - 4, 4);
         return ((x0 ^ y0) | (x1 ^ y1)) != 0;
      }
      if (n == 3) {
         uint16_t x0, y0;
         memcpy(&x0, a, 2);
         memcpy(&y0, b, 2);
         return x0 != y0 || a[2] != b[2];
      }
      return false;
   }
   if (n <= 16) {
      uint64_t x0, x1, y0, y1;
      memcpy(&x0, a, 8);         memcpy(&x1, a + n - 8, 8);
      memcpy(&y0, b, 8);         memcpy(&y1, b + n - 8, 8);
      return ((x0 ^ y0) | (x1 ^ y1)) != 0;
   }
   if (n < 32) {
      uint64_t x0, x1, x2, x3, y0, y1, y2, y3;
      memcpy(&x0, a, 8);          memcpy(&x1, a + 8, 8);
      memcpy(&x2, a + n - 16, 8); memcpy(&x3, a + n - 8, 8);
      memcpy(&y0, b, 8);          memcpy(&y1, b + 8, 8);
      memcpy(&y2, b + n - 16, 8); memcpy(&y3, b + n - 8, 8);
      return ((x0 ^ y0) | (x1 ^ y1) | (x2 ^ y2) | (x3 ^ y3)) != 0;
   }
   if (n <= 64) {
      size_t off = 0;
      for (; off + 8u <= n; off += 8u) {
         uint64_t x, y;
         memcpy(&x, a + off, 8);
         memcpy(&y, b + off, 8);
         if (x != y)
            return true;
      }
      if (off < n) {
         uint64_t x, y;
         memcpy(&x, a + n - 8u, 8);
         memcpy(&y, b + n - 8u, 8);
         if (x != y)
            return true;
      }
      return false;
   }
   return memcmp(a, b, n) != 0;
}

static inline __attribute__((always_inline)) bool
island_bytes_diff(const uint8_t *a, const uint8_t *b, size_t n)
{
   if (n == 1)
      return *a != *b;
   if (n == 32) {
      uint64_t x0, x1, x2, x3, y0, y1, y2, y3;
      memcpy(&x0, a, 8);      memcpy(&x1, a + 8, 8);
      memcpy(&x2, a + 16, 8); memcpy(&x3, a + 24, 8);
      memcpy(&y0, b, 8);      memcpy(&y1, b + 8, 8);
      memcpy(&y2, b + 16, 8); memcpy(&y3, b + 24, 8);
      return ((x0 ^ y0) | (x1 ^ y1) | (x2 ^ y2) | (x3 ^ y3)) != 0;
   }
   if (n == 4) {
      uint32_t x, y;
      memcpy(&x, a, 4);
      memcpy(&y, b, 4);
      return x != y;
   }
   if (n == 8) {
      uint64_t x, y;
      memcpy(&x, a, 8);
      memcpy(&y, b, 8);
      return x != y;
   }
   return island_bytes_diff_other(a, b, n);
}

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
// Word-wise conversion between one-byte-per-bit VHDL signal storage and the
// packed two-state words of a Mode A island.  Bit 0 of the packed element is
// the last byte of the element; on a little-endian host byte j of the word
// array holds the bits of element bytes [width - 8(j+1), width - 8j).

#define ISL_ONES8  UINT64_C(0x0101010101010101)
#define ISL_HIGH8  UINT64_C(0x8080808080808080)
#define ISL_GATHER UINT64_C(0x8040201008040201)

// Eight bytes (byte 0 first) to eight bits (byte 0 in bit 7): bit set iff the
// byte is '1' or 'H' for std_logic, or an odd value without bit 1 otherwise
// (the same rule as a & ~b after std_logic_to_sv)
static inline uint8_t island_pack8_2s(uint64_t w, bool is_std)
{
   uint64_t bits;
   if (is_std) {
      const uint64_t y = (w & (ISL_ONES8 * 0xfb)) ^ (ISL_ONES8 * 0x03);
      const uint64_t nz = (((y & (ISL_ONES8 * 0x7f)) + (ISL_ONES8 * 0x7f)) | y)
         & ISL_HIGH8;
      bits = (~nz & ISL_HIGH8) >> 7;
   }
   else
      bits = w & ~(w >> 1) & ISL_ONES8;
   return (uint8_t)((bits * ISL_GATHER) >> 56);
}

// Bits of one packed element (width bits at ea) to one byte each
static inline void island_unpack_elem_2s(uint8_t *out, const uint64_t *ea,
                                         uint32_t width, uint64_t base)
{
   const uint8_t *eb = (const uint8_t *)ea;
   const uint32_t r = width & 7u;
   uint32_t k = width >> 3;

   if (r != 0) {
      const uint8_t x = eb[k] & (uint8_t)((1u << r) - 1u);
      uint64_t v = ((((uint64_t)x * ISL_ONES8) & UINT64_C(0x0102040810204080))
                    + (ISL_ONES8 * 0x7f)) >> 7;
      v = (v & ISL_ONES8) + base;
      memcpy(out, (const uint8_t *)&v + (8u - r), r);
      out += r;
   }
   while (k-- > 0) {
      uint64_t v = ((((uint64_t)eb[k] * ISL_ONES8)
                     & UINT64_C(0x0102040810204080))
                    + (ISL_ONES8 * 0x7f)) >> 7;
      v = (v & ISL_ONES8) + base;
      memcpy(out, &v, 8);
      out += 8;
   }
}

// One element of VHDL byte storage to packed two-state words
static inline void island_pack_elem_2s(uint64_t *ea, uint32_t nwords,
                                       const uint8_t *src, uint32_t width,
                                       bool is_std)
{
   uint8_t *eb = (uint8_t *)ea;
   const uint32_t r = width & 7u;
   uint32_t k = width >> 3;

   memset(ea, 0, nwords * sizeof(uint64_t));
   if (r != 0) {
      uint64_t w = 0;
      memcpy(&w, src, r);
      eb[k] = (uint8_t)(island_pack8_2s(w, is_std) >> (8u - r));
      src += r;
   }
   while (k-- > 0) {
      uint64_t w;
      memcpy(&w, src, 8);
      eb[k] = island_pack8_2s(w, is_std);
      src += 8;
   }
}

// One element of Verilog byte storage (bit 0 = a, bit 1 = b, the rule of
// the bit loop in island_sync_in_sig) to packed four-state words
static inline void island_pack_elem_4s(uint64_t *ea, uint64_t *eb,
                                       uint32_t nwords, const uint8_t *src,
                                       uint32_t width)
{
   uint8_t *pa = (uint8_t *)ea, *pb = (uint8_t *)eb;
   const uint32_t r = width & 7u;
   uint32_t k = width >> 3;

   memset(ea, 0, nwords * sizeof(uint64_t));
   memset(eb, 0, nwords * sizeof(uint64_t));
   if (r != 0) {
      uint64_t w = 0;
      memcpy(&w, src, r);
      pa[k] = (uint8_t)((((w & ISL_ONES8) * ISL_GATHER) >> 56) >> (8u - r));
      pb[k] = (uint8_t)(((((w >> 1) & ISL_ONES8) * ISL_GATHER) >> 56)
                        >> (8u - r));
      src += r;
   }
   while (k-- > 0) {
      uint64_t w;
      memcpy(&w, src, 8);
      pa[k] = (uint8_t)(((w & ISL_ONES8) * ISL_GATHER) >> 56);
      pb[k] = (uint8_t)((((w >> 1) & ISL_ONES8) * ISL_GATHER) >> 56);
      src += 8;
   }
}

// Eight bytes (byte 0 first) to eight bits (byte 0 in bit 7): bit set iff the
// byte is ONE8 / 0x01010101..., exact for every byte value
static inline uint8_t island_pack8_eq(uint64_t w, uint64_t one8)
{
   const uint64_t x = w ^ one8;
   const uint64_t nz = (((x & (ISL_ONES8 * 0x7f)) + (ISL_ONES8 * 0x7f)) | x)
      & ISL_HIGH8;
   const uint64_t bits = (~nz & ISL_HIGH8) >> 7;
   return (uint8_t)((bits * ISL_GATHER) >> 56);
}

static bool island_spec_pack_elems(uint64_t *words, const uint8_t *src,
                                   uint32_t nelems, uint32_t width,
                                   uint32_t nwords, uint8_t zero)
{
   const uint64_t zero8 = ISL_ONES8 * zero;
   const uint64_t one8 = ISL_ONES8 * (uint8_t)(zero + 1u);
   const uint32_t r = width & 7u;
   uint64_t bad = 0;

   memset(words, 0, (size_t)nwords * nelems * sizeof(uint64_t));
   for (uint32_t e = 0; e < nelems; e++, src += width) {
      const uint8_t *p = src;
      uint8_t *eb = (uint8_t *)(words + (size_t)e * nwords);
      uint32_t k = width >> 3;

      if (r != 0) {
         // The padding bytes of the last partial word read as a clean '0'
         uint64_t w = zero8;
         memcpy(&w, p, r);
         bad |= (w & (ISL_ONES8 * 0xfe)) ^ zero8;
         eb[k] = (uint8_t)(island_pack8_eq(w, one8) >> (8u - r));
         p += r;
      }
      while (k-- > 0) {
         uint64_t w;
         memcpy(&w, p, 8);
         bad |= (w & (ISL_ONES8 * 0xfe)) ^ zero8;
         eb[k] = island_pack8_eq(w, one8);
         p += 8;
      }
   }
   return bad == 0;
}
#else
static bool island_spec_pack_elems(uint64_t *words, const uint8_t *src,
                                   uint32_t nelems, uint32_t width,
                                   uint32_t nwords, uint8_t zero)
{
   memset(words, 0, (size_t)nwords * nelems * sizeof(uint64_t));
   bool clean = true;
   for (uint32_t e = 0; e < nelems; e++) {
      const uint8_t *eb = src + (size_t)e * width;
      uint64_t *ew = words + (size_t)e * nwords;
      for (uint32_t k = 0; k < width; k++) {
         const uint8_t v = eb[k] - zero;
         clean &= (v <= 1);
         if (v == 1) {
            const uint32_t lsb = width - 1u - k;
            ew[lsb / 64u] |= UINT64_C(1) << (lsb & 63u);
         }
      }
   }
   return clean;
}
#endif

// A new value was written into the island state of signal IDX: run the
// combinational processes that read it on the next call (see
// RT_ISLAND_ACT_BYTES)
static inline void island_act_mark(rt_island_t *isl, uint32_t idx)
{
   if (isl->act_on)
      isl->state[idx] = 1;
}

// Only the change bytes of RT_ISIG_ACT signals are read: the hot paths
// shared with Verilog islands test the signal flag
static inline void island_act_mark_sig(rt_island_t *isl,
                                       const rt_island_sig_t *s)
{
   if (s->flags & RT_ISIG_ACT)
      isl->state[s->sig_idx] = 1;
}

static bool island_spec_in_clean(const rt_island_sig_t *s);
static void island_spec_demote(rt_island_t *isl, rt_island_deopt_t why);
static bool island_spec_hold(rt_island_t *isl, uint32_t i, const uint8_t *cur);
static bool island_spec_unhold(rt_island_t *isl, uint32_t i,
                               const uint64_t *words);
static inline bool island_spec_bytes_clean(const uint8_t *p, size_t n,
                                           uint8_t zero);

static void island_sync_in_sig(rt_island_t *isl, const rt_island_sig_t *s)
{
   if (unlikely(s->flags & RT_ISIG_SPEC_A)
       && (s->flags & (RT_ISIG_TWO_STATE | RT_ISIG_SPEC_HELD))) {
      // Mode A island: convert and check in one pass; a value other than
      // '0'/'1' is held as Mode B bytes (island_spec_hold), or converts
      // the island back to Mode B when it cannot be
      const rt_signal_t *sig = s->sig;
      const uint32_t idx = (uint32_t)(s - isl->sigs);
      if ((s->flags & RT_ISIG_SPEC_HELD)
          && (sig->shared.flags & SIG_F_PACKED))
         island_spec_demote(isl, RT_ISLAND_DEOPT_INPUT);
      else if (s->flags & RT_ISIG_SPEC_HELD) {
         const uint8_t *src = sig->shared.data + s->sig_offset;
         const size_t nbytes = (size_t)s->width * s->nelems;
         island_act_mark_sig(isl, s);
         island_copy_bytes(isl->state + s->a_off, src, nbytes);
         const uint8_t zero = island_sig_is_std(sig, s->flags) ? 2u : 0u;
         if (island_spec_bytes_clean(src, nbytes, zero))
            island_spec_unhold(isl, idx, NULL);
         return;
      }
      else if (!(sig->shared.flags & SIG_F_PACKED)) {
         const uint8_t zero = island_sig_is_std(sig, s->flags) ? 2u : 0u;
         const uint8_t *src = sig->shared.data + s->sig_offset;
         if (island_spec_pack_elems((uint64_t *)(isl->state + s->a_off),
                                    src, s->nelems, s->width, s->nwords,
                                    zero)) {
            island_act_mark_sig(isl, s);
            return;
         }
         if (!island_spec_hold(isl, idx, src))
            island_spec_demote(isl, RT_ISLAND_DEOPT_INPUT);
      }
      else if (!island_spec_in_clean(s))
         island_spec_demote(isl, RT_ISLAND_DEOPT_INPUT);
   }

   island_act_mark_sig(isl, s);
   rt_signal_t *sig = s->sig;
   const uint32_t width = s->width;
   const uint32_t nelems = s->nelems;

   if (s->flags & RT_ISIG_STD_LOGIC9) {
      assert(!(sig->shared.flags & SIG_F_PACKED));
      const size_t elem_sz = s->elem_size;
      const size_t nbytes = (size_t)width * nelems * elem_sz;
      const uint8_t *src = sig->shared.data + (size_t)s->sig_offset * elem_sz;
      uint8_t *dst = isl->state + s->a_off;
      island_copy_bytes(dst, src, nbytes);
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

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
   if (two_state && (nelems != 1 || width != 1)) {
      for (uint32_t e = 0; e < nelems; e++)
         island_pack_elem_2s(dst_a + e * nwords, nwords, bytes + e * width,
                             width, is_std);
      return;
   }
   if (!two_state && !is_std && nelems > 1) {
      // Memories: word-wise, the bit loop below costs seconds at reset on
      // arrays of millions of bits
      for (uint32_t e = 0; e < nelems; e++)
         island_pack_elem_4s(dst_a + e * nwords, dst_b + e * nwords, nwords,
                             bytes + (size_t)e * width, width);
      return;
   }
#endif

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
         uint32_t i = 0;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
         if (isl->mgr->fast) {
            for (; i + 8u <= width; i += 8u) {
               uint64_t w;
               memcpy(&w, bytes + i, 8);
               const uint64_t a8 = w & 0x0101010101010101ULL;
               const uint64_t b8 = (w >> 1) & 0x0101010101010101ULL;
               a = (a << 8) | ((a8 * 0x8040201008040201ULL) >> 56);
               b = (b << 8) | ((b8 * 0x8040201008040201ULL) >> 56);
            }
         }
#endif
         for (; i < width; i++) {
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

static inline void island_sync_in_desc(rt_island_t *isl,
                                       const rt_island_in_desc_t *d, bool act)
{
   WSTAT_COUNT(&(isl->sigs[d->sig_idx]), 1);
   if (likely(d->src != NULL)) {
      if (act)
         isl->state[d->sig_idx] = 1;
      island_copy_bytes(d->dst, d->src, d->nbytes);
   }
   else
      island_sync_in_sig(isl, &(isl->sigs[d->sig_idx]));
}

static void island_sync_dirty_inputs(rt_island_t *isl)
{
   if (likely(isl->n_in_sigs <= 64)) {
      uint64_t msk = isl->dirty_in_mask;
      if (msk == 0)
         return;
      isl->dirty_in_mask = 0;
      const rt_island_in_desc_t *descs = isl->in_descs;
      if (isl->act_on) {
         while (msk != 0) {
            const unsigned idx = (unsigned)__builtin_ctzll(msk);
            msk &= msk - 1u;
            island_sync_in_desc(isl, &(descs[idx]), true);
         }
      }
      else {
         while (msk != 0) {
            const unsigned idx = (unsigned)__builtin_ctzll(msk);
            msk &= msk - 1u;
            island_sync_in_desc(isl, &(descs[idx]), false);
         }
      }
   }
   else if (isl->dirty_in_words != NULL) {
      const rt_island_in_desc_t *descs = isl->in_descs;
      for (uint32_t w = 0; w < isl->dirty_in_nwords; w++) {
         uint64_t msk = isl->dirty_in_words[w];
         if (msk == 0)
            continue;
         isl->dirty_in_words[w] = 0;
         const uint32_t base = w << 6;
         while (msk != 0) {
            const unsigned idx = (unsigned)__builtin_ctzll(msk);
            msk &= msk - 1u;
            island_sync_in_desc(isl, &(descs[base + idx]), isl->act_on);
         }
      }
   }
}

static void island_spec_whole_read(rt_island_t *isl, rt_island_sig_t *s);

static void island_sync_out_sig(rt_model_t *m, rt_island_t *isl,
                                rt_island_sig_t *s)
{
   if (unlikely(s->flags & RT_ISIG_SPEC_ROWS))
      island_spec_whole_read(isl, s);
   WSTAT_COUNT(s, 2);
   rt_signal_t *sig = s->sig;
   const uint32_t width = s->width;
   const uint32_t nelems = s->nelems;

   if (s->flags & RT_ISIG_STD_LOGIC9) {
      assert(!(sig->shared.flags & SIG_F_PACKED));
      const uint32_t total = width * nelems;
      const size_t elem_sz = s->elem_size;
      const size_t nbytes = (size_t)total * elem_sz;
      const uint8_t *src = isl->state + s->a_off;
      const uint8_t *cur = sig->shared.data + (size_t)s->sig_offset * elem_sz;
      if (island_bytes_diff(cur, src, nbytes)) {
         WSTAT_COUNT(s, 3);
         deposit_signal(m, sig, src, s->sig_offset, total);
      }
      return;
   }

   const uint32_t nwords = s->nwords;
   const bool two_state = !!(s->flags & RT_ISIG_TWO_STATE);
   const uint64_t *src_a = (const uint64_t *)(isl->state + s->a_off);
   const uint64_t *src_b = two_state
      ? NULL : (const uint64_t *)(isl->state + s->b_off);
   const uint64_t top_mask = island_top_mask(width);

   if (isl->mgr->fast && nelems == 1 && nwords == 1
       && (s->flags & RT_ISIG_SOLE_OWNER)
       && !isl->has_forces && s->ext_write_watch == NULL) {
      const uint64_t a0 = src_a[0] & top_mask;
      const uint64_t b0 = src_b != NULL ? (src_b[0] & top_mask) : 0u;
      if ((s->flags & RT_ISIG_LAST_OUT_VALID)
          && s->last_out_a == a0 && s->last_out_b == b0)
         return;
      s->last_out_a = a0;
      s->last_out_b = b0;
      s->flags |= RT_ISIG_LAST_OUT_VALID;
   }

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
            const uint64_t na = ea[0] & top_mask;
            const uint64_t nb = eb != NULL ? (eb[0] & top_mask) : 0;
            if (isl->mgr->fast) {
               const uint32_t lsb_lo = sig_w - msb_off - width;
               const uint32_t widx = lsb_lo >> 6;
               const uint32_t bshift = lsb_lo & 63u;
               uint64_t ca = cur_a[widx] >> bshift;
               if (bshift + width > 64u)
                  ca |= cur_a[widx + 1u] << (64u - bshift);
               bool changed = ((ca & top_mask) != na);
               if (!changed && sig_is4) {
                  uint64_t cb = cur_b[widx] >> bshift;
                  if (bshift + width > 64u)
                     cb |= cur_b[widx + 1u] << (64u - bshift);
                  changed = ((cb & top_mask) != nb);
               }
               if (!changed)
                  continue;
            }
            rt_island_deposit_packed(m, sig, msb_off, width, na, nb,
                                     PACK_SRC_SCALAR
                                     | (two_state ? PACK_SRC_VEC2 : 0));
         }
         else {
            if (isl->mgr->fast) {
               const uint32_t lsb_lo = sig_w - msb_off - width;
               const uint32_t bshift = lsb_lo & 63u;
               bool changed = false;
               for (uint32_t w = 0; w < nwords && !changed; w++) {
                  const uint64_t msk = (w == nwords - 1u)
                     ? top_mask : ~UINT64_C(0);
                  const uint32_t widx = (lsb_lo >> 6) + w;
                  uint64_t ca = cur_a[widx] >> bshift;
                  if (bshift != 0 && (widx + 1u) < sig_nw)
                     ca |= cur_a[widx + 1u] << (64u - bshift);
                  if ((ca & msk) != (ea[w] & msk))
                     changed = true;
                  else if (sig_is4) {
                     uint64_t cb = cur_b[widx] >> bshift;
                     if (bshift != 0 && (widx + 1u) < sig_nw)
                        cb |= cur_b[widx + 1u] << (64u - bshift);
                     const uint64_t nb = eb != NULL ? (eb[w] & msk) : 0;
                     if ((cb & msk) != nb)
                        changed = true;
                  }
               }
               if (!changed)
                  continue;
            }
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
      const uint64_t a0 = src_a[0] & top_mask;
      const uint64_t b0 = src_b != NULL ? (src_b[0] & top_mask) : 0u;
      if (b0 == 0) {
         const uint8_t base = is_std ? 2u : 0u;
         uint32_t i = 0;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
         if (isl->mgr->fast) {
            const uint64_t base_bcast = is_std ? 0x0202020202020202ULL : 0ULL;
            for (; i + 8u <= width; i += 8u) {
               const uint8_t byte = (uint8_t)(a0 >> (width - 8u - i));
               uint64_t v = ((((uint64_t)byte * 0x0101010101010101ULL)
                              & 0x0102040810204080ULL)
                             + 0x7f7f7f7f7f7f7f7fULL) >> 7;
               v = (v & 0x0101010101010101ULL) + base_bcast;
               memcpy(out_bytes + i, &v, 8);
            }
         }
#endif
         for (; i < width; i++)
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
                                  rt_island_sig_t *s,
                                  uint32_t local_off, uint32_t local_cnt)
{
   if (local_cnt == 0)
      return;
   if (local_off == 0 && local_cnt >= s->width * s->nelems) {
      island_sync_out_sig(m, isl, s);
      return;
   }
   if (unlikely(s->flags & RT_ISIG_SPEC_ROWS))
      island_spec_whole_read(isl, s);
   s->flags &= ~RT_ISIG_LAST_OUT_VALID;

   rt_signal_t *sig = s->sig;
   if (s->flags & RT_ISIG_STD_LOGIC9) {
      assert(!(sig->shared.flags & SIG_F_PACKED));
      const uint32_t sig_off = (uint32_t)s->sig_offset + local_off;
      const size_t elem_sz = s->elem_size;
      const uint8_t *src = isl->state + s->a_off + (size_t)local_off * elem_sz;
      if (memcmp(sig->shared.data + (size_t)sig_off * elem_sz, src,
                 (size_t)local_cnt * elem_sz) != 0)
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

static void island_sync_sig_raw_to(rt_island_t *isl, const rt_island_sig_t *s,
                                   rt_signal_t *sig, int32_t sig_offset,
                                   bool update_last)
{
   // Before the first delta cycle the island state already holds the
   // values of signals written by always_comb blocks, which are published
   // at time zero (see rt_island_eval_sv_reset)
   if (unlikely(isl->mgr->late_pending) && (s->flags & RT_ISIG_RESET_LATE))
      return;

   const uint32_t width = s->width;
   const uint32_t nelems = s->nelems;

   if (s->flags & RT_ISIG_STD_LOGIC9) {
      assert(!(sig->shared.flags & SIG_F_PACKED));
      const size_t elem_sz = s->elem_size;
      const size_t total_bytes = (size_t)width * nelems * elem_sz;
      const size_t byte_off = (size_t)sig_offset * elem_sz;
      uint8_t *dst = sig->shared.data + byte_off;
      memcpy(dst, isl->state + s->a_off, total_bytes);
      if (update_last)
         memcpy(sig->shared.data + sig->shared.size + byte_off,
                dst, total_bytes);
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

      if (nelems == 1 && sig_offset == 0 && width == sig_w) {
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
            const uint32_t msb_bit = (uint32_t)sig_offset + e * width + i;
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
   uint8_t *dst = sig->shared.data + sig_offset;
   uint8_t *last_dst = sig->shared.data + sig->shared.size + sig_offset;

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

static void island_sync_sig_raw(rt_island_t *isl, rt_island_sig_t *s,
                                bool update_last)
{
   s->flags &= ~RT_ISIG_LAST_OUT_VALID;
   island_sync_sig_raw_to(isl, s, s->sig, s->sig_offset, update_last);
}

static void island_sync_sig_raw_slice_to(rt_island_t *isl,
                                         const rt_island_sig_t *s,
                                         rt_signal_t *sig, int32_t sig_offset,
                                         uint32_t local_off, uint32_t local_cnt,
                                         bool update_last)
{
   if (local_cnt == 0)
      return;
   if (unlikely(isl->mgr->late_pending) && (s->flags & RT_ISIG_RESET_LATE))
      return;
   if (local_off == 0 && local_cnt >= s->width * s->nelems) {
      island_sync_sig_raw_to(isl, s, sig, sig_offset, update_last);
      return;
   }

   if (s->flags & RT_ISIG_STD_LOGIC9) {
      assert(!(sig->shared.flags & SIG_F_PACKED));
      const size_t elem_sz = s->elem_size;
      const size_t byte_off = ((size_t)sig_offset + local_off) * elem_sz;
      const size_t byte_cnt = (size_t)local_cnt * elem_sz;
      uint8_t *dst = sig->shared.data + byte_off;
      memcpy(dst, isl->state + s->a_off + (size_t)local_off * elem_sz,
             byte_cnt);
      if (update_last)
         memcpy(sig->shared.data + sig->shared.size + byte_off, dst, byte_cnt);
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
         const uint32_t msb_bit = (uint32_t)sig_offset + pos;
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
   uint8_t *dst = sig->shared.data + sig_offset;
   uint8_t *last_dst = sig->shared.data + sig->shared.size + sig_offset;

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

static void island_sync_sig_raw_slice(rt_island_t *isl,
                                      rt_island_sig_t *s,
                                      uint32_t local_off, uint32_t local_cnt,
                                      bool update_last)
{
   s->flags &= ~RT_ISIG_LAST_OUT_VALID;
   island_sync_sig_raw_slice_to(isl, s, s->sig, s->sig_offset,
                                local_off, local_cnt, update_last);
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

// Element write log of an SV island memory in the next-state half (see
// RT_ISLAND_TOUCH_CAP): it directly follows the planes of the signal
static inline uint64_t *island_touch_log(rt_island_t *isl,
                                         const rt_island_sig_t *s)
{
   const uint32_t plane = s->nwords * s->nelems * 8u;
   const uint32_t off =
      ((s->flags & RT_ISIG_TWO_STATE) ? s->a_off : s->b_off) + plane;
   assert(isl->next_state != NULL);
   assert(off + RT_ISLAND_TOUCH_BYTES <= isl->state_bytes);
   return (uint64_t *)(isl->next_state + off);
}

// A force or release may leave the next state of any element different from
// the current one: the next commit of each memory compares all of it
static void island_touch_invalidate(rt_island_t *isl)
{
   if (isl->next_state == NULL)
      return;
   for (uint32_t i = 0; i < isl->nsigs; i++) {
      const rt_island_sig_t *s = &(isl->sigs[i]);
      if (s->flags & RT_ISIG_TOUCH_LOG)
         island_touch_log(isl, s)[0] = RT_ISLAND_TOUCH_CAP + 1;
   }
}

static inline void island_sync_force_hdr(rt_island_t *isl)
{
   island_touch_invalidate(isl);

   if (isl->nsigs == 0 || (isl->sigs[0].flags & RT_ISIG_VHDL)
       || isl->sigs[0].a_off < sizeof(uint64_t))
      return;
   const uint64_t v = isl->has_forces ? (uint64_t)(uintptr_t)isl : 0;
   *(uint64_t *)(isl->state) = v;
   if (isl->next_state != NULL)
      *(uint64_t *)(isl->next_state) = v;
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
      const uint32_t val_words = (total * s->elem_size + 7u) / 8u;
      uint64_t *mem = xcalloc_array(mask_words + val_words * 2u,
                                    sizeof(uint64_t));
      f->mask = mem;
      f->a = mem + mask_words;
      f->b = NULL;
      f->saved_a = mem + mask_words + val_words;
      f->saved_b = NULL;
   }
   else {
      const uint32_t total_words = s->nwords * s->nelems;
      uint64_t *mem = xcalloc_array(total_words * 5u, sizeof(uint64_t));
      f->mask = mem;
      f->a = mem + total_words;
      f->b = mem + total_words * 2u;
      f->saved_a = mem + total_words * 3u;
      f->saved_b = mem + total_words * 4u;
   }
   if (!isl->has_forces) {
      for (uint32_t i = 0; i < isl->nsigs; i++)
         isl->sigs[i].flags &= ~RT_ISIG_LAST_OUT_VALID;
      isl->has_forces = true;
      isl->mgr->n_forced_islands++;
      island_sync_force_hdr(isl);
   }
   return f;
}

static void island_apply_force_entry(uint8_t *base, const rt_island_sig_t *s,
                                     rt_island_force_t *f, bool save_unforced)
{
   if (s->flags & RT_ISIG_STD_LOGIC9) {
      const uint32_t total = s->width * s->nelems;
      const size_t elem_sz = s->elem_size;
      uint8_t *dst = base + s->a_off;
      const uint8_t *fa = (const uint8_t *)f->a;
      uint8_t *sa = (uint8_t *)f->saved_a;
      for (uint32_t k = 0; k < total; k++) {
         if (f->mask[k >> 6] & (UINT64_C(1) << (k & 63u))) {
            if (save_unforced)
               memcpy(sa + k * elem_sz, dst + k * elem_sz, elem_sz);
            memcpy(dst + k * elem_sz, fa + k * elem_sz, elem_sz);
         }
      }
      return;
   }

   const uint32_t total_words = s->nwords * s->nelems;
   uint64_t *ap = (uint64_t *)(base + s->a_off);
   for (uint32_t w = 0; w < total_words; w++) {
      const uint64_t m = f->mask[w];
      if (m) {
         if (save_unforced)
            f->saved_a[w] = (f->saved_a[w] & ~m) | (ap[w] & m);
         ap[w] = (ap[w] & ~m) | (f->a[w] & m);
      }
   }
   if (!(s->flags & RT_ISIG_TWO_STATE)) {
      uint64_t *bp = (uint64_t *)(base + s->b_off);
      for (uint32_t w = 0; w < total_words; w++) {
         const uint64_t m = f->mask[w];
         if (m) {
            if (save_unforced)
               f->saved_b[w] = (f->saved_b[w] & ~m) | (bp[w] & m);
            bp[w] = (bp[w] & ~m) | (f->b[w] & m);
         }
      }
   }
}

static void island_restore_saved_force(uint8_t *base, const rt_island_sig_t *s,
                                       const rt_island_force_t *f)
{
   if (s->flags & RT_ISIG_STD_LOGIC9) {
      const uint32_t total = s->width * s->nelems;
      const size_t elem_sz = s->elem_size;
      uint8_t *dst = base + s->a_off;
      const uint8_t *sa = (const uint8_t *)f->saved_a;
      for (uint32_t k = 0; k < total; k++) {
         if (f->mask[k >> 6] & (UINT64_C(1) << (k & 63u)))
            memcpy(dst + k * elem_sz, sa + k * elem_sz, elem_sz);
      }
      return;
   }

   const uint32_t total_words = s->nwords * s->nelems;
   uint64_t *ap = (uint64_t *)(base + s->a_off);
   for (uint32_t w = 0; w < total_words; w++) {
      const uint64_t m = f->mask[w];
      if (m)
         ap[w] = (ap[w] & ~m) | (f->saved_a[w] & m);
   }
   if (!(s->flags & RT_ISIG_TWO_STATE)) {
      uint64_t *bp = (uint64_t *)(base + s->b_off);
      for (uint32_t w = 0; w < total_words; w++) {
         const uint64_t m = f->mask[w];
         if (m)
            bp[w] = (bp[w] & ~m) | (f->saved_b[w] & m);
      }
   }
}

static void island_apply_forces_comb(rt_island_t *isl)
{
   for (uint32_t i = 0; i < isl->n_forces; i++) {
      rt_island_force_t *f = &(isl->forces[i]);
      const rt_island_sig_t *s = &(isl->sigs[f->sig_idx]);
      island_apply_force_entry(isl->state, s, f, false);
   }
}

void rt_island_apply_forces(void *handle)
{
   rt_island_t *isl = handle;
   if (isl != NULL && isl->has_forces)
      island_apply_forces_comb(isl);
}

static inline void island_copy_sig_buf(uint8_t *dst_base,
                                       const uint8_t *src_base,
                                       const rt_island_sig_t *s)
{
   if (s->flags & RT_ISIG_STD_LOGIC9) {
      memcpy(dst_base + s->a_off, src_base + s->a_off,
             (size_t)s->width * s->nelems * s->elem_size);
      return;
   }
   const size_t bytes = (size_t)s->nwords * s->nelems * sizeof(uint64_t);
   if (unlikely(s->flags & RT_ISIG_SPEC_ROWF)) {
      // Mode A 2D signal: the row clean bytes follow the rows
      memcpy(dst_base + s->a_off, src_base + s->a_off,
             bytes + RT_ISLAND_SPEC_ROWF_BYTES(s->nelems));
      return;
   }
   memcpy(dst_base + s->a_off, src_base + s->a_off, bytes);
   if (!(s->flags & RT_ISIG_TWO_STATE))
      memcpy(dst_base + s->b_off, src_base + s->b_off, bytes);
}

__attribute__((noinline))
static bool island_commit_bytes_other(uint8_t * restrict dst,
                                      const uint8_t * restrict src, size_t n)
{
   if (n <= 8) {
      if (n == 2) {
         uint16_t d2, s2;
         memcpy(&d2, dst, 2);
         memcpy(&s2, src, 2);
         if (d2 != s2) {
            memcpy(dst, &s2, 2);
            return true;
         }
         return false;
      }
      if (n >= 5) {
         uint32_t d0, d1, s0, s1;
         memcpy(&d0, dst, 4);         memcpy(&d1, dst + n - 4, 4);
         memcpy(&s0, src, 4);         memcpy(&s1, src + n - 4, 4);
         if (((d0 ^ s0) | (d1 ^ s1)) != 0) {
            memcpy(dst, &s0, 4);
            memcpy(dst + n - 4, &s1, 4);
            return true;
         }
         return false;
      }
      if (n == 3) {
         uint16_t d0, s0;
         memcpy(&d0, dst, 2);
         memcpy(&s0, src, 2);
         if (d0 != s0 || dst[2] != src[2]) {
            memcpy(dst, &s0, 2);
            dst[2] = src[2];
            return true;
         }
         return false;
      }
      return false;
   }
   if (n <= 16) {
      uint64_t d0, d1, s0, s1;
      memcpy(&d0, dst, 8);         memcpy(&d1, dst + n - 8, 8);
      memcpy(&s0, src, 8);         memcpy(&s1, src + n - 8, 8);
      if (((d0 ^ s0) | (d1 ^ s1)) != 0) {
         memcpy(dst, &s0, 8);
         memcpy(dst + n - 8, &s1, 8);
         return true;
      }
      return false;
   }
   if (n < 32) {
      uint64_t d0, d1, d2, d3, s0, s1, s2, s3;
      memcpy(&d0, dst, 8);          memcpy(&d1, dst + 8, 8);
      memcpy(&d2, dst + n - 16, 8); memcpy(&d3, dst + n - 8, 8);
      memcpy(&s0, src, 8);          memcpy(&s1, src + 8, 8);
      memcpy(&s2, src + n - 16, 8); memcpy(&s3, src + n - 8, 8);
      if (((d0 ^ s0) | (d1 ^ s1) | (d2 ^ s2) | (d3 ^ s3)) != 0) {
         memcpy(dst, &s0, 8);          memcpy(dst + 8, &s1, 8);
         memcpy(dst + n - 16, &s2, 8); memcpy(dst + n - 8, &s3, 8);
         return true;
      }
      return false;
   }
   if (n <= 64) {
      size_t off = 0;
      for (; off + 8u <= n; off += 8u) {
         uint64_t d, s;
         memcpy(&d, dst + off, 8);
         memcpy(&s, src + off, 8);
         if (d != s) {
            memcpy(dst + off, &s, 8);
            for (off += 8u; off + 8u <= n; off += 8u)
               memcpy(dst + off, src + off, 8);
            if (off < n)
               memcpy(dst + n - 8u, src + n - 8u, 8);
            return true;
         }
      }
      if (off < n) {
         uint64_t d, s;
         memcpy(&d, dst + n - 8u, 8);
         memcpy(&s, src + n - 8u, 8);
         if (d != s) {
            memcpy(dst + n - 8u, &s, 8);
            return true;
         }
      }
      return false;
   }
   if (memcmp(dst, src, n) != 0) {
      memcpy(dst, src, n);
      return true;
   }
   return false;
}

static inline __attribute__((always_inline)) bool
island_commit_bytes(uint8_t * restrict dst,
                    const uint8_t * restrict src, size_t n)
{
   if (n == 1) {
      if (*dst != *src) {
         *dst = *src;
         return true;
      }
      return false;
   }
   if (n == 32) {
      uint64_t d0, d1, d2, d3, s0, s1, s2, s3;
      memcpy(&d0, dst, 8);      memcpy(&d1, dst + 8, 8);
      memcpy(&d2, dst + 16, 8); memcpy(&d3, dst + 24, 8);
      memcpy(&s0, src, 8);      memcpy(&s1, src + 8, 8);
      memcpy(&s2, src + 16, 8); memcpy(&s3, src + 24, 8);
      if (((d0 ^ s0) | (d1 ^ s1) | (d2 ^ s2) | (d3 ^ s3)) != 0) {
         memcpy(dst, &s0, 8);      memcpy(dst + 8, &s1, 8);
         memcpy(dst + 16, &s2, 8); memcpy(dst + 24, &s3, 8);
         return true;
      }
      return false;
   }
   if (n == 4) {
      uint32_t d4, s4;
      memcpy(&d4, dst, 4);
      memcpy(&s4, src, 4);
      if (d4 != s4) {
         memcpy(dst, &s4, 4);
         return true;
      }
      return false;
   }
   if (n == 8) {
      uint64_t d8, s8;
      memcpy(&d8, dst, 8);
      memcpy(&s8, src, 8);
      if (d8 != s8) {
         memcpy(dst, &s8, 8);
         return true;
      }
      return false;
   }
   return island_commit_bytes_other(dst, src, n);
}

__attribute__((noinline))
static bool island_commit_words_slow(rt_island_t *isl,
                                     const rt_island_ff_desc_t *d)
{
   const uint32_t total_words = d->count;
   uint64_t *dst_a = (uint64_t *)(isl->state + d->a_off);
   const uint64_t *src_a = (const uint64_t *)(isl->next_state + d->a_off);

   if (d->flags & RT_IFF_TWO_STATE) {
      if (likely(total_words == 1)) {
         if (dst_a[0] != src_a[0]) {
            dst_a[0] = src_a[0];
            return true;
         }
         return false;
      }
      bool changed = false;
      for (uint32_t i = 0; i < total_words; i++) {
         if (dst_a[i] != src_a[i]) {
            dst_a[i] = src_a[i];
            changed = true;
         }
      }
      return changed;
   }

   const uint32_t b_off = likely(d->b_delta != 0xFFFFu)
      ? (d->a_off + ((uint32_t)d->b_delta << 3))
      : isl->sigs[d->sig_idx].b_off;
   uint64_t *dst_b = (uint64_t *)(isl->state + b_off);
   const uint64_t *src_b = (const uint64_t *)(isl->next_state + b_off);
   if (likely(total_words == 1)) {
      if (dst_a[0] != src_a[0] || dst_b[0] != src_b[0]) {
         dst_a[0] = src_a[0];
         dst_b[0] = src_b[0];
         return true;
      }
      return false;
   }
   bool changed = false;
   for (uint32_t i = 0; i < total_words; i++) {
      if (dst_a[i] != src_a[i] || dst_b[i] != src_b[i]) {
         dst_a[i] = src_a[i];
         dst_b[i] = src_b[i];
         changed = true;
      }
   }
   return changed;
}

// Commit of a Mode A (packed two-state) register: the common one and two word
// cases are compared and copied inline, wider ones with a single OR-reduced
// compare before the copy.
static inline bool island_commit_words(rt_island_t *isl,
                                       const rt_island_ff_desc_t *d)
{
   if (likely(d->flags & RT_IFF_TWO_STATE)) {
      uint64_t *dst = (uint64_t *)(isl->state + d->a_off);
      const uint64_t *src = (const uint64_t *)(isl->next_state + d->a_off);
      const uint32_t n = d->count;
      if (likely(n == 1)) {
         if (dst[0] == src[0])
            return false;
         dst[0] = src[0];
         return true;
      }
      if (n == 2) {
         if (((dst[0] ^ src[0]) | (dst[1] ^ src[1])) == 0)
            return false;
         dst[0] = src[0];
         dst[1] = src[1];
         return true;
      }
      uint64_t diff = 0;
      for (uint32_t i = 0; i < n; i++)
         diff |= dst[i] ^ src[i];
      if (diff == 0)
         return false;
      memcpy(dst, src, n * sizeof(uint64_t));
      return true;
   }
   return island_commit_words_slow(isl, d);
}

static inline bool island_commit_desc_buf(rt_island_t *isl,
                                          const rt_island_ff_desc_t *d)
{
   if (likely(d->flags & RT_IFF_STD_LOGIC9))
      return island_commit_bytes(isl->state + d->a_off,
                                 isl->next_state + d->a_off,
                                 d->count);
   return island_commit_words(isl, d);
}

static inline bool island_copy_direct_sig_same(const rt_island_sig_t *src_s,
                                               rt_island_sig_t *dst_s)
{
   const rt_island_t *src_isl = src_s->isl;
   rt_island_t *dst_isl = dst_s->isl;
   if (src_s->flags & RT_ISIG_STD_LOGIC9) {
      const size_t total = (size_t)src_s->width * src_s->nelems * src_s->elem_size;
      if (!island_commit_bytes(dst_isl->state + dst_s->a_off,
                               src_isl->state + src_s->a_off, total))
         return false;
      island_act_mark_sig(dst_isl, dst_s);
      return true;
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
      if (changed)
         island_act_mark_sig(dst_isl, dst_s);
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
   return changed;   // Four-state: never a VHDL island signal
}

static bool island_spec_copy_mixed(const rt_island_sig_t *src_s,
                                   rt_island_sig_t *dst_s);

static inline bool island_copy_direct_sig(const rt_island_sig_t *src_s,
                                          rt_island_sig_t *dst_s)
{
   if (unlikely(src_s->flags & RT_ISIG_SPEC_MIX))
      return island_spec_copy_mixed(src_s, dst_s);
   return island_copy_direct_sig_same(src_s, dst_s);
}

static inline bool island_copy_direct_desc(const rt_island_direct_desc_t *dd)
{
   if (likely(dd->flags & RT_IDIR_STD_LOGIC9)) {
      // A wire between a Mode A and a Mode B island is a VHDL one too
      if (unlikely(dd->flags & RT_IDIR_MIX))
         return island_spec_copy_mixed(dd->mix_src, dd->mix_dst);
      return island_commit_bytes(dd->dst_a, dd->src_a, dd->count);
   }

   const uint32_t total_words = dd->count;
   const uint64_t *sa = (const uint64_t *)dd->src_a;
   uint64_t *da = (uint64_t *)dd->dst_a;

   if (dd->flags & RT_IDIR_TWO_STATE) {
      if (likely(total_words == 1)) {
         if (da[0] != sa[0]) {
            da[0] = sa[0];
            return true;
         }
         return false;
      }
      bool changed = false;
      for (uint32_t i = 0; i < total_words; i++) {
         if (da[i] != sa[i]) {
            da[i] = sa[i];
            changed = true;
         }
      }
      return changed;
   }

   const uint64_t *sb = (const uint64_t *)dd->src_b;
   uint64_t *db = (uint64_t *)dd->dst_b;
   if (likely(total_words == 1)) {
      if (da[0] != sa[0] || db[0] != sb[0]) {
         da[0] = sa[0];
         db[0] = sb[0];
         return true;
      }
      return false;
   }
   bool changed = false;
   for (uint32_t i = 0; i < total_words; i++) {
      if (da[i] != sa[i] || db[i] != sb[i]) {
         da[i] = sa[i];
         db[i] = sb[i];
         changed = true;
      }
   }
   return changed;
}

static inline void island_defer_dirty(rt_island_t *isl);

// A reader of a RT_ISIG_DIRECT_PUB source runs when it would have through
// its input watch on the kernel signal (rt_island_input_watch_inline): in
// the next delta cycle when the source changed in the blocking update phase
static inline void island_direct_wake(rt_island_t *dst_isl, bool pub)
{
   if (pub && model_blocking_update(dst_isl->model))
      island_defer_dirty(dst_isl);
   else
      island_mark_dirty(dst_isl);
}

__attribute__((noinline))
static void island_direct_wake_desc(const rt_island_direct_desc_t *dd)
{
   if (dd->flags & RT_IDIR_ACT)
      dd->dst_isl->state[dd->flags >> RT_IDIR_IDX_SHIFT] = 1;
   island_direct_wake(dd->dst_isl, !!(dd->flags & RT_IDIR_PUB));
}

static inline void island_propagate_direct_comb(rt_island_t *isl)
{
   const uint32_t n = isl->n_direct_comb_descs;
   const rt_island_direct_desc_t *dd = isl->direct_comb_descs;
   for (uint32_t j = 0; j < n; j++, dd++) {
      if (island_copy_direct_desc(dd) && dd->dst_isl != NULL) {
         if (likely(!(dd->flags & (RT_IDIR_ACT | RT_IDIR_PUB))))
            island_mark_dirty(dd->dst_isl);
         else
            island_direct_wake_desc(dd);
      }
   }
}

static void island_apply_direct_outs(rt_island_t *isl)
{
   if (isl->n_direct_comb_descs > 0) {
      island_propagate_direct_comb(isl);
      return;
   }

   for (uint32_t j = 0; j < isl->n_direct_comb_outs; j++) {
      const rt_island_sig_t *src_s = &(isl->sigs[isl->direct_comb_outs[j]]);
      if (!(src_s->flags & RT_ISIG_DIRECT_LINK))
         continue;
      for (uint32_t t = 0; t < src_s->n_direct_targets; t++) {
         rt_island_sig_t *dst_s = src_s->direct_targets[t];
         if (island_copy_direct_sig(src_s, dst_s)
             && (dst_s->flags & RT_ISIG_COMB_READ)
             && dst_s->isl->eval_handle != JIT_HANDLE_INVALID)
            island_direct_wake(dst_s->isl,
                               !!(src_s->flags & RT_ISIG_DIRECT_PUB));
      }
   }
}

static inline void island_defer_dirty(rt_island_t *isl)
{
   if (!isl->in_defer_dirty) {
      isl->in_defer_dirty = true;
      APUSH(isl->mgr->defer_dirty_q, isl);
   }
   rt_island_request_delta(isl->model);
}

// A VHDL signal assignment takes effect one delta cycle after the process
// that executes it.  VHDL islands therefore evaluate their combinational
// processes readers-first (reverse topological order, see
// vi_build_comb_unit) so every process sees the values of the current
// delta, and run again in the next delta when a signal that is both
// written and read by combinational processes of the island changed.  This
// keeps the delta cycle of every value change identical to
// --rtl-islands=off.
static bool island_sig_buf_differs(const uint8_t *a_base, const uint8_t *b_base,
                                   const rt_island_sig_t *s)
{
   if (s->flags & RT_ISIG_STD_LOGIC9)
      return memcmp(a_base + s->a_off, b_base + s->a_off,
                    (size_t)s->width * s->nelems * s->elem_size) != 0;
   const size_t bytes = (size_t)s->nwords * s->nelems * sizeof(uint64_t);
   if (memcmp(a_base + s->a_off, b_base + s->a_off, bytes) != 0)
      return true;
   return !(s->flags & RT_ISIG_TWO_STATE)
      && memcmp(a_base + s->b_off, b_base + s->b_off, bytes) != 0;
}

static bool island_check_feedback(rt_island_t *isl)
{
   bool changed = false;
   for (uint32_t j = 0; j < isl->n_fb_sigs; j++) {
      const rt_island_sig_t *s = &(isl->sigs[isl->fb_sigs[j]]);
      if (island_sig_buf_differs(isl->state, isl->fb_shadow, s)) {
         island_copy_sig_buf(isl->fb_shadow, isl->state, s);
         island_act_mark(isl, isl->fb_sigs[j]);
         changed = true;
      }
   }
   if (changed && unlikely(island_act_stats) && isl->act_on)
      island_act_info(isl)->fb_reruns++;
   return changed;
}

static inline void island_after_comb(rt_island_t *isl)
{
   if (unlikely(isl->n_fb_sigs > 0) && island_check_feedback(isl))
      island_defer_dirty(isl);
}

static inline void island_comb_direct_outs(rt_island_t *isl)
{
   if (isl->is_vhdl) {
      if (!isl->in_defer_direct) {
         isl->in_defer_direct = true;
         APUSH(isl->mgr->defer_direct_q, isl);
      }
      rt_island_request_delta(isl->model);
   }
   else
      island_propagate_direct_comb(isl);
}

static void island_init_feedback(rt_island_t *isl)
{
   free(isl->fb_sigs);
   free(isl->fb_shadow);
   isl->fb_sigs = NULL;
   isl->fb_shadow = NULL;
   isl->n_fb_sigs = 0;

   if (isl->eval_handle == JIT_HANDLE_INVALID)
      return;

   const uint32_t mask = RT_ISIG_VHDL | RT_ISIG_COMB_WRITTEN | RT_ISIG_COMB_READ;
   for (uint32_t j = 0; j < isl->nsigs; j++) {
      const rt_island_sig_t *s = &(isl->sigs[j]);
      if ((s->flags & mask) != mask || (s->flags & RT_ISIG_SPARSE_RAM))
         continue;
      if (isl->fb_sigs == NULL)
         isl->fb_sigs = xmalloc_array(isl->nsigs, sizeof(uint32_t));
      isl->fb_sigs[isl->n_fb_sigs++] = j;
   }

   if (isl->n_fb_sigs > 0) {
      isl->fb_shadow = xmalloc(MAX(1u, isl->state_bytes));
      memcpy(isl->fb_shadow, isl->state, isl->state_bytes);
   }
}

// Process activity (see RT_ISLAND_ACT_BYTES): every runtime path that
// writes a new value into the state of a VHDL island signal (input sync,
// flip-flop commit, direct wire, the feedback check after a comb call)
// sets its change byte with island_act_mark, and the bytes are cleared
// after each comb call.  The comb function skips a process when no signal
// it reads has its change byte set, which is exact because a combinational
// process computes its outputs from the signals it reads only.  Writes
// that may need the writers to run again (forces, deposits, releases, the
// first call) set the run-all byte instead.  NVC_ISLAND_ACT_CHECK (on by
// default in debug builds) keeps a copy of the state and checks that no
// change was missed.
static void island_init_act(rt_island_t *isl)
{
   rt_island_mgr_t *mgr = isl->mgr;
   if (isl->isl_id < mgr->n_act_info) {
      island_act_info_t *ai = &(mgr->act_info[isl->isl_id]);
      free(ai->sigs);
      free(ai->shadow);
      memset(ai, 0, sizeof(island_act_info_t));
   }
   isl->act_on = false;

   // Every VHDL island comb function is built with the change bytes
   if (isl->eval_handle == JIT_HANDLE_INVALID || !isl->is_vhdl
       || isl->nsigs == 0 || !(isl->sigs[0].flags & RT_ISIG_VHDL))
      return;

   if (isl->isl_id >= mgr->n_act_info) {
      const uint32_t n = MAX(isl->isl_id + 1, mgr->islands.count);
      mgr->act_info = xrealloc_array(mgr->act_info, n,
                                     sizeof(island_act_info_t));
      memset(mgr->act_info + mgr->n_act_info, 0,
             (n - mgr->n_act_info) * sizeof(island_act_info_t));
      mgr->n_act_info = n;
   }

   island_act_info_t *ai = &(mgr->act_info[isl->isl_id]);
   ai->all = true;
   isl->act_on = true;
   for (uint32_t j = 0; j < isl->nsigs; j++) {
      const rt_island_sig_t *s = &(isl->sigs[j]);
      if (!(s->flags & RT_ISIG_ACT))
         continue;
      // The value of a VHDL island signal fills whole 64-bit words, the
      // padding of a std_logic vector is never written
      uint32_t nbytes;
      if (s->flags & RT_ISIG_STD_LOGIC9)
         nbytes = ALIGN_UP(s->width * s->nelems * s->elem_size, 8u);
      else if (s->flags & RT_ISIG_TWO_STATE)
         nbytes = s->nwords * s->nelems * 8u;
      else {
         // Not a VHDL island layout: run every process on every call
         ai->every = true;
         continue;
      }
      assert(s->a_off % 8u == 0 && s->a_off + nbytes <= isl->state_bytes);
      if (ai->sigs == NULL)
         ai->sigs = xmalloc_array(isl->nsigs, sizeof(rt_island_act_t));
      ai->sigs[ai->n_sigs++] = (rt_island_act_t){
         .off    = s->a_off,
         .nwords = nbytes / 8u,
         .idx    = j,
      };
   }

   assert(RT_ISLAND_ACT_BYTES(isl->nsigs) <= isl->state_bytes);
   if (island_act_check) {
      ai->shadow = xmalloc(MAX(1u, isl->state_bytes));
      memcpy(ai->shadow, isl->state, isl->state_bytes);
   }
}

static void island_act_verify(rt_island_t *isl, bool all)
{
   // NVC_ISLAND_ACT_CHECK: every signal whose value differs from the one
   // the previous call saw must have its change byte set
   const island_act_info_t *ai = island_act_info(isl);
   uint8_t *chg = isl->state;
   uint8_t *shadow = ai->shadow;
   for (uint32_t j = 0; j < ai->n_sigs; j++) {
      const rt_island_act_t *a = &(ai->sigs[j]);
      const uint64_t *cur = (const uint64_t *)(chg + a->off);
      uint64_t *old = (uint64_t *)(shadow + a->off);
      uint64_t diff = 0;
      for (uint32_t w = 0; w < a->nwords; w++)
         diff |= cur[w] ^ old[w];
      if (diff == 0)
         continue;
      if (!all && !chg[a->idx])
         fatal_trace("island %u signal %u (flags %x) changed without its "
                     "change byte", isl->isl_id, a->idx,
                     isl->sigs[a->idx].flags);
      for (uint32_t w = 0; w < a->nwords; w++)
         old[w] = cur[w];
   }
}

static inline void island_act_prepare(rt_island_t *isl)
{
   island_act_info_t *ai = island_act_info(isl);
   const bool all = ai->all || ai->every || isl->has_forces
      || !island_act_enabled;
   ai->all = false;
   isl->state[RT_ISLAND_ACT_ALL(isl->nsigs)] = all;

   if (unlikely(island_act_check))
      island_act_verify(isl, all);

   if (unlikely(island_act_stats)) {
      ai->calls++;
      ai->all_calls += all;
   }
}

static inline void island_act_done(rt_island_t *isl)
{
   memset(isl->state, 0, isl->nsigs);
}

static inline void island_act_invalidate(rt_island_t *isl)
{
   if (isl->act_on)
      island_act_info(isl)->all = true;
}

static void island_act_report(rt_island_mgr_t *mgr)
{
   // NVC_ISLAND_ACT_STATS: the process counters are only updated by comb
   // functions lowered with the same variable set
   uint64_t calls = 0, all = 0, fb = 0, run = 0, visit = 0;
   uint32_t nisl = 0;
   for (unsigned i = 0; i < mgr->islands.count; i++) {
      const rt_island_t *isl = mgr->islands.items[i];
      if (!isl->act_on)
         continue;
      nisl++;
      const island_act_info_t *ai = &(mgr->act_info[isl->isl_id]);
      calls += ai->calls;
      all += ai->all_calls;
      fb += ai->fb_reruns;
      const uint64_t *cnt =
         (const uint64_t *)(isl->state + RT_ISLAND_ACT_CNT(isl->nsigs));
      run += cnt[0];
      visit += cnt[1];
   }
   fprintf(stderr, "island act: %u islands, %"PRIu64" comb calls (%"PRIu64
           " run-all), %"PRIu64" feedback re-runs, %"PRIu64" processes "
           "visited, %"PRIu64" run (%.1f%%)\n", nisl, calls, all, fb, visit,
           run, visit ? 100.0 * run / visit : 0.0);
}

static void island_refresh_direct_comb_descs(rt_island_t *isl)
{
   isl->direct_act = false;
   uint32_t total = 0;
   for (uint32_t j = 0; j < isl->n_direct_comb_outs; j++) {
      const rt_island_sig_t *src_s = &(isl->sigs[isl->direct_comb_outs[j]]);
      if (!(src_s->flags & RT_ISIG_DIRECT_LINK))
         continue;
      total += src_s->n_direct_targets;
   }
   // Rebuilt in place when the number is unchanged: a speculative Mode A
   // switch (island_spec_rebuild) may happen while they are walked
   if (total != isl->n_direct_comb_descs || total == 0) {
      free(isl->direct_comb_descs);
      isl->direct_comb_descs = NULL;
      isl->n_direct_comb_descs = 0;
      if (total == 0)
         return;
      isl->direct_comb_descs = xmalloc_array(total,
                                             sizeof(rt_island_direct_desc_t));
   }
   uint32_t pos = 0;
   for (uint32_t j = 0; j < isl->n_direct_comb_outs; j++) {
      const rt_island_sig_t *src_s = &(isl->sigs[isl->direct_comb_outs[j]]);
      if (!(src_s->flags & RT_ISIG_DIRECT_LINK))
         continue;
      uint32_t fl = 0;
      if (src_s->flags & RT_ISIG_STD_LOGIC9)
         fl |= RT_IDIR_STD_LOGIC9;
      else if (src_s->flags & RT_ISIG_TWO_STATE)
         fl |= RT_IDIR_TWO_STATE;
      const uint32_t cnt = (fl & RT_IDIR_STD_LOGIC9)
         ? (src_s->width * src_s->nelems * src_s->elem_size)
         : (src_s->nwords * src_s->nelems);
      const uint8_t *sa = isl->state + src_s->a_off;
      const uint8_t *sb = (fl & (RT_IDIR_STD_LOGIC9 | RT_IDIR_TWO_STATE))
         ? NULL : (isl->state + src_s->b_off);

      for (uint32_t t = 0; t < src_s->n_direct_targets; t++) {
         rt_island_sig_t *dst_s = src_s->direct_targets[t];
         rt_island_t *disl = dst_s->isl;
         rt_island_direct_desc_t *dd = &(isl->direct_comb_descs[pos++]);
         dd->src_a = sa;
         dd->dst_a = disl->state + dst_s->a_off;
         dd->src_b = sb;
         dd->dst_b = (sb != NULL) ? (disl->state + dst_s->b_off) : NULL;
         dd->dst_isl = ((dst_s->flags & RT_ISIG_COMB_READ)
                        && disl->eval_handle != JIT_HANDLE_INVALID)
            ? disl : NULL;
         dd->count = cnt;
         dd->flags = fl;
         if (src_s->flags & RT_ISIG_DIRECT_PUB)
            dd->flags |= RT_IDIR_PUB;
         if (((src_s->flags ^ dst_s->flags)
              & (RT_ISIG_TWO_STATE | RT_ISIG_STD_LOGIC9))
             || (dst_s->flags & RT_ISIG_SPEC_HELD)) {
            dd->flags = RT_IDIR_STD_LOGIC9 | RT_IDIR_MIX
               | (dd->flags & RT_IDIR_PUB);
            dd->mix_src = src_s;
            dd->mix_dst = dst_s;
         }
         if (dst_s->flags & RT_ISIG_ACT) {
            assert(dst_s->sig_idx < (UINT32_C(1) << (32 - RT_IDIR_IDX_SHIFT)));
            dd->flags |= RT_IDIR_ACT | (dst_s->sig_idx << RT_IDIR_IDX_SHIFT);
            isl->direct_act = true;
         }
      }
   }
   isl->n_direct_comb_descs = pos;
}

////////////////////////////////////////////////////////////////////////////////
// Speculative Mode A at run time (islands elaborated with NVC_ISLAND_SPEC=1)
//
// An island with a Mode A variant runs Mode B from time zero.  At a
// promotion check (rt_island_promote_deferred, the start of a delta cycle,
// when no flip-flop commit of the island is pending) every live signal of
// its state must be '0'/'1': the state is then packed in place to the Mode A
// layout, the island signals take the Mode A offsets and flags
// (RT_ISIG_SPEC_A), the island calls its Mode A functions and every
// descriptor that points into its state is rebuilt.  A Mode A island never
// holds a value other than '0'/'1': a signal that is not clean keeps the
// whole island in Mode B, and every way a value enters the island checks it
// first and converts the island back to Mode B (exact unpack) before the
// value is stored:
//  - kernel inputs (island_sync_in_sig) and direct wires from a Mode B
//    island (island_spec_copy_mixed) check the bytes word-wise;
//  - the Mode A functions return a deopt code instead of computing what
//    two-state storage cannot hold (non-01 literal, failing check, report);
//    the comb call is undone (the signals comb processes read back are
//    restored from the feedback shadow, the others are recomputed) and the
//    flip-flop call's next-state writes are discarded, then the Mode B
//    function runs in the same delta;
//  - forces, deposits and releases pin the island to Mode B before they
//    touch it.
// Values leave the island the same way in both modes (unpacked exactly to
// the kernel signal, which observers such as waves and VPI read).
// A switch never schedules anything and changes no value, so every value,
// event and delta cycle is the one of Mode B.  Repeated deopts back off
// exponentially and pin the island to Mode B.

#define ISLAND_SPEC_PIN_DEOPTS   32u
#define ISLAND_SPEC_PIN_REPORTS  4u

// Promotion has a cost: the state is converted twice, every descriptor of
// the island and its neighbours is rebuilt, and holding a value needs the
// checked functions ("$ah"), lowered on their first call.  Values that are
// not '0'/'1' just after time zero mostly clear when reset ends, so an
// island that would hold values waits ISLAND_SPEC_HOLD_WAIT ticks from its
// first such check before it is promoted holding them.  An island that
// deopts within ISLAND_SPEC_QUICK ticks of ISLAND_SPEC_COST_PIN successive
// promotions loses more than it gains: it is retried only every
// ISLAND_SPEC_COST_RETRY ticks until it stays in Mode A longer.
#define ISLAND_SPEC_HOLD_WAIT    1024u
#define ISLAND_SPEC_QUICK        64u
#define ISLAND_SPEC_COST_PIN     4u
#define ISLAND_SPEC_COST_RETRY   (UINT64_C(1) << 20)
#define ISLAND_SPEC_MAX_IVAL     (UINT32_C(1) << 14)
#define ISLAND_SPEC_TIMELINE     40u

static int island_spec_stress = -1;

// NVC_ISLAND_SPEC_STRESS=N: a promotion check on every delta and a deopt
// of every Mode A island each N deltas (test knob)
static uint32_t island_spec_stress_n(void)
{
   if (island_spec_stress < 0) {
      const char *env = getenv("NVC_ISLAND_SPEC_STRESS");
      island_spec_stress = env != NULL ? MAX(0, atoi(env)) : 0;
   }
   return (uint32_t)island_spec_stress;
}

static int64_t island_spec_hold_wait_env = -1;

// Ticks an island waits before it is promoted holding values;
// NVC_ISLAND_SPEC_HOLD_WAIT=N overrides it (test knob)
static uint64_t island_spec_hold_wait(void)
{
   if (island_spec_hold_wait_env < 0) {
      const char *env = getenv("NVC_ISLAND_SPEC_HOLD_WAIT");
      island_spec_hold_wait_env =
         env != NULL ? MAX(0, atoi(env)) : ISLAND_SPEC_HOLD_WAIT;
   }
   return (uint64_t)island_spec_hold_wait_env;
}

static uint8_t *island_spec_scratch(size_t n)
{
   if (n > island_spec_tmp_size) {
      island_spec_tmp_size = MAX(n, 256u);
      island_spec_tmp = xrealloc(island_spec_tmp, island_spec_tmp_size);
   }
   return island_spec_tmp;
}

// Held bytes of a Mode A island (RT_ISLAND_SPEC_HELD_OFF)
static inline uint8_t *island_spec_held(const rt_island_t *isl)
{
   return isl->state + RT_ISLAND_SPEC_HELD_OFF(isl->spec->hdr_bytes);
}

static inline uint8_t *island_spec_req(const rt_island_t *isl)
{
   return isl->state + RT_ISLAND_SPEC_REQ_OFF(isl->spec->hdr_bytes,
                                              isl->nsigs);
}

static inline uint32_t island_spec_rows_bytes(const rt_island_sig_t *s)
{
   return s->nwords * s->nelems * 8u;
}

// Row clean tracking keeps the rows of a 2D signal packed while some are
// held.  Only the island's own flip-flop functions and its comb processes
// read or write such a signal; any other reader of the state (outputs,
// direct wires, inputs, observers) needs the whole value, so a 2D signal
// with one of those is held whole instead.
static bool island_spec_rows_ok(const rt_island_t *isl, uint32_t i)
{
   const rt_island_sig_t *s = &(isl->sigs[i]);
   if (!(isl->spec->a_flags[i] & RT_ISIG_SPEC_ROWF))
      return false;
   const uint32_t ext = RT_ISIG_IN | RT_ISIG_OUT | RT_ISIG_OBSERVED
      | RT_ISIG_DIRECT_LINK | RT_ISIG_COMB_WRITTEN;
   return (s->flags & RT_ISIG_FF_WRITTEN) && !(s->flags & ext)
      && s->n_direct_targets == 0 && s->direct_src == NULL
      && s->n_direct_aliases == 0;
}

// Layout of signal I of the island after a switch (TO_A) or a change of its
// held byte, which must already be in the state
static void island_spec_set_sig_layout(rt_island_t *isl, uint32_t i,
                                       bool to_a)
{
   rt_island_spec_t *sp = isl->spec;
   rt_island_sig_t *s = &(isl->sigs[i]);
   const bool packed = !!(sp->a_flags[i] & RT_ISIG_TWO_STATE);
   s->flags &= ~(RT_ISIG_SPEC_A | RT_ISIG_SPEC_HELD | RT_ISIG_SPEC_ROWF
                 | RT_ISIG_SPEC_ROWS | RT_ISIG_LAST_OUT_VALID);
   if (!to_a) {
      s->a_off = sp->b_off[i];
      if (packed)
         s->flags = (s->flags & ~RT_ISIG_TWO_STATE) | RT_ISIG_STD_LOGIC9;
      return;
   }

   s->flags |= RT_ISIG_SPEC_A;
   if (!packed) {
      s->a_off = sp->a_off[i];
      return;
   }
   if (island_spec_held(isl)[i]) {
      s->a_off = sp->x_off[i];
      s->flags = (s->flags & ~RT_ISIG_TWO_STATE) | RT_ISIG_STD_LOGIC9
         | RT_ISIG_SPEC_HELD;
      return;
   }
   s->a_off = sp->a_off[i];
   s->flags = (s->flags & ~RT_ISIG_STD_LOGIC9) | RT_ISIG_TWO_STATE;
   if (sp->a_flags[i] & RT_ISIG_SPEC_ROWF) {
      s->flags |= RT_ISIG_SPEC_ROWF;
      // Some row may be held: the runtime must not read the whole value
      const uint32_t nb = RT_ISLAND_SPEC_ROWF_BYTES(s->nelems);
      const uint8_t *rf = isl->state + s->a_off + island_spec_rows_bytes(s);
      bool any = false;
      const bool two = isl->next_state != NULL
         && !(s->flags & RT_ISIG_SPARSE_RAM);
      for (uint32_t k = 0; k < nb && !any; k++)
         any = rf[k] != 0 || (two && rf[k + sp->stride] != 0);
      if (any)
         s->flags |= RT_ISIG_SPEC_ROWS;
   }
}

// NVC_ISLAND_SPEC_STATS=3: one line per switch on stderr
static void island_spec_trace(rt_island_t *isl, const char *what, int arg)
{
   static const char *names[RT_ISLAND_DEOPT_COUNT] = {
      "none", "literal", "range", "conv", "report", "state", "input",
      "put", "stress", "unclean", "row", "novar"
   };
   unsigned deltas = 0;
   const uint64_t now = model_now(isl->model, &deltas);
   if (strcmp(what, "promote") == 0)
      fprintf(stderr, "ISLAND_SPEC_TRACE: %"PRIu64"+%u tick=%"PRIu64
              " island=%u promote held=%d held_rows=%d\n", now, deltas,
              isl->mgr->spec_tick, isl->isl_id, arg & 0xffff, arg >> 16);
   else
      fprintf(stderr, "ISLAND_SPEC_TRACE: %"PRIu64"+%u tick=%"PRIu64
              " island=%u %s %s\n", now, deltas, isl->mgr->spec_tick,
              isl->isl_id, what, names[arg]);
}

static void island_spec_set_handles(rt_island_t *isl);

static void island_spec_set_layout(rt_island_t *isl, bool to_a)
{
   rt_island_spec_t *sp = isl->spec;
   isl->state_bytes = to_a ? sp->stride : sp->stride_b;
   if (isl->next_state != NULL)
      isl->next_state = isl->state + isl->state_bytes;
   isl->spec_a = to_a;
   if (sp->sparse_log_off > 0)
      isl->sparse_log_off = to_a ? sp->sparse_log_off : sp->sparse_log_off_b;

   for (uint32_t i = 0; i < isl->nsigs; i++)
      island_spec_set_sig_layout(isl, i, to_a);

   sp->checks = false;
   island_spec_set_handles(isl);
}

// The functions of the island for its mode: Mode A ones that test the held
// bytes while a signal or row is held (sp->checks), the others otherwise
static void island_spec_set_handles(rt_island_t *isl)
{
   rt_island_spec_t *sp = isl->spec;
   const bool to_a = isl->spec_a;
   isl->eval_handle = !to_a ? sp->eval_handle_b
      : sp->checks ? sp->eval_handle_h : sp->eval_handle;
   isl->eval_func = NULL;
   isl->eval_fn = NULL;
   for (uint32_t d = 0; d < isl->ndomains; d++) {
      rt_island_domain_t *dom = &(isl->domains[d]);
      dom->ff_handle = !to_a ? sp->ff_handles_b[d]
         : sp->checks ? sp->ff_handles_h[d] : sp->ff_handles[d];
      dom->ff_func = NULL;
      dom->ff_fn = NULL;
      rt_super_domain_t *sd = dom->super_dom;
      if (sd != NULL && sd->ff_entries != NULL) {
         for (uint32_t k = 0; k < sd->n_doms; k++) {
            if (sd->ff_entries[k].dom == dom) {
               sd->ff_entries[k].ff_fn = NULL;
               sd->ff_entries[k].ff_func = NULL;
            }
         }
      }
   }
}

static void island_spec_refresh_in_descs(rt_island_t *isl)
{
   for (uint32_t j = 0; j < isl->n_in_sigs; j++) {
      const rt_island_sig_t *s = &(isl->sigs[isl->in_sigs[j]]);
      rt_island_in_desc_t *id = &(isl->in_descs[j]);
      id->sig_idx = s->sig_idx;
      // A held signal of a Mode A island goes through island_sync_in_sig,
      // which packs it again once its value is clean
      if ((s->flags & RT_ISIG_STD_LOGIC9) && s->sig != NULL
          && !(s->flags & RT_ISIG_SPEC_HELD)) {
         const size_t elem_sz = s->elem_size;
         id->src    = s->sig->shared.data + (size_t)s->sig_offset * elem_sz;
         id->dst    = isl->state + s->a_off;
         id->nbytes = s->width * s->nelems * s->elem_size;
      }
      else {
         id->src    = NULL;
         id->dst    = NULL;
         id->nbytes = 0;
      }
   }
}

// RT_ISIG_SPEC_MIX: a direct target of the signal has the other layout
static void island_spec_update_mix(rt_island_t *isl)
{
   const uint32_t repr = RT_ISIG_TWO_STATE | RT_ISIG_STD_LOGIC9;
   for (uint32_t i = 0; i < isl->nsigs; i++) {
      rt_island_sig_t *s = &(isl->sigs[i]);
      bool mix = false;
      for (uint32_t t = 0; t < s->n_direct_targets; t++) {
         s->direct_targets[t]->flags &= ~RT_ISIG_MIX_MEMO;
         mix = mix || ((s->direct_targets[t]->flags ^ s->flags) & repr) != 0
            || (s->direct_targets[t]->flags & RT_ISIG_SPEC_HELD);
      }
      if (mix)
         s->flags |= RT_ISIG_SPEC_MIX;
      else
         s->flags &= ~RT_ISIG_SPEC_MIX;
   }
}

static void island_spec_rebuild(rt_island_t *isl)
{
   // The held signals, for island_spec_ff_unhold at every commit
   rt_island_spec_t *sp = isl->spec;
   sp->n_held_list = 0;
   bool checks = false;
   if (isl->spec_a) {
      const uint8_t *held = island_spec_held(isl);
      for (uint32_t i = 0; i < isl->nsigs; i++) {
         checks |= !!(isl->sigs[i].flags & RT_ISIG_SPEC_ROWS);
         if (!held[i])
            continue;
         if (sp->held_list == NULL)
            sp->held_list = xmalloc_array(isl->nsigs, sizeof(uint32_t));
         sp->held_list[sp->n_held_list++] = i;
      }
      checks |= sp->n_held_list > 0;
   }
   isl->spec_held = sp->n_held_list > 0;
   for (uint32_t d = 0; d < isl->ndomains; d++) {
      rt_island_domain_t *dom = &(isl->domains[d]);
      dom->spec_held_w = false;
      if (!isl->spec_held)
         continue;
      const uint8_t *held = island_spec_held(isl);
      for (uint32_t j = 0; j < dom->n_write_sigs; j++)
         dom->spec_held_w |= !!held[dom->write_sigs[j]];
   }
   if (checks != sp->checks) {
      sp->checks = checks;
      island_spec_set_handles(isl);
   }

   // A switch changes no value and the change bytes (in the leading bytes
   // both layouts share) survive it: the next comb call need not run every
   // process, which would load the held signals of a Mode A island
   const bool was_on = isl->act_on;
   const bool was_all = was_on && island_act_info(isl)->all;

   island_spec_refresh_in_descs(isl);
   island_refresh_domain_descs(isl);
   island_refresh_ext_out_descs(isl);
   island_init_feedback(isl);
   island_init_act(isl);
   if (was_on && isl->act_on)
      island_act_info(isl)->all = was_all;

   // Direct wires out of the island and into it (rebuilt in place: a
   // deopt may happen while the source island walks its descriptors)
   island_spec_update_mix(isl);
   island_refresh_direct_comb_descs(isl);
   for (uint32_t i = 0; i < isl->nsigs; i++) {
      const rt_island_sig_t *s = &(isl->sigs[i]);
      if (s->direct_src == NULL || s->direct_src->isl == isl)
         continue;
      rt_island_t *src_isl = s->direct_src->isl;
      island_spec_update_mix(src_isl);
      island_refresh_direct_comb_descs(src_isl);
   }
}

static void island_spec_pin(rt_island_t *isl, rt_island_deopt_t why);

static void island_spec_unpack_words(uint8_t *out, const uint64_t *words,
                                     uint32_t width, uint32_t nwords,
                                     uint32_t nelems, uint8_t base);

static inline uint8_t island_spec_zero(const rt_island_t *isl, uint32_t i)
{
   return (isl->spec->a_flags[i] & RT_ISIG_STD_LOGIC_ELEM) ? 2u : 0u;
}

// Promotion: signal I from the Mode B half B to the Mode A half A (zeroed),
// whose held bytes are HELD.  A value that is not all '0'/'1' keeps its
// Mode B bytes at x_off and is held, or with ROWS only its rows that are
// not clean (row clean bytes after the rows).  Returns the held rows.
static uint32_t island_spec_promote_sig(const rt_island_t *isl, uint32_t i,
                                        const uint8_t *b, uint8_t *a,
                                        uint8_t *held, bool rows, bool hold)
{
   const rt_island_spec_t *sp = isl->spec;
   const rt_island_sig_t *s = &(isl->sigs[i]);
   if (!island_spec_sig_live(s))
      return 0;
   const uint8_t *src = b + sp->b_off[i];
   uint8_t *dst = a + sp->a_off[i];
   if (!(sp->a_flags[i] & RT_ISIG_TWO_STATE)) {
      memcpy(dst, src, sp->b_bytes[i]);
      return 0;
   }

   const uint8_t zero = island_spec_zero(isl, i);
   if (!hold && island_spec_pack_elems((uint64_t *)dst, src, s->nelems,
                                       s->width, s->nwords, zero))
      return 0;

   uint8_t *x = a + sp->x_off[i];
   if (!rows || hold) {
      memset(dst, 0, island_spec_rows_bytes(s));
      memcpy(x, src, sp->b_bytes[i]);
      held[i] = 1;
      return 0;
   }

   uint8_t *rf = dst + island_spec_rows_bytes(s);
   uint32_t n = 0;
   for (uint32_t e = 0; e < s->nelems; e++) {
      const uint8_t *er = src + (size_t)e * s->width;
      uint64_t *ew = (uint64_t *)dst + (size_t)e * s->nwords;
      if (island_spec_pack_elems(ew, er, 1, s->width, s->nwords, zero))
         continue;
      memset(ew, 0, s->nwords * sizeof(uint64_t));
      memcpy(x + (size_t)e * s->width, er, s->width);
      rf[e] = 1;
      n++;
   }
   return n;
}

static void island_spec_unpack_sig_to(const rt_island_t *isl, uint32_t i,
                                      const uint8_t *a, uint8_t *dst,
                                      const uint8_t *held);

// The exact inverse of island_spec_promote_sig for the Mode A half A whose
// signals are held as in HELD (sparse RAMs: island_spec_demote)
static void island_spec_unpack_half(const rt_island_t *isl, const uint8_t *a,
                                    uint8_t *b, const uint8_t *held,
                                    const uint8_t *req)
{
   const rt_island_spec_t *sp = isl->spec;
   memcpy(b, a, sp->hdr_bytes);
   for (uint32_t i = 0; i < isl->nsigs; i++) {
      const rt_island_sig_t *s = &(isl->sigs[i]);
      if (!island_spec_sig_live(s) || (s->flags & RT_ISIG_SPARSE_RAM))
         continue;
      if (req != NULL && held[i] && req[i]) {
         // A flip-flop store waiting for its commit (island_spec_ff_unhold)
         island_spec_unpack_words(b + sp->b_off[i],
                                  (const uint64_t *)(a + sp->a_off[i]),
                                  s->width, s->nwords, s->nelems,
                                  island_spec_zero(isl, i));
         continue;
      }
      island_spec_unpack_sig_to(isl, i, a, b + sp->b_off[i], held);
   }
}

// Signal I of the Mode A state A (held as in HELD) to its Mode B bytes DST
static void island_spec_unpack_sig_to(const rt_island_t *isl, uint32_t i,
                                      const uint8_t *a, uint8_t *dst,
                                      const uint8_t *held)
{
   const rt_island_spec_t *sp = isl->spec;
   const rt_island_sig_t *s = &(isl->sigs[i]);
   if (!(sp->a_flags[i] & RT_ISIG_TWO_STATE)) {
      memcpy(dst, a + sp->a_off[i], sp->b_bytes[i]);
      return;
   }
   const uint8_t *x = a + sp->x_off[i];
   if (held[i]) {
      memcpy(dst, x, sp->b_bytes[i]);
      return;
   }
   const uint8_t zero = island_spec_zero(isl, i);
   const uint64_t *words = (const uint64_t *)(a + sp->a_off[i]);
   if (!(sp->a_flags[i] & RT_ISIG_SPEC_ROWF)) {
      island_spec_unpack_words(dst, words, s->width, s->nwords, s->nelems,
                               zero);
      return;
   }
   const uint8_t *rf = a + sp->a_off[i] + island_spec_rows_bytes(s);
   for (uint32_t e = 0; e < s->nelems; e++) {
      uint8_t *eb = dst + (size_t)e * s->width;
      if (rf[e])
         memcpy(eb, x + (size_t)e * s->width, s->width);
      else
         island_spec_unpack_words(eb, words + (size_t)e * s->nwords,
                                  s->width, s->nwords, 1, zero);
   }
}

// Pending entries of the Mode A sparse log of ISL (packed whole rows of
// packed sparse RAMs, the Mode B bytes of the others) appended to the
// Mode B log in the Mode B image B, which the next commit applies
static void island_spec_log_to_b(rt_island_t *isl, uint8_t *b)
{
   const rt_island_spec_t *sp = isl->spec;
   const rt_island_sparse_log_t *la =
      (const rt_island_sparse_log_t *)(isl->state + sp->sparse_log_off);
   rt_island_sparse_log_t *lb =
      (rt_island_sparse_log_t *)(b + sp->sparse_log_off_b);
   lb->count = 0;
   lb->data_used = 0;
   lb->pad = 0;
   lb->isl_ptr = (uint64_t)(uintptr_t)isl;
   assert(isl->n_sparse_ovf == 0);   // The Mode A variant never spills

   bool spill = false;
   for (uint32_t k = 0; k < (uint32_t)la->count; k++) {
      const uint32_t sig_idx = (uint32_t)la->entries[k].sig_and_off;
      uint32_t boff = (uint32_t)(la->entries[k].sig_and_off >> 32);
      uint32_t len = (uint32_t)la->entries[k].len_and_data;
      const uint32_t doff = (uint32_t)(la->entries[k].len_and_data >> 32);
      const rt_island_sig_t *s = &(isl->sigs[sig_idx]);
      const uint8_t *data = la->data + doff;
      uint8_t row[256];
      uint8_t *heap = NULL;
      if (sp->a_flags[sig_idx] & RT_ISIG_TWO_STATE) {
         const uint32_t rb = s->nwords * 8u;
         assert(len == rb && boff % rb == 0);
         uint8_t *out = s->width <= sizeof(row) ? row
            : (heap = xmalloc(s->width));
         island_spec_unpack_words(out, (const uint64_t *)data, s->width,
                                  s->nwords, 1, island_spec_zero(isl, sig_idx));
         boff = (boff / rb) * s->width;
         len = s->width;
         data = out;
      }
      spill |= lb->count >= RT_ISLAND_SPARSE_INLINE_CAP
         || lb->data_used + len > RT_ISLAND_SPARSE_INLINE_DATA;
      if (spill)
         rt_island_sparse_write(isl, sig_idx, boff, len, data);
      else {
         rt_island_sparse_entry_t *e = &(lb->entries[lb->count++]);
         e->sig_and_off  = (uint64_t)sig_idx | ((uint64_t)boff << 32);
         e->len_and_data = (uint64_t)len | (lb->data_used << 32);
         memcpy(lb->data + lb->data_used, data, len);
         lb->data_used += len;
      }
      free(heap);
   }
}

// Convert a Mode A island back to Mode B: always allowed outside a call of
// its functions, changes no value and schedules nothing
static void island_spec_demote(rt_island_t *isl, rt_island_deopt_t why)
{
   rt_island_spec_t *sp = isl->spec;
   rt_island_mgr_t *mgr = isl->mgr;
   assert(isl->spec_a);

   const uint32_t stride_a = sp->stride;
   const size_t total = isl->total_state_bytes;
   uint8_t *tmp = island_spec_scratch(total);
   memset(tmp, 0, total);
   const uint8_t *held = island_spec_held(isl);
   island_spec_unpack_half(isl, isl->state, tmp, held, NULL);
   if (isl->next_state != NULL)
      island_spec_unpack_half(isl, isl->state + stride_a, tmp + sp->stride_b,
                              held, island_spec_req(isl));
   size_t nbytes = isl->next_state != NULL
      ? 2u * (size_t)sp->stride_b : sp->stride_b;
   if (sp->sparse_log_off > 0) {
      // The Mode A halves may cover the Mode B sparse RAMs and log
      for (uint32_t i = 0; i < isl->nsigs; i++) {
         if (isl->sigs[i].flags & RT_ISIG_SPARSE_RAM)
            island_spec_unpack_sig_to(isl, i, isl->state, tmp + sp->b_off[i],
                                      held);
      }
      island_spec_log_to_b(isl, tmp);
      nbytes = sp->total_bytes_b;
   }
   island_spec_set_layout(isl, false);
   memcpy(isl->state, tmp, nbytes);

   sp->deopts[why]++;
   if (unlikely(mgr->spec_trace))
      island_spec_trace(isl, "deopt", why);
   const uint64_t ran_a = mgr->spec_tick - sp->tick_promoted;
   sp->ticks_a += ran_a;
   sp->n_held = 0;
   mgr->spec_n_a--;
   island_spec_rebuild(isl);

   if (why == RT_ISLAND_DEOPT_STRESS) {
      sp->next_check = mgr->spec_tick + 1;
      return;
   }
   if (why == RT_ISLAND_DEOPT_NOVAR) {
      // The function was lowered on its first call and that failed
      // (vi_lazy_build): this island stays in Mode B
      sp->pinned = true;
      sp->pin_reason = why;
      return;
   }
   if (ran_a < ISLAND_SPEC_QUICK)
      sp->n_quick++;
   else {
      sp->n_quick = 0;
      sp->cost_pinned = false;
   }
   if (why == RT_ISLAND_DEOPT_UNCLEAN || why == RT_ISLAND_DEOPT_ROW) {
      // A held value was read: try again once Mode B may have made it
      // clean, backing off but never pinning (it may become clean later)
      if (sp->pinned)
         return;
      sp->n_unclean++;
      if (sp->n_quick >= ISLAND_SPEC_COST_PIN) {
         if (!sp->cost_pinned)
            sp->cost_pins++;
         sp->cost_pinned = true;
         sp->next_check = mgr->spec_tick + ISLAND_SPEC_COST_RETRY;
      }
      else
         sp->next_check = mgr->spec_tick
            + (UINT64_C(16) << MIN(sp->n_unclean, 12u));
      sp->check_ival = 16;
      mgr->spec_next = MIN(mgr->spec_next, sp->next_check);
      return;
   }
   sp->n_deopt++;
   if (why == RT_ISLAND_DEOPT_REPORT)
      sp->n_report++;
   if (sp->n_deopt >= ISLAND_SPEC_PIN_DEOPTS
       || sp->n_report >= ISLAND_SPEC_PIN_REPORTS)
      island_spec_pin(isl, why);
   else {
      sp->next_check = mgr->spec_tick
         + (UINT64_C(16) << MIN(sp->n_deopt, 12u));
      sp->check_ival = 16;
      mgr->spec_next = MIN(mgr->spec_next, sp->next_check);
   }
}

// Signal I of Mode A island ISL takes a value that is not all '0'/'1':
// keep it as Mode B bytes from now on (both halves), CUR (when not NULL)
// being its new value in the first half.  False when it cannot be held.
static bool island_spec_hold(rt_island_t *isl, uint32_t i, const uint8_t *cur)
{
   rt_island_spec_t *sp = isl->spec;
   rt_island_sig_t *s = &(isl->sigs[i]);
   assert(isl->spec_a);
   if (!(s->flags & RT_ISIG_TWO_STATE)
       || (s->flags & (RT_ISIG_SPEC_ROWS | RT_ISIG_SPARSE_RAM))
       || s->elem_size != 1)
      return false;

   const uint8_t zero = island_spec_zero(isl, i);
   uint8_t *x = isl->state + sp->x_off[i];
   if (cur != NULL)
      memcpy(x, cur, sp->b_bytes[i]);
   else
      island_spec_unpack_words(x, (const uint64_t *)(isl->state + s->a_off),
                               s->width, s->nwords, s->nelems, zero);
   if (isl->next_state != NULL)
      island_spec_unpack_words(isl->next_state + sp->x_off[i],
                               (const uint64_t *)(isl->next_state + s->a_off),
                               s->width, s->nwords, s->nelems, zero);
   if (s->flags & RT_ISIG_SPEC_ROWF) {
      // Row clean bytes are all clear (no RT_ISIG_SPEC_ROWS)
      memset(isl->state + s->a_off + island_spec_rows_bytes(s), 0,
             RT_ISLAND_SPEC_ROWF_BYTES(s->nelems));
   }

   island_spec_held(isl)[i] = 1;
   sp->n_held++;
   sp->holds++;
   island_spec_set_sig_layout(isl, i, true);
   island_spec_rebuild(isl);
   return true;
}

// Held signal I of Mode A island ISL is all '0'/'1' again (its first half;
// WORDS when not NULL is the packed new value): pack it.  False when the
// second half of a flip-flop written signal is not clean.
static bool island_spec_unhold(rt_island_t *isl, uint32_t i,
                               const uint64_t *words)
{
   rt_island_spec_t *sp = isl->spec;
   rt_island_sig_t *s = &(isl->sigs[i]);
   assert(isl->spec_a && (s->flags & RT_ISIG_SPEC_HELD));

   const uint8_t zero = island_spec_zero(isl, i);
   const size_t nw = (size_t)s->nwords * s->nelems;
   uint64_t *dst = (uint64_t *)(isl->state + sp->a_off[i]);
   uint64_t *dst2 = isl->next_state != NULL
      ? (uint64_t *)(isl->next_state + sp->a_off[i]) : NULL;
   if (dst2 != NULL && (s->flags & RT_ISIG_FF_WRITTEN)) {
      uint64_t small[4];
      uint64_t *tmp = nw <= 4 ? small : xmalloc_array(nw, sizeof(uint64_t));
      const bool ok = island_spec_pack_elems(tmp, isl->next_state + s->a_off,
                                             s->nelems, s->width, s->nwords,
                                             zero);
      if (ok)
         memcpy(dst2, tmp, nw * sizeof(uint64_t));
      if (tmp != small)
         free(tmp);
      if (!ok)
         return false;
      dst2 = NULL;
   }

   if (words != NULL)
      memcpy(dst, words, nw * sizeof(uint64_t));
   else if (!island_spec_pack_elems(dst, isl->state + s->a_off, s->nelems,
                                    s->width, s->nwords, zero))
      fatal_trace("held signal is not clean");
   if (dst2 != NULL)
      memcpy(dst2, dst, nw * sizeof(uint64_t));

   island_spec_held(isl)[i] = 0;
   assert(sp->n_held > 0);
   sp->n_held--;
   sp->unholds++;
   island_spec_set_sig_layout(isl, i, true);
   island_spec_rebuild(isl);
   return true;
}

static void island_spec_pin(rt_island_t *isl, rt_island_deopt_t why)
{
   rt_island_spec_t *sp = isl->spec;
   if (sp == NULL || sp->pinned)
      return;
   if (isl->spec_a) {
      sp->pinned = true;   // No back-off scheduling in island_spec_demote
      island_spec_demote(isl, why);
   }
   else if (why >= RT_ISLAND_DEOPT_PUT)
      sp->deopts[why]++;
   sp->pinned = true;
   sp->pin_reason = why;
}

static void island_spec_pin_sig(rt_island_mgr_t *mgr, rt_signal_t *sig,
                                rt_island_deopt_t why)
{
   if (mgr->sig_map == NULL)
      return;
   for (rt_island_sig_t *s = hash_get(mgr->sig_map, sig); s != NULL;
        s = s->next_owner)
      island_spec_pin(s->isl, why);
}

// Commit of domain DOM of a Mode A island with held signals: a held signal
// the flip-flop function stored a whole value into (its request byte is
// set, RT_ISLAND_SPEC_REQ_OFF) is packed again, its current words set to
// differ from the next ones in every bit so the commit takes the new value
// and notifies its readers as for any change
__attribute__((noinline))
static void island_spec_ff_unhold(rt_island_domain_t *dom)
{
   rt_island_t *isl = dom->isl;
   rt_island_spec_t *sp = isl->spec;
   uint8_t *req = island_spec_req(isl);
   uint8_t *held = island_spec_held(isl);
   bool any = false;
   // Only this domain's stores (another domain commits its own)
   for (uint32_t k = 0; k < dom->n_write_sigs; k++) {
      const uint32_t i = dom->write_sigs[k];
      if (!req[i] || !held[i])
         continue;
      req[i] = 0;
      const rt_island_sig_t *s = &(isl->sigs[i]);
      uint64_t *cur = (uint64_t *)(isl->state + sp->a_off[i]);
      const uint64_t *nxt = (const uint64_t *)(isl->next_state + sp->a_off[i]);
      for (uint32_t w = 0; w < s->nwords * s->nelems; w++)
         cur[w] = ~nxt[w];
      held[i] = 0;
      assert(sp->n_held > 0);
      sp->n_held--;
      sp->unholds++;
      island_spec_set_sig_layout(isl, i, true);
      any = true;
   }
   if (any)
      island_spec_rebuild(isl);
}

// The runtime is about to read the whole value of signal S, a 2D signal of
// a Mode A island whose rows may be held: convert the island back to Mode B
__attribute__((noinline))
static void island_spec_whole_read(rt_island_t *isl, rt_island_sig_t *s)
{
   assert(isl->spec_a && s->isl == isl);
   island_spec_demote(isl, RT_ISLAND_DEOPT_ROW);
}

static bool island_spec_try_promote(rt_island_t *isl)
{
   rt_island_spec_t *sp = isl->spec;
   rt_island_mgr_t *mgr = isl->mgr;

   bool safe = !isl->has_forces && isl->pending_vhdl_ff == 0;
   for (uint32_t d = 0; d < isl->ndomains && safe; d++) {
      const rt_island_domain_t *dom = &(isl->domains[d]);
      safe = dom->ff_dirty_mask == 0 && !dom->in_commit_q
         && (dom->super_dom == NULL || !dom->super_dom->in_commit_q);
   }
   if (safe && sp->sparse_log_off > 0) {
      const rt_island_sparse_log_t *lb =
         (const rt_island_sparse_log_t *)(isl->state + isl->sparse_log_off);
      safe = lb->count == 0 && isl->n_sparse_ovf == 0;
   }
   if (!safe) {
      sp->next_check = mgr->spec_tick + 1;
      return false;
   }

   // Every signal whose value is not all '0'/'1' is held (or, for an
   // internal 2D signal, its rows that are not): the Mode A functions
   // deopt when they load or store one, so Mode B computes with it
   const size_t total = isl->total_state_bytes;
   uint8_t *tmp = island_spec_scratch(total);
   memset(tmp, 0, total);
   memcpy(tmp, isl->state, sp->hdr_bytes);
   uint8_t *held = tmp + RT_ISLAND_SPEC_HELD_OFF(sp->hdr_bytes);
   uint32_t n_rows = 0, n_held = 0;
   for (uint32_t i = 0; i < isl->nsigs; i++) {
      const rt_island_sig_t *s = &(isl->sigs[i]);
      // Only the island's own functions read a packed sparse RAM: its
      // commit logs rows, an output or observer would need the bytes
      const bool hold = (s->flags & RT_ISIG_SPARSE_RAM)
         && (sp->a_flags[i] & RT_ISIG_TWO_STATE)
         && (s->flags & (RT_ISIG_OUT | RT_ISIG_OBSERVED));
      n_rows += island_spec_promote_sig(isl, i, isl->state, tmp, held,
                                        island_spec_rows_ok(isl, i), hold);
      n_held += held[i];
   }

   if (n_held == 0 && n_rows == 0)
      sp->unclean_since = 0;
   else if (island_spec_stress_n() == 0) {
      const uint64_t tick = mgr->spec_tick, wait = island_spec_hold_wait();
      if (sp->unclean_since == 0)
         sp->unclean_since = tick + 1;
      const uint64_t waited = tick + 1 - sp->unclean_since;
      if (waited < wait) {
         // Check again soon: the values may become clean
         sp->hold_waits++;
         sp->check_ival = MIN(MAX(sp->check_ival, 16u) * 2u, wait);
         sp->next_check = tick + MIN(sp->check_ival, wait - waited);
         return false;
      }
   }

   size_t nbytes = sp->stride;
   if (isl->next_state != NULL) {
      memcpy(tmp + sp->stride, tmp, sp->stride);
      nbytes += sp->stride;
   }
   if (sp->sparse_log_off > 0) {
      rt_island_sparse_log_t *la =
         (rt_island_sparse_log_t *)(tmp + sp->sparse_log_off);
      la->isl_ptr = (uint64_t)(uintptr_t)isl;
      nbytes = sp->total_bytes;
   }
   memcpy(isl->state, tmp, nbytes);
   island_spec_set_layout(isl, true);
   sp->promotions++;
   if (unlikely(mgr->spec_trace))
      island_spec_trace(isl, "promote", n_held | (n_rows << 16));
   sp->tick_promoted = mgr->spec_tick;
   sp->n_held = n_held;
   sp->n_held_rows = n_rows;
   mgr->spec_n_a++;
   island_spec_rebuild(isl);
   return true;
}

static void island_spec_sweep(rt_island_mgr_t *mgr)
{
   const uint64_t tick = mgr->spec_tick;
   const uint32_t stress = island_spec_stress_n();
   uint64_t next = UINT64_MAX;
   for (unsigned i = 0; i < mgr->islands.count; i++) {
      rt_island_t *isl = mgr->islands.items[i];
      rt_island_spec_t *sp = isl->spec;
      if (sp == NULL || sp->pinned || sp->force_a)
         continue;
      if (isl->spec_a) {
         if (stress > 0 && tick % stress == 0)
            island_spec_demote(isl, RT_ISLAND_DEOPT_STRESS);
         else
            continue;
      }
      if (tick >= sp->next_check)
         island_spec_try_promote(isl);
      if (!isl->spec_a && !sp->pinned)
         next = MIN(next, sp->next_check);
   }
   mgr->spec_next = stress > 0 ? tick + 1 : next;
}

static void island_spec_tick(rt_island_mgr_t *mgr)
{
   const uint64_t tick = ++mgr->spec_tick;
   if ((tick & (tick - 1)) == 0 && mgr->n_spec_timeline < ISLAND_SPEC_TIMELINE) {
      uint32_t n_held = 0;
      for (unsigned i = 0; i < mgr->islands.count; i++) {
         const rt_island_t *isl = mgr->islands.items[i];
         n_held += isl->spec_a && (isl->spec->n_held > 0
                                   || isl->spec->n_held_rows > 0);
      }
      mgr->spec_held_timeline[mgr->n_spec_timeline] = n_held;
      mgr->spec_timeline[mgr->n_spec_timeline++] = mgr->spec_n_a;
   }
   if (tick >= mgr->spec_next)
      island_spec_sweep(mgr);
}

static inline bool island_spec_bytes_clean(const uint8_t *p, size_t n,
                                           uint8_t zero)
{
   const uint64_t want = UINT64_C(0x0101010101010101) * zero;
   const uint64_t mask = UINT64_C(0xfefefefefefefefe);
   size_t i = 0;
   for (; i + 8u <= n; i += 8u) {
      uint64_t w;
      memcpy(&w, p + i, 8);
      if ((w & mask) != want)
         return false;
   }
   for (; i < n; i++) {
      if ((p[i] & 0xfeu) != zero)
         return false;
   }
   return true;
}

// The kernel value of input S of a Mode A island is all '0'/'1'
static bool island_spec_in_clean(const rt_island_sig_t *s)
{
   const rt_signal_t *sig = s->sig;
   if (sig->shared.flags & SIG_F_PACKED) {
      if (!(sig->shared.flags & SIG_F_PACKED4))
         return true;
      const uint32_t nw = (sig->bit_width + 63u) / 64u;
      const uint64_t *b = (const uint64_t *)sig->shared.data + nw;
      for (uint32_t w = 0; w < nw; w++) {
         if (b[w] != 0)
            return false;   // Any X/Z of the signal: exact, if coarse
      }
      return true;
   }
   const uint8_t zero = island_sig_is_std(sig, s->flags) ? 2u : 0u;
   return island_spec_bytes_clean(sig->shared.data + s->sig_offset,
                                  (size_t)s->width * s->nelems, zero);
}

static void island_spec_unpack_words(uint8_t *out, const uint64_t *words,
                                     uint32_t width, uint32_t nwords,
                                     uint32_t nelems, uint8_t base)
{
   for (uint32_t e = 0; e < nelems; e++) {
      const uint64_t *ew = words + (size_t)e * nwords;
      uint8_t *eb = out + (size_t)e * width;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
      island_unpack_elem_2s(eb, ew, width, UINT64_C(0x0101010101010101) * base);
#else
      for (uint32_t k = 0; k < width; k++) {
         const uint32_t lsb = width - 1u - k;
         eb[k] = base + (uint8_t)((ew[lsb / 64u] >> (lsb & 63u)) & 1u);
      }
#endif
   }
}

// A direct wire between a Mode A and a Mode B island, or into a held signal
// of a Mode A island (VHDL signals only: one byte per element or packed
// two-state words)
__attribute__((noinline))
static bool island_spec_copy_mixed(const rt_island_sig_t *src_s,
                                   rt_island_sig_t *dst_s)
{
   const uint32_t repr = RT_ISIG_TWO_STATE | RT_ISIG_STD_LOGIC9;
   const rt_island_t *src_isl = src_s->isl;
   rt_island_t *dst_isl = dst_s->isl;
   const uint32_t width = src_s->width, nelems = src_s->nelems;
   const uint32_t nwords = src_s->nwords;
   const size_t total = (size_t)width * nelems;
   const uint8_t base =
      ((src_s->flags | dst_s->flags) & RT_ISIG_STD_LOGIC_ELEM) ? 2u : 0u;
   const uint32_t dst_idx = (uint32_t)(dst_s - dst_isl->sigs);

   if (((src_s->flags ^ dst_s->flags) & repr) == 0) {
      if (!(dst_s->flags & RT_ISIG_SPEC_HELD))
         return island_copy_direct_sig_same(src_s, dst_s);
      // Bytes into a held signal: pack it once the value is clean
      if (!island_copy_direct_sig_same(src_s, dst_s))
         return false;
      if (island_spec_bytes_clean(dst_isl->state + dst_s->a_off, total, base))
         island_spec_unhold(dst_isl, dst_idx, NULL);
      return true;
   }

   assert(src_s->elem_size == 1 && dst_s->elem_size == 1);

   if (src_s->flags & RT_ISIG_TWO_STATE) {
      const uint64_t *sw = (const uint64_t *)(src_isl->state + src_s->a_off);
      if ((dst_s->flags & RT_ISIG_SPEC_HELD)
          && island_spec_unhold(dst_isl, dst_idx, sw)) {
         island_act_mark_sig(dst_isl, dst_s);
         return true;   // Was not all '0'/'1'
      }
      // A one-word value unchanged since the last copy: the target bytes,
      // written only by this wire, still hold it (island_spec_update_mix
      // drops the memo on every layout change at either end)
      const bool one = nwords * nelems == 1;
      if (one && (dst_s->flags & RT_ISIG_MIX_MEMO) && dst_s->mix_memo == sw[0])
         return false;
      uint8_t small[72];
      uint8_t *tmp = total + 8u <= sizeof(small) ? small
         : island_spec_scratch(total + 8u);
      island_spec_unpack_words(tmp, sw, width, nwords, nelems, base);
      const bool changed =
         island_commit_bytes(dst_isl->state + dst_s->a_off, tmp, total);
      if (one) {
         dst_s->mix_memo = sw[0];
         dst_s->flags |= RT_ISIG_MIX_MEMO;
      }
      if (!changed)
         return false;
      island_act_mark_sig(dst_isl, dst_s);
      return true;
   }

   const uint8_t *src = src_isl->state + src_s->a_off;
   const size_t nw = (size_t)nwords * nelems;
   uint64_t small[4];
   uint64_t *tmp = nw <= 4 ? small
      : (uint64_t *)island_spec_scratch(nw * sizeof(uint64_t));
   if (!island_spec_pack_elems(tmp, src, nelems, width, nwords, base)) {
      if (!island_spec_hold(dst_isl, dst_idx, src)) {
         island_spec_demote(dst_isl, RT_ISLAND_DEOPT_INPUT);
         return island_copy_direct_sig_same(src_s, dst_s);
      }
      island_act_mark_sig(dst_isl, dst_s);
      return true;   // Was all '0'/'1'
   }
   uint64_t *dst = (uint64_t *)(dst_isl->state + dst_s->a_off);
   if (nw == 1) {
      if (dst[0] == tmp[0])
         return false;
      dst[0] = tmp[0];
   }
   else if (memcmp(dst, tmp, nw * sizeof(uint64_t)) == 0)
      return false;
   else
      memcpy(dst, tmp, nw * sizeof(uint64_t));
   island_act_mark_sig(dst_isl, dst_s);
   return true;
}

static bool island_call_comb_raw(jit_t *jit, rt_model_t *m,
                                 rt_island_t *isl, tlab_t *tlab);

// The Mode A comb function returned deopt code CODE: undo the call and run
// the Mode B function in its place
__attribute__((noinline))
static bool island_spec_comb_deopt(jit_t *jit, rt_model_t *m,
                                   rt_island_t *isl, tlab_t *tlab,
                                   int64_t code)
{
   assert(code > 0 && code < RT_ISLAND_DEOPT_COUNT);
   // A comb process computes its outputs from the signals it reads only:
   // the signals written and read by comb processes get back the values
   // they had before the call (the feedback shadow holds them), the
   // others are recomputed by the Mode B call
   for (uint32_t j = 0; j < isl->n_fb_sigs; j++)
      island_copy_sig_buf(isl->state, isl->fb_shadow,
                          &(isl->sigs[isl->fb_sigs[j]]));
   island_spec_demote(isl, (rt_island_deopt_t)code);
   if (isl->act_on)
      isl->state[RT_ISLAND_ACT_ALL(isl->nsigs)] = 1;
   return island_call_comb_raw(jit, m, isl, tlab);
}

static bool island_sample_domain(jit_t *jit, rt_island_domain_t *dom,
                                 tlab_t *tlab);

// The Mode A flip-flop function of DOM returned deopt code CODE: discard
// its next-state writes and sample the domain in Mode B
__attribute__((noinline))
static bool island_spec_ff_deopt(jit_t *jit, rt_island_domain_t *dom,
                                 tlab_t *tlab, int64_t code)
{
   rt_island_t *isl = dom->isl;
   assert(code > 0 && code < RT_ISLAND_DEOPT_COUNT);
   // Drop the requests of the call (only this domain writes the signals)
   uint8_t *req = island_spec_req(isl);
   for (uint32_t j = 0; j < dom->n_write_sigs; j++)
      req[dom->write_sigs[j]] = 0;
   if (isl->spec->sparse_log_off > 0) {
      // Drop the log entries of the call (see vi_spec_sparse_mark)
      rt_island_sparse_log_t *la =
         (rt_island_sparse_log_t *)(isl->state + isl->spec->sparse_log_off);
      la->count = la->pad & UINT32_MAX;
      la->data_used = la->pad >> 32;
   }
   island_spec_demote(isl, (rt_island_deopt_t)code);
   // Outside a flip-flop call the next state of the signals a domain
   // writes equals the state (and a domain whose signals another writer
   // shares copies it before each call anyway)
   for (uint32_t j = 0; j < dom->n_write_sigs; j++) {
      const rt_island_sig_t *s = &(isl->sigs[dom->write_sigs[j]]);
      if (!(s->flags & RT_ISIG_SPARSE_RAM))
         island_copy_sig_buf(isl->next_state, isl->state, s);
   }
   return island_sample_domain(jit, dom, tlab);
}

// Super-domain fast sample: the cached function of entry E returned a
// value without the completion bit 63
__attribute__((noinline))
static bool island_spec_sdom_deopt(jit_t *jit, rt_sdom_ff_entry_t *e,
                                   tlab_t *tlab, uint64_t *result)
{
   rt_island_domain_t *dom = e->dom;
   if (!dom->isl->spec_a)
      return true;
   const uint64_t saved = dom->ff_dirty_mask;
   dom->ff_dirty_mask = 0;
   if (!island_spec_ff_deopt(jit, dom, tlab, (int64_t)*result))
      return false;
   *result = dom->ff_dirty_mask;
   dom->ff_dirty_mask = saved;
   return true;
}

// Run-time switching for island ISL with a Mode A variant (rt_island_finish)
static void island_spec_rt_setup(rt_island_t *isl)
{
   rt_island_spec_t *sp = isl->spec;
   if (sp == NULL)
      return;
   rt_island_mgr_t *mgr = isl->mgr;

   sp->eval_handle_b = isl->eval_handle;
   sp->stride_b = isl->state_bytes;
   if (isl->nsigs > 0) {
      sp->b_off = xmalloc_array(isl->nsigs, sizeof(uint32_t));
      for (uint32_t i = 0; i < isl->nsigs; i++)
         sp->b_off[i] = isl->sigs[i].a_off;
   }
   bool usable = (isl->eval_handle == JIT_HANDLE_INVALID)
      == (sp->eval_handle == JIT_HANDLE_INVALID);
   if (isl->ndomains > 0) {
      sp->ff_handles_b = xmalloc_array(isl->ndomains, sizeof(jit_handle_t));
      for (uint32_t d = 0; d < isl->ndomains; d++) {
         sp->ff_handles_b[d] = isl->domains[d].ff_handle;
         if ((sp->ff_handles_b[d] == JIT_HANDLE_INVALID)
             != (sp->ff_handles[d] == JIT_HANDLE_INVALID))
            usable = false;
      }
   }

   const char *env = getenv("NVC_ISLAND_SPEC_RT");
   if (!usable || sp->force_a || opt_get_int(OPT_SIM_THREADS) > 1
       || (env != NULL && env[0] == '0')) {
      sp->pinned = true;
      return;
   }

   // Both layouts use the same buffer (super-domain entries keep its
   // address): room for two halves of the larger one
   // (and the Mode A sparse RAMs after the whole Mode B state)
   sp->total_bytes_b = isl->sparse_log_off > 0 ? isl->total_state_bytes : 0;
   const size_t need = MAX((isl->ndomains > 0 ? 2u : 1u)
                           * (size_t)MAX(sp->stride_b, sp->stride),
                           (size_t)sp->total_bytes);
   if (need > isl->total_state_bytes) {
      void *buf = NULL;
#ifdef __MINGW32__
      buf = _aligned_malloc(need, 64);
      if (buf == NULL)
         fatal("failed to allocate %zu bytes for island state", need);
      _aligned_free(isl->state);
#else
      if (posix_memalign(&buf, 64, need) != 0)
         fatal("failed to allocate %zu bytes for island state", need);
      free(isl->state);
#endif
      memset(buf, 0, need);
      isl->state = buf;
      isl->next_state = isl->ndomains > 0 ? isl->state + isl->state_bytes
         : NULL;
      isl->total_state_bytes = (uint32_t)need;
      if (isl->sparse_log_off > 0) {
         rt_island_sparse_log_t *log =
            (rt_island_sparse_log_t *)(isl->state + isl->sparse_log_off);
         log->isl_ptr = (uint64_t)(uintptr_t)isl;
      }
   }

   sp->check_ival = 8;
   sp->next_check = 8;
   if (!mgr->spec_on) {
      mgr->spec_on = true;
      mgr->spec_next = 8;
      const char *env = getenv("NVC_ISLAND_SPEC_STATS");
      mgr->spec_trace = env != NULL && atoi(env) >= 3;
   }
}

static void island_spec_rt_report(rt_island_mgr_t *mgr)
{
   const char *env = getenv("NVC_ISLAND_SPEC_STATS");
   if (env == NULL || *env == '\0' || !mgr->spec_on)
      return;

   FILE *f = stdout;
   if (env[0] == '/' && (f = fopen(env, "a")) == NULL)
      return;

   static const char *names[RT_ISLAND_DEOPT_COUNT] = {
      "none", "literal", "range", "conv", "report", "state", "input",
      "put", "stress", "unclean", "row", "novar"
   };
   uint64_t deopts[RT_ISLAND_DEOPT_COUNT] = {}, pins[RT_ISLAND_DEOPT_COUNT] = {};
   uint64_t promotions = 0, failed = 0, ticks_a = 0, holds = 0, unholds = 0;
   uint32_t n_var = 0, n_ever = 0, n_a = 0, n_pinned = 0;
   uint32_t n_a_held = 0, n_held = 0, n_rows = 0, n_cost = 0;
   uint64_t cost_pins = 0, hold_waits = 0;
   for (unsigned i = 0; i < mgr->islands.count; i++) {
      const rt_island_t *isl = mgr->islands.items[i];
      const rt_island_spec_t *sp = isl->spec;
      if (sp == NULL || sp->force_a)
         continue;
      n_var++;
      n_cost += sp->cost_pinned && !isl->spec_a && !sp->pinned;
      cost_pins += sp->cost_pins;
      hold_waits += sp->hold_waits;
      n_ever += sp->promotions > 0;
      n_a += isl->spec_a;
      uint64_t ta = sp->ticks_a;
      if (isl->spec_a)
         ta += mgr->spec_tick - sp->tick_promoted;
      ticks_a += ta;
      promotions += sp->promotions;
      failed += sp->failed_checks;
      if (sp->pinned) {
         n_pinned++;
         pins[sp->pin_reason]++;
      }
      for (int k = 0; k < RT_ISLAND_DEOPT_COUNT; k++)
         deopts[k] += sp->deopts[k];
      holds += sp->holds;
      unholds += sp->unholds;
      if (isl->spec_a) {
         n_a_held += (sp->n_held > 0 || sp->n_held_rows > 0);
         n_held += sp->n_held;
         n_rows += sp->n_held_rows;
      }
      if (atoi(env) >= 2 || env[0] == '/')
         fprintf(f, "ISLAND_SPEC_RT: island=%u mode=%c promotions=%"PRIu64
                 " deopts=%u unclean=%"PRIu64" row=%"PRIu64" held=%u"
                 " held_rows=%u holds=%"PRIu64" unholds=%"PRIu64
                 " ticks_a=%"PRIu64"%s%s scope=%s\n", isl->isl_id,
                 isl->spec_a ? 'A' : 'B', sp->promotions, sp->n_deopt,
                 sp->deopts[RT_ISLAND_DEOPT_UNCLEAN],
                 sp->deopts[RT_ISLAND_DEOPT_ROW], isl->spec_a ? sp->n_held : 0,
                 isl->spec_a ? sp->n_held_rows : 0, sp->holds, sp->unholds,
                 ta, sp->pinned ? " pinned=" : "",
                 sp->pinned ? names[sp->pin_reason] : "",
                 isl->root_scope != NULL ? istr(isl->root_scope->name) : "?");
      if (isl->spec_a && sp->n_held > 0
          && (atoi(env) >= 2 || env[0] == '/')) {
         // Signals held as Mode B bytes
         const uint8_t *held = island_spec_held(isl);
         int shown = 0;
         for (uint32_t j = 0; j < isl->nsigs && shown < 4; j++) {
            const rt_island_sig_t *s = &(isl->sigs[j]);
            if (!held[j])
               continue;
            ident_t name = (s->sig != NULL && s->sig->where != NULL)
               ? object_ident((object_t *)s->sig->where) : NULL;
            fprintf(f, "ISLAND_SPEC_RT:   held sig=%u %s width=%u "
                    "nelems=%u\n", j, name != NULL ? istr(name) : "?",
                    s->width, s->nelems);
            shown++;
         }
      }
   }

   const double frac = (n_var > 0 && mgr->spec_tick > 0)
      ? 100.0 * ticks_a / ((double)n_var * mgr->spec_tick) : 0.0;
   fprintf(f, "ISLAND_SPEC_RT: ticks=%"PRIu64" variants=%u promoted=%u "
           "in_a=%u pinned=%u promotions=%"PRIu64" failed_checks=%"PRIu64
           " time_in_a=%.1f%%\n", mgr->spec_tick, n_var, n_ever, n_a,
           n_pinned, promotions, failed, frac);
   fprintf(f, "ISLAND_SPEC_RT: held in_a_with_held=%u signals=%u rows=%u"
           " holds=%"PRIu64" unholds=%"PRIu64"\n", n_a_held, n_held, n_rows,
           holds, unholds);
   fprintf(f, "ISLAND_SPEC_RT: deopts");
   for (int k = 1; k < RT_ISLAND_DEOPT_COUNT; k++)
      fprintf(f, " %s=%"PRIu64, names[k], deopts[k]);
   fprintf(f, "\nISLAND_SPEC_RT: pinned");
   for (int k = 0; k < RT_ISLAND_DEOPT_COUNT; k++)
      fprintf(f, " %s=%"PRIu64, names[k], pins[k]);
   fprintf(f, "\nISLAND_SPEC_RT: cost pinned_by_cost=%u cost_pins=%"PRIu64
           " hold_waits=%"PRIu64, n_cost, cost_pins, hold_waits);
   fprintf(f, "\nISLAND_SPEC_RT: in_a_at_tick");
   for (uint32_t k = 0; k < mgr->n_spec_timeline; k++)
      fprintf(f, " %"PRIu64":%u", UINT64_C(1) << k, mgr->spec_timeline[k]);
   fprintf(f, "\nISLAND_SPEC_RT: in_a_holding_at_tick");
   for (uint32_t k = 0; k < mgr->n_spec_timeline; k++)
      fprintf(f, " %"PRIu64":%u", UINT64_C(1) << k,
              mgr->spec_held_timeline[k]);
   fprintf(f, "\n");
   if (f != stdout)
      fclose(f);
   else
      fflush(stdout);
}

// Every island back in Mode B (model reset)
static void island_spec_reset(rt_island_mgr_t *mgr)
{
   for (unsigned i = 0; i < mgr->islands.count; i++) {
      rt_island_t *isl = mgr->islands.items[i];
      if (isl->spec == NULL)
         continue;
      if (isl->spec_a) {
         island_spec_set_layout(isl, false);
         island_spec_refresh_in_descs(isl);
      }
      for (uint32_t j = 0; j < isl->nsigs; j++)
         isl->sigs[j].flags &= ~RT_ISIG_SPEC_MIX;
   }
   mgr->spec_n_a = 0;
}

typedef A(rt_island_sig_t *) island_sig_ptr_list_t;

typedef struct {
   rt_signal_t *sig;
   int32_t      sig_offset;
} island_alias_spec_t;

typedef A(island_alias_spec_t) island_alias_list_t;

static bool island_collect_pending_consumers(void *pending,
                                             const rt_island_sig_t *src_s,
                                             const rt_signal_t *cur_sig,
                                             int32_t cur_off,
                                             bool require_exact_sig,
                                             island_sig_ptr_list_t *consumers,
                                             bool *found_out)
{
   if (pending == NULL)
      return true;

   if (pointer_tag(pending) == 1) {
      rt_wakeable_t *w = untag_pointer(pending, rt_wakeable_t);
      if (w->kind != W_WATCH)
         return island_wr_no(island_wake_reason(w));
      rt_watch_t *watch = container_of(w, rt_watch_t, wakeable);
      if (watch->fn != island_input_watch_cb)
         return island_wr_no(WR_READER_WATCH);
      rt_island_sig_t *dst_s = watch->user_data;
      island_wire_obj_sig = dst_s->sig;
      const island_wire_reason_t why =
         island_reader_mismatch(src_s, dst_s, cur_sig, cur_off,
                                require_exact_sig);
      if (why != WR_NONE)
         return island_wr_no(why);

      bool seen = false;
      for (unsigned k = 0; k < consumers->count; k++) {
         if (consumers->items[k] == dst_s) {
            seen = true;
            break;
         }
      }
      if (!seen)
         APUSH(*consumers, dst_s);
      *found_out = true;
      return true;
   }

   const rt_pending_t *p = untag_pointer(pending, rt_pending_t);
   for (unsigned j = 0; j < p->count; j++) {
      rt_wakeable_t *w = p->wake[j];
      if (w == NULL)
         continue;
      if (w->kind != W_WATCH)
         return island_wr_no(island_wake_reason(w));
      rt_watch_t *watch = container_of(w, rt_watch_t, wakeable);
      if (watch->fn != island_input_watch_cb)
         return island_wr_no(WR_READER_WATCH);
      rt_island_sig_t *dst_s = watch->user_data;
      island_wire_obj_sig = dst_s->sig;
      const island_wire_reason_t why =
         island_reader_mismatch(src_s, dst_s, cur_sig, cur_off,
                                require_exact_sig);
      if (why != WR_NONE)
         return island_wr_no(why);

      bool seen = false;
      for (unsigned k = 0; k < consumers->count; k++) {
         if (consumers->items[k] == dst_s) {
            seen = true;
            break;
         }
      }
      if (!seen)
         APUSH(*consumers, dst_s);
      *found_out = true;
   }
   return true;
}

static bool island_collect_port_consumers(const rt_nexus_t *nx,
                                          const rt_island_sig_t *src_s,
                                          island_sig_ptr_list_t *consumers,
                                          island_alias_list_t *aliases,
                                          bool *found_out,
                                          unsigned depth)
{
   if (nx->outputs == NULL)
      return true;
   if (depth >= 4)
      return island_wr_no(WR_PORT_DEPTH);

   const rt_island_mgr_t *mgr = src_s->isl->mgr;
   const uint32_t total = src_s->width * src_s->nelems;
   if (nx->size != src_s->elem_size || nx->width != total)
      return island_wr_no(WR_SRC_SLICE);

   for (const rt_source_t *o = nx->outputs; o != NULL; o = o->chain_output) {
      if (o->tag != SOURCE_PORT)
         return island_wr_no(WR_PORT_KIND);
      const rt_nexus_t *dst_nx = o->u.port.output;
      if (dst_nx == NULL || dst_nx->signal == NULL)
         return island_wr_no(WR_PORT_KIND);
      island_wire_obj_sig = dst_nx->signal;
      if (dst_nx->n_sources != 1)
         return island_wr_no(WR_DST_SOURCES);
      if (dst_nx->flags & (NET_F_FORCED | NET_F_INOUT))
         return island_wr_no(WR_DST_FORCED);
      if (dst_nx->width != nx->width || dst_nx->size != nx->size)
         return island_wr_no(WR_DST_SLICE);
      rt_signal_t *dst_sig = dst_nx->signal;
      if (dst_sig->resolution != NULL && !(dst_nx->flags & NET_F_R_IDENT))
         return island_wr_no(WR_DST_RESOLVED);
      if (dst_sig->shared.flags & SIG_F_PACKED)
         return island_wr_no(WR_DST_PACKED);
      if (dst_sig->shared.flags & (SIG_F_POLL_WATCHED | SIG_F_EXT_READ))
         return island_wr_no(WR_DST_POLL_EXT);
      const uint32_t dst_off = (uint32_t)(dst_nx->offset / dst_nx->size);
      if (island_sig_map_overlaps(mgr, dst_sig, dst_off, dst_nx->width))
         return island_wr_no(WR_DST_ISLAND_OWNED);

      bool local_found = false;
      if (!island_collect_pending_consumers(dst_nx->pending, src_s,
                                            dst_sig, (int32_t)dst_off, true,
                                            consumers, &local_found))
         return false;

      bool sub_found = false;
      if (dst_nx->outputs != NULL) {
         if (!island_collect_port_consumers(dst_nx, src_s, consumers, aliases,
                                            &sub_found, depth + 1))
            return false;
      }

      if (!local_found && !sub_found)
         return island_wr_no(WR_DST_NO_READER);

      bool seen_alias = false;
      for (unsigned k = 0; k < aliases->count; k++) {
         if (aliases->items[k].sig == dst_sig
             && aliases->items[k].sig_offset == (int32_t)dst_off) {
            seen_alias = true;
            break;
         }
      }
      if (!seen_alias) {
         APUSH(*aliases, ((island_alias_spec_t){
            .sig        = dst_sig,
            .sig_offset = (int32_t)dst_off,
         }));
      }

      *found_out = true;
   }
   return true;
}

static bool island_check_direct_sig(const rt_island_sig_t *src_s,
                                    island_sig_ptr_list_t *consumers,
                                    island_alias_list_t *aliases)
{
   consumers->count = 0;
   aliases->count = 0;
   island_wire_obj_sig = NULL;
   island_wire_obj_proc = NULL;
   if (src_s->sig == NULL)
      return island_wr_no(WR_NONE);
   if (src_s->next_owner != NULL)
      return island_wr_no(WR_MULTI_OWNER);
   if (src_s->flags & RT_ISIG_SPARSE_RAM)
      return island_wr_no(WR_SPARSE_RAM);
   if (src_s->flags & RT_ISIG_OBSERVED)
      return island_wr_no(WR_OBSERVED_PRE);

   const rt_signal_t *sig = src_s->sig;
   if (sig->shared.flags & SIG_F_POLL_WATCHED)
      return island_wr_no(WR_POLL);
   if (sig->shared.flags & SIG_F_EXT_READ)
      return island_wr_no(WR_EXT_READ);

   const uint32_t total = src_s->width * src_s->nelems;
   const rt_nexus_t *nx = &(sig->nexus);
   for (uint32_t i = 0; i < sig->n_nexus; i++, nx = nx->chain) {
      if (!island_nexus_overlaps_sig(nx, src_s))
         continue;
      if (nx->n_sources != 0)
         return island_wr_no(WR_SRC_NEXUS);

      if (nx->outputs != NULL) {
         if (sig->shared.flags & SIG_F_PACKED)
            return island_wr_no(WR_PACKED);
         if (nx->size != src_s->elem_size
             || nx->width != total
             || (uint32_t)(nx->offset / nx->size) != (uint32_t)src_s->sig_offset)
            return island_wr_no(WR_SRC_SLICE);
         bool port_found = false;
         if (!island_collect_port_consumers(nx, src_s, consumers, aliases,
                                            &port_found, 0))
            return false;   // reason set by the port walk
         if (!port_found)
            return island_wr_no(WR_DST_NO_READER);
      }

      bool dummy = false;
      if (!island_collect_pending_consumers(nx->pending, src_s,
                                            sig, src_s->sig_offset, false,
                                            consumers, &dummy))
         return false;   // reason set by the consumer walk
   }

   if (consumers->count == 0)
      return island_wr_no(WR_NO_CONSUMER);
   return true;
}

// A Verilog combinational output that cannot be a direct wire because the
// kernel signal has other readers (processes, clock watches, observers,
// ports) can still feed the island inputs that watch the signal itself
// directly, if the island keeps publishing it: those readers then see the
// value when the source island runs, as they do through the input watch,
// and every other reader still sees the kernel signal.
static bool island_check_direct_pub(const rt_island_sig_t *src_s,
                                    island_sig_ptr_list_t *consumers)
{
   consumers->count = 0;
   const rt_signal_t *sig = src_s->sig;
   if (sig == NULL || src_s->isl->is_vhdl
       || src_s->next_owner != NULL
       || (src_s->flags & (RT_ISIG_SPARSE_RAM | RT_ISIG_FF_WRITTEN
                           | RT_ISIG_VHDL | RT_ISIG_STD_LOGIC9))
       || (src_s->flags & (RT_ISIG_COMB_WRITTEN | RT_ISIG_OUT))
          != (RT_ISIG_COMB_WRITTEN | RT_ISIG_OUT))
      return false;

   const rt_nexus_t *nx = &(sig->nexus);
   for (uint32_t i = 0; i < sig->n_nexus; i++, nx = nx->chain) {
      if (!island_nexus_overlaps_sig(nx, src_s))
         continue;
      if (nx->n_sources != 0)
         return false;
      if (nx->pending == NULL)
         continue;

      rt_wakeable_t *const *wake;
      unsigned count;
      rt_wakeable_t *one;
      if (pointer_tag(nx->pending) == 1) {
         one = untag_pointer(nx->pending, rt_wakeable_t);
         wake = &one;
         count = 1;
      }
      else {
         const rt_pending_t *p = untag_pointer(nx->pending, rt_pending_t);
         wake = p->wake;
         count = p->count;
      }

      for (unsigned j = 0; j < count; j++) {
         rt_wakeable_t *w = wake[j];
         if (w == NULL || w->kind != W_WATCH)
            continue;
         rt_watch_t *watch = container_of(w, rt_watch_t, wakeable);
         if (watch->fn != island_input_watch_cb)
            continue;
         rt_island_sig_t *dst_s = watch->user_data;
         if (dst_s->isl->is_vhdl
             || island_reader_mismatch(src_s, dst_s, sig, src_s->sig_offset,
                                       true) != WR_NONE)
            continue;
         bool seen = false;
         for (unsigned k = 0; k < consumers->count && !seen; k++)
            seen = consumers->items[k] == dst_s;
         if (!seen)
            APUSH(*consumers, dst_s);
      }
   }
   return consumers->count > 0;
}

static const rt_nexus_t *island_wstat_nexus_at(const rt_signal_t *sig,
                                               uint32_t off)
{
   const rt_nexus_t *nx = &(sig->nexus);
   if (sig->shared.flags & SIG_F_PACKED)
      return nx;
   for (uint32_t i = 0; i < sig->n_nexus && nx != NULL; i++, nx = nx->chain) {
      const uint32_t n_sz = nx->size > 0 ? nx->size : 1u;
      const uint32_t lo = (uint32_t)(nx->offset / n_sz);
      if (off >= lo && off < lo + nx->width)
         return nx;
   }
   return NULL;
}

static rt_island_sig_t *island_wstat_owner_at(const rt_island_mgr_t *mgr,
                                              const rt_signal_t *sig,
                                              uint32_t off, uint32_t width)
{
   for (rt_island_sig_t *own = hash_get(mgr->sig_map, sig);
        own != NULL; own = own->next_owner) {
      const uint32_t o_lo = (uint32_t)own->sig_offset;
      const uint32_t o_hi = o_lo + own->width * own->nelems;
      if (o_hi > off && o_lo < off + width)
         return own;
   }
   return NULL;
}

static void island_wstat_find_writer(const rt_island_mgr_t *mgr,
                                     const rt_island_sig_t *dst_s,
                                     island_wstat_t *e)
{
   e->rd_kind = RK_OTHER;
   const rt_nexus_t *nx = island_wstat_nexus_at(dst_s->sig,
                                                (uint32_t)dst_s->sig_offset);
   for (unsigned depth = 0; depth < 8 && nx != NULL; depth++) {
      const uint32_t n_sz = nx->size > 0 ? nx->size : 1u;
      rt_island_sig_t *w = nx->signal == NULL ? NULL
         : island_wstat_owner_at(mgr, nx->signal,
                                 (uint32_t)(nx->offset / n_sz), nx->width);
      if (w != NULL && w != dst_s) {
         e->rd_kind = RK_ISLAND;
         e->writer = w;
         return;
      }
      if (nx->n_sources == 0) {
         e->rd_kind = RK_UNDRIVEN;
         return;
      }
      const rt_source_t *src = &(nx->sources);
      if (nx->n_sources > 1) {
         bool all_drv = true;
         for (const rt_source_t *q = src; q != NULL; q = q->chain_input)
            all_drv &= (q->tag == SOURCE_DRIVER);
         e->rd_kind = all_drv ? RK_PROC : RK_OTHER;
         return;
      }
      if (src->tag == SOURCE_PORT) {
         nx = src->u.port.input;
         continue;
      }
      e->rd_kind = src->tag == SOURCE_DRIVER ? RK_PROC : RK_OTHER;
      return;
   }
}

static void island_wstat_note_fail(const rt_island_sig_t *s,
                                   island_wire_reason_t r)
{
   if (r == WR_NONE)
      return;
   island_wstat_t *e = island_wstat_get(s, true);
   if (e == NULL)
      return;
   if (r == WR_OBSERVED_PRE && e->reason != WR_NONE)
      return;   // the observed scan already recorded the precise reason
   e->reason = r;
   e->other_sig = island_wire_obj_sig;
   e->other_proc = island_wire_obj_proc;
}

static void island_wstat_finalize(rt_island_mgr_t *mgr)
{
   for (unsigned i = 0; i < mgr->islands.count; i++) {
      const rt_island_t *isl = mgr->islands.items[i];
      for (uint32_t j = 0; j < isl->nsigs; j++) {
         const rt_island_sig_t *s = &(isl->sigs[j]);
         if (s->sig == NULL)
            continue;
         if (!(s->flags & (RT_ISIG_COMB_WRITTEN | RT_ISIG_FF_WRITTEN
                           | RT_ISIG_IN)))
            continue;
         island_wstat_t *e = island_wstat_get(s, true);
         if ((s->flags & RT_ISIG_IN) && e->rd_kind == RK_NONE)
            island_wstat_find_writer(mgr, s, e);
      }
   }
}

typedef struct {
   const island_wstat_t *e;
   uint64_t wake, sync_in, sync_out, deposit;
   uint32_t n;
} island_wstat_hot_t;

static int island_wstat_cmp_hot(const void *a, const void *b)
{
   const island_wstat_hot_t *x = a, *y = b;
   const uint64_t cx = x->wake + x->sync_in + x->sync_out;
   const uint64_t cy = y->wake + y->sync_in + y->sync_out;
   return cx < cy ? 1 : cx > cy ? -1 : 0;
}

static int island_wstat_cmp_obj(const void *a, const void *b)
{
   const rt_island_sig_t *x = (*(const island_wstat_t *const *)a)->s;
   const rt_island_sig_t *y = (*(const island_wstat_t *const *)b)->s;
   if (x->sig != y->sig)
      return x->sig < y->sig ? -1 : 1;
   if (x->sig_offset != y->sig_offset)
      return x->sig_offset < y->sig_offset ? -1 : 1;
   return x->width < y->width ? -1 : x->width > y->width ? 1 : 0;
}

static const char *island_wstat_label(const island_wstat_t *w, char *buf,
                                      size_t len)
{
   if (w->cause != WB_NONE)
      snprintf(buf, len, "%s -> broken:%s", island_wr_names[w->reason],
               island_wb_names[w->cause]);
   else
      snprintf(buf, len, "%s", island_wr_names[w->reason]);
   return buf;
}

static void island_wstat_name(const rt_island_sig_t *s, char *buf, size_t len)
{
   const rt_signal_t *sig = s->sig;
   const char *scope = (sig->parent != NULL && sig->parent->name != NULL)
      ? istr(sig->parent->name) : "?";
   const char *nm = sig->where != NULL ? istr(tree_ident(sig->where)) : "?";
   snprintf(buf, len, "%s.%s[%d+%u]", scope, nm, s->sig_offset,
            s->width * s->nelems);
}

typedef struct {
   char     label[96];
   uint32_t n_sigs, n_out;
   uint64_t bits, sync_out, deposit;
   uint32_t n_readers;
   uint64_t wake, sync_in;
} island_wstat_row_t;

static void island_wstat_report(const rt_island_mgr_t *mgr)
{
   if (island_wstat_list.count == 0)
      return;

   const unsigned nrows = WR_MAX * WB_MAX + RK_MAX;
   island_wstat_row_t *rows = xcalloc_array(nrows, sizeof(island_wstat_row_t));
   island_wstat_row_t tot = { .label = "TOTAL" };

   static const char *const rk_names[RK_MAX] = {
      "reader:unmapped", "reader:island", "READER<-non-island-process",
      "READER<-undriven", "READER<-other-source",
   };

   for (unsigned k = 0; k < island_wstat_list.count; k++) {
      const island_wstat_t *e = island_wstat_list.items[k];
      const rt_island_sig_t *s = e->s;
      const bool written = !!(s->flags & (RT_ISIG_COMB_WRITTEN
                                          | RT_ISIG_FF_WRITTEN));
      if (written) {
         island_wstat_row_t *r = &rows[e->reason * WB_MAX + e->cause];
         island_wstat_label(e, r->label, sizeof(r->label));
         r->n_sigs++;
         if (s->flags & RT_ISIG_OUT)
            r->n_out++;
         r->bits += (uint64_t)s->width * s->nelems;
         r->sync_out += e->sync_out;
         r->deposit += e->deposit;
         tot.n_sigs++;
         tot.sync_out += e->sync_out;
         tot.deposit += e->deposit;
      }
      if (s->flags & RT_ISIG_IN) {
         island_wstat_row_t *r;
         if (e->rd_kind == RK_ISLAND && e->writer != NULL) {
            const island_wstat_t *we = island_wstat_get(e->writer, false);
            const unsigned ri = we != NULL
               ? we->reason * WB_MAX + we->cause : 0;
            r = &rows[ri];
            if (we != NULL)
               island_wstat_label(we, r->label, sizeof(r->label));
         }
         else {
            r = &rows[WR_MAX * WB_MAX + e->rd_kind];
            snprintf(r->label, sizeof(r->label), "%s", rk_names[e->rd_kind]);
         }
         r->n_readers++;
         r->wake += e->wake;
         r->sync_in += e->sync_in;
         tot.n_readers++;
         tot.wake += e->wake;
         tot.sync_in += e->sync_in;
      }
   }

   FILE *f = stderr;
   fprintf(f, "ISLAND_WIRE: islands=%u super_islands=%u super_domains=%u\n",
           mgr->islands.count, mgr->clusters.count, mgr->super_domains.count);
   fprintf(f, "ISLAND_WIRE: clk_cb=%"PRIu64" rst_cb=%"PRIu64
           " super_clk_cb=%"PRIu64" super_rst_cb=%"PRIu64"\n",
           island_wstat_clk_cb, island_wstat_rst_cb,
           island_wstat_sclk_cb, island_wstat_srst_cb);
   fprintf(f, "ISLAND_WIRE: %-44s %6s %6s %8s %9s %9s | %5s %10s %10s\n",
           "class (writer side)", "wsigs", "out", "bits", "out_call",
           "deposit", "rdrs", "wake", "sync_in");
   for (unsigned i = 0; i < nrows; i++) {
      const island_wstat_row_t *r = &rows[i];
      if (r->n_sigs == 0 && r->n_readers == 0)
         continue;
      fprintf(f, "ISLAND_WIRE: %-44s %6u %6u %8"PRIu64" %9"PRIu64" %9"PRIu64
              " | %5u %10"PRIu64" %10"PRIu64"\n", r->label, r->n_sigs,
              r->n_out, r->bits, r->sync_out, r->deposit, r->n_readers,
              r->wake, r->sync_in);
   }
   fprintf(f, "ISLAND_WIRE: %-44s %6u %6s %8s %9"PRIu64" %9"PRIu64
           " | %5u %10"PRIu64" %10"PRIu64"\n", tot.label, tot.n_sigs, "", "",
           tot.sync_out, tot.deposit, tot.n_readers, tot.wake, tot.sync_in);
   free(rows);
   // Callers that attached observers to island-written signals (level 2:
   // addresses differ per run so they are not part of the default output)
   if (island_wstats_level >= 2) {
      void *ras[64];
      unsigned cnt[64] = { 0 };
      unsigned nra = 0;
      for (unsigned k = 0; k < island_wstat_list.count; k++) {
         const island_wstat_t *e = island_wstat_list.items[k];
         if (e->observer_ra == NULL)
            continue;
         unsigned q;
         for (q = 0; q < nra; q++) {
            if (ras[q] == e->observer_ra)
               break;
         }
         if (q == nra) {
            if (nra == 64)
               continue;
            ras[nra++] = e->observer_ra;
         }
         cnt[q]++;
      }
      // Resolve offline: addr2line -f -e <nvc> <ra - ref + nm(rt_island_mgr_new)>
      for (unsigned q = 0; q < nra; q++)
         fprintf(f, "ISLAND_WIRE_OBS: sigs=%u ra=%p ref=%p\n", cnt[q],
                 ras[q], (void *)rt_island_mgr_new);
   }

   // A few examples per writer-side class
   {
      unsigned shown[WR_MAX * WB_MAX] = { 0 };
      for (unsigned k = 0; k < island_wstat_list.count; k++) {
         const island_wstat_t *e = island_wstat_list.items[k];
         if (!(e->s->flags & (RT_ISIG_COMB_WRITTEN | RT_ISIG_FF_WRITTEN))
             || e->reason == WR_DIRECT || e->reason == WR_NO_CONSUMER)
            continue;
         const unsigned ri = e->reason * WB_MAX + e->cause;
         if (shown[ri]++ >= 4)
            continue;
         char nm[256], lb[128], on[256] = "";
         island_wstat_name(e->s, nm, sizeof(nm));
         if (e->other_sig != NULL) {
            const rt_signal_t *o = e->other_sig;
            snprintf(on, sizeof(on), " -> %s.%s",
                     o->parent != NULL && o->parent->name != NULL
                     ? istr(o->parent->name) : "?",
                     o->where != NULL ? istr(tree_ident(o->where)) : "?");
         }
         else if (e->other_proc != NULL)
            snprintf(on, sizeof(on), " -> proc %s", istr(e->other_proc));
         fprintf(f, "ISLAND_WIRE_EX: [%s] %s%s\n",
                 island_wstat_label(e, lb, sizeof(lb)), nm, on);
      }
   }

   // Hottest signals, merged over the islands that share the signal object
   island_wstat_t **tmp = xmalloc_array(island_wstat_list.count,
                                        sizeof(island_wstat_t *));
   memcpy(tmp, island_wstat_list.items,
          island_wstat_list.count * sizeof(island_wstat_t *));
   qsort(tmp, island_wstat_list.count, sizeof(island_wstat_t *),
         island_wstat_cmp_obj);

   island_wstat_hot_t *hot = xcalloc_array(island_wstat_list.count,
                                           sizeof(island_wstat_hot_t));
   unsigned nhot = 0;
   for (unsigned k = 0; k < island_wstat_list.count; k++) {
      const island_wstat_t *e = tmp[k];
      if (nhot > 0
          && hot[nhot - 1].e->s->sig == e->s->sig
          && hot[nhot - 1].e->s->sig_offset == e->s->sig_offset
          && hot[nhot - 1].e->s->width == e->s->width) {
         island_wstat_hot_t *h = &hot[nhot - 1];
         h->wake += e->wake;
         h->sync_in += e->sync_in;
         h->sync_out += e->sync_out;
         h->deposit += e->deposit;
         h->n++;
         if (e->writer != NULL && h->e->writer == NULL)
            h->e = e;
      }
      else {
         hot[nhot++] = (island_wstat_hot_t){ e, e->wake, e->sync_in, e->sync_out,
                                e->deposit, 1 };
      }
   }
   qsort(hot, nhot, sizeof(island_wstat_hot_t), island_wstat_cmp_hot);
   const unsigned top = MIN(nhot, 40u);
   for (unsigned k = 0; k < top; k++) {
      const island_wstat_hot_t *h = &hot[k];
      const island_wstat_t *e = h->e;
      if (h->wake + h->sync_in + h->sync_out == 0)
         break;
      char nm[256], lb[128];
      island_wstat_name(e->s, nm, sizeof(nm));
      const island_wstat_t *we = e->rd_kind == RK_ISLAND && e->writer != NULL
         ? island_wstat_get(e->writer, false) : e;
      const char *rk = e->rd_kind == RK_PROC ? " <-proc"
         : e->rd_kind == RK_UNDRIVEN ? " <-undriven" : "";
      fprintf(f, "ISLAND_WIRE_TOP: wake=%"PRIu64" sync_in=%"PRIu64
              " out_call=%"PRIu64" deposit=%"PRIu64" n=%u w=%u %s%s%s%s%s [%s]\n",
              h->wake, h->sync_in, h->sync_out, h->deposit, h->n,
              e->s->width * e->s->nelems,
              (e->s->flags & RT_ISIG_IN) ? "in " : "",
              (e->s->flags & RT_ISIG_OUT) ? "out " : "",
              (e->s->flags & RT_ISIG_COMB_WRITTEN) ? "comb " : "",
              nm, rk,
              island_wstat_label(we != NULL ? we : e, lb, sizeof(lb)));
   }
   free(hot);
   free(tmp);
}

static void island_wstat_free(void)
{
   for (unsigned k = 0; k < island_wstat_list.count; k++)
      free(island_wstat_list.items[k]);
   ACLEAR(island_wstat_list);
   if (island_wstats != NULL) {
      hash_free(island_wstats);
      island_wstats = NULL;
   }
}


typedef struct {
   rt_island_sig_t     *src_s;
   rt_island_sig_t    **targets;
   uint32_t             n_targets;
   island_alias_spec_t *aliases;
   uint32_t             n_aliases;
   bool                 rejected;
   bool                 pub;   // RT_ISIG_DIRECT_PUB
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
   island_alias_list_t tmp_aliases = AINIT;

   for (uint32_t i = 0; i < N; i++) {
      rt_island_t *isl = mgr->islands.items[i];
      for (uint32_t j = 0; j < isl->nsigs; j++) {
         rt_island_sig_t *src_s = &(isl->sigs[j]);
         if (!(src_s->flags & (RT_ISIG_COMB_WRITTEN | RT_ISIG_FF_WRITTEN)))
            continue;
         bool pub = false;
         if (!island_check_direct_sig(src_s, &tmp_cons, &tmp_aliases)) {
            if (unlikely(island_wstats_on))
               island_wstat_note_fail(src_s, island_wire_why);
            if (!island_direct_pub_on
                || !island_check_direct_pub(src_s, &tmp_cons))
               continue;
            tmp_aliases.count = 0;
            pub = true;
         }

         hier_cand_t c = {
            .src_s     = src_s,
            .targets   = xmalloc_array(tmp_cons.count,
                                       sizeof(rt_island_sig_t *)),
            .n_targets = tmp_cons.count,
            .aliases   = tmp_aliases.count > 0
               ? xmalloc_array(tmp_aliases.count, sizeof(island_alias_spec_t))
               : NULL,
            .n_aliases = tmp_aliases.count,
            .rejected  = false,
            .pub       = pub,
         };
         memcpy(c.targets, tmp_cons.items,
                tmp_cons.count * sizeof(rt_island_sig_t *));
         if (tmp_aliases.count > 0)
            memcpy(c.aliases, tmp_aliases.items,
                   tmp_aliases.count * sizeof(island_alias_spec_t));
         APUSH(cands, c);
      }
   }
   ACLEAR(tmp_cons);
   ACLEAR(tmp_aliases);

   // 2. Build combinational dependency graph & break any cycles via DFS
   uint32_t *comb_deg LOCAL = xcalloc_array(N, sizeof(uint32_t));
   // The readers of a published source (RT_ISIG_DIRECT_PUB) run in the
   // next delta cycle like those of a kernel signal: no ordering edge and
   // no cluster, as without the wire
   for (unsigned c = 0; c < cands.count; c++) {
      const hier_cand_t *hc = &(cands.items[c]);
      if (!(hc->src_s->flags & RT_ISIG_COMB_WRITTEN) || hc->pub)
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
      if (!(hc->src_s->flags & RT_ISIG_COMB_WRITTEN) || hc->pub)
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
            if (color[v] == 1) {
               // A back edge closes a cycle in the island graph.  This is
               // usually not a combinational loop between signals: two
               // islands often feed each other through independent
               // paths.  SV islands keep the wire: the reader sits
               // earlier in the cluster order, so a change marks it dirty
               // again and island_eval_cluster() requeues the cluster for
               // the next pass, which is when the reader would have seen
               // the signal update otherwise.  A real loop then still
               // counts delta cycles.  VHDL islands keep the signal.
               hier_cand_t *hc = &(cands.items[e.cand_idx]);
               if (hc->src_s->isl->is_vhdl
                   || mgr->islands.items[v]->is_vhdl) {
                  hc->rejected = true;
                  if (unlikely(island_wstats_on))
                     island_wstat_note(hc->src_s, WR_CYCLE);
               }
            }
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
         free(hc->aliases);
         continue;
      }

      rt_island_sig_t *src_s = hc->src_s;
      if (unlikely(island_wstats_on))
         island_wstat_note(src_s, hc->pub ? WR_DIRECT_PUB : WR_DIRECT);
      src_s->direct_targets = hc->targets;
      src_s->n_direct_targets = hc->n_targets;
      if (hc->pub) {
         // Dormant until island_activate_direct_pub(): the readers keep
         // their input watches through time zero
         src_s->flags |= RT_ISIG_DIRECT_PUB;
         src_s->isl->has_direct_pub = true;
         mgr->pub_pending = true;
         continue;
      }
      src_s->flags |= RT_ISIG_DIRECT_LINK;

      if (hc->n_aliases > 0) {
         src_s->direct_aliases = xcalloc_array(hc->n_aliases,
                                               sizeof(rt_island_alias_t));
         src_s->n_direct_aliases = hc->n_aliases;
         for (uint32_t a = 0; a < hc->n_aliases; a++) {
            rt_island_alias_t *al = &(src_s->direct_aliases[a]);
            al->sig = hc->aliases[a].sig;
            al->src_s = src_s;
            al->sig_offset = hc->aliases[a].sig_offset;
            al->sig->shared.flags |= SIG_F_ISLAND;
            al->sig->shared.flags &= ~(SIG_F_FAST_NOOP | SIG_F_FAST_DIRECT);
            al->next = hash_get(mgr->alias_map, al->sig);
            hash_put(mgr->alias_map, al->sig, al);
         }
         free(hc->aliases);
      }

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
         if (island_sig_published(isl->sigs[isl->out_sigs[j]].flags))
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
            if (island_sig_published(isl->sigs[sidx].flags))
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
         cl->dirty_in_list = xmalloc_array(uf_sz[r], sizeof(rt_island_t *));
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
         sd->ff_entries     = xcalloc_array(sd->n_doms,
                                            sizeof(rt_sdom_ff_entry_t));

         A(rt_super_island_t *) cls = AINIT;
         A(rt_island_t *) solos = AINIT;

         for (uint32_t j = 0; j < sd->n_doms; j++) {
            rt_island_domain_t *dom = g->doms.items[j];
            sd->doms[j] = dom;
            dom->super_dom = sd;
            sd->ff_entries[j].inst_ctx = dom->isl->inst_ctx;
            sd->ff_entries[j].state    = dom->isl->state;
            sd->ff_entries[j].dom      = dom;

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
         const bool sv_edge = !(sd->edge_flags
                                & (RT_IEDGE_VHDL | RT_IEDGE_VHDL_EVENT));
         watch_set_inline_edge(sd->clk_watch, sv_edge);
         if (sd->clk_sig->shared.flags & SIG_F_PACKED)
            model_set_event_cb(mgr->model, sd->clk_sig, 0, 1, sd->clk_watch);
         else
            model_set_event_cb(mgr->model, sd->clk_sig, sd->clk_sig_offset, 1,
                               sd->clk_watch);

         if (sd->rst_sig != NULL) {
            sd->rst_watch = watch_new(mgr->model, island_super_rst_watch_cb,
                                      sd, WATCH_EVENT, 1);
            watch_set_inline_edge(sd->rst_watch, sv_edge
                                  && !(sd->edge_flags & RT_IEDGE_RST_LEVEL));
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

   if (unlikely(island_wstats_on))
      island_wstat_finalize(mgr);
}

static void island_classify_outputs(rt_island_mgr_t *mgr)
{
   const bool allow_prune = mgr->fast && mgr->hier_built;
   uint32_t n_sole = 0, n_pruned = 0;

   for (unsigned i = 0; i < mgr->islands.count; i++) {
      rt_island_t *isl = mgr->islands.items[i];
      for (uint32_t j = 0; j < isl->nsigs; j++) {
         rt_island_sig_t *s = &(isl->sigs[j]);
         s->flags &= ~(RT_ISIG_SOLE_OWNER | RT_ISIG_PRUNED
                       | RT_ISIG_LAST_OUT_VALID);
         if (s->sig == NULL
             || !(s->flags & (RT_ISIG_COMB_WRITTEN | RT_ISIG_FF_WRITTEN)))
            continue;
         if (s->next_owner != NULL || s->ext_write_watch != NULL
             || hash_get(mgr->sig_map, s->sig) != s)
            continue;

         const rt_signal_t *sig = s->sig;
         bool sole = true;
         bool has_any_obs = !!(sig->shared.flags
                               & (SIG_F_POLL_WATCHED | SIG_F_EXT_READ));
         const rt_nexus_t *nx = &(sig->nexus);
         for (uint32_t ni = 0; ni < sig->n_nexus; ni++, nx = nx->chain) {
            if (!island_nexus_overlaps_sig(nx, s))
               continue;
            if (nx->n_sources != 0 || (nx->flags & NET_F_FORCED)) {
               sole = false;
               break;
            }
            if (nx->outputs != NULL || nx->pending != NULL)
               has_any_obs = true;
         }
         if (!sole)
            continue;

         s->flags |= RT_ISIG_SOLE_OWNER;
         n_sole++;

         if (allow_prune
             && (s->flags & RT_ISIG_OUT)
             && !(s->flags & (RT_ISIG_DIRECT_LINK | RT_ISIG_OBSERVED))
             && !has_any_obs) {
            s->flags |= RT_ISIG_PRUNED;
            n_pruned++;
         }
      }

      if (mgr->hier_built) {
         uint32_t n_ext = 0;
         for (uint32_t j = 0; j < isl->n_out_sigs; j++) {
            const uint32_t sidx = isl->out_sigs[j];
            const uint32_t f = isl->sigs[sidx].flags;
            if (island_sig_published(f) && !(f & RT_ISIG_PRUNED))
               n_ext++;
         }
         free(isl->ext_out_sigs);
         isl->ext_out_sigs = NULL;
         isl->n_ext_out_sigs = n_ext;
         if (n_ext > 0) {
            isl->ext_out_sigs = xmalloc_array(n_ext, sizeof(uint32_t));
            uint32_t p = 0;
            for (uint32_t j = 0; j < isl->n_out_sigs; j++) {
               const uint32_t sidx = isl->out_sigs[j];
               const uint32_t f = isl->sigs[sidx].flags;
               if (island_sig_published(f) && !(f & RT_ISIG_PRUNED))
                  isl->ext_out_sigs[p++] = sidx;
            }
         }
      }
   }

   if (mgr->fast && opt_get_int(OPT_DUMP_ISLANDS))
      fprintf(stderr, "ISLAND_FAST: fast=%d sole_owner=%u pruned=%u\n",
              (int)mgr->fast, n_sole, n_pruned);
}

static void island_break_direct_link(rt_model_t *m, rt_island_sig_t *src_s);

void rt_island_reset_all(rt_island_mgr_t *mgr)
{
   if (mgr == NULL)
      return;

   // A reset after time zero (restart) goes back to the input watches of
   // the RT_ISIG_DIRECT_PUB readers for good: they are only made once
   for (unsigned i = 0; i < mgr->islands.count; i++) {
      rt_island_t *isl = mgr->islands.items[i];
      if (!isl->has_direct_pub)
         continue;
      for (uint32_t j = 0; j < isl->nsigs; j++) {
         if (isl->sigs[j].flags & RT_ISIG_DIRECT_PUB)
            island_break_direct_link(mgr->model, &(isl->sigs[j]));
      }
   }

   mgr->fast = !!opt_get_int(OPT_ISLAND_FAST);
   mgr->n_forced_islands = 0;
   island_spec_reset(mgr);
   mgr->late_pending = false;
   mgr->dirty_q.count = 0;
   mgr->defer_dirty_q.count = 0;
   mgr->defer_direct_q.count = 0;
   for (unsigned i = 0; i < mgr->defer_put_q.count; i++)
      free(mgr->defer_put_q.items[i].buf);
   mgr->defer_put_q.count = 0;
   mgr->ff_sample_q.count = 0;
   mgr->ff_commit_q.count = 0;
   mgr->dirty_clusters.count = 0;
   mgr->super_sample_q.count = 0;
   mgr->super_commit_q.count = 0;
   memset(&mgr->mt_gate_cl, 0, sizeof(mgr->mt_gate_cl));
   memset(&mgr->mt_gate_isl, 0, sizeof(mgr->mt_gate_isl));
   memset(&mgr->mt_gate_ff, 0, sizeof(mgr->mt_gate_ff));
   memset(&mgr->mt_gate_sva, 0, sizeof(mgr->mt_gate_sva));

   for (unsigned i = 0; i < mgr->clusters.count; i++) {
      rt_super_island_t *cl = mgr->clusters.items[i];
      cl->has_dirty = false;
      cl->has_dirty_inputs = false;
      cl->n_dirty_in = 0;
      cl->dirty_count = 0;
      cl->min_dirty_pos = UINT32_MAX;
      cl->mt_sync_start = 0;
      cl->mt_sync_end = 0;
      cl->pending_vhdl_ff = 0;
   }

   for (unsigned i = 0; i < mgr->islands.count; i++) {
      rt_island_t *isl = mgr->islands.items[i];
      isl->eval_func = NULL;
      isl->eval_fn = NULL;
      isl->need_sync_out = false;
      isl->ff_pending_head = NULL;
      isl->ff_pending_tail = NULL;
      isl->pending_vhdl_ff = 0;
      for (uint32_t f = 0; f < isl->n_forces; f++)
         free(isl->forces[f].mask);
      isl->n_forces = 0;
      isl->has_forces = false;

      island_init_state_defaults(isl);
      if (isl->sparse_log_off > 0) {
         rt_island_sparse_log_t *log =
            (rt_island_sparse_log_t *)(isl->state + isl->sparse_log_off);
         log->count = 0;
         log->data_used = 0;
         log->isl_ptr = (uint64_t)(uintptr_t)isl;
         isl->n_sparse_ovf = 0;
         isl->sparse_ovf_data_used = 0;
      }
      isl->is_vhdl = false;
      for (uint32_t j = 0; j < isl->nsigs; j++) {
         if (isl->sigs[j].flags & (RT_ISIG_VHDL | RT_ISIG_STD_LOGIC9
                                   | RT_ISIG_VHDL_SCHED)) {
            isl->is_vhdl = true;
            break;
         }
      }
      const bool allow_inline = !isl->is_vhdl
         || (mgr->fast && !opt_get_int(OPT_SHUFFLE_PROCS));
      for (uint32_t j = 0; j < isl->nsigs; j++) {
         rt_island_sig_t *s = &(isl->sigs[j]);
         s->flags &= ~(RT_ISIG_LAST_OUT_VALID | RT_ISIG_MIX_MEMO);
         if (s->watch != NULL)
            watch_set_inline_island(s->watch, allow_inline);
         if (s->sig != NULL)
            island_sync_in_sig(isl, s);
      }
      if (isl->next_state != NULL)
         memcpy(isl->next_state, isl->state, isl->state_bytes);

      island_init_feedback(isl);
      island_init_act(isl);
      island_clear_in_dirty(isl);

      for (uint32_t d = 0; d < isl->ndomains; d++) {
         rt_island_domain_t *dom = &(isl->domains[d]);
         dom->ff_func = NULL;
         dom->ff_fn = NULL;
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
         dom->ff_dirty_mask = 0;
      }

      isl->dirty = false;
      isl->in_dirty_list = false;
      isl->in_defer_dirty = false;
      isl->in_defer_direct = false;
      isl->mt_fb_changed = false;
      isl->mt_defer_direct = false;
      isl->has_dirty_inputs = false;
      isl->in_cl_dirty_in = false;
   }

   // Re-scan observed status now that all child scopes have run map_signal
   // and sched_event (rt_island_finish ran per-scope before child init).
   mgr->has_vhdl = false;
   for (unsigned i = 0; i < mgr->islands.count; i++) {
      rt_island_t *isl = mgr->islands.items[i];
      isl->is_vhdl = false;
      for (uint32_t j = 0; j < isl->nsigs; j++) {
         rt_island_sig_t *s = &(isl->sigs[j]);
         if (s->flags & (RT_ISIG_VHDL | RT_ISIG_STD_LOGIC9 | RT_ISIG_VHDL_SCHED)) {
            isl->is_vhdl = true;
            mgr->has_vhdl = true;
         }
         if (s->sig == NULL
             || !(s->flags & (RT_ISIG_COMB_WRITTEN | RT_ISIG_FF_WRITTEN))
             || (s->flags & (RT_ISIG_OBSERVED | RT_ISIG_DIRECT_LINK)))
            continue;
         if (island_sig_observed(s)) {
            if (unlikely(island_wstats_on))
               island_wstat_note(s, island_wire_why);
            s->flags |= RT_ISIG_OBSERVED;
            if ((s->flags & RT_ISIG_COMB_WRITTEN)
                && !(s->flags & RT_ISIG_OUT))
               island_add_obs_sig(isl, j);
         }
      }
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
            sd->used_fast_sample = false;
            for (uint32_t d = 0; d < sd->n_doms; d++) {
               sd->ff_entries[d].ff_func = NULL;
               sd->ff_entries[d].ff_fn = NULL;
               sd->ff_entries[d].dirty_mask = 0;
            }
         }
      }

      for (unsigned i = 0; i < mgr->clusters.count; i++) {
         rt_super_island_t *cl = mgr->clusters.items[i];
         cl->is_vhdl = false;
         for (uint32_t k = 0; k < cl->n_islands; k++) {
            if (cl->islands[k]->is_vhdl) {
               cl->is_vhdl = true;
               break;
            }
         }
      }
   }

   island_classify_outputs(mgr);

   for (unsigned i = 0; i < mgr->islands.count; i++) {
      rt_island_t *isl = mgr->islands.items[i];
      island_refresh_domain_descs(isl);
      island_refresh_direct_comb_descs(isl);
      island_refresh_ext_out_descs(isl);
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
                  && !(s->flags & RT_ISIG_PRUNED)
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
               const bool two_state =
                  (rs->flags & (RT_ISIG_TWO_STATE | RT_ISIG_SPEC_A))
                  == RT_ISIG_TWO_STATE;
               if (island_vhdl_rst_match(cur, dom->rst_posedge,
                                         is_std, two_state)) {
                  if (dom->super_dom != NULL)
                     island_sdom_enqueue_sample(mgr, dom->super_dom);
                  else
                     island_dom_enqueue_sample(mgr, dom);
               }
            }
         }
      }
      if (!no_comb)
         island_mark_dirty(isl);
   }

   mgr->reset_done = true;
}

static bool island_eligible_sv_reset(const rt_island_t *isl)
{
   if (isl->eval_handle == JIT_HANDLE_INVALID)
      return false;

   for (uint32_t j = 0; j < isl->nsigs; j++) {
      if (isl->sigs[j].flags & (RT_ISIG_STD_LOGIC9 | RT_ISIG_VHDL_SCHED
                                | RT_ISIG_VHDL))
         return false;
   }

   for (uint32_t d = 0; d < isl->ndomains; d++) {
      const rt_island_domain_t *dom = &(isl->domains[d]);
      if (dom->edge_flags & (RT_IEDGE_VHDL | RT_IEDGE_VHDL_EVENT
                             | RT_IEDGE_RST_LEVEL))
         return false;
      if (isl->sigs[dom->clk_sig_idx].flags & RT_ISIG_COMB_WRITTEN)
         return false;
      if (dom->rst_sig_idx >= 0
          && (isl->sigs[dom->rst_sig_idx].flags & RT_ISIG_COMB_WRITTEN))
         return false;
   }

   return true;
}

static inline bool island_call_comb(jit_t *jit, rt_model_t *m, rt_island_t *isl,
                                    tlab_t *tlab);

bool rt_island_eval_sv_reset(jit_t *jit, rt_model_t *m, rt_island_mgr_t *mgr,
                             tlab_t *tlab, bool first_pass, bool *progress_out)
{
   if (mgr == NULL)
      return true;

   // Evaluate combinational SV islands at the end of model_reset() so
   // continuous-assign chains driven from parameters or decl-initialised
   // registers settle before cbStartOfSimulation / 0ms+0, matching
   // non-island continuous assignments.
   bool progress = false;
   for (unsigned i = 0; i < mgr->islands.count; i++) {
      rt_island_t *isl = mgr->islands.items[i];
      if (!island_eligible_sv_reset(isl))
         continue;

      bool need_eval = isl->dirty;
      for (uint32_t j = 0; j < isl->n_in_sigs; j++) {
         rt_island_sig_t *s = &(isl->sigs[isl->in_sigs[j]]);
         if (s->flags & RT_ISIG_DIRECT_LINK) {
            if (island_copy_direct_sig(s->direct_src, s))
               need_eval = true;
         }
         else if (s->sig != NULL) {
            if (first_pass || need_eval) {
               island_sync_in_sig(isl, s);
            }
            else {
               const uint32_t nbytes = s->nwords * sizeof(uint64_t);
               const bool two_state = !!(s->flags & RT_ISIG_TWO_STATE);
               uint64_t old_a[8], old_b[8];
               uint64_t *ha = NULL, *hb = NULL;
               uint64_t *pa = old_a, *pb = old_b;
               if (s->nwords > ARRAY_LEN(old_a)) {
                  ha = xmalloc(nbytes);
                  pa = ha;
                  if (!two_state) {
                     hb = xmalloc(nbytes);
                     pb = hb;
                  }
               }
               memcpy(pa, isl->state + s->a_off, nbytes);
               if (!two_state)
                  memcpy(pb, isl->state + s->b_off, nbytes);
               island_sync_in_sig(isl, s);
               if (memcmp(pa, isl->state + s->a_off, nbytes) != 0
                   || (!two_state
                       && memcmp(pb, isl->state + s->b_off, nbytes) != 0))
                  need_eval = true;
               free(ha);
               free(hb);
            }
         }
      }
      island_clear_in_dirty(isl);
      isl->has_dirty_inputs = false;

      if (!need_eval)
         continue;

      island_clear_dirty(isl);

      if (isl->skip_sv_reset)
         continue;

      if (isl->reset_eval_handle != JIT_HANDLE_INVALID) {
         jit_scalar_t args[] = {
            { .pointer = isl->reset_eval_ctx },
            { .pointer = isl->state },
         };
         if (!jit_vfastcall(jit, isl->reset_eval_handle, args, ARRAY_LEN(args),
                            NULL, 0, tlab))
            return false;
         if (unlikely(isl->has_forces))
            island_apply_forces_comb(isl);
      }
      else if (!island_call_comb(jit, m, isl, tlab))
         return false;

      // Signals written by always_comb blocks, and those computed from
      // them, change at time zero as without islands: they are published
      // by island_publish_late()
      const uint32_t skip = RT_ISIG_PRUNED | RT_ISIG_RESET_LATE;
      if (isl->n_direct_comb_descs > 0 && !isl->has_reset_late)
         island_propagate_direct_comb(isl);
      else if (isl->n_direct_comb_outs > 0) {
         for (uint32_t j = 0; j < isl->n_direct_comb_outs; j++) {
            const rt_island_sig_t *src_s =
               &(isl->sigs[isl->direct_comb_outs[j]]);
            if (!(src_s->flags & RT_ISIG_DIRECT_LINK)
                || (src_s->flags & RT_ISIG_RESET_LATE))
               continue;
            for (uint32_t t = 0; t < src_s->n_direct_targets; t++) {
               rt_island_sig_t *dst_s = src_s->direct_targets[t];
               if (island_copy_direct_sig(src_s, dst_s)
                   && (dst_s->flags & RT_ISIG_COMB_READ)
                   && dst_s->isl->eval_handle != JIT_HANDLE_INVALID)
                  island_mark_dirty(dst_s->isl);
            }
         }
      }
      if (mgr->hier_built) {
         for (uint32_t j = 0; j < isl->n_ext_out_sigs; j++) {
            rt_island_sig_t *os = &(isl->sigs[isl->ext_out_sigs[j]]);
            if (likely(!(os->flags & skip)))
               island_sync_out_sig(m, isl, os);
         }
      }
      else {
         for (uint32_t j = 0; j < isl->n_out_sigs; j++) {
            rt_island_sig_t *os = &(isl->sigs[isl->out_sigs[j]]);
            if (likely(!(os->flags & skip)))
               island_sync_out_sig(m, isl, os);
         }
      }
      if (unlikely(isl->n_obs_sigs > 0)) {
         for (uint32_t j = 0; j < isl->n_obs_sigs; j++) {
            rt_island_sig_t *os = &(isl->sigs[isl->obs_sigs[j]]);
            if (likely(!(os->flags & skip)))
               island_sync_out_sig(m, isl, os);
         }
      }

      progress = true;
   }

   if (progress_out != NULL)
      *progress_out = progress;
   return true;
}

void rt_island_mark_sv_reset_edges(rt_island_mgr_t *mgr)
{
   if (mgr == NULL)
      return;

   // Any SV clock/reset signal driven to a new value during model_reset()
   // (e.g. a continuous assign to a variable/net) already queued its watch
   // callback for 0ms+0 and must not be suppressed as a VPI t=0 deposit.
   for (unsigned i = 0; i < mgr->islands.count; i++) {
      rt_island_t *isl = mgr->islands.items[i];
      if (isl->has_reset_late && island_eligible_sv_reset(isl))
         mgr->late_pending = true;   // See island_publish_late
      for (uint32_t d = 0; d < isl->ndomains; d++) {
         rt_island_domain_t *dom = &(isl->domains[d]);
         dom->reset_clk_edge = false;
         dom->reset_rst_edge = false;
         if (dom->super_dom != NULL)
            continue;
         if (!(dom->edge_flags & (RT_IEDGE_VHDL | RT_IEDGE_VHDL_EVENT))) {
            const rt_island_sig_t *cs = &(isl->sigs[dom->clk_sig_idx]);
            if (cs->sig != NULL
                && island_read_sv_bit(cs->sig, cs->sig_offset) != dom->prev_clk)
               dom->reset_clk_edge = true;
         }
         if (dom->rst_sig_idx >= 0 && !(dom->edge_flags & RT_IEDGE_RST_LEVEL)) {
            const rt_island_sig_t *rs = &(isl->sigs[dom->rst_sig_idx]);
            if (rs->sig != NULL
                && island_read_sv_bit(rs->sig, rs->sig_offset) != dom->prev_rst)
               dom->reset_rst_edge = true;
         }
      }
   }
   for (unsigned i = 0; i < mgr->super_domains.count; i++) {
      rt_super_domain_t *sd = mgr->super_domains.items[i];
      sd->reset_clk_edge = false;
      sd->reset_rst_edge = false;
      if (!(sd->edge_flags & (RT_IEDGE_VHDL | RT_IEDGE_VHDL_EVENT))) {
         if (sd->clk_sig != NULL
             && island_read_sv_bit(sd->clk_sig, sd->clk_sig_offset) != sd->prev_clk)
            sd->reset_clk_edge = true;
      }
      if (sd->rst_sig != NULL && !(sd->edge_flags & RT_IEDGE_RST_LEVEL)) {
         if (island_read_sv_bit(sd->rst_sig, sd->rst_sig_offset) != sd->prev_rst)
            sd->reset_rst_edge = true;
      }
   }
}

bool rt_island_has_dirty(const rt_island_mgr_t *mgr)
{
   return mgr != NULL
      && (mgr->dirty_q.count > 0 || mgr->dirty_clusters.count > 0
          || mgr->defer_dirty_q.count > 0 || mgr->defer_direct_q.count > 0
          || mgr->defer_put_q.count > 0);
}

bool rt_island_has_sv_dirty(const rt_island_mgr_t *mgr)
{
   if (mgr == NULL
       || (mgr->dirty_q.count == 0 && mgr->dirty_clusters.count == 0))
      return false;
   if (likely(!mgr->has_vhdl))
      return true;
   for (unsigned i = 0; i < mgr->dirty_q.count; i++) {
      const rt_island_t *isl = mgr->dirty_q.items[i];
      if (isl->dirty && !isl->is_vhdl)
         return true;
   }
   for (unsigned i = 0; i < mgr->dirty_clusters.count; i++) {
      const rt_super_island_t *cl = mgr->dirty_clusters.items[i];
      if (cl->has_dirty && !cl->is_vhdl)
         return true;
   }
   return false;
}

static void island_add_isl_dep(rt_island_t *isl, rt_island_t *dep)
{
   for (uint32_t i = 0; i < isl->n_comb_deps; i++) {
      if (isl->comb_deps[i] == dep)
         return;
   }
   isl->comb_deps = xrealloc_array(isl->comb_deps, isl->n_comb_deps + 1u,
                                   sizeof(rt_island_t *));
   isl->comb_deps[isl->n_comb_deps++] = dep;
}

static void island_add_cl_dep(rt_super_island_t *cl, rt_super_island_t *dep)
{
   for (uint32_t i = 0; i < cl->n_comb_deps; i++) {
      if (cl->comb_deps[i] == dep)
         return;
   }
   cl->comb_deps = xrealloc_array(cl->comb_deps, cl->n_comb_deps + 1u,
                                  sizeof(rt_super_island_t *));
   cl->comb_deps[cl->n_comb_deps++] = dep;
}

static void island_build_mt_deps(rt_island_mgr_t *mgr)
{
   if (mgr->mt_deps_built)
      return;
   mgr->mt_deps_built = true;

   const uint32_t N = mgr->islands.count;
   for (uint32_t i = 0; i < N; i++) {
      rt_island_t *isl = mgr->islands.items[i];
      for (uint32_t j = 0; j < isl->n_in_sigs; j++) {
         const rt_island_sig_t *in_s = &(isl->sigs[isl->in_sigs[j]]);
         if (!(in_s->flags & RT_ISIG_COMB_READ)
             || (in_s->flags & RT_ISIG_DIRECT_LINK)
             || in_s->sig == NULL)
            continue;

         const uint32_t in_start = (uint32_t)in_s->sig_offset;
         const uint32_t in_end = in_start + in_s->width * in_s->nelems;
         for (rt_island_sig_t *own = hash_get(mgr->sig_map, in_s->sig);
              own != NULL; own = own->next_owner) {
            if (own->isl == isl || !(own->flags & RT_ISIG_COMB_WRITTEN))
               continue;
            const uint32_t o_start = (uint32_t)own->sig_offset;
            const uint32_t o_end = o_start + own->width * own->nelems;
            if (MAX(in_start, o_start) >= MIN(in_end, o_end))
               continue;

            if (isl->cluster == NULL && own->isl->cluster == NULL) {
               island_add_isl_dep(isl, own->isl);
               mgr->mt_has_comb_deps = true;
            }
            else if (isl->cluster != NULL && own->isl->cluster != NULL
                     && isl->cluster != own->isl->cluster) {
               island_add_cl_dep(isl->cluster, own->isl->cluster);
               mgr->mt_has_comb_deps = true;
            }
         }
      }
   }
}

static __thread bool island_jit_guarded = false;
static __thread jit_scalar_t *island_batch_args = NULL;

// Call the Mode A function HANDLE of the island on the Mode A state BUF
static bool island_spec_jit_call(jit_t *jit, rt_island_t *isl,
                                 jit_handle_t handle, uint8_t *buf,
                                 tlab_t *tlab, jit_scalar_t *result)
{
   jit_scalar_t p1 = { .pointer = isl->inst_ctx };
   jit_scalar_t p2 = { .pointer = buf };
   result->integer = 0;
   tlab_t null_tlab;
   tlab_t *use_tlab = tlab;
   if (unlikely(use_tlab == NULL)) {
      null_tlab = jit_null_tlab(jit);
      use_tlab = &null_tlab;
   }
   bool ok = true;
   if (island_batch_args != NULL || island_jit_guarded)
      jit_fastcall_guarded(jit, handle, result, p1, p2, use_tlab);
   else
      ok = jit_fastcall(jit, handle, result, p1, p2, use_tlab);
   if (tlab != NULL)
      tlab_reset(tlab);
   return ok;
}

// NVC_ISLAND_SPEC_FORCE=A: run the Mode A comb function on a converted
// copy of the state.  Returns false with *DONE false when the island must
// run its Mode B function instead.
static bool island_spec_call_comb(jit_t *jit, rt_island_t *isl, tlab_t *tlab,
                                  bool *done)
{
   rt_island_spec_t *sp = isl->spec;
   *done = false;
   if (!rt_island_spec_pack(isl, isl->state, sp->buf)) {
      sp->deopts[RT_ISLAND_DEOPT_STATE]++;
      return true;
   }

   jit_scalar_t result;
   if (!island_spec_jit_call(jit, isl, sp->eval_handle, sp->buf, tlab,
                             &result))
      return false;
   sp->calls_a++;
   if (result.integer != RT_ISLAND_DEOPT_NONE) {
      assert(result.integer > 0 && result.integer < RT_ISLAND_DEOPT_COUNT);
      sp->deopts[result.integer]++;
      return true;
   }

   rt_island_spec_unpack(isl, sp->buf, isl->state);
   *done = true;
   return true;
}

// NVC_ISLAND_SPEC_FORCE=A: the same for a flip-flop function, which reads
// both halves of the state and writes the second
static bool island_spec_call_ff(jit_t *jit, rt_island_domain_t *dom,
                                tlab_t *tlab, jit_scalar_t *result,
                                bool *done)
{
   rt_island_t *isl = dom->isl;
   rt_island_spec_t *sp = isl->spec;
   *done = false;

   // Only the signals the domain writes have a value in the second half:
   // the function may read them back after a partial write
   uint8_t *next_a = sp->buf + sp->stride;
   bool clean = rt_island_spec_pack(isl, isl->state, sp->buf);
   memcpy(next_a, sp->buf, sp->state_bytes);
   for (uint32_t j = 0; clean && j < dom->n_write_sigs; j++)
      clean = island_spec_pack_sig(isl, dom->write_sigs[j], isl->next_state,
                                   next_a);
   if (!clean) {
      sp->deopts[RT_ISLAND_DEOPT_STATE]++;
      return true;
   }

   if (!island_spec_jit_call(jit, isl, sp->ff_handles[dom->dom_idx], sp->buf,
                             tlab, result))
      return false;
   sp->calls_a++;
   if (!(result->integer & (INT64_C(1) << 63))) {
      assert(result->integer > 0 && result->integer < RT_ISLAND_DEOPT_COUNT);
      sp->deopts[result->integer]++;
      return true;
   }

   for (uint32_t j = 0; j < dom->n_write_sigs; j++)
      island_spec_unpack_sig(isl, dom->write_sigs[j], next_a, isl->next_state);
   *done = true;
   return true;
}

// NVC_ISLAND_SPEC_FORCE=A: the flip-flop call of island_sample_domain, in
// Mode A when it completes there, else in Mode B
static bool island_spec_sample_domain(jit_t *jit, rt_island_domain_t *dom,
                                      tlab_t *tlab)
{
   rt_island_t *isl = dom->isl;
   jit_scalar_t result;
   bool done;
   if (!island_spec_call_ff(jit, dom, tlab, &result, &done))
      return false;
   if (!done && !island_spec_jit_call(jit, isl, dom->ff_handle, isl->state,
                                      tlab, &result))
      return false;
   dom->ff_dirty_mask |= (uint64_t)result.integer;
   return true;
}

static bool island_call_comb_slow(jit_t *jit, rt_model_t *m, rt_island_t *isl,
                                  tlab_t *tlab)
{
   (void)m;
   if (unlikely(isl->spec != NULL && isl->spec->force_a)) {
      bool done;
      if (!island_spec_call_comb(jit, isl, tlab, &done))
         return false;
      if (done) {
         if (unlikely(isl->has_forces))
            island_apply_forces_comb(isl);
         return true;
      }
   }
   jit_scalar_t p1 = { .pointer = isl->inst_ctx };
   jit_scalar_t p2 = { .pointer = isl->state };
   jit_scalar_t result = { .integer = 0 };
   tlab_t null_tlab;
   tlab_t *use_tlab = tlab;
   if (unlikely(use_tlab == NULL)) {
      null_tlab = jit_null_tlab(jit);
      use_tlab = &null_tlab;
   }
   bool ok = true;
   if (likely(island_batch_args != NULL)) {
      if (likely(isl->eval_fn != NULL)) {
         island_batch_args[0] = p1;
         island_batch_args[1] = p2;
         (*isl->eval_fn)(isl->eval_func, NULL, island_batch_args, use_tlab);
         result = island_batch_args[0];
      }
      else {
         jit_fastcall_guarded(jit, isl->eval_handle, &result, p1, p2, use_tlab);
         if (likely(isl->spec == NULL || !isl->spec->force_a))
            isl->eval_func = jit_try_resolve_native(jit, isl->eval_handle,
                                                    &isl->eval_fn);
      }
   }
   else if (likely(island_jit_guarded))
      jit_fastcall_guarded(jit, isl->eval_handle, &result, p1, p2, use_tlab);
   else
      ok = jit_fastcall(jit, isl->eval_handle, &result, p1, p2, use_tlab);
   if (tlab != NULL
       && (!isl->mgr->fast
           || unlikely(tlab->alloc != 0 || tlab->mem_dirty != 0)))
      tlab_reset(tlab);
   if (!ok)
      return false;

   if (unlikely(isl->spec_a) && unlikely(result.integer != 0))
      return island_spec_comb_deopt(jit, m, isl, tlab, result.integer);

   if (unlikely(isl->has_forces))
      island_apply_forces_comb(isl);
   return true;
}

static inline bool island_call_comb_raw(jit_t *jit, rt_model_t *m,
                                        rt_island_t *isl, tlab_t *tlab)
{
   if (likely(island_batch_args != NULL && isl->eval_fn != NULL
              && tlab != NULL && !isl->has_forces)) {
      jit_scalar_t *args = island_batch_args;
      args[0].pointer = isl->inst_ctx;
      args[1].pointer = isl->state;
      (*isl->eval_fn)(isl->eval_func, NULL, args, tlab);
      if (unlikely(tlab->alloc != 0 || tlab->mem_dirty != 0))
         tlab_reset(tlab);
      if (unlikely(isl->spec_a) && unlikely(args[0].integer != 0))
         return island_spec_comb_deopt(jit, m, isl, tlab, args[0].integer);
      return true;
   }
   return island_call_comb_slow(jit, m, isl, tlab);
}

// A Mode A signal that only this island drives is converted back to one byte
// per bit only when its packed words differ from the ones that were last
// compared against the kernel value: most flushes of a combinational output
// see an unchanged value.
static inline bool island_out_memo_ok(const rt_island_t *isl,
                                      const rt_island_sig_t *s)
{
   return isl->mgr->fast
      && (s->flags & (RT_ISIG_TWO_STATE | RT_ISIG_STD_LOGIC9
                      | RT_ISIG_SOLE_OWNER))
         == (RT_ISIG_TWO_STATE | RT_ISIG_SOLE_OWNER)
      && !(s->sig->shared.flags & SIG_F_PACKED)
      && !isl->has_forces && s->ext_write_watch == NULL;
}

static inline bool island_out_memo_hit(const rt_island_t *isl,
                                       const rt_island_sig_t *s)
{
   if (!(s->flags & RT_ISIG_LAST_OUT_VALID))
      return false;
   const uint64_t *src = (const uint64_t *)(isl->state + s->a_off);
   const uint32_t nw = s->nwords * s->nelems;
   if (nw == 1)
      return s->last_out_a == src[0] && s->last_out_b == 0;
   return s->last_out_words != NULL
      && memcmp(s->last_out_words, src, nw * sizeof(uint64_t)) == 0;
}

// Kept out of line so the Verilog island paths that inline
// island_call_comb only pay for the act_on test
__attribute__((noinline))
static bool island_call_comb_act(jit_t *jit, rt_model_t *m, rt_island_t *isl,
                                 tlab_t *tlab)
{
   island_act_prepare(isl);
   const bool ok = island_call_comb_raw(jit, m, isl, tlab);
   island_act_done(isl);
   return ok;
}

static inline bool island_call_comb(jit_t *jit, rt_model_t *m, rt_island_t *isl,
                                    tlab_t *tlab)
{
   if (isl->act_on)
      return island_call_comb_act(jit, m, isl, tlab);
   return island_call_comb_raw(jit, m, isl, tlab);
}

static void island_sched_out_sig(rt_model_t *m, rt_island_t *isl,
                                 rt_island_sig_t *s);

static inline void island_flush_comb_out_sig(rt_model_t *m, rt_island_t *isl,
                                             rt_island_sig_t *s)
{
   if (unlikely(s->flags & RT_ISIG_PRUNED))
      return;

   // VHDL combinational signal assignments (<=) take effect in the next delta
   // cycle; using deposit_signal() synchronously during rt_island_eval_dirty()
   // stamps event_delta with the current delta while woken VHDL processes run
   // in the next delta, causing 'event / rising_edge() / falling_edge() to
   // read false.
   if ((s->flags & RT_ISIG_VHDL) && model_can_create_delta(m)) {
      s->flags |= RT_ISIG_VHDL_SCHED;
      if (island_out_memo_ok(isl, s) && island_out_memo_hit(isl, s))
         return;
      island_sched_out_sig(m, isl, s);
   }
   else
      island_sync_out_sig(m, isl, s);
}

static inline void island_flush_ext_outs(rt_model_t *m, rt_island_t *isl)
{
   const uint32_t n_ext = isl->n_ext_out_descs;
   if (n_ext == 0)
      return;
   const rt_island_ext_out_desc_t *ed = isl->ext_out_descs;
   const bool can_delta = model_can_create_delta(m);
   for (uint32_t j = 0; j < n_ext; j++, ed++) {
      if (likely(ed->src != NULL && can_delta)) {
         WSTAT_COUNT(&(isl->sigs[ed->sig_idx]), 2);
         if (island_bytes_diff(ed->cur, ed->src, ed->nbytes)) {
            rt_island_sig_t *os = &(isl->sigs[ed->sig_idx]);
            WSTAT_COUNT(os, 3);
            os->flags |= RT_ISIG_VHDL_SCHED;
            sched_deposit(m, os->sig, ed->src, os->sig_offset,
                          os->width * os->nelems, 0, false);
         }
      }
      else if (ed->word != NULL && can_delta) {
         // island_flush_comb_out_sig for a one-word packed output whose
         // value is the one last scheduled
         rt_island_sig_t *os = &(isl->sigs[ed->sig_idx]);
         os->flags |= RT_ISIG_VHDL_SCHED;
         if ((os->flags & RT_ISIG_LAST_OUT_VALID)
             && os->last_out_a == *ed->word && os->last_out_b == 0
             && !isl->has_forces && os->ext_write_watch == NULL)
            continue;
         island_sched_out_sig(m, isl, os);
      }
      else
         island_flush_comb_out_sig(m, isl, &(isl->sigs[ed->sig_idx]));
   }
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
      if (isl->has_dirty_inputs) {
         isl->has_dirty_inputs = false;
         island_sync_dirty_inputs(isl);
      }

      if (isl->eval_handle == JIT_HANDLE_INVALID)
         continue;

      if (!island_call_comb(jit, m, isl, tlab)) {
         ok = false;
         break;
      }

      island_after_comb(isl);
      if (isl->n_direct_comb_descs > 0)
         island_comb_direct_outs(isl);

      island_flush_ext_outs(m, isl);
      if (unlikely(isl->n_obs_sigs > 0)) {
         for (uint32_t j = 0; j < isl->n_obs_sigs; j++)
            island_flush_comb_out_sig(m, isl, &(isl->sigs[isl->obs_sigs[j]]));
      }
   }

   if (ok) {
      if (unlikely(cl->dirty_count > 0)) {
         uint32_t min_pos = UINT32_MAX;
         for (uint32_t m_idx = 0; m_idx < cl->n_islands; m_idx++) {
            if (cl->islands[m_idx]->dirty) {
               min_pos = m_idx;
               break;
            }
         }
         if (min_pos < cl->n_islands) {
            cl->min_dirty_pos = min_pos;
            cl->has_dirty = true;
            APUSH(cl->islands[0]->mgr->dirty_clusters, cl);
         }
         else {
            cl->has_dirty = false;
            cl->min_dirty_pos = UINT32_MAX;
            cl->dirty_count = 0;
         }
      }
      else {
         cl->has_dirty = false;
         cl->min_dirty_pos = UINT32_MAX;
         cl->dirty_count = 0;
      }
   }
   return ok;
}

static bool island_eval_cluster_mt(jit_t *jit, rt_super_island_t *cl,
                                   tlab_t *tlab)
{
   bool ok = true;
   const uint32_t sync_start = cl->min_dirty_pos;
   uint32_t sync_end = 0;
   uint32_t redo_pos = UINT32_MAX;

   for (uint32_t k = cl->min_dirty_pos;
        k < cl->n_islands && cl->dirty_count > 0; k++) {
      rt_island_t *isl = cl->islands[k];
      if (!isl->dirty)
         continue;
      cl->dirty_count--;
      isl->dirty = false;
      if (isl->has_dirty_inputs) {
         isl->has_dirty_inputs = false;
         island_sync_dirty_inputs(isl);
      }

      if (isl->eval_handle == JIT_HANDLE_INVALID)
         continue;

      if (!island_call_comb(jit, isl->model, isl, tlab)) {
         ok = false;
         break;
      }

      if (unlikely(isl->n_fb_sigs > 0) && island_check_feedback(isl))
         isl->mt_fb_changed = true;

      if (isl->is_vhdl) {
         // Applied on the main thread in the next delta
         if (isl->n_direct_comb_outs > 0)
            isl->mt_defer_direct = true;
         if (isl->n_ext_out_sigs > 0 || unlikely(isl->n_obs_sigs > 0)) {
            isl->need_sync_out = true;
            sync_end = k + 1;
         }
         continue;
      }

      for (uint32_t j = 0; j < isl->n_direct_comb_outs; j++) {
         const rt_island_sig_t *src_s = &(isl->sigs[isl->direct_comb_outs[j]]);
         const bool is_direct = !!(src_s->flags & RT_ISIG_DIRECT_LINK);
         if (!is_direct && (src_s->n_direct_targets == 0 || isl->has_forces))
            continue;
         for (uint32_t t = 0; t < src_s->n_direct_targets; t++) {
            rt_island_sig_t *dst_s = src_s->direct_targets[t];
            if (!is_direct && dst_s->isl->has_forces)
               continue;
            assert(dst_s->isl->cluster == cl);
            if (island_copy_direct_sig(src_s, dst_s)
                && (dst_s->flags & RT_ISIG_COMB_READ)
                && dst_s->isl->eval_handle != JIT_HANDLE_INVALID) {
               if (!dst_s->isl->dirty) {
                  dst_s->isl->dirty = true;
                  cl->dirty_count++;
                  if (dst_s->isl->topo_pos <= k)
                     redo_pos = MIN(redo_pos, dst_s->isl->topo_pos);
               }
            }
         }
      }

      if (isl->n_ext_out_sigs > 0 || unlikely(isl->n_obs_sigs > 0)) {
         isl->need_sync_out = true;
         sync_end = k + 1;
      }
   }

   if (ok) {
      if (unlikely(redo_pos != UINT32_MAX)) {
         // A direct wire back to an island earlier in the order (a cycle
         // in the island graph) changed: the main thread requeues the
         // cluster for the next pass as island_eval_cluster() does
         uint32_t n_dirty = 0;
         for (uint32_t k = redo_pos; k < cl->n_islands; k++) {
            if (cl->islands[k]->dirty)
               n_dirty++;
         }
         cl->has_dirty = n_dirty > 0;
         cl->min_dirty_pos = n_dirty > 0 ? redo_pos : UINT32_MAX;
         cl->dirty_count = n_dirty;
         cl->mt_requeue = n_dirty > 0;
      }
      else {
         cl->has_dirty = false;
         cl->min_dirty_pos = UINT32_MAX;
         cl->dirty_count = 0;
      }
      cl->mt_sync_start = sync_start;
      cl->mt_sync_end = sync_end;
   }
   return ok;
}

static bool island_eval_solo_mt(jit_t *jit, rt_island_t *isl, tlab_t *tlab)
{
   if (!isl->dirty)
      return true;
   isl->dirty = false;
   if (isl->has_dirty_inputs) {
      isl->has_dirty_inputs = false;
      island_sync_dirty_inputs(isl);
   }

   if (isl->eval_handle == JIT_HANDLE_INVALID)
      return true;

   if (!island_call_comb(jit, isl->model, isl, tlab))
      return false;

   if (unlikely(isl->n_fb_sigs > 0) && island_check_feedback(isl))
      isl->mt_fb_changed = true;

   const uint32_t n_outs = isl->mgr->hier_built
      ? isl->n_ext_out_sigs : isl->n_out_sigs;
   if (n_outs > 0 || unlikely(isl->n_obs_sigs > 0))
      isl->need_sync_out = true;
   return true;
}

static bool island_sample_domain(jit_t *jit, rt_island_domain_t *dom,
                                 tlab_t *tlab);

static bool island_eval_ff_island_mt(jit_t *jit, rt_island_t *isl,
                                     tlab_t *tlab)
{
   rt_island_domain_t *dom = isl->ff_pending_head;
   isl->ff_pending_head = NULL;
   isl->ff_pending_tail = NULL;

   bool ok = true;
   while (dom != NULL) {
      rt_island_domain_t *next = dom->ff_batch_next;
      dom->ff_batch_next = NULL;
      if (ok && !island_sample_domain(jit, dom, tlab))
         ok = false;
      dom = next;
   }
   return ok;
}

static void island_pool_work(rt_island_pool_t *pool, tlab_t *tlab)
{
   const rt_island_task_kind_t kind = pool->task_kind;
   void *const *items = pool->items;
   const uint32_t n = pool->n_items;
   const uint32_t chunk = pool->chunk;
   jit_t *jit = pool->jit;

   for (;;) {
      if (unlikely(relaxed_load(&pool->failed) != 0))
         break;

      const uint32_t start = relaxed_fetch_add(&pool->next_idx, chunk);
      if (start >= n)
         break;

      const uint32_t end = MIN(start + chunk, n);
      for (uint32_t i = start; i < end; i++) {
         bool ok = true;
         switch (kind) {
         case ISLAND_TASK_EVAL_FF:
            ok = island_eval_ff_island_mt(jit, items[i], tlab);
            break;
         case ISLAND_TASK_EVAL_DIRTY_CLUSTERS:
            ok = island_eval_cluster_mt(jit, items[i], tlab);
            break;
         case ISLAND_TASK_EVAL_DIRTY_ISLANDS:
            ok = island_eval_solo_mt(jit, items[i], tlab);
            break;
         case ISLAND_TASK_EVAL_SVA: {
            rt_sva_batch_item_t *it = items[i];
            (void)sva_tick(it->sva, it->leaves, it->disable);
            break;
         }
         case ISLAND_TASK_NONE:
            break;
         }
         if (unlikely(!ok)) {
            atomic_store(&pool->failed, 1);
            return;
         }
      }
   }
}

static void *island_worker_entry(void *arg)
{
   mspace_stack_limit(MSPACE_CURRENT_FRAME);
   jit_init_thread();

   rt_island_worker_t *w = arg;
   rt_island_pool_t *pool = w->pool;
   uint64_t my_epoch = 0;

   for (;;) {
      bool got_work = false;
      for (int spin = 0; spin < RT_ISLAND_SPIN_ITERS; spin++) {
         if (unlikely(load_acquire(&pool->stop)))
            return NULL;
         const uint64_t ep = load_acquire(&pool->epoch);
         if (ep != my_epoch) {
            my_epoch = ep;
            got_work = true;
            break;
         }
         spin_wait();
      }

      if (!got_work) {
         island_mutex_lock(&pool->park_mtx);
         atomic_fetch_add(&pool->n_parked, 1);
         while (!load_acquire(&pool->stop)
                && load_acquire(&pool->epoch) == my_epoch)
            island_cond_wait(&pool->park_cv, &pool->park_mtx);
         atomic_fetch_add(&pool->n_parked, -1);
         island_mutex_unlock(&pool->park_mtx);

         if (load_acquire(&pool->stop))
            return NULL;
         const uint64_t ep = load_acquire(&pool->epoch);
         if (ep == my_epoch)
            continue;
         my_epoch = ep;
      }

      tlab_reset(w->tlab);
      island_pool_work(pool, w->tlab);
      tlab_reset(w->tlab);

      atomic_fetch_add(&pool->done_count, 1);
   }
}

static rt_island_pool_t *island_pool_ensure(rt_island_mgr_t *mgr, jit_t *jit)
{
   const int req = opt_get_int(OPT_SIM_THREADS);
   if (req <= 1)
      return NULL;

   if (mgr->pool != NULL)
      return mgr->pool;

   const uint32_t nthreads = (uint32_t)MIN(req, MAX_THREADS);
   rt_island_pool_t *pool = xcalloc(sizeof(rt_island_pool_t));
   pool->jit = jit;
   pool->model = mgr->model;
   pool->mgr = mgr;
   pool->nthreads = nthreads;
   pool->nworkers = nthreads - 1u;
   pool->workers = xcalloc_array(pool->nworkers, sizeof(rt_island_worker_t));
   island_mutex_init(&pool->park_mtx);
   island_cond_init(&pool->park_cv);

   for (uint32_t i = 0; i < pool->nworkers; i++) {
      rt_island_worker_t *w = &(pool->workers[i]);
      w->pool = pool;
      w->idx = i + 1u;
      w->tlab = tlab_acquire(jit);
      w->thread = thread_create(island_worker_entry, w, "island %u", i + 1u);
   }

   mgr->pool = pool;
   return pool;
}

static bool island_pool_run(rt_island_pool_t *pool,
                            rt_island_task_kind_t kind,
                            void *const *items, uint32_t n_items,
                            tlab_t *main_tlab)
{
   pool->task_kind = kind;
   pool->items = items;
   pool->n_items = n_items;
   pool->chunk = MAX(1u, (n_items + pool->nthreads * 3u - 1u)
                         / (pool->nthreads * 3u));
   relaxed_store(&pool->next_idx, 0);
   relaxed_store(&pool->failed, 0);
   relaxed_store(&pool->done_count, 0);

   const uint64_t next_epoch = pool->epoch + 1u;
   atomic_store(&pool->epoch, next_epoch);

   if (atomic_load(&pool->n_parked) > 0) {
      island_mutex_lock(&pool->park_mtx);
      island_cond_broadcast(&pool->park_cv);
      island_mutex_unlock(&pool->park_mtx);
   }

   island_pool_work(pool, main_tlab);

   while (load_acquire(&pool->done_count) < pool->nworkers)
      spin_wait();

   pool->task_kind = ISLAND_TASK_NONE;
   pool->items = NULL;
   return atomic_load(&pool->failed) == 0;
}

uint32_t rt_island_eval_sva_batch(jit_t *jit, rt_model_t *m,
                                  rt_island_mgr_t **mgr_p,
                                  rt_sva_batch_item_t *items, uint32_t count,
                                  tlab_t *tlab)
{
   if (count == 0)
      return 0;

   const int nthreads = opt_get_int(OPT_SIM_THREADS);
   if (nthreads <= 1) {
      for (uint32_t i = 0; i < count; i++)
         (void)sva_tick(items[i].sva, items[i].leaves, items[i].disable);
      return 0;
   }

   rt_island_mgr_t *mgr = *mgr_p;
   if (mgr == NULL) {
      mgr = rt_island_mgr_new(m);
      *mgr_p = mgr;
   }

   if (count < RT_SVA_MT_THRESHOLD) {
      for (uint32_t i = 0; i < count; i++)
         (void)sva_tick(items[i].sva, items[i].leaves, items[i].disable);
      if (!mgr->mt_gate_sva.use_mt) {
         mgr->mt_gate_sva.probes_done++;
         if (mgr->mt_gate_sva.probes_done >= 8)
            return 256u;
      }
      return 0;
   }

   bool do_probe = false;
   const bool try_mt = island_mt_gate_should_run(&mgr->mt_gate_sva, &do_probe);
   if (!try_mt) {
      const uint64_t t0 = do_probe ? get_timestamp_ns() : 0;
      for (uint32_t i = 0; i < count; i++)
         (void)sva_tick(items[i].sva, items[i].leaves, items[i].disable);
      if (do_probe) {
         const uint64_t dt = get_timestamp_ns() - t0;
         mgr->mt_gate_sva.use_mt = (dt >= RT_SVA_MT_MIN_BATCH_NS);
         mgr->mt_gate_sva.probes_done++;
         if (mgr->mt_gate_sva.probes_done == 1)
            mgr->mt_gate_sva.next_probe = 64u;
         else if (mgr->mt_gate_sva.probes_done == 2)
            mgr->mt_gate_sva.next_probe = 128u;
         else if (mgr->mt_gate_sva.probes_done == 3)
            mgr->mt_gate_sva.next_probe = 256u;
         else
            mgr->mt_gate_sva.next_probe = 2048u;
      }
      if (!mgr->mt_gate_sva.use_mt && mgr->mt_gate_sva.next_probe > 0) {
         const uint32_t skip = mgr->mt_gate_sva.next_probe;
         mgr->mt_gate_sva.next_probe = 0;
         return skip;
      }
      return 0;
   }

   mgr->mt_sva_ptrs.count = 0;
   for (uint32_t i = 0; i < count; i++)
      APUSH(mgr->mt_sva_ptrs, &(items[i]));

   rt_island_pool_t *pool = island_pool_ensure(mgr, jit);
   (void)island_pool_run(pool, ISLAND_TASK_EVAL_SVA,
                         (void *const *)mgr->mt_sva_ptrs.items,
                         count, tlab);
   return 0;
}

static void island_sync_out_island(rt_model_t *m, rt_island_t *isl)
{
   if (isl->mgr->hier_built)
      island_flush_ext_outs(m, isl);
   else {
      for (uint32_t j = 0; j < isl->n_out_sigs; j++)
         island_flush_comb_out_sig(m, isl, &(isl->sigs[isl->out_sigs[j]]));
   }
   if (unlikely(isl->n_obs_sigs > 0)) {
      for (uint32_t j = 0; j < isl->n_obs_sigs; j++)
         island_flush_comb_out_sig(m, isl, &(isl->sigs[isl->obs_sigs[j]]));
   }
}

static inline bool island_has_pending_vhdl_ff(const rt_island_t *isl)
{
   return isl->pending_vhdl_ff != 0;
}

static inline bool cluster_has_pending_vhdl_ff(const rt_super_island_t *cl)
{
   return cl->pending_vhdl_ff != 0;
}

static bool island_eval_dirty_clusters(jit_t *jit, rt_model_t *m,
                                       rt_island_mgr_t *mgr, tlab_t *tlab,
                                       bool sv_only)
{
   if (mgr->dirty_clusters.count == 0)
      return true;

   const bool skip_vhdl = sv_only && mgr->has_vhdl;
   const int nthreads = opt_get_int(OPT_SIM_THREADS);
   unsigned n = mgr->dirty_clusters.count;
   bool do_probe = false;
   const bool try_mt = (nthreads > 1 && n >= RT_ISLAND_MT_DIRTY_THRESHOLD)
      && island_mt_gate_should_run(&mgr->mt_gate_cl, &do_probe);
   if (!try_mt) {
      const uint64_t t0 = do_probe ? get_timestamp_ns() : 0;
      bool ok = true;
      for (unsigned i = 0; i < n; i++) {
         rt_super_island_t *cl = mgr->dirty_clusters.items[i];
         if (!cl->has_dirty)
            continue;
         if (unlikely(cluster_has_pending_vhdl_ff(cl)
                      || (skip_vhdl && cl->is_vhdl))) {
            APUSH(mgr->dirty_clusters, cl);
            continue;
         }
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

      if (do_probe && ok)
         island_mt_gate_record(&mgr->mt_gate_cl, get_timestamp_ns() - t0);

      return ok;
   }

   island_build_mt_deps(mgr);

   mgr->mt_cl_work_q.count = 0;
   for (unsigned i = 0; i < n; i++) {
      rt_super_island_t *cl = mgr->dirty_clusters.items[i];
      if (!cl->has_dirty)
         continue;
      if (unlikely(cluster_has_pending_vhdl_ff(cl)
                   || (skip_vhdl && cl->is_vhdl)))
         APUSH(mgr->dirty_clusters, cl);
      else
         APUSH(mgr->mt_cl_work_q, cl);
   }
   if (mgr->dirty_clusters.count > n) {
      memmove(mgr->dirty_clusters.items, mgr->dirty_clusters.items + n,
              (mgr->dirty_clusters.count - n) * sizeof(rt_super_island_t *));
      mgr->dirty_clusters.count -= n;
   }
   else
      mgr->dirty_clusters.count = 0;

   bool ok = true;
   while (ok && mgr->mt_cl_work_q.count > 0) {
      mgr->mt_cl_wave_q.count = 0;
      mgr->mt_cl_rem_q.count = 0;

      if (!mgr->mt_has_comb_deps) {
         for (unsigned i = 0; i < mgr->mt_cl_work_q.count; i++) {
            rt_super_island_t *cl = mgr->mt_cl_work_q.items[i];
            if (cl->has_dirty)
               APUSH(mgr->mt_cl_wave_q, cl);
         }
      }
      else {
         const uint32_t tag = ++mgr->mt_wave_tag;
         for (unsigned i = 0; i < mgr->mt_cl_work_q.count; i++) {
            rt_super_island_t *cl = mgr->mt_cl_work_q.items[i];
            if (!cl->has_dirty)
               continue;
            bool dep_in_wave = false;
            for (uint32_t d = 0; d < cl->n_comb_deps; d++) {
               if (cl->comb_deps[d]->mt_wave_tag == tag) {
                  dep_in_wave = true;
                  break;
               }
            }
            if (dep_in_wave)
               APUSH(mgr->mt_cl_rem_q, cl);
            else {
               cl->mt_wave_tag = tag;
               APUSH(mgr->mt_cl_wave_q, cl);
            }
         }
      }

      if (mgr->mt_cl_wave_q.count == 0)
         break;

      if (mgr->mt_cl_wave_q.count < RT_ISLAND_MT_DIRTY_THRESHOLD) {
         for (unsigned i = 0; i < mgr->mt_cl_wave_q.count; i++) {
            rt_super_island_t *cl = mgr->mt_cl_wave_q.items[i];
            if (cl->has_dirty && !island_eval_cluster(jit, m, cl, tlab)) {
               ok = false;
               break;
            }
         }
      }
      else {
         mgr->mt_cl_safe_q.count = 0;
         for (unsigned i = 0; i < mgr->mt_cl_wave_q.count; i++) {
            rt_super_island_t *cl = mgr->mt_cl_wave_q.items[i];
            bool cl_safe = true;
            for (uint32_t k = cl->min_dirty_pos; k < cl->n_islands; k++) {
               rt_island_t *isl = cl->islands[k];
               if (isl->eval_handle != JIT_HANDLE_INVALID
                   && !jit_prepare_call(jit, isl->eval_handle,
                                        isl->dirty ? 1u : 0u))
                  cl_safe = false;
            }
            cl->mt_eval_on_main = !cl_safe;
            if (cl_safe)
               APUSH(mgr->mt_cl_safe_q, cl);
         }

         if (mgr->mt_cl_safe_q.count < RT_ISLAND_MT_DIRTY_THRESHOLD) {
            for (unsigned i = 0; i < mgr->mt_cl_wave_q.count; i++) {
               rt_super_island_t *cl = mgr->mt_cl_wave_q.items[i];
               if (cl->has_dirty && !island_eval_cluster(jit, m, cl, tlab)) {
                  ok = false;
                  break;
               }
            }
         }
         else {
            rt_island_pool_t *pool = island_pool_ensure(mgr, jit);
            if (!island_pool_run(pool, ISLAND_TASK_EVAL_DIRTY_CLUSTERS,
                                 (void *const *)mgr->mt_cl_safe_q.items,
                                 mgr->mt_cl_safe_q.count, tlab)) {
               ok = false;
            }
            else {
               for (unsigned i = 0; i < mgr->mt_cl_wave_q.count; i++) {
                  rt_super_island_t *cl = mgr->mt_cl_wave_q.items[i];
                  if (cl->mt_eval_on_main) {
                     if (cl->has_dirty
                         && !island_eval_cluster(jit, m, cl, tlab)) {
                        ok = false;
                        break;
                     }
                     continue;
                  }
                  if (cl->is_vhdl) {
                     for (uint32_t k = 0; k < cl->n_islands; k++) {
                        rt_island_t *isl = cl->islands[k];
                        if (isl->mt_fb_changed) {
                           isl->mt_fb_changed = false;
                           island_defer_dirty(isl);
                        }
                        if (isl->mt_defer_direct) {
                           isl->mt_defer_direct = false;
                           island_comb_direct_outs(isl);
                        }
                     }
                  }
                  for (uint32_t k = cl->mt_sync_start; k < cl->mt_sync_end; k++) {
                     rt_island_t *isl = cl->islands[k];
                     if (!isl->need_sync_out)
                        continue;
                     isl->need_sync_out = false;
                     for (uint32_t j = 0; j < isl->n_ext_out_sigs; j++)
                        island_flush_comb_out_sig(m, isl,
                                                  &(isl->sigs[isl->ext_out_sigs[j]]));
                     if (unlikely(isl->n_obs_sigs > 0)) {
                        for (uint32_t j = 0; j < isl->n_obs_sigs; j++)
                           island_flush_comb_out_sig(m, isl,
                                                     &(isl->sigs[isl->obs_sigs[j]]));
                     }
                  }
                  cl->mt_sync_start = 0;
                  cl->mt_sync_end = 0;
                  if (unlikely(cl->mt_requeue)) {
                     cl->mt_requeue = false;
                     APUSH(mgr->dirty_clusters, cl);
                  }
               }
            }
         }
      }

      mgr->mt_cl_work_q.count = 0;
      for (unsigned i = 0; i < mgr->mt_cl_rem_q.count; i++)
         APUSH(mgr->mt_cl_work_q, mgr->mt_cl_rem_q.items[i]);
   }

   if (!ok) {
      for (unsigned i = 0; i < mgr->mt_cl_work_q.count; i++) {
         rt_super_island_t *cl = mgr->mt_cl_work_q.items[i];
         if (cl->has_dirty)
            APUSH(mgr->dirty_clusters, cl);
      }
   }

   return ok;
}

typedef struct {
   jit_t           *jit;
   rt_model_t      *m;
   rt_island_mgr_t *mgr;
   tlab_t          *tlab;
   bool             sv_only;
   bool             ok;
} island_eval_guard_ctx_t;

static bool rt_island_eval_dirty_inner(jit_t *jit, rt_model_t *m,
                                       rt_island_mgr_t *mgr, tlab_t *tlab,
                                       bool sv_only);

static void island_eval_dirty_tramp(void *arg)
{
   island_eval_guard_ctx_t *g = arg;
   island_jit_guarded = true;
   if (g->mgr->fast) {
      jit_batch_ctx_t batch;
      jit_batch_begin(g->jit, &batch);
      island_batch_args = batch.args;
      g->ok = rt_island_eval_dirty_inner(g->jit, g->m, g->mgr, g->tlab,
                                         g->sv_only);
      island_batch_args = NULL;
      jit_batch_end(&batch);
   }
   else
      g->ok = rt_island_eval_dirty_inner(g->jit, g->m, g->mgr, g->tlab,
                                         g->sv_only);
}

static bool rt_island_eval_dirty_mode(jit_t *jit, rt_model_t *m,
                                      rt_island_mgr_t *mgr, tlab_t *tlab,
                                      bool sv_only)
{
   if (mgr == NULL || (mgr->dirty_q.count == 0 && mgr->dirty_clusters.count == 0))
      return true;
   if (island_jit_guarded)
      return rt_island_eval_dirty_inner(jit, m, mgr, tlab, sv_only);
   island_eval_guard_ctx_t g = {
      .jit = jit, .m = m, .mgr = mgr, .tlab = tlab, .sv_only = sv_only,
      .ok = false,
   };
   const bool ran = jit_run_guarded(jit, island_eval_dirty_tramp, &g);
   island_jit_guarded = false;
   island_batch_args = NULL;
   return ran && g.ok;
}

static void island_publish_late(rt_island_mgr_t *mgr)
{
   // Signals written by always_comb blocks change after the processes of
   // the first delta cycle have run, as without islands
   mgr->late_pending = false;
   for (unsigned i = 0; i < mgr->islands.count; i++) {
      rt_island_t *isl = mgr->islands.items[i];
      if (isl->has_reset_late && island_eligible_sv_reset(isl))
         island_mark_dirty(isl);
   }
}

bool rt_island_eval_dirty(jit_t *jit, rt_model_t *m, rt_island_mgr_t *mgr,
                          tlab_t *tlab)
{
   if (unlikely(mgr->late_pending))
      island_publish_late(mgr);
   return rt_island_eval_dirty_mode(jit, m, mgr, tlab, false);
}

bool rt_island_eval_sv_dirty(jit_t *jit, rt_model_t *m, rt_island_mgr_t *mgr,
                             tlab_t *tlab)
{
   return rt_island_eval_dirty_mode(jit, m, mgr, tlab, true);
}

static bool rt_island_eval_dirty_inner(jit_t *jit, rt_model_t *m,
                                       rt_island_mgr_t *mgr, tlab_t *tlab,
                                       bool sv_only)
{
   if (mgr == NULL || (mgr->dirty_q.count == 0 && mgr->dirty_clusters.count == 0))
      return true;

   const bool skip_vhdl = sv_only && mgr->has_vhdl;
   bool ok = true;
   if (mgr->dirty_q.count > 0) {
      const int nthreads = opt_get_int(OPT_SIM_THREADS);
      unsigned n = mgr->dirty_q.count;
      bool do_probe = false;
      const bool try_mt = (nthreads > 1 && n >= RT_ISLAND_MT_DIRTY_THRESHOLD)
         && island_mt_gate_should_run(&mgr->mt_gate_isl, &do_probe);

      if (!try_mt) {
         const uint64_t t0 = do_probe ? get_timestamp_ns() : 0;
         for (unsigned i = 0; i < n; i++) {
            rt_island_t *isl = mgr->dirty_q.items[i];
            if (!isl->dirty) {
               isl->in_dirty_list = false;
               continue;
            }
            if (unlikely(island_has_pending_vhdl_ff(isl)
                         || (skip_vhdl && isl->is_vhdl))) {
               APUSH(mgr->dirty_q, isl);
               continue;
            }
            isl->in_dirty_list = false;
            isl->dirty = false;
            if (isl->has_dirty_inputs) {
               isl->has_dirty_inputs = false;
               island_sync_dirty_inputs(isl);
            }

            if (isl->eval_handle == JIT_HANDLE_INVALID)
               continue;

            if (!island_call_comb(jit, m, isl, tlab)) {
               ok = false;
               break;
            }

            island_after_comb(isl);
            if (isl->n_direct_comb_descs > 0)
               island_comb_direct_outs(isl);
            if (mgr->hier_built)
               island_flush_ext_outs(m, isl);
            else {
               for (uint32_t j = 0; j < isl->n_out_sigs; j++)
                  island_flush_comb_out_sig(m, isl, &(isl->sigs[isl->out_sigs[j]]));
            }
            if (unlikely(isl->n_obs_sigs > 0)) {
               for (uint32_t j = 0; j < isl->n_obs_sigs; j++)
                  island_flush_comb_out_sig(m, isl, &(isl->sigs[isl->obs_sigs[j]]));
            }
         }

         if (mgr->dirty_q.count > n) {
            memmove(mgr->dirty_q.items, mgr->dirty_q.items + n,
                    (mgr->dirty_q.count - n) * sizeof(rt_island_t *));
            mgr->dirty_q.count -= n;
         }
         else
            mgr->dirty_q.count = 0;

         if (do_probe && ok)
            island_mt_gate_record(&mgr->mt_gate_isl, get_timestamp_ns() - t0);
      }
      else {
         island_build_mt_deps(mgr);

         mgr->mt_isl_work_q.count = 0;
         for (unsigned i = 0; i < n; i++) {
            rt_island_t *isl = mgr->dirty_q.items[i];
            if (!isl->dirty) {
               isl->in_dirty_list = false;
               continue;
            }
            if (unlikely(island_has_pending_vhdl_ff(isl)
                         || (skip_vhdl && isl->is_vhdl))) {
               APUSH(mgr->dirty_q, isl);
               continue;
            }
            isl->in_dirty_list = false;
            APUSH(mgr->mt_isl_work_q, isl);
         }
         if (mgr->dirty_q.count > n) {
            memmove(mgr->dirty_q.items, mgr->dirty_q.items + n,
                    (mgr->dirty_q.count - n) * sizeof(rt_island_t *));
            mgr->dirty_q.count -= n;
         }
         else
            mgr->dirty_q.count = 0;

         while (ok && mgr->mt_isl_work_q.count > 0) {
            mgr->mt_isl_wave_q.count = 0;
            mgr->mt_isl_rem_q.count = 0;

            if (!mgr->mt_has_comb_deps) {
               for (unsigned i = 0; i < mgr->mt_isl_work_q.count; i++) {
                  rt_island_t *isl = mgr->mt_isl_work_q.items[i];
                  if (isl->dirty)
                     APUSH(mgr->mt_isl_wave_q, isl);
               }
            }
            else {
               const uint32_t tag = ++mgr->mt_wave_tag;
               for (unsigned i = 0; i < mgr->mt_isl_work_q.count; i++) {
                  rt_island_t *isl = mgr->mt_isl_work_q.items[i];
                  if (!isl->dirty)
                     continue;
                  bool dep_in_wave = false;
                  for (uint32_t d = 0; d < isl->n_comb_deps; d++) {
                     if (isl->comb_deps[d]->mt_wave_tag == tag) {
                        dep_in_wave = true;
                        break;
                     }
                  }
                  if (dep_in_wave)
                     APUSH(mgr->mt_isl_rem_q, isl);
                  else {
                     isl->mt_wave_tag = tag;
                     APUSH(mgr->mt_isl_wave_q, isl);
                  }
               }
            }

            if (mgr->mt_isl_wave_q.count == 0)
               break;

            if (mgr->mt_isl_wave_q.count < RT_ISLAND_MT_DIRTY_THRESHOLD) {
               for (unsigned i = 0; i < mgr->mt_isl_wave_q.count; i++) {
                  rt_island_t *isl = mgr->mt_isl_wave_q.items[i];
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
                  island_after_comb(isl);
                  island_sync_out_island(m, isl);
               }
            }
            else {
               mgr->mt_isl_safe_q.count = 0;
               for (unsigned i = 0; i < mgr->mt_isl_wave_q.count; i++) {
                  rt_island_t *isl = mgr->mt_isl_wave_q.items[i];
                  assert(isl->cluster == NULL && isl->n_direct_comb_outs == 0);
                  const bool safe = (isl->eval_handle == JIT_HANDLE_INVALID)
                     || jit_prepare_call(jit, isl->eval_handle, 1u);
                  isl->mt_eval_on_main = !safe;
                  if (safe)
                     APUSH(mgr->mt_isl_safe_q, isl);
               }

               if (mgr->mt_isl_safe_q.count < RT_ISLAND_MT_DIRTY_THRESHOLD) {
                  for (unsigned i = 0; i < mgr->mt_isl_wave_q.count; i++) {
                     rt_island_t *isl = mgr->mt_isl_wave_q.items[i];
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
                     island_after_comb(isl);
                     island_sync_out_island(m, isl);
                  }
               }
               else {
                  rt_island_pool_t *pool = island_pool_ensure(mgr, jit);
                  if (!island_pool_run(pool, ISLAND_TASK_EVAL_DIRTY_ISLANDS,
                                       (void *const *)mgr->mt_isl_safe_q.items,
                                       mgr->mt_isl_safe_q.count, tlab)) {
                     ok = false;
                  }
                  else {
                     for (unsigned i = 0; i < mgr->mt_isl_wave_q.count; i++) {
                        rt_island_t *isl = mgr->mt_isl_wave_q.items[i];
                        if (isl->mt_eval_on_main) {
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
                           island_after_comb(isl);
                           island_sync_out_island(m, isl);
                           continue;
                        }
                        if (unlikely(isl->mt_fb_changed)) {
                           isl->mt_fb_changed = false;
                           island_defer_dirty(isl);
                        }
                        if (!isl->need_sync_out)
                           continue;
                        isl->need_sync_out = false;
                        island_sync_out_island(m, isl);
                     }
                  }
               }
            }

            mgr->mt_isl_work_q.count = 0;
            for (unsigned i = 0; i < mgr->mt_isl_rem_q.count; i++)
               APUSH(mgr->mt_isl_work_q, mgr->mt_isl_rem_q.items[i]);
         }
      }
   }

   if (ok && mgr->dirty_clusters.count > 0)
      ok = island_eval_dirty_clusters(jit, m, mgr, tlab, sv_only);

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
   const bool is_vhdl = !!(dom->edge_flags
                           & (RT_IEDGE_VHDL | RT_IEDGE_VHDL_EVENT
                              | RT_IEDGE_RST_LEVEL));
   if (!is_vhdl && !(isl->mgr->fast && isl->mgr->reset_done && !isl->has_forces)) {
      // Non-island SV processes (e.g. ejected always @*) use blocking deposits
      // whose watch callbacks are deferred to the next delta; sync live inputs
      // from rt_signal_t before sampling SV flip-flops.
      for (uint32_t j = 0; j < isl->n_in_sigs; j++) {
         const rt_island_sig_t *s = &(isl->sigs[isl->in_sigs[j]]);
         if (!(s->flags & RT_ISIG_DIRECT_LINK) && s->sig != NULL)
            island_sync_in_sig(isl, s);
      }
      island_clear_in_dirty(isl);
      isl->has_dirty_inputs = false;
   }
   else if (isl->has_dirty_inputs) {
      island_sync_dirty_inputs(isl);
      isl->has_dirty_inputs = false;
   }

   if (unlikely(dom->needs_presync)) {
      for (uint32_t j = 0; j < dom->n_write_sigs; j++) {
         const rt_island_sig_t *s = &(isl->sigs[dom->write_sigs[j]]);
         if (!(s->flags & RT_ISIG_SPARSE_RAM))
            island_copy_sig_buf(isl->next_state, isl->state, s);
      }
   }
   if (unlikely(isl->has_forces)) {
      for (uint32_t j = 0; j < dom->n_write_sigs; j++) {
         const rt_island_sig_t *s = &(isl->sigs[dom->write_sigs[j]]);
         if (!(s->flags & RT_ISIG_VHDL) || (s->flags & RT_ISIG_SPARSE_RAM))
            continue;
         const rt_island_force_t *f = island_find_force(isl, s->sig_idx);
         if (f != NULL)
            island_restore_saved_force(isl->next_state, s, f);
      }
   }

   jit_scalar_t p1 = { .pointer = isl->inst_ctx };
   jit_scalar_t p2 = { .pointer = isl->state };
   jit_scalar_t result = { .integer = 0 };
   tlab_t null_tlab;
   tlab_t *use_tlab = tlab;
   if (unlikely(use_tlab == NULL)) {
      null_tlab = jit_null_tlab(jit);
      use_tlab = &null_tlab;
   }
   bool ok = true;
   if (likely(island_batch_args != NULL && dom->ff_fn != NULL)) {
      island_batch_args[0] = p1;
      island_batch_args[1] = p2;
      (*dom->ff_fn)(dom->ff_func, NULL, island_batch_args, use_tlab);
      result = island_batch_args[0];
   }
   else if (unlikely(isl->spec != NULL && isl->spec->force_a))
      return island_spec_sample_domain(jit, dom, tlab);
   else if (likely(island_batch_args != NULL)) {
      jit_fastcall_guarded(jit, dom->ff_handle, &result, p1, p2, use_tlab);
      dom->ff_func = jit_try_resolve_native(jit, dom->ff_handle, &dom->ff_fn);
   }
   else if (likely(island_jit_guarded))
      jit_fastcall_guarded(jit, dom->ff_handle, &result, p1, p2, use_tlab);
   else
      ok = jit_fastcall(jit, dom->ff_handle, &result, p1, p2, use_tlab);
   if (tlab != NULL
       && (!isl->mgr->fast
           || unlikely(tlab->alloc != 0 || tlab->mem_dirty != 0)))
      tlab_reset(tlab);
   if (unlikely(isl->spec_a) && ok
       && unlikely(!(result.integer & (INT64_C(1) << 63))))
      return island_spec_ff_deopt(jit, dom, tlab, result.integer);
   if (ok)
      dom->ff_dirty_mask |= (uint64_t)result.integer;
   return ok;
}

static bool island_eval_ff_pre_sync(jit_t *jit, rt_model_t *m,
                                    rt_island_mgr_t *mgr, tlab_t *tlab,
                                    unsigned n_ff, unsigned n_sd)
{
   for (unsigned i = 0; i < n_ff; i++) {
      rt_island_domain_t *dom = mgr->ff_sample_q.items[i];
      rt_island_t *isl = dom->isl;
      const bool is_vhdl = !!(dom->edge_flags
                              & (RT_IEDGE_VHDL | RT_IEDGE_VHDL_EVENT));
      if (!is_vhdl && isl->cluster != NULL && isl->cluster->has_dirty) {
         if (!island_eval_cluster(jit, m, isl->cluster, tlab))
            return false;
      }
      if (isl->has_dirty_inputs) {
         island_sync_dirty_inputs(isl);
         isl->has_dirty_inputs = false;
      }
   }

   for (unsigned i = 0; i < n_sd; i++) {
      rt_super_domain_t *sd = mgr->super_sample_q.items[i];
      const bool is_vhdl = !!(sd->edge_flags
                              & (RT_IEDGE_VHDL | RT_IEDGE_VHDL_EVENT));
      for (uint32_t c = 0; c < sd->n_clusters; c++) {
         rt_super_island_t *cl = sd->clusters[c];
         if (!is_vhdl && cl->has_dirty && !island_eval_cluster(jit, m, cl, tlab))
            return false;
         if (cl->has_dirty_inputs) {
            for (uint32_t k = 0; k < cl->n_dirty_in; k++) {
               rt_island_t *isl = cl->dirty_in_list[k];
               isl->in_cl_dirty_in = false;
               if (isl->has_dirty_inputs) {
                  island_sync_dirty_inputs(isl);
                  isl->has_dirty_inputs = false;
               }
            }
            cl->n_dirty_in = 0;
            cl->has_dirty_inputs = false;
         }
      }

      for (uint32_t s = 0; s < sd->n_solo_islands; s++) {
         rt_island_t *isl = sd->solo_islands[s];
         if (isl->has_dirty_inputs) {
            island_sync_dirty_inputs(isl);
            isl->has_dirty_inputs = false;
         }
         if (!is_vhdl && isl->dirty && isl->eval_handle != JIT_HANDLE_INVALID) {
            isl->dirty = false;
            if (!island_call_comb(jit, m, isl, tlab))
               return false;
            if (isl->n_direct_comb_descs > 0)   // RT_ISIG_DIRECT_PUB only
               island_propagate_direct_comb(isl);
            if (mgr->hier_built) {
               for (uint32_t j = 0; j < isl->n_ext_out_sigs; j++) {
                  rt_island_sig_t *os = &(isl->sigs[isl->ext_out_sigs[j]]);
                  if (likely(!(os->flags & RT_ISIG_PRUNED)))
                     island_sync_out_sig(m, isl, os);
               }
            }
            else {
               for (uint32_t j = 0; j < isl->n_out_sigs; j++) {
                  rt_island_sig_t *os = &(isl->sigs[isl->out_sigs[j]]);
                  if (likely(!(os->flags & RT_ISIG_PRUNED)))
                     island_sync_out_sig(m, isl, os);
               }
            }
            if (unlikely(isl->n_obs_sigs > 0)) {
               for (uint32_t j = 0; j < isl->n_obs_sigs; j++) {
                  rt_island_sig_t *os = &(isl->sigs[isl->obs_sigs[j]]);
                  if (likely(!(os->flags & RT_ISIG_PRUNED)))
                     island_sync_out_sig(m, isl, os);
               }
            }
         }
      }
   }

   return true;
}

static inline void island_ff_enqueue_dom(rt_island_mgr_t *mgr,
                                         rt_island_domain_t *dom,
                                         bool safe)
{
   rt_island_t *isl = dom->isl;
   dom->ff_batch_next = NULL;
   if (isl->ff_pending_head == NULL) {
      isl->ff_pending_head = dom;
      isl->ff_pending_tail = dom;
      isl->mt_ff_unsafe = !safe;
      APUSH(mgr->mt_ff_islands, isl);
   }
   else {
      isl->ff_pending_tail->ff_batch_next = dom;
      isl->ff_pending_tail = dom;
      if (!safe)
         isl->mt_ff_unsafe = true;
   }
}

static inline bool island_edge_is_vhdl(uint8_t edge_flags)
{
   return !!(edge_flags & (RT_IEDGE_VHDL | RT_IEDGE_VHDL_EVENT
                           | RT_IEDGE_RST_LEVEL));
}

static void island_sched_out_sig(rt_model_t *m, rt_island_t *isl,
                                 rt_island_sig_t *s)
{
   WSTAT_COUNT(s, 2);

   const bool memo = island_out_memo_ok(isl, s);
   const uint64_t *memo_src = memo
      ? (const uint64_t *)(isl->state + s->a_off) : NULL;
   const uint32_t memo_words = s->nwords * s->nelems;
   if (memo) {
      if (island_out_memo_hit(isl, s))
         return;
   }
   else
      s->flags &= ~RT_ISIG_LAST_OUT_VALID;

   rt_signal_t *sig = s->sig;
   const uint32_t width = s->width;
   const uint32_t nelems = s->nelems;

   if (s->flags & RT_ISIG_STD_LOGIC9) {
      assert(!(sig->shared.flags & SIG_F_PACKED));
      const uint32_t total = width * nelems;
      const size_t elem_sz = s->elem_size;
      const size_t nbytes = (size_t)total * elem_sz;
      const uint8_t *src = isl->state + s->a_off;
      const uint8_t *cur = sig->shared.data + (size_t)s->sig_offset * elem_sz;
      if (island_bytes_diff(cur, src, nbytes)) {
         WSTAT_COUNT(s, 3);
         sched_deposit(m, sig, src, s->sig_offset, total, 0, false);
      }
      return;
   }

   if (sig->shared.flags & SIG_F_PACKED) {
      island_sync_out_sig(m, isl, s);
      return;
   }

   const uint32_t nwords = s->nwords;
   const bool two_state = !!(s->flags & RT_ISIG_TWO_STATE);
   const uint64_t *src_a = (const uint64_t *)(isl->state + s->a_off);
   const uint64_t *src_b = two_state
      ? NULL : (const uint64_t *)(isl->state + s->b_off);
   const bool is_std = island_sig_is_std(sig, s->flags);
   const uint32_t total = width * nelems;

   uint8_t stack_buf[256];
   uint8_t *heap_buf = NULL;
   uint8_t *out_bytes = stack_buf;
   if (total > sizeof(stack_buf)) {
      heap_buf = xmalloc(total);
      out_bytes = heap_buf;
   }

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
   if (two_state) {
      const uint64_t base = is_std ? ISL_ONES8 * 2u : 0u;
      for (uint32_t e = 0; e < nelems; e++)
         island_unpack_elem_2s(out_bytes + e * width, src_a + e * nwords,
                               width, base);
   }
   else
#endif
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
   if (memcmp(cur, out_bytes, total) != 0) {
      WSTAT_COUNT(s, 3);
      sched_deposit(m, sig, out_bytes, s->sig_offset, total, 0, false);
   }

   free(heap_buf);

   if (memo) {
      if (memo_words == 1) {
         s->last_out_a = memo_src[0];
         s->last_out_b = 0;
      }
      else {
         if (s->last_out_words == NULL)
            s->last_out_words = xmalloc(memo_words * sizeof(uint64_t));
         memcpy(s->last_out_words, memo_src, memo_words * sizeof(uint64_t));
      }
      s->flags |= RT_ISIG_LAST_OUT_VALID;
   }
}

static void island_sched_out_slice(rt_model_t *m, rt_island_t *isl,
                                   rt_island_sig_t *s,
                                   uint32_t local_off, uint32_t local_cnt)
{
   if (local_cnt == 0)
      return;
   if (local_off == 0 && local_cnt >= s->width * s->nelems) {
      island_sched_out_sig(m, isl, s);
      return;
   }
   s->flags &= ~RT_ISIG_LAST_OUT_VALID;

   rt_signal_t *sig = s->sig;
   if (s->flags & RT_ISIG_STD_LOGIC9) {
      assert(!(sig->shared.flags & SIG_F_PACKED));
      const uint32_t sig_off = (uint32_t)s->sig_offset + local_off;
      const size_t elem_sz = s->elem_size;
      const uint8_t *src = isl->state + s->a_off + (size_t)local_off * elem_sz;
      if (memcmp(sig->shared.data + (size_t)sig_off * elem_sz, src,
                 (size_t)local_cnt * elem_sz) != 0)
         sched_deposit(m, sig, src, (int)sig_off, (int)local_cnt, 0, false);
      return;
   }

   if (sig->shared.flags & SIG_F_PACKED) {
      island_sync_out_slice(m, isl, s, local_off, local_cnt);
      return;
   }

   const uint32_t width = s->width;
   const uint32_t nwords = s->nwords;
   const bool two_state = !!(s->flags & RT_ISIG_TWO_STATE);
   const uint64_t *src_a = (const uint64_t *)(isl->state + s->a_off);
   const uint64_t *src_b = two_state
      ? NULL : (const uint64_t *)(isl->state + s->b_off);
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
      sched_deposit(m, sig, out_bytes, (int)sig_off, (int)local_cnt, 0, false);

   free(heap_buf);
}

static inline void island_flush_out_slice(rt_model_t *m, rt_island_t *isl,
                                          rt_island_sig_t *s,
                                          uint32_t local_off,
                                          uint32_t local_cnt)
{
   if ((s->flags & RT_ISIG_VHDL) && model_can_create_delta(m)) {
      s->flags |= RT_ISIG_VHDL_SCHED;
      island_sched_out_slice(m, isl, s, local_off, local_cnt);
   }
   else
      island_sync_out_slice(m, isl, s, local_off, local_cnt);
}

__attribute__((noinline))
static void island_notify_vhdl_ff_desc(rt_model_t *m, rt_island_t *isl,
                                       const rt_island_ff_desc_t *d)
{
   rt_island_sig_t *s = &(isl->sigs[d->sig_idx]);
   if (d->flags & RT_IFF_DIRECT_LINK) {
      for (uint32_t t = 0; t < s->n_direct_targets; t++) {
         rt_island_sig_t *dst_s = s->direct_targets[t];
         if (island_copy_direct_sig(s, dst_s)
             && (dst_s->flags & RT_ISIG_COMB_READ)
             && dst_s->isl->eval_handle != JIT_HANDLE_INVALID)
            island_mark_dirty(dst_s->isl);
      }
   }
   else {
      s->flags |= RT_ISIG_VHDL_SCHED;
      island_sched_out_sig(m, isl, s);
   }
}

static void island_commit_sparse_entries(rt_model_t *m, rt_island_t *isl,
                                         const rt_island_sparse_entry_t *entries,
                                         uint32_t count, const uint8_t *data,
                                         bool has_forces, bool *comb_need_eval)
{
   uint8_t *state = isl->state;
   for (uint32_t i = 0; i < count; i++) {
      const uint32_t sig_idx = (uint32_t)entries[i].sig_and_off;
      const uint32_t boff    = (uint32_t)(entries[i].sig_and_off >> 32);
      const uint32_t len     = (uint32_t)entries[i].len_and_data;
      const uint32_t doff    = (uint32_t)(entries[i].len_and_data >> 32);
      rt_island_sig_t *s = &(isl->sigs[sig_idx]);
      const uint8_t *src = data + doff;
      uint8_t *dst = state + s->a_off + boff;
      bool changed = false;

      if (unlikely(s->flags & RT_ISIG_SPEC_ROWF)) {
         // Mode A: a whole packed row, which is clean now
         const uint32_t rb = s->nwords * 8u;
         uint8_t *rf = state + s->a_off + rb * s->nelems + boff / rb;
         assert(len == rb && boff % rb == 0 && !has_forces);
         changed = *rf != 0;
         *rf = 0;
      }

      if (unlikely(has_forces)) {
         rt_island_force_t *f =
            (rt_island_force_t *)island_find_force(isl, sig_idx);
         if (f != NULL) {
            uint8_t *sa = (uint8_t *)f->saved_a;
            for (uint32_t k = 0; k < len; k++) {
               const uint32_t pos = boff + k;
               const uint64_t mbit = UINT64_C(1) << (pos & 63u);
               if (f->mask[pos >> 6] & mbit)
                  sa[pos] = src[k];
               else if (dst[k] != src[k]) {
                  dst[k] = src[k];
                  changed = true;
               }
            }
         }
         else
            changed = island_commit_bytes(dst, src, len);
      }
      else
         changed |= island_commit_bytes(dst, src, len);

      if (changed) {
         if (s->flags & RT_ISIG_COMB_READ)
            *comb_need_eval = true;
         if (unlikely((s->flags & (RT_ISIG_OUT | RT_ISIG_OBSERVED))
                      && !(s->flags & RT_ISIG_PRUNED))) {
            s->flags |= RT_ISIG_VHDL_SCHED;
            island_sched_out_slice(m, isl, s, boff, len);
         }
      }
   }
}

__attribute__((noinline))
static void island_commit_sparse_log(rt_model_t *m, rt_island_t *isl,
                                     bool has_forces, bool *comb_need_eval)
{
   if (isl->sparse_log_off == 0)
      return;
   rt_island_sparse_log_t *log =
      (rt_island_sparse_log_t *)(isl->state + isl->sparse_log_off);
   const uint32_t n_inline = (uint32_t)log->count;
   if (n_inline > 0) {
      log->count = 0;
      log->data_used = 0;
      island_commit_sparse_entries(m, isl, log->entries, n_inline, log->data,
                                   has_forces, comb_need_eval);
   }
   if (unlikely(isl->n_sparse_ovf > 0)) {
      const uint32_t n_ovf = isl->n_sparse_ovf;
      isl->n_sparse_ovf = 0;
      isl->sparse_ovf_data_used = 0;
      island_commit_sparse_entries(m, isl, isl->sparse_ovf_entries, n_ovf,
                                   isl->sparse_ovf_data, has_forces,
                                   comb_need_eval);
   }
}

static inline void island_commit_vhdl_desc(rt_model_t *m, rt_island_t *isl,
                                           const rt_island_ff_desc_t *d,
                                           bool has_forces,
                                           bool *comb_need_eval)
{
   if (unlikely(d->flags & RT_IFF_SPARSE_RAM)) {
      island_commit_sparse_log(m, isl, has_forces, comb_need_eval);
      return;
   }

   if (unlikely(has_forces)) {
      rt_island_force_t *f = (rt_island_force_t *)island_find_force(isl, d->sig_idx);
      if (f != NULL)
         island_apply_force_entry(isl->next_state, &(isl->sigs[d->sig_idx]), f,
                                  true);
   }

   if (!island_commit_desc_buf(isl, d))
      return;

   if (d->flags & RT_IFF_ACT)
      isl->state[d->sig_idx] = 1;

   if (d->flags & RT_IFF_COMB_READ)
      *comb_need_eval = true;

   if (unlikely(d->flags & RT_IFF_HAS_NOTIFY))
      island_notify_vhdl_ff_desc(m, isl, d);
}

static void island_spec_ff_unhold(rt_island_domain_t *dom);

static void island_commit_vhdl_domain_mask(rt_model_t *m,
                                           rt_island_domain_t *dom,
                                           uint64_t dmask,
                                           bool any_forces)
{
   rt_island_t *isl = dom->isl;
   if (unlikely(dom->spec_held_w))
      island_spec_ff_unhold(dom);
   const rt_island_ff_desc_t *descs = dom->ff_descs;
   const uint32_t n_write = dom->n_write_sigs;
   bool comb_need_eval = false;

   if (likely(!any_forces && (dmask & (UINT64_C(1) << 63)))) {
      uint64_t bits = dmask & ((UINT64_C(1) << 62) - 1u);
      const bool overflow = !!(dmask & (UINT64_C(1) << 62));
      if (bits == 0 && !overflow)
         return;
      uint8_t * restrict state = isl->state;
      const uint8_t * restrict next_state = isl->next_state;
      while (bits != 0) {
         const uint32_t j = (uint32_t)__builtin_ctzll(bits);
         bits &= bits - 1u;
         if (likely(j < n_write)) {
            const rt_island_ff_desc_t *d = &(descs[j]);
            bool changed;
            if (likely(d->flags & RT_IFF_STD_LOGIC9))
               changed = island_commit_bytes(state + d->a_off,
                                             next_state + d->a_off,
                                             d->count);
            else if (d->flags & RT_IFF_SPARSE_RAM) {
               island_commit_sparse_log(m, isl, false, &comb_need_eval);
               continue;
            }
            else
               changed = island_commit_words(isl, d);
            if (changed) {
               if (d->flags & RT_IFF_ACT)
                  isl->state[d->sig_idx] = 1;
               if (d->flags & RT_IFF_COMB_READ)
                  comb_need_eval = true;
               if (unlikely(d->flags & RT_IFF_HAS_NOTIFY))
                  island_notify_vhdl_ff_desc(m, isl, d);
            }
         }
      }
      if (unlikely(overflow)) {
         for (uint32_t j = 62; j < n_write; j++)
            island_commit_vhdl_desc(m, isl, &(descs[j]), false,
                                    &comb_need_eval);
      }
   }
   else {
      const bool has_forces = any_forces && isl->has_forces;
      if (likely(dmask & (UINT64_C(1) << 63))) {
         uint64_t bits = dmask & ((UINT64_C(1) << 62) - 1u);
         const bool overflow = !!(dmask & (UINT64_C(1) << 62));
         if (bits == 0 && !overflow)
            return;
         while (bits != 0) {
            const uint32_t j = (uint32_t)__builtin_ctzll(bits);
            bits &= bits - 1u;
            if (likely(j < n_write))
               island_commit_vhdl_desc(m, isl, &(descs[j]), has_forces,
                                       &comb_need_eval);
         }
         if (unlikely(overflow)) {
            for (uint32_t j = 62; j < n_write; j++)
               island_commit_vhdl_desc(m, isl, &(descs[j]), has_forces,
                                       &comb_need_eval);
         }
      }
      else {
         for (uint32_t j = 0; j < n_write; j++)
            island_commit_vhdl_desc(m, isl, &(descs[j]), has_forces,
                                    &comb_need_eval);
      }
   }

   if (comb_need_eval && isl->eval_handle != JIT_HANDLE_INVALID)
      island_mark_dirty(isl);
}

static void island_commit_vhdl_domain(rt_model_t *m, rt_island_domain_t *dom,
                                      bool any_forces)
{
   const uint64_t dmask = dom->ff_dirty_mask;
   dom->ff_dirty_mask = 0;
   island_commit_vhdl_domain_mask(m, dom, dmask, any_forces);
}

static void island_commit_vhdl_ff_domains(rt_model_t *m, rt_island_mgr_t *mgr)
{
   const bool any_forces = (mgr->n_forced_islands != 0);
   unsigned keep_ff = 0;
   for (unsigned i = 0; i < mgr->ff_commit_q.count; i++) {
      rt_island_domain_t *dom = mgr->ff_commit_q.items[i];
      if (island_edge_is_vhdl(dom->edge_flags)) {
         dom->in_commit_q = false;
         island_commit_vhdl_domain(m, dom, any_forces);
      }
      else
         mgr->ff_commit_q.items[keep_ff++] = dom;
   }
   mgr->ff_commit_q.count = keep_ff;

   unsigned keep_sd = 0;
   for (unsigned i = 0; i < mgr->super_commit_q.count; i++) {
      rt_super_domain_t *sd = mgr->super_commit_q.items[i];
      if (island_edge_is_vhdl(sd->edge_flags)) {
         sd->in_commit_q = false;
         if (likely(sd->used_fast_sample)) {
            sd->used_fast_sample = false;
            rt_sdom_ff_entry_t *entries = sd->ff_entries;
            const uint32_t n_doms = sd->n_doms;
            for (uint32_t d = 0; d < n_doms; d++) {
               const uint64_t dmask = entries[d].dirty_mask;
               entries[d].dirty_mask = 0;
               if (dmask == (UINT64_C(1) << 63))
                  continue;
               island_commit_vhdl_domain_mask(m, entries[d].dom, dmask,
                                              any_forces);
            }
         }
         else {
            for (uint32_t d = 0; d < sd->n_doms; d++) {
               rt_island_domain_t *dom = sd->doms[d];
               if (dom->ff_dirty_mask == (UINT64_C(1) << 63)) {
                  dom->ff_dirty_mask = 0;
                  continue;
               }
               island_commit_vhdl_domain(m, dom, any_forces);
            }
         }
      }
      else
         mgr->super_commit_q.items[keep_sd++] = sd;
   }
   mgr->super_commit_q.count = keep_sd;
}

static bool rt_island_eval_ff_inner(jit_t *jit, rt_model_t *m,
                                    rt_island_mgr_t *mgr, tlab_t *tlab);

static void island_eval_ff_tramp(void *arg)
{
   island_eval_guard_ctx_t *g = arg;
   island_jit_guarded = true;
   if (g->mgr->fast) {
      jit_batch_ctx_t batch;
      jit_batch_begin(g->jit, &batch);
      island_batch_args = batch.args;
      g->ok = rt_island_eval_ff_inner(g->jit, g->m, g->mgr, g->tlab);
      island_batch_args = NULL;
      jit_batch_end(&batch);
   }
   else
      g->ok = rt_island_eval_ff_inner(g->jit, g->m, g->mgr, g->tlab);
}

bool rt_island_eval_ff(jit_t *jit, rt_model_t *m, rt_island_mgr_t *mgr,
                       tlab_t *tlab)
{
   if (mgr == NULL
       || (mgr->ff_sample_q.count == 0 && mgr->super_sample_q.count == 0))
      return true;
   if (island_jit_guarded)
      return rt_island_eval_ff_inner(jit, m, mgr, tlab);
   island_eval_guard_ctx_t g = {
      .jit = jit, .m = m, .mgr = mgr, .tlab = tlab, .ok = false,
   };
   const bool ran = jit_run_guarded(jit, island_eval_ff_tramp, &g);
   island_jit_guarded = false;
   island_batch_args = NULL;
   return ran && g.ok;
}

static bool rt_island_eval_ff_inner(jit_t *jit, rt_model_t *m,
                                    rt_island_mgr_t *mgr, tlab_t *tlab)
{
   if (mgr == NULL
       || (mgr->ff_sample_q.count == 0 && mgr->super_sample_q.count == 0))
      return true;

   const int nthreads = opt_get_int(OPT_SIM_THREADS);
   const unsigned n_ff = mgr->ff_sample_q.count;
   const unsigned n_sd = mgr->super_sample_q.count;

   uint32_t total_doms = n_ff;
   if (nthreads > 1) {
      for (unsigned i = 0; i < n_sd; i++)
         total_doms += mgr->super_sample_q.items[i]->n_doms;
   }

   bool do_probe = false;
   const bool try_mt = (nthreads > 1 && total_doms >= RT_ISLAND_MT_FF_THRESHOLD)
      && island_mt_gate_should_run(&mgr->mt_gate_ff, &do_probe);

   if (!try_mt) {
      const uint64_t t0 = do_probe ? get_timestamp_ns() : 0;
      bool ok = true;
      if (n_ff > 0) {
         for (unsigned i = 0; i < n_ff; i++) {
            rt_island_domain_t *dom = mgr->ff_sample_q.items[i];
            island_dom_dequeue_sample(dom);

            rt_island_t *isl = dom->isl;
            const bool is_vhdl = !!(dom->edge_flags
                                    & (RT_IEDGE_VHDL | RT_IEDGE_VHDL_EVENT));
            if (!is_vhdl && isl->cluster != NULL && isl->cluster->has_dirty) {
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

         if (mgr->ff_sample_q.count > n_ff) {
            memmove(mgr->ff_sample_q.items, mgr->ff_sample_q.items + n_ff,
                    (mgr->ff_sample_q.count - n_ff)
                    * sizeof(rt_island_domain_t *));
            mgr->ff_sample_q.count -= n_ff;
         }
         else
            mgr->ff_sample_q.count = 0;
      }

      if (ok && n_sd > 0) {
         for (unsigned i = 0; i < n_sd; i++) {
            rt_super_domain_t *sd = mgr->super_sample_q.items[i];
            island_sdom_dequeue_sample(sd);
            const bool is_vhdl = !!(sd->edge_flags
                                    & (RT_IEDGE_VHDL | RT_IEDGE_VHDL_EVENT));

            for (uint32_t c = 0; c < sd->n_clusters; c++) {
               rt_super_island_t *cl = sd->clusters[c];
               if (!is_vhdl && cl->has_dirty
                   && !island_eval_cluster(jit, m, cl, tlab)) {
                  ok = false;
                  break;
               }
               if (cl->has_dirty_inputs) {
                  for (uint32_t k = 0; k < cl->n_dirty_in; k++) {
                     rt_island_t *isl = cl->dirty_in_list[k];
                     isl->in_cl_dirty_in = false;
                     if (isl->has_dirty_inputs) {
                        island_sync_dirty_inputs(isl);
                        isl->has_dirty_inputs = false;
                     }
                  }
                  cl->n_dirty_in = 0;
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
               if (!is_vhdl && isl->dirty
                   && isl->eval_handle != JIT_HANDLE_INVALID) {
                  isl->dirty = false;
                  if (!island_call_comb(jit, m, isl, tlab)) {
                     ok = false;
                     break;
                  }
                  if (isl->n_direct_comb_descs > 0)   // RT_ISIG_DIRECT_PUB
                     island_propagate_direct_comb(isl);
                  if (mgr->hier_built) {
                     for (uint32_t j = 0; j < isl->n_ext_out_sigs; j++) {
                        rt_island_sig_t *os = &(isl->sigs[isl->ext_out_sigs[j]]);
                        if (likely(!(os->flags & RT_ISIG_PRUNED)))
                           island_sync_out_sig(m, isl, os);
                     }
                  }
                  else {
                     for (uint32_t j = 0; j < isl->n_out_sigs; j++) {
                        rt_island_sig_t *os = &(isl->sigs[isl->out_sigs[j]]);
                        if (likely(!(os->flags & RT_ISIG_PRUNED)))
                           island_sync_out_sig(m, isl, os);
                     }
                  }
                  if (unlikely(isl->n_obs_sigs > 0)) {
                     for (uint32_t j = 0; j < isl->n_obs_sigs; j++) {
                        rt_island_sig_t *os = &(isl->sigs[isl->obs_sigs[j]]);
                        if (likely(!(os->flags & RT_ISIG_PRUNED)))
                           island_sync_out_sig(m, isl, os);
                     }
                  }
               }
            }
            if (!ok)
               break;

            if (is_vhdl && mgr->fast && likely(island_batch_args != NULL)
                && likely(mgr->n_forced_islands == 0)) {
               sd->used_fast_sample = true;
               jit_scalar_t *args = island_batch_args;
               tlab_t null_tlab;
               tlab_t *use_tlab = tlab;
               if (unlikely(use_tlab == NULL)) {
                  null_tlab = jit_null_tlab(jit);
                  use_tlab = &null_tlab;
               }
               rt_sdom_ff_entry_t *entries = sd->ff_entries;
               const uint32_t n_doms = sd->n_doms;
               for (uint32_t d = 0; d < n_doms; d++) {
                  rt_sdom_ff_entry_t *e = &(entries[d]);
                  if (likely(e->ff_fn != NULL)) {
                     args[0].pointer = e->inst_ctx;
                     args[1].pointer = e->state;
                     (*e->ff_fn)(e->ff_func, NULL, args, use_tlab);
                     if (tlab != NULL
                         && unlikely(tlab->alloc != 0 || tlab->mem_dirty != 0))
                        tlab_reset(tlab);
                     uint64_t r = (uint64_t)args[0].integer;
                     // A Mode A function that returned a deopt code
                     if (unlikely(!(r & (UINT64_C(1) << 63)))
                         && !island_spec_sdom_deopt(jit, e, tlab, &r)) {
                        ok = false;
                        break;
                     }
                     e->dirty_mask |= r;
                  }
                  else {
                     if (!island_sample_domain(jit, e->dom, tlab)) {
                        ok = false;
                        break;
                     }
                     if (e->dom->ff_fn != NULL && !e->dom->needs_presync) {
                        e->ff_func = e->dom->ff_func;
                        e->ff_fn   = e->dom->ff_fn;
                     }
                     e->dirty_mask |= e->dom->ff_dirty_mask;
                     e->dom->ff_dirty_mask = 0;
                  }
               }
            }
            else {
               sd->used_fast_sample = false;
               for (uint32_t d = 0; d < sd->n_doms; d++) {
                  if (!island_sample_domain(jit, sd->doms[d], tlab)) {
                     ok = false;
                     break;
                  }
               }
            }
            if (!ok)
               break;

            if (!sd->in_commit_q) {
               sd->in_commit_q = true;
               APUSH(mgr->super_commit_q, sd);
            }
         }

         if (mgr->super_sample_q.count > n_sd) {
            memmove(mgr->super_sample_q.items, mgr->super_sample_q.items + n_sd,
                    (mgr->super_sample_q.count - n_sd)
                    * sizeof(rt_super_domain_t *));
            mgr->super_sample_q.count -= n_sd;
         }
         else
            mgr->super_sample_q.count = 0;
      }

      if (do_probe && ok)
         island_mt_gate_record(&mgr->mt_gate_ff, get_timestamp_ns() - t0);

      if (ok && rt_island_has_dirty(mgr))
         ok = rt_island_eval_dirty(jit, m, mgr, tlab);
      if (ok)
         island_commit_vhdl_ff_domains(m, mgr);

      return ok;
   }

   if (!island_eval_ff_pre_sync(jit, m, mgr, tlab, n_ff, n_sd))
      return false;

   mgr->mt_ff_islands.count = 0;
   for (unsigned i = 0; i < n_ff; i++) {
      rt_island_domain_t *dom = mgr->ff_sample_q.items[i];
      island_dom_dequeue_sample(dom);
      const bool safe = jit_prepare_call(jit, dom->ff_handle, 1u);
      island_ff_enqueue_dom(mgr, dom, safe);
      if (!dom->in_commit_q) {
         dom->in_commit_q = true;
         APUSH(mgr->ff_commit_q, dom);
      }
   }
   if (n_ff > 0) {
      if (mgr->ff_sample_q.count > n_ff) {
         memmove(mgr->ff_sample_q.items, mgr->ff_sample_q.items + n_ff,
                 (mgr->ff_sample_q.count - n_ff)
                 * sizeof(rt_island_domain_t *));
         mgr->ff_sample_q.count -= n_ff;
      }
      else
         mgr->ff_sample_q.count = 0;
   }

   for (unsigned i = 0; i < n_sd; i++) {
      rt_super_domain_t *sd = mgr->super_sample_q.items[i];
      island_sdom_dequeue_sample(sd);
      sd->used_fast_sample = false;
      for (uint32_t d = 0; d < sd->n_doms; d++) {
         rt_island_domain_t *dom = sd->doms[d];
         const bool safe = jit_prepare_call(jit, dom->ff_handle, 1u);
         island_ff_enqueue_dom(mgr, dom, safe);
      }
      if (!sd->in_commit_q) {
         sd->in_commit_q = true;
         APUSH(mgr->super_commit_q, sd);
      }
   }
   if (n_sd > 0) {
      if (mgr->super_sample_q.count > n_sd) {
         memmove(mgr->super_sample_q.items, mgr->super_sample_q.items + n_sd,
                 (mgr->super_sample_q.count - n_sd)
                 * sizeof(rt_super_domain_t *));
         mgr->super_sample_q.count -= n_sd;
      }
      else
         mgr->super_sample_q.count = 0;
   }

   if (mgr->mt_ff_islands.count == 0)
      return true;

   mgr->mt_ff_safe_q.count = 0;
   for (unsigned i = 0; i < mgr->mt_ff_islands.count; i++) {
      rt_island_t *isl = mgr->mt_ff_islands.items[i];
      if (!isl->mt_ff_unsafe)
         APUSH(mgr->mt_ff_safe_q, isl);
   }

   bool ok = true;
   if (mgr->mt_ff_safe_q.count >= RT_ISLAND_MT_FF_THRESHOLD) {
      rt_island_pool_t *pool = island_pool_ensure(mgr, jit);
      ok = island_pool_run(pool, ISLAND_TASK_EVAL_FF,
                           (void *const *)mgr->mt_ff_safe_q.items,
                           mgr->mt_ff_safe_q.count, tlab);
      if (ok && mgr->mt_ff_safe_q.count < mgr->mt_ff_islands.count) {
         for (unsigned i = 0; i < mgr->mt_ff_islands.count; i++) {
            rt_island_t *isl = mgr->mt_ff_islands.items[i];
            if (isl->mt_ff_unsafe
                && !island_eval_ff_island_mt(jit, isl, tlab)) {
               ok = false;
               break;
            }
         }
      }
   }
   else {
      for (unsigned i = 0; i < mgr->mt_ff_islands.count; i++) {
         if (!island_eval_ff_island_mt(jit, mgr->mt_ff_islands.items[i],
                                       tlab)) {
            ok = false;
            break;
         }
      }
   }

   if (!ok) {
      for (unsigned i = 0; i < mgr->mt_ff_islands.count; i++) {
         rt_island_t *isl = mgr->mt_ff_islands.items[i];
         rt_island_domain_t *dom = isl->ff_pending_head;
         isl->ff_pending_head = NULL;
         isl->ff_pending_tail = NULL;
         while (dom != NULL) {
            rt_island_domain_t *next = dom->ff_batch_next;
            dom->ff_batch_next = NULL;
            dom = next;
         }
      }
   }
   if (ok && rt_island_has_dirty(mgr))
      ok = rt_island_eval_dirty(jit, m, mgr, tlab);
   if (ok)
      island_commit_vhdl_ff_domains(m, mgr);
   return ok;
}

bool rt_island_has_ff_commits(const rt_island_mgr_t *mgr)
{
   return mgr != NULL
      && (mgr->ff_commit_q.count > 0 || mgr->super_commit_q.count > 0);
}

// Commit of an SV island memory with an element write log: compares and
// copies only the logged elements and publishes each changed one.  Returns
// false when the whole memory must be committed instead (log overflowed,
// forces).
__attribute__((noinline))
static bool island_commit_touch(rt_model_t *m, rt_island_t *isl,
                                const rt_island_ff_desc_t *d,
                                bool has_forces, bool *changed_out)
{
   rt_island_sig_t *s = &(isl->sigs[d->sig_idx]);
   uint64_t *log = island_touch_log(isl, s);
   const uint64_t count = log[0];
   log[0] = 0;
   if (has_forces || count > RT_ISLAND_TOUCH_CAP)
      return false;

   const uint32_t nwords = s->nwords;
   const bool two_state = !!(s->flags & RT_ISIG_TWO_STATE);
   uint64_t *dst_a = (uint64_t *)(isl->state + s->a_off);
   const uint64_t *src_a = (const uint64_t *)(isl->next_state + s->a_off);
   uint64_t *dst_b = two_state ? NULL : (uint64_t *)(isl->state + s->b_off);
   const uint64_t *src_b = two_state
      ? NULL : (const uint64_t *)(isl->next_state + s->b_off);
   const bool sync_out = !!(d->flags & RT_IFF_SYNC_OUT);

   bool changed = false;
   for (uint64_t k = 0; k < count; k++) {
      const uint64_t e = log[1 + k];
      if (e >= s->nelems)
         continue;
      const size_t base = (size_t)e * nwords;
      bool elem_changed = false;
      for (uint32_t w = 0; w < nwords; w++) {
         if (dst_a[base + w] != src_a[base + w]) {
            dst_a[base + w] = src_a[base + w];
            elem_changed = true;
         }
         if (dst_b != NULL && dst_b[base + w] != src_b[base + w]) {
            dst_b[base + w] = src_b[base + w];
            elem_changed = true;
         }
      }
      if (elem_changed) {
         changed = true;
         if (sync_out)
            island_sync_out_slice(m, isl, s, (uint32_t)e * s->width,
                                  s->width);
      }
   }

   *changed_out = changed;
   return true;
}

static inline uint64_t *island_glitch_log(rt_island_t *isl,
                                          const rt_island_sig_t *s)
{
   const uint32_t off = island_glitch_log_off(s);
   assert(isl->next_state != NULL);
   assert(off + RT_ISLAND_GLITCH_BYTES(s->nwords,
                                       s->flags & RT_ISIG_TWO_STATE)
          <= isl->state_bytes);
   return (uint64_t *)(isl->next_state + off);
}

static void island_publish_direct(rt_island_sig_t *s)
{
   for (uint32_t t = 0; t < s->n_direct_targets; t++) {
      rt_island_sig_t *dst_s = s->direct_targets[t];
      if (island_copy_direct_sig(s, dst_s)
          && (dst_s->flags & RT_ISIG_COMB_READ)
          && dst_s->isl->eval_handle != JIT_HANDLE_INVALID)
         island_mark_dirty(dst_s->isl);
   }
}

// Publishes the values of an SV island register that non-blocking
// assignments of this time step overwrote (RT_ISIG_GLITCH_LOG), in order,
// before its commit publishes the final value.  Returns true when the
// state now holds the last of them.
__attribute__((noinline))
static bool island_commit_glitch(rt_model_t *m, rt_island_t *isl,
                                 const rt_island_ff_desc_t *d, uint64_t *log)
{
   rt_island_sig_t *s = &(isl->sigs[d->sig_idx]);
   const uint64_t count = log[0];
   log[0] = 0;

   // A forced register does not change
   if (isl->has_forces)
      return false;

   if (unlikely(count > RT_ISLAND_GLITCH_CAP)) {
      static bool warned = false;
      if (!warned) {
         warnf("%s was assigned more than %u different values by "
               "non-blocking assignments in one time step: the events of "
               "the later ones are not reproduced, run with "
               "--rtl-islands=off for exact results",
               s->sig != NULL && s->sig->where != NULL
               ? istr(tree_ident(s->sig->where)) : "a register",
               RT_ISLAND_GLITCH_CAP);
         warned = true;
      }
   }

   const uint32_t nwords = s->nwords;
   const bool two_state = !!(s->flags & RT_ISIG_TWO_STATE);
   const uint32_t n = MIN(count, RT_ISLAND_GLITCH_CAP);
   const uint64_t *ent_a = log + 1;
   const uint64_t *ent_b = ent_a + RT_ISLAND_GLITCH_CAP * nwords;
   uint64_t *dst_a = (uint64_t *)(isl->state + s->a_off);
   uint64_t *dst_b = two_state ? NULL : (uint64_t *)(isl->state + s->b_off);
   const size_t nbytes = nwords * sizeof(uint64_t);

   for (uint32_t e = 0; e < n; e++) {
      memcpy(dst_a, ent_a + e * nwords, nbytes);
      if (dst_b != NULL)
         memcpy(dst_b, ent_b + e * nwords, nbytes);
      if (d->flags & RT_IFF_DIRECT_LINK)
         island_publish_direct(s);
      else
         island_sync_out_sig(m, isl, s);
   }

   return true;
}

// Commit of a run of registers (RT_IFF_RUN)
static inline bool island_commit_run(rt_island_t *isl,
                                     const rt_island_ff_desc_t *d)
{
   uint8_t *dst = isl->state + d->a_off;
   const uint8_t *src = isl->next_state + d->a_off;
   const size_t nbytes = (size_t)d->count * sizeof(uint64_t);
   if (memcmp(dst, src, nbytes) == 0)
      return false;
   memcpy(dst, src, nbytes);
   return true;
}

static void island_commit_domain(rt_model_t *m, rt_island_domain_t *dom)
{
   rt_island_t *isl = dom->isl;
   if (unlikely(dom->spec_held_w))
      island_spec_ff_unhold(dom);
   const bool has_forces = isl->has_forces;
   const bool use_runs = !has_forces && dom->ff_runs != NULL;
   const rt_island_ff_desc_t *descs = use_runs ? dom->ff_runs : dom->ff_descs;
   const uint32_t n_write = use_runs ? dom->n_ff_runs : dom->n_write_sigs;
   const bool act = isl->act_on;
   bool comb_need_eval = false;
   for (uint32_t j = 0; j < dom->n_glitch_resets; j++) {
      uint64_t *count = (uint64_t *)(isl->next_state + dom->glitch_resets[j]);
      if (unlikely(*count != 0))
         *count = 0;
   }
   for (uint32_t j = 0; j < n_write; j++) {
      const rt_island_ff_desc_t *d = &(descs[j]);
      if (likely(d->flags & RT_IFF_RUN)) {
         if (island_commit_run(isl, d) && (d->flags & RT_IFF_COMB_READ))
            comb_need_eval = true;
         continue;
      }
      if (unlikely(d->flags & RT_IFF_SPARSE_RAM)) {
         island_commit_sparse_log(m, isl, has_forces, &comb_need_eval);
         continue;
      }
      bool whole = true;
      if (unlikely(d->flags & RT_IFF_TOUCH_LOG)) {
         bool changed = false;
         if (island_commit_touch(m, isl, d, has_forces, &changed)) {
            if (!changed)
               continue;
            whole = false;   // Changed elements are already published
         }
      }

      uint64_t *glog;
      if (unlikely(d->flags & RT_IFF_GLITCH)
          && unlikely(*(glog = island_glitch_log(isl, &(isl->sigs[d->sig_idx])))
                      != 0)
          && island_commit_glitch(m, isl, d, glog)) {
         if (unlikely(act) && (d->flags & RT_IFF_ACT))
            isl->state[d->sig_idx] = 1;
         if (d->flags & RT_IFF_COMB_READ)
            comb_need_eval = true;
      }

      if (whole) {
         if (unlikely(has_forces)) {
            rt_island_force_t *f =
               (rt_island_force_t *)island_find_force(isl, d->sig_idx);
            if (f != NULL)
               island_apply_force_entry(isl->next_state,
                                        &(isl->sigs[d->sig_idx]), f, true);
         }

         if (!island_commit_desc_buf(isl, d))
            continue;
      }

      if (unlikely(act) && (d->flags & RT_IFF_ACT))
         isl->state[d->sig_idx] = 1;

      if (d->flags & RT_IFF_COMB_READ)
         comb_need_eval = true;

      if (unlikely(d->flags & RT_IFF_HAS_NOTIFY)) {
         rt_island_sig_t *s = &(isl->sigs[d->sig_idx]);
         if (d->flags & RT_IFF_DIRECT_LINK)
            island_publish_direct(s);
         else if (whole)
            island_sync_out_sig(m, isl, s);
      }
   }

   if (comb_need_eval && isl->eval_handle != JIT_HANDLE_INVALID)
      island_mark_dirty(isl);
}

static void island_eval_clusters_tramp(void *arg)
{
   island_eval_guard_ctx_t *g = arg;
   island_jit_guarded = true;
   jit_batch_ctx_t batch;
   jit_batch_begin(g->jit, &batch);
   island_batch_args = batch.args;
   g->ok = island_eval_dirty_clusters(g->jit, g->m, g->mgr, g->tlab, false);
   island_batch_args = NULL;
   jit_batch_end(&batch);
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

   if (mgr->dirty_clusters.count > 0) {
      jit_t *jit = rt_island_model_jit(m);
      if (mgr->fast && !island_jit_guarded) {
         island_eval_guard_ctx_t g = {
            .jit = jit, .m = m, .mgr = mgr, .tlab = tlab, .ok = false,
         };
         (void)jit_run_guarded(jit, island_eval_clusters_tramp, &g);
         island_jit_guarded = false;
         island_batch_args = NULL;
      }
      else
         island_eval_dirty_clusters(jit, m, mgr, tlab, false);
   }
}

bool rt_island_read_sig(rt_island_mgr_t *mgr, rt_signal_t *sig,
                        uint64_t *a_out, uint64_t *b_out,
                        uint32_t *width_out, bool *two_state_out)
{
   if (mgr == NULL || mgr->sig_map == NULL || sig == NULL)
      return false;

   rt_island_sig_t *s = hash_get(mgr->sig_map, sig);
   if (s == NULL)
      return false;

   rt_island_t *isl = s->isl;
   if (unlikely(s->flags & RT_ISIG_SPEC_ROWS))
      island_spec_whole_read(isl, s);
   const uint32_t nwords = s->nwords;
   const uint32_t nelems = s->nelems;
   if (s->flags & RT_ISIG_STD_LOGIC9) {
      if (s->elem_size != 1)
         return false;
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
   for (; s != NULL; s = s->next_owner) {
      if ((s->flags & RT_ISIG_VHDL_SCHED)
          && (s->flags & (RT_ISIG_OUT | RT_ISIG_OBSERVED))
          && !(s->flags & (RT_ISIG_DIRECT_LINK | RT_ISIG_PRUNED)))
         continue;
      island_sync_sig_raw(s->isl, s, false);
   }

   for (rt_island_alias_t *a = hash_get(mgr->alias_map, sig);
        a != NULL; a = a->next) {
      if (a->src_s->flags & RT_ISIG_DIRECT_LINK)
         island_sync_sig_raw_to(a->src_s->isl, a->src_s,
                                a->sig, a->sig_offset, false);
   }
}

void rt_island_sync_if_unobserved(rt_model_t *m, rt_signal_t *sig)
{
   rt_island_mgr_t *mgr = rt_island_model_mgr(m);
   if (mgr == NULL || mgr->sig_map == NULL || sig == NULL)
      return;

   rt_island_sig_t *s = hash_get(mgr->sig_map, sig);
   for (; s != NULL; s = s->next_owner) {
      if ((s->flags & (RT_ISIG_PRUNED | RT_ISIG_DIRECT_LINK))
          || !(s->flags & (RT_ISIG_OUT | RT_ISIG_OBSERVED)))
         island_sync_sig_raw(s->isl, s, false);
   }

   for (rt_island_alias_t *a = hash_get(mgr->alias_map, sig);
        a != NULL; a = a->next) {
      if (a->src_s->flags & RT_ISIG_DIRECT_LINK)
         island_sync_sig_raw_to(a->src_s->isl, a->src_s,
                                a->sig, a->sig_offset, false);
   }
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
      if ((s->flags & RT_ISIG_VHDL_SCHED)
          && (s->flags & (RT_ISIG_OUT | RT_ISIG_OBSERVED))
          && !(s->flags & (RT_ISIG_DIRECT_LINK | RT_ISIG_PRUNED)))
         continue;
      const uint32_t s_start = (uint32_t)s->sig_offset;
      const uint32_t s_end = s_start + s->width * s->nelems;
      const uint32_t r_start = MAX(offset, s_start);
      const uint32_t r_end = MIN(offset + count, s_end);
      if (r_start < r_end)
         island_sync_sig_raw_slice(s->isl, s, r_start - s_start,
                                   r_end - r_start, false);
   }

   for (rt_island_alias_t *a = hash_get(mgr->alias_map, sig);
        a != NULL; a = a->next) {
      if (!(a->src_s->flags & RT_ISIG_DIRECT_LINK))
         continue;
      const uint32_t a_start = (uint32_t)a->sig_offset;
      const uint32_t a_end = a_start + a->src_s->width * a->src_s->nelems;
      const uint32_t r_start = MAX(offset, a_start);
      const uint32_t r_end = MIN(offset + count, a_end);
      if (r_start < r_end)
         island_sync_sig_raw_slice_to(a->src_s->isl, a->src_s,
                                      a->sig, a->sig_offset,
                                      r_start - a_start, r_end - r_start,
                                      false);
   }
}

static void island_break_direct_link(rt_model_t *m, rt_island_sig_t *src_s)
{
   if (!(src_s->flags & RT_ISIG_DIRECT_LINK)) {
      src_s->flags &= ~RT_ISIG_DIRECT_PUB;   // Dormant: never made
      return;
   }

   if (unlikely(island_wstats_on))
      island_wstat_cause(src_s, island_wb_cur);
   src_s->flags &= ~(RT_ISIG_DIRECT_LINK | RT_ISIG_DIRECT_PUB);
   src_s->flags |= RT_ISIG_OBSERVED;
   island_sync_sig_raw(src_s->isl, src_s, true);
   for (uint32_t a = 0; a < src_s->n_direct_aliases; a++) {
      const rt_island_alias_t *al = &(src_s->direct_aliases[a]);
      island_sync_sig_raw_to(src_s->isl, src_s, al->sig, al->sig_offset, true);
   }
   if (src_s->flags & RT_ISIG_COMB_WRITTEN)
      island_add_comb_flush_sig(src_s->isl, src_s);

   const bool allow_vhdl_inline = src_s->isl->mgr->fast
      && !opt_get_int(OPT_SHUFFLE_PROCS);
   for (uint32_t t = 0; t < src_s->n_direct_targets; t++) {
      rt_island_sig_t *dst_s = src_s->direct_targets[t];
      island_copy_direct_sig(src_s, dst_s);
      dst_s->flags &= ~RT_ISIG_DIRECT_LINK;
      if (dst_s->watch == NULL && dst_s->sig != NULL) {
         dst_s->watch = watch_new(m, island_input_watch_cb, dst_s,
                                  WATCH_EVENT, 1);
         watch_set_inline_island(dst_s->watch,
                                 !dst_s->isl->is_vhdl || allow_vhdl_inline);
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
      if (dst_s->flags & RT_ISIG_FF_WRITTEN)
         island_refresh_domain_descs(dst_s->isl);
   }
   island_refresh_domain_descs(src_s->isl);
   island_refresh_direct_comb_descs(src_s->isl);
}

// RT_ISIG_DIRECT_PUB wires are made at the start of the first delta cycle
// after time zero: until then the readers take the value from the kernel
// signal through their input watches, which follows the late publication
// of always_comb values and the reset-time evaluation exactly.  At this
// point the source island state, its kernel signal and every reader agree
// (a reader with a pending input sync takes the same kernel value later).
static void island_activate_direct_pub(rt_island_mgr_t *mgr)
{
   const int64_t now = model_now(mgr->model, NULL);
   if (now == 0)
      return;

   mgr->pub_pending = false;
   for (unsigned i = 0; i < mgr->islands.count; i++) {
      rt_island_t *isl = mgr->islands.items[i];
      if (!isl->has_direct_pub)
         continue;

      bool any = false;
      for (uint32_t j = 0; j < isl->nsigs; j++) {
         rt_island_sig_t *src_s = &(isl->sigs[j]);
         if ((src_s->flags & (RT_ISIG_DIRECT_PUB | RT_ISIG_DIRECT_LINK))
             != RT_ISIG_DIRECT_PUB)
            continue;
         src_s->flags |= RT_ISIG_DIRECT_LINK;
         for (uint32_t t = 0; t < src_s->n_direct_targets; t++) {
            rt_island_sig_t *dst_s = src_s->direct_targets[t];
            dst_s->flags |= RT_ISIG_DIRECT_LINK;
            dst_s->direct_src = src_s;
            if (dst_s->watch != NULL) {
               watch_free(mgr->model, dst_s->watch);
               dst_s->watch = NULL;
            }
            island_copy_direct_sig(src_s, dst_s);
         }
         any = true;
      }
      if (!any)
         continue;

      uint32_t n_dir = 0;
      for (uint32_t j = 0; j < isl->nsigs; j++) {
         const uint32_t f = isl->sigs[j].flags;
         if ((f & (RT_ISIG_COMB_WRITTEN | RT_ISIG_DIRECT_LINK))
             == (RT_ISIG_COMB_WRITTEN | RT_ISIG_DIRECT_LINK))
            n_dir++;
      }
      free(isl->direct_comb_outs);
      isl->direct_comb_outs = xmalloc_array(n_dir, sizeof(uint32_t));
      isl->n_direct_comb_outs = 0;
      for (uint32_t j = 0; j < isl->nsigs; j++) {
         const uint32_t f = isl->sigs[j].flags;
         if ((f & (RT_ISIG_COMB_WRITTEN | RT_ISIG_DIRECT_LINK))
             == (RT_ISIG_COMB_WRITTEN | RT_ISIG_DIRECT_LINK))
            isl->direct_comb_outs[isl->n_direct_comb_outs++] = j;
      }
      island_refresh_direct_comb_descs(isl);
   }
}

static void island_break_aliases(rt_model_t *m, rt_island_mgr_t *mgr,
                                 rt_signal_t *sig)
{
   const island_wire_cause_t saved = island_wb_cur;
   island_wb_cur = WB_ALIAS;
   for (rt_island_alias_t *a = hash_get(mgr->alias_map, sig);
        a != NULL; a = a->next) {
      if (a->src_s->flags & RT_ISIG_DIRECT_LINK)
         island_break_direct_link(m, a->src_s);
   }
   island_wb_cur = saved;
}

static void island_ext_deposit_watch_cb(uint64_t now, rt_signal_t *signal,
                                        rt_watch_t *watch, void *user)
{
   rt_island_sig_t *s = user;
   rt_island_t *isl = s->isl;
   island_sync_in_sig(isl, s);
   island_act_invalidate(isl);
   if (unlikely(isl->has_forces)) {
      rt_island_force_t *f = (rt_island_force_t *)island_find_force(isl, s->sig_idx);
      if (f != NULL) {
         island_apply_force_entry(isl->state, s, f, true);
         island_sync_sig_raw(isl, s, false);
      }
   }
   if (isl->next_state != NULL && !(s->flags & RT_ISIG_SPARSE_RAM))
      island_copy_sig_buf(isl->next_state, isl->state, s);
   if ((s->flags & RT_ISIG_COMB_READ)
       && isl->eval_handle != JIT_HANDLE_INVALID)
      island_mark_dirty(isl);
}

static void island_unprune_sig(rt_island_mgr_t *mgr, rt_island_sig_t *s)
{
   if (!(s->flags & RT_ISIG_PRUNED))
      return;
   s->flags &= ~(RT_ISIG_PRUNED | RT_ISIG_LAST_OUT_VALID);
   s->flags |= RT_ISIG_OBSERVED;
   if (mgr->reset_done)
      island_sync_sig_raw(s->isl, s, true);
   island_add_comb_flush_sig(s->isl, s);
   island_refresh_domain_descs(s->isl);
}

void rt_island_on_ext_deposit(rt_model_t *m, rt_signal_t *sig)
{
   rt_island_mgr_t *mgr = rt_island_model_mgr(m);
   if (mgr == NULL || mgr->sig_map == NULL || sig == NULL)
      return;

   island_spec_pin_sig(mgr, sig, RT_ISLAND_DEOPT_PUT);
   island_wb_cur = WB_EXT_DEPOSIT;
   island_break_aliases(m, mgr, sig);

   rt_island_sig_t *s = hash_get(mgr->sig_map, sig);
   for (; s != NULL; s = s->next_owner) {
      if (unlikely(island_wstats_on))
         island_wstat_cause(s, WB_EXT_DEPOSIT);
      s->flags &= ~(RT_ISIG_SOLE_OWNER | RT_ISIG_LAST_OUT_VALID);
      if (s->flags & RT_ISIG_DIRECT_LINK)
         island_break_direct_link(m, s);
      if (s->flags & RT_ISIG_PRUNED)
         island_unprune_sig(mgr, s);
      else if (!(s->flags & RT_ISIG_OBSERVED)) {
         if (!(s->flags & RT_ISIG_OUT))
            island_sync_sig_raw(s->isl, s, true);
         s->flags |= RT_ISIG_OBSERVED;
         if ((s->flags & RT_ISIG_COMB_WRITTEN) && !(s->flags & RT_ISIG_OUT))
            island_add_obs_sig(s->isl, s->sig_idx);
         island_refresh_domain_descs(s->isl);
      }
      s->flags |= RT_ISIG_VHDL_SCHED;
      if (s->ext_write_watch == NULL && s->sig != NULL) {
         s->ext_write_watch = watch_new(m, island_ext_deposit_watch_cb, s,
                                        WATCH_EVENT, 1);
         if (s->sig->shared.flags & SIG_F_PACKED)
            model_set_event_cb(m, s->sig, 0, 1, s->ext_write_watch);
         else
            model_set_event_cb(m, s->sig, s->sig_offset,
                               (int)(s->width * s->nelems),
                               s->ext_write_watch);
      }
   }
}

void rt_island_on_observer_attached(rt_model_t *m, rt_signal_t *sig)
{
   if (unlikely(island_wstats_on))
      island_wb_ra = __builtin_return_address(0);
   rt_island_mgr_t *mgr = rt_island_model_mgr(m);
   if (mgr == NULL || mgr->sig_map == NULL || sig == NULL)
      return;

   sig->shared.flags &= ~(SIG_F_FAST_NOOP | SIG_F_FAST_DIRECT);
   island_wb_cur = WB_OBSERVER;
   island_break_aliases(m, mgr, sig);

   rt_island_sig_t *s = hash_get(mgr->sig_map, sig);
   for (; s != NULL; s = s->next_owner) {
      if (unlikely(island_wstats_on))
         island_wstat_cause(s, WB_OBSERVER);
      // A packed sparse RAM or 2D signal with held rows of a Mode A island
      // has no exact whole value for the observer (and a sparse RAM logs
      // packed rows): back to Mode B, promoted again with the signal held
      if (unlikely(s->flags & RT_ISIG_SPEC_A)
          && (s->flags & (RT_ISIG_SPEC_ROWS | RT_ISIG_SPARSE_RAM))
          && (s->flags & RT_ISIG_TWO_STATE))
         island_spec_demote(s->isl, RT_ISLAND_DEOPT_ROW);
      s->flags &= ~RT_ISIG_LAST_OUT_VALID;
      if (s->flags & RT_ISIG_DIRECT_PUB)
         continue;   // The kernel signal is up to date
      else if (s->flags & RT_ISIG_DIRECT_LINK)
         island_break_direct_link(m, s);
      else if (s->flags & RT_ISIG_PRUNED)
         island_unprune_sig(mgr, s);
      else if (!(s->flags & RT_ISIG_OBSERVED)) {
         if (mgr->reset_done && !(s->flags & RT_ISIG_OUT))
            island_sync_sig_raw(s->isl, s, true);
         s->flags |= RT_ISIG_OBSERVED;
         if ((s->flags & RT_ISIG_COMB_WRITTEN) && !(s->flags & RT_ISIG_OUT))
            island_add_obs_sig(s->isl, s->sig_idx);
         island_refresh_domain_descs(s->isl);
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

   if (isl->eval_handle != JIT_HANDLE_INVALID) {
      island_call_comb(rt_island_model_jit(m), m, isl, NULL);
      island_after_comb(isl);
   }

   if (isl->n_direct_comb_outs > 0 && isl->is_vhdl)
      island_comb_direct_outs(isl);
   else if (isl->n_direct_comb_outs > 0) {
      for (uint32_t j = 0; j < isl->n_direct_comb_outs; j++) {
         const rt_island_sig_t *src_s = &(isl->sigs[isl->direct_comb_outs[j]]);
         if (unlikely(!(src_s->flags & RT_ISIG_DIRECT_LINK)))
            continue;
         for (uint32_t t = 0; t < src_s->n_direct_targets; t++) {
            rt_island_sig_t *dst_s = src_s->direct_targets[t];
            if (island_copy_direct_sig(src_s, dst_s)
                && (dst_s->flags & RT_ISIG_COMB_READ)
                && dst_s->isl->eval_handle != JIT_HANDLE_INVALID) {
               island_direct_wake(dst_s->isl,
                                  !!(src_s->flags & RT_ISIG_DIRECT_PUB));
               rt_island_request_delta(m);
            }
         }
      }
   }
   if (isl->mgr->hier_built) {
      for (uint32_t j = 0; j < isl->n_ext_out_sigs; j++)
         island_flush_comb_out_sig(m, isl, &(isl->sigs[isl->ext_out_sigs[j]]));
   }
   else {
      for (uint32_t j = 0; j < isl->n_out_sigs; j++)
         island_flush_comb_out_sig(m, isl, &(isl->sigs[isl->out_sigs[j]]));
   }
   if (unlikely(isl->n_obs_sigs > 0)) {
      for (uint32_t j = 0; j < isl->n_obs_sigs; j++)
         island_flush_comb_out_sig(m, isl, &(isl->sigs[isl->obs_sigs[j]]));
   }

   island_clear_dirty(isl);
}

static void island_write_bits_slice(rt_island_t *isl, rt_island_sig_t *s,
                                    uint32_t local_off, uint32_t local_cnt,
                                    const uint8_t *bytes,
                                    const uint64_t *a_words,
                                    const uint64_t *b_words,
                                    bool is_packed_src, uint32_t total_src_w,
                                    bool is_force)
{
   island_act_invalidate(isl);
   rt_island_force_t *f = NULL;
   if (is_force)
      f = island_get_or_add_force(isl, s);
   else if (unlikely(isl->has_forces))
      f = (rt_island_force_t *)island_find_force(isl, s->sig_idx);

   if (s->flags & RT_ISIG_STD_LOGIC9) {
      const size_t elem_sz = s->elem_size;
      uint8_t *dst = isl->state + s->a_off;
      uint8_t *ndst = (isl->next_state != NULL
                       && !(s->flags & RT_ISIG_SPARSE_RAM))
         ? isl->next_state + s->a_off : NULL;
      uint8_t *fa = f != NULL ? (uint8_t *)f->a : NULL;
      uint8_t *sa = f != NULL ? (uint8_t *)f->saved_a : NULL;
      for (uint32_t k = 0; k < local_cnt; k++) {
         const uint32_t pos = local_off + k;
         uint8_t val_buf[8] = {0};
         if (is_packed_src) {
            const uint32_t src_bit = total_src_w - 1u - k;
            const uint8_t a = (a_words[src_bit / 64u] >> (src_bit & 63u)) & 1u;
            const uint8_t b = b_words != NULL
               ? ((b_words[src_bit / 64u] >> (src_bit & 63u)) & 1u) : 0u;
            val_buf[0] = sv_to_std_logic((uint8_t)(a | (b << 1)));
         }
         else if (bytes != NULL)
            memcpy(val_buf, bytes + (size_t)k * elem_sz, elem_sz);

         const uint64_t mbit = UINT64_C(1) << (pos & 63u);
         if (is_force) {
            if (!(f->mask[pos >> 6] & mbit))
               memcpy(sa + (size_t)pos * elem_sz, dst + (size_t)pos * elem_sz,
                      elem_sz);
            f->mask[pos >> 6] |= mbit;
            memcpy(fa + (size_t)pos * elem_sz, val_buf, elem_sz);
            memcpy(dst + (size_t)pos * elem_sz, val_buf, elem_sz);
         }
         else {
            if (f != NULL && (f->mask[pos >> 6] & mbit)) {
               memcpy(sa + (size_t)pos * elem_sz, val_buf, elem_sz);
               continue;
            }
            memcpy(dst + (size_t)pos * elem_sz, val_buf, elem_sz);
            if (ndst != NULL)
               memcpy(ndst + (size_t)pos * elem_sz, val_buf, elem_sz);
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

   // Bytes come in the encoding of the signal: std_logic for VHDL
   const bool is_std = bytes != NULL && island_sig_is_std(s->sig, s->flags);

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
         const uint8_t v = is_std ? std_logic_to_sv(bytes[k]) : bytes[k];
         a = v & 1u;
         b = (v >> 1) & 1u;
      }

      if (two_state) {
         a &= ~b;
         b = 0;
      }

      if (is_force) {
         if (!(f->mask[widx] & bit)) {
            f->saved_a[widx] = (f->saved_a[widx] & ~bit) | (ap[widx] & bit);
            if (bp != NULL)
               f->saved_b[widx] = (f->saved_b[widx] & ~bit) | (bp[widx] & bit);
         }
         f->mask[widx] |= bit;
         f->a[widx] = (f->a[widx] & ~bit) | (a ? bit : 0);
         f->b[widx] = (f->b[widx] & ~bit) | (b ? bit : 0);
         ap[widx] = (ap[widx] & ~bit) | (a ? bit : 0);
         if (bp != NULL)
            bp[widx] = (bp[widx] & ~bit) | (b ? bit : 0);
      }
      else {
         if (f != NULL && (f->mask[widx] & bit)) {
            f->saved_a[widx] = (f->saved_a[widx] & ~bit) | (a ? bit : 0);
            if (bp != NULL)
               f->saved_b[widx] = (f->saved_b[widx] & ~bit) | (b ? bit : 0);
            continue;
         }
         ap[widx] = (ap[widx] & ~bit) | (a ? bit : 0);
         if (bp != NULL)
            bp[widx] = (bp[widx] & ~bit) | (b ? bit : 0);
         if (nap != NULL)
            nap[widx] = (nap[widx] & ~bit) | (a ? bit : 0);
         if (nbp != NULL)
            nbp[widx] = (nbp[widx] & ~bit) | (b ? bit : 0);
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

   island_spec_pin_sig(mgr, sig, RT_ISLAND_DEOPT_PUT);
   island_wb_cur = WB_VPI_PUT;
   island_break_aliases(m, mgr, sig);
   rt_island_sig_t *s = hash_get(mgr->sig_map, sig);
   if (s == NULL)
      return false;

   for (; s != NULL; s = s->next_owner) {
      s->flags &= ~RT_ISIG_LAST_OUT_VALID;
      if (s->flags & RT_ISIG_DIRECT_LINK)
         island_break_direct_link(m, s);
      if (s->flags & RT_ISIG_PRUNED)
         island_unprune_sig(mgr, s);
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

static bool island_build_cov_mask(rt_island_sig_t *owners, uint32_t offset,
                                  uint32_t count, uint64_t *stack_buf,
                                  size_t stack_words, uint64_t **mask_out,
                                  uint64_t **heap_out)
{
   const uint32_t nwords = (count + 63u) / 64u;
   uint64_t *mask = stack_buf;
   *heap_out = NULL;
   if (nwords > stack_words) {
      *heap_out = xcalloc_array(nwords, sizeof(uint64_t));
      mask = *heap_out;
   }
   else
      memset(mask, 0, (size_t)nwords * sizeof(uint64_t));
   *mask_out = mask;

   for (rt_island_sig_t *s = owners; s != NULL; s = s->next_owner) {
      const uint32_t s_start = (uint32_t)s->sig_offset;
      const uint32_t s_end = s_start + s->width * s->nelems;
      const uint32_t r_start = MAX(offset, s_start);
      const uint32_t r_end = MIN(offset + count, s_end);
      for (uint32_t pos = r_start; pos < r_end; pos++) {
         const uint32_t k = pos - offset;
         mask[k >> 6] |= UINT64_C(1) << (k & 63u);
      }
   }

   for (uint32_t w = 0; w < nwords; w++) {
      const uint64_t expect = (w + 1u == nwords && (count & 63u) != 0)
         ? ((UINT64_C(1) << (count & 63u)) - 1u) : ~UINT64_C(0);
      if ((mask[w] & expect) != expect)
         return false;
   }
   return true;
}

// A force or release issued by a VHDL process changes the effective value
// of the signal in the next simulation cycle (IEEE 1076-2008 10.5.2.2), so
// the island processes that read it must see the new value only from the
// next delta cycle on, as with --rtl-islands=off.  The force entry and the
// scheduled update of the signal are created at once, but the island state
// keeps its current value until rt_island_promote_deferred (forced bits are
// also applied after an evaluation of the island in the current delta).
static inline bool island_defer_vhdl_put(rt_model_t *m,
                                         const rt_island_sig_t *s)
{
   return (s->flags & RT_ISIG_VHDL) && model_can_create_delta(m);
}

static size_t island_sig_buf_bytes(const rt_island_sig_t *s)
{
   if (s->flags & RT_ISIG_STD_LOGIC9)
      return (size_t)s->width * s->nelems * s->elem_size;
   return (size_t)s->nwords * s->nelems * sizeof(uint64_t);
}

static inline bool island_sig_has_b(const rt_island_sig_t *s)
{
   return !(s->flags & (RT_ISIG_STD_LOGIC9 | RT_ISIG_TWO_STATE));
}

static uint8_t *island_save_sig_state(const rt_island_t *isl,
                                      const rt_island_sig_t *s)
{
   const size_t bytes = island_sig_buf_bytes(s);
   const bool has_b = island_sig_has_b(s);
   uint8_t *buf = xmalloc(MAX(1u, has_b ? bytes * 2 : bytes));
   memcpy(buf, isl->state + s->a_off, bytes);
   if (has_b)
      memcpy(buf + bytes, isl->state + s->b_off, bytes);
   return buf;
}

static void island_defer_part(uint8_t *state, uint8_t *diff, uint8_t *val,
                              const uint8_t *old, size_t bytes)
{
   for (size_t i = 0; i < bytes; i++) {
      diff[i] = state[i] ^ old[i];
      val[i] = state[i];
      state[i] = old[i];
   }
}

// Puts back the state saved by island_save_sig_state and queues the change
// made since then for the next delta cycle
static void island_defer_sig_state(rt_island_t *isl, const rt_island_sig_t *s,
                                   uint8_t *old)
{
   const size_t bytes = island_sig_buf_bytes(s);
   const bool has_b = island_sig_has_b(s);
   uint8_t *buf = xmalloc(MAX(1u, bytes * (has_b ? 4 : 2)));
   island_defer_part(isl->state + s->a_off, buf, buf + bytes, old, bytes);
   if (has_b)
      island_defer_part(isl->state + s->b_off, buf + 2 * bytes,
                        buf + 3 * bytes, old + bytes, bytes);
   free(old);

   island_defer_put_t d = {
      .isl   = isl,
      .a_off = s->a_off,
      .b_off = s->b_off,
      .bytes = bytes,
      .has_b = has_b,
      .buf   = buf,
   };
   APUSH(isl->mgr->defer_put_q, d);
   rt_island_request_delta(isl->model);
}

static void island_apply_deferred_puts(rt_island_mgr_t *mgr)
{
   const unsigned n = mgr->defer_put_q.count;
   for (unsigned i = 0; i < n; i++) {
      island_defer_put_t *d = &(mgr->defer_put_q.items[i]);
      rt_island_t *isl = d->isl;
      for (int part = 0; part < (d->has_b ? 2 : 1); part++) {
         uint8_t *state = isl->state + (part ? d->b_off : d->a_off);
         const uint8_t *diff = d->buf + 2 * part * d->bytes;
         const uint8_t *val = diff + d->bytes;
         for (uint32_t k = 0; k < d->bytes; k++)
            state[k] = (state[k] & ~diff[k]) | (val[k] & diff[k]);
      }
      free(d->buf);
      island_act_invalidate(isl);
      if (isl->eval_handle != JIT_HANDLE_INVALID)
         island_mark_dirty(isl);
   }
   mgr->defer_put_q.count = 0;
}

bool rt_island_put_bytes(rt_model_t *m, rt_signal_t *sig,
                         const uint8_t *bytes, uint32_t offset, uint32_t count,
                         bool is_force)
{
   rt_island_mgr_t *mgr = rt_island_model_mgr(m);
   if (mgr == NULL || mgr->sig_map == NULL || sig == NULL || count == 0)
      return false;

   island_spec_pin_sig(mgr, sig, RT_ISLAND_DEOPT_PUT);
   island_wb_cur = WB_VPI_PUT;
   island_break_aliases(m, mgr, sig);
   rt_island_sig_t *owners = hash_get(mgr->sig_map, sig);
   if (owners == NULL)
      return false;

   bool any_hit = false;
   bool full_cover = false;
   for (rt_island_sig_t *s = owners; s != NULL; s = s->next_owner) {
      const uint32_t s_start = (uint32_t)s->sig_offset;
      const uint32_t s_end = s_start + s->width * s->nelems;
      const uint32_t r_start = MAX(offset, s_start);
      const uint32_t r_end = MIN(offset + count, s_end);
      if (r_start >= r_end)
         continue;

      any_hit = true;
      if (r_start == offset && r_end == offset + count)
         full_cover = true;

      s->flags &= ~RT_ISIG_LAST_OUT_VALID;
      if (s->flags & RT_ISIG_DIRECT_LINK)
         island_break_direct_link(m, s);
      if (s->flags & RT_ISIG_PRUNED)
         island_unprune_sig(mgr, s);

      uint8_t *saved = NULL;
      if (is_force && island_defer_vhdl_put(m, s))
         saved = island_save_sig_state(s->isl, s);

      const size_t step = (s->flags & RT_ISIG_STD_LOGIC9) ? s->elem_size : 1u;
      island_write_bits_slice(s->isl, s, r_start - s_start, r_end - r_start,
                              bytes != NULL ? bytes + (size_t)(r_start - offset) * step : NULL,
                              NULL, NULL, false, 0, is_force);
      island_flush_out_slice(m, s->isl, s, r_start - s_start, r_end - r_start);

      if (saved != NULL)
         island_defer_sig_state(s->isl, s, saved);
      else if (is_force || !(s->flags & RT_ISIG_COMB_WRITTEN)) {
         if ((s->flags & RT_ISIG_COMB_READ)
             && s->isl->eval_handle != JIT_HANDLE_INVALID) {
            island_mark_dirty(s->isl);
            rt_island_request_delta(m);
         }
      }
   }

   if (!any_hit)
      return false;
   if (full_cover)
      return true;

   uint64_t cov_stack[16];
   uint64_t *cov = NULL, *cov_heap = NULL;
   if (!island_build_cov_mask(owners, offset, count, cov_stack,
                              ARRAY_LEN(cov_stack), &cov, &cov_heap)) {
      const size_t step = (sig->shared.flags & SIG_F_PACKED)
         ? 1u : (sig->nexus.size > 0 ? sig->nexus.size : 1u);
      uint32_t k = 0;
      while (k < count) {
         if (cov[k >> 6] & (UINT64_C(1) << (k & 63u))) {
            k++;
            continue;
         }
         const uint32_t u_start = k;
         while (k < count && !(cov[k >> 6] & (UINT64_C(1) << (k & 63u))))
            k++;
         const uint32_t u_cnt = k - u_start;
         const uint8_t *src = bytes != NULL ? bytes + (size_t)u_start * step : NULL;
         if (is_force)
            rt_island_force_unowned_bytes(m, sig, src, (int)(offset + u_start),
                                          u_cnt);
         else
            deposit_signal(m, sig, src, (int)(offset + u_start), u_cnt);
      }
   }
   free(cov_heap);
   return true;
}

bool rt_island_put_packed(rt_model_t *m, rt_signal_t *sig,
                          uint32_t msb_off, uint32_t count,
                          uint64_t a_val, uint64_t b_val, uint32_t src_flags,
                          bool is_force)
{
   rt_island_mgr_t *mgr = rt_island_model_mgr(m);
   if (mgr == NULL || mgr->sig_map == NULL || sig == NULL || count == 0)
      return false;

   island_spec_pin_sig(mgr, sig, RT_ISLAND_DEOPT_PUT);
   island_wb_cur = WB_VPI_PUT;
   island_break_aliases(m, mgr, sig);
   rt_island_sig_t *owners = hash_get(mgr->sig_map, sig);
   if (owners == NULL)
      return false;

   const uint64_t *src_a = (src_flags & PACK_SRC_SCALAR)
      ? &a_val : (const uint64_t *)(uintptr_t)a_val;
   const uint64_t *src_b = (src_flags & PACK_SRC_VEC2) ? NULL
      : ((src_flags & PACK_SRC_SCALAR)
         ? &b_val : (const uint64_t *)(uintptr_t)b_val);

   bool any_hit = false;
   bool full_cover = false;
   for (rt_island_sig_t *s = owners; s != NULL; s = s->next_owner) {
      const uint32_t s_start = (uint32_t)s->sig_offset;
      const uint32_t s_end = s_start + s->width * s->nelems;
      const uint32_t r_start = MAX(msb_off, s_start);
      const uint32_t r_end = MIN(msb_off + count, s_end);
      if (r_start >= r_end)
         continue;

      any_hit = true;
      if (r_start == msb_off && r_end == msb_off + count)
         full_cover = true;

      s->flags &= ~RT_ISIG_LAST_OUT_VALID;
      if (s->flags & RT_ISIG_DIRECT_LINK)
         island_break_direct_link(m, s);
      if (s->flags & RT_ISIG_PRUNED)
         island_unprune_sig(mgr, s);

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

   if (!any_hit)
      return false;
   if (full_cover)
      return true;

   uint64_t cov_stack[16];
   uint64_t *cov = NULL, *cov_heap = NULL;
   if (!island_build_cov_mask(owners, msb_off, count, cov_stack,
                              ARRAY_LEN(cov_stack), &cov, &cov_heap)) {
      const uint32_t vec_flags = src_flags & ~PACK_SRC_SCALAR;
      uint32_t k = 0;
      while (k < count) {
         if (cov[k >> 6] & (UINT64_C(1) << (k & 63u))) {
            k++;
            continue;
         }
         const uint32_t u_start = k;
         while (k < count && !(cov[k >> 6] & (UINT64_C(1) << (k & 63u))))
            k++;
         const uint32_t sub_cnt = k - u_start;
         const uint32_t nw = (sub_cnt + 63u) / 64u;
         uint64_t sa[4], sb[4];
         uint64_t *heap = NULL;
         uint64_t *wa = sa, *wb = sb;
         if (nw > ARRAY_LEN(sa)) {
            heap = xcalloc_array(nw * 2u, sizeof(uint64_t));
            wa = heap;
            wb = heap + nw;
         }
         else {
            memset(sa, 0, (size_t)nw * sizeof(uint64_t));
            memset(sb, 0, (size_t)nw * sizeof(uint64_t));
         }
         for (uint32_t j = 0; j < sub_cnt; j++) {
            const uint32_t src_bit = count - 1u - (u_start + j);
            const uint32_t dst_bit = sub_cnt - 1u - j;
            if ((src_a[src_bit / 64u] >> (src_bit & 63u)) & 1u)
               wa[dst_bit / 64u] |= UINT64_C(1) << (dst_bit & 63u);
            if (src_b != NULL
                && ((src_b[src_bit / 64u] >> (src_bit & 63u)) & 1u))
               wb[dst_bit / 64u] |= UINT64_C(1) << (dst_bit & 63u);
         }
         if (is_force)
            rt_island_force_unowned_packed(m, sig, msb_off + u_start, sub_cnt,
                                           (uint64_t)(uintptr_t)wa,
                                           (uint64_t)(uintptr_t)wb, vec_flags);
         else
            rt_island_deposit_unowned_packed(m, sig, msb_off + u_start, sub_cnt,
                                             (uint64_t)(uintptr_t)wa,
                                             (uint64_t)(uintptr_t)wb,
                                             vec_flags);
         free(heap);
      }
   }
   free(cov_heap);
   return true;
}

bool rt_island_vpi_release_slice(rt_model_t *m, rt_signal_t *sig,
                                 uint32_t offset, uint32_t count)
{
   rt_island_mgr_t *mgr = rt_island_model_mgr(m);
   if (mgr == NULL || mgr->sig_map == NULL || sig == NULL || count == 0)
      return false;

   island_spec_pin_sig(mgr, sig, RT_ISLAND_DEOPT_PUT);
   island_wb_cur = WB_VPI_PUT;
   island_break_aliases(m, mgr, sig);
   rt_island_sig_t *owners = hash_get(mgr->sig_map, sig);
   if (owners == NULL)
      return false;

   bool any_hit = false;
   bool full_cover = false;
   for (rt_island_sig_t *s = owners; s != NULL; s = s->next_owner) {
      const uint32_t s_start = (uint32_t)s->sig_offset;
      const uint32_t s_end = s_start + s->width * s->nelems;
      const uint32_t r_start = MAX(offset, s_start);
      const uint32_t r_end = MIN(offset + count, s_end);
      if (r_start >= r_end)
         continue;

      any_hit = true;
      if (r_start == offset && r_end == offset + count)
         full_cover = true;

      s->flags &= ~RT_ISIG_LAST_OUT_VALID;
      rt_island_t *isl = s->isl;
      if (!isl->has_forces)
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
      island_act_invalidate(isl);
      // VHDL signals restore their underlying driving value upon release
      // (IEEE 1076), whereas SV variables retain the forced value until the
      // next procedural assignment (IEEE 1800-2017 10.6.2).
      const bool restore_saved = !(s->flags & RT_ISIG_COMB_WRITTEN)
         && !!(s->flags & RT_ISIG_VHDL);
      uint8_t *saved = NULL;
      if (restore_saved && island_defer_vhdl_put(m, s)
          && !(s->flags & RT_ISIG_DIRECT_LINK))
         saved = island_save_sig_state(isl, s);
      bool released_any = false;
      if (s->flags & RT_ISIG_STD_LOGIC9) {
         const size_t elem_sz = s->elem_size;
         uint8_t *dst = isl->state + s->a_off;
         uint8_t *ndst = (isl->next_state != NULL
                          && !(s->flags & RT_ISIG_SPARSE_RAM))
            ? isl->next_state + s->a_off : NULL;
         const uint8_t *sa = (const uint8_t *)f->saved_a;
         for (uint32_t k = r_start - s_start; k < r_end - s_start; k++) {
            const uint64_t mbit = UINT64_C(1) << (k & 63u);
            if (f->mask[k >> 6] & mbit) {
               released_any = true;
               if (restore_saved) {
                  memcpy(dst + (size_t)k * elem_sz, sa + (size_t)k * elem_sz,
                         elem_sz);
                  if (ndst != NULL)
                     memcpy(ndst + (size_t)k * elem_sz,
                            sa + (size_t)k * elem_sz, elem_sz);
               }
               f->mask[k >> 6] &= ~mbit;
            }
         }
         const uint32_t mask_words = (s->width * s->nelems + 63u) / 64u;
         bool any = false;
         for (uint32_t w = 0; w < mask_words && !any; w++)
            any = (f->mask[w] != 0);
         if (!any) {
            free(f->mask);
            if ((uint32_t)fidx + 1u < isl->n_forces)
               isl->forces[fidx] = isl->forces[isl->n_forces - 1u];
            isl->n_forces--;
            if (isl->n_forces == 0 && isl->has_forces) {
               isl->has_forces = false;
               assert(isl->mgr->n_forced_islands > 0);
               isl->mgr->n_forced_islands--;
               island_sync_force_hdr(isl);
            }
         }
      }
      else {
         const bool two_state = !!(s->flags & RT_ISIG_TWO_STATE);
         uint64_t *ap = (uint64_t *)(isl->state + s->a_off);
         uint64_t *bp = two_state ? NULL : (uint64_t *)(isl->state + s->b_off);
         uint64_t *nap = isl->next_state != NULL
            ? (uint64_t *)(isl->next_state + s->a_off) : NULL;
         uint64_t *nbp = (nap != NULL && !two_state)
            ? (uint64_t *)(isl->next_state + s->b_off) : NULL;
         for (uint32_t k = r_start - s_start; k < r_end - s_start; k++) {
            const uint32_t e = k / s->width;
            const uint32_t lsb = s->width - 1u - (k % s->width);
            const uint32_t widx = e * s->nwords + (lsb / 64u);
            const uint64_t bit = UINT64_C(1) << (lsb & 63u);
            if (f->mask[widx] & bit) {
               released_any = true;
               if (restore_saved) {
                  ap[widx] = (ap[widx] & ~bit) | (f->saved_a[widx] & bit);
                  if (bp != NULL)
                     bp[widx] = (bp[widx] & ~bit) | (f->saved_b[widx] & bit);
                  if (nap != NULL)
                     nap[widx] = (nap[widx] & ~bit) | (f->saved_a[widx] & bit);
                  if (nbp != NULL)
                     nbp[widx] = (nbp[widx] & ~bit) | (f->saved_b[widx] & bit);
               }
               f->mask[widx] &= ~bit;
            }
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
            if (isl->n_forces == 0 && isl->has_forces) {
               isl->has_forces = false;
               assert(isl->mgr->n_forced_islands > 0);
               isl->mgr->n_forced_islands--;
               island_sync_force_hdr(isl);
            }
         }
      }

      if ((s->flags & (RT_ISIG_PROC_COMB | RT_ISIG_VHDL)) == RT_ISIG_PROC_COMB) {
         // A variable written by an always_comb block keeps the forced value
         // until the block assigns it again (IEEE 1800-2017 10.6.2)
         continue;
      }
      else if ((s->flags & RT_ISIG_COMB_WRITTEN)
          && isl->eval_handle != JIT_HANDLE_INVALID) {
         island_eval_comb_now(m, isl);
         if (s->flags & RT_ISIG_DIRECT_LINK) {
            for (uint32_t t = 0; t < s->n_direct_targets; t++) {
               rt_island_sig_t *dst_s = s->direct_targets[t];
               if (island_copy_direct_sig(s, dst_s)
                   && (dst_s->flags & RT_ISIG_COMB_READ)
                   && dst_s->isl->eval_handle != JIT_HANDLE_INVALID) {
                  island_direct_wake(dst_s->isl,
                                     !!(s->flags & RT_ISIG_DIRECT_PUB));
                  rt_island_request_delta(m);
               }
            }
         }
         else if (!(s->flags & (RT_ISIG_OUT | RT_ISIG_OBSERVED)))
            island_sync_sig_raw(isl, s, true);
      }
      else {
         if (released_any && saved != NULL) {
            island_flush_out_slice(m, isl, s, r_start - s_start,
                                   r_end - r_start);
            island_defer_sig_state(isl, s, saved);
            continue;
         }
         free(saved);
         if (released_any) {
            island_flush_out_slice(m, isl, s, r_start - s_start,
                                   r_end - r_start);
            if (s->flags & RT_ISIG_DIRECT_LINK) {
               for (uint32_t t = 0; t < s->n_direct_targets; t++) {
                  rt_island_sig_t *dst_s = s->direct_targets[t];
                  if (island_copy_direct_sig(s, dst_s)
                      && (dst_s->flags & RT_ISIG_COMB_READ)
                      && dst_s->isl->eval_handle != JIT_HANDLE_INVALID) {
                     island_direct_wake(dst_s->isl,
                                        !!(s->flags & RT_ISIG_DIRECT_PUB));
                     rt_island_request_delta(m);
                  }
               }
            }
         }
         if ((s->flags & RT_ISIG_COMB_READ)
             && isl->eval_handle != JIT_HANDLE_INVALID) {
            island_mark_dirty(isl);
            rt_island_request_delta(m);
         }
      }
   }

   if (!any_hit)
      return false;
   if (full_cover)
      return true;

   uint64_t cov_stack[16];
   uint64_t *cov = NULL, *cov_heap = NULL;
   const bool all_covered = island_build_cov_mask(owners, offset, count,
                                                  cov_stack,
                                                  ARRAY_LEN(cov_stack),
                                                  &cov, &cov_heap);
   free(cov_heap);
   return all_covered;
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

static void island_is_forced_cb(uint32_t start, uint32_t len, void *arg)
{
   (void)start;
   if (len > 0)
      *(bool *)arg = true;
}

bool rt_island_is_forced(rt_island_mgr_t *mgr, rt_signal_t *sig,
                         uint32_t offset, uint32_t count)
{
   bool forced = false;
   rt_island_report_forces(mgr, sig, offset, count, island_is_forced_cb,
                           &forced);
   return forced;
}
