//
//  Copyright (C) 2022-2026  Nick Gasson
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
#include "diag.h"
#include "jit/jit-exits.h"
#include "jit/jit-ffi.h"
#include "jit/jit-priv.h"
#include "jit/jit.h"
#include "lib.h"
#include "object.h"
#include "psl/psl-node.h"
#include "rt/assert.h"
#include "rt/defs.h"
#include "rt/mspace.h"
#include "rt/structs.h"
#include "type.h"
#include "tree.h"
#include "vlog/vlog-node.h"
#include "vlog/vlog-number.h"
#include "vlog/vlog-util.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void x_index_fail(int64_t value, int64_t left, int64_t right, int8_t dir,
                  tree_t where, tree_t hint)
{
   type_t type = tree_type(hint);

   LOCAL_TEXT_BUF tb = tb_new();
   tb_cat(tb, "index ");
   to_string(tb, type, value);
   tb_printf(tb, " outside of %pT range ", type);
   to_string(tb, type, left);
   tb_cat(tb, dir == RANGE_TO ? " to " : " downto ");
   to_string(tb, type, right);

   jit_msg(tree_loc(where), DIAG_FATAL, "%s", tb_get(tb));
}

void x_length_fail(int64_t left, int64_t right, int32_t dim, tree_t where)
{
   const tree_kind_t kind = tree_kind(where);

   LOCAL_TEXT_BUF tb = tb_new();
   switch (kind) {
   case T_PORT_DECL:
   case T_GENERIC_DECL:
   case T_PARAM_DECL:
   case T_PARAM:
      tb_cat(tb, "actual");
      break;
   case T_CASE:
   case T_MATCH_CASE:
      tb_cat(tb, "expression");
      break;
   case T_ASSOC:
      tb_cat(tb, "choice");
      break;
   case T_AGGREGATE:
      tb_cat(tb, "aggregate");
      break;
   case T_EXTERNAL_NAME:
      {
         tree_t last = tree_part(where, tree_parts(where) - 1);
         tb_printf(tb, "object %s", istr(tree_ident(last)));
      }
      break;
   default:
      tb_cat(tb, "value");
      break;
   }

   tb_printf(tb, " length %"PRIi64, right);
   if (dim > 0)
      tb_printf(tb, " for dimension %d", dim);
   tb_cat(tb, " does not match ");

   switch (kind) {
   case T_PORT_DECL:
      tb_printf(tb, "port %s", istr(tree_ident(where)));
      break;
   case T_PARAM_DECL:
      tb_printf(tb, "parameter %s", istr(tree_ident(where)));
      break;
   case T_GENERIC_DECL:
      tb_printf(tb, "generic %s", istr(tree_ident(where)));
      break;
   case T_VAR_DECL:
      tb_printf(tb, "variable %s", istr(tree_ident(where)));
      break;
   case T_CONST_DECL:
      tb_printf(tb, "constant %s", istr(tree_ident(where)));
      break;
   case T_SIGNAL_DECL:
      tb_printf(tb, "signal %s", istr(tree_ident(where)));
      break;
   case T_REF:
      tb_printf(tb, "%s %s", class_str(class_of(where)),
                istr(tree_ident(where)));
      break;
   case T_FIELD_DECL:
      tb_printf(tb, "field %s", istr(tree_ident(where)));
      break;
   case T_ALIAS:
      tb_printf(tb, "alias %s", istr(tree_ident(where)));
      break;
   case T_CASE:
   case T_MATCH_CASE:
      tb_cat(tb, "case choice");
      break;
   case T_ASSOC:
      tb_cat(tb, "expected");
      break;
   case T_PARAM:
      tb_cat(tb, "formal");
      break;
   case T_EXTERNAL_NAME:
      tb_cat(tb, "external name subtype indication");
      break;
   case T_TYPE_CONV:
   case T_ATTR_REF:
   case T_AGGREGATE:
      tb_printf(tb, "subtype %pT", tree_type(where));
      break;
   default:
      tb_cat(tb, "target");
      break;
   }

   tb_printf(tb, " length %"PRIi64, left);

   jit_msg(tree_loc(where), DIAG_FATAL, "%s", tb_get(tb));
}

void x_dir_fail(range_kind_t value, range_kind_t dir, tree_t where)
{
   assert(tree_kind(where) == T_RANGE);
   assert(value == RANGE_TO || value == RANGE_DOWNTO);
   assert(dir == RANGE_TO || dir == RANGE_DOWNTO);

   const char *map[] = { "TO", "DOWNTO" };
   jit_msg(tree_loc(where), DIAG_FATAL, "range direction of slice %s does "
           "not match prefix %s", map[value], map[dir]);
}

void x_range_fail(int64_t value, int64_t left, int64_t right, int8_t dir,
                  tree_t where, tree_t hint)
{
   // Hint tree may be an array type conversion
   type_t type = type_elem_recur(tree_type(hint));

   LOCAL_TEXT_BUF tb = tb_new();
   tb_cat(tb, "value ");
   to_string(tb, type, value);
   tb_printf(tb, " outside of %pT range ", type);
   to_string(tb, type, left);
   tb_cat(tb, dir == RANGE_TO ? " to " : " downto ");
   to_string(tb, type, right);

   switch (tree_kind(hint)) {
   case T_SIGNAL_DECL:
   case T_CONST_DECL:
   case T_VAR_DECL:
   case T_REF:
      tb_printf(tb, " for %s %s", class_str(class_of(hint)),
                istr(tree_ident(hint)));
      break;
   case T_PORT_DECL:
      tb_printf(tb, " for port %s", istr(tree_ident(hint)));
      break;
   case T_PARAM_DECL:
      tb_printf(tb, " for parameter %s", istr(tree_ident(hint)));
      break;
   case T_GENERIC_DECL:
      tb_printf(tb, " for generic %s", istr(tree_ident(hint)));
      break;
   case T_ATTR_REF:
      tb_printf(tb, " for attribute '%s", istr(tree_ident(hint)));
      break;
   default:
      break;
   }

   jit_msg(tree_loc(where), DIAG_FATAL, "%s", tb_get(tb));
}

void x_exponent_fail(int64_t value, tree_t where)
{
   jit_msg(tree_loc(where), DIAG_FATAL, "negative exponent %"PRIi64
           " only allowed for floating-point types", value);
}

void x_overflow(int64_t lhs, int64_t rhs, tree_t where)
{
   if (tree_kind(where) == T_FCALL
       && (tree_flags(where) & TREE_F_WRAP_OVERFLOW))
      return;   // The result has already wrapped around

   LOCAL_TEXT_BUF tb = tb_new();
   if (tree_kind(where) == T_FCALL) {
      switch (tree_subkind(tree_ref(where))) {
      case S_ADD:
         tb_printf(tb, "%"PRIi64" + %"PRIi64, lhs, rhs);
         break;
      case S_MUL:
         tb_printf(tb, "%"PRIi64" * %"PRIi64, lhs, rhs);
         break;
      case S_SUB:
         tb_printf(tb, "%"PRIi64" - %"PRIi64, lhs, rhs);
         break;
      case S_NEGATE:
         tb_printf(tb, "-(%"PRIi64")", lhs);
         break;
      case S_EXP:
         tb_printf(tb, "%"PRIi64" ** %"PRIi64, lhs, rhs);
         break;
      }
   }

   jit_msg(tree_loc(where), DIAG_FATAL,
           "result of %s cannot be represented as %pT",
           tb_get(tb), tree_type(where));
}

