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

#ifndef _RT_CRV_H
#define _RT_CRV_H

#include "prim.h"

// Constraint solver for System Verilog randomize().  Generated code builds
// a graph on each call: variables being randomized, constants for the
// current value of everything else, and constraints over them.

typedef struct _crv crv_t;
typedef int32_t crv_node_t;     // Index of a node in the graph
typedef int32_t crv_var_t;      // Index of a scalar variable
typedef int32_t crv_array_t;    // Index of an array variable

typedef enum {
   // Results have the width and signedness given to crv_binary.  The
   // operands have already been converted to this by crv_cast where the
   // language requires it.
   CRV_ADD, CRV_SUB, CRV_MUL, CRV_DIV, CRV_MOD,
   CRV_AND, CRV_OR, CRV_XOR, CRV_SHL, CRV_SHR, CRV_ASR,
   // Results are one bit.  The comparison uses the signedness of the left
   // operand and both operands have the same width and signedness.
   CRV_EQ, CRV_NE, CRV_LT, CRV_LE, CRV_GT, CRV_GE,
   CRV_LOG_AND, CRV_LOG_OR, CRV_IMPLIES,
} crv_binary_t;

typedef enum {
   CRV_NEG, CRV_INV,            // Same width and signedness as the operand
   CRV_LOG_NOT,                 // Result is one bit
   CRV_REDUCE_AND, CRV_REDUCE_OR, CRV_REDUCE_XOR,
   CRV_POPCNT,                  // 32 bit signed count of the one bits
} crv_unary_t;

crv_t *crv_new(void);
void crv_free(crv_t *c);

// Seed the generator for this thread: the sequence is deterministic for a
// given seed and the order of calls
void crv_seed(uint64_t seed);
void crv_push_seed(uint64_t seed);
uint64_t crv_pop_seed(void);

// Scalar variable with at most 64 bits.  The current value is used if the
// variable is left out of the solution so must be valid.  Values are held
// sign or zero extended to 64 bits according to is_signed.
crv_var_t crv_var(crv_t *c, unsigned width, bool is_signed, int64_t current);

// Scalar variable of arbitrary width (up to 16384 bits).  current points to
// (width + 63) / 64 little-endian 64-bit words, or NULL for zero.
crv_var_t crv_var_wide(crv_t *c, unsigned width, bool is_signed,
                       const uint64_t *current);

// Fix a variable (narrow or wide) to its current value (rand_mode(0)).
void crv_var_fix(crv_t *c, crv_var_t v);

// Every value of the variable is taken once before any repeats.  The state
// is the address of a word owned by the caller, zero to start with, which
// must be passed again on the next call for this variable and object: the
// solver keeps the cycle there and it is never freed.
void crv_var_randc(crv_t *c, crv_var_t v, uint64_t *state);

// Solves of a solver in trial mode draw randc values without taking them
// from the cycle, so the same value may be drawn again by the next solve.
// Once the final solve is done crv_randc_commit takes the values the
// solver holds, as a solve outside trial mode would have.
void crv_trial(crv_t *c, bool on);
void crv_randc_commit(crv_t *c);

// Exclude a value from a variable: used to give an enum only its members
// by calling crv_var_domain with each of them instead.  A variable with
// any domain value takes only those.
void crv_var_domain(crv_t *c, crv_var_t v, int64_t value);

// Make a dynamic array of scalars being randomized.  The size is itself a
// variable which takes current_size unless it is constrained: size_rand is
// false for a fixed array or one where the size is state.  Elements are
// written with crv_array_init before solving and read after it.
crv_array_t crv_array(crv_t *c, unsigned elem_width, bool is_signed,
                      unsigned current_size, bool size_rand, bool elems_rand);
void crv_array_init(crv_t *c, crv_array_t a, unsigned index, int64_t value);

// True if the solver may change the size of the array
bool crv_array_resizable(crv_t *c, crv_array_t a);

