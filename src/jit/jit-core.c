//
//  Copyright (C) 2022-2025  Nick Gasson
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
#include "diag.h"
#include "hash.h"
#include "jit/jit-priv.h"
#include "jit/jit.h"
#include "lib.h"
#include "lower.h"
#include "mir/mir-unit.h"
#include "object.h"
#include "option.h"
#include "rt/model.h"
#include "rt/mspace.h"
#include "rt/structs.h"
#include "thread.h"
#include "tree.h"
#include "type.h"
#include "vcode.h"
#include "vlog/vlog-node.h"

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __MINGW32__
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef CRITICAL_SECTION   jit_mutex_t;
typedef CONDITION_VARIABLE jit_cond_t;
static inline void jit_mutex_init(jit_mutex_t *m)    { InitializeCriticalSection(m); }
static inline void jit_mutex_init_recursive(jit_mutex_t *m) { InitializeCriticalSection(m); }
static inline void jit_mutex_destroy(jit_mutex_t *m) { DeleteCriticalSection(m); }
static inline void jit_mutex_lock(jit_mutex_t *m)    { EnterCriticalSection(m); }
static inline void jit_mutex_unlock(jit_mutex_t *m)  { LeaveCriticalSection(m); }
static inline void jit_cond_init(jit_cond_t *c)      { InitializeConditionVariable(c); }
static inline void jit_cond_destroy(jit_cond_t *c)   { (void)c; }
static inline void jit_cond_signal(jit_cond_t *c)    { WakeConditionVariable(c); }
static inline void jit_cond_broadcast(jit_cond_t *c) { WakeAllConditionVariable(c); }
static inline void jit_cond_wait(jit_cond_t *c, jit_mutex_t *m) {
   SleepConditionVariableCS(c, m, INFINITE);
}
#else
#include <pthread.h>
typedef pthread_mutex_t jit_mutex_t;
typedef pthread_cond_t  jit_cond_t;
static inline void jit_mutex_init(jit_mutex_t *m)    { pthread_mutex_init(m, NULL); }
static inline void jit_mutex_init_recursive(jit_mutex_t *m)
{
   pthread_mutexattr_t attr;
   pthread_mutexattr_init(&attr);
   pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
   pthread_mutex_init(m, &attr);
   pthread_mutexattr_destroy(&attr);
}
static inline void jit_mutex_destroy(jit_mutex_t *m) { pthread_mutex_destroy(m); }
static inline void jit_mutex_lock(jit_mutex_t *m)    { pthread_mutex_lock(m); }
static inline void jit_mutex_unlock(jit_mutex_t *m)  { pthread_mutex_unlock(m); }
static inline void jit_cond_init(jit_cond_t *c)      { pthread_cond_init(c, NULL); }
static inline void jit_cond_destroy(jit_cond_t *c)   { pthread_cond_destroy(c); }
static inline void jit_cond_signal(jit_cond_t *c)    { pthread_cond_signal(c); }
static inline void jit_cond_broadcast(jit_cond_t *c) { pthread_cond_broadcast(c); }
static inline void jit_cond_wait(jit_cond_t *c, jit_mutex_t *m) {
   pthread_cond_wait(c, m);
}
#endif

#define FUNC_HASH_SZ         1024
#define FUNC_LIST_SZ         512
#define COMPILE_TIMEOUT      10000
#define JIT_POOL_MAX_WORKERS 4

typedef struct _linked_tlab linked_tlab_t;
typedef struct _jit_pool    jit_pool_t;

typedef struct {
   jit_func_t *func;
   jit_tier_t *tier;
} jit_job_t;

struct _jit_pool {
   jit_t         *jit;
   jit_pool_t    *next;
   unsigned       nworkers;
   nvc_thread_t **workers;
   jit_job_t     *queue;
   unsigned       head;
   unsigned       tail;
   unsigned       count;
   unsigned       cap;
   unsigned       in_flight;
   bool           stop;
   jit_mutex_t    mtx;
   jit_cond_t     cv;
};

typedef struct _jit_tier {
   jit_tier_t    *next;
   int            threshold;
   jit_plugin_t   plugin;
   void          *context;
} jit_tier_t;

typedef struct {
   size_t      length;
   jit_func_t *items[0];
} func_array_t;

typedef struct _linked_tlab {
   linked_tlab_t *next;
   linked_tlab_t *prev;
   tlab_t         tlab;
} linked_tlab_t;

typedef struct _jit {
   chash_t          *index;
   mspace_t         *mspace;
   void             *lower_ctx;
   bool              silent;
   unsigned          suppressed;
   bool              shutdown;
   int               exit_status;
   jit_tier_t       *tiers;
   func_array_t     *funcs;
   unsigned          next_handle;
   nvc_lock_t        lock;
   jit_mutex_t       mir_lock;
   jit_irq_fn_t      interrupt;
   void             *interrupt_ctx;
   unit_registry_t  *registry;
   mir_context_t    *mir;
   linked_tlab_t    *live_tlabs;
   linked_tlab_t    *free_tlabs;
   jit_pool_t       *pool;
} jit_t;

static void jit_transition(jit_thread_local_t *thread, jit_t *j,
                           jit_state_t from, jit_state_t to);
static void jit_diag_cb(diag_t *d, void *arg);
static void jit_pool_destroy(jit_t *j);

static void jit_oom_cb(mspace_t *m, size_t size, void *ctx)
{
   diag_t *d = diag_new(DIAG_FATAL, NULL);
   diag_printf(d, "out of memory attempting to allocate %zu byte object", size);

   const size_t heapsize = opt_get_size(OPT_HEAP_SIZE);
   diag_hint(d, NULL, "the current heap size is %zu bytes which you can "
             "increase with the $bold$-H$$ option, for example $bold$-H %zum$$",
             heapsize, MAX(1, (heapsize * 2) / 1024 / 1024));

   diag_emit(d);
   jit_abort_with_status(EXIT_FAILURE);
}

static void jit_mark_cb(mspace_t *m, void *cookie, void *ctx)
{
   jit_t *j = ctx;

   for (linked_tlab_t *lt = j->live_tlabs; lt; lt = lt->next)
      mspace_user_mark(m, cookie, lt->tlab.data, lt->tlab.alloc);
}

__attribute__((always_inline))
static inline jit_thread_local_t **jit_thread_local_ptr(void)
{
   static __thread jit_thread_local_t *local = NULL;
   return &local;
}

__attribute__((cold, noinline))
static jit_thread_local_t *jit_alloc_thread_local(void)
{
   jit_thread_local_t *thread = xcalloc(sizeof(jit_thread_local_t));
   thread->state = JIT_IDLE;
   return thread;
}

jit_thread_local_t *jit_thread_local(void)
{
   jit_thread_local_t **ptr = jit_thread_local_ptr();

   if (unlikely(*ptr == NULL))
      *ptr = jit_alloc_thread_local();

   return *ptr;
}

void jit_init_thread(void)
{
   (void)jit_thread_local();
}

jit_t *jit_new(unit_registry_t *ur, mir_context_t *mc)
{
   jit_t *j = xcalloc(sizeof(jit_t));
   j->registry    = ur;
   j->index       = chash_new(FUNC_HASH_SZ);
   j->exit_status = INT_MIN;
   j->mir         = mc;
   jit_mutex_init_recursive(&j->mir_lock);

   const mspace_handler_t mspace_handler = {
      .oom     = jit_oom_cb,
      .mark    = jit_mark_cb,
      .context = j,
   };

   j->mspace = mspace_new(opt_get_size(OPT_HEAP_SIZE), &mspace_handler);

   j->funcs = xcalloc_flex(sizeof(func_array_t),
                           FUNC_LIST_SZ, sizeof(jit_func_t *));
   j->funcs->length = FUNC_LIST_SZ;

   // Ensure we can resolve symbols from the executable
   ffi_load_dll(NULL);

   diag_add_hint_fn(jit_diag_cb, j);

   return j;
}

static void jit_free_func(jit_func_t *f)
{
   mptr_free(f->jit->mspace, &(f->privdata));
   free(f->irbuf);
   free(f->linktab);
   free(f->cpool);
   free(f->relocs);
   free(f);
}

void jit_free(jit_t *j)
{
   store_release(&j->shutdown, true);
   jit_pool_destroy(j);
   async_barrier();

   for (int i = 0; i < j->next_handle; i++)
      jit_free_func(j->funcs->items[i]);
   free(j->funcs);

   for (jit_tier_t *it = j->tiers, *tmp; it; it = tmp) {
      tmp = it->next;
      (*it->plugin.cleanup)(it->context);
      free(it);
   }

   diag_remove_hint_fn(jit_diag_cb, j);

   assert(j->live_tlabs == NULL);
   for (linked_tlab_t *lt = j->free_tlabs, *tmp; lt; lt = tmp) {
      tmp = lt->next;
      free(lt);
   }

   mspace_destroy(j->mspace);
   chash_free(j->index);
   jit_mutex_destroy(&j->mir_lock);
   free(j);
}

