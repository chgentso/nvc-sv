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

#ifndef _VHDL_ISLAND_H
#define _VHDL_ISLAND_H

#include "prim.h"
#include "array.h"
#include "tree.h"
#include "type.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define VHDL_ISIG_MAX_DEPTH         8
#define VHDL_ISLAND_MAX_ANCESTORS   16
#define VHDL_ISLAND_MAX_ELEMS       65536u
#define VHDL_ISLAND_MAX_WIDTH       4096u
#define VHDL_ISLAND_MAX_TOTAL_BITS  (1u << 20)
#define VHDL_ISLAND_MAX_ARRAY2D_BITS 1024u
#define VHDL_ISLAND_MAX_LOOP_ITERS  4096
#define VHDL_ISLAND_MAX_CALL_DEPTH  6

typedef enum {
   VISIG_STD_LOGIC = 0,
   VISIG_BIT,
   VISIG_BOOL,
   VISIG_ENUM,
   VISIG_INT,
} vhdl_isig_elem_t;

typedef struct {
   uint32_t sig_id;
   uint32_t pos_lo;
   uint32_t pos_hi;
} vhdl_write_range_t;

typedef struct {
   uint32_t         id;
   tree_t           decl;
   type_t           type;
   uint8_t          path_len;
   uint8_t          byte_size;
   int              field_path[VHDL_ISIG_MAX_DEPTH];
   vhdl_isig_elem_t elem_kind;
   bool             is_signed;
   bool             is_vector;
   bool             is_array2d;
   bool             is_port;
   port_mode_t      port_mode;
   bool             two_state;
   bool             ineligible;
   bool             has_ext_driver;
   bool             has_9state_driver;
   bool             ext_read;
   bool             child_read;
   int              driver_proc;
   uint32_t         width;
   uint32_t         nelems;
   range_kind_t     dir;
   int64_t          left;
   int64_t          right;
   range_kind_t     outer_dir;
   int64_t          outer_left;
   int64_t          outer_right;
} vhdl_isig_t;

typedef struct {
   uint32_t sig_id;
   uint32_t a_off;
   uint32_t b_off;
   uint32_t rt_flags;
} vhdl_isl_sig_t;

typedef enum {
   VIPROC_NONE = 0,
   VIPROC_COMB,
   VIPROC_FF,
} vhdl_iproc_kind_t;

typedef struct {
   tree_t            stmt;
   int               block_stmt_idx;
   tree_t            owner_block;
   tree_t            owner_ancestors[VHDL_ISLAND_MAX_ANCESTORS];
   int               n_owner_ancestors;
   ident_t           fused_key;
   vhdl_iproc_kind_t kind;
   bool              rejected;
   uint32_t          clk_sig;
   int32_t           rst_sig;
   bool              clk_posedge;
   bool              rst_posedge;
   uint8_t           edge_flags;
   tree_t            if_stmt;
   tree_t            clk_branch;
   tree_t            rst_branch;
   uint32_t         *reads;
   uint32_t          n_reads;
   uint32_t         *writes;
   uint32_t          n_writes;
   vhdl_write_range_t *read_ranges;
   uint32_t          n_read_ranges;
   vhdl_write_range_t *write_ranges;
   uint32_t          n_write_ranges;
   int               island_idx;
   int               dom_idx;
} vhdl_iproc_t;

typedef struct {
   uint32_t  clk_isl_sig;
   int32_t   rst_isl_sig;
   bool      clk_posedge;
   bool      rst_posedge;
   uint8_t   edge_flags;
   ident_t   fn_name;
   uint32_t *proc_idxs;
   uint32_t  n_procs;
   uint32_t *write_isl_sigs;
   uint32_t  n_write_sigs;
} vhdl_idom_t;

typedef struct {
   uint32_t        id;
   ident_t         eval_name;
   bool            two_state;
   uint32_t        state_bytes;
   vhdl_isl_sig_t *sigs;
   uint32_t        n_sigs;
   uint32_t       *comb_procs;
   uint32_t        n_comb_procs;
   vhdl_idom_t    *doms;
   uint32_t        n_doms;
} vhdl_island_t;

static inline const vhdl_isl_sig_t *vhdl_island_find_sig(const vhdl_island_t *isl,
                                                         uint32_t sig_id)
{
   for (uint32_t i = 0; i < isl->n_sigs; i++) {
      if (isl->sigs[i].sig_id == sig_id)
         return &(isl->sigs[i]);
   }
   return NULL;
}

typedef struct _vhdl_island_ctx {
   tree_t         block;
   tree_t         ancestors[VHDL_ISLAND_MAX_ANCESTORS];
   int            n_ancestors;
   tree_t         eval_block;
   tree_t         eval_ancestors[VHDL_ISLAND_MAX_ANCESTORS];
   int            n_eval_ancestors;
   ident_t        scope_prefix;
   vhdl_isig_t   *sigs;
   uint32_t       n_sigs;
   vhdl_iproc_t  *procs;
   uint32_t       n_procs;
   vhdl_island_t *islands;
   uint32_t       n_islands;
   uint32_t      *ext_read_sigs;
   uint32_t       n_ext_read_sigs;
   bool          *stmt_fused;
   int            n_block_stmts;
} vhdl_island_ctx_t;

vhdl_island_ctx_t *vhdl_island_analyze(tree_t block, const tree_t *ancestors,
                                       int n_ancestors, ident_t scope_prefix);
void vhdl_island_ctx_free(vhdl_island_ctx_t *ctx);

bool vhdl_island_stmt_fused(const vhdl_island_ctx_t *ctx, int stmt_idx);
int vhdl_island_lookup_leaf(const vhdl_island_ctx_t *ctx, tree_t decl,
                            const int *path, uint8_t path_len);
bool vhdl_island_extract_path(tree_t block, tree_t expr, tree_t *decl_out,
                              int *path_out, uint8_t *len_out);
bool vhdl_island_extract_path_ctx(const vhdl_island_ctx_t *ctx, tree_t expr,
                                  tree_t *decl_out, int *path_out,
                                  uint8_t *len_out);
bool vhdl_island_collect_expr_leaves(const vhdl_island_ctx_t *ctx, tree_t expr,
                                     uint32_t **out_ids, uint32_t *out_n);
bool vhdl_island_eval_const_int(tree_t block, tree_t expr, int64_t *out);
bool vhdl_island_eval_const_int_ctx(const vhdl_island_ctx_t *ctx, tree_t expr,
                                    int64_t *out);
bool vhdl_island_const_slice_width(const vhdl_island_ctx_t *ctx, tree_t rg,
                                   uint32_t *width_out);
bool vhdl_island_is_sig_root(tree_t expr);
tree_t vhdl_island_resolve_func_body(tree_t decl);

static inline void vhdl_island_set_eval_scope(vhdl_island_ctx_t *ctx,
                                              tree_t blk,
                                              const tree_t *ancs,
                                              int n_ancs)
{
   ctx->eval_block = blk;
   if (n_ancs > VHDL_ISLAND_MAX_ANCESTORS)
      n_ancs = VHDL_ISLAND_MAX_ANCESTORS;
   ctx->n_eval_ancestors = n_ancs;
   if (n_ancs > 0)
      memcpy(ctx->eval_ancestors, ancs, n_ancs * sizeof(tree_t));
}

#endif   // _VHDL_ISLAND_H
