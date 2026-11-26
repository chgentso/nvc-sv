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
#include "object.h"
#include "option.h"
#include "vlog/vlog-island.h"
#include "vlog/vlog-node.h"
#include "vlog/vlog-number.h"
#include "vlog/vlog-util.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static vlog_find_mod_fn_t island_find_mod = NULL;
static void *island_find_mod_ctx = NULL;
static vlog_node_t last_analyzed_body = NULL;
static bool last_analyzed_is_mixed = false;

void vlog_island_set_find_mod(vlog_find_mod_fn_t fn, void *ctx)
{
   island_find_mod = fn;
   island_find_mod_ctx = ctx;
   if (fn == NULL) {
      last_analyzed_body = NULL;
      last_analyzed_is_mixed = false;
   }
}

bool vlog_island_body_is_mixed(vlog_node_t body)
{
   return body != NULL && body == last_analyzed_body && last_analyzed_is_mixed;
}

typedef struct {
   vlog_node_t               decl;
   vlog_node_t               orig_decl;
   vlog_node_t               port;
   ident_t                   name;
   ident_t                   gen_path;
   ident_t                   orig_name;
   uint32_t                  width;
   uint32_t                  nelems;
   bool                      two_state;
   bool                      eligible;
   bool                      is_gen_local;
   bool                      is_external;
   bool                      is_inout;
   bool                      is_input_port;
   bool                      is_output_port;
   bool                      ext_read;
   bool                      ext_written;
   bool                      level_watched;
   uint32_t                  driver_count;
   island_sig_eject_reason_t eject_reason;
} sig_meta_t;

typedef struct {
   uint32_t sig_idx;
   uint64_t bit_lo;
   uint64_t bit_hi;
} sv_write_range_t;

typedef struct {
   vlog_node_t stmt;
   vlog_node_t orig_stmt;
} gen_proc_item_t;

typedef struct {
   vlog_node_t                stmt;
   vlog_node_t                orig_stmt;
   bool                       eligible;
   bool                       from_gen;
   island_proc_kind_t         kind;
   island_proc_eject_reason_t eject_reason;
   uint32_t                   node_count;
   A(uint32_t)                reads;
   A(uint32_t)                writes;
   A(sv_write_range_t)        write_ranges;
   int                        clk_sig_idx;   // Index into sig_metas[]
   bool                       clk_posedge;
   int                        rst_sig_idx;   // Index into sig_metas[] or -1
   bool                       rst_posedge;
   int                        island_idx;
} proc_meta_t;

typedef struct {
   vlog_node_t        body;
   bool               is_mixed;
   sig_meta_t        *sigs;
   uint32_t           nsigs;
   uint32_t           cap_sigs;
   hash_t            *decl_map;      // vlog_node_t -> (void *)(uintptr_t)(idx + 1)
   proc_meta_t       *procs;
   uint32_t           nprocs;
   uint32_t           cap_procs;
   A(gen_proc_item_t) gen_procs;
   A(vlog_node_t)     gen_ext_stmts;
   hset_t            *expanded_gens;
} analyze_ctx_t;

const char *vlog_island_sig_eject_str(island_sig_eject_reason_t r)
{
   switch (r) {
   case ISIG_EJ_NONE:             return "none";
   case ISIG_EJ_MULTI_DRIVER:     return "multi_driver";
   case ISIG_EJ_INOUT_PORT:       return "inout_port";
   case ISIG_EJ_NETTYPE:          return "nettype";
   case ISIG_EJ_ALIAS:            return "alias";
   case ISIG_EJ_FORCE_TARGET:     return "force_target";
   case ISIG_EJ_HIER_REF:         return "hier_ref";
   case ISIG_EJ_DYNAMIC_TYPE:     return "dynamic_type";
   case ISIG_EJ_REAL_TYPE:        return "real_type";
   case ISIG_EJ_MIXED_PORT:       return "mixed_vhdl_port";
   case ISIG_EJ_SHARED_EJECTED:   return "shared_ejected";
   case ISIG_EJ_UNSUPPORTED_TYPE: return "unsupported_type";
   default:                       return "unknown";
   }
}

const char *vlog_island_proc_eject_str(island_proc_eject_reason_t r)
{
   switch (r) {
   case IPROC_EJ_NONE:                return "none";
   case IPROC_EJ_INITIAL:             return "initial";
   case IPROC_EJ_FINAL:               return "final";
   case IPROC_EJ_DELAY:               return "delay";
   case IPROC_EJ_EVENT_OR_WAIT:       return "event_or_wait";
   case IPROC_EJ_FORK_JOIN:           return "fork_join";
   case IPROC_EJ_FORCE_RELEASE:       return "force_release";
   case IPROC_EJ_SVA:                 return "sva";
   case IPROC_EJ_SYSCALL:             return "syscall";
   case IPROC_EJ_UNSUPPORTED_STMT:    return "unsupported_stmt";
   case IPROC_EJ_UNSUPPORTED_EXPR:    return "unsupported_expr";
   case IPROC_EJ_HIER_REF:            return "hier_ref";
   case IPROC_EJ_DYNAMIC_OP:          return "dynamic_op";
   case IPROC_EJ_MULTI_CLOCK:         return "multi_clock";
   case IPROC_EJ_NON_RTL_SENSITIVITY: return "non_rtl_sensitivity";
   case IPROC_EJ_MIXED_ASSIGN_IN_FF:  return "mixed_assign_in_ff";
   case IPROC_EJ_LATCH:               return "latch";
   case IPROC_EJ_GATE_OR_UDP:         return "gate_or_udp";
   case IPROC_EJ_MULTI_DRIVER_TARGET: return "multi_driver_target";
   case IPROC_EJ_TOO_LARGE:           return "too_large";
   default:                           return "unknown";
   }
}

static vlog_node_t unwrap_decl(vlog_node_t d)
{
   if (d != NULL && vlog_kind(d) == V_PORT_DECL && vlog_has_ref(d))
      return vlog_ref(d);
   return d;
}

static int find_sig_idx(const analyze_ctx_t *ctx, vlog_node_t d)
{
   d = unwrap_decl(d);
   if (d == NULL)
      return -1;
   uintptr_t v = (uintptr_t)hash_get(ctx->decl_map, d);
   return v == 0 ? -1 : (int)(v - 1);
}

static void mark_sig_external(sig_meta_t *s, island_sig_eject_reason_t reason)
{
   s->is_external = true;
   if (s->eject_reason == ISIG_EJ_NONE)
      s->eject_reason = reason;
}

static bool sig_type_is_two_state(vlog_node_t type)
{
   if (type == NULL)
      return false;
   if (vlog_kind(type) == V_ENUM_DECL) {
      if (!vlog_has_type(type))
         return true;
      return sig_type_is_two_state(vlog_get_type(vlog_type(type)));
   }
   if (vlog_kind(type) == V_TYPE_DECL)
      return sig_type_is_two_state(vlog_type(type));
   return vlog_is_two_state(type);
}

static bool sig_type_is_nettype(vlog_node_t decl)
{
   if (vlog_kind(decl) == V_VAR_DECL && (vlog_flags(decl) & VLOG_F_USERNET))
      return true;
   if (!vlog_has_type(decl))
      return false;
   vlog_node_t t = vlog_type(decl);
   return vlog_kind(t) == V_TYPE_DECL && (vlog_flags(t) & VLOG_F_NETTYPE);
}

static bool check_sig_type_eligible(vlog_node_t decl, uint32_t *width_out,
                                    uint32_t *nelems_out, bool *two_state_out,
                                    island_sig_eject_reason_t *reason_out)
{
   if (!vlog_has_type(decl)) {
      *reason_out = ISIG_EJ_UNSUPPORTED_TYPE;
      return false;
   }

   if (vlog_is_real(decl)) {
      *reason_out = ISIG_EJ_REAL_TYPE;
      return false;
   }

   vlog_node_t t = vlog_type(decl);
   if (vlog_is_dynamic_type(t)) {
      *reason_out = ISIG_EJ_DYNAMIC_TYPE;
      return false;
   }

   // Unwrap typedefs to check underlying type kind
   vlog_node_t base = t;
   while (base != NULL && vlog_kind(base) == V_TYPE_DECL
          && vlog_has_type(base))
      base = vlog_type(base);

   if (base == NULL) {
      *reason_out = ISIG_EJ_UNSUPPORTED_TYPE;
      return false;
   }

   if (vlog_kind(base) == V_UNION_DECL && (vlog_flags(base) & VLOG_F_TAGGED)) {
      *reason_out = ISIG_EJ_UNSUPPORTED_TYPE;
      return false;
   }

   if (vlog_kind(base) == V_DATA_TYPE) {
      switch (vlog_subkind(base)) {
      case DT_IMPLICIT:
      case DT_LOGIC:
      case DT_INTEGER:
      case DT_BYTE:
      case DT_SHORTINT:
      case DT_INT:
      case DT_LONGINT:
      case DT_TIME:
      case DT_BIT:
         break;
      default:
         *reason_out = ISIG_EJ_UNSUPPORTED_TYPE;
         return false;
      }
   }

   const int nranges = vlog_ranges(decl);
   for (int i = 0; i < nranges; i++) {
      vlog_node_t r = vlog_range(decl, i);
      if (vlog_subkind(r) != V_DIM_UNPACKED || vlog_size(r) == 0) {
         *reason_out = ISIG_EJ_DYNAMIC_TYPE;
         return false;
      }
   }

   const unsigned w = vlog_size(t);
   const unsigned n = vlog_size(decl);
   if (w == 0 || n == 0) {
      *reason_out = ISIG_EJ_UNSUPPORTED_TYPE;
      return false;
   }

   *width_out = w;
   *nelems_out = n;
   *two_state_out = sig_type_is_two_state(t);
   return true;
}

static void collect_module_sigs(analyze_ctx_t *ctx)
{
   const int ndecls = vlog_decls(ctx->body);
   ctx->cap_sigs = MAX(16, ndecls);
   ctx->sigs = xcalloc_array(ctx->cap_sigs, sizeof(sig_meta_t));
   ctx->decl_map = hash_new(MAX(16, ndecls * 2));

   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(ctx->body, i);
      const vlog_kind_t k = vlog_kind(d);
      if (k != V_NET_DECL && k != V_VAR_DECL)
         continue;

      uint32_t idx = ctx->nsigs++;
      if (idx >= ctx->cap_sigs) {
         ctx->cap_sigs *= 2;
         ctx->sigs = xrealloc_array(ctx->sigs, ctx->cap_sigs, sizeof(sig_meta_t));
      }

      sig_meta_t *s = &(ctx->sigs[idx]);
      memset(s, 0, sizeof(*s));
      s->decl = d;
      s->name = vlog_ident(d);

      hash_put(ctx->decl_map, d, (void *)(uintptr_t)(idx + 1));

      if (sig_type_is_nettype(d)) {
         s->eligible = false;
         mark_sig_external(s, ISIG_EJ_NETTYPE);
      }
      else {
         island_sig_eject_reason_t r = ISIG_EJ_NONE;
         if (check_sig_type_eligible(d, &s->width, &s->nelems, &s->two_state,
                                     &r)) {
            s->eligible = true;
         }
         else {
            s->eligible = false;
            mark_sig_external(s, r);
         }
      }
   }

   const int nports = vlog_ports(ctx->body);
   for (int i = 0; i < nports; i++) {
      vlog_node_t ref = vlog_port(ctx->body, i);
      if (!vlog_has_ref(ref))
         continue;
      vlog_node_t p = vlog_ref(ref);
      if (vlog_kind(p) != V_PORT_DECL)
         continue;

      const int idx = find_sig_idx(ctx, p);
      if (idx < 0)
         continue;

      sig_meta_t *s = &(ctx->sigs[idx]);
      s->port = p;

      switch (vlog_subkind(p)) {
      case V_PORT_INPUT:
         s->is_input_port = true;
         break;
      case V_PORT_OUTPUT:
         s->is_output_port = true;
         break;
      case V_PORT_INOUT:
      case V_PORT_REF:
         s->is_inout = true;
         mark_sig_external(s, ISIG_EJ_INOUT_PORT);
         break;
      }

      if (ctx->is_mixed)
         mark_sig_external(s, ISIG_EJ_MIXED_PORT);
   }
}

typedef struct {
   analyze_ctx_t *ctx;
   bool           mark_ext_read;
   bool           mark_ext_written;
   bool           add_driver;
   island_sig_eject_reason_t force_reason;
   hset_t        *seen_once;
} ref_scan_t;

static void scan_refs_cb(vlog_node_t v, void *arg)
{
   ref_scan_t *rs = arg;
   const vlog_kind_t k = vlog_kind(v);

   if (k == V_HIER_REF && vlog_has_ref(v)) {
      vlog_node_t refd = unwrap_decl(vlog_ref(v));
      const int idx = find_sig_idx(rs->ctx, refd);
      if (idx >= 0) {
         mark_sig_external(&rs->ctx->sigs[idx], ISIG_EJ_HIER_REF);
         rs->ctx->sigs[idx].ext_read = true;
         rs->ctx->sigs[idx].ext_written = true;
      }
      else {
         for (uint32_t i = 0; i < rs->ctx->nsigs; i++) {
            if (rs->ctx->sigs[i].is_gen_local
                && rs->ctx->sigs[i].orig_decl == refd) {
               mark_sig_external(&rs->ctx->sigs[i], ISIG_EJ_HIER_REF);
               rs->ctx->sigs[i].ext_read = true;
               rs->ctx->sigs[i].ext_written = true;
            }
         }
      }
      return;
   }

   if (k != V_REF || !vlog_has_ref(v))
      return;

   const int idx = find_sig_idx(rs->ctx, vlog_ref(v));
   if (idx < 0)
      return;

   sig_meta_t *s = &(rs->ctx->sigs[idx]);
   if (rs->force_reason != ISIG_EJ_NONE)
      mark_sig_external(s, rs->force_reason);
   if (rs->mark_ext_read)
      s->ext_read = true;
   if (rs->mark_ext_written)
      s->ext_written = true;
   if (rs->add_driver) {
      if (rs->seen_once == NULL || !hset_contains(rs->seen_once, s->decl)) {
         if (rs->seen_once != NULL)
            hset_insert(rs->seen_once, s->decl);
         s->driver_count++;
      }
   }
}

static void scan_lvalue_refs(analyze_ctx_t *ctx, vlog_node_t lhs,
                             bool mark_ext_read, bool mark_ext_written,
                             bool add_driver, hset_t *driver_once)
{
   if (lhs == NULL)
      return;

   switch (vlog_kind(lhs)) {
   case V_REF:
      if (vlog_has_ref(lhs)) {
         const int idx = find_sig_idx(ctx, vlog_ref(lhs));
         if (idx >= 0) {
            sig_meta_t *s = &(ctx->sigs[idx]);
            if (mark_ext_written)
               s->ext_written = true;
            if (mark_ext_read)
               s->ext_read = true;
            if (add_driver && (driver_once == NULL
                               || !hset_contains(driver_once, s->decl))) {
               if (driver_once != NULL)
                  hset_insert(driver_once, s->decl);
               s->driver_count++;
            }
         }
      }
      break;
   case V_HIER_REF:
      if (vlog_has_ref(lhs)) {
         const int idx = find_sig_idx(ctx, vlog_ref(lhs));
         if (idx >= 0)
            mark_sig_external(&ctx->sigs[idx], ISIG_EJ_HIER_REF);
      }
      break;
   case V_BIT_SELECT:
      scan_lvalue_refs(ctx, vlog_value(lhs), mark_ext_read, mark_ext_written,
                       add_driver, driver_once);
      for (unsigned i = 0; i < vlog_params(lhs); i++) {
         ref_scan_t rs = { .ctx = ctx, .mark_ext_read = mark_ext_read };
         vlog_visit(vlog_param(lhs, i), scan_refs_cb, &rs);
      }
      break;
   case V_PART_SELECT:
      scan_lvalue_refs(ctx, vlog_value(lhs), mark_ext_read, mark_ext_written,
                       add_driver, driver_once);
      {
         ref_scan_t rs = { .ctx = ctx, .mark_ext_read = mark_ext_read };
         vlog_visit(vlog_left(lhs), scan_refs_cb, &rs);
         if (vlog_has_right(lhs))
            vlog_visit(vlog_right(lhs), scan_refs_cb, &rs);
      }
      break;
   case V_MEMBER_REF:
      scan_lvalue_refs(ctx, vlog_value(lhs), mark_ext_read, mark_ext_written,
                       add_driver, driver_once);
      break;
   case V_CONCAT:
      for (unsigned i = 0; i < vlog_params(lhs); i++)
         scan_lvalue_refs(ctx, vlog_param(lhs, i), mark_ext_read,
                          mark_ext_written, add_driver, driver_once);
      break;
   default:
      break;
   }
}

static void collect_stmt_writes_only(analyze_ctx_t *ctx, vlog_node_t stmt,
                                     hset_t *driver_once)
{
   switch (vlog_kind(stmt)) {
   case V_ASSIGN:
   case V_NBASSIGN:
   case V_BASSIGN:
   case V_OP_ASSIGN:
   case V_PREFIX:
   case V_POSTFIX:
      if (vlog_has_target(stmt))
         scan_lvalue_refs(ctx, vlog_target(stmt), false, false, true,
                          driver_once);
      break;
   case V_ALWAYS:
   case V_INITIAL:
   case V_FINAL:
   case V_SEQ_BLOCK:
   case V_TIMING:
   case V_FOREACH:
   case V_WHILE:
   case V_DO_WHILE:
   case V_REPEAT:
   case V_FOREVER:
   case V_FOR_LOOP:
   case V_CASE:
   case V_CASE_ITEM:
   case V_FORK:
      for (unsigned i = 0; i < vlog_stmts(stmt); i++)
         collect_stmt_writes_only(ctx, vlog_stmt(stmt, i), driver_once);
      if (vlog_kind(stmt) == V_FOR_LOOP) {
         if (vlog_has_left(stmt))
            collect_stmt_writes_only(ctx, vlog_left(stmt), driver_once);
         if (vlog_has_right(stmt))
            collect_stmt_writes_only(ctx, vlog_right(stmt), driver_once);
      }
      break;
   case V_FOR_INIT:
   case V_FOR_STEP:
      for (unsigned i = 0; i < vlog_stmts(stmt); i++)
         collect_stmt_writes_only(ctx, vlog_stmt(stmt, i), driver_once);
      break;
   case V_IF:
      for (unsigned i = 0; i < vlog_conds(stmt); i++) {
         vlog_node_t c = vlog_cond(stmt, i);
         for (unsigned j = 0; j < vlog_stmts(c); j++)
            collect_stmt_writes_only(ctx, vlog_stmt(c, j), driver_once);
      }
      break;
   default:
      break;
   }
}

static void scan_mutating_sysf_cb(vlog_node_t v, void *arg)
{
   analyze_ctx_t *ctx = arg;
   if (vlog_kind(v) != V_SYS_FCALL || !vlog_has_ident(v))
      return;

   ident_t id = vlog_ident(v);
   const unsigned np = vlog_params(v);
   if (id == ident_new("$fscanf") || id == ident_new("$sscanf")) {
      for (unsigned i = 2; i < np; i++)
         scan_lvalue_refs(ctx, vlog_param(v, i), false, true, false, NULL);
   }
   else if (id == ident_new("$value$plusargs")) {
      if (np >= 2)
         scan_lvalue_refs(ctx, vlog_param(v, 1), false, true, false, NULL);
   }
   else if (id == ident_new("$fgets") || id == ident_new("$fread")) {
      if (np >= 1)
         scan_lvalue_refs(ctx, vlog_param(v, 0), false, true, false, NULL);
   }
}