void x_null_deref(tree_t where)
{
   // A SystemVerilog method call or property access through a null
   // handle names the member (8.4)
   vlog_node_t v = vlog_from_object(tree_to_object(where));
   if (v != NULL) {
      vlog_node_t decl = NULL, cls = NULL;
      const char *what = "call to";
      switch (vlog_kind(v)) {
      case V_METHOD_CALL:
      case V_USER_FCALL:
      case V_USER_TCALL:
         decl = vlog_has_ref(v) ? vlog_ref(v) : NULL;
         break;
      case V_MEMBER_REF:
         decl = vlog_has_ref(v) ? vlog_ref(v) : NULL;
         cls = vlog_get_type(vlog_value(v));
         what = "property";
         break;
      default:
         break;
      }

      if (decl != NULL && cls == NULL) {
         switch (vlog_kind(decl)) {
         case V_FUNC_DECL:
         case V_TASK_DECL:
            if (vlog_has_ref(decl))
               cls = vlog_ref(decl);
            break;
         default:
            break;
         }
      }

      if (decl != NULL && vlog_has_ident(decl)) {
         LOCAL_TEXT_BUF tb = tb_new();
         if (cls != NULL && vlog_kind(cls) == V_CLASS_DECL) {
            const char *cname = istr(vlog_ident(cls));
            const char *at = strchr(cname, '@');
            tb_catn(tb, cname, at != NULL ? at - cname : strlen(cname));
            tb_cat(tb, "::");
         }
         tb_istr(tb, vlog_ident(decl));

         jit_msg(tree_loc(where), DIAG_FATAL, "null access: %s %s", what,
                 tb_get(tb));
         return;
      }
   }

   jit_msg(tree_loc(where), DIAG_FATAL, "null access dereference");
}

void x_div_zero(tree_t where)
{
   jit_msg(tree_loc(where), DIAG_FATAL, "division by zero");
}

void x_unreachable(tree_t where)
{
   if (where != NULL && tree_kind(where) == T_FUNC_BODY)
      jit_msg(tree_loc(where), DIAG_FATAL, "function %s did not return a value",
              istr(tree_ident(where)));
   else
      jit_msg(NULL, DIAG_FATAL, "executed unreachable instruction");
}

void x_func_wait(void)
{
   jit_stack_trace_t *trace = jit_stack_trace();
   tree_t inner = tree_from_object(trace->frames[0].object);
   free(trace);

   const char *what;
   switch (tree_kind(inner)) {
   case T_PROC_BODY: what = "call to protected type method"; break;
   case T_PROCESS: what = "process with sensitivity list"; break;
   default: what = "function call";
   }

   jit_msg(NULL, DIAG_FATAL, "cannot wait inside %s", what);
}

////////////////////////////////////////////////////////////////////////////////
// Entry point from interpreter or JIT compiled code

void fast_sched_waveform(jit_anchor_t *anchor, sig_shared_t *shared,
                         int32_t offset, int32_t count, jit_scalar_t value,
                         int64_t after, int64_t reject, bool scalar)
{
   jit_thread_local_t *thread = jit_attach_thread(anchor);

   if (scalar)
      x_sched_waveform_s(shared, offset, value.integer, after, reject);
   else
      x_sched_waveform(shared, offset, value.pointer, count,
                       after, reject);

   thread->anchor = NULL;
}

void fast_sched_waveform_s0(jit_anchor_t *anchor, sig_shared_t *shared,
                            int32_t offset, jit_scalar_t value)
{
   jit_thread_local_t *thread = jit_attach_thread(anchor);

   x_sched_waveform_s0(shared, offset, value.integer);

   thread->anchor = NULL;
}

void fast_sched_waveform_0(jit_anchor_t *anchor, sig_shared_t *shared,
                           int32_t offset, int32_t count, jit_scalar_t value)
{
   jit_thread_local_t *thread = jit_attach_thread(anchor);

   x_sched_waveform(shared, offset, value.pointer, count, 0, 0);

   thread->anchor = NULL;
}

DLLEXPORT
void __nvc_test_event(jit_anchor_t *anchor, jit_scalar_t *args, tlab_t *tlab)
{
   jit_thread_local_t *thread = jit_attach_thread(anchor);

   sig_shared_t *shared = args[0].pointer;
   int32_t       offset = args[1].integer;
   int32_t       count  = args[2].integer;

   args[0].integer = x_test_net_event(shared, offset, count);

   thread->anchor = NULL;
}

void __nvc_last_event(jit_anchor_t *anchor, jit_scalar_t *args, tlab_t *tlab)
{
   jit_thread_local_t *thread = jit_attach_thread(anchor);

   sig_shared_t *shared = args[0].pointer;
   uint32_t      offset = args[1].integer;
   uint32_t      count  = args[2].integer;

   args[0].integer = x_last_event(shared, offset, count);

   thread->anchor = NULL;
}

void fast_sched_process(jit_anchor_t *anchor, int64_t after)
{
   jit_thread_local_t *thread = jit_attach_thread(anchor);

   x_sched_process(after);

   thread->anchor = NULL;
}

void fast_drive_signal(jit_anchor_t *anchor, sig_shared_t *shared,
                       int32_t offset, int32_t count)
{
   jit_thread_local_t *thread = jit_attach_thread(anchor);

   x_drive_signal(shared, offset, count);

   thread->anchor = NULL;
}

void fast_sched_event(jit_anchor_t *anchor, sig_shared_t *shared,
                      int32_t offset, int32_t count)
{
   jit_thread_local_t *thread = jit_attach_thread(anchor);

   x_sched_event(shared, offset, count);

   thread->anchor = NULL;
}

void fast_sched_active(jit_anchor_t *anchor, sig_shared_t *shared,
                       int32_t offset, int32_t count)
{
   jit_thread_local_t *thread = jit_attach_thread(anchor);

   x_sched_active(shared, offset, count);

   thread->anchor = NULL;
}

void fast_clear_event(jit_anchor_t *anchor, sig_shared_t *shared,
                      int32_t offset, int32_t count)
{
   jit_thread_local_t *thread = jit_attach_thread(anchor);

   x_clear_event(shared, offset, count);

   thread->anchor = NULL;
}

void fast_enable_trigger(jit_anchor_t *anchor, rt_trigger_t *trigger)
{
   jit_thread_local_t *thread = jit_attach_thread(anchor);

   x_enable_trigger(trigger);

   thread->anchor = NULL;
}

void fast_disable_trigger(jit_anchor_t *anchor, rt_trigger_t *trigger)
{
   jit_thread_local_t *thread = jit_attach_thread(anchor);

   x_disable_trigger(trigger);

   thread->anchor = NULL;
}

void fast_deposit_signal(jit_anchor_t *anchor, sig_shared_t *shared,
                         int32_t offset, int32_t count, jit_scalar_t value,
                         bool scalar)
{
   jit_thread_local_t *thread = jit_attach_thread(anchor);

   if (scalar)
      x_deposit_signal(shared, offset, count, &value.integer);
   else
      x_deposit_signal(shared, offset, count, value.pointer);

   thread->anchor = NULL;
}

void fast_sched_deposit(jit_anchor_t *anchor, sig_shared_t *shared,
                        int32_t offset, int32_t count, jit_scalar_t value,
                        bool scalar, int64_t after)
{
   jit_thread_local_t *thread = jit_attach_thread(anchor);

   if (scalar)
      x_sched_deposit(shared, offset, count, &value.integer, after);
   else
      x_sched_deposit(shared, offset, count, value.pointer, after);

   thread->anchor = NULL;
}

void fast_put_driver(jit_anchor_t *anchor, sig_shared_t *shared,
                     int32_t offset, int32_t count, jit_scalar_t value,
                     bool scalar)
{
   jit_thread_local_t *thread = jit_attach_thread(anchor);

   if (scalar)
      x_put_driver(shared, offset, count, &value.integer);
   else
      x_put_driver(shared, offset, count, value.pointer);

   thread->anchor = NULL;
}

