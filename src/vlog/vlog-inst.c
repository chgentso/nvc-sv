//
//  Copyright (C) 2025-2026 Nick Gasson
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
#include "object.h"
#include "diag.h"
#include "hash.h"
#include "lib.h"
#include "ident.h"
#include "vlog/vlog-node.h"
#include "vlog/vlog-number.h"
#include "vlog/vlog-phase.h"
#include "vlog/vlog-util.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

static vlog_node_t bind_parameter(vlog_node_t decl, int nth, vlog_node_t inst)
{
   vlog_node_t value = NULL;
   bool given = false;
   if (vlog_has_value(decl))
      value = vlog_value(decl);

   if (inst != NULL) {
      vlog_node_t p;
      const int nparams = vlog_params(inst);
      if (nth < nparams && !vlog_has_ident((p = vlog_param(inst, nth)))) {
         value = vlog_value(p);
         given = true;
      }
      else {
         ident_t id = vlog_ident(decl);
         for (int i = 0; i < nparams; i++) {
            p = vlog_param(inst, i);
            if (!vlog_has_ident(p))
               continue;
            else if (vlog_ident(p) != id)
               continue;

            if (vlog_has_value(p)) {
               value = vlog_value(p);
               given = true;
            }

            break;
         }
      }
   }

   if (value == NULL) {
      diag_t *d = diag_new(DIAG_ERROR, vlog_loc(inst));
      diag_printf(d, "missing value for parameter %s", istr(vlog_ident(decl)));
      diag_hint(d, vlog_loc(decl), "parameter declared here");
      diag_emit(d);
      return NULL;
   }

   if (vlog_kind(value) == V_REF) {
      vlog_node_t decl = vlog_ref(value);
      if (vlog_kind(decl) == V_LOCALPARAM) {
         vlog_node_t type = vlog_type(decl);
         value = vlog_value(decl);

         const bool vector = vlog_kind(type) == V_DATA_TYPE
            && (vlog_subkind(type) == DT_LOGIC || vlog_subkind(type) == DT_BIT
                || vlog_subkind(type) == DT_IMPLICIT);

         if (vector && vlog_ranges(decl) == 0
             && (vlog_ranges(type) > 0 || !is_implicit_data_type(type)
                 || (vlog_flags(type) & (VLOG_F_SIGNED | VLOG_F_UNSIGNED)))) {
            // The value has the type declared for the parameter
            vlog_node_t cast = vlog_new(V_CAST);
            vlog_set_subkind(cast, V_CAST_TYPE);
            vlog_set_type(cast, type);
            vlog_set_value(cast, value);
            vlog_set_loc(cast, vlog_loc(value));
            value = cast;
         }
      }
   }

   vlog_node_t local = vlog_new(V_LOCALPARAM);
   vlog_set_loc(local, vlog_loc(decl));
   vlog_set_ident(local, vlog_ident(decl));
   vlog_set_value(local, value);
   vlog_set_type(local, vlog_type(decl));
   vlog_set_flags(local, VLOG_F_OVERRIDABLE | (given ? VLOG_F_OVERRIDDEN : 0));

   const int nranges = vlog_ranges(decl);
   for (int i = 0; i < nranges; i++)
      vlog_add_range(local, vlog_range(decl, i));

   return local;
}

static vlog_node_t bind_type_parameter(vlog_node_t decl, int nth,
                                       vlog_node_t inst)
{
   // The value of an overridden type parameter or null for the default

   if (inst == NULL)
      return NULL;

   vlog_node_t p;
   const int nparams = vlog_params(inst);
   if (nth < nparams && !vlog_has_ident((p = vlog_param(inst, nth))))
      return vlog_has_value(p) ? vlog_value(p) : NULL;

   ident_t id = vlog_ident(decl);
   for (int i = 0; i < nparams; i++) {
      p = vlog_param(inst, i);
      if (vlog_has_ident(p) && vlog_ident(p) == id)
         return vlog_has_value(p) ? vlog_value(p) : NULL;
   }

   return NULL;
}

// Nodes inside classes shared by all instances: copying one of these
// would copy the whole method and leave references to the original
static hset_t *shared_nodes = NULL;

static void collect_shared_cb(vlog_node_t v, void *ctx)
{
   hset_insert(shared_nodes, v);
}

static void collect_shared_class_cb(vlog_node_t v, void *ctx)
{
   if (vlog_kind(v) == V_CLASS_DECL && (vlog_flags(v) & VLOG_F_GLOBAL))
      vlog_visit(v, collect_shared_cb, NULL);
}

// Nodes inside the generate block being instantiated: a reference to an
// instance elsewhere in the module has already been copied with it
static hset_t *copy_scope = NULL;

static void collect_scope_cb(vlog_node_t v, void *ctx)
{
   hset_insert(copy_scope, v);
}

static bool is_shared(vlog_node_t v)
{
   return shared_nodes != NULL && hset_contains(shared_nodes, v);
}

static bool copy_checker = false;

static bool is_event_formal(vlog_node_t d)
{
   // A formal of event type may be replaced by an event expression in
   // the instance

   vlog_node_t type;
   if (copy_checker && vlog_subkind(d) == V_PORT_INPUT)
      return true;   // May be replaced by a constant actual

   return vlog_subkind(d) == V_PORT_INPUT
      && vlog_has_type(d)
      && (type = vlog_get_type(vlog_type(d))) != NULL
      && vlog_kind(type) == V_DATA_TYPE
      && vlog_subkind(type) == DT_EVENT;
}

// Enumerations whose base type has a width that depends on a parameter:
// their names refer back to the declaration so must be copied with it
static hset_t *param_enums = NULL;

static void collect_enum_decls_cb(vlog_node_t v, void *ctx)
{
   if (vlog_kind(v) == V_ENUM_DECL)
      APUSH(*(A(vlog_node_t) *)ctx, v);
   else if (vlog_kind(v) == V_TYPE_DECL && vlog_has_type(v)
            && vlog_kind(vlog_type(v)) == V_ENUM_DECL)
      APUSH(*(A(vlog_node_t) *)ctx, vlog_type(v));
}

