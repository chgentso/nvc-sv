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
#include "mir/mir-node.h"
#include "mir/mir-unit.h"
#include "rt/island.h"
#include "vlog/vlog-island-lower.h"
#include "vlog/vlog-node.h"
#include "vlog/vlog-phase.h"
#include "vlog/vlog-util.h"

#include <assert.h>
#include <limits.h>
#include <string.h>

typedef struct {
   mir_unit_t          *inst_mu;
   const vlog_island_t *isl;
   hash_t              *sig_map;   // vlog_node_t decl -> island_sig_t *
   hset_t              *local_vars;
   hset_t              *indexed_refs;
   hset_t              *allowed_timings;
   hset_t              *checked_funcs;
   A(vlog_node_t)       active_funcs;
   bool                 ok;
} m2_check_t;

static void m2_check_subtree(vlog_node_t root, m2_check_t *mc);

static bool m2_func_is_pure(m2_check_t *mc, vlog_node_t fn)
{
   if (hset_contains(mc->checked_funcs, fn))
      return true;
   for (unsigned i = 0; i < mc->active_funcs.count; i++) {
      if (mc->active_funcs.items[i] == fn)
         return false;
   }

   APUSH(mc->active_funcs, fn);

   bool saved_ok = mc->ok;
   hset_t *saved_locals = mc->local_vars;
   mc->local_vars = hset_new(16);
   const int nd = vlog_decls(fn);
   for (int i = 0; i < nd; i++)
      hset_insert(mc->local_vars, vlog_decl(fn, i));

   mc->ok = true;

   const int nstmts = vlog_stmts(fn);
   for (int i = 0; i < nstmts && mc->ok; i++)
      m2_check_subtree(vlog_stmt(fn, i), mc);

   const bool pure = mc->ok;
   hset_free(mc->local_vars);
   mc->local_vars = saved_locals;
   mc->ok = saved_ok;
   APOP(mc->active_funcs);

   if (pure)
      hset_insert(mc->checked_funcs, fn);
   return pure;
}

static bool m2_decl_in_island(m2_check_t *mc, vlog_node_t decl)
{
   const island_sig_t *isig = hash_get(mc->sig_map, decl);
   if (isig == NULL || isig->width == 0 || isig->nelems == 0)
      return false;

   int hops = 0;
   mir_value_t var = mir_search_object(mc->inst_mu, decl, &hops);
   if (mir_is_null(var) || hops != 0)
      return false;

   if (!mir_points_to(mc->inst_mu, var, MIR_TYPE_SIGNAL))
      return false;

   return true;
}

static void m2_check_ref_decl(m2_check_t *mc, vlog_node_t ref_node,
                              bool is_indexed)
{
   if (!vlog_has_ref(ref_node)) {
      mc->ok = false;
      return;
   }

   vlog_node_t decl = vlog_ref(ref_node);
   if (vlog_kind(decl) == V_PORT_DECL)
      decl = vlog_ref(decl);

   switch (vlog_kind(decl)) {
   case V_PARAM_DECL:
   case V_LOCALPARAM:
   case V_ENUM_NAME:
   case V_GENVAR_DECL:
   case V_LOCAL_DECL:
   case V_TF_PORT_DECL:
      return;

   case V_VAR_DECL:
   case V_NET_DECL:
      {
         if (mc->local_vars != NULL && hset_contains(mc->local_vars, decl))
            return;
         // Static package/class vars have ident2 set -> allow as global const/var
         if (vlog_kind(decl) == V_VAR_DECL && vlog_has_ident2(decl))
            return;
         if (!m2_decl_in_island(mc, decl)) {
            mc->ok = false;
            return;
         }
         const island_sig_t *isig = hash_get(mc->sig_map, decl);
         if (isig->nelems > 1 && !is_indexed)
            mc->ok = false;
      }
      return;

   default:
      mc->ok = false;
      return;
   }
}

