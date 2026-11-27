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
#include "common.h"
#include "array.h"
#include "diag.h"
#include "hash.h"
#include "ident.h"
#include "parse.h"
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
   bool             forward_net;
   ghash_t         *builtins;
   bool             lenient;        // A unit of this file was skipped
   bool             syntax_error;   // Seen in this file in recovery mode
   hset_t          *reported;      // Names missing since that happened
} vlog_symtab_t;

typedef struct {
   ident_t     name;
   vlog_node_t node;
   bool        imported;   // Made visible by a wildcard import
   ident_t     via;        // Package whose own import made it visible
   ident_t     from;       // Package it was imported from
} vlog_symbol_t;

typedef struct _vlog_scope {
   vlog_scope_t  *parent;
   vlog_node_t    container;
   size_t         max_symbols;
   size_t         num_symbols;
   vlog_symbol_t *symbols;
   node_list_t    deferred;
   node_list_t    net_deferred;   // May be declared implicitly later
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
   if (st->reported != NULL)
      hset_free(st->reported);
   free(st);
}

static void report_missing(vlog_symtab_t *st, vlog_node_t v, ident_t name)
{
   // A name may be declared in a unit that was skipped
   if (st->lenient && parse_recovery())
      return;

   // After a syntax error the declaration may be one that was skipped: say
   // so once for each name
   if (st->syntax_error && parse_recovery()) {
      if (st->reported == NULL)
         st->reported = hset_new(16);

      if (hset_contains(st->reported, name))
         return;

      hset_insert(st->reported, name);
   }

   error_at(vlog_loc(v), "no visible declaration for %pQ", name);
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

static const vlog_symbol_t *symbol_for_scope(vlog_symtab_t *st, ident_t name,
                                             vlog_scope_t **found_scope)
{
   for (vlog_scope_t *s = st->top; s != NULL; s = s->parent) {
      assert(is_power_of_2(s->max_symbols));

      if (s->num_symbols > 0) {
         for (int slot = ident_hash(name) & (s->max_symbols - 1);
              s->symbols[slot].name != NULL;
              slot = (slot + 1) & (s->max_symbols - 1)) {
            if (s->symbols[slot].name == name) {
               // The name of a generate block is only kept to detect
               // duplicates: it is not a declaration that can be referenced
               const vlog_node_t n = s->symbols[slot].node;
               if (n != NULL && n != POISON_NODE
                   && vlog_kind(n) == V_GEN_BLOCK)
                  break;
               if (found_scope != NULL)
                  *found_scope = s;
               return &(s->symbols[slot]);
            }
         }
      }
   }

   if (found_scope != NULL)
      *found_scope = NULL;
   return NULL;
}

static const vlog_symbol_t *symbol_for(vlog_symtab_t *st, ident_t name)
{
   return symbol_for_scope(st, name, NULL);
}

void vlog_symtab_push(vlog_symtab_t *st, vlog_node_t v)
{
   vlog_scope_t *s = pool_calloc(st->pool, sizeof(vlog_scope_t));
   s->parent    = st->top;
   s->container = v;

   st->top = s;
}

void vlog_symtab_push_members(vlog_symtab_t *st, vlog_node_t class,
                              const ident_t *only, int nonly)
{
   // Names declared in the class or inherited from a base class hide
   // those in the enclosing scopes: used for inline constraints.  If
   // only is not NULL then just those names are visible.

   vlog_symtab_push(st, class);

   for (vlog_node_t c = class; ; c = vlog_ref(c)) {
      const int ndecls = vlog_decls(c);
      for (int i = 0; i < ndecls; i++) {
         vlog_node_t d = vlog_decl(c, i);
         if (vlog_kind(d) == V_TYPE_DECL && only == NULL
             && vlog_ident(d) != ident_new("$implements")
             && vlog_kind(vlog_type(d)) == V_ENUM_DECL) {
            // The names of an enumeration declared in the class
            vlog_node_t et = vlog_type(d);
            const int nnames = vlog_decls(et);
            for (int j = 0; j < nnames; j++) {
               vlog_node_t n = vlog_decl(et, j);
               vlog_symbol_t *sym = fresh_symbol_for(st, vlog_ident(n));
               if (sym->node == NULL)
                  sym->node = n;
            }
            continue;
         }

         if (vlog_kind(d) == V_FUNC_DECL && only == NULL
             && !(vlog_flags(d) & VLOG_F_STATIC)) {
            // The methods of the object are found before those of the caller
            vlog_symbol_t *sym = fresh_symbol_for(st, vlog_ident(d));
            if (sym->node == NULL)
               sym->node = d;
            continue;
         }

         if (vlog_kind(d) != V_VAR_DECL)
            continue;

         vlog_node_t et = vlog_class_prop_enum(c, d);
         if (et != NULL && only == NULL) {
            // The names of an enumeration given in the type of a property
            const int nnames = vlog_decls(et);
            for (int j = 0; j < nnames; j++) {
               vlog_symbol_t *sym = fresh_symbol_for(st, vlog_ident(vlog_decl(et, j)));
               if (sym->node == NULL)
                  sym->node = vlog_decl(et, j);
            }
         }

         if (only != NULL) {
            int j = 0;
            for (; j < nonly && only[j] != vlog_ident(d); j++)
               ;
            if (j == nonly)
               continue;
         }

         vlog_symbol_t *sym = fresh_symbol_for(st, vlog_ident(d));
         if (sym->node == NULL)
            sym->node = d;
      }

      if (!vlog_has_ref(c))
         break;
   }
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

static void check_forward_ref(vlog_node_t v, vlog_node_t d)
{
   // As an extension, an argument of a system task or function may refer
   // to a variable, net or parameter declared later, which the LRM only
   // allows for hierarchical names (23.6)

   if (vlog_kind(v) != V_REF)
      return;

   switch (vlog_kind(d)) {
   case V_VAR_DECL:
   case V_NET_DECL:
   case V_PORT_DECL:
   case V_TF_PORT_DECL:
   case V_LOCAL_DECL:
   case V_PARAM_DECL:
   case V_LOCALPARAM:
   case V_GENVAR_DECL:
      break;
   default:
      return;
   }

   diag_t *dg = diag_new(DIAG_WARN, vlog_loc(v));
   diag_printf(dg, "%pQ is used before its declaration", vlog_ident(v));
   diag_hint(dg, vlog_loc(v), "used here");
   diag_hint(dg, vlog_loc(d), "declared here");
   diag_emit(dg);
}

static bool is_non_agg_data_type(vlog_node_t d)
{
   if (d == NULL || !is_data_type(d))
      return false;

   vlog_node_t t = vlog_get_type(d);
   if (t == NULL)
      return true;

   const vlog_kind_t k = vlog_kind(t);
   return k != V_STRUCT_DECL && k != V_UNION_DECL && k != V_CLASS_DECL;
}

void vlog_symtab_resolve_deferred(vlog_symtab_t *st)
{
   // Names used in subroutines of the compilation unit scope may be
   // declared after the subroutine

   for (int i = 0; i < st->top->deferred.count; i++) {
      vlog_node_t v = st->top->deferred.items[i];
      vlog_node_t d = vlog_symtab_query(st, vlog_ident(v));
      if (d != NULL && !(vlog_kind(v) == V_MOD_REF
                         && is_non_agg_data_type(d))) {
         check_forward_ref(v, d);
         vlog_set_ref(v, d);
      }
      else if (vlog_kind(v) != V_MOD_REF) {
         ident_t name = vlog_ident(v);
         report_missing(st, v, name);
         vlog_symtab_poison(st, name);
      }
   }

   ACLEAR(st->top->deferred);
}

bool vlog_symtab_top_is(vlog_symtab_t *st, vlog_kind_t kind)
{
   assert(st->top != NULL);
   return st->top->container != NULL && vlog_kind(st->top->container) == kind;
}

static bool is_implicit_net(vlog_node_t d)
{
   return vlog_kind(d) == V_NET_DECL && vlog_has_type(d)
      && vlog_kind(vlog_type(d)) == V_DATA_TYPE
      && (vlog_flags(vlog_type(d)) & VLOG_F_IMPLICIT_NET);
}

void vlog_symtab_pop(vlog_symtab_t *st)
{
   assert(st->top != NULL);

   // A net declared implicitly by a later port connection
   for (int i = 0; i < st->top->net_deferred.count; i++) {
      vlog_node_t v = st->top->net_deferred.items[i];
      vlog_node_t d = vlog_symtab_query(st, vlog_ident(v));
      if (d != NULL && is_implicit_net(d))
         vlog_set_ref(v, d);
      else if (st->top->parent != NULL
               && (st->top->parent->container != NULL
                   || is_subroutine(st->top->container)))
         APUSH(st->top->parent->net_deferred, v);
      else {
         ident_t name = vlog_ident(v);
         report_missing(st, v, name);
         vlog_symtab_poison(st, name);
      }
   }
   ACLEAR(st->top->net_deferred);

   const bool leaving_to_root =
      st->top->parent == NULL || st->top->parent->parent == NULL;

   unsigned wptr = 0;
   for (int i = 0; i < st->top->deferred.count; i++) {
      vlog_node_t v = st->top->deferred.items[i];
      vlog_scope_t *found = NULL;
      const vlog_symbol_t *sym = symbol_for_scope(st, vlog_ident(v), &found);
      vlog_node_t d = (sym != NULL && sym->node != POISON_NODE)
         ? sym->node : NULL;
      if (d != NULL
          && !(vlog_kind(v) == V_MOD_REF && is_non_agg_data_type(d))
          && (leaving_to_root || found == NULL || found->parent != NULL
              || (vlog_kind(v) != V_USER_TCALL
                  && vlog_kind(v) != V_USER_FCALL))) {
         check_forward_ref(v, d);
         vlog_set_ref(v, d);
      }
      else if (vlog_kind(v) == V_REF && is_enclosing_module(st->top, v))
         vlog_set_ref(v, st->top->container);
      else
         st->top->deferred.items[wptr++] = v;
   }
   ATRIM(st->top->deferred, wptr);

   for (int i = 0; i < st->top->deferred.count; i++) {
      vlog_node_t v = st->top->deferred.items[i];
      // The scope of a generate loop is between its blocks and the module
      if (st->top->parent != NULL
          && (st->top->parent->container != NULL
              || is_subroutine(st->top->container)
              || (st->top->parent->parent != NULL
                  && st->top->parent->parent->container != NULL)))
         APUSH(st->top->parent->deferred, v);
      else if (vlog_kind(v) != V_MOD_REF) {
         ident_t name = vlog_ident(v);
         report_missing(st, v, name);
         vlog_symtab_poison(st, name);
      }
   }
   ACLEAR(st->top->deferred);

   st->top = st->top->parent;
}

bool vlog_symtab_set_forward_net(vlog_symtab_t *st, bool forward)
{
   const bool old = st->forward_net;
   st->forward_net = forward;
   return old;
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

static vlog_node_t class_member_or_param(vlog_node_t class, ident_t id)
{
   // A parameter of a derived class hides a member of the same name in a
   // base class (8.13)

   vlog_node_t m = vlog_class_member(class, id);
   for (vlog_node_t c = class; c != NULL;
        c = vlog_has_ref(c) ? vlog_ref(c) : NULL) {
      const int ndecls = vlog_decls(c);
      for (int i = 0; i < ndecls; i++) {
         vlog_node_t d = vlog_decl(c, i);
         if (d == m)
            return m;
         else if ((vlog_kind(d) == V_PARAM_DECL
                   || vlog_kind(d) == V_LOCALPARAM) && vlog_ident(d) == id)
            return d;
      }

      if (vlog_kind(c) != V_CLASS_DECL)
         break;
   }

   return m;
}

static void lookup_member(vlog_symtab_t *st, vlog_node_t v)
{
   vlog_node_t prefix = vlog_value(v), type = vlog_get_type(prefix);
   if (vlog_typeparam_pending(v))
      return;   // Looked up when the type parameter is bound
   else if (type == NULL && vlog_virtual_chain(prefix))
      return;   // Looked up when the interface is known
   else if (type == NULL) {
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
      vlog_node_t m = class_member_or_param(type, id);
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

static bool lookup_rand_method(vlog_node_t v, vlog_node_t prefix)
{
   // Methods which are built in to every class, random variable, or
   // constraint

   ident_t id = vlog_ident(v);
   vlog_node_t type = vlog_get_type(prefix);
   const bool is_class = type != NULL && vlog_kind(type) == V_CLASS_DECL;

   vlog_node_t base = prefix;
   while (vlog_kind(base) == V_BIT_SELECT)
      base = vlog_value(base);

   vlog_node_t decl = NULL;
   switch (vlog_kind(base)) {
   case V_REF:
   case V_MEMBER_REF:
      if (vlog_has_ref(base))
         decl = vlog_ref(base);
      break;
   default:
      break;
   }

   if (id == ident_new("randomize")) {
      if (!is_class)
         return false;

      vlog_set_subkind(v, V_METHOD_RANDOMIZE);
      return true;
   }
   else if (id == ident_new("rand_mode")) {
      const bool is_rand = decl != NULL && vlog_kind(decl) == V_VAR_DECL
         && (vlog_flags(decl) & (VLOG_F_RAND | VLOG_F_RANDC));

      if (!is_class && !is_rand && (decl != NULL || type != NULL))
         error_at(vlog_loc(prefix), "rand_mode can only be called on an "
                  "object or a random variable");

      vlog_set_subkind(v, V_METHOD_RAND_MODE);
      return true;
   }
   else if (id == ident_new("constraint_mode")) {
      const bool is_cons = decl != NULL && vlog_kind(decl) == V_CONSTRAINT;

      if (!is_class && !is_cons && (decl != NULL || type != NULL))
         error_at(vlog_loc(prefix), "constraint_mode can only be called on "
                  "an object or a constraint");

      vlog_set_subkind(v, V_METHOD_CONSTRAINT_MODE);
      return true;
   }

   return false;
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

static bool lookup_array_method(vlog_node_t v, vlog_node_t prefix,
                                vlog_node_t type)
{
   static const struct {
      const char    *name;
      vlog_method_t  method;
   } tab[] = {
      { "reverse",          V_METHOD_ARR_REVERSE },
      { "sort",             V_METHOD_ARR_SORT },
      { "rsort",            V_METHOD_ARR_RSORT },
      { "shuffle",          V_METHOD_ARR_SHUFFLE },
      { "sum",              V_METHOD_ARR_SUM },
      { "product",          V_METHOD_ARR_PRODUCT },
      { "and",              V_METHOD_ARR_AND },
      { "or",               V_METHOD_ARR_OR },
      { "xor",              V_METHOD_ARR_XOR },
      { "min",              V_METHOD_ARR_MIN },
      { "max",              V_METHOD_ARR_MAX },
      { "unique",           V_METHOD_ARR_UNIQUE },
      { "unique_index",     V_METHOD_ARR_UNIQUE_INDEX },
      { "find",             V_METHOD_ARR_FIND },
      { "find_index",       V_METHOD_ARR_FIND_INDEX },
      { "find_first",       V_METHOD_ARR_FIND_FIRST },
      { "find_first_index", V_METHOD_ARR_FIND_FIRST_INDEX },
      { "find_last",        V_METHOD_ARR_FIND_LAST },
      { "find_last_index",  V_METHOD_ARR_FIND_LAST_INDEX },
      { "map",              V_METHOD_ARR_MAP },
      { "insert",           V_METHOD_ARR_INSERT },
   };

   vlog_node_t elem;
   bool is_queue = false, is_assoc = false;
   if (vlog_kind(type) == V_DATA_TYPE && vlog_subkind(type) == DT_QUEUE) {
      elem = vlog_type(type);
      is_queue = true;
   }
   else if (vlog_kind(type) == V_DATA_TYPE && vlog_subkind(type) == DT_DYNARRAY)
      elem = vlog_type(type);
   else if (vlog_kind(type) == V_DATA_TYPE && vlog_subkind(type) == DT_ASSOC) {
      elem = vlog_type(type);
      is_assoc = true;
   }
   else if ((vlog_kind(prefix) == V_REF || vlog_kind(prefix) == V_MEMBER_REF)
            && vlog_has_ref(prefix) && vlog_is_fixed_unpacked(vlog_ref(prefix)))
      elem = type;
   else
      return false;

   vlog_method_t m = V_METHOD_USER;
   for (int i = 0; i < ARRAY_LEN(tab); i++) {
      if (vlog_ident(v) == ident_new(tab[i].name))
         m = tab[i].method;
   }

   if (m == V_METHOD_USER || (m == V_METHOD_ARR_INSERT && !is_queue))
      return false;

   if (is_assoc) {
      switch (m) {
      case V_METHOD_ARR_FIND:
      case V_METHOD_ARR_FIND_INDEX:
      case V_METHOD_ARR_FIND_FIRST:
      case V_METHOD_ARR_FIND_FIRST_INDEX:
      case V_METHOD_ARR_FIND_LAST:
      case V_METHOD_ARR_FIND_LAST_INDEX:
      case V_METHOD_ARR_MAP:
      case V_METHOD_ARR_SUM:
      case V_METHOD_ARR_PRODUCT:
      case V_METHOD_ARR_AND:
      case V_METHOD_ARR_OR:
      case V_METHOD_ARR_XOR:
      case V_METHOD_ARR_MIN:
      case V_METHOD_ARR_MAX:
      case V_METHOD_ARR_UNIQUE:
      case V_METHOD_ARR_UNIQUE_INDEX:
         break;
      default:
         return false;
      }
   }

   vlog_set_subkind(v, m);

   const loc_t *loc = vlog_loc(v);
   const bool has_with = vlog_stmts(v) == 3;

   switch (m) {
   case V_METHOD_ARR_SUM:
   case V_METHOD_ARR_PRODUCT:
   case V_METHOD_ARR_AND:
   case V_METHOD_ARR_OR:
   case V_METHOD_ARR_XOR:
      if (has_with)
         vlog_set_ref(v, make_atom_type(DT_INT, 32, VLOG_F_SIGNED, loc));
      else
         vlog_set_ref(v, elem);
      break;
   case V_METHOD_ARR_MIN:
   case V_METHOD_ARR_MAX:
   case V_METHOD_ARR_UNIQUE:
   case V_METHOD_ARR_FIND:
   case V_METHOD_ARR_FIND_FIRST:
   case V_METHOD_ARR_FIND_LAST:
      vlog_set_ref(v, make_queue_type(elem, loc));
      break;
   case V_METHOD_ARR_MAP:
      {
         vlog_node_t rtype = NULL;
         if (has_with) {
            vlog_node_t w = vlog_stmt(v, 2);
            if (vlog_is_real(w))
               rtype = make_atom_type(DT_REAL, 64, 0, loc);
            else if (vlog_get_type(w) != NULL)
               rtype = vlog_get_type(w);
         }
         if (rtype == NULL)
            rtype = make_atom_type(DT_INT, 32, VLOG_F_SIGNED, loc);
         vlog_set_ref(v, make_queue_type(rtype, loc));
      }
      break;
   case V_METHOD_ARR_UNIQUE_INDEX:
   case V_METHOD_ARR_FIND_INDEX:
   case V_METHOD_ARR_FIND_FIRST_INDEX:
   case V_METHOD_ARR_FIND_LAST_INDEX:
      {
         // The indices of an associative array are its keys
         vlog_node_t key = is_assoc ? vlog_assoc_key(type) : NULL;
         vlog_set_ref(v, make_queue_type(
                         key != NULL ? key
                         : make_atom_type(DT_INT, 32, VLOG_F_SIGNED, loc),
                         loc));
      }
      break;
   default:
      break;
   }

   return true;
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

   if (kind == V_CLASS_DECL
       || (kind == V_DATA_TYPE && vlog_subkind(type) == DT_PROCESS)) {
      // Every class and process has these
      ident_t id = vlog_ident(v);
      if (id == ident_new("srandom")) {
         vlog_set_subkind(v, V_METHOD_SRANDOM);
         return;
      }
      else if (id == ident_new("set_randstate")) {
         vlog_set_subkind(v, V_METHOD_RANDSTATE_SET);
         return;
      }
      else if (id == ident_new("get_randstate")) {
         vlog_node_t str = vlog_new(V_DATA_TYPE);
         vlog_set_subkind(str, DT_STRING);
         vlog_set_loc(str, vlog_loc(v));
         vlog_set_ref(v, str);
         vlog_set_subkind(v, V_METHOD_RANDSTATE_GET);
         return;
      }
   }

   if (kind == V_DATA_TYPE && vlog_subkind(type) == DT_VIRTUAL) {
      if (!vlog_has_ref(type)) {
         vlog_set_subkind(v, V_METHOD_CLASS);   // Interface is declared later
         return;
      }

      vlog_node_t iface = vlog_ref(type);
      const int ndecls = vlog_decls(iface);
      for (int i = 0; i < ndecls; i++) {
         vlog_node_t d = vlog_decl(iface, i);
         const vlog_kind_t dk = vlog_kind(d);
         if ((dk == V_FUNC_DECL || dk == V_TASK_DECL)
             && vlog_ident(d) == vlog_ident(v)) {
            vlog_set_ref(v, d);
            vlog_set_subkind(v, V_METHOD_CLASS);
            return;
         }
      }

      error_at(vlog_loc(v), "interface has no method named %pQ",
               vlog_ident(v));
      return;
   }
   else if (kind == V_CLASS_DECL) {
      vlog_node_t m = vlog_class_member(type, vlog_ident(v));
      if (m == NULL && (vlog_ident(v) == ident_new("pre_randomize")
                        || vlog_ident(v) == ident_new("post_randomize")))
         vlog_set_subkind(v, V_METHOD_NOP);   // The built-in hook does nothing
      else if (m == NULL || (vlog_kind(m) != V_FUNC_DECL
                        && vlog_kind(m) != V_TASK_DECL))
         error_at(vlog_loc(v), "class %pQ has no method named %pQ",
                  vlog_ident(type), vlog_ident(v));
      else {
         vlog_set_ref(v, m);
         vlog_set_subkind(v, V_METHOD_CLASS);
      }
      return;
   }

   if (lookup_array_method(v, prefix, type))
      return;

   vlog_method_t m;
   if (kind == V_DATA_TYPE) {
      static const struct {
         const char    *name;
         data_type_t    dt;
         vlog_method_t  method;
      } tab[] = {
         { "size",       DT_DYNARRAY, V_METHOD_SIZE },
         { "delete",     DT_DYNARRAY, V_METHOD_DELETE },
         { "size",       DT_ASSOC,    V_METHOD_SIZE },
         { "num",        DT_ASSOC,    V_METHOD_SIZE },
         { "delete",     DT_ASSOC,    V_METHOD_DELETE },
         { "exists",     DT_ASSOC,    V_METHOD_ASSOC_EXISTS },
         { "first",      DT_ASSOC,    V_METHOD_ASSOC_FIRST },
         { "last",       DT_ASSOC,    V_METHOD_ASSOC_LAST },
         { "next",       DT_ASSOC,    V_METHOD_ASSOC_NEXT },
         { "prev",       DT_ASSOC,    V_METHOD_ASSOC_PREV },
         { "size",       DT_QUEUE,    V_METHOD_SIZE },
         { "delete",     DT_QUEUE,    V_METHOD_DELETE },
         { "push_back",  DT_QUEUE,    V_METHOD_PUSH_BACK },
         { "push_front", DT_QUEUE,    V_METHOD_PUSH_FRONT },
         { "pop_back",   DT_QUEUE,    V_METHOD_POP_BACK },
         { "pop_front",  DT_QUEUE,    V_METHOD_POP_FRONT },
         { "triggered",  DT_EVENT,    V_METHOD_EVENT_TRIGGERED },
         { "status",     DT_PROCESS,  V_METHOD_PROC_STATUS },
         { "kill",       DT_PROCESS,  V_METHOD_PROC_KILL },
         { "await",      DT_PROCESS,  V_METHOD_PROC_AWAIT },
         { "suspend",    DT_PROCESS,  V_METHOD_PROC_SUSPEND },
         { "resume",     DT_PROCESS,  V_METHOD_PROC_RESUME },
         { "get",        DT_SEMAPHORE, V_METHOD_SEM_GET },
         { "put",        DT_SEMAPHORE, V_METHOD_SEM_PUT },
         { "try_get",    DT_SEMAPHORE, V_METHOD_SEM_TRY_GET },
         { "put",        DT_MAILBOX,  V_METHOD_MBOX_PUT },
         { "get",        DT_MAILBOX,  V_METHOD_MBOX_GET },
         { "try_get",    DT_MAILBOX,  V_METHOD_MBOX_TRY_GET },
         { "num",        DT_MAILBOX,  V_METHOD_MBOX_NUM },
         { "try_put",    DT_MAILBOX,  V_METHOD_MBOX_TRY_PUT },
         { "peek",       DT_MAILBOX,  V_METHOD_MBOX_PEEK },
         { "try_peek",   DT_MAILBOX,  V_METHOD_MBOX_TRY_PEEK },
         { "len",        DT_STRING,   V_METHOD_STR_LEN },
         { "putc",       DT_STRING,   V_METHOD_STR_PUTC },
         { "getc",       DT_STRING,   V_METHOD_STR_GETC },
         { "toupper",    DT_STRING,   V_METHOD_STR_TOUPPER },
         { "tolower",    DT_STRING,   V_METHOD_STR_TOLOWER },
         { "compare",    DT_STRING,   V_METHOD_STR_COMPARE },
         { "icompare",   DT_STRING,   V_METHOD_STR_ICOMPARE },
         { "substr",     DT_STRING,   V_METHOD_STR_SUBSTR },
         { "atoi",       DT_STRING,   V_METHOD_STR_ATOI },
         { "atohex",     DT_STRING,   V_METHOD_STR_ATOHEX },
         { "atooct",     DT_STRING,   V_METHOD_STR_ATOOCT },
         { "atobin",     DT_STRING,   V_METHOD_STR_ATOBIN },
         { "atoreal",    DT_STRING,   V_METHOD_STR_ATOREAL },
         { "itoa",       DT_STRING,   V_METHOD_STR_ITOA },
         { "hextoa",     DT_STRING,   V_METHOD_STR_HEXTOA },
         { "octtoa",     DT_STRING,   V_METHOD_STR_OCTTOA },
         { "bintoa",     DT_STRING,   V_METHOD_STR_BINTOA },
         { "realtoa",    DT_STRING,   V_METHOD_STR_REALTOA },
      };

      m = V_METHOD_USER;
      for (int i = 0; i < ARRAY_LEN(tab); i++) {
         if (tab[i].dt == vlog_subkind(type)
             && vlog_ident(v) == ident_new(tab[i].name))
            m = tab[i].method;
      }

      // The type is only known after elaboration if it is a parameter
      if (m == V_METHOD_USER && vlog_kind(prefix) == V_REF
          && vlog_has_ref(prefix) && vlog_has_type(vlog_ref(prefix))) {
         vlog_node_t decl_type = vlog_type(vlog_ref(prefix));
         if (vlog_kind(decl_type) == V_TYPE_DECL
             && (vlog_flags(decl_type) & VLOG_F_TYPEPARAM))
            m = get_builtin_method(st, V_ENUM_DECL, vlog_ident(v));
      }
   }
   else
      m = get_builtin_method(st, kind, vlog_ident(v));

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

static void note_indirect(vlog_symbol_t *sym, const loc_t *loc)
{
   // Say once that a name was found through an import of an import

   if (sym->via == NULL || !sym->imported)
      return;

   const char *via = istr(ident_rfrom(sym->via, '.') ?: sym->via);
   const char *from = istr(ident_rfrom(sym->from, '.') ?: sym->from);

   diag_t *d = diag_new(DIAG_WARN, loc);
   diag_printf(d, "%pQ is visible only through the import of package %s in "
               "package %s", vlog_ident(sym->node), from, via);
   diag_hint(d, vlog_loc(sym->node), "declared here");
   diag_hint(d, NULL, "the standard requires package %s to export it with "
             "'export %s::*' (IEEE 1800-2017 section 26.6)", via, from);
   diag_emit(d);

   sym->via = sym->from = NULL;
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
   vlog_scope_t *sym_scope = NULL;
   const vlog_symbol_t *sym = symbol_for_scope(st, name, &sym_scope);
   vlog_node_t home = NULL;
   for (vlog_scope_t *s = st->top; s != NULL && home == NULL; s = s->parent)
      home = s->container;   // The scope of a generate loop has none

   if (sym == NULL && vlog_kind(v) == V_REF && st->implicit != V_NET_NONE
       && home != NULL) {
      // See 1800-2017 section 6.10 "Implicit declarations"
      vlog_node_t decl = vlog_new(V_NET_DECL);
      vlog_set_ident(decl, name);
      vlog_set_loc(decl, vlog_loc(v));
      vlog_set_subkind(decl, V_NET_WIRE);

      vlog_node_t dt = vlog_new(V_DATA_TYPE);
      vlog_set_subkind(dt, DT_LOGIC);
      vlog_set_flags(dt, VLOG_F_IMPLICIT_NET);

      vlog_set_type(decl, dt);

      vlog_set_ref(v, decl);

      vlog_add_decl(home, decl);

      fresh_symbol_for(st, name)->node = decl;
   }
   else if (sym == NULL
            || (vlog_kind(v) == V_MOD_REF && sym->node != POISON_NODE
                && is_non_agg_data_type(sym->node))) {
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
         else if (st->forward_net) {
            APUSH(st->top->net_deferred, v);
            break;
         }
         // Fall-through
      default:
         report_missing(st, v, name);
         vlog_symtab_poison(st, name);
      }
   }
   else if (sym->node != POISON_NODE) {
      vlog_set_ref(v, sym->node);
      note_indirect((vlog_symbol_t *)sym, vlog_loc(v));

      // A subroutine call bound to the compilation unit scope may still
      // be shadowed by a later local declaration in an enclosing scope
      if ((vlog_kind(v) == V_USER_TCALL || vlog_kind(v) == V_USER_FCALL)
          && sym_scope != NULL && sym_scope->parent == NULL
          && st->top->parent != NULL)
         APUSH(st->top->deferred, v);
   }
}

static bool copy_data_type_pred(vlog_node_t v, void *ctx)
{
   return vlog_kind(v) == V_DATA_TYPE;
}

static void merge_port_signed(vlog_node_t port, vlog_node_t decl)
{
   // A signed net or variable declared for a port makes the port signed
   // even if the port declaration did not say so (23.2.2.1)

   if (!vlog_has_type(port) || !vlog_has_type(decl))
      return;

   vlog_node_t pt = vlog_type(port), dt = vlog_type(decl);
   if (vlog_kind(pt) != V_DATA_TYPE || vlog_kind(dt) != V_DATA_TYPE
       || (vlog_flags(pt) & VLOG_F_SIGNED) || !(vlog_flags(dt) & VLOG_F_SIGNED))
      return;

   // The type may be shared with other ports in the same declaration
   vlog_node_t copy = vlog_copy(pt, copy_data_type_pred, NULL);
   vlog_set_flags(copy, vlog_flags(copy) | VLOG_F_SIGNED);
   vlog_set_type(port, copy);
}

void vlog_symtab_put(vlog_symtab_t *st, vlog_node_t v)
{
   ident_t name = vlog_ident(v);
   if (name == NULL) {
      assert(error_count() > 0);   // Parser is recovering from an error
      return;
   }

   vlog_scope_t *enclosing =
      st->top->container == v ? st->top->parent : st->top;
   if (enclosing != NULL && enclosing->container != NULL
       && vlog_kind(enclosing->container) == V_FUNC_DECL
       && vlog_has_type(enclosing->container)
       && vlog_ident(enclosing->container) == name) {
      diag_t *d = diag_new(DIAG_ERROR, vlog_loc(v));
      diag_printf(d, "cannot redeclare enclosing function name %pQ", name);
      diag_hint(d, vlog_loc(enclosing->container), "function %pQ declared here",
                name);
      diag_suppress(d, name == well_known(W_ERROR) || st->top->suppress);
      diag_emit(d);
      return;
   }

   vlog_symbol_t *sym = fresh_symbol_for(st, name);

   assert(!loc_invalid_p(vlog_loc(v)));

   if (sym->node == POISON_NODE || sym->node == v)
      return;

   if (sym->imported) {
      // A declaration takes precedence over a name from a wildcard import
      sym->imported = false;
      sym->via = sym->from = NULL;
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
               merge_port_signed(v, sym->node);
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
               merge_port_signed(sym->node, v);
               return;
            default:
               break;
            }
         }
         break;
      case V_CLASS_DECL:
         if ((vlog_flags(sym->node) & VLOG_F_FORWARD)
             && ((vlog_kind(v) == V_CLASS_DECL
                  && (vlog_flags(v) & VLOG_F_GENERIC))
                 || vlog_kind(v) == V_TYPE_DECL)) {
            sym->node = v;
            return;
         }
         break;
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

vlog_node_t vlog_symtab_query_at(vlog_symtab_t *st, ident_t name,
                                 const loc_t *loc)
{
   const vlog_symbol_t *sym = symbol_for(st, name);
   if (sym == NULL || sym->node == POISON_NODE)
      return NULL;

   note_indirect((vlog_symbol_t *)sym, loc);
   return sym->node;
}

vlog_node_t vlog_symtab_query(vlog_symtab_t *st, ident_t name)
{
   const vlog_symbol_t *sym = symbol_for(st, name);
   if (sym == NULL || sym->node == POISON_NODE)
      return NULL;

   return sym->node;
}

vlog_node_t vlog_symtab_query_local(vlog_symtab_t *st, ident_t name)
{
   vlog_scope_t *s = st->top;
   if (s == NULL || s->num_symbols == 0)
      return NULL;

   for (int slot = ident_hash(name) & (s->max_symbols - 1);
        s->symbols[slot].name != NULL;
        slot = (slot + 1) & (s->max_symbols - 1)) {
      if (s->symbols[slot].name == name) {
         const vlog_node_t n = s->symbols[slot].node;
         return n == POISON_NODE ? NULL : n;
      }
   }

   return NULL;
}

vlog_node_t vlog_symtab_query_unit(vlog_symtab_t *st, ident_t name)
{
   vlog_scope_t *s = st->top;
   while (s != NULL && s->parent != NULL)
      s = s->parent;

   if (s == NULL || s->num_symbols == 0)
      return NULL;

   for (int slot = ident_hash(name) & (s->max_symbols - 1);
        s->symbols[slot].name != NULL;
        slot = (slot + 1) & (s->max_symbols - 1)) {
      if (s->symbols[slot].name == name) {
         const vlog_node_t n = s->symbols[slot].node;
         return n == POISON_NODE ? NULL : n;
      }
   }

   return NULL;
}

void vlog_symtab_lenient(vlog_symtab_t *st)
{
   st->lenient = true;
}

void vlog_symtab_suppress(vlog_symtab_t *st)
{
   st->top->suppress = true;
   st->syntax_error = true;
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
         case V_GEN_BLOCK:
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

ident_t vlog_symtab_display_scope(vlog_symtab_t *st)
{
   // Names of the enclosing scopes as written in the source, each followed
   // by the separator that precedes a name inside it: :: after a package or
   // class (8.23, 26.3) and . after anything else

   ident_t name = NULL;
   for (vlog_scope_t *s = st->top; s != NULL; s = s->parent) {
      vlog_node_t c = s->container;
      if (c == NULL || !vlog_has_ident(c))
         continue;

      ident_t id;
      const char *sep = ".";
      switch (vlog_kind(c)) {
      case V_PACKAGE:
         sep = "::";
         // Fall-through
      case V_MODULE:
      case V_PROGRAM:
         id = vlog_has_ident2(c) ? vlog_ident2(c) : vlog_ident(c);
         break;
      case V_CLASS_DECL:
         sep = "::";
         // Fall-through
      default:
         id = vlog_ident(c);
         break;
      }

      LOCAL_TEXT_BUF tb = tb_new();
      tb_cat(tb, istr(id));
      tb_cat(tb, sep);
      if (name != NULL)
         tb_cat(tb, istr(name));
      name = ident_new(tb_get(tb));
   }

   return name;
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

static void put_imported(vlog_symtab_t *st, vlog_node_t d, ident_t via,
                         ident_t from)
{
   // Names with the same name from different wildcard imports are only an
   // error if one is used which is not checked.  A name which is only
   // visible through the import of another package (via is not NULL) is
   // given way to by any other.

   vlog_symbol_t *sym = fresh_symbol_for(st, vlog_ident(d));
   if (sym->node != NULL && !(sym->imported && sym->via != NULL
                              && via == NULL))
      return;

   sym->node = d;
   sym->imported = true;
   sym->via = via;
   sym->from = from;
}

static void import_package(vlog_symtab_t *st, vlog_node_t pack, ident_t match,
                           ident_t via, ident_t from, int depth)
{
   assert(vlog_kind(pack) == V_PACKAGE);

   // IEEE 1800-2017 section 26.3 does not make the names a package imports
   // visible to its users unless they are exported, but a name which is
   // only imported indirectly is found with a warning under --relaxed
   const bool relaxed = relaxed_rules();

   const int ndecls = vlog_decls(pack);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(pack, i);
      if (vlog_kind(d) == V_CLASS_DECL && (vlog_flags(d) & VLOG_F_SPEC))
         continue;   // A specialisation has no name that can be used
      else if (vlog_kind(d) == V_IMPORT_DECL) {
         ident_t item = vlog_has_ident(d) ? vlog_ident(d) : NULL;
         if (item != NULL && match != NULL && item != match)
            continue;

         if (!(vlog_flags(d) & VLOG_F_EXPORT)) {
            if (relaxed && match == NULL && depth < 8 && vlog_has_ref(d)
                && vlog_kind(vlog_ref(d)) == V_PACKAGE && item == NULL) {
               vlog_node_t q = vlog_ref(d);
               import_package(st, q, NULL, via ?: vlog_ident(pack),
                              via ? from : vlog_ident(q), depth + 1);
            }
            continue;
         }

         if (vlog_has_ref(d) && vlog_kind(vlog_ref(d)) == V_PACKAGE)
            import_package(st, vlog_ref(d), item ?: match, via, from,
                           depth + 1);
         else if (!vlog_has_ref(d)) {
            for (int j = 0; j < ndecls; j++) {
               vlog_node_t imp = vlog_decl(pack, j);
               if (vlog_kind(imp) == V_IMPORT_DECL
                   && !(vlog_flags(imp) & VLOG_F_EXPORT)
                   && vlog_has_ref(imp)
                   && vlog_kind(vlog_ref(imp)) == V_PACKAGE)
                  import_package(st, vlog_ref(imp),
                                 vlog_has_ident(imp) ? vlog_ident(imp)
                                 : match, via, from, depth + 1);
            }
         }
         continue;
      }

      if (match == NULL)
         put_imported(st, d, via, from);
      else if (vlog_ident(d) == match)
         vlog_symtab_put(st, d);

      // The names of an enumeration are declared in the package too
      if (vlog_kind(d) != V_TYPE_DECL && vlog_kind(d) != V_VAR_DECL)
         continue;

      vlog_node_t e = vlog_get_type(vlog_type(d));
      if (e == NULL || vlog_kind(e) != V_ENUM_DECL)
         continue;

      const int nnames = vlog_decls(e);
      for (int j = 0; j < nnames; j++) {
         vlog_node_t n = vlog_decl(e, j);
         if (match == NULL)
            put_imported(st, n, via, from);
         else if (vlog_ident(n) == match)
            vlog_symtab_put(st, n);
      }
   }
}

void vlog_symtab_import(vlog_symtab_t *st, vlog_node_t v)
{
   assert(vlog_kind(v) == V_IMPORT_DECL);

   if (!vlog_has_ref(v))
      return;

   ident_t match = NULL;
   if (vlog_has_ident(v))
      match = vlog_ident(v);

   import_package(st, vlog_ref(v), match, NULL, NULL, 0);
}