static void collect_stmt_writes_ext_rec(analyze_ctx_t *ctx, vlog_node_t stmt,
                                        hset_t *seen_tasks)
{
   switch (vlog_kind(stmt)) {
   case V_ASSIGN:
   case V_NBASSIGN:
   case V_BASSIGN:
   case V_OP_ASSIGN:
   case V_PREFIX:
   case V_POSTFIX:
      if (vlog_has_target(stmt))
         scan_lvalue_refs(ctx, vlog_target(stmt), false, true, false, NULL);
      break;
   case V_SYS_TCALL:
      if (vlog_has_ident(stmt)) {
         ident_t id = vlog_ident(stmt);
         const unsigned np = vlog_params(stmt);
         if (id == ident_new("$readmemh") || id == ident_new("$readmemb")
             || id == ident_new("$value$plusargs")) {
            if (np >= 2)
               scan_lvalue_refs(ctx, vlog_param(stmt, 1), false, true, false, NULL);
         }
         else if (id == ident_new("$fscanf") || id == ident_new("$sscanf")) {
            for (unsigned i = 2; i < np; i++)
               scan_lvalue_refs(ctx, vlog_param(stmt, i), false, true, false, NULL);
         }
         else if (id == ident_new("$fgets") || id == ident_new("$fread")
                  || id == ident_new("$sformat") || id == ident_new("$swrite")
                  || id == ident_new("$swriteb") || id == ident_new("$swriteo")
                  || id == ident_new("$swriteh")) {
            if (np >= 1)
               scan_lvalue_refs(ctx, vlog_param(stmt, 0), false, true, false, NULL);
         }
      }
      break;
   case V_USER_TCALL:
      if (vlog_has_ref(stmt) && vlog_kind(vlog_ref(stmt)) == V_TASK_DECL) {
         vlog_node_t task = vlog_ref(stmt);
         const unsigned nports = vlog_ports(task);
         const unsigned np = vlog_params(stmt);
         for (unsigned i = 0; i < np && i < nports; i++) {
            vlog_node_t p = vlog_port(task, i);
            if (vlog_kind(p) == V_TF_PORT_DECL
                && vlog_subkind(p) != V_PORT_INPUT)
               scan_lvalue_refs(ctx, vlog_param(stmt, i), false, true, false, NULL);
         }
         if (!hset_contains(seen_tasks, task)) {
            hset_insert(seen_tasks, task);
            for (unsigned i = 0; i < vlog_stmts(task); i++)
               collect_stmt_writes_ext_rec(ctx, vlog_stmt(task, i), seen_tasks);
         }
      }
      break;
   case V_ALWAYS:
   case V_INITIAL:
   case V_FINAL:
   case V_SEQ_BLOCK:
   case V_TIMING:
   case V_FOREACH:
   case V_WHILE:
   case V_DO_WHILE:
   case V_REPEAT:
   case V_FOREVER:
   case V_FOR_LOOP:
   case V_CASE:
   case V_CASE_ITEM:
   case V_FORK:
      for (unsigned i = 0; i < vlog_stmts(stmt); i++)
         collect_stmt_writes_ext_rec(ctx, vlog_stmt(stmt, i), seen_tasks);
      if (vlog_kind(stmt) == V_FOR_LOOP) {
         if (vlog_has_left(stmt))
            collect_stmt_writes_ext_rec(ctx, vlog_left(stmt), seen_tasks);
         if (vlog_has_right(stmt))
            collect_stmt_writes_ext_rec(ctx, vlog_right(stmt), seen_tasks);
      }
      break;
   case V_FOR_INIT:
   case V_FOR_STEP:
      for (unsigned i = 0; i < vlog_stmts(stmt); i++)
         collect_stmt_writes_ext_rec(ctx, vlog_stmt(stmt, i), seen_tasks);
      break;
   case V_IF:
      for (unsigned i = 0; i < vlog_conds(stmt); i++) {
         vlog_node_t c = vlog_cond(stmt, i);
         for (unsigned j = 0; j < vlog_stmts(c); j++)
            collect_stmt_writes_ext_rec(ctx, vlog_stmt(c, j), seen_tasks);
      }
      break;
   default:
      break;
   }
}

static void collect_stmt_writes_ext(analyze_ctx_t *ctx, vlog_node_t stmt)
{
   hset_t *seen_tasks = hset_new(8);
   collect_stmt_writes_ext_rec(ctx, stmt, seen_tasks);
   hset_free(seen_tasks);
   vlog_visit_only(stmt, scan_mutating_sysf_cb, ctx, V_SYS_FCALL);
}

static void prescan_stmt_drivers(analyze_ctx_t *ctx, vlog_node_t s,
                                 bool in_generate)
{
   const vlog_kind_t k = vlog_kind(s);
   switch (k) {
   case V_ASSIGN:
      if (in_generate) {
         ref_scan_t rs = { .ctx = ctx, .mark_ext_read = true };
         vlog_visit(s, scan_refs_cb, &rs);
         hset_t *once = hset_new(8);
         scan_lvalue_refs(ctx, vlog_target(s), false, true, true, once);
         hset_free(once);
      }
      else {
         hset_t *once = hset_new(8);
         scan_lvalue_refs(ctx, vlog_target(s), false, false, true, once);
         hset_free(once);
      }
      break;

   case V_ALWAYS:
      if (in_generate) {
         ref_scan_t rs = { .ctx = ctx, .mark_ext_read = true };
         vlog_visit(s, scan_refs_cb, &rs);
         collect_stmt_writes_ext(ctx, s);
         hset_t *once = hset_new(8);
         collect_stmt_writes_only(ctx, s, once);
         hset_free(once);
      }
      else {
         hset_t *once = hset_new(8);
         collect_stmt_writes_only(ctx, s, once);
         hset_free(once);
      }
      break;

   case V_INITIAL:
   case V_FINAL:
      {
         // Mark writes as external without inflating multi-driver counts
         ref_scan_t rs = {
            .ctx           = ctx,
            .mark_ext_read = true,
         };
         vlog_visit(s, scan_refs_cb, &rs);
         collect_stmt_writes_ext(ctx, s);
      }
      break;

   case V_GATE_INST:
      {
         if (vlog_subkind(s) == V_GATE_ALIAS) {
            hset_t *once = hset_new(8);
            ref_scan_t rs = {
               .ctx              = ctx,
               .mark_ext_read    = true,
               .mark_ext_written = true,
               .add_driver       = true,
               .force_reason     = ISIG_EJ_ALIAS,
               .seen_once        = once,
            };
            vlog_visit(s, scan_refs_cb, &rs);
            if (vlog_has_target(s))
               scan_lvalue_refs(ctx, vlog_target(s), true, true, true, once);
            hset_free(once);
            break;
         }

         hset_t *once = hset_new(8);
         if (vlog_has_target(s))
            scan_lvalue_refs(ctx, vlog_target(s), false, true, true, once);
         for (unsigned j = 0; j < vlog_params(s); j++) {
            vlog_node_t p = vlog_param(s, j);
            ref_scan_t rs = {
               .ctx              = ctx,
               .mark_ext_read    = true,
               .mark_ext_written = (vlog_subkind(s) >= V_GATE_TRAN
                                    && vlog_subkind(s) <= V_GATE_RTRANIF1),
               .add_driver       = (vlog_subkind(s) >= V_GATE_TRAN
                                    && vlog_subkind(s) <= V_GATE_RTRANIF1),
               .seen_once        = once,
            };
            vlog_visit(p, scan_refs_cb, &rs);
         }
         hset_free(once);
      }
      break;

   case V_UDP_TABLE:
   case V_SPECIFY:
      {
         ref_scan_t rs = {
            .ctx              = ctx,
            .mark_ext_read    = true,
            .mark_ext_written = true,
         };
         vlog_visit(s, scan_refs_cb, &rs);
      }
      break;

   case V_INST_LIST:
      {
         vlog_node_t mod = island_find_mod != NULL
            ? (*island_find_mod)(s, island_find_mod_ctx) : NULL;
         if (mod != NULL && vlog_kind(mod) != V_MODULE)
            mod = NULL;

         bool has_iface = false;
         int nmod_ports = 0;
         if (mod != NULL) {
            nmod_ports = vlog_ports(mod);
            const int ndecls = vlog_decls(mod);
            for (int d = 0; d < ndecls; d++) {
               if (vlog_kind(vlog_decl(mod, d)) == V_IFACE_PORT) {
                  has_iface = true;
                  break;
               }
            }
         }

         const int ninst = vlog_stmts(s);
         for (int j = 0; j < ninst; j++) {
            vlog_node_t inst = vlog_stmt(s, j);
            if (vlog_kind(inst) != V_MOD_INST)
               continue;
            bool seen_named = false;
            const int nconns = vlog_params(inst);
            for (int p = 0; p < nconns; p++) {
               vlog_node_t conn = vlog_param(inst, p);
               if (vlog_kind(conn) != V_PORT_CONN)
                  continue;
               if (vlog_has_ident(conn))
                  seen_named = true;
               if (!vlog_has_value(conn))
                  continue;

               vlog_node_t port = NULL;
               if (vlog_has_ref(conn)
                   && vlog_kind(vlog_ref(conn)) == V_PORT_DECL)
                  port = vlog_ref(conn);
               else if (mod != NULL) {
                  if (vlog_has_ident(conn)) {
                     ident_t cname = vlog_ident(conn);
                     for (int k = 0; k < nmod_ports; k++) {
                        vlog_node_t pr = vlog_port(mod, k);
                        if (vlog_has_ref(pr)
                            && vlog_ident(vlog_ref(pr)) == cname) {
                           port = vlog_ref(pr);
                           break;
                        }
                     }
                  }
                  else if (!seen_named && !has_iface && p < nmod_ports) {
                     vlog_node_t pr = vlog_port(mod, p);
                     if (vlog_has_ref(pr))
                        port = vlog_ref(pr);
                  }
               }

               bool is_in = false, is_out = false;
               if (port != NULL && vlog_kind(port) == V_PORT_DECL) {
                  switch (vlog_subkind(port)) {
                  case V_PORT_INPUT:  is_in = true; break;
                  case V_PORT_OUTPUT: is_out = true; break;
                  default:            is_in = is_out = true; break;
                  }
               }
               else {
                  // Direction not bound yet: mark shared read/written so
                  // boundary ports are promoted safely without inflating
                  // single-driver count on parent inputs
                  is_in = true;
                  is_out = true;
               }

               ref_scan_t rs = {
                  .ctx              = ctx,
                  .mark_ext_read    = is_in,
                  .mark_ext_written = is_out,
                  .add_driver       = (port != NULL && is_out),
               };
               vlog_visit(vlog_value(conn), scan_refs_cb, &rs);
            }
         }
      }
      break;

   case V_GEN_BLOCK:
      {
         const int ndecls = vlog_decls(s);
         ref_scan_t rs = { .ctx = ctx, .mark_ext_read = true };
         for (int j = 0; j < ndecls; j++) {
            vlog_node_t d = vlog_decl(s, j);
            const vlog_kind_t dk = vlog_kind(d);
            if ((dk == V_NET_DECL || dk == V_VAR_DECL) && vlog_has_value(d))
               vlog_visit(vlog_value(d), scan_refs_cb, &rs);
         }
         const int nstmts = vlog_stmts(s);
         for (int j = 0; j < nstmts; j++)
            prescan_stmt_drivers(ctx, vlog_stmt(s, j), true);
      }
      break;

   case V_IF_GENERATE:
      {
         ref_scan_t rs = { .ctx = ctx, .mark_ext_read = true };
         const int nconds = vlog_conds(s);
         for (int c = 0; c < nconds; c++) {
            vlog_node_t cond = vlog_cond(s, c);
            if (vlog_has_value(cond))
               vlog_visit(vlog_value(cond), scan_refs_cb, &rs);
            const int nstmts = vlog_stmts(cond);
            for (int j = 0; j < nstmts; j++)
               prescan_stmt_drivers(ctx, vlog_stmt(cond, j), true);
         }
      }
      break;

   case V_FOR_GENERATE:
      {
         ref_scan_t rs = { .ctx = ctx, .mark_ext_read = true };
         if (vlog_has_left(s))
            vlog_visit(vlog_left(s), scan_refs_cb, &rs);
         if (vlog_has_value(s))
            vlog_visit(vlog_value(s), scan_refs_cb, &rs);
         if (vlog_has_right(s))
            vlog_visit(vlog_right(s), scan_refs_cb, &rs);
         const int nstmts = vlog_stmts(s);
         for (int j = 0; j < nstmts; j++)
            prescan_stmt_drivers(ctx, vlog_stmt(s, j), true);
      }
      break;

   default:
      break;
   }
}

static void prescan_external_and_drivers(analyze_ctx_t *ctx)
{
   // Scan force/release and hier refs across the whole module body
   ref_scan_t href_scan = {
      .ctx          = ctx,
      .force_reason = ISIG_EJ_NONE,
   };
   vlog_visit_only(ctx->body, scan_refs_cb, &href_scan, V_HIER_REF);

   // Inline net assignments (wire w = expr;) and non-const var initializers
   for (uint32_t i = 0; i < ctx->nsigs; i++) {
      sig_meta_t *s = &(ctx->sigs[i]);
      if (s->is_gen_local)
         continue;
      if (vlog_kind(s->decl) == V_NET_DECL && vlog_has_value(s->decl)) {
         s->driver_count++;
         s->ext_written = true;
         ref_scan_t rs = { .ctx = ctx, .mark_ext_read = true };
         vlog_visit(vlog_value(s->decl), scan_refs_cb, &rs);
      }
      else if (vlog_kind(s->decl) == V_VAR_DECL && vlog_has_value(s->decl)) {
         vlog_node_t init = vlog_value(s->decl);
         if (!vlog_is_const(init)) {
            s->ext_written = true;
            ref_scan_t rs = { .ctx = ctx, .mark_ext_read = true };
            vlog_visit(init, scan_refs_cb, &rs);
         }
      }
   }

   const int nstmts = vlog_stmts(ctx->body);
   for (int i = 0; i < nstmts; i++) {
      vlog_node_t s = vlog_stmt(ctx->body, i);
      if (ctx->expanded_gens != NULL && hset_contains(ctx->expanded_gens, s))
         continue;
      prescan_stmt_drivers(ctx, s, false);
   }

   for (unsigned i = 0; i < ctx->gen_procs.count; i++)
      prescan_stmt_drivers(ctx, ctx->gen_procs.items[i].stmt, false);
   for (unsigned i = 0; i < ctx->gen_ext_stmts.count; i++)
      prescan_stmt_drivers(ctx, ctx->gen_ext_stmts.items[i], true);
}

#define NVC_ISLAND_MAX_GEN_ITERS 1024
#define NVC_ISLAND_MAX_GEN_PROCS 2048

typedef struct {
   vlog_node_t decl;
   int64_t     val;
   vlog_node_t lp;
} gen_env_entry_t;

typedef A(gen_env_entry_t) gen_env_t;

static bool gen_eval_const(vlog_node_t v, const gen_env_t *env, int64_t *out)
{
   if (v == NULL)
      return false;

   switch (vlog_kind(v)) {
   case V_NUMBER:
      {
         number_t n = vlog_number(v);
         if (!number_is_defined(n))
            return false;
         *out = number_integer(n);
         return true;
      }

   case V_REF:
      if (!vlog_has_ref(v))
         return false;
      {
         vlog_node_t d = vlog_ref(v);
         if (env != NULL) {
            for (int i = (int)env->count - 1; i >= 0; i--) {
               if (env->items[i].decl == d) {
                  *out = env->items[i].val;
                  return true;
               }
            }
         }
         switch (vlog_kind(d)) {
         case V_PARAM_DECL:
         case V_LOCALPARAM:
            return vlog_has_value(d) && gen_eval_const(vlog_value(d), env, out);
         case V_ENUM_NAME:
            return vlog_get_const(v, out);
         default:
            return false;
         }
      }

   case V_UNARY:
      {
         int64_t a;
         if (!gen_eval_const(vlog_value(v), env, &a))
            return false;
         switch (vlog_subkind(v)) {
         case V_UNARY_IDENTITY: *out = a; return true;
         case V_UNARY_NEG:      *out = -a; return true;
         case V_UNARY_NOT:      *out = !a; return true;
         case V_UNARY_BITNEG:   *out = ~a; return true;
         case V_UNARY_OR:       *out = (a != 0); return true;
         case V_UNARY_NOR:      *out = (a == 0); return true;
         default:               return false;
         }
      }

   case V_BINARY:
      {
         int64_t a, b;
         if (!gen_eval_const(vlog_left(v), env, &a)
             || !gen_eval_const(vlog_right(v), env, &b))
            return false;
         switch (vlog_subkind(v)) {
         case V_BINARY_PLUS:     *out = a + b; return true;
         case V_BINARY_MINUS:    *out = a - b; return true;
         case V_BINARY_TIMES:    *out = a * b; return true;
         case V_BINARY_DIVIDE:   if (b == 0) return false; *out = a / b; return true;
         case V_BINARY_MOD:      if (b == 0) return false; *out = a % b; return true;
         case V_BINARY_EXP:
            if (b < 0 || b > 62)
               return false;
            {
               int64_t r = 1, base = a;
               int64_t e = b;
               while (e > 0) {
                  if (e & 1) r *= base;
                  e >>= 1;
                  if (e > 0) base *= base;
               }
               *out = r;
               return true;
            }
         case V_BINARY_SHIFT_LL:
         case V_BINARY_SHIFT_LA:
            if (b < 0 || b >= 63) return false;
            *out = (int64_t)((uint64_t)a << b);
            return true;
         case V_BINARY_SHIFT_RL:
            if (b < 0 || b >= 63) return false;
            *out = (int64_t)((uint64_t)a >> b);
            return true;
         case V_BINARY_SHIFT_RA:
            if (b < 0 || b >= 63) return false;
            *out = a >> b;
            return true;
         case V_BINARY_LT:       *out = (a < b); return true;
         case V_BINARY_LEQ:      *out = (a <= b); return true;
         case V_BINARY_GT:       *out = (a > b); return true;
         case V_BINARY_GEQ:      *out = (a >= b); return true;
         case V_BINARY_LOG_EQ:
         case V_BINARY_CASE_EQ:
         case V_BINARY_WILD_EQ:  *out = (a == b); return true;
         case V_BINARY_LOG_NEQ:
         case V_BINARY_CASE_NEQ:
         case V_BINARY_WILD_NEQ: *out = (a != b); return true;
         case V_BINARY_AND:      *out = (a & b); return true;
         case V_BINARY_OR:       *out = (a | b); return true;
         case V_BINARY_XOR:      *out = (a ^ b); return true;
         case V_BINARY_XNOR:     *out = ~(a ^ b); return true;
         case V_BINARY_LOG_AND:  *out = (a && b); return true;
         case V_BINARY_LOG_OR:   *out = (a || b); return true;
         default:                return false;
         }
      }

   case V_COND_EXPR:
      {
         int64_t c;
         if (!gen_eval_const(vlog_value(v), env, &c))
            return false;
         return gen_eval_const(c ? vlog_left(v) : vlog_right(v), env, out);
      }

   case V_CONCAT:
      if (!vlog_has_value(v) && vlog_params(v) == 1)
         return gen_eval_const(vlog_param(v, 0), env, out);
      return false;

   case V_CAST:
      if (vlog_subkind(v) == V_CAST_SIGN || vlog_subkind(v) == V_CAST_WIDTH
          || vlog_subkind(v) == V_CAST_TYPE)
         return gen_eval_const(vlog_value(v), env, out);
      return false;

   case V_SYS_FCALL:
      if (vlog_params(v) == 1) {
         int64_t a;
         if (!gen_eval_const(vlog_param(v, 0), env, &a))
            return false;
         switch (vlog_subkind(v)) {
         case V_SYSTF_SIGNED:
         case V_SYSTF_UNSIGNED:
            *out = a;
            return true;
         case V_SYSTF_CLOG2:
            if (a <= 1) {
               *out = 0;
               return true;
            }
            {
               uint64_t u = (uint64_t)(a - 1);
               int64_t lg = 0;
               while (u > 0) {
                  lg++;
                  u >>= 1;
               }
               *out = lg;
               return true;
            }
         default:
            return false;
         }
      }
      return false;

   default:
      return false;
   }
}