typedef struct {
   bool            found;
   A(vlog_node_t)  pending;
} param_scan_t;

static void find_param_ref_cb(vlog_node_t v, void *ctx)
{
   param_scan_t *scan = ctx;
   if (vlog_kind(v) == V_REF && vlog_has_ref(v)) {
      vlog_node_t d = vlog_ref(v);
      switch (vlog_kind(d)) {
      case V_PARAM_DECL:
      case V_GENVAR_DECL:
         scan->found = true;
         break;
      case V_LOCALPARAM:
         APUSH(scan->pending, d);
         break;
      default:
         break;
      }
   }
}

static bool depends_on_param(vlog_node_t expr)
{
   // A local parameter can depend on a parameter of the module.  This
   // cannot be done while copying as a visit uses the same mark bits.

   param_scan_t scan = { .found = false };
   hset_t *seen = hset_new(8);
   vlog_visit(expr, find_param_ref_cb, &scan);

   for (unsigned i = 0; !scan.found && i < scan.pending.count; i++) {
      vlog_node_t d = scan.pending.items[i];
      if (hset_contains(seen, d) || !vlog_has_value(d))
         continue;
      hset_insert(seen, d);
      vlog_visit(vlog_value(d), find_param_ref_cb, &scan);
   }

   const bool found = scan.found;
   ACLEAR(scan.pending);
   hset_free(seen);
   return found;
}

static void collect_param_enums(vlog_node_t mod)
{
   A(vlog_node_t) decls = AINIT;
   vlog_visit(mod, collect_enum_decls_cb, &decls);

   for (unsigned i = 0; i < decls.count; i++) {
      vlog_node_t decl = decls.items[i];
      if (!vlog_has_type(decl) || vlog_kind(vlog_type(decl)) != V_DATA_TYPE)
         continue;

      vlog_node_t base = vlog_type(decl);
      bool found = false;
      const int nranges = vlog_ranges(base);
      for (int j = 0; j < nranges && !found; j++) {
         vlog_node_t r = vlog_range(base, j);
         if (vlog_subkind(r) == V_DIM_UNSIZED || !vlog_has_left(r))
            continue;
         found = depends_on_param(vlog_left(r))
            || (vlog_has_right(r) && depends_on_param(vlog_right(r)));
      }

      if (found) {
         if (param_enums == NULL)
            param_enums = hset_new(16);
         hset_insert(param_enums, decl);
      }
   }

   ACLEAR(decls);
}

static bool is_param_enum_name(vlog_node_t d)
{
   return param_enums != NULL && vlog_kind(d) == V_ENUM_NAME
      && vlog_has_type(d) && hset_contains(param_enums, vlog_type(d));
}

static bool copy_instance_pred(vlog_node_t v, void *ctx)
{
   switch (vlog_kind(v)) {
   case V_REF:
      if (!vlog_has_ref(v))
         return false;
      switch (vlog_kind(vlog_ref(v))) {
      case V_ENUM_NAME:
         return is_param_enum_name(vlog_ref(v));
      case V_PARAM_DECL:
      case V_GENVAR_DECL:
         return true;
      case V_PORT_DECL:
         return is_event_formal(vlog_ref(v));
      case V_FUNC_DECL:
         // The name of a function assigned to give its result
         return true;
      case V_VAR_DECL:
      case V_LOCAL_DECL:
         {
            // A variable whose initial value refers to itself is only
            // copied together with its type if the reference is copied too
            vlog_node_t d = vlog_ref(v);
            if (!vlog_has_type(d))
               return false;

            if (strncmp(istr(vlog_ident(d)), "$sampled.", 9) == 0) {
               // Sampled copy of a formal of event type
               vlog_node_t t = vlog_get_type(vlog_type(d));
               return copy_checker || (t != NULL && vlog_kind(t) == V_DATA_TYPE
                                       && vlog_subkind(t) == DT_EVENT);
            }

            // The type parameters have already been bound when the
            // module was copied so a generate block shares the variable
            if (copy_scope != NULL)
               return false;

            vlog_node_t type = vlog_type(d);
            return vlog_kind(type) == V_TYPE_DECL
               && (vlog_flags(type) & VLOG_F_TYPEPARAM);
         }
      default:
         return false;
      }
   case V_PORT_DECL:
      return is_event_formal(v);
   case V_MOD_REF:
      return copy_scope == NULL || hset_contains(copy_scope, v);
   case V_ENUM_NAME:
      return is_param_enum_name(v)
         && (copy_scope == NULL || hset_contains(copy_scope, v));
   case V_TYPE_DECL:
      return copy_scope == NULL && (vlog_flags(v) & VLOG_F_TYPEPARAM);
   case V_SYS_TCALL:
   case V_SYS_FCALL:
      // A call in a declaration outside a generate block is not copied with
      // the block or the declaration would no longer be the one it uses
      return vlog_design_precision() >= 0 && !is_shared(v)
         && (copy_scope == NULL || hset_contains(copy_scope, v));
   case V_NUMBER:
      return !!(vlog_flags(v) & (VLOG_F_TIMELIT | VLOG_F_ONESTEP))
         && !is_shared(v)
         && (copy_scope == NULL || hset_contains(copy_scope, v));
   case V_FUNC_DECL:
   case V_TASK_DECL:
   case V_CLASS_DECL:
   case V_CONSTRUCTOR:
      // Declarations outside of modules are shared by all instances
      return !(vlog_flags(v) & VLOG_F_GLOBAL);
   default:
      return false;
   }
}