static void m2_check_pre_cb(vlog_node_t v, void *arg)
{
   m2_check_t *mc = arg;
   if (!mc->ok)
      return;

   const vlog_kind_t k = vlog_kind(v);
   switch (k) {
   case V_SEQ_BLOCK:
   case V_FOR_INIT:
      {
         const int nd = vlog_decls(v);
         for (int i = 0; i < nd; i++)
            hset_insert(mc->local_vars, vlog_decl(v, i));
      }
      break;

   case V_BIT_SELECT:
      {
         vlog_node_t val = vlog_value(v);
         if (vlog_kind(val) == V_REF && vlog_has_ref(val)) {
            vlog_node_t decl = vlog_ref(val);
            if (vlog_kind(decl) == V_PORT_DECL)
               decl = vlog_ref(decl);
            const island_sig_t *isig = hash_get(mc->sig_map, decl);
            if (isig != NULL && isig->nelems > 1) {
               const int nunpacked = vlog_ranges(decl);
               if (nunpacked > 0 && vlog_params(v) >= nunpacked)
                  hset_insert(mc->indexed_refs, val);
            }
            else
               hset_insert(mc->indexed_refs, val);
         }
      }
      break;

   case V_PART_SELECT:
   case V_MEMBER_REF:
      {
         vlog_node_t val = vlog_value(v);
         if (vlog_kind(val) == V_REF && vlog_has_ref(val))
            hset_insert(mc->indexed_refs, val);
      }
      break;

   default:
      break;
   }
}

static void m2_check_post_cb(vlog_node_t v, void *arg)
{
   m2_check_t *mc = arg;
   if (!mc->ok)
      return;

   const vlog_kind_t k = vlog_kind(v);
   switch (k) {
   case V_HIER_REF:
   case V_FORCE:
   case V_RELEASE:
   case V_NBASSIGN:
   case V_DISABLE:
   case V_FORK:
   case V_WAIT:
   case V_DELAY_CONTROL:
   case V_USER_TCALL:
   case V_SYS_TCALL:
   case V_ASSERT:
   case V_ASSUME:
   case V_COVER:
      mc->ok = false;
      return;

   case V_REF:
      m2_check_ref_decl(mc, v, hset_contains(mc->indexed_refs, v));
      return;

   case V_USER_FCALL:
      if (!vlog_has_ref(v) || !m2_func_is_pure(mc, vlog_ref(v)))
         mc->ok = false;
      return;

   case V_ASSIGN:
      if (vlog_has_delay(v) || vlog_params(v) > 0)
         mc->ok = false;
      return;

   case V_TIMING:
      if (vlog_stmts(v) > 0 && !hset_contains(mc->allowed_timings, v))
         mc->ok = false;
      return;

   default:
      return;
   }
}

static void m2_check_subtree(vlog_node_t root, m2_check_t *mc)
{
   if (root == NULL || !mc->ok)
      return;

   vlog_visit_prepost(root, m2_check_pre_cb, m2_check_post_cb, mc);
}

