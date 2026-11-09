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

// Called when a VHDL assertion or report statement, or a Verilog severity
// task or failing assertion, produces a message.  The callback runs on the
// simulation thread inside the process that raised the message, so it should
// only record the event.  file is NULL if the location is unknown.  Passing
// a NULL function removes the callback.
typedef void (*vpi_nvc_diag_fn_t)(int severity, const char *text,
                                  const char *file, int line, void *user_data);

void vpi_nvc_set_diag_cb(vpi_nvc_diag_fn_t fn, void *user_data);

#endif  // VPI_EXT_NVC_H