void fast_sched_inactive(jit_anchor_t *anchor)
{
   jit_thread_local_t *thread = jit_attach_thread(anchor);

   x_sched_inactive();

   thread->anchor = NULL;
}

__attribute__((cold, noinline))
static void exit_instance_name(jit_scalar_t *args, tlab_t *tlab)
{
   const attr_kind_t kind = args[0].integer;

   LOCAL_TEXT_BUF tb = tb_new();
   x_instance_name(kind, tb);
   ffi_return_string(tb_get(tb), args, tlab);
}

void fast_length_fail(jit_anchor_t *anchor, int64_t left, int64_t right,
                      int32_t dim, tree_t where)
{
   jit_thread_local_t *thread = jit_attach_thread(anchor);

   x_length_fail(left, right, dim, where);

   thread->anchor = NULL;
}

void fast_index_fail(jit_anchor_t *anchor, int64_t value, int64_t left,
                     int64_t right, int8_t dir, tree_t where, tree_t hint)
{
   jit_thread_local_t *thread = jit_attach_thread(anchor);

   x_index_fail(value, left, right, dir, where, hint);

   thread->anchor = NULL;
}

DLLEXPORT
void __nvc_do_exit(jit_exit_t which, jit_anchor_t *anchor, jit_scalar_t *args,
                   tlab_t *tlab)
{
   jit_thread_local_t *thread = jit_attach_thread(anchor);

   switch (which) {
   case JIT_EXIT_ASSERT_FAIL:
      {
         uint8_t  *msg        = args[0].pointer;
         int32_t   len        = args[1].integer;
         int32_t   severity   = args[2].integer;
         int64_t   hint_left  = args[3].integer;
         int64_t   hint_right = args[4].integer;
         int8_t    hint_valid = args[5].integer;
         object_t *where      = args[6].pointer;

         x_assert_fail(msg, len, severity, hint_left, hint_right,
                       hint_valid, where);
      }
      break;

   case JIT_EXIT_REPORT:
      {
         uint8_t  *msg      = args[0].pointer;
         int32_t   len      = args[1].integer;
         int32_t   severity = args[2].integer;
         object_t *where    = args[3].pointer;

         x_report(msg, len, severity, where);
      }
      break;

   case JIT_EXIT_TCHECK:
      {
         int64_t   stamp = args[0].integer;
         int64_t   limit = args[1].integer;
         object_t *where = args[2].pointer;

         x_timing_check(stamp, limit, where);
      }
      break;

   case JIT_EXIT_INIT_SIGNAL:
      {
         int64_t      count  = args[0].integer;
         int32_t      size   = args[1].integer;
         jit_scalar_t value  = { .integer = args[2].integer };
         int32_t      flags  = args[3].integer;
         tree_t       where  = args[4].pointer;
         int32_t      offset = args[5].integer;
         bool         scalar = args[6].integer;

         args[0].pointer = x_init_signal(count, size, value, scalar,
                                         flags, where, offset);
      }
      break;

   case JIT_EXIT_RESOLVE_SIGNAL:
      {
         sig_shared_t *shared = args[0].pointer;

         ffi_closure_t *closure = *(ffi_closure_t **)(args[1].pointer + 0);
         int32_t        nlits   = *(int64_t *)(args[1].pointer + 8);
         int32_t        flags   = *(int32_t *)(args[1].pointer + 16);

         x_resolve_signal(shared, closure, nlits, flags);
      }
      break;

   case JIT_EXIT_DRIVE_SIGNAL:
      {
         sig_shared_t *ss     = args[0].pointer;
         int32_t       offset = args[1].integer;
         int32_t       count  = args[2].integer;

         x_drive_signal(ss, offset, count);
      }
      break;

   case JIT_EXIT_TRANSFER_SIGNAL:
      {
         sig_shared_t *target    = args[0].pointer;
         int32_t       toffset   = args[1].integer;
         sig_shared_t *source    = args[2].pointer;
         int32_t       soffset   = args[3].integer;
         int32_t       count     = args[4].integer;
         int64_t       after     = args[5].integer;
         int64_t       reject    = args[6].integer;

         x_transfer_signal(target, toffset, source, soffset,
                           count, after, reject);
      }
      break;

   case JIT_EXIT_MAP_SIGNAL:
      {
         sig_shared_t  *src_ss     = args[0].pointer;
         uint32_t       src_offset = args[1].integer;
         sig_shared_t  *dst_ss     = args[2].pointer;
         uint32_t       dst_offset = args[3].integer;
         uint32_t       count      = args[4].integer;

         x_map_signal(src_ss, src_offset, dst_ss, dst_offset, count);
      }
      break;

   case JIT_EXIT_MAP_CONST:
      {
         sig_shared_t *dst_ss     = args[0].pointer;
         uint32_t      dst_offset = args[1].integer;
         uint32_t      dst_count  = args[3].integer;
         bool          scalar     = args[4].integer;

         const void *vptr = scalar ? &args[2].integer : args[2].pointer;

         x_map_const(dst_ss, dst_offset, vptr, dst_count);
      }
      break;

   case JIT_EXIT_SCHED_PROCESS:
      {
         int64_t after = args[0].integer;

         x_sched_process(after);
      }
      break;

   case JIT_EXIT_SCHED_WAVEFORM:
      {
         sig_shared_t *shared = args[0].pointer;
         int32_t       offset = args[1].integer;
         int32_t       count  = args[2].integer;
         jit_scalar_t  value  = { .integer = args[3].integer };
         int64_t       after  = args[4].integer;
         int64_t       reject = args[5].integer;
         bool          scalar = args[6].integer;

         if (scalar)
            x_sched_waveform_s(shared, offset, value.integer, after, reject);
         else
            x_sched_waveform(shared, offset, value.pointer, count,
                             after, reject);
      }
      break;

   case JIT_EXIT_SCHED_ZERO:
      {
         sig_shared_t *shared = args[0].pointer;
         int32_t       offset = args[1].integer;
         int32_t       count  = args[2].integer;
         jit_scalar_t  value  = { .integer = args[3].integer };
         bool          scalar = args[4].integer;

         if (scalar)
            x_sched_waveform_s(shared, offset, value.integer, 0, 0);
         else
            x_sched_waveform(shared, offset, value.pointer, count, 0, 0);
      }
      break;

   case JIT_EXIT_SCHED_INACTIVE:
      x_sched_inactive();
      break;

   case JIT_EXIT_SCHED_EVENT:
      {
         sig_shared_t *shared  = args[0].pointer;
         int32_t       offset  = args[1].integer;
         int32_t       count   = args[2].integer;

         x_sched_event(shared, offset, count);
      }
      break;

   case JIT_EXIT_SCHED_ACTIVE:
      {
         sig_shared_t *shared  = args[0].pointer;
         int32_t       offset  = args[1].integer;
         int32_t       count   = args[2].integer;

         x_sched_active(shared, offset, count);
      }
      break;

   case JIT_EXIT_STMT_PROBE:
      x_stmt_probe(args[0].pointer, args);
      break;

   case JIT_EXIT_ENABLE_TRIGGER:
      {
         rt_trigger_t *trigger = args[0].pointer;
         x_enable_trigger(trigger);
      }
      break;

   case JIT_EXIT_DISABLE_TRIGGER:
      {
         rt_trigger_t *trigger = args[0].pointer;
         x_disable_trigger(trigger);
      }
      break;

   case JIT_EXIT_ALIAS_SIGNAL:
      {
         sig_shared_t *ss     = args[0].pointer;
         uint32_t      offset = args[1].integer;
         tree_t        where  = args[2].pointer;

         x_alias_signal(ss, offset, where);
      }
      break;

   case JIT_EXIT_DISCONNECT:
      {
         sig_shared_t *shared = args[0].pointer;
         int32_t       offset = args[1].integer;
         int32_t       count  = args[2].integer;
         int64_t       reject = args[3].integer;
         int64_t       after  = args[4].integer;

         x_disconnect(shared, offset, count, after, reject);
      }
      break;

   case JIT_EXIT_UNREACHABLE:
      {
         tree_t where = args[0].pointer;
         x_unreachable(where);
      }
      break;

   case JIT_EXIT_OVERFLOW:
      {
         int64_t lhs   = args[0].integer;
         int64_t rhs   = args[1].integer;
         tree_t  where = args[2].pointer;

         x_overflow(lhs, rhs, where);
      }
      break;

   case JIT_EXIT_INDEX_FAIL:
      {
         int64_t      value = args[0].integer;
         int64_t      left  = args[1].integer;
         int64_t      right = args[2].integer;
         range_kind_t dir   = args[3].integer;
         tree_t       where = args[4].pointer;
         tree_t       hint  = args[5].pointer;

         x_index_fail(value, left, right, dir, where, hint);
      }
      break;

   case JIT_EXIT_RANGE_FAIL:
      {
         int64_t      value = args[0].integer;
         int64_t      left  = args[1].integer;
         int64_t      right = args[2].integer;
         range_kind_t dir   = args[3].integer;
         tree_t       where = args[4].pointer;
         tree_t       hint  = args[5].pointer;

         x_range_fail(value, left, right, dir, where, hint);
      }
      break;

   case JIT_EXIT_FORCE:
      {
         sig_shared_t *shared = args[0].pointer;
         int32_t       offset = args[1].integer;
         int32_t       count  = args[2].integer;
         bool          scalar = args[4].integer;

         if (scalar)
            x_force(shared, offset, count, &args[3].integer);
         else
            x_force(shared, offset, count, args[3].pointer);
      }
      break;

   case JIT_EXIT_RELEASE:
      {
         sig_shared_t *shared = args[0].pointer;
         int32_t       offset = args[1].integer;
         int32_t       count  = args[2].integer;

         x_release(shared, offset, count);
      }
      break;

   case JIT_EXIT_DEPOSIT_SIGNAL:
      {
         sig_shared_t *shared = args[0].pointer;
         int32_t       offset = args[1].integer;
         int32_t       count  = args[2].integer;
         bool          scalar = args[4].integer;

         if (scalar)
            x_deposit_signal(shared, offset, count, &args[3].integer);
         else
            x_deposit_signal(shared, offset, count, args[3].pointer);
      }
      break;

   case JIT_EXIT_SCHED_DEPOSIT:
      {
         sig_shared_t *shared = args[0].pointer;
         int32_t       offset = args[1].integer;
         int32_t       count  = args[2].integer;
         bool          scalar = args[4].integer;
         int64_t       after  = args[5].integer;

         if (scalar)
            x_sched_deposit(shared, offset, count, &args[3].integer, after);
         else
            x_sched_deposit(shared, offset, count, args[3].pointer, after);
      }
      break;

   case JIT_EXIT_PUT_DRIVER:
      {
         sig_shared_t *shared = args[0].pointer;
         int32_t       offset = args[1].integer;
         int32_t       count  = args[2].integer;
         bool          scalar = args[4].integer;

         if (scalar)
            x_put_driver(shared, offset, count, &args[3].integer);
         else
            x_put_driver(shared, offset, count, args[3].pointer);
      }
      break;

   case JIT_EXIT_PUSH_SCOPE:
      {
         tree_t          where = args[0].pointer;
         int32_t         size  = args[1].integer;
         rt_scope_kind_t kind  = args[2].integer;

         x_push_scope(where, size, kind);
      }
      break;

   case JIT_EXIT_POP_SCOPE:
      x_pop_scope();
      break;

   case JIT_EXIT_FUNC_WAIT:
      x_func_wait();
      break;

   case JIT_EXIT_DIV_ZERO:
      {
         tree_t where = args[0].pointer;
         x_div_zero(where);
      }
      break;

   case JIT_EXIT_LENGTH_FAIL:
      {
         int32_t left  = args[0].integer;
         int32_t right = args[1].integer;
         int32_t dim   = args[2].integer;
         tree_t  where = args[3].pointer;

         x_length_fail(left, right, dim, where);
      }
      break;

   case JIT_EXIT_DIR_FAIL:
      {
         range_kind_t value = args[0].integer;
         range_kind_t dir   = args[1].integer;
         tree_t       where = args[2].pointer;

         x_dir_fail(value, dir, where);
      }
      break;

   case JIT_EXIT_NULL_DEREF:
      {
         tree_t where = args[0].pointer;
         x_null_deref(where);
      }
      break;

   case JIT_EXIT_EXPONENT_FAIL:
      {
         int32_t value = args[0].integer;
         tree_t  where = args[1].pointer;

         x_exponent_fail(value, where);
      }
      break;

   case JIT_EXIT_FILE_OPEN:
      {
         int8_t   *status     = args[0].pointer;
         void    **_fp        = args[1].pointer;
         uint8_t  *name_bytes = args[2].pointer;
         int32_t   name_len   = args[3].integer;
         int32_t   mode       = args[4].integer;
         int64_t   elem_size  = args[5].integer;

         x_file_open(status, _fp, name_bytes, name_len, mode, elem_size);
      }
      break;

   case JIT_EXIT_FILE_READ:
      {
         void    **_fp   = args[0].pointer;
         uint8_t  *data  = args[1].pointer;
         int64_t   size  = args[2].integer;
         int64_t   count = args[3].integer;

         args[0].integer = x_file_read(_fp, data, size, count);
      }
      break;

   case JIT_EXIT_FILE_WRITE:
      {
         void   **_fp    = args[0].pointer;
         size_t   size   = args[2].integer;
         size_t   count  = args[3].integer;
         bool     scalar = args[4].integer;

         if (scalar)
            x_file_write(_fp, &args[1].integer, size, count);
         else
            x_file_write(_fp, args[1].pointer, size, count);
      }
      break;

   case JIT_EXIT_DEBUG_OUT:
      {
         int64_t value = args[0].integer;
         debugf("DEBUG %"PRIi64, value);
      }
      break;

   case JIT_EXIT_LAST_EVENT:
      __nvc_last_event(anchor, args, tlab);
      break;

   case JIT_EXIT_LAST_ACTIVE:
      {
         sig_shared_t *shared = args[0].pointer;
         uint32_t      offset = args[1].integer;
         uint32_t      count  = args[2].integer;

         args[0].integer = x_last_active(shared, offset, count);
      }
      break;

   case JIT_EXIT_TEST_EVENT:
      __nvc_test_event(anchor, args, tlab);
      break;

   case JIT_EXIT_TEST_ACTIVE:
      {
         sig_shared_t *shared = args[0].pointer;
         int32_t       offset = args[1].integer;
         int32_t       count  = args[2].integer;

         args[0].integer = x_test_net_active(shared, offset, count);
      }
      break;

   case JIT_EXIT_DRIVING:
      {
         sig_shared_t *shared = args[0].pointer;
         int32_t       offset = args[1].integer;
         int32_t       count  = args[2].integer;

         args[0].integer = x_driving(shared, offset, count);
      }
      break;

   case JIT_EXIT_DRIVING_VALUE:
      {
         sig_shared_t *shared = args[0].pointer;
         int32_t       offset = args[1].integer;
         int32_t       count  = args[2].integer;

         args[0].pointer = x_driving_value(shared, offset, count);
      }
      break;

   case JIT_EXIT_COVER_TOGGLE:
      {
         sig_shared_t *shared = args[0].pointer;
         uint32_t      offset = args[1].integer;
         int32_t       count  = args[2].integer;
         int32_t       tag    = args[3].integer;

         x_cover_setup_toggle_cb(shared, offset, count, tag);
      }
      break;

   case JIT_EXIT_COVER_STATE:
      {
         sig_shared_t *shared = args[0].pointer;
         int64_t      low     = args[1].integer;
         int32_t      tag     = args[2].integer;

         x_cover_setup_state_cb(shared, low, tag);
      }
      break;

   case JIT_EXIT_PROCESS_INIT:
      {
         ffi_closure_t *closure = args[0].pointer;
         tree_t         where   = args[1].pointer;

         x_process_init(closure, where);
      }
      break;

   case JIT_EXIT_CLEAR_EVENT:
      {
         sig_shared_t *shared = args[0].pointer;
         int32_t       offset = args[1].integer;
         int32_t       count  = args[2].integer;

         x_clear_event(shared, offset, count);
      }
      break;

   case JIT_EXIT_ENTER_STATE:
      {
         int32_t state  = args[0].integer;
         bool    strong = !!args[1].integer;

         x_enter_state(state, strong);
      }
      break;

   case JIT_EXIT_REFLECT_VALUE:
      {
         void         *context = args[0].pointer;
         jit_scalar_t  value   = args[1];
         tree_t        where   = args[2].pointer;

         args[0].pointer = x_reflect_value(context, value, where, args + 3);
      }
      break;

   case JIT_EXIT_REFLECT_SUBTYPE:
      {
         void   *context = args[0].pointer;
         tree_t  where   = args[1].pointer;

         args[0].pointer = x_reflect_subtype(context, where, args + 2);
      }
      break;

   case JIT_EXIT_FUNCTION_TRIGGER:
      {
         jit_handle_t handle = args[0].integer;
         unsigned     nargs  = args[1].integer;

         args[0].pointer = x_function_trigger(handle, nargs, args + 2);
      }
      break;

   case JIT_EXIT_OR_TRIGGER:
      {
         void *left  = args[0].pointer;
         void *right = args[1].pointer;

         args[0].pointer = x_or_trigger(left, right);
      }
      break;

   case JIT_EXIT_CMP_TRIGGER:
      {
         sig_shared_t *shared = args[0].pointer;
         int32_t       offset = args[1].integer;
         int64_t       right  = args[2].integer;

         args[0].pointer = x_cmp_trigger(shared, offset, right);
      }
      break;

   case JIT_EXIT_LEVEL_TRIGGER:
      {
         sig_shared_t *shared = args[0].pointer;
         int32_t       offset = args[1].integer;
         int32_t       count  = args[2].integer;

         args[0].pointer = x_level_trigger(shared, offset, count);
      }
      break;

   case JIT_EXIT_ADD_TRIGGER:
      {
         void *trigger = args[0].pointer;

         if (trigger != NULL)
            x_add_trigger(trigger);
      }
      break;

   case JIT_EXIT_BIND_FOREIGN:
      {
         const uint8_t *spec   = args[0].pointer;
         size_t         length = args[1].integer;
         tree_t         where  = args[2].pointer;
         tree_t         region = args[3].pointer;

         jit_bind_foreign(anchor->func, spec, length, where, region);
      }
      break;

   case JIT_EXIT_INSTANCE_NAME:
      exit_instance_name(args, tlab);
      break;

   case JIT_EXIT_BIND_EXTERNAL:
      {
         object_t    *where = args[0].pointer;
         jit_handle_t scope = args[1].integer;

         x_bind_external(where, scope, args);
      }
      break;

   case JIT_EXIT_SYSCALL:
      {
         vlog_node_t where = args[0].pointer;
         jit_do_syscall(where, anchor, args, tlab);
      }
      break;

   case JIT_EXIT_GET_COUNTERS:
      {
         jit_handle_t handle = args[0].integer;
         args[0].pointer = x_get_counters(handle);
      }
      break;

   default:
      fatal_trace("unhandled exit %s", jit_exit_name(which));
   }

   thread->anchor = NULL;
}