static bool m2_proc_eligible(const vlog_island_t *isl,
                             const island_proc_t *ip, hash_t *sig_map,
                             mir_unit_t *inst_mu)
{
   if (ip->kind != IPROC_ASSIGN && ip->kind != IPROC_COMB)
      return false;
   if (ip->in_cycle)
      return false;

   for (uint32_t i = 0; i < ip->nreads; i++) {
      const uint32_t sidx = ip->reads[i];
      if (sidx >= isl->nsigs)
         return false;
      const island_sig_t *isig = &(isl->sigs[sidx]);
      if (isig->width == 0 || isig->nelems == 0)
         return false;
   }
   for (uint32_t i = 0; i < ip->nwrites; i++) {
      const uint32_t sidx = ip->writes[i];
      if (sidx >= isl->nsigs)
         return false;
      const island_sig_t *isig = &(isl->sigs[sidx]);
      if (isig->width == 0 || isig->nelems == 0)
         return false;
      if (isig->eject_reason == ISIG_EJ_MULTI_DRIVER
          || isig->eject_reason == ISIG_EJ_FORCE_TARGET
          || isig->eject_reason == ISIG_EJ_INOUT_PORT
          || isig->eject_reason == ISIG_EJ_NETTYPE
          || isig->eject_reason == ISIG_EJ_ALIAS)
         return false;
   }

   m2_check_t mc = {
      .inst_mu         = inst_mu,
      .isl             = isl,
      .sig_map         = sig_map,
      .local_vars      = hset_new(16),
      .indexed_refs    = hset_new(32),
      .allowed_timings = hset_new(4),
      .checked_funcs   = hset_new(8),
      .ok              = true,
   };

   if (ip->kind == IPROC_COMB && vlog_kind(ip->stmt) == V_ALWAYS
       && vlog_subkind(ip->stmt) == V_ALWAYS_PLAIN
       && vlog_stmts(ip->stmt) > 0) {
      vlog_node_t top = vlog_stmt(ip->stmt, 0);
      vlog_node_t timing = top;
      if (vlog_stmts(ip->stmt) == 1 && vlog_kind(top) == V_SEQ_BLOCK
          && vlog_stmts(top) > 0)
         timing = vlog_stmt(top, 0);
      if (vlog_kind(timing) == V_TIMING)
         hset_insert(mc.allowed_timings, timing);
   }

   m2_check_subtree(ip->stmt, &mc);
   const bool ok = mc.ok;
   hset_free(mc.local_vars);
   hset_free(mc.indexed_refs);
   hset_free(mc.allowed_timings);
   hset_free(mc.checked_funcs);
   ACLEAR(mc.active_funcs);
   return ok;
}

static void m2_compute_boundaries(vlog_island_m2_t *m2)
{
   const vlog_island_t *isl = m2->isl;
   const uint32_t nsigs = isl->nsigs;

   bool *read_fused LOCAL = xcalloc_array(MAX(1u, nsigs), sizeof(bool));
   bool *write_fused LOCAL = xcalloc_array(MAX(1u, nsigs), sizeof(bool));
   bool *read_other LOCAL = xcalloc_array(MAX(1u, nsigs), sizeof(bool));
   bool *write_other LOCAL = xcalloc_array(MAX(1u, nsigs), sizeof(bool));

   for (uint32_t p = 0; p < isl->nprocs; p++) {
      const island_proc_t *ip = &(isl->procs[p]);
      if (m2->fused_proc[p]) {
         for (uint32_t j = 0; j < ip->nreads; j++)
            read_fused[ip->reads[j]] = true;
         for (uint32_t j = 0; j < ip->nwrites; j++)
            write_fused[ip->writes[j]] = true;
      }
      else {
         for (uint32_t j = 0; j < ip->nreads; j++)
            read_other[ip->reads[j]] = true;
         for (uint32_t j = 0; j < ip->nwrites; j++)
            write_other[ip->writes[j]] = true;
         if (ip->domain_id >= 0 && (uint32_t)ip->domain_id < isl->ndomains) {
            const island_domain_t *dom = &(isl->domains[ip->domain_id]);
            if (dom->clk_sig >= 0 && (uint32_t)dom->clk_sig < nsigs)
               read_other[dom->clk_sig] = true;
            if (dom->rst_sig >= 0 && (uint32_t)dom->rst_sig < nsigs)
               read_other[dom->rst_sig] = true;
         }
      }
   }

   for (uint32_t i = 0; i < nsigs; i++) {
      const island_sig_t *s = &(isl->sigs[i]);
      const bool in_port = s->port != NULL
         && (vlog_subkind(s->port) == V_PORT_INPUT
             || vlog_subkind(s->port) == V_PORT_INOUT
             || vlog_subkind(s->port) == V_PORT_REF);
      const bool out_port = s->port != NULL
         && (vlog_subkind(s->port) == V_PORT_OUTPUT
             || vlog_subkind(s->port) == V_PORT_INOUT
             || vlog_subkind(s->port) == V_PORT_REF);
      const bool ext_w = write_other[i] || in_port
         || s->kind == ISIG_BOUNDARY_IN || s->kind == ISIG_BOUNDARY_INOUT
         || s->kind == ISIG_CLOCK || s->kind == ISIG_RESET
         || (s->is_external && s->port == NULL
             && s->kind != ISIG_BOUNDARY_OUT)
         || (vlog_kind(s->decl) == V_NET_DECL && vlog_has_value(s->decl));
      const bool ext_r = read_other[i] || out_port
         || s->kind == ISIG_BOUNDARY_OUT || s->kind == ISIG_BOUNDARY_INOUT
         || (s->is_external && s->port == NULL
             && s->kind != ISIG_BOUNDARY_IN);

      m2->sig_in[i] = read_fused[i] && (ext_w || !write_fused[i]);
      m2->sig_out[i] = write_fused[i] && ext_r;
      m2->sig_comb_w[i] = write_fused[i];
   }
}