bool jit_is_shutdown(jit_t *j)
{
   return load_acquire(&j->shutdown);
}

mspace_t *jit_get_mspace(jit_t *j)
{
   return j->mspace;
}

void *jit_mspace_alloc(size_t size)
{
   jit_thread_local_t *thread = jit_thread_local();
   assert(thread->state == JIT_RUNNING);
   return mspace_alloc(thread->jit->mspace, size);
}

static void jit_install(jit_t *j, jit_func_t *f)
{
   assert_lock_held(&(j->lock));

   func_array_t *list = j->funcs;
   if (f->handle >= list->length) {
      const size_t newlen = MAX(f->handle + 1, list->length * 2);
      func_array_t *new = xcalloc_flex(sizeof(func_array_t),
                                       newlen, sizeof(jit_func_t *));
      new->length = newlen;
      for (size_t i = 0; i < list->length; i++)
         new->items[i] = list->items[i];

      // Synchronises with load_acquire in jit_get_func
      store_release(&(j->funcs), new);

      async_free(list);
      list = new;
   }

   // Synchronises with load_acquire in jit_get_func
   store_release(&(list->items[f->handle]), f);

   chash_put(j->index, f->name, f);
}

static jit_handle_t jit_lazy_compile_locked(jit_t *j, ident_t name)
{
   assert_lock_held(&j->lock);

   jit_func_t *f = chash_get(j->index, name);
   if (f != NULL)
      return f->handle;

   jit_entry_fn_t entry = jit_bind_intrinsic(name) ?: jit_interp;

   f = xcalloc(sizeof(jit_func_t));
   f->name      = name;
   f->state     = JIT_FUNC_PLACEHOLDER;
   f->jit       = j;
   f->handle    = j->next_handle++;
   f->next_tier = j->tiers;
   f->hotness   = f->next_tier ? f->next_tier->threshold : 0;
   f->entry     = entry;

   // Install now to allow circular references in relocations
   jit_install(j, f);

   return f->handle;
}

jit_handle_t jit_lazy_compile(jit_t *j, ident_t name)
{
   jit_func_t *f = chash_get(j->index, name);
   if (f != NULL)
      return f->handle;

   SCOPED_LOCK(j->lock);
   return jit_lazy_compile_locked(j, name);
}

bool jit_is_fused_proc(jit_t *j, ident_t name)
{
   return j != NULL && j->registry != NULL
      && unit_registry_is_fused(j->registry, name);
}

jit_func_t *jit_get_func(jit_t *j, jit_handle_t handle)
{
   assert(handle != JIT_HANDLE_INVALID);

   // Synchronises with store_release in jit_install
   func_array_t *list = load_acquire(&(j->funcs));
   assert(handle < list->length);

   // Synchronises with store_release in jit_install
   jit_func_t *f = load_acquire(&(list->items[handle]));
   assert(f != NULL);
   return f;
}

bool jit_func_signal_only(jit_t *j, jit_handle_t handle)
{
   if (handle == JIT_HANDLE_INVALID)
      return false;

   jit_func_t *f = jit_get_func(j, handle);
   return load_acquire(&(f->state)) == JIT_FUNC_READY && f->signal_only;
}

__attribute__((noreturn))
static void jit_missing_unit(jit_func_t *f)
{
   tree_t unit = lib_get_qualified(f->name);
   if (unit != NULL && tree_kind(unit) == T_PACKAGE)
      jit_msg(tree_loc(unit), DIAG_FATAL, "missing body for package %s",
              istr(f->name));
   else
      jit_msg(NULL, DIAG_FATAL, "missing body for %s", istr(f->name));

   __builtin_unreachable();
}

void jit_fill_irbuf(jit_func_t *f)
{
   const func_state_t state = load_acquire(&(f->state));
   switch (state) {
   case JIT_FUNC_READY:
      if (f->irbuf != NULL)
         return;
      // Fall-through
   case JIT_FUNC_PLACEHOLDER:
      if (atomic_cas(&(f->state), state, JIT_FUNC_COMPILING))
         break;
      // Fall-through
   case JIT_FUNC_COMPILING:
      // Another thread is compiling this function
      for (int timeout = 0; load_acquire(&(f->state)) != JIT_FUNC_READY; ) {
         if (++timeout % COMPILE_TIMEOUT == 0)
            warnf("waiting for %s to finish compiling", istr(f->name));
         thread_sleep(100);
      }
      return;
   case JIT_FUNC_ERROR:
      jit_missing_unit(f);
      break;
   default:
      fatal_trace("illegal function state for %s", istr(f->name));
   }

   assert(f->irbuf == NULL);

   jit_thread_local_t *thread = jit_thread_local();
   const jit_state_t oldstate = thread->state;
   jit_transition(thread, f->jit, oldstate, JIT_COMPILING);

   mir_unit_t *mu;
   {
      // Unit registry and MIR import is not thread-safe; use a dedicated
      // recursive mutex because lazy MIR lowering may fold constants via
      // jit_call_thunk2 -> jit_lazy_compile (j->lock) or nested mir_get_unit.
      jit_mutex_lock(&f->jit->mir_lock);
      mu = mir_get_unit(f->jit->mir, f->name);
      if (mu == NULL && f->jit->registry != NULL) {
         (void)unit_registry_get(f->jit->registry, f->name);
         mu = mir_get_unit(f->jit->mir, f->name);
      }
      jit_mutex_unlock(&f->jit->mir_lock);
   }

   if (mu == NULL) {
      store_release(&(f->state), JIT_FUNC_ERROR);
      jit_missing_unit(f);
   }

   jit_irgen(f, mu);

   if (mir_get_kind(mu) == MIR_UNIT_PROCESS) {
      // A process is compiled once and its code is no longer needed:
      // only the shape is kept for units nested inside it
      jit_mutex_lock(&f->jit->mir_lock);
      mir_unit_free(mu);
      jit_mutex_unlock(&f->jit->mir_lock);
   }

   jit_transition(thread, f->jit, JIT_COMPILING, oldstate);
}

jit_handle_t jit_compile(jit_t *j, ident_t name)
{
   jit_handle_t handle = jit_lazy_compile(j, name);
   if (handle == JIT_HANDLE_INVALID)
      return handle;

   jit_func_t *f = jit_get_func(j, handle);
   jit_fill_irbuf(f);

   return handle;
}

static bool jit_exit_is_mt_safe(jit_exit_t exit)
{
   switch (exit) {
   case JIT_EXIT_INDEX_FAIL:
   case JIT_EXIT_OVERFLOW:
   case JIT_EXIT_NULL_DEREF:
   case JIT_EXIT_LENGTH_FAIL:
   case JIT_EXIT_UNREACHABLE:
   case JIT_EXIT_DIV_ZERO:
   case JIT_EXIT_EXPONENT_FAIL:
   case JIT_EXIT_RANGE_FAIL:
   case JIT_EXIT_DIR_FAIL:
      return true;
   default:
      return false;
   }
}

static bool jit_intrinsic_is_mt_safe(ident_t name)
{
   const char *s = istr(name);
   if (strncmp(s, "IEEE.STD_LOGIC_1164.", 20) == 0
       || strncmp(s, "IEEE.NUMERIC_STD.", 17) == 0
       || strncmp(s, "IEEE.MATH_REAL.", 15) == 0
       || strncmp(s, "STD.STANDARD.", 13) == 0)
      return true;

   return strcmp(s, "__sv_bits_to_real") == 0
      || strcmp(s, "__sv_real_to_int") == 0
      || strcmp(s, "__sv_real_to_word") == 0
      || strcmp(s, "__sv_words_to_real") == 0
      || strcmp(s, "__sv_real_finite") == 0
      || strcmp(s, "__sv_wildcard_eq") == 0
      || strcmp(s, "__sv_logic_to_std") == 0
      || strcmp(s, "__sv_std_to_logic") == 0
      || strcmp(s, "__sv_island_in_reset") == 0;
}

static void jit_prepare_deps(jit_t *j, jit_func_t *f)
{
   if (f->deps_prepared)
      return;

   f->deps_prepared = 1;

   for (unsigned i = 0; i < f->nirs; i++) {
      const jit_ir_t *ir = &(f->irbuf[i]);
      if (ir->op == MACRO_EXIT) {
         if (!jit_exit_is_mt_safe(ir->arg1.exit))
            f->mt_unsafe = 1;
      }
      else if (ir->op == MACRO_PUTPRIV)
         f->mt_unsafe = 1;
      else if (ir->op == J_CALL && ir->arg1.kind != JIT_VALUE_HANDLE)
         f->mt_unsafe = 1;
   }

   for (unsigned i = 0; i < f->nirs; i++) {
      const jit_ir_t *ir = &(f->irbuf[i]);
      if (ir->op == J_CALL && ir->arg1.kind == JIT_VALUE_HANDLE) {
         if (!jit_prepare_call(j, ir->arg1.handle, 0))
            f->mt_unsafe = 1;
      }
   }
}