DLLEXPORT
void __nvc_vec4op(jit_vec_op_t op, jit_anchor_t *anchor, jit_scalar_t *args,
                  int size)
{
   jit_thread_local_t *thread = jit_attach_thread(anchor);

   const int nwords = (size + 63) / 64;

   if (size > 64 && op != JIT_VEC_EXTRACT) {
      // Two-state operands have no X/Z plane so supply an all-zero one
      uint64_t *zero = NULL;
      for (int i = 1; i < 6; i += 2) {
         if (args[i].pointer != NULL)
            continue;
         else if (op == JIT_VEC_TERNARY && i == 1)
            continue;
         else if ((op == JIT_VEC_ZEXT || op == JIT_VEC_SEXT)
                  && (i > 1 || args[2].integer <= 64))
            continue;
         else if (op == JIT_VEC_INSERT && i == 3 && args[4].integer <= 64)
            continue;
         else if (op == JIT_VEC_INSERT && i > 3)
            continue;
         else if (i == 5 && op != JIT_VEC_TERNARY)
            continue;

         if (zero == NULL) {
            zero = jit_mspace_alloc(nwords * sizeof(uint64_t));
            memset(zero, 0, nwords * sizeof(uint64_t));
         }
         args[i].pointer = zero;
      }
   }

   switch (op) {
   case JIT_VEC_CASE_EQ:
   case JIT_VEC_CASE_NEQ:
   case JIT_VEC_CASEX_EQ:
   case JIT_VEC_CASEZ_EQ:
   case JIT_VEC_LOG_EQ:
   case JIT_VEC_LOG_NEQ:
   case JIT_VEC_LOG_NOT:
   case JIT_VEC_LOG_AND:
   case JIT_VEC_LOG_OR:
   case JIT_VEC_ULT:
   case JIT_VEC_ULE:
   case JIT_VEC_UGT:
   case JIT_VEC_UGE:
   case JIT_VEC_SLT:
   case JIT_VEC_SLE:
   case JIT_VEC_SGT:
   case JIT_VEC_SGE:
   case JIT_VEC_AND1:
   case JIT_VEC_OR1:
   case JIT_VEC_XOR1:
      {
         assert(size > 64);

         const uint64_t *xa = args[0].pointer;
         const uint64_t *xb = args[1].pointer;
         const uint64_t *ya = args[2].pointer;
         const uint64_t *yb = args[3].pointer;

         int aresult, bresult;
         switch (op) {
         case JIT_VEC_CASE_EQ:
            aresult = memcmp(xa, ya, nwords * sizeof(uint64_t)) == 0
               && memcmp(xb, yb, nwords * sizeof(uint64_t)) == 0;
            bresult = 0;
            break;
         case JIT_VEC_CASE_NEQ:
            aresult = memcmp(xa, ya, nwords * sizeof(uint64_t)) != 0
               || memcmp(xb, yb, nwords * sizeof(uint64_t)) != 0;
            bresult = 0;
            break;
         case JIT_VEC_CASEX_EQ:
            aresult = 1;
            for (int i = 0; i < nwords; i++) {
               const uint64_t mask = xb[i] | yb[i];
               if ((xa[i] | mask) != (ya[i] | mask)) {
                  aresult = 0;
                  break;
               }
            }
            bresult = 0;
            break;
         case JIT_VEC_CASEZ_EQ:
            aresult = 1;
            for (int i = 0; i < nwords; i++) {
               const uint64_t zmask =
                  ((~xa[i]) & xb[i]) | ((~ya[i]) & yb[i]);
               if (((xa[i] | zmask) != (ya[i] | zmask))
                   || ((xb[i] & ~zmask) != (yb[i] & ~zmask))) {
                  aresult = 0;
                  break;
               }
            }
            bresult = 0;
            break;
         case JIT_VEC_LOG_EQ:
         case JIT_VEC_LOG_NEQ:
            {
               bool unknown = false, known_diff = false;
               for (int i = 0; i < nwords; i++) {
                  const uint64_t umask = xb[i] | yb[i];
                  unknown |= umask != 0;
                  known_diff |= ((xa[i] ^ ya[i]) & ~umask) != 0;
               }

               if (known_diff) {
                  aresult = op == JIT_VEC_LOG_NEQ;
                  bresult = 0;
               }
               else if (unknown) {
                  aresult = 1;
                  bresult = 1;
               }
               else {
                  aresult = op == JIT_VEC_LOG_EQ;
                  bresult = 0;
               }
            }
            break;
         case JIT_VEC_AND1:
         case JIT_VEC_OR1:
            {
               // A known bit decides the result whatever the others are
               const bool is_and = op == JIT_VEC_AND1;
               bool decided = false;
               for (int i = 0; i < nwords; i++) {
                  uint64_t valid = ~UINT64_C(0);
                  if (i == nwords - 1 && size % 64 != 0)
                     valid >>= 64 - size % 64;

                  const uint64_t known = ~xb[i] & valid;
                  decided |= is_and ? (known & ~xa[i]) != 0
                     : (known & xa[i]) != 0;
               }

               const bool unknown = vec2_or1(size, xb) != 0;
               if (decided) {
                  aresult = !is_and;
                  bresult = 0;
               }
               else if (unknown)
                  aresult = bresult = 1;
               else {
                  aresult = is_and;
                  bresult = 0;
               }
            }
            break;
         case JIT_VEC_XOR1:
            {
               int parity = 0;
               for (int i = 0; i < nwords; i++)
                  parity ^= __builtin_parityll(xa[i]);
               bresult = vec2_or1(size, xb);
               aresult = parity | bresult;
            }
            break;
         case JIT_VEC_LOG_NOT:
            {
               vlog_logic_t r = vec4_not(size, xa, xb);
               bresult = r >> 1;
               aresult = r & 1;
            }
            break;
         case JIT_VEC_LOG_AND:
            {
               vlog_logic_t r = vec4_logand(size, xa, xb, ya, yb);
               bresult = r >> 1;
               aresult = r & 1;
            }
            break;
         case JIT_VEC_LOG_OR:
            {
               vlog_logic_t r = vec4_logor(size, xa, xb, ya, yb);
               bresult = r >> 1;
               aresult = r & 1;
            }
            break;
         case JIT_VEC_ULT:
         case JIT_VEC_ULE:
         case JIT_VEC_UGT:
         case JIT_VEC_UGE:
         case JIT_VEC_SLT:
         case JIT_VEC_SLE:
         case JIT_VEC_SGT:
         case JIT_VEC_SGE:
            {
               vlog_logic_t r;
               switch (op) {
               case JIT_VEC_ULT:
                  r = vec4_lt(size, xa, xb, ya, yb);
                  break;
               case JIT_VEC_ULE:
                  r = vec4_le(size, xa, xb, ya, yb);
                  break;
               case JIT_VEC_UGT:
                  r = vec4_gt(size, xa, xb, ya, yb);
                  break;
               case JIT_VEC_UGE:
                  r = vec4_ge(size, xa, xb, ya, yb);
                  break;
               case JIT_VEC_SLT:
                  r = vec4_slt(size, xa, xb, ya, yb);
                  break;
               case JIT_VEC_SLE:
                  r = vec4_sle(size, xa, xb, ya, yb);
                  break;
               case JIT_VEC_SGT:
                  r = vec4_sgt(size, xa, xb, ya, yb);
                  break;
               case JIT_VEC_SGE:
                  r = vec4_sge(size, xa, xb, ya, yb);
                  break;
               default:
                  should_not_reach_here();
               }

               bresult = r >> 1;
               aresult = r & 1;
            }
            break;
         default:
            should_not_reach_here();
         }

         args[0].integer = aresult;
         args[1].integer = bresult;
      }
      break;
   case JIT_VEC_DEFINED:
      {
         assert(size > 64);

         const uint64_t *xb = args[0].pointer;
         bool result = true;
         for (int i = 0; i < nwords && result; i++)
            result = xb[i] == 0;

         args[0].integer = result;
      }
      break;
   case JIT_VEC_ZEXT:
   case JIT_VEC_SEXT:
      {
         assert(size > 64);

         void *ptr = jit_mspace_alloc(nwords * 2 * sizeof(uint64_t));
         uint64_t *aresult = ptr, *bresult = aresult + nwords;

         const int arg_size = args[2].integer;
         const int msb_word = (arg_size - 1) / 64;
         const uint64_t msb_mask = UINT64_C(1) << ((arg_size - 1) % 64);
         const bool sext = op == JIT_VEC_SEXT;

         if (arg_size <= 64) {
            const uint64_t aext =
               sext && (args[0].integer & msb_mask) ? ~UINT64_C(0) : 0;
            const uint64_t bext =
               sext && (args[1].integer & msb_mask) ? ~UINT64_C(0) : 0;

            aresult[0] = args[0].integer;
            bresult[0] = args[1].integer;

            if (arg_size < 64) {
               aresult[0] |= aext << arg_size;
               bresult[0] |= bext << arg_size;
            }

            for (int i = 1; i < nwords; i++) {
               aresult[i] = aext;
               bresult[i] = bext;
            }
         }
         else {
            const uint64_t *asrc = args[0].pointer;
            const uint64_t *bsrc = args[1].pointer;

            const uint64_t aext =
               sext && (asrc[msb_word] & msb_mask) ? ~UINT64_C(0) : 0;
            const uint64_t bext =
               sext && (bsrc[msb_word] & msb_mask) ? ~UINT64_C(0) : 0;

            const int full_words = arg_size / 64;
            const int partial_index = (arg_size % 64) != 0 ? full_words : -1;

            for (int i = 0; i < nwords; i++) {
               if (i < full_words) {
                  aresult[i] = asrc[i];
                  bresult[i] = bsrc[i];
               } else if (i == partial_index) {
                  const int shift = arg_size - i * 64;
                  aresult[i] = asrc[i] | (aext << shift);
                  bresult[i] = bsrc[i] | (bext << shift);
               } else {
                  aresult[i] = aext;
                  bresult[i] = bext;
               }
            }
         }

         if (size % 64 != 0) {
            const uint64_t mask = ~UINT64_C(0) >> (64 - size % 64);
            aresult[nwords - 1] &= mask;
            bresult[nwords - 1] &= mask;
         }

         args[0].pointer = aresult;
         args[1].pointer = bresult;
      }
      break;
   case JIT_VEC_INSERT:
      {
         assert(size > 64);

         void *ptr = jit_mspace_alloc(nwords * 2 * sizeof(uint64_t));
         uint64_t *aresult = ptr, *bresult = aresult + nwords;

         const uint64_t *afull = args[0].pointer;
         const uint64_t *bfull = args[1].pointer;

         memcpy(aresult, afull, nwords * sizeof(uint64_t));
         memcpy(bresult, bfull, nwords * sizeof(uint64_t));

         const int part_size = args[4].integer;
         const int pos       = args[5].integer;

         const int bitpos = size - part_size - pos;

         for (int i = 0; i < part_size; i += 64) {
            uint64_t apart, bpart;
            if (part_size <= 64) {
               apart = args[2].integer;
               bpart = args[3].integer;
            }
            else {
               apart = ((const uint64_t *)args[2].pointer)[i / 64];
               bpart = ((const uint64_t *)args[3].pointer)[i / 64];
            }

            const int opos = bitpos + i;
            const int nbits = MIN(64, part_size - i);

            uint64_t lmask = ~UINT64_C(0);
            if (nbits < 64)
               lmask >>= 64 - nbits;

            apart &= lmask;
            bpart &= lmask;

            const int word = opos / 64, shift = opos % 64;

            aresult[word] = (aresult[word] & ~(lmask << shift))
               | (apart << shift);
            bresult[word] = (bresult[word] & ~(lmask << shift))
               | (bpart << shift);

            if (shift + nbits > 64) {
               const uint64_t umask = lmask >> (64 - shift);

               aresult[word + 1] = (aresult[word + 1] & ~umask)
                  | (apart >> (64 - shift));
               bresult[word + 1] = (bresult[word + 1] & ~umask)
                  | (bpart >> (64 - shift));
            }
         }

         args[0].pointer = aresult;
         args[1].pointer = bresult;
      }
      break;
   case JIT_VEC_EXTRACT:
      {
         const int arg_size = args[2].integer;
         const int pos      = args[3].integer;

         assert(arg_size > 64);

         const int bitpos = arg_size - size - pos;
         const uint64_t *afull = args[0].pointer;
         const uint64_t *bfull = args[1].pointer;

         if (size <= 64) {
            const int word = bitpos / 64;
            const int shift = bitpos % 64;

            uint64_t abits = afull[word] >> shift;
            uint64_t bbits = bfull != NULL ? bfull[word] >> shift : 0;

            if (shift != 0 && size > 64 - shift) {
               abits |= afull[word + 1] << (64 - shift);
               if (bfull != NULL)
                  bbits |= bfull[word + 1] << (64 - shift);
            }

            if (size < 64) {
               const uint64_t mask = ~UINT64_C(0) >> (64 - size);
               abits &= mask;
               bbits &= mask;
            }

            args[0].integer = abits;
            args[1].integer = bbits;
         }
         else {
            void *ptr = jit_mspace_alloc(nwords * 2 * sizeof(uint64_t));
            uint64_t *aresult = ptr, *bresult = aresult + nwords;

            for (int i = 0; i < nwords; i++) {
               const int offset = bitpos + i * 64;
               const int word = offset / 64;
               const int shift = offset % 64;
               const int remain = size - i * 64;
               const int chunk = MIN(remain, 64);

               aresult[i] = afull[word] >> shift;
               bresult[i] = bfull != NULL ? bfull[word] >> shift : 0;

               if (shift != 0 && chunk > 64 - shift) {
                  aresult[i] |= afull[word + 1] << (64 - shift);
                  if (bfull != NULL)
                     bresult[i] |= bfull[word + 1] << (64 - shift);
               }

               if (chunk < 64) {
                  const uint64_t mask = ~UINT64_C(0) >> (64 - chunk);
                  aresult[i] &= mask;
                  bresult[i] &= mask;
               }
            }

            args[0].pointer = aresult;
            args[1].pointer = bresult;
         }
      }
      break;
   case JIT_VEC_TERNARY:
      {
         const uint64_t abits = args[0].integer & 1;
         const uint64_t bbits = args[1].integer & 1;

         const bool is_true = (abits & ~bbits) != 0;
         const bool is_false = (abits | bbits) == 0;

         void *ptr = jit_mspace_alloc(nwords * 2 * sizeof(uint64_t));
         uint64_t *aresult = ptr, *bresult = aresult + nwords;

         const uint64_t *atrue = args[2].pointer;
         const uint64_t *afalse = args[4].pointer;
         const uint64_t *btrue = args[3].pointer;
         const uint64_t *bfalse = args[5].pointer;

         if (is_true) {
            memcpy(aresult, atrue, nwords * sizeof(uint64_t));
            memcpy(bresult, btrue, nwords * sizeof(uint64_t));
         }
         else if (is_false) {
            memcpy(aresult, afalse, nwords * sizeof(uint64_t));
            memcpy(bresult, bfalse, nwords * sizeof(uint64_t));
         }
         else {
            for (int i = 0; i < nwords; i++) {
               const uint64_t diff =
                  (atrue[i] ^ afalse[i]) | (btrue[i] ^ bfalse[i]);
               aresult[i] = atrue[i] | diff;
               bresult[i] = btrue[i] | bfalse[i] | diff;
            }
         }

         args[0].pointer = aresult;
         args[1].pointer = bresult;
      }
      break;
   default:
      {
         uint64_t *aresult, *bresult, *a2, *b2;
         if (size > 64) {
            void *ptr = jit_mspace_alloc(nwords * 2 * sizeof(uint64_t));
            aresult = ptr;
            bresult = aresult + nwords;

            memcpy(aresult, args[0].pointer, nwords * sizeof(uint64_t));
            memcpy(bresult, args[1].pointer, nwords * sizeof(uint64_t));

            args[0].pointer = aresult;
            args[1].pointer = bresult;

            a2 = args[2].pointer;
            b2 = args[3].pointer;
         }
         else {
            aresult = (uint64_t *)&args[0].integer;
            bresult = (uint64_t *)&args[1].integer;
            a2 = (uint64_t *)&args[2].integer;
            b2 = (uint64_t *)&args[3].integer;
         }

         switch (op) {
         case JIT_VEC_AND2:
            vec4_and2(size, aresult, bresult, a2, b2);
            break;
         case JIT_VEC_OR2:
            vec4_or2(size, aresult, bresult, a2, b2);
            break;
         case JIT_VEC_XOR2:
            vec4_xor2(size, aresult, bresult, a2, b2);
            break;
         case JIT_VEC_ADD:
            vec4_add(size, aresult, bresult, a2, b2);
            break;
         case JIT_VEC_SUB:
            vec4_sub(size, aresult, bresult, a2, b2);
            break;
         case JIT_VEC_MUL:
            vec4_mul(size, aresult, bresult, a2, b2);
            break;
         case JIT_VEC_DIV:
            vec4_div(size, aresult, bresult, a2, b2);
            break;
         case JIT_VEC_MOD:
            vec4_mod(size, aresult, bresult, a2, b2);
            break;
         case JIT_VEC_SDIV:
            vec4_sdiv(size, aresult, bresult, a2, b2);
            break;
         case JIT_VEC_SMOD:
            vec4_smod(size, aresult, bresult, a2, b2);
            break;
         case JIT_VEC_SHL:
            vec4_shl(size, aresult, bresult, a2, b2);
            break;
         case JIT_VEC_SHR:
            vec4_shr(size, aresult, bresult, a2, b2);
            break;
         case JIT_VEC_ASR:
            vec4_asr(size, aresult, bresult, a2, b2);
            break;
         case JIT_VEC_NOT:
            vec4_inv(size, aresult, bresult);
            break;
         case JIT_VEC_EXP:
            vec4_exp(size, aresult, bresult, a2, b2);
            break;
         case JIT_VEC_SEXP:
            vec4_sexp(size, false, aresult, bresult, a2, b2);
            break;
         case JIT_VEC_SSEXP:
            vec4_sexp(size, true, aresult, bresult, a2, b2);
            break;
         case JIT_VEC_NEGATE:
            vec4_neg(size, aresult, bresult);
            break;
         default:
            should_not_reach_here();
         }
      }
      break;
   }

   thread->anchor = NULL;
}

