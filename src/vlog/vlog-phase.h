//
//  Copyright (C) 2022-2026 Nick Gasson
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

#ifndef _VLOG_PHASE_H
#define _VLOG_PHASE_H

#include "prim.h"

void vlog_preprocess(text_buf_t *tb, bool precise);
void vlog_preprocess_reset(void);
vlog_node_t vlog_parse(void);
void vlog_set_default_timescale(uint64_t unit_fs, unsigned precision_log10);
void vlog_check(vlog_node_t v);
void vlog_dump(vlog_node_t v, int indent);
void vlog_simp(vlog_node_t mod);
vlog_node_t vlog_simp_format(vlog_node_t v);
void vlog_trans(vlog_node_t mod, tree_t out);
void vlog_lower_udp(mir_unit_t *mu, object_t *obj);
void vlog_lower_block(mir_context_t *mc, ident_t parent, tree_t b);
void vlog_set_interconnect_nettype(vlog_node_t decl, vlog_node_t nettype);
void vlog_lower_instance(mir_context_t *mc, vlog_node_t body, ident_t parent,
                         tree_t trans);
void vlog_lower_const_funcs(mir_context_t *mc, vlog_node_t inst);
mir_unit_t *vlog_lower_thunk(mir_context_t *mc, ident_t parent, vlog_node_t v);
vlog_node_t vlog_new_instance(vlog_node_t mod, vlog_node_t inst, ident_t id);
void vlog_set_instance_scope(ident_t dotted);
bool vlog_bind_is_selective(vlog_node_t mod);
vlog_node_t vlog_generate_instance(vlog_node_t v, vlog_node_t genvar,
                                   int32_t value, ident_t prefix);
void vlog_fold(vlog_node_t mod, mir_context_t *mc, jit_t *j);
void vlog_lint(vlog_node_t unit);
void vlog_lint_partial(vlog_node_t unit);
void vlog_lint_design(vlog_node_t mod);
void vlog_lint_port(vlog_node_t port, vlog_node_t value, const loc_t *loc);
void vlog_set_fold_context(mir_context_t *mc, jit_t *j);
vlog_node_t vlog_virtual_spec(vlog_node_t type);
bool vlog_virtual_unresolved(vlog_node_t type);
typedef vlog_node_t (*vlog_port_body_fn_t)(vlog_node_t port, void *ctx);
bool vlog_resolve_iface_params(vlog_node_t body, vlog_port_body_fn_t fn,
                               void *ctx);
ident_t vlog_instance_key(vlog_node_t mod, vlog_node_t body);
void vlog_load_binds(lib_t lib, object_t **tops, int ntops);
bool vlog_bind_has_hier_ref(vlog_node_t mod);
void vlog_resolve_method(vlog_node_t call);
void vlog_set_dpi_stubs(const char *path);

// The classes of the design, for debuggers: the class that an object with
// this $vid belongs to, the name of the unit that is the context of its
// objects, the name of the variable in that context that holds a property,
// the same for a static property in the context of the namespace unit given
// by vlog_class_home, and the name of the class as $typename shows it
vlog_node_t vlog_class_by_id(uint64_t vid);
bool vlog_class_known(vlog_node_t c);
vlog_node_t vlog_class_any(void);
ident_t vlog_class_unit(vlog_node_t c);
ident_t vlog_class_slot(vlog_node_t owner, vlog_node_t decl);
ident_t vlog_class_static(vlog_node_t c, ident_t name);
ident_t vlog_class_home(vlog_node_t c);
bool vlog_class_typename(vlog_node_t c, text_buf_t *tb, bool qualified);

#endif  // _VLOG_PHASE_H
