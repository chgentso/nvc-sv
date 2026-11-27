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

#ifndef _JIT_EXITS_H
#define _JIT_EXITS_H

#include "prim.h"
#include "jit/jit.h"
#include "jit/jit-ffi.h"
#include "rt/defs.h"

#define WEAK __attribute__((weak))

void x_sched_process(int64_t delay);
void x_sched_inactive(void);
void x_drive_signal(sig_shared_t *ss, uint32_t offset, int32_t count);
sig_shared_t *x_init_signal(int64_t count, uint32_t size, jit_scalar_t value,
                            bool scalar, uint32_t flags, tree_t where,
                            int32_t offset);
void x_sched_waveform(sig_shared_t *ss, uint32_t offset, void *values,
                      int32_t count, int64_t after, int64_t reject);
void x_transfer_signal(sig_shared_t *target_ss, uint32_t toffset,
                       sig_shared_t *source_ss, uint32_t soffset,
                       int32_t count, int64_t after, int64_t reject);
int32_t x_test_net_event(sig_shared_t *ss, uint32_t offset, int32_t count);
int32_t x_test_net_active(sig_shared_t *ss, uint32_t offset,
                          int32_t count);
void x_sched_event(sig_shared_t *ss, uint32_t offset, int32_t count);
void x_sched_active(sig_shared_t *ss, uint32_t offset, int32_t count);
void x_stmt_probe(object_t *where, const jit_scalar_t *args);

// One byte per statement site of code generated with --debug-stmts: set if a
// breakpoint may match the statement, so the code takes the exit only then.
// Site zero belongs to every statement that has no site of its own and is
// always set.
#define JIT_STMT_SITES (1 << 20)
extern uint8_t jit_stmt_armed[JIT_STMT_SITES];
void x_alias_signal(sig_shared_t *ss, uint32_t offset, tree_t where);
void x_sched_waveform_s(sig_shared_t *ss, uint32_t offset, uint64_t scalar,
                        int64_t after, int64_t reject);
void x_sched_waveform_s0(sig_shared_t *ss, uint32_t offset, uint64_t scalar);
struct _jit_anchor;
void x_sched_waveform_zero(struct _jit_anchor *anchor, sig_shared_t *ss,
                           uint32_t offset, int32_t count, void *values);
void x_sched_waveform_zero_s(struct _jit_anchor *anchor, sig_shared_t *ss,
                             uint32_t offset, uint64_t scalar);
void slow_sched_waveform_0(struct _jit_anchor *anchor, sig_shared_t *shared,
                           uint32_t offset, int32_t count, void *values);
void slow_sched_waveform_s0(struct _jit_anchor *anchor, sig_shared_t *shared,
                            uint32_t offset, uint64_t scalar);
void x_file_open(int8_t *status, void **_fp, const uint8_t *name_bytes,
                 int32_t name_len, int8_t mode, int64_t elem_size);
void x_file_write(void **_fp, void *data, int64_t size, int64_t count);
int64_t x_file_read(void **_fp, void *data, int64_t size, int64_t count);
void x_index_fail(int64_t value, int64_t left, int64_t right, int8_t dir,
                  tree_t where, tree_t hint);
void x_length_fail(int64_t left, int64_t right, int32_t dim, tree_t where);
void x_range_fail(int64_t value, int64_t left, int64_t right, int8_t dir,
                  tree_t where, tree_t hint);
void x_exponent_fail(int64_t value, tree_t where);
void x_overflow(int64_t lhs, int64_t rhs, tree_t where);
void x_null_deref(tree_t where);
void x_div_zero(tree_t where);
void x_assert_fail(const uint8_t *msg, int32_t msg_len, int8_t severity,
                   int64_t hint_left, int64_t hint_right, int8_t hint_valid,
                   object_t *where);
void x_timing_check(int64_t stamp, int64_t limit, object_t *where);
void x_report(const uint8_t *msg, int32_t msg_len, int8_t severity,
              object_t *where);
int64_t x_last_event(sig_shared_t *ss, uint32_t offset, int32_t count);
int64_t x_last_active(sig_shared_t *ss, uint32_t offset, int32_t count);
void x_map_signal(sig_shared_t *src_ss, uint32_t src_offset,
                  sig_shared_t *dst_ss, uint32_t dst_offset, uint32_t count);
