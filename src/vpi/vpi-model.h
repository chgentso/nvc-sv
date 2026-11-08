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

#ifndef _VPI_MODEL_H
#define _VPI_MODEL_H

#include "prim.h"
#include "jit/jit.h"
#include "vpi/svdpi.h"

typedef unsigned int  PLI_UINT32;
typedef int           PLI_INT32;
typedef PLI_UINT32   *vpiHandle;

vpi_context_t *vpi_context_new(void);
void vpi_context_initialise(vpi_context_t *c, tree_t top, rt_model_t *model,
                            jit_t *jit, int argc, char **argv);
void vpi_context_free(vpi_context_t *c);

vpiHandle vpi_bind_foreign(ident_t name, vlog_node_t where);
void vpi_abort_call(void);
void vpi_call_foreign(vpiHandle handle, jit_scalar_t *args, tlab_t *tlab);
PLI_INT32 vpi_func_type(ident_t name);

// Size of the buffer for a string variable set by a system task
#define STR_TARGET_MAX 4096
void vpi_set_plusargs(int argc, char **argv);

int uvm_hdl_check_path(const char *path);
int uvm_hdl_read(const char *path, svLogicVecVal *value);
int uvm_hdl_deposit(const char *path, const svLogicVecVal *value);
int uvm_hdl_force(const char *path, const svLogicVecVal *value);
int uvm_hdl_release(const char *path);
int uvm_hdl_release_and_read(const char *path, svLogicVecVal *value);
const char *vpi_get_plusarg(int nth);

int64_t vpi_caller_timescale(void);
const char *vpi_caller_scope_name(void);
const char *vpi_caller_name(void);
int64_t vpi_caller_precision(void);
int64_t vpi_caller_design_precision(void);
bool vpi_instance_timescale(const char *path, text_buf_t *name,
                            int64_t *unit, int64_t *precision);
void vpi_return_string(const char *str, size_t len);
int vpi_string_length(vpiHandle handle);
bool vpi_is_class_handle(vpiHandle handle);


#endif  // _VPI_MODEL_H
