//
//  Copyright (C) 2025-2026  Nick Gasson
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

#include "util.h"
#include "array.h"
#include "diag.h"
#include "hash.h"
#include "ident.h"
#include "printf.h"
#include "vlog/vlog-node.h"
#include "vlog/vlog-number.h"
#include "vlog/vlog-symtab.h"
#include "vlog/vlog-util.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define POISON_NODE (vlog_node_t)-1

typedef A(vlog_node_t) node_list_t;

typedef struct _vlog_symtab {
   vlog_scope_t    *top;
   hash_t          *scopes;
   mem_pool_t      *pool;
   vlog_net_kind_t  implicit;
   bool             forward;
   ghash_t         *builtins;
} vlog_symtab_t;

typedef struct {
   ident_t     name;
   vlog_node_t node;
   bool        imported;   // Made visible by a wildcard import
} vlog_symbol_t;

typedef struct _vlog_scope {
   vlog_scope_t  *parent;
   vlog_node_t    container;
   size_t         max_symbols;
   size_t         num_symbols;
   vlog_symbol_t *symbols;
   node_list_t    deferred;
   bool           suppress;
} vlog_scope_t;

typedef struct {
   ident_t       name;
   vlog_kind_t   kind;
   vlog_method_t method;
} vlog_builtin_t;

vlog_symtab_t *vlog_symtab_new(void)
{
   vlog_symtab_t *st = xcalloc(sizeof(vlog_symtab_t));
   st->pool     = pool_new();
   st->scopes   = hash_new(16);
   st->implicit = V_NET_NONE;

   return st;
}

void vlog_symtab_free(vlog_symtab_t *st)
{
   assert(st->top == NULL);
   pool_free(st->pool);
   hash_free(st->scopes);
   ghash_free(st->builtins);
   free(st);
}

static vlog_symbol_t *fresh_symbol_for(vlog_symtab_t *st, ident_t name)
{
   vlog_scope_t *s = st->top;
   assert(s != NULL);
   assert(is_power_of_2(s->max_symbols));

   if (unlikely(s->num_symbols + 1 > s->max_symbols / 2)) {
      const int old_max = s->max_symbols;
      s->max_symbols = MAX(s->max_symbols * 2, 8);

      const vlog_symbol_t *old_symbols = s->symbols;
      size_t size = s->max_symbols * sizeof(vlog_symbol_t);
      s->symbols = pool_calloc(st->pool, size);

      for (int i = 0; i < old_max; i++) {
         if (old_symbols[i].name != NULL) {
            int slot = ident_hash(old_symbols[i].name) & (s->max_symbols - 1);
            for (; ; slot = (slot + 1) & (s->max_symbols - 1)) {
               if (s->symbols[slot].name == NULL) {
                  s->symbols[slot] = old_symbols[i];
                  break;
               }
               else
                  assert(s->symbols[slot].name != old_symbols[i].name);
            }
         }
      }
   }

   for (int slot = ident_hash(name) & (s->max_symbols - 1);;
        slot = (slot + 1) & (s->max_symbols - 1)) {
      if (s->symbols[slot].name == name)
         return &(s->symbols[slot]);
      else if (s->symbols[slot].name == NULL) {
         s->symbols[slot].name = name;
         s->num_symbols++;
         return &(s->symbols[slot]);
      }
   }
}

static const vlog_symbol_t *symbol_for(vlog_symtab_t *st, ident_t name)
{
   for (vlog_scope_t *s = st->top; s != NULL; s = s->parent) {
      assert(is_power_of_2(s->max_symbols));

      if (s->num_symbols > 0) {
         for (int slot = ident_hash(name) & (s->max_symbols - 1);
              s->symbols[slot].name != NULL;
              slot = (slot + 1) & (s->max_symbols - 1)) {
            if (s->symbols[slot].name == name)
               return &(s->symbols[slot]);
         }
      }
   }

   return NULL;
}

void vlog_symtab_push(vlog_symtab_t *st, vlog_node_t v)
{
   vlog_scope_t *s = pool_calloc(st->pool, sizeof(vlog_scope_t));
   s->parent    = st->top;
   s->container = v;

   st->top = s;
}

static bool is_enclosing_module(const vlog_scope_t *scope, vlog_node_t v)
{
   // The name of the module can be used in the arguments of a system task

   if (scope->container == NULL || vlog_kind(scope->container) != V_MODULE)
      return false;

   return vlog_ident2(scope->container) == vlog_ident(v);
}

