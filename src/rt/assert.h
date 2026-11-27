//
//  Copyright (C) 2024  Nick Gasson
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

#ifndef _RT_ASSERT_H
#define _RT_ASSERT_H

#include "prim.h"
#include "diag.h"

typedef enum {
   SEVERITY_NOTE = 0,
   SEVERITY_WARNING = 1,
   SEVERITY_ERROR = 2,
   SEVERITY_FAILURE = 3
} vhdl_severity_t;

vhdl_severity_t set_exit_severity(vhdl_severity_t severity);
void set_status_severity(vhdl_severity_t severity);

int64_t get_vhdl_assert_count(vhdl_severity_t severity);
void clear_vhdl_assert(void);
void set_vhdl_assert_enable(vhdl_severity_t severity, bool enable);
bool get_vhdl_assert_enable(vhdl_severity_t severity);
int get_vhdl_assert_exit_status(void);

diag_level_t get_diag_severity(vhdl_severity_t severity);
void emit_vhdl_diag(diag_t *d, vhdl_severity_t severity);

typedef void (*assert_hook_t)(vhdl_severity_t severity, const char *text,
                              const loc_t *loc, void *context);

// Call a function whenever an assertion, report statement or Verilog
// severity task produces a message
void set_assert_hook(assert_hook_t fn, void *context);

// Tell the hook about a message that is emitted without emit_vhdl_diag
void notify_assert_hook(diag_t *d, vhdl_severity_t severity);

// Deferred SystemVerilog unique/unique0/priority violation reports
// (IEEE 1800-2017 12.4.2.1 & 12.5.3.1)
extern unsigned sv_unique_pending;
void sv_unique_flush_proc(void *proc);

#endif   // _RT_ASSERT_H
