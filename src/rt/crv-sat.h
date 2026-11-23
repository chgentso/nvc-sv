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

#ifndef _RT_CRV_SAT_H
#define _RT_CRV_SAT_H

#include "prim.h"
#include "rt/crv-blast.h"

// CDCL SAT solver for the complete fallback stage of the constraint solver.
// A fresh solver is built per component with no state kept across calls.
// Literals are 2 * var + sign (sign 0 is positive, 1 is negative).

typedef enum { SAT_SAT, SAT_UNSAT, SAT_GIVEUP } sat_res_t;

typedef struct _sat sat_t;
typedef uint64_t (*sat_rng_fn_t)(void *ctx);

sat_t *sat_new(unsigned nvars);
void sat_free(sat_t *s);

unsigned sat_new_var(sat_t *s);
bool sat_add_clause(sat_t *s, const uint32_t *lits, unsigned n);
void sat_set_budget(sat_t *s, uint64_t conflicts);

// Solve the CNF with randomized initial polarities and decision order driven
// by rng (or deterministic defaults if rng is NULL).
sat_res_t sat_solve(sat_t *s, sat_rng_fn_t rng, void *rng_ctx);
bool sat_value(const sat_t *s, unsigned var);
uint64_t sat_conflicts(const sat_t *s);

// Tseitin-encode the cones of roots (all asserted true) from the bit-blaster
// AIG and solve.  On SAT_SAT, input_vals[0 .. blast_input_count(b) - 1]
// receives 0 or 1 for every AIG input.
sat_res_t sat_solve_aig(const blast_t *b, const blit_t *roots, unsigned nroots,
                        uint64_t conflict_budget, sat_rng_fn_t rng,
                        void *rng_ctx, uint8_t *input_vals);

#endif  // _RT_CRV_SAT_H