vlog_island_lower_ctx_t *vlog_island_lower_prepare(vlog_node_t body,
                                                   bool is_mixed,
                                                   mir_unit_t *inst_mu,
                                                   bool cover_counts)
{
   if (cover_counts)
      return NULL;

   vlog_island_plan_t *plan = vlog_island_analyze(body, is_mixed);
   if (plan == NULL || plan->nislands == 0) {
      vlog_island_plan_free(plan);
      return NULL;
   }

   vlog_island_lower_ctx_t *ctx = xcalloc(sizeof(vlog_island_lower_ctx_t));
   ctx->plan = plan;
   ctx->islands = xcalloc_array(plan->nislands, sizeof(vlog_island_m2_t));
   ctx->fused_stmts = hset_new(64);

   ident_t qual = vlog_ident(body);

   for (uint32_t ii = 0; ii < plan->nislands; ii++) {
      const vlog_island_t *isl = plan->islands[ii];
      vlog_island_m2_t *m2 = &(ctx->islands[ii]);
      m2->isl = isl;

      if (isl->ntopo == 0 || isl->state_bytes == 0)
         continue;

      hash_t *sig_map = hash_new(MAX(16u, isl->nsigs * 2));
      for (uint32_t i = 0; i < isl->nsigs; i++)
         hash_put(sig_map, isl->sigs[i].decl, (void *)&(isl->sigs[i]));

      m2->fused_proc = xcalloc_array(MAX(1u, isl->nprocs), sizeof(bool));
      m2->sig_in = xcalloc_array(MAX(1u, isl->nsigs), sizeof(bool));
      m2->sig_out = xcalloc_array(MAX(1u, isl->nsigs), sizeof(bool));
      m2->sig_comb_w = xcalloc_array(MAX(1u, isl->nsigs), sizeof(bool));

      for (uint32_t c = 0; c < isl->ntopo; c++) {
         const uint32_t pidx = isl->topo_order[c];
         assert(pidx < isl->nprocs);
         if (m2_proc_eligible(isl, &(isl->procs[pidx]), sig_map, inst_mu))
            m2->fused_proc[pidx] = true;
      }

      // Eject any proc whose outputs collide with external/non-fused drivers
      bool changed;
      do {
         changed = false;
         m2_compute_boundaries(m2);
         for (uint32_t p = 0; p < isl->nprocs; p++) {
            if (!m2->fused_proc[p])
               continue;
            const island_proc_t *ip = &(isl->procs[p]);
            for (uint32_t j = 0; j < ip->nwrites; j++) {
               const uint32_t sidx = ip->writes[j];
               if (m2->sig_in[sidx]) {
                  m2->fused_proc[p] = false;
                  changed = true;
                  break;
               }
            }
         }
      } while (changed);

      m2->n_fused_comb = 0;
      for (uint32_t c = 0; c < isl->ntopo; c++) {
         const uint32_t pidx = isl->topo_order[c];
         if (m2->fused_proc[pidx]) {
            m2->n_fused_comb++;
            hset_insert(ctx->fused_stmts, isl->procs[pidx].stmt);
         }
      }

      if (m2->n_fused_comb > 0)
         m2->eval_sym = ident_sprintf("%s.$island_comb%u", istr(qual), ii);

      hash_free(sig_map);
   }

   return ctx;
}