////////////////////////////////////////////////////////////////////////////////
// Entry points from compiled code

DLLEXPORT
void _debug_out(intptr_t val, int32_t reg)
{
   printf("DEBUG: r%d val=%"PRIxPTR"\n", reg, val);
   fflush(stdout);
}

DLLEXPORT
void _debug_dump(const uint8_t *ptr, int32_t len)
{
   printf("---- %p ----\n", ptr);

   if (ptr != NULL) {
      for (int i = 0; i < len; i++)
         printf("%02x%c", ptr[i], (i % 8 == 7) ? '\n' : ' ');
      if (len % 8 != 0)
         printf("\n");
   }

   fflush(stdout);
}

DLLEXPORT
void *__nvc_mspace_alloc(uintptr_t size, jit_anchor_t *anchor)
{
   jit_thread_local_t *thread = jit_attach_thread(anchor);

   if (unlikely(size > UINT32_MAX)) {
      jit_msg(NULL, DIAG_FATAL, "attempting to allocate %zu byte object "
              "which is larger than the maximum supported %u bytes",
              size, UINT32_MAX);
      __builtin_unreachable();
   }
   else if (size == 0)
      size = 1;   // Never return a NULL pointer

   void *ptr = jit_mspace_alloc(size);

   thread->anchor = NULL;
   return ptr;
}