static bool jit_func_is_linked_pkg(const jit_func_t *f)
{
   if (f->irbuf == NULL || f->nirs < 5)
      return false;
   if (f->irbuf[0].op != MACRO_GETPRIV
       || f->irbuf[0].arg1.kind != JIT_VALUE_HANDLE
       || f->irbuf[0].arg1.handle != f->handle)
      return false;
   if (f->irbuf[1].op != J_CMP || f->irbuf[2].op != J_JUMP
       || f->irbuf[3].op != J_SEND || f->irbuf[4].op != J_RET)
      return false;

   const mptr_t mp = load_acquire(&f->privdata);
   return mp != MPTR_INVALID && *mptr_get(mp) != NULL;
}

bool jit_prepare_call(jit_t *j, jit_handle_t handle, unsigned count)
{
   if (handle == JIT_HANDLE_INVALID)
      return true;

   jit_func_t *f = jit_get_func(j, handle);
   const jit_entry_fn_t entry = load_acquire(&f->entry);
   const func_state_t state = load_acquire(&f->state);

   if (entry != jit_interp && state == JIT_FUNC_PLACEHOLDER) {
      if (!f->deps_prepared) {
         f->deps_prepared = 1;
         if (!jit_intrinsic_is_mt_safe(f->name))
            f->mt_unsafe = 1;
      }
      return !f->mt_unsafe;
   }

   if (!f->deps_prepared) {
      if (state != JIT_FUNC_READY || f->irbuf == NULL)
         jit_fill_irbuf(f);
      if (jit_func_is_linked_pkg(f))
         f->deps_prepared = 1;
      else
         jit_prepare_deps(j, f);
   }
   else if (f->mt_unsafe && f->irbuf != NULL && jit_func_is_linked_pkg(f)) {
      f->mt_unsafe = 0;
   }

   if (entry == jit_interp && relaxed_load(&f->next_tier) != NULL && count > 0) {
      if (f->hotness <= count) {
         f->hotness = 0;
         jit_tier_up(f);
      }
      else
         f->hotness -= count;
   }

   return !f->mt_unsafe;
}

void *jit_link(jit_t *j, jit_handle_t handle)
{
   if (handle == JIT_HANDLE_INVALID)
      return NULL;

   jit_func_t *f = jit_get_func(j, handle);
   void **privptr = jit_get_privdata_ptr(j, f);
   void *existing = load_acquire(privptr);
   if (existing != NULL)
      return existing;

   jit_fill_irbuf(f);

   tlab_t tlab = jit_null_tlab(j);
   jit_scalar_t p1 = { .pointer = NULL }, p2 = p1, result;
   if (!jit_fastcall(j, f->handle, &result, p1, p2, &tlab)) {
      error_at(&(f->object->loc), "failed to initialise %s", istr(f->name));
      result.pointer = NULL;
   }
   else if (result.pointer == NULL)
      fatal_trace("link %s returned NULL", istr(f->name));
   else if (f->privdata == MPTR_INVALID)
      fatal_trace("cannot link unit %s", istr(f->name));

   // Initialisation should save the context pointer
   assert(result.pointer == *mptr_get(f->privdata));

   return result.pointer;
}

void **jit_get_privdata_ptr(jit_t *j, jit_func_t *f)
{
   if (unlikely(load_acquire(&f->privdata) == MPTR_INVALID)) {
      SCOPED_LOCK(j->lock);
      if (f->privdata == MPTR_INVALID)
         store_release(&f->privdata, mptr_new(j->mspace, "privdata"));
   }

   return mptr_get(f->privdata);
}

void *jit_get_frame_var(jit_t *j, jit_handle_t handle, void *p, ident_t name)
{
   jit_func_t *f = jit_get_func(j, handle);
   jit_fill_irbuf(f);

   for (int i = 0; i < f->nvars; i++) {
      if (f->linktab[i].name == name)
         return p + f->linktab[i].offset;
   }

   fatal_trace("%s has no variable %s", istr(f->name), istr(name));
}

// Name the process of an RTL island function that contains LOC.  A process
// longer than a loc_t line delta can express (DELTA_INVALID) contains every
// line after its first: the one starting last before LOC is taken when no
// process range contains it.
static bool find_island_proc_loc_r(tree_t blk, const loc_t *loc,
                                   text_buf_t *tb, int depth, bool open_end,
                                   unsigned *best_line, text_buf_t *best)
{
   if (blk == NULL || loc == NULL || depth > 16)
      return false;

   const int nstmts = tree_stmts(blk);
   for (int i = 0; i < nstmts; i++) {
      tree_t st = tree_stmt(blk, i);
      if (tree_kind(st) == T_PROCESS) {
         const loc_t *pl = tree_loc(st);
         if (pl->file_ref != loc->file_ref || loc->first_line < pl->first_line)
            continue;
         if (loc->first_line <= pl->first_line + pl->line_delta) {
            tb_append(tb, ':');
            tb_istr(tb, ident_downcase(tree_ident(st)));
            return true;
         }
         else if (open_end && pl->line_delta == DELTA_INVALID
                  && (*best_line == 0 || pl->first_line > *best_line)) {
            *best_line = pl->first_line;
            tb_rewind(best);
            tb_cat(best, tb_get(tb));
            tb_append(best, ':');
            tb_istr(best, ident_downcase(tree_ident(st)));
         }
      }
      else if (tree_kind(st) == T_BLOCK && tree_decls(st) > 0
               && tree_kind(tree_decl(st, 0)) == T_HIER) {
         tree_t unit = tree_ref(tree_decl(st, 0));
         if (unit != NULL && !is_design_unit(unit)
             && tree_kind(unit) != T_COMPONENT) {
            const size_t mark = tb_len(tb);
            tb_append(tb, ':');
            tb_istr(tb, ident_downcase(tree_ident(st)));
            if (find_island_proc_loc_r(st, loc, tb, depth + 1, open_end,
                                       best_line, best))
               return true;
            tb_trim(tb, mark);
         }
      }
   }
   return false;
}

static bool find_island_proc_loc(tree_t blk, const loc_t *loc, text_buf_t *tb)
{
   LOCAL_TEXT_BUF best = tb_new();
   unsigned best_line = 0;
   if (find_island_proc_loc_r(blk, loc, tb, 0, true, &best_line, best))
      return true;
   else if (best_line == 0)
      return false;
   tb_rewind(tb);
   tb_cat(tb, tb_get(best));
   return true;
}

// The process named by the last stack trace of an RTL island function
static __thread ident_t last_island_proc = NULL;

ident_t jit_last_island_proc(void)
{
   return last_island_proc;
}

