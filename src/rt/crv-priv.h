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

#ifndef _RT_CRV_PRIV_H
#define _RT_CRV_PRIV_H

#include "prim.h"
#include "rt/crv.h"

// The graph of the constraint solver: shared by the solver and the
// bit-blaster

// Values are held as 128-bit integers during propagation so that the
// result of any 64-bit operation can be represented exactly before it is
// wrapped to the width of its node.
typedef __int128 i128;

typedef struct { i128 lo, hi; } ivl_t;     // Empty if lo > hi
typedef struct { i128 lo, hi; } range_t;


typedef enum {
   N_CONST, N_VAR, N_BIN, N_UN, N_CAST, N_COND, N_ASIZE, N_AELEM, N_ASUM,
   N_INSIDE, N_DIST, N_INDEX, N_FAIL,
} nkind_t;

typedef struct {
   uint8_t kind, op, width;
   bool    sgn;
   bool    inst;     // Contains an index or array element to substitute
   int32_t a, b, c;
   int32_t aux;
   int64_t val;
} node_t;

typedef struct {
   unsigned  width;
   bool      sgn;
   int64_t   cur;
   uint64_t *rc;
   int64_t  *dom;
   unsigned  ndom, cdom;
} cvar_t;

typedef struct {
   unsigned  width;
   bool      sgn, size_rand, elems_rand;
   unsigned  len;
   int64_t  *init;
   unsigned  ninit;
} carr_t;

typedef struct {
   crv_node_t lo, hi;      // hi is -1 for a single value
} item_t;

typedef struct {
   item_t   *items;
   unsigned  n, cap;
   range_t  *rr;
   unsigned  nrr;
   bool      allconst, prep;
} cset_t;

typedef struct {
   int64_t lo, hi, weight;
   bool    per_value;
} drange_t;

typedef struct {
   drange_t *r;
   unsigned  n, cap;
   bool      sgn, prep;
   range_t  *mr;           // Merged ranges with non-zero weight
   unsigned  nmr;
   range_t  *pr;           // The same ranges for picking
   double   *unit;         // Weight of each value
   int64_t  *wt;           // The weight of each as written and whether it
   bool     *pvl;          // is for each value (:=) or the range (:/)
   unsigned  npr;
} cdist_t;

typedef enum { C_REQ, C_FOREACH, C_UNIQUE } ckind_t;

typedef struct {
   ckind_t     kind;
   crv_node_t  root;
   int         soft;       // Index among soft constraints or -1
   unsigned    nsoft;      // Soft indices of a foreach, one per element
   crv_array_t arr;
   crv_node_t  idx;
   unsigned    first, count;
   bool        dead;       // Soft constraint discarded by disable soft
   int         line;       // Source line for NVC_CRV_DEBUG
} ccons_t;

typedef struct {
   bool        array;
   int         before;
   crv_var_t   after;
} edge_t;

struct _crv {
   node_t   *nodes;
   unsigned  nnodes, cnodes;
   cvar_t   *vars;
   unsigned  nvars, cvars;
   carr_t   *arrays;
   unsigned  narrays, carrays;
   cset_t   *sets;
   unsigned  nsets, csets;
   cdist_t  *dists;
   unsigned  ndists, cdists;
   ccons_t  *cons;
   unsigned  ncons, ccons;
   crv_node_t *uniq;
   unsigned  nuniq, cuniq;
   edge_t   *edges;
   unsigned  nedges, cedges;
   int       nindex;
   int       nsoft;
   bool      greedy;   // Soft constraints are kept one at a time
   bool      bad;      // The graph names an array that does not exist
   bool      trial;    // Solves do not consume randc values
};


// Interface of the solver for the bit-blaster

// Evaluate a node on a full assignment: vals holds the scalar variables,
// then the sizes of the arrays, then their elements (ebase is the first
// element of each array or -1).  Sets *fail if the value is undefined.
int64_t crv_eval(const crv_t *c, crv_node_t n, const int64_t *vals,
                 unsigned nsc, const int *ebase, const unsigned *sizes,
                 bool *fail);

// Merge the ranges of a distribution ready for use
void crv_dist_prepare(cdist_t *cd);

#endif  // _RT_CRV_PRIV_H