DLLEXPORT
void __nvc_putpriv(jit_handle_t handle, void *data)
{
   jit_t *j = jit_for_thread();
   jit_func_t *f = jit_get_func(j, handle);

   store_release(jit_get_privdata_ptr(j, f), data);
}

DLLEXPORT
object_t *__nvc_get_object(const char *unit, ptrdiff_t offset)
{
   return object_from_locus(ident_new(unit), offset, lib_load_handler);
}

static inline void pack_8bytes(const uint8_t *src, uint8_t *a_out,
                               uint8_t *b_out)
{
   // Gather bit zero and one of eight bytes into two bytes, the first byte
   // in the most significant position
   uint64_t word;
   memcpy(&word, src, sizeof(word));
   const uint64_t sa = word & UINT64_C(0x0101010101010101);
   const uint64_t sb = (word >> 1) & UINT64_C(0x0101010101010101);
   *a_out = (sa * UINT64_C(0x8040201008040201)) >> 56;
   *b_out = (sb * UINT64_C(0x8040201008040201)) >> 56;
}

DLLEXPORT
void __nvc_pack(const uint8_t *src, int32_t size, jit_scalar_t *args)
{
   if (size > 64) {
      const int nwords = (size + 63) / 64;
      uint64_t *mem = jit_mspace_alloc(nwords * 2 * sizeof(uint64_t));
      uint64_t *abits = mem, *bbits = mem + nwords;

      const int top = size % 64;
      if (top > 0) {
         uint64_t a = 0, b = 0;
         int i = 0;
         for (; i + 8 <= top; i += 8) {
            uint8_t a8, b8;
            pack_8bytes(src + i, &a8, &b8);
            a = (a << 8) | a8;
            b = (b << 8) | b8;
         }
         for (; i < top; i++) {
            a = (a << 1) | (src[i] & 1);
            b = (b << 1) | ((src[i] >> 1) & 1);
         }
         abits[nwords - 1] = a;
         bbits[nwords - 1] = b;
      }

      for (int w = size / 64 - 1, i = top; w >= 0; w--, i += 64) {
         uint64_t a = 0, b = 0;
         for (int j = 0; j < 64; j += 8) {
            uint8_t a8, b8;
            pack_8bytes(src + i + j, &a8, &b8);
            a = (a << 8) | a8;
            b = (b << 8) | b8;
         }
         abits[w] = a;
         bbits[w] = b;
      }

      args[0].pointer = abits;
      args[1].pointer = bbits;
   }
   else if (size == 1) {
      args[0].integer = src[0] & 1;
      args[1].integer = (src[0] >> 1) & 1;
   }
   else {
      uint64_t abits = 0, bbits = 0;
      int i = 0;

      for (; i + 8 <= size; i += 8) {
         uint8_t a8, b8;
         pack_8bytes(src + i, &a8, &b8);
         abits = (abits << 8) | a8;
         bbits = (bbits << 8) | b8;
      }

      for (; i < size; i++) {
         abits = (abits << 1) | (src[i] & 1);
         bbits = (bbits << 1) | ((src[i] >> 1) & 1);
      }

      args[0].integer = abits;
      args[1].integer = bbits;
   }
}

