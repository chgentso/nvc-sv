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
// scope but its contents are only visible through VHPI.  The same type is
// used for the VHDL instances which contain Verilog instances in a design
// with a VHDL top level: those instances are its children.
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

// Properties and flags of a process object (vpiInitial, vpiAlways or
// vpiFinal): vpi_get(vpiNvcProcState, process) is a combination of these
// flags, or 0 if the process is suspended in an event control or wait.
#define vpiNvcProcState    5001
#define vpiNvcProcRunning  0x01   // The process is executing
#define vpiNvcProcRunnable 0x02   // It will run in the current delta cycle
#define vpiNvcProcTimeout  0x04   // It has a time out: vpi_nvc_get_wake_time
#define vpiNvcProcEnded    0x08   // It has finished

// Time at which a process waiting for a delay resumes, in the form given by
// time_p->type.  Returns 1 if it has a time out, otherwise 0.
PLI_INT32 vpi_nvc_get_wake_time(vpiHandle process, p_vpi_time time_p);

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

// Drivers and loads of a signal, found from the model of the simulation (so
// for VHDL and for Verilog alike).  path names the signal by the names of its
// scopes and its own name separated by spaces (top u_sub q; VHDL names are
// matched without regard to case).  The range is counted in the scalar
// elements of the signal from its first element (the bits of a vector, the
// leftmost one of the declaration first); a count of 0 means up to the end,
// and a count that is too large is cut.  The ranges in the connections are
// relative to the start of the range asked for.  The callback is called once for every
// driver and then once for every load with
// the information below; strings are valid only during the call.  Only the
// drivers of the range itself are reported, a driver that is a port
// connection names the signal on the other side, which can be queried in its
// turn.  Must be called with the simulation paused, from its thread.
//
// Returns -1 if there is no such signal, otherwise a combination of the
// vpiNvcConnLoadsMissing flag (the static description of the design could not
// give the loads of this signal, so none were reported) and 0.  The loads are
// the processes that read the range and the ports of instances below, or the
// actual in the instance above, that it is connected to, from the static
// description of the design (conn.h) rather than from the model, so a
// process is a load whether or not it is waiting at the moment.  A load that
// is a port has vpiNvcConnFromChild if the other signal is below.

#define vpiNvcConnProcess   0   // A process drives the signal
#define vpiNvcConnPort      1   // Its value comes from another signal
#define vpiNvcConnForce     2   // It is forced
#define vpiNvcConnDeposit   3   // A deposit on it is pending

// Flags of a connection
#define vpiNvcConnCollapsed 0x1  // The port was merged with the signal it is
                                 // connected to, this hop is not in the model
#define vpiNvcConnFromChild 0x2  // The other signal is in an instance below
                                 // that of the signal (an output port)

#define vpiNvcConnApprox    0x4  // Found from the code of a Verilog process
                                 // that assigns to the variable, which has
                                 // no driver in the model: the range is the
                                 // whole range that was asked for

#define vpiNvcConnLoadsMissing 0x1

typedef struct {
   PLI_INT32   kind;         // vpiNvcConn*
   PLI_INT32   is_load;      // 0 for a driver, 1 for a load
   const char *name;         // Process name, for a process
   const char *proc_kind;    // process, always_ff, assign, initial, ... or NULL
   const char *scope;        // Scope of the process (spaces as in path); for a
                             // port the scope of the port connection
   const char *file;         // Where it is, or NULL
   PLI_INT32   line;
   PLI_INT32   first;        // The range it concerns, relative to the start of
   PLI_INT32   count;        // the range that was asked for
   PLI_INT32   total;        // The length of the range that was asked for
   const char *other;        // For a port: path of the signal on the other side
   PLI_INT32   other_first;  // and the element where the range starts there
   PLI_INT32   other_width;  // and the number of elements of that signal
   PLI_INT32   flags;        // vpiNvcConn* flags
} vpi_nvc_conn_t;

typedef void (*vpi_nvc_conn_fn_t)(const vpi_nvc_conn_t *conn, void *user_data);

PLI_INT32 vpi_nvc_connections(const char *path, PLI_INT32 first,
                              PLI_INT32 count, vpi_nvc_conn_fn_t fn,
                              void *user_data);

// The signals a process reads and drives, for debuggers.  scope_path is the
// scope of the process as in vpi_nvc_connections and name its name as
// reported there.  The callback is called for every range of a signal that
// the process drives (is_write) or is waiting for, which is its sensitivity
// while it is suspended; a process that is runnable or running reports only
// what it drives and the waits that it began since.  Returns -1 if there is
// no such process.
typedef struct {
   const char *path;         // As in vpi_nvc_connections
   PLI_INT32   first;        // The range of elements of the signal
   PLI_INT32   count;
   PLI_INT32   width;        // The number of elements of the signal
   PLI_INT32   is_write;
} vpi_nvc_procsig_t;

typedef void (*vpi_nvc_procsig_fn_t)(const vpi_nvc_procsig_t *sig,
                                     void *user_data);

PLI_INT32 vpi_nvc_process_signals(const char *scope_path, const char *name,
                                  vpi_nvc_procsig_fn_t fn, void *user_data);

#endif  // VPI_EXT_NVC_H
