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

#ifndef _JIT_FRAME_H
#define _JIT_FRAME_H

#include "prim.h"
#include "jit/jit.h"

typedef struct _jit_func jit_func_t;

// Variables and parameters of a subprogram frame, for debuggers.  Code
// generated with --debug-stmts passes the places of the variables of the
// subprogram to the statement probe exit when the probe is armed, which
// costs nothing otherwise.  The layout below says what those arguments are.

// Where the value of a variable is found
typedef enum {
   FRAME_VALUE,   // The argument is the value (integer or real parameter)
   FRAME_MEM,     // The argument is the address of the variable
   FRAME_SLOTS,   // The arguments are the words of the value (parameter)
   FRAME_PTR,     // The argument is a pointer to the data (parameter)
} frame_carrier_t;

// How the value is represented
typedef enum {
   FRAME_INT,
   FRAME_REAL,
   FRAME_VEC2,
   FRAME_VEC4,
   FRAME_UARRAY,   // Pointer to the data followed by the bounds
   FRAME_CARRAY,   // Array of constant size held in place
   FRAME_RECORD,
   FRAME_POINTER,  // Pointer to a value described by the declared type
   FRAME_OBJECT,   // Context pointer: a class handle
} frame_class_t;

typedef struct {
   ident_t name;
   uint8_t carrier;
   uint8_t class;
   uint8_t is_param;
   uint8_t is_signed;
   uint8_t first;      // First argument slot passed to the probe exit
   uint8_t nslots;
   uint8_t ndims;      // Dimensions of an unconstrained array
   uint8_t size;       // Bytes of an integer in memory
   uint16_t index;     // Number of the parameter or variable in the unit
   uint32_t bits;      // Bits of an integer, width of a vector
} frame_var_t;

typedef struct _frame_layout {
   unsigned    count;
   unsigned    nslots;
   frame_var_t vars[0];
} frame_layout_t;

// Whether name is a parameter or variable that the user declared in the
// subprogram, as opposed to a temporary of the code generator
bool frame_find_decl(object_t *subprogram, ident_t name, bool *is_param);

typedef enum {
   FRAME_K_NONE,     // No single value, only the text
   FRAME_K_LOGIC,    // Four-state or nine-valued bits, one char per bit
   FRAME_K_BIT,      // Two-state bits
   FRAME_K_INTEGER,
   FRAME_K_ENUM,
   FRAME_K_REAL,
   FRAME_K_TIME,
   FRAME_K_STRING,   // Eight bits per character
   FRAME_K_OBJECT,   // Context pointer in addr: whether it is an object is for the caller
} frame_vkind_t;

typedef struct {
   const char        *name;
   bool               is_param;
   frame_vkind_t       kind;
   bool               is_signed;
   int                width;       // Chars in bits
   const char        *type_name;
   const char        *bits;       // Most significant bit first, or NULL
   const char        *text;       // Always set
   unsigned           nliterals;
   const char *const *literals;
   bool               has_range;
   int64_t            low, high;
   const loc_t       *loc;
   uintptr_t          addr;        // FRAME_K_OBJECT
} frame_info_t;

typedef void (*frame_fn_t)(const frame_info_t *info, void *ctx);

// Describes the variables of the frame of f whose probe passed args (the
// arguments of the exit after the location)
void frame_describe(jit_func_t *f, const jit_scalar_t *args,
                    frame_fn_t fn, void *ctx);

#endif   // _JIT_FRAME_H