typedef struct {
   vlog_node_t      root;
   hash_t          *subst;
   const gen_env_t *env;
   hset_t          *in_stmt;
} gen_copy_ctx_t;

typedef struct {
   hset_t           *in_stmt;
   hset_t           *seen_arenas;
   A(object_t *)     roots;
} gen_stmt_scan_t;

static void gen_stmt_scan_cb(vlog_node_t v, void *arg)
{
   gen_stmt_scan_t *gs = arg;
   hset_insert(gs->in_stmt, v);
   object_t *obj = vlog_to_object(v);
   object_arena_t *a = object_arena(obj);
   if (!hset_contains(gs->seen_arenas, a)) {
      hset_insert(gs->seen_arenas, a);
      APUSH(gs->roots, obj);
   }
}

static bool gen_stmt_copy_pred(vlog_node_t v, void *arg)
{
   const gen_copy_ctx_t *gc = arg;
   if (!hset_contains(gc->in_stmt, v))
      return false;

   switch (vlog_kind(v)) {
   case V_REF:
      return vlog_has_ref(v)
          && (hash_get(gc->subst, vlog_ref(v)) != NULL
              || hset_contains(gc->in_stmt, vlog_ref(v)));
   case V_PART_SELECT:
   case V_BIT_SELECT:
   case V_CONCAT:
   case V_CAST:
      return true;
   default:
      return v == gc->root;
   }
}

static vlog_node_t make_gen_number(int64_t val, const loc_t *loc)
{
   vlog_node_t num = vlog_new(V_NUMBER);
   vlog_set_number(num, number_from_int(val));
   vlog_set_loc(num, loc);
   return num;
}

static vlog_node_t gen_stmt_rewrite_cb(vlog_node_t v, void *arg)
{
   const gen_copy_ctx_t *gc = arg;
   switch (vlog_kind(v)) {
   case V_REF:
      if (vlog_has_ref(v)) {
         vlog_node_t repl = hash_get(gc->subst, vlog_ref(v));
         if (repl != NULL)
            vlog_set_ref(v, repl);
      }
      return v;

   case V_PART_SELECT:
      {
         int64_t cval;
         if (vlog_kind(vlog_left(v)) != V_NUMBER
             && gen_eval_const(vlog_left(v), gc->env, &cval))
            vlog_set_left(v, make_gen_number(cval, vlog_loc(vlog_left(v))));
         if (vlog_has_right(v) && vlog_kind(vlog_right(v)) != V_NUMBER
             && gen_eval_const(vlog_right(v), gc->env, &cval))
            vlog_set_right(v, make_gen_number(cval, vlog_loc(vlog_right(v))));
      }
      return v;

   case V_BIT_SELECT:
      {
         const unsigned np = vlog_params(v);
         for (unsigned i = 0; i < np; i++) {
            vlog_node_t p = vlog_param(v, i);
            int64_t cval;
            if (vlog_kind(p) != V_NUMBER && gen_eval_const(p, gc->env, &cval))
               vlog_set_param(v, i, make_gen_number(cval, vlog_loc(p)));
         }
      }
      return v;

   case V_CONCAT:
      if (vlog_has_value(v) && vlog_kind(vlog_value(v)) != V_NUMBER) {
         int64_t cval;
         if (gen_eval_const(vlog_value(v), gc->env, &cval))
            vlog_set_value(v, make_gen_number(cval, vlog_loc(vlog_value(v))));
      }
      return v;

   case V_CAST:
      if (vlog_subkind(v) == V_CAST_WIDTH && vlog_has_left(v)
          && vlog_kind(vlog_left(v)) != V_NUMBER) {
         int64_t cval;
         if (gen_eval_const(vlog_left(v), gc->env, &cval))
            vlog_set_left(v, make_gen_number(cval, vlog_loc(vlog_left(v))));
      }
      return v;

   default:
      return v;
   }
}

static vlog_node_t specialize_gen_stmt(vlog_node_t s, const gen_env_t *env,
                                       hash_t *subst, bool has_subst)
{
   if (!has_subst)
      return s;

   gen_stmt_scan_t gs = {
      .in_stmt     = hset_new(32),
      .seen_arenas = hset_new(8),
      .roots       = AINIT,
   };
   object_t *root_obj = vlog_to_object(s);
   hset_insert(gs.seen_arenas, object_arena(root_obj));
   APUSH(gs.roots, root_obj);
   vlog_visit(s, gen_stmt_scan_cb, &gs);

   gen_copy_ctx_t gc = {
      .root    = s,
      .subst   = subst,
      .env     = env,
      .in_stmt = gs.in_stmt,
   };

   object_copy_ctx_t *copy = xcalloc_flex(sizeof(object_copy_ctx_t),
                                          gs.roots.count, sizeof(object_t *));
   copy->generation   = object_next_generation();
   copy->pred_context = &gc;
   copy->nroots       = gs.roots.count;
   for (unsigned i = 0; i < gs.roots.count; i++)
      copy->roots[i] = gs.roots.items[i];
   copy->should_copy[OBJECT_TAG_VLOG] = (object_copy_pred_t)gen_stmt_copy_pred;

   object_copy_begin(copy);
   object_copy_finish(copy);

   vlog_node_t copied = vlog_from_object(copy->roots[0]);
   free(copy);
   ACLEAR(gs.roots);
   hset_free(gs.seen_arenas);
   hset_free(gs.in_stmt);

   return vlog_rewrite(copied, NULL, gen_stmt_rewrite_cb, &gc);
}

static vlog_node_t make_gen_localparam(vlog_node_t orig, int64_t val)
{
   vlog_node_t lp = vlog_new(V_LOCALPARAM);
   vlog_set_ident(lp, vlog_ident(orig));
   vlog_set_loc(lp, vlog_loc(orig));
   if (vlog_has_type(orig))
      vlog_set_type(lp, vlog_type(orig));
   vlog_set_value(lp, make_gen_number(val, vlog_loc(orig)));
   return lp;
}

static vlog_node_t clone_gen_local_sig(vlog_node_t d, ident_t gen_path)
{
   const vlog_kind_t k = vlog_kind(d);
   vlog_node_t c = vlog_new(k);
   vlog_set_ident(c, ident_sprintf("%s.%s", istr(gen_path),
                                   istr(vlog_ident(d))));
   vlog_set_loc(c, vlog_loc(d));
   if (k == V_VAR_DECL)
      vlog_set_flags(c, vlog_flags(d));
   else if (k == V_NET_DECL)
      vlog_set_subkind(c, vlog_subkind(d));
   if (vlog_has_type(d))
      vlog_set_type(c, vlog_type(d));
   const int nr = vlog_ranges(d);
   for (int i = 0; i < nr; i++)
      vlog_add_range(c, vlog_range(d, i));
   return c;
}

static bool add_gen_local_sig(analyze_ctx_t *ctx, vlog_node_t orig,
                              ident_t gen_path, vlog_node_t *clone_out)
{
   if (sig_type_is_nettype(orig))
      return false;
   if (vlog_kind(orig) == V_NET_DECL && vlog_has_value(orig))
      return false;   // Inline net assignment inside gen block
   if (vlog_kind(orig) == V_VAR_DECL && vlog_has_value(orig))
      return false;   // Var initializer inside gen block
   if (vlog_kind(orig) == V_NET_DECL && vlog_has_delay(orig))
      return false;

   uint32_t w = 0, n = 0;
   bool two_state = false;
   island_sig_eject_reason_t r = ISIG_EJ_NONE;
   if (!check_sig_type_eligible(orig, &w, &n, &two_state, &r))
      return false;

   vlog_node_t d_copy = clone_gen_local_sig(orig, gen_path);

   uint32_t idx = ctx->nsigs++;
   if (idx >= ctx->cap_sigs) {
      ctx->cap_sigs *= 2;
      ctx->sigs = xrealloc_array(ctx->sigs, ctx->cap_sigs, sizeof(sig_meta_t));
   }

   sig_meta_t *s = &(ctx->sigs[idx]);
   memset(s, 0, sizeof(*s));
   s->decl = d_copy;
   s->orig_decl = orig;
   s->name = vlog_ident(d_copy);
   s->gen_path = gen_path;
   s->orig_name = vlog_ident(orig);
   s->width = w;
   s->nelems = n;
   s->two_state = two_state;
   s->eligible = true;
   s->is_gen_local = true;

   hash_put(ctx->decl_map, d_copy, (void *)(uintptr_t)(idx + 1));
   *clone_out = d_copy;
   return true;
}

static bool expand_gen_node(analyze_ctx_t *ctx, vlog_node_t node,
                            gen_env_t *env, hash_t *subst,
                            ident_t path_prefix, bool in_for_iter,
                            bool has_subst);

static bool expand_gen_block(analyze_ctx_t *ctx, vlog_node_t blk,
                             gen_env_t *env, hash_t *subst,
                             ident_t blk_path, bool has_subst)
{
   const unsigned saved_env = env->count;
   A(vlog_node_t) added_keys = AINIT;
   A(vlog_node_t) saved_vals = AINIT;

   const int ndecls = vlog_decls(blk);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(blk, i);
      const vlog_kind_t dk = vlog_kind(d);
      if (dk == V_LOCALPARAM) {
         if (vlog_ranges(d) > 0 || !vlog_has_value(d))
            goto fail;
         int64_t val;
         if (!gen_eval_const(vlog_value(d), env, &val))
            goto fail;
         vlog_node_t lp = make_gen_localparam(d, val);
         gen_env_entry_t e = { .decl = d, .val = val, .lp = lp };
         APUSH(*env, e);
         APUSH(added_keys, d);
         APUSH(saved_vals, hash_get(subst, d));
         hash_put(subst, d, lp);
         has_subst = true;
      }
      else if (dk == V_VAR_DECL || dk == V_NET_DECL) {
         vlog_node_t d_copy = NULL;
         if (!add_gen_local_sig(ctx, d, blk_path, &d_copy))
            goto fail;
         APUSH(added_keys, d);
         APUSH(saved_vals, hash_get(subst, d));
         hash_put(subst, d, d_copy);
         has_subst = true;
      }
      else if (dk == V_GENVAR_DECL || dk == V_TYPE_DECL || dk == V_ENUM_DECL
               || dk == V_IMPORT_DECL) {
         continue;
      }
      else
         goto fail;
   }

   const int nstmts = vlog_stmts(blk);
   for (int i = 0; i < nstmts; i++) {
      vlog_node_t s = vlog_stmt(blk, i);
      if (!expand_gen_node(ctx, s, env, subst, blk_path, false, has_subst))
         goto fail;
   }

   for (int i = (int)added_keys.count - 1; i >= 0; i--)
      hash_put(subst, added_keys.items[i], saved_vals.items[i]);
   ACLEAR(added_keys);
   ACLEAR(saved_vals);
   env->count = saved_env;
   return true;

fail:
   for (int i = (int)added_keys.count - 1; i >= 0; i--)
      hash_put(subst, added_keys.items[i], saved_vals.items[i]);
   ACLEAR(added_keys);
   ACLEAR(saved_vals);
   env->count = saved_env;
   return false;
}

static bool eval_gen_for_step(vlog_node_t step_node, vlog_node_t gv,
                              const gen_env_t *env, int64_t cur_val,
                              int64_t *next_out)
{
   if (step_node == NULL || vlog_kind(step_node) != V_FOR_STEP
       || vlog_stmts(step_node) != 1)
      return false;

   vlog_node_t st = vlog_stmt(step_node, 0);
   switch (vlog_kind(st)) {
   case V_PREFIX:
   case V_POSTFIX:
      if (!vlog_has_target(st) || vlog_kind(vlog_target(st)) != V_REF
          || !vlog_has_ref(vlog_target(st))
          || vlog_ref(vlog_target(st)) != gv)
         return false;
      if (vlog_subkind(st) == V_INCDEC_PLUS) {
         *next_out = cur_val + 1;
         return true;
      }
      if (vlog_subkind(st) == V_INCDEC_MINUS) {
         *next_out = cur_val - 1;
         return true;
      }
      return false;

   case V_OP_ASSIGN:
      if (!vlog_has_target(st) || vlog_kind(vlog_target(st)) != V_REF
          || !vlog_has_ref(vlog_target(st))
          || vlog_ref(vlog_target(st)) != gv)
         return false;
      {
         int64_t rhs;
         if (!gen_eval_const(vlog_value(st), env, &rhs))
            return false;
         switch (vlog_subkind(st)) {
         case V_ASSIGN_PLUS:  *next_out = cur_val + rhs; return true;
         case V_ASSIGN_MINUS: *next_out = cur_val - rhs; return true;
         case V_ASSIGN_TIMES: *next_out = cur_val * rhs; return true;
         default:             return false;
         }
      }

   case V_BASSIGN:
      if (!vlog_has_target(st) || vlog_kind(vlog_target(st)) != V_REF
          || !vlog_has_ref(vlog_target(st))
          || vlog_ref(vlog_target(st)) != gv)
         return false;
      return gen_eval_const(vlog_value(st), env, next_out);

   default:
      return false;
   }
}

static bool expand_gen_node(analyze_ctx_t *ctx, vlog_node_t node,
                            gen_env_t *env, hash_t *subst,
                            ident_t path_prefix, bool in_for_iter,
                            bool has_subst)
{
   (void)in_for_iter;
   const vlog_kind_t k = vlog_kind(node);
   switch (k) {
   case V_ASSIGN:
   case V_ALWAYS:
      if (ctx->gen_procs.count >= NVC_ISLAND_MAX_GEN_PROCS)
         return false;
      {
         vlog_node_t spec = specialize_gen_stmt(node, env, subst, has_subst);
         gen_proc_item_t item = { .stmt = spec, .orig_stmt = node };
         APUSH(ctx->gen_procs, item);
      }
      return true;

   case V_GEN_BLOCK:
      {
         ident_t bname = vlog_ident(node);
         ident_t bpath = path_prefix != NULL
            ? ident_sprintf("%s%%%s", istr(path_prefix), istr(bname))
            : ident_sprintf("%%%s", istr(bname));
         return expand_gen_block(ctx, node, env, subst, bpath, has_subst);
      }

   case V_IF_GENERATE:
      {
         const int nconds = vlog_conds(node);
         for (int c = 0; c < nconds; c++) {
            vlog_node_t cond = vlog_cond(node, c);
            if (vlog_has_value(cond)) {
               int64_t val = 0;
               if (!gen_eval_const(vlog_value(cond), env, &val))
                  return false;
               if (val == 0)
                  continue;
            }
            const int ns = vlog_stmts(cond);
            for (int j = 0; j < ns; j++) {
               if (!expand_gen_node(ctx, vlog_stmt(cond, j), env, subst,
                                    path_prefix, false, has_subst))
                  return false;
            }
            return true;
         }
         return true;
      }

   case V_FOR_GENERATE:
      {
         if (!vlog_has_left(node) || !vlog_has_value(node)
             || !vlog_has_right(node) || vlog_stmts(node) != 1)
            return false;

         vlog_node_t init = vlog_left(node);
         if (vlog_kind(init) != V_FOR_INIT || vlog_stmts(init) != 1)
            return false;

         vlog_node_t is0 = vlog_stmt(init, 0);
         if (vlog_kind(is0) != V_BASSIGN || vlog_kind(vlog_target(is0)) != V_REF
             || !vlog_has_ref(vlog_target(is0)))
            return false;

         vlog_node_t gv = vlog_ref(vlog_target(is0));
         if (vlog_kind(gv) != V_GENVAR_DECL)
            return false;

         vlog_node_t blk = vlog_stmt(node, 0);
         if (vlog_kind(blk) != V_GEN_BLOCK)
            return false;

         int64_t cur_val = 0;
         if (!gen_eval_const(vlog_value(is0), env, &cur_val))
            return false;

         vlog_node_t saved_sub = hash_get(subst, gv);
         const unsigned saved_env = env->count;
         gen_env_entry_t ge = { .decl = gv, .val = cur_val, .lp = NULL };
         APUSH(*env, ge);

         bool ok = true;
         unsigned iters = 0;
         while (true) {
            env->items[saved_env].val = cur_val;
            int64_t cond_val = 0;
            if (!gen_eval_const(vlog_value(node), env, &cond_val)) {
               ok = false;
               break;
            }
            if (cond_val == 0)
               break;
            if (++iters > NVC_ISLAND_MAX_GEN_ITERS) {
               ok = false;
               break;
            }

            vlog_node_t lp = make_gen_localparam(gv, cur_val);
            env->items[saved_env].lp = lp;
            hash_put(subst, gv, lp);

            ident_t bname = vlog_ident(blk);
            ident_t ipath = path_prefix != NULL
               ? ident_sprintf("%s%%%s[%" PRIi64 "]", istr(path_prefix),
                               istr(bname), cur_val)
               : ident_sprintf("%%%s[%" PRIi64 "]", istr(bname), cur_val);

            if (!expand_gen_block(ctx, blk, env, subst, ipath, true)) {
               ok = false;
               break;
            }

            int64_t next_val = 0;
            if (!eval_gen_for_step(vlog_right(node), gv, env, cur_val,
                                   &next_val)) {
               ok = false;
               break;
            }
            cur_val = next_val;
         }

         hash_put(subst, gv, saved_sub);
         env->count = saved_env;
         return ok;
      }

   case V_INST_LIST:
   case V_INITIAL:
   case V_FINAL:
   case V_GATE_INST:
   case V_UDP_TABLE:
   case V_SPECIFY:
      APUSH(ctx->gen_ext_stmts,
            specialize_gen_stmt(node, env, subst, has_subst));
      return true;

   case V_DEFPARAM:
      return true;

   default:
      return false;
   }
}

static void expand_module_generates(analyze_ctx_t *ctx)
{
   if (vlog_flags(ctx->body) & VLOG_F_GEN_BODY)
      return;

   const int nstmts = vlog_stmts(ctx->body);
   bool has_gen = false;
   for (int i = 0; i < nstmts; i++) {
      const vlog_kind_t k = vlog_kind(vlog_stmt(ctx->body, i));
      if (k == V_GEN_BLOCK || k == V_IF_GENERATE || k == V_FOR_GENERATE) {
         has_gen = true;
         break;
      }
   }
   if (!has_gen)
      return;

   ctx->expanded_gens = hset_new(16);
   gen_env_t env = AINIT;
   hash_t *subst = hash_new(32);

   for (int i = 0; i < nstmts; i++) {
      vlog_node_t s = vlog_stmt(ctx->body, i);
      const vlog_kind_t k = vlog_kind(s);
      if (k != V_GEN_BLOCK && k != V_IF_GENERATE && k != V_FOR_GENERATE)
         continue;

      const unsigned saved_procs = ctx->gen_procs.count;
      const unsigned saved_ext = ctx->gen_ext_stmts.count;
      const uint32_t saved_sigs = ctx->nsigs;

      if (expand_gen_node(ctx, s, &env, subst, NULL, false, false))
         hset_insert(ctx->expanded_gens, s);
      else {
         // Roll back partial expansion and fall back to conservative prescan
         for (uint32_t j = saved_sigs; j < ctx->nsigs; j++)
            hash_put(ctx->decl_map, ctx->sigs[j].decl, NULL);
         ctx->nsigs = saved_sigs;
         ctx->gen_procs.count = saved_procs;
         ctx->gen_ext_stmts.count = saved_ext;
      }
   }

   hash_free(subst);
   ACLEAR(env);
}