static void jit_emit_trace(diag_t *d, const loc_t *loc, object_t *enclosing,
                           const char *symbol)
{
   tree_t tree = tree_from_object(enclosing);
   if (tree != NULL) {
      switch (tree_kind(tree)) {
      case T_PROCESS:
         {
            rt_proc_t *proc = get_active_proc();
            ident_t name = proc ? proc->name : tree_ident(tree);
            diag_trace(d, loc, "Process$$ %pi", name);
         }
         break;
      case T_ATTR_SPEC:
         assert(ident_casecmp(tree_ident(tree), well_known(W_FOREIGN)));
         diag_trace(d, loc, "Subprogram$$ %pT", tree_type(tree_ref(tree)));
         break;
      case T_FUNC_BODY:
      case T_FUNC_DECL:
         diag_trace(d, loc, "Function$$ %pT", tree_type(tree));
         break;
      case T_PROC_BODY:
      case T_PROC_DECL:
         diag_trace(d, loc, "Procedure$$ %pT", tree_type(tree));
         break;
      case T_TYPE_DECL:
         if (strstr(symbol, "$value"))
            diag_trace(d, loc, "Attribute$$ %pI'VALUE", tree_ident(tree));
         else
            diag_trace(d, loc, "Type$$ %pI", tree_ident(tree));
         break;
      case T_BLOCK:
         if (symbol != NULL) {
            const char *tag = strstr(symbol, ".$vhdl_island_");
            const char *dot = tag != NULL ? strchr(symbol, '.') : NULL;
            if (dot != NULL && dot < tag) {
               LOCAL_TEXT_BUF tb = tb_new();
               for (const char *p = dot; p < tag; p++)
                  tb_append(tb, *p == '.' ? ':' : tolower_iso88591(*p));
               find_island_proc_loc(tree, loc, tb);
               diag_trace(d, loc, "Process$$ %s", tb_get(tb));
               last_island_proc = ident_new(tb_get(tb));
               break;
            }
         }
         diag_trace(d, loc, "Process$$ (init)");
         break;
      case T_PACKAGE:
      case T_PACK_BODY:
      case T_PACK_INST:
         diag_trace(d, loc, "Package$$ %pI", tree_ident(tree));
         break;
      case T_INERTIAL:
         diag_trace(d, loc, "Equivalent process");
         break;
      case T_TYPE_CONV:
         diag_trace(d, loc, "Type conversion %pT", tree_type(tree));
         break;
      case T_PARAM_DECL:
         diag_trace(d, loc, "Parameter %pI", tree_ident(tree));
         break;
      case T_PARAM:
         diag_trace(d, loc, "Port conversion");
         break;
      case T_ASSOC:
         diag_trace(d, loc, "Resolution wrapper");
         break;
      default:
         {
            const char *class = class_str(class_of(tree));
            diag_trace(d, loc, "%c%s %s", toupper_iso88591(class[0]),
                       class + 1, istr(tree_ident(tree)));
         }
         break;
      }
   }

   vlog_node_t v = vlog_from_object(enclosing);
   if (v != NULL) {
      switch (vlog_kind(v)) {
      case V_INITIAL:
         diag_trace(d, loc, "Initial procedure %pQ", vlog_ident(v));
         break;
      case V_ALWAYS:
         diag_trace(d, loc, "Always procedure %pQ", vlog_ident(v));
         break;
      case V_FINAL:
         diag_trace(d, loc, "Final procedure");
         break;
      case V_ASSIGN:
         diag_trace(d, loc, "Continuous assignment");
         break;
      case V_NET_DECL:
         diag_trace(d, loc, "Net %pQ", vlog_ident(v));
         break;
      case V_GATE_INST:
         diag_trace(d, loc, "Gate instance");
         break;
      case V_PORT_MAP:
         diag_trace(d, loc, "Port connection for %pQ",
                    vlog_ident(vlog_ref(v)));
         break;
      case V_INST_BODY:
         diag_trace(d, loc, "Instance %pQ", vlog_ident(v));
         break;
      case V_TCHECK:
         diag_trace(d, loc, "Timing check");
         break;
      case V_FUNC_DECL:
      case V_TASK_DECL:
      case V_CONSTRUCTOR:
      case V_CLASS_DECL:
      case V_NAMESPACE:
      case V_PACKAGE:
         diag_trace(d, loc, "%pQ", vlog_ident(v));
         break;
      default:   // Statement in a fork block
         diag_trace(d, loc, "Forked process");
      }
   }
}

void jit_lower_file(jit_t *j, jit_file_fn_t fn, void *ctx)
{
   jit_mutex_lock(&j->mir_lock);

   if (j->registry != NULL)
      unit_registry_lower_file(j->registry, fn, ctx);

   if (j->mir != NULL)
      mir_lower_file(j->mir, fn, ctx);

   jit_mutex_unlock(&j->mir_lock);
}

int jit_stack_depth(void)
{
   // Number of frames of jit_stack_trace without building them
   int count = 0;
   for (jit_anchor_t *a = jit_thread_local()->anchor; a; a = a->caller)
      count++;
   return count;
}

jit_stack_trace_t *jit_stack_trace(void)
{
   jit_thread_local_t *thread = jit_thread_local();

   int count = 0;
   for (jit_anchor_t *a = thread->anchor; a; a = a->caller)
      count++;

   jit_stack_trace_t *stack =
      xmalloc_flex(sizeof(jit_stack_trace_t), count, sizeof(jit_frame_t));
   stack->count = count;

   jit_frame_t *frame = stack->frames;
   for (jit_anchor_t *a = thread->anchor; a; a = a->caller, frame++) {
      frame->loc    = LOC_INVALID;
      frame->symbol = a->func->name;
      frame->object = NULL;

      jit_fill_irbuf(a->func);

      frame->object = a->func->object;

      // Scan backwards to find the last debug info
      assert(a->irpos < a->func->nirs);
      frame->loc = frame->object ? frame->object->loc : LOC_INVALID;
      for (jit_ir_t *ir = &(a->func->irbuf[a->irpos]);
           ir >= a->func->irbuf; ir--) {
         if (ir->op == J_DEBUG) {
            frame->loc = ir->arg1.loc;
            break;
         }
         else if (ir->target)
            break;
      }
   }

   return stack;
}

static void jit_diag_cb(diag_t *d, void *arg)
{
   jit_t *j = arg;

   last_island_proc = NULL;

   if (jit_thread_local()->state != JIT_RUNNING)
      return;
   else if (j->silent) {
      diag_suppress(d, true);
      j->suppressed++;
      return;
   }

   jit_stack_trace_t *stack LOCAL = jit_stack_trace();

   // The innermost frame has no debug info before the operation that raised
   // the diagnostic so its location falls back to the declaration of the
   // subprogram or process: the diagnostic knows the statement
   const loc_t *dloc = diag_get_loc(d);
   if (stack->count > 0 && dloc != NULL && !loc_invalid_p(dloc)) {
      jit_frame_t *inner = &(stack->frames[0]);
      if (inner->object != NULL && inner->loc.first_line == inner->object->loc.first_line
          && inner->loc.first_column == inner->object->loc.first_column
          && inner->loc.file_ref == inner->object->loc.file_ref)
         inner->loc = *dloc;
   }

   for (int i = 0; i < stack->count; i++) {
      jit_frame_t *frame = &(stack->frames[i]);

      if (frame->object != NULL)
         jit_emit_trace(d, &(frame->loc), frame->object, istr(frame->symbol));
      else
         diag_trace(d, &(frame->loc), "%s", istr(frame->symbol));
   }
}

static void jit_transition(jit_thread_local_t *thread, jit_t *j,
                           jit_state_t from, jit_state_t to)
{
#ifdef DEBUG
   if (thread->state != from)
      fatal_trace("expected thread state %d but have %d", from, thread->state);
#endif

   thread->state = to;

   switch (to) {
   case JIT_RUNNING:
      if (from != JIT_RUNNING)
         thread->jit = j;
      else
         assert(thread->jit == j);
      break;
   case JIT_IDLE:
   case JIT_COMPILING:
      if (from == JIT_RUNNING)
         thread->jit = NULL;
      break;
   }
}

static bool jit_try_vcall(jit_t *j, jit_func_t *f, jit_scalar_t *args,
                          tlab_t *tlab)
{
   jit_thread_local_t *thread = jit_thread_local();
   const jit_state_t oldstate = thread->state;

   jit_transition(thread, j, oldstate, JIT_RUNNING);
   bool ok = jit_trampoline(thread, f, args, tlab);
   jit_transition(thread, j, JIT_RUNNING, oldstate);

   return ok;
}

static void jit_unpack_args(jit_func_t *f, jit_scalar_t *args, va_list ap)
{
   jit_fill_irbuf(f);  // Ensure FFI spec is set

   int wptr = 0;
   for (int i = 1; ffi_spec_has(f->spec, i); i++) {
      switch (ffi_spec_get(f->spec, i)) {
      case FFI_POINTER:
         args[wptr++].pointer = va_arg(ap, void *);
         break;
      case FFI_FLOAT:
         args[wptr++].real = va_arg(ap, double);
         break;
      case FFI_UARRAY:
         args[wptr++].pointer = va_arg(ap, void *);
         args[wptr++].integer = va_arg(ap, int32_t);
         args[wptr++].integer = va_arg(ap, int32_t);
         break;
      case FFI_INT8:
      case FFI_INT16:
      case FFI_INT32:
         args[wptr++].integer = va_arg(ap, int32_t);
         break;
      case FFI_UINT8:
      case FFI_UINT16:
      case FFI_UINT32:
         args[wptr++].integer = va_arg(ap, uint32_t);
         break;
      default:
         args[wptr++].integer = va_arg(ap, int64_t);
         break;
      }
   }
}

bool jit_try_call(jit_t *j, jit_handle_t handle, jit_scalar_t *result, ...)
{
   va_list ap;
   va_start(ap, result);

   jit_func_t *f = jit_get_func(j, handle);

   jit_scalar_t args[JIT_MAX_ARGS];
   jit_unpack_args(f, args, ap);

   va_end(ap);

   tlab_t tlab = jit_null_tlab(j);
   if (!jit_try_vcall(j, f, args, &tlab))
      return false;

   *result = args[0];
   return true;
}

bool jit_try_call_tlab(jit_t *j, jit_handle_t handle, tlab_t *tlab,
                       jit_scalar_t *result, ...)
{
   va_list ap;
   va_start(ap, result);

   jit_func_t *f = jit_get_func(j, handle);

   jit_scalar_t args[JIT_MAX_ARGS];
   jit_unpack_args(f, args, ap);

   va_end(ap);

   if (!jit_try_vcall(j, f, args, tlab))
      return false;

   *result = args[0];
   return true;
}