static void round_delay(vlog_node_t ctrl)
{
   // A time literal is rounded to the time precision of the module (5.8)
   // but 1step is the smallest precision in the design (14.4)

   vlog_node_t value = vlog_value(ctrl);
   if (vlog_kind(value) != V_NUMBER
       || !(vlog_flags(value) & (VLOG_F_TIMELIT | VLOG_F_ONESTEP)))
      return;

   const int64_t ival = vlog_time_resolve(vlog_ival(ctrl));
   unsigned log10 = VLOG_TIME_LOG10(ival);
   if (vlog_design_precision() >= 0 && (vlog_flags(value) & VLOG_F_ONESTEP))
      log10 = MIN(log10, vlog_design_precision());

   vlog_node_t new = vlog_new(V_NUMBER);
   vlog_set_loc(new, vlog_loc(value));

   int64_t fs = number_integer(vlog_number(value));
   if (vlog_flags(value) & VLOG_F_ONESTEP)
      fs = vlog_time_quantum(log10);
   else
      fs = vlog_round_time(fs, log10);

   vlog_set_number(new, number_from_int(fs));

   vlog_set_value(ctrl, new);
   vlog_set_ival(ctrl, VLOG_TIME_ENCODE(1, VLOG_TIME_LOG10(ival)));
}

static vlog_node_t fixup_refs_cb(vlog_node_t v, void *ctx)
{
   hash_t *map = ctx;

   switch (vlog_kind(v)) {
   case V_NUMBER:
      if ((vlog_flags(v) & VLOG_F_TIMEEXPR) && !is_shared(v)) {
         vlog_node_t new = vlog_new(V_NUMBER);
         vlog_set_loc(new, vlog_loc(v));
         vlog_set_number(new, number_from_int(
                            vlog_time_literal(number_integer(vlog_number(v)))));
         return new;
      }
      break;
   case V_REF:
      {
         vlog_node_t new = hash_get(map, vlog_ref(v));
         if (new != NULL)
            vlog_set_ref(v, new);
      }
      break;
   case V_MEMBER_REF:
      if (vlog_typeparam_pending(v)) {
         // The type parameter is now bound so the field can be found
         vlog_node_t type = vlog_get_type(vlog_value(v));
         const vlog_kind_t kind = type ? vlog_kind(type) : V_DATA_TYPE;
         if (kind == V_STRUCT_DECL || kind == V_UNION_DECL) {
            const int ndecls = vlog_decls(type);
            for (int i = 0; i < ndecls; i++) {
               vlog_node_t d = vlog_decl(type, i);
               if (vlog_ident(d) == vlog_ident(v)) {
                  vlog_set_ref(v, d);
                  return v;
               }
            }

            error_at(vlog_loc(v), "%s has no field named %pQ",
                     kind == V_STRUCT_DECL ? "struct" : "union",
                     vlog_ident(v));
         }
         else
            error_at(vlog_loc(v), "%pQ is not a struct or union",
                     vlog_has_ident(vlog_value(v))
                     ? vlog_ident(vlog_value(v)) : vlog_ident(v));
      }
      break;
   case V_SYS_TCALL:
   case V_SYS_FCALL:
      if (vlog_design_precision() >= 0 && !is_shared(v))
         vlog_set_ival(v, VLOG_TIME_SET_DESIGN(
                           vlog_time_resolve(vlog_ival(v)),
                           vlog_design_precision()));
      break;
   case V_DELAY_CONTROL:
      if (!is_shared(v))
         round_delay(v);
      break;
   default:
      break;
   }

   return v;
}

static void rename_tf_decls(vlog_node_t v, ident_t prefix)
{
   const int ndecls = vlog_decls(v);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(v, i);
      switch (vlog_kind(d)) {
      case V_FUNC_DECL:
      case V_TASK_DECL:
      case V_CONSTRUCTOR:
         vlog_set_ident2(d, prefix);
         break;
      case V_CLASS_DECL:
         if (vlog_flags(d) & VLOG_F_GLOBAL)
            break;   // Shared by all instances
         vlog_set_ident2(d, prefix);
         rename_tf_decls(d, ident_prefix(prefix, vlog_ident(d), '.'));
         break;
      default:
         break;
      }
   }
}

static A(vlog_node_t) bind_units = AINIT;

static void load_bind_cb(lib_t lib, ident_t name, int kind, void *ctx)
{
   if (strstr(istr(name), ".BIND#") == NULL)
      return;

   object_t *obj = lib_get_generic(lib, name, NULL);
   vlog_node_t b = obj != NULL ? vlog_from_object(obj) : NULL;
   if (b != NULL && vlog_kind(b) == V_BIND)
      APUSH(bind_units, b);
}

static void collect_insts_cb(vlog_node_t v, void *ctx)
{
   if (vlog_kind(v) == V_INST_LIST)
      APUSH(*(A(ident_t) *)ctx, vlog_ident(v));
}

static ident_t bind_key(ident_t name)
{
   // The name of a module without its library, in upper case

   const char *str = istr(name), *dot = strrchr(str, '.');
   LOCAL_TEXT_BUF tb = tb_new();
   tb_cat(tb, dot != NULL ? dot + 1 : str);
   tb_upcase(tb);
   return ident_new(tb_get(tb));
}

static unsigned reached = 0;

static void reach_module(lib_t lib, ident_t name, hset_t *seen)
{
   ident_t key = bind_key(name);
   if (hset_contains(seen, key))
      return;

   hset_insert(seen, key);
   reached++;

   ident_t qual = ident_prefix(lib_name(lib), key, '.');
   object_t *obj = lib_get_generic(lib, qual, NULL);
   vlog_node_t mod = obj != NULL ? vlog_from_object(obj) : NULL;
   if (mod == NULL || vlog_kind(mod) != V_MODULE)
      return;

   A(ident_t) insts = AINIT;
   vlog_visit(mod, collect_insts_cb, &insts);

   for (unsigned i = 0; i < insts.count; i++)
      reach_module(lib, insts.items[i], seen);

   ACLEAR(insts);
}

