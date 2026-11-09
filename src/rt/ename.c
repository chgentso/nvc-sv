//
//  Copyright (C) 2024-2026  Nick Gasson
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
#include "diag.h"
#include "hash.h"
#include "ident.h"
#include "jit/jit-exits.h"
#include "jit/jit.h"
#include "lib.h"
#include "printf.h"
#include "rt/model.h"
#include "rt/mspace.h"
#include "rt/structs.h"
#include "tree.h"
#include "type.h"
#include "vlog/vlog-node.h"
#include "vlog/vlog-util.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

static tree_t select_name(tree_t where, ident_t id)
{
   const int ndecls = tree_decls(where);
   for (int i = 0; i < ndecls; i++) {
      tree_t d = tree_decl(where, i);
      if (tree_ident(d) == id)
         return d;
   }

   const int ngenerics = tree_generics(where);
   for (int i = 0; i < ngenerics; i++) {
      tree_t g = tree_generic(where, i);
      if (tree_ident(g) == id)
         return g;
   }

   switch (tree_kind(where)) {
   case T_PACKAGE:
      return select_name(body_of(where), id);
   case T_PACK_BODY:
      return NULL;
   default:
      break;
   }

   const int nstmts = tree_stmts(where);
   for (int i = 0; i < nstmts; i++) {
      tree_t s = tree_stmt(where, i);
      if (tree_ident(s) == id)
         return s;
   }

   const int nports = tree_ports(where);
   for (int i = 0; i < nports; i++) {
      tree_t p = tree_port(where, i);
      if (tree_ident(p) == id)
         return p;
   }

   return NULL;
}

static bool is_implicit_block(tree_t block)
{
   if (tree_kind(block) != T_BLOCK)
      return false;

   tree_t hier = tree_decl(block, 0);
   assert(tree_kind(hier) == T_HIER);

   return tree_subkind(hier) == T_COMPONENT;
}

