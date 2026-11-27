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

#ifndef _RT_MODEL_H
#define _RT_MODEL_H

#include "prim.h"
#include "rt/defs.h"
#include "jit/jit.h"

typedef enum {
   WATCH_EVENT,
   WATCH_POSTPONED,
} watch_kind_t;

typedef enum {
   END_OF_INITIALISATION,
   START_OF_SIMULATION,
   START_OF_PROCESSES,
   END_OF_PROCESSES,
   START_OF_POSTPONED,
   LAST_KNOWN_DELTA_CYCLE,
   NEXT_TIME_STEP,
   END_TIME_STEP,
   NEXT_CYCLE,
   END_OF_SIMULATION,
} model_phase_t;

typedef enum {
   PSL_EVENT_COVER,
   PSL_EVENT_FAIL,
   PSL_EVENT_PASS,
} psl_event_t;

typedef enum {
   PSL_QUERY_COVERED,
   PSL_QUERY_ASSERT_COVERED,
   PSL_QUERY_FAILED,
} psl_query_t;

rt_model_t *model_new(jit_t *jit, cover_data_t *cover);
void model_free(rt_model_t *m);
void model_reset(rt_model_t *m);
void model_run(rt_model_t *m, uint64_t stop_time);
bool model_step(rt_model_t *m);
bool model_can_create_delta(rt_model_t *m);
bool model_in_reset(rt_model_t *m);
int64_t model_now(rt_model_t *m, unsigned *deltas);
bool model_rng_draw(uint64_t *r);
int64_t model_next_time(rt_model_t *m);
void model_stop(rt_model_t *m);
void model_finish(rt_model_t *m);
void model_interrupt(rt_model_t *m);
int model_exit_status(rt_model_t *m);

void model_psl_event(rt_model_t *m, psl_event_t event);
bool model_psl_status(rt_model_t *m, psl_query_t query);
void model_psl_clear(rt_model_t *m);
void model_psl_set_cover_assert(rt_model_t *m, bool enable);
bool model_psl_get_cover_assert(rt_model_t *m);

rt_watch_t *watch_new(rt_model_t *m, sig_event_fn_t fn, void *user,
                      watch_kind_t kind, unsigned slots);
void watch_free(rt_model_t *m, rt_watch_t *w);

void model_set_phase_cb(rt_model_t *m, model_phase_t phase, rt_event_fn_t fn,
                        void *user);
void model_set_event_cb(rt_model_t *m, rt_signal_t *s, int offset, int count,
                        rt_watch_t *w);
void watch_set_inline_island(rt_watch_t *w, bool enable);
rt_sampler_t *model_add_sampler(rt_model_t *m, rt_event_fn_t fn, void *user);
void model_remove_sampler(rt_model_t *m, rt_sampler_t *s);
void model_set_timeout_cb(rt_model_t *m, uint64_t when, rt_event_fn_t fn,
                          void *user);

void call_with_model(rt_model_t *m, void (*cb)(void *), void *arg);
void model_run_island_init(rt_model_t *m, rt_scope_t *s, vcode_unit_t thunk);

rt_model_t *get_model(void);
rt_model_t *get_model_or_null(void);
rt_proc_t *get_active_proc(void);

// Statement breakpoints: code generated with --debug-stmts calls the hook
// before a statement on a line with a breakpoint runs
typedef void (*stmt_hook_fn_t)(const char *file, int line, rt_proc_t *proc,
                               void *context);
void set_stmt_hook(stmt_hook_fn_t fn, void *context);
void stmt_break_add(const char *file, int line);
bool stmt_break_remove(const char *file, int line);
void stmt_step_set(bool enable);
bool stmt_step_process(void);

// Modes of stmt_step_depth_set
#define STMT_STEP_IN   0   // Next statement, also in called subprograms
#define STMT_STEP_OVER 1   // Next statement at the same or a lower depth
#define STMT_STEP_OUT  2   // Next statement at a lower depth
bool stmt_step_depth_set(int mode);
bool stmt_probe_where(loc_t *loc);
const jit_scalar_t *stmt_probe_frame_args(void);

// Result of stmt_site_query: bit set
#define STMT_SITE_EXACT 0x1   // There is a statement on the line
#define STMT_SITE_FILE  0x2   // There are statements in the file
int stmt_site_query(const char *file, int line, int *nearest);
uint32_t stmt_site_new(const char *file, int line);
rt_proc_t *model_defer_note(uint32_t *runs, uint32_t *seq);
bool model_defer_live(rt_proc_t *proc, uint32_t runs, uint32_t seq,
                      const int64_t *scopes, unsigned nscopes);
rt_scope_t *get_active_scope(rt_model_t *m);
cover_data_t *get_coverage(rt_model_t *m);

rt_scope_t *find_scope(rt_model_t *m, tree_t container);
rt_scope_t *find_scope_by_context(rt_model_t *m, void *ctx);
rt_scope_t *root_scope(rt_model_t *m);
rt_scope_t *child_scope(rt_scope_t *scope, tree_t decl);
rt_scope_t *child_scope_at(rt_scope_t *scope, int index);
rt_signal_t *find_signal(rt_scope_t *scope, tree_t decl, uint32_t *offset);
rt_proc_t *find_proc(rt_scope_t *scope, tree_t proc);
bool is_signal_scope(rt_scope_t *scope);
rt_scope_t *create_scope(rt_model_t *m, tree_t block, rt_scope_t *parent);

