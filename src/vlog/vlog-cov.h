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

#ifndef _VLOG_COV_H
#define _VLOG_COV_H

#include "prim.h"
#include "parse.h"

typedef struct {
   const tokenq_t *t;
   int             n;
} cov_toks_t;

typedef struct {
   cov_toks_t lo, hi;
   bool       range;
   bool       lo_dollar, hi_dollar;
} cov_item_t;

typedef struct {
   cov_item_t *items;
   int         nitems;
   bool        rep;
   int         rkind;    // 0 consecutive, 1 goto, 2 non-consecutive
   cov_toks_t  rmin, rmax;
} cov_step_t;

typedef struct {
   cov_step_t *steps;
   int         nsteps;
} cov_seq_t;

typedef enum {
   COV_REGULAR, COV_IGNORE, COV_ILLEGAL, COV_DEFAULT
} cov_bin_kind_t;

typedef struct {
   cov_bin_kind_t kind;
   ident_t        name;
   int            array;     // 0 for none, 1 for [], 2 for [ size ]
   cov_toks_t     size;
   bool           wildcard;
   bool           trans;
   bool           has_with;
   cov_toks_t     with_expr;
   bool           has_set;
   cov_toks_t     set_expr;
   bool           has_iff;
   cov_toks_t     iff;
   cov_item_t    *items;
   int            nitems;
   cov_seq_t     *seqs;
   int            nseqs;
} cov_bin_t;

typedef struct {
   ident_t    name;
   bool       type_option;
   cov_toks_t value;
} cov_option_t;

typedef struct {
   ident_t       label;
   bool          implicit;
   bool          is_enum;
   ident_t       enum_cast;   // Type name when the expression is a cast
   cov_toks_t    expr;
   cov_toks_t    iff;
   bool          has_iff;
   cov_bin_t    *bins;
   int           nbins;
   cov_option_t *opts;
   int           nopts;
} cov_cp_t;

typedef enum {
   COV_SEL_AND, COV_SEL_OR, COV_SEL_NOT, COV_SEL_BINSOF, COV_SEL_WITH
} cov_sel_kind_t;

typedef struct _cov_sel {
   cov_sel_kind_t   kind;
   struct _cov_sel *left, *right;
   ident_t          cp, bin;
   bool             has_bin;
   cov_item_t      *isect;
   int              nisect;
   bool             has_isect;
   int              set;
   cov_toks_t       with;
   int              wid;
} cov_sel_t;

typedef struct {
   cov_bin_kind_t kind;
   ident_t        name;
   cov_sel_t     *sel;
   bool           has_iff;
   cov_toks_t     iff;
} cov_xbin_t;

typedef struct {
   ident_t       label;
   ident_t      *cps;
   int           ncps;
   cov_xbin_t   *bins;
   int           nbins;
   bool          has_iff;
   cov_toks_t    iff;
   cov_option_t *opts;
   int           nopts;
} cov_cross_t;

typedef struct {
   ident_t       name;
   ident_t       owner;      // Enclosing class or NULL
   bool          has_event;
   cov_toks_t    event;
   cov_toks_t    ctor_args;
   cov_toks_t    sample_args;
   ident_t      *ctor_names;
   int           nctor;
   cov_toks_t   *ctor_decls;
   int           nctor_decls;
   bool         *ctor_ref;
   int           nctor_ref;
   ident_t      *sample_names;
   int           nsample;
   cov_toks_t   *sample_decls;
   int           nsample_decls;
   cov_cp_t     *cps;
   int           ncps;
   cov_cross_t  *crosses;
   int           ncrosses;
   cov_option_t *opts;
   int           nopts;
} cov_group_t;

// Source text for a covergroup where each span of tokens of the model is
// replaced by an identifier cov_slot_name(i)
typedef struct {
   text_buf_t  *text;
   cov_toks_t  *slots;
   int          nslots;
} cov_text_t;

const char *cov_generate(cov_group_t *g, cov_text_t *out);
const char *cov_slot_prefix(void);

#endif  // _VLOG_COV_H