static void bind_external_name(tree_t name, jit_handle_t scope,
                               jit_scalar_t *args)
{
   assert(tree_kind(name) == T_EXTERNAL_NAME);

   jit_t *j = jit_for_thread();
   tree_t where = tree_from_object(jit_get_object(j, scope));
   tree_t container = NULL, next = NULL;
   ident_t path = jit_get_name(j, scope);
   int next_arg = 2;

   const int nparts = tree_parts(name);
   for (int i = 0; i < nparts; i++, where = next, next = NULL) {
      tree_t pe = tree_part(name, i);
      assert(tree_kind(pe) == T_PATH_ELT);

      ident_t id = NULL;
      switch (tree_subkind(pe)) {
      case PE_ABSOLUTE:
         {
            tree_t top;
            rt_model_t *m = get_model_or_null();
            if (m == NULL)
               top = tree_container(where);
            else
               top = root_scope(m)->where;

            if (tree_kind(top) != T_ELAB)
               jit_msg(tree_loc(pe), DIAG_FATAL, "design hierarchy root has "
                       "not yet been elaborated");

            tree_t entity = tree_part(name, ++i);
            assert(tree_subkind(entity) == PE_SIMPLE);

            const int nroots = tree_stmts(top);
            tree_t root = NULL;
            for (int i = 0; i < nroots; i++) {
               tree_t s = tree_stmt(top, i);
               if (ident_casecmp(tree_ident(s), tree_ident(entity))) {
                  root = s;
                  break;
               }
            }

            if (root == NULL)
               jit_msg(tree_loc(pe), DIAG_FATAL, "%pI is not the name of a "
                       "root of the design hierarchy", tree_ident(entity));

            path = ident_prefix(lib_name(lib_work()), tree_ident(root), '.');
            next = root;
         }
         continue;
      case PE_SIMPLE:
         id = tree_ident(pe);
         break;
      case PE_GENERATE:
         {
            LOCAL_TEXT_BUF tb = tb_new();
            tb_istr(tb, tree_ident(pe));
            tb_printf(tb, "(%"PRIi64")", args[next_arg++].integer);

            id = ident_new(tb_get(tb));
         }
         break;
      case PE_LIBRARY:
         {
            lib_t lib = lib_require(tree_ident(pe));

            pe = tree_part(name, ++i);
            assert(tree_subkind(pe) == PE_SIMPLE);

            ident_t qual = ident_prefix(lib_name(lib), tree_ident(pe), '.');

            tree_t pack = lib_get(lib, qual);
            if (pack == NULL || !is_package(pack))
               jit_msg(tree_loc(pe), DIAG_FATAL, "%s is not a package",
                       istr(qual));

            path = tree_ident(pack);
            next = pack;
         }
         continue;
      case PE_RELATIVE:
         next = where;
         continue;
      case PE_CARET:
         do {
            ident_t parent = ident_runtil(path, '.');
            assert(parent != NULL);

            if (parent == lib_name(lib_work()) || tree_kind(where) != T_BLOCK)
               jit_msg(tree_loc(pe), DIAG_FATAL, "relative pathname has "
                       "no containing declarative region");

            scope = jit_lazy_compile(j, parent);
            path = jit_get_name(j, scope);
            next = tree_from_object(jit_get_object(j, scope));
         } while (is_implicit_block(next));
         continue;
      default:
         should_not_reach_here();
      }

      if (!is_concurrent_block(where) && !is_package(where)) {
         diag_t *d = diag_new(DIAG_ERROR, tree_loc(pe));
         diag_printf(d, "%s is not a concurrent region",
                     istr(tree_ident(where)));
         diag_hint(d, tree_loc(where), "location of %s",
                   istr(tree_ident(where)));
         diag_emit(d);
         jit_abort_with_status(1);
      }
      else if (is_implicit_block(where)) {
         // Skip over implicit block for component declaration
         where = tree_stmt(where, 0);
         assert(tree_kind(where) == T_BLOCK);
         path = ident_prefix(path, tree_ident(where), '.');
      }

      container = where;

      if ((next = select_name(where, id)) == NULL) {
         diag_t *d = diag_new(DIAG_ERROR, tree_loc(pe));
         diag_printf(d, "external name %s not found",
                     istr(tree_ident(tree_part(name, nparts - 1))));
         diag_hint(d, tree_loc(pe), "name %s not found inside %s", istr(id),
                   istr(tree_ident(where)));

         if (!tree_frozen(where))
            diag_hint(d, NULL, "an object cannot be referenced by an "
                      "external name until it has been elaborated");

         diag_emit(d);
         jit_abort_with_status(1);
      }

      if (i + 1 < nparts)
         path = ident_prefix(path, id, '.');
   }

   if (class_of(where) != tree_class(name)) {
      diag_t *d = diag_new(DIAG_ERROR, tree_loc(name));
      diag_printf(d, "class of object %s is not %s", istr(tree_ident(where)),
                  class_str(tree_class(name)));
      diag_hint(d, tree_loc(name), "external name with class %s",
                class_str(tree_class(name)));
      diag_hint(d, tree_loc(where), "declaration of %s as %s",
                istr(tree_ident(where)), class_str(class_of(where)));
      diag_emit(d);
      jit_abort_with_status(1);
   }

   type_t type = tree_type(where);
   type_t expect = tree_type(name);

   if (!type_eq(type, expect)) {
      diag_t *d = diag_new(DIAG_ERROR, tree_loc(name));
      diag_printf(d, "type of %s %pI is %pT but external name has %pT",
                  class_str(tree_class(name)), tree_ident(where), type, expect);
      diag_hint(d, tree_loc(name), "external name with type %pT", expect);
      diag_hint(d, tree_loc(where), "declaration of %pI with type %pT",
                tree_ident(where), type);
      diag_emit(d);
      jit_abort_with_status(1);
   }

   jit_handle_t handle = jit_compile(j, path);

   void *ctx;
   if (is_concurrent_block(container)) {
      rt_scope_t *rts = find_scope(get_model(), container);
      assert(rts != NULL);

      if ((ctx = *mptr_get(rts->privdata)) == NULL)
         jit_msg(tree_loc(name), DIAG_FATAL, "%pI has not yet been elaborated",
                 tree_ident(container));
   }
   else
      ctx = jit_link(j, handle);

   void *ptr = jit_get_frame_var(j, handle, ctx, tree_ident(where));

   if (type_is_array(type) && type_const_bounds(type)) {
      int ndims = 0;
      for (type_t t = type; type_is_array(t); t = type_elem(t))
         ndims += dimension_of(t);

      jit_scalar_t *slots =
         jit_mspace_alloc((2 + ndims * 2) * sizeof(jit_scalar_t));

      int pos = 0;
      if (tree_class(name) == C_SIGNAL && type_is_homogeneous(type)) {
         slots[pos++].pointer = *(sig_shared_t **)ptr;
         slots[pos++].integer = *(int32_t *)(ptr + 8);
      }
      else
         slots[pos++].pointer = ptr;

      for (type_t t = type; type_is_array(t); t = type_elem(t)) {
         const int ndims = dimension_of(t);
         for (int i = 0; i < ndims; i++) {
            tree_t r = range_of(t, i);
            const range_kind_t dir = tree_subkind(r);
            const int64_t left = assume_int(tree_left(r));
            const int64_t right = assume_int(tree_right(r));
            const int64_t length =
               MAX(0, dir == RANGE_TO ? right - left + 1 : left - right + 1);

            slots[pos++].integer = left;
            slots[pos++].integer = dir == RANGE_TO ? length : ~length;
         }
      }

      args[0].pointer = slots;
   }
   else
      args[0].pointer = ptr;
}

