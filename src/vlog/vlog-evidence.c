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

#include "util.h"
#include "conn-priv.h"
#include "vlog/vlog-node.h"

//
// Clocking evidence of Verilog processes and attributes of nets and
// modules, for --netlist --evidence.
//

void vlog_proc_evidence(conn_design_t *cd, conn_proc_t *proc, vlog_node_t v)
{
   conn_evidence_t *ev = dalloc(cd, sizeof(conn_evidence_t));
   ev->cls = CONN_EV_OTHER;
   proc->ev = ev;
}

void vlog_net_attrs(conn_design_t *cd, conn_net_t *net, vlog_node_t decl)
{
}

void vlog_block_attrs(conn_design_t *cd, conn_block_t *blk, vlog_node_t body)
{
}