void x_map_const(sig_shared_t *ss, uint32_t offset,
                 const uint8_t *values, uint32_t count);
void x_push_scope(tree_t where, int32_t size, rt_scope_kind_t kind);
void x_pop_scope(void);
bool x_driving(sig_shared_t *ss, uint32_t offset, int32_t count);
void *x_driving_value(sig_shared_t *ss, uint32_t offset, int32_t count);
void x_disconnect(sig_shared_t *ss, uint32_t offset, int32_t count,
                  int64_t after, int64_t reject);
void x_force(sig_shared_t *ss, uint32_t offset, int32_t count, void *values);
void x_release(sig_shared_t *ss, uint32_t offset, int32_t count);
void x_deposit_signal(sig_shared_t *ss, uint32_t offset, int32_t count,
                      void *values);
void x_sched_deposit(sig_shared_t *ss, uint32_t offset, int32_t count,
                     void *values, int64_t after);
void x_put_driver(sig_shared_t *ss, uint32_t offset, int32_t count,
                  void *values);
sig_shared_t *x_init_packed(int64_t width, uint64_t abits, uint64_t bbits,
                            uint32_t flags, tree_t where, int32_t offset);
void x_deposit_packed(sig_shared_t *ss, uint32_t offset, int32_t count,
                      uint64_t abits, uint64_t bbits, uint32_t src_flags);
void x_sched_deposit_packed(sig_shared_t *ss, uint32_t offset, int32_t count,
                            uint64_t abits, uint64_t bbits, uint32_t src_flags,
                            int64_t after);
void x_put_driver_packed(sig_shared_t *ss, uint32_t offset, int32_t count,
                         uint64_t abits, uint64_t bbits, uint32_t src_flags);
void x_force_packed(sig_shared_t *ss, uint32_t offset, int32_t count,
                    uint64_t abits, uint64_t bbits, uint32_t src_flags);
void x_resolve_signal(sig_shared_t *ss, ffi_closure_t *closure, int32_t nlits,
                      int32_t flags);
void x_unreachable(tree_t where);
void x_cover_setup_toggle_cb(sig_shared_t *ss, uint32_t offset, int32_t count,
                             int32_t tag);
void x_cover_setup_state_cb(sig_shared_t *ss, int64_t low, int32_t tag);
void x_process_init(const ffi_closure_t *closure, tree_t where);
void x_clear_event(sig_shared_t *ss, uint32_t offset, int32_t count);
void x_enter_state(int32_t state, bool strong);
void *x_reflect_value(void *context, jit_scalar_t value, tree_t where,
                      const jit_scalar_t *bounds);
void *x_reflect_subtype(void *context, tree_t where,
                        const jit_scalar_t *bounds);
void *x_function_trigger(jit_handle_t handle, unsigned nargs,
                         const jit_scalar_t *args);
rt_trigger_t *x_or_trigger(rt_trigger_t *left, rt_trigger_t *right);
void *x_cmp_trigger(sig_shared_t *ss, uint32_t offset, int64_t right);
void *x_level_trigger(sig_shared_t *ss, uint32_t offset, int32_t count);
void x_add_trigger(void *ptr);
void x_bind_external(object_t *where, jit_handle_t scope, jit_scalar_t *result);
void x_instance_name(attr_kind_t kind, text_buf_t *tb);
void x_enable_trigger(rt_trigger_t *trigger);
void x_disable_trigger(rt_trigger_t *trigger);
int32_t *x_get_counters(jit_handle_t handle);

void x_sv_event_wait(void *event);
void x_sv_queue_wait(void *event);
int64_t x_sv_program_begin(void);
int64_t x_sv_reactive_yield(void);
int64_t x_sv_sva_yield(void);
int64_t x_sv_sva_defer_tick(void *sva, uint64_t leaves, int disable);
void x_sv_program_end(void);
void x_sv_event_trigger(void *event);
void x_sv_event_trigger_nba(void *event, int64_t after);
void x_sv_poll_watch(const void *ptr, unsigned size);
void x_sv_poll_watch_sig(sig_shared_t *ss, uint32_t offset, unsigned count);
void x_sv_poll_wait(void);
void x_sv_poll_fired(void);

