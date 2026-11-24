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

#ifndef _VLOG_ISLAND_LOWER_H
#define _VLOG_ISLAND_LOWER_H

#include "prim.h"
#include "hash.h"
#include "vlog/vlog-island.h"

typedef struct {
   uint32_t *write_sigs;
   uint32_t  n_write_sigs;
   uint32_t  n_fused_ff;
   ident_t   ff_sym;
} vlog_island_dom_m3_t;

typedef struct {
   const vlog_island_t  *isl;
   bool                 *fused_proc;   // [isl->nprocs]
   uint32_t              n_fused_comb;
   uint32_t              n_fused_ff;
   bool                 *sig_in;       // [isl->nsigs]
   bool                 *sig_out;      // [isl->nsigs]
   bool                 *sig_comb_w;   // [isl->nsigs]
   bool                 *sig_comb_r;   // [isl->nsigs]
   bool                 *sig_ext_w;    // [isl->nsigs]
   vlog_island_dom_m3_t *doms;         // [isl->ndomains]
   ident_t               eval_sym;
} vlog_island_m2_t;

typedef struct {
   vlog_island_plan_t *plan;
   vlog_island_m2_t   *islands;       // [plan->nislands]
   hset_t             *fused_stmts;
} vlog_island_lower_ctx_t;

vlog_island_lower_ctx_t *vlog_island_lower_prepare(vlog_node_t body,
                                                   bool is_mixed,
                                                   mir_unit_t *inst_mu,
                                                   bool cover_counts);
void vlog_island_lower_emit_init(vlog_island_lower_ctx_t *ctx,
                                 mir_unit_t *inst_mu, mir_value_t self);
void vlog_island_lower_emit_funcs(vlog_island_lower_ctx_t *ctx,
                                  mir_context_t *mc, vlog_node_t body,
                                  ident_t qual);
void vlog_island_lower_free(vlog_island_lower_ctx_t *ctx);

#endif  // _VLOG_ISLAND_LOWER_H
