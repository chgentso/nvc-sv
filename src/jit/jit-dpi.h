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

#ifndef _JIT_DPI_H
#define _JIT_DPI_H

#include "prim.h"
#include "jit/jit.h"

void dpi_add_lib(const char *name);
void dpi_load_libs(void);
int64_t dpi_call(const char *sig, int64_t siglen, void *context,
                 void *result, void **argv);
int64_t dpi_resume(int64_t handle, void **argv);
void dpi_proc_killed(rt_proc_t *proc);
void dpi_export(jit_t *j, void *context, const char *sig, int64_t siglen,
                const char *unit, int64_t unitlen);
int64_t dpi_oa_make(const jit_scalar_t *args);
int64_t dpi_string_length(int64_t index);
void dpi_string_copy(int64_t index, char *dst);

#endif   // _JIT_DPI_H