static bool is_subroutine(vlog_node_t container)
{
   if (container == NULL)
      return false;

   switch (vlog_kind(container)) {
   case V_FUNC_DECL:
   case V_TASK_DECL:
      return true;
   default:
      return false;
   }
}

void vlog_symtab_pop(vlog_symtab_t *st)
{
   assert(st->top != NULL);

   unsigned wptr = 0;
   for (int i = 0; i < st->top->deferred.count; i++) {
      vlog_node_t v = st->top->deferred.items[i];
      vlog_node_t d = vlog_symtab_query(st, vlog_ident(v));
      if (d != NULL)
         vlog_set_ref(v, d);
      else
         st->top->deferred.items[wptr++] = v;
   }
   ATRIM(st->top->deferred, wptr);

   for (int i = 0; i < st->top->deferred.count; i++) {
      vlog_node_t v = st->top->deferred.items[i];
      if (st->top->parent != NULL
          && (st->top->parent->container != NULL
              || is_subroutine(st->top->container)))
         APUSH(st->top->parent->deferred, v);
      else if (vlog_kind(v) != V_MOD_REF) {
         ident_t name = vlog_ident(v);
         error_at(vlog_loc(v), "no visible declaration for %pQ", name);
         vlog_symtab_poison(st, name);
      }
   }
   ACLEAR(st->top->deferred);

   st->top = st->top->parent;
}

bool vlog_symtab_set_forward(vlog_symtab_t *st, bool forward)
{
   const bool old = st->forward;
   st->forward = forward;
   return old;
}

void vlog_symtab_set_implicit(vlog_symtab_t *st, vlog_net_kind_t kind)
{
   st->implicit = kind;
}

static bool modport_has_member(vlog_node_t mp, ident_t id)
{
   const int nmembers = vlog_decls(mp);
   for (int i = 0; i < nmembers; i++) {
      if (vlog_ident(vlog_decl(mp, i)) == id)
         return true;
   }

   return false;
}

static vlog_node_t nested_interface_decl(vlog_node_t iface, ident_t id)
{
   // An interface instance declared inside an interface is a member of a
   // virtual interface whose type is the nested interface

   const int nstmts = vlog_stmts(iface);
   for (int i = 0; i < nstmts; i++) {
      vlog_node_t list = vlog_stmt(iface, i);
      if (vlog_kind(list) != V_INST_LIST)
         continue;

      const int ninst = vlog_stmts(list);
      for (int j = 0; j < ninst; j++) {
         vlog_node_t inst = vlog_stmt(list, j);
         if (vlog_kind(inst) != V_MOD_INST || vlog_ident(inst) != id)
            continue;

         vlog_node_t type = vlog_new(V_DATA_TYPE);
         vlog_set_subkind(type, DT_VIRTUAL);
         vlog_set_ident(type, vlog_ident(list));
         vlog_set_loc(type, vlog_loc(inst));

         vlog_node_t mod = vlog_virtual_iface(type);
         if (mod == NULL || vlog_kind(mod) != V_MODULE
             || !(vlog_flags(mod) & VLOG_F_INTERFACE))
            return NULL;

         vlog_set_ref(type, mod);

         const int nparams = vlog_params(list);
         for (int k = 0; k < nparams; k++)
            vlog_add_param(type, vlog_param(list, k));

         vlog_node_t decl = vlog_new(V_VAR_DECL);
         vlog_set_ident(decl, id);
         vlog_set_type(decl, type);
         vlog_set_flags(decl, VLOG_F_NESTED_INST);
         vlog_set_loc(decl, vlog_loc(inst));
         return vlog_ranges(inst) > 0 ? NULL : decl;
      }
   }

   return NULL;
}