void vlog_island_lower_emit_init(vlog_island_lower_ctx_t *ctx,
                                 mir_unit_t *inst_mu, mir_value_t self)
{
   if (ctx == NULL)
      return;

   mir_type_t t_offset = mir_offset_type(inst_mu);
   mir_type_t t_ptr = mir_pointer_type(inst_mu, mir_int_type(inst_mu, 0, 255));

   for (uint32_t ii = 0; ii < ctx->plan->nislands; ii++) {
      const vlog_island_m2_t *m2 = &(ctx->islands[ii]);
      if (m2->n_fused_comb == 0)
         continue;

      const vlog_island_t *isl = m2->isl;

      mir_value_t cl_args[] = { self };
      mir_value_t closure =
         mir_build_closure(inst_mu, m2->eval_sym, t_offset, cl_args, 1);

      mir_value_t b_args[] = {
         closure,
         mir_const(inst_mu, t_offset, isl->state_bytes),
         mir_const(inst_mu, t_offset, isl->nsigs),
      };
      mir_value_t handle = mir_build_fcall(inst_mu, ident_new("__sv_island_begin"),
                                           t_ptr, MIR_NULL_STAMP,
                                           b_args, ARRAY_LEN(b_args));

      for (uint32_t i = 0; i < isl->nsigs; i++) {
         const island_sig_t *s = &(isl->sigs[i]);
         int hops = 0;
         mir_value_t var = mir_search_object(inst_mu, s->decl, &hops);
         assert(!mir_is_null(var) && hops == 0);
         mir_value_t sig_val = mir_build_load(inst_mu, var);

         uint32_t flags = 0;
         if (m2->sig_in[i])
            flags |= RT_ISIG_IN;
         if (m2->sig_out[i])
            flags |= RT_ISIG_OUT;
         if (m2->sig_comb_w[i])
            flags |= RT_ISIG_COMB_WRITTEN;
         if (s->two_state)
            flags |= RT_ISIG_TWO_STATE;
         if (vlog_kind(s->decl) == V_NET_DECL)
            flags |= RT_ISIG_IS_NET;

         mir_value_t s_args[] = {
            handle,
            mir_const(inst_mu, t_offset, i),
            sig_val,
            mir_const(inst_mu, t_offset, s->a_off),
            mir_const(inst_mu, t_offset,
                      s->two_state ? UINT32_MAX : s->b_off),
            mir_const(inst_mu, t_offset, s->width),
            mir_const(inst_mu, t_offset, s->nelems),
            mir_const(inst_mu, t_offset, flags),
         };
         mir_value_t bind_res =
            mir_build_fcall(inst_mu, ident_new("__sv_island_bind_sig"),
                            t_offset, MIR_NULL_STAMP,
                            s_args, ARRAY_LEN(s_args));
         mir_build_consume(inst_mu, bind_res);
      }

      mir_value_t f_args[] = { handle };
      mir_value_t fin_res =
         mir_build_fcall(inst_mu, ident_new("__sv_island_finish"),
                         t_offset, MIR_NULL_STAMP,
                         f_args, ARRAY_LEN(f_args));
      mir_build_consume(inst_mu, fin_res);
   }
}

void vlog_island_lower_emit_funcs(vlog_island_lower_ctx_t *ctx,
                                  mir_context_t *mc, vlog_node_t body,
                                  ident_t qual)
{
   if (ctx == NULL)
      return;

   for (uint32_t ii = 0; ii < ctx->plan->nislands; ii++) {
      const vlog_island_m2_t *m2 = &(ctx->islands[ii]);
      if (m2->n_fused_comb == 0)
         continue;

      vlog_lower_island_comb_fn(mc, body, qual, m2->eval_sym,
                                m2->isl, m2->fused_proc);
   }
}

void vlog_island_lower_free(vlog_island_lower_ctx_t *ctx)
{
   if (ctx == NULL)
      return;

   for (uint32_t ii = 0; ii < ctx->plan->nislands; ii++) {
      free(ctx->islands[ii].fused_proc);
      free(ctx->islands[ii].sig_in);
      free(ctx->islands[ii].sig_out);
      free(ctx->islands[ii].sig_comb_w);
   }
   free(ctx->islands);
   if (ctx->fused_stmts != NULL)
      hset_free(ctx->fused_stmts);
   vlog_island_plan_free(ctx->plan);
   free(ctx);
}