void vlog_load_binds(lib_t lib, object_t **tops, int ntops)
{
   ACLEAR(bind_units);
   lib_walk_index(lib, load_bind_cb, NULL);

   // A bind directive in a module only applies if the module is in the
   // elaborated design.  If a top level is not a Verilog module the
   // hierarchy is not known so every directive applies.
   hset_t *seen = hset_new(64);
   bool known = true;
   for (int i = 0; i < ntops; i++) {
      vlog_node_t top = vlog_from_object(tops[i]);
      if (top == NULL || vlog_kind(top) != V_MODULE)
         known = false;
      else
         reach_module(lib, vlog_ident2(top), seen);
   }

   if (known) {
      bool *done LOCAL = xcalloc_array(MAX(bind_units.count, 1), sizeof(bool));
      bool changed;
      do {
         changed = false;
         for (unsigned i = 0; i < bind_units.count; i++) {
            if (done[i])
               continue;

            vlog_node_t b = bind_units.items[i];
            const char *name = istr(vlog_ident(b));
            const char *scope = strchr(strstr(name, ".BIND#") + 6, '#');
            scope = scope != NULL ? strchr(scope + 1, '#') : NULL;

            const bool active = scope == NULL
               || hset_contains(seen, bind_key(ident_new(scope + 1)));
            if (active) {
               done[i] = true;

               // The bound instances may contain others
               A(ident_t) insts = AINIT;
               vlog_visit(b, collect_insts_cb, &insts);
               for (unsigned j = 0; j < insts.count; j++) {
                  const unsigned before = reached;
                  reach_module(lib, insts.items[j], seen);
                  changed |= reached != before;
               }
               ACLEAR(insts);
            }
         }
      } while (changed);

      unsigned wptr = 0;
      for (unsigned i = 0; i < bind_units.count; i++) {
         if (done[i])
            bind_units.items[wptr++] = bind_units.items[i];
      }
      ATRIM(bind_units, wptr);
   }

   hset_free(seen);
}

static bool copy_bind_pred(vlog_node_t v, void *ctx)
{
   switch (vlog_kind(v)) {
   case V_REF:
   case V_HIER_REF:
   case V_MOD_REF:
   case V_PORT_CONN:
      return true;
   default:
      return false;
   }
}

typedef struct {
   vlog_node_t copy;
   vlog_node_t body;
   bool        vhdl_shim;
} bind_ctx_t;

static bool bind_is_vhdl_shim(vlog_node_t mod)
{
   const char *str = istr(vlog_ident(mod));
   const char *dollar = strrchr(str, '$');
   if (dollar == NULL || dollar[1] == '\0')
      return false;

   for (const char *p = dollar + 1; *p != '\0'; p++) {
      if (*p < '0' || *p > '9')
         return false;
   }

   return true;
}

static bool bind_matches_mod(vlog_node_t b, vlog_node_t mod)
{
   if (vlog_ident2(b) == vlog_ident(mod))
      return true;

   if (bind_is_vhdl_shim(mod)) {
      ident_t base = ident_until(vlog_ident(mod), '$');
      return bind_key(base) == bind_key(vlog_ident2(b));
   }

   return false;
}

static vlog_node_t fix_bind_ref_cb(vlog_node_t v, void *ctx)
{
   const bind_ctx_t *bc = ctx;
   vlog_node_t copy = bc->copy;

   if (vlog_kind(v) == V_PORT_CONN && (vlog_flags(v) & VLOG_F_WILDCARD)) {
      // Implicit connections are to the module the instance is bound to
      vlog_set_ref(v, copy);
      return v;
   }

   if (vlog_kind(v) != V_REF || !vlog_has_ident(v))
      return v;

   ident_t id = vlog_ident(v);

   const int nports = vlog_ports(copy);
   for (int i = 0; i < nports; i++) {
      vlog_node_t p = vlog_port(copy, i);
      if (vlog_kind(p) == V_PORT_DECL
          && (vlog_ident(p) == id
              || (bc->vhdl_shim && ident_casecmp(vlog_ident(p), id)))) {
         vlog_set_ref(v, p);
         return v;
      }
   }

   const int ndecls = vlog_decls(copy);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(copy, i);
      switch (vlog_kind(d)) {
      case V_PORT_DECL:
      case V_VAR_DECL:
      case V_NET_DECL:
      case V_PARAM_DECL:
      case V_LOCALPARAM:
         if (vlog_ident(d) == id
             || (bc->vhdl_shim && ident_casecmp(vlog_ident(d), id)))
            vlog_set_ref(v, d);
         break;
      default:
         break;
      }
   }

   // Implicit nets declared in the target for the bound instance
   const int nbody = vlog_decls(bc->body);
   for (int i = 0; i < nbody; i++) {
      vlog_node_t d = vlog_decl(bc->body, i);
      if (vlog_kind(d) == V_NET_DECL && vlog_ident(d) == id)
         vlog_set_ref(v, d);
   }

   return v;
}

bool vlog_bind_has_hier_ref(vlog_node_t mod)
{
   for (unsigned i = 0; i < bind_units.count; i++) {
      vlog_node_t b = bind_units.items[i];
      if (bind_matches_mod(b, mod)
          && (vlog_global_flags(b) & VLOG_GF_HIER_REF))
         return true;
   }

   return false;
}

static ident_t inst_dotted = NULL;

void vlog_set_instance_scope(ident_t dotted)
{
   inst_dotted = dotted;
}

bool vlog_bind_is_selective(vlog_node_t mod)
{
   for (unsigned i = 0; i < bind_units.count; i++) {
      vlog_node_t b = bind_units.items[i];
      if (bind_matches_mod(b, mod) && vlog_params(b) > 0)
         return true;
   }

   return false;
}

static bool bind_path_match(const char *path, const char *pattern)
{
   while (*pattern != '\0' && *path != '\0') {
      if (pattern[0] == '[' && pattern[1] == '*' && pattern[2] == ']') {
         if (*path != '[')
            return false;
         path++;
         if (*path == '-')
            path++;
         if (*path < '0' || *path > '9')
            return false;
         while (*path >= '0' && *path <= '9')
            path++;
         if (*path != ']')
            return false;
         path++;
         pattern += 3;
      }
      else if (*path != *pattern)
         return false;
      else {
         path++;
         pattern++;
      }
   }

   return *path == '\0' && *pattern == '\0';
}