jit_scalar_t jit_call(jit_t *j, jit_handle_t handle, ...)
{
   va_list ap;
   va_start(ap, handle);

   jit_func_t *f = jit_get_func(j, handle);

   jit_scalar_t args[JIT_MAX_ARGS];
   jit_unpack_args(f, args, ap);

   va_end(ap);

   tlab_t tlab = jit_null_tlab(j);

   if (!jit_try_vcall(j, f, args, &tlab))
      fatal_trace("call to %s failed", istr(jit_get_func(j, handle)->name));

   return args[0];
}

bool jit_fastcall(jit_t *j, jit_handle_t handle, jit_scalar_t *result,
                  jit_scalar_t p1, jit_scalar_t p2, tlab_t *tlab)
{
   jit_func_t *f = jit_get_func(j, handle);

   jit_scalar_t args[JIT_MAX_ARGS];
   args[0] = p1;
   args[1] = p2;

   if (!jit_try_vcall(j, f, args, tlab))
      return false;

   *result = args[0];
   return true;
}

bool jit_vfastcall(jit_t *j, jit_handle_t handle, const jit_scalar_t *inargs,
                   unsigned nargs, jit_scalar_t *results, unsigned nresults,
                   tlab_t *tlab)
{
   jit_func_t *f = jit_get_func(j, handle);

   jit_scalar_t args[JIT_MAX_ARGS];
   memcpy(args, inargs, nargs * sizeof(jit_scalar_t));

   if (!jit_try_vcall(j, f, args, tlab))
      return false;

   if (nresults > 0)
      memcpy(results, args, nresults * sizeof(jit_scalar_t));

   return true;
}

bool jit_call_closure(jit_t *j, ffi_closure_t *closure, jit_scalar_t *result,
                      jit_scalar_t arg, tlab_t *tlab)
{
   jit_func_t *f = jit_get_func(j, closure->handle);

   jit_scalar_t args[JIT_MAX_ARGS];
   assert(closure->nargs + 2 <= JIT_MAX_ARGS);
   if (likely(closure->nargs == 1)) {
      args[0] = closure->args[0];
      args[1] = arg;
   }
   else {
      memcpy(args, closure->args, closure->nargs * sizeof(jit_scalar_t));
      args[closure->nargs] = arg;
   }

   if (!jit_try_vcall(j, f, args, tlab))
      return false;

   *result = args[0];
   return true;
}

bool jit_run_guarded(jit_t *j, void (*fn)(void *), void *ctx)
{
   return jit_trampoline_guard(jit_thread_local(), fn, ctx);
}

void jit_call_closure_guarded(jit_t *j, ffi_closure_t *closure,
                              jit_scalar_t *result, jit_scalar_t arg,
                              tlab_t *tlab)
{
   jit_func_t *f = jit_get_func(j, closure->handle);

   jit_thread_local_t *thread = jit_thread_local();
   assert(thread->jmp_buf != NULL);
   assert(thread->guard_args != NULL);

   jit_scalar_t *args = thread->guard_args;
   if (likely(closure->nargs == 1)) {
      args[0] = closure->args[0];
      args[1] = arg;
   }
   else {
      assert(closure->nargs + 2 <= JIT_MAX_ARGS);
      memcpy(args, closure->args, closure->nargs * sizeof(jit_scalar_t));
      args[closure->nargs] = arg;
   }

   const jit_state_t oldstate = thread->state;
   jit_anchor_t *saved_anchor = thread->anchor;
   jit_t *saved_jit = thread->jit;
   thread->state = JIT_RUNNING;
   thread->jit = j;

   jit_entry_fn_t entry = load_acquire(&f->entry);
   (*entry)(f, NULL, args, tlab);

   thread->anchor = saved_anchor;
   thread->state = oldstate;
   thread->jit = saved_jit;

   *result = args[0];
}

void jit_fastcall_guarded(jit_t *j, jit_handle_t handle, jit_scalar_t *result,
                          jit_scalar_t p1, jit_scalar_t p2, tlab_t *tlab)
{
   jit_func_t *f = jit_get_func(j, handle);

   jit_thread_local_t *thread = jit_thread_local();
   assert(thread->jmp_buf != NULL);
   assert(thread->guard_args != NULL);

   jit_scalar_t *args = thread->guard_args;
   args[0] = p1;
   args[1] = p2;

   const jit_state_t oldstate = thread->state;
   jit_anchor_t *saved_anchor = thread->anchor;
   jit_t *saved_jit = thread->jit;
   thread->state = JIT_RUNNING;
   thread->jit = j;

   jit_entry_fn_t entry = load_acquire(&f->entry);
   (*entry)(f, NULL, args, tlab);

   thread->anchor = saved_anchor;
   thread->state = oldstate;
   thread->jit = saved_jit;

   *result = args[0];
}

void jit_batch_begin(jit_t *j, jit_batch_ctx_t *ctx)
{
   jit_thread_local_t *thread = jit_thread_local();
   assert(thread->jmp_buf != NULL);
   assert(thread->guard_args != NULL);

   ctx->thread       = thread;
   ctx->saved_anchor = thread->anchor;
   ctx->saved_jit    = thread->jit;
   ctx->saved_state  = thread->state;
   ctx->args         = thread->guard_args;

   thread->state = JIT_RUNNING;
   thread->jit   = j;
}

void jit_batch_end(const jit_batch_ctx_t *ctx)
{
   jit_thread_local_t *thread = ctx->thread;
   thread->anchor = ctx->saved_anchor;
   thread->state  = (jit_state_t)ctx->saved_state;
   thread->jit    = ctx->saved_jit;
}

void *jit_try_resolve_native(jit_t *j, jit_handle_t handle,
                             jit_fast_entry_fn_t *entry_out)
{
   jit_func_t *f = jit_get_func(j, handle);
   if (load_acquire(&f->state) != JIT_FUNC_READY)
      return NULL;
   if (relaxed_load(&f->next_tier) != NULL)
      return NULL;
   jit_entry_fn_t entry = load_acquire(&f->entry);
   if (entry == NULL || entry == jit_interp)
      return NULL;
   *entry_out = (jit_fast_entry_fn_t)entry;
   return f;
}

void *jit_call_thunk(jit_t *j, vcode_unit_t unit, void *context,
                     thunk_result_fn_t fn, void *arg)
{
   assert(vcode_unit_kind(unit) == VCODE_UNIT_THUNK);

   jit_func_t *f = xcalloc(sizeof(jit_func_t));
   f->state  = JIT_FUNC_COMPILING;
   f->jit    = j;
   f->handle = JIT_HANDLE_INVALID;
   f->entry  = jit_interp;
   f->object = vcode_unit_object(unit);

   mir_unit_t *mu = mir_import(f->jit->mir, unit);
   jit_irgen(f, mu);
   mir_unit_free(mu);

   jit_scalar_t args[JIT_MAX_ARGS];
   args[0].pointer = context;

   tlab_t tlab = jit_null_tlab(j);

   void *user = NULL;
   if (jit_try_vcall(j, f, args, &tlab))
      user = (*fn)(args, arg);

   jit_free_func(f);
   return user;
}

void *jit_call_thunk2(jit_t *j, mir_unit_t *mu, void *context,
                      thunk_result_fn_t fn, void *arg)
{
   assert(mir_get_kind(mu) == MIR_UNIT_THUNK);

   jit_func_t *f = xcalloc(sizeof(jit_func_t));
   f->state  = JIT_FUNC_COMPILING;
   f->jit    = j;
   f->handle = JIT_HANDLE_INVALID;
   f->entry  = jit_interp;
   f->object = mir_get_object(mu);

   jit_irgen(f, mu);

   jit_scalar_t args[JIT_MAX_ARGS];
   args[0].pointer = context;

   tlab_t tlab = jit_null_tlab(j);

   void *user = NULL;
   if (jit_try_vcall(j, f, args, &tlab) && fn != NULL)
      user = (*fn)(args, arg);

   jit_free_func(f);
   return user;
}

tlab_t jit_null_tlab(jit_t *j)
{
   tlab_t t = { .mspace = j->mspace };
   return t;
}

unsigned jit_suppressed_count(jit_t *j)
{
   return j->suppressed;
}

bool jit_set_silent(jit_t *j, bool silent)
{
   const bool prev = j->silent;
   j->silent = silent;
   return prev;
}

bool jit_is_silent(jit_t *j)
{
   return j->silent;
}

void jit_msg(const loc_t *where, diag_level_t level, const char *fmt, ...)
{
   diag_t *d = diag_new(level, where);

   va_list ap;
   va_start(ap, fmt);
   diag_vprintf(d, fmt, ap);
   va_end(ap);

   diag_emit(d);

   if (level == DIAG_FATAL)
      jit_abort_with_status(EXIT_FAILURE);
}

