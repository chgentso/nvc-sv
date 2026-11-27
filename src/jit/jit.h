//
//  Copyright (C) 2022-2024  Nick Gasson
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

#ifndef _JIT_H
#define _JIT_H

#include "prim.h"
#include "diag.h"
#include "util.h"

typedef int32_t jit_handle_t;
#define JIT_HANDLE_INVALID 0xffffffff

#define TLAB_SIZE (64 * 1024)

// The code generator knows the layout of this struct
typedef struct _tlab {
   mspace_t *mspace;
   uint32_t  alloc;
   uint32_t  limit;
   uint64_t  mem_dirty;
   union {
      void     *quiet;   // Model state for generated code (rt_quiet_t)
      uint64_t  _pad;
   };
   char      data[0];
} tlab_t;

STATIC_ASSERT(offsetof(tlab_t, data) % 16 == 0);

typedef union {
   int64_t  integer;
   void    *pointer;
   double   real;
} jit_scalar_t;

STATIC_ASSERT(sizeof(jit_scalar_t) == 8);

typedef struct {
   void *(*init)(jit_t *);
   void (*cgen)(jit_t *, jit_handle_t, void *);
   void (*cleanup)(void *);
} jit_plugin_t;

typedef struct {
   loc_t     loc;
   object_t *object;
   ident_t   symbol;
} jit_frame_t;

typedef struct {
   size_t      count;
   jit_frame_t frames[0];
} jit_stack_trace_t;

typedef void (*jit_irq_fn_t)(jit_t *, void *);

jit_t *jit_new(unit_registry_t *ur, mir_context_t *mc);
void jit_free(jit_t *j);
jit_handle_t jit_compile(jit_t *j, ident_t name);
jit_handle_t jit_lazy_compile(jit_t *j, ident_t name);
bool jit_prepare_call(jit_t *j, jit_handle_t handle, unsigned count);
void jit_init_thread(void);
jit_handle_t jit_assemble(jit_t *j, ident_t name, const char *text);
void *jit_link(jit_t *j, jit_handle_t handle);
void *jit_get_frame_var(jit_t *j, jit_handle_t handle, void *p, ident_t name);
bool jit_set_silent(jit_t *j, bool silent);
bool jit_is_silent(jit_t *j);
unsigned jit_suppressed_count(jit_t *j);
mspace_t *jit_get_mspace(jit_t *j);
bool jit_exit_status(jit_t *j, int *status);
void jit_reset_exit_status(jit_t *j);
void jit_add_tier(jit_t *j, int threshold, const jit_plugin_t *plugin);
ident_t jit_get_name(jit_t *j, jit_handle_t handle);
object_t *jit_get_object(jit_t *j, jit_handle_t handle);
bool jit_func_signal_only(jit_t *j, jit_handle_t handle);
void jit_register_native_plugin(jit_t *j);
void jit_register_llvm_plugin(jit_t *j);
void jit_interrupt(jit_t *j, jit_irq_fn_t fn, void *ctx);
void jit_check_interrupt(jit_t *j);
void jit_reset(jit_t *j);
bool jit_is_shutdown(jit_t *j);
void jit_get_stats(jit_t *j, unsigned *nfuncs, unsigned *nirfuncs,
                   size_t *irbytes);
mir_context_t *jit_get_mir(jit_t *j);

tlab_t *tlab_acquire(jit_t *j);
void tlab_release(jit_t *j, tlab_t *t);
bool jit_is_fused_proc(jit_t *j, ident_t name);
ident_t jit_last_island_proc(void);

void *jit_mspace_alloc(size_t size) RETURNS_NONNULL;
void *tlab_alloc(tlab_t *t, size_t size) RETURNS_NONNULL;
jit_stack_trace_t *jit_stack_trace(void);
int jit_stack_depth(void);

// Lowers the subprograms and other units that have not been called yet and
// are declared in a file that the function accepts, so that their statement
// sites exist
typedef bool (*jit_file_fn_t)(const char *file, void *ctx);
void jit_lower_file(jit_t *j, jit_file_fn_t fn, void *ctx);
jit_t *jit_for_thread(void);

typedef void *(*thunk_result_fn_t)(jit_scalar_t *, void *);

bool jit_try_call(jit_t *j, jit_handle_t handle, jit_scalar_t *result, ...);
bool jit_try_call_tlab(jit_t *j, jit_handle_t handle, tlab_t *tlab,
                       jit_scalar_t *result, ...);
jit_scalar_t jit_call(jit_t *j, jit_handle_t handle, ...);
void *jit_call_thunk(jit_t *j, vcode_unit_t unit, void *context,
                     thunk_result_fn_t fn, void *arg);
void *jit_call_thunk2(jit_t *j, mir_unit_t *mu, void *context,
                      thunk_result_fn_t fn, void *arg);
bool jit_fastcall(jit_t *j, jit_handle_t handle, jit_scalar_t *result,
                  jit_scalar_t p1, jit_scalar_t p2, tlab_t *tlab);
bool jit_vfastcall(jit_t *j, jit_handle_t handle, const jit_scalar_t *inargs,
                   unsigned nargs, jit_scalar_t *results, unsigned nresults,
                   tlab_t *tlab);
bool jit_call_closure(jit_t *j, ffi_closure_t *closure, jit_scalar_t *result,
                      jit_scalar_t arg, tlab_t *tlab);
bool jit_run_guarded(jit_t *j, void (*fn)(void *), void *ctx);
void jit_call_closure_guarded(jit_t *j, ffi_closure_t *closure,
                              jit_scalar_t *result, jit_scalar_t arg,
                              tlab_t *tlab);
void jit_fastcall_guarded(jit_t *j, jit_handle_t handle, jit_scalar_t *result,
                          jit_scalar_t p1, jit_scalar_t p2, tlab_t *tlab);

typedef void (*jit_fast_entry_fn_t)(void *, void *, jit_scalar_t *, tlab_t *);

typedef struct {
   void         *thread;
   void         *saved_anchor;
   jit_t        *saved_jit;
   jit_scalar_t *args;
   int           saved_state;
} jit_batch_ctx_t;

void jit_batch_begin(jit_t *j, jit_batch_ctx_t *ctx);
void jit_batch_end(const jit_batch_ctx_t *ctx);
void *jit_try_resolve_native(jit_t *j, jit_handle_t handle,
                             jit_fast_entry_fn_t *entry_out);

tlab_t jit_null_tlab(jit_t *j);

__attribute__((format(printf, 3, 4)))
void jit_msg(const loc_t *where, diag_level_t level, const char *fmt, ...);

__attribute__((noreturn))
void jit_abort(void);

__attribute__((noreturn))
void jit_abort_with_status(int code);

#endif   // _JIT_H
