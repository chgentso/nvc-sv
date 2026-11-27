//
//  Copyright (C) 2011-2026  Nick Gasson
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
#include "common.h"
#include "cov/cov-api.h"
#include "debug.h"
#include "hash.h"
#include "jit/jit-dpi.h"
#include "jit/jit-exits.h"
#include "jit/jit.h"
#include "lib.h"
#include "option.h"
#include "object.h"
#include "printf.h"
#include "psl/psl-node.h"
#include "rt/assert.h"
#include "rt/copy.h"
#include "rt/defs.h"
#include "rt/heap.h"
#include "rt/island.h"
#include "rt/model.h"
#include "rt/mtprof.h"
#include "rt/random.h"
#include "rt/structs.h"
#include "rt/sva.h"
#include "thread.h"
#include "tree.h"
#include "type.h"
#include "vcode.h"
#include "vlog/vlog-node.h"
#include "vlog/vlog-util.h"

#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

typedef struct _rt_callback rt_callback_t;

struct _rt_sampler {
   rt_sampler_t *next;
   rt_event_fn_t fn;
   void         *user;
   bool          removed;
};
typedef struct _memblock memblock_t;

typedef struct _rt_callback {
   rt_event_fn_t  fn;
   void          *user;
   rt_callback_t *next;
} rt_callback_t;

typedef enum {
   EVENT_TIMEOUT,
   EVENT_DRIVER,
   EVENT_PROCESS,
   EVENT_PSEUDO,
   EVENT_NONBLOCK,
   EVENT_PACKED_DEPOSIT,
   EVENT_PACKED_NONBLOCK,
} event_kind_t;

#define MEMBLOCK_ALIGN   64
#define MEMBLOCK_PAGE_SZ 0x800000
#define TRIGGER_TAB_MIN 64

#if ASAN_ENABLED
#define MEMBLOCK_REDZONE 16
#else
#define MEMBLOCK_REDZONE 0
#endif

typedef struct _memblock {
   memblock_t *chain;
   size_t      alloc;
   size_t      limit;
   uint8_t     data[];
} memblock_t;

STATIC_ASSERT(sizeof(memblock_t) <= MEMBLOCK_ALIGN);

typedef struct {
   waveform_t    *free_waveforms;
   tlab_t        *tlab;
   rt_wakeable_t *active_obj;
   rt_scope_t    *active_scope;
   bool           jit_guarded;
} __attribute__((aligned(64))) model_thread_t;

typedef void (*defer_fn_t)(rt_model_t *, void *);

typedef struct {
   defer_fn_t  fn;
   void       *arg;
} defer_task_t;

typedef struct {
   defer_task_t *tasks;
   unsigned      count;
   unsigned      max;
} deferq_t;

typedef struct {
   rt_signal_t *signal;
   uint32_t     offset;
   int32_t      count;
} init_event_t;

typedef A(init_event_t) init_event_list_t;
typedef A(rt_sva_batch_item_t) sva_batch_list_t;

#define DEFER_MAX 4096

typedef struct _rt_mixed_bridge {
   rt_signal_t             *sv_sig;
   rt_signal_t             *vhdl_sig;
   mixed_bridge_kind_t      kind;
   bool                     sv_is_net;
   bool                     sv_is_signed;
   uint8_t                  vhdl_size;
   struct _rt_mixed_bridge *next;
} rt_mixed_bridge_t;

typedef struct _rt_packed_force {
   uint64_t                *mask;
   uint64_t                *abits;
   uint64_t                *bbits;
   struct _rt_packed_force *next;
} rt_packed_force_t;

typedef struct _rt_packed_sched {
   rt_signal_t             *signal;
   uint32_t                 offset;
   uint32_t                 count;
   int64_t                  when;
   uint64_t                *abits;
   uint64_t                *bbits;
   uint64_t                 small_a;
   uint64_t                 small_b;
   bool                     has_b;
   bool                     nonblock;
   struct _rt_packed_sched *next;
} rt_packed_sched_t;

typedef struct {
   rt_signal_t *sig;
   uint64_t     a;
   uint64_t     b;
   uint32_t     flags;
} nb_packed_t;

typedef struct {
   rt_nexus_t    *nexus;
   unsigned char *init;
   unsigned char  buf[32];
} rt_deferred_t;

typedef struct _rt_model {
   tree_t             top;
   hash_t            *scopes;
   hash_t            *ctx_scopes;
   rt_scope_t        *root;
   mspace_t          *mspace;
   jit_t             *jit;
   rt_nexus_t        *nexuses;
   rt_nexus_t       **nexus_tail;
   delta_cycle_t      stop_delta;
   int                iteration;
   uint64_t           now;
   uint64_t           trigger_epoch;
   bool               can_create_delta;
   bool               in_reset;
   bool               next_is_delta;
   uint16_t           quiet_count;
   bool               force_stop;
   bool               sva_flush;
   bool               finished;
   bool               blocking_update;
   bool               psl_cover_assert;
   bool               has_verilog;
   bool               has_level_trigger;
   bool               skip_comb;
   bool               shuffle;
   bool               liveness;
   bool               sv_poll_mem_dirty;
   bool               sv_poll_susp_dirty;
   bool               mt_sva_enabled;
   uint32_t           sva_mt_skip;
   unsigned           ndeferred;
   unsigned           nsva_procs;
   unsigned           nfinal_procs;
   unsigned           n_signals;
   rt_proc_t        **sva_procs;
   sva_batch_list_t   sva_batch;
   sva_batch_list_t   sva_batch_tmp;
   rt_deferred_t     *deferred;
   rt_island_mgr_t   *islands;
   heap_t            *eventq_heap;
   ihash_t           *res_memo;
   rt_watch_t        *watches;
   uint32_t           proc_seq;
   deferq_t           procq;
   deferq_t           spawnq;
   deferq_t           next_procq;
   deferq_t           driverq;
   deferq_t           next_driverq;
   deferq_t           postponedq;
   deferq_t           reactiveq;
   uint64_t           wait_seq;
   uint32_t           event_seq;
   deferq_t           latepostq;
   deferq_t           triggerq;
   deferq_t           inactiveq;
   deferq_t           next_inactiveq;
   deferq_t           nonblockq;
   deferq_t           reschedq;
   heap_t            *driving_heap;
   heap_t            *effective_heap;
   rt_callback_t     *phase_cbs[END_OF_SIMULATION + 1];
   rt_sampler_t      *samplers;
   unsigned           nsamplers;
   rt_mixed_bridge_t *mixed_bridges;
   unsigned           sampler_depth;
   cover_data_t      *cover;
   nvc_rusage_t       ready_rusage;
   nvc_lock_t         memlock;
   memblock_t        *memblocks;
   model_thread_t    *threads[MAX_THREADS];
   signal_list_t      eventsigs;
   init_event_list_t  init_events;
   rt_trigger_t     **triggertab;
   unsigned           triggertab_size;
   unsigned           ntriggers;
   hash_t            *packed_forces;
   rt_packed_force_t *packed_force_list;
   rt_packed_sched_t *free_packed_sched;
   hash_t            *packed_delayed;
   struct {
      nb_packed_t *items;
      unsigned     count;
      unsigned     max;
   } nb_packed_q;
} rt_model_t;

#define FMT_VALUES_SZ   128
#define NEXUS_INDEX_MIN 8
#define TRACE_SIGNALS   1
#define WAVEFORM_CHUNK  256
#define PENDING_MIN     4
#define PENDING_INDEX_MIN 16

static void pending_build_index(rt_pending_t *p);
static void pending_clear(rt_pending_t *p, unsigned slot);
#define MAX_RANK        UINT8_MAX

#define TRACE(...) do {                                 \
      if (unlikely(__trace_on))                         \
         __model_trace(get_model(), __VA_ARGS__);       \
   } while (0)

#define MODEL_ENTRY(m)                                                  \
   rt_model_t *__save __attribute__((unused, cleanup(__model_exit)));   \
   __model_entry(m, &__save);                                           \

static __thread rt_model_t *__model = NULL;

static bool __trace_on = false;

static void *source_value(rt_nexus_t *nexus, rt_source_t *src);
static bool has_active_source(const rt_nexus_t *n);
static void free_value(rt_nexus_t *n, rt_value_t v);
static rt_nexus_t *clone_nexus(rt_model_t *m, rt_nexus_t *old, int offset);
static void put_driving(rt_model_t *m, rt_nexus_t *n, const void *value);
static void put_effective(rt_model_t *m, rt_nexus_t *n, const void *value);
static void update_driving(rt_model_t *m, rt_nexus_t *n, bool safe);
static void update_effective(rt_model_t *m, rt_nexus_t *n);
static bool run_trigger(rt_model_t *m, rt_trigger_t *t);
static void wakeup_all(rt_model_t *m, void **pending);
static void reset_scope(rt_model_t *m, rt_scope_t *s);
static void async_run_process(rt_model_t *m, void *arg);
static void reset_process(rt_model_t *m, rt_proc_t *proc);
static void reached_iteration_limit(rt_model_t *m);
typedef struct _sv_task sv_task_t;
static void sv_task_destroy(rt_model_t *m, rt_proc_t *p);
static bool sv_task_hold(rt_proc_t *p);
static bool sv_task_is_suspended(const rt_proc_t *p);
static void sv_task_note_reg(rt_proc_t *p, void **pending);
static void sv_task_drop_reg(rt_proc_t *p, void **pending);
static void sv_task_note_run(rt_proc_t *p);
static void sv_task_note_skipped_poll(rt_proc_t *p);

#define PROC_REGS_MAX 0xffff

static inline void proc_pin(rt_wakeable_t *obj)
{
   // Counts the places other than the scope that hold a pointer to a
   // process so that it is not freed while they do

   if (obj->kind == W_PROC) {
      rt_proc_t *p = container_of(obj, rt_proc_t, wakeable);
      if (p->regs != PROC_REGS_MAX)
         p->regs++;
   }
}

static inline void proc_unpin(rt_wakeable_t *obj)
{
   if (obj->kind == W_PROC) {
      rt_proc_t *p = container_of(obj, rt_proc_t, wakeable);
      if (p->regs != PROC_REGS_MAX) {
         assert(p->regs > 0);
         p->regs--;
      }
   }
}

static inline void proc_pin_forever(rt_proc_t *p)
{
   p->regs = PROC_REGS_MAX;
}
static void async_update_property(rt_model_t *m, void *arg);
static void async_update_driver(rt_model_t *m, void *arg);
static void async_fast_driver(rt_model_t *m, void *arg);
static void async_fast_all_drivers(rt_model_t *m, void *arg);
static void async_pseudo_source(rt_model_t *m, void *arg);
static rt_packed_sched_t *pop_packed_delayed(rt_model_t *m, rt_signal_t *s,
                                             bool nonblock);
static void async_packed_deposit(rt_model_t *m, void *arg);
static void async_transfer_signal(rt_model_t *m, void *arg);
static void async_run_trigger(rt_model_t *m, void *arg);

static int fmt_time_r(char *buf, size_t len, int64_t t, const char *sep)
{
   static const struct {
      int64_t time;
      const char *unit;
   } units[] = {
      { INT64_C(1), "fs" },
      { INT64_C(1000), "ps" },
      { INT64_C(1000000), "ns" },
      { INT64_C(1000000000), "us" },
      { INT64_C(1000000000000), "ms" },
      { 0, NULL }
   };

   int u = 0;
   while (units[u + 1].unit && (t % units[u + 1].time == 0))
      ++u;

   return checked_sprintf(buf, len, "%"PRIi64"%s%s",
                          t / units[u].time, sep, units[u].unit);
}

__attribute__((format(printf, 2, 3)))
static void __model_trace(rt_model_t *m, const char *fmt, ...)
{
   va_list ap;
   va_start(ap, fmt);

   static nvc_lock_t lock = 0;
   {
      SCOPED_LOCK(lock);

      ostream_t *os = nvc_stderr();
      if (m->iteration < 0)
         ostream_puts(os, "TRACE (init): ");
      else {
         char buf[64];
         fmt_time_r(buf, sizeof(buf), m->now, "");
         nvc_fprintf(os, "TRACE %s+%d: ", buf, m->iteration);
      }
      nvc_vfprintf(os, fmt, ap);
      ostream_puts(os, "\n");
      fflush(stderr);
   }

   va_end(ap);
}

static const char *trace_time(uint64_t value)
{
   static __thread char buf[2][32];
   static __thread int which = 0;

   which ^= 1;
   fmt_time_r(buf[which], 32, value, "");
   return buf[which];
}

static const char *trace_states(bit_mask_t *mask)
{
   static __thread text_buf_t *tb = NULL;

   if (tb == NULL)
      tb = tb_new();

   tb_rewind(tb);
   tb_append(tb, '{');

   size_t bit = -1;
   while (mask_iter(mask, &bit))
      tb_printf(tb, "%s%zd", tb_len(tb) > 1 ? "," : "", bit);

   tb_append(tb, '}');

   return tb_get(tb);
}

static const char *trace_nexus(rt_nexus_t *n)
{
   static __thread text_buf_t *tb = NULL;

   if (tb == NULL)
      tb = tb_new();

   tb_rewind(tb);

   if (is_signal_scope(n->signal->parent))
      tb_printf(tb, "%s.", istr(n->signal->parent->name));

   tb_istr(tb, tree_ident(n->signal->where));

   if (n->width * n->size < n->signal->shared.size) {
      tb_printf(tb, "[%d", n->offset);
      if (n->width > 1)
         tb_printf(tb, ":%d", n->offset + n->width - 1);
      tb_append(tb, ']');
   }

   return tb_get(tb);
}

static void model_diag_cb(diag_t *d, void *arg)
{
   rt_model_t *m = arg;

   if (m->iteration < 0)
      diag_printf(d, "(init): ");
   else  {
      char tmbuf[64];
      fmt_time_r(tmbuf, sizeof(tmbuf), m->now, "");

      diag_printf(d, "%s+%d: ", tmbuf, m->iteration);
   }
}

static void __model_entry(rt_model_t *m, rt_model_t **save)
{
   if (__model == NULL)
      diag_add_hint_fn(model_diag_cb, m);

   *save = __model;
   __model = m;
}

static void __model_exit(rt_model_t **save)
{
   rt_model_t *m = __model;
   assert(m != NULL);

   __model = *save;
   *save = NULL;

   if (__model == NULL)
      diag_remove_hint_fn(model_diag_cb, m);
}

static char *fmt_values_r(const void *values, size_t len, char *buf, size_t max)
{
   char *p = buf;
   const uint8_t *vptr = values;

   for (unsigned i = 0; i < len; i++) {
      if (buf + max - p <= 5) {
         checked_sprintf(p, buf + max - p, "...");
         break;
      }
      else
         p += checked_sprintf(p, buf + max - p, "%02x", *vptr++);
   }

   return buf;
}

static const char *fmt_nexus(rt_nexus_t *n, const void *values)
{
   static char buf[FMT_VALUES_SZ*2 + 2];
   return fmt_values_r(values, n->size * n->width, buf, sizeof(buf));
}

static const char *fmt_values(const void *values, uint32_t len)
{
   static char buf[FMT_VALUES_SZ*2 + 2];
   return fmt_values_r(values, len, buf, sizeof(buf));
}

static const char *fmt_jit_value(jit_scalar_t value, bool scalar, uint32_t len)
{
   static char buf[FMT_VALUES_SZ*2 + 2];
   if (scalar) {
      checked_sprintf(buf, sizeof(buf), "%"PRIx64, value.integer);
      return buf;
   }
   else
      return fmt_values_r(value.pointer, len, buf, sizeof(buf));
}

static model_thread_t *model_thread(rt_model_t *m)
{
#if RT_MULTITHREADED
   const int my_id = thread_id();

   if (unlikely(m->threads[my_id] == NULL))
      return (m->threads[my_id] = xcalloc(sizeof(model_thread_t)));

   return m->threads[my_id];
#else
   assert(thread_id() == 0);
   return m->threads[0];
#endif
}

__attribute__((cold, noinline))
static void deferq_grow(deferq_t *dq)
{
   dq->max = MAX(dq->max * 2, 64);
   dq->tasks = xrealloc_array(dq->tasks, dq->max, sizeof(defer_task_t));
}

static inline void deferq_do(deferq_t *dq, defer_fn_t fn, void *arg)
{
   if (unlikely(dq->count == dq->max))
      deferq_grow(dq);

   dq->tasks[dq->count++] = (defer_task_t){ fn, arg };
}

static void deferq_scan(deferq_t *dq, scan_fn_t fn, void *arg)
{
   for (int i = 0; i < dq->count; i++)
      (*fn)(dq->tasks[i].fn, dq->tasks[i].arg, arg);
}

static void deferq_shuffle(deferq_t *dq)
{
   int cur = dq->count;
   while (cur > 0) {
      const int swap = get_random() % cur--;
      const defer_task_t tmp = dq->tasks[cur];
      dq->tasks[cur] = dq->tasks[swap];
      dq->tasks[swap] = tmp;
   }
}

static void deferq_swap(deferq_t *a, deferq_t *b)
{
   deferq_t tmp = *a;
   *a = *b;
   *b = tmp;
}

static void run_spawned(rt_model_t *m)
{
   // Processes created by a fork statement start as soon as the parent
   // blocks (IEEE 1800-2017 9.3.2), before any other process runs.  The
   // spawn queue is first in, first out, so they start in the order they
   // were created, and those they fork in turn start after them

   for (int i = 0; i < m->spawnq.count; i++)
      (*m->spawnq.tasks[i].fn)(m, m->spawnq.tasks[i].arg);
   m->spawnq.count = 0;
}

typedef struct {
   rt_model_t *m;
   deferq_t   *dq;
   int         idx;
   int         spawn_idx;
   unsigned    resched_iters;
} deferq_guard_ctx_t;

static void flush_deferred(rt_model_t *m);

__attribute__((noinline))
static void run_spawned_ctx(rt_model_t *m, deferq_guard_ctx_t *ctx)
{
   while (ctx->spawn_idx < m->spawnq.count) {
      const int si = ctx->spawn_idx++;
      (*m->spawnq.tasks[si].fn)(m, m->spawnq.tasks[si].arg);
   }
   m->spawnq.count = 0;
   ctx->spawn_idx = 0;
}

static void deferq_run_body(void *arg)
{
   deferq_guard_ctx_t *ctx = arg;
   rt_model_t *m = ctx->m;
   deferq_t *dq = ctx->dq;

   for (;;) {
      if (unlikely(m->spawnq.count > 0))
         run_spawned_ctx(m, ctx);

      if (dq->count == 0)
         break;

      while (ctx->idx < dq->count) {
         const int i = ctx->idx++;
         if (i + 1 < dq->count)
            prefetch_read(dq->tasks[i + 1].arg);
         (*dq->tasks[i].fn)(m, dq->tasks[i].arg);
         if (unlikely(m->spawnq.count > 0))
            run_spawned_ctx(m, ctx);
      }

      dq->count = 0;
      ctx->idx = 0;

      if (m->reschedq.count == 0)
         break;

      if (unlikely(m->stop_delta > 0
                   && (uint64_t)MAX(m->iteration, 0) + ++ctx->resched_iters
                      >= (uint64_t)m->stop_delta)) {
         if (m->iteration < (int)m->stop_delta)
            m->iteration = m->stop_delta;
         reached_iteration_limit(m);
         m->reschedq.count = 0;
         break;
      }

      deferq_swap(&m->reschedq, dq);
   }
}

static void deferq_run_procs(rt_model_t *m, deferq_t *dq)
{
   if (dq->count == 0)
      return;

   model_thread_t *thread = model_thread(m);
   deferq_guard_ctx_t ctx = {
      .m = m, .dq = dq, .idx = 0, .spawn_idx = 0, .resched_iters = 0
   };

   const bool prev_guarded = thread->jit_guarded;
   thread->jit_guarded = true;

   while (!jit_run_guarded(m->jit, deferq_run_body, &ctx)) {
      m->force_stop = true;
      thread->active_obj = NULL;
      thread->active_scope = NULL;
      if (thread->tlab != NULL)
         tlab_reset(thread->tlab);
      if (unlikely(m->ndeferred > 0))
         flush_deferred(m);
   }

   thread->jit_guarded = prev_guarded;
}

static void deferq_run(rt_model_t *m, deferq_t *dq)
{
   // A Verilog process whose delay expired may already be on the reschedule
   // queue: it is run after the tasks in this queue as if it was added by one

   unsigned resched_iters = 0;
   while (dq->count > 0) {
      const defer_task_t *tasks = dq->tasks;
      const int count = dq->count;

      int i = 0;
      for (; i < count - 1; i++) {
         // Prefetch ahead the next task argument to avoid cache misses
         // when we execute it
         prefetch_read(tasks[i + 1].arg);
         (*tasks[i].fn)(m, tasks[i].arg);
         if (m->spawnq.count > 0)
            run_spawned(m);
      }
      for (; i < count; i++) {
         (*tasks[i].fn)(m, tasks[i].arg);
         if (m->spawnq.count > 0)
            run_spawned(m);
      }

      assert(dq->tasks == tasks);
      assert(dq->count == count);

      dq->count = 0;

      if (m->reschedq.count == 0)
         break;

      if (unlikely(m->stop_delta > 0
                   && (uint64_t)MAX(m->iteration, 0) + ++resched_iters
                      >= (uint64_t)m->stop_delta)) {
         if (m->iteration < (int)m->stop_delta)
            m->iteration = m->stop_delta;
         reached_iteration_limit(m);
         m->reschedq.count = 0;
         break;
      }

      deferq_swap(&m->reschedq, dq);
   }
}

static void *static_alloc(rt_model_t *m, size_t size)
{
   const int total_bytes = ALIGN_UP(size + MEMBLOCK_REDZONE, MEMBLOCK_ALIGN);

   RT_LOCK(m->memlock);

   memblock_t *mb = m->memblocks;

   if (mb == NULL || mb->alloc + total_bytes > mb->limit) {
      const size_t pagesz =
         MAX(MEMBLOCK_PAGE_SZ, total_bytes + 2 * MEMBLOCK_ALIGN);

      mb = map_huge_pages(MEMBLOCK_ALIGN, pagesz);
      mb->alloc = MEMBLOCK_ALIGN;
      mb->limit = pagesz - MEMBLOCK_ALIGN;   // Allow overreading in intrinsics

      ASAN_POISON(mb->data, pagesz - sizeof(memblock_t));

      m->memblocks = mb;
   }

   assert((mb->alloc & (MEMBLOCK_ALIGN - 1)) == 0);

   void *ptr = (void *)mb + mb->alloc;
   mb->alloc += total_bytes;

   ASAN_UNPOISON(ptr, size);
   return ptr;
}

static void run_callbacks(rt_model_t *m, model_phase_t phase)
{
   rt_callback_t *list = m->phase_cbs[phase];
   m->phase_cbs[phase] = NULL;

   if (list != NULL && phase != END_OF_SIMULATION)
      m->sv_poll_mem_dirty = true;

   for (rt_callback_t *it = list, *tmp; it; it = tmp) {
      tmp = it->next;
      if (unlikely(mtp.on)) {
         const mtp_phase_t ph = mtp_classify_fn((void *)it->fn, MTP_PHASECB);
         const mtp_phase_t prev = mtp_enter(ph);
         mtp.runs[ph]++;
         (*it->fn)(m, it->user);
         MTP_LEAVE(prev);
      }
      else
         (*it->fn)(m, it->user);
      free(it);
   }
}

static void run_samplers(rt_model_t *m)
{
   // Samplers are called once the delta cycles of a time step have
   // settled, so they see the final values of the variables which are
   // not held in signals

   m->sampler_depth++;

   for (rt_sampler_t *it = m->samplers; it; it = it->next) {
      if (!it->removed)
         (*it->fn)(m, it->user);
   }

   if (--(m->sampler_depth) > 0)
      return;

   for (rt_sampler_t **p = &(m->samplers); *p != NULL; ) {
      rt_sampler_t *it = *p;
      if (it->removed) {
         *p = it->next;
         m->nsamplers--;
         free(it);
      }
      else
         p = &(it->next);
   }
}

rt_model_t *model_new(jit_t *jit, cover_data_t *cover)
{
   rt_model_t *m = xcalloc(sizeof(rt_model_t));
   m->scopes      = hash_new(256);
   m->ctx_scopes  = hash_new(256);
   m->mspace      = jit_get_mspace(jit);
   m->jit         = jit;
   m->nexus_tail  = &(m->nexuses);
   m->iteration   = -1;
   m->event_seq   = 1;
   m->eventq_heap = heap_new(512);
   m->res_memo    = ihash_new(128);
   m->cover       = cover;

   m->driving_heap   = heap_new(64);
   m->effective_heap = heap_new(64);

   m->can_create_delta  = true;
   m->next_is_delta     = true;
   m->sv_poll_mem_dirty = true;
   m->sv_poll_susp_dirty = false;

   m->threads[thread_id()] = static_alloc(m, sizeof(model_thread_t));

   __trace_on = opt_get_int(OPT_RT_TRACE);

   mtp_init();

   return m;
}

rt_model_t *get_model(void)
{
   assert(__model != NULL);
   return __model;
}

rt_model_t *get_model_or_null(void)
{
   return __model;
}

void model_mark_sv_poll_mem_dirty(rt_model_t *m)
{
   m->sv_poll_mem_dirty = true;
}

static rt_wakeable_t *get_active_wakeable(void)
{
   return __model ? model_thread(__model)->active_obj : NULL;
}

rt_proc_t *get_active_proc(void)
{
   rt_wakeable_t *obj = get_active_wakeable();
   if (obj == NULL)
      return NULL;

   assert(obj->kind == W_PROC);
   return container_of(obj, rt_proc_t, wakeable);
}

rt_scope_t *get_active_scope(rt_model_t *m)
{
   return model_thread(m)->active_scope;
}

static void free_waveform(rt_model_t *m, waveform_t *w)
{
   model_thread_t *thread = model_thread(m);
   w->next = thread->free_waveforms;
   thread->free_waveforms = w;
}

static void cleanup_nexus(rt_model_t *m, rt_nexus_t *n)
{
   if (n->pending != NULL && pointer_tag(n->pending) == 0) {
      rt_pending_t *p = n->pending;
      free(p->index);
      free(p);
   }
}

static void cleanup_signal(rt_model_t *m, rt_signal_t *s)
{
   rt_nexus_t *n = &(s->nexus), *tmp;
   for (int i = 0; i < s->n_nexus; i++, n = tmp) {
      tmp = n->chain;
      cleanup_nexus(m, n);
   }

   free(s->index);
}

static void sv_watch_free(struct _sv_watch *w);

static void cleanup_scope(rt_model_t *m, rt_scope_t *scope)
{
   for (int i = 0; i < scope->procs.count; i++) {
      rt_proc_t *p = scope->procs.items[i];
      mptr_free(m->mspace, &(p->privdata));
      tlab_release(m->jit, p->tlab);
      if (p->task != NULL)
         sv_task_destroy(m, p);
      sv_watch_free(p->watch);
      free(p);
   }
   ACLEAR(scope->procs);

   for (int i = 0; i < scope->signals.count; i++)
      cleanup_signal(m, scope->signals.items[i]);
   ACLEAR(scope->signals);

   for (int i = 0; i < scope->aliases.count; i++)
      free(scope->aliases.items[i]);
   ACLEAR(scope->aliases);

   for (int i = 0; i < scope->properties.count; i++) {
      rt_prop_t *p = scope->properties.items[i];
      mask_free(&p->state);
      mask_free(&p->newstate);
      mptr_free(m->mspace, &(p->privdata));
      free(p);
   }
   ACLEAR(scope->properties);

   for (int i = 0; i < scope->children.count; i++)
      cleanup_scope(m, scope->children.items[i]);
   ACLEAR(scope->children);

   mptr_free(m->mspace, &(scope->privdata));
   free(scope);
}

static void sv_poll_free(void);

void model_free(rt_model_t *m)
{
   sv_poll_free();
   for (rt_mixed_bridge_t *b = m->mixed_bridges, *tmp; b; b = tmp) {
      tmp = b->next;
      free(b);
   }
   m->mixed_bridges = NULL;
   free(m->sva_procs);
   free(m->deferred);
   free(m->triggertab);

   for (rt_packed_force_t *pf = m->packed_force_list, *tmp; pf; pf = tmp) {
      tmp = pf->next;
      free(pf);
   }
   m->packed_force_list = NULL;
   if (m->packed_forces != NULL) {
      hash_free(m->packed_forces);
      m->packed_forces = NULL;
   }
   for (rt_packed_sched_t *ps = m->free_packed_sched, *tmp; ps; ps = tmp) {
      tmp = ps->next;
      free(ps);
   }
   m->free_packed_sched = NULL;

   if (opt_get_int(OPT_RT_STATS)) {
      nvc_rusage_t ru;
      nvc_rusage(&ru);

      unsigned mem = 0;
      for (memblock_t *mb = m->memblocks; mb; mb = mb->chain)
         mem += mb->alloc;

      unsigned nfuncs, nirfuncs;
      size_t irbytes;
      jit_get_stats(m->jit, &nfuncs, &nirfuncs, &irbytes);

      notef("setup:%ums run:%ums user:%ums sys:%ums maxrss:%ukB static:%ukB "
            "funcs:%u compiled:%u ir:%zukB", m->ready_rusage.ms, ru.ms,
            ru.user, ru.sys, ru.rss, mem / 1024, nfuncs, nirfuncs,
            irbytes / 1024);
   }

   mtp_report(m->root);

   while (heap_size(m->eventq_heap) > 0) {
      void *e = heap_extract_min(m->eventq_heap);
      if (pointer_tag(e) == EVENT_TIMEOUT)
         free(untag_pointer(e, rt_callback_t));
      else if (pointer_tag(e) == EVENT_PACKED_DEPOSIT
               || pointer_tag(e) == EVENT_PACKED_NONBLOCK) {
         rt_signal_t *s = untag_pointer(e, rt_signal_t);
         const bool nb = pointer_tag(e) == EVENT_PACKED_NONBLOCK;
         rt_packed_sched_t *ps = pop_packed_delayed(m, s, nb);
         free(ps->abits);
         free(ps);
      }
   }
   if (m->packed_delayed != NULL) {
      hash_free(m->packed_delayed);
      m->packed_delayed = NULL;
   }

   ACLEAR(m->sva_batch);
   ACLEAR(m->sva_batch_tmp);

   rt_island_mgr_free(m->islands);
   m->islands = NULL;

   if (m->root != NULL)
      cleanup_scope(m, m->root);

   for (int i = 0; i < MAX_THREADS; i++) {
      model_thread_t *thread = m->threads[i];
      if (thread != NULL)
         tlab_release(m->jit, thread->tlab);
   }

   while (m->samplers != NULL) {
      rt_sampler_t *next = m->samplers->next;
      free(m->samplers);
      m->samplers = next;
   }

   free(m->procq.tasks);
   free(m->spawnq.tasks);
   free(m->next_procq.tasks);
   free(m->postponedq.tasks);
   free(m->reactiveq.tasks);
   free(m->latepostq.tasks);
   free(m->triggerq.tasks);
   free(m->inactiveq.tasks);
   free(m->next_inactiveq.tasks);
   free(m->nonblockq.tasks);
   free(m->reschedq.tasks);
   free(m->nb_packed_q.items);
   free(m->driverq.tasks);
   free(m->next_driverq.tasks);

   for (rt_watch_t *it = m->watches, *tmp; it; it = tmp) {
      tmp = it->chain_all;
      free(it);
   }

   for (int i = 0; i < ARRAY_LEN(m->phase_cbs); i++) {
      for (rt_callback_t *it = m->phase_cbs[i], *tmp; it; it = tmp) {
         tmp = it->next;
         free(it);
      }
   }

   for (memblock_t *mb = m->memblocks, *tmp; mb; mb = tmp) {
      tmp = mb->chain;
      nvc_munmap(mb, mb->limit + MEMBLOCK_ALIGN);
   }

   heap_free(m->effective_heap);
   heap_free(m->driving_heap);
   heap_free(m->eventq_heap);
   ename_free_cache(m);
   hash_free(m->ctx_scopes);
   hash_free(m->scopes);
   ihash_free(m->res_memo);
   ACLEAR(m->eventsigs);
   free(m);
}

bool is_signal_scope(rt_scope_t *s)
{
   return s->kind == SCOPE_RECORD || s->kind == SCOPE_ARRAY;
}

rt_signal_t *find_signal(rt_scope_t *scope, tree_t decl, uint32_t *offset)
{
   for (int i = 0; i < scope->signals.count; i++) {
      if (scope->signals.items[i]->where == decl) {
         if (offset != NULL)
            *offset = 0;
         return scope->signals.items[i];
      }
   }

   for (int i = 0; i < scope->aliases.count; i++) {
      if (scope->aliases.items[i]->where == decl) {
         if (offset != NULL)
            *offset = scope->aliases.items[i]->offset;
         return scope->aliases.items[i]->signal;
      }
   }

   return NULL;
}

rt_proc_t *find_proc(rt_scope_t *scope, tree_t proc)
{
   for (int i = 0; i < scope->procs.count; i++) {
      if (scope->procs.items[i]->where == proc)
         return scope->procs.items[i];
   }

   return NULL;
}

rt_watch_t *find_watch(rt_nexus_t *n, sig_event_fn_t fn)
{
   if (n->pending == NULL)
      return NULL;
   else if (pointer_tag(n->pending) == 1) {
      rt_wakeable_t *obj = untag_pointer(n->pending, rt_wakeable_t);
      if (obj->kind == W_WATCH) {
         rt_watch_t *w = container_of(obj, rt_watch_t, wakeable);
         if (w->fn == fn)
            return w;
      }

      return NULL;
   }
   else {
      rt_pending_t *p = untag_pointer(n->pending, rt_pending_t);

      for (int i = 0; i < p->count; i++) {
         if (p->wake[i] == NULL)
            continue;
         rt_wakeable_t *obj = untag_pointer(p->wake[i], rt_wakeable_t);
         if (obj->kind == W_WATCH) {
            rt_watch_t *w = container_of(obj, rt_watch_t, wakeable);
            if (w->fn == fn)
               return w;
         }
      }

      return NULL;
   }
}

rt_scope_t *create_scope(rt_model_t *m, tree_t block, rt_scope_t *parent)
{
   if (parent == NULL) {
      assert(m->top == NULL);
      assert(tree_kind(block) == T_ELAB);

      m->top = block;

      m->root = xcalloc(sizeof(rt_scope_t));
      m->root->kind     = SCOPE_ROOT;
      m->root->where    = block;
      m->root->privdata = MPTR_INVALID;
      m->root->name     = lib_name(lib_work());

      return m->root;
   }
   else {
      rt_scope_t *s = xcalloc(sizeof(rt_scope_t));
      s->where    = block;
      s->kind     = SCOPE_INSTANCE;
      s->privdata = mptr_new(m->mspace, "block privdata");
      s->parent   = parent;
      s->name     = ident_prefix(parent->name, tree_ident(block), '.');

      APUSH(parent->children, s);

      hash_put(m->scopes, block, s);

      MODEL_ENTRY(m);

      TRACE("initialise scope %s", istr(s->name));

      model_thread_t *thread = model_thread(m);
      thread->active_scope = s;

      jit_handle_t handle = jit_lazy_compile(m->jit, s->name);
      if (handle == JIT_HANDLE_INVALID)
         fatal_trace("failed to compile %s", istr(s->name));

      jit_scalar_t result, context = { .pointer = NULL };
      jit_scalar_t p2 = { .integer = 0 };

      if (s->parent->kind != SCOPE_ROOT) {
         context.pointer = *mptr_get(s->parent->privdata);

         if (context.pointer == NULL) {
            // The parent could not be created so this cannot be either
            m->force_stop = true;
            thread->active_scope = NULL;
            return s;
         }
      }

      tlab_t tlab = jit_null_tlab(m->jit);

      if (jit_fastcall(m->jit, handle, &result, context, p2, &tlab)) {
         *mptr_get(s->privdata) = result.pointer;
         if (result.pointer != NULL)
            hash_put(m->ctx_scopes, result.pointer, s);
      }
      else
         m->force_stop = true;

      assert(thread->active_scope == s);
      thread->active_scope = NULL;
      return s;
   }
}

static void *island_init_noop(jit_scalar_t *args, void *arg)
{
   return NULL;
}

void model_run_island_init(rt_model_t *m, rt_scope_t *s, vcode_unit_t thunk)
{
   if (thunk == NULL)
      return;

   void *ctx = (s != NULL) ? *mptr_get(s->privdata) : NULL;
   if (ctx != NULL) {
      MODEL_ENTRY(m);
      model_thread_t *thread = model_thread(m);
      rt_scope_t *saved = thread->active_scope;
      thread->active_scope = s;
      jit_call_thunk(m->jit, thunk, ctx, island_init_noop, NULL);
      thread->active_scope = saved;
   }
   vcode_unit_unref(thunk);
}

rt_scope_t *find_scope(rt_model_t *m, tree_t container)
{
   return hash_get(m->scopes, container);
}

rt_scope_t *find_scope_by_context(rt_model_t *m, void *ctx)
{
   return ctx != NULL ? hash_get(m->ctx_scopes, ctx) : NULL;
}

rt_scope_t *root_scope(rt_model_t *m)
{
   return m->root;
}

rt_scope_t *child_scope(rt_scope_t *scope, tree_t decl)
{
   for (int i = 0; i < scope->children.count; i++) {
      rt_scope_t *s = scope->children.items[i];
      if (s->where == decl)
         return s;
   }

   return NULL;
}

rt_scope_t *child_scope_at(rt_scope_t *scope, int index)
{
   return AGET(scope->children, index);
}

const void *signal_value(rt_signal_t *s)
{
   if (unlikely(s->shared.flags & SIG_F_ISLAND)) {
      rt_model_t *m = get_model_or_null();
      if (m != NULL)
         rt_island_sync_if_unobserved(m, s);
   }
   return s->shared.data;
}

const void *signal_last_value(rt_signal_t *s)
{
   return s->shared.data + s->shared.size;
}

uint8_t signal_size(rt_signal_t *s)
{
   return s->nexus.size;
}

uint32_t signal_width(rt_signal_t *s)
{
   if (s->shared.flags & SIG_F_PACKED)
      return s->bit_width;

   return s->shared.size / s->nexus.size;
}

size_t signal_expand(rt_signal_t *s, uint64_t *buf, size_t max)
{
   if (unlikely(s->shared.flags & SIG_F_ISLAND)) {
      rt_model_t *m = get_model_or_null();
      if (m != NULL)
         rt_island_sync_if_unobserved(m, s);
   }
   if (s->shared.flags & SIG_F_PACKED) {
      const uint32_t total = s->bit_width;
      const int nwords = (total + 63) / 64;
      const uint64_t *abits = (const uint64_t *)s->shared.data;
      const uint64_t *bbits = (s->shared.flags & SIG_F_PACKED4)
         ? abits + nwords : NULL;
      for (uint32_t i = 0; i < max && i < total; i++) {
         const uint32_t bit = total - 1 - i;
         const uint64_t a = (abits[bit / 64] >> (bit % 64)) & 1;
         const uint64_t b = bbits ? ((bbits[bit / 64] >> (bit % 64)) & 1) : 0;
         buf[i] = (b << 1) | a;
      }
      return total;
   }

   const size_t total = s->shared.size / s->nexus.size;

#define SIGNAL_READ_EXPAND_U64(type) do {                               \
      const type *sp = (type *)s->shared.data;                          \
      for (int i = 0; i < max && i < total; i++)                        \
         buf[i] = sp[i];                                                \
   } while (0)

   FOR_ALL_SIZES(s->nexus.size, SIGNAL_READ_EXPAND_U64);

   return total;
}

static inline void set_pending(rt_wakeable_t *wake)
{
   assert(!wake->pending);
   assert(!wake->delayed);
   wake->pending = true;
}

typedef struct {
   rt_proc_t *proc;
   uint64_t   when;
} stale_event_t;

// Timed wake ups of processes which were interrupted and must be ignored
static A(stale_event_t) stale_events = AINIT;

static bool stale_event_pop(rt_proc_t *proc, uint64_t when)
{
   for (int i = 0; i < stale_events.count; i++) {
      if (stale_events.items[i].proc == proc
          && stale_events.items[i].when == when) {
         stale_events.items[i] = stale_events.items[--stale_events.count];
         return true;
      }
   }

   return false;
}

static void deltaq_insert_proc(rt_model_t *m, uint64_t delta, rt_proc_t *proc)
{
   if (delta == 0) {
      set_pending(&proc->wakeable);
      deferq_do(&m->procq, async_run_process, proc);
      m->next_is_delta = true;
   }
   else {
      assert(!proc->wakeable.delayed);
      proc->wakeable.delayed = true;
      proc->wake_at = m->now + delta;

      void *e = tag_pointer(proc, EVENT_PROCESS);
      if (tree_kind(proc->where) == T_VERILOG)
         heap_insert_ordered(m->eventq_heap, m->now + delta, e);
      else
         heap_insert(m->eventq_heap, m->now + delta, e);
   }
}

static void deltaq_insert_driver(rt_model_t *m, uint64_t delta,
                                 rt_source_t *source)
{
   if (delta == 0) {
      deferq_do(&m->driverq, async_update_driver, source);
      m->next_is_delta = true;
   }
   else {
      void *e = tag_pointer(source, EVENT_DRIVER);
      heap_insert(m->eventq_heap, m->now + delta, e);
   }
}

static void deltaq_insert_pseudo_source(rt_model_t *m, uint64_t delta,
                                        rt_source_t *src, bool nonblock)
{
   if (delta == 0) {
      deferq_do(&m->driverq, async_pseudo_source, src);
      m->next_is_delta = true;
   }
   else {
      void *e = tag_pointer(src, nonblock ? EVENT_NONBLOCK : EVENT_PSEUDO);
      heap_insert(m->eventq_heap, m->now + delta, e);
   }
}

static void reset_process_state(rt_model_t *m, rt_proc_t *proc)
{
   TRACE("reset process %s", istr(proc->name));

   assert(proc->tlab == NULL);
   assert(model_thread(m)->tlab == NULL);   // Not used during reset

   model_thread_t *thread = model_thread(m);
   thread->active_obj = &(proc->wakeable);
   thread->active_scope = proc->scope;

   if (proc->closure.args[0].pointer == NULL)
      proc->closure.args[0].pointer = *mptr_get(proc->scope->privdata);

   jit_scalar_t state = { .pointer = NULL }, result;

   tlab_t tlab = jit_null_tlab(m->jit);

   if (jit_call_closure(m->jit, &proc->closure, &result, state, &tlab)) {
      *mptr_get(proc->privdata) = result.pointer;
      if (unlikely(proc->verilog) && unlikely(!proc->signal_only_known)) {
         proc->signal_only = !proc->wakeable.forked && proc->task == NULL
            && jit_func_signal_only(m->jit, proc->closure.handle);
         proc->signal_only_known = 1;
      }
   }
   else
      m->force_stop = true;

   thread->active_obj = NULL;
   thread->active_scope = NULL;
}

static void reset_process(rt_model_t *m, rt_proc_t *proc)
{
   proc->create_seq = ++(m->proc_seq);   // The order processes start

   reset_process_state(m, proc);

   // Schedule the process to run immediately
   deferq_t *dq = proc->wakeable.reschedule ? &m->reschedq : &m->procq;
   if (proc->wakeable.reactive && !proc->wakeable.forked) {
      // The initial procedures of a program start in the Reactive region
      dq = &m->reactiveq;
      proc->wake_iter = m->iteration;
   }
   deferq_do(dq, async_run_process, proc);
   set_pending(&proc->wakeable);
}

static void reset_property(rt_model_t *m, rt_prop_t *prop)
{
   TRACE("reset property %s", istr(prop->name));

   assert(model_thread(m)->tlab == NULL);   // Not used during reset

   model_thread_t *thread = model_thread(m);
   thread->active_obj = &(prop->wakeable);
   thread->active_scope = prop->scope;

   tlab_t tlab = jit_null_tlab(m->jit);

   jit_scalar_t args[] = {
      { .pointer = NULL },
      { .pointer = *mptr_get(prop->scope->privdata) },
      { .integer = -1 },
   }, results[2];

   if (jit_vfastcall(m->jit, prop->handle, args, ARRAY_LEN(args),
                     results, ARRAY_LEN(results), &tlab))
      *mptr_get(prop->privdata) = results[0].pointer;
   else
      m->force_stop = true;

   TRACE("needs %"PRIi64" state bits", results[1].integer);

   mask_init(&prop->state, results[1].integer);
   mask_init(&prop->newstate, results[1].integer);

   mask_set(&prop->state, 0);
   mask_set(&prop->state, results[1].integer - 1);   // Update prev() variables

   thread->active_obj = NULL;
   thread->active_scope = NULL;
}

#define SV_POLL_HAS_SIG   (1 << 0)
#define SV_POLL_HAS_DEFER (1 << 1)

// Processes waiting for a condition on state which is not a signal
typedef struct {
   rt_proc_t  *proc;
   const void *ptr0;
   uint64_t    snap0;
   uint64_t    snap1;
   uint8_t     sz0;
   uint8_t     flags;
   uint16_t    count;
} sv_watch_poller_t;

typedef struct {
   rt_proc_t **procs;
   uint32_t    count;
   uint32_t    cap;
   uint64_t    dirty_gen;
} sv_sig_subs_t;

static A(rt_proc_t *)        sv_pollers = AINIT;
static A(sv_watch_poller_t)  sv_watched_mem = AINIT;
static A(rt_proc_t *)        sv_watched_sig_only = AINIT;
static A(rt_proc_t *)        sv_watched_defer = AINIT;
static A(rt_proc_t *)        sv_sig_dirty_q = AINIT;
static A(rt_proc_t *)        sv_wake_buf = AINIT;
static hash_t               *sv_sig_subs = NULL;
static unsigned              sv_watched_sig_total = 0;
static uint64_t              sv_sig_dirty_gen = 1;
static unsigned              sv_pollers_woken = 0;
static unsigned              sv_pollers_kept = 0;
static unsigned              sv_pollers_fired = 0;
static unsigned              sv_procs_run = 0;
static unsigned              sv_suspended_tasks = 0;
static unsigned              sv_pending_defers = 0;

static bool sv_proc_finished(const rt_proc_t *proc);
static bool sv_task_has_pending_defers(const rt_proc_t *p);

// Forked processes which have finished and may be freed
static A(rt_proc_t *) sv_reap = AINIT;
static unsigned       sv_reap_next = 256;

static void sv_reap_procs(rt_model_t *m);

// A process handle is an index into this table so that the process can be
// freed once it has finished while old handles stay valid
typedef struct {
   sv_task_t *task;
   bool       killed;
} sv_handle_t;

static A(sv_handle_t) sv_handles = AINIT;

static void note_wait(rt_model_t *m, rt_proc_t *proc);

#define SV_WATCH_INLINE 8
#define SV_WATCH_BYTES  32

typedef struct {
   const void  *ptr;
   rt_signal_t *sig;
   unsigned     size;
   bool         is_sig;
   union {
      uint8_t  snap[SV_WATCH_BYTES];
      uint8_t *ext;
   };
} sv_watch_item_t;

typedef struct _sv_watch {
   unsigned         count;
   unsigned         max;
   uint8_t          has_sig : 1;
   uint8_t          has_mem : 1;
   uint8_t          in_watched : 1;
   uint8_t          in_dirty_q : 1;
   uint8_t          has_defer : 1;
   uint8_t          sz0;
   uint32_t         list_idx;
   uint32_t         defer_idx;
   uint32_t         dirty_idx;
   const void      *ptr0;
   uint64_t         snap0;
   uint64_t         snap1;
   sv_watch_item_t *items;
   sv_watch_item_t  inline_items[SV_WATCH_INLINE];
} sv_watch_t;

// Memory read by a condition the process polls, collected until it waits
static sv_watch_t sv_watch_pending;
static bool       sv_watch_never = false;

static void sv_watch_clear(sv_watch_t *w)
{
   if (w == NULL)
      return;

   for (unsigned i = 0; i < w->count; i++) {
      if (w->items[i].size > SV_WATCH_BYTES)
         free(w->items[i].ext);
   }
   w->count = 0;
   w->has_sig = 0;
   w->has_mem = 0;
   w->in_watched = 0;
   w->in_dirty_q = 0;
   w->has_defer = 0;
   w->sz0 = 0;
}

static void sv_watch_free(sv_watch_t *w)
{
   if (w == NULL)
      return;

   sv_watch_clear(w);
   if (w->items != NULL && w->items != w->inline_items)
      free(w->items);
   free(w);
}

static inline bool sv_watch_item_eq(const void *a, const void *b, unsigned sz)
{
   switch (sz) {
   case 1:
      return *(const uint8_t *)a == *(const uint8_t *)b;
   case 2:
      {
         uint16_t x, y;
         memcpy(&x, a, 2);
         memcpy(&y, b, 2);
         return x == y;
      }
   case 4:
      {
         uint32_t x, y;
         memcpy(&x, a, 4);
         memcpy(&y, b, 4);
         return x == y;
      }
   case 8:
      {
         uint64_t x, y;
         memcpy(&x, a, 8);
         memcpy(&y, b, 8);
         return x == y;
      }
   case 16:
      {
         uint64_t x0, x1, y0, y1;
         memcpy(&x0, a, 8);
         memcpy(&x1, (const uint8_t *)a + 8, 8);
         memcpy(&y0, b, 8);
         memcpy(&y1, (const uint8_t *)b + 8, 8);
         return ((x0 ^ y0) | (x1 ^ y1)) == 0;
      }
   case 24:
      {
         uint64_t x0, x1, x2, y0, y1, y2;
         memcpy(&x0, a, 8);
         memcpy(&x1, (const uint8_t *)a + 8, 8);
         memcpy(&x2, (const uint8_t *)a + 16, 8);
         memcpy(&y0, b, 8);
         memcpy(&y1, (const uint8_t *)b + 8, 8);
         memcpy(&y2, (const uint8_t *)b + 16, 8);
         return ((x0 ^ y0) | (x1 ^ y1) | (x2 ^ y2)) == 0;
      }
   default:
      return memcmp(a, b, sz) == 0;
   }
}

static inline bool sv_watch_item_unchanged(const sv_watch_item_t *item)
{
   const void *snap = item->size <= SV_WATCH_BYTES ? item->snap : item->ext;
   return sv_watch_item_eq(item->ptr, snap, item->size);
}

static inline bool sv_watch_unchanged(const sv_watch_t *w)
{
   for (unsigned i = 0; i < w->count; i++) {
      if (!sv_watch_item_unchanged(&w->items[i]))
         return false;
   }

   return true;
}

static inline bool sv_inline_eq_val(const void *ptr, uint64_t snap0,
                                    uint64_t snap1, uint8_t sz)
{
   switch (sz) {
   case 1:
      return *(const uint8_t *)ptr == (uint8_t)snap0;
   case 2:
      {
         uint16_t x;
         memcpy(&x, ptr, 2);
         return x == (uint16_t)snap0;
      }
   case 4:
      {
         uint32_t x;
         memcpy(&x, ptr, 4);
         return x == (uint32_t)snap0;
      }
   case 8:
      {
         uint64_t x;
         memcpy(&x, ptr, 8);
         return x == snap0;
      }
   case 16:
      {
         uint64_t x0, x1;
         memcpy(&x0, ptr, 8);
         memcpy(&x1, (const uint8_t *)ptr + 8, 8);
         return ((x0 ^ snap0) | (x1 ^ snap1)) == 0;
      }
   default:
      return false;
   }
}

static inline bool sv_poller_inline_eq(const sv_watch_poller_t *e)
{
   return sv_inline_eq_val(e->ptr0, e->snap0, e->snap1, e->sz0);
}

static inline bool sv_watch_fast_unchanged(const sv_watch_t *w)
{
   if (likely(w->sz0 != 0)) {
      if (!sv_inline_eq_val(w->ptr0, w->snap0, w->snap1, w->sz0))
         return false;
      for (unsigned j = 1; j < w->count; j++) {
         if (!sv_watch_item_unchanged(&w->items[j]))
            return false;
      }
      return true;
   }

   return sv_watch_unchanged(w);
}

static void sv_sig_subscribe(rt_signal_t *sig, rt_proc_t *proc)
{
   if (unlikely(sv_sig_subs == NULL))
      sv_sig_subs = hash_new(64);

   sv_sig_subs_t *subs = hash_get(sv_sig_subs, sig);
   if (subs == NULL) {
      subs = xcalloc(sizeof(sv_sig_subs_t));
      subs->cap = 4;
      subs->procs = xmalloc_array(subs->cap, sizeof(rt_proc_t *));
      hash_put(sv_sig_subs, sig, subs);
   }
   else if (subs->count == subs->cap) {
      subs->cap *= 2;
      subs->procs = xrealloc_array(subs->procs, subs->cap,
                                   sizeof(rt_proc_t *));
   }

   subs->procs[subs->count++] = proc;
   subs->dirty_gen = 0;
}

static void sv_sig_unsubscribe(rt_signal_t *sig, rt_proc_t *proc)
{
   if (unlikely(sv_sig_subs == NULL))
      return;

   sv_sig_subs_t *subs = hash_get(sv_sig_subs, sig);
   if (subs == NULL)
      return;

   for (uint32_t i = subs->count; i > 0; i--) {
      if (subs->procs[i - 1] == proc) {
         subs->procs[i - 1] = subs->procs[--subs->count];
         return;
      }
   }
}

static void sv_poll_notify_sig_slow(rt_signal_t *s)
{
   if (unlikely(sv_sig_subs == NULL))
      return;

   sv_sig_subs_t *subs = hash_get(sv_sig_subs, s);
   if (subs == NULL || subs->count == 0
       || subs->dirty_gen == sv_sig_dirty_gen)
      return;

   subs->dirty_gen = sv_sig_dirty_gen;

   for (uint32_t i = 0; i < subs->count; i++) {
      rt_proc_t *p = subs->procs[i];
      sv_watch_t *w = p->watch;
      if (!w->in_dirty_q) {
         w->in_dirty_q = 1;
         w->dirty_idx = sv_sig_dirty_q.count;
         APUSH(sv_sig_dirty_q, p);
      }
   }
}

static inline void sv_poll_notify_sig(rt_signal_t *s)
{
   if (unlikely(sv_watched_sig_total > 0)
       && (s->shared.flags & SIG_F_POLL_WATCHED))
      sv_poll_notify_sig_slow(s);
}

static inline void sv_watch_poller_push(rt_proc_t *proc)
{
   sv_watch_t *w = proc->watch;
   const rt_wakeable_t *obj = &(proc->wakeable);
   if (unlikely(w == NULL || w->count == 0
                || (obj->forked && obj->zombie)
                || obj->interrupted || obj->pending
                || obj->trigger != NULL)) {
      APUSH(sv_pollers, proc);
      return;
   }

   assert(!w->in_watched);
   assert(!w->in_dirty_q);

   const unsigned sz0 = w->items[0].size;
   if (sz0 == 1 || sz0 == 2 || sz0 == 4 || sz0 == 8) {
      w->ptr0 = w->items[0].ptr;
      w->sz0  = sz0;
      memcpy(&w->snap0, w->items[0].snap, sz0);
   }
   else if (sz0 == 16) {
      w->ptr0 = w->items[0].ptr;
      w->sz0  = 16;
      memcpy(&w->snap0, w->items[0].snap, 8);
      memcpy(&w->snap1, w->items[0].snap + 8, 8);
   }
   else
      w->sz0 = 0;

   w->in_watched = 1;
   w->in_dirty_q = 0;

   const bool has_defer = sv_task_has_pending_defers(proc);
   w->has_defer = has_defer ? 1 : 0;
   if (unlikely(has_defer)) {
      w->defer_idx = sv_watched_defer.count;
      APUSH(sv_watched_defer, proc);
   }

   if (w->has_sig) {
      for (unsigned i = 0; i < w->count; i++) {
         rt_signal_t *sig = w->items[i].sig;
         if (sig == NULL)
            continue;
         bool dup = false;
         for (unsigned j = 0; j < i; j++) {
            if (w->items[j].sig == sig) {
               dup = true;
               break;
            }
         }
         if (!dup)
            sv_sig_subscribe(sig, proc);
      }
      sv_watched_sig_total++;
   }

   if (unlikely(sv_task_is_suspended(proc)))
      get_model()->sv_poll_susp_dirty = true;

   if (w->has_mem) {
      sv_watch_poller_t e = {
         .proc  = proc,
         .ptr0  = w->ptr0,
         .snap0 = w->snap0,
         .snap1 = w->snap1,
         .sz0   = w->sz0,
         .flags = (w->has_sig ? SV_POLL_HAS_SIG : 0)
            | (has_defer ? SV_POLL_HAS_DEFER : 0),
         .count = MIN(w->count, UINT16_MAX),
      };
      w->list_idx = sv_watched_mem.count;
      APUSH(sv_watched_mem, e);
   }
   else {
      assert(w->has_sig);
      w->list_idx = sv_watched_sig_only.count;
      APUSH(sv_watched_sig_only, proc);
   }
}

static void sv_watch_poller_unlink(rt_proc_t *proc)
{
   sv_watch_t *w = proc->watch;
   assert(w != NULL && w->in_watched);

   w->in_watched = 0;

   if (w->in_dirty_q) {
      const uint32_t idx = w->dirty_idx;
      assert(idx < sv_sig_dirty_q.count && sv_sig_dirty_q.items[idx] == proc);
      rt_proc_t *last = sv_sig_dirty_q.items[--sv_sig_dirty_q.count];
      if (idx < sv_sig_dirty_q.count) {
         sv_sig_dirty_q.items[idx] = last;
         last->watch->dirty_idx = idx;
      }
      w->in_dirty_q = 0;
   }

   if (w->has_defer) {
      const uint32_t idx = w->defer_idx;
      assert(idx < sv_watched_defer.count && sv_watched_defer.items[idx] == proc);
      rt_proc_t *last = sv_watched_defer.items[--sv_watched_defer.count];
      if (idx < sv_watched_defer.count) {
         sv_watched_defer.items[idx] = last;
         last->watch->defer_idx = idx;
      }
      w->has_defer = 0;
   }

   if (w->has_mem) {
      const uint32_t idx = w->list_idx;
      assert(idx < sv_watched_mem.count && sv_watched_mem.items[idx].proc == proc);
      const sv_watch_poller_t last = sv_watched_mem.items[--sv_watched_mem.count];
      if (idx < sv_watched_mem.count) {
         sv_watched_mem.items[idx] = last;
         last.proc->watch->list_idx = idx;
      }
   }
   else {
      const uint32_t idx = w->list_idx;
      assert(idx < sv_watched_sig_only.count
             && sv_watched_sig_only.items[idx] == proc);
      rt_proc_t *last = sv_watched_sig_only.items[--sv_watched_sig_only.count];
      if (idx < sv_watched_sig_only.count) {
         sv_watched_sig_only.items[idx] = last;
         last->watch->list_idx = idx;
      }
   }

   if (w->has_sig) {
      for (unsigned i = 0; i < w->count; i++) {
         rt_signal_t *sig = w->items[i].sig;
         if (sig == NULL)
            continue;
         bool dup = false;
         for (unsigned j = 0; j < i; j++) {
            if (w->items[j].sig == sig) {
               dup = true;
               break;
            }
         }
         if (!dup)
            sv_sig_unsubscribe(sig, proc);
      }
      assert(sv_watched_sig_total > 0);
      sv_watched_sig_total--;
   }
}

static void sv_poller_remove(rt_proc_t *proc)
{
   if (proc->watch != NULL && proc->watch->in_watched) {
      sv_watch_poller_unlink(proc);
      return;
   }

   for (int i = 0; i < sv_pollers.count; i++) {
      if (sv_pollers.items[i] == proc) {
         sv_pollers.items[i] = sv_pollers.items[--sv_pollers.count];
         return;
      }
   }
}

static void flush_deferred(rt_model_t *m);

static void run_process_inner(rt_model_t *m, rt_proc_t *proc)
{
   TRACE("run %sprocess %s", *mptr_get(proc->privdata) ? "" :  "stateless ",
         istr(proc->name));

   rt_wakeable_t *obj = &(proc->wakeable);

   if (unlikely(obj->forked && obj->zombie))
      return;   // Disabled by disable fork

   const bool was_interrupted = obj->interrupted;
   if (unlikely(was_interrupted))
      obj->interrupted = false;
   else if (obj->trigger != NULL && !run_trigger(m, obj->trigger))
      return;   // Filtered

   if (unlikely(proc->task != NULL) && sv_task_hold(proc))
      return;   // Runs once it is resumed

   if (unlikely(proc->poll_wake)) {
      proc->poll_wake = 0;

      // A poll of a condition that reads only memory which has not changed
      // since the process began waiting would find the same value and wait
      // again, so do that without running the process
      const sv_watch_t *w = proc->watch;
      if (w != NULL && w->count > 0 && !was_interrupted
          && sv_watch_unchanged(w)) {
         sv_procs_run++;
         if (unlikely(sv_pending_defers > 0) && proc->task != NULL)
            sv_task_note_skipped_poll(proc);

         note_wait(m, proc);
         sv_watch_poller_push(proc);
         return;
      }
   }

   if (unlikely(m->has_verilog))
      sv_procs_run++;

   if (unlikely(proc->task != NULL))
      sv_task_note_run(proc);

   if (unlikely(sv_unique_pending > 0))
      sv_unique_flush_proc(proc);

   model_thread_t *thread = model_thread(m);
   assert(thread->tlab != NULL);
   assert(thread->tlab->alloc == 0);

   thread->active_obj = obj;
   thread->active_scope = proc->scope;

   // Stateless processes have NULL privdata so pass a dummy pointer
   // value in so it can be distinguished from a reset
   jit_scalar_t state = {
      .pointer = *mptr_get(proc->privdata) ?: (void *)-1
   };
   jit_scalar_t result;

   const bool is_verilog = unlikely(proc->verilog);
   const uint64_t wait_seq = is_verilog ? proc->wait_seq : 0;

   tlab_t *tlab = proc->tlab ?: thread->tlab;
   assert(tlab->mem_dirty == 0);

   if (is_verilog && unlikely(!proc->signal_only_known)) {
      tlab->mem_dirty = 0;
      m->sv_poll_mem_dirty = true;
   }

   if (likely(thread->jit_guarded))
      jit_call_closure_guarded(m->jit, &proc->closure, &result, state, tlab);
   else if (!jit_call_closure(m->jit, &proc->closure, &result, state, tlab))
      m->force_stop = true;

   if (is_verilog) {
      if (unlikely(!proc->signal_only_known)) {
         proc->signal_only = !proc->wakeable.forked
            && jit_func_signal_only(m->jit, proc->closure.handle);
         proc->signal_only_known = 1;
         if (!proc->signal_only)
            m->sv_poll_mem_dirty = true;
         tlab->mem_dirty = 0;
      }
      else if (!proc->signal_only && tlab->mem_dirty) {
         m->sv_poll_mem_dirty = true;
         tlab->mem_dirty = 0;
      }

      proc->ended = !obj->forked && !obj->delayed
         && proc->wait_seq == wait_seq
         && (proc->task == NULL || sv_proc_finished(proc));
   }
   else
      tlab->mem_dirty = 0;

   if (proc->tlab != NULL && result.pointer == NULL) {
      tlab_release(m->jit, proc->tlab);
      proc->tlab = NULL;
   }
   else if (proc->tlab == NULL && result.pointer != NULL) {
      TRACE("claiming TLAB for private use (used %u/%u)",
            thread->tlab->alloc, thread->tlab->limit);
      proc->tlab = thread->tlab;
      thread->tlab = tlab_acquire(m->jit);
   }
   else if (proc->tlab == NULL)
      tlab_reset(thread->tlab);   // All data inside the TLAB is dead

   if (proc->tlab != NULL && proc->wakeable.forked && sv_proc_finished(proc)) {
      // A forked process that has finished never runs again
      tlab_release(m->jit, proc->tlab);
      proc->tlab = NULL;
   }

   thread->active_obj = NULL;
   thread->active_scope = NULL;

   if (unlikely(m->ndeferred > 0))
      flush_deferred(m);
}

static void run_process(rt_model_t *m, rt_proc_t *proc)
{
   if (likely(!mtp.on)) {
      run_process_inner(m, proc);
      return;
   }

   const mtp_phase_t ph = mtp_classify_proc(proc);
   const mtp_phase_t prev = mtp_enter(ph);
   mtp.runs[ph]++;
   mtp.cycle_procs++;
   run_process_inner(m, proc);
   MTP_LEAVE(prev);
}

static bool scope_precedes(rt_scope_t *child, rt_proc_t *proc)
{
   const loc_t *cl = tree_loc(child->where), *pl = tree_loc(proc->where);
   if (loc_invalid_p(cl) || loc_invalid_p(pl) || cl->file_ref != pl->file_ref)
      return true;
   else if (cl->first_line != pl->first_line)
      return cl->first_line < pl->first_line;
   else
      return cl->first_column <= pl->first_column;   // Same place: a macro
}

static void reset_scope(rt_model_t *m, rt_scope_t *s)
{
   // Processes start in the order of their declarations in the source
   // text, those of an instance or a generate block at the place where it
   // is declared.  IEEE 1800-2017 4.7 leaves this order unspecified

   int pi = 0, ci = 0;
   while (pi < s->procs.count && ci < s->children.count) {
      if (scope_precedes(s->children.items[ci], s->procs.items[pi]))
         reset_scope(m, s->children.items[ci++]);
      else
         reset_process(m, s->procs.items[pi++]);
   }

   for (; ci < s->children.count; ci++)
      reset_scope(m, s->children.items[ci]);

   for (; pi < s->procs.count; pi++)
      reset_process(m, s->procs.items[pi]);

   for (int i = 0; i < s->properties.count; i++)
      reset_property(m, s->properties.items[i]);
}

static res_memo_t *memo_resolution_fn(rt_model_t *m, rt_signal_t *signal,
                                      ffi_closure_t *closure, int32_t nlits,
                                      res_flags_t flags)
{
   // Optimise some common resolution functions by memoising them

   res_memo_t *memo = ihash_get(m->res_memo, closure->handle);
   if (memo != NULL)
      return memo;

   memo = static_alloc(m, sizeof(res_memo_t));
   memo->flags = flags;

   assert(closure->nargs == 1);
   ffi_copy_closure(&memo->closure, closure);

   ihash_put(m->res_memo, memo->closure.handle, memo);

   if (flags & R_BLOB) {
      // The function resolves whole values of this many elements
      memo->blobsz = MAX(nlits, 1);
      return memo;
   }

   if (nlits == 0 || nlits > 16)
      return memo;

   const vhdl_severity_t old_severity = set_exit_severity(SEVERITY_NOTE);

   const bool prev_silent = jit_set_silent(m->jit, true);

   // Memoise the function for all two value cases

   for (int i = 0; i < nlits; i++) {
      for (int j = 0; j < nlits; j++) {
         int8_t args[2] = { i, j };
         jit_scalar_t result;
         if (jit_try_call(m->jit, memo->closure.handle, &result,
                          memo->closure.args[0], args, 2)) {
            assert(result.integer < nlits && result.integer >= 0);
            memo->tab2[i][j] = result.integer;
         }
      }
   }

   // Memoise the function for all single value cases and determine if the
   // function behaves like the identity function

   bool identity = true;
   for (int i = 0; i < nlits; i++) {
      int8_t args[1] = { i };
      jit_scalar_t result;
      if (jit_try_call(m->jit, memo->closure.handle, &result,
                       memo->closure.args[0], args, 1)) {
         memo->tab1[i] = result.integer;
         identity = identity && (memo->tab1[i] == i);
      }
   }

   if (model_exit_status(m) == 0) {
      memo->flags |= R_MEMO;
      if (identity)
         memo->flags |= R_IDENT;
   }

   TRACE("memoised resolution function %pi for type %pT",
         jit_get_name(m->jit, closure->handle), tree_type(signal->where));

   jit_set_silent(m->jit, prev_silent);
   jit_reset_exit_status(m->jit);

   set_exit_severity(old_severity);

   return memo;
}

static inline void *nexus_effective(rt_nexus_t *n)
{
   return n->signal->shared.data + n->offset;
}

static inline void *nexus_last_value(rt_nexus_t *n)
{
   return n->signal->shared.data + n->offset + n->signal->shared.size;
}

static inline void *nexus_driving(rt_nexus_t *n)
{
   assert(n->flags & NET_F_EFFECTIVE);
   return n->signal->shared.data + n->offset + 2*n->signal->shared.size;
}

static inline void *nexus_initial(rt_nexus_t *n)
{
   assert(n->flags & NET_F_HAS_INITIAL);
   return n->signal->shared.data + n->offset + 2*n->signal->shared.size;
}

static rt_value_t alloc_value(rt_model_t *m, rt_nexus_t *n)
{
   rt_value_t result = {};

   const size_t valuesz = n->size * n->width;
   if (valuesz > sizeof(rt_value_t)) {
      if (n->free_value != NULL) {
         result.ext = n->free_value;
         n->free_value = *(void **)result.ext;
      }
      else
         result.ext = static_alloc(m, valuesz);
   }

   return result;
}

static void free_value(rt_nexus_t *n, rt_value_t v)
{
   const size_t valuesz = n->width * n->size;
   // The value of a null transaction is zero and has no storage
   if (valuesz > sizeof(rt_value_t) && v.ext != NULL) {
      *(void **)v.ext = n->free_value;
      n->free_value = v.ext;
   }
}

static inline uint8_t *value_ptr(rt_nexus_t *n, rt_value_t *v)
{
   const size_t valuesz = n->width * n->size;
   if (valuesz <= sizeof(rt_value_t))
      return v->bytes;
   else
      return v->ext;
}

static void copy_value_ptr(rt_nexus_t *n, rt_value_t *v, const void *p)
{
   const size_t valuesz = n->width * n->size;
   if (valuesz <= sizeof(rt_value_t)) {
#if ASAN_ENABLED
      memcpy(v->bytes, p, valuesz);
#else
      v->qword = unaligned_load(p, uint64_t);
#endif
   }
   else
      memcpy(v->ext, p, valuesz);
}

static inline bool cmp_values(rt_nexus_t *n, rt_value_t a, rt_value_t b)
{
   const size_t valuesz = n->width * n->size;
   if (valuesz <= sizeof(rt_value_t))
      return a.qword == b.qword;
   else
      return cmp_bytes(a.ext, b.ext, valuesz);
}

static inline bool is_pseudo_source(source_kind_t kind)
{
   return kind == SOURCE_FORCING || kind == SOURCE_DEPOSIT;
}

static void check_multiple_sources(rt_nexus_t *n, source_kind_t kind)
{
   if (n->signal->resolution != NULL || is_pseudo_source(kind))
      return;

   diag_t *d;
   if (is_signal_scope(n->signal->parent)) {
      rt_scope_t *root = n->signal->parent;
      for (; is_signal_scope(root->parent); root = root->parent);

      d = diag_new(DIAG_FATAL, tree_loc(root->where));
      diag_printf(d, "element %pI of signal %pI has multiple sources",
                  tree_ident(n->signal->where), tree_ident(root->where));
      diag_hint(d, tree_loc(n->signal->where), "element %pI declared here",
                tree_ident(n->signal->where));
      if (tree_kind(root->where) == T_VERILOG)
         diag_hint(d, tree_loc(root->where), "composite variable %pI declared "
                   "here", tree_ident(root->where));
      else
         diag_hint(d, tree_loc(root->where), "composite signal %pI declared "
                   "with unresolved type %pT", tree_ident(root->where),
                   tree_type(root->where));
   }
   else {
      d = diag_new(DIAG_FATAL, tree_loc(n->signal->where));
      diag_printf(d, "unresolved signal %pI has multiple sources",
                  tree_ident(n->signal->where));
      if (tree_kind(n->signal->where) == T_VERILOG)
         diag_hint(d, tree_loc(n->signal->where), "variable %pI declared here",
                   tree_ident(n->signal->where));
      else
         diag_hint(d, tree_loc(n->signal->where), "signal %pI declared with "
                   "unresolved type %pT", tree_ident(n->signal->where),
                   tree_type(n->signal->where));
   }

   if (n->sources.tag == SOURCE_DRIVER) {
      const rt_proc_t *p = n->sources.u.driver.proc;
      diag_hint(d, tree_loc(p->where), "driven by process %s", istr(p->name));
   }
   else if (n->sources.tag == SOURCE_PORT) {
      const rt_signal_t *s = n->sources.u.port.input->signal;
      tree_t where = s->where;
      if (is_signal_scope(s->parent)) {
         for (rt_scope_t *it = s->parent; is_signal_scope(it); it = it->parent)
            where = it->where;
      }

      if (tree_kind(where) == T_PORT_DECL)
         diag_hint(d, tree_loc(where), "connected to %s port %s",
                   port_mode_str(tree_subkind(where)), istr(tree_ident(where)));
      else
         diag_hint(d, tree_loc(where), "connected to signal %s",
                   istr(tree_ident(where)));
   }

   if (kind == SOURCE_DRIVER) {
      const rt_proc_t *p = get_active_proc();
      diag_hint(d, tree_loc(p->where), "driven by process %s", istr(p->name));
   }

   diag_emit(d);
   jit_abort_with_status(EXIT_FAILURE);
}

static rt_source_t *add_source(rt_model_t *m, rt_nexus_t *n, source_kind_t kind)
{
   rt_source_t *src = NULL;
   if (n->n_sources == 0)
      src = &(n->sources);
   else {
      check_multiple_sources(n, kind);

      rt_source_t **p;
      for (p = &(n->sources.chain_input); *p; p = &((*p)->chain_input))
         ;
      *p = src = static_alloc(m, sizeof(rt_source_t));
   }

   // The only interesting values of n_sources are 0, 1, and 2
   if (n->n_sources < UINT8_MAX)
      n->n_sources++;

   if (n->n_sources > 1)
      n->flags &= ~NET_F_FAST_DRIVER;

   src->chain_input  = NULL;
   src->chain_output = NULL;
   src->tag          = kind;
   src->disconnected = 0;
   src->fastqueued   = 0;
   src->sigqueued    = 0;

   switch (kind) {
   case SOURCE_DRIVER:
      {
         src->u.driver.proc  = NULL;
         src->u.driver.nexus = n;

         waveform_t *w0 = &(src->u.driver.waveforms);
         w0->when  = TIME_HIGH;
         w0->next  = NULL;
      }
      break;

   case SOURCE_PORT:
      src->u.port.input  = NULL;
      src->u.port.output = n;
      break;

   case SOURCE_DEPOSIT:
   case SOURCE_FORCING:
      src->u.pseudo.nexus = n;
      src->u.pseudo.waveforms.when  = TIME_HIGH;
      src->u.pseudo.waveforms.next  = NULL;
      src->u.pseudo.waveforms.value = alloc_value(m, n);
      break;

   case SOURCE_ACTIVE:
      should_not_reach_here();
   }

   return src;
}

static inline int map_index(rt_index_t *index, unsigned offset)
{
   if (likely(index->how >= 0))
      return offset >> index->how;
   else
      return offset / -(index->how);
}

static inline int unmap_index(rt_index_t *index, unsigned key)
{
   if (likely(index->how >= 0))
      return key << index->how;
   else
      return key * -(index->how);
}

static inline bool index_valid(rt_index_t *index, unsigned offset)
{
   if (likely(index->how >= 0))
      return (offset >> index->how) << index->how == offset;
   else
      return offset % -(index->how) == 0;
}

static void build_index(rt_signal_t *signal)
{
   const unsigned signal_w = signal->shared.size / signal->nexus.size;

   int shift = INT_MAX, gcd = 0;
   rt_nexus_t *n = &(signal->nexus);
   for (int i = 0, offset = 0; i < signal->n_nexus;
        i++, offset += n->width, n = n->chain) {
      if (offset > 0) {
         const int tzc = __builtin_ctz(offset);
         shift = MIN(shift, tzc);
      }

      // Compute greatest common divisor
      for (int b = offset; b > 0;) {
         int temp = b;
         b = gcd % b;
         gcd = temp;
      }
   }

   const int how = gcd > 1 && gcd > (1 << shift) && gcd > 1 ? -gcd : shift;
   const int count =
      how < 0 ? (signal_w - how - 1) / -how : (signal_w >> shift) + 1;

   TRACE("create index for signal %pi how=%d count=%d",
         tree_ident(signal->where), how, count);

   rt_index_t *index = xcalloc_flex(sizeof(rt_index_t), count,
                                    sizeof(rt_nexus_t *));
   index->how = how;

   n = &(signal->nexus);
   for (int i = 0, offset = 0; i < signal->n_nexus;
        i++, offset += n->width, n = n->chain) {
      index->nexus[map_index(index, offset)] = n;
   }

   free(signal->index);
   signal->index = index;
}

static void update_index(rt_signal_t *s, rt_nexus_t *n)
{
   const unsigned offset = n->offset / n->size;

   if (!index_valid(s->index, offset)) {
      TRACE("rebuild index for %pi offset=%d how=%d",
            tree_ident(s->where), offset, s->index->how);
      build_index(s);
      assert(s->index->nexus[map_index(s->index, offset)] == n);
   }
   else {
      const int elt = map_index(s->index, offset);
      assert(s->index->nexus[elt] == NULL);
      s->index->nexus[elt] = n;
   }
}

static rt_nexus_t *lookup_index(rt_signal_t *s, int *offset)
{
   if (likely(offset == 0 || s->index == NULL))
      return &(s->nexus);
   else if (!index_valid(s->index, *offset)) {
      TRACE("invalid index for %pi offset=%d how=%d", tree_ident(s->where),
            *offset, s->index->how);
      free(s->index);
      s->index = NULL;
      return &(s->nexus);
   }
   else {
      const int key = map_index(s->index, *offset);
      for (int k = key; k >= 0; k--) {
         rt_nexus_t *n = s->index->nexus[k];
         if (n != NULL) {
            *offset = unmap_index(s->index, key - k);
            return n;
         }
      }
      return &(s->nexus);
   }
}

static waveform_t *alloc_waveform(rt_model_t *m)
{
   model_thread_t *thread = model_thread(m);

   if (thread->free_waveforms == NULL) {
      // Ensure waveforms are always within one cache line
      STATIC_ASSERT(sizeof(waveform_t) <= 32);
      char *mem = static_alloc(m, WAVEFORM_CHUNK * 32);
      for (int i = 1; i < WAVEFORM_CHUNK; i++)
         free_waveform(m, (waveform_t *)(mem + i*32));

      return (waveform_t *)mem;
   }
   else {
      waveform_t *w = thread->free_waveforms;
      thread->free_waveforms = w->next;
      prefetch_write(w->next);
      w->next = NULL;
      return w;
   }
}

static void split_value(rt_nexus_t *nexus, rt_value_t *v_new,
                        rt_value_t *v_old, int offset)
{
   const int split = offset * nexus->size;
   const int oldsz = (offset + nexus->width) * nexus->size;
   const int newsz = nexus->width * nexus->size;

   if (split > sizeof(rt_value_t) && newsz > sizeof(rt_value_t)) {
      // Split the external memory with no copying
      v_new->ext = (char *)v_old->ext + split;
   }
   else if (newsz > sizeof(rt_value_t)) {
      // Wasting up to eight bytes at the start of the the old waveform
      char *ext = v_old->ext;
      v_old->qword = *(uint64_t *)ext;
      v_new->ext = ext + split;
   }
   else if (split > sizeof(rt_value_t)) {
      // Wasting up to eight bytes at the end of the the old waveform
      memcpy(v_new->bytes, v_old->ext + split, newsz);
   }
   else if (oldsz > sizeof(rt_value_t)) {
      // The memory backing this waveform is lost now but this can only
      // happen a bounded number of times as nexuses only ever shrink
      char *ext = v_old->ext;
      memcpy(v_new->bytes, ext + split, newsz);
      v_old->qword = *(uint64_t *)ext;
   }
   else {
      // This trick with shifting probably only works on little-endian
      // systems
      v_new->qword = v_old->qword >> (split * 8);
   }
}

static void clone_source(rt_model_t *m, rt_nexus_t *nexus,
                         rt_source_t *old, int offset)
{
   rt_source_t *new = add_source(m, nexus, old->tag);

   switch (old->tag) {
   case SOURCE_PORT:
      {
         new->u.port.input = old->u.port.input;

         if (old->u.port.input->width == offset)
            new->u.port.input = old->u.port.input->chain;  // Cycle breaking
         else {
            RT_LOCK(old->u.port.input->signal->lock);
            rt_nexus_t *n = clone_nexus(m, old->u.port.input, offset);
            new->u.port.input = n;
         }
         assert(new->u.port.input->width == nexus->width);
      }
      break;

   case SOURCE_DRIVER:
      {
         new->u.driver.proc = old->u.driver.proc;

         // Current transaction
         waveform_t *w_new = &(new->u.driver.waveforms);
         waveform_t *w_old = &(old->u.driver.waveforms);
         w_new->when = w_old->when;
         w_new->next = NULL;

         split_value(nexus, &w_new->value, &w_old->value, offset);

         // Pending fast driver update
         if ((nexus->flags & NET_F_FAST_DRIVER) && old->fastqueued) {
            rt_nexus_t *n0 = &(nexus->signal->nexus);
            if (!n0->sources.sigqueued)
               deferq_do(&m->driverq, async_fast_driver, new);
            new->fastqueued = 1;
         }

         new->was_active = old->was_active;

         // Future transactions
         for (w_old = w_old->next; w_old; w_old = w_old->next) {
            w_new = (w_new->next = alloc_waveform(m));
            w_new->when = w_old->when;
            w_new->next = NULL;

            split_value(nexus, &w_new->value, &w_old->value, offset);

            assert(w_old->when >= m->now);
            deltaq_insert_driver(m, w_new->when - m->now, new);
         }
      }
      break;

   case SOURCE_FORCING:
   case SOURCE_DEPOSIT:
      {
         waveform_t *w_new = &(new->u.pseudo.waveforms);
         waveform_t *w_old = &(old->u.pseudo.waveforms);
         w_new->when = w_old->when;
         w_new->next = NULL;

         split_value(nexus, &w_new->value, &w_old->value, offset);

         // Future transactions
         for (w_old = w_old->next; w_old; w_old = w_old->next) {
            w_new = (w_new->next = alloc_waveform(m));
            w_new->when = w_old->when;
            w_new->next = NULL;

            split_value(nexus, &w_new->value, &w_old->value, offset);

            assert(w_old->when >= m->now);
            deltaq_insert_pseudo_source(m, w_new->when - m->now, new, false);
         }
      }
      break;

   case SOURCE_ACTIVE:
      break;
   }
}

static void find_queued_nexus_cb(uint64_t key, void *user, void *context)
{
   void **search = context;
   if (user == search[0])
      search[1] = search;   // Non-NULL means found
}

static bool nexus_in_heap(heap_t *h, rt_nexus_t *n)
{
   void *search[2] = { n, NULL };
   heap_walk(h, find_queued_nexus_cb, search);
   return search[1] != NULL;
}

static rt_nexus_t *clone_nexus(rt_model_t *m, rt_nexus_t *old, int offset)
{
   assert(offset < old->width);

   rt_signal_t *signal = old->signal;
   MULTITHREADED_ONLY(assert_lock_held(&signal->lock));
   signal->n_nexus++;

   if (signal->n_nexus == 2 && (old->flags & NET_F_FAST_DRIVER))
      signal->shared.flags |= NET_F_FAST_DRIVER;

   rt_nexus_t *new = static_alloc(m, sizeof(rt_nexus_t));
   new->width        = old->width - offset;
   new->size         = old->size;
   new->signal       = signal;
   new->offset       = old->offset + offset * old->size;
   new->chain        = old->chain;
   new->flags        = old->flags;
   new->active_delta = old->active_delta;
   new->event_delta  = old->event_delta;
   new->last_event   = old->last_event;
   new->rank         = old->rank;

   old->chain = new;
   old->width = offset;

   if (old->flags & NET_F_PENDING) {
      // A driving or effective value update is queued for the old nexus
      // and must also be performed for the new one: the pending flag
      // was copied but the queues hold only the old pointer
      if (nexus_in_heap(m->driving_heap, old))
         heap_insert(m->driving_heap, new->rank, new);
      if (nexus_in_heap(m->effective_heap, old))
         heap_insert(m->effective_heap, MAX_RANK - new->rank, new);
   }

   if (old->pending == NULL)
      new->pending = NULL;
   else if (pointer_tag(old->pending) == 1) {
      new->pending = old->pending;
      proc_pin(untag_pointer(old->pending, rt_wakeable_t));
   }
   else {
      rt_pending_t *old_p = untag_pointer(old->pending, rt_pending_t);
      rt_pending_t *new_p = xmalloc_flex(sizeof(rt_pending_t), old_p->count,
                                         sizeof(rt_wakeable_t *));

      new_p->count = new_p->max = old_p->count;
      new_p->first_free = 0;
      new_p->index_size = new_p->index_used = 0;
      new_p->index = NULL;

      for (int i = 0; i < old_p->count; i++) {
         new_p->wake[i] = old_p->wake[i];
         if (old_p->wake[i] != NULL)
            proc_pin(old_p->wake[i]);
      }

      if (new_p->count >= PENDING_INDEX_MIN)
         pending_build_index(new_p);

      new->pending = tag_pointer(new_p, 0);
   }

   if (new->chain == NULL)
      m->nexus_tail = &(new->chain);

   if (old->n_sources > 0) {
      for (rt_source_t *it = &(old->sources); it; it = it->chain_input)
         clone_source(m, new, it, offset);
   }

   for (rt_source_t *old_o = old->outputs; old_o; old_o = old_o->chain_output) {
      switch (old_o->tag) {
      case SOURCE_PORT:
         {
            rt_nexus_t *out_n;
            if (old_o->u.port.output->width == offset)
               out_n = old_o->u.port.output->chain;   // Cycle breaking
            else {
               RT_LOCK(old_o->u.port.output->signal->lock);
               out_n = clone_nexus(m, old_o->u.port.output, offset);
            }

            for (rt_source_t *s = &(out_n->sources); s; s = s->chain_input) {
               if (s->tag != old_o->tag)
                  continue;
               else if (s->u.port.input == new || s->u.port.input == old) {
                  s->u.port.input = new;
                  s->chain_output = new->outputs;
                  new->outputs = s;
                  break;
               }
            }
         }
         break;

      case SOURCE_ACTIVE:
         {
            rt_source_t *src = static_alloc(m, sizeof(rt_source_t));
            src->tag          = SOURCE_ACTIVE;
            src->chain_output = new->outputs;
            src->u.wakeable   = old_o->u.wakeable;

            new->outputs = src;
         }
         break;

      default:
         should_not_reach_here();
      }
   }

   if (signal->index == NULL && signal->n_nexus >= NEXUS_INDEX_MIN)
      build_index(signal);
   else if (signal->index != NULL)
      update_index(signal, new);

   return new;
}

static rt_nexus_t *split_nexus_slow(rt_model_t *m, rt_signal_t *s,
                                    int offset, int count)
{
   assert(offset + count <= s->shared.size / s->nexus.size || count == 0);

   rt_nexus_t *result = NULL;
   for (rt_nexus_t *it = lookup_index(s, &offset); count > 0; it = it->chain) {
      if (offset >= it->width) {
         offset -= it->width;
         continue;
      }
      else if (offset > 0) {
         clone_nexus(m, it, offset);
         offset = 0;
         continue;
      }
      else {
         if (it->width > count)
            clone_nexus(m, it, count);

         count -= it->width;

         if (result == NULL)
            result = it;
      }
   }

   return result;
}

static inline rt_nexus_t *split_nexus(rt_model_t *m, rt_signal_t *s,
                                      int offset, int count)
{
   MULTITHREADED_ONLY(assert_lock_held(&s->lock));

   rt_nexus_t *n0 = &(s->nexus);
   if (unlikely(s->shared.flags & SIG_F_PACKED))
      return n0;
   if (likely(offset == 0 && n0->width == count))
      return n0;
   else if (offset == 0 && count == s->shared.size / n0->size)
      return n0;

   return split_nexus_slow(m, s, offset, count);
}

static void setup_signal(rt_model_t *m, rt_signal_t *s, tree_t where,
                         unsigned count, unsigned size, sig_flags_t flags,
                         unsigned offset)
{
   rt_scope_t *parent = model_thread(m)->active_scope;

   s->where   = where;
   s->n_nexus = 1;
   s->offset  = offset;
   s->parent  = parent;

   s->shared.flags = flags;
   s->shared.size  = count * size;

   APUSH(parent->signals, s);

   s->nexus.width        = count;
   s->nexus.size         = size;
   s->nexus.n_sources    = 0;
   s->nexus.offset       = 0;
   s->nexus.flags        = flags | NET_F_FAST_DRIVER | NET_F_HAS_INITIAL;
   s->nexus.signal       = s;
   s->nexus.pending      = NULL;
   s->nexus.active_delta = DELTA_CYCLE_MAX;
   s->nexus.event_delta  = DELTA_CYCLE_MAX;
   s->nexus.last_event   = TIME_HIGH;

   *m->nexus_tail = &(s->nexus);
   m->nexus_tail = &(s->nexus.chain);

   m->n_signals++;
}

static bool vlog_is_assertion_process(vlog_node_t v)
{
   if (vlog_stmts(v) != 1 || vlog_kind(vlog_stmt(v, 0)) != V_TIMING)
      return false;

   vlog_node_t timing = vlog_stmt(v, 0);
   if (vlog_stmts(timing) != 1)
      return false;

   vlog_node_t s = vlog_stmt(timing, 0);
   switch (vlog_kind(s)) {
   case V_ASSERT:
   case V_ASSUME:
   case V_COVER:
      return vlog_kind(vlog_value(s)) == V_PROP_SPEC;
   default:
      return false;
   }
}

static void setup_process(rt_model_t *m, rt_proc_t *p, const char *path)
{
   p->create_seq = ++(m->proc_seq);

   switch (tree_kind(p->where)) {
   case T_VERILOG:
      {
         p->verilog = true;
         m->has_verilog = true;

         vlog_node_t v = tree_vlog(p->where);
         switch (vlog_kind(v)) {
         case V_ASSIGN:
         case V_PORT_MAP:
         case V_GATE_INST:
         case V_NET_DECL:
            p->wakeable.reschedule = true;
            p->name = ident_sprintf("%s:%s", path, istr(tree_ident(p->where)));
            break;
         case V_UDP_TABLE:
            // TODO: reschedule = (vlog_subkind(v) == V_UDP_COMB);
            p->wakeable.reschedule = true;
            p->name = ident_sprintf("%s:_udp", path);
            break;
         case V_ALWAYS:
            p->name = ident_sprintf("%s:%s", path, istr(tree_ident(p->where)));
            p->comb = vlog_subkind(v) == V_ALWAYS_COMB_DONE
               || vlog_subkind(v) == V_ALWAYS_LATCH_DONE;

            if (vlog_is_assertion_process(v)) {
               m->sva_procs = xrealloc_array(m->sva_procs, m->nsva_procs + 1,
                                             sizeof(rt_proc_t *));
               m->sva_procs[m->nsva_procs++] = p;
            }

            // Processes of the Postponed region: the copies made for
            // assertions run first and then, in the order they were
            // woken, the lines of $monitor and $strobe and the reports
            // of final deferred assertions
            p->wakeable.postponed =
               ident_starts_with(tree_ident(p->where), ident_new("$postponed"));
            p->wakeable.latepost =
               ident_starts_with(tree_ident(p->where), ident_new("$postponed_s"));

            // Copies of variables for assertions that nothing waits for,
            // and the values of the reports of a final deferred assertion
            // which only its own process reads
            p->wakeable.sampler =
               ident_starts_with(tree_ident(p->where), ident_new("$postponed_a"))
               || p->wakeable.latepost;
            break;
         case V_INITIAL:
            p->name = ident_sprintf("%s:%s", path, istr(tree_ident(p->where)));
            break;
         case V_FINAL:
            m->nfinal_procs++;
            p->name = ident_sprintf("%s:%s", path, istr(tree_ident(p->where)));
            break;
         case V_VAR_DECL:
            // Initialiser that refers to an instance by hierarchical name
            p->name = ident_sprintf("%s:%s", path, istr(tree_ident(p->where)));
            break;
         case V_TCHECK:
            // TODO: Should names here be unique ?
            p->name = ident_sprintf("%s", path);
            p->wakeable.postponed = true;
            // The notifier is toggled in the time step of the violation
            p->wakeable.sampler = true;
            break;
         default:
            should_not_reach_here();
         }
      }
      break;
   case T_IMPLICIT_SIGNAL:
      p->wakeable.reschedule = (tree_subkind(p->where) != IMPLICIT_DELAYED);
      // Fall-through
   case T_INERTIAL:
      {
         const char *suffix = istr(ident_downcase(tree_ident(p->where)));
         p->name = ident_sprintf("%s:%s", path, suffix);
      }
      break;
   case T_PARAM:  // Conversion function
      p->name = jit_get_name(get_model()->jit, p->closure.handle);
      p->wakeable.reschedule = true;
      break;
   default:
      should_not_reach_here();
   }
}

static void copy_sub_signal_sources(rt_scope_t *scope, void *buf, int stride,
                                    unsigned limit)
{
   assert(is_signal_scope(scope));

   for (int i = 0; i < scope->signals.count; i++) {
      rt_signal_t *s = scope->signals.items[i];
      rt_nexus_t *n = &(s->nexus);
      for (unsigned j = 0; j < s->n_nexus; j++, n = n->chain) {
         unsigned o = 0;
         for (rt_source_t *src = &(n->sources); src; src = src->chain_input) {
            const void *data = source_value(n, src);
            if (data == NULL)
               continue;
            else if (o >= limit)
               break;

            memcpy(buf + s->offset + n->offset + (o++ * stride), data,
                   n->size * n->width);
         }
      }
   }

   for (int i = 0; i < scope->children.count; i++)
      copy_sub_signal_sources(scope->children.items[i], buf, stride, limit);
}

static bool has_active_source(const rt_nexus_t *n)
{
   // An inout port that is not driven by the architecture contributes
   // nothing to the actual that it is connected to
   for (const rt_source_t *s = &(n->sources); n->n_sources > 0 && s;
        s = s->chain_input) {
      switch (s->tag) {
      case SOURCE_DRIVER:
         if (!s->disconnected)
            return true;
         break;
      case SOURCE_PORT:
         if (!(s->u.port.input->flags & NET_F_INOUT)
             || has_active_source(s->u.port.input))
            return true;
         break;
      default:
         break;
      }
   }

   return false;
}

static __thread bool skip_undriven_ports = false;

static void *source_value(rt_nexus_t *nexus, rt_source_t *src)
{
   switch (src->tag) {
   case SOURCE_DRIVER:
      if (unlikely(src->disconnected))
         return NULL;
      else
         return value_ptr(nexus, &(src->u.driver.waveforms.value));

   case SOURCE_PORT:
      if (skip_undriven_ports && (src->u.port.input->flags & NET_F_INOUT)
          && !has_active_source(src->u.port.input))
         return NULL;
      else if (src->u.port.input->flags & NET_F_EFFECTIVE)
         return nexus_driving(src->u.port.input);
      else
         return nexus_effective(src->u.port.input);

   case SOURCE_FORCING:
   case SOURCE_DEPOSIT:
      assert(src->disconnected);
      return NULL;

   case SOURCE_ACTIVE:
      should_not_reach_here();
   }

   return NULL;
}

static void call_resolution(rt_model_t *m, rt_nexus_t *n, res_memo_t *r,
                            int nonnull, rt_source_t *s0)
{
   if ((n->flags & NET_F_R_IDENT) && nonnull == 1) {
      // Resolution function behaves like identity for a single driver
      put_driving(m, n, source_value(n, s0));
   }
   else if ((r->flags & R_MEMO) && nonnull == 1) {
      // Resolution function has been memoised so do a table lookup

      model_thread_t *thread = model_thread(m);
      assert(thread->tlab != NULL);

      const uint32_t mark = tlab_mark(thread->tlab);

      void *resolved = tlab_alloc(thread->tlab, n->width * n->size);
      char *p0 = source_value(n, s0);

      for (int j = 0; j < n->width; j++) {
         const int index = ((uint8_t *)p0)[j];
         ((int8_t *)resolved)[j] = r->tab1[index];
      }

      put_driving(m, n, resolved);
      tlab_trim(thread->tlab, mark);
   }
   else if ((r->flags & R_MEMO) && nonnull == 2) {
      // Resolution function has been memoised so do a table lookup

      model_thread_t *thread = model_thread(m);
      assert(thread->tlab != NULL);

      const uint32_t mark = tlab_mark(thread->tlab);

      void *resolved = tlab_alloc(thread->tlab, n->width * n->size);

      char *p0 = source_value(n, s0), *p1 = NULL;
      for (rt_source_t *s1 = s0->chain_input;
           s1 && (p1 = source_value(n, s1)) == NULL;
           s1 = s1->chain_input)
         ;

      for (int j = 0; j < n->width; j++)
         ((int8_t *)resolved)[j] = r->tab2[(int)p0[j]][(int)p1[j]];

      put_driving(m, n, resolved);
      tlab_trim(thread->tlab, mark);
   }
   else if (r->flags & R_BLOB) {
      // The function is called with the values of all the sources for
      // each group of elements making up one value and returns a pointer
      // to the resolved value (user-defined net types, IEEE 1800 6.6.7)
      const int k = r->blobsz;
      if (n->width % k != 0 || (n->offset / n->size) % k != 0)
         jit_msg(NULL, DIAG_FATAL, "sorry, part of a net of a user-defined "
                 "net type cannot be driven on its own");

      model_thread_t *thread = model_thread(m);
      assert(thread->tlab != NULL);

      const uint32_t mark = tlab_mark(thread->tlab);

      const size_t valsz = k * n->size;
      char *resolved = tlab_alloc(thread->tlab, n->width * n->size);
      char *inputs = tlab_alloc(thread->tlab, MAX(nonnull, 1) * valsz);

      tlab_t res_tlab = jit_null_tlab(m->jit);
      for (int e = 0; e < n->width; e += k) {
         unsigned o = 0;
         for (rt_source_t *s = s0; s; s = s->chain_input) {
            const char *data = source_value(n, s);
            if (data != NULL)
               memcpy(inputs + (o++) * valsz, data + e * n->size, valsz);
         }
         assert(o == nonnull);

         jit_scalar_t result;
         if (!jit_try_call_tlab(m->jit, r->closure.handle, &res_tlab, &result,
                                r->closure.args[0], inputs, (int64_t)nonnull)) {
            m->force_stop = true;
            memcpy(resolved + e * n->size, nexus_effective(n) + e * n->size,
                   valsz);
         }
         else
            memcpy(resolved + e * n->size, result.pointer, valsz);
      }
      if (unlikely(res_tlab.mem_dirty))
         m->sv_poll_mem_dirty = true;

      put_driving(m, n, resolved);
      tlab_trim(thread->tlab, mark);
   }
   else if (r->flags & R_COMPOSITE) {
      // Call resolution function of composite type
      rt_scope_t *scope = n->signal->parent, *rscope = scope;
      while (is_signal_scope(scope->parent)) {
         scope = scope->parent;
         if (scope->flags & SCOPE_F_RESOLVED)
            rscope = scope;
      }

      TRACE("resolved composite signal needs %d bytes", scope->size);

      model_thread_t *thread = model_thread(m);
      assert(thread->tlab != NULL);

      const uint32_t mark = tlab_mark(thread->tlab);

      uint8_t *inputs = tlab_alloc(thread->tlab, nonnull * scope->size);
      copy_sub_signal_sources(scope, inputs, scope->size, nonnull);

      tlab_t res_tlab = jit_null_tlab(m->jit);
      jit_scalar_t result;
      if (jit_try_call_tlab(m->jit, r->closure.handle, &res_tlab, &result,
                            r->closure.args[0], inputs, nonnull))
         put_driving(m, n, result.pointer + n->signal->offset
                     + n->offset - rscope->offset);
      else
         m->force_stop = true;
      if (unlikely(res_tlab.mem_dirty))
         m->sv_poll_mem_dirty = true;

      tlab_trim(thread->tlab, mark);
   }
   else {
      model_thread_t *thread = model_thread(m);
      assert(thread->tlab != NULL);

      const uint32_t mark = tlab_mark(thread->tlab);

      void *resolved = tlab_alloc(thread->tlab, n->width * n->size);

      tlab_t res_tlab = jit_null_tlab(m->jit);
      for (int j = 0; j < n->width; j++) {
#define CALL_RESOLUTION_FN(type) do {                                   \
            type vals[nonnull];                                         \
            unsigned o = 0;                                             \
            for (rt_source_t *s = s0; s; s = s->chain_input) {          \
               const void *data = source_value(n, s);                   \
               if (data != NULL)                                        \
                  vals[o++] = ((const type *)data)[j];                  \
            }                                                           \
            assert(o == nonnull);                                       \
            type *p = (type *)resolved;                                 \
            jit_scalar_t result = { .integer = 0 };                     \
            if (!jit_try_call_tlab(m->jit, r->closure.handle, &res_tlab,\
                                   &result, r->closure.args[0], vals,   \
                                   nonnull))                            \
               m->force_stop = true;                                    \
            p[j] = result.integer;                                      \
         } while (0)

         FOR_ALL_SIZES(n->size, CALL_RESOLUTION_FN);
      }
      if (unlikely(res_tlab.mem_dirty))
         m->sv_poll_mem_dirty = true;

      put_driving(m, n, resolved);
      tlab_trim(thread->tlab, mark);
   }
}

static rt_source_t *get_pseudo_source(rt_model_t *m, rt_nexus_t *n,
                                      source_kind_t kind)
{
   assert(is_pseudo_source(kind));

   if (n->n_sources > 0) {
      for (rt_source_t *s = &(n->sources); s; s = s->chain_input) {
         if (s->tag == kind)
            return s;
      }
   }

   return add_source(m, n, kind);
}

static void calculate_driving_value(rt_model_t *m, rt_nexus_t *n)
{
   // Algorithm for driving values is in LRM 08 section 14.7.3.2

   // If S has no source, then the driving value of S is given by the
   // default value associated with S
   if (n->n_sources == 0) {
      put_driving(m, n, nexus_initial(n));
      return;
   }

   res_memo_t *r = n->signal->resolution;

   if (n->flags & NET_F_FORCED) {
      // A forced value has priority over an assignment scheduled earlier
      for (rt_source_t *s = &(n->sources); s; s = s->chain_input) {
         if (s->tag == SOURCE_FORCING) {
            put_driving(m, n, value_ptr(n, &(s->u.pseudo.waveforms.value)));
            return;
         }
      }
   }

   bool skip_undriven = true, skipped = false;

 again:;
   int nonnull = 0;
   rt_source_t *s0 = NULL;
   for (rt_source_t *s = &(n->sources); s; s = s->chain_input) {
      if (s->disconnected)
         continue;
      else if (skip_undriven && s->tag == SOURCE_PORT
               && (s->u.port.input->flags & NET_F_INOUT)
               && !has_active_source(s->u.port.input)) {
         skipped = true;
         continue;
      }
      else if (s->tag == SOURCE_FORCING) {
         // If S is driving-value forced, the driving value of S is
         // unchanged from its previous value; no further steps are
         // required.
         put_driving(m, n, value_ptr(n, &(s->u.pseudo.waveforms.value)));
         return;
      }
      else if (s->tag == SOURCE_DEPOSIT) {
         // If a driving-value deposit is scheduled for S or for a
         // signal of which S is a subelement, the driving value of S is
         // the driving deposit value for S or the element of the
         // driving deposit value for the signal of which S is a
         // subelement, as appropriate.
         s->disconnected = 1;
         put_driving(m, n, value_ptr(n, &(s->u.pseudo.waveforms.value)));
         return;
      }
      else if (s0 == NULL)
         s0 = s;
      nonnull++;
   }

   if (s0 == NULL && skipped && skip_undriven) {
      // Nothing else drives this so use the value of the undriven port
      skip_undriven = false;
      goto again;
   }

   skip_undriven_ports = skip_undriven;

   if (unlikely(s0 == NULL)) {
      // If S is of signal kind register and all the sources of S have
      // values determined by the null transaction, then the driving
      // value of S is unchanged from its previous value.
      if (n->signal->shared.flags & SIG_F_REGISTER)
         put_driving(m, n, nexus_effective(n));
      else if (r == NULL || is_pseudo_source(n->sources.tag))
         put_driving(m, n, nexus_initial(n));
      else
         call_resolution(m, n, r, nonnull, s0);
   }
   else if (r == NULL) {
      switch (s0->tag) {
      case SOURCE_DRIVER:
         // If S has one source that is a driver and S is not a resolved
         // signal, then the driving value of S is the current value of
         // that driver.
         assert(!s0->disconnected);
         put_driving(m, n, value_ptr(n, &(s0->u.driver.waveforms.value)));
         break;

      case SOURCE_PORT:
         // If S has one source that is a port and S is not a resolved
         // signal, then the driving value of S is the driving value of
         // the formal part of the association element that associates S
         // with that port
         if (s0->u.port.input->flags & NET_F_EFFECTIVE)
            put_driving(m, n, nexus_driving(s0->u.port.input));
         else
            put_driving(m, n, nexus_effective(s0->u.port.input));
         break;

      default:
         break;
      }
   }
   else {
      // Otherwise, the driving value of S is obtained by executing the
      // resolution function associated with S
      call_resolution(m, n, r, nonnull, s0);
   }

   skip_undriven_ports = false;
}

static void calculate_effective_value(rt_model_t *m, rt_nexus_t *n)
{
   // Algorithm for effective values is in LRM 08 section 14.7.7.3

   // If S is a connected port of mode in or inout, then the effective
   // value of S is the same as the effective value of the actual part
   // of the association element that associates an actual with S
   if (n->flags & NET_F_INOUT) {
      for (rt_source_t *s = n->outputs; s; s = s->chain_output) {
         if (s->tag == SOURCE_PORT) {
            put_effective(m, n, nexus_effective(s->u.port.output));
            return;
         }
      }
   }

   // If S is a signal declared by a signal declaration, a port of mode
   // out or buffer, or an unconnected port of mode inout, then the
   // effective value of S is the same as the driving value of S.
   if (n->flags & NET_F_EFFECTIVE)
      put_effective(m, n, nexus_driving(n));

   // If S is an unconnected port of mode in, the effective value of S
   // is given by the default value associated with S.
}

static void calculate_initial_value(rt_model_t *m, rt_nexus_t *n)
{
   if (unlikely(n->signal->shared.flags & SIG_F_PACKED)) {
      rt_signal_t *s = n->signal;
      memcpy(s->shared.data + s->shared.size, s->shared.data, s->shared.size);
      return;
   }

   calculate_driving_value(m, n);

   if (n->flags & NET_F_EFFECTIVE) {
      // Driving and effective values must be calculated separately
      assert(n->flags & NET_F_PENDING);
   }
   else {
      // Effective value is always the same as the driving value
      memcpy(nexus_last_value(n), nexus_effective(n), n->size * n->width);
   }
}

static void update_rank(rt_nexus_t *n, int min)
{
   if (n->rank >= min)
      return;
   else if (min > MAX_RANK)
      fatal_at(tree_loc(n->signal->where), "signal rank is greater "
               "than the maximum supported %d", MAX_RANK);

   // Avoid infinite recursion if the net contains cycles
   n->rank = MAX_RANK;

   for (rt_source_t *o = n->outputs; o; o = o->chain_output) {
      switch (o->tag) {
      case SOURCE_PORT:
         update_rank(o->u.port.output, min + 1);
         break;
      default:
         should_not_reach_here();
      }
   }

   n->rank = min;
}

cover_data_t *get_coverage(rt_model_t *m)
{
   return m->cover;
}

#if TRACE_SIGNALS
static void dump_one_signal(rt_model_t *m, rt_scope_t *scope, rt_signal_t *s,
                            tree_t alias)
{
   rt_nexus_t *n = &(s->nexus);

   LOCAL_TEXT_BUF tb = tb_new();
   if (is_signal_scope(scope))
      tb_printf(tb, "%s.", istr(scope->name));
   tb_cat(tb, istr(tree_ident(alias ?: s->where)));
   if (alias != NULL)
      tb_append(tb, '*');

   for (int nth = 0; nth < s->n_nexus; nth++, n = n->chain) {
      int n_outputs = 0;
      for (rt_source_t *s = n->outputs; s != NULL; s = s->chain_output)
         n_outputs++;

      const void *driving = NULL;
      if (n->flags & NET_F_EFFECTIVE)
         driving = nexus_driving(n);

      fprintf(stderr, "%-20s %-5d %-4d %-7d %-7d %-4d ",
              nth == 0 ? tb_get(tb) : "+",
              n->width, n->size, n->n_sources, n_outputs, n->rank);

      if (s->shared.flags & SIG_F_PACKED) {
         fprintf(stderr, "[packed %u bits]\n", s->bit_width);
         continue;
      }

      if (n->event_delta == m->iteration && n->last_event == m->now)
         fprintf(stderr, "%s -> ", fmt_nexus(n, nexus_last_value(n)));

      fputs(fmt_nexus(n, nexus_effective(n)), stderr);

      if (driving != NULL)
         fprintf(stderr, " (%s)", fmt_nexus(n, driving));

      fputs("\n", stderr);
   }
}

static void dump_signals(rt_model_t *m, rt_scope_t *scope)
{
   if (scope->signals.count == 0 && scope->children.count == 0)
      return;

   if (!is_signal_scope(scope)) {
      const char *sname = istr(scope->name);
      fprintf(stderr, "== %s ", sname);
      for (int pad = 74 - strlen(sname); pad > 0; pad--)
         fputc('=', stderr);
      fputc('\n', stderr);

      fprintf(stderr, "%-20s %5s %4s %7s %7s %4s %s\n",
              "Signal", "Width", "Size", "Sources", "Outputs", "Rank", "Value");
   }

   for (int i = 0; i < scope->signals.count; i++)
      dump_one_signal(m, scope, scope->signals.items[i], NULL);

   for (int i = 0; i < scope->aliases.count; i++) {
      rt_alias_t *a = scope->aliases.items[i];
      dump_one_signal(m, scope, a->signal, a->where);
   }

   for (int i = 0; i < scope->children.count; i++) {
      rt_scope_t *c = scope->children.items[i];
      if (is_signal_scope(c))
         dump_signals(m, c);
   }

   for (int i = 0; i < scope->children.count; i++) {
      rt_scope_t *c = scope->children.items[i];
      if (!is_signal_scope(c))
         dump_signals(m, c);
   }
}
#endif   // TRACE_SIGNALS

static text_buf_t *signal_full_name(rt_signal_t *s)
{
   text_buf_t *tb = tb_new();
   if (is_signal_scope(s->parent))
      tb_printf(tb, "%s.", istr(s->parent->name));
   tb_cat(tb, istr(tree_ident(s->where)));
   return tb;
}

static void check_undriven_std_logic(rt_nexus_t *n)
{
   // Print a warning if any STD_LOGIC signal has multiple sources one
   // of which is an undriven port with initial value 'U'. The resolved
   // value will then always be 'U' which often confuses users.

   if (n->n_sources < 2 || !(n->signal->shared.flags & SIG_F_STD_LOGIC))
      return;

   rt_signal_t *undriven = NULL;
   for (rt_source_t *s = &(n->sources); s; s = s->chain_input) {
      if (s->tag == SOURCE_PORT) {
         rt_nexus_t *input = s->u.port.input;
         if (input->n_sources == 0) {
            const unsigned char *init = nexus_effective(input), *p = init;
            for (; *p == 0 && p < init + input->width; p++);

            if (p == init + input->width)
               undriven = s->u.port.input->signal;
         }
      }
   }

   if (undriven == NULL)
      return;

   LOCAL_TEXT_BUF sig_name = signal_full_name(n->signal);
   LOCAL_TEXT_BUF port_name = signal_full_name(undriven);

   const loc_t *sig_loc = tree_loc(n->signal->where);
   rt_scope_t *sig_scope = n->signal->parent;
   for (; is_signal_scope(sig_scope); sig_scope = sig_scope->parent)
      sig_loc = tree_loc(sig_scope->where);

   const loc_t *port_loc = tree_loc(undriven->where);
   rt_scope_t *port_scope = undriven->parent;
   for (; is_signal_scope(port_scope); port_scope = port_scope->parent)
      port_loc = tree_loc(port_scope->where);

   diag_t *d = diag_new(DIAG_WARN, sig_loc);
   diag_printf(d, "%ssignal %s has %d sources including port %s which has "
               "initial value 'U' and no driver in instance %s",
               n->signal->n_nexus > 1 ? "sub-element of " : "",
               tb_get(sig_name), n->n_sources,
               tb_get(port_name), istr(tree_ident(port_scope->where)));
   diag_hint(d, sig_loc, "signal %s declared here", tb_get(sig_name));
   diag_hint(d, port_loc, "sourced by port %s which always contributes 'U'",
             tb_get(port_name));
   diag_hint(d, NULL, "the resolved value will always be 'U' which was almost "
             "certainly not intended");
   diag_emit(d);

   // Prevent multiple warnings for the same signal
   n->signal->shared.flags &= ~SIG_F_STD_LOGIC;
}

static void create_processes(rt_model_t *m, rt_scope_t *s)
{
   for (int i = 0; i < s->children.count; i++) {
      if (s->children.items[i]->kind == SCOPE_INSTANCE)
         create_processes(m, s->children.items[i]);
   }

   if (s->kind != SCOPE_INSTANCE)
      return;

   tree_t hier = tree_decl(s->where, 0);
   assert(tree_kind(hier) == T_HIER);

   LOCAL_TEXT_BUF tb = tb_new();
   get_path_name(s, tb);

   ident_t path = ident_new(tb_get(tb));
   ident_t sym_prefix = tree_ident2(hier);

   const int nstmts = tree_stmts(s->where);
   for (int i = 0; i < nstmts; i++) {
      tree_t t = tree_stmt(s->where, i);
      switch (tree_kind(t)) {
      case T_PROCESS:
         {
            ident_t name = tree_ident(t);
            ident_t sym = ident_prefix(sym_prefix, name, '.');
            if (jit_is_fused_proc(m->jit, ident_prefix(s->name, name, '.')))
               break;

            rt_proc_t *p = xcalloc(sizeof(rt_proc_t));
            p->where     = t;
            p->name      = ident_prefix(path, ident_downcase(name), ':');
            p->scope     = s;
            p->privdata  = mptr_new(m->mspace, "process privdata");

            p->closure.handle = jit_lazy_compile(m->jit, sym);
            p->closure.nargs = 1;
            p->closure.args[0].pointer = NULL;

            p->wakeable.kind      = W_PROC;
            p->wakeable.pending   = false;
            p->wakeable.delayed   = false;
            p->wakeable.postponed = !!(tree_flags(t) & TREE_F_POSTPONED);

            APUSH(s->procs, p);
         }
         break;

      case T_PSL_DIRECT:
         {
            psl_node_t psl = tree_psl(t);

            const psl_kind_t kind = psl_kind(psl);
            if (kind != P_ASSERT && kind != P_ASSUME && kind != P_COVER)
               continue;

            ident_t name = tree_ident(t);
            ident_t sym = ident_prefix(sym_prefix, name, '.');

            rt_prop_t *p = xcalloc(sizeof(rt_prop_t));
            p->where    = tree_psl(t);
            p->handle   = jit_lazy_compile(m->jit, sym);
            p->scope    = s;
            p->name     = sym;
            p->label    = name;
            p->privdata = mptr_new(m->mspace, "property privdata");

            p->wakeable.kind    = W_PROPERTY;
            p->wakeable.pending = false;
            p->wakeable.delayed = false;

            APUSH(s->properties, p);
         }
         break;

      default:
         break;
      }
   }
}

void model_reset(rt_model_t *m)
{
   MODEL_ENTRY(m);

   // Re-read options as these may have changed
   m->stop_delta = opt_get_int(OPT_STOP_DELTA);
   m->shuffle    = opt_get_int(OPT_SHUFFLE_PROCS);

   __trace_on = opt_get_int(OPT_RT_TRACE);

   m->sv_poll_mem_dirty = true;
   m->sv_poll_susp_dirty = false;

   create_processes(m, m->root);

   nvc_rusage(&m->ready_rusage);

   // Initialisation is described in LRM 93 section 12.6.4

   m->proc_seq = 0;
   m->mt_sva_enabled = false;
   m->sva_mt_skip = 0;
   m->sva_batch.count = 0;
   m->sva_batch_tmp.count = 0;
   reset_scope(m, m->root);

   if (m->force_stop)
      return;   // Error in intialisation

   m->mt_sva_enabled = (opt_get_int(OPT_SIM_THREADS) > 1);

#if TRACE_SIGNALS > 0
   if (__trace_on)
      dump_signals(m, m->root);
#endif

   TRACE("calculate initial signal values");

   model_thread_t *thread = model_thread(m);
   thread->tlab = tlab_acquire(m->jit);

   // The signals in the model are updated as follows in an order such
   // that if a given signal R depends upon the current value of another
   // signal S, then the current value of S is updated prior to the
   // updating of the current value of R.

   for (rt_nexus_t *n = m->nexuses; n != NULL; n = n->chain) {
      // The initial value of each driver is the default value of the signal
      if (n->n_sources > 0) {
         for (rt_source_t *s = &(n->sources); s; s = s->chain_input) {
            if (s->tag == SOURCE_DRIVER)
               copy_value_ptr(n, &(s->u.driver.waveforms.value),
                              nexus_effective(n));
         }
      }

      if (n->rank > 0 || n->n_sources > 1)
         heap_insert(m->driving_heap, n->rank, n);
      else {
         calculate_initial_value(m, n);
         check_undriven_std_logic(n);
      }
   }

   while (heap_size(m->driving_heap) > 0) {
      rt_nexus_t *n = heap_extract_min(m->driving_heap);
      calculate_initial_value(m, n);
      check_undriven_std_logic(n);
   }

   // Update effective values after all initial driving values calculated
   while (heap_size(m->effective_heap) > 0) {
      rt_nexus_t *n = heap_extract_min(m->effective_heap);
      n->flags &= ~NET_F_PENDING;

      calculate_effective_value(m, n);
   }

   for (int i = 0; i < m->reschedq.count; i++)
      (*m->reschedq.tasks[i].fn)(m, m->reschedq.tasks[i].arg);
   m->reschedq.count = 0;

   tlab_reset(thread->tlab);   // No allocations can be live past here

   rt_island_reset_all(m->islands);

   if (m->islands != NULL) {
      m->in_reset = true;
      for (unsigned pass = 0; pass < 16; pass++) {
         bool progress = false;
         if (!rt_island_eval_sv_reset(m->jit, m, m->islands, thread->tlab,
                                      pass == 0, &progress)) {
            m->force_stop = true;
            break;
         }
         bool drained = false;
         do {
            for (int i = 0; i < m->reschedq.count; i++) {
               (*m->reschedq.tasks[i].fn)(m, m->reschedq.tasks[i].arg);
               drained = true;
            }
            m->reschedq.count = 0;
            while (heap_size(m->driving_heap) > 0) {
               rt_nexus_t *n = heap_extract_min(m->driving_heap);
               update_driving(m, n, true);
               drained = true;
            }
            while (heap_size(m->effective_heap) > 0) {
               rt_nexus_t *n = heap_extract_min(m->effective_heap);
               update_effective(m, n);
               drained = true;
            }
         } while (m->reschedq.count > 0);
         tlab_reset(thread->tlab);
         if (!progress && !drained)
            break;
      }
      m->in_reset = false;
      rt_island_mark_sv_reset_edges(m->islands);
   }

   run_callbacks(m, END_OF_INITIALISATION);
}

static void update_property(rt_model_t *m, rt_prop_t *prop)
{
   TRACE("update property %pi state %s", prop->name,
         trace_states(&prop->state));

   rt_wakeable_t *obj = &(prop->wakeable);

   if (obj->trigger != NULL && !run_trigger(m, obj->trigger))
      return;   // Filtered

   model_thread_t *thread = model_thread(m);
   assert(thread->tlab != NULL);

   thread->active_obj = obj;
   thread->active_scope = prop->scope;

   jit_scalar_t args[] = {
      { .pointer = *mptr_get(prop->privdata) ?: (void *)-1 },
      { .pointer = *mptr_get(prop->scope->privdata) },
      { .integer = -1 },
   };

   mask_clearall(&prop->newstate);
   prop->strong = false;

   size_t bit = -1;
   while (mask_iter(&prop->state, &bit)) {
      args[2].integer = bit;

      if (!jit_vfastcall(m->jit, prop->handle, args, ARRAY_LEN(args),
                         NULL, 0, thread->tlab))
         m->force_stop = true;
   }

   tlab_reset(thread->tlab);   // No allocations can be live past here

   thread->active_obj = NULL;
   thread->active_scope = NULL;

   TRACE("new state %s%s", trace_states(&prop->newstate),
         prop->strong ? " strong" : "");

   mask_copy(&prop->state, &prop->newstate);

   m->liveness |= prop->strong;
}

static inline void reg_pin(rt_wakeable_t *obj, void **pending)
{
   proc_pin(obj);

   if (obj->kind == W_PROC) {
      rt_proc_t *p = container_of(obj, rt_proc_t, wakeable);
      if (unlikely(p->task != NULL))
         sv_task_note_reg(p, pending);
   }
}

static inline void reg_unpin(rt_wakeable_t *obj, void **pending)
{
   proc_unpin(obj);

   if (obj->kind == W_PROC) {
      rt_proc_t *p = container_of(obj, rt_proc_t, wakeable);
      if (unlikely(p->task != NULL))
         sv_task_drop_reg(p, pending);
   }
}

static void note_wait(rt_model_t *m, rt_proc_t *proc)
{
   // Gives the order in which wake_order_cmp sorts the processes
   proc->wait_seq = ++(m->wait_seq);
}

static void pending_build_index(rt_pending_t *p)
{
   // A long list of wakeables has a hash table so that a process can be
   // added and removed without searching for it

   free(p->index);

   unsigned size = 32;
   while (size < p->max * 2)
      size *= 2;

   p->index = xcalloc_array(size, sizeof(uint32_t));
   p->index_size = size;
   p->index_used = 0;

   const unsigned mask = size - 1;
   for (unsigned i = 0; i < p->count; i++) {
      if (p->wake[i] == NULL)
         continue;

      unsigned h = mix_bits_64((uintptr_t)p->wake[i]) & mask;
      while (p->index[h] != 0)
         h = (h + 1) & mask;

      p->index[h] = i + 1;
      p->index_used++;
   }
}

static inline void pending_index_erase(rt_pending_t *p, unsigned h)
{
   // Backward-shift deletion avoids tombstones so lookups stay short
   const unsigned mask = p->index_size - 1;
   unsigned cur = h;
   for (unsigned next = (cur + 1) & mask; ; next = (next + 1) & mask) {
      const uint32_t e = p->index[next];
      if (e == 0) {
         p->index[cur] = 0;
         break;
      }

      const unsigned ideal = mix_bits_64((uintptr_t)p->wake[e - 1]) & mask;
      if (((cur - ideal) & mask) < ((next - ideal) & mask)) {
         p->index[cur] = e;
         cur = next;
      }
   }

   assert(p->index_used > 0);
   p->index_used--;
}

static inline void pending_shrink_tail(rt_pending_t *p)
{
   while (p->count > 0 && p->wake[p->count - 1] == NULL)
      p->count--;

   if (p->first_free > p->count)
      p->first_free = p->count;
}

static void pending_clear(rt_pending_t *p, unsigned slot)
{
   rt_wakeable_t *obj = p->wake[slot];
   assert(obj != NULL);

   if (p->index != NULL) {
      const unsigned mask = p->index_size - 1;
      for (unsigned h = mix_bits_64((uintptr_t)obj) & mask; ;
           h = (h + 1) & mask) {
         if (p->index[h] == slot + 1) {
            pending_index_erase(p, h);
            break;
         }
         assert(p->index[h] != 0);
      }
   }

   p->wake[slot] = NULL;

   if (slot < p->first_free)
      p->first_free = slot;
   if (slot + 1 == p->count)
      pending_shrink_tail(p);
}

static void sched_event(rt_model_t *m, void **pending, rt_wakeable_t *obj)
{
   if (obj->kind == W_PROC) {
      rt_proc_t *p = container_of(obj, rt_proc_t, wakeable);
      if (unlikely(p->verilog))
         note_wait(m, p);
   }

   if (*pending == NULL) {
      *pending = tag_pointer(obj, 1);
      reg_pin(obj, pending);
   }
   else if (pointer_tag(*pending) == 1) {
      rt_wakeable_t *cur = untag_pointer(*pending, rt_wakeable_t);
      if (cur == obj)
         return;

      reg_pin(obj, pending);

      rt_pending_t *p = xmalloc_flex(sizeof(rt_pending_t), PENDING_MIN,
                                     sizeof(rt_wakeable_t *));
      p->max = PENDING_MIN;
      p->count = 2;
      p->first_free = 2;
      p->index_size = p->index_used = 0;
      p->index = NULL;
      p->wake[0] = cur;
      p->wake[1] = obj;

      *pending = tag_pointer(p, 0);
   }
   else {
      rt_pending_t *p = untag_pointer(*pending, rt_pending_t);
      unsigned insert_h = 0;
      DEBUG_ONLY(bool have_h = false);

      if (p->index == NULL) {
         for (unsigned i = 0; i < p->count; i++) {
            if (p->wake[i] == obj)
               return;
         }
      }
      else {
         const unsigned mask = p->index_size - 1;
         unsigned h = mix_bits_64((uintptr_t)obj) & mask;
         for (; ; h = (h + 1) & mask) {
            const uint32_t e = p->index[h];
            if (e == 0) {
               insert_h = h;
               DEBUG_ONLY(have_h = true);
               break;
            }
            if (p->wake[e - 1] == obj)
               return;
         }
      }

      reg_pin(obj, pending);

      // Slots are reused lowest first so processes wake in the order they
      // began waiting
      unsigned slot = p->first_free;
      while (slot < p->count && p->wake[slot] != NULL)
         slot++;

      p->first_free = slot + 1;

      if (slot == p->count && p->count == p->max) {
         p->max = MAX(PENDING_MIN, p->max * 2);
         p = xrealloc_flex(p, sizeof(rt_pending_t), p->max,
                           sizeof(rt_wakeable_t *));
         *pending = tag_pointer(p, 0);
      }

      if (slot == p->count) {
         assert(p->count < p->max);
         p->count++;
      }

      p->wake[slot] = obj;

      if (p->index != NULL) {
         if (p->index_used * 4 >= p->index_size * 3
             || p->index_size < p->max * 2)
            pending_build_index(p);
         else {
            assert(have_h && p->index[insert_h] == 0);
            p->index[insert_h] = slot + 1;
            p->index_used++;
         }
      }
      else if (p->count >= PENDING_INDEX_MIN)
         pending_build_index(p);
   }
}

static void clear_event(rt_model_t *m, void **pending, rt_wakeable_t *obj)
{
   if (pointer_tag(*pending) == 1) {
      rt_wakeable_t *wake = untag_pointer(*pending, rt_wakeable_t);
      if (wake == obj) {
         *pending = NULL;
         reg_unpin(obj, pending);
      }
   }
   else if (*pending != NULL) {
      rt_pending_t *p = untag_pointer(*pending, rt_pending_t);
      if (p->index == NULL) {
         for (unsigned i = 0; i < p->count; i++) {
            if (p->wake[i] == obj) {
               if (i + 1 < p->count)
                  memmove(&p->wake[i], &p->wake[i + 1],
                          (p->count - i - 1) * sizeof(rt_wakeable_t *));
               p->count--;
               p->wake[p->count] = NULL;
               if (p->first_free > i)
                  p->first_free--;
               pending_shrink_tail(p);
               reg_unpin(obj, pending);
               return;
            }
         }
      }
      else {
         const unsigned mask = p->index_size - 1;
         for (unsigned h = mix_bits_64((uintptr_t)obj) & mask; ;
              h = (h + 1) & mask) {
            const uint32_t e = p->index[h];
            if (e == 0)
               return;
            const unsigned slot = e - 1;
            if (p->wake[slot] == obj) {
               pending_index_erase(p, h);
               p->wake[slot] = NULL;
               if (slot < p->first_free)
                  p->first_free = slot;
               if (slot + 1 == p->count)
                  pending_shrink_tail(p);
               reg_unpin(obj, pending);
               return;
            }
         }
      }
   }
}

static rt_source_t *find_driver(rt_nexus_t *nexus, rt_proc_t *proc)
{
   // Try to find this process in the list of existing drivers
   for (rt_source_t *d = &(nexus->sources); d; d = d->chain_input) {
      if (d->tag == SOURCE_DRIVER && d->u.driver.proc == proc)
         return d;
   }

   return NULL;
}

static bool insert_transaction(rt_model_t *m, rt_nexus_t *nexus,
                               rt_source_t *source, waveform_t *w,
                               uint64_t when, uint64_t reject)
{
   waveform_t *last = &(source->u.driver.waveforms);
   waveform_t *it   = last->next;
   while (it != NULL && it->when < when) {
      // If the current transaction is within the pulse rejection interval
      // and the value is different to that of the new transaction then
      // delete the current transaction
      assert(it->when >= m->now);
      if (it->when >= when - reject
          && !cmp_values(nexus, it->value, w->value)) {
         waveform_t *next = it->next;
         last->next = next;
         free_value(nexus, it->value);
         free_waveform(m, it);
         it = next;
      }
      else {
         last = it;
         it = it->next;
      }
   }
   last->next = w;

   // Delete all transactions later than this
   // We could remove this transaction from the deltaq as well but the
   // overhead of doing so is probably higher than the cost of waking
   // up for the empty event
   bool already_scheduled = false;
   for (waveform_t *next; it != NULL; it = next) {
      next = it->next;
      already_scheduled |= (it->when == when);
      free_value(nexus, it->value);
      free_waveform(m, it);
   }

   return already_scheduled;
}

static void queue_waveform(rt_model_t *m, waveform_t *last, waveform_t *w)
{
   waveform_t *it = last->next;
   while (it != NULL && it->when <= w->when) {
      assert(it->when >= m->now);
      last = it;
      it = it->next;
   }
   w->next = last->next;
   last->next = w;
}

static bool will_observe_active(rt_nexus_t *n, const void *value, waveform_t *w)
{
   if (!cmp_bytes(value, value_ptr(n, &w->value), n->width * n->size))
      return true;

   if (n->flags & NET_F_EFFECTIVE)
      return true;

   return false;
}

static void unfast_driver(rt_model_t *m, rt_nexus_t *n, rt_source_t *d)
{
   if ((n->flags & NET_F_FAST_DRIVER) && d->fastqueued) {
      // A fast update to this driver is already scheduled so convert the
      // pending value to an ordinary transaction before queueing others
      waveform_t *w0 = alloc_waveform(m);
      w0->when  = m->now;
      w0->next  = NULL;
      w0->value = alloc_value(m, n);

      const uint8_t *prev = value_ptr(n, &(d->u.driver.waveforms.value));
      copy_value_ptr(n, &w0->value, prev);

      assert(d->u.driver.waveforms.next == NULL);
      d->u.driver.waveforms.next = w0;
   }

   n->flags &= ~NET_F_FAST_DRIVER;
}

static void sched_driver(rt_model_t *m, rt_nexus_t *n, uint64_t after,
                         uint64_t reject, const void *value, rt_proc_t *proc)
{
   if (after == 0 && (n->flags & NET_F_FAST_DRIVER)) {
      rt_source_t *d = &(n->sources);
      assert(n->n_sources == 1);

      waveform_t *w = &d->u.driver.waveforms;
      w->when = m->now;
      assert(w->next == NULL);

      rt_signal_t *signal = n->signal;
      rt_source_t *d0 = &(signal->nexus.sources);

      if (d->fastqueued)
         assert(m->next_is_delta);
      else if ((signal->shared.flags & NET_F_FAST_DRIVER) && d0->sigqueued) {
         assert(m->next_is_delta);
         d->fastqueued = 1;
      }
      else if (!will_observe_active(n, value, w)) {
         m->next_is_delta = true;
         d->was_active = (n->active_delta == m->iteration);
         n->active_delta = m->iteration + 1;
         return;
      }
      else if (signal->shared.flags & NET_F_FAST_DRIVER) {
         deferq_do(&m->driverq, async_fast_all_drivers, signal);
         m->next_is_delta = true;
         d0->sigqueued = 1;
         d->fastqueued = 1;
      }
      else {
         deferq_do(&m->driverq, async_fast_driver, d);
         m->next_is_delta = true;
         d->fastqueued = 1;
      }

      copy_value_ptr(n, &w->value, value);
   }
   else {
      rt_source_t *d = find_driver(n, proc);
      assert(d != NULL);

      unfast_driver(m, n, d);

      waveform_t *w = alloc_waveform(m);
      w->when  = m->now + after;
      w->next  = NULL;
      w->value = alloc_value(m, n);

      copy_value_ptr(n, &w->value, value);

      if (!insert_transaction(m, n, d, w, w->when, reject))
         deltaq_insert_driver(m, after, d);
   }
}

static void sched_disconnect(rt_model_t *m, rt_nexus_t *nexus, uint64_t after,
                             uint64_t reject, rt_proc_t *proc)
{
   rt_source_t *d = find_driver(nexus, proc);
   assert(d != NULL);

   const uint64_t when = m->now + after;

   // Need update_driver to clear disconnected flag
   unfast_driver(m, nexus, d);

   waveform_t *w = alloc_waveform(m);
   w->when = -when;   // Use sign bit to represent null
   w->next = NULL;
   w->value.qword = 0;

   if (!insert_transaction(m, nexus, d, w, when, reject))
      deltaq_insert_driver(m, after, d);
}

static void async_watch_callback(rt_model_t *m, void *arg)
{
   rt_watch_t *w = arg;

   assert(w->wakeable.pending);
   w->wakeable.pending = false;

   if (w->wakeable.zombie)
      free(w);
   else if (unlikely(mtp.on)) {
      const mtp_phase_t ph = mtp_classify_fn((void *)w->fn, MTP_WATCH);
      const mtp_phase_t prev = mtp_enter(ph);
      mtp.runs[ph]++;
      (*w->fn)(m->now, w->signals[0], w, w->user_data);
      MTP_LEAVE(prev);
   }
   else
      (*w->fn)(m->now, w->signals[0], w, w->user_data);
}

static void async_timeout_callback(rt_model_t *m, void *arg)
{
   rt_callback_t *cb = arg;
   (*cb->fn)(m, cb->user);
   free(cb);
}

static void async_run_process(rt_model_t *m, void *arg)
{
   rt_proc_t *proc = arg;

   assert(proc->wakeable.pending);
   proc->wakeable.pending = false;

   run_process(m, proc);
}

static void async_update_property(rt_model_t *m, void *arg)
{
   rt_prop_t *prop = arg;

   assert(prop->wakeable.pending);
   prop->wakeable.pending = false;

   update_property(m, prop);
}

static bool heap_delete_proc_cb(uint64_t key, void *value, void *search)
{
   if (pointer_tag(value) != EVENT_PROCESS)
      return false;

   return untag_pointer(value, rt_proc_t) == search;
}

static bool run_trigger(rt_model_t *m, rt_trigger_t *t)
{
   if (t->epoch == m->trigger_epoch)
      return t->result.integer != 0;   // Cached

   switch (t->kind) {
   case FUNC_TRIGGER:
      {
         tlab_t tlab = jit_null_tlab(m->jit);
         if (!jit_vfastcall(m->jit, t->handle, t->args, t->nargs,
                            &t->result, 1, &tlab))
            m->force_stop = true;
         if (unlikely(tlab.mem_dirty))
            m->sv_poll_mem_dirty = true;

         TRACE("run trigger %p %pi ==> %"PRIi64, t,
               jit_get_name(m->jit, t->handle), t->result.integer);
      }
      break;

   case OR_TRIGGER:
      {
         rt_trigger_t *left = t->args[0].pointer;
         rt_trigger_t *right = t->args[1].pointer;
         t->result.integer = run_trigger(m, left) || run_trigger(m, right);

         TRACE("or trigger %p ==> %"PRIi64, t, t->result.integer);
      }
      break;

   case CMP_TRIGGER:
      {
         rt_signal_t *s = t->args[0].pointer;
         uint32_t offset = t->args[1].integer;
         int64_t right = t->args[2].integer;

#define COMPARE_SCALAR(type) do {                                       \
            const type *data = (type *)s->shared.data;                  \
            t->result.integer = (data[offset] == right);                \
      } while (0)

         FOR_ALL_SIZES(s->nexus.size, COMPARE_SCALAR);

         TRACE("cmp trigger %p ==> %"PRIi64, t, t->result.integer);
      }
      break;

   case LEVEL_TRIGGER:
      {
         rt_signal_t *s = t->args[0].pointer;
         uint32_t offset = t->args[1].integer;
         int32_t count = t->args[2].integer;

         t->result.integer = 0;

         if (unlikely(s->shared.flags & SIG_F_PACKED)) {
            rt_nexus_t *n = &(s->nexus);
            if (n->last_event == m->now && n->event_delta == m->iteration) {
               if (offset == 0 && (uint32_t)count >= s->bit_width)
                  t->result.integer = 1;
               else if (offset < s->bit_width && count > 0) {
                  uint32_t cnt = (uint32_t)count;
                  if (cnt > s->bit_width - offset)
                     cnt = s->bit_width - offset;
                  const uint32_t lsb_off = s->bit_width - offset - cnt;
                  const int w0 = lsb_off / 64;
                  const int w1 = (lsb_off + cnt - 1) / 64;
                  const bool is4 = !!(s->shared.flags & SIG_F_PACKED4);
                  const int nwords = (s->bit_width + 63) / 64;
                  const uint64_t *eff_a = (const uint64_t *)s->shared.data;
                  const uint64_t *eff_b = is4 ? eff_a + nwords : NULL;
                  const uint64_t *last_a =
                     (const uint64_t *)(s->shared.data + s->shared.size);
                  const uint64_t *last_b = is4 ? last_a + nwords : NULL;
                  for (int w = w0; w <= w1; w++) {
                     const uint32_t d_start = (w == w0) ? (lsb_off % 64) : 0;
                     const uint32_t d_end =
                        (w == w1) ? ((lsb_off + cnt - 1) % 64) : 63;
                     const uint32_t span = d_end - d_start + 1;
                     const uint64_t wmask = (span == 64)
                        ? ~UINT64_C(0)
                        : (((UINT64_C(1) << span) - 1) << d_start);
                     if (((eff_a[w] ^ last_a[w]) & wmask)
                         || (is4 && ((eff_b[w] ^ last_b[w]) & wmask))) {
                        t->result.integer = 1;
                        break;
                     }
                  }
               }
            }
         }
         else if (s->last_event_seq == m->event_seq) {
            rt_nexus_t *n = split_nexus(m, s, offset, count);
            for (; count > 0; n = n->chain) {
               if (n->last_event == m->now && n->event_delta == m->iteration) {
                  t->result.integer = 1;
                  break;
               }

               count -= n->width;
               assert(count >= 0);
            }
         }

         TRACE("level trigger %pi+%d ==> %"PRIi64, tree_ident(s->where),
               offset, t->result.integer);
      }
      break;
   }

   t->epoch = m->trigger_epoch;

   return t->result.integer != 0;
}

static void procq_do(rt_model_t *m, rt_wakeable_t *obj, defer_fn_t fn,
                     void *arg)
{
   if (unlikely(obj->postponed || obj->reschedule || obj->reactive)) {
      if (obj->postponed && obj->latepost)
         deferq_do(&m->latepostq, fn, arg);
      else if (obj->postponed)
         deferq_do(&m->postponedq, fn, arg);
      else if (obj->reschedule)
         deferq_do(&m->reschedq, fn, arg);
      else {
         deferq_do(&m->reactiveq, fn, arg);
         container_of(obj, rt_proc_t, wakeable)->wake_iter = m->iteration;
      }
   }
   else {
      deferq_do(&m->procq, fn, arg);
      m->next_is_delta |= m->blocking_update;
   }

   set_pending(obj);
}

static void wakeup_one(rt_model_t *m, rt_wakeable_t *obj)
{
   if (obj->pending)
      return;   // Already scheduled

   if (likely(obj->kind == W_PROC)) {
      rt_proc_t *proc = container_of(obj, rt_proc_t, wakeable);
      if (unlikely(m->skip_comb) && proc->comb)
         return;

      TRACE("%s %sprocess %s", obj->reschedule ? "reschedule" : "wakeup",
            obj->postponed ? "postponed " : "", istr(proc->name));

      if (unlikely(proc->wakeable.delayed)) {
         // This process was already scheduled to run at a later
         // time so we need to delete it from the simulation queue
         heap_delete(m->eventq_heap, heap_delete_proc_cb, proc);
         proc->wakeable.delayed = false;
      }

      if (unlikely(m->blocking_update) && tree_kind(proc->where) == T_PARAM) {
         // A Verilog process changed the source of this conversion so
         // run it with the signal updates in the next delta cycle
         deferq_do(&m->driverq, async_run_process, proc);
         set_pending(obj);
         m->next_is_delta = true;
      }
      else
         procq_do(m, obj, async_run_process, proc);
      return;
   }

   switch (obj->kind) {
   case W_PROC:
      should_not_reach_here();

   case W_PROPERTY:
      {
         rt_prop_t *prop = container_of(obj, rt_prop_t, wakeable);
         TRACE("wakeup property %s", istr(prop->name));
         procq_do(m, obj, async_update_property, prop);
      }
      break;

   case W_WATCH:
      {
         rt_watch_t *w = container_of(obj, rt_watch_t, wakeable);
         if (likely(w->inline_island_input)) {
            rt_island_input_watch_inline(m, w->user_data, m->blocking_update);
            break;
         }
         TRACE("wakeup %svalue change callback %p %s",
               obj->postponed ? "postponed " : "", w, debug_symbol_name(w->fn));

         assert(!w->wakeable.zombie);
         procq_do(m, obj, async_watch_callback, w);
      }
      break;

   case W_TRANSFER:
      {
         rt_transfer_t *t = container_of(obj, rt_transfer_t, wakeable);
         TRACE("wakeup signal transfer for %s",
               istr(tree_ident(t->target->signal->where)));

         procq_do(m, obj, async_transfer_signal, t);
      }
      break;

   case W_TRIGGER:
      {
         rt_trigger_t *t = container_of(obj, rt_trigger_t, wakeable);
         TRACE("wakeup trigger %p", t);

         if (!m->blocking_update) {
            deferq_do(&m->triggerq, async_run_trigger, t);
            set_pending(obj);
         }
         else if (t->level_only) {
            t->result.integer = 1;
            t->epoch = m->trigger_epoch;
            wakeup_all(m, &(t->pending));
         }
         else if (run_trigger(m, t))
            wakeup_all(m, &(t->pending));
      }
      break;
   }
}

typedef struct {
   rt_wakeable_t *wake;
   uint64_t       key;
} wake_entry_t;

static inline uint64_t wake_order_key(const rt_wakeable_t *w)
{
   // Non-Verilog wakeables sort first (key 0) in pending-list order;
   // Verilog processes follow in wait_seq order (IEEE 1800-2017 4.7).
   if (w->kind == W_PROC) {
      const rt_proc_t *p = container_of(w, const rt_proc_t, wakeable);
      if (p->verilog)
         return p->wait_seq + 1;
   }
   return 0;
}

static void wake_merge_sort(wake_entry_t *arr, int n)
{
   wake_entry_t *tmp LOCAL = xmalloc_array(n, sizeof(wake_entry_t));
   wake_entry_t *src = arr, *dst = tmp;

   for (int width = 1; width < n; width *= 2) {
      for (int i = 0; i < n; i += 2 * width) {
         const int mid = MIN(i + width, n);
         const int end = MIN(i + 2 * width, n);
         int a = i, b = mid, out = i;
         while (a < mid && b < end) {
            if (src[a].key <= src[b].key)
               dst[out++] = src[a++];
            else
               dst[out++] = src[b++];
         }
         while (a < mid)
            dst[out++] = src[a++];
         while (b < end)
            dst[out++] = src[b++];
      }
      wake_entry_t *swap = src;
      src = dst;
      dst = swap;
   }

   if (src != arr)
      memcpy(arr, src, n * sizeof(wake_entry_t));
}

__attribute__((noinline))
static void wakeup_all_ordered(rt_model_t *m, rt_pending_t *p)
{
   const unsigned count = p->count;
   wake_entry_t stack_buf[64];
   wake_entry_t *heap_buf = NULL;
   wake_entry_t *order = count <= ARRAY_LEN(stack_buf)
      ? stack_buf
      : (heap_buf = xmalloc_array(count, sizeof(wake_entry_t)));

   int n = 0;
   bool sorted = true;
   uint64_t prev_key = 0;

   for (unsigned i = 0; i < count; i++) {
      rt_wakeable_t *w = p->wake[i];
      if (w == NULL)
         continue;
      if (unlikely(w->forked && w->zombie)) {
         proc_unpin(w);
         pending_clear(p, i);
         continue;
      }

      const uint64_t k = wake_order_key(w);
      if (k < prev_key)
         sorted = false;
      prev_key = k;

      order[n].wake = w;
      order[n].key = k;
      n++;
   }

   if (!sorted) {
      if (n <= 32) {
         // Stable insertion sort for small unsorted lists
         for (int i = 1; i < n; i++) {
            const wake_entry_t key = order[i];
            int j = i - 1;
            for (; j >= 0 && order[j].key > key.key; j--)
               order[j + 1] = order[j];
            order[j + 1] = key;
         }
      }
      else
         wake_merge_sort(order, n);
   }

   for (int i = 0; i < n; i++)
      wakeup_one(m, order[i].wake);

   free(heap_buf);
}

static void wakeup_all(rt_model_t *m, void **pending)
{
   if (pointer_tag(*pending) == 1) {
      rt_wakeable_t *wake = untag_pointer(*pending, rt_wakeable_t);
      if (unlikely(wake->forked && wake->zombie)) {
         // A forked process which was stopped while it waited
         *pending = NULL;
         proc_unpin(wake);
      }
      else
         wakeup_one(m, wake);
   }
   else if (*pending != NULL) {
      rt_pending_t *p = untag_pointer(*pending, rt_pending_t);
      const unsigned count = p->count;
      if (count == 0)
         return;

      if (likely(!m->has_verilog)) {
         for (unsigned i = 0; i < count; i++) {
            rt_wakeable_t *w = p->wake[i];
            if (w != NULL)
               wakeup_one(m, w);
         }
      }
      else
         wakeup_all_ordered(m, p);
   }
}

static inline void set_event_flag(rt_model_t *m, rt_signal_t *s);

static void notify_event(rt_model_t *m, rt_nexus_t *n)
{
   n->last_event = m->now;
   n->event_delta = m->iteration;
   if (unlikely(m->has_level_trigger)
       && !(n->signal->shared.flags & SIG_F_PACKED))
      n->signal->last_event_seq = m->event_seq;
   if (unlikely(mtp.on))
      mtp.events++;

   // Triggers run synchronously in blocking update phase
   if (unlikely(m->blocking_update))
      m->trigger_epoch++;

   if (n->flags & NET_F_CACHE_EVENT)
      set_event_flag(m, n->signal);

   sv_poll_notify_sig(n->signal);

   wakeup_all(m, &(n->pending));
}

static void put_effective(rt_model_t *m, rt_nexus_t *n, const void *value)
{
   TRACE("update %s effective value %s", trace_nexus(n), fmt_nexus(n, value));

   unsigned char *eff = nexus_effective(n);
   unsigned char *last = nexus_last_value(n);

   const size_t valuesz = n->size * n->width;

   n->active_delta = m->iteration;

   if (!cmp_bytes(eff, value, valuesz)) {
      copy2(last, eff, value, valuesz);
      notify_event(m, n);
   }
}

static void enqueue_effective(rt_model_t *m, rt_nexus_t *n)
{
   if (n->flags & NET_F_PENDING)
      return;

   n->flags |= NET_F_PENDING;
   heap_insert(m->effective_heap, MAX_RANK - n->rank, n);
}

static void update_effective(rt_model_t *m, rt_nexus_t *n)
{
   n->active_delta = m->iteration;
   n->flags &= ~NET_F_PENDING;
   if (unlikely(mtp.on))
      mtp.nupd_eff++;

   calculate_effective_value(m, n);

   if (n->n_sources > 0) {
      for (rt_source_t *s = &(n->sources); s; s = s->chain_input) {
         if (s->tag != SOURCE_PORT)
            continue;
         else if (s->u.port.input->flags & NET_F_INOUT)
            enqueue_effective(m, s->u.port.input);
      }
   }
}

static void put_driving(rt_model_t *m, rt_nexus_t *n, const void *value)
{
   if (n->flags & NET_F_EFFECTIVE) {
      TRACE("update %s driving value %s", trace_nexus(n), fmt_nexus(n, value));

      memcpy(nexus_driving(n), value, n->size * n->width);

      n->flags &= ~NET_F_HAS_INITIAL;

      // May already be pending if a Verilog process drove this net twice
      enqueue_effective(m, n);
   }
   else
      put_effective(m, n, value);
}

static void defer_driving_update(rt_model_t *m, rt_nexus_t *n)
{
   // The pending flag is also set when only the effective value is
   // waiting to be updated, which does not recalculate the driving value
   if ((n->flags & NET_F_PENDING) && !(n->flags & NET_F_EFFECTIVE))
      return;

   TRACE("defer %s driving value update", trace_nexus(n));
   heap_insert(m->driving_heap, n->rank, n);
   n->flags |= NET_F_PENDING;
}

static void update_driving(rt_model_t *m, rt_nexus_t *n, bool safe)
{
   if (n->n_sources == 1 || safe) {
      n->active_delta = m->iteration;
      n->flags &= ~NET_F_PENDING;
      if (unlikely(mtp.on))
         mtp.nupd_drv++;

      calculate_driving_value(m, n);

      // Update output ports if the effective value must be calculated
      // separately or there was an event on this signal
      const bool update_output_ports = !!(n->flags & NET_F_EFFECTIVE)
         || (n->event_delta == m->iteration && n->last_event == m->now);

      for (rt_source_t *o = n->outputs; o; o = o->chain_output) {
         switch (o->tag) {
         case SOURCE_PORT:
            if (update_output_ports)
               update_driving(m, o->u.port.output, false);
            break;
         case SOURCE_ACTIVE:
            wakeup_one(m, o->u.wakeable);
            break;
         default:
            should_not_reach_here();
         }
      }
   }
   else
      defer_driving_update(m, n);
}

static void update_driver(rt_model_t *m, rt_nexus_t *n, rt_source_t *source)
{
   waveform_t *w_now  = &(source->u.driver.waveforms);
   waveform_t *w_next = w_now->next;

   if (likely(w_next != NULL && w_next->when == m->now)) {
      free_value(n, w_now->value);
      *w_now = *w_next;
      free_waveform(m, w_next);
      source->disconnected = 0;
      update_driving(m, n, false);
   }
   else if (unlikely(w_next != NULL && w_next->when == -m->now)) {
      // Disconnect source due to null transaction
      *w_now = *w_next;
      free_waveform(m, w_next);
      source->disconnected = 1;
      update_driving(m, n, false);
   }
}

static void fast_update_driver(rt_model_t *m, rt_nexus_t *nexus)
{
   rt_source_t *src = &(nexus->sources);

   if (likely(nexus->flags & NET_F_FAST_DRIVER)) {
      // Preconditions for fast driver updates
      assert(nexus->n_sources == 1);
      assert(src->tag == SOURCE_DRIVER);
      assert(src->u.driver.waveforms.next == NULL);

      update_driving(m, nexus, false);
   }
   else
      update_driver(m, nexus, src);

   assert(src->fastqueued);
   src->fastqueued = 0;
}

static void fast_update_all_drivers(rt_model_t *m, rt_signal_t *signal)
{
   assert(signal->shared.flags & NET_F_FAST_DRIVER);

   rt_nexus_t *n = &(signal->nexus);
   assert(n->sources.sigqueued);
   n->sources.sigqueued = 0;

   int count = 0;
   for (int i = 0; i < signal->n_nexus; i++, n = n->chain) {
      if (n->sources.fastqueued) {
         fast_update_driver(m, n);
         count++;
      }
   }

   if (count < signal->n_nexus >> 1) {
      // Unlikely to be worth the iteration cost
      signal->shared.flags &= ~NET_F_FAST_DRIVER;
   }
}

static void update_pseudo_source(rt_model_t *m, rt_source_t *src)
{
   assert(src->tag == SOURCE_FORCING || src->tag == SOURCE_DEPOSIT);

   rt_nexus_t *n = src->u.pseudo.nexus;
   waveform_t *w_now = &(src->u.pseudo.waveforms);

   if (w_now->next != NULL && w_now->next->when == m->now) {
      waveform_t *w_next = w_now->next;
      free_value(n, w_now->value);
      *w_now = *w_next;
      free_waveform(m, w_next);
      src->disconnected = 0;
   }
   else {
      assert(w_now->next == NULL || w_now->next->when > m->now);
      src->disconnected = !(n->flags & NET_F_FORCED);
      w_now->when = m->now;
   }

   update_driving(m, n, false);
}

static void async_update_driver(rt_model_t *m, void *arg)
{
   rt_source_t *src = arg;
   update_driver(m, src->u.driver.nexus, src);
}

static void async_fast_driver(rt_model_t *m, void *arg)
{
   rt_source_t *src = arg;
   fast_update_driver(m, src->u.driver.nexus);
}

static void async_fast_all_drivers(rt_model_t *m, void *arg)
{
   rt_signal_t *signal = arg;
   fast_update_all_drivers(m, signal);
}

static void async_pseudo_source(rt_model_t *m, void *arg)
{
   rt_source_t *src = arg;
   update_pseudo_source(m, src);
}

static void async_transfer_signal(rt_model_t *m, void *arg)
{
   rt_transfer_t *t = arg;

   assert(t->wakeable.pending);
   t->wakeable.pending = false;

   rt_nexus_t *n = t->target;
   char *vptr = nexus_effective(t->source);
   for (int count = t->count; count > 0; n = n->chain) {
      count -= n->width;
      assert(count >= 0);

      sched_driver(m, n, t->after, t->reject, vptr, t->proc);
      vptr += n->width * n->size;
   }
}

static void async_run_trigger(rt_model_t *m, void *arg)
{
   rt_trigger_t *t = arg;

   assert(t->wakeable.pending);
   t->wakeable.pending = false;

   if (t->level_only) {
      t->result.integer = 1;
      t->epoch = m->trigger_epoch;
      wakeup_all(m, &(t->pending));
   }
   else if (run_trigger(m, t))
      wakeup_all(m, &(t->pending));
}

static void iteration_limit_proc_cb(void *fn, void *arg, void *extra)
{
   diag_t *d = extra;
   rt_proc_t *proc = NULL;

   if (fn == async_run_process)
      proc = arg;
   else if (fn == async_transfer_signal) {
      rt_transfer_t *t = arg;
      proc = t->proc;
   }

   if (proc == NULL)
      return;

   const loc_t *loc = tree_loc(proc->where);
   diag_hint(d, loc, "process %s is active", istr(proc->name));
}

static void iteration_limit_driver_cb(void *fn, void *arg, void *extra)
{
   diag_t *d = extra;
   tree_t decl = NULL;

   if (fn == async_update_driver || fn == async_fast_driver) {
      rt_source_t *src = arg;
      if (src->tag == SOURCE_DRIVER)
         decl = src->u.driver.nexus->signal->where;
   }
   else if (fn == async_fast_all_drivers) {
      rt_signal_t *s = arg;
      decl = s->where;
   }

   if (decl == NULL)
      return;

   diag_hint(d, tree_loc(decl), "driver for %s %s is active",
             tree_kind(decl) == T_PORT_DECL ? "port" : "signal",
             istr(tree_ident(decl)));
}

static void reached_iteration_limit(rt_model_t *m)
{
   diag_t *d = diag_new(DIAG_FATAL, NULL);

   diag_printf(d, "limit of %d delta cycles reached", m->stop_delta);

   deferq_scan(&m->procq, iteration_limit_proc_cb, d);
   deferq_scan(&m->reschedq, iteration_limit_proc_cb, d);
   deferq_scan(&m->driverq, iteration_limit_driver_cb, d);

   diag_hint(d, NULL, "you can increase this limit with $bold$--stop-delta$$");
   diag_emit(d);

   m->force_stop = true;
}

static inline void set_event_flag(rt_model_t *m, rt_signal_t *s)
{
   // The signals with the flag set are kept in a list so that the flag can
   // be cleared again without visiting every signal that caches events
   if (!(s->shared.flags & SIG_F_EVENT_FLAG)) {
      s->shared.flags |= SIG_F_EVENT_FLAG;
      APUSH(m->eventsigs, s);
   }
}

static void sync_event_cache(rt_model_t *m)
{
   // Only the signals whose event flag is set need to be visited

   unsigned keep = 0;
   for (int i = 0; i < m->eventsigs.count; i++) {
      rt_signal_t *s = m->eventsigs.items[i];
      assert(s->shared.flags & SIG_F_CACHE_EVENT);
      assert(s->shared.flags & SIG_F_EVENT_FLAG);

      const bool event = s->nexus.last_event == m->now
         && s->nexus.event_delta == m->iteration;

      TRACE("sync event flag %d for %s", event, istr(tree_ident(s->where)));

      if (event)
         m->eventsigs.items[keep++] = s;
      else
         s->shared.flags &= ~SIG_F_EVENT_FLAG;
   }

   ARESIZE(m->eventsigs, keep);
}

static void sv_poll_end_cycle(rt_model_t *m)
{
   // Conditions are checked again in the delta cycle following any activity.
   // A cycle where every process that ran was a poller which went straight
   // back to waiting has changed nothing, which lets an otherwise idle
   // simulation finish.

   const unsigned total_watched =
      sv_watched_sig_only.count + sv_watched_mem.count;
   const unsigned total_pollers = sv_pollers.count + total_watched;
   const bool idle = sv_pollers_woken > 0
      && sv_procs_run == sv_pollers_woken
      && total_pollers == sv_pollers_kept + sv_pollers_woken
      && sv_pollers_fired == 0
      && m->nonblockq.count == 0 && m->nb_packed_q.count == 0
      && m->inactiveq.count == 0;

   sv_procs_run = 0;
   sv_pollers_woken = 0;
   sv_pollers_fired = 0;

   if (total_pollers == 0 || idle) {
      sv_pollers_kept = total_pollers;
      if (idle) {
         m->sv_poll_mem_dirty = false;
         m->sv_poll_susp_dirty = false;
         if (sv_sig_dirty_q.count > 0) {
            for (unsigned i = 0; i < sv_sig_dirty_q.count; i++)
               sv_sig_dirty_q.items[i]->watch->in_dirty_q = 0;
            sv_sig_dirty_q.count = 0;
            sv_sig_dirty_gen++;
         }
      }
      return;
   }

   // Conditions are checked again in the order they began waiting.
   // Signal-only pollers wake only via per-signal dirty notification;
   // memory-watching pollers are scanned only on MACRO_MEM_DIRTY.
   const bool susp_dirty = m->sv_poll_susp_dirty;
   const bool mem_dirty = m->sv_poll_mem_dirty;

   if (sv_pollers.count == 0
       && sv_sig_dirty_q.count == 0
       && (!mem_dirty || sv_watched_mem.count == 0)
       && !susp_dirty
       && sv_watched_defer.count == 0) {
      sv_pollers_kept = total_pollers;
      m->sv_poll_mem_dirty = false;
      return;
   }

   // 1. Drain signal-dirty pollers (both signal-only and mixed sig+mem).
   if (sv_sig_dirty_q.count > 0) {
      while (sv_sig_dirty_q.count > 0) {
         rt_proc_t *p = sv_sig_dirty_q.items[--sv_sig_dirty_q.count];
         sv_watch_t *w = p->watch;
         assert(w != NULL && w->in_watched && w->in_dirty_q);
         w->in_dirty_q = 0;

         const rt_wakeable_t *obj = &(p->wakeable);
         if (unlikely(obj->forked && obj->zombie)) {
            sv_watch_poller_unlink(p);
            continue;
         }

         bool unchanged = false;
         if (likely(!obj->interrupted && !obj->pending
                    && obj->trigger == NULL
                    && (!susp_dirty || !sv_task_is_suspended(p))))
            unchanged = sv_watch_fast_unchanged(w);

         if (!unchanged) {
            sv_watch_poller_unlink(p);
            APUSH(sv_wake_buf, p);
         }
      }
      sv_sig_dirty_gen++;
   }

   // 2. Scan memory-watching pollers only when non-signal memory changed.
   if (mem_dirty && sv_watched_mem.count > 0) {
      for (unsigned i = 0; i < sv_watched_mem.count; ) {
         sv_watch_poller_t e = sv_watched_mem.items[i];
         rt_proc_t *p = e.proc;
         const rt_wakeable_t *obj = &(p->wakeable);

         if (unlikely(obj->forked && obj->zombie)) {
            sv_watch_poller_unlink(p);
            continue;
         }

         if (likely(!obj->interrupted && !obj->pending
                    && obj->trigger == NULL
                    && (!susp_dirty || !sv_task_is_suspended(p)))) {
            bool unchanged;
            if (likely(e.sz0 != 0)) {
               unchanged = sv_poller_inline_eq(&e);
               if (unchanged && e.count > 1) {
                  const sv_watch_t *w = p->watch;
                  for (unsigned j = 1; j < w->count; j++) {
                     if (!sv_watch_item_unchanged(&w->items[j])) {
                        unchanged = false;
                        break;
                     }
                  }
               }
            }
            else
               unchanged = sv_watch_unchanged(p->watch);

            if (likely(unchanged)) {
               i++;
               continue;
            }
         }

         sv_watch_poller_unlink(p);
         APUSH(sv_wake_buf, p);
      }
   }

   // 3. Rare slow path: a parked process was suspended via process::suspend().
   if (unlikely(susp_dirty)) {
      for (unsigned i = 0; i < sv_watched_sig_only.count; ) {
         rt_proc_t *p = sv_watched_sig_only.items[i];
         const rt_wakeable_t *obj = &(p->wakeable);
         if (unlikely(obj->forked && obj->zombie)) {
            sv_watch_poller_unlink(p);
            continue;
         }
         if (obj->interrupted || obj->pending || obj->trigger != NULL
             || sv_task_is_suspended(p)) {
            sv_watch_poller_unlink(p);
            APUSH(sv_wake_buf, p);
            continue;
         }
         i++;
      }

      if (!mem_dirty) {
         for (unsigned i = 0; i < sv_watched_mem.count; ) {
            rt_proc_t *p = sv_watched_mem.items[i].proc;
            const rt_wakeable_t *obj = &(p->wakeable);
            if (unlikely(obj->forked && obj->zombie)) {
               sv_watch_poller_unlink(p);
               continue;
            }
            if (obj->interrupted || obj->pending || obj->trigger != NULL
                || sv_task_is_suspended(p)) {
               sv_watch_poller_unlink(p);
               APUSH(sv_wake_buf, p);
               continue;
            }
            i++;
         }
      }
   }

   // 4. Advance skipped-poll counters for parked pollers with pending defers.
   if (unlikely(sv_watched_defer.count > 0)) {
      for (unsigned i = 0; i < sv_watched_defer.count; ) {
         rt_proc_t *p = sv_watched_defer.items[i];
         if (sv_task_has_pending_defers(p)) {
            sv_task_note_skipped_poll(p);
            i++;
         }
         else {
            sv_watch_t *w = p->watch;
            rt_proc_t *last = sv_watched_defer.items[--sv_watched_defer.count];
            if (i < sv_watched_defer.count) {
               sv_watched_defer.items[i] = last;
               last->watch->defer_idx = i;
            }
            w->has_defer = 0;
         }
      }
   }

   // 5. Enqueue woken watched pollers in deterministic wait_seq order.
   unsigned woken = 0;
   if (sv_wake_buf.count > 0) {
      if (sv_wake_buf.count > 1) {
         for (unsigned i = 1; i < sv_wake_buf.count; i++) {
            rt_proc_t *key = sv_wake_buf.items[i];
            const uint64_t key_seq = key->wait_seq;
            unsigned j = i;
            while (j > 0 && sv_wake_buf.items[j - 1]->wait_seq > key_seq) {
               sv_wake_buf.items[j] = sv_wake_buf.items[j - 1];
               j--;
            }
            sv_wake_buf.items[j] = key;
         }
      }

      for (unsigned i = 0; i < sv_wake_buf.count; i++) {
         rt_proc_t *p = sv_wake_buf.items[i];
         p->poll_wake = 1;
         deltaq_insert_proc(m, 0, p);
         woken++;
      }
      sv_wake_buf.count = 0;
   }

   for (int i = 0; i < sv_pollers.count; i++) {
      rt_proc_t *p = sv_pollers.items[i];
      if (unlikely(p->wakeable.forked && p->wakeable.zombie))
         continue;

      p->poll_wake = 1;
      deltaq_insert_proc(m, 0, p);
      woken++;
   }

   ARESIZE(sv_pollers, 0);

   sv_pollers_woken = woken;
   sv_pollers_kept = sv_watched_sig_only.count + sv_watched_mem.count;
   m->sv_poll_mem_dirty = false;
   m->sv_poll_susp_dirty = false;
}

static void fire_init_events(rt_model_t *m)
{
   // A net driven to 0 or 1 when the simulation starts has an event so
   // processes waiting for it run in the next delta cycle whatever order
   // they were started in.  A net which is still z has not changed from
   // its initial value.

   for (int i = 0; i < m->init_events.count; i++) {
      const init_event_t *ev = &(m->init_events.items[i]);

      if (unlikely(ev->signal->shared.flags & SIG_F_PACKED)) {
         rt_signal_t *s = ev->signal;
         rt_nexus_t *n = &(s->nexus);
         if (n->flags & NET_F_EFFECTIVE)
            continue;

         bool known = false;
         if (s->shared.flags & SIG_F_PACKED2)
            known = s->bit_width > 0;
         else {
            const uint32_t w = s->bit_width;
            const int nwords = (w + 63) / 64;
            const uint64_t *bbits = (const uint64_t *)s->shared.data + nwords;
            for (int j = 0; j < nwords && !known; j++) {
               const uint64_t mask = (j == nwords - 1 && (w & 63))
                  ? ((UINT64_C(1) << (w & 63)) - 1) : ~UINT64_C(0);
               if ((~bbits[j]) & mask)
                  known = true;
            }
         }

         if (known) {
            notify_event(m, n);
            m->next_is_delta = true;
         }
         continue;
      }

      int remaining = ev->count;
      rt_nexus_t *n = split_nexus(m, ev->signal, ev->offset, ev->count);
      for (; remaining > 0; n = n->chain) {
         remaining -= n->width;

         if (n->flags & NET_F_EFFECTIVE)
            continue;

         const unsigned char *eff = nexus_effective(n);
         bool known = false;
         for (int j = 0; j < n->size * n->width; j++)
            known |= (eff[j] & 2) == 0;   // Neither x nor z

         if (known) {
            notify_event(m, n);
            m->next_is_delta = true;
         }
      }
   }

   m->init_events.count = 0;
}

static void drain_nb_packed(rt_model_t *m);

static void reactive_sort(deferq_t *dq)
{
   for (int i = 1; i < dq->count; i++) {
      const defer_task_t key = dq->tasks[i];
      const rt_proc_t *kp = key.arg;
      int j = i - 1;
      for (; j >= 0; j--) {
         const rt_proc_t *jp = dq->tasks[j].arg;
         if (jp->wake_iter < kp->wake_iter
             || (jp->wake_iter == kp->wake_iter
                 && jp->wait_seq <= kp->wait_seq))
            break;
         dq->tasks[j + 1] = dq->tasks[j];
      }
      dq->tasks[j + 1] = key;
   }
}

static int sva_batch_cmp(const void *pa, const void *pb)
{
   const rt_sva_batch_item_t *a = pa;
   const rt_sva_batch_item_t *b = pb;
   if (a->proc->create_seq < b->proc->create_seq)
      return -1;
   if (a->proc->create_seq > b->proc->create_seq)
      return 1;
   return 0;
}

static void model_flush_sva_batch(rt_model_t *m)
{
   while (m->sva_batch.count > 0) {
      sva_batch_list_t work = m->sva_batch;
      m->sva_batch = m->sva_batch_tmp;
      m->sva_batch.count = 0;
      m->sva_batch_tmp = work;

      const uint32_t skip = rt_island_eval_sva_batch(
         m->jit, m, &m->islands, work.items, work.count,
         model_thread(m)->tlab);
      if (skip > 0)
         m->sva_mt_skip = skip;

      bool sorted = true;
      for (unsigned i = 1; i < work.count; i++) {
         if (work.items[i - 1].proc->create_seq
             > work.items[i].proc->create_seq) {
            sorted = false;
            break;
         }
      }

      if (!sorted) {
         if (work.count <= 32) {
            for (unsigned i = 1; i < work.count; i++) {
               const rt_sva_batch_item_t key = work.items[i];
               const uint32_t kseq = key.proc->create_seq;
               int j = (int)i - 1;
               while (j >= 0 && work.items[j].proc->create_seq > kseq) {
                  work.items[j + 1] = work.items[j];
                  j--;
               }
               work.items[j + 1] = key;
            }
         }
         else
            qsort(work.items, work.count, sizeof(rt_sva_batch_item_t),
                  sva_batch_cmp);
      }

      for (unsigned i = 0; i < work.count; i++) {
         rt_proc_t *proc = work.items[i].proc;
         proc->wakeable.pending = true;
         deferq_do(&m->next_procq, async_run_process, proc);
      }
      m->sva_batch_tmp.count = 0;

      deferq_run_procs(m, &m->next_procq);
   }
}

static void model_cycle_body(rt_model_t *m)
{
   // Simulation cycle is described in LRM 93 section 12.6.4

   const bool is_delta_cycle = m->next_is_delta;
   m->next_is_delta = false;

   if (is_delta_cycle)
      m->iteration = m->iteration + 1;
   else {
      m->now = heap_min_key(m->eventq_heap);
      m->iteration = 0;
      if (unlikely(m->sva_mt_skip > 0))
         m->sva_mt_skip--;
   }
   if (unlikely(m->has_level_trigger) && unlikely(++m->event_seq == 0)) {
      m->event_seq = 1;
      for (rt_nexus_t *nx = m->nexuses; nx != NULL; nx = nx->chain) {
         if (!(nx->signal->shared.flags & SIG_F_PACKED))
            nx->signal->last_event_seq = 0;
      }
   }

   m->blocking_update = false;

   TRACE("begin cycle");

#if TRACE_DELTAQ > 0
   if (__trace_on)
      deltaq_dump(m);
#endif

   if (unlikely(sv_reap.count >= sv_reap_next))
      sv_reap_procs(m);

   if (m->iteration == 0)
      run_callbacks(m, NEXT_TIME_STEP);

   run_callbacks(m, NEXT_CYCLE);

   (void)mtp_enter(MTP_SCHED);

   if (!is_delta_cycle) {
      for (;;) {
         void *e = heap_extract_min(m->eventq_heap);
         switch (pointer_tag(e)) {
         case EVENT_PROCESS:
            {
               rt_proc_t *proc = untag_pointer(e, rt_proc_t);
               if (stale_events.count > 0 && stale_event_pop(proc, m->now))
                  break;
               assert(proc->wakeable.delayed);
               proc->wakeable.delayed = false;
               procq_do(m, &proc->wakeable, async_run_process, proc);
            }
            break;
         case EVENT_DRIVER:
            {
               rt_source_t *source = untag_pointer(e, rt_source_t);
               deferq_do(&m->driverq, async_update_driver, source);
            }
            break;
         case EVENT_TIMEOUT:
            {
               rt_callback_t *cb = untag_pointer(e, rt_callback_t);
               deferq_do(&m->driverq, async_timeout_callback, cb);
            }
            break;
         case EVENT_PSEUDO:
            {
               rt_source_t *source = untag_pointer(e, rt_source_t);
               deferq_do(&m->driverq, async_pseudo_source, source);
            }
            break;
         case EVENT_NONBLOCK:
            {
               // A delayed non-blocking assignment updates in the
               // non-blocking region of the time step it matures in
               rt_source_t *source = untag_pointer(e, rt_source_t);
               deferq_do(&m->nonblockq, async_pseudo_source, source);
            }
            break;
         case EVENT_PACKED_DEPOSIT:
            {
               rt_signal_t *s = untag_pointer(e, rt_signal_t);
               rt_packed_sched_t *ps = pop_packed_delayed(m, s, false);
               assert(ps->when == m->now);
               deferq_do(&m->driverq, async_packed_deposit, ps);
            }
            break;
         case EVENT_PACKED_NONBLOCK:
            {
               rt_signal_t *s = untag_pointer(e, rt_signal_t);
               rt_packed_sched_t *ps = pop_packed_delayed(m, s, true);
               assert(ps->when == m->now);
               s->shared.flags |= SIG_F_NB_DEFERQ;
               deferq_do(&m->nonblockq, async_packed_deposit, ps);
            }
            break;
         }

         if (heap_size(m->eventq_heap) == 0)
            break;
         else if (heap_min_key(m->eventq_heap) > m->now)
            break;
      }
   }

   (void)mtp_enter(MTP_DRIVERQ);
   deferq_swap(&m->next_driverq, &m->driverq);
   deferq_run(m, &m->next_driverq);

   unsigned resched_iters = 0;
   do {
      for (int i = 0; i < m->reschedq.count; i++)
         (*m->reschedq.tasks[i].fn)(m, m->reschedq.tasks[i].arg);
      m->reschedq.count = 0;

      (void)mtp_enter(MTP_UPD_DRV);
      while (heap_size(m->driving_heap) > 0) {
         rt_nexus_t *n = heap_extract_min(m->driving_heap);
         update_driving(m, n, true);
      }

      (void)mtp_enter(MTP_UPD_EFF);
      while (heap_size(m->effective_heap) > 0) {
         rt_nexus_t *n = heap_extract_min(m->effective_heap);
         update_effective(m, n);
      }
      (void)mtp_enter(MTP_DRIVERQ);
      if (m->reschedq.count > 0 && unlikely(m->stop_delta > 0
                                            && (uint64_t)MAX(m->iteration, 0)
                                               + ++resched_iters
                                               >= (uint64_t)m->stop_delta)) {
         if (m->iteration < (int)m->stop_delta)
            m->iteration = m->stop_delta;
         reached_iteration_limit(m);
         m->reschedq.count = 0;
         break;
      }
   } while (m->reschedq.count > 0);

   (void)mtp_enter(MTP_OTHER);
   sync_event_cache(m);

   m->blocking_update = true;

   assert(model_thread(m)->tlab->alloc == 0);

#if TRACE_SIGNALS > 0
   if (__trace_on)
      dump_signals(m, m->root);
#endif

   m->trigger_epoch++;
   deferq_run(m, &m->triggerq);  // Sensitivity list filter

   run_callbacks(m, START_OF_PROCESSES);

   if (unlikely(m->islands != NULL)) {
      rt_island_promote_deferred(m->islands);
      if (unlikely(m->has_verilog) && rt_island_has_sv_dirty(m->islands)) {
         for (unsigned pass = 0; pass < 16; pass++) {
            if (!rt_island_eval_sv_dirty(m->jit, m, m->islands,
                                         model_thread(m)->tlab)) {
               m->force_stop = true;
               break;
            }
            if (m->reschedq.count > 0) {
               deferq_swap(&m->next_procq, &m->reschedq);
               deferq_run_procs(m, &m->next_procq);
               if (unlikely(m->sva_batch.count > 0))
                  model_flush_sva_batch(m);
            }
            rt_island_promote_deferred(m->islands);
            if (!rt_island_has_sv_dirty(m->islands))
               break;
         }
         if (unlikely(m->force_stop))
            return;
      }
   }

   if (m->shuffle)
      deferq_shuffle(&m->procq);

   // Run all non-postponed processes and event callbacks
   deferq_swap(&m->next_procq, &m->procq);
   (void)mtp_enter(MTP_TASKQ);
   deferq_run_procs(m, &m->next_procq);
   if (unlikely(m->sva_batch.count > 0))
      model_flush_sva_batch(m);
   (void)mtp_enter(MTP_OTHER);

   if (unlikely(m->islands != NULL)) {
      if (!rt_island_eval_dirty(m->jit, m, m->islands, model_thread(m)->tlab))
         m->force_stop = true;
      else if (m->reschedq.count > 0) {
         deferq_swap(&m->next_procq, &m->reschedq);
         deferq_run_procs(m, &m->next_procq);
         if (unlikely(m->sva_batch.count > 0))
            model_flush_sva_batch(m);
      }
      if (!m->force_stop && rt_island_has_ff_sample(m->islands)) {
         if (!rt_island_eval_ff(m->jit, m, m->islands, model_thread(m)->tlab))
            m->force_stop = true;
      }
      if (rt_island_has_dirty(m->islands) || heap_size(m->driving_heap) > 0
          || heap_size(m->effective_heap) > 0)
         m->next_is_delta = true;
   }

   run_callbacks(m, END_OF_PROCESSES);

   if (m->init_events.count > 0)
      fire_init_events(m);

   // Verilog scheduling regions

   if (m->next_is_delta)
      goto next_delta;

   if (unlikely(m->inactiveq.count > 0)) {
      TRACE("begin inactive region");
      deferq_swap(&m->next_inactiveq, &m->inactiveq);
      (void)mtp_enter(MTP_TASKQ);
      deferq_run_procs(m, &m->next_inactiveq);
      if (unlikely(m->sva_batch.count > 0))
         model_flush_sva_batch(m);
      (void)mtp_enter(MTP_OTHER);
   }

   if (m->next_is_delta)
      goto next_delta;

   if (unlikely(m->nonblockq.count > 0 || m->nb_packed_q.count > 0
                || (m->islands != NULL && rt_island_has_ff_commits(m->islands)))) {
      TRACE("begin non-blocking assignment region");
      (void)mtp_enter(MTP_NBA);
      if (m->nb_packed_q.count > 0) {
         drain_nb_packed(m);
         if (m->nonblockq.count == 0 && m->reschedq.count > 0)
            deferq_swap(&m->reschedq, &m->nonblockq);
      }
      if (m->nonblockq.count > 0)
         deferq_run(m, &m->nonblockq);
      if (m->islands != NULL && rt_island_has_ff_commits(m->islands)) {
         rt_island_commit_ff(m, m->islands, model_thread(m)->tlab);
         if (m->reschedq.count > 0) {
            deferq_swap(&m->reschedq, &m->nonblockq);
            deferq_run(m, &m->nonblockq);
         }
         if (rt_island_has_dirty(m->islands))
            m->next_is_delta = true;
      }
      (void)mtp_enter(MTP_OTHER);
      sv_procs_run++;   // May have changed state a poller depends on

      // A net with several sources updates its driving value in the
      // next delta cycle
      if (heap_size(m->driving_heap) > 0)
         m->next_is_delta = true;
   }

   if (!m->next_is_delta && unlikely(m->reactiveq.count > 0)) {
      // Program processes run once nothing else is scheduled in the time
      // step, in the order they began waiting, and may then schedule more
      // events
      TRACE("begin reactive region");
      reactive_sort(&m->reactiveq);
      deferq_swap(&m->procq, &m->reactiveq);
      m->next_is_delta = true;
   }

 next_delta:
   if (unlikely(m->has_verilog))
      sv_poll_end_cycle(m);

   if (!m->next_is_delta) {
      if (m->samplers != NULL) {
         (void)mtp_enter(MTP_PHASECB);
         run_samplers(m);
         (void)mtp_enter(MTP_OTHER);
      }

      run_callbacks(m, LAST_KNOWN_DELTA_CYCLE);
   }

   if (!m->next_is_delta) {
      m->can_create_delta = false;

      // Run all postponed processes and event callbacks
      (void)mtp_enter(MTP_TASKQ);
      deferq_run_procs(m, &m->postponedq);
      deferq_run_procs(m, &m->latepostq);
      (void)mtp_enter(MTP_OTHER);

      run_callbacks(m, END_TIME_STEP);

      m->can_create_delta = true;
   }
   else if (!m->force_stop && m->stop_delta > 0 && m->iteration == m->stop_delta)
      reached_iteration_limit(m);
}

static void model_cycle(rt_model_t *m)
{
   if (likely(!mtp.on)) {
      model_cycle_body(m);
      return;
   }

   const bool is_delta = m->next_is_delta;
   mtp_cycle_begin();
   model_cycle_body(m);
   mtp_cycle_end(is_delta);
}

static bool should_stop_now(rt_model_t *m, uint64_t stop_time)
{
   if (m->force_stop) {
      // Make sure we print the interrupted message if this was the
      // result of an interrupt
      jit_check_interrupt(m->jit);
      return true;
   }
   else if (m->next_is_delta)
      return false;
   else if (heap_size(m->eventq_heap) == 0)
      return true;
   else
      return heap_min_key(m->eventq_heap) > stop_time;
}

static void check_liveness_properties(rt_model_t *m, rt_scope_t *s)
{
   model_thread_t *thread = model_thread(m);

   for (int i = 0; i < s->properties.count; i++) {
      rt_prop_t *p = s->properties.items[i];
      if (p->strong) {
         TRACE("property %s in strong state", istr(p->name));

         // Passing an invalid state triggers the assertion failure
         jit_scalar_t args[] = {
            { .pointer = *mptr_get(p->privdata) ?: (void *)-1 },
            { .pointer = *mptr_get(p->scope->privdata) },
            { .integer = INT_MAX },
         };
         jit_vfastcall(m->jit, p->handle, args, ARRAY_LEN(args),
                       NULL, 0, thread->tlab);
      }
   }

   for (int i = 0; i < s->children.count; i++)
      check_liveness_properties(m, s->children.items[i]);
}

static void run_final_procs(rt_model_t *m, rt_scope_t *s)
{
   // Instances are visited before their parents

   for (int i = 0; i < s->children.count; i++)
      run_final_procs(m, s->children.items[i]);

   for (int i = 0; i < s->procs.count; i++) {
      rt_proc_t *p = s->procs.items[i];
      if (tree_kind(p->where) == T_VERILOG
          && vlog_kind(tree_vlog(p->where)) == V_FINAL)
         run_process(m, p);
   }
}

void model_run(rt_model_t *m, uint64_t stop_time)
{
   MODEL_ENTRY(m);

   if (m->force_stop)
      return;   // Was error during intialisation

   mtp_start();

   run_callbacks(m, START_OF_SIMULATION);

   while (!should_stop_now(m, stop_time))
      model_cycle(m);

   if (m->nsva_procs > 0 && (!m->force_stop || m->finished)) {
      // Attempts of strong properties that have not completed fail
      m->sva_flush = true;
      for (unsigned i = 0; i < m->nsva_procs; i++)
         run_process(m, m->sva_procs[i]);
      m->sva_flush = false;
   }

   if (m->nfinal_procs > 0 && (!m->force_stop || m->finished)) {
      // Final procedures run once at the end of simulation
      if (!m->force_stop && stop_time != TIME_HIGH && m->now < stop_time)
         m->now = stop_time;

      m->nfinal_procs = 0;
      run_final_procs(m, m->root);
   }

   run_callbacks(m, END_OF_SIMULATION);

   if (m->liveness)
      check_liveness_properties(m, m->root);

   mtp_stop();
}

bool model_step(rt_model_t *m)
{
   MODEL_ENTRY(m);

   if (!m->force_stop) {
      mtp_start();
      model_cycle(m);
      mtp_stop();
   }

   return should_stop_now(m, TIME_HIGH);
}

static inline void check_postponed(int64_t after, rt_proc_t *proc)
{
   if (unlikely(proc->wakeable.postponed && after == 0))
      fatal("postponed process %s cannot cause a delta cycle",
            istr(proc->name));
}

static inline void check_reject_limit(rt_signal_t *s, uint64_t after,
                                      uint64_t reject)
{
   if (unlikely(reject > after))
      jit_msg(NULL, DIAG_FATAL, "signal %s pulse reject limit %s is greater "
              "than delay %s", istr(tree_ident(s->where)),
              trace_time(reject), trace_time(after));
}

static inline void check_delay(int64_t delay)
{
   if (unlikely(delay < 0)) {
      char buf[32];
      fmt_time_r(buf, sizeof(buf), delay, " ");
      jit_msg(NULL, DIAG_FATAL, "illegal negative delay %s", buf);
   }
}

static uint8_t sv_to_vhdl_byte(uint8_t b, bool sv_is_net, bool vhdl_is_bit)
{
   const uint8_t lv = b & 3;
   if (vhdl_is_bit)
      return lv == 1 ? 1 : 0;

   if (sv_is_net) {
      const uint8_t s0 = (b >> 2) & 7;
      const uint8_t s1 = (b >> 5) & 7;
      switch (lv) {
      case 0:
         if (s0 == 0 && s1 != 0)
            return 4;   // 'Z'
         else if (s0 != 0 && s0 < 6)
            return 6;   // 'L'
         else
            return 2;   // '0'
      case 1:
         if (s1 == 0 && s0 != 0)
            return 4;   // 'Z'
         else if (s1 != 0 && s1 < 6)
            return 7;   // 'H'
         else
            return 3;   // '1'
      case 2:
         return 4;   // 'Z'
      default:
         return 1;   // 'X'
      }
   }

   static const uint8_t map[4] = { 2, 3, 4, 1 };   // '0','1','Z','X'
   return map[lv];
}

static uint8_t vhdl_to_sv_byte(uint8_t b, bool sv_is_net, bool vhdl_is_bit)
{
   uint8_t lv;
   uint8_t st = 6;   // St
   if (vhdl_is_bit)
      lv = b == 1 ? 1 : 0;
   else {
      switch (b) {
      case 2: lv = 0; break;            // '0'
      case 3: lv = 1; break;            // '1'
      case 4: lv = 2; st = 0; break;    // 'Z'
      case 6: lv = 0; st = 3; break;    // 'L' (We)
      case 7: lv = 1; st = 3; break;    // 'H' (We)
      default: lv = 3; break;           // 'U','X','W','-'
      }
   }

   if (sv_is_net)
      return lv + (st << 2) + (st << 5);
   else
      return lv;
}

static inline uint8_t packed_read_sv_bit(const rt_signal_t *s, uint32_t msb_idx)
{
   const uint32_t width = s->bit_width;
   const uint32_t bit = width - 1 - msb_idx;
   const int nwords = (width + 63) / 64;
   const uint64_t *abits = (const uint64_t *)s->shared.data;
   const uint64_t *bbits = (s->shared.flags & SIG_F_PACKED4)
      ? abits + nwords : NULL;
   const uint64_t a = (abits[bit / 64] >> (bit % 64)) & 1;
   const uint64_t b = bbits != NULL ? ((bbits[bit / 64] >> (bit % 64)) & 1) : 0;
   return (uint8_t)((b << 1) | a);
}

static void sv_to_vhdl_int(const rt_mixed_bridge_t *b, void *dst)
{
   const uint32_t width = signal_width(b->sv_sig);
   const bool packed = !!(b->sv_sig->shared.flags & SIG_F_PACKED);
   const uint8_t *src = b->sv_sig->shared.data;

   uint64_t val = 0;
   for (uint32_t i = 0; i < width; i++) {
      const uint8_t svb = packed ? packed_read_sv_bit(b->sv_sig, i) : src[i];
      const uint8_t bit = ((svb & 3) == 1) ? 1 : 0;
      val = (val << 1) | bit;
   }

   const uint8_t msb = width > 0
      ? (packed ? packed_read_sv_bit(b->sv_sig, 0) : src[0]) : 0;
   if (b->sv_is_signed && width > 0 && width < 64 && ((msb & 3) == 1))
      val |= (~UINT64_C(0) << width);

   switch (b->vhdl_size) {
   case 1: *(int8_t *)dst = (int8_t)val; break;
   case 2: *(int16_t *)dst = (int16_t)val; break;
   case 4: *(int32_t *)dst = (int32_t)val; break;
   case 8: *(int64_t *)dst = (int64_t)val; break;
   default: should_not_reach_here();
   }
}

static void vhdl_int_to_sv(const rt_mixed_bridge_t *b, const void *src,
                           uint8_t *dst)
{
   int64_t val = 0;
   switch (b->vhdl_size) {
   case 1: val = *(const int8_t *)src; break;
   case 2: val = *(const int16_t *)src; break;
   case 4: val = *(const int32_t *)src; break;
   case 8: val = *(const int64_t *)src; break;
   default: should_not_reach_here();
   }

   const uint32_t width = signal_width(b->sv_sig);
   for (uint32_t i = 0; i < width; i++) {
      const uint32_t bit_idx = width - 1 - i;
      const uint8_t bit = (bit_idx < 64)
         ? (uint8_t)(((uint64_t)val >> bit_idx) & 1)
         : (val < 0 ? 1 : 0);
      dst[i] = vhdl_to_sv_byte(bit, b->sv_is_net, true);
   }
}

static void mixed_bridge_sv_event(uint64_t now, rt_signal_t *signal,
                                  rt_watch_t *watch, void *user)
{
   rt_mixed_bridge_t *b = user;
   rt_model_t *m = get_model();

   if (b->kind == MIXED_BRIDGE_INT) {
      uint8_t buf[8] = { 0 };
      sv_to_vhdl_int(b, buf);
      sched_deposit(m, b->vhdl_sig, buf, 0, 1, 0, false);
      return;
   }
   else if (b->kind == MIXED_BRIDGE_REAL) {
      sched_deposit(m, b->vhdl_sig, b->sv_sig->shared.data, 0, 1, 0, false);
      return;
   }

   const uint32_t width = signal_width(b->sv_sig);
   const bool vhdl_is_bit = (b->kind == MIXED_BRIDGE_BIT);
   const bool packed = !!(b->sv_sig->shared.flags & SIG_F_PACKED);

   uint8_t *buf LOCAL = xmalloc(width);
   for (uint32_t i = 0; i < width; i++) {
      const uint8_t svb = packed ? packed_read_sv_bit(b->sv_sig, i)
         : b->sv_sig->shared.data[i];
      buf[i] = sv_to_vhdl_byte(svb, b->sv_is_net, vhdl_is_bit);
   }

   sched_deposit(m, b->vhdl_sig, buf, 0, width, 0, false);
}

static void mixed_bridge_force(rt_model_t *m, rt_signal_t *s,
                               const void *values, int offset, size_t count)
{
   for (rt_mixed_bridge_t *b = m->mixed_bridges; b != NULL; b = b->next) {
      if (b->vhdl_sig != s)
         continue;

      if (b->kind == MIXED_BRIDGE_INT) {
         const uint32_t width = signal_width(b->sv_sig);
         uint8_t *buf LOCAL = xmalloc(width);
         vhdl_int_to_sv(b, values, buf);
         force_signal(m, b->sv_sig, buf, 0, width);
         return;
      }
      else if (b->kind == MIXED_BRIDGE_REAL) {
         force_signal(m, b->sv_sig, values, 0, 1);
         return;
      }

      const bool vhdl_is_bit = (b->kind == MIXED_BRIDGE_BIT);
      const uint8_t *src = values;
      uint8_t *buf LOCAL = xmalloc(count);
      for (size_t i = 0; i < count; i++)
         buf[i] = vhdl_to_sv_byte(src[i], b->sv_is_net, vhdl_is_bit);

      force_signal(m, b->sv_sig, buf, offset, count);
      return;
   }
}

static void mixed_bridge_release(rt_model_t *m, rt_signal_t *s,
                                 int offset, size_t count)
{
   for (rt_mixed_bridge_t *b = m->mixed_bridges; b != NULL; b = b->next) {
      if (b->vhdl_sig != s)
         continue;

      uint8_t *driving = b->vhdl_sig->shared.data + 2 * b->vhdl_sig->shared.size;
      if (b->kind == MIXED_BRIDGE_INT) {
         const uint32_t sv_width = signal_width(b->sv_sig);
         sv_to_vhdl_int(b, driving);
         if (!b->sv_is_net)
            sched_deposit(m, b->sv_sig, b->sv_sig->shared.data, 0, sv_width,
                          -1, false);
         release_signal(m, b->sv_sig, 0, sv_width);
         return;
      }
      else if (b->kind == MIXED_BRIDGE_REAL) {
         memcpy(driving, b->sv_sig->shared.data, sizeof(double));
         if (!b->sv_is_net)
            sched_deposit(m, b->sv_sig, b->sv_sig->shared.data, 0, 1,
                          -1, false);
         release_signal(m, b->sv_sig, 0, 1);
         return;
      }

      // Sync the proxy's default/driving area with the current SV value so
      // releasing the proxy does not revert to stale initial data
      const bool vhdl_is_bit = (b->kind == MIXED_BRIDGE_BIT);
      const bool packed = !!(b->sv_sig->shared.flags & SIG_F_PACKED);
      for (size_t i = 0; i < count; i++) {
         const uint8_t svb = packed ? packed_read_sv_bit(b->sv_sig, offset + i)
            : b->sv_sig->shared.data[offset + i];
         driving[offset + i] = sv_to_vhdl_byte(svb, b->sv_is_net, vhdl_is_bit);
      }

      if (!b->sv_is_net)
         sched_deposit(m, b->sv_sig,
                       packed ? NULL : b->sv_sig->shared.data + offset,
                       offset, count, -1, false);
      release_signal(m, b->sv_sig, offset, count);
      return;
   }
}

sig_shared_t *model_mixed_signal_proxy(rt_model_t *m, rt_signal_t *sv_sig,
                                       bool sv_is_net,
                                       mixed_bridge_kind_t kind,
                                       bool sv_is_signed,
                                       uint8_t vhdl_size, tree_t where)
{
   for (rt_mixed_bridge_t *b = m->mixed_bridges; b != NULL; b = b->next) {
      if (b->sv_sig == sv_sig && b->kind == kind
          && b->vhdl_size == vhdl_size && b->sv_is_signed == sv_is_signed)
         return &(b->vhdl_sig->shared);
   }

   const uint32_t sv_width = signal_width(sv_sig);
   const bool scalar_word = (kind == MIXED_BRIDGE_INT
                             || kind == MIXED_BRIDGE_REAL);
   const uint32_t vhdl_width = scalar_word ? 1 : sv_width;
   const uint8_t elem_size = scalar_word ? vhdl_size : 1;
   const size_t total_bytes = (size_t)vhdl_width * elem_size;
   const size_t datasz = MAX(3 * total_bytes, 8);
   rt_signal_t *s = static_alloc(m, sizeof(rt_signal_t) + datasz);

   s->where   = where;
   s->n_nexus = 1;
   s->offset  = 0;
   s->parent  = sv_sig->parent;

   s->shared.flags = 0;
   s->shared.size  = total_bytes;

   s->nexus.width        = vhdl_width;
   s->nexus.size         = elem_size;
   s->nexus.n_sources    = 0;
   s->nexus.offset       = 0;
   s->nexus.flags        = NET_F_FAST_DRIVER | NET_F_HAS_INITIAL;
   s->nexus.signal       = s;
   s->nexus.pending      = NULL;
   s->nexus.active_delta = DELTA_CYCLE_MAX;
   s->nexus.event_delta  = DELTA_CYCLE_MAX;
   s->nexus.last_event   = TIME_HIGH;

   *m->nexus_tail = &(s->nexus);
   m->nexus_tail = &(s->nexus.chain);
   m->n_signals++;

   rt_mixed_bridge_t *b = xcalloc(sizeof(rt_mixed_bridge_t));
   b->sv_sig       = sv_sig;
   b->vhdl_sig     = s;
   b->kind         = kind;
   b->sv_is_net    = sv_is_net;
   b->sv_is_signed = sv_is_signed;
   b->vhdl_size    = elem_size;
   b->next         = m->mixed_bridges;
   m->mixed_bridges = b;

   if (unlikely(sv_sig->shared.flags & SIG_F_ISLAND))
      rt_island_on_observer_attached(m, sv_sig);

   uint8_t *eff = s->shared.data;
   uint8_t *last = eff + total_bytes;
   uint8_t *driving = eff + 2 * total_bytes;
   if (kind == MIXED_BRIDGE_INT) {
      sv_to_vhdl_int(b, eff);
      memcpy(last, eff, elem_size);
      memcpy(driving, eff, elem_size);
   }
   else if (kind == MIXED_BRIDGE_REAL) {
      memcpy(eff, sv_sig->shared.data, elem_size);
      memcpy(last, eff, elem_size);
      memcpy(driving, eff, elem_size);
   }
   else {
      const bool vhdl_is_bit = (kind == MIXED_BRIDGE_BIT);
      const bool packed = !!(sv_sig->shared.flags & SIG_F_PACKED);
      for (uint32_t i = 0; i < sv_width; i++) {
         const uint8_t svb = packed ? packed_read_sv_bit(sv_sig, i)
            : sv_sig->shared.data[i];
         uint8_t v = sv_to_vhdl_byte(svb, sv_is_net, vhdl_is_bit);
         eff[i] = last[i] = driving[i] = v;
      }
   }

   rt_watch_t *w = watch_new(m, mixed_bridge_sv_event, b, WATCH_EVENT, 1);
   model_set_event_cb(m, sv_sig, 0, sv_width, w);

   return &(s->shared);
}

static void packed_bytes_to_words(const uint8_t *bytes, uint32_t count,
                                  uint64_t *abits, uint64_t *bbits)
{
   const int nwords = (count + 63) / 64;
   for (int i = 0; i < nwords; i++) {
      abits[i] = 0;
      if (bbits != NULL)
         bbits[i] = 0;
   }
   for (uint32_t i = 0; i < count; i++) {
      const uint32_t bit = count - 1 - i;
      const uint8_t v = bytes[i];
      if (v & 1)
         abits[bit / 64] |= UINT64_C(1) << (bit % 64);
      if (bbits != NULL && (v & 2))
         bbits[bit / 64] |= UINT64_C(1) << (bit % 64);
   }
}

static inline uint64_t packed_top_mask(uint32_t width)
{
   return (width & 63) ? ((UINT64_C(1) << (width & 63)) - 1) : ~UINT64_C(0);
}

static inline bool packed_can_fast_direct(const rt_signal_t *s)
{
   return (s->shared.flags & SIG_F_PACKED)
      && s->bit_width <= 64
      && !(s->nexus.flags & (NET_F_FORCED | NET_F_EFFECTIVE | NET_F_CACHE_EVENT))
      && !(s->shared.flags & (SIG_F_POLL_WATCHED | SIG_F_CACHE_EVENT))
      && s->nexus.n_sources == 0
      && s->nexus.pending == NULL;
}

static bool save_deferred_ex(rt_model_t *m, rt_nexus_t *n, size_t valuesz,
                             const void *init_src, bool *already_saved)
{
   rt_proc_t *ap = get_active_proc();
   if (ap == NULL || !ap->comb)
      return false;

   if (unlikely(m->deferred == NULL))
      m->deferred = xmalloc_array(DEFER_MAX, sizeof(rt_deferred_t));

   unsigned i = 0;
   while (i < m->ndeferred && m->deferred[i].nexus != n)
      i++;
   if (i < m->ndeferred) {
      if (already_saved != NULL)
         *already_saved = true;
      return true;
   }

   if (m->ndeferred < DEFER_MAX) {
      unsigned char *init = valuesz <= sizeof(m->deferred[i].buf)
         ? m->deferred[i].buf : xmalloc(valuesz);
      m->deferred[i].nexus = n;
      m->deferred[i].init = init;
      memcpy(init, init_src, valuesz);
      m->ndeferred++;
      if (already_saved != NULL)
         *already_saved = false;
      return true;
   }
   return false;
}

static bool save_deferred(rt_model_t *m, rt_nexus_t *n, size_t valuesz,
                          const void *init_src)
{
   return save_deferred_ex(m, n, valuesz, init_src, NULL);
}

static bool packed_save_deferred(rt_model_t *m, rt_nexus_t *n, size_t valuesz,
                                 bool *already_saved)
{
   return save_deferred_ex(m, n, valuesz, n->signal->shared.data,
                           already_saved);
}

static inline void packed_merge_last_word(uint64_t *last_a, uint64_t *last_b,
                                          int w, uint64_t old_a, uint64_t old_b,
                                          uint64_t new_a, uint64_t new_b,
                                          bool is4)
{
   if (is4) {
      const uint64_t same = ~((new_a ^ last_a[w]) | (new_b ^ last_b[w]));
      last_a[w] = (last_a[w] & ~same) | (old_a & same);
      last_b[w] = (last_b[w] & ~same) | (old_b & same);
   }
   else {
      const uint64_t same = ~(new_a ^ last_a[w]);
      last_a[w] = (last_a[w] & ~same) | (old_a & same);
   }
}

static void packed_rmw_range(uint64_t *dst_a, uint64_t *dst_b,
                             uint32_t width, uint32_t msb_off, uint32_t count,
                             const uint64_t *src_a, const uint64_t *src_b,
                             const uint64_t *force_mask)
{
   const uint32_t lsb_off = width - msb_off - count;
   const int src_nwords = (count + 63) / 64;
   const int w0 = lsb_off / 64;
   const int w1 = (lsb_off + count - 1) / 64;

   for (int w = w0; w <= w1; w++) {
      const uint32_t d_start = (w == w0) ? (lsb_off % 64) : 0;
      const uint32_t d_end = (w == w1) ? ((lsb_off + count - 1) % 64) : 63;
      const uint32_t span = d_end - d_start + 1;
      uint64_t wmask = (span == 64)
         ? ~UINT64_C(0) : (((UINT64_C(1) << span) - 1) << d_start);
      if (force_mask != NULL)
         wmask &= ~force_mask[w];
      if (wmask == 0)
         continue;

      const uint32_t s_bit = (uint32_t)(w * 64 + d_start) - lsb_off;
      const int sw = s_bit / 64;
      const int ssh = s_bit % 64;
      uint64_t sa = src_a[sw] >> ssh;
      if (ssh != 0 && sw + 1 < src_nwords)
         sa |= src_a[sw + 1] << (64 - ssh);
      const uint64_t val_a = sa << d_start;
      dst_a[w] = (dst_a[w] & ~wmask) | (val_a & wmask);

      if (dst_b != NULL) {
         uint64_t sb = 0;
         if (src_b != NULL) {
            sb = src_b[sw] >> ssh;
            if (ssh != 0 && sw + 1 < src_nwords)
               sb |= src_b[sw + 1] << (64 - ssh);
         }
         const uint64_t val_b = sb << d_start;
         dst_b[w] = (dst_b[w] & ~wmask) | (val_b & wmask);
      }
   }
}

static void deposit_notify(rt_model_t *m, rt_nexus_t *n);

static bool packed_range_differs(const uint64_t *eff_a, const uint64_t *eff_b,
                                 uint32_t width, uint32_t msb_off,
                                 uint32_t count, const uint64_t *src_a,
                                 const uint64_t *src_b,
                                 const uint64_t *force_mask)
{
   const uint32_t lsb_off = width - msb_off - count;
   const int src_nwords = (count + 63) / 64;
   const int w0 = lsb_off / 64;
   const int w1 = (lsb_off + count - 1) / 64;

   for (int w = w0; w <= w1; w++) {
      const uint32_t d_start = (w == w0) ? (lsb_off % 64) : 0;
      const uint32_t d_end = (w == w1) ? ((lsb_off + count - 1) % 64) : 63;
      const uint32_t span = d_end - d_start + 1;
      uint64_t wmask = (span == 64)
         ? ~UINT64_C(0) : (((UINT64_C(1) << span) - 1) << d_start);
      if (force_mask != NULL)
         wmask &= ~force_mask[w];
      if (wmask == 0)
         continue;

      const uint32_t s_bit = (uint32_t)(w * 64 + d_start) - lsb_off;
      const int sw = s_bit / 64;
      const int ssh = s_bit % 64;
      uint64_t sa = src_a[sw] >> ssh;
      if (ssh != 0 && sw + 1 < src_nwords)
         sa |= src_a[sw + 1] << (64 - ssh);
      if (((sa << d_start) ^ eff_a[w]) & wmask)
         return true;

      if (eff_b != NULL) {
         uint64_t sb = 0;
         if (src_b != NULL) {
            sb = src_b[sw] >> ssh;
            if (ssh != 0 && sw + 1 < src_nwords)
               sb |= src_b[sw + 1] << (64 - ssh);
         }
         if (((sb << d_start) ^ eff_b[w]) & wmask)
            return true;
      }
   }
   return false;
}

static void deposit_packed_apply(rt_model_t *m, rt_signal_t *s,
                                 uint32_t msb_off, uint32_t count,
                                 uint64_t a_val, uint64_t b_val,
                                 uint32_t src_flags, bool respect_force,
                                 bool check_comb)
{
   if (count == 0)
      return;

   const uint32_t width = s->bit_width;
   assert(msb_off + count <= width);

   const int nwords = (width + 63) / 64;
   const bool is4 = !!(s->shared.flags & SIG_F_PACKED4);
   uint64_t *eff_a = (uint64_t *)s->shared.data;
   uint64_t *eff_b = is4 ? eff_a + nwords : NULL;
   uint64_t *last_a = (uint64_t *)(s->shared.data + s->shared.size);
   uint64_t *last_b = is4 ? last_a + nwords : NULL;

   const uint64_t *force_mask = NULL;
   if (unlikely(respect_force && (s->nexus.flags & NET_F_FORCED))) {
      rt_packed_force_t *pf = m->packed_forces
         ? hash_get(m->packed_forces, s) : NULL;
      if (pf != NULL)
         force_mask = pf->mask;
   }

   if (likely(msb_off == 0 && count == width && force_mask == NULL
              && (src_flags & PACK_SRC_SCALAR) && nwords == 1)) {
      const uint64_t top_mask = packed_top_mask(width);
      const uint64_t na = a_val & top_mask;
      const uint64_t nb = (is4 && !(src_flags & PACK_SRC_VEC2))
         ? (b_val & top_mask) : 0;
      if (eff_a[0] == na && (!is4 || eff_b[0] == nb))
         return;

      bool already_saved = false;
      const bool defer = check_comb
         && packed_save_deferred(m, &(s->nexus), s->shared.size,
                                 &already_saved);
      const bool already_delta =
         (s->nexus.last_event == m->now && s->nexus.event_delta == m->iteration)
         || (defer && already_saved);
      const uint64_t oa = eff_a[0];
      const uint64_t ob = is4 ? eff_b[0] : 0;
      eff_a[0] = na;
      if (is4)
         eff_b[0] = nb;
      if (likely(!already_delta)) {
         last_a[0] = oa;
         if (is4)
            last_b[0] = ob;
      }
      else
         packed_merge_last_word(last_a, last_b, 0, oa, ob, na, nb, is4);
      m->trigger_epoch++;
      if (!defer)
         deposit_notify(m, &(s->nexus));
      return;
   }

   const uint64_t *src_a = (src_flags & PACK_SRC_SCALAR)
      ? &a_val : (const uint64_t *)(uintptr_t)a_val;
   const uint64_t *src_b = (src_flags & PACK_SRC_VEC2) ? NULL
      : ((src_flags & PACK_SRC_SCALAR)
         ? &b_val : (const uint64_t *)(uintptr_t)b_val);

   if (msb_off == 0 && count == width && force_mask == NULL) {
      const uint64_t top_mask = packed_top_mask(width);
      bool changed = false;
      for (int i = 0; i < nwords && !changed; i++) {
         const uint64_t msk = (i == nwords - 1) ? top_mask : ~UINT64_C(0);
         if ((eff_a[i] ^ src_a[i]) & msk)
            changed = true;
         if (is4 && ((eff_b[i] ^ (src_b ? src_b[i] : 0)) & msk))
            changed = true;
      }
      if (!changed)
         return;

      bool already_saved = false;
      const bool defer = check_comb
         && packed_save_deferred(m, &(s->nexus), s->shared.size,
                                 &already_saved);
      const bool already_delta =
         (s->nexus.last_event == m->now && s->nexus.event_delta == m->iteration)
         || (defer && already_saved);
      if (likely(!already_delta)) {
         memcpy(last_a, eff_a, s->shared.size);
         memcpy(eff_a, src_a, (size_t)nwords * sizeof(uint64_t));
         eff_a[nwords - 1] &= top_mask;
         if (is4) {
            if (src_b != NULL) {
               memcpy(eff_b, src_b, (size_t)nwords * sizeof(uint64_t));
               eff_b[nwords - 1] &= top_mask;
            }
            else
               memset(eff_b, 0, (size_t)nwords * sizeof(uint64_t));
         }
      }
      else {
         for (int i = 0; i < nwords; i++) {
            const uint64_t msk = (i == nwords - 1) ? top_mask : ~UINT64_C(0);
            const uint64_t oa = eff_a[i];
            const uint64_t ob = is4 ? eff_b[i] : 0;
            const uint64_t na = src_a[i] & msk;
            const uint64_t nb = (is4 && src_b != NULL) ? (src_b[i] & msk) : 0;
            eff_a[i] = na;
            if (is4)
               eff_b[i] = nb;
            packed_merge_last_word(last_a, last_b, i, oa, ob, na, nb, is4);
         }
      }
      m->trigger_epoch++;
      if (!defer)
         deposit_notify(m, &(s->nexus));
      return;
   }

   if (!packed_range_differs(eff_a, eff_b, width, msb_off, count,
                             src_a, src_b, force_mask))
      return;

   bool already_saved = false;
   const bool defer = check_comb
      && packed_save_deferred(m, &(s->nexus), s->shared.size, &already_saved);
   const bool already_delta =
      (s->nexus.last_event == m->now && s->nexus.event_delta == m->iteration)
      || (defer && already_saved);
   if (likely(!already_delta)) {
      memcpy(last_a, eff_a, s->shared.size);
      packed_rmw_range(eff_a, eff_b, width, msb_off, count,
                       src_a, src_b, force_mask);
   }
   else {
      const uint32_t lsb_off = width - msb_off - count;
      const int w0 = lsb_off / 64;
      const int w1 = (lsb_off + count - 1) / 64;
      const int nw_span = w1 - w0 + 1;
      uint64_t sa[8], sb[8];
      uint64_t *heap LOCAL = NULL;
      uint64_t *old_a = sa, *old_b = sb;
      if (nw_span > (int)ARRAY_LEN(sa)) {
         heap = xmalloc_array((size_t)nw_span * (is4 ? 2 : 1), sizeof(uint64_t));
         old_a = heap;
         old_b = is4 ? heap + nw_span : NULL;
      }
      memcpy(old_a, eff_a + w0, (size_t)nw_span * sizeof(uint64_t));
      if (is4)
         memcpy(old_b, eff_b + w0, (size_t)nw_span * sizeof(uint64_t));
      packed_rmw_range(eff_a, eff_b, width, msb_off, count,
                       src_a, src_b, force_mask);
      for (int w = w0; w <= w1; w++)
         packed_merge_last_word(last_a, last_b, w, old_a[w - w0],
                                is4 ? old_b[w - w0] : 0, eff_a[w],
                                is4 ? eff_b[w] : 0, is4);
   }
   m->trigger_epoch++;
   if (!defer)
      deposit_notify(m, &(s->nexus));
}

static void async_packed_deposit(rt_model_t *m, void *arg)
{
   rt_packed_sched_t *ps = arg;
   if (ps->nonblock)
      ps->signal->shared.flags &= ~SIG_F_NB_DEFERQ;
   if (ps->abits != NULL) {
      if (unlikely(ps->signal->shared.flags & SIG_F_ISLAND))
         rt_island_put_packed(m, ps->signal, ps->offset, ps->count,
                              (uint64_t)(uintptr_t)ps->abits,
                              (uint64_t)(uintptr_t)ps->bbits,
                              ps->has_b ? 0 : PACK_SRC_VEC2, false);
      else
         deposit_packed_apply(m, ps->signal, ps->offset, ps->count,
                              (uint64_t)(uintptr_t)ps->abits,
                              (uint64_t)(uintptr_t)ps->bbits,
                              ps->has_b ? 0 : PACK_SRC_VEC2, true, false);
      free(ps->abits);
   }
   else {
      if (unlikely(ps->signal->shared.flags & SIG_F_ISLAND))
         rt_island_put_packed(m, ps->signal, ps->offset, ps->count,
                              ps->small_a, ps->small_b,
                              PACK_SRC_SCALAR | (ps->has_b ? 0 : PACK_SRC_VEC2),
                              false);
      else
         deposit_packed_apply(m, ps->signal, ps->offset, ps->count,
                              ps->small_a, ps->small_b,
                              PACK_SRC_SCALAR | (ps->has_b ? 0 : PACK_SRC_VEC2),
                              true, false);
   }
   ps->abits = ps->bbits = NULL;
   ps->next = m->free_packed_sched;
   m->free_packed_sched = ps;
}

static void drain_nb_packed(rt_model_t *m)
{
   const unsigned n = m->nb_packed_q.count;
   m->nb_packed_q.count = 0;

   nb_packed_t *items = m->nb_packed_q.items;
   for (unsigned i = 0; i < n; i++) {
      rt_signal_t *s = items[i].sig;
      const uint32_t sflags = s->shared.flags;
      if (likely(sflags & SIG_F_FAST_NOOP)) {
         const uint32_t width = s->bit_width;
         const uint64_t top_mask = packed_top_mask(width);
         const bool is4 = !!(sflags & SIG_F_PACKED4);
         const uint64_t na = items[i].a & top_mask;
         const uint64_t nb = (is4 && !(items[i].flags & PACK_SRC_VEC2))
            ? (items[i].b & top_mask) : 0;
         uint64_t *eff_a = (uint64_t *)s->shared.data;
         uint64_t *eff_b = is4 ? eff_a + 1 : NULL;
         if (eff_a[0] == na && (!is4 || eff_b[0] == nb))
            continue;

         uint64_t *last_a = (uint64_t *)(s->shared.data + s->shared.size);
         uint64_t *last_b = is4 ? last_a + 1 : NULL;
         const bool already_delta =
            s->nexus.last_event == m->now
            && s->nexus.event_delta == m->iteration;
         const uint64_t oa = eff_a[0];
         const uint64_t ob = is4 ? eff_b[0] : 0;
         eff_a[0] = na;
         if (is4)
            eff_b[0] = nb;
         if (likely(!already_delta)) {
            last_a[0] = oa;
            if (is4)
               last_b[0] = ob;
         }
         else
            packed_merge_last_word(last_a, last_b, 0, oa, ob, na, nb, is4);
         if (!(sflags & SIG_F_FAST_DIRECT)) {
            m->trigger_epoch++;
            deposit_notify(m, &(s->nexus));
         }
         continue;
      }
      if (unlikely(sflags & SIG_F_ISLAND)
          && rt_island_put_packed(m, s, 0, s->bit_width, items[i].a, items[i].b,
                                  items[i].flags, false))
         continue;
      deposit_packed_apply(m, s, 0, s->bit_width, items[i].a, items[i].b,
                           items[i].flags, true, false);
   }
}

static inline void *packed_delayed_key(rt_signal_t *s, bool nonblock)
{
   return (void *)((uintptr_t)s | (nonblock ? 1u : 0u));
}

static void push_packed_delayed(rt_model_t *m, rt_packed_sched_t *ps,
                                bool nonblock)
{
   if (m->packed_delayed == NULL)
      m->packed_delayed = hash_new(16);

   void *key = packed_delayed_key(ps->signal, nonblock);
   rt_packed_sched_t *head = hash_get(m->packed_delayed, key);
   if (head == NULL || ps->when < head->when) {
      ps->next = head;
      hash_put(m->packed_delayed, key, ps);
   }
   else {
      rt_packed_sched_t *it = head;
      while (it->next != NULL && it->next->when <= ps->when)
         it = it->next;
      ps->next = it->next;
      it->next = ps;
   }
}

static rt_packed_sched_t *pop_packed_delayed(rt_model_t *m, rt_signal_t *s,
                                             bool nonblock)
{
   assert(m->packed_delayed != NULL);
   void *key = packed_delayed_key(s, nonblock);
   rt_packed_sched_t *head = hash_get(m->packed_delayed, key);
   assert(head != NULL);
   if (head->next != NULL)
      hash_put(m->packed_delayed, key, head->next);
   else
      hash_delete(m->packed_delayed, key);
   head->next = NULL;
   return head;
}

static void sched_deposit_packed(rt_model_t *m, rt_signal_t *s,
                                 uint32_t msb_off, uint32_t count,
                                 uint64_t a_val, uint64_t b_val,
                                 uint32_t src_flags, int64_t after,
                                 bool nonblock)
{
   if (after < 0) {
      if (!(s->shared.flags & SIG_F_PACKED_DRIVEN)
          && (s->nexus.flags & NET_F_FORCED))
         s->shared.flags |= SIG_F_REGISTER;
      return;
   }

   if (count == 0)
      return;

   if (after == 0 && nonblock && msb_off == 0 && count == s->bit_width
       && s->bit_width <= 64 && (src_flags & PACK_SRC_SCALAR)
       && !(s->shared.flags & SIG_F_NB_DEFERQ)) {
      if (m->nb_packed_q.count == m->nb_packed_q.max) {
         m->nb_packed_q.max = MAX(m->nb_packed_q.max * 2, 64);
         m->nb_packed_q.items = xrealloc_array(m->nb_packed_q.items,
                                               m->nb_packed_q.max,
                                               sizeof(nb_packed_t));
      }
      m->nb_packed_q.items[m->nb_packed_q.count++] = (nb_packed_t){
         .sig   = s,
         .a     = a_val,
         .b     = (src_flags & PACK_SRC_VEC2) ? 0 : b_val,
         .flags = src_flags,
      };
      return;
   }

   rt_packed_sched_t *ps = m->free_packed_sched;
   if (ps != NULL)
      m->free_packed_sched = ps->next;
   else
      ps = xmalloc(sizeof(rt_packed_sched_t));

   ps->signal = s;
   ps->offset = msb_off;
   ps->count  = count;
   ps->has_b    = !(src_flags & PACK_SRC_VEC2) && (s->shared.flags & SIG_F_PACKED4);
   ps->nonblock = nonblock;
   ps->next     = NULL;

   if (src_flags & PACK_SRC_SCALAR) {
      ps->abits   = NULL;
      ps->bbits   = NULL;
      ps->small_a = a_val;
      ps->small_b = ps->has_b ? b_val : 0;
   }
   else {
      const int nwords = (count + 63) / 64;
      const size_t wbytes = (size_t)nwords * sizeof(uint64_t);
      uint64_t *buf = xmalloc(ps->has_b ? 2 * wbytes : wbytes);
      memcpy(buf, (const void *)(uintptr_t)a_val, wbytes);
      ps->abits = buf;
      if (ps->has_b) {
         ps->bbits = buf + nwords;
         memcpy(ps->bbits, (const void *)(uintptr_t)b_val, wbytes);
      }
      else
         ps->bbits = NULL;
   }

   if (after == 0) {
      if (nonblock)
         s->shared.flags |= SIG_F_NB_DEFERQ;
      deferq_do(nonblock ? &m->nonblockq : &m->driverq,
                async_packed_deposit, ps);
   }
   else {
      ps->when = m->now + after;
      push_packed_delayed(m, ps, nonblock);
      void *e = tag_pointer(s, nonblock
                            ? EVENT_PACKED_NONBLOCK : EVENT_PACKED_DEPOSIT);
      heap_insert(m->eventq_heap, m->now + after, e);
   }
}

static void force_packed_raw(rt_model_t *m, rt_signal_t *s, uint32_t msb_off,
                             uint32_t count, uint64_t a_val, uint64_t b_val,
                             uint32_t src_flags)
{
   if (count == 0)
      return;

   const uint32_t width = s->bit_width;
   assert(msb_off + count <= width);
   const int nwords = (width + 63) / 64;
   const bool is4 = !!(s->shared.flags & SIG_F_PACKED4);

   if (m->packed_forces == NULL)
      m->packed_forces = hash_new(16);

   rt_packed_force_t *pf = hash_get(m->packed_forces, s);
   if (pf == NULL) {
      const size_t wbytes = (size_t)nwords * sizeof(uint64_t);
      const size_t alloc = sizeof(rt_packed_force_t) + (is4 ? 3 : 2) * wbytes;
      pf = xcalloc(alloc);
      uint64_t *words = (uint64_t *)(pf + 1);
      pf->mask  = words;
      pf->abits = words + nwords;
      pf->bbits = is4 ? words + 2 * nwords : NULL;
      pf->next  = m->packed_force_list;
      m->packed_force_list = pf;
      hash_put(m->packed_forces, s, pf);
   }

   const uint64_t *src_a = (src_flags & PACK_SRC_SCALAR)
      ? &a_val : (const uint64_t *)(uintptr_t)a_val;
   const uint64_t *src_b = (src_flags & PACK_SRC_VEC2) ? NULL
      : ((src_flags & PACK_SRC_SCALAR)
         ? &b_val : (const uint64_t *)(uintptr_t)b_val);

   const uint32_t lsb_off = width - msb_off - count;
   const int w0 = lsb_off / 64;
   const int w1 = (lsb_off + count - 1) / 64;
   for (int w = w0; w <= w1; w++) {
      const uint32_t d_start = (w == w0) ? (lsb_off % 64) : 0;
      const uint32_t d_end = (w == w1) ? ((lsb_off + count - 1) % 64) : 63;
      const uint32_t span = d_end - d_start + 1;
      const uint64_t wmask = (span == 64)
         ? ~UINT64_C(0) : (((UINT64_C(1) << span) - 1) << d_start);
      pf->mask[w] |= wmask;
   }
   packed_rmw_range(pf->abits, pf->bbits, width, msb_off, count,
                    src_a, src_b, NULL);
   s->nexus.flags |= NET_F_FORCED;
   s->shared.flags &= ~(SIG_F_FAST_NOOP | SIG_F_FAST_DIRECT);

   deposit_packed_apply(m, s, msb_off, count, a_val, b_val, src_flags,
                        false, false);
}

static void force_packed(rt_model_t *m, rt_signal_t *s, uint32_t msb_off,
                         uint32_t count, uint64_t a_val, uint64_t b_val,
                         uint32_t src_flags)
{
   if (count == 0)
      return;

   if (unlikely(s->shared.flags & SIG_F_ISLAND)
       && rt_island_put_packed(m, s, msb_off, count, a_val, b_val,
                               src_flags, true))
      return;

   force_packed_raw(m, s, msb_off, count, a_val, b_val, src_flags);
}

static void release_packed(rt_model_t *m, rt_signal_t *s, uint32_t msb_off,
                           uint32_t count, bool immediate)
{
   if (count == 0)
      return;

   if (unlikely(s->shared.flags & SIG_F_ISLAND)
       && rt_island_vpi_release_slice(m, s, msb_off, count))
      return;

   if (!(s->nexus.flags & NET_F_FORCED))
      return;

   rt_packed_force_t *pf = m->packed_forces
      ? hash_get(m->packed_forces, s) : NULL;
   if (pf == NULL) {
      s->nexus.flags &= ~NET_F_FORCED;
      return;
   }

   const uint32_t width = s->bit_width;
   assert(msb_off + count <= width);
   const int nwords = (width + 63) / 64;
   const bool is4 = !!(s->shared.flags & SIG_F_PACKED4);

   const uint32_t lsb_off = width - msb_off - count;
   const int w0 = lsb_off / 64;
   const int w1 = (lsb_off + count - 1) / 64;
   for (int w = w0; w <= w1; w++) {
      const uint32_t d_start = (w == w0) ? (lsb_off % 64) : 0;
      const uint32_t d_end = (w == w1) ? ((lsb_off + count - 1) % 64) : 63;
      const uint32_t span = d_end - d_start + 1;
      const uint64_t wmask = (span == 64)
         ? ~UINT64_C(0) : (((UINT64_C(1) << span) - 1) << d_start);
      pf->mask[w] &= ~wmask;
   }

   bool any_forced = false;
   for (int w = 0; w < nwords && !any_forced; w++)
      any_forced = pf->mask[w] != 0;
   if (!any_forced) {
      s->nexus.flags &= ~NET_F_FORCED;
      if (s->bit_width <= 64) {
         s->shared.flags |= SIG_F_FAST_NOOP;
         if (packed_can_fast_direct(s))
            s->shared.flags |= SIG_F_FAST_DIRECT;
      }
   }

   if (!immediate && !(s->shared.flags & SIG_F_PACKED_DRIVEN)
       && (s->shared.flags & SIG_F_REGISTER)) {
      if (!any_forced)
         s->shared.flags &= ~SIG_F_REGISTER;
      return;
   }

   const uint64_t *drv_a = (const uint64_t *)(s->shared.data + 2 * s->shared.size);
   const uint64_t *drv_b = is4 ? drv_a + nwords : NULL;
   uint64_t *eff_a = (uint64_t *)s->shared.data;
   uint64_t *eff_b = is4 ? eff_a + nwords : NULL;
   uint64_t *last_a = (uint64_t *)(s->shared.data + s->shared.size);

   const uint64_t *fmask = any_forced ? pf->mask : NULL;
   bool changed = false;
   for (int w = w0; w <= w1; w++) {
      const uint32_t d_start = (w == w0) ? (lsb_off % 64) : 0;
      const uint32_t d_end = (w == w1) ? ((lsb_off + count - 1) % 64) : 63;
      const uint32_t span = d_end - d_start + 1;
      uint64_t wmask = (span == 64)
         ? ~UINT64_C(0) : (((UINT64_C(1) << span) - 1) << d_start);
      if (fmask != NULL)
         wmask &= ~fmask[w];
      if ((eff_a[w] ^ drv_a[w]) & wmask)
         changed = true;
      if (is4 && ((eff_b[w] ^ drv_b[w]) & wmask))
         changed = true;
   }
   if (changed) {
      uint64_t *last_b = is4 ? last_a + nwords : NULL;
      const bool already_delta =
         s->nexus.last_event == m->now && s->nexus.event_delta == m->iteration;
      if (likely(!already_delta))
         memcpy(last_a, eff_a, s->shared.size);
      for (int w = w0; w <= w1; w++) {
         const uint32_t d_start = (w == w0) ? (lsb_off % 64) : 0;
         const uint32_t d_end = (w == w1) ? ((lsb_off + count - 1) % 64) : 63;
         const uint32_t span = d_end - d_start + 1;
         uint64_t wmask = (span == 64)
            ? ~UINT64_C(0) : (((UINT64_C(1) << span) - 1) << d_start);
         if (fmask != NULL)
            wmask &= ~fmask[w];
         const uint64_t oa = eff_a[w];
         const uint64_t ob = is4 ? eff_b[w] : 0;
         eff_a[w] = (eff_a[w] & ~wmask) | (drv_a[w] & wmask);
         if (is4)
            eff_b[w] = (eff_b[w] & ~wmask) | (drv_b[w] & wmask);
         if (unlikely(already_delta))
            packed_merge_last_word(last_a, last_b, w, oa, ob, eff_a[w],
                                   is4 ? eff_b[w] : 0, is4);
      }
      m->trigger_epoch++;
      deposit_notify(m, &(s->nexus));
   }

   if (!any_forced)
      s->shared.flags &= ~SIG_F_REGISTER;
}

static void force_signal_raw(rt_model_t *m, rt_signal_t *s, const void *values,
                             int offset, size_t count)
{
   if (unlikely(s->shared.flags & SIG_F_PACKED)) {
      if (count == 0)
         return;
      const int nwords = (count + 63) / 64;
      uint64_t sa[4], sb[4];
      uint64_t *heap LOCAL = NULL;
      uint64_t *pa = sa, *pb = sb;
      if (nwords > ARRAY_LEN(sa)) {
         heap = xmalloc_array(nwords * 2, sizeof(uint64_t));
         pa = heap;
         pb = heap + nwords;
      }
      packed_bytes_to_words(values, count, pa, pb);
      force_packed_raw(m, s, offset, count, (uint64_t)(uintptr_t)pa,
                       (uint64_t)(uintptr_t)pb, 0);
      return;
   }

   TRACE("force signal %s+%d to %s", istr(tree_ident(s->where)), offset,
         fmt_values(values, count));

   assert(m->can_create_delta);

   rt_nexus_t *n = split_nexus(m, s, offset, count);
   const char *vptr = values;
   for (; count > 0; n = n->chain) {
      count -= n->width;
      assert(count >= 0);

      n->flags |= NET_F_FORCED;

      rt_source_t *src = get_pseudo_source(m, n, SOURCE_FORCING);
      copy_value_ptr(n, &(src->u.pseudo.waveforms.value), vptr);
      src->disconnected = 0;

      waveform_t *w = alloc_waveform(m);
      w->when  = m->now;
      w->next  = NULL;
      w->value = alloc_value(m, n);

      copy_value_ptr(n, &w->value, vptr);

      queue_waveform(m, &src->u.pseudo.waveforms, w);

      deltaq_insert_pseudo_source(m, 0, src, false);

      vptr += n->width * n->size;
   }
}

void force_signal(rt_model_t *m, rt_signal_t *s, const void *values,
                  int offset, size_t count)
{
   if (unlikely(m->mixed_bridges != NULL))
      mixed_bridge_force(m, s, values, offset, count);

   RT_LOCK(s->lock);

   if (unlikely(s->shared.flags & SIG_F_ISLAND)) {
      if (s->shared.flags & SIG_F_PACKED) {
         if (count == 0)
            return;
         const int nwords = (count + 63) / 64;
         uint64_t sa[4], sb[4];
         uint64_t *heap LOCAL = NULL;
         uint64_t *pa = sa, *pb = sb;
         if (nwords > ARRAY_LEN(sa)) {
            heap = xmalloc_array(nwords * 2, sizeof(uint64_t));
            pa = heap;
            pb = heap + nwords;
         }
         packed_bytes_to_words(values, count, pa, pb);
         if (rt_island_put_packed(m, s, offset, count, (uint64_t)(uintptr_t)pa,
                                  (uint64_t)(uintptr_t)pb, 0, true))
            return;
      }
      else if (rt_island_put_bytes(m, s, values, offset, count, true))
         return;
   }

   force_signal_raw(m, s, values, offset, count);
}

void release_signal(rt_model_t *m, rt_signal_t *s, int offset, size_t count)
{
   if (unlikely(m->mixed_bridges != NULL))
      mixed_bridge_release(m, s, offset, count);

   RT_LOCK(s->lock);

   if (unlikely(s->shared.flags & SIG_F_ISLAND)) {
      assert(m->can_create_delta);
      if (rt_island_vpi_release_slice(m, s, offset, count))
         return;
   }

   if (unlikely(s->shared.flags & SIG_F_PACKED)) {
      assert(m->can_create_delta);
      release_packed(m, s, offset, count, false);
      return;
   }

   TRACE("release signal %s+%d", istr(tree_ident(s->where)), offset);

   assert(m->can_create_delta);

   rt_nexus_t *n = split_nexus(m, s, offset, count);
   for (; count > 0; n = n->chain) {
      count -= n->width;
      assert(count >= 0);

      // Releasing a net which is not forced has no effect (IEEE 1800-2017
      // 10.6.2): it must not take the value of a source it never had
      if (!(n->flags & NET_F_FORCED))
         continue;

      n->flags &= ~NET_F_FORCED;

      rt_source_t *src = get_pseudo_source(m, n, SOURCE_FORCING);
      deltaq_insert_pseudo_source(m, 0, src, false);
   }
}

static bool heap_delete_ptr_cb(uint64_t key, void *value, void *ctx)
{
   return value == ctx;
}

void release_signal_now(rt_model_t *m, rt_signal_t *s, int offset,
                        size_t count)
{
   // A net which is released takes the value of its drivers immediately
   // (IEEE 1800-2017 section 10.6.2)

   RT_LOCK(s->lock);

   if (unlikely(s->shared.flags & SIG_F_ISLAND)) {
      assert(m->can_create_delta);
      if (rt_island_vpi_release_slice(m, s, offset, count))
         return;
   }

   if (unlikely(s->shared.flags & SIG_F_PACKED)) {
      assert(m->can_create_delta);
      release_packed(m, s, offset, count, true);
      return;
   }

   TRACE("release signal %s+%d now", istr(tree_ident(s->where)), offset);

   assert(m->can_create_delta);

   rt_nexus_t *n = split_nexus(m, s, offset, count);
   for (; count > 0; n = n->chain) {
      count -= n->width;
      assert(count >= 0);

      if (!(n->flags & NET_F_FORCED))
         continue;

      n->flags &= ~NET_F_FORCED;

      rt_source_t *src = get_pseudo_source(m, n, SOURCE_FORCING);
      src->disconnected = 1;

      update_driving(m, n, true);

      if (n->flags & NET_F_PENDING) {
         // The effective value must be calculated separately
         heap_delete(m->effective_heap, heap_delete_ptr_cb, n);
         update_effective(m, n);
      }
   }
}

static void deposit_notify(rt_model_t *m, rt_nexus_t *n)
{
   // A cached result of a trigger may predate the event
   m->trigger_epoch++;

   n->last_event = m->now;
   n->event_delta = m->iteration;
   if (unlikely(m->has_level_trigger)
       && !(n->signal->shared.flags & SIG_F_PACKED))
      n->signal->last_event_seq = m->event_seq;

   if (n->flags & NET_F_CACHE_EVENT)
      set_event_flag(m, n->signal);

   sv_poll_notify_sig(n->signal);

   wakeup_all(m, &(n->pending));

   // The effective value of a connected port of mode inout is that
   // of the actual
   if (n->n_sources > 0) {
      for (rt_source_t *s = &(n->sources); s; s = s->chain_input) {
         if (s->tag == SOURCE_PORT
             && (s->u.port.input->flags & NET_F_INOUT))
            enqueue_effective(m, s->u.port.input);
      }
   }

   if (unlikely(n->signal->shared.flags & SIG_F_ISLAND) && n->n_sources == 0) {
      for (rt_source_t *o = n->outputs; o; o = o->chain_output) {
         switch (o->tag) {
         case SOURCE_PORT:
            update_driving(m, o->u.port.output, false);
            break;
         case SOURCE_ACTIVE:
            wakeup_one(m, o->u.wakeable);
            break;
         default:
            should_not_reach_here();
         }
      }
   }
}

static void flush_deferred(rt_model_t *m)
{
   const unsigned n = m->ndeferred;
   m->ndeferred = 0;

   for (unsigned i = 0; i < n; i++) {
      rt_nexus_t *nx = m->deferred[i].nexus;
      const size_t valuesz = (nx->signal->shared.flags & SIG_F_PACKED)
         ? nx->signal->shared.size : (size_t)nx->size * nx->width;
      if (!cmp_bytes(nexus_effective(nx), m->deferred[i].init, valuesz))
         deposit_notify(m, nx);
      else {
         // The update events of a variable which an always_comb process
         // assigned and restored still resume an explicit event control or
         // wait (IEEE 1800-2017 9.4.2) but not the always_comb and
         // always_latch processes which see only the final value
         m->skip_comb = true;
         deposit_notify(m, nx);
         m->skip_comb = false;
      }
      if (m->deferred[i].init != m->deferred[i].buf)
         free(m->deferred[i].init);
   }
}

void deposit_signal(rt_model_t *m, rt_signal_t *s, const void *values,
                    int offset, size_t count)
{
   RT_LOCK(s->lock);

   if (unlikely(s->shared.flags & SIG_F_PACKED)) {
      if (count == 0)
         return;
      assert(get_active_proc() == NULL || !get_active_proc()->wakeable.postponed
             || get_active_proc()->wakeable.sampler);
      const int nwords = (count + 63) / 64;
      uint64_t sa[4], sb[4];
      uint64_t *heap LOCAL = NULL;
      uint64_t *pa = sa, *pb = sb;
      if (nwords > ARRAY_LEN(sa)) {
         heap = xmalloc_array(nwords * 2, sizeof(uint64_t));
         pa = heap;
         pb = heap + nwords;
      }
      packed_bytes_to_words(values, count, pa, pb);
      deposit_packed_apply(m, s, offset, count, (uint64_t)(uintptr_t)pa,
                           (uint64_t)(uintptr_t)pb, 0, true, true);
      return;
   }

   TRACE("deposit signal %s+%d value=%s count=%zd", istr(tree_ident(s->where)),
         offset, fmt_values(values, count * s->nexus.size), count);

   // A VPI application may deposit a value outside of any process
   assert(get_active_proc() == NULL || !get_active_proc()->wakeable.postponed
          || get_active_proc()->wakeable.sampler);

   rt_nexus_t *n = split_nexus(m, s, offset, count);
   const char *vptr = values;
   for (; count > 0; n = n->chain) {
      count -= n->width;
      assert(count >= 0);

      unsigned char *eff = nexus_effective(n);
      unsigned char *last = nexus_last_value(n);

      const size_t valuesz = n->size * n->width;

      if (n->flags & NET_F_FORCED) {
         // A procedural assignment does not change a forced variable
         vptr += valuesz;
         continue;
      }

      if (!cmp_bytes(eff, vptr, valuesz)) {
         const bool defer = save_deferred(m, n, valuesz, eff);

         if ((n->flags & NET_F_EFFECTIVE)
             && unlikely(n->signal->shared.flags & SIG_F_ISLAND)
             && n->n_sources == 0)
            memcpy(nexus_driving(n), vptr, valuesz);

         copy2(last, eff, vptr, valuesz);
         m->trigger_epoch++;

         if (!defer)
            deposit_notify(m, n);
      }

      if (n->flags & NET_F_PENDING) {
         // Cancel any deferred effective value update
         heap_delete(m->effective_heap, heap_delete_ptr_cb, n);
         n->flags &= ~NET_F_PENDING;
      }

      vptr += valuesz;
   }
}

void sched_deposit(rt_model_t *m, rt_signal_t *s, const void *values,
                   int offset, size_t count, int64_t after, bool nonblock)
{
   RT_LOCK(s->lock);

   if (unlikely(s->shared.flags & SIG_F_PACKED)) {
      assert(m->can_create_delta);
      if (after < 0) {
         sched_deposit_packed(m, s, offset, count, 0, 0, PACK_SRC_SCALAR,
                              after, nonblock);
         return;
      }
      if (count == 0)
         return;
      const int nwords = (count + 63) / 64;
      uint64_t sa[4], sb[4];
      uint64_t *heap LOCAL = NULL;
      uint64_t *pa = sa, *pb = sb;
      if (nwords > ARRAY_LEN(sa)) {
         heap = xmalloc_array(nwords * 2, sizeof(uint64_t));
         pa = heap;
         pb = heap + nwords;
      }
      packed_bytes_to_words(values, count, pa, pb);
      sched_deposit_packed(m, s, offset, count, (uint64_t)(uintptr_t)pa,
                           (uint64_t)(uintptr_t)pb, 0, after, nonblock);
      return;
   }

   TRACE("schedule deposit %s+%d value=%s count=%zd after=%s",
         istr(tree_ident(s->where)), offset,
         fmt_values(values, count * s->nexus.size), count, trace_time(after));

   assert(m->can_create_delta);

   // A negative delay gives a variable which is being released its forced
   // value unless it is assigned in this time step
   const bool keep = after < 0;
   if (keep)
      after = 0;

   rt_nexus_t *n = split_nexus(m, s, offset, count);
   const char *vptr = values;
   for (; count > 0; n = n->chain) {
      count -= n->width;
      assert(count >= 0);

      if (keep) {
         // A variable with a continuous assignment returns to its value
         bool driven = false;
         for (rt_source_t *src = &(n->sources); src != NULL && !driven;
              src = src->chain_input)
            driven = src->tag == SOURCE_DRIVER || src->tag == SOURCE_PORT;

         if (driven) {
            vptr += n->width * n->size;
            continue;
         }

         // Nothing drives the variable once released so it keeps whatever
         // value it has, including one assigned later in this time step
         if (n->flags & NET_F_FORCED)
            s->shared.flags |= SIG_F_REGISTER;

         vptr += n->width * n->size;
         continue;
      }

      rt_source_t *src = get_pseudo_source(m, n, SOURCE_DEPOSIT);

      waveform_t *w = alloc_waveform(m);
      w->when  = m->now + after;
      w->next  = NULL;
      w->value = alloc_value(m, n);

      copy_value_ptr(n, &w->value, vptr);

      queue_waveform(m, &src->u.pseudo.waveforms, w);

      if (nonblock && after == 0)
         deferq_do(&m->nonblockq, async_pseudo_source, src);
      else
         deltaq_insert_pseudo_source(m, after, src, nonblock);

      vptr += n->width * n->size;
   }
}

bool model_can_create_delta(rt_model_t *m)
{
   return m->can_create_delta;
}

bool model_in_reset(rt_model_t *m)
{
   return m != NULL && m->in_reset;
}

int64_t x_sv_island_in_reset(void)
{
   return model_in_reset(get_model_or_null()) ? 1 : 0;
}

int64_t model_now(rt_model_t *m, unsigned *deltas)
{
   if (deltas != NULL)
      *deltas = MAX(m->iteration, 0);

   return m->now;
}

int64_t model_next_time(rt_model_t *m)
{
   if (heap_size(m->eventq_heap) == 0)
      return TIME_HIGH;
   else
      return heap_min_key(m->eventq_heap);
}

void model_finish(rt_model_t *m)
{
   // Called by $finish and $stop
   m->finished = true;
}

void model_stop(rt_model_t *m)
{
   relaxed_store(&m->force_stop, true);
}

void model_set_phase_cb(rt_model_t *m, model_phase_t phase, rt_event_fn_t fn,
                        void *user)
{
   // Add to end of list so callbacks are called in registration order
   rt_callback_t **p = &(m->phase_cbs[phase]);
   for (; *p; p = &(*p)->next);

   rt_callback_t *cb = xcalloc(sizeof(rt_callback_t));
   cb->next = NULL;
   cb->fn   = fn;
   cb->user = user;

   *p = cb;
}

void model_set_timeout_cb(rt_model_t *m, uint64_t when, rt_event_fn_t fn,
                          void *user)
{
   rt_callback_t *cb = xcalloc(sizeof(rt_callback_t));
   cb->next = NULL;
   cb->fn   = fn;
   cb->user = user;

   assert(when > m->now);   // TODO: delta timeouts?

   void *e = tag_pointer(cb, EVENT_TIMEOUT);
   heap_insert(m->eventq_heap, when, e);
}

rt_sampler_t *model_add_sampler(rt_model_t *m, rt_event_fn_t fn, void *user)
{
   rt_sampler_t *s = xcalloc(sizeof(rt_sampler_t));
   s->fn   = fn;
   s->user = user;

   // Append so samplers are called in the order they were added
   rt_sampler_t **p = &(m->samplers);
   while (*p != NULL)
      p = &((*p)->next);
   *p = s;

   m->nsamplers++;
   return s;
}

void model_remove_sampler(rt_model_t *m, rt_sampler_t *s)
{
   // May be called from a sampler
   s->removed = true;

   if (m->sampler_depth > 0)
      return;

   for (rt_sampler_t **p = &(m->samplers); *p != NULL; p = &((*p)->next)) {
      if (*p == s) {
         *p = s->next;
         m->nsamplers--;
         free(s);
         return;
      }
   }

   should_not_reach_here();
}

rt_watch_t *watch_new(rt_model_t *m, sig_event_fn_t fn, void *user,
                      watch_kind_t kind, unsigned slots)
{
   rt_watch_t *w = xcalloc_flex(sizeof(rt_watch_t), slots,
                                sizeof(rt_signal_t *));
   w->fn        = fn;
   w->chain_all = m->watches;
   w->user_data = user;
   w->num_slots = slots;

   w->wakeable.kind      = W_WATCH;
   w->wakeable.postponed = (kind == WATCH_POSTPONED);
   w->wakeable.pending   = false;
   w->wakeable.delayed   = false;

   m->watches = w;

   return w;
}

void watch_free(rt_model_t *m, rt_watch_t *w)
{
   assert(!w->wakeable.zombie);

   for (int i = 0; i < w->next_slot; i++) {
      rt_nexus_t *n = &(w->signals[i]->nexus);
      for (int j = 0; j < w->signals[i]->n_nexus; j++, n = n->chain)
         clear_event(m, &(n->pending), &(w->wakeable));
   }

   rt_watch_t **last = &m->watches;
   for (rt_watch_t *it = *last; it;
        last = &(it->chain_all), it = it->chain_all) {
      if (it == w) {
         *last = it->chain_all;
         if (w->wakeable.pending)
            w->wakeable.zombie = true;   // Will be freed in callback
         else
            free(w);
         return;
      }
   }

   should_not_reach_here();
}

void model_set_event_cb(rt_model_t *m, rt_signal_t *s, int offset,
                        int count, rt_watch_t *w)
{
   assert(!w->wakeable.zombie);
   assert(w->next_slot < w->num_slots);
   assert(offset >= 0);
   assert(count >= 0);
   assert(offset + count <= signal_width(s));

   w->signals[w->next_slot++] = s;

   s->shared.flags &= ~SIG_F_FAST_DIRECT;

   if (unlikely(s->shared.flags & SIG_F_ISLAND) && !rt_island_is_input_watch(w))
      rt_island_on_observer_attached(m, s);

   if (unlikely(s->shared.flags & SIG_F_PACKED))
      count = count > 0 ? 1 : 0;

   rt_nexus_t *n = split_nexus(m, s, offset, count);
   for (; count > 0; n = n->chain) {
      count -= n->width;
      assert(count >= 0);

      sched_event(m, &(n->pending), &(w->wakeable));
   }
}

static void handle_interrupt_cb(jit_t *j, void *ctx)
{
   rt_proc_t *proc = get_active_proc();

   if (proc != NULL)
      jit_msg(NULL, DIAG_FATAL, "interrupted in process %s", istr(proc->name));
   else {
      diag_t *d = diag_new(DIAG_FATAL, NULL);
      diag_printf(d, "interrupted");
      diag_emit(d);
   }
}

void model_interrupt(rt_model_t *m)
{
   model_stop(m);
   jit_interrupt(m->jit, handle_interrupt_cb, m);
}

int model_exit_status(rt_model_t *m)
{
   int status;
   if (jit_exit_status(m->jit, &status))
      return status;
   else if (m->stop_delta > 0 && m->iteration == m->stop_delta)
      return EXIT_FAILURE;
   else
      return get_vhdl_assert_exit_status();
}

static bool nexus_active(rt_model_t *m, rt_nexus_t *nexus)
{
   if (nexus->n_sources > 0) {
      for (rt_source_t *s = &(nexus->sources); s; s = s->chain_input) {
         if (s->tag == SOURCE_PORT) {
            RT_LOCK(s->u.port.input->signal->lock);
            if (nexus_active(m, s->u.port.input))
               return true;
         }
         else if ((s->tag == SOURCE_DRIVER
                   && s->u.driver.waveforms.when == m->now)
                  || (is_pseudo_source(s->tag)
                      && s->u.pseudo.waveforms.when == m->now)) {
            if (nexus->active_delta == m->iteration)
               return true;
            else if (nexus->active_delta == m->iteration + 1 && s->was_active)
               return true;
         }
      }
   }

   return false;
}

static uint64_t nexus_last_active(rt_model_t *m, rt_nexus_t *nexus)
{
   int64_t last = TIME_HIGH;

   if (nexus->n_sources > 0) {
      for (rt_source_t *s = &(nexus->sources); s; s = s->chain_input) {
          if (s->tag == SOURCE_PORT) {
             RT_LOCK(s->u.port.input->signal->lock);
             last = MIN(last, nexus_last_active(m, s->u.port.input));
         }
         else if (s->tag == SOURCE_DRIVER
                  && s->u.driver.waveforms.when <= m->now)
            last = MIN(last, m->now - s->u.driver.waveforms.when);
         else if (is_pseudo_source(s->tag)
                  && s->u.pseudo.waveforms.when <= m->now)
            last = MIN(last, m->now - s->u.pseudo.waveforms.when);
      }
   }

   return last;
}

bool signal_is_forced(rt_signal_t *s, int offset, size_t count)
{
   // True if any element in the range is held by a force

   RT_LOCK(s->lock);

   if (count == 0)
      return false;

   for (rt_nexus_t *it = lookup_index(s, &offset); it != NULL && count > 0;
        it = it->chain) {
      if (offset >= it->width) {
         offset -= it->width;
         continue;
      }

      if (it->flags & NET_F_FORCED)
         return true;

      const size_t left = it->width - offset;
      count = count > left ? count - left : 0;
      offset = 0;
   }

   return false;
}

////////////////////////////////////////////////////////////////////////////////
// Connectivity queries for debuggers

static bool scope_is_verilog(const rt_scope_t *s)
{
   for (; s != NULL; s = s->parent) {
      if (s->kind == SCOPE_INSTANCE) {
         tree_t hier = tree_decl(s->where, 0);
         return tree_kind(hier) == T_HIER && tree_subkind(hier) == T_VERILOG;
      }
   }

   return false;
}

static bool scope_is_component(const rt_scope_t *s)
{
   if (s->kind != SCOPE_INSTANCE)
      return false;

   tree_t hier = tree_decl(s->where, 0);
   return tree_kind(hier) == T_HIER && tree_subkind(hier) == T_COMPONENT;
}

static bool scope_has_wrapper_child(const rt_scope_t *s)
{
   // The scope which a user sees as the one containing the contents of
   // its only child
   return scope_is_component(s)
      || (s->kind == SCOPE_INSTANCE && find_vhdl_shim_entity(s->where) != NULL);
}

static const char *scope_local_name(const rt_scope_t *s, char *buf, size_t max)
{
   // The last component of the name a user sees for this scope
   const char *full = istr(s->kind == SCOPE_INSTANCE
                           ? tree_ident(s->where) : s->name);
   const char *dot = strrchr(full, '.');
   if (s->kind != SCOPE_INSTANCE && dot != NULL && strchr(dot, '(') == NULL)
      full = dot + 1;

   checked_sprintf(buf, max, "%s", full);
   return buf;
}

static void append_component(text_buf_t *tb, const char *name, bool verilog)
{
   if (tb_len(tb) > 0)
      tb_append(tb, ' ');

   if (verilog)
      tb_cat(tb, name);
   else {
      for (const char *p = name; *p; p++)
         tb_append(tb, (*p >= 'A' && *p <= 'Z') ? *p + ('a' - 'A') : *p);
   }
}

void model_scope_path(rt_scope_t *s, text_buf_t *tb)
{
   // The path of a scope with the names separated by spaces, as in the
   // scope names of the CXXRTL protocol; Verilog names keep their case and
   // VHDL names are lower case.  The implicit block of a component
   // instantiation and the entity inside the shim for a Verilog parent have
   // no name.

   if (s->kind == SCOPE_ROOT)
      return;

   model_scope_path(s->parent, tb);

   if (scope_is_wrapper(s))
      return;

   char buf[256];
   append_component(tb, scope_local_name(s, buf, sizeof(buf)),
                    scope_is_verilog(s));
}

void model_item_path(rt_scope_t *scope, ident_t name, text_buf_t *tb)
{
   model_scope_path(scope, tb);
   append_component(tb, istr(name), scope_is_verilog(scope));
}

static bool path_match(const char *want, const char *have, bool verilog)
{
   return verilog ? strcmp(want, have) == 0 : strcasecmp(want, have) == 0;
}

static vlog_node_t port_map_for(const rt_scope_t *scope, ident_t name)
{
   // The connection of a port of a Verilog instance to the signal in the
   // scope above

   if (scope->kind != SCOPE_INSTANCE || !scope_is_verilog(scope))
      return NULL;

   const int ndecls = tree_decls(scope->where);
   for (int i = 0; i < ndecls; i++) {
      tree_t d = tree_decl(scope->where, i);
      if (tree_kind(d) != T_VERILOG)
         continue;

      vlog_node_t v = tree_vlog(d);
      if (vlog_kind(v) == V_PORT_MAP && vlog_ident(v) == name)
         return v;
   }

   return NULL;
}

static bool find_in_scope(rt_scope_t *scope, ident_t name, rt_found_t *out)
{
   for (int i = 0; i < scope->signals.count; i++) {
      rt_signal_t *s = scope->signals.items[i];
      if (tree_ident(s->where) == name) {
         *out = (rt_found_t){ .signal = s, .scope = scope, .name = name };
         return true;
      }
   }

   for (int i = 0; i < scope->aliases.count; i++) {
      rt_alias_t *a = scope->aliases.items[i];
      if (tree_ident(a->where) == name) {
         // A port that was merged with the signal it is connected to
         *out = (rt_found_t){
            .signal = a->signal,
            .offset = a->offset,
            .scope  = scope,
            .name   = name,
            .hop    = true,
         };
         return true;
      }
   }

   vlog_node_t map = port_map_for(scope, name);
   if (map != NULL && vlog_kind(vlog_value(map)) == V_REF) {
      // A port that shares the storage of the net or variable it is
      // connected to
      rt_found_t up;
      if (!find_in_scope(scope->parent, vlog_ident(vlog_value(map)), &up))
         return false;

      *out = (rt_found_t){
         .signal = up.signal,
         .offset = up.offset,
         .scope  = scope,
         .name   = name,
         .hop    = vlog_subkind(vlog_ref(map)) == V_PORT_INPUT,
         .up_scope = up.scope,
         .up_name  = up.name,
      };
      return true;
   }

   return false;
}

static bool lookup_in(rt_scope_t *scope, char *const *names, int n,
                      rt_found_t *out)
{
   char buf[256];

   if (n == 1) {
      // Names are interned in the case they were declared in
      const bool verilog = scope_is_verilog(scope);
      for (int i = 0; i < scope->signals.count; i++) {
         rt_signal_t *s = scope->signals.items[i];
         if (path_match(names[0], istr(tree_ident(s->where)), verilog))
            return find_in_scope(scope, tree_ident(s->where), out);
      }

      for (int i = 0; i < scope->aliases.count; i++) {
         rt_alias_t *a = scope->aliases.items[i];
         if (path_match(names[0], istr(tree_ident(a->where)), verilog))
            return find_in_scope(scope, tree_ident(a->where), out);
      }

      if (verilog) {
         const int ndecls = tree_decls(scope->where);
         for (int i = 0; i < ndecls; i++) {
            tree_t d = tree_decl(scope->where, i);
            if (tree_kind(d) != T_VERILOG)
               continue;

            vlog_node_t v = tree_vlog(d);
            if (vlog_kind(v) == V_PORT_MAP
                && path_match(names[0], istr(vlog_ident(v)), true)
                && find_in_scope(scope, vlog_ident(v), out))
               return true;
         }
      }
   }
   else {
      for (int i = 0; i < scope->children.count; i++) {
         rt_scope_t *c = scope->children.items[i];
         if (c->kind == SCOPE_PACKAGE)
            continue;

         const char *local = scope_local_name(c, buf, sizeof(buf));
         if (path_match(names[0], local, scope_is_verilog(c))
             && lookup_in(c, names + 1, n - 1, out))
            return true;
      }
   }

   if (scope_has_wrapper_child(scope)) {
      // The block inside a component instance or the entity inside the
      // shim of a Verilog parent has no name of its own
      for (int i = 0; i < scope->children.count; i++) {
         if (lookup_in(scope->children.items[i], names, n, out))
            return true;
      }
   }

   return false;
}

bool model_lookup_signal(rt_model_t *m, const char *path, rt_found_t *out)
{
   // Find a signal from the names of its scopes separated by spaces (see
   // model_scope_path).  A port that was merged with the signal it is
   // connected to is found too, with the signal it was merged with.

   char *copy LOCAL = xstrdup(path);
   char *names[64], *saveptr = NULL;
   int n = 0;
   for (char *t = strtok_r(copy, " ", &saveptr); t != NULL && n < 64;
        t = strtok_r(NULL, " ", &saveptr))
      names[n++] = t;

   if (n == 0)
      return false;

   return lookup_in(root_scope(m), names, n, out);
}

const char *model_proc_kind(const rt_proc_t *p)
{
   switch (tree_kind(p->where)) {
   case T_PROCESS:
      return "process";
   case T_IMPLICIT_SIGNAL:
      return "implicit";
   case T_INERTIAL:
      return "inertial";
   case T_PARAM:
      return "conversion";
   case T_VERILOG:
      switch (vlog_kind(tree_vlog(p->where))) {
      case V_ASSIGN:    return "assign";
      case V_PORT_MAP:  return "port_map";
      case V_GATE_INST: return "gate";
      case V_NET_DECL:  return "net_init";
      case V_UDP_TABLE: return "udp";
      case V_INITIAL:   return "initial";
      case V_FINAL:     return "final";
      case V_VAR_DECL:  return "var_init";
      case V_ALWAYS:
         switch (vlog_subkind(tree_vlog(p->where))) {
         case V_ALWAYS_COMB_DONE:  return "always_comb";
         case V_ALWAYS_LATCH_DONE: return "always_latch";
         case V_ALWAYS_FF:         return "always_ff";
         default:                  return "always";
         }
      default:
         return "process";
      }
   default:
      return "process";
   }
}

typedef A(rt_conn_t) conn_list_t;

typedef struct {
   ident_t name;
   bool    found;
} vlog_write_t;

static void vlog_target_writes(vlog_node_t t, vlog_write_t *w)
{
   switch (vlog_kind(t)) {
   case V_REF:
      w->found |= (vlog_ident(t) == w->name);
      break;
   case V_BIT_SELECT:
   case V_PART_SELECT:
   case V_MEMBER_REF:
      vlog_target_writes(vlog_value(t), w);
      break;
   case V_CONCAT:
      for (int i = 0; i < vlog_params(t); i++)
         vlog_target_writes(vlog_param(t, i), w);
      break;
   default:
      break;
   }
}

static void vlog_assign_writes(vlog_node_t v, void *ctx)
{
   vlog_target_writes(vlog_target(v), ctx);
}

static bool proc_assigns_to(const rt_proc_t *p, ident_t name)
{
   // Variables are written directly by procedural assignments, they have
   // no driver in the model, so look at the code of the process

   if (tree_kind(p->where) != T_VERILOG)
      return false;

   vlog_node_t v = tree_vlog(p->where);
   switch (vlog_kind(v)) {
   case V_ALWAYS:
   case V_INITIAL:
   case V_FINAL:
      break;
   default:
      return false;
   }

   vlog_write_t w = { .name = name };
   vlog_visit_only(v, vlog_assign_writes, &w, V_BASSIGN);
   vlog_visit_only(v, vlog_assign_writes, &w, V_NBASSIGN);
   return w.found;
}

static bool scope_below(const rt_scope_t *s, const rt_scope_t *above)
{
   for (; s != NULL; s = s->parent) {
      if (s == above)
         return true;
   }

   return false;
}

static void conn_add(conn_list_t *list, const rt_conn_t *c)
{
   // Join the pieces of a driver that spans several nexuses
   for (int i = 0; i < list->count; i++) {
      rt_conn_t *o = &(list->items[i]);
      if (o->kind == c->kind && o->proc == c->proc
          && o->other_scope == c->other_scope && o->other_name == c->other_name
          && o->first + o->count == c->first
          && (c->other_scope == NULL
              || o->other_first + o->count == c->other_first)) {
         o->count += c->count;
         return;
      }
   }

   APUSH(*list, *c);
}

void model_nexus_drivers(rt_signal_t *s, uint32_t offset, uint32_t count,
                         rt_conn_fn_t fn, void *ctx)
{
   // Pure read of the sources of the nexuses that overlap a range of
   // elements of the signal, without splitting any nexus

   RT_LOCK(s->lock);

   const uint32_t total = signal_width(s);
   if (offset >= total)
      return;
   else if (count == 0 || count > total - offset)
      count = total - offset;

   conn_list_t list = AINIT;

   uint32_t start = 0;
   rt_nexus_t *n = &(s->nexus);
   for (unsigned j = 0; j < s->n_nexus; j++, n = n->chain) {
      const uint32_t end = start + n->width;
      if (end > offset && start < offset + count && n->n_sources > 0) {
         const uint32_t lo = MAX(start, offset);
         const uint32_t hi = MIN(end, offset + count);

         for (rt_source_t *src = &(n->sources); src; src = src->chain_input) {
            rt_conn_t c = { .first = lo, .count = hi - lo };
            switch (src->tag) {
            case SOURCE_DRIVER:
               c.kind = RT_CONN_DRIVER;
               c.proc = src->u.driver.proc;
               c.scope = c.proc != NULL ? c.proc->scope : NULL;
               break;
            case SOURCE_PORT:
               {
                  rt_nexus_t *in = src->u.port.input;
                  if (in == NULL)
                     continue;
                  c.kind = RT_CONN_PORT;
                  c.other_scope = in->signal->parent;
                  c.other_name = tree_ident(in->signal->where);
                  c.other_first = in->offset / in->size + (lo - start);
                  c.other_width = signal_width(in->signal);

                  // The connection belongs to the instance that is lower in
                  // the hierarchy
                  if (scope_below(c.other_scope, s->parent)) {
                     c.scope = c.other_scope;
                     c.from_child = true;
                  }
                  else
                     c.scope = s->parent;
               }
               break;
            case SOURCE_FORCING:
               c.kind = RT_CONN_FORCE;
               c.scope = s->parent;
               break;
            case SOURCE_DEPOSIT:
               c.kind = RT_CONN_DEPOSIT;
               c.scope = s->parent;
               break;
            default:
               continue;
            }

            conn_add(&list, &c);
         }
      }

      start = end;
   }

   for (int i = 0; i < list.count; i++)
      (*fn)(&(list.items[i]), ctx);

   ACLEAR(list);
}

typedef struct {
   rt_conn_fn_t fn;
   void        *ctx;
   uint32_t     base;
   bool         have_driver;
} conn_relay_t;

static rt_scope_t *lookup_scope_in(rt_scope_t *scope, char *const *names,
                                   int n)
{
   char buf[256];

   if (n == 0)
      return scope;

   for (int i = 0; i < scope->children.count; i++) {
      rt_scope_t *c = scope->children.items[i];
      if (c->kind == SCOPE_PACKAGE)
         continue;

      const char *local = scope_local_name(c, buf, sizeof(buf));
      if (path_match(names[0], local, scope_is_verilog(c))) {
         rt_scope_t *r = lookup_scope_in(c, names + 1, n - 1);
         if (r != NULL)
            return r;
      }
   }

   if (scope_has_wrapper_child(scope)) {
      for (int i = 0; i < scope->children.count; i++) {
         rt_scope_t *r = lookup_scope_in(scope->children.items[i], names, n);
         if (r != NULL)
            return r;
      }
   }

   return NULL;
}

static rt_proc_t *scope_find_proc(rt_scope_t *scope, const char *name)
{
   for (int i = 0; i < scope->procs.count; i++) {
      rt_proc_t *p = scope->procs.items[i];
      const char *full = istr(p->name);
      const char *colon = strrchr(full, ':');
      if (path_match(name, colon != NULL ? colon + 1 : full,
                     scope_is_verilog(scope)))
         return p;
   }

   if (scope_has_wrapper_child(scope)) {
      for (int i = 0; i < scope->children.count; i++) {
         rt_proc_t *p = scope_find_proc(scope->children.items[i], name);
         if (p != NULL)
            return p;
      }
   }

   return NULL;
}

rt_proc_t *model_lookup_proc(rt_model_t *m, const char *scope_path,
                             const char *name)
{
   char *copy LOCAL = xstrdup(scope_path);
   char *names[64], *saveptr = NULL;
   int n = 0;
   for (char *t = strtok_r(copy, " ", &saveptr); t != NULL && n < 64;
        t = strtok_r(NULL, " ", &saveptr))
      names[n++] = t;

   rt_scope_t *scope = lookup_scope_in(root_scope(m), names, n);
   return scope != NULL ? scope_find_proc(scope, name) : NULL;
}

static rt_proc_t *scope_find_proc_at(rt_scope_t *scope, const char *file,
                                     unsigned line)
{
   for (int i = 0; i < scope->procs.count; i++) {
      rt_proc_t *p = scope->procs.items[i];
      const loc_t *loc = tree_loc(p->where);
      if (loc == NULL || loc_invalid_p(loc) || loc->first_line != line)
         continue;

      // The conn layer has the file as it was analysed, so does the loc
      if (file == NULL || strcmp(loc_file_str(loc), file) == 0)
         return p;
   }

   if (scope_has_wrapper_child(scope)) {
      for (int i = 0; i < scope->children.count; i++) {
         rt_proc_t *p = scope_find_proc_at(scope->children.items[i], file, line);
         if (p != NULL)
            return p;
      }
   }

   return NULL;
}

rt_proc_t *model_lookup_proc_at(rt_model_t *m, const char *scope_path,
                                const char *file, unsigned line)
{
   // The process of a scope that starts at a line, for matching what the
   // static description of the design says with the model

   char *copy LOCAL = xstrdup(scope_path);
   char *names[64], *saveptr = NULL;
   int n = 0;
   for (char *t = strtok_r(copy, " ", &saveptr); t != NULL && n < 64;
        t = strtok_r(NULL, " ", &saveptr))
      names[n++] = t;

   rt_scope_t *scope = lookup_scope_in(root_scope(m), names, n);
   return scope != NULL ? scope_find_proc_at(scope, file, line) : NULL;
}

static bool wakeable_is_proc(const rt_wakeable_t *w, const rt_proc_t *p)
{
   // A signal assignment that nvc turns into a transfer waits on behalf of
   // the process that made it
   switch (w->kind) {
   case W_PROC:
      return w == &(p->wakeable);
   case W_TRANSFER:
      return container_of(w, const rt_transfer_t, wakeable)->proc == p;
   default:
      return false;
   }
}

static bool nexus_waited_on_by(const rt_nexus_t *n, const rt_proc_t *p)
{
   if (n->pending == NULL)
      return false;
   else if (pointer_tag(n->pending) == 1)
      return wakeable_is_proc(untag_pointer(n->pending, rt_wakeable_t), p);

   const rt_pending_t *l = untag_pointer(n->pending, rt_pending_t);
   for (int i = 0; i < l->count; i++) {
      if (l->wake[i] != NULL
          && wakeable_is_proc(untag_pointer(l->wake[i], rt_wakeable_t), p))
         return true;
   }

   return false;
}

static bool nexus_driven_by(const rt_nexus_t *n, const rt_proc_t *p)
{
   if (n->n_sources == 0)
      return false;

   for (const rt_source_t *src = &(n->sources); src; src = src->chain_input) {
      if (src->tag == SOURCE_DRIVER && src->u.driver.proc == p)
         return true;
   }

   return false;
}

static void procsig_scan(rt_scope_t *scope, rt_proc_t *p, rt_procsig_fn_t fn,
                         void *ctx)
{
   for (int i = 0; i < scope->signals.count; i++) {
      rt_signal_t *s = scope->signals.items[i];

      // Runs of elements, as reads and as writes
      uint32_t run[2] = { 0, 0 }, len[2] = { 0, 0 }, start = 0;
      const rt_nexus_t *n = &(s->nexus);
      for (unsigned j = 0; j < s->n_nexus; j++, n = n->chain) {
         const bool hit[2] = { nexus_waited_on_by(n, p), nexus_driven_by(n, p) };
         for (int k = 0; k < 2; k++) {
            if (hit[k]) {
               if (len[k] == 0)
                  run[k] = start;
               len[k] += n->width;
            }
            else if (len[k] > 0) {
               (*fn)(s, run[k], len[k], k == 1, ctx);
               len[k] = 0;
            }
         }

         start += n->width;
      }

      for (int k = 0; k < 2; k++) {
         if (len[k] > 0)
            (*fn)(s, run[k], len[k], k == 1, ctx);
      }
   }

   for (int i = 0; i < scope->children.count; i++)
      procsig_scan(scope->children.items[i], p, fn, ctx);
}

void model_process_signals(rt_model_t *m, rt_proc_t *p, rt_procsig_fn_t fn,
                           void *ctx)
{
   // The signals a process drives are known from its drivers.  What it
   // waits for is what it is registered with at the moment, which is its
   // sensitivity while it is suspended in a wait.

   procsig_scan(root_scope(m), p, fn, ctx);
}

static void conn_relay(const rt_conn_t *c, void *arg)
{
   conn_relay_t *r = arg;
   rt_conn_t shifted = *c;
   shifted.first -= r->base;
   r->have_driver |= (c->kind == RT_CONN_DRIVER);
   (*r->fn)(&shifted, r->ctx);
}

void model_item_drivers(const rt_found_t *f, uint32_t first, uint32_t count,
                        rt_conn_fn_t fn, void *ctx)
{
   // The drivers of a signal, a port or a range of it.  Beside the sources
   // of the model this restores what the model has lost or never had:
   // ports that were merged with the signal they are connected to, and the
   // processes which assign to a Verilog variable (it has no driver).

   const uint32_t width = signal_width(f->signal);
   if (f->offset + first >= width)
      return;

   const uint32_t base = f->offset + first;
   const uint32_t n =
      count > 0 ? MIN(count, width - base) : width - base;

   if (f->hop) {
      rt_conn_t hop = {
         .kind        = RT_CONN_PORT,
         .other_scope = f->up_scope ?: f->signal->parent,
         .other_name  = f->up_name ?: tree_ident(f->signal->where),
         .other_first = base,
         .other_width = width,
         .scope       = f->scope,
         .first       = 0,
         .count       = n,
         .collapsed   = true,
      };
      (*fn)(&hop, ctx);
      return;
   }

   // Drivers that are in the model

   conn_relay_t r = { .fn = fn, .ctx = ctx, .base = base };
   model_nexus_drivers(f->signal, base, n, conn_relay, &r);

   rt_scope_t *scope = f->scope;
   if (!scope_is_verilog(scope) || scope->kind != SCOPE_INSTANCE)
      return;

   if (!r.have_driver) {
      for (int i = 0; i < scope->procs.count; i++) {
         rt_proc_t *p = scope->procs.items[i];
         if (proc_assigns_to(p, f->name)) {
            rt_conn_t c = {
               .kind   = RT_CONN_DRIVER,
               .proc   = p,
               .scope  = scope,
               .first  = 0,
               .count  = n,
               .approx = true,
            };
            (*fn)(&c, ctx);
         }
      }
   }

   // An output of an instance below that is connected to this signal
   for (int i = 0; i < scope->children.count; i++) {
      rt_scope_t *c = scope->children.items[i];
      const int ndecls = c->kind == SCOPE_INSTANCE && scope_is_verilog(c)
         ? tree_decls(c->where) : 0;
      for (int j = 0; j < ndecls; j++) {
         tree_t d = tree_decl(c->where, j);
         if (tree_kind(d) != T_VERILOG)
            continue;

         vlog_node_t map = tree_vlog(d);
         if (vlog_kind(map) != V_PORT_MAP
             || vlog_kind(vlog_value(map)) != V_REF
             || vlog_ident(vlog_value(map)) != f->name
             || vlog_subkind(vlog_ref(map)) == V_PORT_INPUT)
            continue;

         rt_conn_t hop = {
            .kind        = RT_CONN_PORT,
            .other_scope = c,
            .other_name  = vlog_ident(map),
            .other_first = 0,
            .other_width = width,
            .scope       = c,
            .first       = 0,
            .count       = n,
            .collapsed   = true,
            .from_child  = true,
         };
         (*fn)(&hop, ctx);
      }
   }
}

void get_forcing_value(rt_signal_t *s, uint8_t *value)
{
   if (unlikely(s->shared.flags & SIG_F_PACKED)) {
      rt_model_t *m = get_model();
      rt_packed_force_t *pf = m->packed_forces
         ? hash_get(m->packed_forces, s) : NULL;
      const uint32_t width = s->bit_width;
      const int nwords = (width + 63) / 64;
      const bool is4 = !!(s->shared.flags & SIG_F_PACKED4);
      const uint64_t *sa = (const uint64_t *)s->shared.data;
      const uint64_t *sb = is4 ? sa + nwords : NULL;
      for (uint32_t i = 0; i < width; i++) {
         const uint32_t bit = width - 1 - i;
         const int w = bit / 64, sh = bit % 64;
         const bool forced = pf != NULL && ((pf->mask[w] >> sh) & 1);
         const uint64_t a = forced
            ? ((pf->abits[w] >> sh) & 1) : ((sa[w] >> sh) & 1);
         const uint64_t b = forced
            ? (pf->bbits ? ((pf->bbits[w] >> sh) & 1) : 0)
            : (sb ? ((sb[w] >> sh) & 1) : 0);
         value[i] = (uint8_t)((b << 1) | a);
      }
      return;
   }

   uint8_t *p = value;
   rt_nexus_t *n = &(s->nexus);
   for (int i = 0; i < s->n_nexus; i++) {
      assert(n->n_sources > 0);
      rt_source_t *s = NULL;
      for (s = &(n->sources); s; s = s->chain_input) {
         if (s->tag == SOURCE_FORCING)
            break;
      }
      assert(s != NULL);

      memcpy(p, value_ptr(n, &(s->u.pseudo.waveforms.value)),
             n->width * n->size);
      p += n->width * n->size;
   }
   assert(p == value + s->shared.size);
}

static void arm_trigger(rt_model_t *m, rt_trigger_t *t, rt_wakeable_t *obj)
{
   switch (t->kind) {
   case CMP_TRIGGER:
      {
         assert(t->nargs == 3);
         rt_signal_t *s = t->args[0].pointer;
         int32_t offset = t->args[1].integer;

         s->shared.flags &= ~SIG_F_FAST_DIRECT;
         if (unlikely(s->shared.flags & SIG_F_ISLAND))
            rt_island_on_observer_attached(m, s);
         rt_nexus_t *n = split_nexus(m, s, offset, 1);
         sched_event(m, &(n->pending), obj);
      }
      break;
   case FUNC_TRIGGER:
      {
         if (t->nargs >= 3) {
            sig_shared_t *ss = t->args[1].pointer;
            int32_t offset = t->args[2].integer;

            ss->flags &= ~SIG_F_FAST_DIRECT;
            rt_signal_t *s = container_of(ss, rt_signal_t, shared);
            if (unlikely(s->shared.flags & SIG_F_ISLAND))
               rt_island_on_observer_attached(m, s);

            rt_nexus_t *n = split_nexus(m, s, offset, 1);
            sched_event(m, &(n->pending), obj);
         }
      }
      break;
   case LEVEL_TRIGGER:
      {
         assert(t->nargs == 3);
         rt_signal_t *s = t->args[0].pointer;
         int32_t offset = t->args[1].integer;
         int32_t count = t->args[2].integer;

         s->shared.flags &= ~SIG_F_FAST_DIRECT;
         if (unlikely(s->shared.flags & SIG_F_ISLAND))
            rt_island_on_observer_attached(m, s);
         if (unlikely(s->shared.flags & SIG_F_PACKED))
            count = count > 0 ? 1 : 0;

         rt_nexus_t *n = split_nexus(m, s, offset, count);
         for (; count > 0; n = n->chain) {
            sched_event(m, &(n->pending), obj);

            count -= n->width;
            assert(count >= 0);
         }
      }
      break;
   case OR_TRIGGER:
      {
         assert(t->nargs == 2);
         arm_trigger(m, t->args[0].pointer, obj);
         arm_trigger(m, t->args[1].pointer, obj);
      }
      break;
   }
}

static rt_trigger_t *new_trigger(rt_model_t *m, trigger_kind_t kind,
                                 uint64_t hash, jit_handle_t handle,
                                 unsigned nargs, const jit_scalar_t *args)
{
   if (m->triggertab == NULL) {
      m->triggertab_size = TRIGGER_TAB_MIN;
      m->triggertab = xcalloc_array(m->triggertab_size,
                                    sizeof(rt_trigger_t *));
   }
   else if (m->ntriggers > m->triggertab_size * 2) {
      // Triggers are never freed so keep the chains short
      const unsigned nsize = m->triggertab_size * 4;
      rt_trigger_t **ntab = xcalloc_array(nsize, sizeof(rt_trigger_t *));

      for (unsigned i = 0; i < m->triggertab_size; i++) {
         for (rt_trigger_t *t = m->triggertab[i], *next; t; t = next) {
            next = t->chain;
            rt_trigger_t **nb = &(ntab[t->hash & (nsize - 1)]);
            t->chain = *nb;
            *nb = t;
         }
      }

      free(m->triggertab);
      m->triggertab = ntab;
      m->triggertab_size = nsize;
   }

   rt_trigger_t **bucket = &(m->triggertab[hash & (m->triggertab_size - 1)]);

   for (rt_trigger_t *exist = *bucket; exist; exist = exist->chain) {
      bool hit = exist->handle == handle
         && exist->nargs == nargs
         && exist->kind == kind;

      for (int i = 0; hit && i < nargs; i++)
         hit &= (exist->args[i].integer == args[i].integer);

      if (hit)
         return exist;
   }

   const size_t argsz = nargs * sizeof(jit_scalar_t);

   rt_trigger_t *t = static_alloc(m, sizeof(rt_trigger_t) + argsz);
   memset(t, '\0', sizeof(rt_trigger_t));
   t->wakeable.kind = W_TRIGGER;
   t->handle = handle;
   t->nargs  = nargs;
   t->epoch  = UINT64_MAX;
   t->kind   = kind;
   bool level_only = (kind == LEVEL_TRIGGER);
   if (level_only) {
      m->has_level_trigger = true;
      const rt_signal_t *s = args[0].pointer;
      const uint32_t offset = args[1].integer;
      const int32_t count = args[2].integer;
      if ((s->shared.flags & SIG_F_PACKED)
          && (offset > 0 || count < (int32_t)s->bit_width))
         level_only = false;
   }
   else if (kind == OR_TRIGGER)
      level_only = ((rt_trigger_t *)args[0].pointer)->level_only
         && ((rt_trigger_t *)args[1].pointer)->level_only;
   t->level_only = level_only;
   t->chain  = *bucket;
   t->hash   = hash;
   memcpy(t->args, args, argsz);

   m->ntriggers++;

   arm_trigger(m, t, &(t->wakeable));

   return (*bucket = t);
}

void call_with_model(rt_model_t *m, void (*cb)(void *), void *arg)
{
   MODEL_ENTRY(m);
   (*cb)(arg);
}

bool scope_is_wrapper(const rt_scope_t *s)
{
   // Elaboration adds a scope at the boundary between Verilog and VHDL
   // that has no counterpart in the source: the block of the module bound
   // to a component and the block of an entity instantiated from Verilog.
   // The parent takes the name of the instance and the wrapper is not
   // shown in hierarchical names.

   const rt_scope_t *p = s->parent;
   if (p == NULL || p->kind != SCOPE_INSTANCE)
      return false;

   tree_t hier = tree_decl(p->where, 0);
   if (tree_kind(hier) != T_HIER)
      return false;
   else if (tree_subkind(hier) == T_COMPONENT)
      return true;

   return s->kind == SCOPE_INSTANCE && is_vhdl_shim_entity(p->where, s->where);
}

void get_instance_name(rt_scope_t *s, text_buf_t *tb)
{
   if (s->kind == SCOPE_ROOT)
      return;

   tree_t hier = tree_decl(s->where, 0);
   assert(tree_kind(hier) == T_HIER);

   switch (tree_subkind(hier)) {
   case T_ARCH:
      {
         tree_t unit = tree_ref(hier);

         get_instance_name(s->parent, tb);

         if (s->parent->kind != SCOPE_ROOT) {
            if (!scope_is_wrapper(s)) {
               tb_append(tb, ':');
               tb_istr(tb, tree_ident(s->where));
            }

            tb_append(tb, '@');
         }
         else
            tb_append(tb, ':');

         const char *arch = strchr(istr(tree_ident(unit)), '-') + 1;
         tb_printf(tb, "%s(%s)", istr(tree_ident2(unit)), arch);
      }
      break;

   case T_BLOCK:
   case T_FOR_GENERATE:
   case T_IF_GENERATE:
   case T_CASE_GENERATE:
      get_instance_name(s->parent, tb);
      tb_append(tb, ':');
      tb_printf(tb, "%s", istr(tree_ident(s->where)));
      break;

   case T_COMPONENT:
      get_instance_name(s->parent, tb);
      tb_printf(tb, ":%s", istr(tree_ident(s->where)));
      break;

   // TODO: Should Verilog hierarchy have the same format as VHDL ?
   case T_VERILOG:
      get_instance_name(s->parent, tb);
      if (!scope_is_wrapper(s))
         tb_printf(tb, ":%s", istr(tree_ident(s->where)));
      break;

   default:
      should_not_reach_here();
   }

   tb_downcase(tb);
}

void get_path_name(rt_scope_t *s, text_buf_t *tb)
{
   if (s->kind == SCOPE_ROOT)
      return;

   get_path_name(s->parent, tb);

   if (scope_is_wrapper(s))
      return;

   tb_append(tb, ':');
   tb_istr(tb, tree_ident(s->where));
   tb_downcase(tb);
}

void get_verilog_path_name(rt_scope_t *s, text_buf_t *tb)
{
   if (s->kind == SCOPE_ROOT)
      return;

   get_verilog_path_name(s->parent, tb);

   if (scope_is_wrapper(s))
      return;

   if (s->parent->kind != SCOPE_ROOT)
      tb_append(tb, '.');

   tb_istr(tb, tree_ident(s->where));
}

////////////////////////////////////////////////////////////////////////////////
// Entry points from compiled code

sig_shared_t *x_init_signal(int64_t count, uint32_t size, jit_scalar_t value,
                            bool scalar, sig_flags_t flags, tree_t where,
                            int32_t offset)
{
   TRACE("init signal %s count=%"PRIi64" size=%d value=%s flags=%x offset=%d",
         istr(tree_ident(where)), count, size,
         fmt_jit_value(value, scalar, size * count), flags, offset);

   rt_model_t *m = get_model();

   if (count > INT32_MAX)
      jit_msg(tree_loc(where), DIAG_FATAL, "signal %s has %"PRIi64
              " sub-elements which is greater than the maximum supported %d",
              istr(tree_ident(where)), count, INT32_MAX);

   const size_t datasz = MAX(3 * count * size, 8);
   rt_signal_t *s = static_alloc(m, sizeof(rt_signal_t) + datasz);
   setup_signal(m, s, where, count, size, flags, offset);

   // The driving value area is also used to save the default value
   void *driving = s->shared.data + 2*s->shared.size;

   if (scalar) {
#define COPY_SCALAR(type) do {                  \
         type *pi = (type *)s->shared.data;     \
         type *pd = (type *)driving;            \
         for (int i = 0; i < count; i++)        \
            pi[i] = pd[i] = value.integer;      \
      } while (0)

      FOR_ALL_SIZES(size, COPY_SCALAR);
   }
   else {
      memcpy(s->shared.data, value.pointer, s->shared.size);
      memcpy(driving, value.pointer, s->shared.size);
   }

   return &(s->shared);
}

void x_drive_signal(sig_shared_t *ss, uint32_t offset, int32_t count)
{
   rt_signal_t *s = container_of(ss, rt_signal_t, shared);
   RT_LOCK(s->lock);

   if (unlikely(ss->flags & SIG_F_PACKED)) {
      ss->flags |= SIG_F_PACKED_DRIVEN;
      return;
   }

   TRACE("drive signal %s+%d count=%d", istr(tree_ident(s->where)),
         offset, count);

   rt_model_t *m = get_model();
   rt_proc_t *proc = get_active_proc();
   rt_nexus_t *n = split_nexus(m, s, offset, count);
   for (; count > 0; n = n->chain) {
      rt_source_t *s;
      for (s = &(n->sources); s; s = s->chain_input) {
         if (s->tag == SOURCE_DRIVER && s->u.driver.proc == proc)
            break;
      }

      if (s == NULL) {
         s = add_source(m, n, SOURCE_DRIVER);
         s->u.driver.waveforms.value = alloc_value(m, n);
         s->u.driver.proc = proc;
         proc_pin_forever(proc);
      }

      count -= n->width;
      assert(count >= 0);
   }
}

typedef struct {
   char *file;
   int   line;
} stmt_break_t;

typedef struct {
   const char *file;   // Owned by the identifier table
   int         line;
} stmt_site_t;

static A(stmt_break_t)  stmt_breaks;
static A(stmt_site_t)   stmt_sites;
static bool             stmt_stepping;
static rt_proc_t       *stmt_step_proc;   // Step only in this process
static int              stmt_step_mode;   // STMT_STEP_* when depth-aware
static int              stmt_step_depth;  // Call depth the step started at
static const loc_t     *stmt_probe_loc;   // Statement that the hook is for
static const jit_scalar_t *stmt_probe_args;   // Arguments of the probe exit
static stmt_hook_fn_t   stmt_hook;
static void            *stmt_hook_context;

uint8_t jit_stmt_armed[JIT_STMT_SITES] = { [0] = 1 };

void set_stmt_hook(stmt_hook_fn_t fn, void *context)
{
   stmt_hook = fn;
   stmt_hook_context = context;
}

static inline bool is_dir_sep(char c)
{
#ifdef __MINGW32__
   return c == '/' || c == '\\';
#else
   return c == '/';
#endif
}

static bool same_file(const char *a, const char *b)
{
   // One name may be a path that ends with the other
   const size_t alen = strlen(a), blen = strlen(b);
   const char *longer = alen >= blen ? a : b;
   const char *shorter = alen >= blen ? b : a;
   const size_t llen = alen >= blen ? alen : blen;
   const size_t slen = alen >= blen ? blen : alen;

   const char *tail = longer + (llen - slen);
   for (size_t i = 0; i < slen; i++) {
      if (is_dir_sep(tail[i]) && is_dir_sep(shorter[i]))
         continue;
      else if (tail[i] != shorter[i])
         return false;
   }

   return llen == slen || is_dir_sep(longer[llen - slen - 1]);
}

static bool stmt_site_armed(const stmt_site_t *site)
{
   if (stmt_stepping)
      return true;

   for (int i = 0; i < stmt_breaks.count; i++) {
      const stmt_break_t *b = &(stmt_breaks.items[i]);
      if (b->line == site->line && site->file != NULL
          && same_file(b->file, site->file))
         return true;
   }

   return false;
}

static void stmt_rearm(void)
{
   // The sites start at one: zero is the site of statements without one
   for (int i = 0; i < stmt_sites.count; i++)
      jit_stmt_armed[i + 1] = stmt_site_armed(&(stmt_sites.items[i]));
}

uint32_t stmt_site_new(const char *file, int line)
{
   if (stmt_sites.count + 1 >= JIT_STMT_SITES)
      return 0;   // Too many: the statement always takes the exit

   stmt_site_t site = { .file = file, .line = line };
   APUSH(stmt_sites, site);

   const uint32_t id = stmt_sites.count;
   jit_stmt_armed[id] = stmt_site_armed(&site);
   return id;
}

void stmt_step_set(bool enable)
{
   stmt_stepping = enable;
   stmt_step_proc = NULL;
   stmt_step_mode = STMT_STEP_IN;
   stmt_rearm();
}

bool stmt_step_process(void)
{
   // Called from the hook: limit the stepping that follows to the process
   // that the statement belongs to
   rt_proc_t *proc = get_active_proc();
   if (proc == NULL || !stmt_stepping)
      return false;

   stmt_step_proc = proc;
   return true;
}

bool stmt_step_depth_set(int mode)
{
   // Called from the hook after stmt_step_set(true): step in the current
   // process but only to statements at the call depth that mode asks for,
   // relative to the depth of the statement that the hook is for
   if (mode < STMT_STEP_IN || mode > STMT_STEP_OUT || !stmt_step_process())
      return false;

   stmt_step_depth = (mode == STMT_STEP_IN) ? 0 : jit_stack_depth();

   // Nothing to return to from the outermost frame: stop at the next
   // statement of the process, as over does
   if (mode == STMT_STEP_OUT && stmt_step_depth <= 1)
      mode = STMT_STEP_OVER;

   stmt_step_mode = mode;
   return true;
}

bool stmt_probe_where(loc_t *loc)
{
   if (stmt_probe_loc == NULL)
      return false;

   *loc = *stmt_probe_loc;
   return true;
}

const jit_scalar_t *stmt_probe_frame_args(void)
{
   return stmt_probe_args;
}

static bool stmt_file_match(const char *file, void *ctx)
{
   return same_file(file, ctx);
}

int stmt_site_query(const char *file, int line, int *nearest)
{
   // The sites are created when a unit is lowered and subprograms are not
   // lowered before they are called, so do that for the file first
   if (__model != NULL)
      jit_lower_file(__model->jit, stmt_file_match, (void *)file);

   int result = 0, best = 0;
   for (int i = 0; i < stmt_sites.count; i++) {
      const stmt_site_t *site = &(stmt_sites.items[i]);
      if (site->file == NULL || !same_file(file, site->file))
         continue;

      result |= STMT_SITE_FILE;
      if (site->line == line)
         result |= STMT_SITE_EXACT;
      else if (site->line > line && (best == 0 || site->line < best))
         best = site->line;
   }

   *nearest = (result & STMT_SITE_EXACT) ? line : best;
   return result;
}

void stmt_break_add(const char *file, int line)
{
   stmt_break_t b = { .file = xstrdup(file), .line = line };
   APUSH(stmt_breaks, b);
   stmt_rearm();
}

bool stmt_break_remove(const char *file, int line)
{
   for (int i = 0; i < stmt_breaks.count; i++) {
      stmt_break_t *b = &(stmt_breaks.items[i]);
      if (b->line == line && same_file(b->file, file)) {
         free(b->file);
         stmt_breaks.items[i] = stmt_breaks.items[--stmt_breaks.count];
         stmt_rearm();
         return true;
      }
   }

   return false;
}

void x_stmt_probe(object_t *where, const jit_scalar_t *args)
{
   // Called before a statement of code generated with --debug-stmts if
   // its site is armed
   if (stmt_hook == NULL)
      return;

   const loc_t *loc = &(where->loc);
   if (loc_invalid_p(loc))
      return;

   const char *file = loc_file_str(loc);

   rt_proc_t *proc = get_active_proc();

   // A step stops only in a process, and in the one that it started in if
   // that was asked for: statements outside a process are those of the
   // functions that evaluate the conditions of waits in the scheduler and
   // other code that no process is running
   bool match = stmt_stepping && proc != NULL
      && (stmt_step_proc == NULL || stmt_step_proc == proc);

   // The depth is only needed, and only computed, by a depth-aware step of
   // the process that it is for
   if (match && stmt_step_mode != STMT_STEP_IN && stmt_step_proc == proc) {
      const int depth = jit_stack_depth();
      match = (stmt_step_mode == STMT_STEP_OVER && depth <= stmt_step_depth)
         || (stmt_step_mode == STMT_STEP_OUT && depth < stmt_step_depth);
   }
   for (int i = 0; i < stmt_breaks.count && !match; i++) {
      const stmt_break_t *b = &(stmt_breaks.items[i]);
      match = b->line == loc->first_line && same_file(b->file, file);
   }

   if (match) {
      const loc_t *saved = stmt_probe_loc;
      const jit_scalar_t *saved_args = stmt_probe_args;
      stmt_probe_loc = loc;
      stmt_probe_args = args;
      (*stmt_hook)(file, loc->first_line, proc, stmt_hook_context);
      stmt_probe_loc = saved;
      stmt_probe_args = saved_args;
   }
}

void x_sched_process(int64_t delay)
{
   rt_proc_t *proc = get_active_proc();
   if (proc == NULL)
      return;    // May be called during constant folding

   TRACE("schedule process %s delay=%s", istr(proc->name), trace_time(delay));

   check_delay(delay);
   deltaq_insert_proc(get_model(), delay, proc);
}

void x_sched_inactive(void)
{
   rt_proc_t *proc = get_active_proc();
   rt_model_t *m = get_model();

   TRACE("schedule process %s in inactive region", istr(proc->name));

   set_pending(&proc->wakeable);
   deferq_do(&m->inactiveq, async_run_process, proc);
   m->next_is_delta = true;
}

int64_t x_sv_sva_flushing(void)
{
   return get_model()->sva_flush;
}

void x_sv_sched_postponed(bool late)
{
   rt_proc_t *proc = get_active_proc();
   rt_model_t *m = get_model();

   TRACE("schedule process %s in postponed region", istr(proc->name));

   set_pending(&proc->wakeable);
   deferq_do(late ? &m->latepostq : &m->postponedq, async_run_process, proc);
}

static inline bool quiet_driver(rt_nexus_t *n, const void *value)
{
   // True if a zero-delay transaction with this value on the only driver
   // of the nexus would take the "will not observe" fast driver path of
   // sched_driver without any other effect
   if ((n->flags & (NET_F_FAST_DRIVER | NET_F_EFFECTIVE)) != NET_F_FAST_DRIVER)
      return false;

   rt_source_t *d = &(n->sources);
   if (d->fastqueued)
      return false;

   return cmp_bytes(value, value_ptr(n, &(d->u.driver.waveforms.value)),
                    n->width * n->size);
}

static inline void quiet_transaction(rt_model_t *m, rt_nexus_t *n)
{
   // Same as the fast driver path of sched_driver for a transaction that
   // does not change the value of the driver: the driver is active in the
   // next delta cycle but there is nothing to update
   rt_source_t *d = &(n->sources);
   d->u.driver.waveforms.when = m->now;
   d->was_active = (n->active_delta == m->iteration);
   n->active_delta = m->iteration + 1;
}

__attribute__((noinline))
static bool quiet_slices(rt_model_t *m, rt_signal_t *s, uint32_t offset,
                         const void *values, int32_t count)
{
   rt_nexus_t *n = &(s->nexus);
   if (offset != 0) {
      int off = offset;
      for (n = lookup_index(s, &off); n != NULL && off >= n->width;
           n = n->chain)
         off -= n->width;

      if (n == NULL || off != 0)
         return false;   // Would split a nexus
   }

   const uint8_t *vptr = values;
   rt_nexus_t *it = n;
   for (int left = count; left > 0; it = it->chain) {
      if (it == NULL || it->width > left)
         return false;   // Would split a nexus
      else if (!quiet_driver(it, vptr))
         return false;

      vptr += it->width * it->size;
      left -= it->width;
   }

   RT_LOCK(s->lock);

   for (int left = count; left > 0; n = n->chain) {
      quiet_transaction(m, n);
      left -= n->width;
   }

   m->next_is_delta = true;
   return true;
}

static inline bool sched_waveform_quiet(sig_shared_t *ss, uint32_t offset,
                                        const void *values, int32_t count)
{
   // Short path for the most common transaction in RTL designs: a
   // zero-delay assignment which does not change the value of any of the
   // drivers it updates, where each nexus has a single fast driver.
   // Returns false without any effect for anything else, and then the
   // caller takes the full path through x_sched_waveform.

   rt_signal_t *s = container_of(ss, rt_signal_t, shared);
   rt_model_t *m = __model;

   if (unlikely(__trace_on) || unlikely(!m->can_create_delta))
      return false;   // Postponed processes go through check_postponed
   else if (unlikely(++m->quiet_count == 0))
      return false;   // The full path checks for interrupts now and then
   else if (ss->flags & SIG_F_PACKED)
      return false;
   else if ((ss->flags & NET_F_FAST_DRIVER) && s->nexus.sources.sigqueued)
      return false;   // Update of all drivers already scheduled

   rt_nexus_t *n = &(s->nexus);
   if (likely(offset == 0 && n->width == count)) {
      if (!quiet_driver(n, values))
         return false;

      RT_LOCK(s->lock);
      quiet_transaction(m, n);
      m->next_is_delta = true;
      return true;
   }
   else
      return quiet_slices(m, s, offset, values, count);
}

void x_sched_waveform_zero(struct _jit_anchor *anchor, sig_shared_t *ss,
                           uint32_t offset, int32_t count, void *values)
{
   // Entry from the fast exit for a zero-delay waveform: the full path
   // runs only when the short path does not apply, as a tail call so the
   // short path needs no stack frame
   if (!sched_waveform_quiet(ss, offset, values, count))
      slow_sched_waveform_0(anchor, ss, offset, count, values);
}

void x_sched_waveform_zero_s(struct _jit_anchor *anchor, sig_shared_t *ss,
                             uint32_t offset, uint64_t scalar)
{
   if (!sched_waveform_quiet(ss, offset, &scalar, 1))
      slow_sched_waveform_s0(anchor, ss, offset, scalar);
}

void x_sched_waveform_s(sig_shared_t *ss, uint32_t offset, uint64_t scalar,
                        int64_t after, int64_t reject)
{
   rt_signal_t *s = container_of(ss, rt_signal_t, shared);
   RT_LOCK(s->lock);

   TRACE("sched waveform %pI+%d value=%"PRIi64" after=%s reject=%s",
         tree_ident(s->where), offset, scalar, trace_time(after),
         trace_time(reject));

   rt_proc_t *proc = get_active_proc();

   check_delay(after);
   check_postponed(after, proc);
   check_reject_limit(s, after, reject);

   rt_model_t *m = get_model();
   rt_nexus_t *n = split_nexus(m, s, offset, 1);

   sched_driver(m, n, after, reject, &scalar, proc);
}

void x_sched_waveform_s0(sig_shared_t *ss, uint32_t offset, uint64_t scalar)
{
   rt_signal_t *s = container_of(ss, rt_signal_t, shared);
   RT_LOCK(s->lock);

   TRACE("sched waveform %pI+%d value=%"PRIi64" after=0fs reject=0fs",
         tree_ident(s->where), offset, scalar);

   rt_proc_t *proc = get_active_proc();
   check_postponed(0, proc);

   rt_model_t *m = get_model();
   rt_nexus_t *n = split_nexus(m, s, offset, 1);

   sched_driver(m, n, 0, 0, &scalar, proc);
}

void x_sched_waveform(sig_shared_t *ss, uint32_t offset, void *values,
                      int32_t count, int64_t after, int64_t reject)
{
   rt_signal_t *s = container_of(ss, rt_signal_t, shared);
   RT_LOCK(s->lock);

   TRACE("sched waveform %pI+%d value=%s count=%d after=%s reject=%s",
         tree_ident(s->where), offset,
         fmt_values(values, count * s->nexus.size),
         count, trace_time(after), trace_time(reject));

   rt_proc_t *proc = get_active_proc();

   check_delay(after);
   check_postponed(after, proc);
   check_reject_limit(s, after, reject);

   rt_model_t *m = get_model();
   rt_nexus_t *n = split_nexus(m, s, offset, count);
   char *vptr = values;
   for (; count > 0; n = n->chain) {
      count -= n->width;
      assert(count >= 0);

      sched_driver(m, n, after, reject, vptr, proc);
      vptr += n->width * n->size;
   }
}

void x_transfer_signal(sig_shared_t *target_ss, uint32_t toffset,
                       sig_shared_t *source_ss, uint32_t soffset,
                       int32_t count, int64_t after, int64_t reject)
{
   rt_signal_t *target = container_of(target_ss, rt_signal_t, shared);
   rt_signal_t *source = container_of(source_ss, rt_signal_t, shared);

   TRACE("transfer signal %s+%d to %s+%d count=%d",
         istr(tree_ident(source->where)), soffset,
         istr(tree_ident(target->where)), toffset, count);

   rt_proc_t *proc = get_active_proc();

   check_delay(after);
   check_postponed(after, proc);
   check_reject_limit(target, after, reject);

   source_ss->flags &= ~SIG_F_FAST_DIRECT;
   target_ss->flags &= ~SIG_F_FAST_DIRECT;

   rt_model_t *m = get_model();
   if (unlikely(source_ss->flags & SIG_F_ISLAND))
      rt_island_on_observer_attached(m, source);

   rt_transfer_t *t = static_alloc(m, sizeof(rt_transfer_t));
   t->proc   = proc;
   t->target = split_nexus(m, target, toffset, count);
   t->source = split_nexus(m, source, soffset, count);
   t->count  = count;
   t->after  = after;
   t->reject = reject;

   t->wakeable.kind      = W_TRANSFER;
   t->wakeable.postponed = false;
   t->wakeable.pending   = false;
   t->wakeable.delayed   = false;

   for (rt_nexus_t *n = t->source; count > 0; n = n->chain) {
      sched_event(m, &(n->pending), &(t->wakeable));

      if (!t->wakeable.pending) {
         // Schedule initial update immediately
         deferq_do(&m->procq, async_transfer_signal, t);
         t->wakeable.pending = true;
      }

      count -= n->width;
      assert(count >= 0);
   }
}

int32_t x_test_net_event(sig_shared_t *ss, uint32_t offset, int32_t count)
{
   rt_signal_t *s = container_of(ss, rt_signal_t, shared);
   RT_LOCK(s->lock);

   TRACE("_test_net_event %s offset=%d count=%d",
         istr(tree_ident(s->where)), offset, count);

   ss->flags &= ~SIG_F_FAST_DIRECT;

   int32_t result = 0;
   rt_model_t *m = get_model();
   if (unlikely(ss->flags & SIG_F_ISLAND))
      rt_island_on_observer_attached(m, s);
   if (unlikely(ss->flags & SIG_F_PACKED))
      count = count > 0 ? 1 : 0;
   rt_nexus_t *n = split_nexus(m, s, offset, count);
   for (; count > 0; n = n->chain) {
      if (n->last_event == m->now && n->event_delta == m->iteration) {
         result = 1;
         break;
      }

      count -= n->width;
      assert(count >= 0);
   }

   if ((ss->flags & SIG_F_PACKED) || ss->size == s->nexus.size) {
      assert(!(ss->flags & SIG_F_CACHE_EVENT));   // Should have taken fast-path
      ss->flags |= SIG_F_CACHE_EVENT;
      s->nexus.flags |= NET_F_CACHE_EVENT;
      if (result)
         set_event_flag(m, s);
   }

   return result;
}

int32_t x_test_net_active(sig_shared_t *ss, uint32_t offset, int32_t count)
{
   rt_signal_t *s = container_of(ss, rt_signal_t, shared);
   RT_LOCK(s->lock);

   TRACE("_test_net_active %s offset=%d count=%d",
         istr(tree_ident(s->where)), offset, count);

   ss->flags &= ~SIG_F_FAST_DIRECT;

   rt_model_t *m = get_model();
   if (unlikely(ss->flags & SIG_F_ISLAND))
      rt_island_on_observer_attached(m, s);
   if (unlikely(ss->flags & SIG_F_PACKED))
      count = count > 0 ? 1 : 0;
   rt_nexus_t *n = split_nexus(m, s, offset, count);
   for (; count > 0; n = n->chain) {
      if (nexus_active(m, n))
         return 1;

      count -= n->width;
      assert(count >= 0);
   }

   return 0;
}

void x_sched_event(sig_shared_t *ss, uint32_t offset, int32_t count)
{
   rt_signal_t *s = container_of(ss, rt_signal_t, shared);
   RT_LOCK(s->lock);

   TRACE("schedule event %s+%d count=%d", istr(tree_ident(s->where)),
         offset, count);

   ss->flags &= ~SIG_F_FAST_DIRECT;

   rt_wakeable_t *obj = get_active_wakeable();

   rt_model_t *m = get_model();
   if (unlikely(ss->flags & SIG_F_ISLAND))
      rt_island_on_observer_attached(m, s);
   if (unlikely(ss->flags & SIG_F_PACKED))
      count = count > 0 ? 1 : 0;
   rt_nexus_t *n = split_nexus(m, s, offset, count);
   for (; count > 0; n = n->chain) {
      sched_event(m, &(n->pending), obj);

      count -= n->width;
      assert(count >= 0);
   }
}

void x_clear_event(sig_shared_t *ss, uint32_t offset, int32_t count)
{
   rt_signal_t *s = container_of(ss, rt_signal_t, shared);
   RT_LOCK(s->lock);

   TRACE("clear event %s+%d count=%d",
         istr(tree_ident(s->where)), offset, count);

   rt_model_t *m = get_model();
   rt_proc_t *proc = get_active_proc();
   if (unlikely(ss->flags & SIG_F_PACKED))
      count = count > 0 ? 1 : 0;
   rt_nexus_t *n = split_nexus(m, s, offset, count);
   for (; count > 0; n = n->chain) {
      clear_event(m, &(n->pending), &(proc->wakeable));

      count -= n->width;
      assert(count >= 0);
   }
}

void x_sched_active(sig_shared_t *ss, uint32_t offset, int32_t count)
{
   rt_signal_t *s = container_of(ss, rt_signal_t, shared);
   RT_LOCK(s->lock);

   TRACE("schedule active %s+%d count=%d", istr(tree_ident(s->where)),
         offset, count);

   ss->flags &= ~SIG_F_FAST_DIRECT;

   rt_wakeable_t *obj = get_active_wakeable();
   rt_model_t *m = get_model();
   if (unlikely(ss->flags & SIG_F_ISLAND))
      rt_island_on_observer_attached(m, s);

   if (obj->kind == W_PROC)
      proc_pin_forever(container_of(obj, rt_proc_t, wakeable));

   rt_nexus_t *n = split_nexus(m, s, offset, count);
   for (; count > 0; n = n->chain) {
      // TODO: it would be better to use a separate pending list for this
      rt_source_t *src = static_alloc(m, sizeof(rt_source_t));
      src->tag          = SOURCE_ACTIVE;
      src->chain_output = n->outputs;
      src->u.wakeable   = obj;

      n->flags |= NET_F_EFFECTIVE;
      n->outputs = src;

      count -= n->width;
      assert(count >= 0);
   }
}

void x_enable_trigger(rt_trigger_t *trigger)
{
   TRACE("enable trigger %p", trigger);

   rt_wakeable_t *obj = get_active_wakeable();
   rt_model_t *m = get_model();

   sched_event(m, &(trigger->pending), obj);
}

void x_disable_trigger(rt_trigger_t *trigger)
{
   TRACE("disable trigger %p", trigger);

   rt_wakeable_t *obj = get_active_wakeable();
   rt_model_t *m = get_model();

   clear_event(m, &(trigger->pending), obj);
}

void x_enter_state(int32_t state, bool strong)
{
   rt_wakeable_t *obj = get_active_wakeable();
   assert(obj->kind == W_PROPERTY);

   rt_prop_t *prop = container_of(obj, rt_prop_t, wakeable);
   mask_set(&prop->newstate, state);
   prop->strong |= strong;
}

static void psl_set_signal(rt_model_t *m, rt_prop_t *prop, bool value)
{
   if (!prop->sig_searched) {
      ident_t id = ident_prefix(prop->label, ident_new("SIGNAL"), '$');
      for (int i = 0; i < prop->scope->signals.count; i++) {
         rt_signal_t *s = prop->scope->signals.items[i];
         if (tree_ident(s->where) == id) {
            prop->sig = s;
            break;
         }
      }

      prop->sig_searched = true;
   }

   if (prop->sig != NULL) {
      const uint8_t byte = value;
      sched_deposit(m, prop->sig, &byte, 0, 1, 0, false);
   }
}

void model_psl_event(rt_model_t *m, psl_event_t event)
{
   rt_wakeable_t *obj = get_active_wakeable();
   if (obj == NULL || obj->kind != W_PROPERTY)
      return;

   rt_prop_t *prop = container_of(obj, rt_prop_t, wakeable);

   switch (event) {
   case PSL_EVENT_COVER:
      TRACE("PSL cover event in property %pi", prop->name);
      prop->covered = true;
      psl_set_signal(m, prop, true);
      break;
   case PSL_EVENT_FAIL:
      TRACE("PSL assert fail event in property %pi", prop->name);
      prop->failed = true;
      psl_set_signal(m, prop, true);
      break;
   case PSL_EVENT_PASS:
      TRACE("PSL assert pass event in property %pi", prop->name);
      prop->covered = true;
      break;
   }
}

typedef struct {
   bool has_cover;
   bool has_assert;
   bool all_covered;
   bool all_asserts_covered;
   bool any_failed;
} psl_status_t;

static void psl_scan_scope(rt_model_t *m, rt_scope_t *scope,
                           psl_status_t *status, bool clear)
{
   for (int i = 0; i < scope->properties.count; i++) {
      rt_prop_t *prop = scope->properties.items[i];

      if (clear) {
         prop->covered = prop->failed = false;
         psl_set_signal(m, prop, false);
         continue;
      }

      switch (psl_kind(prop->where)) {
      case P_COVER:
         status->has_cover = true;
         status->all_covered &= prop->covered;
         break;
      case P_ASSERT:
         status->has_assert = true;
         status->all_asserts_covered &= prop->covered;
         status->any_failed |= prop->failed;
         if (m->psl_cover_assert) {
            status->has_cover = true;
            status->all_covered &= prop->covered;
         }
         break;
      default:
         status->any_failed |= prop->failed;
         break;
      }
   }

   for (int i = 0; i < scope->children.count; i++)
      psl_scan_scope(m, scope->children.items[i], status, clear);
}

bool model_psl_status(rt_model_t *m, psl_query_t query)
{
   psl_status_t status = {
      .has_cover = false,
      .has_assert = false,
      .all_covered = true,
      .all_asserts_covered = true,
      .any_failed = false,
   };
   psl_scan_scope(m, m->root, &status, false);

   switch (query) {
   case PSL_QUERY_COVERED: return status.has_cover && status.all_covered;
   case PSL_QUERY_ASSERT_COVERED:
      return m->psl_cover_assert && status.has_assert
         && status.all_asserts_covered;
   case PSL_QUERY_FAILED: return status.any_failed;
   }

   return false;
}

void model_psl_clear(rt_model_t *m)
{
   psl_scan_scope(m, m->root, NULL, true);
}

void model_psl_set_cover_assert(rt_model_t *m, bool enable)
{
   m->psl_cover_assert = enable;
}

bool model_psl_get_cover_assert(rt_model_t *m)
{
   return m->psl_cover_assert;
}

void x_alias_signal(sig_shared_t *ss, uint32_t offset, tree_t where)
{
   rt_signal_t *s = container_of(ss, rt_signal_t, shared);
   RT_LOCK(s->lock);

   TRACE("alias signal %s+%u to %s", istr(tree_ident(s->where)), offset,
         istr(tree_ident(where)));

   rt_alias_t *a = xcalloc(sizeof(rt_alias_t));
   a->where  = where;
   a->signal = s;
   a->offset = offset;

   model_thread_t *thread = model_thread(get_model());
   APUSH(thread->active_scope->aliases, a);
}

int64_t x_last_event(sig_shared_t *ss, uint32_t offset, int32_t count)
{
   rt_signal_t *s = container_of(ss, rt_signal_t, shared);
   RT_LOCK(s->lock);

   TRACE("_last_event %s offset=%d count=%d",
         istr(tree_ident(s->where)), offset, count);

   ss->flags &= ~SIG_F_FAST_DIRECT;

   int64_t last = TIME_HIGH;

   rt_model_t *m = get_model();
   if (unlikely(ss->flags & SIG_F_ISLAND))
      rt_island_on_observer_attached(m, s);
   if (unlikely(ss->flags & SIG_F_PACKED))
      count = count > 0 ? 1 : 0;
   rt_nexus_t *n = split_nexus(m, s, offset, count);
   for (; count > 0; n = n->chain) {
      if (n->last_event <= m->now)
         last = MIN(last, m->now - n->last_event);

      count -= n->width;
      assert(count >= 0);
   }

   return last;
}

int64_t x_last_active(sig_shared_t *ss, uint32_t offset, int32_t count)
{
   rt_signal_t *s = container_of(ss, rt_signal_t, shared);
   RT_LOCK(s->lock);

   TRACE("_last_active %s offset=%d count=%d",
         istr(tree_ident(s->where)), offset, count);

   ss->flags &= ~SIG_F_FAST_DIRECT;

   int64_t last = TIME_HIGH;

   rt_model_t *m = get_model();
   if (unlikely(ss->flags & SIG_F_ISLAND))
      rt_island_on_observer_attached(m, s);
   if (unlikely(ss->flags & SIG_F_PACKED))
      count = count > 0 ? 1 : 0;
   rt_nexus_t *n = split_nexus(m, s, offset, count);
   for (; count > 0; n = n->chain) {
      last = MIN(last, nexus_last_active(m, n));

      count -= n->width;
      assert(count >= 0);
   }

   return last;
}

void x_map_signal(sig_shared_t *src_ss, uint32_t src_offset,
                  sig_shared_t *dst_ss, uint32_t dst_offset, uint32_t count)
{
   rt_signal_t *src_s = container_of(src_ss, rt_signal_t, shared);
   RT_LOCK(src_s->lock);

   rt_signal_t *dst_s = container_of(dst_ss, rt_signal_t, shared);
   RT_LOCK(dst_s->lock);

   TRACE("map signal %s+%d to %s+%d count %d",
         istr(tree_ident(src_s->where)), src_offset,
         istr(tree_ident(dst_s->where)), dst_offset, count);

   assert(src_s != dst_s);

   src_ss->flags &= ~(SIG_F_FAST_NOOP | SIG_F_FAST_DIRECT);
   dst_ss->flags &= ~(SIG_F_FAST_NOOP | SIG_F_FAST_DIRECT);

   rt_model_t *m = get_model();

   rt_nexus_t *src_n = split_nexus(m, src_s, src_offset, count);
   rt_nexus_t *dst_n = split_nexus(m, dst_s, dst_offset, count);

   while (count > 0) {
      if (src_n->width > dst_n->width)
         clone_nexus(m, src_n, dst_n->width);
      else if (src_n->width < dst_n->width)
         clone_nexus(m, dst_n, src_n->width);

      assert(src_n->width == dst_n->width);
      assert(src_n->size == dst_n->size);

      // Effective value updates must propagate through ports
      src_n->flags |= (dst_n->flags & NET_F_EFFECTIVE);
      dst_n->flags |= (src_n->flags & NET_F_EFFECTIVE);

      rt_source_t *port = add_source(m, dst_n, SOURCE_PORT);
      port->u.port.input = src_n;

      port->chain_output = src_n->outputs;
      src_n->outputs = port;

      update_rank(dst_n, src_n->rank + 1);

      count -= src_n->width;
      assert(count >= 0);

      src_n = src_n->chain;
      dst_n = dst_n->chain;
   }
}

void x_map_const(sig_shared_t *ss, uint32_t offset,
                 const uint8_t *values, uint32_t count)
{
   rt_signal_t *s = container_of(ss, rt_signal_t, shared);
   RT_LOCK(s->lock);

   TRACE("map const %s to %s+%d count %d", fmt_values(values, count),
         istr(tree_ident(s->where)), offset, count);

   rt_model_t *m = get_model();
   rt_nexus_t *n = split_nexus(m, s, offset, count);
   for (; count > 0; n = n->chain) {
      const size_t valuesz = n->size * n->width;
      memcpy(nexus_effective(n), values, valuesz);
      memcpy(nexus_initial(n), values, valuesz);

      n->flags |= NET_F_HAS_INITIAL;
      values += valuesz;

      count -= n->width;
      assert(count >= 0);
   }
}

void x_push_scope(tree_t where, int32_t size, rt_scope_kind_t kind)
{
   TRACE("push scope %s size=%d kind=%d", istr(tree_ident(where)), size, kind);

   rt_model_t *m = get_model();
   model_thread_t *thread = model_thread(m);

   ident_t name;
   if (thread->active_scope && thread->active_scope->kind == SCOPE_ARRAY)
      name = ident_sprintf("%s(%d)", istr(thread->active_scope->name),
                           thread->active_scope->children.count);
   else if (thread->active_scope && thread->active_scope->kind == SCOPE_RECORD)
      name = ident_prefix(thread->active_scope->name, tree_ident(where), '.');
   else
      name = tree_ident(where);

   rt_scope_t *s = xcalloc(sizeof(rt_scope_t));
   s->where    = where;
   s->name     = name;
   s->kind     = kind;
   s->parent   = thread->active_scope;
   s->size     = size;
   s->privdata = MPTR_INVALID;

   if (kind != SCOPE_PACKAGE) {
      type_t type = tree_type(where);
      assert(type_is_composite(type));
      if (type_kind(type) == T_SUBTYPE && type_has_resolution(type))
         s->flags |= SCOPE_F_RESOLVED;
   }

   thread->active_scope = s;
}

void x_pop_scope(void)
{
   rt_model_t *m = get_model();
   model_thread_t *thread = model_thread(m);

   rt_scope_t *pop = thread->active_scope, *old = pop->parent;

   TRACE("pop scope %s", istr(tree_ident(pop->where)));

   int offset = INT_MAX;
   for (int i = 0; i < pop->children.count; i++)
      offset = MIN(offset, pop->children.items[i]->offset);
   for (int i = 0; i < pop->signals.count; i++)
      offset = MIN(offset, pop->signals.items[i]->offset);
   pop->offset = offset;

   thread->active_scope = old;

   if (pop->kind == SCOPE_PACKAGE)
      pop->parent = m->root;   // Always attach packages to root scope

   APUSH(pop->parent->children, pop);
}

bool x_driving(sig_shared_t *ss, uint32_t offset, int32_t count)
{
   rt_signal_t *s = container_of(ss, rt_signal_t, shared);
   RT_LOCK(s->lock);

   TRACE("_driving %s offset=%d count=%d",
         istr(tree_ident(s->where)), offset, count);

   int ntotal = 0, ndriving = 0;
   bool found = false;
   rt_model_t *m = get_model();
   rt_proc_t *proc = get_active_proc();
   rt_nexus_t *n = split_nexus(m, s, offset, count);
   for (; count > 0; n = n->chain) {
      if (n->n_sources > 0) {
         rt_source_t *src = find_driver(n, proc);
         if (src != NULL) {
            if (!src->disconnected) ndriving++;
            found = true;
         }
      }

      ntotal++;
      count -= n->width;
      assert(count >= 0);
   }

   if (!found)
      jit_msg(NULL, DIAG_FATAL, "process %s does not contain a driver for %s",
              istr(proc->name), istr(tree_ident(s->where)));

   return ntotal == ndriving;
}

void *x_driving_value(sig_shared_t *ss, uint32_t offset, int32_t count)
{
   rt_signal_t *s = container_of(ss, rt_signal_t, shared);
   RT_LOCK(s->lock);

   TRACE("driving value %s offset=%d count=%d", istr(tree_ident(s->where)),
         offset, count);

   rt_model_t *m = get_model();
   rt_nexus_t *n = split_nexus(m, s, offset, count);

   rt_proc_t *proc = get_active_proc();
   if (proc->wakeable.reschedule) {   // Called in output conversion
      if (n->flags & NET_F_EFFECTIVE)
         return nexus_driving(n);
      else
         return nexus_effective(n);
   }

   void *result = tlab_alloc(model_thread(m)->tlab, s->shared.size);

   uint8_t *p = result;
   for (; count > 0; n = n->chain) {
      rt_source_t *src = find_driver(n, proc);
      if (src == NULL)
         jit_msg(NULL, DIAG_FATAL, "process %s does not contain a driver "
                 "for %s", istr(proc->name), istr(tree_ident(s->where)));

      const uint8_t *driving;
      if (n->flags & NET_F_FAST_DRIVER)
         driving = nexus_effective(n);
      else
         driving = value_ptr(n, &(src->u.driver.waveforms.value));

      memcpy(p, driving, n->width * n->size);
      p += n->width * n->size;

      count -= n->width;
      assert(count >= 0);
   }

   return result;
}

void x_disconnect(sig_shared_t *ss, uint32_t offset, int32_t count,
                  int64_t after, int64_t reject)
{
   rt_signal_t *s = container_of(ss, rt_signal_t, shared);

   TRACE("_disconnect %s+%d len=%d after=%s reject=%s",
         istr(tree_ident(s->where)), offset, count, trace_time(after),
         trace_time(reject));

   rt_proc_t *proc = get_active_proc();

   check_postponed(after, proc);
   check_reject_limit(s, after, reject);

   rt_model_t *m = get_model();
   rt_nexus_t *n = split_nexus(m, s, offset, count);
   for (; count > 0; n = n->chain) {
      count -= n->width;
      assert(count >= 0);

      sched_disconnect(m, n, after, reject, proc);
   }
}

void x_force(sig_shared_t *ss, uint32_t offset, int32_t count, void *values)
{
   rt_signal_t *s = container_of(ss, rt_signal_t, shared);

   TRACE("force signal %s+%d value=%s count=%d", istr(tree_ident(s->where)),
         offset, fmt_values(values, count), count);

   rt_proc_t *proc = get_active_proc();
   rt_model_t *m = get_model();

   check_postponed(0, proc);

   force_signal(m, s, values, offset, count);
}

void x_release(sig_shared_t *ss, uint32_t offset, int32_t count)
{
   rt_signal_t *s = container_of(ss, rt_signal_t, shared);

   TRACE("release signal %s+%d count=%d", istr(tree_ident(s->where)),
         offset, count);

   rt_proc_t *proc = get_active_proc();
   rt_model_t *m = get_model();

   check_postponed(0, proc);

   release_signal(m, s, offset, count);
}

void x_deposit_signal(sig_shared_t *ss, uint32_t offset, int32_t count,
                      void *values)
{
   rt_signal_t *s = container_of(ss, rt_signal_t, shared);
   rt_model_t *m = get_model();

   if (unlikely(ss->flags & SIG_F_ISLAND)
       && rt_island_put_bytes(m, s, values, offset, count, false))
      return;

   deposit_signal(m, s, values, offset, count);
}

void x_sched_deposit(sig_shared_t *ss, uint32_t offset, int32_t count,
                     void *values, int64_t after)
{
   rt_signal_t *s = container_of(ss, rt_signal_t, shared);
   rt_model_t *m = get_model();

   if (unlikely(ss->flags & SIG_F_ISLAND))
      rt_island_on_ext_deposit(m, s);

   sched_deposit(m, s, values, offset, count, after, true);
}

void x_put_driver(sig_shared_t *ss, uint32_t offset, int32_t count,
                  void *values)
{
   rt_signal_t *s = container_of(ss, rt_signal_t, shared);

   TRACE("put driver %s+%d value=%s count=%d", istr(tree_ident(s->where)),
         offset, fmt_values(values, count * s->nexus.size), count);

   rt_proc_t *proc = get_active_proc();
   rt_model_t *m = get_model();

   if ((s->shared.flags & SIG_F_INIT_EVENT) && m->now == 0
       && m->iteration <= 0) {
      const init_event_t ev = { s, offset, count };
      APUSH(m->init_events, ev);
   }

   rt_nexus_t *n = split_nexus(m, s, offset, count);
   const char *vptr = values;
   for (; count > 0; n = n->chain) {
      count -= n->width;
      assert(count >= 0);

      rt_source_t *d = find_driver(n, proc);
      assert(d != NULL);

      // Delete any pending transactions
      for (waveform_t *it = d->u.driver.waveforms.next, *tmp = it;
           it != NULL; it = tmp) {
         tmp = it->next;
         free_value(n, it->value);
         free_waveform(m, it);
      }
      d->u.driver.waveforms.next = NULL;

      copy_value_ptr(n, &d->u.driver.waveforms.value, vptr);

      d->u.driver.waveforms.when = m->now;
      n->active_delta = m->iteration;

      calculate_driving_value(m, n);

      // The effective value is updated in the next delta cycle
      if (n->flags & NET_F_EFFECTIVE)
         m->next_is_delta = true;

      for (rt_source_t *o = n->outputs; o; o = o->chain_output) {
         switch (o->tag) {
         case SOURCE_PORT:
            defer_driving_update(m, o->u.port.output);
            m->next_is_delta = true;
            break;
         case SOURCE_ACTIVE:
            wakeup_one(m, o->u.wakeable);
            break;
         default:
            should_not_reach_here();
         }
      }

      vptr += n->size * n->width;
   }
}

sig_shared_t *x_init_packed(int64_t width, uint64_t abits, uint64_t bbits,
                            uint32_t flags, tree_t where, int32_t offset)
{
   rt_model_t *m = get_model();

   assert(width >= 1 && width <= INT32_MAX);
   const uint32_t w = (uint32_t)width;
   const int nwords = (w + 63) / 64;
   const bool is4 = !!(flags & SIG_F_PACKED4);
   const size_t snapsz = (is4 ? 2 : 1) * (size_t)nwords * sizeof(uint64_t);
   const size_t datasz = MAX(3 * snapsz, 8);

   rt_signal_t *s = static_alloc(m, sizeof(rt_signal_t) + datasz);
   setup_signal(m, s, where, 1, MIN(snapsz, 255U), flags, offset);
   s->bit_width = w;
   s->shared.size = (uint32_t)snapsz;
   if (w <= 64 && !(s->nexus.flags & (NET_F_FORCED | NET_F_EFFECTIVE)))
      s->shared.flags |= SIG_F_FAST_NOOP | SIG_F_FAST_DIRECT;

   uint64_t *eff_a = (uint64_t *)s->shared.data;
   uint64_t *drv_a = (uint64_t *)(s->shared.data + 2 * snapsz);
   const uint64_t top_mask = packed_top_mask(w);

   if (w <= 64) {
      const uint64_t na = abits & top_mask;
      eff_a[0] = drv_a[0] = na;
      if (is4) {
         uint64_t *eff_b = eff_a + 1;
         uint64_t *drv_b = drv_a + 1;
         const uint64_t nb = bbits & top_mask;
         eff_b[0] = drv_b[0] = nb;
      }
   }
   else {
      const uint64_t *sa = (const uint64_t *)(uintptr_t)abits;
      memcpy(eff_a, sa, (size_t)nwords * sizeof(uint64_t));
      eff_a[nwords - 1] &= top_mask;
      memcpy(drv_a, eff_a, (size_t)nwords * sizeof(uint64_t));
      if (is4) {
         uint64_t *eff_b = eff_a + nwords;
         uint64_t *drv_b = drv_a + nwords;
         const uint64_t *sb = (const uint64_t *)(uintptr_t)bbits;
         if (sb != NULL) {
            memcpy(eff_b, sb, (size_t)nwords * sizeof(uint64_t));
            eff_b[nwords - 1] &= top_mask;
            memcpy(drv_b, eff_b, (size_t)nwords * sizeof(uint64_t));
         }
      }
   }

   return &(s->shared);
}

void x_deposit_packed(sig_shared_t *ss, uint32_t offset, int32_t count,
                      uint64_t abits, uint64_t bbits, uint32_t src_flags)
{
   rt_signal_t *s = container_of(ss, rt_signal_t, shared);
   RT_LOCK(s->lock);
   rt_model_t *m = get_model();
   if (unlikely(ss->flags & SIG_F_ISLAND)
       && rt_island_put_packed(m, s, offset, count, abits, bbits, src_flags,
                               false))
      return;
   deposit_packed_apply(m, s, offset, count, abits, bbits,
                        src_flags, true, true);
}

void x_sched_deposit_packed(sig_shared_t *ss, uint32_t offset, int32_t count,
                            uint64_t abits, uint64_t bbits, uint32_t src_flags,
                            int64_t after)
{
   rt_signal_t *s = container_of(ss, rt_signal_t, shared);
   RT_LOCK(s->lock);
   sched_deposit_packed(get_model(), s, offset, count, abits, bbits,
                        src_flags, after, true);
}

void x_put_driver_packed(sig_shared_t *ss, uint32_t offset, int32_t count,
                         uint64_t abits, uint64_t bbits, uint32_t src_flags)
{
   if (count == 0)
      return;

   rt_signal_t *s = container_of(ss, rt_signal_t, shared);
   RT_LOCK(s->lock);

   rt_model_t *m = get_model();
   if ((ss->flags & SIG_F_INIT_EVENT) && m->now == 0 && m->iteration <= 0) {
      const init_event_t ev = { s, offset, count };
      APUSH(m->init_events, ev);
   }

   ss->flags |= SIG_F_PACKED_DRIVEN;

   const uint32_t width = s->bit_width;
   const int nwords = (width + 63) / 64;
   const bool is4 = !!(ss->flags & SIG_F_PACKED4);
   uint64_t *drv_a = (uint64_t *)(s->shared.data + 2 * s->shared.size);
   uint64_t *drv_b = is4 ? drv_a + nwords : NULL;

   const uint64_t *src_a = (src_flags & PACK_SRC_SCALAR)
      ? &abits : (const uint64_t *)(uintptr_t)abits;
   const uint64_t *src_b = (src_flags & PACK_SRC_VEC2) ? NULL
      : ((src_flags & PACK_SRC_SCALAR)
         ? &bbits : (const uint64_t *)(uintptr_t)bbits);
   packed_rmw_range(drv_a, drv_b, width, offset, count, src_a, src_b, NULL);
   drv_a[nwords - 1] &= packed_top_mask(width);
   if (drv_b != NULL)
      drv_b[nwords - 1] &= packed_top_mask(width);

   deposit_packed_apply(m, s, offset, count, abits, bbits, src_flags,
                        true, false);
}

void x_force_packed(sig_shared_t *ss, uint32_t offset, int32_t count,
                    uint64_t abits, uint64_t bbits, uint32_t src_flags)
{
   rt_signal_t *s = container_of(ss, rt_signal_t, shared);
   RT_LOCK(s->lock);

   rt_proc_t *proc = get_active_proc();
   check_postponed(0, proc);

   force_packed(get_model(), s, offset, count, abits, bbits, src_flags);
}

void x_resolve_signal(sig_shared_t *ss, ffi_closure_t *closure, int32_t nlits,
                      int32_t flags)
{
   rt_signal_t *s = container_of(ss, rt_signal_t, shared);

   TRACE("resolve signal %pi", tree_ident(s->where));

   rt_model_t *m = get_model();
   const bool replaced = s->resolution != NULL;
   s->resolution = memo_resolution_fn(m, s, closure, nlits, flags);

   // Copy R_IDENT into the nexus flags to avoid rt_resolve_nexus_fast
   // having to dereference the resolution pointer in the common case
   if (s->resolution->flags & R_IDENT) {
      s->shared.flags |= NET_F_R_IDENT;

      rt_nexus_t *n = &(s->nexus);
      for (int i = 0; i < s->n_nexus; i++, n = n->chain)
         n->flags |= NET_F_R_IDENT;
   }
   else if (replaced && (s->shared.flags & NET_F_R_IDENT)) {
      s->shared.flags &= ~NET_F_R_IDENT;

      rt_nexus_t *n = &(s->nexus);
      for (int i = 0; i < s->n_nexus; i++, n = n->chain)
         n->flags &= ~NET_F_R_IDENT;
   }
}

void x_process_init(const ffi_closure_t *closure, tree_t where)
{
   rt_model_t *m = get_model();

   TRACE("init process %pi", jit_get_name(m->jit, closure->handle));

   rt_scope_t *s = model_thread(m)->active_scope;
   assert(s != NULL);
   assert(s->kind == SCOPE_INSTANCE);

   LOCAL_TEXT_BUF tb = tb_new();
   get_path_name(s, tb);

   rt_proc_t *p = xcalloc_flex(sizeof(rt_proc_t), closure->nargs - 1,
                               sizeof(jit_scalar_t));

   p->where    = where;
   p->scope    = s;
   p->privdata = mptr_new(m->mspace, "process privdata");

   ffi_copy_closure(&p->closure, closure);

   p->wakeable.kind      = W_PROC;
   p->wakeable.pending   = false;
   p->wakeable.postponed = false;
   p->wakeable.delayed   = false;

   setup_process(m, p, tb_get(tb));

   APUSH(s->procs, p);
}

typedef struct {
   int        live;       // Children still running
   int        finished;   // Children that ran to completion
   bool       ended;      // Parent has finished with this group
   rt_proc_t *waiter;
   sv_task_t *parent;     // Task that forked the children
} sv_group_t;

typedef struct {
   rt_scope_t *scope;
   uint64_t    label;
   sv_group_t *group;
} sv_named_t;

static A(sv_named_t) sv_named_groups = AINIT;

static void sv_group_free(sv_group_t *g)
{
   for (int i = 0; i < sv_named_groups.count; i++) {
      if (sv_named_groups.items[i].group == g) {
         sv_named_groups.items[i] = sv_named_groups.items[--sv_named_groups.count];
         break;
      }
   }

   free(g);
}

struct _sv_task {
   rt_proc_t       *proc;
   struct _sv_task *parent;
   struct _sv_task *first_child;
   struct _sv_task *next_sibling;
   struct _sv_task *prev_sibling;
   sv_group_t      *group;
   int64_t          block_label;   // Named block this process executes
   int              live_children;
   rt_proc_t       *waiter;        // Blocked in wait fork
   mptr_t           frame;
   bool             done;
   unsigned         handle;        // Identifier given out by process::self
   void            *wait_event;    // Event or queue it is blocked on
   bool             killed;
   int64_t          disabled;      // Label of block disabled by a child
   uint32_t         runs;          // Times resumed since it was created
   uint32_t         dseq;          // Deferred assertion evaluations so far
   uint32_t         pending_defers; // Pending deferred assertions for this task
   struct {
      int64_t  label;
      uint32_t seq;
   }               *dis;           // Scopes disabled and when
   unsigned         ndis, maxdis;
   int64_t          marks[8];      // Blocks around the wait it is suspended in
   unsigned         nmarks;
   bool             marked;
   bool             listed;        // On the list of processes with marks
   bool             suspended;     // Stopped by process::suspend
   bool             missed;        // A wake up arrived while suspended
   rt_proc_t      **awaiters;      // Blocked in process::await
   unsigned         nawaiters;
   void          ***regs;           // Events the process is waiting on
   unsigned         nregs;
   uint64_t         rng;            // Random stream, zero if not seeded
};

static A(sv_task_t *) sv_marked_tasks = AINIT;

static bool sv_task_is_suspended(const rt_proc_t *p)
{
   return p->task != NULL && p->task->suspended;
}

static bool sv_task_has_pending_defers(const rt_proc_t *p)
{
   return p->task != NULL && p->task->pending_defers > 0;
}

static bool sv_task_hold(rt_proc_t *p)
{
   // A wake up of a process suspended by process::suspend is remembered

   sv_task_t *t = p->task;
   if (t == NULL || !t->suspended)
      return false;

   t->missed = true;
   return true;
}

static void sv_task_destroy(rt_model_t *m, rt_proc_t *p)
{
   sv_task_t *t = p->task;

   if (t->suspended) {
      t->suspended = false;
      sv_suspended_tasks--;
   }

   if (t->pending_defers > 0) {
      sv_pending_defers -= t->pending_defers;
      t->pending_defers = 0;
   }

   if (t->listed) {
      for (unsigned i = 0; i < sv_marked_tasks.count; i++) {
         if (sv_marked_tasks.items[i] == t) {
            sv_marked_tasks.items[i] =
               sv_marked_tasks.items[--sv_marked_tasks.count];
            break;
         }
      }
   }

   if (t->frame != NULL)
      mptr_free(m->mspace, &t->frame);

   free(t->awaiters);
   free(t->regs);
   free(t->dis);
   free(t);
   p->task = NULL;
}

#define SV_MAX_REGS 64

static void sv_task_note_run(rt_proc_t *p)
{
   p->task->runs++;
   p->task->marked = false;
}

static void sv_task_note_skipped_poll(rt_proc_t *p)
{
   // The process is still suspended in the same wait so the named blocks
   // around it that it recorded remain valid
   p->task->runs++;
}

static void sv_task_note_reg(rt_proc_t *p, void **pending)
{
   // A forked process which is killed must not stay on the list of a
   // signal that may never change

   sv_task_t *t = p->task;
   if (t == NULL || !p->wakeable.forked || t->nregs >= SV_MAX_REGS)
      return;

   if (t->regs == NULL)
      t->regs = xmalloc_array(SV_MAX_REGS, sizeof(void **));

   t->regs[t->nregs++] = pending;
}

static void sv_task_drop_reg(rt_proc_t *p, void **pending)
{
   sv_task_t *t = p->task;
   if (t == NULL)
      return;

   for (unsigned i = t->nregs; i > 0; i--) {
      if (t->regs[i - 1] == pending) {
         t->regs[i - 1] = t->regs[--t->nregs];
         return;
      }
   }
}

static bool sv_proc_finished(const rt_proc_t *proc)
{
   return proc->task != NULL && proc->task->done;
}

static sv_task_t *sv_task_for(rt_proc_t *proc)
{
   if (proc->task == NULL) {
      proc->task = xcalloc(sizeof(sv_task_t));
      proc->task->proc = proc;
   }

   return proc->task;
}

static sv_handle_t *sv_process_handle(void *arg);

static uint64_t sv_rng_mix(uint64_t *x)
{
   uint64_t z = (*x += UINT64_C(0x9e3779b97f4a7c15));
   z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
   z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
   return z ^ (z >> 31);
}

static uint64_t sv_rng_child(sv_task_t *parent)
{
   // A process created by a seeded process takes its seed from the stream
   // of the parent (IEEE 1800-2017 18.14.2); otherwise both use the
   // default streams

   if (parent->rng == 0)
      return 0;

   uint64_t next = sv_rng_mix(&(parent->rng));
   return next | 1;
}

static sv_task_t *sv_rng_active(void)
{
   rt_proc_t *proc = get_active_proc();
   return proc != NULL ? proc->task : NULL;
}

bool model_rng_draw(uint64_t *r)
{
   // Takes the next value of the random stream of the running process
   // if it has been seeded

   sv_task_t *t = sv_rng_active();
   if (t == NULL || t->rng == 0)
      return false;

   *r = sv_rng_mix(&(t->rng));
   return true;
}

int64_t x_sv_rng_get(void)
{
   sv_task_t *t = sv_rng_active();
   return t != NULL ? (int64_t)t->rng : 0;
}

void x_sv_rng_set(int64_t state)
{
   // Only a process which has a stream keeps the state a call leaves

   sv_task_t *t = sv_rng_active();
   if (t != NULL && t->rng != 0 && state != 0)
      t->rng = state;
}

int64_t x_sv_rng_new(void)
{
   // The seed of an object comes from the stream of the process that
   // creates it (18.14.1)

   uint64_t r;
   return model_rng_draw(&r) ? (int64_t)(r | 1) : 0;
}

void x_sv_rng_seed(void *arg, int64_t seed)
{
   sv_task_t *t = sv_process_handle(arg)->task;
   if (t != NULL) {
      uint64_t x = seed;
      t->rng = sv_rng_mix(&x) | 1;
   }
}

static uint64_t sv_rng_fresh(void)
{
   // A stream for a process that is asked for its state before it has
   // one: the same on every run

   static uint64_t counter = 0;
   if (counter == 0)
      counter = opt_verilog_seed() * UINT64_C(0x2545f4914f6cdd1d) + 1;

   return sv_rng_mix(&counter) | 1;
}

int64_t x_sv_rng_fresh(void)
{
   return (int64_t)sv_rng_fresh();
}

int64_t x_sv_rng_state(void *arg)
{
   // The state of a process which has not been seeded cannot be restored
   // so it takes a stream now

   sv_task_t *t = sv_process_handle(arg)->task;
   if (t == NULL)
      return 0;
   else if (t->rng == 0)
      t->rng = sv_rng_fresh();

   return (int64_t)t->rng;
}

void x_sv_rng_restore(void *arg, int64_t state)
{
   sv_task_t *t = sv_process_handle(arg)->task;
   if (t != NULL && state != 0)
      t->rng = state;
}

typedef struct _sv_waiter {
   struct _sv_waiter *next;
   rt_proc_t         *proc;
} sv_waiter_t;

static hash_t *sv_events = NULL;

static void sv_event_forget(rt_proc_t *proc, void *event)
{
   // Removes a process that was stopped while blocked on an event

   sv_waiter_t *head = sv_events != NULL ? hash_get(sv_events, event) : NULL;
   for (sv_waiter_t **link = &head; *link != NULL; link = &((*link)->next)) {
      sv_waiter_t *w = *link;
      if (w->proc == proc) {
         *link = w->next;
         proc_unpin(&(proc->wakeable));
         free(w);
         break;
      }
   }

   hash_put(sv_events, event, head);
}

void x_sv_queue_wait(void *event)
{
   // The caller suspends with no other wake up scheduled.  Processes
   // waiting for an event, and blocked requests of a semaphore or a
   // mailbox, are served in the order they began to wait

   rt_proc_t *proc = get_active_proc();
   assert(proc != NULL);

   if (sv_events == NULL)
      sv_events = hash_new(64);

   TRACE("process %s queues for %p", istr(proc->name), event);

   note_wait(get_model(), proc);

   sv_waiter_t *w = xmalloc(sizeof(sv_waiter_t));
   w->proc = proc;
   w->next = NULL;
   proc_pin(&(proc->wakeable));
   sv_task_for(proc)->wait_event = event;

   sv_waiter_t *head = hash_get(sv_events, event);
   if (head == NULL)
      hash_put(sv_events, event, w);
   else {
      while (head->next != NULL)
         head = head->next;
      head->next = w;
   }
}

void x_sv_event_wait(void *event)
{
   x_sv_queue_wait(event);
}

static int sv_programs = 0;   // Program initial blocks that have started

static void async_sv_program_check(rt_model_t *m, void *arg)
{
   // The simulation ends when every program has completed unless a program
   // started later in the time step

   if (sv_programs == 0 && !m->finished) {
      notef("implicit $finish from program");
      model_finish(m);
      model_stop(m);
   }
}

int64_t x_sv_program_begin(void)
{
   // Returns non-zero if the caller must suspend until the Reactive region

   rt_proc_t *proc = get_active_proc();
   if (proc == NULL)
      return 0;

   sv_programs++;

   if (proc->wakeable.reactive)
      return 0;

   proc->wakeable.reactive = true;
   procq_do(get_model(), &proc->wakeable, async_run_process, proc);
   return 1;
}

int64_t x_sv_reactive_yield(void)
{
   // The first call returns non-zero and the process runs again in the
   // Reactive region.  The next call restores its normal behaviour.

   rt_proc_t *proc = get_active_proc();
   if (proc == NULL)
      return 0;

   if (proc->wakeable.reactive) {
      proc->wakeable.reactive = false;
      return 0;
   }

   proc->wakeable.reactive = true;
   procq_do(get_model(), &proc->wakeable, async_run_process, proc);
   return 1;
}

int64_t x_sv_sva_yield(void)
{
   // As x_sv_reactive_yield but the action blocks of assertions in the
   // Reactive region run in the order the assertions appear

   rt_proc_t *proc = get_active_proc();
   if (proc != NULL && !proc->wakeable.reactive)
      proc->wait_seq = proc->create_seq;

   return x_sv_reactive_yield();
}

int64_t x_sv_sva_defer_tick(void *sva, uint64_t leaves, int disable)
{
   rt_model_t *m = get_model();
   if (likely(!m->mt_sva_enabled || m->sva_mt_skip > 0))
      return 0;

   sva_t *s = sva;
   if (unlikely(!sva_is_mt_safe(s) || m->sva_flush))
      return 0;

   rt_proc_t *proc = get_active_proc();
   if (unlikely(proc == NULL || proc->wakeable.pending
                || proc->wakeable.reactive))
      return 0;

   rt_sva_batch_item_t item = {
      .sva     = s,
      .leaves  = leaves,
      .disable = disable,
      .proc    = proc,
   };
   APUSH(m->sva_batch, item);
   return 1;
}

void x_sv_program_end(void)
{
   assert(sv_programs > 0);

   if (--sv_programs == 0) {
      rt_model_t *m = get_model();
      deferq_do(&m->postponedq, async_sv_program_check, NULL);
   }
}

static void sv_watch_add(const void *ptr, unsigned size, rt_signal_t *sig)
{
   if (ptr == NULL || size == 0) {
      sv_watch_never = true;
      return;
   }

   const bool is_sig = (sig != NULL);

   for (unsigned i = 0; i < sv_watch_pending.count; i++) {
      if (sv_watch_pending.items[i].ptr == ptr
          && sv_watch_pending.items[i].size == size) {
         if (is_sig) {
            sv_watch_pending.items[i].is_sig = true;
            sv_watch_pending.items[i].sig = sig;
            sv_watch_pending.has_sig = 1;
         }
         else
            sv_watch_pending.has_mem = 1;
         return;
      }
   }

   if (sv_watch_pending.items == NULL) {
      sv_watch_pending.items = sv_watch_pending.inline_items;
      sv_watch_pending.max = SV_WATCH_INLINE;
   }
   else if (sv_watch_pending.count == sv_watch_pending.max) {
      const unsigned new_max = sv_watch_pending.max * 2;
      if (sv_watch_pending.items == sv_watch_pending.inline_items) {
         sv_watch_item_t *arr = xmalloc_array(new_max, sizeof(sv_watch_item_t));
         memcpy(arr, sv_watch_pending.inline_items,
                sv_watch_pending.count * sizeof(sv_watch_item_t));
         sv_watch_pending.items = arr;
      }
      else {
         sv_watch_pending.items = xrealloc_array(sv_watch_pending.items,
                                                 new_max,
                                                 sizeof(sv_watch_item_t));
      }
      sv_watch_pending.max = new_max;
   }

   sv_watch_item_t *it = &sv_watch_pending.items[sv_watch_pending.count++];
   it->ptr = ptr;
   it->sig = sig;
   it->size = size;
   it->is_sig = is_sig;
   if (size <= SV_WATCH_BYTES)
      memcpy(it->snap, ptr, size);
   else {
      it->ext = xmalloc(size);
      memcpy(it->ext, ptr, size);
   }
   if (is_sig)
      sv_watch_pending.has_sig = 1;
   else
      sv_watch_pending.has_mem = 1;
}

void x_sv_poll_watch(const void *ptr, unsigned size)
{
   // The condition about to be polled reads this memory.  A null address
   // or anything which is not simple memory makes it always polled.

   sv_watch_add(ptr, size, NULL);
}

void x_sv_poll_watch_sig(sig_shared_t *ss, uint32_t offset, unsigned count)
{
   if (ss == NULL || count == 0) {
      sv_watch_never = true;
      return;
   }

   rt_signal_t *s = container_of(ss, rt_signal_t, shared);
   ss->flags = (ss->flags | SIG_F_POLL_WATCHED) & ~SIG_F_FAST_DIRECT;

   if (unlikely(ss->flags & SIG_F_ISLAND))
      rt_island_on_observer_attached(get_model(), s);

   if (ss->flags & SIG_F_PACKED) {
      sv_watch_add(ss->data, ss->size, s);
      return;
   }

   const unsigned elemsz = s->nexus.size;
   sv_watch_add(ss->data + (size_t)offset * elemsz, count * elemsz, s);
}

void x_sv_poll_wait(void)
{
   // The caller suspends with no other wake up scheduled

   rt_proc_t *proc = get_active_proc();
   assert(proc != NULL);

   TRACE("process %s polls", istr(proc->name));

   if (sv_watch_pending.count > 0 && !sv_watch_never) {
      if (proc->watch == NULL) {
         proc->watch = xcalloc(sizeof(sv_watch_t));
         proc->watch->items = proc->watch->inline_items;
         proc->watch->max = SV_WATCH_INLINE;
      }
      else
         sv_watch_clear(proc->watch);

      const unsigned n = sv_watch_pending.count;
      if (n > proc->watch->max) {
         if (proc->watch->items != proc->watch->inline_items)
            free(proc->watch->items);
         proc->watch->items = xmalloc_array(n, sizeof(sv_watch_item_t));
         proc->watch->max = n;
      }

      memcpy(proc->watch->items, sv_watch_pending.items,
             n * sizeof(sv_watch_item_t));
      proc->watch->count = n;
      proc->watch->has_sig = sv_watch_pending.has_sig;
      proc->watch->has_mem = sv_watch_pending.has_mem;

      // Ownership of any ext buffers transferred to proc->watch
      sv_watch_pending.count = 0;
      sv_watch_pending.has_sig = 0;
      sv_watch_pending.has_mem = 0;
      sv_watch_never = false;

      note_wait(get_model(), proc);
      sv_watch_poller_push(proc);
   }
   else {
      sv_watch_clear(&sv_watch_pending);
      sv_watch_never = false;

      if (proc->watch != NULL)
         sv_watch_clear(proc->watch);

      note_wait(get_model(), proc);
      APUSH(sv_pollers, proc);
   }
}

void x_sv_poll_fired(void)
{
   // A poller found its condition changed and went on to do something
   // which may change the state another poller depends on

   sv_pollers_fired++;
}

void rt_island_deposit_packed(rt_model_t *m, rt_signal_t *s, uint32_t msb_off,
                              uint32_t count, uint64_t a_val, uint64_t b_val,
                              uint32_t src_flags)
{
   deposit_packed_apply(m, s, msb_off, count, a_val, b_val, src_flags,
                        true, false);
}

void rt_island_deposit_unowned_packed(rt_model_t *m, rt_signal_t *s,
                                      uint32_t msb_off, uint32_t count,
                                      uint64_t a_val, uint64_t b_val,
                                      uint32_t src_flags)
{
   deposit_packed_apply(m, s, msb_off, count, a_val, b_val, src_flags,
                        true, true);
}

void rt_island_force_unowned_packed(rt_model_t *m, rt_signal_t *s,
                                    uint32_t msb_off, uint32_t count,
                                    uint64_t a_val, uint64_t b_val,
                                    uint32_t src_flags)
{
   force_packed_raw(m, s, msb_off, count, a_val, b_val, src_flags);
}

void rt_island_force_unowned_bytes(rt_model_t *m, rt_signal_t *s,
                                   const void *values, int offset,
                                   size_t count)
{
   force_signal_raw(m, s, values, offset, count);
}

void rt_island_request_delta(rt_model_t *m)
{
   if (m != NULL && m->can_create_delta)
      m->next_is_delta = true;
}

jit_t *rt_island_model_jit(rt_model_t *m)
{
   return m->jit;
}

rt_island_mgr_t *rt_island_model_mgr(rt_model_t *m)
{
   return m != NULL ? m->islands : NULL;
}

rt_scope_t *rt_island_model_active_scope(rt_model_t *m)
{
   return m != NULL ? model_thread(m)->active_scope : NULL;
}

void watch_set_inline_island(rt_watch_t *w, bool enable)
{
   if (w != NULL)
      w->inline_island_input = enable;
}

bool rt_model_has_init_event(const rt_model_t *m, const rt_signal_t *s)
{
   if (m == NULL || s == NULL)
      return false;

   for (int i = 0; i < m->init_events.count; i++) {
      if (m->init_events.items[i].signal == s)
         return true;
   }
   return false;
}

void *x_sv_island_begin(struct _ffi_closure *closure, uint32_t state_bytes,
                        uint32_t nsigs, uint32_t ndomains)
{
   rt_model_t *m = get_model();
   ffi_closure_t *c = (ffi_closure_t *)closure;
   jit_handle_t eval_handle = c != NULL ? c->handle : JIT_HANDLE_INVALID;
   void *inst_ctx = (c != NULL && c->nargs > 0) ? c->args[0].pointer : NULL;
   return rt_island_begin(m, &m->islands, eval_handle, inst_ctx,
                          state_bytes, nsigs, ndomains);
}

void x_sv_island_set_sparse(void *handle, uint32_t sparse_log_off,
                            uint32_t total_state_bytes)
{
   rt_island_set_sparse(handle, sparse_log_off, total_state_bytes);
}

void x_sv_island_sparse_write(void *isl_ptr, uint32_t sig_idx,
                              uint32_t byte_off, uint32_t len,
                              const void *data)
{
   rt_island_sparse_write(isl_ptr, sig_idx, byte_off, len, data);
}

void x_sv_island_bind_sig(void *handle, uint32_t idx, sig_shared_t *ss,
                          int32_t sig_offset, uint32_t a_off, uint32_t b_off,
                          uint32_t width, uint32_t nelems, uint32_t flags)
{
   rt_island_bind_sig(handle, idx, ss, sig_offset, a_off, b_off,
                      width, nelems, flags);
}

void x_sv_island_bind_gen_sig(void *handle, uint32_t idx, void *owner_block,
                              void *decl, uint32_t a_off, uint32_t b_off,
                              uint32_t width, uint32_t nelems, uint32_t flags)
{
   rt_model_t *m = get_model();
   rt_scope_t *scope = find_scope(m, (tree_t)owner_block);
   assert(scope != NULL);
   uint32_t sig_off = 0;
   rt_signal_t *sig = find_signal(scope, (tree_t)decl, &sig_off);
   assert(sig != NULL);
   rt_island_bind_sig(handle, idx, &(sig->shared), (int32_t)sig_off,
                      a_off, b_off, width, nelems, flags);
}

void x_sv_island_set_gen_key(void *handle, uint32_t idx, uint64_t key_hash)
{
   rt_island_set_gen_key(handle, idx, key_hash);
}

void x_sv_island_attach_sig(void *handle, uint32_t idx, sig_shared_t *ss,
                            int32_t sig_offset)
{
   rt_island_attach_sig(handle, idx, ss, sig_offset);
}

void x_sv_island_apply_forces(void *handle)
{
   rt_island_apply_forces(handle);
}

void x_sv_island_add_domain(void *handle, uint32_t dom_idx,
                            struct _ffi_closure *ff_closure,
                            uint32_t clk_sig_idx, int32_t rst_sig_idx,
                            uint32_t edge_flags, uint32_t n_write_sigs)
{
   ffi_closure_t *c = (ffi_closure_t *)ff_closure;
   assert(c != NULL);
   void *inst_ctx = c->nargs > 0 ? c->args[0].pointer : NULL;
   rt_island_add_domain(handle, dom_idx, c->handle, inst_ctx, clk_sig_idx,
                        rst_sig_idx, edge_flags, n_write_sigs);
}

void x_sv_island_add_domain_write(void *handle, uint32_t dom_idx,
                                  uint32_t sig_idx)
{
   rt_island_add_domain_write(handle, dom_idx, sig_idx);
}

void x_sv_island_finish(void *handle)
{
   rt_island_finish(handle);
}

void x_sv_island_set_variant_a(void *handle, struct _ffi_closure *closure,
                               struct _ffi_closure *closure_h,
                               uint32_t state_bytes, uint32_t hdr_bytes,
                               uint32_t sparse_log_off, uint32_t total_bytes)
{
   ffi_closure_t *c = (ffi_closure_t *)closure;
   ffi_closure_t *ch = (ffi_closure_t *)closure_h;
   rt_island_set_variant_a(handle, c != NULL ? c->handle : JIT_HANDLE_INVALID,
                           ch != NULL ? ch->handle : JIT_HANDLE_INVALID,
                           state_bytes, hdr_bytes, sparse_log_off,
                           total_bytes);
}

void x_sv_island_bind_sig_a(void *handle, uint32_t idx, uint32_t a_off,
                            uint32_t a_flags, uint32_t b_bytes, uint32_t x_off)
{
   rt_island_bind_sig_a(handle, idx, a_off, a_flags, b_bytes, x_off);
}

void x_sv_island_add_domain_a(void *handle, uint32_t dom_idx,
                              struct _ffi_closure *ff_closure,
                              struct _ffi_closure *ff_closure_h)
{
   ffi_closure_t *c = (ffi_closure_t *)ff_closure;
   ffi_closure_t *ch = (ffi_closure_t *)ff_closure_h;
   assert(c != NULL && ch != NULL);
   rt_island_add_domain_a(handle, dom_idx, c->handle, ch->handle);
}

void x_sv_island_mark_ext_read(sig_shared_t *ss)
{
   if (ss == NULL)
      return;
   ss->flags |= SIG_F_EXT_READ;
   if (unlikely(ss->flags & SIG_F_ISLAND))
      rt_island_on_observer_attached(get_model(),
                                     container_of(ss, rt_signal_t, shared));
}

typedef struct _sv_nba {
   struct _sv_nba  *next;
   struct _sv_nba **pprev;
   mptr_t           root;     // Keeps the target and new value alive
   size_t           bytes;
   int64_t          offset;
   int64_t          count;
   int64_t          wide;     // Size of a vector held in separate planes
   bool             trigger;  // Trigger an event rather than store
} sv_nba_t;

static sv_nba_t *sv_nba_pending = NULL;

static void async_sv_nba_store(rt_model_t *m, void *arg)
{
   sv_nba_t *n = arg;
   m->sv_poll_mem_dirty = true;

   void **block = *mptr_get(n->root);
   char *dest = block[0];
   const char *value = (const char *)(block + 1);

   if (n->trigger)
      x_sv_event_trigger(dest);
   else if (n->count < 0)
      memcpy(dest, value, n->bytes);
   else if (n->wide != 0) {
      // The planes of a vector wider than 64 bits are shared so the new
      // bits go into a copy
      const bool four = n->wide > 0;
      const int size = four ? n->wide : -n->wide;
      const size_t nwords = (size + 63) / 64;
      const int lsb = n->offset;

      uint64_t *planes[2] = { NULL, NULL };
      for (int p = 0; p < (four ? 2 : 1); p++) {
         uint64_t *old, *src;
         memcpy(&old, dest + p * sizeof(uint64_t), sizeof(uint64_t *));
         memcpy(&src, value + p * sizeof(uint64_t), sizeof(uint64_t *));

         uint64_t *copy = mspace_alloc(m->mspace, nwords * sizeof(uint64_t));
         if (old != NULL)
            memcpy(copy, old, nwords * sizeof(uint64_t));
         else
            memset(copy, 0, nwords * sizeof(uint64_t));

         for (int64_t i = 0; i < n->count; i++) {
            const uint64_t bit = src == NULL ? 0 : (src[i / 64] >> (i % 64)) & 1;
            const int at = lsb + i;
            copy[at / 64] &= ~(UINT64_C(1) << (at % 64));
            copy[at / 64] |= bit << (at % 64);
         }

         planes[p] = copy;
      }

      for (int p = 0; p < (four ? 2 : 1); p++)
         memcpy(dest + p * sizeof(uint64_t), &planes[p], sizeof(uint64_t *));
   }
   else {
      // The new bits of a part select merge with whatever other assignments
      // in this region have written.  Each word of a vector holds one plane.
      const uint64_t mask = n->count >= 64
         ? ~UINT64_C(0) : (UINT64_C(1) << n->count) - 1;

      for (size_t i = 0; i < n->bytes; i += sizeof(uint64_t)) {
         uint64_t word, bits;
         memcpy(&word, dest + i, sizeof(uint64_t));
         memcpy(&bits, value + i, sizeof(uint64_t));
         word &= ~(mask << n->offset);
         word |= (bits & mask) << n->offset;
         memcpy(dest + i, &word, sizeof(uint64_t));
      }
   }

   mptr_free(m->mspace, &n->root);

   *n->pprev = n->next;
   if (n->next != NULL)
      n->next->pprev = n->pprev;

   free(n);
}

static void sv_nba_timeout_cb(rt_model_t *m, void *arg)
{
   deferq_do(&m->nonblockq, async_sv_nba_store, arg);
}

void x_sv_nba_store(void *dest, const void *value, int64_t bytes,
                    int64_t offset, int64_t count, int64_t after,
                    int64_t wide)
{
   rt_model_t *m = get_model();

   TRACE("non-blocking store to %p bytes=%"PRIi64" offset=%"PRIi64
         " count=%"PRIi64" after=%"PRIi64, dest, bytes, offset, count, after);

   sv_nba_t *n = xmalloc(sizeof(sv_nba_t));
   n->bytes  = bytes;
   n->offset = offset;
   n->count  = count;
   n->wide   = wide;
   n->trigger = false;
   n->root   = mptr_new(m->mspace, "non-blocking store");

   void **block = mspace_alloc(m->mspace, sizeof(void *) + bytes);
   block[0] = dest;
   memcpy(block + 1, value, bytes);

   *mptr_get(n->root) = block;

   n->next  = sv_nba_pending;
   n->pprev = &sv_nba_pending;
   if (n->next != NULL)
      n->next->pprev = &n->next;
   sv_nba_pending = n;

   if (after == 0)
      deferq_do(&m->nonblockq, async_sv_nba_store, n);
   else
      model_set_timeout_cb(m, m->now + after, sv_nba_timeout_cb, n);
}

void x_sv_event_trigger_nba(void *event, int64_t after)
{
   // The event is triggered in the NBA region of this or a later time step

   rt_model_t *m = get_model();

   sv_nba_t *n = xmalloc(sizeof(sv_nba_t));
   n->bytes   = 0;
   n->offset  = 0;
   n->count   = 0;
   n->wide    = 0;
   n->trigger = true;
   n->root    = mptr_new(m->mspace, "non-blocking trigger");

   void **block = mspace_alloc(m->mspace, sizeof(void *));
   block[0] = event;
   *mptr_get(n->root) = block;

   n->next  = sv_nba_pending;
   n->pprev = &sv_nba_pending;
   if (n->next != NULL)
      n->next->pprev = &n->next;
   sv_nba_pending = n;

   if (after <= 0)
      deferq_do(&m->nonblockq, async_sv_nba_store, n);
   else
      model_set_timeout_cb(m, m->now + after, sv_nba_timeout_cb, n);
}

static void sv_poll_free(void)
{
   for (sv_nba_t *it = sv_nba_pending, *tmp; it != NULL; it = tmp) {
      tmp = it->next;
      free(it);
   }
   sv_nba_pending = NULL;

   sv_watch_clear(&sv_watch_pending);
   if (sv_watch_pending.items != NULL
       && sv_watch_pending.items != sv_watch_pending.inline_items)
      free(sv_watch_pending.items);
   sv_watch_pending.items = NULL;
   sv_watch_pending.max = 0;
   sv_watch_never = false;

   for (unsigned i = 0; i < sv_watched_sig_only.count; i++) {
      sv_watch_t *w = sv_watched_sig_only.items[i]->watch;
      if (w != NULL) {
         w->in_watched = 0;
         w->in_dirty_q = 0;
         w->has_defer = 0;
      }
   }
   for (unsigned i = 0; i < sv_watched_mem.count; i++) {
      sv_watch_t *w = sv_watched_mem.items[i].proc->watch;
      if (w != NULL) {
         w->in_watched = 0;
         w->in_dirty_q = 0;
         w->has_defer = 0;
      }
   }

   if (sv_sig_subs != NULL) {
      const void *key;
      void *val;
      for (hash_iter_t it = HASH_BEGIN; hash_iter(sv_sig_subs, &it, &key, &val); ) {
         sv_sig_subs_t *subs = val;
         free(subs->procs);
         free(subs);
      }
      hash_free(sv_sig_subs);
      sv_sig_subs = NULL;
   }

   ACLEAR(sv_pollers);
   ACLEAR(sv_watched_mem);
   ACLEAR(sv_watched_sig_only);
   ACLEAR(sv_watched_defer);
   ACLEAR(sv_sig_dirty_q);
   ACLEAR(sv_wake_buf);
   sv_watched_sig_total = 0;
   sv_sig_dirty_gen = 1;
   ACLEAR(sv_reap);
   ACLEAR(sv_handles);
   ACLEAR(stale_events);
   sv_pollers_woken = 0;
   sv_pollers_kept = 0;
   sv_pollers_fired = 0;
   sv_procs_run = 0;
   sv_suspended_tasks = 0;
   sv_pending_defers = 0;
}

int64_t x_sv_event_triggered(void *event)
{
   rt_model_t *m = get_model();
   return *(int64_t *)event == m->now + 1;
}

void x_sv_event_trigger(void *event)
{
   rt_model_t *m = get_model();

   // The state of the event until the end of this time step
   *(int64_t *)event = m->now + 1;

   if (sv_events == NULL)
      return;

   sv_waiter_t *keep = NULL, **tail = &keep;
   for (sv_waiter_t *w = hash_get(sv_events, event), *next; w; w = next) {
      next = w->next;
      if (w->proc->task->suspended) {
         // Still waiting for the next trigger once it is resumed
         w->next = NULL;
         *tail = w;
         tail = &(w->next);
         continue;
      }

      TRACE("event %p wakes process %s", event, istr(w->proc->name));
      deltaq_insert_proc(m, 0, w->proc);
      w->proc->task->wait_event = NULL;
      proc_unpin(&(w->proc->wakeable));
      free(w);
   }

   hash_put(sv_events, event, keep);
}

static void sv_group_child_gone(rt_model_t *m, sv_group_t *g, bool completed)
{
   g->live--;
   if (completed)
      g->finished++;

   if (g->waiter != NULL) {
      rt_proc_t *waiter = g->waiter;
      g->waiter = NULL;
      if (!waiter->wakeable.zombie)
         deltaq_insert_proc(m, 0, waiter);
   }
   else if (g->ended && g->live == 0)
      sv_group_free(g);
}

static void sv_task_finish(rt_model_t *m, sv_task_t *t, bool completed)
{
   if (t->done)
      return;

   t->done = true;

   if (t->proc->wakeable.forked) {
      t->proc->wakeable.zombie = true;
      APUSH(sv_reap, t->proc);
   }

   if (t->frame != NULL)
      mptr_free(m->mspace, &t->frame);

   for (unsigned i = 0; i < t->nawaiters; i++) {
      if (!t->awaiters[i]->wakeable.zombie)
         deltaq_insert_proc(m, 0, t->awaiters[i]);
      proc_unpin(&(t->awaiters[i]->wakeable));
   }

   free(t->awaiters);
   t->awaiters = NULL;
   t->nawaiters = 0;

   if (t->group != NULL)
      sv_group_child_gone(m, t->group, completed);

   sv_task_t *p = t->parent;
   if (p != NULL) {
      p->live_children--;
      if (p->live_children == 0 && p->waiter != NULL) {
         rt_proc_t *waiter = p->waiter;
         p->waiter = NULL;
         if (!waiter->wakeable.zombie)
            deltaq_insert_proc(m, 0, waiter);
      }
   }
}

static void sv_task_kill_tree(rt_model_t *m, sv_task_t *t);

static void sv_task_kill(rt_model_t *m, sv_task_t *t)
{
   // The process may still be scheduled or waiting on a trigger so it is
   // marked to do nothing when it next runs

   t->proc->wakeable.zombie = true;
   t->killed = true;
   if (t->suspended) {
      t->suspended = false;
      sv_suspended_tasks--;
   }

   dpi_proc_killed(t->proc);

   if (t->handle != 0)
      sv_handles.items[t->handle - 1].killed = true;

   sv_poller_remove(t->proc);

   if (t->wait_event != NULL) {
      sv_event_forget(t->proc, t->wait_event);
      t->wait_event = NULL;
   }

   while (t->nregs > 0) {
      const unsigned before = t->nregs;
      clear_event(m, t->regs[before - 1], &(t->proc->wakeable));
      if (t->nregs == before)
         t->nregs--;
   }

   if (t->proc->tlab != NULL && t->proc != get_active_proc()) {
      tlab_release(m->jit, t->proc->tlab);
      t->proc->tlab = NULL;
   }

   for (sv_task_t *c = t->first_child; c != NULL; c = c->next_sibling)
      sv_task_kill_tree(m, c);

   sv_task_finish(m, t, false);
}

static void sv_task_kill_tree(rt_model_t *m, sv_task_t *t)
{
   // A task which has finished may still have running descendants

   if (!t->done)
      sv_task_kill(m, t);
   else {
      for (sv_task_t *c = t->first_child; c != NULL; c = c->next_sibling)
         sv_task_kill_tree(m, c);
   }
}

static void sv_proc_free(rt_model_t *m, rt_proc_t *p)
{
   sv_task_t *t = p->task;

   if (t->handle != 0)
      sv_handles.items[t->handle - 1].task = NULL;

   if (t->prev_sibling != NULL)
      t->prev_sibling->next_sibling = t->next_sibling;
   else
      t->parent->first_child = t->next_sibling;

   if (t->next_sibling != NULL)
      t->next_sibling->prev_sibling = t->prev_sibling;

   rt_scope_t *s = p->scope;
   assert(s->procs.items[p->scope_idx] == p);
   rt_proc_t *last = s->procs.items[--s->procs.count];
   s->procs.items[p->scope_idx] = last;
   last->scope_idx = p->scope_idx;

   mptr_free(m->mspace, &(p->privdata));
   tlab_release(m->jit, p->tlab);
   sv_task_destroy(m, p);
   sv_watch_free(p->watch);
   free(p);
}

static void sv_reap_procs(rt_model_t *m)
{
   // Frees the forked processes that nothing refers to any longer

   assert(get_active_proc() == NULL);

   unsigned keep = 0;
   for (unsigned i = 0; i < sv_reap.count; i++) {
      rt_proc_t *p = sv_reap.items[i];
      const sv_task_t *t = p->task;

      if (p->regs == PROC_REGS_MAX || t->parent == NULL)
         continue;   // Never freed
      else if (p->wakeable.pending || p->wakeable.delayed
               || t->first_child != NULL || p->regs > 0)
         sv_reap.items[keep++] = p;
      else
         sv_proc_free(m, p);
   }

   sv_reap.count = keep;
   sv_reap_next = MAX(256, keep * 2);
}

static void async_spawn_process(rt_model_t *m, void *arg)
{
   sv_task_t *task = arg;
   rt_proc_t *proc = task->proc;

   assert(proc->wakeable.pending);
   proc->wakeable.pending = false;

   if (proc->wakeable.zombie)
      return;   // Disabled before it could start

   model_thread_t *thread = model_thread(m);
   tlab_t *saved = thread->tlab;
   thread->tlab = NULL;

   reset_process_state(m, proc);

   thread->tlab = saved;

   // Runs until it first blocks before any other process
   run_process(m, proc);
   m->next_is_delta = true;

   // The state of the process now keeps the frame alive
   mptr_free(m->mspace, &task->frame);
}

void *x_sv_fork_begin(void)
{
   sv_group_t *g = xcalloc(sizeof(sv_group_t));
   rt_proc_t *proc = get_active_proc();
   g->parent = proc != NULL ? sv_task_for(proc) : NULL;
   return g;
}

void x_sv_fork_name(void *arg, int64_t label)
{
   // Allows a fork to be disabled by name from any process in the scope

   rt_proc_t *proc = get_active_proc();
   assert(proc != NULL);

   const sv_named_t n = { proc->scope, label, arg };
   for (int i = 0; i < sv_named_groups.count; i++) {
      if (sv_named_groups.items[i].scope == n.scope
          && sv_named_groups.items[i].label == label) {
         sv_named_groups.items[i] = n;
         return;
      }
   }

   APUSH(sv_named_groups, n);
}

static void sv_task_interrupt(rt_model_t *m, sv_task_t *t);
static bool sv_task_in_block(const sv_task_t *t, int64_t label);
static void sv_task_defer_disable(sv_task_t *t, int64_t label);

int64_t x_sv_fork_disable_name(int64_t label)
{
   // Returns non-zero if the caller has been stopped

   rt_proc_t *proc = get_active_proc();
   assert(proc != NULL);

   for (int i = 0; i < sv_named_groups.count; i++) {
      const sv_named_t *n = &(sv_named_groups.items[i]);
      if (n->scope != proc->scope || n->label != label)
         continue;

      rt_model_t *m = get_model();
      sv_group_t *g = n->group;
      sv_task_t *parent = g->parent;
      for (sv_task_t *c = parent->first_child; c != NULL;
           c = c->next_sibling) {
         if (c->group == g && !c->done)
            sv_task_kill(m, c);
      }

      return sv_task_for(proc)->killed;
   }

   // A named block executed by a forked process
   rt_model_t *m = get_model();
   for (int i = proc->scope->procs.count - 1; i >= 0; i--) {
      rt_proc_t *p = proc->scope->procs.items[i];
      if (p == proc || !p->wakeable.forked || p->task == NULL)
         continue;

      sv_task_t *t = p->task;
      if (t->block_label == label && !t->done && !t->killed)
         sv_task_kill_tree(m, t);
   }

   // A named block of another process of the same scope which is
   // suspended inside it (9.6.2)
   sv_task_t *self = sv_task_for(proc);
   for (unsigned i = 0; i < sv_marked_tasks.count; i++) {
      sv_task_t *t = sv_marked_tasks.items[i];
      if (t == self || t->proc->scope != proc->scope || t->done
          || t->killed || !sv_task_in_block(t, label))
         continue;

      t->disabled = label;
      sv_task_defer_disable(t, label);
      sv_task_interrupt(get_model(), t);
   }

   return self->killed;
}

void x_sv_fork_end(void *arg)
{
   sv_group_t *g = arg;
   g->ended = true;
   if (g->live == 0)
      sv_group_free(g);
}

int64_t x_sv_fork_wait(void *arg, int64_t mode)
{
   // Returns non-zero if the caller must suspend

   rt_proc_t *proc = get_active_proc();
   assert(proc != NULL);

   if (mode == 2) {
      sv_task_t *t = sv_task_for(proc);
      if (t->live_children == 0)
         return 0;

      t->waiter = proc;
      return 1;
   }

   sv_group_t *g = arg;
   if (g->live == 0 || (mode == 1 && g->finished > 0))
      return 0;

   g->waiter = proc;
   return 1;
}

void x_sv_fork_finish(void)
{
   // Called at the end of a forked statement or an initial process

   rt_proc_t *proc = get_active_proc();
   assert(proc != NULL);

   if (!proc->wakeable.forked && proc->task == NULL)
      return;

   sv_task_finish(get_model(), sv_task_for(proc), true);
}

void x_sv_fork_disable(void)
{
   rt_proc_t *proc = get_active_proc();
   assert(proc != NULL);

   rt_model_t *m = get_model();
   sv_task_t *t = sv_task_for(proc);
   for (sv_task_t *c = t->first_child; c != NULL; c = c->next_sibling)
      sv_task_kill_tree(m, c);
}

static void sv_task_interrupt(rt_model_t *m, sv_task_t *t)
{
   // Wakes a process suspended in a wait statement so it can unwind

   rt_proc_t *proc = t->proc;
   if (proc->wakeable.pending) {
      // Already scheduled to run, perhaps to poll its wait which would
      // otherwise be skipped as nothing it reads has changed
      proc->wakeable.interrupted = true;
      return;
   }

   if (proc->wakeable.delayed) {
      proc->wakeable.delayed = false;
      const stale_event_t stale = { proc, proc->wake_at };
      APUSH(stale_events, stale);
   }

   sv_poller_remove(proc);

   if (sv_events != NULL) {
      const void *key;
      void *value;
      for (hash_iter_t it = HASH_BEGIN; hash_iter(sv_events, &it, &key, &value); ) {
         sv_waiter_t **link = (sv_waiter_t **)&value;
         while (*link != NULL) {
            sv_waiter_t *w = *link;
            if (w->proc == proc) {
               *link = w->next;
               t->wait_event = NULL;
               proc_unpin(&(proc->wakeable));
               free(w);
            }
            else
               link = &(w->next);
         }

         if (value != hash_get(sv_events, key))
            hash_put(sv_events, key, value);
      }
   }

   proc->wakeable.interrupted = true;
   deltaq_insert_proc(m, 0, proc);
}

void x_sv_wait_mark(int64_t label, int64_t first)
{
   // Records the named blocks around the wait the caller suspends in

   rt_proc_t *proc = get_active_proc();
   assert(proc != NULL);

   sv_task_t *t = sv_task_for(proc);
   if (first)
      t->nmarks = 0;

   if (t->nmarks < ARRAY_LEN(t->marks))
      t->marks[t->nmarks++] = label;

   t->marked = true;

   if (!t->listed) {
      t->listed = true;
      APUSH(sv_marked_tasks, t);
   }
}

static bool sv_task_in_block(const sv_task_t *t, int64_t label)
{
   if (!t->marked)
      return false;

   for (unsigned i = 0; i < t->nmarks; i++) {
      if (t->marks[i] == label)
         return true;
   }

   return false;
}

int64_t x_sv_fork_disable_group(int64_t depth, int64_t label)
{
   // Stops every process forked by the same fork as the caller or, for
   // a larger depth, as one of its ancestors

   rt_proc_t *proc = get_active_proc();
   assert(proc != NULL);

   rt_model_t *m = get_model();
   sv_task_t *t = sv_task_for(proc);
   for (int64_t i = 1; i < depth && t->parent != NULL; i++)
      t = t->parent;

   sv_group_t *g = t->group;
   sv_task_t *parent = t->parent;
   if (g == NULL || parent == NULL)
      return 0;

   // The parent only unwinds if it is still waiting inside the block
   const bool joining = parent->proc == g->waiter;
   const bool interrupt = label != 0 && !joining && sv_task_in_block(parent, label);

   if (label != 0 && (joining || interrupt))
      parent->disabled = label;

   for (sv_task_t *c = parent->first_child; c != NULL; c = c->next_sibling) {
      if (c->group == g)
         sv_task_kill_tree(m, c);
   }

   if (interrupt)
      sv_task_interrupt(m, parent);

   return 1;
}

int64_t x_sv_fork_disabled(void)
{
   // Label of the block which a forked process disabled, or zero

   rt_proc_t *proc = get_active_proc();
   assert(proc != NULL);

   sv_task_t *t = sv_task_for(proc);
   const int64_t label = t->disabled;
   t->disabled = 0;
   t->marked = false;
   return label;
}

rt_proc_t *model_defer_note(uint32_t *runs, uint32_t *seq)
{
   // The process which evaluates a deferred assertion, kept until its
   // report matures

   rt_proc_t *proc = get_active_proc();
   if (proc == NULL) {
      *runs = *seq = 0;
      return NULL;
   }

   sv_task_t *t = sv_task_for(proc);
   *runs = t->runs;
   *seq = ++t->dseq;
   t->pending_defers++;
   sv_pending_defers++;
   proc_pin(&(proc->wakeable));
   return proc;
}

bool model_defer_live(rt_proc_t *proc, uint32_t runs, uint32_t seq,
                      const int64_t *scopes, unsigned nscopes)
{
   // A pending deferred assertion report is discarded when its process
   // has resumed or been killed since the evaluation, or when a block
   // that contains it or its label has been disabled (16.4.4)

   if (proc == NULL)
      return true;

   sv_task_t *t = proc->task;
   if (t != NULL && t->pending_defers > 0) {
      t->pending_defers--;
      sv_pending_defers--;
   }
   bool live = t != NULL && t->runs == runs && !t->killed;
   for (unsigned i = 0; live && i < t->ndis; i++) {
      if (t->dis[i].seq < seq)
         continue;

      for (unsigned j = 0; j < nscopes; j++) {
         if (scopes[j] == t->dis[i].label)
            live = false;
      }
   }

   proc_unpin(&(proc->wakeable));
   return live;
}

static void sv_task_defer_disable(sv_task_t *t, int64_t label)
{
   // Reports pending in the block with this name are cancelled

   const int64_t folded = label & 0x7fffffff;
   for (unsigned i = 0; i < t->ndis; i++) {
      if (t->dis[i].label == folded) {
         t->dis[i].seq = t->dseq;
         return;
      }
   }

   if (t->ndis == t->maxdis) {
      t->maxdis = MAX(t->maxdis * 2, 4);
      t->dis = xrealloc_array(t->dis, t->maxdis, sizeof(t->dis[0]));
   }

   t->dis[t->ndis].label = folded;
   t->dis[t->ndis].seq = t->dseq;
   t->ndis++;
}

int64_t x_sv_defer_disable(int64_t label)
{
   rt_proc_t *proc = get_active_proc();
   if (proc != NULL)
      sv_task_defer_disable(sv_task_for(proc), label);

   return 0;
}

void *x_sv_process_self(void)
{
   // There is no process while static variables are being initialised
   rt_proc_t *proc = get_active_proc();
   if (proc == NULL)
      return NULL;

   sv_task_t *t = sv_task_for(proc);
   if (t->handle == 0) {
      sv_handle_t h = { .task = t };
      APUSH(sv_handles, h);
      t->handle = sv_handles.count;
   }

   return (void *)(uintptr_t)t->handle;
}

static sv_handle_t *sv_process_handle(void *arg)
{
   const uintptr_t id = (uintptr_t)arg;
   if (id == 0)
      jit_msg(NULL, DIAG_FATAL, "null process handle");

   assert(id <= sv_handles.count);
   return &(sv_handles.items[id - 1]);
}

int64_t x_sv_process_status(void *arg)
{
   // Matches the values of process::state

   sv_handle_t *h = sv_process_handle(arg);
   sv_task_t *t = h->task;

   if (h->killed)
      return 4;
   else if (t == NULL || t->done)
      return 0;
   else if (t->suspended)
      return 3;
   else if (t->proc == get_active_proc())
      return 1;
   else
      return 2;
}

int64_t x_sv_process_suspend(void *arg)
{
   // Returns non-zero if the caller suspended itself and must stop

   sv_task_t *t = sv_process_handle(arg)->task;
   if (t == NULL || t->done || t->suspended)
      return 0;

   t->suspended = true;
   sv_suspended_tasks++;
   get_model()->sv_poll_susp_dirty = true;

   if (t->proc == get_active_proc()) {
      t->missed = true;
      return 1;
   }

   return 0;
}

int64_t x_sv_process_resume(void *arg)
{
   sv_task_t *t = sv_process_handle(arg)->task;
   if (t == NULL || t->done || !t->suspended)
      return 0;

   t->suspended = false;
   sv_suspended_tasks--;

   if (t->missed) {
      t->missed = false;
      deltaq_insert_proc(get_model(), 0, t->proc);
   }

   return 0;
}

int64_t x_sv_process_kill(void *arg)
{
   // Returns non-zero if the caller has killed itself and must stop

   sv_task_t *t = sv_process_handle(arg)->task;
   if (t == NULL)
      return 0;

   rt_model_t *m = get_model();
   if (t->done) {
      // IEEE 1800-2023 9.7: killing a finished process still kills live
      // descendants without changing its own FINISHED status
      sv_task_kill_tree(m, t);
      return 0;
   }

   // The process may be one that was not forked
   t->proc->wakeable.forked = true;

   sv_task_kill(m, t);

   return t->proc == get_active_proc();
}

int64_t x_sv_process_await(void *arg)
{
   // Returns non-zero if the caller must suspend

   sv_task_t *t = sv_process_handle(arg)->task;
   if (t == NULL || t->done)
      return 0;

   rt_proc_t *proc = get_active_proc();
   assert(proc != NULL);

   if (t->proc == proc) {
      jit_msg(NULL, DIAG_FATAL, "a process cannot await itself");
      return 0;
   }

   for (unsigned i = 0; i < t->nawaiters; i++) {
      if (t->awaiters[i] == proc)
         return 1;   // Woken for some other reason
   }

   t->awaiters = xrealloc_array(t->awaiters, t->nawaiters + 1,
                                sizeof(rt_proc_t *));
   t->awaiters[t->nawaiters++] = proc;
   proc_pin(&(proc->wakeable));
   return 1;
}

void x_sv_spawn(const ffi_closure_t *closure, void *group, int64_t label)
{
   rt_model_t *m = get_model();

   if (get_active_proc() == NULL) {
      // 13.4.4: the error may be given at compile time or run time
      jit_msg(NULL, DIAG_ERROR, "fork-join_none in a function is only "
              "allowed in a process created by an initial or always "
              "procedure, not during initialisation of a variable");
      return;
   }

   rt_proc_t *parent = get_active_proc();

   TRACE("spawn process %pi", jit_get_name(m->jit, closure->handle));

   rt_scope_t *s = parent->scope;

   LOCAL_TEXT_BUF tb = tb_new();
   get_path_name(s, tb);

   rt_proc_t *p = xcalloc_flex(sizeof(rt_proc_t), closure->nargs - 1,
                               sizeof(jit_scalar_t));

   p->where    = parent->where;
   p->scope    = s;
   p->privdata = mptr_new(m->mspace, "process privdata");

   ffi_copy_closure(&p->closure, closure);

   p->wakeable.kind      = W_PROC;
   p->wakeable.pending   = false;
   p->wakeable.postponed = false;
   p->wakeable.delayed   = false;
   p->wakeable.forked    = true;
   p->wakeable.reactive  = parent->wakeable.reactive;

   setup_process(m, p, tb_get(tb));

   p->scope_idx = s->procs.count;
   APUSH(s->procs, p);

   sv_task_t *pt = sv_task_for(parent);
   sv_task_t *task = sv_task_for(p);
   task->parent = pt;
   task->group = group;
   task->block_label = label;
   task->rng = sv_rng_child(pt);
   task->next_sibling = pt->first_child;
   task->prev_sibling = NULL;
   if (pt->first_child != NULL)
      pt->first_child->prev_sibling = task;
   pt->first_child = task;
   pt->live_children++;

   sv_group_t *g = group;
   g->live++;

   // Only the process state is a root for the garbage collector and it
   // has not been created yet so keep the captured frame alive until then
   task->frame = mptr_new(m->mspace, "fork frame");
   *mptr_get(task->frame) = closure->args[0].pointer;

   set_pending(&p->wakeable);
   deferq_do(&m->spawnq, async_spawn_process, task);
   m->next_is_delta = true;
}

void *x_function_trigger(jit_handle_t handle, unsigned nargs,
                         const jit_scalar_t *args)
{
   rt_model_t *m = get_model();

   uint64_t hash = mix_bits_32(handle);
   for (int i = 0; i < nargs; i++)
      hash ^= mix_bits_64(args[i].integer);

   TRACE("function trigger %s nargs=%u hash=%"PRIx64,
         istr(jit_get_name(m->jit, handle)), nargs, hash);

   return new_trigger(m, FUNC_TRIGGER, hash, handle, nargs, args);
}

rt_trigger_t *x_or_trigger(rt_trigger_t *left, rt_trigger_t *right)
{
   rt_model_t *m = get_model();

   uint64_t hash = mix_bits_64(left) ^ mix_bits_64(right);

   TRACE("or trigger %p %p hash=%"PRIx64, left, right, hash);

   const jit_scalar_t args[] = {
      { .pointer = left < right ? left : right },
      { .pointer = left < right ? right : left }
   };

   return new_trigger(m, OR_TRIGGER, hash, JIT_HANDLE_INVALID, 2, args);
}

void *x_cmp_trigger(sig_shared_t *ss, uint32_t offset, int64_t right)
{
   ss->flags &= ~SIG_F_FAST_DIRECT;
   rt_model_t *m = get_model();
   rt_signal_t *s = container_of(ss, rt_signal_t, shared);

   uint64_t hash = mix_bits_64(s) ^ mix_bits_32(offset) ^ mix_bits_64(right);

   TRACE("cmp trigger %s+%d right=%"PRIi64" hash=%"PRIx64,
         istr(tree_ident(s->where)), offset, right, hash);

   const jit_scalar_t args[] = {
      { .pointer = s },
      { .integer = offset },
      { .integer = right }
   };

   return new_trigger(m, CMP_TRIGGER, hash, JIT_HANDLE_INVALID, 3, args);
}

void *x_level_trigger(sig_shared_t *ss, uint32_t offset, int32_t count)
{
   ss->flags &= ~SIG_F_FAST_DIRECT;
   rt_model_t *m = get_model();
   rt_signal_t *s = container_of(ss, rt_signal_t, shared);

   uint64_t hash = mix_bits_64(s) ^ mix_bits_32(offset) ^ mix_bits_32(count);

   TRACE("level trigger %s+%d count=%d hash=%"PRIx64,
         istr(tree_ident(s->where)), offset, count, hash);

   const jit_scalar_t args[] = {
      { .pointer = s },
      { .integer = offset },
      { .integer = count }
   };

   return new_trigger(m, LEVEL_TRIGGER, hash, JIT_HANDLE_INVALID, 3, args);
}

void x_add_trigger(void *ptr)
{
   TRACE("add trigger %p", ptr);

   rt_wakeable_t *obj = get_active_wakeable();
   assert(obj->trigger == NULL);

   obj->trigger = ptr;
}

void x_instance_name(attr_kind_t kind, text_buf_t *tb)
{
   rt_model_t *m = get_model();
   rt_scope_t *s = get_active_scope(m);

   switch (kind) {
   case ATTR_INSTANCE_NAME:
      get_instance_name(s, tb);
      break;
   case ATTR_PATH_NAME:
      get_path_name(s, tb);
      break;
   default:
      should_not_reach_here();
   }
}

int32_t *x_get_counters(jit_handle_t handle)
{
   rt_model_t *m = get_model();
   ident_t name = jit_get_name(m->jit, handle);

   return cover_get_counters(m->cover, name);
}

void x_timing_check(int64_t stamp, int64_t limit, object_t *where)
{
   diag_t *d = diag_new(DIAG_ERROR, &(where->loc));

   vlog_node_t v = vlog_from_object(where);
   assert(v != NULL);
   assert(vlog_kind(v) == V_TCHECK);

   static const char *names[] = {
      [V_TCHECK_SETUP]    = "$setup",
      [V_TCHECK_HOLD]     = "$hold",
      [V_TCHECK_RECOVERY] = "$recovery",
      [V_TCHECK_REMOVAL]  = "$removal",
   };

   rt_model_t *m = get_model();

   LOCAL_TEXT_BUF tb = tb_new();
   get_instance_name(get_active_scope(m), tb);

   char stamp_buf[32], now_buf[32], limit_buf[32];
   fmt_time_r(stamp_buf, sizeof(stamp_buf), stamp, "");
   fmt_time_r(now_buf, sizeof(now_buf), model_now(m, NULL), "");
   fmt_time_r(limit_buf, sizeof(limit_buf), limit, "");

   diag_printf(d, "timing check violation: %s(%s, %s, %s)",
               names[vlog_subkind(v)], stamp_buf, now_buf, limit_buf);
   diag_hint(d, NULL, "instance '%s'", tb_get(tb));

   emit_vhdl_diag(d, SEVERITY_ERROR);
}