static void mark_force_cb(vlog_node_t v, void *arg)
{
   analyze_ctx_t *ctx = arg;
   if (vlog_has_target(v)) {
      ref_scan_t rs = {
         .ctx              = ctx,
         .mark_ext_read    = true,
         .mark_ext_written = true,
         .force_reason     = ISIG_EJ_FORCE_TARGET,
      };
      vlog_visit(vlog_target(v), scan_refs_cb, &rs);
   }
}

static void prescan_forces(analyze_ctx_t *ctx)
{
   vlog_visit_only(ctx->body, mark_force_cb, ctx, V_FORCE);
   vlog_visit_only(ctx->body, mark_force_cb, ctx, V_RELEASE);
}

typedef struct {
   analyze_ctx_t              *ctx;
   proc_meta_t                *proc;
   hset_t                     *locals;
   hset_t                     *read_set;
   hset_t                     *write_set;
   A(vlog_node_t)              active_funcs;
   bool                        allow_nbassign;
   bool                        allow_mod_bassign;
   bool                        in_func;
   island_proc_eject_reason_t  reject;
} check_ctx_t;

static void add_u32_unique(uint32_t **arr, uint32_t *count, hset_t *seen,
                           uint32_t val)
{
   void *key = (void *)(uintptr_t)(val + 1);
   if (hset_contains(seen, key))
      return;
   hset_insert(seen, key);

   *arr = xrealloc_array(*arr, *count + 1, sizeof(uint32_t));
   (*arr)[(*count)++] = val;
}

static void fail_proc(check_ctx_t *cc, island_proc_eject_reason_t r)
{
   if (cc->reject == IPROC_EJ_NONE)
      cc->reject = r;
}

static bool is_pure_systf(vlog_node_t v)
{
   switch (vlog_subkind(v)) {
   case V_SYSTF_SIGNED:
   case V_SYSTF_UNSIGNED:
   case V_SYSTF_CLOG2:
   case V_SYSTF_BITS:
      return true;
   default:
      {
         static const char *const pure_names[] = {
            "$isunknown",
            "$left", "$right", "$low", "$high", "$size", "$dimensions",
            "$unpacked_dimensions", "$increment",
         };
         ident_t id = vlog_ident(v);
         for (size_t i = 0; i < ARRAY_LEN(pure_names); i++) {
            if (id == ident_new(pure_names[i]))
               return true;
         }
         return false;
      }
   }
}

static bool check_expr(check_ctx_t *cc, vlog_node_t v);
static bool check_stmt(check_ctx_t *cc, vlog_node_t v);

static vlog_node_t unwrap_typedef(vlog_node_t t)
{
   while (t != NULL && vlog_kind(t) == V_TYPE_DECL && vlog_has_type(t))
      t = vlog_type(t);
   return t;
}

static bool sv_expr_exact_bit_range(const analyze_ctx_t *ctx, vlog_node_t expr,
                                    uint32_t *sig_idx_out, uint64_t *bit_lo_out,
                                    uint64_t *bit_hi_out, hset_t *locals)
{
   vlog_node_t steps[16];
   int nsteps = 0;
   vlog_node_t cur = expr;
   while (cur != NULL && vlog_kind(cur) != V_REF) {
      const vlog_kind_t k = vlog_kind(cur);
      if (k != V_MEMBER_REF && k != V_BIT_SELECT && k != V_PART_SELECT)
         return false;
      if (nsteps >= 16)
         return false;
      steps[nsteps++] = cur;
      cur = vlog_value(cur);
   }

   if (cur == NULL || vlog_kind(cur) != V_REF || !vlog_has_ref(cur))
      return false;
   vlog_node_t d = vlog_ref(cur);
   if (locals != NULL && hset_contains(locals, d))
      return false;

   const int idx = find_sig_idx(ctx, d);
   if (idx < 0)
      return false;

   *sig_idx_out = (uint32_t)idx;

   const sig_meta_t *s = &(ctx->sigs[idx]);
   const uint64_t total_bits = (uint64_t)s->width * s->nelems;
   uint64_t elem_lo = 0, elem_hi = s->nelems > 0 ? s->nelems - 1 : 0;
   uint64_t bit_off = 0, bit_w = s->width;
   vlog_node_t cur_type = vlog_has_type(s->decl) ? vlog_type(s->decl) : NULL;
   const int n_unpacked = vlog_ranges(s->decl);
   int unpacked_dim = 0, packed_dim = 0;
   bool exact = (total_bits > 0 && cur_type != NULL);

   for (int si = nsteps - 1; si >= 0 && exact; si--) {
      vlog_node_t step = steps[si];
      const vlog_kind_t sk = vlog_kind(step);
      cur_type = unwrap_typedef(cur_type);
      if (cur_type == NULL) {
         exact = false;
         break;
      }

      if (sk == V_MEMBER_REF) {
         if (unpacked_dim != n_unpacked
             || vlog_kind(cur_type) != V_STRUCT_DECL
             || !(vlog_flags(cur_type) & VLOG_F_PACKED)) {
            exact = false;
            break;
         }
         vlog_node_t mdecl = NULL;
         if (vlog_has_ref(step))
            mdecl = vlog_ref(step);
         else if (vlog_has_ident(step)) {
            ident_t mid = vlog_ident(step);
            const int nd = vlog_decls(cur_type);
            for (int k = 0; k < nd; k++) {
               vlog_node_t fd = vlog_decl(cur_type, k);
               if (vlog_ident(fd) == mid) {
                  mdecl = fd;
                  break;
               }
            }
         }
         if (mdecl == NULL || !vlog_has_type(mdecl)) {
            exact = false;
            break;
         }
         const unsigned moff = vlog_packed_offset(cur_type, mdecl);
         const unsigned mw = vlog_size(vlog_type(mdecl));
         if (mw == 0 || moff + mw > bit_w) {
            exact = false;
            break;
         }
         bit_off += moff;
         bit_w = mw;
         cur_type = vlog_type(mdecl);
         packed_dim = 0;
      }
      else if (sk == V_BIT_SELECT) {
         const unsigned np = vlog_params(step);
         for (unsigned p = 0; p < np && exact; p++) {
            int64_t iv;
            if (!vlog_get_const(vlog_param(step, p), &iv)) {
               exact = false;
               break;
            }
            if (unpacked_dim < n_unpacked) {
               vlog_node_t r = vlog_range(s->decl, unpacked_dim);
               int64_t l, rr;
               const unsigned dsz = vlog_size(r);
               if (dsz == 0 || !vlog_bounds(r, &l, &rr)) {
                  exact = false;
                  break;
               }
               const int64_t pos = (l <= rr) ? (iv - l) : (l - iv);
               if (pos < 0 || (uint64_t)pos >= dsz) {
                  exact = false;
                  break;
               }
               const uint64_t stride = (elem_hi - elem_lo + 1) / dsz;
               elem_lo += (uint64_t)pos * stride;
               elem_hi = elem_lo + stride - 1;
               unpacked_dim++;
            }
            else {
               cur_type = unwrap_typedef(cur_type);
               int64_t l = 0, rr = 0;
               uint64_t dsz = 0;
               if (cur_type != NULL && vlog_kind(cur_type) == V_DATA_TYPE
                   && packed_dim < vlog_ranges(cur_type)) {
                  vlog_node_t r = vlog_range(cur_type, packed_dim);
                  dsz = vlog_size(r);
                  if (dsz == 0 || !vlog_bounds(r, &l, &rr)) {
                     exact = false;
                     break;
                  }
                  packed_dim++;
               }
               else if (packed_dim == 0 && bit_w > 0) {
                  l = (int64_t)bit_w - 1;
                  rr = 0;
                  dsz = bit_w;
                  packed_dim++;
               }
               else {
                  exact = false;
                  break;
               }
               const int64_t pos = (l >= rr) ? (l - iv) : (iv - l);
               if (pos < 0 || (uint64_t)pos >= dsz || bit_w < dsz) {
                  exact = false;
                  break;
               }
               const uint64_t ew = bit_w / dsz;
               bit_off += (uint64_t)pos * ew;
               bit_w = ew;
            }
         }
      }
      else if (sk == V_PART_SELECT) {
         if (unpacked_dim != n_unpacked) {
            exact = false;
            break;
         }
         int64_t dl = 0, dr = 0;
         uint64_t dsz = 0;
         if (vlog_kind(cur_type) == V_DATA_TYPE
             && packed_dim < vlog_ranges(cur_type)) {
            vlog_node_t r = vlog_range(cur_type, packed_dim);
            dsz = vlog_size(r);
            if (dsz == 0 || !vlog_bounds(r, &dl, &dr)) {
               exact = false;
               break;
            }
         }
         else if (packed_dim == 0 && bit_w > 0) {
            dl = (int64_t)bit_w - 1;
            dr = 0;
            dsz = bit_w;
         }
         else {
            exact = false;
            break;
         }

         int64_t sl = 0, sr = 0;
         const unsigned rk = vlog_subkind(step);
         if (rk == V_RANGE_CONST) {
            if (!vlog_get_const(vlog_left(step), &sl)
                || !vlog_get_const(vlog_right(step), &sr)) {
               exact = false;
               break;
            }
         }
         else if (rk == V_RANGE_POS || rk == V_RANGE_NEG) {
            int64_t base, sw;
            if (!vlog_get_const(vlog_left(step), &base)
                || !vlog_get_const(vlog_right(step), &sw) || sw <= 0) {
               exact = false;
               break;
            }
            if (rk == V_RANGE_POS) {
               sl = base;
               sr = base + sw - 1;
            }
            else {
               sl = base - sw + 1;
               sr = base;
            }
         }
         else {
            exact = false;
            break;
         }

         const int64_t p0 = (dl >= dr) ? (dl - sl) : (sl - dl);
         const int64_t p1 = (dl >= dr) ? (dl - sr) : (sr - dl);
         const int64_t plo = MIN(p0, p1), phi = MAX(p0, p1);
         if (plo < 0 || (uint64_t)phi >= dsz || bit_w < dsz) {
            exact = false;
            break;
         }
         const uint64_t ew = bit_w / dsz;
         bit_off += (uint64_t)plo * ew;
         bit_w = (uint64_t)(phi - plo + 1) * ew;
         packed_dim++;
      }
   }

   if (exact) {
      *bit_lo_out = elem_lo * s->width + bit_off;
      *bit_hi_out = (elem_lo == elem_hi)
         ? (*bit_lo_out + bit_w - 1)
         : ((elem_hi + 1) * s->width - 1);
   }
   else {
      *bit_lo_out = 0;
      *bit_hi_out = total_bits > 0 ? total_bits - 1 : 0;
   }
   return true;
}

static void record_lvalue_write_ranges(check_ctx_t *cc, vlog_node_t lhs)
{
   if (lhs == NULL || cc->in_func)
      return;

   if (vlog_kind(lhs) == V_CONCAT) {
      for (unsigned i = 0; i < vlog_params(lhs); i++)
         record_lvalue_write_ranges(cc, vlog_param(lhs, i));
      return;
   }

   sv_write_range_t wr;
   if (sv_expr_exact_bit_range(cc->ctx, lhs, &wr.sig_idx, &wr.bit_lo,
                               &wr.bit_hi, cc->locals))
      APUSH(cc->proc->write_ranges, wr);
}

static bool check_lvalue(check_ctx_t *cc, vlog_node_t lhs, bool is_nba,
                         bool is_read_modify_write)
{
   if (lhs == NULL) {
      fail_proc(cc, IPROC_EJ_UNSUPPORTED_EXPR);
      return false;
   }

   cc->proc->node_count++;

   switch (vlog_kind(lhs)) {
   case V_REF:
      {
         if (!vlog_has_ref(lhs)) {
            fail_proc(cc, IPROC_EJ_UNSUPPORTED_EXPR);
            return false;
         }

         vlog_node_t d = vlog_ref(lhs);
         if (hset_contains(cc->locals, d)) {
            if (is_nba) {
               fail_proc(cc, IPROC_EJ_UNSUPPORTED_STMT);
               return false;
            }
            return true;
         }

         if (cc->in_func) {
            // User function writing to module-scope variable has side effects
            fail_proc(cc, IPROC_EJ_SYSCALL);
            return false;
         }

         const int idx = find_sig_idx(cc->ctx, d);
         if (idx < 0) {
            fail_proc(cc, IPROC_EJ_UNSUPPORTED_EXPR);
            return false;
         }

         const sig_meta_t *s = &(cc->ctx->sigs[idx]);
         if (!s->eligible) {
            fail_proc(cc, s->eject_reason == ISIG_EJ_DYNAMIC_TYPE
                      ? IPROC_EJ_DYNAMIC_OP : IPROC_EJ_UNSUPPORTED_EXPR);
            return false;
         }

         if (s->ext_written) {
            fail_proc(cc, IPROC_EJ_MULTI_DRIVER_TARGET);
            return false;
         }

         if (s->eject_reason == ISIG_EJ_INOUT_PORT
             || s->eject_reason == ISIG_EJ_NETTYPE
             || s->eject_reason == ISIG_EJ_ALIAS) {
            fail_proc(cc, IPROC_EJ_UNSUPPORTED_EXPR);
            return false;
         }

         if (!is_nba && !cc->allow_mod_bassign) {
            fail_proc(cc, IPROC_EJ_MIXED_ASSIGN_IN_FF);
            return false;
         }

         add_u32_unique(&cc->proc->writes.items, &cc->proc->writes.count,
                        cc->write_set, (uint32_t)idx);
         if (is_read_modify_write)
            add_u32_unique(&cc->proc->reads.items, &cc->proc->reads.count,
                           cc->read_set, (uint32_t)idx);
         return true;
      }

   case V_HIER_REF:
      fail_proc(cc, IPROC_EJ_HIER_REF);
      return false;

   case V_BIT_SELECT:
      if (!check_lvalue(cc, vlog_value(lhs), is_nba, is_read_modify_write))
         return false;
      for (unsigned i = 0; i < vlog_params(lhs); i++) {
         if (!check_expr(cc, vlog_param(lhs, i)))
            return false;
      }
      return true;

   case V_PART_SELECT:
      if (!check_lvalue(cc, vlog_value(lhs), is_nba, is_read_modify_write))
         return false;
      if (!check_expr(cc, vlog_left(lhs)))
         return false;
      if (vlog_has_right(lhs) && !check_expr(cc, vlog_right(lhs)))
         return false;
      return true;

   case V_MEMBER_REF:
      {
         vlog_node_t base_t = vlog_get_type(vlog_value(lhs));
         if (vlog_virtual_pending(lhs) || base_t == NULL
             || vlog_is_dynamic_type(base_t)
             || (vlog_kind(base_t) == V_UNION_DECL
                 && (vlog_flags(base_t) & VLOG_F_TAGGED))) {
            fail_proc(cc, IPROC_EJ_DYNAMIC_OP);
            return false;
         }
         return check_lvalue(cc, vlog_value(lhs), is_nba, is_read_modify_write);
      }

   case V_CONCAT:
      if (vlog_has_value(lhs)) {
         fail_proc(cc, IPROC_EJ_UNSUPPORTED_EXPR);
         return false;
      }
      for (unsigned i = 0; i < vlog_params(lhs); i++) {
         if (!check_lvalue(cc, vlog_param(lhs, i), is_nba,
                           is_read_modify_write))
            return false;
      }
      return true;

   default:
      fail_proc(cc, IPROC_EJ_UNSUPPORTED_EXPR);
      return false;
   }
}

static bool check_func_body(check_ctx_t *cc, vlog_node_t fn)
{
   if (vlog_flags(fn) & (VLOG_F_DPI | VLOG_F_EXTERN | VLOG_F_VIRTUAL)) {
      fail_proc(cc, IPROC_EJ_SYSCALL);
      return false;
   }

   for (unsigned i = 0; i < cc->active_funcs.count; i++) {
      if (cc->active_funcs.items[i] == fn) {
         fail_proc(cc, IPROC_EJ_UNSUPPORTED_EXPR);  // Recursion not allowed
         return false;
      }
   }

   APUSH(cc->active_funcs, fn);

   const unsigned np = vlog_ports(fn);
   for (unsigned i = 0; i < np; i++) {
      vlog_node_t p = vlog_port(fn, i);
      if (vlog_kind(p) == V_TF_PORT_DECL) {
         if (vlog_has_type(p) && (vlog_is_dynamic_type(vlog_type(p))
                                  || vlog_is_real(p))) {
            APOP(cc->active_funcs);
            fail_proc(cc, IPROC_EJ_DYNAMIC_OP);
            return false;
         }
         hset_insert(cc->locals, p);
      }
   }

   const unsigned nd = vlog_decls(fn);
   for (unsigned i = 0; i < nd; i++) {
      vlog_node_t d = vlog_decl(fn, i);
      const vlog_kind_t dk = vlog_kind(d);
      if (dk == V_LOCAL_DECL || dk == V_VAR_DECL || dk == V_TF_PORT_DECL) {
         if (vlog_has_type(d) && (vlog_is_dynamic_type(vlog_type(d))
                                  || vlog_is_real(d))) {
            APOP(cc->active_funcs);
            fail_proc(cc, IPROC_EJ_DYNAMIC_OP);
            return false;
         }
         hset_insert(cc->locals, d);
         if (vlog_has_value(d) && !check_expr(cc, vlog_value(d))) {
            APOP(cc->active_funcs);
            return false;
         }
      }
   }

   bool saved_in_func = cc->in_func;
   bool saved_nba = cc->allow_nbassign;
   bool saved_mod_b = cc->allow_mod_bassign;
   cc->in_func = true;
   cc->allow_nbassign = false;
   cc->allow_mod_bassign = false;

   bool ok = true;
   const unsigned ns = vlog_stmts(fn);
   for (unsigned i = 0; i < ns && ok; i++)
      ok = check_stmt(cc, vlog_stmt(fn, i));

   cc->in_func = saved_in_func;
   cc->allow_nbassign = saved_nba;
   cc->allow_mod_bassign = saved_mod_b;
   APOP(cc->active_funcs);
   return ok;
}