struct _ffi_closure;
void *x_sv_island_begin(struct _ffi_closure *closure, uint32_t state_bytes,
                        uint32_t nsigs, uint32_t ndomains);
void x_sv_island_set_sparse(void *handle, uint32_t sparse_log_off,
                            uint32_t total_state_bytes);
void x_sv_island_sparse_write(void *isl_ptr, uint32_t sig_idx,
                              uint32_t byte_off, uint32_t len,
                              const void *data);
void x_sv_island_bind_sig(void *handle, uint32_t idx, sig_shared_t *ss,
                          int32_t sig_offset, uint32_t a_off, uint32_t b_off,
                          uint32_t width, uint32_t nelems, uint32_t flags);
void x_sv_island_bind_gen_sig(void *handle, uint32_t idx, void *owner_block,
                              void *decl, uint32_t a_off, uint32_t b_off,
                              uint32_t width, uint32_t nelems, uint32_t flags);
void x_sv_island_set_gen_key(void *handle, uint32_t idx, uint64_t key_hash);
void x_sv_island_attach_sig(void *handle, uint32_t idx, sig_shared_t *ss,
                            int32_t sig_offset);
void x_sv_island_apply_forces(void *handle);
void x_sv_island_add_domain(void *handle, uint32_t dom_idx,
                            struct _ffi_closure *ff_closure,
                            uint32_t clk_sig_idx, int32_t rst_sig_idx,
                            uint32_t edge_flags, uint32_t n_write_sigs);
void x_sv_island_add_domain_write(void *handle, uint32_t dom_idx,
                                  uint32_t sig_idx);
void x_sv_island_finish(void *handle);
void x_sv_island_set_variant_a(void *handle, struct _ffi_closure *closure,
                               struct _ffi_closure *closure_h,
                               uint32_t state_bytes, uint32_t hdr_bytes,
                               uint32_t sparse_log_off, uint32_t total_bytes);
void x_sv_island_bind_sig_a(void *handle, uint32_t idx, uint32_t a_off,
                            uint32_t a_flags, uint32_t b_bytes,
                            uint32_t x_off);
void x_sv_island_add_domain_a(void *handle, uint32_t dom_idx,
                              struct _ffi_closure *ff_closure,
                              struct _ffi_closure *ff_closure_h);
void x_sv_island_mark_ext_read(sig_shared_t *ss);
int64_t x_sv_event_triggered(void *event);
void x_sv_nba_store(void *dest, const void *value, int64_t bytes,
                    int64_t offset, int64_t count, int64_t after,
                    int64_t wide);
void x_sv_spawn(const ffi_closure_t *closure, void *group, int64_t label);
void x_sv_sched_postponed(bool late);
int64_t x_sv_sva_flushing(void);
void *x_sv_fork_begin(void);
void x_sv_fork_end(void *group);
int64_t x_sv_fork_wait(void *group, int64_t mode);
void x_sv_fork_disable(void);
int64_t x_sv_fork_disable_group(int64_t depth, int64_t label);
int64_t x_sv_fork_disabled(void);
void x_sv_wait_mark(int64_t label, int64_t first);
void x_sv_fork_name(void *group, int64_t label);
int64_t x_sv_fork_disable_name(int64_t label);
int64_t x_sv_defer_disable(int64_t label);
void x_sv_fork_finish(void);
void *x_sv_process_self(void);
int64_t x_sv_rng_get(void);
void x_sv_rng_set(int64_t state);
int64_t x_sv_rng_new(void);
int64_t x_sv_rng_fresh(void);
void x_sv_rng_seed(void *handle, int64_t seed);
int64_t x_sv_rng_state(void *handle);
void x_sv_rng_restore(void *handle, int64_t state);
int64_t x_sv_process_status(void *handle);
int64_t x_sv_process_kill(void *handle);
int64_t x_sv_process_await(void *handle);
int64_t x_sv_process_suspend(void *handle);
int64_t x_sv_process_resume(void *handle);
int64_t x_sv_fread(int64_t fd, int64_t nbytes);
int64_t x_sv_fread_count(void);
void x_sv_hier_scope(void *context);
void *x_sv_vif_child(void *context, int64_t label);
int64_t x_sv_class_oid(void);

#endif  // _JIT_EXITS_H