// Graph nodes
crv_node_t crv_const(crv_t *c, int64_t value, unsigned width, bool is_signed);
crv_node_t crv_const_wide(crv_t *c, const uint64_t *words, unsigned width,
                          bool is_signed);
crv_node_t crv_var_ref(crv_t *c, crv_var_t v);
crv_node_t crv_binary(crv_t *c, crv_binary_t op, crv_node_t l, crv_node_t r,
                      unsigned width, bool is_signed);
crv_node_t crv_unary(crv_t *c, crv_unary_t op, crv_node_t arg);
crv_node_t crv_cast(crv_t *c, crv_node_t arg, unsigned width, bool is_signed);
crv_node_t crv_cond(crv_t *c, crv_node_t test, crv_node_t t, crv_node_t f);

// Array nodes.  Size has 32 bits and is signed as for the size method.
crv_node_t crv_array_size(crv_t *c, crv_array_t a);
crv_node_t crv_array_elem(crv_t *c, crv_array_t a, crv_node_t index);
crv_node_t crv_array_sum(crv_t *c, crv_array_t a, unsigned width,
                         bool is_signed);
crv_node_t crv_array_reduce(crv_t *c, crv_binary_t op, crv_array_t a,
                            crv_node_t index, crv_node_t body,
                            unsigned width, bool is_signed);

// Sets.  A set is built up then inside is a one bit node.
typedef int32_t crv_set_t;
crv_set_t crv_set_new(crv_t *c);
void crv_set_value(crv_t *c, crv_set_t s, crv_node_t value);
void crv_set_range(crv_t *c, crv_set_t s, crv_node_t lo, crv_node_t hi);
crv_node_t crv_inside(crv_t *c, crv_node_t arg, crv_set_t s);

// Distributions.  Weights are evaluated when the graph is built.  With
// per_value the weight applies to each value in the range (:=) otherwise it
// is shared across the range (:/).  A one bit node that also makes the
// solver pick the argument with these weights.  A value with zero weight is
// excluded.  The result is like inside for the purposes of solving.
typedef int32_t crv_dist_t;
crv_dist_t crv_dist_new(crv_t *c);
void crv_dist_range(crv_t *c, crv_dist_t d, int64_t lo, int64_t hi,
                    int64_t weight, bool per_value);
crv_node_t crv_dist(crv_t *c, crv_node_t arg, crv_dist_t d);

// Constraints.  A soft constraint is dropped if it cannot be satisfied with
// the hard constraints, trying to keep the later ones.
void crv_require(crv_t *c, crv_node_t test, bool soft, int line);
void crv_disable_soft(crv_t *c, crv_node_t target);
void crv_disable_soft_if(crv_t *c, crv_node_t cond, crv_node_t target);

// Every node given must be different.  Used by the unique constraint.
void crv_unique(crv_t *c, const crv_node_t *nodes, unsigned count);

// A body repeated for each index of an array.  Allocate an index, use it in
// nodes, then call crv_foreach with the root of the body: the body is
// required for every index of the array once the array's size is known.
// Foreach may be nested, including over the elements of a two dimensional
// constraint using the outer index in the inner array size expression.
crv_node_t crv_index(crv_t *c);
void crv_foreach(crv_t *c, crv_array_t a, crv_node_t index, crv_node_t body,
                 bool soft);

// Solve a before b
void crv_solve_before(crv_t *c, crv_var_t before, crv_var_t after);
void crv_array_solve_before(crv_t *c, crv_array_t before, crv_var_t after);

// There is no solution in which case nothing changes, or the search ran
// out of work before finding one although there may be one
typedef enum { CRV_SAT, CRV_UNSAT, CRV_GIVEUP } crv_result_t;

crv_result_t crv_solve2(crv_t *c);

// Returns false if there is no solution in which case nothing changes
bool crv_solve(crv_t *c);