static bool check_expr(check_ctx_t *cc, vlog_node_t v)
{
   if (v == NULL)
      return true;

   cc->proc->node_count++;

   switch (vlog_kind(v)) {
   case V_NUMBER:
      return true;

   case V_STRING:
      return true;

   case V_REAL:
      fail_proc(cc, IPROC_EJ_UNSUPPORTED_EXPR);
      return false;

   case V_REF:
      {
         if (!vlog_has_ref(v)) {
            fail_proc(cc, IPROC_EJ_UNSUPPORTED_EXPR);
            return false;
         }
         vlog_node_t d = vlog_ref(v);
         switch (vlog_kind(d)) {
         case V_PARAM_DECL:
         case V_LOCALPARAM:
         case V_ENUM_NAME:
         case V_GENVAR_DECL:
            return true;
         case V_LOCAL_DECL:
         case V_TF_PORT_DECL:
            if (hset_contains(cc->locals, d))
               return true;
            fail_proc(cc, IPROC_EJ_UNSUPPORTED_EXPR);
            return false;
         case V_VAR_DECL:
         case V_NET_DECL:
         case V_PORT_DECL:
            if (hset_contains(cc->locals, d))
               return true;
            {
               const int idx = find_sig_idx(cc->ctx, d);
               if (idx < 0) {
                  fail_proc(cc, IPROC_EJ_UNSUPPORTED_EXPR);
                  return false;
               }
               const sig_meta_t *s = &(cc->ctx->sigs[idx]);
               if (!s->eligible) {
                  fail_proc(cc, s->eject_reason == ISIG_EJ_DYNAMIC_TYPE
                            ? IPROC_EJ_DYNAMIC_OP : IPROC_EJ_UNSUPPORTED_EXPR);
                  return false;
               }
               add_u32_unique(&cc->proc->reads.items, &cc->proc->reads.count,
                              cc->read_set, (uint32_t)idx);
               return true;
            }
         default:
            fail_proc(cc, IPROC_EJ_UNSUPPORTED_EXPR);
            return false;
         }
      }

   case V_HIER_REF:
      fail_proc(cc, IPROC_EJ_HIER_REF);
      return false;

   case V_BIT_SELECT:
      {
         vlog_node_t base = vlog_value(v);
         vlog_node_t bt = vlog_get_type(base);
         if (bt != NULL && vlog_kind(bt) == V_DATA_TYPE
             && (vlog_subkind(bt) == DT_STRING
                 || vlog_subkind(bt) == DT_DYNARRAY
                 || vlog_subkind(bt) == DT_QUEUE
                 || vlog_subkind(bt) == DT_ASSOC)) {
            fail_proc(cc, IPROC_EJ_DYNAMIC_OP);
            return false;
         }
         if (!check_expr(cc, base))
            return false;
         for (unsigned i = 0; i < vlog_params(v); i++) {
            if (!check_expr(cc, vlog_param(v, i)))
               return false;
         }
         return true;
      }

   case V_PART_SELECT:
      if (!check_expr(cc, vlog_value(v)) || !check_expr(cc, vlog_left(v)))
         return false;
      if (vlog_has_right(v) && !check_expr(cc, vlog_right(v)))
         return false;
      return true;

   case V_MEMBER_REF:
      {
         vlog_node_t base_t = vlog_get_type(vlog_value(v));
         if (vlog_virtual_pending(v) || base_t == NULL
             || vlog_is_dynamic_type(base_t)
             || (vlog_kind(base_t) == V_UNION_DECL
                 && (vlog_flags(base_t) & VLOG_F_TAGGED))) {
            fail_proc(cc, IPROC_EJ_DYNAMIC_OP);
            return false;
         }
         return check_expr(cc, vlog_value(v));
      }

   case V_CONCAT:
      if (vlog_has_value(v) && !check_expr(cc, vlog_value(v)))
         return false;
      for (unsigned i = 0; i < vlog_params(v); i++) {
         if (!check_expr(cc, vlog_param(v, i)))
            return false;
      }
      return true;

   case V_UNARY:
   case V_MIN_TYP_MAX:
      if (vlog_is_real(v)) {
         fail_proc(cc, IPROC_EJ_UNSUPPORTED_EXPR);
         return false;
      }
      return check_expr(cc, vlog_value(v));

   case V_BINARY:
      if (vlog_is_real(v)) {
         fail_proc(cc, IPROC_EJ_UNSUPPORTED_EXPR);
         return false;
      }
      return check_expr(cc, vlog_left(v)) && check_expr(cc, vlog_right(v));

   case V_COND_EXPR:
      if (vlog_is_real(v)) {
         fail_proc(cc, IPROC_EJ_UNSUPPORTED_EXPR);
         return false;
      }
      return check_expr(cc, vlog_value(v))
          && check_expr(cc, vlog_left(v))
          && check_expr(cc, vlog_right(v));

   case V_CAST:
      if (vlog_subkind(v) == V_CAST_STRING
          || vlog_subkind(v) == V_CAST_TYPEREF) {
         fail_proc(cc, IPROC_EJ_DYNAMIC_OP);
         return false;
      }
      if (vlog_subkind(v) == V_CAST_TYPE && vlog_has_type(v)) {
         vlog_node_t ct = vlog_get_type(vlog_type(v));
         if (ct == NULL || vlog_is_dynamic_type(ct)) {
            fail_proc(cc, IPROC_EJ_DYNAMIC_OP);
            return false;
         }
      }
      if (vlog_subkind(v) == V_CAST_WIDTH && vlog_has_left(v)
          && !check_expr(cc, vlog_left(v)))
         return false;
      return check_expr(cc, vlog_value(v));

   case V_INSIDE:
      if (!check_expr(cc, vlog_value(v)))
         return false;
      for (unsigned i = 0; i < vlog_params(v); i++) {
         if (!check_expr(cc, vlog_param(v, i)))
            return false;
      }
      return true;

   case V_INSIDE_RANGE:
      return check_expr(cc, vlog_left(v)) && check_expr(cc, vlog_right(v));

   case V_PATTERN_EXPR:
      for (unsigned i = 0; i < vlog_params(v); i++) {
         if (!check_expr(cc, vlog_param(v, i)))
            return false;
      }
      return true;

   case V_PATTERN_ITEM:
      return !vlog_has_right(v) || check_expr(cc, vlog_right(v));

   case V_USER_FCALL:
      if (!vlog_has_ref(v) || vlog_kind(vlog_ref(v)) != V_FUNC_DECL) {
         fail_proc(cc, IPROC_EJ_UNSUPPORTED_EXPR);
         return false;
      }
      for (unsigned i = 0; i < vlog_params(v); i++) {
         if (!check_expr(cc, vlog_param(v, i)))
            return false;
      }
      return check_func_body(cc, vlog_ref(v));

   case V_SYS_FCALL:
      if (!is_pure_systf(v)) {
         fail_proc(cc, IPROC_EJ_SYSCALL);
         return false;
      }
      for (unsigned i = 0; i < vlog_params(v); i++) {
         if (!check_expr(cc, vlog_param(v, i)))
            return false;
      }
      return true;

   case V_PREFIX:
   case V_POSTFIX:
      if (!check_lvalue(cc, vlog_target(v), false, true))
         return false;
      record_lvalue_write_ranges(cc, vlog_target(v));
      return true;

   case V_METHOD_CALL:
   case V_CLASS_NEW:
   case V_DYNAMIC_NEW:
   case V_THIS:
   case V_NULL:
   case V_STD_RANDOMIZE:
   case V_STREAM:
      fail_proc(cc, IPROC_EJ_DYNAMIC_OP);
      return false;

   default:
      fail_proc(cc, IPROC_EJ_UNSUPPORTED_EXPR);
      return false;
   }
}

static bool check_stmt(check_ctx_t *cc, vlog_node_t v)
{
   if (v == NULL)
      return true;

   cc->proc->node_count++;

   switch (vlog_kind(v)) {
   case V_EMPTY:
      return true;

   case V_ALWAYS:
   case V_INITIAL:
   case V_FINAL:
   case V_SEQ_BLOCK:
      {
         if (vlog_flags(v) & VLOG_F_NAMED_DISABLE) {
            fail_proc(cc, IPROC_EJ_UNSUPPORTED_STMT);
            return false;
         }

         const unsigned nd = vlog_decls(v);
         for (unsigned i = 0; i < nd; i++) {
            vlog_node_t d = vlog_decl(v, i);
            const vlog_kind_t dk = vlog_kind(d);
            if (dk == V_LOCAL_DECL || dk == V_VAR_DECL) {
               if (vlog_has_type(d) && (vlog_is_dynamic_type(vlog_type(d))
                                        || vlog_is_real(d))) {
                  fail_proc(cc, IPROC_EJ_DYNAMIC_OP);
                  return false;
               }
               hset_insert(cc->locals, d);
               if (vlog_has_value(d) && !check_expr(cc, vlog_value(d)))
                  return false;
            }
            else if (dk != V_LOCALPARAM && dk != V_TYPE_DECL) {
               fail_proc(cc, IPROC_EJ_UNSUPPORTED_STMT);
               return false;
            }
         }

         const unsigned ns = vlog_stmts(v);
         for (unsigned i = 0; i < ns; i++) {
            if (!check_stmt(cc, vlog_stmt(v, i)))
               return false;
         }
         return true;
      }

   case V_BASSIGN:
      if (vlog_has_delay(v)) {
         fail_proc(cc, IPROC_EJ_DELAY);
         return false;
      }
      if (!check_lvalue(cc, vlog_target(v), false, false))
         return false;
      record_lvalue_write_ranges(cc, vlog_target(v));
      return check_expr(cc, vlog_value(v));

   case V_OP_ASSIGN:
      if (!check_lvalue(cc, vlog_target(v), false, true))
         return false;
      record_lvalue_write_ranges(cc, vlog_target(v));
      return check_expr(cc, vlog_value(v));

   case V_PREFIX:
   case V_POSTFIX:
      if (!check_lvalue(cc, vlog_target(v), false, true))
         return false;
      record_lvalue_write_ranges(cc, vlog_target(v));
      return true;

   case V_ASSIGN:
   case V_NBASSIGN:
      if (!cc->allow_nbassign) {
         fail_proc(cc, IPROC_EJ_UNSUPPORTED_STMT);
         return false;
      }
      if (vlog_has_delay(v)) {
         fail_proc(cc, IPROC_EJ_DELAY);
         return false;
      }
      if (!check_lvalue(cc, vlog_target(v), true, false))
         return false;
      record_lvalue_write_ranges(cc, vlog_target(v));
      return check_expr(cc, vlog_value(v));

   case V_IF:
      for (unsigned i = 0; i < vlog_conds(v); i++) {
         vlog_node_t c = vlog_cond(v, i);
         if (vlog_has_value(c) && !check_expr(cc, vlog_value(c)))
            return false;
         for (unsigned j = 0; j < vlog_stmts(c); j++) {
            if (!check_stmt(cc, vlog_stmt(c, j)))
               return false;
         }
      }
      return true;

   case V_CASE:
      if (!check_expr(cc, vlog_value(v)))
         return false;
      for (unsigned i = 0; i < vlog_stmts(v); i++) {
         vlog_node_t item = vlog_stmt(v, i);
         for (unsigned p = 0; p < vlog_params(item); p++) {
            if (!check_expr(cc, vlog_param(item, p)))
               return false;
         }
         for (unsigned j = 0; j < vlog_stmts(item); j++) {
            if (!check_stmt(cc, vlog_stmt(item, j)))
               return false;
         }
      }
      return true;

   case V_FOR_LOOP:
      {
         vlog_node_t init = vlog_left(v);
         const unsigned nd = vlog_decls(init);
         for (unsigned i = 0; i < nd; i++) {
            vlog_node_t d = vlog_decl(init, i);
            if (vlog_has_type(d) && (vlog_is_dynamic_type(vlog_type(d))
                                     || vlog_is_real(d))) {
               fail_proc(cc, IPROC_EJ_DYNAMIC_OP);
               return false;
            }
            hset_insert(cc->locals, d);
            if (vlog_has_value(d) && !check_expr(cc, vlog_value(d)))
               return false;
         }
         for (unsigned i = 0; i < vlog_stmts(init); i++) {
            if (!check_stmt(cc, vlog_stmt(init, i)))
               return false;
         }
         if (vlog_has_value(v) && !check_expr(cc, vlog_value(v)))
            return false;

         vlog_node_t step = vlog_right(v);
         for (unsigned i = 0; i < vlog_stmts(step); i++) {
            if (!check_stmt(cc, vlog_stmt(step, i)))
               return false;
         }
         for (unsigned i = 0; i < vlog_stmts(v); i++) {
            if (!check_stmt(cc, vlog_stmt(v, i)))
               return false;
         }
         return true;
      }

   case V_WHILE:
   case V_DO_WHILE:
   case V_REPEAT:
      if (!check_expr(cc, vlog_value(v)))
         return false;
      for (unsigned i = 0; i < vlog_stmts(v); i++) {
         if (!check_stmt(cc, vlog_stmt(v, i)))
            return false;
      }
      return true;

   case V_LOOP_JUMP:
      return true;

   case V_FOREACH:
      {
         const unsigned nd = vlog_decls(v);
         for (unsigned i = 0; i < nd; i++)
            hset_insert(cc->locals, vlog_decl(v, i));
         if (!check_expr(cc, vlog_value(v)))
            return false;
         for (unsigned i = 0; i < vlog_stmts(v); i++) {
            if (!check_stmt(cc, vlog_stmt(v, i)))
               return false;
         }
         return true;
      }

   case V_RETURN:
      if (!cc->in_func) {
         fail_proc(cc, IPROC_EJ_UNSUPPORTED_STMT);
         return false;
      }
      return !vlog_has_value(v) || check_expr(cc, vlog_value(v));

   case V_DELAY_CONTROL:
      fail_proc(cc, IPROC_EJ_DELAY);
      return false;

   case V_TIMING:
      if (vlog_has_value(v) && vlog_kind(vlog_value(v)) == V_DELAY_CONTROL)
         fail_proc(cc, IPROC_EJ_DELAY);
      else
         fail_proc(cc, IPROC_EJ_EVENT_OR_WAIT);
      return false;

   case V_WAIT:
   case V_EVENT_TRIGGER:
   case V_FOREVER:
      fail_proc(cc, IPROC_EJ_EVENT_OR_WAIT);
      return false;

   case V_FORK:
   case V_FORK_CTRL:
      fail_proc(cc, IPROC_EJ_FORK_JOIN);
      return false;

   case V_FORCE:
   case V_RELEASE:
   case V_DEASSIGN:
      fail_proc(cc, IPROC_EJ_FORCE_RELEASE);
      return false;

   case V_ASSERT:
   case V_ASSUME:
   case V_COVER:
   case V_EXPECT:
   case V_SVA_OP:
   case V_PROP_SPEC:
      fail_proc(cc, IPROC_EJ_SVA);
      return false;

   case V_SYS_TCALL:
      fail_proc(cc, IPROC_EJ_SYSCALL);
      return false;

   case V_VOID_CALL:
      if (vlog_has_value(v) && vlog_kind(vlog_value(v)) == V_USER_FCALL)
         return check_expr(cc, vlog_value(v));
      fail_proc(cc, IPROC_EJ_SYSCALL);
      return false;

   default:
      fail_proc(cc, IPROC_EJ_UNSUPPORTED_STMT);
      return false;
   }
}

static bool expr_tests_sig(vlog_node_t expr, vlog_node_t decl)
{
   if (expr == NULL)
      return false;

   switch (vlog_kind(expr)) {
   case V_REF:
      return vlog_has_ref(expr) && unwrap_decl(vlog_ref(expr)) == decl;
   case V_UNARY:
      return expr_tests_sig(vlog_value(expr), decl);
   case V_BINARY:
      return expr_tests_sig(vlog_left(expr), decl)
          || expr_tests_sig(vlog_right(expr), decl);
   case V_COND_EXPR:
      return expr_tests_sig(vlog_value(expr), decl);
   default:
      return false;
   }
}

static vlog_node_t first_exec_stmt(vlog_node_t s)
{
   while (s != NULL && vlog_kind(s) == V_SEQ_BLOCK) {
      if (vlog_stmts(s) == 0)
         return NULL;
      s = vlog_stmt(s, 0);
   }
   return s;
}

static void check_assign_proc(analyze_ctx_t *ctx, proc_meta_t *pm,
                              vlog_node_t s)
{
   // Continuous assignments in a mixed VHDL-SV boundary module run as
   // stateless processes during model_reset and signal-update (reschedq)
   // so VHDL sees zero extra delta cycles across the language boundary.
   if (ctx->is_mixed) {
      pm->eject_reason = IPROC_EJ_UNSUPPORTED_STMT;
      return;
   }

   if (vlog_has_delay(s)) {
      pm->eject_reason = IPROC_EJ_DELAY;
      return;
   }

   // Check net declaration delay if target is a net
   vlog_node_t tgt = vlog_target(s);
   if (vlog_kind(tgt) == V_REF && vlog_has_ref(tgt)
       && vlog_kind(vlog_ref(tgt)) == V_NET_DECL
       && vlog_has_delay(vlog_ref(tgt))) {
      pm->eject_reason = IPROC_EJ_DELAY;
      return;
   }

   check_ctx_t cc = {
      .ctx               = ctx,
      .proc              = pm,
      .locals            = hset_new(8),
      .read_set          = hset_new(16),
      .write_set         = hset_new(8),
      .allow_nbassign    = false,
      .allow_mod_bassign = true,
   };

   if (check_lvalue(&cc, tgt, false, false)
       && check_expr(&cc, vlog_value(s))) {
      record_lvalue_write_ranges(&cc, tgt);
      pm->eligible = true;
      pm->kind = IPROC_ASSIGN;
   }
   else
      pm->eject_reason = cc.reject ?: IPROC_EJ_UNSUPPORTED_EXPR;

   hset_free(cc.locals);
   hset_free(cc.read_set);
   hset_free(cc.write_set);
   ACLEAR(cc.active_funcs);
}

static bool event_to_sig(analyze_ctx_t *ctx, vlog_node_t ev, int *sig_idx,
                         bool *is_posedge)
{
   if (vlog_kind(ev) != V_EVENT || vlog_has_left(ev))
      return false;

   const v_event_kind_t ek = vlog_subkind(ev);
   if (ek != V_EVENT_POSEDGE && ek != V_EVENT_NEGEDGE)
      return false;

   vlog_node_t val = vlog_value(ev);
   if (vlog_kind(val) != V_REF || !vlog_has_ref(val))
      return false;

   const int idx = find_sig_idx(ctx, vlog_ref(val));
   if (idx < 0 || !ctx->sigs[idx].eligible || ctx->sigs[idx].width != 1
       || ctx->sigs[idx].nelems != 1)
      return false;

   *sig_idx = idx;
   *is_posedge = (ek == V_EVENT_POSEDGE);
   return true;
}

static bool sensitivity_covers_reads(analyze_ctx_t *ctx, vlog_node_t ctrl,
                                     const proc_meta_t *pm)
{
   hset_t *sens = hset_new(16);
   const unsigned n = vlog_params(ctrl);
   for (unsigned i = 0; i < n; i++) {
      vlog_node_t ev = vlog_param(ctrl, i);
      if (vlog_kind(ev) != V_EVENT || vlog_has_left(ev)
          || vlog_subkind(ev) != V_EVENT_LEVEL) {
         hset_free(sens);
         return false;
      }
      vlog_node_t val = vlog_value(ev);
      while (vlog_kind(val) == V_BIT_SELECT || vlog_kind(val) == V_PART_SELECT
             || vlog_kind(val) == V_MEMBER_REF)
         val = vlog_value(val);

      if (vlog_kind(val) == V_REF && vlog_has_ref(val)) {
         const int idx = find_sig_idx(ctx, vlog_ref(val));
         if (idx >= 0)
            hset_insert(sens, (void *)(uintptr_t)(idx + 1));
      }
   }

   bool ok = true;
   for (uint32_t i = 0; i < pm->reads.count && ok; i++) {
      if (!hset_contains(sens, (void *)(uintptr_t)(pm->reads.items[i] + 1)))
         ok = false;
   }

   hset_free(sens);
   return ok;
}