static void lookup_member(vlog_symtab_t *st, vlog_node_t v)
{
   vlog_node_t prefix = vlog_value(v), type = vlog_get_type(prefix);
   if (type == NULL) {
      if (error_count() == 0)  // Otherwise was earlier error
         error_at(vlog_loc(v), "sorry, the prefix of %pQ is not supported in "
                  "this kind of hierarchical reference", vlog_ident(v));
      return;
   }

   const vlog_kind_t kind = vlog_kind(type);
   ident_t id = vlog_ident(v);

   if (kind == V_DATA_TYPE && vlog_subkind(type) == DT_VIRTUAL) {
      if (!vlog_has_ref(type))
         return;   // Interface is declared later

      vlog_node_t iface = vlog_ref(type);
      const int ndecls = vlog_decls(iface);
      for (int i = 0; i < ndecls; i++) {
         vlog_node_t d = vlog_decl(iface, i);
         switch (vlog_kind(d)) {
         case V_PORT_DECL:
         case V_NET_DECL:
         case V_VAR_DECL:
         case V_PARAM_DECL:
         case V_LOCALPARAM:
            if (vlog_ident(d) == id) {
               if (vlog_has_type(type) && !modport_has_member(
                      vlog_type(type), id)) {
                  error_at(vlog_loc(v), "virtual interface element %pQ "
                           "not found in modport %pQ", id,
                           vlog_ident(vlog_type(type)));
                  return;
               }

               vlog_set_ref(v, d);
               return;
            }
            break;
         default:
            break;
         }
      }

      vlog_node_t nested = nested_interface_decl(iface, id);
      if (nested != NULL) {
         vlog_set_ref(v, nested);
         return;
      }

      error_at(vlog_loc(v), "interface has no member named %pQ", id);
      return;
   }
   else if (kind != V_STRUCT_DECL && kind != V_UNION_DECL
            && kind != V_CLASS_DECL) {
      diag_t *d = diag_new(DIAG_ERROR, vlog_loc(prefix));
      if (vlog_kind(prefix) == V_REF)
         diag_printf(d, "%pQ", vlog_ident(prefix));
      else
         diag_printf(d, "prefix");
      diag_printf(d, " is not a struct, union, or class");
      diag_emit(d);
      return;
   }

   if (kind == V_CLASS_DECL) {
      vlog_node_t m = vlog_class_member(type, id);
      if (m == NULL) {
         // Parameters and enumeration names are constants of the class
         m = vlog_scope_member(type, id);
         if (m != NULL && vlog_kind(m) != V_PARAM_DECL
             && vlog_kind(m) != V_LOCALPARAM && vlog_kind(m) != V_ENUM_NAME)
            m = NULL;
      }

      if (m != NULL) {
         vlog_set_ref(v, m);
         return;
      }
   }
   else {
      const int ndecls = vlog_decls(type);
      for (int i = 0; i < ndecls; i++) {
         vlog_node_t d = vlog_decl(type, i);
         if (vlog_ident(d) == id) {
            vlog_set_ref(v, d);
            return;
         }
      }
   }

   diag_t *d = diag_new(DIAG_ERROR, vlog_loc(v));
   if (kind == V_STRUCT_DECL || kind == V_UNION_DECL)
      diag_printf(d, "%s ", kind == V_STRUCT_DECL ? "struct" : "union");
   else
      diag_printf(d, "class %pQ ", vlog_ident(type));
   diag_printf(d, "has no field named %pQ", id);
   diag_hint(d, vlog_loc(type), "%s declared here",
             kind == V_STRUCT_DECL ? "struct" :
             kind == V_UNION_DECL ? "union" : "class");
   diag_emit(d);
}

static uint32_t hash_builtin_cb(const void *key)
{
   const vlog_builtin_t *b = key;
   return ident_hash(b->name) ^ knuth_hash(b->kind);
}

static bool cmp_builtin_cb(const void *a, const void *b)
{
   const vlog_builtin_t *ba = a, *bb = b;
   return ba->name == bb->name && ba->kind == bb->kind;
}

static vlog_method_t get_builtin_method(vlog_symtab_t *st, vlog_kind_t kind,
                                        ident_t name)
{
   static const struct {
      const char     name[8];
      vlog_kind_t    kind;
      vlog_method_t  method;
   } tab[] = {
      { "first", V_ENUM_DECL, V_METHOD_ENUM_FIRST },
      { "last",  V_ENUM_DECL, V_METHOD_ENUM_LAST },
      { "next",  V_ENUM_DECL, V_METHOD_ENUM_NEXT },
      { "prev",  V_ENUM_DECL, V_METHOD_ENUM_PREV },
      { "num",   V_ENUM_DECL, V_METHOD_ENUM_NUM },
      { "name",  V_ENUM_DECL, V_METHOD_ENUM_NAME },
   };

   if (st == NULL) {
      for (int i = 0; i < ARRAY_LEN(tab); i++) {
         if (tab[i].kind == kind && ident_new(tab[i].name) == name)
            return tab[i].method;
      }

      return V_METHOD_USER;
   }

   if (st->builtins == NULL) {
      st->builtins = ghash_new(ARRAY_LEN(tab) * 2 + 1, hash_builtin_cb,
                               cmp_builtin_cb);

      vlog_builtin_t *bs = pool_malloc_array(st->pool, ARRAY_LEN(tab),
                                             sizeof(vlog_builtin_t));
      for (int i = 0; i < ARRAY_LEN(tab); i++) {
         bs[i].name   = ident_new(tab[i].name);
         bs[i].kind   = tab[i].kind;
         bs[i].method = tab[i].method;

         ghash_put(st->builtins, &bs[i], &bs[i]);
      }
   }

   const vlog_builtin_t query = { name, kind };

   vlog_builtin_t *b = ghash_get(st->builtins, &query);
   if (b == NULL)
      return V_METHOD_USER;

   return b->method;
}