// Which stage of the solver may answer.  Read once from NVC_CRV_ENGINE
// (dfs, reject, bdd, sat or auto) and overridden by crv_set_engine for
// tests.  Dfs is the search alone with the components, reject is the
// rejection stage alone (it gives up on a
// component it cannot sample) and auto is the stages in turn, the search
// last, bdd is the exact stage on small groups on its own (it gives up on a
// component it cannot take).  A stage that is not written yet is taken as
// dfs: crv_engine_available says which exist.
typedef enum {
   CRV_ENGINE_AUTO, CRV_ENGINE_DFS, CRV_ENGINE_REJECT, CRV_ENGINE_BDD,
   CRV_ENGINE_SAT,
} crv_engine_t;

crv_engine_t crv_engine(void);
void crv_set_engine(crv_engine_t engine);
bool crv_engine_available(crv_engine_t engine);
const char *crv_engine_name(crv_engine_t engine);

// The limit on the nodes of one BDD (NVC_CRV_BDD_NODES, default 32768;
// 0 turns the stage off).  A forced bdd engine allows 100 times as many.
unsigned crv_bdd_nodes(void);
void crv_set_bdd_nodes(unsigned nodes);

// For the tests of the BDD: the number of solutions of the hard constraints
// of a problem without arrays by counting in the BDD, and the values of the
// variables (nvars of them) of the first max solutions by unranking.  False
// if the stage cannot take the problem.
bool crv_bdd_enumerate(crv_t *c, uint64_t max, uint64_t *count,
                       int64_t *vals);

// Statistics of the calls of crv_solve.  NVC_CRV_STATS=1 prints a table per
// call site at the end of the run and NVC_CRV_STATS=<file> appends one tab
// separated line per call site to the file.  The solver never reads them.
typedef enum {
   CRV_STAGE_NONE,              // No free variable: decided by propagation
   CRV_STAGE_REJECT, CRV_STAGE_BDD,   // Exact: uniform over the solutions
   CRV_STAGE_DFS, CRV_STAGE_SAT, CRV_STAGE_MIX,
   CRV_STAGE_COUNT
} crv_stage_t;

// Why the bdd stage did not take a component
typedef enum {
   CRV_BDD_RANDC,       // A randc variable
   CRV_BDD_GUARDED,     // A dist guarded by a condition
   CRV_BDD_DISTS,       // More than 8 dist variables
   CRV_BDD_BITS,        // More than 256 decision bits
   CRV_BDD_SIZE,        // The size of an array with many possible values
   CRV_BDD_UNIQUE,      // A unique set of more than 8 members
   CRV_BDD_NONLINEAR,   // A product or quotient of two wide variables
   CRV_BDD_ADDERS,      // A sum of more than 6 terms
   CRV_BDD_GATES,       // A circuit of more than 20000 gates
   CRV_BDD_BUDGET,      // The node limit was reached
   CRV_BDD_CHECK,       // The sample failed the check (a bug if it happens)
   CRV_BDD_REASONS
} crv_bdd_reason_t;

typedef struct {
   uint32_t    comps;       // Components of linked variables with a free one
   uint32_t    loose;       // Free variables that no constraint mentions
   uint32_t    freevars;    // Free variables in the components
   uint32_t    maxfree;     // Free variables in the largest component
   uint32_t    stage[CRV_STAGE_COUNT];   // Components answered by each stage
   uint32_t    declined[3]; // Components a stage cannot take, budget of a
                            // stage, all proposals rejected
   bool        soft;        // Has soft constraints
   bool        ok;          // A solution was found
   bool        giveup;      // Not ok and the last attempt ran out of work
   crv_stage_t top;         // Last stage of the call in the order above
   uint64_t    build_ns, solve_ns;
   uint32_t    bdd_declined[CRV_BDD_REASONS];
   uint64_t    bdd_build_ns, bdd_sample_ns;   // Within solve_ns
   uint32_t    bdd_bytes;           // Most memory a BDD build held
} crv_call_stat_t;