static void check_always_proc(analyze_ctx_t *ctx, proc_meta_t *pm,
                              vlog_node_t s)
{
   if (vlog_has_ident(s)) {
      const char *pname = istr(vlog_ident(s));
      if (pname[0] == '$') {
         if (strncmp(pname, "$postponed_s", 12) == 0)
            pm->eject_reason = IPROC_EJ_SYSCALL;
         else
            pm->eject_reason = IPROC_EJ_SVA;
         return;
      }
   }

   const vlog_always_kind_t ak = vlog_subkind(s);

   if (ak == V_ALWAYS_LATCH || ak == V_ALWAYS_LATCH_DONE) {
      pm->eject_reason = IPROC_EJ_LATCH;
      return;
   }

   if (vlog_stmts(s) == 0) {
      pm->eject_reason = IPROC_EJ_UNSUPPORTED_STMT;
      return;
   }

   if (ak == V_ALWAYS_COMB || ak == V_ALWAYS_COMB_DONE) {
      check_ctx_t cc = {
         .ctx               = ctx,
         .proc              = pm,
         .locals            = hset_new(16),
         .read_set          = hset_new(16),
         .write_set         = hset_new(16),
         .allow_nbassign    = false,
         .allow_mod_bassign = true,
      };

      bool ok = true;
      vlog_node_t top = vlog_stmt(s, 0);
      if (vlog_kind(top) == V_SEQ_BLOCK && vlog_stmts(top) > 0) {
         const unsigned nd = vlog_decls(top);
         for (unsigned i = 0; i < nd && ok; i++) {
            vlog_node_t d = vlog_decl(top, i);
            if (vlog_kind(d) == V_LOCAL_DECL || vlog_kind(d) == V_VAR_DECL) {
               if (vlog_has_type(d) && (vlog_is_dynamic_type(vlog_type(d))
                                        || vlog_is_real(d))) {
                  fail_proc(&cc, IPROC_EJ_DYNAMIC_OP);
                  ok = false;
                  break;
               }
               hset_insert(cc.locals, d);
               if (vlog_has_value(d))
                  ok = check_expr(&cc, vlog_value(d));
            }
         }

         const unsigned ns = vlog_stmts(top);
         for (unsigned i = 0; i < ns && ok; i++) {
            vlog_node_t st = vlog_stmt(top, i);
            if (i == ns - 1 && vlog_kind(st) == V_TIMING
                && vlog_stmts(st) == 0)
               break;  // Trailing wait synthesized by vlog_simp
            ok = check_stmt(&cc, st);
         }
      }
      else {
         for (unsigned i = 0; i < vlog_stmts(s) && ok; i++)
            ok = check_stmt(&cc, vlog_stmt(s, i));
      }

      if (ok) {
         pm->eligible = true;
         pm->kind = IPROC_COMB;
      }
      else
         pm->eject_reason = cc.reject ?: IPROC_EJ_UNSUPPORTED_STMT;

      hset_free(cc.locals);
      hset_free(cc.read_set);
      hset_free(cc.write_set);
      ACLEAR(cc.active_funcs);
      return;
   }

   // V_ALWAYS_PLAIN or V_ALWAYS_FF: locate leading V_TIMING
   vlog_node_t top = vlog_stmt(s, 0), timing = top, blk = NULL;
   if (vlog_stmts(s) == 1 && vlog_kind(top) == V_SEQ_BLOCK
       && vlog_stmts(top) > 0) {
      blk = top;
      timing = vlog_stmt(blk, 0);
   }

   if (vlog_kind(timing) != V_TIMING) {
      pm->eject_reason = IPROC_EJ_EVENT_OR_WAIT;
      return;
   }

   vlog_node_t ctrl = vlog_value(timing);
   if (vlog_kind(ctrl) == V_DELAY_CONTROL) {
      pm->eject_reason = IPROC_EJ_DELAY;
      return;
   }
   else if (vlog_kind(ctrl) != V_EVENT_CONTROL) {
      pm->eject_reason = IPROC_EJ_EVENT_OR_WAIT;
      return;
   }

   A(vlog_node_t) body_stmts = AINIT;
   for (unsigned i = 0; i < vlog_stmts(timing); i++)
      APUSH(body_stmts, vlog_stmt(timing, i));
   if (blk != NULL) {
      for (unsigned i = 1; i < vlog_stmts(blk); i++)
         APUSH(body_stmts, vlog_stmt(blk, i));
   }

   const unsigned np = vlog_params(ctrl);
   if (np == 0) {
      pm->eject_reason = IPROC_EJ_NON_RTL_SENSITIVITY;
      ACLEAR(body_stmts);
      return;
   }

   bool all_level = true, all_edge = true;
   for (unsigned i = 0; i < np; i++) {
      vlog_node_t ev = vlog_param(ctrl, i);
      if (vlog_kind(ev) != V_EVENT || vlog_has_left(ev)) {
         all_level = all_edge = false;
         break;
      }
      const v_event_kind_t ek = vlog_subkind(ev);
      if (ek != V_EVENT_LEVEL)
         all_level = false;
      if (ek != V_EVENT_POSEDGE && ek != V_EVENT_NEGEDGE)
         all_edge = false;
   }

   if (all_level && ak == V_ALWAYS_PLAIN) {
      check_ctx_t cc = {
         .ctx               = ctx,
         .proc              = pm,
         .locals            = hset_new(16),
         .read_set          = hset_new(16),
         .write_set         = hset_new(16),
         .allow_nbassign    = false,
         .allow_mod_bassign = true,
      };

      bool ok = true;
      for (unsigned i = 0; i < body_stmts.count && ok; i++)
         ok = check_stmt(&cc, body_stmts.items[i]);

      if (ok && !sensitivity_covers_reads(ctx, ctrl, pm)) {
         ok = false;
         cc.reject = IPROC_EJ_NON_RTL_SENSITIVITY;
      }

      if (ok) {
         pm->eligible = true;
         pm->kind = IPROC_COMB;
      }
      else
         pm->eject_reason = cc.reject ?: IPROC_EJ_UNSUPPORTED_STMT;

      hset_free(cc.locals);
      hset_free(cc.read_set);
      hset_free(cc.write_set);
      ACLEAR(cc.active_funcs);
      ACLEAR(body_stmts);
      return;
   }

   if (!all_edge) {
      pm->eject_reason = IPROC_EJ_NON_RTL_SENSITIVITY;
      ACLEAR(body_stmts);
      return;
   }

   if (np > 2) {
      pm->eject_reason = IPROC_EJ_MULTI_CLOCK;
      ACLEAR(body_stmts);
      return;
   }

   int sig0 = -1, sig1 = -1;
   bool pos0 = false, pos1 = false;
   if (!event_to_sig(ctx, vlog_param(ctrl, 0), &sig0, &pos0)
       || (np == 2 && !event_to_sig(ctx, vlog_param(ctrl, 1), &sig1, &pos1))) {
      pm->eject_reason = IPROC_EJ_NON_RTL_SENSITIVITY;
      ACLEAR(body_stmts);
      return;
   }

   if (np == 1) {
      pm->clk_sig_idx = sig0;
      pm->clk_posedge = pos0;
      pm->rst_sig_idx = -1;
      pm->rst_posedge = false;
   }
   else {
      if (sig0 == sig1) {
         pm->eject_reason = IPROC_EJ_MULTI_CLOCK;
         ACLEAR(body_stmts);
         return;
      }

      vlog_node_t first = body_stmts.count > 0
         ? first_exec_stmt(body_stmts.items[0]) : NULL;
      if (first == NULL || vlog_kind(first) != V_IF || vlog_conds(first) == 0
          || !vlog_has_value(vlog_cond(first, 0))) {
         pm->eject_reason = IPROC_EJ_MULTI_CLOCK;
         ACLEAR(body_stmts);
         return;
      }

      vlog_node_t cond0 = vlog_value(vlog_cond(first, 0));
      const bool tests0 = expr_tests_sig(cond0, ctx->sigs[sig0].decl);
      const bool tests1 = expr_tests_sig(cond0, ctx->sigs[sig1].decl);
      if (tests0 == tests1) {
         pm->eject_reason = IPROC_EJ_MULTI_CLOCK;
         ACLEAR(body_stmts);
         return;
      }

      if (tests1) {
         pm->clk_sig_idx = sig0;
         pm->clk_posedge = pos0;
         pm->rst_sig_idx = sig1;
         pm->rst_posedge = pos1;
      }
      else {
         pm->clk_sig_idx = sig1;
         pm->clk_posedge = pos1;
         pm->rst_sig_idx = sig0;
         pm->rst_posedge = pos0;
      }
   }

   check_ctx_t cc = {
      .ctx               = ctx,
      .proc              = pm,
      .locals            = hset_new(16),
      .read_set          = hset_new(16),
      .write_set         = hset_new(16),
      .allow_nbassign    = true,
      .allow_mod_bassign = false,
   };

   bool ok = true;
   for (unsigned i = 0; i < body_stmts.count && ok; i++)
      ok = check_stmt(&cc, body_stmts.items[i]);

   if (ok) {
      pm->eligible = true;
      pm->kind = IPROC_FF;
   }
   else
      pm->eject_reason = cc.reject ?: IPROC_EJ_UNSUPPORTED_STMT;

   hset_free(cc.locals);
   hset_free(cc.read_set);
   hset_free(cc.write_set);
   ACLEAR(cc.active_funcs);
   ACLEAR(body_stmts);
}

static void mark_ejected_level_watches(analyze_ctx_t *ctx, vlog_node_t s)
{
   if (vlog_kind(s) != V_ALWAYS || vlog_stmts(s) == 0)
      return;
   const vlog_always_kind_t ak = vlog_subkind(s);
   if (ak == V_ALWAYS_COMB || ak == V_ALWAYS_COMB_DONE
       || ak == V_ALWAYS_LATCH || ak == V_ALWAYS_LATCH_DONE)
      return;

   vlog_node_t top = vlog_stmt(s, 0);
   vlog_node_t timing = top;
   if (vlog_stmts(s) == 1 && vlog_kind(top) == V_SEQ_BLOCK
       && vlog_stmts(top) > 0)
      timing = vlog_stmt(top, 0);
   if (vlog_kind(timing) != V_TIMING || !vlog_has_value(timing))
      return;

   vlog_node_t ctrl = vlog_value(timing);
   if (vlog_kind(ctrl) != V_EVENT_CONTROL)
      return;

   const unsigned np = vlog_params(ctrl);
   for (unsigned i = 0; i < np; i++) {
      vlog_node_t ev = vlog_param(ctrl, i);
      if (vlog_kind(ev) != V_EVENT || vlog_subkind(ev) != V_EVENT_LEVEL)
         continue;
      vlog_node_t val = vlog_value(ev);
      while (vlog_kind(val) == V_BIT_SELECT || vlog_kind(val) == V_PART_SELECT
             || vlog_kind(val) == V_MEMBER_REF)
         val = vlog_value(val);
      if (vlog_kind(val) == V_REF && vlog_has_ref(val)) {
         const int idx = find_sig_idx(ctx, vlog_ref(val));
         if (idx >= 0)
            ctx->sigs[idx].level_watched = true;
      }
   }
}

static void mark_ejected_proc_refs(analyze_ctx_t *ctx, vlog_node_t s)
{
   collect_stmt_writes_ext(ctx, s);
   mark_ejected_level_watches(ctx, s);

   ref_scan_t rs = {
      .ctx           = ctx,
      .mark_ext_read = true,
   };
   vlog_visit(s, scan_refs_cb, &rs);
}

static void check_all_procs(analyze_ctx_t *ctx)
{
   const int nstmts = vlog_stmts(ctx->body);
   ctx->cap_procs = MAX(8u, (uint32_t)nstmts + ctx->gen_procs.count);
   ctx->procs = xcalloc_array(ctx->cap_procs, sizeof(proc_meta_t));

   for (int i = 0; i < nstmts; i++) {
      vlog_node_t s = vlog_stmt(ctx->body, i);
      const vlog_kind_t k = vlog_kind(s);

      switch (k) {
      case V_INST_LIST:
      case V_GEN_BLOCK:
      case V_IF_GENERATE:
      case V_FOR_GENERATE:
      case V_DEFPARAM:
         continue;
      default:
         break;
      }

      uint32_t pidx = ctx->nprocs++;
      if (pidx >= ctx->cap_procs) {
         ctx->cap_procs *= 2;
         ctx->procs = xrealloc_array(ctx->procs, ctx->cap_procs,
                                     sizeof(proc_meta_t));
      }

      proc_meta_t *pm = &(ctx->procs[pidx]);
      memset(pm, 0, sizeof(*pm));
      pm->stmt = s;
      pm->orig_stmt = s;
      pm->from_gen = false;
      pm->clk_sig_idx = -1;
      pm->rst_sig_idx = -1;
      pm->island_idx = -1;

      switch (k) {
      case V_ASSIGN:
         check_assign_proc(ctx, pm, s);
         break;
      case V_ALWAYS:
         check_always_proc(ctx, pm, s);
         break;
      case V_INITIAL:
         pm->eject_reason = IPROC_EJ_INITIAL;
         break;
      case V_FINAL:
         pm->eject_reason = IPROC_EJ_FINAL;
         break;
      case V_GATE_INST:
      case V_UDP_TABLE:
         pm->eject_reason = IPROC_EJ_GATE_OR_UDP;
         break;
      case V_SPECIFY:
         pm->eject_reason = IPROC_EJ_SVA;
         break;
      default:
         pm->eject_reason = IPROC_EJ_UNSUPPORTED_STMT;
         break;
      }

      if (!pm->eligible) {
         ACLEAR(pm->reads);
         ACLEAR(pm->writes);
         ACLEAR(pm->write_ranges);
         if (k == V_ASSIGN || k == V_ALWAYS)
            mark_ejected_proc_refs(ctx, s);
      }
   }

   for (unsigned i = 0; i < ctx->gen_procs.count; i++) {
      vlog_node_t s = ctx->gen_procs.items[i].stmt;
      vlog_node_t orig = ctx->gen_procs.items[i].orig_stmt;
      const vlog_kind_t k = vlog_kind(s);

      uint32_t pidx = ctx->nprocs++;
      if (pidx >= ctx->cap_procs) {
         ctx->cap_procs *= 2;
         ctx->procs = xrealloc_array(ctx->procs, ctx->cap_procs,
                                     sizeof(proc_meta_t));
      }

      proc_meta_t *pm = &(ctx->procs[pidx]);
      memset(pm, 0, sizeof(*pm));
      pm->stmt = s;
      pm->orig_stmt = orig;
      pm->from_gen = true;
      pm->clk_sig_idx = -1;
      pm->rst_sig_idx = -1;
      pm->island_idx = -1;

      if (k == V_ASSIGN)
         check_assign_proc(ctx, pm, s);
      else if (k == V_ALWAYS)
         check_always_proc(ctx, pm, s);
      else
         pm->eject_reason = IPROC_EJ_UNSUPPORTED_STMT;

      if (!pm->eligible) {
         ACLEAR(pm->reads);
         ACLEAR(pm->writes);
         ACLEAR(pm->write_ranges);
         if (k == V_ASSIGN || k == V_ALWAYS)
            mark_ejected_proc_refs(ctx, s);
      }
   }
}

static bool sv_procs_write_conflict(const proc_meta_t *a, const proc_meta_t *b,
                                    uint32_t si)
{
   if ((a->kind == IPROC_FF) != (b->kind == IPROC_FF)) {
      // Allow disjoint comb + FF writes on the same signal when the comb
      // side is constant (e.g. tied-off generate elements alongside FFs).
      const proc_meta_t *comb = (a->kind == IPROC_FF) ? b : a;
      if (comb->reads.count > 0)
         return true;
   }
   else if (a->kind == IPROC_FF) {
      if (a->clk_sig_idx != b->clk_sig_idx || a->clk_posedge != b->clk_posedge
          || a->rst_sig_idx != b->rst_sig_idx
          || (a->rst_sig_idx >= 0 && a->rst_posedge != b->rst_posedge))
         return true;
   }

   bool a_has = false, b_has = false;
   for (uint32_t i = 0; i < a->write_ranges.count; i++) {
      const sv_write_range_t ra = a->write_ranges.items[i];
      if (ra.sig_idx != si)
         continue;
      a_has = true;
      for (uint32_t j = 0; j < b->write_ranges.count; j++) {
         const sv_write_range_t rb = b->write_ranges.items[j];
         if (rb.sig_idx != si)
            continue;
         b_has = true;
         if (ra.bit_lo <= rb.bit_hi && rb.bit_lo <= ra.bit_hi)
            return true;
      }
   }
   return !a_has || !b_has;
}

static bool sv_output_port_fully_written(const analyze_ctx_t *ctx, uint32_t si)
{
   const sig_meta_t *sm = &(ctx->sigs[si]);
   const uint64_t total_bits = (uint64_t)sm->width * sm->nelems;
   if (total_bits == 0)
      return false;

   A(sv_write_range_t) ranges = AINIT;
   for (uint32_t k = 0; k < ctx->nprocs; k++) {
      const proc_meta_t *q = &(ctx->procs[k]);
      if (!q->eligible)
         continue;
      for (uint32_t r = 0; r < q->write_ranges.count; r++) {
         if (q->write_ranges.items[r].sig_idx == si)
            APUSH(ranges, q->write_ranges.items[r]);
      }
   }

   if (ranges.count == 0) {
      ACLEAR(ranges);
      return false;
   }

   for (uint32_t i = 1; i < ranges.count; i++) {
      sv_write_range_t key = ranges.items[i];
      uint32_t j = i;
      while (j > 0 && ranges.items[j - 1].bit_lo > key.bit_lo) {
         ranges.items[j] = ranges.items[j - 1];
         j--;
      }
      ranges.items[j] = key;
   }

   uint64_t covered = 0;
   for (uint32_t i = 0; i < ranges.count; i++) {
      if (ranges.items[i].bit_lo > covered)
         break;
      if (ranges.items[i].bit_hi + 1u > covered)
         covered = ranges.items[i].bit_hi + 1u;
      if (covered >= total_bits)
         break;
   }

   ACLEAR(ranges);
   return covered >= total_bits;
}

static void resolve_multi_drivers(analyze_ctx_t *ctx)
{
   hset_t *ejected_origs = hset_new(16);
   bool changed = true;
   while (changed) {
      changed = false;

      // Record any generate template stmt that already has an ineligible iter
      for (uint32_t i = 0; i < ctx->nprocs; i++) {
         if (ctx->procs[i].from_gen && !ctx->procs[i].eligible)
            hset_insert(ejected_origs, ctx->procs[i].orig_stmt);
      }

      for (uint32_t i = 0; i < ctx->nprocs; i++) {
         proc_meta_t *p = &(ctx->procs[i]);
         if (!p->eligible)
            continue;

         bool conflict = false;
         if (p->from_gen && hset_contains(ejected_origs, p->orig_stmt))
            conflict = true;

         // Generate-local signals must remain purely island-internal
         for (uint32_t j = 0; j < p->reads.count && !conflict; j++) {
            const sig_meta_t *sm = &(ctx->sigs[p->reads.items[j]]);
            if (sm->is_gen_local
                && (sm->ext_read || sm->ext_written || sm->is_external))
               conflict = true;
         }
         if (!conflict && p->clk_sig_idx >= 0
             && ctx->sigs[p->clk_sig_idx].is_gen_local)
            conflict = true;
         if (!conflict && p->rst_sig_idx >= 0
             && ctx->sigs[p->rst_sig_idx].is_gen_local)
            conflict = true;

         for (uint32_t j = 0; j < p->writes.count && !conflict; j++) {
            const uint32_t si = p->writes.items[j];
            sig_meta_t *sm = &(ctx->sigs[si]);
            if (sm->is_gen_local
                && (sm->ext_read || sm->ext_written || sm->is_external)) {
               conflict = true;
               break;
            }
            if (sm->ext_written) {
               conflict = true;
               break;
            }
            if (sm->level_watched
                && (p->kind == IPROC_COMB || p->kind == IPROC_ASSIGN)) {
               conflict = true;
               break;
            }
            if (sm->is_output_port && !sv_output_port_fully_written(ctx, si)) {
               sm->ext_written = true;
               conflict = true;
               break;
            }
            if (sm->driver_count > 1) {
               for (uint32_t k = 0; k < ctx->nprocs; k++) {
                  if (k == i || !ctx->procs[k].eligible)
                     continue;
                  const proc_meta_t *q = &(ctx->procs[k]);
                  bool q_writes = false;
                  for (uint32_t w = 0; w < q->writes.count; w++) {
                     if (q->writes.items[w] == si) {
                        q_writes = true;
                        break;
                     }
                  }
                  if (q_writes && sv_procs_write_conflict(p, q, si)) {
                     sm->ext_written = true;
                     mark_sig_external(sm, ISIG_EJ_MULTI_DRIVER);
                     conflict = true;
                     break;
                  }
               }
            }
         }

         if (conflict) {
            p->eligible = false;
            p->eject_reason = IPROC_EJ_MULTI_DRIVER_TARGET;
            if (p->from_gen)
               hset_insert(ejected_origs, p->orig_stmt);
            ACLEAR(p->reads);
            ACLEAR(p->writes);
            ACLEAR(p->write_ranges);
            mark_ejected_proc_refs(ctx, p->stmt);
            changed = true;
         }
      }
   }
   hset_free(ejected_origs);

   for (uint32_t i = 0; i < ctx->nsigs; i++) {
      if (ctx->sigs[i].driver_count > 1 && ctx->sigs[i].ext_written)
         mark_sig_external(&ctx->sigs[i], ISIG_EJ_MULTI_DRIVER);
   }
}

