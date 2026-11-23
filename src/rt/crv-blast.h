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

#ifndef _RT_CRV_BLAST_H
#define _RT_CRV_BLAST_H

#include "prim.h"
#include "rt/crv.h"

// Bit-blaster: turns the nodes of the constraint graph into an and-inverter
// graph (AIG) over the bits of the variables.  Slices, masks and casts are
// wiring, adders are ripple adders, multiplication is an array multiplier,
// division and modulo are a restoring divider whose quotient and remainder
// are gate outputs, shifts are barrel shifters.  Gates are shared by
// structural hashing and folded when an input is constant.
//
// The value of a node is as crv_eval computes it: the bits of its width,
// and a literal that says the value is undefined (division by zero, an
// index out of range, a null access) which the logical operators treat as
// crv_eval does.

typedef uint32_t blit_t;           // Literal: 2 * gate + inverted
#define BLIT_FALSE 0
#define BLIT_TRUE  1

typedef struct _blast blast_t;

// The variables that are the inputs, laid out as the solver lays them out:
// the scalar variables, then the size of each array, then the elements
typedef struct {
   unsigned        nv;          // Number of variables
   const uint8_t  *width;       // Width and signedness of each
   const bool     *sgn;
   unsigned        nsc;         // As for crv_eval
   const int      *ebase;
   const unsigned *sizes;
   const bool     *fixed;       // NULL or per variable: value is known
   const int64_t  *value;
} blast_shape_t;

// Facts about the circuit that the stages after the blaster decline on
typedef struct {
   unsigned inputs, gates;
   unsigned nonlinear;          // Widest of the narrower operands of a
                                // multiplier, divider or modulo whose both
                                // operands have variable bits
   unsigned nops;               // Arithmetic and comparison nodes encoded
} blast_stats_t;

// An arithmetic or comparison node whose operands have variable bits: the
// variables of its operands are related by it
typedef struct {
   crv_node_t node;
   uint8_t    op;               // crv_binary_t
   uint8_t    wa, wb;           // Bits of each operand that are not constant
} blast_op_t;

blast_t *blast_new(const crv_t *c, const blast_shape_t *shape);
void blast_free(blast_t *b);

blit_t blast_not(blit_t a);
blit_t blast_and(blast_t *b, blit_t x, blit_t y);
blit_t blast_or(blast_t *b, blit_t x, blit_t y);

// Value of a node: bit i (zero above its width), width and undefined flag
unsigned blast_width(blast_t *b, crv_node_t n);
blit_t blast_bit(blast_t *b, crv_node_t n, unsigned i);
blit_t blast_fail(blast_t *b, crv_node_t n);

// True when the value is defined and not zero: a hard constraint holds
blit_t blast_holds(blast_t *b, crv_node_t n);

// True when every value is defined and they are all different
blit_t blast_unique(blast_t *b, const crv_node_t *nodes, unsigned count);

// Inputs
unsigned blast_input_count(const blast_t *b);
void blast_input_var(const blast_t *b, unsigned i, int *var, unsigned *bit);

// Inputs in the order a depth-first walk from the outputs first reaches
// them.  Returns how many were reached, at most the number of inputs.
unsigned blast_fanin_order(const blast_t *b, const blit_t *outs,
                           unsigned nouts, unsigned *order);

// Gates in the cone of the outputs
unsigned blast_cone_gates(const blast_t *b, const blit_t *outs,
                          unsigned nouts);

blast_stats_t blast_stats(const blast_t *b);
const blast_op_t *blast_ops(const blast_t *b, unsigned *count);

// Evaluate up to 64 assignments at once: vals holds nv values for each
// lane.  Then blast_lit reports a literal for every lane as the bits of a
// word.
void blast_sim(blast_t *b, const int64_t *vals, unsigned lanes);
uint64_t blast_lit(const blast_t *b, blit_t lit);

#endif  // _RT_CRV_BLAST_H
