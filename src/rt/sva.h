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

#ifndef _RT_SVA_H
#define _RT_SVA_H

#include "prim.h"

// Checker for SystemVerilog concurrent assertions.  The generated code
// builds a tree of sequence and property operators once and then passes
// the sampled values of the boolean leaves at each clock tick.  Every
// tick starts an evaluation attempt which stays pending until the
// property is decided by the ticks seen so far.

typedef struct _sva sva_t;

typedef enum {
   SVA_ASSERT,
   SVA_COVER_PROPERTY,
   SVA_COVER_SEQUENCE,
   SVA_EXPECT,
} sva_kind_t;

// Bits of the flags argument of sva_new
#define SVA_F_ASSUME   1       // The directive is assume
#define SVA_F_PASS     2       // The assertion has a pass statement

// Control types of $assertcontrol (20.12, Table 20-5)
typedef enum {
   SVA_CTRL_LOCK = 1,
   SVA_CTRL_UNLOCK,
   SVA_CTRL_ON,
   SVA_CTRL_OFF,
   SVA_CTRL_KILL,
   SVA_CTRL_PASS_ON,
   SVA_CTRL_PASS_OFF,
   SVA_CTRL_FAIL_ON,
   SVA_CTRL_FAIL_OFF,
   SVA_CTRL_NONVACUOUS_ON,
   SVA_CTRL_VACUOUS_OFF,
} sva_ctrl_t;

// Assertion types (Table 20-6) and directive types (Table 20-7)
#define SVA_ATYPE_CONCURRENT 1
#define SVA_ATYPE_EXPECT     16
#define SVA_DTYPE_ASSERT     1
#define SVA_DTYPE_COVER      2
#define SVA_DTYPE_ASSUME     4

typedef enum {
   SVA_RESULT_PASS_ACTIONS,   // Pass statements to run at the last tick
   SVA_RESULT_FAIL_ACTIONS,   // Fail statements to run at the last tick
   SVA_RESULT_SUCCESSES,      // Successes, vacuous or not, at the last tick
} sva_result_t;

#define SVA_LEAF       1000
#define SVA_DLEAF      1001    // Leaf expression which reads local variables
#define SVA_MAX_LEAVES 64
#define SVA_MAX_LOCALS 16
#define SVA_MAX_SLOTS  64
#define SVA_MAX_FCALLS 32      // Function calls in expressions with locals
#define SVA_MAX_FARGS  6
#define SVA_WORDS      16      // Largest width of a local variable is 1024

// Operators of the expressions which read local variables
typedef enum {
   SVA_X_CONST, SVA_X_SLOT, SVA_X_LOCAL, SVA_X_COND,
   SVA_X_ADD, SVA_X_SUB, SVA_X_MUL, SVA_X_DIV, SVA_X_MOD, SVA_X_POW,
   SVA_X_AND, SVA_X_OR, SVA_X_XOR, SVA_X_XNOR, SVA_X_NAND, SVA_X_NOR,
   SVA_X_SHL, SVA_X_SHR, SVA_X_ASHR,
   SVA_X_EQ, SVA_X_NE, SVA_X_CEQ, SVA_X_CNE,
   SVA_X_LT, SVA_X_GT, SVA_X_LE, SVA_X_GE, SVA_X_LAND, SVA_X_LOR,
   SVA_X_NEG, SVA_X_BITNEG, SVA_X_LNOT,
   SVA_X_BITSEL, SVA_X_PARTSEL,
   SVA_X_RAND, SVA_X_ROR, SVA_X_RXOR, SVA_X_RNAND, SVA_X_RNOR, SVA_X_RXNOR,
   SVA_X_CAST, SVA_X_CALL,
} sva_xop_t;

#define SVA_XF_SIGNED  1
#define SVA_XF_UNKNOWN 2

typedef enum {
   SVA_ITEM_ASSIGN,
   SVA_ITEM_CALL,
} sva_item_t;

typedef bool (*sva_call_fn_t)(void *jit, int handle, void *ctx,
                              const int64_t *args, int nargs, int64_t *result);

sva_t *sva_new(sva_kind_t kind, unsigned flags);
void sva_set_callfn(sva_t *s, int idx, int handle, void *ctx);
void sva_set_caller(sva_call_fn_t fn, void *jit);
void sva_config(sva_t *s, int nlocals, int nslots, unsigned unknown);
int sva_expr(sva_t *s, int op, int a, int b, int c, int width, int flags,
             int64_t value);
int sva_item(sva_t *s, int kind, int local, int expr, int width, int flags);
void sva_slot(sva_t *s, int slot, int64_t value, bool unknown);
void sva_slotw(sva_t *s, int slot, int width, const uint64_t *bits,
               bool unknown);
void sva_localw(sva_t *s, int local, int width);
int sva_node(sva_t *s, int op, int a, int b, int leaf, int64_t lo, int64_t hi);
void sva_set_root(sva_t *s, int node);
bool sva_wanted(sva_t *s, int leaf);
bool sva_slot_wanted(sva_t *s, int slot, uint64_t leaves);
unsigned sva_tick(sva_t *s, uint64_t leaves, int disable);
unsigned sva_result(const sva_t *s, sva_result_t which);
unsigned sva_nevents(const sva_t *s);
int sva_event_call(const sva_t *s, unsigned k);
int64_t sva_event_local(const sva_t *s, unsigned k, int local);
bool sva_idle(const sva_t *s);
unsigned sva_finish(sva_t *s);
void sva_free(sva_t *s);
void sva_set_label(sva_t *s, const char *label);
void sva_control(int ctype, unsigned atype, unsigned dtype, int levels,
                 const char *label);

#endif  // _RT_SVA_H