static uint32_t uf_find(uint32_t *parent, uint32_t x)
{
   while (parent[x] != x) {
      parent[x] = parent[parent[x]];
      x = parent[x];
   }
   return x;
}

static void uf_union(uint32_t *parent, uint32_t a, uint32_t b)
{
   uint32_t ra = uf_find(parent, a);
   uint32_t rb = uf_find(parent, b);
   if (ra != rb)
      parent[rb] = ra;
}

static uint32_t partition_islands(analyze_ctx_t *ctx)
{
   uint32_t n_elig = 0;
   uint32_t total_nodes = 0;
   for (uint32_t i = 0; i < ctx->nprocs; i++) {
      if (ctx->procs[i].eligible) {
         n_elig++;
         total_nodes += ctx->procs[i].node_count;
      }
   }

   if (n_elig == 0)
      return 0;

   // Fast path in module mode: all eligible processes fit in a single island
   if (total_nodes <= NVC_ISLAND_MAX_NODES) {
      for (uint32_t i = 0; i < ctx->nprocs; i++) {
         if (ctx->procs[i].eligible)
            ctx->procs[i].island_idx = 0;
      }
      return 1;
   }

   // Split large modules by connected components over internal signals,
   // packing components into islands up to NVC_ISLAND_MAX_NODES
   uint32_t *parent = xmalloc_array(ctx->nprocs, sizeof(uint32_t));
   int *sig_owner = xmalloc_array(MAX(1u, ctx->nsigs), sizeof(int));
   hash_t *orig_owner = hash_new(32);
   for (uint32_t i = 0; i < ctx->nprocs; i++)
      parent[i] = i;
   for (uint32_t i = 0; i < ctx->nsigs; i++)
      sig_owner[i] = -1;

   for (uint32_t i = 0; i < ctx->nprocs; i++) {
      const proc_meta_t *pm = &(ctx->procs[i]);
      if (!pm->eligible)
         continue;

      if (pm->from_gen) {
         uintptr_t prev = (uintptr_t)hash_get(orig_owner, pm->orig_stmt);
         if (prev == 0)
            hash_put(orig_owner, pm->orig_stmt, (void *)(uintptr_t)(i + 1));
         else
            uf_union(parent, (uint32_t)(prev - 1), i);
      }

      for (uint32_t j = 0; j < pm->writes.count; j++) {
         uint32_t si = pm->writes.items[j];
         if (ctx->sigs[si].driver_count > 1 || ctx->sigs[si].is_gen_local
             || (!ctx->sigs[si].is_external && !ctx->sigs[si].is_input_port
                 && !ctx->sigs[si].is_output_port)) {
            if (sig_owner[si] < 0)
               sig_owner[si] = (int)i;
            else
               uf_union(parent, (uint32_t)sig_owner[si], i);
         }
      }
      for (uint32_t j = 0; j < pm->reads.count; j++) {
         uint32_t si = pm->reads.items[j];
         if (ctx->sigs[si].is_gen_local
             || (!ctx->sigs[si].is_external && !ctx->sigs[si].is_input_port
                 && !ctx->sigs[si].is_output_port)) {
            if (sig_owner[si] < 0)
               sig_owner[si] = (int)i;
            else
               uf_union(parent, (uint32_t)sig_owner[si], i);
         }
      }
   }
   hash_free(orig_owner);

   // Assign deterministic island ids respecting NVC_ISLAND_MAX_NODES
   int *comp_island = xmalloc_array(ctx->nprocs, sizeof(int));
   for (uint32_t i = 0; i < ctx->nprocs; i++)
      comp_island[i] = -1;

   uint32_t cur_island = 0;
   uint32_t cur_nodes = 0;

   for (uint32_t i = 0; i < ctx->nprocs; i++) {
      proc_meta_t *pm = &(ctx->procs[i]);
      if (!pm->eligible)
         continue;

      uint32_t root = uf_find(parent, i);
      if (comp_island[root] >= 0) {
         // Check if adding this proc keeps its island under budget
         // (if a huge component itself exceeds budget, break into slices)
         if (pm->island_idx < 0) {
            if (cur_nodes > 0 && cur_nodes + pm->node_count > NVC_ISLAND_MAX_NODES) {
               cur_island++;
               cur_nodes = 0;
            }
            pm->island_idx = (int)cur_island;
            cur_nodes += pm->node_count;
         }
      }
      else {
         // Measure component total
         uint32_t comp_nodes = 0;
         for (uint32_t k = i; k < ctx->nprocs; k++) {
            if (ctx->procs[k].eligible && uf_find(parent, k) == root)
               comp_nodes += ctx->procs[k].node_count;
         }

         if (comp_nodes <= NVC_ISLAND_MAX_NODES) {
            if (cur_nodes > 0 && cur_nodes + comp_nodes > NVC_ISLAND_MAX_NODES) {
               cur_island++;
               cur_nodes = 0;
            }
            comp_island[root] = (int)cur_island;
            for (uint32_t k = i; k < ctx->nprocs; k++) {
               if (ctx->procs[k].eligible && uf_find(parent, k) == root)
                  ctx->procs[k].island_idx = (int)cur_island;
            }
            cur_nodes += comp_nodes;
         }
         else {
            if (cur_nodes > 0) {
               cur_island++;
               cur_nodes = 0;
            }
            comp_island[root] = (int)cur_island;
            pm->island_idx = (int)cur_island;
            cur_nodes = pm->node_count;
         }
      }
   }

   const uint32_t nislands = cur_island + 1;

   // Any signal used across >1 island must be marked externally shared.
   // If a multi-driver signal had its writers split across >1 island (due to
   // budget slicing), eject its writers safely.
   if (nislands > 1) {
      int *sig_island = xmalloc_array(MAX(1u, ctx->nsigs), sizeof(int));
      int *sig_w_island = xmalloc_array(MAX(1u, ctx->nsigs), sizeof(int));
      for (uint32_t i = 0; i < ctx->nsigs; i++) {
         sig_island[i] = -1;
         sig_w_island[i] = -1;
      }

      bool split_multi = false;
      hash_t *orig_island = hash_new(32);
      for (uint32_t i = 0; i < ctx->nprocs; i++) {
         proc_meta_t *pm = &(ctx->procs[i]);
         if (!pm->eligible)
            continue;
         if (pm->from_gen) {
            uintptr_t prev = (uintptr_t)hash_get(orig_island, pm->orig_stmt);
            if (prev == 0)
               hash_put(orig_island, pm->orig_stmt,
                        (void *)(uintptr_t)(pm->island_idx + 1));
            else if ((int)(prev - 1) != pm->island_idx) {
               pm->eligible = false;
               pm->eject_reason = IPROC_EJ_TOO_LARGE;
               ACLEAR(pm->reads);
               ACLEAR(pm->writes);
               ACLEAR(pm->write_ranges);
               mark_ejected_proc_refs(ctx, pm->stmt);
               split_multi = true;
               continue;
            }
         }
         for (uint32_t j = 0; j < pm->writes.count; j++) {
            uint32_t si = pm->writes.items[j];
            if (sig_w_island[si] < 0)
               sig_w_island[si] = pm->island_idx;
            else if (sig_w_island[si] != pm->island_idx) {
               ctx->sigs[si].ext_written = true;
               mark_sig_external(&ctx->sigs[si], ISIG_EJ_MULTI_DRIVER);
               split_multi = true;
            }
         }
      }
      hash_free(orig_island);
      free(sig_w_island);
      if (split_multi)
         resolve_multi_drivers(ctx);

      bool split_gen_local = false;
      for (uint32_t i = 0; i < ctx->nprocs; i++) {
         const proc_meta_t *pm = &(ctx->procs[i]);
         if (!pm->eligible)
            continue;
         for (uint32_t j = 0; j < pm->reads.count; j++) {
            uint32_t si = pm->reads.items[j];
            if (sig_island[si] < 0)
               sig_island[si] = pm->island_idx;
            else if (sig_island[si] != pm->island_idx) {
               ctx->sigs[si].ext_read = true;
               if (ctx->sigs[si].is_gen_local)
                  split_gen_local = true;
            }
         }
         for (uint32_t j = 0; j < pm->writes.count; j++) {
            uint32_t si = pm->writes.items[j];
            if (sig_island[si] < 0)
               sig_island[si] = pm->island_idx;
            else if (sig_island[si] != pm->island_idx) {
               ctx->sigs[si].ext_written = true;
               if (ctx->sigs[si].is_gen_local)
                  split_gen_local = true;
            }
         }
         if (pm->clk_sig_idx >= 0) {
            uint32_t si = (uint32_t)pm->clk_sig_idx;
            if (sig_island[si] < 0)
               sig_island[si] = pm->island_idx;
            else if (sig_island[si] != pm->island_idx)
               ctx->sigs[si].ext_read = true;
         }
         if (pm->rst_sig_idx >= 0) {
            uint32_t si = (uint32_t)pm->rst_sig_idx;
            if (sig_island[si] < 0)
               sig_island[si] = pm->island_idx;
            else if (sig_island[si] != pm->island_idx)
               ctx->sigs[si].ext_read = true;
         }
      }
      if (split_gen_local)
         resolve_multi_drivers(ctx);
      free(sig_island);
   }

   free(comp_island);
   free(sig_owner);
   free(parent);
   return nislands;
}

static bool sv_expr_reads_overlap_sig(const analyze_ctx_t *ctx, vlog_node_t v,
                                      uint32_t gsig, const proc_meta_t *pm)
{
   if (v == NULL)
      return false;

   const vlog_kind_t k = vlog_kind(v);
   if (k == V_REF || k == V_BIT_SELECT || k == V_PART_SELECT
       || k == V_MEMBER_REF) {
      uint32_t rsig = 0;
      uint64_t rlo = 0, rhi = 0;
      if (sv_expr_exact_bit_range(ctx, v, &rsig, &rlo, &rhi, NULL)) {
         if (rsig == gsig) {
            for (uint32_t i = 0; i < pm->write_ranges.count; i++) {
               const sv_write_range_t wr = pm->write_ranges.items[i];
               if (wr.sig_idx == gsig && rlo <= wr.bit_hi && wr.bit_lo <= rhi)
                  return true;
            }
         }
         // Still check any dynamic index expressions inside selects
         vlog_node_t cur = v;
         while (cur != NULL && vlog_kind(cur) != V_REF) {
            const vlog_kind_t ck = vlog_kind(cur);
            if (ck == V_BIT_SELECT) {
               for (unsigned i = 0; i < vlog_params(cur); i++) {
                  if (sv_expr_reads_overlap_sig(ctx, vlog_param(cur, i),
                                                gsig, pm))
                     return true;
               }
            }
            else if (ck == V_PART_SELECT) {
               if (sv_expr_reads_overlap_sig(ctx, vlog_left(cur), gsig, pm)
                   || (vlog_has_right(cur)
                       && sv_expr_reads_overlap_sig(ctx, vlog_right(cur),
                                                    gsig, pm)))
                  return true;
            }
            cur = vlog_value(cur);
         }
         return false;
      }
   }

   switch (k) {
   case V_NUMBER:
   case V_STRING:
   case V_REAL:
      return false;
   case V_UNARY:
   case V_CAST:
   case V_MIN_TYP_MAX:
      return sv_expr_reads_overlap_sig(ctx, vlog_value(v), gsig, pm);
   case V_BINARY:
   case V_INSIDE_RANGE:
      return sv_expr_reads_overlap_sig(ctx, vlog_left(v), gsig, pm)
          || sv_expr_reads_overlap_sig(ctx, vlog_right(v), gsig, pm);
   case V_COND_EXPR:
      return sv_expr_reads_overlap_sig(ctx, vlog_value(v), gsig, pm)
          || sv_expr_reads_overlap_sig(ctx, vlog_left(v), gsig, pm)
          || sv_expr_reads_overlap_sig(ctx, vlog_right(v), gsig, pm);
   case V_CONCAT:
      if (vlog_has_value(v)
          && sv_expr_reads_overlap_sig(ctx, vlog_value(v), gsig, pm))
         return true;
      for (unsigned i = 0; i < vlog_params(v); i++) {
         if (sv_expr_reads_overlap_sig(ctx, vlog_param(v, i), gsig, pm))
            return true;
      }
      return false;
   case V_INSIDE:
      if (sv_expr_reads_overlap_sig(ctx, vlog_value(v), gsig, pm))
         return true;
      for (unsigned i = 0; i < vlog_params(v); i++) {
         if (sv_expr_reads_overlap_sig(ctx, vlog_param(v, i), gsig, pm))
            return true;
      }
      return false;
   case V_PATTERN_EXPR:
   case V_SYS_FCALL:
      for (unsigned i = 0; i < vlog_params(v); i++) {
         if (sv_expr_reads_overlap_sig(ctx, vlog_param(v, i), gsig, pm))
            return true;
      }
      return false;
   case V_PATTERN_ITEM:
      return vlog_has_right(v)
          && sv_expr_reads_overlap_sig(ctx, vlog_right(v), gsig, pm);
   default:
      return true;   // Conservative for complex expressions (e.g. user fcall)
   }
}

static bool sv_assign_lvalue_idx_overlaps(const analyze_ctx_t *ctx,
                                          vlog_node_t lhs, uint32_t gsig,
                                          const proc_meta_t *pm)
{
   if (lhs == NULL)
      return false;
   switch (vlog_kind(lhs)) {
   case V_CONCAT:
      for (unsigned i = 0; i < vlog_params(lhs); i++) {
         if (sv_assign_lvalue_idx_overlaps(ctx, vlog_param(lhs, i), gsig, pm))
            return true;
      }
      return false;
   case V_BIT_SELECT:
      for (unsigned i = 0; i < vlog_params(lhs); i++) {
         if (sv_expr_reads_overlap_sig(ctx, vlog_param(lhs, i), gsig, pm))
            return true;
      }
      return sv_assign_lvalue_idx_overlaps(ctx, vlog_value(lhs), gsig, pm);
   case V_PART_SELECT:
      if (sv_expr_reads_overlap_sig(ctx, vlog_left(lhs), gsig, pm)
          || (vlog_has_right(lhs)
              && sv_expr_reads_overlap_sig(ctx, vlog_right(lhs), gsig, pm)))
         return true;
      return sv_assign_lvalue_idx_overlaps(ctx, vlog_value(lhs), gsig, pm);
   case V_MEMBER_REF:
      return sv_assign_lvalue_idx_overlaps(ctx, vlog_value(lhs), gsig, pm);
   default:
      return false;
   }
}

static void run_comb_tarjan_topo(const analyze_ctx_t *ctx, vlog_island_t *isl,
                                 const uint32_t *l_sigs,
                                 const uint32_t *p_indices)
{
   // Collect combinational processes (IPROC_ASSIGN and IPROC_COMB)
   A(uint32_t) comb_idx = AINIT;
   int *proc_to_c = xmalloc_array(MAX(1u, isl->nprocs), sizeof(int));
   for (uint32_t i = 0; i < isl->nprocs; i++) {
      if (isl->procs[i].kind == IPROC_ASSIGN || isl->procs[i].kind == IPROC_COMB) {
         proc_to_c[i] = (int)comb_idx.count;
         APUSH(comb_idx, i);
      }
      else
         proc_to_c[i] = -1;
   }

   const uint32_t n = comb_idx.count;
   if (n == 0) {
      free(proc_to_c);
      ACLEAR(comb_idx);
      return;
   }

   // Build adjacency list: edge u -> v when u writes a signal that v reads
   A(uint32_t) *adj = xcalloc_array(n, sizeof(*adj));
   bool *self_loop = xcalloc_array(n, sizeof(bool));

   // Map each signal to combinational writers
   A(uint32_t) *sig_writers = xcalloc_array(MAX(1u, isl->nsigs),
                                            sizeof(*sig_writers));
   for (uint32_t u = 0; u < n; u++) {
      const island_proc_t *p = &(isl->procs[comb_idx.items[u]]);
      for (uint32_t j = 0; j < p->nwrites; j++)
         APUSH(sig_writers[p->writes[j]], u);
   }

   hset_t *edge_seen = hset_new(MAX(16, n * 2));
   for (uint32_t v = 0; v < n; v++) {
      const island_proc_t *pv = &(isl->procs[comb_idx.items[v]]);
      for (uint32_t j = 0; j < pv->nreads; j++) {
         uint32_t s = pv->reads[j];
         for (uint32_t k = 0; k < sig_writers[s].count; k++) {
            uint32_t u = sig_writers[s].items[k];
            if (u == v) {
               // Only continuous assign reading bits overlapping its own
               // written slice is a self-loop cycle (e.g. short_op_a[16] =
               // ... & short_op_a[15] is acyclic).
               if (pv->kind == IPROC_ASSIGN) {
                  const proc_meta_t *pm =
                     &(ctx->procs[p_indices[comb_idx.items[v]]]);
                  const uint32_t gsig = l_sigs[s];
                  if (pm->write_ranges.count == 0
                      || sv_expr_reads_overlap_sig(ctx, vlog_value(pv->stmt),
                                                   gsig, pm)
                      || sv_assign_lvalue_idx_overlaps(ctx,
                                                       vlog_target(pv->stmt),
                                                       gsig, pm))
                     self_loop[u] = true;
               }
               continue;
            }
            uint64_t key = ((uint64_t)u << 32) | v;
            if (!hset_contains(edge_seen, (void *)(uintptr_t)key)) {
               hset_insert(edge_seen, (void *)(uintptr_t)key);
               APUSH(adj[u], v);
            }
         }
      }
   }
   hset_free(edge_seen);

   for (uint32_t i = 0; i < isl->nsigs; i++)
      ACLEAR(sig_writers[i]);
   free(sig_writers);

   // Iterative/recursive Tarjan SCC
   int *dfn = xmalloc_array(n, sizeof(int));
   int *low = xmalloc_array(n, sizeof(int));
   int *scc_of = xmalloc_array(n, sizeof(int));
   bool *on_stack = xcalloc_array(n, sizeof(bool));
   uint32_t *stack = xmalloc_array(n, sizeof(uint32_t));
   uint32_t *call_u = xmalloc_array(n, sizeof(uint32_t));
   uint32_t *call_ei = xmalloc_array(n, sizeof(uint32_t));
   for (uint32_t i = 0; i < n; i++)
      dfn[i] = low[i] = scc_of[i] = -1;

   typedef struct {
      uint32_t *nodes;
      uint32_t  count;
      bool      is_cycle;
   } scc_group_t;

   scc_group_t *sccs = xcalloc_array(n, sizeof(scc_group_t));
   uint32_t nsccs = 0;
   uint32_t sp = 0;
   int timer = 0;

   for (uint32_t root = 0; root < n; root++) {
      if (dfn[root] >= 0)
         continue;

      int csp = 0;
      call_u[0] = root;
      call_ei[0] = 0;
      dfn[root] = low[root] = timer++;
      stack[sp++] = root;
      on_stack[root] = true;

      while (csp >= 0) {
         uint32_t u = call_u[csp];
         uint32_t ei = call_ei[csp];

         if (ei < adj[u].count) {
            call_ei[csp]++;
            uint32_t w = adj[u].items[ei];
            if (dfn[w] < 0) {
               dfn[w] = low[w] = timer++;
               stack[sp++] = w;
               on_stack[w] = true;
               csp++;
               call_u[csp] = w;
               call_ei[csp] = 0;
            }
            else if (on_stack[w]) {
               if (dfn[w] < low[u])
                  low[u] = dfn[w];
            }
         }
         else {
            if (low[u] == dfn[u]) {
               scc_group_t *g = &(sccs[nsccs]);
               A(uint32_t) members = AINIT;
               for (;;) {
                  uint32_t w = stack[--sp];
                  on_stack[w] = false;
                  scc_of[w] = (int)nsccs;
                  APUSH(members, w);
                  if (w == u)
                     break;
               }
               // Reverse pop order for deterministic ordering
               for (uint32_t a = 0, b = members.count - 1; a < b; a++, b--) {
                  uint32_t tmp = members.items[a];
                  members.items[a] = members.items[b];
                  members.items[b] = tmp;
               }
               g->nodes = members.items;
               g->count = members.count;
               g->is_cycle = (g->count > 1) || self_loop[g->nodes[0]];
               if (g->is_cycle)
                  isl->comb_sccs++;
               nsccs++;
            }

            csp--;
            if (csp >= 0) {
               uint32_t parent_u = call_u[csp];
               if (low[u] < low[parent_u])
                  low[parent_u] = low[u];
            }
         }
      }
   }

   // Tarjan emits SCCs in reverse topological order: reverse into topological order
   isl->topo_order = xmalloc_array(n, sizeof(uint32_t));
   isl->ntopo = n;

   uint32_t pos = 0;
   for (int si = (int)nsccs - 1; si >= 0; si--) {
      const int topo_scc_id = (int)nsccs - 1 - si;
      const scc_group_t *g = &(sccs[si]);
      for (uint32_t k = 0; k < g->count; k++) {
         uint32_t u = g->nodes[k];
         uint32_t pidx = comb_idx.items[u];
         isl->topo_order[pos] = pidx;
         isl->procs[pidx].topo_order = (int)pos;
         isl->procs[pidx].scc_id = topo_scc_id;
         isl->procs[pidx].in_cycle = g->is_cycle;
         isl->procs[pidx].self_loop = (g->count == 1 && self_loop[u]);
         isl->procs[pidx].topo_level = 0;
         pos++;
      }
   }

   // Compute longest combinational path depth in topological order
   int max_level = 0;
   for (uint32_t i = 0; i < n; i++) {
      uint32_t pidx = isl->topo_order[i];
      uint32_t u = (uint32_t)proc_to_c[pidx];
      int lvl = isl->procs[pidx].topo_level;
      if (lvl > max_level)
         max_level = lvl;

      for (uint32_t ei = 0; ei < adj[u].count; ei++) {
         uint32_t v = adj[u].items[ei];
         uint32_t vpidx = comb_idx.items[v];
         if (isl->procs[vpidx].scc_id != isl->procs[pidx].scc_id) {
            if (lvl + 1 > isl->procs[vpidx].topo_level)
               isl->procs[vpidx].topo_level = lvl + 1;
         }
         else {
            if (lvl > isl->procs[vpidx].topo_level)
               isl->procs[vpidx].topo_level = lvl;
         }
      }
   }
   isl->comb_levels = (uint32_t)(max_level + 1);

   for (uint32_t i = 0; i < nsccs; i++)
      free(sccs[i].nodes);
   free(sccs);
   free(call_ei);
   free(call_u);
   free(stack);
   free(on_stack);
   free(scc_of);
   free(low);
   free(dfn);
   for (uint32_t i = 0; i < n; i++)
      ACLEAR(adj[i]);
   free(adj);
   free(self_loop);
   free(proc_to_c);
   ACLEAR(comb_idx);
}

