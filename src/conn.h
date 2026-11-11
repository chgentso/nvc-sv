//
//  Copyright (C) 2025  Nick Gasson
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

#ifndef _CONN_H
#define _CONN_H

#include "prim.h"

//
// Static connectivity of an elaborated design.
//
// This is a read-only description of the block structure of a design
// built from the elaborated tree (T_ELAB): the ports and signals of each
// block, the child instances with the actual connected to each port, and
// the processes with the bits they read and drive.  VHDL and Verilog /
// SystemVerilog blocks are described uniformly.  It does not need a
// running model so it can be used offline (--netlist) and from a
// simulation (the elaborated tree is the root scope's `where`, see
// root_scope() in rt/model.h, or the tree passed to
// vhpi_context_initialise).
//
// Terminology
//   block      One elaborated instance.  Generate blocks (VHDL for/if/case
//              generate, Verilog generate) are inlined into the enclosing
//              instance: their signals, processes and instances appear in
//              the enclosing block with the generate label as a name
//              prefix, e.g. "G(0).TMP".
//   net        A port or signal of a block.  Every scalar element of the
//              declared type is one bit.
//   position   Bits of a net are numbered 0..width-1 in declaration order,
//              left to right, i.e. position 0 is the left bound of a
//              range.  This is independent of the direction of the range.
//              A conn_range_t names positions lo..hi inclusive.
//
// The API is small: conn_build / conn_free to build and release the
// structure, the plain structures below to walk it, and the query
// functions conn_find_net, conn_drivers and conn_readers.
//

typedef struct _conn_design conn_design_t;
typedef struct _conn_block  conn_block_t;
typedef struct _conn_net    conn_net_t;
typedef struct _conn_inst   conn_inst_t;
typedef struct _conn_proc   conn_proc_t;

typedef enum {
   CONN_VHDL,
   CONN_VERILOG,
} conn_lang_t;

typedef enum {
   CONN_LOCAL,     // Internal signal
   CONN_IN,
   CONN_OUT,
   CONN_INOUT,
} conn_dir_t;

typedef struct {
   conn_net_t *net;
   unsigned    lo, hi;       // Bit positions, inclusive
} conn_range_t;

typedef struct {
   const char *name;       // Appended to the net name: ".FIELD", "(1).FIELD"
   unsigned    lo, hi;     // Bit positions in the net
} conn_field_t;

struct _conn_net {
   const char   *name;      // Name in block, with generate prefix
   conn_block_t *block;
   conn_dir_t    dir;
   unsigned      width;     // Number of bits
   bool          descending;// Declared range goes high downto low
   bool          record;    // Declared type contains a record
   conn_field_t *fields;    // Scalar and array fields of a record or packed
   unsigned      nfields;   // structure, for display only
   bool          iface;     // Stands for a member of an interface instance
   conn_block_t *ifblock;   // ... that instance
   const char   *member;    // ... and the name of the member in it
   const void   *decl;      // Declaration (internal)
   const void   *decl2;     // Port declaration (internal)
   int64_t       left, right;  // Declared bounds of a one-dimensional net
   bool          has_bounds;
   const char   *src;       // File name or NULL
   unsigned      line;
};

typedef enum {
   CONN_SEG_NET,      // Bits aliased to a range of a net
   CONN_SEG_CONST,    // Constant bits
   CONN_SEG_EXPR,     // Value computed by an expression
   CONN_SEG_OPEN,     // Unconnected
} conn_seg_kind_t;

typedef struct {
   conn_seg_kind_t kind;
   unsigned        width;
   conn_range_t    range;    // CONN_SEG_NET
   const char     *bits;     // CONN_SEG_CONST: one char of 01xz per bit
   const char     *text;     // CONN_SEG_EXPR: expression source text
   conn_range_t   *reads;    // CONN_SEG_EXPR: nets the expression reads
   unsigned        nreads;
   const char     *conv;     // Name of conversion function or NULL
   bool            approx;   // Whole net used for a non-static select
} conn_seg_t;

typedef struct {
   conn_net_t *formal;       // Port of the child block
   unsigned    lo, hi;       // Positions of the formal this connects
   conn_seg_t *segs;         // Actual, in position order of the formal
   unsigned    nsegs;
   const char *src;
   unsigned    line;
} conn_pconn_t;

struct _conn_inst {
   conn_block_t *parent;     // Block containing this instance
   conn_block_t *child;
   const char   *label;      // Instance name with generate prefix
   const char   *src;
   unsigned      line;
   conn_pconn_t *conns;
   unsigned      nconns;
};

