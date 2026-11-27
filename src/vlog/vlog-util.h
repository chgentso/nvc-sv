//
//  Copyright (C) 2024-2026 Nick Gasson
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

#ifndef _VLOG_UTIL_H
#define _VLOG_UTIL_H

#include "prim.h"

bool vlog_is_net(vlog_node_t v);
unsigned vlog_dimensions(vlog_node_t v);
vlog_node_t vlog_gate_dimension(vlog_node_t gate);
bool vlog_get_const(vlog_node_t v, int64_t *value);
int64_t vlog_must_be_const(vlog_node_t v);
bool vlog_is_const(vlog_node_t v);
bool vlog_is_up(vlog_node_t v);
bool vlog_bounds(vlog_node_t v, int64_t *left, int64_t *right);
bool vlog_is_fixed_unpacked(vlog_node_t decl);
int vlog_open_dims(vlog_node_t port, vlog_node_t *elem);
bool vlog_has_open_packed(vlog_node_t type);
bool vlog_is_open_array(vlog_node_t port);
bool vlog_range_bound(vlog_node_t v, int64_t *value);
bool vlog_is_real(vlog_node_t v);
unsigned vlog_size(vlog_node_t v);
unsigned vlog_packed_offset(vlog_node_t type, vlog_node_t member);
unsigned vlog_tag_bits(vlog_node_t v);
unsigned vlog_width(vlog_node_t v);
bool vlog_is_scan_target(vlog_node_t call, int n);
bool vlog_is_signed(vlog_node_t v);
bool vlog_is_sformatf(ident_t id);
bool vlog_is_unsized(vlog_node_t v);
bool is_top_level(vlog_node_t v);
bool is_data_type(vlog_node_t v);
bool vlog_has_static_shape(vlog_node_t v);
bool vlog_is_two_state(vlog_node_t type);
bool vlog_is_four_state(vlog_node_t v);
vlog_node_t vlog_bind_default(vlog_node_t subr, vlog_node_t value,
                              vlog_node_t *actuals, int nbound);
tree_t vlog_hier_vhdl_port(tree_t b, ident_t id);
tree_t vlog_hier_target_block(tree_t b, ident_t id);
tree_t vlog_iface_port_target(tree_t b, vlog_node_t port, ident_t id);
vlog_node_t vlog_class_member(vlog_node_t class, ident_t id);
vlog_node_t vlog_class_prop_enum(vlog_node_t class, vlog_node_t prop);
vlog_node_t vlog_select_type(vlog_node_t v);
bool vlog_is_dynamic_type(vlog_node_t type);
bool vlog_nettype_is_flat(vlog_node_t type);
bool is_implicit_data_type(vlog_node_t v);
vlog_node_t vlog_longest_static_prefix(vlog_node_t v);
bool vlog_is_const_arith(vlog_node_t v);
bool vlog_equal_node(vlog_node_t a, vlog_node_t b);
uint32_t vlog_hash_node(vlog_node_t v);
vlog_node_t vlog_get_type(vlog_node_t v);
void vlog_push_type_override(vlog_node_t decl, vlog_node_t type);
void vlog_pop_type_override(void);
vlog_node_t vlog_select_type_packed(vlog_node_t v);
vlog_node_t vlog_assoc_key(vlog_node_t type);
vlog_node_t vlog_scope_member(vlog_node_t scope, ident_t id);
unsigned vlog_class_ifaces(vlog_node_t class);
vlog_node_t vlog_class_iface(vlog_node_t class, unsigned n);
bool vlog_class_derives(vlog_node_t c, vlog_node_t base);
bool vlog_needs_polling(vlog_node_t v);
vlog_node_t vlog_get_dim(vlog_node_t v, int n);
tree_t vlog_walk_mod_refs(vlog_node_t v, tree_t where);
void vlog_set_mod_ref_alias(ident_t id, tree_t block);
bool vlog_block_is_named(tree_t b, ident_t name);
ident_t vlog_mod_ref_name(vlog_node_t v);
vlog_node_t vlog_member_decl(vlog_node_t v);
vlog_node_t vlog_method_decl(vlog_node_t v);
vlog_node_t vlog_virtual_iface(vlog_node_t type);
vlog_node_t vlog_find_interface(ident_t name);
vlog_node_t vlog_virtual_decl(vlog_node_t v);
vlog_node_t vlog_virtual_decl_or_null(vlog_node_t v);
bool vlog_virtual_pending(vlog_node_t v);
bool vlog_virtual_chain(vlog_node_t v);
bool vlog_uses_typeparam(vlog_node_t type);
bool vlog_typeparam_pending(vlog_node_t v);
bool vlog_type_matches_restrict(unsigned r, vlog_node_t dt);
const char *vlog_type_restrict_str(unsigned r);
vlog_node_t vlog_virtual_joined_base(vlog_node_t v);
void vlog_set_design_precision(int log10);
int vlog_design_precision(void);
int64_t vlog_time_quantum(unsigned log10);
int64_t vlog_time_resolve(int64_t ival);
int64_t vlog_time_literal(int64_t fs);
int64_t vlog_round_time(int64_t fs, unsigned log10);

#define CANNOT_HANDLE(v) do {                                           \
      fatal_at(vlog_loc(v), "cannot handle %pK in %s" ,                 \
               v, __FUNCTION__);                                        \
   } while (0)

int64_t vlog_label_hash(ident_t name);

#endif  // _VLOG_UTIL_H