void get_instance_name(rt_scope_t *s, text_buf_t *tb);
void get_path_name(rt_scope_t *s, text_buf_t *tb);
void get_verilog_path_name(rt_scope_t *s, text_buf_t *tb);
bool scope_is_wrapper(const rt_scope_t *s);
rt_scope_t *scope_for_context(rt_scope_t *s, void *ctx);
void ename_free_cache(rt_model_t *m);

const void *signal_value(rt_signal_t *s);
const void *signal_last_value(rt_signal_t *s);
uint8_t signal_size(rt_signal_t *s);
uint32_t signal_width(rt_signal_t *s);
size_t signal_expand(rt_signal_t *s, uint64_t *buf, size_t max);
void force_signal(rt_model_t *m, rt_signal_t *s, const void *values,
                  int offset, size_t count);
void release_signal(rt_model_t *m, rt_signal_t *s, int offset, size_t count);
void release_signal_now(rt_model_t *m, rt_signal_t *s, int offset,
                        size_t count);
void deposit_signal(rt_model_t *m, rt_signal_t *s, const void *values,
                    int offset, size_t count);
void model_mark_sv_poll_mem_dirty(rt_model_t *m);
void sched_deposit(rt_model_t *m, rt_signal_t *s, const void *values,
                   int offset, size_t count, int64_t after, bool nonblock);
typedef enum {
   MIXED_BRIDGE_LOGIC,
   MIXED_BRIDGE_BIT,
   MIXED_BRIDGE_INT,
   MIXED_BRIDGE_REAL,
} mixed_bridge_kind_t;

sig_shared_t *model_mixed_signal_proxy(rt_model_t *m, rt_signal_t *sv_sig,
                                       bool sv_is_net,
                                       mixed_bridge_kind_t kind,
                                       bool sv_is_signed,
                                       uint8_t vhdl_size, tree_t where);
rt_watch_t *find_watch(rt_nexus_t *n, sig_event_fn_t fn);
bool signal_is_forced(rt_signal_t *s, int offset, size_t count);

// What drives a range of a signal, for debuggers.  Offsets and counts are in
// scalar elements from the start of the signal.
typedef enum {
   RT_CONN_DRIVER,    // A process drives the range
   RT_CONN_PORT,      // The value comes from another signal through a port
   RT_CONN_FORCE,     // The range is forced
   RT_CONN_DEPOSIT,   // A deposit is pending on the range
} rt_conn_kind_t;

typedef struct {
   rt_conn_kind_t  kind;
   rt_proc_t      *proc;        // RT_CONN_DRIVER
   rt_scope_t     *scope;       // Where the process is, or the instance that
                                // the port connection belongs to
   rt_scope_t     *other_scope; // RT_CONN_PORT: the signal on the other side
   ident_t         other_name;
   uint32_t        other_first; // The element of it that matches first
   uint32_t        other_width; // The number of elements of that signal
   uint32_t        first;       // The range of the signal that was asked for
   uint32_t        count;
   bool            approx;      // Found from the code, the range is the whole
                                // range asked for
   bool            collapsed;   // A port connection that the model has lost
   bool            from_child;  // The other side is in an instance below
} rt_conn_t;

typedef void (*rt_conn_fn_t)(const rt_conn_t *conn, void *ctx);

// A signal found by its name for a query
typedef struct {
   rt_signal_t *signal;    // Where the value is held
   uint32_t     offset;    // The first element of the item in the signal
   rt_scope_t  *scope;     // The scope that declares the item
   ident_t      name;
   bool         hop;       // An input port: a hop to the signal it is
                           // connected to is all there is
   rt_scope_t  *up_scope;  // For a port of a Verilog instance
   ident_t      up_name;   // the net or variable connected to it
} rt_found_t;

void model_nexus_drivers(rt_signal_t *s, uint32_t offset, uint32_t count,
                         rt_conn_fn_t fn, void *ctx);
void model_item_drivers(const rt_found_t *f, uint32_t first, uint32_t count,
                        rt_conn_fn_t fn, void *ctx);
const char *model_proc_kind(const rt_proc_t *p);

// The signals a process waits for and drives, as ranges of elements
typedef void (*rt_procsig_fn_t)(rt_signal_t *s, uint32_t first, uint32_t count,
                                bool write, void *ctx);

rt_proc_t *model_lookup_proc(rt_model_t *m, const char *scope_path,
                             const char *name);
rt_proc_t *model_lookup_proc_at(rt_model_t *m, const char *scope_path,
                                const char *file, unsigned line);
void model_process_signals(rt_model_t *m, rt_proc_t *p, rt_procsig_fn_t fn,
                           void *ctx);
bool model_lookup_signal(rt_model_t *m, const char *path, rt_found_t *out);
void model_scope_path(rt_scope_t *s, text_buf_t *tb);
void model_item_path(rt_scope_t *scope, ident_t name, text_buf_t *tb);
void get_forcing_value(rt_signal_t *s, uint8_t *value);
bool rt_model_has_init_event(const rt_model_t *m, const rt_signal_t *s);

void _std_standard_init(void);
void _std_env_init(void);
void _std_reflection_init(void);
void _file_io_init(void);
void _nvc_sim_pkg_init(void);

#endif  // _RT_MODEL_H
