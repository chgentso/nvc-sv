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

#ifndef _VLOG_SYMTAB_H
#define _VLOG_SYMTAB_H

#include "prim.h"
#include "vlog/vlog-node.h"

vlog_symtab_t *vlog_symtab_new(void);
void vlog_symtab_free(vlog_symtab_t *st);

void vlog_symtab_push(vlog_symtab_t *st, vlog_node_t v);
void vlog_symtab_pop(vlog_symtab_t *st);
void vlog_symtab_push_members(vlog_symtab_t *st, vlog_node_t class,
                              const ident_t *only, int nonly);
void vlog_symtab_resolve_deferred(vlog_symtab_t *st);
bool vlog_symtab_set_forward(vlog_symtab_t *st, bool forward);
void vlog_symtab_set_implicit(vlog_symtab_t *st, vlog_net_kind_t kind);

void vlog_symtab_lookup(vlog_symtab_t *st, vlog_node_t v);
void vlog_symtab_lookup_in(vlog_symtab_t *st, vlog_node_t v,
                           vlog_node_t scope);
void vlog_symtab_put(vlog_symtab_t *st, vlog_node_t v);
void vlog_symtab_poison(vlog_symtab_t *st, ident_t name);
void vlog_symtab_import(vlog_symtab_t *st, vlog_node_t v);

void vlog_symtab_suppress(vlog_symtab_t *st);

vlog_node_t vlog_symtab_query(vlog_symtab_t *st, ident_t name);

vlog_node_t vlog_symtab_subr(vlog_symtab_t *st);
vlog_node_t vlog_symtab_class(vlog_symtab_t *st);
vlog_node_t vlog_symtab_static_home(vlog_symtab_t *st);
vlog_node_t vlog_symtab_design_unit(vlog_symtab_t *st);
typedef struct _vlog_scope vlog_scope_t;
vlog_scope_t *vlog_symtab_current(vlog_symtab_t *st);
vlog_scope_t *vlog_symtab_enter(vlog_symtab_t *st, vlog_scope_t *scope);
ident_t vlog_symtab_qualified(vlog_symtab_t *st);
ident_t vlog_symtab_display_scope(vlog_symtab_t *st);
typedef enum { HIER_INSTANCE, HIER_PACKAGE, HIER_UNIT } hier_owner_t;

ident_t vlog_symtab_hier_suffix(vlog_symtab_t *st, hier_owner_t *owner,
                                vlog_node_t *pkg);

#endif  // _VLOG_SYMTAB
