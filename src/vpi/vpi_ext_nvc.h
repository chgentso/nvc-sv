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

#ifndef VPI_EXT_NVC_H
#define VPI_EXT_NVC_H

// Object types outside the range used by the IEEE 1800 headers

// Instance of a VHDL entity in a Verilog design: it has a name and a
// scope but its contents are only visible through VHPI
#define vpiForeignInst 1001

// Severity levels reported to a diagnostic callback, as in VHDL
#define vpiNvcNote    0
#define vpiNvcWarning 1
#define vpiNvcError   2
#define vpiNvcFailure 3

// Callback reason: the end of every delta cycle, after the processes of the
// cycle have run.  This includes the first cycle of a time step.  Values
// read in the callback are those of the cycle, and unlike cbReadOnlySynch the
// callback may change signals, which then happens in the next delta cycle.
// Use cbReadWriteSynch for the last delta cycle of a time step.
#define cbNvcEndOfDelta 1001

// The number of the current delta cycle in the time step: 0 for the first
// cycle, which is the one where time has just advanced.
PLI_INT32 vpi_nvc_get_delta(void);

// Statement breakpoints.  The code of the design has to be generated with the
// --debug-stmts option of nvc (at elaboration or when running): it then calls
// the callback before a statement on a line that has a breakpoint runs.  The
// callback runs inside the process that executes the statement, so a tool can
// block in it and read signals and variables while the simulation is paused
// there; process is NULL outside a process.  A line breakpoint matches the
// statements whose first line is that line, in any process and any instance
// that executes it.  file may be a path that ends with the name of the file
// or the other way round.
typedef void (*vpi_nvc_stmt_fn_t)(const char *file, int line,
                                  const char *process, const char *scope,
                                  void *user_data);

void vpi_nvc_set_stmt_cb(vpi_nvc_stmt_fn_t fn, void *user_data);

// Adds a breakpoint on a line.  Returns 1 if the code of the design is
// instrumented (--debug-stmts), otherwise 0 and the breakpoint cannot fire.
PLI_INT32 vpi_nvc_stmt_breakpoint_add(const char *file, int line);

// While enabled, the callback is called before every statement, to step from
// one statement to the next.  Returns 1 if the code of the design is
// instrumented (--debug-stmts).
PLI_INT32 vpi_nvc_stmt_step(PLI_INT32 enable);

// Removes a breakpoint, returns 1 if there was one.
PLI_INT32 vpi_nvc_stmt_breakpoint_remove(const char *file, int line);

// Called when a VHDL assertion or report statement, or a Verilog severity
// task or failing assertion, produces a message.  The callback runs on the
// simulation thread inside the process that raised the message, so it should
// only record the event.  file is NULL if the location is unknown.  Passing
// a NULL function removes the callback.
typedef void (*vpi_nvc_diag_fn_t)(int severity, const char *text,
                                  const char *file, int line, void *user_data);

void vpi_nvc_set_diag_cb(vpi_nvc_diag_fn_t fn, void *user_data);

#endif  // VPI_EXT_NVC_H