void jit_abort(void)
{
   jit_thread_local_t *thread = jit_thread_local();

   switch (thread->state) {
   case JIT_IDLE:
   case JIT_COMPILING:
      fatal_exit(1);
      break;
   case JIT_RUNNING:
      if (relaxed_load(&thread->jmp_buf) != NULL)
         jit_unwind(thread);
      else {
         const int code = atomic_load(&thread->jit->exit_status);
         fatal_exit(code);
      }
      break;
   }

   __builtin_unreachable();
}

void jit_abort_with_status(int code)
{
   assert(code != INT_MIN);

   jit_thread_local_t *thread = jit_thread_local();
   if (thread->jit != NULL)
      atomic_store(&(thread->jit->exit_status), code);

   jit_abort();
}

void jit_reset_exit_status(jit_t *j)
{
   atomic_store(&(j->exit_status), INT_MIN);
}

bool jit_exit_status(jit_t *j, int *status)
{
   if (j->exit_status != INT_MIN) {
      *status = j->exit_status;
      return true;
   }
   else
      return false;
}

static nvc_lock_t  jit_pools_lock = 0;
static jit_pool_t *jit_pools_head = NULL;
static bool        jit_pools_atexit_registered = false;

static void jit_pool_stop_and_join(jit_pool_t *pool)
{
   if (pool == NULL)
      return;

   store_release(&pool->jit->shutdown, true);

   jit_mutex_lock(&pool->mtx);
   pool->stop  = true;
   pool->head  = 0;
   pool->tail  = 0;
   pool->count = 0;
   jit_cond_broadcast(&pool->cv);
   jit_mutex_unlock(&pool->mtx);

   for (unsigned i = 0; i < pool->nworkers; i++) {
      if (pool->workers[i] != NULL) {
         thread_join(pool->workers[i]);
         pool->workers[i] = NULL;
      }
   }
}

static void jit_pools_shutdown_atexit(void)
{
   if (thread_attached() && thread_id() != 0)
      return;

   jit_pool_t *list;
   {
      SCOPED_LOCK(jit_pools_lock);
      list = jit_pools_head;
      jit_pools_head = NULL;
   }

   for (jit_pool_t *p = list; p != NULL; p = p->next)
      jit_pool_stop_and_join(p);
}

static void *jit_pool_worker(void *arg)
{
   mspace_stack_limit(MSPACE_CURRENT_FRAME);
   jit_init_thread();

   jit_pool_t *pool = arg;
   for (;;) {
      jit_mutex_lock(&pool->mtx);
      while (!pool->stop && pool->count == 0)
         jit_cond_wait(&pool->cv, &pool->mtx);

      if (pool->stop) {
         jit_mutex_unlock(&pool->mtx);
         return NULL;
      }

      jit_job_t job = pool->queue[pool->head];
      pool->head = (pool->head + 1u) & (pool->cap - 1u);
      pool->count--;
      pool->in_flight++;
      jit_mutex_unlock(&pool->mtx);

      if (!jit_is_shutdown(job.func->jit))
         (*job.tier->plugin.cgen)(job.func->jit, job.func->handle,
                                  job.tier->context);

      jit_mutex_lock(&pool->mtx);
      pool->in_flight--;
      jit_mutex_unlock(&pool->mtx);
   }
}

static jit_pool_t *jit_pool_ensure(jit_t *j)
{
   jit_pool_t *pool = load_acquire(&j->pool);
   if (likely(pool != NULL))
      return pool;

   SCOPED_LOCK(j->lock);
   if (j->pool != NULL)
      return j->pool;
   if (jit_is_shutdown(j))
      return NULL;

   const int sim_threads = opt_get_int(OPT_SIM_THREADS);
   const unsigned req = sim_threads > 1
      ? (unsigned)MIN(JIT_POOL_MAX_WORKERS, sim_threads)
      : 1u;
   const unsigned nworkers = MIN(req, thread_max_workers());

   pool = xcalloc(sizeof(jit_pool_t));
   pool->jit      = j;
   pool->nworkers = nworkers;
   pool->cap      = 64;
   pool->queue    = xcalloc_array(pool->cap, sizeof(jit_job_t));
   pool->workers  = xcalloc_array(nworkers, sizeof(nvc_thread_t *));
   jit_mutex_init(&pool->mtx);
   jit_cond_init(&pool->cv);

   {
      SCOPED_LOCK(jit_pools_lock);
      if (!jit_pools_atexit_registered) {
         atexit(jit_pools_shutdown_atexit);
         jit_pools_atexit_registered = true;
      }
      pool->next = jit_pools_head;
      jit_pools_head = pool;
   }

   for (unsigned i = 0; i < nworkers; i++)
      pool->workers[i] = thread_create(jit_pool_worker, pool, "jit %u", i + 1u);

   store_release(&j->pool, pool);
   return pool;
}

static void jit_pool_enqueue(jit_pool_t *pool, jit_func_t *f, jit_tier_t *tier)
{
   jit_mutex_lock(&pool->mtx);
   if (pool->stop || jit_is_shutdown(pool->jit)) {
      jit_mutex_unlock(&pool->mtx);
      return;
   }

   if (pool->count == pool->cap) {
      const unsigned newcap = pool->cap * 2u;
      jit_job_t *newq = xcalloc_array(newcap, sizeof(jit_job_t));
      const unsigned first = pool->cap - pool->head;
      memcpy(newq, pool->queue + pool->head, first * sizeof(jit_job_t));
      if (pool->head > 0)
         memcpy(newq + first, pool->queue, pool->head * sizeof(jit_job_t));
      free(pool->queue);
      pool->queue = newq;
      pool->head  = 0;
      pool->tail  = pool->count;
      pool->cap   = newcap;
   }

   pool->queue[pool->tail] = (jit_job_t){ .func = f, .tier = tier };
   pool->tail = (pool->tail + 1u) & (pool->cap - 1u);
   pool->count++;
   jit_cond_signal(&pool->cv);
   jit_mutex_unlock(&pool->mtx);
}

static void jit_pool_destroy(jit_t *j)
{
   jit_pool_t *pool = load_acquire(&j->pool);
   if (pool == NULL)
      return;

   {
      SCOPED_LOCK(jit_pools_lock);
      for (jit_pool_t **pp = &jit_pools_head; *pp != NULL; pp = &((*pp)->next)) {
         if (*pp == pool) {
            *pp = pool->next;
            break;
         }
      }
   }

   jit_pool_stop_and_join(pool);
   assert(pool->in_flight == 0);

   jit_cond_destroy(&pool->cv);
   jit_mutex_destroy(&pool->mtx);
   free(pool->queue);
   free(pool->workers);
   free(pool);
   store_release(&j->pool, NULL);
}

void jit_tier_up(jit_func_t *f)
{
   assert(f->hotness <= 0);

   jit_tier_t *tier = atomic_xchg(&f->next_tier, NULL);
   if (tier == NULL)
      return;

   f->hotness = 0;

   if (load_acquire(&f->state) != JIT_FUNC_READY || f->irbuf == NULL)
      jit_fill_irbuf(f);

   if (opt_get_int(OPT_JIT_ASYNC) && thread_max_workers() > 1) {
      jit_pool_t *pool = jit_pool_ensure(f->jit);
      if (pool != NULL) {
         jit_pool_enqueue(pool, f, tier);
         return;
      }
   }

   if (!jit_is_shutdown(f->jit))
      (*tier->plugin.cgen)(f->jit, f->handle, tier->context);
}

void jit_add_tier(jit_t *j, int threshold, const jit_plugin_t *plugin)
{
   assert(threshold > 0);

   jit_tier_t *t = xcalloc(sizeof(jit_tier_t));
   t->next      = j->tiers;
   t->threshold = threshold;
   t->plugin    = *plugin;
   t->context   = (*plugin->init)(j);

   j->tiers = t;
}

ident_t jit_get_name(jit_t *j, jit_handle_t handle)
{
   return jit_get_func(j, handle)->name;
}

object_t *jit_get_object(jit_t *j, jit_handle_t handle)
{
   jit_func_t *f = jit_get_func(j, handle);
   jit_fill_irbuf(f);
   return f->object;
}

bool jit_writes_flags(jit_ir_t *ir)
{
   return ir->op == J_CMP || ir->op == J_FCMP
      || ir->op == J_CCMP || ir->op == J_FCCMP
      || (ir->op == J_ADD && ir->cc != JIT_CC_NONE)
      || (ir->op == J_SUB && ir->cc != JIT_CC_NONE)
      || (ir->op == J_MUL && ir->cc != JIT_CC_NONE)
      || (ir->op == MACRO_EXP && ir->cc != JIT_CC_NONE);
}