bool crv_stats_enabled(void);
void crv_stats_force(bool on);                 // Collect without NVC_CRV_STATS
void crv_stats_site(const char *site);         // Label for the next solve
const crv_call_stat_t *crv_stats_last(void);   // Most recent solve, this thread
void crv_stats_reset(void);                    // Forget the sites
void crv_stats_write(void);                    // Write and forget them
void crv_stats_record(const crv_call_stat_t *s);   // Used by crv_solve
const char *crv_stage_name(crv_stage_t stage);
const char *crv_bdd_reason_name(crv_bdd_reason_t reason);

// Solution
int64_t crv_value(crv_t *c, crv_var_t v);
void crv_value_wide(crv_t *c, crv_var_t v, uint64_t *out);
unsigned crv_array_len(crv_t *c, crv_array_t a);
int64_t crv_array_value(crv_t *c, crv_array_t a, unsigned index);

// Interface from generated code to the solver

#define CRV_FLAGS(width, is_signed) ((width) | ((is_signed) ? 0x10000 : 0))
#define CRV_FLAG_DYNAMIC 0x20000

typedef enum {
   CRV_PHASE_PRE, CRV_PHASE_BUILD, CRV_PHASE_STORE, CRV_PHASE_POST,
   CRV_PHASE_COUNT
} crv_phase_t;

typedef enum {
   CRV_OP_BEGIN, CRV_OP_END, CRV_OP_SOLVE, CRV_OP_OBJECT_ID,
   CRV_OP_ONLY_LISTED, CRV_OP_LIST, CRV_OP_IS_FREE, CRV_OP_VAR,
   CRV_OP_VAR_DOMAIN, CRV_OP_VAR_RANDC, CRV_OP_ARRAY, CRV_OP_ARRAY_INIT,
   CRV_OP_REF, CRV_OP_FIND_ARRAY, CRV_OP_FIND_FREE, CRV_OP_FIND_FREE_ARRAY,
   CRV_OP_CONST, CRV_OP_BINARY, CRV_OP_UNARY, CRV_OP_CAST, CRV_OP_COND,
   CRV_OP_ARRAY_SIZE, CRV_OP_ARRAY_ELEM, CRV_OP_SET_NEW, CRV_OP_SET_VALUE,
   CRV_OP_SET_RANGE, CRV_OP_SET_ARRAY, CRV_OP_INSIDE, CRV_OP_DIST_NEW,
   CRV_OP_DIST_PER_VALUE, CRV_OP_DIST_PER_RANGE, CRV_OP_DIST, CRV_OP_REQUIRE,
   CRV_OP_UNIQUE_PUSH, CRV_OP_UNIQUE_ARRAY, CRV_OP_UNIQUE_END, CRV_OP_INDEX,
   CRV_OP_FOREACH, CRV_OP_SOLVE_BEFORE, CRV_OP_VALUE, CRV_OP_ARRAY_LEN,
   CRV_OP_ARRAY_VALUE, CRV_OP_TEST_BIT, CRV_OP_SET_BIT, CRV_OP_RANDOM,
   CRV_OP_DISABLE_SOFT, CRV_OP_SEED_BEGIN, CRV_OP_SEED_END, CRV_OP_ARRAY_SUM,
   CRV_OP_FAIL, CRV_OP_TRIAL, CRV_OP_RANDC_COMMIT, CRV_OP_DISABLE_SOFT_IF,
   CRV_OP_ARRAY_REDUCE, CRV_OP_VAR_WIDE, CRV_OP_REF_WIDE, CRV_OP_CONST_WIDE,
   CRV_OP_VALUE_WIDE,
} crv_op_t;

int64_t crv_generated(crv_op_t op, const int64_t *args);

// The generated code that is calling the solver, a jit_anchor_t: only used
// to find the source line of the randomize call for the statistics
void crv_set_caller(void *anchor);

#endif  // _RT_CRV_H
