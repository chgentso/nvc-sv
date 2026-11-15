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

#ifndef _FSM_H
#define _FSM_H

#include "prim.h"

// State machine detection for the scan mode (--scan --fsm).  The walkers
// take a unit tree and write one JSON object per machine through this
// emitter; scandeps.c collects the objects into the "fsms" key of a file.

typedef struct _fsm_out fsm_out_t;

// Walkers: append one JSON object per machine found in UNIT to OUT and
// increment COUNT.  Written to be called on any tree the scan produces,
// including those of files with errors.
void vhdl_fsm_scan(tree_t unit, text_buf_t *out, unsigned *count);
void vlog_fsm_scan(vlog_node_t unit, text_buf_t *out, unsigned *count);
void vlog_fsm_reset_skipped(void);
void vhdl_fsm_reset_skipped(void);
unsigned vhdl_fsm_skipped(void);
unsigned vlog_fsm_skipped(void);

// Begin a machine.  KIND is "enum" or "constants", STYLE "one-process" or
// "two-process".  The object is appended to TB when fsm_end is called and
// COUNT is incremented.
fsm_out_t *fsm_begin(text_buf_t *tb, unsigned *count, const char *unit,
                     const char *container, const char *kind,
                     const char *style, bool typed);

// Does the VHDL expression test for a clock edge: rising_edge, falling_edge
// or clk'event and clk = '1'.  CLK is the reference to the signal.
bool fsm_vhdl_edge(tree_t e, tree_t *clk, bool *rising);

// Names are matched without regard to case when finding a position in the
// source: call with true for VHDL (the default is case sensitive)
void fsm_set_vhdl(fsm_out_t *f, bool vhdl);

// KEY is "state" or "next".  The position is that of NAME within LOC.
void fsm_object(fsm_out_t *f, const char *key, const char *name,
                const loc_t *loc, const char *type);
void fsm_clock(fsm_out_t *f, const char *name, bool rising);
void fsm_process(fsm_out_t *f, const char *role, const char *label,
                 const loc_t *loc);
void fsm_case(fsm_out_t *f, const loc_t *loc);

// DECL, VALUE and BRANCH may be NULL
void fsm_state(fsm_out_t *f, const char *name, const loc_t *decl,
               const char *value, const loc_t *branch);

// One transition: KIND is "case", "default", "any" or "reset".  Exactly one
// of TO and EXPR is non-NULL (EXPR for a computed target).  Follow with any
// number of fsm_trans_from and fsm_trans_guard calls.  Columns are 1-based,
// end_col is one past the last column of the assignment.
void fsm_trans_begin(fsm_out_t *f, const char *kind, const loc_t *assign,
                     const char *to, const char *expr);
void fsm_trans_from(fsm_out_t *f, const char *state);   // "*", "*others" or a name
void fsm_trans_guard(fsm_out_t *f, const char *text, const loc_t *loc,
                     bool neg);
void fsm_trans_end(fsm_out_t *f);

void fsm_note(fsm_out_t *f, const char *fmt, ...)
   __attribute__((format(printf, 2, 3)));

// Finish the machine and append it to the output; INITIAL may be NULL
void fsm_end(fsm_out_t *f, const char *initial);

// Drop a machine that turned out not to be one
void fsm_discard(fsm_out_t *f);

// Source text of a location with runs of white space squeezed to one space
// and comments left as they are.  The caller frees the result.  Returns
// NULL when the source is not available.
char *fsm_source_text(const loc_t *loc);

// As above for VHDL: comments are removed
char *fsm_source_text_vhdl(const loc_t *loc);

#endif   // _FSM_H
