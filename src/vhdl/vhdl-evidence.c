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
#include "tree.h"

//
// Clocking evidence of VHDL processes and attributes of signals and
// units, for --netlist --evidence.
//

void vhdl_proc_evidence(conn_design_t *cd, conn_proc_t *proc, tree_t t)
{
   conn_evidence_t *ev = dalloc(cd, sizeof(conn_evidence_t));
   ev->cls = CONN_EV_OTHER;
   proc->ev = ev;
}

void vhdl_net_attrs(conn_design_t *cd, conn_net_t *net, tree_t decl)
{
}

void vhdl_block_attrs(conn_design_t *cd, conn_block_t *blk, tree_t block,
                      tree_t outer)
{
}
