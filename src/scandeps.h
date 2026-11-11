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

#ifndef _SCANDEPS_H
#define _SCANDEPS_H

#include "prim.h"

// Scan a source file for the design units it provides and the units it
// requires, appending one line of JSON to the buffer.  No library other
// than the standard ones is needed and nothing is written to any library.
// Returns the number of syntax errors found.
unsigned scan_file_deps(const char *file, text_buf_t *json);

// Also report the declarations and references of each file (--symbols)
void scan_set_symbols(bool enable);

#endif   // _SCANDEPS_H