static vlog_node_t make_atom_type(data_type_t dt, int width, vlog_flags_t flags,
                                  const loc_t *loc)
{
   vlog_node_t left = vlog_new(V_NUMBER);
   vlog_set_number(left, number_from_int(width - 1));

   vlog_node_t right = vlog_new(V_NUMBER);
   vlog_set_number(right, number_from_int(0));

   vlog_node_t r = vlog_new(V_DIMENSION);
   vlog_set_subkind(r, V_DIM_PACKED);
   vlog_set_left(r, left);
   vlog_set_right(r, right);

   vlog_node_t t = vlog_new(V_DATA_TYPE);
   vlog_set_subkind(t, dt);
   vlog_set_flags(t, flags);
   vlog_add_range(t, r);
   vlog_set_loc(t, loc);
   return t;
}

static vlog_node_t make_queue_type(vlog_node_t elem, const loc_t *loc)
{
   vlog_node_t t = vlog_new(V_DATA_TYPE);
   vlog_set_subkind(t, DT_QUEUE);
   vlog_set_type(t, elem);
   vlog_set_loc(t, loc);
   return t;
}

static void lookup_method_call(vlog_symtab_t *st, vlog_node_t v)
{
   vlog_node_t prefix = vlog_value(v), type = vlog_get_type(prefix);

   if (lookup_rand_method(v, prefix))
      return;
   else if (type == NULL) {  // Was earlier error
      assert(error_count() > 0);
      return;
   }

   const vlog_kind_t kind = vlog_kind(type);
   if (kind == V_CLASS_DECL) {
      error_at(vlog_loc(v), "sorry, class method calls are not yet supported");
      return;
   }

   vlog_method_t m = get_builtin_method(st, kind, vlog_ident(v));
   vlog_set_subkind(v, m);

   // The reference of a built-in method holds the type of its result
   switch (m) {
   case V_METHOD_STR_TOUPPER:
   case V_METHOD_STR_TOLOWER:
   case V_METHOD_STR_SUBSTR:
      vlog_set_ref(v, type);
      break;
   case V_METHOD_STR_ATOREAL:
      {
         vlog_node_t real = vlog_new(V_DATA_TYPE);
         vlog_set_subkind(real, DT_REAL);
         vlog_set_loc(real, vlog_loc(v));
         vlog_set_ref(v, real);
      }
      break;
   case V_METHOD_STR_GETC:
      {
         vlog_set_ref(v, make_atom_type(DT_BYTE, 8, VLOG_F_SIGNED,
                                        vlog_loc(v)));
      }
      break;
   case V_METHOD_EVENT_TRIGGERED:
      vlog_set_ref(v, make_atom_type(DT_INT, 32, VLOG_F_SIGNED, vlog_loc(v)));
      break;
   case V_METHOD_ENUM_NAME:
      {
         vlog_node_t str = vlog_new(V_DATA_TYPE);
         vlog_set_subkind(str, DT_STRING);
         vlog_set_loc(str, vlog_loc(v));
         vlog_set_ref(v, str);
      }
      break;
   default:
      break;
   }

   if (m == V_METHOD_USER) {
      diag_t *d = diag_new(DIAG_ERROR, vlog_loc(prefix));
      if (vlog_kind(prefix) == V_REF)
         diag_printf(d, "%pQ", vlog_ident(prefix));
      else
         diag_printf(d, "prefix");
      diag_printf(d, " has no method named %pQ", vlog_ident(v));
      diag_emit(d);
   }
}

void vlog_resolve_method(vlog_node_t v)
{
   // The prefix of a method call on a hierarchical name is only known
   // after elaboration

   assert(vlog_kind(v) == V_METHOD_CALL);
   lookup_method_call(NULL, v);
}

