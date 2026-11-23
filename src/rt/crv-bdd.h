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

#ifndef _RT_CRV_BDD_H
#define _RT_CRV_BDD_H

#include "prim.h"

// Reduced ordered binary decision diagrams for the exact sampling stage of
// the constraint solver.  Variable i is the i'th in the order, the first at
// the top.  A node is a (variable, low, high) triple kept once in a unique
// table; there are no complement edges, negation is an operation with a
// cache of its own.  The number of nodes made is limited: when it is
// reached the manager is "over", every operation returns the false node
// from then on and the caller discards the whole build.  A manager is kept
// per thread and its memory is reused from one build to the next.

typedef uint32_t bnode_t;
#define BDD_FALSE 0
#define BDD_TRUE  1

// Counts of assignments, up to 2^256: little endian limbs, as many as the
// largest count needs (two when the variables counted are at most 126)
#define BNUM_MAX 5
typedef struct {
   unsigned nl;
   uint64_t w[BNUM_MAX];
} bnum_t;

bnum_t bnum_zero(unsigned nl);
unsigned bnum_bitlen(const bnum_t *a);           // 0 for zero
bool bnum_is_zero(const bnum_t *a);
int bnum_cmp(const bnum_t *a, const bnum_t *b);
void bnum_sub1(bnum_t *a);                       // a - 1, a not zero
unsigned __int128 bnum_u128(const bnum_t *a);    // The value, if it fits

typedef struct _bdd bdd_t;

// Start a build over nvars variables with at most max_nodes nodes
bdd_t *bdd_open(unsigned nvars, unsigned max_nodes);
void bdd_close(bdd_t *b);

bool bdd_over(const bdd_t *b);
unsigned bdd_nodes(const bdd_t *b);      // Made so far, terminals included
size_t bdd_bytes(const bdd_t *b);        // Memory held, the high-water mark

unsigned bdd_top(const bdd_t *b, bnode_t f);   // Variable of a node

bnode_t bdd_var(bdd_t *b, unsigned i);   // The function "variable i"
bnode_t bdd_not(bdd_t *b, bnode_t f);
bnode_t bdd_and(bdd_t *b, bnode_t f, bnode_t g);
bnode_t bdd_or(bdd_t *b, bnode_t f, bnode_t g);

// Exists the variables with quant[var] not zero
bnode_t bdd_exists_set(bdd_t *b, bnode_t f, const uint8_t *quant);

// The function with each variable with fix[var] not zero set to val[var]
bnode_t bdd_cofactor(bdd_t *b, bnode_t f, const uint8_t *fix,
                     const uint8_t *val);

// The unsigned number whose bits are the variables pos[0] (least
// significant) to pos[w - 1], with the top bit inverted if inv_msb (a
// signed number), is >= c or <= c.  The positions may be in any order.
bnode_t bdd_cmp_const(bdd_t *b, const unsigned *pos, unsigned w,
                      bool inv_msb, uint64_t c, bool ge);

// Number of assignments of the nv variables vars[0 .. nv - 1] (positions
// in increasing order) for which f is true, f depending on no others.
// False if there are more than 256 variables or the build is over.
bool bdd_count(bdd_t *b, bnode_t f, const unsigned *vars, unsigned nv,
               bnum_t *total);

// The r'th assignment of the same set, 0 <= r < the count, written to
// bits[vars[i]].  Call bdd_count with the same f and vars just before.
// Together they make a bijection between [0, count) and the assignments.
void bdd_unrank(bdd_t *b, bnode_t f, const unsigned *vars, unsigned nv,
                const bnum_t *r, uint8_t *bits);

#endif  // _RT_CRV_BDD_H