static bool bind_matches_instance(vlog_node_t b)
{
   const int nsel = vlog_params(b);
   if (nsel == 0)
      return true;

   for (int i = 0; i < nsel; i++) {
      number_t n = vlog_number(vlog_param(b, i));
      const int len = number_width(n) / 8;
      char *text LOCAL = xmalloc(len + 1);
      for (int j = 0; j < len; j++)
         text[j] = number_byte(n, len - 1 - j);
      text[len] = '\0';

      // The dotted name starts with the library
      const char *path = inst_dotted != NULL ? strchr(istr(inst_dotted), '.')
         : NULL;
      if (path != NULL && bind_path_match(path + 1, text))
         return true;
   }

   return false;
}

static void add_bound_instances(vlog_node_t v, vlog_node_t mod,
                                vlog_node_t copy)
{
   const bool vhdl_shim = bind_is_vhdl_shim(mod);

   for (unsigned i = 0; i < bind_units.count; i++) {
      vlog_node_t b = bind_units.items[i];
      if (!bind_matches_mod(b, mod) || !bind_matches_instance(b))
         continue;

      // A name used in a port connection of the bound instance and not
      // declared in the target is an implicit net there (6.10)
      const int nbdecls = vlog_decls(b);
      for (int j = 0; j < nbdecls; j++) {
         vlog_node_t d = vlog_decl(b, j);
         if (vlog_kind(d) != V_NET_DECL || !vlog_has_type(d))
            continue;

         vlog_node_t dt = vlog_type(d);
         if (vlog_kind(dt) != V_DATA_TYPE
             || !(vlog_flags(dt) & VLOG_F_IMPLICIT_NET))
            continue;

         ident_t name = vlog_ident(d);
         bool exists = false;
         for (int k = 0; k < vlog_ports(copy) && !exists; k++)
            exists = vlog_ident(vlog_port(copy, k)) == name;
         for (int k = 0; k < vlog_decls(copy) && !exists; k++) {
            vlog_node_t o = vlog_decl(copy, k);
            exists = vlog_has_ident(o) && vlog_ident(o) == name;
         }

         if (!exists) {
            vlog_node_t nd = vlog_new(V_NET_DECL);
            vlog_set_ident(nd, name);
            vlog_set_loc(nd, vlog_loc(d));
            vlog_set_subkind(nd, vlog_subkind(d));

            vlog_node_t ndt = vlog_new(V_DATA_TYPE);
            vlog_set_subkind(ndt, vlog_subkind(dt));
            vlog_set_flags(ndt, vlog_flags(dt));
            vlog_set_loc(ndt, vlog_loc(dt));
            vlog_set_type(nd, ndt);

            vlog_add_decl(v, nd);
         }
      }

      bind_ctx_t bc = { copy, v, vhdl_shim };
      const int nstmts = vlog_stmts(b);
      for (int j = 0; j < nstmts; j++) {
         vlog_node_t c = vlog_copy(vlog_stmt(b, j), copy_bind_pred, NULL);
         vlog_rewrite(c, NULL, fix_bind_ref_cb, &bc);
         vlog_add_stmt(v, c);
      }
   }
}

vlog_node_t vlog_new_instance(vlog_node_t mod, vlog_node_t inst, ident_t id)
{
   vlog_node_t v = vlog_new(V_INST_BODY);
   vlog_set_ident(v, id);
   vlog_set_loc(v, vlog_loc(inst ?: mod));

   switch (vlog_kind(mod)) {
   case V_MODULE:
      vlog_set_flags(v, vlog_flags(mod)
                     & (VLOG_F_INTERFACE | VLOG_F_CHECKER
                        | VLOG_F_BLACKBOX));
      // Fall-through
   case V_PROGRAM:
      vlog_set_ident2(v, vlog_ident2(mod));
      vlog_set_ival(v, vlog_ival(mod));   // Time unit and precision
      break;
   case V_PRIMITIVE:
      vlog_set_ident2(v, vlog_ident2(mod));
      vlog_set_ival(v, VLOG_TIME_ENCODE(1, VLOG_TIME_UNSCALED));
      break;
   case V_GEN_BLOCK:
      vlog_set_flags(v, VLOG_F_GEN_BODY);
      vlog_set_ident2(v, vlog_ident(mod));
      vlog_set_ival(v, VLOG_TIME_ENCODE(1, VLOG_TIME_UNSCALED));
      break;
   default:
      should_not_reach_here();
   }

   shared_nodes = hset_new(64);
   vlog_visit(mod, collect_shared_class_cb, NULL);

   if (vlog_kind(mod) == V_GEN_BLOCK) {
      copy_scope = hset_new(64);
      vlog_visit(mod, collect_scope_cb, NULL);
   }

   collect_param_enums(mod);

   copy_checker = vlog_kind(mod) == V_MODULE
      && (vlog_flags(mod) & VLOG_F_CHECKER);
   vlog_node_t copy = vlog_copy(mod, copy_instance_pred, NULL);
   copy_checker = false;

   if (param_enums != NULL) {
      hset_free(param_enums);
      param_enums = NULL;
   }

   hash_t *map = hash_new(16);

   switch (vlog_kind(mod)) {
   case V_MODULE:
   case V_PROGRAM:
   case V_PRIMITIVE:
      {
         const int nports = vlog_ports(copy);
         for (int i = 0; i < nports; i++)
            vlog_add_port(v, vlog_port(copy, i));
      }
      break;
   default:
      break;
   }

   if (vlog_kind(mod) == V_MODULE) {
      const int npragmas = vlog_pragmas(mod);
      for (int i = 0; i < npragmas; i++)
         vlog_add_pragma(v, vlog_pragma(mod, i));
   }

   int pidx = 0;
   const int ndecls = vlog_decls(copy);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(copy, i);
      switch (vlog_kind(d)) {
      case V_PARAM_DECL:
         {
            vlog_node_t local = bind_parameter(d, pidx++, inst);
            if (local != NULL) {
               vlog_add_decl(v, local);
               hash_put(map, d, local);
            }
         }
         break;
      case V_TYPE_DECL:
         if (vlog_flags(d) & VLOG_F_TYPEPARAM) {
            vlog_node_t value = bind_type_parameter(d, pidx++, inst);
            if (value != NULL) {
               const unsigned r = vlog_flags(d) & VLOG_F_RESTRICT_MASK;
               if (r && !vlog_type_matches_restrict(r, value))
                  error_at(vlog_loc(inst), "type parameter %pQ requires a %s "
                           "type", vlog_ident(d), vlog_type_restrict_str(r));
               vlog_set_type(d, value);
               vlog_set_flags(d, vlog_flags(d) | VLOG_F_OVERRIDDEN);
            }
         }
         vlog_add_decl(v, d);
         break;
      default:
         vlog_add_decl(v, d);
         break;
      }
   }

   if (inst != NULL) {
      const int nparams = vlog_params(inst);
      if (pidx < nparams)
         error_at(vlog_loc(inst), "module %s has %d parameter%s but %d "
                  "values given", istr(vlog_ident2(mod)), pidx,
                  pidx != 1 ? "s" : "", nparams);
   }

   const int nstmts = vlog_stmts(copy);
   for (int i = 0; i < nstmts; i++)
      vlog_add_stmt(v, vlog_stmt(copy, i));

   if (vlog_kind(mod) == V_MODULE)
      add_bound_instances(v, mod, copy);

   rename_tf_decls(v, id);

   vlog_rewrite(v, NULL, fixup_refs_cb, map);

   hash_free(map);
   hset_free(shared_nodes);
   shared_nodes = NULL;
   if (copy_scope != NULL) {
      hset_free(copy_scope);
      copy_scope = NULL;
   }
   return v;
}