void vlog_symtab_lookup_in(vlog_symtab_t *st, vlog_node_t v,
                           vlog_node_t scope)
{
   // A name qualified with a package or class

   if (vlog_has_ref(v))
      return;

   assert(!loc_invalid_p(vlog_loc(v)));

   ident_t name = vlog_ident(v);
   vlog_node_t d = vlog_scope_member(scope, name);
   if (d == NULL) {
      error_at(vlog_loc(v), "%pQ is not a member of %s %pQ", name,
               vlog_kind(scope) == V_PACKAGE ? "package" : "class",
               vlog_ident(scope));
   }
   else
      vlog_set_ref(v, d);
}

void vlog_symtab_lookup(vlog_symtab_t *st, vlog_node_t v)
{
   if (vlog_has_ref(v))
      return;

   assert(!loc_invalid_p(vlog_loc(v)));

   switch (vlog_kind(v)) {
   case V_MEMBER_REF:
      lookup_member(st, v);
      return;
   case V_METHOD_CALL:
      lookup_method_call(st, v);
      return;
   default:
      break;
   }

   ident_t name = vlog_ident(v);
   const vlog_symbol_t *sym = symbol_for(st, name);
   if (sym == NULL && vlog_kind(v) == V_REF && st->implicit != V_NET_NONE) {
      // See 1800-2017 section 6.10 "Implicit declarations"
      vlog_node_t decl = vlog_new(V_NET_DECL);
      vlog_set_ident(decl, name);
      vlog_set_loc(decl, vlog_loc(v));
      vlog_set_subkind(decl, V_NET_WIRE);

      vlog_node_t dt = vlog_new(V_DATA_TYPE);
      vlog_set_subkind(dt, DT_LOGIC);

      vlog_set_type(decl, dt);

      vlog_set_ref(v, decl);

      vlog_add_decl(st->top->container, decl);

      fresh_symbol_for(st, name)->node = decl;
   }
   else if (sym == NULL) {
      switch (vlog_kind(v)) {
      case V_USER_FCALL:
      case V_USER_TCALL:
      case V_MOD_REF:
         // May be declared later
         APUSH(st->top->deferred, v);
         break;
      case V_REF:
         if (st->forward) {
            APUSH(st->top->deferred, v);
            break;
         }
         // Fall-through
      default:
         error_at(vlog_loc(v), "no visible declaration for %pQ", name);
         vlog_symtab_poison(st, name);
      }
   }
   else if (sym->node != POISON_NODE)
      vlog_set_ref(v, sym->node);
}

void vlog_symtab_put(vlog_symtab_t *st, vlog_node_t v)
{
   ident_t name = vlog_ident(v);
   if (name == NULL) {
      assert(error_count() > 0);   // Parser is recovering from an error
      return;
   }

   vlog_symbol_t *sym = fresh_symbol_for(st, name);

   assert(!loc_invalid_p(vlog_loc(v)));

   if (sym->node == POISON_NODE || sym->node == v)
      return;

   if (sym->imported) {
      // A declaration takes precedence over a name from a wildcard import
      sym->imported = false;
      sym->node = v;
      return;
   }

   if (sym->node != NULL) {
      switch (vlog_kind(sym->node)) {
      case V_VAR_DECL:
      case V_NET_DECL:
      case V_LOCAL_DECL:
         switch (vlog_kind(v)) {
         case V_PORT_DECL:
         case V_TF_PORT_DECL:
            if (!vlog_has_ref(v)) {
               vlog_set_ref(v, sym->node);
               sym->node = v;
               return;
            }
            break;
         default:
            break;
         }
         break;
      case V_PORT_DECL:
      case V_TF_PORT_DECL:
         if (!vlog_has_ref(sym->node)) {
            switch (vlog_kind(v)) {
            case V_VAR_DECL:
            case V_NET_DECL:
            case V_LOCAL_DECL:
               vlog_set_ref(sym->node, v);
               return;
            default:
               break;
            }
         }
      default:
         break;
      }

      diag_t *d = diag_new(DIAG_ERROR, vlog_loc(v));
      diag_printf(d, "duplicate declaration of %pQ", name);
      diag_hint(d, vlog_loc(sym->node), "%pQ was previously declared here",
                name);
      diag_hint(d, vlog_loc(v), "duplicate declaration");
      diag_suppress(d, name == well_known(W_ERROR) || st->top->suppress);
      diag_emit(d);
   }

   sym->node = v;
}

void vlog_symtab_poison(vlog_symtab_t *st, ident_t name)
{
   vlog_symbol_t *sym = fresh_symbol_for(st, name);
   sym->node = POISON_NODE;
}

