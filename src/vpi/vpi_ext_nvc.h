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

// While stepping from the statement callback, limit the stepping to the
// process that the statement belongs to: the callback is then called only for
// the statements that this process runs (and for breakpoints in any process).
// Call it from the callback after vpi_nvc_stmt_step(1); the limit ends with
// the next vpi_nvc_stmt_step.  Returns 0, and nothing is limited, if the
// callback is not in a process or stepping is off.  Stepping, with or
// without the limit, does not call the callback for statements that run
// outside a process, such as the functions that the simulator calls to
// evaluate the condition of a wait.
PLI_INT32 vpi_nvc_stmt_step_process(void);

// Like vpi_nvc_stmt_step_process, and in addition the stepping stops only at
// statements of the call depth that mode asks for, counted from the
// statement that the callback is for: vpiNvcStepIn is the next statement
// of the process, also in the subprograms that it calls, vpiNvcStepOver is
// the next statement at the same or a lower depth (calls are run without
// stopping, a task or procedure that waits is stepped over until it returns,
// other processes run meanwhile), vpiNvcStepOut is the next statement at a
// lower depth, that is in the caller once the current subprogram has
// returned.  Out from a statement of the process itself has no caller to
// return to and so stops as over does, at the next statement of the
// process, after its next activation if it has to.  Breakpoints still stop in any depth.  Call after
// vpi_nvc_stmt_step(1); returns 0, and does nothing, if the callback is not
// in a process, stepping is off or mode is not one of these.  The cost is
// only paid while the step is armed.
#define vpiNvcStepIn   0
#define vpiNvcStepOver 1
#define vpiNvcStepOut  2

PLI_INT32 vpi_nvc_stmt_step_depth(PLI_INT32 mode);

// Asks whether a breakpoint on file:line can fire: whether a statement of the
// code that is instrumented (--debug-stmts) begins on that line.  Returns -1
// if the code is not instrumented, otherwise a combination of
// vpiNvcSiteLine (there is a statement on the line) and vpiNvcSiteFile
// (there are statements in the file).  *nearest is the line itself if it has a
// statement, otherwise the first following line that has one, or 0 if there
// is none.  file is matched as for breakpoints.  A statement is known once
// its unit has been lowered, so the answer for a unit that the design has not
// needed yet is 0 and may change.
#define vpiNvcSiteLine 0x1
#define vpiNvcSiteFile 0x2

PLI_INT32 vpi_nvc_stmt_site_query(const char *file, PLI_INT32 line,
                                  PLI_INT32 *nearest);

// The call stack of the code that is running, innermost frame first, for the
// callbacks that run inside it (the statement callback, and the diagnostic
// callback).  The callback is called once for every frame: a process (or
// initial, always block) is the last, subprograms and methods called from it
// come before it.  kind is process, initial, always, final, function,
// procedure, task, constructor, package, block, assign, fork, class or code;
// name is the process name (as the statement callback has it) or the name of
// the subprogram, and symbol the unique name of the compiled unit.  file and
// line are the statement the frame is at: for the innermost frame of a
// statement callback that is the statement the callback is for, for the
// others the call they are waiting in; for a diagnostic callback the
// innermost line is the place of the diagnostic.  file is NULL if unknown.  Strings are valid
// during the call.  Returns the number of frames, or -1 if called outside
// such a callback.  Costs nothing unless called.
typedef struct {
   PLI_INT32   index;        // 0 for the innermost frame
   const char *kind;
   const char *name;
   const char *symbol;
   const char *file;
   PLI_INT32   line;
} vpi_nvc_frame_t;

typedef void (*vpi_nvc_frame_fn_t)(const vpi_nvc_frame_t *frame,
                                   void *user_data);

PLI_INT32 vpi_nvc_get_frames(vpi_nvc_frame_fn_t fn, void *user_data);

// The parameters and variables of a subprogram (VHDL function or procedure,
// Verilog function or task) that a frame of the call stack is in, from the
// statement callback.  Only the innermost frame (frame 0) has them, and only
// if the statement is in a subprogram: for a process, use its variables
// through VHPI or VPI.  The values are read-only copies of what the code
// holds at this statement.  The callback is called once for every variable
// that the user declared (parameters first), with strings that are valid
// during the call.  The values are formatted for display: text is always
// set; for a value that has a simple form (scalar, array of std_logic, bit
// or characters, Verilog vector, string) kind says which and bits is its
// value, one char per bit, most significant bit first (vpiNvcVarLogic: from
// 01zx for Verilog and uxz01wlh- for VHDL; the others 0 and 1; a string has
// eight bits for every character).  literals gives the names of the literals
// of an enumeration.  Returns the number of variables, 0 if the frame has
// none (including every frame but 0), or -1 if called outside a statement
// callback or if the code is not instrumented with --debug-stmts.  The cost
// is only paid by the code of an armed statement.
#define vpiNvcVarNone    0
#define vpiNvcVarLogic   1
#define vpiNvcVarBit     2
#define vpiNvcVarInteger 3
#define vpiNvcVarEnum    4
#define vpiNvcVarReal    5
#define vpiNvcVarTime    6   // Femtoseconds
#define vpiNvcVarString  7

typedef struct {
   const char        *name;
   PLI_INT32          is_param;     // A parameter (argument) of the subprogram
   PLI_INT32          kind;         // vpiNvcVar*
   PLI_INT32          is_signed;
   PLI_INT32          width;        // Number of chars in bits
   const char        *type_name;    // Or NULL
   const char        *bits;         // Or NULL
   const char        *text;
   PLI_INT32          nliterals;
   const char *const *literals;
   PLI_INT32          has_range;    // Integer subtype range: low and high
   PLI_INT64          low;
   PLI_INT64          high;
   const char        *file;         // Declaration, or NULL
   PLI_INT32          line;
} vpi_nvc_var_t;

typedef void (*vpi_nvc_var_fn_t)(const vpi_nvc_var_t *var, void *user_data);

PLI_INT32 vpi_nvc_frame_variables(PLI_INT32 frame, vpi_nvc_var_fn_t fn,
                                  void *user_data);

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