typedef enum {
   CONN_PROC_VHDL,       // VHDL process (including concurrent statements)
   CONN_PROC_ALWAYS,
   CONN_PROC_ALWAYS_FF,
   CONN_PROC_ALWAYS_COMB,
   CONN_PROC_ALWAYS_LATCH,
   CONN_PROC_INITIAL,
   CONN_PROC_FINAL,
   CONN_PROC_ASSIGN,     // Verilog continuous assignment
   CONN_PROC_GATE,       // Verilog gate or UDP instance
   CONN_PROC_OTHER,
} conn_proc_kind_t;

struct _conn_proc {
   conn_block_t    *block;
   const char      *name;     // Name in block, with generate prefix
   conn_proc_kind_t kind;
   const char      *src;
   unsigned         line;
   conn_range_t    *reads;
   unsigned         nreads;
   conn_range_t    *drives;
   unsigned         ndrives;
   bool             approx;   // Sets may be over- or under-approximated
};

struct _conn_block {
   const char   *name;       // Hierarchical path, e.g. "TOP.U_SUB"
   const char   *label;      // Last component of path
   const char   *unit;       // Design unit, e.g. "WORK.SUB" or NULL
   conn_lang_t   lang;
   bool          interface; // SystemVerilog interface instance: all of its
                            // signals are ports and are also nets of the
                            // parent named LABEL.SIGNAL
   conn_block_t *parent;
   conn_inst_t  *inst;       // Instance in parent, NULL for a root
   const char   *src;
   unsigned      line;
   conn_net_t  **nets;       // Ports first, then local signals
   unsigned      nnets;
   unsigned      nports;
   conn_inst_t **insts;
   unsigned      ninsts;
   conn_proc_t **procs;
   unsigned      nprocs;
};

// Build the connectivity of the design rooted at `root` which is the
// T_ELAB tree or one of its top-level T_BLOCKs.  Returns NULL if the tree
// is not an elaborated design.
conn_design_t *conn_build(tree_t root);
void conn_free(conn_design_t *cd);

unsigned conn_roots(const conn_design_t *cd);
conn_block_t *conn_root(const conn_design_t *cd, unsigned n);

// Visit every block of the design, parents before children.
typedef void (*conn_block_fn_t)(conn_block_t *b, void *ctx);
void conn_walk(const conn_design_t *cd, conn_block_fn_t fn, void *ctx);

// Find a block or net by hierarchical path.  Components may be separated
// by '.' or ':' and a leading separator is ignored, so both
// "top.u_sub.q" and ":top:u_sub:q" work.  Names are matched case
// insensitively.  The first component names a root block.
conn_block_t *conn_find_block(const conn_design_t *cd, const char *path);
conn_net_t *conn_find_net(const conn_design_t *cd, const char *path);

// Full hierarchical path of a net, e.g. "TOP.U_SUB.Q".  The result is
// owned by the design.
const char *conn_net_path(const conn_design_t *cd, const conn_net_t *net);

//
// Static drivers and readers of a range of bits of a net
//
// Each call reports every place that drives (or reads) any bit of
// positions lo..hi of `net` through the callback.  Following a net
// through its ports is up to the caller: an end with kind
// CONN_END_PORT_UP or CONN_END_PORT_DOWN gives `other` to continue the
// search from.
//

typedef enum {
   CONN_END_PROC,       // `proc` drives or reads the net
   CONN_END_CHILD,      // Port of a child instance connected to the net
   CONN_END_PARENT,     // Actual connected to a port of the block holding
                        // the net, in the parent block
   CONN_END_EXPR,       // Child port connected via an expression that
                        // reads the net
} conn_end_kind_t;

typedef struct {
   conn_end_kind_t kind;
   conn_block_t   *block;    // Block containing the process / instance
   const conn_proc_t *proc;  // CONN_END_PROC
   const conn_inst_t *inst;  // CONN_END_CHILD, CONN_END_EXPR, CONN_END_PARENT
   const conn_net_t  *port;  // Port of the child (CHILD, EXPR) or of the
                             // block (PARENT)
   const char     *src;      // Where, may be NULL
   unsigned        line;
   unsigned        lo, hi;   // Positions of the queried net covered
   conn_range_t    other;    // CHILD: bits of `port` ; PARENT: bits of
                             // the actual in the parent (net may be NULL
                             // if it is not a plain net)
   bool            approx;
} conn_end_t;

typedef void (*conn_end_fn_t)(const conn_end_t *end, void *ctx);

// Returns the number of ends reported.
unsigned conn_drivers(const conn_design_t *cd, const conn_net_t *net,
                      unsigned lo, unsigned hi, conn_end_fn_t fn, void *ctx);
unsigned conn_readers(const conn_design_t *cd, const conn_net_t *net,
                      unsigned lo, unsigned hi, conn_end_fn_t fn, void *ctx);

// Printable description of an end, e.g. "process P (top.vhd:12)".
// The result is owned by the design and valid until conn_free.
const char *conn_end_describe(const conn_design_t *cd, const conn_end_t *end);

#endif   // _CONN_H
