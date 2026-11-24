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

#ifndef _VLOG_ISLAND_H
#define _VLOG_ISLAND_H

#include "prim.h"
#include "vlog/vlog-phase.h"

#include <stdbool.h>
#include <stdint.h>

#define NVC_ISLAND_MAX_NODES 8000

typedef enum {
   ISIG_INTERNAL = 0,
   ISIG_BOUNDARY_IN,
   ISIG_BOUNDARY_OUT,
   ISIG_BOUNDARY_INOUT,
   ISIG_CLOCK,
   ISIG_RESET,
} island_sig_kind_t;

typedef enum {
   ISIG_EJ_NONE = 0,
   ISIG_EJ_MULTI_DRIVER,
   ISIG_EJ_INOUT_PORT,
   ISIG_EJ_NETTYPE,
   ISIG_EJ_ALIAS,
   ISIG_EJ_FORCE_TARGET,
   ISIG_EJ_HIER_REF,
   ISIG_EJ_DYNAMIC_TYPE,
   ISIG_EJ_REAL_TYPE,
   ISIG_EJ_MIXED_PORT,
   ISIG_EJ_SHARED_EJECTED,
   ISIG_EJ_UNSUPPORTED_TYPE,
   ISIG_EJ_MAX
} island_sig_eject_reason_t;

typedef struct {
   vlog_node_t               decl;
   vlog_node_t               port;
   ident_t                   name;
   uint32_t                  width;
   uint32_t                  nelems;
   bool                      two_state;
   bool                      is_external;
   island_sig_kind_t         kind;
   island_sig_eject_reason_t eject_reason;
   uint32_t                  a_off;
   uint32_t                  b_off;
   uint32_t                  nwords;
} island_sig_t;

typedef enum {
   IPROC_ASSIGN = 0,
   IPROC_COMB,
   IPROC_FF,
} island_proc_kind_t;

typedef enum {
   IPROC_EJ_NONE = 0,
   IPROC_EJ_INITIAL,
   IPROC_EJ_FINAL,
   IPROC_EJ_DELAY,
   IPROC_EJ_EVENT_OR_WAIT,
   IPROC_EJ_FORK_JOIN,
   IPROC_EJ_FORCE_RELEASE,
   IPROC_EJ_SVA,
   IPROC_EJ_SYSCALL,
   IPROC_EJ_UNSUPPORTED_STMT,
   IPROC_EJ_UNSUPPORTED_EXPR,
   IPROC_EJ_HIER_REF,
   IPROC_EJ_DYNAMIC_OP,
   IPROC_EJ_MULTI_CLOCK,
   IPROC_EJ_NON_RTL_SENSITIVITY,
   IPROC_EJ_MIXED_ASSIGN_IN_FF,
   IPROC_EJ_LATCH,
   IPROC_EJ_GATE_OR_UDP,
   IPROC_EJ_MULTI_DRIVER_TARGET,
   IPROC_EJ_TOO_LARGE,
   IPROC_EJ_MAX
} island_proc_eject_reason_t;

typedef struct {
   vlog_node_t        stmt;
   island_proc_kind_t kind;
   int                domain_id;
   uint32_t          *reads;
   uint32_t           nreads;
   uint32_t          *writes;
   uint32_t           nwrites;
   uint32_t           node_count;
   int                topo_order;
   int                topo_level;
   int                scc_id;
   bool               in_cycle;
} island_proc_t;

typedef struct {
   int       clk_sig;
   bool      clk_posedge;
   int       rst_sig;
   bool      rst_posedge;
   uint32_t *ff_procs;
   uint32_t  nff_procs;
} island_domain_t;

typedef struct {
   vlog_node_t                stmt;
   island_proc_eject_reason_t reason;
} island_ejected_proc_t;

typedef struct _vlog_island {
   ident_t          mod_name;
   ident_t          body_name;
   uint32_t         island_id;
   island_sig_t    *sigs;
   uint32_t         nsigs;
   island_proc_t   *procs;
   uint32_t         nprocs;
   island_domain_t *domains;
   uint32_t         ndomains;
   uint32_t        *topo_order;
   uint32_t         ntopo;
   uint32_t         comb_levels;
   uint32_t         comb_sccs;
   uint32_t         n_assign;
   uint32_t         n_comb;
   uint32_t         n_ff;
   uint32_t         n_internal_sigs;
   uint32_t         n_boundary_in;
   uint32_t         n_boundary_out;
   uint32_t         n_boundary_inout;
   uint32_t         n_clock_sigs;
   uint32_t         n_reset_sigs;
   uint32_t         header_bytes;
   uint32_t         state_bytes;
   uint32_t         total_nodes;
} vlog_island_t;

typedef struct {
   ident_t                mod_name;
   ident_t                body_name;
   vlog_island_t        **islands;
   uint32_t               nislands;
   island_ejected_proc_t *ejected_procs;
   uint32_t               nejected_procs;
   uint32_t               proc_eject_counts[IPROC_EJ_MAX];
   uint32_t               sig_eject_counts[ISIG_EJ_MAX];
   uint32_t               n_total_procs;
   uint32_t               n_fused_procs;
   uint32_t               n_ejected_sigs;
   bool                   has_mixed_ports;
} vlog_island_plan_t;

void vlog_island_set_find_mod(vlog_find_mod_fn_t fn, void *ctx);
vlog_island_plan_t *vlog_island_analyze(vlog_node_t body, bool is_mixed);
void vlog_island_plan_free(vlog_island_plan_t *plan);
void vlog_island_dump(const vlog_island_plan_t *plan);

const char *vlog_island_sig_eject_str(island_sig_eject_reason_t r);
const char *vlog_island_proc_eject_str(island_proc_eject_reason_t r);

#endif  // _VLOG_ISLAND_H