static bool copy_generate_pred(vlog_node_t v, void *ctx)
{
   switch (vlog_kind(v)) {
   case V_REF:
      return vlog_kind(vlog_ref(v)) == V_GENVAR_DECL;
   case V_GEN_BLOCK:
      return v == ctx;
   case V_FUNC_DECL:
   case V_TASK_DECL:
      return !(vlog_flags(v) & VLOG_F_GLOBAL);
   default:
      return false;
   }
}

vlog_node_t vlog_generate_instance(vlog_node_t v, vlog_node_t genvar,
                                   int32_t value, ident_t prefix)
{
   assert(vlog_kind(v) == V_GEN_BLOCK);
   assert(vlog_kind(genvar) == V_GENVAR_DECL);

   vlog_node_t copy = vlog_copy(v, copy_generate_pred, v);

   ident_t id = ident_sprintf("%s[%d]", istr(vlog_ident(v)), value);
   vlog_set_ident(copy, id);

   vlog_node_t num = vlog_new(V_NUMBER);
   vlog_set_number(num, number_from_int(value));
   vlog_set_loc(num, vlog_loc(genvar));

   vlog_node_t lp = vlog_new(V_LOCALPARAM);
   vlog_set_ident(lp, vlog_ident(genvar));
   vlog_set_type(lp, vlog_type(genvar));
   vlog_set_value(lp, num);

   vlog_add_decl(copy, lp);

   rename_tf_decls(copy, ident_prefix(prefix, id, '.'));

   hash_t *map = hash_new(4);
   hash_put(map, genvar, lp);

   vlog_rewrite(copy, NULL, fixup_refs_cb, map);

   hash_free(map);
   return copy;
}

static mir_context_t *fold_mir = NULL;
static jit_t         *fold_jit = NULL;

void vlog_set_fold_context(mir_context_t *mc, jit_t *j)
{
   fold_mir = mc;
   fold_jit = j;
}

static bool has_parameters(vlog_node_t mod)
{
   const int ndecls = vlog_decls(mod);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(mod, i);
      if (vlog_kind(d) == V_PARAM_DECL
          || (vlog_kind(d) == V_TYPE_DECL && (vlog_flags(d) & VLOG_F_TYPEPARAM)))
         return true;
   }

   return false;
}

static void key_append_type(text_buf_t *tb, vlog_node_t t)
{
   for (;;) {
      if (vlog_kind(t) == V_REF && vlog_has_ref(t))
         t = vlog_ref(t);   // A name qualified with a package
      else if (vlog_kind(t) == V_TYPE_DECL && vlog_has_type(t))
         t = vlog_type(t);
      else
         break;
   }

   switch (vlog_kind(t)) {
   case V_DATA_TYPE:
      tb_printf(tb, "%d.%u.%u", vlog_subkind(t), vlog_size(t),
                (unsigned)(vlog_flags(t) & (VLOG_F_SIGNED | VLOG_F_PACKED)));
      break;
   default:
      tb_printf(tb, "%s", vlog_has_ident(t) ? istr(vlog_ident(t)) : "?");
      break;
   }
}

static bool key_append_value(text_buf_t *tb, vlog_node_t v)
{
   if (vlog_kind(v) == V_PATTERN_EXPR) {
      tb_append(tb, '{');
      const int nparams = vlog_params(v);
      for (int i = 0; i < nparams; i++) {
         vlog_node_t item = vlog_param(v, i);
         if (vlog_subkind(item) != 0 || !key_append_value(tb, vlog_right(item)))
            return false;
         tb_append(tb, ',');
      }
      tb_append(tb, '}');
      return true;
   }

   if (vlog_kind(v) == V_STRING) {
      number_print(vlog_number(v), tb);
      return true;
   }

   int64_t value;
   if (!vlog_get_const(v, &value))
      return false;

   tb_printf(tb, "%"PRIi64, value);
   return true;
}

