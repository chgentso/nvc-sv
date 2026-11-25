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

#ifndef _VHDL_ISLAND_LOWER_H
#define _VHDL_ISLAND_LOWER_H

#include "prim.h"
#include "cov/cov-api.h"
#include "lower.h"
#include "mir/mir-unit.h"
#include "tree.h"
#include "vhdl/vhdl-island.h"

typedef vcode_reg_t (*vhdl_island_sig_lookup_fn_t)(lower_unit_t *lu,
                                                   tree_t decl,
                                                   const int *path,
                                                   uint8_t path_len);

vhdl_island_ctx_t *vhdl_island_lower_prepare(mir_context_t *mc, tree_t block,
                                             ident_t scope_prefix,
                                             cover_data_t *cover);

void vhdl_island_lower_emit_vcode_init(const vhdl_island_ctx_t *ctx,
                                       lower_unit_t *lu,
                                       vhdl_island_sig_lookup_fn_t get_sig_fn);

#endif   // _VHDL_ISLAND_LOWER_H