rt_scope_t *scope_for_context(rt_scope_t *s, void *ctx)
{
   static hash_t *cache = NULL;

   if (cache == NULL)
      cache = hash_new(64);
   else {
      rt_scope_t *cached = hash_get(cache, ctx);
      if (cached != NULL)
         return cached;
   }

   if (s->kind == SCOPE_INSTANCE && *mptr_get(s->privdata) == ctx) {
      hash_put(cache, ctx, s);
      return s;
   }

   for (int i = 0; i < s->children.count; i++) {
      rt_scope_t *found = scope_for_context(s->children.items[i], ctx);
      if (found != NULL)
         return found;
   }

   return NULL;
}

void *x_sv_vif_child(void *context, int64_t label)
{
   // The context of an interface instance inside the instance of the
   // interface which a virtual interface refers to

   if (context == NULL)
      jit_msg(NULL, DIAG_FATAL, "null virtual interface handle");

   rt_model_t *m = get_model();
   rt_scope_t *s = scope_for_context(root_scope(m), context);
   if (s == NULL)
      jit_msg(NULL, DIAG_FATAL, "virtual interface handle does not refer "
              "to an interface instance");

   for (int i = 0; i < s->children.count; i++) {
      rt_scope_t *c = s->children.items[i];
      if (c->kind != SCOPE_INSTANCE || tree_kind(c->where) != T_BLOCK)
         continue;

      uint64_t hash = UINT64_C(14695981039346656037);
      for (const char *p = istr(tree_ident(c->where)); *p != '\0'; p++)
         hash = (hash ^ (uint8_t)*p) * UINT64_C(1099511628211);

      if ((int64_t)hash == label)
         return *mptr_get(c->privdata);
   }

   jit_msg(NULL, DIAG_FATAL, "interface instance not found in the virtual "
           "interface");
   return NULL;
}