static inline uint64_t unpack_bits(uint64_t byte)
{
   // Spread the bits of a byte to the low bit of eight bytes with the most
   // significant bit in the first byte
   const uint64_t sel = (byte * UINT64_C(0x0101010101010101))
      & UINT64_C(0x0102040810204080);
   return ((sel + UINT64_C(0x7f7f7f7f7f7f7f7f)) >> 7)
      & UINT64_C(0x0101010101010101);
}

DLLEXPORT
void __nvc_unpack(jit_scalar_t aval, jit_scalar_t bval, jit_scalar_t *args)
{
   uint8_t *dest = args[0].pointer;
   size_t size = args[1].integer;
   uint8_t strength = args[2].integer;

   if (size > 64) {
      const uint64_t *abits = aval.pointer;
      const uint64_t *bbits = bval.pointer;
      const uint64_t str8 = strength * UINT64_C(0x0101010101010101);

      const int top = size % 64;
      if (top > 0) {
         const int nwords = (size + 63) / 64;
         const uint64_t aw = abits[nwords - 1];
         const uint64_t bw = bbits ? bbits[nwords - 1] : 0;
         int i = 0;
         for (; i + 8 <= top; i += 8) {
            const unsigned shift = top - 8 - i;
            const uint64_t a = unpack_bits((aw >> shift) & 0xff);
            const uint64_t b = unpack_bits((bw >> shift) & 0xff);
            const uint64_t out = a | (b << 1) | str8;
            memcpy(dest + i, &out, sizeof(out));
         }
         for (; i < top; i++) {
            const unsigned shift = top - 1 - i;
            dest[i] = ((aw >> shift) & 1) | (((bw >> shift) & 1) << 1)
               | strength;
         }
      }

      if (bbits != NULL) {
         for (int w = size / 64 - 1, i = top; w >= 0; w--, i += 64) {
            const uint64_t aw = abits[w], bw = bbits[w];
            for (int j = 0; j < 64; j += 8) {
               const unsigned shift = 56 - j;
               const uint64_t a = unpack_bits((aw >> shift) & 0xff);
               const uint64_t b = unpack_bits((bw >> shift) & 0xff);
               const uint64_t out = a | (b << 1) | str8;
               memcpy(dest + i + j, &out, sizeof(out));
            }
         }
      }
      else {
         for (int w = size / 64 - 1, i = top; w >= 0; w--, i += 64) {
            const uint64_t aw = abits[w];
            for (int j = 0; j < 64; j += 8) {
               const unsigned shift = 56 - j;
               const uint64_t a = unpack_bits((aw >> shift) & 0xff);
               const uint64_t out = a | str8;
               memcpy(dest + i + j, &out, sizeof(out));
            }
         }
      }
   }
   else if (size == 1) {
      dest[0] = (aval.integer & 1) | ((bval.integer & 1) << 1) | strength;
   }
   else {
      const uint64_t abits = aval.integer;
      const uint64_t bbits = bval.integer;
      const uint64_t str8 = strength * UINT64_C(0x0101010101010101);

      size_t i = 0;
      for (; i + 8 <= size; i += 8) {
         const unsigned shift = size - 8 - i;
         const uint64_t a = unpack_bits((abits >> shift) & 0xff);
         const uint64_t b = unpack_bits((bbits >> shift) & 0xff);
         const uint64_t out = a | (b << 1) | str8;
         memcpy(dest + i, &out, sizeof(out));
      }

      for (; i < size; i++) {
         const unsigned shift = size - 1 - i;
         dest[i] = ((abits >> shift) & 1) | (((bbits >> shift) & 1) << 1)
            | strength;
      }
   }
}