vlog_node_t vlog_symtab_query(vlog_symtab_t *st, ident_t name)
{
   const vlog_symbol_t *sym = symbol_for(st, name);
   if (sym == NULL || sym->node == POISON_NODE)
      return NULL;

   return sym->node;
}

void vlog_symtab_suppress(vlog_symtab_t *st)
{
   st->top->suppress = true;
}

vlog_scope_t *vlog_symtab_current(vlog_symtab_t *st)
{
   return st->top;
}

vlog_scope_t *vlog_symtab_enter(vlog_symtab_t *st, vlog_scope_t *scope)
{
   vlog_scope_t *old = st->top;
   st->top = scope;
   return old;
}

vlog_node_t vlog_symtab_static_home(vlog_symtab_t *st)
{
   for (vlog_scope_t *s = st->top; s != NULL; s = s->parent) {
      if (s->container != NULL) {
         switch (vlog_kind(s->container)) {
         case V_MODULE:
         case V_PACKAGE:
         case V_PROGRAM:
         case V_CLASS_DECL:
            return s->container;
         default:
            break;
         }
      }
   }

   return NULL;
}

vlog_node_t vlog_symtab_design_unit(vlog_symtab_t *st)
{
   for (vlog_scope_t *s = st->top; s != NULL; s = s->parent) {
      if (s->container != NULL) {
         switch (vlog_kind(s->container)) {
         case V_MODULE:
         case V_PACKAGE:
         case V_PROGRAM:
            return s->container;
         default:
            break;
         }
      }
   }

   return NULL;
}

vlog_node_t vlog_symtab_class(vlog_symtab_t *st)
{
   for (vlog_scope_t *s = st->top; s != NULL; s = s->parent) {
      if (s->container != NULL && vlog_kind(s->container) == V_CLASS_DECL)
         return s->container;
   }

   return NULL;
}

vlog_node_t vlog_symtab_subr(vlog_symtab_t *st)
{
   for (vlog_scope_t *s = st->top; s != NULL; s = s->parent) {
      if (s->container != NULL) {
         switch (vlog_kind(s->container)) {
         case V_FUNC_DECL:
         case V_TASK_DECL:
         case V_CONSTRUCTOR:
            return s->container;
         default:
            break;
         }
      }
   }

   return NULL;
}

ident_t vlog_symtab_qualified(vlog_symtab_t *st)
{
   ident_t qual = NULL;
   for (vlog_scope_t *s = st->top; s != NULL; s = s->parent) {
      if (s->container != NULL)
         qual = ident_prefix(vlog_ident(s->container), qual, '.');
   }

   return qual;
}

ident_t vlog_symtab_hier_suffix(vlog_symtab_t *st, hier_owner_t *owner,
                                vlog_node_t *pkg)
{
   // Names of the subprograms, classes, and blocks within the enclosing
   // module instance, package, or compilation unit

   ident_t suffix = NULL;
   *owner = HIER_UNIT;
   *pkg = NULL;

   for (vlog_scope_t *s = st->top; s != NULL; s = s->parent) {
      vlog_node_t c = s->container;
      if (c == NULL)
         continue;

      switch (vlog_kind(c)) {
      case V_MODULE:
      case V_PROGRAM:
         *owner = HIER_INSTANCE;
         return suffix;
      case V_PACKAGE:
         *owner = HIER_PACKAGE;
         *pkg = c;
         return suffix;
      case V_FUNC_DECL:
      case V_TASK_DECL:
      case V_SEQ_BLOCK:
      case V_FORK:
      case V_CLASS_DECL:
         if (vlog_has_ident(c))
            suffix = ident_prefix(vlog_ident(c), suffix, '.');
         break;
      default:
         break;
      }
   }

   return suffix;
}

static void put_imported(vlog_symtab_t *st, vlog_node_t d)
{
   // Names with the same name from different wildcard imports are only an
   // error if one is used which is not checked

   vlog_symbol_t *sym = fresh_symbol_for(st, vlog_ident(d));
   if (sym->node != NULL)
      return;

   sym->node = d;
   sym->imported = true;
}

void vlog_symtab_import(vlog_symtab_t *st, vlog_node_t v)
{
   assert(vlog_kind(v) == V_IMPORT_DECL);

   if (!vlog_has_ref(v))
      return;

   ident_t match = NULL;
   if (vlog_has_ident(v))
      match = vlog_ident(v);

   import_package(st, vlog_ref(v), match);
}
