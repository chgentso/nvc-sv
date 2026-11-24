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

#ifndef _RT_ISLAND_H
#define _RT_ISLAND_H

#include "prim.h"
#include "rt/defs.h"
#include "rt/model.h"

#include <stdint.h>

typedef enum {
   RT_ISIG_IN           = (1u << 0),
   RT_ISIG_OUT          = (1u << 1),
   RT_ISIG_TWO_STATE    = (1u << 2),
   RT_ISIG_IS_NET       = (1u << 3),
   RT_ISIG_COMB_WRITTEN = (1u << 4),
} rt_island_sig_flags_t;

typedef struct {
   rt_signal_t *sig;
   int32_t      sig_offset;
   uint32_t     a_off;
   uint32_t     b_off;
   uint32_t     width;
   uint32_t     nelems;
   uint32_t     nwords;
   uint32_t     flags;
   rt_watch_t  *watch;
} rt_island_sig_t;

typedef struct _rt_island {
   rt_model_t          *model;
   struct _rt_island_mgr *mgr;
   uint8_t             *state;
   uint32_t         state_bytes;
   jit_handle_t     eval_handle;
   void            *inst_ctx;
   rt_island_sig_t *sigs;
   uint32_t         nsigs;
   uint32_t        *in_sigs;
   uint32_t         n_in_sigs;
   uint32_t        *sync_sigs;
   uint32_t         n_sync_sigs;
   uint64_t        *scratch;
   uint32_t         scratch_words;
   uint32_t         dirty_gen;
   bool             dirty;
   bool             in_dirty_list;
   bool             initialized;
} rt_island_t;

typedef struct _rt_island_mgr rt_island_mgr_t;

rt_island_mgr_t *rt_island_mgr_new(rt_model_t *m);
void rt_island_mgr_free(rt_island_mgr_t *mgr);
void rt_island_reset_all(rt_island_mgr_t *mgr);
bool rt_island_eval_dirty(jit_t *jit, rt_model_t *m, rt_island_mgr_t *mgr);

void *rt_island_begin(rt_model_t *m, rt_island_mgr_t **mgr_p,
                      jit_handle_t eval_handle, void *inst_ctx,
                      uint32_t state_bytes, uint32_t nsigs);
void rt_island_bind_sig(void *handle, uint32_t idx, sig_shared_t *ss,
                        int32_t sig_offset, uint32_t a_off, uint32_t b_off,
                        uint32_t width, uint32_t nelems, uint32_t flags);
void rt_island_finish(void *handle);

// Runtime hook provided by model.c to push packed updates cleanly
void rt_island_deposit_packed(rt_model_t *m, rt_signal_t *s, uint32_t msb_off,
                              uint32_t count, uint64_t a_val, uint64_t b_val,
                              uint32_t src_flags);

#endif  // _RT_ISLAND_H
