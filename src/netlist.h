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

#ifndef _NETLIST_H
#define _NETLIST_H

#include "prim.h"
#include "conn.h"

#include <stdio.h>

typedef struct {
   int max_depth;      // Deepest block level emitted as a module, -1 for all
} netlist_opts_t;

// Write a block-level schematic of the design in Yosys JSON format as
// consumed by netlistsvg.  One module is written per elaborated instance;
// processes are `$process` cells and expressions in port maps are `$expr`
// cells.  Instances below max_depth appear as cells whose type is not
// defined by a module.
void netlist_write(const conn_design_t *cd, const netlist_opts_t *opts,
                   FILE *f);

#endif   // _NETLIST_H