bool jit_reads_flags(jit_ir_t *ir)
{
   return (ir->op == J_JUMP && ir->cc != JIT_CC_NONE)
      || ir->op == J_CSET || ir->op == J_CSEL || ir->op == J_CCMP
      || ir->op == J_FCCMP;
}

jit_handle_t jit_assemble(jit_t *j, ident_t name, const char *text)
{
   jit_func_t *f = chash_get(j->index, name);
   if (f != NULL)
      return f->handle;

   {
      SCOPED_LOCK(j->lock);

      f = xcalloc(sizeof(jit_func_t));

      f->name      = name;
      f->state     = JIT_FUNC_READY;
      f->jit       = j;
      f->handle    = j->next_handle++;
      f->next_tier = j->tiers;
      f->hotness   = f->next_tier ? f->next_tier->threshold : 0;
      f->entry     = jit_interp;

      jit_install(j, f);
   }

   enum { LABEL, INS, CCSIZE, RESULT, ARG1, ARG2, NEWLINE, CP } state = LABEL;

   static const struct {
      const char *name;
      jit_op_t    op;
      int         nresult;
      int         nargs;
   } optab[] = {
      { "MOV",     J_MOV,        1, 1 },
      { "ADD",     J_ADD,        1, 2 },
      { "SUB",     J_SUB,        1, 2 },
      { "MUL",     J_MUL,        1, 2 },
      { "DIV",     J_DIV,        1, 2 },
      { "REM",     J_REM,        1, 2 },
      { "RECV",    J_RECV,       1, 1 },
      { "SEND",    J_SEND,       0, 2 },
      { "RET",     J_RET,        0, 0 },
      { "CMP",     J_CMP,        0, 2 },
      { "CCMP",    J_CCMP,       0, 2 },
      { "JUMP",    J_JUMP,       0, 1 },
      { "CSEL",    J_CSEL,       1, 2 },
      { "CSET",    J_CSET,       1, 0 },
      { "NOP",     J_NOP,        0, 0 },
      { "CLAMP",   J_CLAMP,      1, 1 },
      { "CALL",    J_CALL,       0, 1 },
      { "STORE",   J_STORE,      0, 2 },
      { "LOAD",    J_LOAD,       1, 1 },
      { "ULOAD",   J_ULOAD,      1, 1 },
      { "SHL",     J_SHL,        1, 2 },
      { "SHR",     J_SHR,        1, 2 },
      { "ASR",     J_ASR,        1, 2 },
      { "LEA",     J_LEA,        1, 1 },
      { "AND",     J_AND,        1, 2 },
      { "OR",      J_OR,         1, 2 },
      { "XOR",     J_XOR,        1, 2 },
      { "NOT",     J_NOT,        1, 1 },
      { "NEG",     J_NEG,        1, 1 },
      { "FADD",    J_FADD,       1, 2 },
      { "FSUB",    J_FSUB,       1, 2 },
      { "FMUL",    J_FMUL,       1, 2 },
      { "FDIV",    J_FDIV,       1, 2 },
      { "FNEG",    J_FNEG,       1, 1 },
      { "FCMP",    J_FCMP,       0, 2 },
      { "FCVTNS",  J_FCVTNS,     1, 1 },
      { "SCVTF",   J_SCVTF,      1, 1 },
      { "$EXIT",   MACRO_EXIT,   0, 1 },
      { "$COPY",   MACRO_COPY,   1, 2 },
      { "$MOVE",   MACRO_MOVE,   1, 2 },
      { "$CASE",   MACRO_CASE,   1, 2 },
      { "$SALLOC", MACRO_SALLOC, 1, 2 },
      { "$LALLOC", MACRO_LALLOC, 1, 1 },
      { "$BZERO",  MACRO_BZERO,  1, 1 },
      { "$MEMSET", MACRO_MEMSET, 1, 2 },
      { "$EXP",    MACRO_EXP,    1, 2 },
      { "$FEXP",   MACRO_FEXP,   1, 2 },
   };

   static const struct {
      const char *name;
      jit_cc_t    cc;
   } cctab[] = {
      { "T",  JIT_CC_T },
      { "F",  JIT_CC_F },
      { "EQ", JIT_CC_EQ },
      { "NE", JIT_CC_NE },
      { "O",  JIT_CC_O },
      { "NO", JIT_CC_NO },
      { "C",  JIT_CC_C },
      { "NC", JIT_CC_NC },
      { "LT", JIT_CC_LT },
      { "LE", JIT_CC_LE },
      { "GT", JIT_CC_GT },
      { "GE", JIT_CC_GE },
      { "ULT", JIT_CC_C },
      { "ULE", JIT_CC_NO },
      { "UGT", JIT_CC_O },
      { "UGE", JIT_CC_NC },
   };

   const unsigned bufsz = 128;
   f->irbuf = xcalloc_array(bufsz, sizeof(jit_ir_t));

   SCOPED_A(int) lpatch = AINIT;
   int *labels LOCAL = NULL, maxlabel = 0;
   jit_ir_t *ir = NULL;
   int nresult = 0, nargs = 0, cpoolptr = 0;
   char *copy LOCAL = xstrdup(text);
   for (char *tok = strtok(copy, " \r\t"); tok; tok = strtok(NULL, " \r\t")) {
   again:
      switch (state) {
      case LABEL:
         assert(f->nirs < bufsz);
         ir = &(f->irbuf[f->nirs++]);

         if (tok[0] == 'L' && isdigit((int)tok[1])) {
            char *eptr = NULL;
            const int lab = strtol(tok + 1, &eptr, 10);
            if (*eptr != ':')
               fatal_trace("cannot parse label '%s'", tok);

            if (lab > maxlabel) {
               maxlabel = next_power_of_2(lab);
               labels = xrealloc_array(labels, maxlabel + 1, sizeof(int));
            }

            ir->target = 1;
            labels[lab] = ir - f->irbuf;
            state = INS;
         }
         else if (isdigit((int)tok[1])) {
            state = CP;
            goto again;
         }
         else {
            state = INS;
            goto again;
         }
         break;

      case INS:
         {
            char *dot = strchr(tok, '.');
            if (dot != NULL)
               *dot++ = '\0';

            int ipos = 0;
            for (; ipos < ARRAY_LEN(optab)
                    && strcmp(optab[ipos].name, tok); ipos++);

            if (ipos == ARRAY_LEN(optab))
               fatal_trace("illegal instruction %s", tok);

            nargs = optab[ipos].nargs;
            nresult = optab[ipos].nresult;

            ir->op = optab[ipos].op;
            ir->size = JIT_SZ_UNSPEC;
            ir->result = JIT_REG_INVALID;

            if (dot != NULL) {
               tok = dot;
               state = CCSIZE;
               goto again;
            }

            state = nresult > 0 ? RESULT : (nargs > 0 ? ARG1 : NEWLINE);
         }
         break;

      case CCSIZE:
         {
            char *dot = strchr(tok, '.');
            if (dot != NULL)
               *dot++ = '\0';

            if (isdigit((int)tok[0])) {
               switch (atoi(tok)) {
               case 8: ir->size = JIT_SZ_8; break;
               case 16: ir->size = JIT_SZ_16; break;
               case 32: ir->size = JIT_SZ_32; break;
               case 64: ir->size = JIT_SZ_64; break;
               default:
                  fatal_trace("illegal operation size %s", tok);
               }
            }
            else {
               int cpos = 0;
               for (; cpos < ARRAY_LEN(cctab)
                       && strcmp(cctab[cpos].name, tok); cpos++)
                  ;

               if (cpos == ARRAY_LEN(cctab))
                  fatal_trace("illegal condition code %s", tok);

               ir->cc = cctab[cpos].cc;
            }

            if (dot != NULL) {
               tok = dot;
               state = CCSIZE;
               goto again;
            }

            state = nresult > 0 ? RESULT : (nargs > 0 ? ARG1 : NEWLINE);
         }
         break;

      case RESULT:
         if (tok[0] != 'R')
            fatal_trace("expected register name but got '%s'", tok);

         ir->result = atoi(tok + 1);
         f->nregs = MAX(ir->result + 1, f->nregs);
         state = nargs > 0 ? ARG1 : NEWLINE;
         break;

      case ARG1:
      case ARG2:
         {
            jit_value_t arg;
            size_t toklen = strlen(tok);
            if (tok[0] == 'R') {
               arg.kind = JIT_VALUE_REG;
               arg.reg  = atoi(tok + 1);
               f->nregs = MAX(arg.reg + 1, f->nregs);
            }
            else if (tok[0] == '#') {
               arg.kind  = JIT_VALUE_INT64;
               arg.int64 = strtoll(tok + 1, NULL, 0);
            }
            else if (tok[0] == '%') {
               arg.kind  = JIT_VALUE_DOUBLE;
               arg.dval = strtod(tok + 1, NULL);
            }
            else if (tok[0] == 'L') {
               APUSH(lpatch, ir - f->irbuf);
               arg.kind = JIT_VALUE_LABEL;
               arg.label = atoi(tok + 1);
            }
            else if (tok[0] == '[' && tok[1] == 'R') {
               char *eptr = NULL;
               arg.kind = JIT_ADDR_REG;
               arg.reg = strtol(tok + 2, &eptr, 0);
               if (*eptr == '+' || *eptr == '-')
                  arg.disp = strtol(eptr, &eptr, 0);
               else
                  arg.disp = 0;
               if (*eptr != ']')
                  fatal_trace("invalid address %s", tok);
               f->nregs = MAX(arg.reg + 1, f->nregs);
            }
            else if (tok[0] == '[' && tok[1] == 'C' && tok[2] == 'P') {
               char *eptr = NULL;
               arg.int64 = strtol(tok + 4, &eptr, 0);
               arg.kind = JIT_ADDR_CPOOL;
               if (tok[3] != '+' || *eptr != ']')
                  fatal_trace("invalid address %s", tok);
            }
            else if (tok[0] == '<' && tok[toklen - 1] == '>') {
               tok[toklen - 1] = '\0';
               arg.kind = JIT_VALUE_HANDLE;
               arg.handle = jit_lazy_compile(j, ident_new(tok + 1));
            }
            else if (tok[0] == '\n')
               fatal_trace("got newline, expecting argument");
            else
               fatal_trace("cannot parse argument '%s'", tok);

            if (state == ARG1)
               ir->arg1 = arg;
            else
               ir->arg2 = arg;

            state = state == ARG1 && nargs > 1 ? ARG2 : NEWLINE;
         }
         break;

      case NEWLINE:
         if (*tok != '\n')
            fatal_trace("expected newline, got '%s'", tok);
         if (ir->op == MACRO_SALLOC)
            f->framesz += ALIGN_UP(ir->arg2.int64, 8);
         state = LABEL;
         if (*++tok != '\0')
            goto again;
         break;

      case CP:
         {
            if (*tok == '\n' && *++tok == '\0')
               continue;

            char *eptr = NULL;
            unsigned long byte = strtoul(tok, &eptr, 16);
            if (*eptr != '\0' || byte >= 256)
               fatal_trace("invalid constant pool value '%s'", tok);

            if (f->cpool == NULL)
               f->cpool = xmalloc((f->cpoolsz = 64));
            else if (cpoolptr == f->cpoolsz)
               fatal_trace("constant pool overflow");

            f->cpool[cpoolptr++] = byte;
         }
         break;
      }
   }

   f->cpoolsz = cpoolptr;

   for (int i = 0; i < lpatch.count; i++) {
      jit_ir_t *ir = &(f->irbuf[lpatch.items[i]]);
      if (ir->arg1.kind == JIT_VALUE_LABEL)
         ir->arg1.label = labels[ir->arg1.label];
      else
         ir->arg2.label = labels[ir->arg2.label];
   }

   return f->handle;
}