static void bind_hier_ref(vlog_node_t v, jit_handle_t scope,
                          jit_scalar_t *args)
{
   assert(vlog_kind(v) == V_HIER_REF);

   rt_model_t *m = get_model();
   rt_scope_t *s = get_active_scope(m);

   if (args[2].pointer != NULL) {
      rt_scope_t *lexical = scope_for_context(root_scope(m), args[2].pointer);
      if (lexical != NULL)
         s = lexical;
   }

   tree_t alias = NULL;
   ident_t alias_id = NULL;

   // Port connections are evaluated in the child instance but the name
   // is relative to the parent where the connection was written
   vlog_node_t root = vlog_value(v);
   while (vlog_has_value(root))
      root = vlog_value(root);

   if (vlog_has_ref(root) || vlog_kind(root) == V_MOD_REF) {
      for (rt_scope_t *it = s; it != NULL; it = it->parent) {
         if (it->kind != SCOPE_INSTANCE || tree_kind(it->where) != T_BLOCK)
            continue;

         if (vlog_has_ref(root) && vlog_kind(vlog_ref(root)) == V_IFACE_PORT) {
            vlog_node_t port = vlog_ref(root);
            if (vlog_iface_port_target(it->where, port,
                                       vlog_ranges(port) > 0
                                       ? vlog_mod_ref_name(root) : NULL)) {
               s = it;
               goto found;
            }

            continue;
         }

         const int nstmts = tree_stmts(it->where);
         for (int i = 0; i < nstmts; i++) {
            tree_t c = tree_stmt(it->where, i);
            if (tree_kind(c) == T_BLOCK
                && ident_casecmp(tree_ident(c), vlog_mod_ref_name(root))) {
               s = it;
               goto found;
            }
         }
      }
   }

   if (!vlog_has_ref(root) && vlog_kind(root) == V_MOD_REF) {
      // An upwards name can be that of an enclosing instance or module
      ident_t name = vlog_mod_ref_name(root);
      bool child = false;
      for (rt_scope_t *it = s; it != NULL && !child; it = it->parent) {
         if (it->kind != SCOPE_INSTANCE || tree_kind(it->where) != T_BLOCK)
            continue;

         const int nstmts = tree_stmts(it->where);
         for (int i = 0; i < nstmts && !child; i++) {
            tree_t c = tree_stmt(it->where, i);
            child = tree_kind(c) == T_BLOCK && ident_casecmp(tree_ident(c), name);
         }

         if (!child && vlog_block_is_named(it->where, name)) {
            alias = it->where;
            alias_id = name;
            break;
         }
      }
   }

 found:
   ;
   vlog_set_mod_ref_alias(alias_id, alias);
   tree_t b = vlog_walk_mod_refs(vlog_value(v), s->where);
   vlog_set_mod_ref_alias(NULL, NULL);
   assert(b != NULL);

   if (vlog_ident(v) == ident_new("$self"))
      ;   // The context of the interface instance itself
   else if (!vlog_has_ref(v) || (vlog_kind(vlog_ref(v)) != V_FUNC_DECL
                                 && vlog_kind(vlog_ref(v)) != V_TASK_DECL))
      b = vlog_hier_target_block(b, vlog_ident(v));

   tree_t h = tree_decl(b, 0);
   assert(tree_kind(h) == T_HIER);

   rt_scope_t *rts = find_scope(get_model(), b);
   assert(rts != NULL);

   void *ctx = *mptr_get(rts->privdata);
   if (ctx == NULL)
      jit_msg(vlog_loc(v), DIAG_FATAL, "%pi has not yet been elaborated",
              tree_ident(b));

   jit_t *j = jit_for_thread();
   jit_handle_t handle = jit_compile(j, tree_ident2(h));

   // Subroutines are called with the context of the instance
   ident_t name = vlog_ident(v);
   if (vlog_has_ref(v)) {
      const vlog_kind_t kind = vlog_kind(vlog_ref(v));
      if (kind == V_FUNC_DECL || kind == V_TASK_DECL)
         name = ident_new("$self");
   }

   if (args[4].integer == 1)   // The signal of a structure net
      name = ident_prefix(name, ident_new("net"), '$');

   args[0].pointer = jit_get_frame_var(j, handle, ctx, name);

   // The width of a net connected to an inout port cannot be checked
   // before the name is bound
   const uint32_t expect = args[3].integer;
   if (expect > 0) {
      sig_shared_t *ss = *(sig_shared_t **)args[0].pointer;
      rt_signal_t *sig = container_of(ss, rt_signal_t, shared);
      if (signal_width(sig) != expect)
         jit_msg(vlog_loc(v), DIAG_FATAL, "sorry, an inout port connected to "
                 "a hierarchical name of a different width is not supported");
   }
}

void x_bind_external(object_t *where, jit_handle_t scope, jit_scalar_t *args)
{
   tree_t t = tree_from_object(where);
   if (t != NULL)
      bind_external_name(t, scope, args);

   vlog_node_t v = vlog_from_object(where);
   if (v != NULL)
      bind_hier_ref(v, scope, args);
}