ident_t vlog_instance_key(vlog_node_t mod, vlog_node_t body)
{
   // The values of the parameters identify the layout of an interface

   LOCAL_TEXT_BUF tb = tb_new();

   const int ndecls = vlog_decls(mod);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(mod, i);
      if (vlog_kind(d) == V_TYPE_DECL && (vlog_flags(d) & VLOG_F_TYPEPARAM)) {
         bool found = false;
         const int nbody = vlog_decls(body);
         for (int j = 0; j < nbody && !found; j++) {
            vlog_node_t b = vlog_decl(body, j);
            if (vlog_kind(b) != V_TYPE_DECL || vlog_ident(b) != vlog_ident(d)
                || !vlog_has_type(b))
               continue;

            tb_printf(tb, "%s=type ", istr(vlog_ident(d)));
            key_append_type(tb, vlog_type(b));
            tb_append(tb, ';');
            found = true;
         }

         if (!found)
            return NULL;
         continue;
      }

      if (vlog_kind(d) != V_PARAM_DECL)
         continue;

      bool found = false;
      const int nbody = vlog_decls(body);
      for (int j = 0; j < nbody && !found; j++) {
         vlog_node_t b = vlog_decl(body, j);
         if (vlog_kind(b) != V_LOCALPARAM || vlog_ident(b) != vlog_ident(d))
            continue;

         if (!vlog_has_value(b))
            return NULL;

         tb_printf(tb, "%s=", istr(vlog_ident(d)));
         if (!key_append_value(tb, vlog_value(b)))
            return NULL;
         tb_append(tb, ';');
         found = true;
      }

      if (!found)
         return NULL;
   }

   if (tb_len(tb) == 0)
      return NULL;   // No parameters so nothing distinguishes instances

   return ident_new(tb_get(tb));
}

vlog_node_t vlog_virtual_spec(vlog_node_t type)
{
   // The elaborated declarations of the interface of a virtual interface
   // type whose parameters are given values

   static hash_t *cache = NULL;

   if (fold_mir == NULL)
      return NULL;

   vlog_node_t mod = vlog_virtual_iface(type);
   if (mod == NULL || vlog_kind(mod) != V_MODULE || !has_parameters(mod))
      return NULL;

   if (cache == NULL)
      cache = hash_new(16);

   vlog_node_t cached = hash_get(cache, type);
   if (cached != NULL)
      return cached == (vlog_node_t)-1 ? NULL : cached;

   object_arena_t *saved = detach_global_arena();
   make_new_arena();

   vlog_node_t list = vlog_new(V_INST_LIST);
   vlog_set_loc(list, vlog_loc(type));

   const int nparams = vlog_params(type);
   for (int i = 0; i < nparams; i++)
      vlog_add_param(list, vlog_param(type, i));

   const int base_errors = error_count();

   vlog_node_t spec = NULL;
   vlog_node_t tmp = vlog_new_instance(mod, list, ident_new("$spec"));
   vlog_fold(tmp, fold_mir, fold_jit);

   ident_t key = error_count() == base_errors
      ? vlog_instance_key(mod, tmp) : NULL;
   if (key != NULL) {
      ident_t id = ident_sprintf("%s#%s", istr(vlog_ident(mod)), istr(key));
      spec = vlog_new_instance(mod, list, id);
      vlog_fold(spec, fold_mir, fold_jit);
   }

   // Keep the arena open as freezing would discard the unreachable nodes
   detach_global_arena();
   attach_global_arena(saved);

   hash_put(cache, type, spec ?: (vlog_node_t)-1);
   return spec;
}

bool vlog_virtual_unresolved(vlog_node_t type)
{
   // The interface has parameters whose values are not known

   vlog_node_t mod = vlog_virtual_iface(type);
   if (mod == NULL || vlog_kind(mod) != V_MODULE)
      return false;

   return has_parameters(mod) && vlog_virtual_spec(type) == NULL;
}

typedef struct {
   vlog_node_t        body;
   vlog_port_body_fn_t port_fn;
   void              *port_ctx;
   hash_t            *specs;
   hash_t            *hidden;
   unsigned           depth;
   unsigned           in_params;
   bool               changed;
} iface_res_t;

static bool copy_all_pred(vlog_node_t v, void *ctx)
{
   return true;
}

static vlog_node_t find_inst_list(vlog_node_t body, ident_t name,
                                  bool indexed, vlog_node_t *inst)
{
   // The instance list of an instance declared at the top level of the body

   const int nstmts = vlog_stmts(body);
   for (int i = 0; i < nstmts; i++) {
      vlog_node_t list = vlog_stmt(body, i);
      if (vlog_kind(list) != V_INST_LIST)
         continue;

      const int ninst = vlog_stmts(list);
      for (int j = 0; j < ninst; j++) {
         vlog_node_t it = vlog_stmt(list, j);
         if (vlog_kind(it) == V_MOD_INST && vlog_ident(it) == name
             && (vlog_ranges(it) > 0) == indexed) {
            *inst = it;
            return list;
         }
      }
   }

   return NULL;
}

static vlog_node_t resolve_href_cb(vlog_node_t v, void *ctx);

static vlog_node_t iface_instance_spec(iface_res_t *res, vlog_node_t list)
{
   // The elaborated declarations of an interface instance declared in a
   // body whose parameters have already been given values

   vlog_node_t cached = hash_get(res->specs, list);
   if (cached != NULL)
      return cached == (vlog_node_t)-1 ? NULL : cached;

   hash_put(res->specs, list, (vlog_node_t)-1);   // Also breaks cycles

   vlog_node_t mod = vlog_find_interface(vlog_ident(list));
   if (mod == NULL || vlog_kind(mod) != V_MODULE
       || !(vlog_flags(mod) & VLOG_F_INTERFACE) || fold_mir == NULL
       || res->depth > 8)
      return NULL;

   // The nodes of the new instance may only refer to nodes of the same
   // arena or one that is frozen so copy the overrides
   object_arena_t *saved = detach_global_arena();
   make_new_arena();

   // The overrides may themselves be names inside other interfaces
   vlog_node_t plist = vlog_new(V_INST_LIST);
   vlog_set_loc(plist, vlog_loc(list));
   vlog_set_ident(plist, vlog_ident(list));

   res->depth++;
   res->in_params++;
   const int nparams = vlog_params(list);
   for (int i = 0; i < nparams; i++) {
      vlog_node_t p = vlog_param(list, i);
      if (vlog_has_value(p)) {
         p = vlog_copy(p, copy_all_pred, NULL);
         vlog_set_value(p, vlog_rewrite(vlog_value(p), NULL, resolve_href_cb,
                                        res));
      }
      vlog_add_param(plist, p);
   }
   res->in_params--;
   res->depth--;

   const int base_errors = error_count();

   vlog_node_t spec = vlog_new_instance(mod, plist, ident_new("$spec"));
   vlog_fold(spec, fold_mir, fold_jit);

   iface_res_t sub = {
      .body  = spec,
      .specs  = res->specs,
      .hidden = hash_new(4),
      .depth  = res->depth + 1,
   };
   vlog_rewrite(spec, NULL, resolve_href_cb, &sub);
   if (sub.changed)
      vlog_fold(spec, fold_mir, fold_jit);

   hash_free(sub.hidden);

   if (error_count() != base_errors)
      spec = NULL;

   // Keep the arena open as freezing would discard the unreachable nodes
   detach_global_arena();
   attach_global_arena(saved);

   hash_put(res->specs, list, spec ?: (vlog_node_t)-1);
   return spec;
}