bool jit_will_abort(jit_ir_t *ir)
{
   if (ir->op == MACRO_EXIT) {
      return ir->arg1.exit == JIT_EXIT_INDEX_FAIL
         || ir->arg1.exit == JIT_EXIT_OVERFLOW
         || ir->arg1.exit == JIT_EXIT_NULL_DEREF
         || ir->arg1.exit == JIT_EXIT_UNREACHABLE
         || ir->arg1.exit == JIT_EXIT_LENGTH_FAIL
         || ir->arg1.exit == JIT_EXIT_DIV_ZERO
         || ir->arg1.exit == JIT_EXIT_EXPONENT_FAIL
         || ir->arg1.exit == JIT_EXIT_RANGE_FAIL;
   }
   else
      return ir->op == J_TRAP;
}

void jit_interrupt(jit_t *j, jit_irq_fn_t fn, void *ctx)
{
   relaxed_store(&j->interrupt_ctx, ctx);
   if (!atomic_cas(&j->interrupt, NULL, fn))
      fatal_exit(1);
}

__attribute__((cold, noinline))
static void jit_handle_interrupt(jit_t *j)
{
   jit_irq_fn_t fn = load_acquire(&j->interrupt);
   if (!atomic_cas(&j->interrupt, fn, NULL))
      return;

   (*fn)(j, relaxed_load(&j->interrupt_ctx));
}

void jit_check_interrupt(jit_t *j)
{
   if (unlikely(relaxed_load(&j->interrupt) != NULL))
      jit_handle_interrupt(j);
}

void jit_reset(jit_t *j)
{
   SCOPED_LOCK(j->lock);

   for (int i = 0; i < j->next_handle; i++) {
      jit_func_t *f = j->funcs->items[i];
      if (f->privdata != MPTR_INVALID)
         *mptr_get(f->privdata) = NULL;
   }

   jit_reset_exit_status(j);
}

mir_context_t *jit_get_mir(jit_t *j)
{
   return j->mir;
}

void jit_get_stats(jit_t *j, unsigned *nfuncs, unsigned *nirfuncs,
                   size_t *irbytes)
{
   // Number of functions and the memory used by their intermediate code

   SCOPED_LOCK(j->lock);

   *nfuncs = j->next_handle;
   *nirfuncs = 0;
   *irbytes = 0;

   for (int i = 0; i < j->next_handle; i++) {
      jit_func_t *f = j->funcs->items[i];
      if (f->irbuf != NULL) {
         (*nirfuncs)++;
         *irbytes += f->nirs * sizeof(jit_ir_t);
      }
   }
}

jit_t *jit_for_thread(void)
{
   jit_thread_local_t *thread = jit_thread_local();

   if (unlikely(thread->jit == NULL))
      fatal_trace("thread not attached to JIT");

   return thread->jit;
}

jit_thread_local_t *jit_attach_thread(jit_anchor_t *anchor)
{
   jit_thread_local_t *thread = *jit_thread_local_ptr();
   assert(thread != NULL);

   thread->anchor = anchor;

   jit_check_interrupt(thread->jit);

   return thread;
}


#ifdef DEBUG
static bool tlab_on_list(linked_tlab_t *lt, linked_tlab_t *list)
{
   for (linked_tlab_t *it = list; it; it = it->next) {
      if (it == lt)
         return true;
   }

   return false;
}
#endif

tlab_t *tlab_acquire(jit_t *j)
{
   SCOPED_LOCK(j->lock);

   linked_tlab_t *lt = j->free_tlabs;
   if (lt == NULL) {
      lt = xmalloc(sizeof(linked_tlab_t) + TLAB_SIZE);
      lt->tlab.mspace = j->mspace;
   }
   else {
      assert(!tlab_on_list(lt, j->live_tlabs));
      assert(lt->prev == NULL);
      j->free_tlabs = lt->next;
   }

   lt->next = j->live_tlabs;
   lt->prev = NULL;

   if (j->live_tlabs != NULL) {
      assert(j->live_tlabs->prev == NULL);
      j->live_tlabs->prev = lt;
   }

   j->live_tlabs = lt;

   // Ensure proper starting alignment on 32-bit systems
   const size_t data_off = offsetof(tlab_t, data);
   lt->tlab.alloc = ALIGN_UP(data_off, sizeof(double)) - data_off;
   lt->tlab.limit = TLAB_SIZE - OVERRUN_MARGIN;
   lt->tlab.mem_dirty = 0;
   lt->tlab.quiet = NULL;

   return &(lt->tlab);
}

void tlab_release(jit_t *j, tlab_t *t)
{
   if (t == NULL)
      return;

   SCOPED_LOCK(j->lock);

   linked_tlab_t *lt = container_of(t, linked_tlab_t, tlab);

   assert(t->alloc <= t->limit);
   assert(!tlab_on_list(lt, j->free_tlabs));
   assert(tlab_on_list(lt, j->live_tlabs));

   if (lt->prev == NULL)
      j->live_tlabs = lt->next;
   else {
      assert(lt->prev->next == lt);
      lt->prev->next = lt->next;
   }

   if (lt->next != NULL)
      lt->next->prev = lt->prev;

   assert(!tlab_on_list(lt, j->live_tlabs));

   lt->prev = NULL;
   lt->next = j->free_tlabs;
   j->free_tlabs = lt;
}

void *tlab_alloc(tlab_t *t, size_t size)
{
   assert(t->alloc <= t->limit);
   assert((t->alloc & (sizeof(double) - 1)) == 0);

   if (t->alloc + size <= t->limit) {
      void *p = t->data + t->alloc;
      t->alloc += ALIGN_UP(size, sizeof(double));
      return p;
   }
   else
      return mspace_alloc(t->mspace, size);
}