static vlog_island_t *build_island(analyze_ctx_t *ctx, uint32_t island_id)
{
   vlog_island_t *isl = xcalloc(sizeof(vlog_island_t));
   isl->mod_name = vlog_has_ident2(ctx->body)
      ? vlog_ident2(ctx->body) : vlog_ident(ctx->body);
   isl->body_name = vlog_ident(ctx->body);
   isl->island_id = island_id;

   // Map global sig idx -> island sig idx (-1 if not in this island)
   int *g_to_l = xmalloc_array(MAX(1u, ctx->nsigs), sizeof(int));
   bool *sig_read = xcalloc_array(MAX(1u, ctx->nsigs), sizeof(bool));
   bool *sig_write = xcalloc_array(MAX(1u, ctx->nsigs), sizeof(bool));
   bool *sig_clk = xcalloc_array(MAX(1u, ctx->nsigs), sizeof(bool));
   bool *sig_rst = xcalloc_array(MAX(1u, ctx->nsigs), sizeof(bool));
   for (uint32_t i = 0; i < ctx->nsigs; i++)
      g_to_l[i] = -1;

   A(uint32_t) l_sigs = AINIT;
   A(uint32_t) p_indices = AINIT;

   for (uint32_t i = 0; i < ctx->nprocs; i++) {
      const proc_meta_t *pm = &(ctx->procs[i]);
      if (!pm->eligible || pm->island_idx != (int)island_id)
         continue;

      APUSH(p_indices, i);
      isl->total_nodes += pm->node_count;

      for (uint32_t j = 0; j < pm->reads.count; j++) {
         uint32_t gs = pm->reads.items[j];
         sig_read[gs] = true;
         if (g_to_l[gs] < 0) {
            g_to_l[gs] = (int)l_sigs.count;
            APUSH(l_sigs, gs);
         }
      }
      for (uint32_t j = 0; j < pm->writes.count; j++) {
         uint32_t gs = pm->writes.items[j];
         sig_write[gs] = true;
         if (g_to_l[gs] < 0) {
            g_to_l[gs] = (int)l_sigs.count;
            APUSH(l_sigs, gs);
         }
      }
      if (pm->clk_sig_idx >= 0) {
         uint32_t gs = (uint32_t)pm->clk_sig_idx;
         sig_clk[gs] = true;
         if (g_to_l[gs] < 0) {
            g_to_l[gs] = (int)l_sigs.count;
            APUSH(l_sigs, gs);
         }
      }
      if (pm->rst_sig_idx >= 0) {
         uint32_t gs = (uint32_t)pm->rst_sig_idx;
         sig_rst[gs] = true;
         if (g_to_l[gs] < 0) {
            g_to_l[gs] = (int)l_sigs.count;
            APUSH(l_sigs, gs);
         }
      }
   }

   isl->nsigs = l_sigs.count;
   isl->sigs = xcalloc_array(MAX(1u, isl->nsigs), sizeof(island_sig_t));

   for (uint32_t i = 0; i < isl->nsigs; i++) {
      const uint32_t gs = l_sigs.items[i];
      sig_meta_t *sm = &(ctx->sigs[gs]);
      island_sig_t *dst = &(isl->sigs[i]);

      dst->decl = sm->decl;
      dst->port = sm->port;
      dst->name = sm->name;
      dst->gen_path = sm->gen_path;
      dst->orig_name = sm->orig_name;
      dst->width = sm->width;
      dst->nelems = sm->nelems;
      dst->two_state = sm->two_state
         || (opt_get_int(OPT_TWO_STATE) >= TWO_STATE_ISLANDS);
      dst->is_external = sm->is_external;
      dst->eject_reason = sm->eject_reason;
      dst->nwords = (sm->width + 63u) / 64u;

      if (sm->is_inout)
         dst->kind = ISIG_BOUNDARY_INOUT;
      else if (sig_clk[gs] && !sig_write[gs])
         dst->kind = ISIG_CLOCK;
      else if (sig_rst[gs] && !sig_write[gs])
         dst->kind = ISIG_RESET;
      else if (sig_write[gs] && (sm->is_output_port || sm->ext_read
                                 || sm->is_external))
         dst->kind = ISIG_BOUNDARY_OUT;
      else if (sig_read[gs] && (sm->is_input_port || sm->ext_written
                                || sm->is_external))
         dst->kind = ISIG_BOUNDARY_IN;
      else if (sig_write[gs] && (sm->is_input_port || sm->ext_written))
         dst->kind = ISIG_BOUNDARY_INOUT;
      else if (sig_clk[gs])
         dst->kind = ISIG_CLOCK;
      else if (sig_rst[gs])
         dst->kind = ISIG_RESET;
      else
         dst->kind = ISIG_INTERNAL;

      if ((sm->ext_read || sm->ext_written) && !sm->is_input_port
          && !sm->is_output_port && dst->eject_reason == ISIG_EJ_NONE) {
         dst->is_external = true;
         dst->eject_reason = ISIG_EJ_SHARED_EJECTED;
         sm->is_external = true;
         sm->eject_reason = ISIG_EJ_SHARED_EJECTED;
      }

      switch (dst->kind) {
      case ISIG_INTERNAL:       isl->n_internal_sigs++; break;
      case ISIG_BOUNDARY_IN:    isl->n_boundary_in++; break;
      case ISIG_BOUNDARY_OUT:   isl->n_boundary_out++; break;
      case ISIG_BOUNDARY_INOUT: isl->n_boundary_inout++; break;
      case ISIG_CLOCK:          isl->n_clock_sigs++; break;
      case ISIG_RESET:          isl->n_reset_sigs++; break;
      }
   }

   // Compute flat island_state_t layout offsets (dirty_out_mask header + a/b planes)
   const uint32_t dirty_words = MAX(1u, (isl->n_boundary_out + 63u) / 64u);
   isl->header_bytes = dirty_words * 8u;
   uint32_t offset = isl->header_bytes;

   for (uint32_t i = 0; i < isl->nsigs; i++) {
      island_sig_t *dst = &(isl->sigs[i]);
      const uint32_t plane_bytes = dst->nwords * dst->nelems * 8u;
      dst->a_off = offset;
      offset += plane_bytes;
      if (!dst->two_state) {
         dst->b_off = offset;
         offset += plane_bytes;
      }
      else
         dst->b_off = UINT32_MAX;
   }
   isl->state_bytes = offset;

   // Populate processes and clock domains
   isl->nprocs = p_indices.count;
   isl->procs = xcalloc_array(MAX(1u, isl->nprocs), sizeof(island_proc_t));
   A(island_domain_t) doms = AINIT;

   for (uint32_t i = 0; i < isl->nprocs; i++) {
      const proc_meta_t *pm = &(ctx->procs[p_indices.items[i]]);
      island_proc_t *ip = &(isl->procs[i]);

      ip->stmt = pm->stmt;
      ip->orig_stmt = pm->orig_stmt;
      ip->kind = pm->kind;
      ip->from_gen = pm->from_gen;
      ip->node_count = pm->node_count;
      ip->topo_order = -1;
      ip->topo_level = -1;
      ip->scc_id = -1;
      ip->domain_id = -1;

      ip->nreads = pm->reads.count;
      if (ip->nreads > 0) {
         ip->reads = xmalloc_array(ip->nreads, sizeof(uint32_t));
         for (uint32_t j = 0; j < ip->nreads; j++)
            ip->reads[j] = (uint32_t)g_to_l[pm->reads.items[j]];
      }

      ip->nwrites = pm->writes.count;
      if (ip->nwrites > 0) {
         ip->writes = xmalloc_array(ip->nwrites, sizeof(uint32_t));
         for (uint32_t j = 0; j < ip->nwrites; j++)
            ip->writes[j] = (uint32_t)g_to_l[pm->writes.items[j]];
      }

      switch (ip->kind) {
      case IPROC_ASSIGN: isl->n_assign++; break;
      case IPROC_COMB:   isl->n_comb++; break;
      case IPROC_FF:
         isl->n_ff++;
         {
            const int lclk = g_to_l[pm->clk_sig_idx];
            const int lrst = pm->rst_sig_idx >= 0 ? g_to_l[pm->rst_sig_idx] : -1;
            int found = -1;
            for (uint32_t d = 0; d < doms.count; d++) {
               if (doms.items[d].clk_sig == lclk
                   && doms.items[d].clk_posedge == pm->clk_posedge
                   && doms.items[d].rst_sig == lrst
                   && (lrst < 0 || doms.items[d].rst_posedge == pm->rst_posedge)) {
                  found = (int)d;
                  break;
               }
            }
            if (found < 0) {
               found = (int)doms.count;
               island_domain_t nd = {
                  .clk_sig     = lclk,
                  .clk_posedge = pm->clk_posedge,
                  .rst_sig     = lrst,
                  .rst_posedge = pm->rst_posedge,
               };
               APUSH(doms, nd);
            }
            ip->domain_id = found;
            island_domain_t *dom = &(doms.items[found]);
            dom->ff_procs = xrealloc_array(dom->ff_procs, dom->nff_procs + 1,
                                           sizeof(uint32_t));
            dom->ff_procs[dom->nff_procs++] = i;
         }
         break;
      }
   }

   isl->ndomains = doms.count;
   isl->domains = doms.items;

   run_comb_tarjan_topo(ctx, isl, l_sigs.items, p_indices.items);

   free(g_to_l);
   free(sig_read);
   free(sig_write);
   free(sig_clk);
   free(sig_rst);
   ACLEAR(l_sigs);
   ACLEAR(p_indices);
   return isl;
}

vlog_island_plan_t *vlog_island_analyze(vlog_node_t body, bool is_mixed)
{
   assert(vlog_kind(body) == V_INST_BODY);

   if (body != last_analyzed_body) {
      last_analyzed_body = body;
      last_analyzed_is_mixed = is_mixed;
   }

   if (vlog_flags(body) & VLOG_F_INTERFACE) {
      vlog_island_plan_t *plan = xcalloc(sizeof(vlog_island_plan_t));
      plan->mod_name = vlog_has_ident2(body) ? vlog_ident2(body) : vlog_ident(body);
      plan->body_name = vlog_ident(body);
      plan->has_mixed_ports = is_mixed;
      return plan;
   }

   analyze_ctx_t ctx = {
      .body     = body,
      .is_mixed = is_mixed,
   };

   collect_module_sigs(&ctx);
   expand_module_generates(&ctx);
   prescan_external_and_drivers(&ctx);
   prescan_forces(&ctx);
   check_all_procs(&ctx);
   resolve_multi_drivers(&ctx);

   const uint32_t nislands = partition_islands(&ctx);

   vlog_island_plan_t *plan = xcalloc(sizeof(vlog_island_plan_t));
   plan->mod_name = vlog_has_ident2(body) ? vlog_ident2(body) : vlog_ident(body);
   plan->body_name = vlog_ident(body);
   plan->has_mixed_ports = is_mixed;
   plan->nislands = nislands;
   plan->n_total_procs = ctx.nprocs;

   if (nislands > 0) {
      plan->islands = xcalloc_array(nislands, sizeof(vlog_island_t *));
      for (uint32_t i = 0; i < nislands; i++)
         plan->islands[i] = build_island(&ctx, i);
   }

   A(island_ejected_proc_t) ej = AINIT;
   for (uint32_t i = 0; i < ctx.nprocs; i++) {
      if (ctx.procs[i].eligible)
         plan->n_fused_procs++;
      else {
         island_ejected_proc_t ep = {
            .stmt   = ctx.procs[i].stmt,
            .reason = ctx.procs[i].eject_reason,
         };
         APUSH(ej, ep);
         plan->proc_eject_counts[ep.reason]++;
      }
   }
   plan->ejected_procs = ej.items;
   plan->nejected_procs = ej.count;

   for (uint32_t i = 0; i < ctx.nsigs; i++) {
      if (ctx.sigs[i].is_external && ctx.sigs[i].eject_reason != ISIG_EJ_NONE) {
         plan->n_ejected_sigs++;
         plan->sig_eject_counts[ctx.sigs[i].eject_reason]++;
      }
   }

   for (uint32_t i = 0; i < ctx.nprocs; i++) {
      ACLEAR(ctx.procs[i].reads);
      ACLEAR(ctx.procs[i].writes);
      ACLEAR(ctx.procs[i].write_ranges);
   }
   ACLEAR(ctx.gen_procs);
   ACLEAR(ctx.gen_ext_stmts);
   if (ctx.expanded_gens != NULL)
      hset_free(ctx.expanded_gens);
   free(ctx.procs);
   free(ctx.sigs);
   hash_free(ctx.decl_map);

   return plan;
}

void vlog_island_plan_free(vlog_island_plan_t *plan)
{
   if (plan == NULL)
      return;

   for (uint32_t i = 0; i < plan->nislands; i++) {
      vlog_island_t *isl = plan->islands[i];
      for (uint32_t j = 0; j < isl->nprocs; j++) {
         free(isl->procs[j].reads);
         free(isl->procs[j].writes);
      }
      for (uint32_t j = 0; j < isl->ndomains; j++)
         free(isl->domains[j].ff_procs);
      free(isl->sigs);
      free(isl->procs);
      free(isl->domains);
      free(isl->topo_order);
      free(isl);
   }

   free(plan->islands);
   free(plan->ejected_procs);
   free(plan);
}

void vlog_island_dump(const vlog_island_plan_t *plan)
{
   printf("ISLAND_MODULE %s (%s): islands=%u procs=%u/%u ejected_procs=%u "
          "ejected_sigs=%u\n",
          istr(plan->mod_name), istr(plan->body_name), plan->nislands,
          plan->n_fused_procs, plan->n_total_procs, plan->nejected_procs,
          plan->n_ejected_sigs);

   for (uint32_t i = 0; i < plan->nislands; i++) {
      const vlog_island_t *isl = plan->islands[i];
      uint32_t n_two_state = 0;
      for (uint32_t j = 0; j < isl->nsigs; j++) {
         if (isl->sigs[j].two_state)
            n_two_state++;
      }
      printf("  ISLAND %u: assign=%u comb=%u ff=%u domains=%u topo_levels=%u "
             "comb_sccs=%u sigs(int=%u in=%u out=%u inout=%u clk=%u rst=%u) "
             "state_bytes=%u two_state=%u/%u\n",
             isl->island_id, isl->n_assign, isl->n_comb, isl->n_ff,
             isl->ndomains, isl->comb_levels, isl->comb_sccs,
             isl->n_internal_sigs, isl->n_boundary_in, isl->n_boundary_out,
             isl->n_boundary_inout, isl->n_clock_sigs, isl->n_reset_sigs,
             isl->state_bytes, n_two_state, isl->nsigs);

      for (uint32_t d = 0; d < isl->ndomains; d++) {
         const island_domain_t *dom = &(isl->domains[d]);
         const char *clk_name = istr(isl->sigs[dom->clk_sig].name);
         const char *clk_edge = dom->clk_posedge ? "posedge" : "negedge";
         if (dom->rst_sig >= 0) {
            const char *rst_name = istr(isl->sigs[dom->rst_sig].name);
            const char *rst_edge = dom->rst_posedge ? "posedge" : "negedge";
            printf("    DOMAIN %u: clk=%s(%s) rst=%s(%s) ff=%u\n",
                   d, clk_name, clk_edge, rst_name, rst_edge, dom->nff_procs);
         }
         else {
            printf("    DOMAIN %u: clk=%s(%s) rst=none ff=%u\n",
                   d, clk_name, clk_edge, dom->nff_procs);
         }
      }
   }

   if (plan->nejected_procs > 0) {
      printf("  EJECTED_PROCS:");
      for (int r = 1; r < IPROC_EJ_MAX; r++) {
         if (plan->proc_eject_counts[r] > 0)
            printf(" %s=%u", vlog_island_proc_eject_str(r),
                   plan->proc_eject_counts[r]);
      }
      printf("\n");
   }

   if (plan->n_ejected_sigs > 0) {
      printf("  EJECTED_SIGS:");
      for (int r = 1; r < ISIG_EJ_MAX; r++) {
         if (plan->sig_eject_counts[r] > 0)
            printf(" %s=%u", vlog_island_sig_eject_str(r),
                   plan->sig_eject_counts[r]);
      }
      printf("\n");
   }
}