static vlog_node_t iface_path_body(iface_res_t *res, vlog_node_t body,
                                   vlog_node_t mr)
{
   // The body of the interface instance named by a component of the path

   ident_t name = vlog_ident(mr);

   if (body == res->body) {
      const int ndecls = vlog_decls(body);
      for (int i = 0; i < ndecls; i++) {
         vlog_node_t d = vlog_decl(body, i);
         if (vlog_kind(d) == V_IFACE_PORT && vlog_ident(d) == name) {
            if ((vlog_ranges(d) > 0) != (vlog_params(mr) > 0)
                || res->port_fn == NULL)
               return NULL;   // Elements of an array have the same layout

            return res->port_fn(d, res->port_ctx);
         }
      }
   }

   vlog_node_t inst;
   vlog_node_t list = find_inst_list(body, name, vlog_params(mr) > 0, &inst);
   if (list == NULL)
      return NULL;

   return iface_instance_spec(res, list);
}

static bool is_aggregate_param(vlog_node_t d)
{
   if (!vlog_has_type(d))
      return false;

   vlog_node_t t = vlog_type(d);
   while (vlog_kind(t) == V_TYPE_DECL && vlog_has_type(t))
      t = vlog_type(t);

   return vlog_kind(t) == V_STRUCT_DECL || vlog_kind(t) == V_UNION_DECL;
}

static vlog_node_t resolve_href_cb(vlog_node_t v, void *ctx)
{
   iface_res_t *res = ctx;

   if (vlog_kind(v) != V_HIER_REF || vlog_ident(v) == ident_new("$self"))
      return v;

   // The path of instance names that the member is selected from
   vlog_node_t path[8];
   int npath = 0;
   for (vlog_node_t it = vlog_value(v); ; it = vlog_value(it)) {
      if (vlog_kind(it) != V_MOD_REF || vlog_params(it) > 1
          || npath == ARRAY_LEN(path))
         return v;

      path[npath++] = it;
      if (!vlog_has_value(it))
         break;
   }

   vlog_node_t cur = res->body;
   for (int i = npath - 1; i >= 0; i--) {
      if ((cur = iface_path_body(res, cur, path[i])) == NULL)
         return v;
   }

   ident_t id = vlog_ident(v);
   const int ndecls = vlog_decls(cur);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(cur, i);
      switch (vlog_kind(d)) {
      case V_LOCALPARAM:
      case V_PARAM_DECL:
         if (vlog_ident(d) != id || !vlog_has_value(d) || vlog_ranges(d) > 0)
            continue;

         vlog_kind_t vkind = vlog_kind(vlog_value(d));
         if (vkind == V_NUMBER && is_aggregate_param(d))
            vkind = V_PATTERN_EXPR;   // A packed structure has a number

         switch (vkind) {
         case V_NUMBER:
         case V_REAL:
         case V_STRING:
            res->changed = true;
            return vlog_copy(vlog_value(d), copy_all_pred, NULL);
         case V_PATTERN_EXPR:
            if (res->in_params > 0) {
               // The value of a parameter of another interface
               res->changed = true;
               return vlog_copy(vlog_value(d), copy_all_pred, NULL);
            }

            {
               // An aggregate is a parameter of this body from now on so
               // that members of it can be selected
               vlog_node_t lp = hash_get(res->hidden, d);
               if (lp == NULL) {
                  lp = vlog_new(V_LOCALPARAM);
                  vlog_set_loc(lp, vlog_loc(v));
                  vlog_set_ident(lp, ident_sprintf("$iface.%s#%u", istr(id),
                                                 hash_members(res->hidden)));
                  vlog_set_type(lp, vlog_copy(vlog_type(d), copy_all_pred,
                                              NULL));
                  vlog_set_value(lp, vlog_copy(vlog_value(d), copy_all_pred,
                                               NULL));
                  vlog_add_decl(res->body, lp);
                  hash_put(res->hidden, d, lp);
               }

               vlog_node_t ref = vlog_new(V_REF);
               vlog_set_ident(ref, vlog_ident(lp));
               vlog_set_ref(ref, lp);
               vlog_set_loc(ref, vlog_loc(v));
               res->changed = true;
               return ref;
            }
         default:
            return v;
         }
      default:
         break;
      }
   }

   return v;
}

bool vlog_resolve_iface_params(vlog_node_t body, vlog_port_body_fn_t fn,
                               void *ctx)
{
   // The value of a parameter of an interface instance or port is known
   // before the body is translated and its dimensions may depend on it

   if (fold_mir == NULL)
      return false;

   iface_res_t res = {
      .body     = body,
      .port_fn  = fn,
      .port_ctx = ctx,
      .specs    = hash_new(8),
      .hidden   = hash_new(4),
   };

   vlog_rewrite(body, NULL, resolve_href_cb, &res);

   hash_free(res.specs);
   hash_free(res.hidden);
   return res.changed;
}
