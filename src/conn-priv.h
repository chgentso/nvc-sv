//
//  Copyright (C) 2025  Nick Gasson
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

#ifndef _CONN_PRIV_H
#define _CONN_PRIV_H

#include "prim.h"
#include "array.h"
#include "conn.h"
#include "hash.h"

#include <string.h>

//
// What the evidence walkers (vhdl/vhdl-evidence.c, vlog/vlog-evidence.c)
// need from conn.c.  Not for use outside the connectivity code.
//

typedef A(conn_range_t) range_list_t;

#define SCOPED_LIST(type, name) \
   type name __attribute__((cleanup(__cleanup_array))) = AINIT

// Allocate zeroed memory owned by the design and freed with it
void *dalloc(conn_design_t *cd, size_t size);
char *dstr(conn_design_t *cd, const char *s);

#define DDUP(cd, list, type) ({                                   \
         type *_p = dalloc((cd), MAX((list).count, 1) * sizeof(type)); \
         if ((list).count > 0)                                    \
            memcpy(_p, (list).items, (list).count * sizeof(type)); \
         _p;                                                      \
      })

// Append a range of bits of a net: ignored if NET is NULL or HI < LO
void add_range(range_list_t *l, conn_net_t *net, unsigned lo, unsigned hi);

// Sort and merge overlapping or adjacent ranges of the same net
void merge_ranges(range_list_t *l);

// The bits of a VHDL name (a reference with selects) or a Verilog
// expression that names a net: returns false if it is not a net of the
// design.  APPROX is set if a non-static select made the whole net stand
// for the part.  Only meaningful while the instance is being built.
bool vhdl_select(conn_design_t *cd, tree_t name, conn_range_t *out,
                 bool *approx);
bool vlog_select(conn_design_t *cd, vlog_node_t v, conn_range_t *out,
                 bool *approx);

// Append the ranges of the nets that a VHDL tree reads.  If
// TARGETS_ARE_WRITES the targets of signal assignments in it are not reads.
void vhdl_collect_reads(conn_design_t *cd, tree_t root, range_list_t *reads,
                        bool targets_are_writes);

// Walkers: compute the evidence of one process and store it in
// proc->ev (called after the reads and drives of the process are known),
// and the attributes of nets and blocks.  Allocate with dalloc.
void vhdl_proc_evidence(conn_design_t *cd, conn_proc_t *proc, tree_t t);
void vlog_proc_evidence(conn_design_t *cd, conn_proc_t *proc, vlog_node_t v);
void vhdl_net_attrs(conn_design_t *cd, conn_net_t *net, tree_t decl);
void vlog_net_attrs(conn_design_t *cd, conn_net_t *net, vlog_node_t decl);
void vhdl_block_attrs(conn_design_t *cd, conn_block_t *blk, tree_t block,
                      tree_t outer);
void vlog_block_attrs(conn_design_t *cd, conn_block_t *blk, vlog_node_t body);

// Declaration -> list of attribute specifications, for the VHDL walker
hash_t *conn_specs(conn_design_t *cd);

// Helper for the walkers: append an attribute, replacing one of the same
// name
void conn_add_attr(conn_design_t *cd, conn_attr_t **list, unsigned *count,
                   const char *name, const char *value);

#endif   // _CONN_PRIV_H
