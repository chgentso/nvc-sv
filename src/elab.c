//
//  Copyright (C) 2011-2026  Nick Gasson
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
#include "common.h"
#include "cov/cov-api.h"
#include "diag.h"
#include "eval.h"
#include "hash.h"
#include "lib.h"
#include "lint.h"
#include "lower.h"
#include "mask.h"
#include "mir/mir-unit.h"
#include "object.h"
#include "option.h"
#include "phase.h"
#include "printf.h"
#include "psl/psl-phase.h"
#include "rt/model.h"
#include "rt/structs.h"
#include "scan.h"
#include "thread.h"
#include "type.h"
#include "vhdl/vhdl-phase.h"
#include "vlog/vlog-defs.h"
#include "vlog/vlog-node.h"
#include "vlog/vlog-phase.h"
#include "vlog/vlog-util.h"

#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#define MAX_DEPTH 127    // Limited by vcode type indexes

typedef A(tree_t) tree_list_t;

typedef struct _elab_ctx elab_ctx_t;

typedef struct {
   tree_t      wrap;     // Holds the instance connected to the port
   tree_t      parent;   // Block where the connection was written
   vlog_node_t port;
   vlog_node_t value;    // NULL for implicit connection by name
   loc_t       loc;
} elab_iface_conn_t;

typedef A(elab_iface_conn_t) iface_conn_list_t;

typedef struct {
   vlog_node_t block;
   vlog_node_t body;
   tree_t      where;
} elab_gen_body_t;

typedef A(elab_gen_body_t) gen_body_list_t;

typedef struct {
   tree_t      where;
   hash_t     *parents;
   hash_t     *iface_first;
   hset_t     *iface_values;
   vlog_node_t root;
   unsigned    gen_depth;
} elab_href_ctx_t;
typedef struct _generic_list generic_list_t;

typedef struct {
   tree_t    unit;
   ident_t   entity;
   ident_t   arch;
   unsigned  nlabels;
   ident_t  *labels;
} elab_vunit_t;

typedef struct {
   const elab_vunit_t *vunit;
   unsigned            pos;
} elab_vunit_pending_t;

typedef struct {
   unsigned      count;
   elab_vunit_t *items;
} elab_vunit_list_t;

// Hierarchical references which are the subject of a call
static hset_t *call_values = NULL;
static hset_t *defparam_targets = NULL;

typedef struct {
   const ident_t *path;     // Instance names below this scope
   unsigned       npath;
   ident_t        param;
   vlog_node_t    value;
   vlog_node_t    where;
   bool          *passed;   // An instance on the path was found
   bool          *applied;  // The parameter was found
   const void    *home;     // Module containing the defparam
   unsigned       line;
} elab_defparam_t;

typedef struct _elab_ctx {
   const elab_ctx_t *parent;
   tree_t            out;
   object_t        **roots;
   unsigned          nroots;
   tree_t            inst;
   ident_t           dotted;
   ident_t           cloned;
   lib_t             library;
   jit_t            *jit;
   unit_registry_t  *registry;
   mir_context_t    *mir;
   lower_unit_t     *lowered;
   cover_data_t     *cover;
   sdf_file_t       *sdf;
   hash_t           *modcache;
   hash_t           *mracache;
   hash_t           *vucache;
   const elab_vunit_pending_t *pending;
   unsigned          npending;
   bool              via_component;
   iface_conn_list_t *iface_conns;
   gen_body_list_t  *gen_bodies;
   hash_t           *block_parents;
   hash_t           *iface_first;
   hash_t           *iface_units;
   hset_t           *iface_values;
   unsigned         *nshims;
   rt_model_t       *model;
   rt_scope_t       *scope;
   mem_pool_t       *pool;
   cover_obj_t       cscope;
   const elab_defparam_t *defparams;
   unsigned          ndefparams;
   const void       *dp_home;
   vlog_node_t       config;
   ident_t           cfg_path;
   unsigned          depth;
   unsigned          errors;
} elab_ctx_t;

typedef struct {
   tree_t  comp;
   tree_t *result;
   bool    verilog;
} synth_binding_params_t;

typedef struct _generic_list {
   generic_list_t *next;
   ident_t         name;
   char           *value;
} generic_list_t;

typedef struct {
   object_t *object;
   ghash_t  *instances;
   unsigned  count;
   unsigned  unique;
} mod_cache_t;

typedef struct _elab_instance elab_instance_t;

typedef struct _elab_instance {
   vlog_node_t      body;
   tree_t           block;
   tree_t           wrap;
   tree_t           first;
   cover_obj_t      cscope;
   bool             has_vunits;
   elab_instance_t *chain;   // Others with the same key, not cloned
} elab_instance_t;

static void elab_vhdl_block(tree_t t, const elab_ctx_t *ctx);
static void elab_vhdl_processes(tree_t t, const elab_ctx_t *ctx);
static void elab_vhdl_sub_blocks(tree_t t, const elab_ctx_t *ctx);
static void elab_verilog_sub_blocks(vlog_node_t v, const elab_ctx_t *ctx);
static void elab_verilog_processes(vlog_node_t v, const elab_ctx_t *ctx);
static void elab_decls(tree_t t, const elab_ctx_t *ctx);
static void elab_push_scope(tree_t t, elab_ctx_t *ctx);
static tree_t elab_eval_expr(tree_t t, const elab_ctx_t *ctx);
static void elab_pop_scope(elab_ctx_t *ctx);

static generic_list_t *generic_override = NULL;

static lib_t elab_find_lib(ident_t name, const elab_ctx_t *ctx)
{
   ident_t lib_name = ident_until(name, '.');
   if (lib_name == well_known(W_WORK))
      return ctx->library;
   else
      return lib_require(lib_name);
}

static tree_t elab_pick_arch(tree_t entity, const loc_t *loc,
                             const elab_ctx_t *ctx)
{
   // When an explicit architecture name is not given select the most
   // recently analysed architecture of this entity

   tree_t cached = hash_get(ctx->mracache, entity);
   if (cached != NULL)
      return cached;

   ident_t name = tree_ident(entity);
   lib_t lib = elab_find_lib(name, ctx);
   ident_t search_name =
      ident_prefix(lib_name(lib), ident_rfrom(name, '.'), '.');

   tree_t arch = find_default_arch(lib, search_name);
   if (arch == NULL)
      fatal_at(loc, "no suitable architecture for %pI", search_name);

   hash_put(ctx->mracache, entity, arch);
   return arch;
}

static uint32_t elab_hash_vhdl_generic(tree_t g, tree_t value)
{
   uint32_t h = ident_hash(tree_ident(g));

   switch (tree_class(g)) {
   case C_TYPE:
      h ^= (uintptr_t)tree_type(value);
      break;
   case C_FUNCTION:
   case C_PROCEDURE:
   case C_PACKAGE:
      h ^= (uintptr_t)tree_ref(value);
      break;
   case C_CONSTANT:
      // Only literal values are folded so instances with different
      // non-literal generic maps can be reused
      switch (tree_kind(value)) {
      case T_REF:
         {
            tree_t decl = tree_ref(value);
            if (tree_kind(decl) == T_ENUM_LIT)
               h ^= knuth_hash(tree_pos(decl));
         }
         break;
      case T_LITERAL:
         switch (tree_subkind(value)) {
         case L_PHYSICAL:
            h ^= ident_hash(tree_ident(value));
            // Fall-through
         case L_INT:
            h ^= mix_bits_64(tree_ival(value));
            break;
         case L_REAL:
            h ^= mix_bits_64(FLOAT_BITS(tree_dval(value)));
            break;
         default:
            should_not_reach_here();
         }
         break;
      default:
         break;
      }
      break;
   default:
      should_not_reach_here();
   }

   return h;
}

static uint32_t elab_hash_vhdl_inst(tree_t t)
{
   uint32_t h = 0;

   switch (tree_kind(t)) {
   case T_SPEC:
      {
         if (tree_has_value(t)) {
            tree_t bind = tree_value(t);
            h ^= elab_hash_vhdl_inst(bind);

            // Mapping of component to entity ports is significant
            const int nparams = tree_params(bind);
            for (int i = 0; i < nparams; i++) {
               tree_t m = tree_param(bind, i);
               if (tree_subkind(m) == P_POS) {
                  tree_t value = tree_value(m);
                  if (tree_kind(value) == T_REF)
                     h ^= (uintptr_t)tree_ref(value);
                  else
                     h ^= (uintptr_t)value;
               }
               else
                  h ^= (uintptr_t)m;   // Ignore for now
            }
         }

         if (tree_decls(t) > 0)
            h ^= (uintptr_t)t;   // Ignore for now
      }
      break;
   case T_INSTANCE:
      if (tree_has_spec(t))
         h ^= elab_hash_vhdl_inst(tree_spec(t));
      // Fall-through
   case T_BINDING:
      {
         tree_t unit = tree_ref(t), ent = primary_unit_of(unit);
         h ^= mix_bits_64((uintptr_t)unit);

         const int ngenmaps = tree_genmaps(t);
         for (int i = 0; i < ngenmaps; i++) {
            tree_t m = tree_genmap(t, i);
            assert(tree_subkind(m) == P_POS);

            h ^= elab_hash_vhdl_generic(tree_generic(ent, i), tree_value(m));
         }
      }
      break;
   default:
      should_not_reach_here();
   }

   return h;
}

static uint32_t elab_hash_inst(const void *key)
{
   tree_t t = tree_from_object((object_t *)key);
   if (t != NULL)
      return elab_hash_vhdl_inst(t);

   vlog_node_t v = vlog_from_object((object_t *)key);
   if (v != NULL) {
      assert(vlog_kind(v) == V_INST_LIST);

      uint32_t h = ident_hash(vlog_ident(v));

      const int nparams = vlog_params(v);
      for (int i = 0; i < nparams; i++)
         h ^= vlog_hash_node(vlog_param(v, i));

      return h;
   }

   should_not_reach_here();
}

static bool elab_cmp_vhdl_generic(tree_t g, tree_t a, tree_t b)
{
   switch (tree_class(g)) {
   case C_CONSTANT:
      return (!is_literal(a) && !is_literal(b)) || same_tree(a, b);
   case C_TYPE:
      return type_strict_eq(tree_type(a), tree_type(b));
   case C_FUNCTION:
   case C_PROCEDURE:
   case C_PACKAGE:
      return tree_ref(a) == tree_ref(b);
   default:
      should_not_reach_here();
   }
}

static bool elab_cmp_vhdl_inst(tree_t a, tree_t b)
{
   const tree_kind_t kind = tree_kind(a);
   if (kind != tree_kind(b))
      return false;

   switch (kind) {
   case T_SPEC:
      {
         tree_t a_bind = tree_has_value(a) ? tree_value(a) : NULL;
         tree_t b_bind = tree_has_value(b) ? tree_value(b) : NULL;

         if (a_bind != NULL && b_bind != NULL) {
            if (!elab_cmp_vhdl_inst(a_bind, b_bind))
               return false;

            const int nparams = tree_params(a_bind);
            if (tree_params(b_bind) != nparams)
               return false;

            for (int i = 0; i < nparams; i++) {
               tree_t ma = tree_param(a_bind, i);
               tree_t mb = tree_param(b_bind, i);

               if (tree_subkind(ma) != P_POS || tree_subkind(mb) != P_POS)
                  return false;

               tree_t a_value = tree_value(ma);
               tree_t b_value = tree_value(mb);

               if (tree_kind(a_value) != T_REF || tree_kind(b_value) != T_REF)
                  return false;
               else if (tree_ref(a_value) != tree_ref(b_value))
                  return false;
            }
         }
         else if (a_bind != NULL || b_bind != NULL)
            return false;

         if (tree_decls(a) > 0 || tree_decls(b) > 0)
            return false;

         return true;
      }
   case T_INSTANCE:
      {
         tree_t a_spec = tree_has_spec(a) ? tree_spec(a) : NULL;
         tree_t b_spec = tree_has_spec(b) ? tree_spec(b) : NULL;

         if (a_spec != NULL && b_spec != NULL) {
            if (!elab_cmp_vhdl_inst(tree_spec(a), tree_spec(b)))
               return false;
         }
         else if (a_spec != NULL || b_spec != NULL)
            return false;
      }
      // Fall-through
   case T_BINDING:
      {
         tree_t unit = tree_ref(a), ent = primary_unit_of(unit);
         if (tree_ref(b) != unit)
            return false;

         const int ngenmaps = tree_genmaps(a);
         if (tree_genmaps(b) != ngenmaps)
            return false;

         for (int i = 0; i < ngenmaps; i++) {
            tree_t ma = tree_genmap(a, i);
            tree_t mb = tree_genmap(b, i);

            assert(tree_subkind(ma) == P_POS);
            assert(tree_subkind(mb) == P_POS);

            tree_t a = tree_value(ma);
            tree_t b = tree_value(mb);

            if (!elab_cmp_vhdl_generic(tree_generic(ent, i), a, b))
               return false;
         }

         return true;
      }
   default:
      should_not_reach_here();
   }
}

static bool elab_cmp_inst(const void *a, const void *b)
{
   tree_t ta = tree_from_object((object_t *)a);
   tree_t tb = tree_from_object((object_t *)b);
   if (ta != NULL && tb != NULL)
      return elab_cmp_vhdl_inst(ta, tb);

   vlog_node_t va = vlog_from_object((object_t *)a);
   vlog_node_t vb = vlog_from_object((object_t *)b);
   if (va != NULL && vb != NULL) {
      assert(vlog_kind(va) == V_INST_LIST);
      assert(vlog_kind(vb) == V_INST_LIST);

      if (vlog_ident(va) != vlog_ident(vb))
         return false;

      const int nparams = vlog_params(va);
      if (vlog_params(vb) != nparams)
         return false;

      for (int i = 0; i < nparams; i++) {
         if (!vlog_equal_node(vlog_param(va, i), vlog_param(vb, i)))
            return false;
      }

      return true;
   }

   return false;
}

static mod_cache_t *elab_cached_module(object_t *obj, const elab_ctx_t *ctx)
{
   mod_cache_t *mc = hash_get(ctx->modcache, obj);
   if (mc != NULL)
      return mc;

   mc = pool_calloc(ctx->pool, sizeof(mod_cache_t));
   mc->object    = obj;
   mc->instances = ghash_new(4, elab_hash_inst, elab_cmp_inst);

   hash_put(ctx->modcache, obj, mc);
   return mc;
}

static bool elab_synth_binding_cb(lib_t lib, void *__ctx)
{
   synth_binding_params_t *params = __ctx;

   ident_t name = ident_prefix(lib_name(lib), tree_ident(params->comp), '.');
   object_t *obj = lib_get_generic(lib, name, NULL);

   // A Verilog module is bound by the caller
   *(params->result) = tree_from_object(obj);
   params->verilog = vlog_from_object(obj) != NULL;

   return *(params->result) == NULL && !params->verilog;
}

static type_t elab_std_ulogic_equiv(type_t type)
{
   // Types such as UNSIGNED and SIGNED have the same representation as
   // STD_ULOGIC_VECTOR so can use the same conversion functions

   if (type_is_array(type) && dimension_of(type) == 1
       && type_eq(type_elem(type), ieee_type(IEEE_STD_ULOGIC)))
      return ieee_type(IEEE_STD_ULOGIC_VECTOR);
   else
      return type;
}

static tree_t elab_std_conv(const char *func, std_type_t std, type_t actual)
{
   if (type_eq(std_type(NULL, std), actual))
      return verilog_func(ident_new(func));
   else
      return NULL;
}

static tree_t elab_to_vhdl(type_t from, type_t to, bool shim)
{
   to = elab_std_ulogic_equiv(to);

   // The BIT, BIT_VECTOR, and INTEGER conversions are only for ports
   // of the generated shim, not component ports bound to a Verilog module
   if (shim && type_eq(from, verilog_type(VERILOG_NET_VALUE))) {
      tree_t f = elab_std_conv("NVC.VERILOG.TO_VHDL_BIT(" T_NET_VALUE ")J",
                               STD_BIT, to);
      if (f == NULL)
         f = elab_std_conv("NVC.VERILOG.TO_VHDL_BOOLEAN(" T_NET_VALUE ")B",
                           STD_BOOLEAN, to);
      if (f != NULL)
         return f;
   }
   else if (shim && type_eq(from, verilog_type(VERILOG_WIRE_ARRAY))) {
      tree_t f = elab_std_conv("NVC.VERILOG.TO_VHDL_BIT_VECTOR("
                               T_WIRE_ARRAY ")Q", STD_BIT_VECTOR, to);
      if (f == NULL)
         f = elab_std_conv("NVC.VERILOG.TO_VHDL_INTEGER(" T_WIRE_ARRAY ")I",
                           STD_INTEGER, to);
      if (f != NULL)
         return f;
   }

   static struct {
      const verilog_type_t from_id;
      const ieee_type_t    to_id;
      const char *const    func;
      type_t               from;
      type_t               to;
      tree_t               decl;
   } table[] = {
      { VERILOG_LOGIC, IEEE_STD_LOGIC, "NVC.VERILOG.TO_VHDL(" T_LOGIC ")U" },
      { VERILOG_NET_VALUE, IEEE_STD_LOGIC,
        "NVC.VERILOG.TO_VHDL(" T_NET_VALUE ")U" },
      { VERILOG_WIRE_ARRAY, IEEE_STD_ULOGIC_VECTOR,
        "NVC.VERILOG.TO_VHDL(" T_WIRE_ARRAY ")Y" },
      { VERILOG_WIRE_ARRAY, IEEE_STD_LOGIC_VECTOR,
        "NVC.VERILOG.TO_VHDL(" T_WIRE_ARRAY ")Y" },
      { VERILOG_LOGIC_ARRAY, IEEE_STD_ULOGIC_VECTOR,
        "NVC.VERILOG.TO_VHDL(" T_LOGIC_ARRAY ")Y" },
      { VERILOG_LOGIC_ARRAY, IEEE_STD_LOGIC_VECTOR,
        "NVC.VERILOG.TO_VHDL(" T_LOGIC_ARRAY ")Y" },
   };

   INIT_ONCE({
         for (int i = 0; i < ARRAY_LEN(table); i++) {
            table[i].from = verilog_type(table[i].from_id);
            table[i].to   = ieee_type(table[i].to_id);
            table[i].decl = verilog_func(ident_new(table[i].func));
         }
      });

   for (int i = 0; i < ARRAY_LEN(table); i++) {
      if (type_eq(table[i].from, from) && type_eq(table[i].to, to))
         return table[i].decl;
   }

   return NULL;
}

static tree_t elab_to_verilog(type_t from, type_t to, bool shim)
{
   from = elab_std_ulogic_equiv(from);

   if (shim && type_eq(to, verilog_type(VERILOG_NET_VALUE))) {
      tree_t f = elab_std_conv("NVC.VERILOG.TO_VERILOG_BIT(J)" T_NET_VALUE,
                               STD_BIT, from);
      if (f == NULL)
         f = elab_std_conv("NVC.VERILOG.TO_VERILOG_BOOLEAN(B)" T_NET_VALUE,
                           STD_BOOLEAN, from);
      if (f != NULL)
         return f;
   }
   else if (shim && type_eq(to, verilog_type(VERILOG_WIRE_ARRAY))) {
      tree_t f = elab_std_conv("NVC.VERILOG.TO_VERILOG_BIT_VECTOR(Q)"
                               T_NET_ARRAY, STD_BIT_VECTOR, from);
      if (f == NULL)
         f = elab_std_conv("NVC.VERILOG.TO_VERILOG_INTEGER(I)" T_NET_ARRAY,
                           STD_INTEGER, from);
      if (f != NULL)
         return f;
   }

   static struct {
      const ieee_type_t    from_id;
      const verilog_type_t to_id;
      const char *const    func;
      type_t               from;
      type_t               to;
      tree_t               decl;
   } table[] = {
      { IEEE_STD_ULOGIC, VERILOG_LOGIC, "NVC.VERILOG.TO_VERILOG(U)" T_LOGIC },
      { IEEE_STD_ULOGIC, VERILOG_NET_VALUE,
        "NVC.VERILOG.TO_VERILOG(U)" T_NET_VALUE },
      { IEEE_STD_ULOGIC_VECTOR, VERILOG_LOGIC_ARRAY,
        "NVC.VERILOG.TO_VERILOG(Y)" T_LOGIC_ARRAY },
      { IEEE_STD_LOGIC_VECTOR, VERILOG_LOGIC_ARRAY,
        "NVC.VERILOG.TO_VERILOG(Y)" T_LOGIC_ARRAY },
      { IEEE_STD_ULOGIC_VECTOR, VERILOG_WIRE_ARRAY,
        "NVC.VERILOG.TO_VERILOG(Y)" T_NET_ARRAY },
      { IEEE_STD_LOGIC_VECTOR, VERILOG_WIRE_ARRAY,
        "NVC.VERILOG.TO_VERILOG(Y)" T_NET_ARRAY },
   };

   INIT_ONCE({
         for (int i = 0; i < ARRAY_LEN(table); i++) {
           table[i].from = ieee_type(table[i].from_id);
           table[i].to   = verilog_type(table[i].to_id);
           table[i].decl = verilog_func(ident_new(table[i].func));
         }
      });

   for (int i = 0; i < ARRAY_LEN(table); i++) {
      if (type_eq(table[i].from, from) && type_eq(table[i].to, to))
         return table[i].decl;
   }

   return NULL;
}

static tree_t elab_mixed_binding(tree_t comp, const elab_instance_t *ei)
{
   assert(tree_kind(comp) == T_COMPONENT);

   tree_t bind = tree_new(T_BINDING);
   tree_set_ident(bind, tree_ident(ei->wrap));
   tree_set_loc(bind, tree_loc(comp));
   tree_set_ref(bind, ei->wrap);
   tree_set_class(bind, C_ENTITY);

   const int vhdl_nports = tree_ports(comp);
   const int vlog_nports = vlog_ports(ei->body);

   LOCAL_BIT_MASK have;
   mask_init(&have, vhdl_nports);

   bool have_named = false;
   for (int i = 0; i < vlog_nports; i++) {
      vlog_node_t ref = vlog_port(ei->body, i);
      assert(vlog_kind(ref) == V_REF);

      vlog_node_t mport = vlog_ref(ref);
      assert(vlog_kind(mport) == V_PORT_DECL);

      ident_t name = vlog_ident2(mport);

      tree_t vport = tree_port(ei->block, i);
      assert(tree_ident(vport) == vlog_ident(mport));

      tree_t cport = NULL;
      for (int j = 0; j < vhdl_nports; j++) {
         tree_t pj = tree_port(comp, j);
         if (ident_casecmp(tree_ident(pj), name)) {
            cport = pj;
            mask_set(&have, j);
            break;
         }
      }

      if (cport == NULL) {
         error_at(tree_loc(comp), "missing matching VHDL port declaration for "
                  "Verilog port %pQ in component %pI", vlog_ident(mport),
                  tree_ident(comp));
         return NULL;
      }

      const v_port_kind_t mdir = vlog_subkind(mport);
      const port_mode_t cdir = tree_subkind(cport);

      if (mdir != V_PORT_INPUT && mdir != V_PORT_OUTPUT
          && mdir != V_PORT_INOUT) {
         error_at(tree_loc(cport), "only input, output, and inout ports are "
                  "supported for mixed language instantiation");
         return NULL;
      }
      else if (!(mdir == V_PORT_INPUT && cdir == PORT_IN)
               && !(mdir == V_PORT_OUTPUT && cdir == PORT_OUT)
               && !(mdir == V_PORT_INOUT && cdir == PORT_INOUT)) {
         error_at(tree_loc(cport), "VHDL port direction %s does not match "
                  "corresponding Verilog port %pQ which is declared as %s",
                  port_mode_str(cdir), vlog_ident(mport),
                  mdir == V_PORT_INPUT ? "input"
                  : mdir == V_PORT_OUTPUT ? "output" : "inout");
         return NULL;
      }

      type_t btype = tree_type(cport);
      type_t vtype = tree_type(vport);

      tree_t conv = tree_new(T_CONV_FUNC);
      tree_set_loc(conv, tree_loc(cport));

      if (mdir == V_PORT_INPUT) {
         tree_t func = elab_to_verilog(btype, vtype, false);
         if (func == NULL) {
            error_at(tree_loc(cport), "cannot connect VHDL signal with type "
                     "%pT to Verilog input port %pQ", btype,
                     vlog_ident(mport));
            return NULL;
         }

         tree_set_ref(conv, func);
         tree_set_ident(conv, tree_ident(func));
         tree_set_type(conv, type_result(tree_type(func)));
         tree_set_value(conv, make_ref(cport));

         if (have_named)
            add_param(bind, conv, P_NAMED, make_ref(vport));
         else
            add_param(bind, conv, P_POS, NULL);
      }
      else if (mdir == V_PORT_INOUT) {
         tree_t in_func = elab_to_verilog(btype, vtype, false);
         tree_t out_func = elab_to_vhdl(vtype, btype, false);
         if (in_func == NULL || out_func == NULL) {
            error_at(tree_loc(cport), "cannot connect VHDL signal with type "
                     "%pT to Verilog inout port %pQ", btype,
                     vlog_ident(mport));
            return NULL;
         }

         tree_set_ref(conv, in_func);
         tree_set_ident(conv, tree_ident(in_func));
         tree_set_type(conv, type_result(tree_type(in_func)));
         tree_set_value(conv, make_ref(cport));

         tree_t oconv = tree_new(T_CONV_FUNC);
         tree_set_loc(oconv, tree_loc(cport));
         tree_set_ref(oconv, out_func);
         tree_set_ident(oconv, tree_ident(out_func));
         tree_set_type(oconv, type_result(tree_type(out_func)));
         tree_set_value(oconv, make_ref(vport));

         add_param(bind, conv, P_NAMED, oconv);
         have_named = true;
      }
      else {
         tree_t func = elab_to_vhdl(vtype, btype, false);
         if (func == NULL) {
            error_at(tree_loc(cport), "cannot connect VHDL signal with type "
                     "%pT to Verilog output port %pQ", btype,
                     vlog_ident(mport));
            return NULL;
         }

         tree_set_ref(conv, func);
         tree_set_ident(conv, tree_ident(func));
         tree_set_type(conv, type_result(tree_type(func)));
         tree_set_value(conv, make_ref(vport));

         add_param(bind, make_ref(cport), P_NAMED, conv);
         have_named = true;
      }
   }

   for (int i = 0; i < vhdl_nports; i++) {
      if (!mask_test(&have, i)) {
         tree_t p = tree_port(comp, i);
         diag_t *d = diag_new(DIAG_ERROR, tree_loc(p));
         diag_printf(d, "port %pI not found in Verilog module %pQ",
                     tree_ident(p), vlog_ident2(ei->body));
         diag_emit(d);
      }
   }

   return bind;
}

static void elab_free_type_map(hash_t **map)
{
   if (*map != NULL)
      hash_free(*map);
}

static void elab_map_anonymous_type(type_t ctype, type_t etype, hash_t *map)
{
   // Anonymous types in the component and entity correspond if they
   // are the implicit generic types of ports with the same name

   if (type_kind(ctype) != T_GENERIC || type_has_ident(ctype))
      return;
   else if (type_kind(etype) != T_GENERIC || type_has_ident(etype))
      return;
   else if (type_subkind(ctype) != type_subkind(etype))
      return;

   hash_put(map, ctype, etype);

   if (type_subkind(ctype) == GTYPE_ARRAY) {
      elab_map_anonymous_type(type_elem(ctype), type_elem(etype), map);

      const int nindex = type_indexes(ctype);
      if (type_indexes(etype) == nindex) {
         for (int i = 0; i < nindex; i++)
            elab_map_anonymous_type(type_index(ctype, i),
                                    type_index(etype, i), map);
      }
   }
}

static unsigned  elab_nblack;
static hset_t   *elab_black_seen;

unsigned elab_black_boxes(void)
{
   return elab_nblack;
}

static bool elab_black_box_allowed(ident_t lib)
{
   return opt_black_box_allowed(istr(lib));
}

static void elab_black_box(const loc_t *loc, ident_t unit, ident_t lib)
{
   // Instances of a missing unit are black boxes with --black-box-missing:
   // this is reported once for each unit but every instance counts
   elab_nblack++;

   if (elab_black_seen == NULL)
      elab_black_seen = hset_new(16);

   if (hset_contains(elab_black_seen, unit))
      return;

   hset_insert(elab_black_seen, unit);

   note_at(loc, "black box for missing unit %pI (library %pI)", unit, lib);
}

static tree_t elab_default_binding(tree_t inst, const elab_ctx_t *ctx)
{
   // Default binding indication is described in LRM 93 section 5.2.2

   tree_t comp = tree_ref(inst);

   ident_t full_i = tree_ident(comp);
   ident_t lib_i = ident_until(full_i, '.');

   lib_t lib = NULL;
   bool synth_binding = true;
   if (lib_i == full_i) {
      lib    = ctx->library;
      full_i = ident_prefix(lib_name(lib), full_i, '.');
   }
   else {
      synth_binding = false;
      lib = elab_find_lib(lib_i, ctx);

      // Strip out the component package name, if any
      full_i = ident_prefix(lib_i, ident_rfrom(full_i, '.'), '.');
   }

   object_t *obj = lib_get_generic(lib, full_i, NULL);

   if (vlog_from_object(obj) != NULL)
      return NULL;  // Verilog binding handled separately

   tree_t entity = tree_from_object(obj);

   if (entity == NULL && synth_binding) {
      // This is not correct according to the LRM but matches the
      // behaviour of many synthesis tools
      synth_binding_params_t params = {
         .comp     = comp,
         .result   = &entity
      };
      lib_for_all(elab_synth_binding_cb, &params);

      if (entity == NULL && params.verilog)
         return NULL;   // Verilog binding handled separately
   }

   if (entity == NULL) {
      // With --black-box-missing this is reported where the instance is
      // elaborated, as the unit may be a Verilog module
      if (!elab_black_box_allowed(lib_name(lib)))
         warn_at(tree_loc(inst), "cannot find entity for component %pI "
                 "without binding indication", tree_ident(comp));
      return NULL;
   }

   tree_t arch = elab_pick_arch(entity, tree_loc(comp), ctx);

   // Check entity is compatible with component declaration

   tree_t bind = tree_new(T_BINDING);
   tree_set_ident(bind, tree_ident(arch));
   tree_set_loc(bind, tree_loc(arch));
   tree_set_ref(bind, arch);
   tree_set_class(bind, C_ENTITY);

   const int c_ngenerics = tree_generics(comp);
   const int e_ngenerics = tree_generics(entity);

   LOCAL_BIT_MASK ghave;
   mask_init(&ghave, c_ngenerics);
   mask_setall(&ghave);

   // Map from anonymous types in the component to those in the entity
   __attribute__((cleanup(elab_free_type_map))) hash_t *tmap = NULL;

   for (int i = 0; i < e_ngenerics; i++) {
      tree_t eg = tree_generic(entity, i);

      tree_t match = NULL;
      for (int j = 0; j < c_ngenerics; j++) {
         tree_t cg = tree_generic(comp, j);
         if (!ident_casecmp(tree_ident(eg), tree_ident(cg)))
            continue;
         else if ((tree_flags(eg) & TREE_F_PREDEFINED)
                  && (!mask_test(&ghave, j)
                      || !type_eq_map(tree_type(cg), tree_type(eg), tmap)))
            continue;   // Implicit operator for a different generic type

         match = cg;
         mask_clear(&ghave, j);
         break;
      }

      tree_t value;
      if (match != NULL) {
         const class_t class = tree_class(eg);

         if (class != tree_class(match)) {
            diag_t *d = diag_new(DIAG_ERROR, tree_loc(inst));
            diag_printf(d, "generic %pI in component %pI has class %s which is "
                        "incompatible with class %s in entity %pI",
                        tree_ident(match), tree_ident(comp),
                        class_str(tree_class(match)), class_str(class),
                        tree_ident(entity));
            diag_hint(d, tree_loc(match), "declaration of generic %pI in "
                      "component", tree_ident(match));
            diag_hint(d, tree_loc(eg), "declaration of generic %pI in entity",
                      tree_ident(eg));
            diag_emit(d);
            return NULL;
         }
         else if (class == C_PACKAGE) {
            value = tree_new(T_REF);
            tree_set_ident(value, tree_ident(match));
            tree_set_ref(value, match);
         }
         else {
            type_t ctype = tree_type(match);
            type_t etype = tree_type(eg);

            if (class == C_TYPE) {
               if (tmap == NULL)
                  tmap = hash_new(16);
               elab_map_anonymous_type(ctype, etype, tmap);
            }

            if (!type_eq_map(ctype, etype, tmap)) {
               diag_t *d = diag_new(DIAG_ERROR, tree_loc(inst));
               diag_printf(d, "generic %pI in component %pI has type %pT "
                           "which is incompatible with type %pT in entity %pI",
                           tree_ident(match), tree_ident(comp), ctype, etype,
                           tree_ident(entity));
               diag_hint(d, tree_loc(match), "declaration of generic %pI in "
                         "component", tree_ident(match));
               diag_hint(d, tree_loc(eg), "declaration of generic %pI in "
                         "entity", tree_ident(eg));
               diag_emit(d);
               return NULL;
            }

            value = make_ref(match);
         }
      }
      else if (tree_has_value(eg)) {
         tree_t def = tree_value(eg);
         if (is_literal(def))
            value = def;
         else {
            tree_t open = tree_new(T_OPEN);
            tree_set_loc(open, tree_loc(eg));
            tree_set_type(open, tree_type(eg));

            value = open;
         }
      }
      else {
         diag_t *d = diag_new(DIAG_ERROR, tree_loc(inst));
         diag_printf(d, "generic %pI in entity %pI without a default value "
                     "has no corresponding generic in component %pI",
                     tree_ident(eg), tree_ident(entity), tree_ident(comp));
         diag_hint(d, tree_loc(eg), "declaration of generic %pI in entity",
                   tree_ident(eg));
         diag_emit(d);
         return NULL;
      }

      tree_t map = tree_new(T_PARAM);
      tree_set_loc(map, tree_loc(inst));
      tree_set_value(map, value);
      tree_set_subkind(map, P_POS);
      tree_set_pos(map, i);

      tree_add_genmap(bind, map);
   }

   const ssize_t gmissing = mask_ffs(&ghave);
   if (gmissing != -1) {
      tree_t g = tree_generic(comp, gmissing);
      diag_t *d = diag_new(DIAG_ERROR, tree_loc(g));
      diag_printf(d, "generic %pI not found in entity %pI",
                  tree_ident(g), tree_ident(entity));
      diag_hint(d, tree_loc(inst), "while elaborating default binding for %pI",
                tree_ident(inst));
      diag_emit(d);
      return NULL;
   }

   const int c_nports = tree_ports(comp);
   const int e_nports = tree_ports(entity);

   LOCAL_BIT_MASK phave;
   mask_init(&phave, c_nports);
   mask_setall(&phave);

   for (int i = 0; i < e_nports; i++) {
      tree_t ep = tree_port(entity, i);

      tree_t match = NULL;
      for (int j = 0; j < c_nports; j++) {
         tree_t cp = tree_port(comp, j);
         if (ident_casecmp(tree_ident(ep), tree_ident(cp))) {
            match = cp;
            mask_clear(&phave, j);
            break;
         }
      }

      tree_t value;
      if (match != NULL) {
         type_t ctype = tree_type(match);
         type_t etype = tree_type(ep);
         if (!type_eq_map(ctype, etype, tmap)) {
            diag_t *d = diag_new(DIAG_ERROR, tree_loc(inst));
            diag_printf(d, "port %pI in component %pI has type %pT which "
                        "is incompatible with type %pT in entity %pI",
                        tree_ident(match), tree_ident(comp), ctype, etype,
                        tree_ident(entity));
            diag_hint(d, tree_loc(match), "declaration of port %pI in "
                      "component", tree_ident(match));
            diag_hint(d, tree_loc(ep), "declaration of port %pI in entity",
                      tree_ident(ep));
            diag_emit(d);
            return NULL;
         }

         value = make_ref(match);
      }
      else {
         const bool open_ok =
            tree_has_value(ep)
            || (tree_subkind(ep) == PORT_OUT
                && !type_is_unconstrained(tree_type(ep)));

          if (open_ok) {
             tree_t open = tree_new(T_OPEN);
             tree_set_loc(open, tree_loc(ep));
             tree_set_type(open, tree_type(ep));

             value = open;
          }
          else {
            diag_t *d = diag_new(DIAG_ERROR, tree_loc(inst));
            diag_printf(d, "port %pI in entity %pI without a default value "
                        "has no corresponding port in component %pI",
                        tree_ident(ep), tree_ident(entity), tree_ident(comp));
            diag_hint(d, tree_loc(ep), "port %pI declared here",
                      tree_ident(ep));
            diag_emit(d);
            return NULL;
         }
      }

      add_param(bind, value, P_POS, NULL);
   }

   const ssize_t pmissing = mask_ffs(&phave);
   if (pmissing != -1) {
      tree_t p = tree_port(comp, pmissing);
      diag_t *d = diag_new(DIAG_ERROR, tree_loc(p));
      diag_printf(d, "port %pI not found in entity %pI",
                  tree_ident(p), tree_ident(entity));
      diag_hint(d, tree_loc(inst), "while elaborating default binding for %pI",
                tree_ident(inst));
      diag_emit(d);
      return NULL;
   }

   return bind;
}

static void elab_write_generic(text_buf_t *tb, tree_t value)
{
   switch (tree_kind(value)) {
   case T_LITERAL:
      switch (tree_subkind(value)) {
      case L_INT:  tb_printf(tb, "%"PRIi64, tree_ival(value)); break;
      case L_REAL: tb_printf(tb, "%lf", tree_dval(value)); break;
      case L_PHYSICAL:
         tb_printf(tb, "%"PRIi64" %pI", tree_ival(value),
                   tree_ident(value));
         break;
      }
      break;
   case T_STRING:
      {
         tb_printf(tb, "\"");
         const int nchars = tree_chars(value);
         for (int i = 0; i < nchars; i++)
            tb_append(tb, ident_char(tree_ident(tree_char(value, i)), 1));
         tb_printf(tb, "\"");
      }
      break;
   case T_AGGREGATE:
      {
         tb_append(tb, '(');
         const int nassocs = tree_assocs(value);
         for (int i = 0; i < nassocs; i++) {
            if (i > 0) tb_cat(tb, ", ");
            elab_write_generic(tb, tree_value(tree_assoc(value, i)));
         }
         tb_append(tb, ')');
      }
      break;
   case T_REF:
      if (is_subprogram(tree_ref(value)))
         tb_printf(tb, "%pT", tree_type(value));
      else
         tb_printf(tb, "%pI", tree_ident(value));
      break;
   case T_TYPE_CONV:
   case T_QUALIFIED:
      elab_write_generic(tb, tree_value(value));
      break;
   case T_TYPE_REF:
      tb_printf(tb, "%pT", tree_type(value));
      break;
   case T_OPEN:
      tb_cat(tb, "OPEN");
      break;
   default:
      tb_printf(tb, "...");
      DEBUG_ONLY(tb_cat(tb, tree_kind_str(tree_kind(value))));
   }
}

static void elab_hint_fn(diag_t *d, void *arg)
{
   tree_t t = arg;

   diag_hint(d, tree_loc(t), "while elaborating instance %pI",
             tree_ident(t));

   tree_t unit = tree_ref(t);
   const tree_kind_t kind = tree_kind(unit);
   if (kind == T_CONFIGURATION || kind == T_ARCH)
      unit = tree_primary(unit);

   const int ngenerics = tree_genmaps(t);
   for (int i = 0; i < ngenerics; i++) {
      tree_t p = tree_genmap(t, i);
      ident_t name = NULL;
      switch (tree_subkind(p)) {
      case P_POS:
         name = tree_ident(tree_generic(unit, tree_pos(p)));
         break;
      case P_NAMED:
         name = tree_ident(tree_name(p));
         break;
      default:
         continue;
      }

      LOCAL_TEXT_BUF tb = tb_new();
      elab_write_generic(tb, tree_value(p));
      diag_hint(d, NULL, "generic %pI => %s", name, tb_get(tb));
   }
}

static bool elab_port_value_is_variable(vlog_node_t value)
{
   switch (vlog_kind(value)) {
   case V_BIT_SELECT:
   case V_PART_SELECT:
   case V_MEMBER_REF:
      return elab_port_value_is_variable(vlog_value(value));
   case V_CONCAT:
      {
         const int nparams = vlog_params(value);
         for (int i = 0; i < nparams; i++) {
            if (!elab_port_value_is_variable(vlog_param(value, i)))
               return false;
         }

         return nparams > 0 && !vlog_has_value(value);
      }
   case V_REF:
   case V_HIER_REF:
      {
         if (!vlog_has_ref(value))
            return false;

         const vlog_kind_t kind = vlog_kind(vlog_ref(value));
         return kind == V_VAR_DECL || kind == V_PORT_DECL;
      }
   default:
      return false;
   }
}

static bool elab_port_drives_variable(vlog_node_t value)
{
   // System Verilog allows an output port to drive a variable.  This is
   // only allowed here for types imported from a VHDL package and
   // hierarchical references which are bound later.

   switch (vlog_kind(value)) {
   case V_HIER_REF:
      return !vlog_has_ref(value);
   case V_BIT_SELECT:
   case V_PART_SELECT:
   case V_MEMBER_REF:
      return elab_port_drives_variable(vlog_value(value));
   case V_CONCAT:
      {
         const int nparams = vlog_params(value);
         for (int i = 0; i < nparams; i++) {
            vlog_node_t p = vlog_param(value, i);
            if (!elab_port_drives_variable(p) && !elab_port_value_is_variable(p)
                && !vlog_is_net(p))
               return false;
         }

         return nparams > 0 && !vlog_has_value(value);
      }
   case V_REF:
      {
         vlog_node_t type = vlog_get_type(value);
         if (type == NULL)
            return false;
         else if (vlog_kind(type) == V_DATA_TYPE)
            return vlog_has_type(type);   // Packed array of structures
         else
            return vlog_kind(type) == V_STRUCT_DECL
               && (vlog_flags(type) & VLOG_F_PACKED);
      }
   default:
      return false;
   }
}

static void elab_ports(tree_t entity, tree_t bind, const elab_ctx_t *ctx)
{
   const int nports = tree_ports(entity);
   const int nparams = tree_params(bind);
   bool have_named = false;

   for (int i = 0; i < nports; i++) {
      tree_t p = tree_port(entity, i), map = NULL;
      ident_t pname = tree_ident(p);

      if (i < nparams && !have_named) {
         tree_t m = tree_param(bind, i);
         if (tree_subkind(m) == P_POS) {
            assert(tree_pos(m) == i);
            tree_add_param(ctx->out, m);
            map = m;
         }
      }

      if (map == NULL) {
         for (int j = 0; j < nparams; j++) {
            tree_t m = tree_param(bind, j);
            if (tree_subkind(m) == P_NAMED) {
               tree_t name = tree_name(m), ref;
               bool is_conv = false;

               switch (tree_kind(name)) {
               case T_TYPE_CONV:
               case T_CONV_FUNC:
                  is_conv = true;
                  ref = name_to_ref(tree_value(name));
                  break;
               default:
                  ref = name_to_ref(name);
                  break;
               }
               assert(ref != NULL);

               if (!ident_casecmp(tree_ident(ref), pname))
                  continue;

               map = tree_new(T_PARAM);
               tree_set_loc(map, tree_loc(m));
               tree_set_value(map, tree_value(m));

               tree_add_param(ctx->out, map);

               if (!have_named && !is_conv && ref == name) {
                  tree_set_subkind(map, P_POS);
                  tree_set_pos(map, i);
                  break;
               }
               else {
                  tree_set_subkind(map, P_NAMED);
                  tree_set_name(map, change_ref(tree_name(m), p));
                  have_named = true;
               }
            }
         }
      }

      if (map == NULL) {
         map = tree_new(T_PARAM);
         tree_set_loc(map, tree_loc(p));

         if (have_named) {
            tree_set_subkind(map, P_NAMED);
            tree_set_name(map, make_ref(p));
         }
         else {
            tree_set_subkind(map, P_POS);
            tree_set_pos(map, i);
         }

         tree_t open = tree_new(T_OPEN);
         tree_set_type(open, tree_type(p));
         tree_set_loc(open, tree_loc(p));

         tree_set_value(map, open);

         tree_add_param(ctx->out, map);
      }

      tree_add_port(ctx->out, p);
   }
}

static tree_t elab_parse_generic_string(tree_t generic, const char *str)
{
   type_t type = tree_type(generic);

   parsed_value_t value;
   if (!parse_value(type, str, &value))
      fatal("failed to parse \"%s\" as type %pT for generic %pI",
            str, type, tree_ident(generic));

   if (type_is_enum(type)) {
      type_t base = type_base_recur(type);
      tree_t lit = type_enum_literal(base, value.integer);

      tree_t result = tree_new(T_REF);
      tree_set_type(result, type);
      tree_set_ident(result, ident_new(str));
      tree_set_ref(result, lit);
      tree_set_loc(result, tree_loc(generic));

      return result;
   }
   else if (type_is_integer(type)) {
      tree_t result = tree_new(T_LITERAL);
      tree_set_subkind(result, L_INT);
      tree_set_type(result, type);
      tree_set_ival(result, value.integer);
      tree_set_loc(result, tree_loc(generic));

      return result;
   }
   else if (type_is_real(type)) {
      tree_t result = tree_new(T_LITERAL);
      tree_set_subkind(result, L_REAL);
      tree_set_type(result, type);
      tree_set_dval(result, value.real);
      tree_set_loc(result, tree_loc(generic));

      return result;
   }
   else if (type_is_physical(type)) {
      tree_t result = tree_new(T_LITERAL);
      tree_set_subkind(result, L_PHYSICAL);
      tree_set_type(result, type);
      tree_set_ival(result, value.integer);
      tree_set_loc(result, tree_loc(generic));
      tree_set_ident(result, tree_ident(type_unit(type, 0)));

      return result;
   }
   else if (type_is_character_array(type)) {
      tree_t t = tree_new(T_STRING);
      tree_set_loc(t, tree_loc(generic));

      type_t elem = type_base_recur(type_elem(type));
      for (int i = 0; i < value.enums->count; i++) {
         tree_t lit = type_enum_literal(elem, value.enums->values[i]);

         tree_t ref = tree_new(T_REF);
         tree_set_ident(ref, tree_ident(lit));
         tree_set_ref(ref, lit);
         tree_add_char(t, ref);
      }
      free(value.enums);

      tree_set_type(t, subtype_for_string(t, type));
      return t;
   }
   else
      fatal("cannot override generic %pI of type %pT", tree_ident(generic),
            type);
}

static tree_t elab_find_generic_override(tree_t g, const elab_ctx_t *ctx)
{
   if (generic_override == NULL)
      return NULL;

   ident_t name = tree_ident(g);

   generic_list_t **it, *tmp;
   for (it = &generic_override;
        *it && !ident_casecmp((*it)->name, name);
        it = &((*it)->next))
      ;

   if (*it == NULL)
      return NULL;

   tree_t value = elab_parse_generic_string(g, (*it)->value);

   *it = (tmp = (*it))->next;
   free(tmp->value);
   free(tmp);

   return value;
}

static tree_t elab_override_instance_generics(tree_t t, const elab_ctx_t *ctx)
{
   if (generic_override == NULL)
      return t;

   ident_t prefix = tree_ident(t);
   for (const elab_ctx_t *e = ctx; e->inst; e = e->parent)
      prefix = ident_prefix(tree_ident(e->inst), prefix, '.');

   tree_t unit = tree_ref(t), entity = primary_unit_of(unit);

   const int ngenerics = tree_generics(entity);
   assert(tree_genmaps(t) == ngenerics);

   tree_t new = tree_new(T_INSTANCE);
   tree_set_loc(new, tree_loc(t));
   tree_set_ref(new, unit);
   tree_set_class(new, tree_class(t));
   tree_set_ident(new, tree_ident(t));
   tree_set_ident2(new, tree_ident2(t));

   for (int i = 0; i < ngenerics; i++) {
      tree_t g = tree_generic(entity, i);
      tree_t m = tree_genmap(t, i);
      assert(tree_subkind(m) == P_POS);
      assert(tree_pos(m) == i);

      ident_t qual = ident_prefix(prefix, tree_ident(g), '.');

      generic_list_t **it, *tmp;
      for (it = &generic_override;
           *it && !ident_casecmp((*it)->name, qual);
           it = &((*it)->next))
         ;

      if (*it == NULL)
         tree_add_genmap(new, m);
      else {
         tree_t m2 = tree_new(T_PARAM);
         tree_set_subkind(m2, P_POS);
         tree_set_pos(m2, i);
         tree_set_value(m2, elab_parse_generic_string(g, (*it)->value));

         tree_add_genmap(new, m2);

         *it = (tmp = (*it))->next;
         free(tmp->value);
         free(tmp);
      }
   }

   const int nparams = tree_params(t);
   for (int i = 0; i < nparams; i++)
      tree_add_param(new, tree_param(t, i));

   return new;
}

static void elab_generics(tree_t entity, tree_t bind, elab_ctx_t *ctx)
{
   const int ngenerics = tree_generics(entity);
   const int ngenmaps = tree_genmaps(bind);

   for (int i = 0; i < ngenerics; i++) {
      tree_t g = tree_generic(entity, i);
      tree_add_generic(ctx->out, g);

      tree_t map = NULL;
      if (i < ngenmaps) {
         map = tree_genmap(bind, i);
         assert(tree_subkind(map) == P_POS);
         assert(tree_pos(map) == i);
      }
      else if (tree_has_value(g)) {
         map = tree_new(T_PARAM);
         tree_set_loc(map, tree_loc(g));
         tree_set_subkind(map, P_POS);
         tree_set_pos(map, i);
         tree_set_value(map, tree_value(g));
      }

      if (map == NULL) {
         error_at(tree_loc(bind), "missing value for generic %pI with no "
                  "default", tree_ident(g));
         continue;
      }

      tree_add_genmap(ctx->out, map);
   }
}

static void elab_map_generic_type(type_t generic, type_t actual, hash_t *map)
{
   assert(type_kind(generic) == T_GENERIC);

   switch (type_subkind(generic)) {
   case GTYPE_ARRAY:
      {
         type_t gelem = type_elem(generic);
         if (type_kind(gelem) == T_GENERIC && !type_has_ident(gelem))
            elab_map_generic_type(gelem, type_elem(actual), map);

         const int ndims = type_indexes(generic);
         for (int i = 0; i < ndims; i++) {
            type_t index = type_index(generic, i);
            if (type_kind(index) == T_GENERIC && !type_has_ident(index))
               elab_map_generic_type(index, index_type_of(actual, i), map);
         }
      }
      break;
   }

   hash_put(map, generic, actual);
}

static void elab_map_generic_package(tree_t generic, tree_t actual, hash_t *map)
{
   tree_t formal = tree_ref(tree_value(generic));

   const int ndecls = tree_decls(formal);
   for (int i = 0; i < ndecls; i++) {
      tree_t gd = tree_decl(formal, i);
      tree_t ad = tree_decl(actual, i);
      assert(tree_kind(gd) == tree_kind(ad));

      hash_put(map, gd, ad);

      if (is_type_decl(gd))
         hash_put(map, tree_type(gd), tree_type(ad));
   }

   const int ngenerics = tree_generics(formal);
   for (int i = 0; i < ngenerics; i++) {
      tree_t fg = tree_generic(formal, i);
      tree_t ag = tree_generic(actual, i);

      tree_t m = tree_genmap(actual, i);
      assert(tree_subkind(m) == P_POS);

      tree_t value = tree_value(m);

      switch (tree_class(fg)) {
      case C_FUNCTION:
      case C_PROCEDURE:
         // Get the actual subprogram from the generic map
         assert(tree_kind(value) == T_REF);
         hash_put(map, fg, tree_ref(value));
         break;
      case C_TYPE:
         elab_map_generic_type(tree_type(fg), tree_type(ag), map);
         break;
      case C_CONSTANT:
         if (is_literal(value)) {
            hash_put(map, fg, value);
            hash_put(map, ag, value);
         }
         else if (tree_kind(value) == T_REF) {
            tree_t ref = tree_ref(value);
            hash_put(map, fg, ref);
            hash_put(map, ag, ref);
         }
         else {
            hash_put(map, fg, ag);
         }
         break;
      case C_PACKAGE:
         elab_map_generic_package(fg, tree_ref(value), map);
         break;
      default:
         hash_put(map, fg, ag);
         break;
      }
   }

   hash_put(map, generic, actual);
}

static void elab_fold_generics(tree_t b, const elab_ctx_t *ctx)
{
   assert(tree_kind(b) == T_BLOCK);

   const int ngenerics = tree_generics(b);
   assert(tree_genmaps(b) == ngenerics);

   hash_t *map = NULL, *fixup = NULL;

   for (int i = 0; i < ngenerics; i++) {
      tree_t g = tree_generic(b, i);

      tree_t m = tree_genmap(b, i);
      assert(tree_subkind(m) == P_POS);

      tree_t value = tree_value(m);

      const class_t class = tree_class(g);
      if (fixup == NULL && class != C_CONSTANT)
         fixup = hash_new(ngenerics * 2);
      else if (map == NULL && class == C_CONSTANT)
         map = hash_new(ngenerics * 2);

      switch (class) {
      case C_CONSTANT:
         if (is_literal(value)) {
            // These values can be safely substituted for all references
            // to the generic name
            hash_put(map, g, value);
         }
         break;

      case C_TYPE:
         elab_map_generic_type(tree_type(g), tree_type(value), fixup);
         break;

      case C_PACKAGE:
         elab_map_generic_package(g, tree_ref(value), fixup);
         break;

      case C_FUNCTION:
      case C_PROCEDURE:
         hash_put(fixup, g, tree_ref(value));
         break;

      default:
         should_not_reach_here();
      }
   }

   if (fixup != NULL)
      vhdl_instance_fixup(b, fixup);

   simplify_global(b, map, ctx->jit, ctx->registry, ctx->mir);

   hash_free(map);
   hash_free(fixup);
}

static void elab_context(tree_t t)
{
   const int nctx = tree_contexts(t);
   for (int i = 0; i < nctx; i++) {
      // Make sure any referenced libraries are loaded to allow synth
      // binding to search for entities in them
      tree_t c = tree_context(t, i);
      if (tree_kind(c) != T_LIBRARY)
         continue;
      else if (lib_find(tree_ident(c)) == NULL
               && opt_black_box_allowed(istr(tree_ident(c))))
         continue;   // Its units are black boxes
      else
         lib_require(tree_ident(c));
   }
}

static void elab_inherit_context(elab_ctx_t *ctx, const elab_ctx_t *parent)
{
   ctx->parent   = parent;
   ctx->jit      = parent->jit;
   ctx->registry = parent->registry;
   ctx->mir      = parent->mir;
   ctx->roots    = parent->roots;
   ctx->nroots   = parent->nroots;
   ctx->dotted   = ctx->dotted ?: parent->dotted;
   ctx->library  = ctx->library ?: parent->library;
   ctx->out      = ctx->out ?: parent->out;
   ctx->cover    = parent->cover;
   ctx->sdf      = parent->sdf;
   ctx->inst     = ctx->inst ?: parent->inst;
   ctx->modcache = parent->modcache;
   ctx->mracache = parent->mracache;
   ctx->vucache  = parent->vucache;
   ctx->iface_conns = parent->iface_conns;
   ctx->gen_bodies = parent->gen_bodies;
   ctx->block_parents = parent->block_parents;
   ctx->iface_first = parent->iface_first;
   ctx->iface_units = parent->iface_units;
   ctx->iface_values = parent->iface_values;
   ctx->nshims   = parent->nshims;
   ctx->depth    = parent->depth + 1;
   ctx->dp_home  = parent->dp_home;
   ctx->config   = parent->config;
   ctx->model    = parent->model;
   ctx->errors   = error_count();
   ctx->pool     = parent->pool;
   ctx->cscope   = parent->cscope;
}

static bool elab_new_errors(const elab_ctx_t *ctx)
{
   return error_count() - ctx->errors;
}

static void elab_lower(tree_t b, elab_ctx_t *ctx)
{
   tree_t hier = tree_decl(b, 0);
   assert(tree_kind(hier) == T_HIER);

   if (tree_subkind(hier) == T_VERILOG)
      vlog_lower_block(ctx->mir, ctx->parent->cloned ?: ctx->parent->dotted, b);
   else {
      ctx->lowered = lower_instance(ctx->registry, ctx->parent->lowered,
                                    ctx->cover, ctx->cscope, b);

#ifdef DEBUG
      if (ctx->cloned != NULL) {
         // Cloned blocks must have identical layout
         mir_unit_t *new = mir_get_unit(ctx->mir, ctx->dotted);
         mir_unit_t *orig = mir_get_unit(ctx->mir, ctx->cloned);
         mir_compare_layout(new, orig);
      }
#endif
   }

   if (ctx->inst != NULL)
      diag_add_hint_fn(elab_hint_fn, ctx->inst);

   ctx->scope = create_scope(ctx->model, b, ctx->parent->scope);

   if (ctx->inst != NULL)
      diag_remove_hint_fn(elab_hint_fn, ctx->inst);
}

static bool elab_can_clone_instance(elab_instance_t *ei, const elab_ctx_t *ctx)
{
   if (ei == NULL)
      return false;
   else if (cover_is_null(ei->cscope) && cover_is_null(ctx->cscope))
      return true;

   return cover_compatible_spec(ctx->cover, ei->cscope, ctx->cscope);
}

static bool elab_port_name_eq(ident_t a, ident_t b, bool nocase)
{
   return nocase ? ident_casecmp(a, b) : a == b;
}

static vlog_node_t elab_array_iface_value(vlog_node_t inst, vlog_node_t value);
static vlog_node_t elab_iface_element(vlog_node_t value, int pos, int count);
static bool elab_instance_array_bounds(vlog_node_t inst, int64_t *left,
                                       int64_t *right);

static void elab_verilog_iface_port(vlog_node_t inst, vlog_node_t port,
                                    vlog_node_t wildcard, bit_mask_t *used,
                                    bool nocase, const elab_ctx_t *ctx)
{
   ident_t name = vlog_ident(port);

   vlog_node_t conn = NULL;
   const int pos = vlog_subkind(port), nparams = vlog_params(inst);
   if (pos < nparams) {
      vlog_node_t c = vlog_param(inst, pos);
      if (!(vlog_flags(c) & VLOG_F_WILDCARD) && !vlog_has_ident(c)) {
         conn = c;
         mask_set(used, pos);
      }
   }

   for (int i = 0; conn == NULL && i < nparams; i++) {
      vlog_node_t c = vlog_param(inst, i);
      if (vlog_has_ident(c)
          && elab_port_name_eq(vlog_ident(c), name, nocase)) {
         conn = c;
         mask_set(used, i);
      }
   }

   if ((conn == NULL && wildcard == NULL)
       || (conn != NULL && !vlog_has_value(conn))) {
      diag_t *d = diag_new(DIAG_ERROR, vlog_loc(inst));
      diag_printf(d, "interface port %pQ must be connected", name);
      diag_hint(d, vlog_loc(port), "%pQ declared here", name);
      diag_emit(d);
      return;
   }

   vlog_node_t value = conn != NULL
      ? elab_array_iface_value(inst, vlog_value(conn)) : NULL;

   int64_t left = 0, right = 0;
   const int count = vlog_ranges(port) > 0
      && elab_instance_array_bounds(port, &left, &right)
      ? llabs(left - right) + 1 : 0;

   for (int i = 0; i < MAX(count, 1); i++) {
      ident_t id = name;
      vlog_node_t elem = value;
      if (count > 0) {
         id = ident_sprintf("%s[%"PRIi64"]", istr(name),
                            left >= right ? left - i : left + i);
         if (value != NULL) {
            elem = elab_iface_element(value, i, count);
            if (elem == value) {
               error_at(vlog_loc(value), "interface port array %pQ must be "
                        "connected to an array of interfaces", name);
               return;
            }
         }
      }

      tree_t wrap = tree_new(T_VERILOG);
      tree_set_ident(wrap, id);
      tree_set_vlog(wrap, port);
      tree_set_loc(wrap, vlog_loc(port));

      tree_add_decl(ctx->out, wrap);

      assert(ctx->parent != NULL);

      elab_iface_conn_t c = {
         .wrap   = wrap,
         .parent = ctx->parent->out,
         .port   = port,
         .value  = elem,
         .loc    = *vlog_loc(conn ?: inst),
      };
      APUSH(*ctx->iface_conns, c);
   }
}

static bool elab_instance_array_bounds(vlog_node_t inst, int64_t *left,
                                       int64_t *right)
{
   vlog_node_t r = vlog_range(inst, 0);
   if (vlog_get_const(vlog_left(r), left) && vlog_get_const(vlog_right(r), right))
      return true;

   error_at(vlog_loc(inst), "bounds of instance array %pQ must be constant "
            "expressions", vlog_ident(inst));
   return false;
}

static vlog_node_t elab_array_element(vlog_node_t inst, int pos, int count,
                                      int64_t index)
{
   // A copy of an element of an instance array which remembers its position

   vlog_node_t e = vlog_new(V_MOD_INST);
   vlog_set_ident(e, ident_sprintf("%s[%"PRIi64"]", istr(vlog_ident(inst)),
                                   index));
   vlog_set_loc(e, vlog_loc(inst));
   vlog_set_ival(e, ((int64_t)count << 32) | pos);

   const int nparams = vlog_params(inst);
   for (int i = 0; i < nparams; i++)
      vlog_add_param(e, vlog_param(inst, i));

   return e;
}

static vlog_node_t elab_number(int64_t value, const loc_t *loc)
{
   vlog_node_t n = vlog_new(V_NUMBER);
   vlog_set_number(n, number_from_int(value));
   vlog_set_loc(n, loc);
   return n;
}

static vlog_node_t elab_array_port_value(vlog_node_t inst, vlog_node_t port,
                                         vlog_node_t value)
{
   // A connection as wide as the whole array is divided between the
   // elements, otherwise every element is connected to the same value

   const int count = vlog_ival(inst) >> 32, pos = vlog_ival(inst) & 0xffffffff;
   const unsigned pw = vlog_width(port), vw = vlog_width(value);

   if (count < 2 || vw != pw * count)
      return value;

   if (vlog_kind(value) == V_CONCAT) {
      int64_t repeat = 1;
      if (vlog_has_value(value) && !vlog_get_const(vlog_value(value), &repeat))
         repeat = 0;

      unsigned offset = 0;
      const int nparts = vlog_params(value);
      for (int64_t r = 0; r < repeat; r++) {
         for (int i = 0; i < nparts; i++) {
            vlog_node_t part = vlog_param(value, i);
            if (offset == pw * pos && vlog_width(part) == pw)
               return part;
            offset += vlog_width(part);
         }
      }

      error_at(vlog_loc(value), "sorry, the parts of this concatenation do "
               "not match the ports of the instance array");
      return value;
   }

   if (vlog_kind(value) == V_NUMBER && pw <= 64) {
      jit_scalar_t abits = { .integer = 0 }, bbits = { .integer = 0 };
      number_t n = vlog_number(value);
      for (unsigned i = 0; i < pw; i++) {
         const vlog_logic_t bit = number_bit(n, vw - pw * (pos + 1) + i);
         abits.integer |= (uint64_t)(bit & 1) << i;
         bbits.integer |= (uint64_t)(bit >> 1) << i;
      }

      vlog_node_t part = vlog_new(V_NUMBER);
      vlog_set_number(part, number_from_jit(pw, false, abits, bbits));
      vlog_set_loc(part, vlog_loc(value));
      return part;
   }

   vlog_node_t base = value;
   int64_t msb = vw - 1, lsb = 0;
   switch (vlog_kind(value)) {
   case V_REF:
      {
         vlog_node_t dim = vlog_get_dim(value, 0);
         if (!vlog_get_const(vlog_left(dim), &msb)
             || !vlog_get_const(vlog_right(dim), &lsb))
            return value;
      }
      break;
   case V_PART_SELECT:
      // Select from the same bits of the prefix
      base = vlog_value(value);
      if (vlog_subkind(value) != V_RANGE_CONST
          || !vlog_get_const(vlog_left(value), &msb)
          || !vlog_get_const(vlog_right(value), &lsb)) {
         error_at(vlog_loc(value), "sorry, a part select with a variable "
                  "index cannot be divided between the elements of an "
                  "instance array");
         return value;
      }
      break;
   default:
      break;
   }

   int64_t left, right;
   if (msb >= lsb) {
      left = msb - (int64_t)pw * pos;
      right = left - pw + 1;
   }
   else {
      left = msb + (int64_t)pw * pos;
      right = left + pw - 1;
   }

   vlog_node_t ps = vlog_new(V_PART_SELECT);
   vlog_set_subkind(ps, V_RANGE_CONST);
   vlog_set_value(ps, base);
   vlog_set_left(ps, elab_number(left, vlog_loc(value)));
   vlog_set_right(ps, elab_number(right, vlog_loc(value)));
   vlog_set_loc(ps, vlog_loc(value));
   return ps;
}

static vlog_node_t elab_iface_element(vlog_node_t value, int pos, int count)
{
   // Element of an array of interface instances or interface ports where
   // the position is counted from the left

   if (vlog_kind(value) != V_HIER_REF)
      return value;

   vlog_node_t mr = vlog_value(value);
   if (vlog_kind(mr) != V_MOD_REF || vlog_params(mr) > 0 || !vlog_has_ref(mr))
      return value;

   vlog_node_t decl = vlog_ref(mr);
   if ((vlog_kind(decl) != V_MOD_INST && vlog_kind(decl) != V_IFACE_PORT)
       || vlog_ranges(decl) == 0)
      return value;

   int64_t left, right;
   if (!elab_instance_array_bounds(decl, &left, &right))
      return value;

   if (llabs(left - right) + 1 != count) {
      error_at(vlog_loc(value), "array of interface instances %pQ has %lld "
               "elements but the port is connected to an array of %d "
               "instances", vlog_ident(mr), (long long)(llabs(left - right) + 1),
               count);
      return value;
   }

   vlog_node_t emr = vlog_new(V_MOD_REF);
   vlog_set_ident(emr, vlog_ident(mr));
   vlog_set_ref(emr, decl);
   vlog_set_loc(emr, vlog_loc(mr));
   if (vlog_has_value(mr))
      vlog_set_value(emr, vlog_value(mr));
   vlog_add_param(emr, elab_number(left >= right ? left - pos : left + pos,
                                   vlog_loc(mr)));

   vlog_node_t href = vlog_new(V_HIER_REF);
   vlog_set_ident(href, vlog_ident(value));
   vlog_set_value(href, emr);
   vlog_set_loc(href, vlog_loc(value));
   vlog_set_global_flags(href, VLOG_GF_HIER_REF);
   return href;
}

static vlog_node_t elab_array_iface_value(vlog_node_t inst, vlog_node_t value)
{
   // An array of interface instances is connected one to one

   if (vlog_ival(inst) == 0)
      return value;

   return elab_iface_element(value, vlog_ival(inst) & 0xffffffff,
                             vlog_ival(inst) >> 32);
}

static hash_t *elab_ic_types = NULL;   // Interconnect to its port net type

static vlog_node_t elab_nettype_of(vlog_node_t decl)
{
   // The user-defined net type of the declaration or null

   if (!vlog_has_type(decl))
      return NULL;

   vlog_node_t t = vlog_type(decl);
   if (vlog_kind(t) != V_TYPE_DECL || !(vlog_flags(t) & VLOG_F_NETTYPE))
      return NULL;

   while (vlog_has_type(t) && vlog_kind(vlog_type(t)) == V_TYPE_DECL
          && (vlog_flags(vlog_type(t)) & VLOG_F_NETTYPE))
      t = vlog_type(t);

   return t;
}

static bool elab_is_interconnect(vlog_node_t decl)
{
   if (!vlog_has_type(decl))
      return false;

   vlog_node_t t = vlog_type(decl);
   return vlog_kind(t) == V_DATA_TYPE && (vlog_flags(t) & VLOG_F_USERNET)
      && vlog_ranges(t) == 0 && vlog_ranges(decl) == 0;
}

static object_t *elab_find_instance_unit(vlog_node_t v, const elab_ctx_t *ctx,
                                         ident_t *qual);

static vlog_node_t elab_ic_nettype(vlog_node_t scope, vlog_node_t decl,
                                   vlog_node_t alt, int depth,
                                   const elab_ctx_t *ctx)
{
   // The user-defined net type of the ports which an interconnect net
   // connects in the instances of a module, looking through the ports of
   // generic modules which are interconnects themselves (6.6.8)

   if (depth > 16)
      return NULL;

   const int nstmts = vlog_stmts(scope);
   for (int i = 0; i < nstmts; i++) {
      vlog_node_t list = vlog_stmt(scope, i);
      if (vlog_kind(list) != V_INST_LIST)
         continue;

      ident_t qual;
      object_t *obj = elab_find_instance_unit(list, ctx, &qual);
      vlog_node_t mod = obj != NULL ? vlog_from_object(obj) : NULL;
      if (mod == NULL || vlog_kind(mod) != V_MODULE)
         continue;

      const int nports = vlog_ports(mod);
      const int ninsts = vlog_stmts(list);
      for (int j = 0; j < ninsts; j++) {
         vlog_node_t inst = vlog_stmt(list, j);
         const int nparams = vlog_params(inst);
         for (int k = 0; k < nparams; k++) {
            vlog_node_t conn = vlog_param(inst, k);
            if (vlog_kind(conn) != V_PORT_CONN || !vlog_has_value(conn))
               continue;

            vlog_node_t value = vlog_value(conn);
            if (vlog_kind(value) != V_REF || !vlog_has_ref(value)
                || (vlog_ref(value) != decl && vlog_ref(value) != alt))
               continue;

            vlog_node_t port = NULL;
            for (int n = 0; n < nports && port == NULL; n++) {
               vlog_node_t p = vlog_ref(vlog_port(mod, n));
               if (vlog_has_ident(conn) ? vlog_ident(p) == vlog_ident(conn)
                   : n == k)
                  port = p;
            }

            if (port == NULL)
               continue;

            vlog_node_t nt = elab_nettype_of(port);
            if (nt == NULL && elab_is_interconnect(port))
               nt = elab_ic_nettype(mod, port, vlog_has_ref(port)
                                    ? vlog_ref(port) : NULL, depth + 1, ctx);

            if (nt != NULL)
               return nt;
         }
      }
   }

   return NULL;
}

static void elab_infer_interconnects(vlog_node_t body, const elab_ctx_t *ctx)
{
   // A scalar interconnect net becomes a net of the user-defined net type
   // of the ports it connects (6.6.8, 23.3.3.4)

   const int ndecls = vlog_decls(body);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(body, i);
      if (vlog_kind(d) != V_NET_DECL || !elab_is_interconnect(d))
         continue;

      vlog_node_t port = NULL;
      for (int j = 0; j < ndecls && port == NULL; j++) {
         vlog_node_t p = vlog_decl(body, j);
         if (vlog_kind(p) == V_PORT_DECL && vlog_has_ref(p)
             && vlog_ref(p) == d)
            port = p;
      }

      vlog_node_t nt = elab_ic_nettype(body, d, port, 0, ctx);
      if (nt != NULL)
         vlog_set_interconnect_nettype(d, nt);
   }
}

static void elab_check_nettype_conn(vlog_node_t port, vlog_node_t value,
                                    const loc_t *where)
{
   // IEEE 1800-2017 23.3.3.5, 6.6.8: a port merges the nets it connects so
   // their user-defined net types shall match

   vlog_node_t root = value;
   bool whole = true;
   while (vlog_kind(root) == V_BIT_SELECT || vlog_kind(root) == V_PART_SELECT) {
      root = vlog_value(root);
      whole = false;
   }

   if (vlog_kind(root) != V_REF || !vlog_has_ref(root))
      return;

   vlog_node_t decl = vlog_ref(root);
   switch (vlog_kind(decl)) {
   case V_NET_DECL:
   case V_VAR_DECL:
   case V_PORT_DECL:
      break;
   default:
      return;
   }

   vlog_node_t pt = elab_nettype_of(port), nt = elab_nettype_of(decl);

   bool interconnect = false;
   if (vlog_has_type(decl)) {
      vlog_node_t t = vlog_type(decl);
      interconnect = vlog_kind(t) == V_DATA_TYPE
         && (vlog_flags(t) & VLOG_F_USERNET) && vlog_kind(decl) != V_VAR_DECL;
   }

   bool port_interconnect = false;
   if (vlog_has_type(port)) {
      vlog_node_t t = vlog_type(port);
      port_interconnect = vlog_kind(t) == V_DATA_TYPE
         && (vlog_flags(t) & VLOG_F_USERNET);
   }

   if (port_interconnect)
      return;

   if (interconnect) {
      // The bits of an interconnect can resolve with different net types
      if (pt == NULL || !whole)
         return;

      if (elab_ic_types == NULL)
         elab_ic_types = hash_new(64);

      vlog_node_t first = hash_get(elab_ic_types, decl);
      if (first == NULL)
         hash_put(elab_ic_types, decl, pt);
      else if (first != pt)
         error_at(where, "interconnect '%s' connects ports of incompatible "
                  "nettypes %s and %s", istr(vlog_ident(decl)),
                  istr(vlog_ident(first)), istr(vlog_ident(pt)));
      return;
   }

   if (pt != NULL && nt == NULL && vlog_kind(decl) == V_NET_DECL)
      error_at(where, "cannot connect net '%s' of a built-in net type to "
               "port '%s' of nettype %s", istr(vlog_ident(decl)),
               istr(vlog_ident(port)), istr(vlog_ident(pt)));
   else if (pt != NULL && nt != NULL && pt != nt)
      error_at(where, "cannot connect net '%s' of nettype %s to port '%s' of "
               "nettype %s", istr(vlog_ident(decl)), istr(vlog_ident(nt)),
               istr(vlog_ident(port)), istr(vlog_ident(pt)));
   else if (pt == NULL && nt != NULL && vlog_kind(port) == V_PORT_DECL)
      error_at(where, "cannot connect net '%s' of nettype %s to port '%s' of "
               "a built-in net type", istr(vlog_ident(decl)),
               istr(vlog_ident(nt)), istr(vlog_ident(port)));
}
static bool elab_is_event_formal(vlog_node_t port);
static vlog_node_t elab_conn_event(vlog_node_t conn);

static void elab_verilog_ports(vlog_node_t inst, elab_instance_t *ei,
                               bool nocase, const elab_ctx_t *ctx)
{
   // Port names are case-insensitive if the module is the shim for a
   // VHDL entity
   assert(vlog_kind(inst) == V_MOD_INST);

   const int nports = vlog_ports(ei->body);

   // Ports with an interface type are numbered among the others
   int niface = 0;
   const int nbody = vlog_decls(ei->body);
   for (int i = 0; i < nbody; i++)
      niface += vlog_kind(vlog_decl(ei->body, i)) == V_IFACE_PORT;

   // A wildcard connects each port not otherwise connected to the
   // declaration with the same name in the parent
   vlog_node_t wildcard = NULL;
   int nparams = vlog_params(inst);
   bool positional = false;
   for (int i = 0; i < vlog_params(inst); i++) {
      vlog_node_t conn = vlog_param(inst, i);
      if (vlog_flags(conn) & VLOG_F_WILDCARD) {
         wildcard = conn;
         nparams--;
      }
      else if (!vlog_has_ident(conn))
         positional = true;
   }

   // Named connections to ports that do not exist are reported by name
   // below
   if (positional && nparams > nports + niface) {
      error_at(vlog_loc(inst), "expected at most %d port connections for "
               "module %pQ but found %d", nports + niface,
               vlog_ident2(ei->body), nparams);
      return;
   }

   LOCAL_BIT_MASK used;
   mask_init(&used, vlog_params(inst));

   for (int i = 0; niface > 0 && i < nbody; i++) {
      vlog_node_t port = vlog_decl(ei->body, i);
      if (vlog_kind(port) != V_IFACE_PORT)
         continue;

      elab_verilog_iface_port(inst, port, wildcard, &used, nocase, ctx);
   }

   // Index in the list of ports including those of interface type
   int position = -1;

   for (int i = 0; i < nports; i++) {
      vlog_node_t port = vlog_ref(vlog_port(ei->body, i)), conn = NULL;
      ident_t port_name = vlog_ident(port);

      for (position++; niface > 0; position++) {
         bool is_iface = false;
         for (int j = 0; j < nbody && !is_iface; j++) {
            vlog_node_t d = vlog_decl(ei->body, j);
            is_iface = vlog_kind(d) == V_IFACE_PORT
               && vlog_subkind(d) == position;
         }

         if (!is_iface)
            break;
      }

      if (position < vlog_params(inst)) {
         conn = vlog_param(inst, position);
         assert(vlog_kind(conn) == V_PORT_CONN);

         if (vlog_flags(conn) & VLOG_F_WILDCARD)
            conn = NULL;
         else if (vlog_has_ident(conn)
                  && !elab_port_name_eq(vlog_ident(conn), port_name, nocase))
            conn = NULL;
         else
            mask_set(&used, position);
      }

      if (conn == NULL) {
         for (int j = 0; j < vlog_params(inst); j++) {
            vlog_node_t cj = vlog_param(inst, j);
            if (vlog_has_ident(cj)
                && elab_port_name_eq(vlog_ident(cj), port_name, nocase)) {
               conn = cj;
               mask_set(&used, j);
               break;
            }
         }
      }

      vlog_node_t value = NULL;
      const loc_t *where = vlog_loc(inst);
      if (conn != NULL) {
         if (!vlog_has_value(conn)) {
            if ((vlog_flags(ei->body) & VLOG_F_CHECKER) && vlog_has_value(port))
               conn = NULL;
            else
               continue;
         }
      }

      if (conn != NULL) {
         value = vlog_value(conn);
         where = vlog_loc(conn);

         // The edge of an event connection was bound in the instance
         vlog_node_t event = elab_conn_event(conn);
         if (event != value && !elab_is_event_formal(port))
            value = vlog_value(event);

         if (vlog_ival(inst) != 0)
            value = elab_array_port_value(inst, port, value);

         // An assignment pattern has the type of the formal as it is in
         // this instance, after the parameters have been overridden
         if (vlog_kind(value) == V_PATTERN_EXPR && vlog_has_type(port)
             && vlog_get_type(vlog_type(port)) != NULL) {
            vlog_node_t pat = vlog_new(V_PATTERN_EXPR);
            vlog_set_type(pat, vlog_get_type(vlog_type(port)));
            vlog_set_loc(pat, vlog_loc(value));
            const int nitems = vlog_params(value);
            for (int j = 0; j < nitems; j++)
               vlog_add_param(pat, vlog_param(value, j));
            value = pat;
         }
      }
      else if (wildcard != NULL && vlog_has_ref(wildcard)) {
         vlog_node_t parent = vlog_ref(wildcard);
         const int ndecls = vlog_decls(parent);
         for (int j = 0; j < ndecls; j++) {
            vlog_node_t d = vlog_decl(parent, j);
            switch (vlog_kind(d)) {
            case V_NET_DECL:
            case V_VAR_DECL:
            case V_PORT_DECL:
               if (vlog_ident(d) == port_name) {
                  value = vlog_new(V_REF);
                  vlog_set_ident(value, port_name);
                  vlog_set_ref(value, d);
                  vlog_set_loc(value, vlog_loc(wildcard));
               }
               break;
            default:
               break;
            }

            if (value != NULL)
               break;
         }

         where = vlog_loc(wildcard);
      }


      // A checker argument can have a default value
      if (value == NULL && vlog_has_value(port) && vlog_subkind(port) == V_PORT_INPUT)
         value = vlog_value(port);

      if (value == NULL) {
         diag_t *d = diag_new(DIAG_WARN, vlog_loc(inst));
         diag_printf(d, "missing port connection for %pQ", port_name);
         diag_hint(d, vlog_loc(port), "%pQ declared here", port_name);
         diag_hint(d, vlog_loc(inst), "instance %pQ", vlog_ident(inst));
         diag_emit(d);
         continue;
      }

      // System Verilog allows an output port to drive any variable
      const bool sv_conn = (conn ?: wildcard) != NULL
         && (vlog_flags(conn ?: wildcard) & VLOG_F_SYSTEMVERILOG)
         && elab_port_value_is_variable(value);

      if (vlog_subkind(port) != V_PORT_INPUT && !vlog_is_net(value)
          && !sv_conn && !elab_port_drives_variable(value)) {
         diag_t *d = diag_new(DIAG_ERROR, where);
         if (vlog_kind(value) == V_REF)
            diag_printf(d, "%pQ", vlog_ident(value));
         else
            diag_printf(d, "expression");
         diag_printf(d, " cannot be driven by continuous assignment from "
                     "port %pQ", vlog_ident(port));
         diag_emit(d);
         continue;
      }

      elab_check_nettype_conn(port, value, where);

      if (opt_get_int(OPT_LINT))
         vlog_lint_port(port, value, where);

      if (vlog_subkind(port) == V_PORT_INPUT && vlog_kind(value) == V_REF
          && vlog_has_ref(value)) {
         // An unpacked array parameter has no signal: connect the pattern
         // that is its value
         vlog_node_t decl = vlog_ref(value);
         if ((vlog_kind(decl) == V_PARAM_DECL
              || vlog_kind(decl) == V_LOCALPARAM)
             && vlog_ranges(decl) > 0 && vlog_has_value(decl)
             && vlog_kind(vlog_value(decl)) == V_PATTERN_EXPR)
            value = vlog_value(decl);
      }

      vlog_node_t map = vlog_new(V_PORT_MAP);
      vlog_set_ident(map, port_name);
      vlog_set_ref(map, port);
      vlog_set_value(map, value);
      vlog_set_loc(map, where);

      tree_t wrap = tree_new(T_VERILOG);
      tree_set_ident(wrap, vlog_ident(port));
      tree_set_vlog(wrap, map);
      tree_set_loc(wrap, vlog_loc(port));

      tree_add_decl(ctx->out, wrap);
   }

   for (int i = 0; i < vlog_params(inst); i++) {
      if (!mask_test(&used, i)) {
         vlog_node_t conn = vlog_param(inst, i);
         if (vlog_flags(conn) & VLOG_F_WILDCARD)
            continue;

         assert(vlog_has_ident(conn));

         error_at(vlog_loc(conn), "port %pQ not found in %pQ",
                  vlog_ident(conn), vlog_ident2(ei->body));
      }
   }
}

static void elab_vhdl_in_verilog(tree_t entity, vlog_node_t inst,
                                 const elab_instance_t *ei, elab_ctx_t *ctx);

static void elab_early_iface_conns(const elab_ctx_t *ctx, int first);

static void elab_rewrite_hier_ref_pre_cb(vlog_node_t v, void *arg);
static vlog_node_t elab_rewrite_hier_ref_cb(vlog_node_t v, void *arg);

static vlog_node_t elab_find_href_cb(vlog_node_t v, void *arg)
{
   if (vlog_kind(v) == V_HIER_REF)
      *(bool *)arg = true;

   return v;
}

static bool elab_copy_param_pred(vlog_node_t v, void *ctx)
{
   switch (vlog_kind(v)) {
   case V_INST_LIST:
   case V_PARAM_ASSIGN:
   case V_HIER_REF:
   case V_MOD_REF:
   case V_BINARY:
   case V_UNARY:
   case V_COND_EXPR:
   case V_CONCAT:
   case V_BIT_SELECT:
   case V_PART_SELECT:
   case V_SYS_FCALL:
   case V_MIN_TYP_MAX:
   case V_CAST:
      return true;
   default:
      return false;
   }
}

static vlog_node_t elab_resolve_param_hrefs(vlog_node_t list,
                                            const elab_ctx_t *ctx)
{
   // A parameter override may be the value of a parameter of an instance
   // which has already been elaborated

   bool found = false;
   const int nparams = vlog_params(list);
   for (int i = 0; i < nparams && !found; i++) {
      vlog_node_t p = vlog_param(list, i);
      if (vlog_has_value(p))
         vlog_rewrite(vlog_value(p), NULL, elab_find_href_cb, &found);
   }

   if (!found)
      return list;

   // The connections of the instances are bound later with the rest of
   // the module so only the parameters are copied and rewritten
   vlog_node_t copy = vlog_new(V_INST_LIST);
   vlog_set_ident(copy, vlog_ident(list));
   vlog_set_loc(copy, vlog_loc(list));

   elab_href_ctx_t hctx = {
      .where        = ctx->out,
      .parents      = ctx->block_parents,
      .iface_first  = ctx->iface_first,
      .iface_values = ctx->iface_values,
   };

   for (int i = 0; i < nparams; i++) {
      vlog_node_t p = vlog_param(list, i);
      if (vlog_has_value(p)) {
         p = vlog_copy(p, elab_copy_param_pred, NULL);
         hctx.root = p;
         vlog_rewrite(p, elab_rewrite_hier_ref_pre_cb,
                      elab_rewrite_hier_ref_cb, &hctx);
      }

      vlog_add_param(copy, p);
   }

   const int nstmts = vlog_stmts(list);
   for (int i = 0; i < nstmts; i++)
      vlog_add_stmt(copy, vlog_stmt(list, i));

   return copy;
}

static vlog_node_t elab_apply_defparams(vlog_node_t list, vlog_node_t mod,
                                        ident_t label, const elab_ctx_t *ctx,
                                        elab_defparam_t *down,
                                        unsigned *ndown)
{
   // A defparam in an enclosing scope overrides the value given in the
   // instantiation of the module it names

   vlog_node_t copy = NULL;
   const elab_defparam_t **set LOCAL =
      xcalloc_array(ctx->ndefparams, sizeof(elab_defparam_t *));
   unsigned nset = 0;
   for (unsigned i = 0; i < ctx->ndefparams; i++) {
      const elab_defparam_t *dp = &(ctx->defparams[i]);
      if (dp->path[0] != label)
         continue;
      else if (dp->npath > 1) {
         *dp->passed = true;
         down[(*ndown)++] = (elab_defparam_t){
            .path    = dp->path + 1,
            .npath   = dp->npath - 1,
            .param   = dp->param,
            .value   = dp->value,
            .where   = dp->where,
            .passed  = dp->passed,
            .applied = dp->applied,
            .home    = dp->home,
            .line    = dp->line,
         };
         continue;
      }

      *dp->passed = true;

      bool found = false;
      const int nd = vlog_decls(mod);
      for (int j = 0; j < nd && !found; j++) {
         vlog_node_t d = vlog_decl(mod, j);
         found = vlog_kind(d) == V_PARAM_DECL && vlog_ident(d) == dp->param;
      }

      if (!found)
         continue;   // Reported by the scope containing the defparam

      *dp->applied = true;

      // The last defparam for a parameter in the source text is used
      // (23.10.1) but more than one is likely a mistake
      unsigned prev = 0;
      while (prev < nset && set[prev]->param != dp->param)
         prev++;

      if (prev == nset)
         set[nset++] = dp;
      else {
         if (set[prev]->where != NULL && dp->where != NULL) {
            diag_t *d = diag_new(DIAG_WARN, vlog_loc(dp->where));
            diag_printf(d, "parameter %s of instance %s is set by more "
                        "than one defparam", istr(dp->param), istr(label));
            diag_hint(d, vlog_loc(set[prev]->where), "also set here");
            diag_hint(d, vlog_loc(dp->where), "this value is used");
            diag_emit(d);
         }
         set[prev] = dp;
      }

      if (copy == NULL) {
         // Positional overrides are given the names of the parameters
         copy = vlog_new(V_INST_LIST);
         vlog_set_ident(copy, vlog_ident(list));
         vlog_set_loc(copy, vlog_loc(list));

         int pidx = 0;
         const int ndecls = vlog_decls(mod);
         const int nparams = vlog_params(list);
         for (int j = 0; j < ndecls; j++) {
            vlog_node_t d = vlog_decl(mod, j);
            if (vlog_kind(d) != V_PARAM_DECL)
               continue;

            vlog_node_t p = NULL;
            if (pidx < nparams && !vlog_has_ident(vlog_param(list, pidx)))
               p = vlog_param(list, pidx);
            else {
               for (int k = 0; k < nparams && p == NULL; k++) {
                  vlog_node_t q = vlog_param(list, k);
                  if (vlog_has_ident(q) && vlog_ident(q) == vlog_ident(d))
                     p = q;
               }
            }

            pidx++;

            if (p == NULL || !vlog_has_value(p))
               continue;

            vlog_node_t named = vlog_new(V_PARAM_ASSIGN);
            vlog_set_ident(named, vlog_ident(d));
            vlog_set_value(named, vlog_value(p));
            vlog_set_loc(named, vlog_loc(p));
            vlog_add_param(copy, named);
         }

         const int nstmts = vlog_stmts(list);
         for (int j = 0; j < nstmts; j++)
            vlog_add_stmt(copy, vlog_stmt(list, j));
      }

      bool replaced = false;
      const int nparams = vlog_params(copy);
      for (int j = 0; j < nparams && !replaced; j++) {
         vlog_node_t p = vlog_param(copy, j);
         if (vlog_ident(p) == dp->param) {
            vlog_set_value(p, dp->value);
            replaced = true;
         }
      }

      if (!replaced) {
         vlog_node_t named = vlog_new(V_PARAM_ASSIGN);
         vlog_set_ident(named, dp->param);
         vlog_set_value(named, dp->value);
         vlog_set_loc(named, vlog_loc(dp->value));
         vlog_add_param(copy, named);
      }
   }

   return copy ?: list;
}

static void elab_collect_defparams(vlog_node_t body, elab_defparam_t *out,
                                   unsigned *count, const elab_ctx_t *ctx,
                                   const void *home)
{
   const int nstmts = vlog_stmts(body);
   for (int i = 0; i < nstmts; i++) {
      vlog_node_t s = vlog_stmt(body, i);
      if (vlog_kind(s) != V_DEFPARAM)
         continue;

      vlog_node_t target = vlog_target(s);
      if (vlog_kind(target) != V_HIER_REF) {
         error_at(vlog_loc(s), "sorry, this defparam target is not supported");
         continue;
      }

      unsigned depth = 0;
      for (vlog_node_t it = vlog_value(target); ; it = vlog_value(it)) {
         depth++;
         if (!vlog_has_value(it))
            break;
      }

      ident_t *path = pool_malloc(ctx->pool, depth * sizeof(ident_t));
      unsigned pos = depth;
      for (vlog_node_t it = vlog_value(target); ; it = vlog_value(it)) {
         path[--pos] = vlog_kind(it) == V_MOD_REF ? vlog_mod_ref_name(it)
            : vlog_ident(it);
         if (!vlog_has_value(it))
            break;
      }

      vlog_node_t value = vlog_value(s);
      while (vlog_kind(value) == V_REF && vlog_has_ref(value)
             && vlog_kind(vlog_ref(value)) == V_LOCALPARAM)
         value = vlog_value(vlog_ref(value));

      if (vlog_kind(value) != V_NUMBER && vlog_kind(value) != V_REAL
          && vlog_kind(value) != V_STRING) {
         error_at(vlog_loc(value), "sorry, the value of a defparam must be "
                  "a constant");
         continue;
      }

      bool *flags = pool_calloc(ctx->pool, 2 * sizeof(bool));

      out[(*count)++] = (elab_defparam_t){
         .path    = path,
         .npath   = depth,
         .param   = vlog_ident(target),
         .value   = value,
         .where   = s,
         .passed  = &(flags[0]),
         .applied = &(flags[1]),
         .home    = home,
         .line    = vlog_loc(s)->first_line,
      };
   }
}

static void elab_check_defparams(vlog_node_t body, const elab_ctx_t *ctx)
{
   // A defparam in this scope which did not find its target

   for (unsigned i = 0; i < ctx->ndefparams; i++) {
      const elab_defparam_t *dp = &(ctx->defparams[i]);
      if (dp->where == NULL || *dp->applied)
         continue;

      bool mine = false;
      const int nstmts = vlog_stmts(body);
      for (int j = 0; j < nstmts && !mine; j++)
         mine = vlog_stmt(body, j) == dp->where;

      if (!mine)
         continue;

      LOCAL_TEXT_BUF tb = tb_new();
      for (unsigned j = 0; j < dp->npath; j++)
         tb_printf(tb, "%s.", istr(dp->path[j]));
      tb_cat(tb, istr(dp->param));

      error_at(vlog_loc(dp->where), "cannot find %pQ in hierarchical "
               "identifier '%s'", *dp->passed ? dp->param : dp->path[0],
               tb_get(tb));
   }
}

typedef struct {
   vlog_node_t *decls;
   vlog_node_t *actuals;
   int          count;
   hset_t      *ports;
   hash_t      *sampled;
} elab_event_formals_t;

static vlog_node_t elab_conn_event(vlog_node_t conn)
{
   // The edge in the connection to an event formal

   if (vlog_has_ref(conn) && vlog_kind(vlog_ref(conn)) == V_EVENT)
      return vlog_ref(conn);

   return vlog_value(conn);
}

static vlog_node_t elab_find_conn(vlog_node_t inst, int index,
                                  vlog_node_t port)
{
   const int nparams = vlog_params(inst);
   if (index < nparams) {
      vlog_node_t c = vlog_param(inst, index);
      if (!(vlog_flags(c) & VLOG_F_WILDCARD) && !vlog_has_ident(c))
         return c;
   }

   for (int i = 0; i < nparams; i++) {
      vlog_node_t c = vlog_param(inst, i);
      if (vlog_has_ident(c) && vlog_ident(c) == vlog_ident(port))
         return c;
   }

   return NULL;
}

static vlog_node_t elab_event_actual(vlog_node_t inst, vlog_node_t body,
                                     int index, vlog_node_t port)
{
   // The event expression connected to a port of event type

   vlog_node_t c = elab_find_conn(inst, index, port);
   return c != NULL && vlog_has_value(c) ? elab_conn_event(c) : NULL;
}

static vlog_node_t elab_constant_actual(vlog_node_t inst, int index,
                                        vlog_node_t port)
{
   // A formal of a checker given a constant is replaced by it so that it
   // can be used where a constant expression is needed (17.2)

   vlog_node_t c = elab_find_conn(inst, index, port), v = NULL;
   if (c != NULL && vlog_has_value(c))
      v = vlog_value(c);
   else if (vlog_has_value(port))
      v = vlog_value(port);

   return v != NULL && vlog_kind(v) == V_NUMBER ? v : NULL;
}

static bool elab_is_event_formal(vlog_node_t port)
{
   if (vlog_kind(port) != V_PORT_DECL || vlog_subkind(port) != V_PORT_INPUT)
      return false;

   vlog_node_t type = vlog_get_type(vlog_type(port));
   return type != NULL && vlog_kind(type) == V_DATA_TYPE
      && vlog_subkind(type) == DT_EVENT;
}

static bool elab_has_event_actuals(vlog_node_t mod, vlog_node_t inst)
{
   if (inst == NULL || vlog_kind(mod) != V_MODULE)
      return false;

   const bool checker = !!(vlog_flags(mod) & VLOG_F_CHECKER);

   const int nports = vlog_ports(mod);
   for (int i = 0; i < nports; i++) {
      vlog_node_t port = vlog_ref(vlog_port(mod, i));
      if (elab_is_event_formal(port)) {
         vlog_node_t actual = elab_event_actual(inst, mod, i, port);
         if (actual != NULL && vlog_kind(actual) == V_EVENT)
            return true;
      }
      else if (checker && vlog_subkind(port) == V_PORT_INPUT
               && elab_constant_actual(inst, i, port) != NULL)
         return true;
   }

   return false;
}

static vlog_node_t elab_event_formal_cb(vlog_node_t v, void *arg)
{
   if (vlog_kind(v) == V_REF && vlog_has_ref(v)) {
      const elab_event_formals_t *ef = arg;
      if (hset_contains(ef->ports, v))
         return v;

      vlog_node_t s;
      if (ef->sampled != NULL
          && (s = hash_get(ef->sampled, vlog_ref(v))) != NULL) {
         vlog_node_t ref = vlog_new(V_REF);
         vlog_set_ident(ref, vlog_ident(s));
         vlog_set_ref(ref, s);
         vlog_set_loc(ref, vlog_loc(v));
         return ref;
      }

      for (int i = 0; i < ef->count; i++) {
         if (vlog_ref(v) == ef->decls[i]
             && vlog_kind(ef->actuals[i]) == V_NUMBER)
            return ef->actuals[i];
      }

      return v;
   }

   if (vlog_kind(v) == V_SVA_OP) {
      // The bounds of a delay or repetition are constants and are not
      // sampled
      const elab_event_formals_t *ef = arg;
      for (int side = 0; side < 2; side++) {
         if (!(side == 0 ? vlog_has_left(v) : vlog_has_right(v)))
            continue;

         vlog_node_t b = side == 0 ? vlog_left(v) : vlog_right(v);
         if (vlog_kind(b) != V_REF)
            continue;

         const char *name = istr(vlog_ident(b));
         if (strncmp(name, "$sampled.", 9) != 0)
            continue;

         for (int i = 0; i < ef->count; i++) {
            if (vlog_kind(ef->actuals[i]) == V_NUMBER
                && strcmp(name + 9, istr(vlog_ident(ef->decls[i]))) == 0) {
               if (side == 0)
                  vlog_set_left(v, ef->actuals[i]);
               else
                  vlog_set_right(v, ef->actuals[i]);
            }
         }
      }

      return v;
   }

   if (vlog_kind(v) != V_EVENT || !vlog_has_value(v)
       || vlog_subkind(v) != V_EVENT_LEVEL)
      return v;

   vlog_node_t ref = vlog_value(v);
   if (vlog_kind(ref) != V_REF || !vlog_has_ref(ref))
      return v;

   // The clocking event of a sampled value function is not sampled
   const char *name = istr(vlog_ident(ref));
   const bool sampled = strncmp(name, "$sampled.", 9) == 0;

   const elab_event_formals_t *ef = arg;
   for (int i = 0; i < ef->count; i++) {
      if (sampled && strcmp(name + 9, istr(vlog_ident(ef->decls[i]))) == 0) {
         ref = vlog_new(V_REF);
         vlog_set_ident(ref, vlog_ident(ef->decls[i]));
         vlog_set_ref(ref, ef->decls[i]);
         vlog_set_loc(ref, vlog_loc(v));
      }
      else if (vlog_ref(ref) != ef->decls[i])
         continue;

      vlog_node_t e = vlog_new(V_EVENT);
      vlog_set_loc(e, vlog_loc(v));
      vlog_set_subkind(e, vlog_subkind(ef->actuals[i]));
      vlog_set_value(e, ref);
      return e;
   }

   return v;
}

static void elab_bind_event_formals(vlog_node_t body, vlog_node_t inst)
{
   // A formal of event type connected to an event expression such as
   // posedge clk becomes a scalar input connected to the signal and the
   // edge of the expression is applied where the formal is used

   const int nports = vlog_ports(body);
   elab_event_formals_t ef = {
      .decls   = xcalloc_array(nports, sizeof(vlog_node_t)),
      .actuals = xcalloc_array(nports, sizeof(vlog_node_t)),
   };

   for (int i = 0; i < nports; i++) {
      vlog_node_t port = vlog_ref(vlog_port(body, i));
      if (!elab_is_event_formal(port)) {
         vlog_node_t k;
         if ((vlog_flags(body) & VLOG_F_CHECKER)
             && vlog_subkind(port) == V_PORT_INPUT
             && (k = elab_constant_actual(inst, i, port)) != NULL) {
            ef.decls[ef.count] = port;
            ef.actuals[ef.count++] = k;
         }
         continue;
      }

      vlog_node_t actual = elab_event_actual(inst, body, i, port);
      if (actual == NULL || vlog_kind(actual) != V_EVENT)
         continue;

      vlog_node_t type = vlog_new(V_DATA_TYPE);
      vlog_set_subkind(type, DT_LOGIC);
      vlog_set_loc(type, vlog_loc(port));

      vlog_node_t var = vlog_new(V_VAR_DECL);
      vlog_set_ident(var, vlog_ident(port));
      vlog_set_type(var, type);
      vlog_set_loc(var, vlog_loc(port));

      vlog_set_type(port, type);
      vlog_set_ref(port, var);
      vlog_add_decl(body, var);

      // The sampled copy of the formal now holds the signal
      ident_t sid = ident_prefix(ident_new("$sampled"), vlog_ident(port), '.');
      const int ndecls = vlog_decls(body);
      for (int j = 0; j < ndecls; j++) {
         vlog_node_t d = vlog_decl(body, j);
         if (vlog_kind(d) != V_VAR_DECL || vlog_ident(d) != sid)
            continue;

         vlog_node_t s = vlog_new(V_VAR_DECL);
         vlog_set_ident(s, sid);
         vlog_set_type(s, type);
         vlog_set_loc(s, vlog_loc(d));
         vlog_add_decl(body, s);

         if (ef.sampled == NULL)
            ef.sampled = hash_new(16);
         hash_put(ef.sampled, d, s);
         break;
      }

      ef.decls[ef.count] = port;
      ef.actuals[ef.count++] = actual;
   }

   if (ef.count > 0) {
      ef.ports = hset_new(nports);
      for (int i = 0; i < nports; i++)
         hset_insert(ef.ports, vlog_port(body, i));

      vlog_rewrite(body, NULL, elab_event_formal_cb, &ef);
      hset_free(ef.ports);
   }

   if (ef.sampled != NULL)
      hash_free(ef.sampled);

   free(ef.decls);
   free(ef.actuals);
}

static tree_t elab_iface_conn_target(const elab_iface_conn_t *c,
                                     const elab_ctx_t *ctx);
static void elab_ignore_diag(diag_t *d, void *context);

typedef struct {
   vlog_node_t       inst;
   const elab_ctx_t *ctx;
} elab_port_body_t;

static vlog_node_t elab_early_port_body(vlog_node_t port, void *arg)
{
   // The body of the interface instance connected to a port, if that
   // instance is already known

   const elab_port_body_t *pb = arg;
   vlog_node_t inst = pb->inst;
   if (inst == NULL || vlog_kind(inst) != V_MOD_INST)
      return NULL;

   ident_t name = vlog_ident(port);
   vlog_node_t conn = NULL, wildcard = NULL;
   const int pos = vlog_subkind(port), nparams = vlog_params(inst);
   if (pos < nparams) {
      vlog_node_t c = vlog_param(inst, pos);
      if (!(vlog_flags(c) & VLOG_F_WILDCARD) && !vlog_has_ident(c))
         conn = c;
   }

   for (int i = 0; i < nparams; i++) {
      vlog_node_t c = vlog_param(inst, i);
      if (vlog_flags(c) & VLOG_F_WILDCARD)
         wildcard = c;
      else if (conn == NULL && vlog_has_ident(c) && vlog_ident(c) == name)
         conn = c;
   }

   if (conn != NULL && !vlog_has_value(conn))
      return NULL;
   else if (conn == NULL && wildcard == NULL)
      return NULL;

   elab_iface_conn_t c = {
      .parent = pb->ctx->out,
      .port   = port,
      .value  = conn != NULL
         ? elab_array_iface_value(inst, vlog_value(conn)) : NULL,
      .loc    = *vlog_loc(conn ?: inst),
   };

   void *saved_ctx;
   diag_consumer_t saved = diag_get_consumer(&saved_ctx);
   diag_set_consumer(elab_ignore_diag, NULL);

   tree_t target = elab_iface_conn_target(&c, pb->ctx);

   diag_set_consumer(saved, saved_ctx);

   if (target == NULL || tree_decls(target) == 0)
      return NULL;

   tree_t h = tree_decl(target, 0);
   if (tree_kind(h) != T_HIER || !tree_has_ref(h)
       || tree_kind(tree_ref(h)) != T_VERILOG)
      return NULL;

   vlog_node_t body = tree_vlog(tree_ref(h));
   return (vlog_flags(body) & VLOG_F_INTERFACE) ? body : NULL;
}

static void elab_verilog_module(tree_t comp, ident_t label, vlog_node_t mod,
                                vlog_node_t list, vlog_node_t inst,
                                tree_t entity, const elab_ctx_t *ctx)
{
   list = elab_resolve_param_hrefs(list, ctx);

   if (opt_get_int(OPT_LINT))
      vlog_lint_design(mod);

   elab_defparam_t *down = NULL;
   unsigned ndown = 0;
   if (ctx->ndefparams > 0) {
      down = pool_malloc(ctx->pool, ctx->ndefparams * sizeof(elab_defparam_t));
      list = elab_apply_defparams(list, mod, label, ctx, down, &ndown);
   }

   ident_t ndotted = ident_prefix(ctx->dotted, label, '.');

   elab_ctx_t new_ctx = {
      .dotted   = ndotted,
      .cfg_path = ctx->cfg_path != NULL
         ? ident_prefix(ctx->cfg_path, label, '.') : label,
   };
   elab_inherit_context(&new_ctx, ctx);

   tree_t b = tree_new(T_BLOCK);
   tree_set_ident(b, label);
   tree_set_loc(b, inst != NULL ? vlog_loc(inst) : tree_loc(ctx->out));

   tree_add_stmt(ctx->out, b);
   new_ctx.out = b;

   hash_put(ctx->block_parents, b, ctx->out);

   mod_cache_t *mc = elab_cached_module(vlog_to_object(mod), &new_ctx);
   mc->count++;

   // The members of the interface connected to a port may differ for
   // each instance so references to them cannot be shared
   bool has_iface_port = false;
   if (vlog_kind(mod) == V_MODULE) {
      const int ndecls = vlog_decls(mod);
      for (int i = 0; !has_iface_port && i < ndecls; i++)
         has_iface_port = vlog_kind(vlog_decl(mod, i)) == V_IFACE_PORT;
   }

   elab_instance_t *ei = ghash_get(mc->instances, list), *chain = NULL;
   if (vlog_kind(mod) == V_MODULE && vlog_bind_is_selective(mod))
      ei = NULL;   // The instance may have its own bound instances
   else if (elab_has_event_actuals(mod, inst)) {
      // Each event expression gives a different body
      chain = ei;
      ei = NULL;
   }
   else if (has_iface_port) {
      chain = ei;
      ei = NULL;
   }

   if (!elab_can_clone_instance(ei, &new_ctx)) {
      ident_t id = ident_sprintf("%s#%d", istr(vlog_ident(mod)), mc->unique);

      vlog_set_instance_scope(ndotted);
      vlog_node_t body = vlog_new_instance(mod, list, id);
      bool folded = false;

      if (elab_has_event_actuals(mod, inst))
         elab_bind_event_formals(body, inst);

      // The layout of an interface depends on its parameters and a virtual
      // interface refers to it by the values of the parameters
      const bool is_iface = vlog_kind(mod) == V_MODULE
         && (vlog_flags(mod) & VLOG_F_INTERFACE);
      ident_t key = NULL;
      elab_instance_t *shared = NULL;
      if (is_iface) {
         const int base_errors = error_count();
         vlog_fold(body, ctx->mir, ctx->jit);
         folded = true;

         if (error_count() == base_errors
             && (key = vlog_instance_key(mod, body)) != NULL) {
            id = ident_sprintf("%s#%s", istr(vlog_ident(mod)), istr(key));
            if ((shared = hash_get(ctx->iface_units, id)) == NULL) {
               body = vlog_new_instance(mod, list, id);
               folded = false;
            }
         }
         else
            key = NULL;
      }

      if (shared != NULL) {
         ghash_put(mc->instances, list, shared);
         ei = shared;
         goto have_instance;
      }

      ei = pool_calloc(ctx->pool, sizeof(elab_instance_t));
      ei->chain = chain;
      ei->body = body;

      if (key != NULL)
         hash_put(ctx->iface_units, id, ei);

      if (is_iface && hash_get(ctx->iface_first, vlog_ident2(mod)) == NULL)
         hash_put(ctx->iface_first, vlog_ident2(mod), ei->body);

      ei->first = b;

      if (!folded)
         vlog_fold(ei->body, ctx->mir, ctx->jit);

      if (vlog_kind(mod) == V_MODULE
          && (vlog_global_flags(mod) & VLOG_GF_HIER_REF)) {
         // A parameter of an interface may give the size of a variable
         elab_port_body_t pb = { .inst = inst, .ctx = ctx };
         if (vlog_resolve_iface_params(ei->body, elab_early_port_body, &pb))
            vlog_fold(ei->body, ctx->mir, ctx->jit);
      }

      ei->wrap = tree_new(T_VERILOG);
      tree_set_loc(ei->wrap, vlog_loc(mod));
      tree_set_ident(ei->wrap, vlog_ident(mod));
      tree_set_vlog(ei->wrap, ei->body);

      ei->block = tree_new(T_BLOCK);
      tree_set_loc(ei->block, vlog_loc(list));
      tree_set_ident(ei->block, ndotted);

      vlog_trans(ei->body, ei->block);
      elab_infer_interconnects(ei->body, ctx);
      vlog_lower_instance(ctx->mir, ei->body, NULL, ei->block);

      ghash_put(mc->instances, list, ei);
      mc->unique++;
   }

 have_instance:
   new_ctx.cloned = vlog_ident(ei->body);
   new_ctx.dp_home = ei->body;

   {
      unsigned nbody = 0;
      const int nstmts = vlog_stmts(ei->body);
      for (int i = 0; i < nstmts; i++) {
         if (vlog_kind(vlog_stmt(ei->body, i)) == V_DEFPARAM)
            nbody++;
      }

      if (nbody + ndown > 0) {
         elab_defparam_t *all =
            pool_malloc(ctx->pool, (nbody + ndown) * sizeof(elab_defparam_t));
         unsigned nall = 0;
         for (unsigned i = 0; i < ndown; i++)
            all[nall++] = down[i];
         elab_collect_defparams(ei->body, all, &nall, ctx, ei->body);

         // The name of the scope containing the defparam may start the path
         for (unsigned i = ndown; i < nall; i++) {
            elab_defparam_t *dp = &(all[i]);
            if (dp->path[0] == label && dp->npath > 1) {
               dp->path++;
               dp->npath--;
            }
         }

         new_ctx.defparams = all;
         new_ctx.ndefparams = nall;
      }
   }

   elab_push_scope(ei->wrap, &new_ctx);

   if (comp != NULL) {
      tree_t bind = elab_mixed_binding(comp, ei);
      if (bind != NULL)
         elab_ports(ei->block, bind, &new_ctx);
   }
   else {
      if (entity != NULL) {
         // Ports of the shim module are driven by the Verilog port
         // connections below and read by the VHDL entity
         tree_t none = tree_new(T_BINDING);
         tree_set_loc(none, vlog_loc(inst));
         elab_ports(ei->block, none, &new_ctx);
      }

      const int first_conn = new_ctx.iface_conns->count;
      elab_verilog_ports(inst, ei, entity != NULL, &new_ctx);
      elab_early_iface_conns(&new_ctx, first_conn);
   }

   if (elab_new_errors(&new_ctx) == 0) {
      elab_decls(ei->block, &new_ctx);
      elab_verilog_processes(ei->body, &new_ctx);
   }

   if (elab_new_errors(&new_ctx) == 0)
      elab_lower(b, &new_ctx);

   if (elab_new_errors(&new_ctx) == 0)
      elab_verilog_sub_blocks(ei->body, &new_ctx);

   elab_check_defparams(ei->body, &new_ctx);

   if (entity != NULL && elab_new_errors(&new_ctx) == 0)
      elab_vhdl_in_verilog(entity, inst, ei, &new_ctx);

   elab_pop_scope(&new_ctx);
}

static void elab_verilog_vhdl_instance_list(vlog_node_t v, tree_t entity,
                                            const elab_ctx_t *ctx);

static bool elab_is_instantiable(object_t *obj)
{
   // A Verilog module or VHDL entity can be instantiated from Verilog
   if (vlog_from_object(obj) != NULL)
      return true;

   tree_t t = tree_from_object(obj);
   return t != NULL && tree_kind(t) == T_ENTITY;
}

static object_t *elab_find_unit_by_name(ident_t modname, const elab_ctx_t *ctx,
                                        ident_t *qual)
{
   ident_t libname = lib_name(ctx->library);

   LOCAL_TEXT_BUF tb = tb_new();
   tb_istr(tb, modname);
   tb_upcase(tb);

   ident_t unit = ident_new(tb_get(tb));
   *qual = ident_prefix(libname, unit, '.');

   object_t *obj = lib_get_generic(ctx->library, *qual, NULL);
   if (obj == NULL) {
      // Fall back to searching all other libraries
      lib_t lib;
      if ((obj = lib_search_generic(unit, elab_is_instantiable, &lib)))
         *qual = ident_prefix(lib_name(lib), unit, '.');
   }

   return obj;
}

static object_t *elab_find_instance_unit(vlog_node_t v, const elab_ctx_t *ctx,
                                         ident_t *qual)
{
   return elab_find_unit_by_name(vlog_ident(v), ctx, qual);
}

static ident_t elab_config_cell(ident_t name)
{
   // A cell name without the library

   const char *str = istr(name), *dot = strchr(str, '.');
   return dot != NULL ? ident_new(dot + 1) : name;
}

static bool elab_config_bind(const elab_ctx_t *ctx, vlog_node_t list,
                             vlog_node_t inst, vlog_node_t *mod,
                             vlog_node_t *newlist)
{
   // Apply the instance and cell clauses of a configuration (33.4)

   ident_t path = ident_prefix(ctx->cfg_path, vlog_ident(inst), '.');
   ident_t cell = vlog_ident(list);

   vlog_node_t irule = NULL, crule = NULL;
   const int nrules = vlog_stmts(ctx->config);
   for (int i = 0; i < nrules; i++) {
      vlog_node_t r = vlog_stmt(ctx->config, i);
      if (vlog_subkind(r) == V_CONFIG_INSTANCE && vlog_ident(r) == path)
         irule = r;
      else if (vlog_subkind(r) == V_CONFIG_CELL
               && elab_config_cell(vlog_ident(r)) == cell)
         crule = r;
   }

   if (irule == NULL && crule == NULL)
      return true;

   vlog_node_t rules[] = { crule, irule };

   ident_t target = NULL;
   for (int i = 0; i < 2; i++) {
      if (rules[i] != NULL && vlog_has_ident2(rules[i]))
         target = vlog_ident2(rules[i]);
   }

   if (target != NULL) {
      ident_t qual;
      object_t *obj = elab_find_unit_by_name(elab_config_cell(target), ctx,
                                             &qual);
      vlog_node_t m = obj != NULL ? vlog_from_object(obj) : NULL;
      if (m == NULL || vlog_kind(m) != V_MODULE) {
         error_at(vlog_loc(irule ?: crule), "cell %pQ of configuration "
                  "'%s' not found in library %pI or any other library",
                  target, istr(vlog_ident2(ctx->config)),
                  lib_name(ctx->library));
         return false;
      }

      *mod = m;
   }

   vlog_node_t copy = vlog_new(V_INST_LIST);
   vlog_set_ident(copy, target ? elab_config_cell(target) : cell);
   vlog_set_loc(copy, vlog_loc(list));
   vlog_add_stmt(copy, inst);

   // Parameters given by a clause replace those of the instance
   const int nparams = vlog_params(list);
   for (int i = 0; i < nparams; i++) {
      vlog_node_t p = vlog_param(list, i);
      bool replaced = false;
      for (int j = 0; j < 2 && vlog_has_ident(p); j++) {
         if (rules[j] == NULL)
            continue;

         const int ncp = vlog_params(rules[j]);
         for (int k = 0; k < ncp; k++)
            replaced |= vlog_ident(vlog_param(rules[j], k)) == vlog_ident(p);
      }

      if (!replaced)
         vlog_add_param(copy, p);
   }

   for (int j = 0; j < 2; j++) {
      if (rules[j] == NULL)
         continue;

      const int ncp = vlog_params(rules[j]);
      for (int k = 0; k < ncp; k++)
         vlog_add_param(copy, vlog_param(rules[j], k));
   }

   *newlist = copy;
   return true;
}

static bool elab_copy_all_cb(vlog_node_t v, void *ctx);

typedef struct {
   ident_t name;
   int     width;
   bool    input;   // The actual is not something that can be driven
} bb_port_t;

static bool elab_black_box_vlog_allowed(const elab_ctx_t *ctx)
{
   // A Verilog module has no library, so a list of libraries has to name
   // the work library to include it
   return elab_black_box_allowed(lib_name(ctx->library));
}

static vlog_node_t elab_black_box_module(vlog_node_t list, ident_t modname,
                                         const elab_ctx_t *ctx)
{
   // Generate a module for an instance list whose module cannot be found:
   // its ports are those that the instances connect, with the widths of
   // the actuals and no direction known, and its parameters those that
   // the instances set

   A(bb_port_t) ports = AINIT;
   A(ident_t) params = AINIT;

   const int ninst = vlog_stmts(list);
   for (int i = 0; i < ninst; i++) {
      vlog_node_t inst = vlog_stmt(list, i);
      const int nconns = vlog_params(inst);
      for (int j = 0; j < nconns; j++) {
         vlog_node_t conn = vlog_param(inst, j);
         if (vlog_flags(conn) & VLOG_F_WILDCARD)
            continue;

         ident_t name;
         if (vlog_has_ident(conn))
            name = vlog_ident(conn);
         else
            name = ident_sprintf("p%d", j);

         int width = 1;
         bool input = false;
         if (vlog_has_value(conn)) {
            vlog_node_t value = vlog_value(conn);
            const unsigned size = vlog_width(value);
            if (size > 0 && size < (1 << 20))
               width = size;

            switch (vlog_kind(value)) {
            case V_REF:
            case V_BIT_SELECT:
            case V_PART_SELECT:
            case V_CONCAT:
            case V_MEMBER_REF:
            case V_HIER_REF:
               break;
            default:
               input = true;   // A constant or an expression
               break;
            }
         }

         bool have = false;
         for (int k = 0; k < ports.count && !have; k++) {
            if (ports.items[k].name == name) {
               ports.items[k].width = MAX(ports.items[k].width, width);
               ports.items[k].input &= input;
               have = true;
            }
         }

         if (!have) {
            bb_port_t p = { .name = name, .width = width, .input = input };
            APUSH(ports, p);
         }
      }
   }

   const int nparams = vlog_params(list);
   for (int i = 0; i < nparams; i++) {
      vlog_node_t p = vlog_param(list, i);
      ident_t name = vlog_has_ident(p) ? vlog_ident(p) : ident_sprintf("P%d", i);

      bool have = false;
      for (int k = 0; k < params.count && !have; k++)
         have = params.items[k] == name;

      if (!have)
         APUSH(params, name);
   }

   LOCAL_TEXT_BUF tb = tb_new();
   tb_printf(tb, "module \\%s$bb%u ", istr(modname), (*ctx->nshims)++);

   for (int i = 0; i < params.count; i++)
      tb_printf(tb, "%s \\%s = 0", i == 0 ? "#(parameter" : ", parameter",
                istr(params.items[i]));
   if (params.count > 0)
      tb_cat(tb, ") ");

   tb_cat(tb, "(");
   for (int i = 0; i < ports.count; i++) {
      tb_printf(tb, "%s%s wire", i > 0 ? ", " : "",
                ports.items[i].input ? "input" : "inout");
      if (ports.items[i].width > 1)
         tb_printf(tb, " [%d:0]", ports.items[i].width - 1);
      tb_printf(tb, " \\%s ", istr(ports.items[i].name));
   }
   tb_cat(tb, ");\nendmodule\n");

   ACLEAR(ports);
   ACLEAR(params);

   const int base_errors = error_count();

   // The parser allocates objects in its own arena so set aside the one
   // used for the elaborated design while it runs
   object_arena_t *saved = detach_global_arena();

   file_ref_t file_ref = loc_file_ref("<black-box>", NULL);
   input_from_buffer(tb_get(tb), tb_len(tb), file_ref, SOURCE_VERILOG);

   vlog_node_t mod = vlog_parse();
   if (mod != NULL && vlog_parse() != NULL   // Consume end of file
       && error_count() == base_errors)
      fatal_trace("more than one module in generated black box");

   if (mod != NULL && error_count() == base_errors) {
      vlog_check(mod);
      if (error_count() == base_errors) {
         vlog_simp(mod);
         vlog_set_flags(mod, vlog_flags(mod) | VLOG_F_BLACKBOX);
      }
   }

   freeze_global_arena();
   attach_global_arena(saved);

   if (error_count() != base_errors)
      return NULL;

   return vlog_copy(mod, elab_copy_all_cb, NULL);
}

static void elab_verilog_instance_list(vlog_node_t v, const elab_ctx_t *ctx)
{
   ident_t modname = vlog_ident(v);
   ident_t libname = lib_name(ctx->library);

   ident_t qual;
   object_t *obj = elab_find_instance_unit(v, ctx, &qual);

   vlog_node_t mod = NULL;
   bool black_box = false;
   if (obj == NULL && elab_black_box_vlog_allowed(ctx)) {
      mod = elab_black_box_module(v, modname, ctx);
      if (mod == NULL)
         return;

      elab_black_box(vlog_loc(v), modname, libname);
      black_box = true;
   }
   else if (obj == NULL) {
      error_at(vlog_loc(v), "module %pQ not found in library %pI or any "
               "other library", modname, libname);
      return;
   }
   else
      mod = vlog_from_object(obj);

   if (black_box)
      ;
   else if (mod == NULL) {
      tree_t entity = tree_from_object(obj);
      if (entity != NULL && tree_kind(entity) == T_ENTITY) {
         elab_verilog_vhdl_instance_list(v, entity, ctx);
         return;
      }

      error_at(&obj->loc, "unit %pI is not a Verilog module or VHDL entity",
               qual);
      return;
   }
   else if (vlog_ident2(mod) != modname) {
      diag_t *d = diag_new(DIAG_ERROR, vlog_loc(v));
      diag_printf(d, "name of Verilog module %pQ in library unit %pI "
                  "does not match name %pQ in module instance %pQ",
                  vlog_ident2(mod), qual, modname,
                  vlog_ident(vlog_stmt(v, 0)));
      diag_hint(d, NULL, "this tool does not preserve case sensitivity "
                "in module names");
      diag_emit(d);
      return;
   }

   const int nstmts = vlog_stmts(v);
   for (int i = 0; i < nstmts; i++) {
      vlog_node_t inst = vlog_stmt(v, i);
      assert(vlog_kind(inst) == V_MOD_INST);

      vlog_node_t imod = mod, ilist = v;
      if (ctx->config != NULL && !elab_config_bind(ctx, v, inst, &imod, &ilist))
         continue;

      if (vlog_ranges(inst) == 0) {
         elab_verilog_module(NULL, vlog_ident(inst), imod, ilist, inst, NULL,
                             ctx);
         continue;
      }

      int64_t left, right;
      if (!elab_instance_array_bounds(inst, &left, &right))
         continue;

      const int count = llabs(left - right) + 1;
      for (int j = 0; j < count; j++) {
         const int64_t index = left >= right ? left - j : left + j;
         vlog_node_t elem = elab_array_element(inst, j, count, index);
         elab_verilog_module(NULL, vlog_ident(elem), imod, ilist, elem, NULL,
                             ctx);
      }
   }
}

static void elab_architecture(tree_t inst, tree_t arch, const elab_ctx_t *ctx);

static tree_t elab_constant_field(tree_t ref);

static tree_t elab_constant_aggregate(tree_t t)
{
   // Follow a record generic or constant to the aggregate giving its value

   switch (tree_kind(t)) {
   case T_AGGREGATE:
      return t;
   case T_QUALIFIED:
      return elab_constant_aggregate(tree_value(t));
   case T_REF:
      {
         tree_t decl = tree_ref(t);
         const tree_kind_t kind = tree_kind(decl);
         if ((kind == T_GENERIC_DECL || kind == T_CONST_DECL)
             && tree_has_value(decl))
            return elab_constant_aggregate(tree_value(decl));

         return NULL;
      }
   case T_RECORD_REF:
      {
         tree_t field = elab_constant_field(t);
         return field != NULL ? elab_constant_aggregate(field) : NULL;
      }
   default:
      return NULL;
   }
}

static tree_t elab_constant_field(tree_t ref)
{
   // Value of a field selected from a record with a constant value

   tree_t aggr = elab_constant_aggregate(tree_value(ref));
   if (aggr == NULL)
      return NULL;

   tree_t field = tree_ref(ref);
   const int nassocs = tree_assocs(aggr);
   for (int i = 0; i < nassocs; i++) {
      tree_t a = tree_assoc(aggr, i);
      switch (tree_subkind(a)) {
      case A_POS:
         if (tree_pos(a) == tree_pos(field))
            return tree_value(a);
         break;
      case A_NAMED:
         {
            tree_t name = tree_name(a);
            if (tree_kind(name) == T_REF
                && tree_ident(name) == tree_ident(field))
               return tree_value(a);
         }
         break;
      case A_OTHERS:
         return tree_value(a);
      default:
         break;
      }
   }

   return NULL;
}

static bool elab_is_enum_generic(type_t type)
{
   // User defined enumeration types take the position of the value

   return type_is_enum(type) && !type_eq(type, std_type(NULL, STD_BOOLEAN))
      && !type_eq(type, type_elem(std_type(NULL, STD_STRING)))
      && !type_eq(type, ieee_type(IEEE_STD_ULOGIC));
}

static bool elab_eval_call(tree_t t, const int64_t *arg, int nparams,
                           const elab_ctx_t *ctx, int64_t *result)
{
   // Evaluate a call to a user function with all arguments known

   if (ctx == NULL || tree_kind(tree_ref(t)) == T_GENERIC_DECL)
      return false;

   tree_t call = tree_new(T_FCALL);
   tree_set_loc(call, tree_loc(t));
   tree_set_ident(call, tree_ident(t));
   tree_set_ref(call, tree_ref(t));
   tree_set_type(call, tree_type(t));
   tree_set_flag(call, tree_flags(t));

   for (int i = 0; i < nparams; i++) {
      tree_t p = tree_param(t, i);
      if (tree_subkind(p) != P_POS)
         return false;

      tree_t value = tree_value(p);
      type_t type = tree_type(value);

      tree_t lit = NULL;
      if (type_is_integer(type) || type_is_enum(type))
         lit = get_discrete_lit(value, type, arg[i]);

      if (lit == NULL)
         return false;

      add_param(call, lit, P_POS, NULL);
   }

   if (!eval_possible(call, ctx->registry, ctx->mir))
      return false;

   tree_t folded = eval_try_fold(ctx->jit, call, ctx->registry, NULL, NULL);
   if (folded == call)
      return false;
   else if (folded_int(folded, result))
      return true;
   else if (tree_kind(folded) == T_REF && tree_has_ref(folded)
            && tree_kind(tree_ref(folded)) == T_ENUM_LIT) {
      *result = tree_pos(tree_ref(folded));
      return true;
   }

   return false;
}

static bool elab_eval_generic_expr(tree_t t, tree_t entity,
                                   const int64_t *gvals, int64_t *result,
                                   const elab_ctx_t *ctx)
{
   // Evaluate a simple integer expression involving generics of the
   // entity for which values have been chosen

   switch (tree_kind(t)) {
   case T_LITERAL:
      return folded_int(t, result);
   case T_QUALIFIED:
      return elab_eval_generic_expr(tree_value(t), entity, gvals, result, ctx);
   case T_TYPE_CONV:
      return elab_eval_generic_expr(tree_value(t), entity, gvals, result, ctx);
   case T_RECORD_REF:
      {
         tree_t value = elab_constant_field(t);
         return value != NULL
            && elab_eval_generic_expr(value, entity, gvals, result, ctx);
      }
   case T_REF:
      {
         tree_t decl = tree_ref(t);
         if (tree_kind(decl) == T_GENERIC_DECL) {
            if (entity == NULL)
               return false;

            // Only discrete generics have a value in gvals
            type_t type = tree_type(decl);
            if (!type_is_integer(type) && !elab_is_enum_generic(type)
                && !type_eq(type, std_type(NULL, STD_BOOLEAN)))
               return false;

            const int ngenerics = tree_generics(entity);
            for (int i = 0; i < ngenerics; i++) {
               if (tree_generic(entity, i) == decl) {
                  *result = gvals[i];
                  return true;
               }
            }
         }
         else if (tree_kind(decl) == T_CONST_DECL && tree_has_value(decl))
            return elab_eval_generic_expr(tree_value(decl), entity, gvals,
                                          result, ctx);
         else if (tree_kind(decl) == T_ENUM_LIT
                  && elab_is_enum_generic(tree_type(decl))) {
            *result = tree_pos(decl);
            return true;
         }

         return false;
      }
   case T_FCALL:
      {
         const int nparams = tree_params(t);
         if (nparams < 1 || nparams > 4 || !tree_has_ref(t))
            return false;

         int64_t arg[4] = { 0, 0, 0, 0 };
         for (int i = 0; i < nparams; i++) {
            tree_t value = tree_value(tree_param(t, i));
            if (!elab_eval_generic_expr(value, entity, gvals, &arg[i], ctx))
               return false;
         }

         // Fail rather than overflow or trap
         switch (tree_subkind(tree_ref(t))) {
         case S_NEGATE:
            return nparams == 1 && !__builtin_sub_overflow(0, arg[0], result);
         case S_ADD:
            return nparams == 2
               && !__builtin_add_overflow(arg[0], arg[1], result);
         case S_SUB:
            return nparams == 2
               && !__builtin_sub_overflow(arg[0], arg[1], result);
         case S_MUL:
            return nparams == 2
               && !__builtin_mul_overflow(arg[0], arg[1], result);
         case S_DIV:
            if (arg[1] == 0 || (arg[0] == INT64_MIN && arg[1] == -1))
               return false;
            *result = arg[0] / arg[1];
            return nparams == 2;
         case S_REM:
            if (arg[1] == 0 || (arg[0] == INT64_MIN && arg[1] == -1))
               return false;
            *result = arg[0] % arg[1];
            return nparams == 2;
         case S_MOD:
            if (arg[1] == 0 || (arg[0] == INT64_MIN && arg[1] == -1))
               return false;
            *result = arg[0] % arg[1];
            if (*result != 0 && ((*result < 0) != (arg[1] < 0)))
               *result += arg[1];
            return nparams == 2;
         case S_ABS:
            if (arg[0] == INT64_MIN)
               return false;
            *result = llabs(arg[0]);
            return nparams == 1;
         case S_EXP:
            return nparams == 2 && arg[1] >= 0
               && ipow_safe(arg[0], arg[1], result);
         default:
            return elab_eval_call(t, arg, nparams, ctx, result);
         }
      }
   default:
      return false;
   }
}

static int64_t elab_range_width(tree_t r, int64_t left, int64_t right)
{
   // Returns the number of elements in a range, zero if it is null, or
   // a negative value if there are too many to represent

   const bool up = tree_subkind(r) == RANGE_TO;
   if (up ? left > right : left < right)
      return 0;

   int64_t diff;
   if (__builtin_sub_overflow(up ? right : left, up ? left : right, &diff)
       || diff == INT64_MAX)
      return -1;

   return diff + 1;
}

static bool elab_vlog_real_value(vlog_node_t v, double *value)
{
   switch (vlog_kind(v)) {
   case V_REAL:
      *value = vlog_dval(v);
      return true;
   case V_NUMBER:
      {
         number_t n = vlog_number(v);
         if (!number_is_defined(n))
            return false;
         *value = number_integer(n);
         return true;
      }
   case V_REF:
      return elab_vlog_real_value(vlog_ref(v), value);
   case V_LOCALPARAM:
   case V_PARAM_DECL:
      return vlog_has_value(v) && elab_vlog_real_value(vlog_value(v), value);
   default:
      return false;
   }
}

static bool elab_vlog_string_value(vlog_node_t v, text_buf_t *tb)
{
   switch (vlog_kind(v)) {
   case V_STRING:
   case V_NUMBER:
      {
         // A string literal is a vector with eight bits per character
         number_t n = vlog_number(v);
         if (!number_is_defined(n))
            return false;

         bool leading = true;
         for (int i = number_width(n) / 8 - 1; i >= 0; i--) {
            const uint8_t byte = number_byte(n, i);
            if (byte != 0 || !leading) {
               tb_append(tb, byte);
               leading = false;
            }
         }
         return true;
      }
   case V_REF:
      return elab_vlog_string_value(vlog_ref(v), tb);
   case V_LOCALPARAM:
   case V_PARAM_DECL:
      return vlog_has_value(v) && elab_vlog_string_value(vlog_value(v), tb);
   default:
      return false;
   }
}

static bool elab_is_vector_generic(type_t type)
{
   if (!type_is_array(type) || dimension_of(type) != 1)
      return false;

   type_t elem = type_elem(type);
   return type_eq(elem, ieee_type(IEEE_STD_ULOGIC))
      || type_eq(elem, std_type(NULL, STD_BIT));
}

static char elab_logic_char(tree_t lit)
{
   // Map an element of a bit string to a Verilog four-state value
   switch (istr(tree_ident(lit))[1]) {
   case '0': case 'L': return '0';
   case '1': case 'H': return '1';
   case 'Z':           return 'z';
   default:            return 'x';
   }
}

static bool elab_vlog_number(vlog_node_t v, number_t *n)
{
   switch (vlog_kind(v)) {
   case V_NUMBER:
      *n = vlog_number(v);
      return true;
   case V_REF:
      return elab_vlog_number(vlog_ref(v), n);
   case V_LOCALPARAM:
   case V_PARAM_DECL:
      return vlog_has_value(v) && elab_vlog_number(vlog_value(v), n);
   default:
      return false;
   }
}

static void elab_vhdl_name(ident_t id, text_buf_t *tb)
{
   // Append the name of a VHDL port or generic as it would be spelled in
   // Verilog: basic identifiers are case-insensitive so use lower case
   // and extended identifiers are case-sensitive

   const char *str = istr(id);
   if (str[0] != '\\') {
      tb_istr(tb, id);
      tb_downcase(tb);
      return;
   }

   tb_catn(tb, str + 1, strlen(str) - 2);   // Without the backslashes
}

static bool elab_vlog_name_match(ident_t vlog_id, ident_t vhdl_id)
{
   if (istr(vhdl_id)[0] != '\\')
      return ident_casecmp(vlog_id, vhdl_id);

   LOCAL_TEXT_BUF tb = tb_new();
   elab_vhdl_name(vhdl_id, tb);
   return strcmp(istr(vlog_id), tb_get(tb)) == 0;
}

static bool elab_vlog_name(vlog_node_t node, tree_t decl, text_buf_t *tb)
{
   // Spell the name of a VHDL port or generic as a Verilog escaped
   // identifier so that it cannot be mistaken for a keyword.  Verilog
   // names are case-sensitive so pick the spelling used for the
   // parameter overrides or port connections of node if there is one.

   LOCAL_TEXT_BUF name = tb_new();
   elab_vhdl_name(tree_ident(decl), name);

   const int nparams = vlog_params(node);
   for (int i = 0; i < nparams; i++) {
      vlog_node_t p = vlog_param(node, i);
      if (vlog_has_ident(p)
          && elab_vlog_name_match(vlog_ident(p), tree_ident(decl))) {
         tb_rewind(name);
         tb_istr(name, vlog_ident(p));
         break;
      }
   }

   // An escaped identifier is a backslash followed by printable
   // characters and terminated by white space
   for (const char *p = tb_get(name); *p != '\0'; p++) {
      if (*p <= ' ' || *p > '~') {
         error_at(tree_loc(decl), "name %pI cannot be represented as a "
                  "Verilog identifier for mixed language instantiation",
                  tree_ident(decl));
         return false;
      }
   }

   tb_printf(tb, "\\%s ", tb_get(name));
   return true;
}

static vlog_node_t elab_vlog_param_override(vlog_node_t list, tree_t g,
                                            int nth)
{
   // Find the override given for a generic in the parameter overrides of
   // a Verilog instance list

   const int nparams = vlog_params(list);
   for (int i = 0; i < nparams; i++) {
      vlog_node_t p = vlog_param(list, i);
      if (vlog_has_ident(p)) {
         if (elab_vlog_name_match(vlog_ident(p), tree_ident(g)))
            return p;
      }
      else if (i == nth)
         return p;
   }

   return NULL;
}

static bool elab_vlog_param_value(vlog_node_t list, tree_t g, int nth,
                                  int64_t *value)
{
   vlog_node_t p = elab_vlog_param_override(list, g, nth);
   return p != NULL && vlog_has_value(p)
      && vlog_get_const(vlog_value(p), value);
}

static tree_t elab_generic_default(tree_t g, const elab_ctx_t *ctx)
{
   // Return the default value of a generic, evaluating it if it is not
   // already a literal, or NULL if this is not possible

   if (!tree_has_value(g))
      return NULL;

   tree_t def = tree_value(g);
   switch (tree_kind(def)) {
   case T_LITERAL:
   case T_STRING:
      return def;
   default:
      break;
   }

   if (!eval_possible(def, ctx->registry, ctx->mir))
      return NULL;

   tree_t folded = eval_try_fold(ctx->jit, def, ctx->registry, NULL, NULL);
   return folded != def ? folded : NULL;
}

static bool elab_copy_all_cb(vlog_node_t v, void *ctx)
{
   return true;
}

static bool elab_vector_default(tree_t def, int64_t width, int64_t left,
                                bool up, bool try_eval, const elab_ctx_t *ctx,
                                text_buf_t *tb)
{
   // Append the Verilog four-state values for the default value of a
   // vector generic

   switch (tree_kind(def)) {
   case T_STRING:
      for (int i = 0, n = tree_chars(def); i < n; i++)
         tb_append(tb, elab_logic_char(tree_ref(tree_char(def, i))));
      return true;
   case T_REF:
      {
         tree_t decl = tree_ref(def);
         if (tree_kind(decl) == T_CONST_DECL && tree_has_value(decl))
            return elab_vector_default(tree_value(decl), width, left, up,
                                       try_eval, ctx, tb);
         else
            return false;
      }
   case T_AGGREGATE:
      {
         const int nassocs = tree_assocs(def);

         // Without known bounds only a positional aggregate can be handled
         const int size = width >= 0 ? width : nassocs;
         char *buf LOCAL = xmalloc(size + 1);
         bool *set LOCAL = xcalloc_array(MAX(size, 1), sizeof(bool));
         memset(buf, 'x', size);
         buf[size] = '\0';

         int pos = 0;
         char others = 0;
         for (int i = 0; i < nassocs; i++) {
            tree_t a = tree_assoc(def, i);
            tree_t value = tree_value(a);
            if (tree_kind(value) != T_REF
                || tree_kind(tree_ref(value)) != T_ENUM_LIT)
               goto try_eval;

            const char ch = elab_logic_char(tree_ref(value));

            int64_t lo, hi;
            switch (tree_subkind(a)) {
            case A_POS:
               lo = hi = pos++;
               break;
            case A_OTHERS:
               others = ch;
               continue;
            case A_NAMED:
               {
                  int64_t idx;
                  if (width < 0 || !folded_int(tree_name(a), &idx))
                     goto try_eval;
                  lo = hi = up ? idx - left : left - idx;
               }
               break;
            case A_RANGE:
               {
                  tree_t r = tree_range(a, 0);
                  int64_t l, rr;
                  if (width < 0 || !folded_int(tree_left(r), &l)
                      || !folded_int(tree_right(r), &rr))
                     goto try_eval;
                  lo = up ? l - left : left - l;
                  hi = up ? rr - left : left - rr;
                  if (lo > hi) {
                     const int64_t tmp = lo;
                     lo = hi;
                     hi = tmp;
                  }
               }
               break;
            default:
               goto try_eval;
            }

            if (lo < 0 || hi >= size)
               goto try_eval;

            for (int64_t j = lo; j <= hi; j++) {
               buf[j] = ch;
               set[j] = true;
            }
         }

         for (int i = 0; i < size; i++) {
            if (!set[i] && others == 0)
               goto try_eval;
            else if (!set[i])
               buf[i] = others;
         }

         tb_cat(tb, buf);
         return true;
      }
   default:
      break;
   }

 try_eval:
   if (!try_eval || !eval_possible(def, ctx->registry, ctx->mir))
      return false;

   // The result is a string or positional aggregate if it succeeded
   tree_t folded = eval_try_fold(ctx->jit, def, ctx->registry, NULL, NULL);
   if (folded == def)
      return false;

   tb_rewind(tb);
   return elab_vector_default(folded, width, left, up, false, ctx, tb);
}

static vlog_node_t elab_no_default(tree_t g, tree_t entity)
{
   error_at(tree_loc(g), "cannot evaluate default value of generic %pI of "
            "VHDL entity %pI so it must be set by a parameter override in "
            "the Verilog instance", tree_ident(g), tree_ident(entity));
   return NULL;
}

typedef struct {
   tree_t              entity;
   const int64_t      *gvals;
   const elab_ctx_t   *ctx;
} generic_env_t;

static bool elab_composite_length(tree_t r, int64_t *len,
                                  const generic_env_t *env)
{
   // The bounds may depend on a field of a record generic's default value

   if (folded_length(r, len))
      return true;

   int64_t left, right;
   if (tree_subkind(r) == RANGE_EXPR
       || !elab_eval_generic_expr(tree_left(r), env->entity, env->gvals,
                                  &left, env->ctx)
       || !elab_eval_generic_expr(tree_right(r), env->entity, env->gvals,
                                  &right, env->ctx))
      return false;

   *len = elab_range_width(r, left, right);
   return *len >= 0;
}

static int64_t elab_composite_width(type_t type, const generic_env_t *env)
{
   // Number of bits in the flattened form of a composite port, or a
   // negative value if it cannot be represented

   if (type_is_record(type)) {
      type_t base = type_base_recur(type);

      int64_t total = 0;
      for (int i = 0, n = type_fields(base); i < n; i++) {
         const int64_t w = elab_composite_width(tree_type(type_field(base, i)), env);
         if (w < 0)
            return -1;

         total += w;
      }

      return total;
   }
   else if (type_is_array(type)) {
      if (type_is_unconstrained(type))
         return -1;

      int64_t total = elab_composite_width(type_elem(type), env);
      if (total < 0)
         return -1;

      for (int i = 0, n = dimension_of(type); i < n; i++) {
         int64_t len;
         tree_t r = range_of(type, i);
         if (r == NULL || !elab_composite_length(r, &len, env))
            return -1;

         total *= len;
      }

      return total;
   }
   else if (type_is_integer(type))
      return 32;
   else if (type_eq(type_base_recur(type), ieee_type(IEEE_STD_ULOGIC)))
      return 1;
   else
      return -1;
}

static inline bool elab_is_composite_port(type_t type)
{
   return type_is_record(type)
      || (type_is_array(type) && (type_is_record(type_elem(type))
                                  || type_is_array(type_elem(type))
                                  || type_is_integer(type_elem(type))
                                  || dimension_of(type) > 1));
}

static bool elab_connect_composite(tree_t inst, tree_t eport, tree_t formal,
                                   tree_t vport, int64_t width,
                                   int64_t *offset, const generic_env_t *env);

static bool elab_connect_leaf(tree_t inst, tree_t eport, tree_t formal,
                              tree_t vport, int64_t width, int64_t *offset,
                              const generic_env_t *env)
{
   // Connect part of the flat Verilog port to this element of the record
   // where the first element is the leftmost bits

   type_t ftype = tree_type(formal);
   type_t vtype = tree_type(vport);
   type_t ptype = type_base_recur(vtype);

   tree_t actual;
   type_t atype;
   int64_t nbits = 1;

   type_t t_integer = std_type(NULL, STD_INTEGER);

   if (type_is_array(ftype) || type_is_integer(ftype)) {
      if (type_is_integer(ftype))
         nbits = 32;
      else {
         tree_t fr = range_of(ftype, 0);
         if (fr == NULL || !elab_composite_length(fr, &nbits, env))
            return false;
      }

      const int64_t hi = width - 1 - *offset;
      const int64_t lo = hi - nbits + 1;

      tree_t r = tree_new(T_RANGE);
      tree_set_subkind(r, RANGE_DOWNTO);
      tree_set_left(r, get_int_lit(eport, t_integer, hi));
      tree_set_right(r, get_int_lit(eport, t_integer, lo));
      tree_set_type(r, t_integer);
      tree_set_loc(r, tree_loc(eport));

      tree_t c = tree_new(T_CONSTRAINT);
      tree_set_subkind(c, C_INDEX);
      tree_set_loc(c, tree_loc(eport));
      tree_add_range(c, r);

      atype = type_new(T_SUBTYPE);
      type_set_base(atype, ptype);
      type_set_constraint(atype, c);

      actual = tree_new(T_ARRAY_SLICE);
      tree_set_value(actual, make_ref(vport));
      tree_add_range(actual, r);
      tree_set_type(actual, atype);
      tree_set_loc(actual, tree_loc(eport));
   }
   else {
      atype = type_elem(ptype);

      tree_t idx = tree_new(T_PARAM);
      tree_set_subkind(idx, P_POS);
      tree_set_pos(idx, 0);
      tree_set_value(idx, get_int_lit(eport, t_integer, width - 1 - *offset));
      tree_set_loc(idx, tree_loc(eport));

      actual = tree_new(T_ARRAY_REF);
      tree_set_value(actual, make_ref(vport));
      tree_add_param(actual, idx);
      tree_set_type(actual, atype);
      tree_set_loc(actual, tree_loc(eport));
   }

   *offset += nbits;

   tree_t conv = tree_new(T_CONV_FUNC);
   tree_set_loc(conv, tree_loc(eport));

   if (tree_subkind(eport) == PORT_IN) {
      tree_t func = elab_to_vhdl(atype, ftype, true);
      if (func == NULL)
         return false;

      tree_set_ref(conv, func);
      tree_set_ident(conv, tree_ident(func));
      tree_set_type(conv, type_result(tree_type(func)));
      tree_set_value(conv, actual);

      add_param(inst, conv, P_NAMED, formal);
   }
   else {
      tree_t func = elab_to_verilog(ftype, atype, true);
      if (func == NULL)
         return false;

      tree_set_ref(conv, func);
      tree_set_ident(conv, tree_ident(func));
      tree_set_type(conv, type_result(tree_type(func)));
      tree_set_value(conv, formal);

      add_param(inst, actual, P_NAMED, conv);
   }

   return true;
}

static bool elab_connect_array(tree_t inst, tree_t eport, tree_t formal,
                               tree_t vport, int64_t width, int64_t *offset,
                               int dim, int64_t *index,
                               const generic_env_t *env)
{
   type_t type = tree_type(formal);

   if (dim == dimension_of(type)) {
      tree_t ref = tree_new(T_ARRAY_REF);
      tree_set_value(ref, formal);
      tree_set_type(ref, type_elem(type));
      tree_set_loc(ref, tree_loc(eport));

      for (int i = 0; i < dim; i++) {
         tree_t idx = tree_new(T_PARAM);
         tree_set_subkind(idx, P_POS);
         tree_set_pos(idx, i);
         tree_set_value(idx, get_int_lit(eport, index_type_of(type, i),
                                         index[i]));
         tree_set_loc(idx, tree_loc(eport));
         tree_add_param(ref, idx);
      }

      return elab_connect_composite(inst, eport, ref, vport, width, offset, env);
   }

   tree_t r = range_of(type, dim);
   int64_t left, right;
   if (tree_subkind(r) == RANGE_EXPR
       || !elab_eval_generic_expr(tree_left(r), env->entity, env->gvals,
                                  &left, env->ctx)
       || !elab_eval_generic_expr(tree_right(r), env->entity, env->gvals,
                                  &right, env->ctx))
      return false;
   else if (elab_range_width(r, left, right) == 0)
      return true;

   const int64_t step = tree_subkind(r) == RANGE_DOWNTO ? -1 : 1;
   for (int64_t i = left;; i += step) {
      index[dim] = i;
      if (!elab_connect_array(inst, eport, formal, vport, width, offset,
                              dim + 1, index, env))
         return false;
      else if (i == right)
         break;
   }

   return true;
}

static bool elab_connect_composite(tree_t inst, tree_t eport, tree_t formal,
                                   tree_t vport, int64_t width, int64_t *offset,
                              const generic_env_t *env)
{
   type_t type = tree_type(formal);

   if (type_is_record(type)) {
      type_t base = type_base_recur(type);

      for (int i = 0, n = type_fields(base); i < n; i++) {
         tree_t f = type_field(base, i);

         tree_t ref = tree_new(T_RECORD_REF);
         tree_set_value(ref, formal);
         tree_set_ident(ref, tree_ident(f));
         tree_set_ref(ref, f);
         tree_set_type(ref, tree_type(f));
         tree_set_loc(ref, tree_loc(eport));

         if (!elab_connect_composite(inst, eport, ref, vport, width, offset, env))
            return false;
      }

      return true;
   }
   else if (elab_is_composite_port(type)) {
      int64_t index[4];
      assert(dimension_of(type) <= ARRAY_LEN(index));
      return elab_connect_array(inst, eport, formal, vport, width, offset,
                                0, index, env);
   }
   else
      return elab_connect_leaf(inst, eport, formal, vport, width, offset, env);
}

static bool elab_is_simple_generic(type_t type)
{
   // Other types have no Verilog equivalent and can only take their default

   return type_is_real(type) || type_eq(type, std_type(NULL, STD_STRING))
      || elab_is_vector_generic(type) || type_is_integer(type)
      || elab_is_enum_generic(type)
      || type_eq(type, std_type(NULL, STD_BOOLEAN));
}

static vlog_node_t elab_vhdl_shim_module(vlog_node_t list, tree_t entity,
                                         const elab_ctx_t *ctx)
{
   // Generate a Verilog module with the same ports as the VHDL entity
   // which can be instantiated from Verilog and bound to the entity

   // The module name must differ from the entity and from the shims for
   // other instance lists as it also names the unit and arena
   LOCAL_TEXT_BUF tb = tb_new();
   tb_printf(tb, "module \\%s$%u ", istr(vlog_ident(list)), (*ctx->nshims)++);

   // An override for a name that is not a generic of the entity is
   // ignored with a warning
   const int noverrides = vlog_params(list);
   LOCAL_BIT_MASK extra;
   mask_init(&extra, MAX(noverrides, 1));
   int nextra = 0;
   for (int i = 0; i < noverrides; i++) {
      vlog_node_t p = vlog_param(list, i);
      if (!vlog_has_ident(p))
         continue;

      bool found = false;
      for (int j = 0; j < tree_generics(entity) && !found; j++) {
         tree_t g = tree_generic(entity, j);
         found = elab_vlog_name_match(vlog_ident(p), tree_ident(g));
      }

      if (!found) {
         warn_at(vlog_loc(p), "parameter %pQ not found in VHDL entity %pI, "
                 "ignoring the override", vlog_ident(p), tree_ident(entity));
         mask_set(&extra, i);
         nextra++;
      }
   }

   // Each VHDL generic becomes a Verilog parameter
   const int ngenerics = tree_generics(entity);
   int64_t *gvals LOCAL = xcalloc_array(MAX(ngenerics, 1), sizeof(int64_t));
   for (int i = 0; i < ngenerics; i++) {
      tree_t g = tree_generic(entity, i);
      type_t type = tree_type(g);

      if (tree_class(g) != C_CONSTANT) {
         error_at(tree_loc(g), "generic %pI of VHDL entity %pI is not a "
                  "constant", tree_ident(g), tree_ident(entity));
         return NULL;
      }

      LOCAL_TEXT_BUF nametb = tb_new();
      if (!elab_vlog_name(list, g, nametb))
         return NULL;

      const char *name = tb_get(nametb);

      tb_printf(tb, "%s ", i == 0 ? "#(parameter" : ", parameter");

      const bool overridden =
         elab_vlog_param_override(list, g, i) != NULL;

      if (type_is_real(type)) {
         tb_printf(tb, "real %s", name);

         tree_t def = overridden ? NULL : elab_generic_default(g, ctx);
         if (def != NULL && tree_kind(def) == T_LITERAL
             && tree_subkind(def) == L_REAL) {
            char buf[64];
            checked_sprintf(buf, sizeof(buf), "%.17g", tree_dval(def));
            tb_printf(tb, " = %s%s", buf,
                      strpbrk(buf, ".en") == NULL ? ".0" : "");
         }
         else if (!overridden && tree_has_value(g))
            return elab_no_default(g, entity);
      }
      else if (type_eq(type, std_type(NULL, STD_STRING))) {
         tb_printf(tb, "%s", name);

         tree_t str = overridden ? NULL : elab_generic_default(g, ctx);
         if (str != NULL && tree_kind(str) == T_STRING) {
            tb_cat(tb, " = \"");
            for (int j = 0, n = tree_chars(str); j < n; j++) {
               const int ch = assume_int(tree_char(str, j));
               if (ch == '"' || ch == '\\')
                  tb_append(tb, '\\');
               if (ch >= ' ' && ch < 127)
                  tb_append(tb, ch);
            }
            tb_append(tb, '"');
         }
         else if (!overridden && tree_has_value(g))
            return elab_no_default(g, entity);
      }
      else if (elab_is_vector_generic(type)) {
         int64_t left = 0, right, width = -1;
         tree_t r = NULL;
         if (!type_is_unconstrained(type) && (r = range_of(type, 0)) != NULL
             && elab_eval_generic_expr(tree_left(r), entity, gvals, &left, ctx)
             && elab_eval_generic_expr(tree_right(r), entity, gvals, &right,
                                       ctx)) {
            width = elab_range_width(r, left, right);
            if (width == 0) {
               error_at(tree_loc(g), "generic %pI of VHDL entity %pI has a "
                        "null range which cannot be set from Verilog",
                        tree_ident(g), tree_ident(entity));
               return NULL;
            }
         }

         if (width < 0 && overridden) {
            // An unconstrained generic takes the width of the actual
            vlog_node_t o = elab_vlog_param_override(list, g, i);
            if (o != NULL && vlog_has_value(o)
                && vlog_kind(vlog_value(o)) == V_NUMBER)
               width = vlog_width(vlog_value(o));
         }

         LOCAL_TEXT_BUF bits = tb_new();
         const bool have_def = tree_has_value(g)
            && elab_vector_default(tree_value(g), width, left,
                                   width >= 0 && r != NULL
                                   && tree_subkind(r) == RANGE_TO,
                                   true, ctx, bits);

         if (!have_def && !overridden && tree_has_value(g))
            return elab_no_default(g, entity);
         else if (width < 0 && have_def)
            width = tb_len(bits);
         else if (width < 0) {
            error_at(tree_loc(g), "generic %pI of VHDL entity %pI must have "
                     "constant bounds or a default value to be set from "
                     "Verilog", tree_ident(g), tree_ident(entity));
            return NULL;
         }

         tb_printf(tb, "[%"PRIi64":0] %s", width - 1, name);

         if (have_def)
            tb_printf(tb, " = %d'b%s", (int)tb_len(bits), tb_get(bits));
      }
      else if (type_is_integer(type) || elab_is_enum_generic(type)
               || type_eq(type, std_type(NULL, STD_BOOLEAN))) {
         tb_cat(tb, name);

         if (overridden)
            (void)elab_vlog_param_value(list, g, i, &gvals[i]);
         else if (tree_has_value(g)) {
            bool b;
            tree_t def;
            if (elab_eval_generic_expr(tree_value(g), entity, gvals,
                                       &gvals[i], ctx))
               tb_printf(tb, " = %"PRIi64, gvals[i]);
            else if (folded_bool(tree_value(g), &b)) {
               gvals[i] = b;
               tb_printf(tb, " = %d", b);
            }
            else if ((def = elab_generic_default(g, ctx)) != NULL
                     && folded_int(def, &gvals[i]))
               tb_printf(tb, " = %"PRIi64, gvals[i]);
            else if (def != NULL && folded_bool(def, &b)) {
               gvals[i] = b;
               tb_printf(tb, " = %d", b);
            }
            else
               return elab_no_default(g, entity);
         }
      }
      else if (!overridden && tree_has_value(g))
         tb_printf(tb, "%s = 0", name);   // Keeps its default value
      else {
         error_at(tree_loc(g), "only generics of integer, BOOLEAN, REAL, "
                  "STRING, or bit vector type are supported for mixed "
                  "language instantiation");
         return NULL;
      }

      if (i == ngenerics - 1 && nextra == 0)
         tb_cat(tb, ") ");
   }

   for (int i = 0, n = 0; i < noverrides; i++) {
      if (!mask_test(&extra, i))
         continue;

      tb_printf(tb, "%s \\%s = 0", ngenerics + n == 0 ? "#(parameter"
                : ", parameter", istr(vlog_ident(vlog_param(list, i))));

      if (++n == nextra)
         tb_cat(tb, ") ");
   }

   tb_append(tb, '(');

   assert(vlog_stmts(list) > 0);
   vlog_node_t first = vlog_stmt(list, 0);

   const generic_env_t env = { entity, gvals, ctx };

   const int nports = tree_ports(entity);
   for (int i = 0; i < nports; i++) {
      tree_t p = tree_port(entity, i);

      const char *dir;
      switch (tree_subkind(p)) {
      case PORT_IN:  dir = "input"; break;
      case PORT_OUT:
      case PORT_BUFFER: dir = "output"; break;
      case PORT_INOUT: dir = "inout"; break;
      default:
         error_at(tree_loc(p), "only input, output, buffer, and inout ports "
                  "are supported for mixed language instantiation");
         return NULL;
      }

      type_t type = tree_type(p);

      char range[64] = "";
      if (type_is_integer(type))
         checked_sprintf(range, sizeof(range), " [31:0]");
      else if (elab_is_composite_port(type)) {
         // Reported when the ports are connected if it cannot be flattened
         const int64_t width = elab_composite_width(type, &env);
         if (width > 0)
            checked_sprintf(range, sizeof(range), " [%"PRIi64":0]", width - 1);
      }
      else if (type_is_array(type)) {
         int64_t left, right;
         tree_t r;
         if (type_is_unconstrained(type) || (r = range_of(type, 0)) == NULL) {
            error_at(tree_loc(p), "port %pI of VHDL entity %pI must have "
                     "constant bounds to be instantiated from Verilog",
                     tree_ident(p), tree_ident(entity));
            return NULL;
         }
         else if (!elab_eval_generic_expr(tree_left(r), entity, gvals, &left,
                                          ctx)
                  || !elab_eval_generic_expr(tree_right(r), entity, gvals,
                                             &right, ctx)
                  || elab_range_width(r, left, right) < 0) {
            error_at(tree_loc(p), "cannot compute the bounds of port %pI of "
                     "VHDL entity %pI which may only depend on integer and "
                     "BOOLEAN generics to be instantiated from Verilog",
                     tree_ident(p), tree_ident(entity));
            return NULL;
         }
         else if (elab_range_width(r, left, right) == 0) {
            error_at(tree_loc(p), "port %pI of VHDL entity %pI has a null "
                     "range which cannot be instantiated from Verilog",
                     tree_ident(p), tree_ident(entity));
            return NULL;
         }

         checked_sprintf(range, sizeof(range), " [%"PRIi64":%"PRIi64"]",
                         left, right);
      }

      LOCAL_TEXT_BUF name = tb_new();
      if (!elab_vlog_name(first, p, name))
         return NULL;

      tb_printf(tb, "%s%s wire%s %s", i > 0 ? ", " : "", dir, range,
                tb_get(name));
   }

   tb_cat(tb, ");\nendmodule\n");

   const int base_errors = error_count();

   // The parser allocates objects in its own arena so set aside the one
   // used for the elaborated design while it runs
   object_arena_t *saved = detach_global_arena();

   file_ref_t file_ref = loc_file_ref("<vhdl-entity-shim>", NULL);
   input_from_buffer(tb_get(tb), tb_len(tb), file_ref, SOURCE_VERILOG);

   vlog_node_t mod = vlog_parse();
   if (mod != NULL && vlog_parse() != NULL   // Consume end of file
       && error_count() == base_errors)
      fatal_trace("more than one module in generated shim");

   if (mod != NULL && error_count() == base_errors) {
      vlog_check(mod);
      if (error_count() == base_errors)
         vlog_simp(mod);
   }

   freeze_global_arena();
   attach_global_arena(saved);

   if (error_count() != base_errors)
      return NULL;

   // Copy into the arena for the elaborated design so that it does not
   // depend on a unit that cannot be loaded from a library
   return vlog_copy(mod, elab_copy_all_cb, NULL);
}

static bool elab_verilog_port_open(vlog_node_t inst, tree_t eport, int pos)
{
   // True if this instance has no connection for the VHDL port

   const int nparams = vlog_params(inst);
   for (int i = 0; i < nparams; i++) {
      vlog_node_t conn = vlog_param(inst, i);
      if (vlog_has_ident(conn)
          ? elab_vlog_name_match(vlog_ident(conn), tree_ident(eport))
          : i == pos)
         return !vlog_has_value(conn);
   }

   return true;
}

static void elab_vhdl_in_verilog(tree_t entity, vlog_node_t inst_v,
                                 const elab_instance_t *ei, elab_ctx_t *ctx)
{
   // Instantiate the VHDL entity inside the shim block and connect its
   // ports to the ports of the shim

   ident_t name = ident_rfrom(tree_ident(entity), '.') ?: tree_ident(entity);

   tree_t inst = tree_new(T_INSTANCE);
   tree_set_ident(inst, name);
   tree_set_loc(inst, tree_loc(entity));
   tree_set_class(inst, C_ENTITY);
   tree_set_ref(inst, entity);

   const int ngenerics = tree_generics(entity);
   int64_t *gvals LOCAL = xcalloc_array(MAX(ngenerics, 1), sizeof(int64_t));
   for (int i = 0; i < ngenerics; i++) {
      tree_t g = tree_generic(entity, i);
      type_t type = tree_type(g);

      tree_t value = NULL;
      const int ndecls = elab_is_simple_generic(type) ? vlog_decls(ei->body) : 0;
      for (int j = 0; j < ndecls; j++) {
         vlog_node_t d = vlog_decl(ei->body, j);
         if (vlog_kind(d) != V_LOCALPARAM
             || !elab_vlog_name_match(vlog_ident(d), tree_ident(g)))
            continue;

         int64_t ival;
         double dval;
         if (type_is_real(type)) {
            if (!elab_vlog_real_value(d, &dval)) {
               error_at(tree_loc(g), "value of parameter %pQ is not a "
                        "constant real", vlog_ident(d));
               return;
            }

            value = get_real_lit(g, type, dval);
         }
         else if (type_eq(type, std_type(NULL, STD_STRING))) {
            LOCAL_TEXT_BUF tb = tb_new();
            if (!elab_vlog_string_value(d, tb)) {
               error_at(tree_loc(g), "value of parameter %pQ is not a "
                        "constant string", vlog_ident(d));
               return;
            }

            value = tree_new(T_STRING);
            tree_set_loc(value, tree_loc(g));

            type_t elem = type_base_recur(type_elem(type));
            const char *str = tb_get(tb);
            for (int k = 0; k < tb_len(tb); k++) {
               tree_t lit = type_enum_literal(elem, (unsigned char)str[k]);
               tree_add_char(value, make_ref(lit));
            }

            tree_set_type(value, subtype_for_string(value, type));
         }
         else if (elab_is_vector_generic(type)) {
            number_t num;
            if (!elab_vlog_number(d, &num)) {
               error_at(tree_loc(g), "value of parameter %pQ is not a "
                        "constant vector", vlog_ident(d));
               return;
            }

            type_t elem = type_base_recur(type_elem(type));
            const bool is_bit = type_eq(elem, std_type(NULL, STD_BIT));

            // The override takes the width of the parameter as in a
            // Verilog assignment unless the generic is unconstrained
            const unsigned nbits = number_width(num);
            const unsigned width =
               type_is_unconstrained(type) ? nbits : vlog_width(d);
            const vlog_logic_t ext =
               number_signed(num) ? number_bit(num, nbits - 1) : LOGIC_0;

            value = tree_new(T_STRING);
            tree_set_loc(value, tree_loc(g));

            for (int k = width - 1; k >= 0; k--) {
               const vlog_logic_t bit = k < nbits ? number_bit(num, k) : ext;
               const char ch = "01zx"[bit];
               if (is_bit && ch != '0' && ch != '1') {
                  error_at(tree_loc(g), "parameter %pQ has undefined bits "
                           "which cannot be converted to type %pT",
                           vlog_ident(d), type);
                  return;
               }

               const char lname[] = { '\'', toupper_iso88591(ch), '\'', 0 };
               tree_t lit = NULL;
               for (int l = 0, n = type_enum_literals(elem); l < n; l++) {
                  tree_t it = type_enum_literal(elem, l);
                  if (strcmp(istr(tree_ident(it)), lname) == 0) {
                     lit = it;
                     break;
                  }
               }
               assert(lit != NULL);

               tree_add_char(value, make_ref(lit));
            }

            tree_set_type(value, subtype_for_string(value, type));
         }
         else if (!vlog_get_const(d, &ival)) {
            error_at(tree_loc(g), "value of parameter %pQ is not a defined "
                     "constant", vlog_ident(d));
            return;
         }
         else if (type_is_integer(type))
            value = get_int_lit(g, type, ival);
         else if (elab_is_enum_generic(type)) {
            value = get_discrete_lit(g, type, ival);
            if (value == NULL) {
               error_at(tree_loc(g), "value %"PRIi64" of parameter %pQ is out "
                        "of range for type %pT", ival, vlog_ident(d), type);
               return;
            }
         }
         else
            value = get_enum_lit(g, type, ival != 0);
         break;
      }

      if (value == NULL && tree_has_value(g))
         value = tree_value(g);
      else if (value == NULL) {
         error_at(tree_loc(g), "missing value for generic %pI with no "
                  "default", tree_ident(g));
         return;
      }

      tree_t map = tree_new(T_PARAM);
      tree_set_loc(map, tree_loc(g));
      tree_set_subkind(map, P_POS);
      tree_set_pos(map, i);
      tree_set_value(map, value);

      tree_add_genmap(inst, map);

      bool bval;
      if (!folded_int(value, &gvals[i]) && folded_bool(value, &bval))
         gvals[i] = bval;
      else if (tree_kind(value) == T_REF && tree_has_ref(value)
               && tree_kind(tree_ref(value)) == T_ENUM_LIT)
         gvals[i] = tree_pos(tree_ref(value));
   }

   const generic_env_t env = { entity, gvals, ctx };

   const int nports = tree_ports(entity);
   assert(tree_ports(ei->block) == nports);

   bool have_named = false;
   for (int i = 0; i < nports; i++) {
      tree_t eport = tree_port(entity, i);
      tree_t vport = tree_port(ei->block, i);

      type_t btype = tree_type(eport);
      type_t vtype = tree_type(vport);

      if (tree_subkind(eport) == PORT_IN && tree_has_value(eport)
          && elab_verilog_port_open(inst_v, eport, i)) {
         // Leave the port open so that it takes its default value
         have_named = true;
         continue;
      }

      if (elab_is_composite_port(btype)
          && elab_composite_width(btype, &env) > 0) {
         if (tree_subkind(eport) == PORT_INOUT) {
            error_at(tree_loc(eport), "sorry, inout port %pI with composite "
                     "type cannot be instantiated from Verilog",
                     tree_ident(eport));
            return;
         }

         int64_t offset = 0;
         if (!elab_connect_composite(inst, eport, make_ref(eport), vport,
                                     elab_composite_width(btype, &env), &offset,
                                     &env)) {
            error_at(tree_loc(eport), "cannot connect Verilog net to VHDL "
                     "port %pI with type %pT", tree_ident(eport), btype);
            return;
         }

         have_named = true;
         continue;
      }

      tree_t conv = tree_new(T_CONV_FUNC);
      tree_set_loc(conv, tree_loc(eport));

      if (tree_subkind(eport) == PORT_INOUT) {
         // Convert in both directions
         tree_t in_func = elab_to_vhdl(vtype, btype, true);
         tree_t out_func = elab_to_verilog(btype, vtype, true);
         if (in_func == NULL || out_func == NULL) {
            error_at(tree_loc(eport), "cannot connect Verilog net to VHDL "
                     "inout port %pI with type %pT", tree_ident(eport), btype);
            return;
         }

         tree_set_ref(conv, in_func);
         tree_set_ident(conv, tree_ident(in_func));
         tree_set_type(conv, type_result(tree_type(in_func)));
         tree_set_value(conv, make_ref(vport));

         tree_t oconv = tree_new(T_CONV_FUNC);
         tree_set_loc(oconv, tree_loc(eport));
         tree_set_ref(oconv, out_func);
         tree_set_ident(oconv, tree_ident(out_func));
         tree_set_type(oconv, type_result(tree_type(out_func)));
         tree_set_value(oconv, make_ref(eport));

         add_param(inst, conv, P_NAMED, oconv);
         have_named = true;
      }
      else if (tree_subkind(eport) == PORT_IN) {
         tree_t func = elab_to_vhdl(vtype, btype, true);
         if (func == NULL) {
            error_at(tree_loc(eport), "cannot connect Verilog net to VHDL "
                     "input port %pI with type %pT", tree_ident(eport), btype);
            return;
         }

         tree_set_ref(conv, func);
         tree_set_ident(conv, tree_ident(func));
         tree_set_type(conv, type_result(tree_type(func)));
         tree_set_value(conv, make_ref(vport));

         if (have_named)
            add_param(inst, conv, P_NAMED, make_ref(eport));
         else
            add_param(inst, conv, P_POS, NULL);
      }
      else {
         tree_t func = elab_to_verilog(btype, vtype, true);
         if (func == NULL) {
            error_at(tree_loc(eport), "cannot connect VHDL output port %pI "
                     "with type %pT to Verilog net", tree_ident(eport), btype);
            return;
         }

         tree_set_ref(conv, func);
         tree_set_ident(conv, tree_ident(func));
         tree_set_type(conv, type_result(tree_type(func)));
         tree_set_value(conv, make_ref(eport));

         add_param(inst, make_ref(vport), P_NAMED, conv);
         have_named = true;
      }
   }

   // The shim block is lowered to MIR and has no VHDL parent unit so
   // create a placeholder that exposes its port variables to the entity
   ident_t qual = ctx->cloned;
   assert(qual != NULL);

   mir_shape_t *shape = mir_get_shape(ctx->mir, qual);
   assert(shape != NULL);

   int *slots LOCAL = xmalloc_array(nports, sizeof(int));
   for (int i = 0; i < nports; i++) {
      slots[i] = mir_find_slot(shape, tree_ident(tree_port(ctx->out, i)));
      assert(slots[i] >= 0);
   }

   ctx->lowered = lower_verilog_parent(ctx->registry, qual, ctx->dotted,
                                       ctx->out, slots);

   tree_t arch = elab_pick_arch(entity, tree_loc(entity), ctx);
   elab_architecture(inst, arch, ctx);

   ctx->lowered = NULL;   // Placeholder is never emitted
}

static void elab_verilog_vhdl_instance_list(vlog_node_t v, tree_t entity,
                                            const elab_ctx_t *ctx)
{
   v = elab_resolve_param_hrefs(v, ctx);

   vlog_node_t shim = elab_vhdl_shim_module(v, entity, ctx);
   if (shim == NULL)
      return;

   const int nstmts = vlog_stmts(v);
   for (int i = 0; i < nstmts; i++) {
      vlog_node_t inst = vlog_stmt(v, i);
      assert(vlog_kind(inst) == V_MOD_INST);

      if (vlog_ranges(inst) == 0) {
         elab_verilog_module(NULL, vlog_ident(inst), shim, v, inst, entity,
                             ctx);
         continue;
      }

      int64_t left, right;
      if (!elab_instance_array_bounds(inst, &left, &right))
         continue;

      const int count = llabs(left - right) + 1;
      for (int j = 0; j < count; j++) {
         const int64_t index = left >= right ? left - j : left + j;
         vlog_node_t elem = elab_array_element(inst, j, count, index);
         elab_verilog_module(NULL, vlog_ident(elem), shim, v, elem, entity,
                             ctx);
      }
   }
}

static void elab_verilog_block(vlog_node_t v, const elab_ctx_t *ctx)
{
   ident_t id = vlog_ident(v);

   tree_t b = tree_new(T_BLOCK);
   tree_set_ident(b, id);
   tree_set_loc(b, vlog_loc(v));

   tree_add_stmt(ctx->out, b);

   ident_t ndotted = ident_prefix(ctx->dotted, id, '.');

   elab_ctx_t new_ctx = {
      .out    = b,
      .dotted = ndotted,
   };
   elab_inherit_context(&new_ctx, ctx);

   new_ctx.defparams  = ctx->defparams;
   new_ctx.ndefparams = ctx->ndefparams;

   if (ctx->ndefparams > 0) {
      // A path through the block continues inside it
      elab_defparam_t *all =
         pool_malloc(ctx->pool, 2 * ctx->ndefparams * sizeof(elab_defparam_t));
      unsigned nall = 0;
      for (unsigned i = 0; i < ctx->ndefparams; i++) {
         const elab_defparam_t *dp = &(ctx->defparams[i]);
         all[nall++] = *dp;
         if (dp->path[0] == id && dp->npath > 1) {
            *dp->passed = true;
            all[nall] = *dp;
            all[nall].path++;
            all[nall].npath--;
            nall++;
         }
      }

      new_ctx.defparams = all;
      new_ctx.ndefparams = nall;
   }

   // TODO: use elab_instance_t to cache this

   ident_t id2 = ident_prefix(ctx->dotted, vlog_ident(v), '%');
   vlog_set_instance_scope(NULL);
   vlog_node_t body = vlog_new_instance(v, NULL, id2);
   vlog_fold(body, ctx->mir, ctx->jit);

   unsigned nbody = 0;
   const int nbstmts = vlog_stmts(body);
   for (int i = 0; i < nbstmts; i++) {
      if (vlog_kind(vlog_stmt(body, i)) == V_DEFPARAM)
         nbody++;
   }

   if (nbody > 0) {
      // The last defparam in a module for a parameter takes effect
      elab_defparam_t *all = pool_malloc(
         ctx->pool, (new_ctx.ndefparams + nbody) * sizeof(elab_defparam_t));
      unsigned nown = 0;
      elab_collect_defparams(body, all, &nown, ctx, ctx->dp_home);

      // The name of the block containing the defparam may start the path
      for (unsigned i = 0; i < nown; i++) {
         elab_defparam_t *dp = &(all[i]);
         if (dp->path[0] == id && dp->npath > 1) {
            dp->path++;
            dp->npath--;
         }
      }

      unsigned nall = 0;
      elab_defparam_t *merged = pool_malloc(
         ctx->pool, (new_ctx.ndefparams + nown) * sizeof(elab_defparam_t));
      unsigned next = 0;
      for (unsigned i = 0; i < new_ctx.ndefparams; i++) {
         const elab_defparam_t *dp = &(new_ctx.defparams[i]);
         for (; next < nown && dp->home == all[next].home
                 && all[next].line < dp->line; next++)
            merged[nall++] = all[next];
         merged[nall++] = *dp;
      }
      for (; next < nown; next++)
         merged[nall++] = all[next];

      new_ctx.defparams = merged;
      new_ctx.ndefparams = nall;
   }

   tree_t wrap = tree_new(T_VERILOG);
   tree_set_ident(wrap, id);
   tree_set_loc(wrap, vlog_loc(body));
   tree_set_vlog(wrap, body);

   tree_t block = tree_new(T_BLOCK);
   tree_set_loc(block, vlog_loc(v));
   tree_set_ident(block, ndotted);

   vlog_trans(body, block);
   vlog_lower_instance(ctx->mir, body, ctx->cloned, block);

   hash_put(ctx->block_parents, b, ctx->out);

   elab_gen_body_t gb = { .block = v, .body = body, .where = b };
   APUSH(*ctx->gen_bodies, gb);

   new_ctx.cloned = vlog_ident(body);

   elab_push_scope(wrap, &new_ctx);

   elab_decls(block, &new_ctx);
   elab_verilog_processes(body, &new_ctx);

   elab_lower(b, &new_ctx);

   if (elab_new_errors(&new_ctx) == 0)
      elab_verilog_sub_blocks(v, &new_ctx);

   elab_check_defparams(body, &new_ctx);

   elab_pop_scope(&new_ctx);
}

static void *elab_verilog_for_generate_test_cb(jit_scalar_t *args, void *ctx)
{
   const uint64_t abits = args[0].integer;
   const uint64_t bbits = args[1].integer;
   return (void *)(uintptr_t)(abits == 1 && bbits == 0);
}

static void *elab_verilog_for_generate_index_cb(jit_scalar_t *args, void *ctx)
{
   const uint64_t abits = args[0].integer;
   const uint64_t bbits = args[1].integer;

   (void)bbits;   // TODO: check not X/Z

   int32_t *index = ctx;
   *index = abits;

   return NULL;
}

static void elab_verilog_if_generate(vlog_node_t v, const elab_ctx_t *ctx)
{
   void *context = *mptr_get(ctx->scope->privdata);
   const int nconds = vlog_conds(v);

   for (int i = 0; i < nconds; i++) {
      vlog_node_t c = vlog_cond(v, i);

      if (vlog_has_value(c)) {
         mir_unit_t *test = vlog_lower_thunk(ctx->mir, ctx->cloned,
                                             vlog_value(c));
         bool taken = jit_call_thunk2(ctx->jit, test, context,
                                      elab_verilog_for_generate_test_cb,
                                      NULL) != NULL;
         mir_unit_free(test);

         if (!taken)
            continue;
      }

      // First true condition (or else branch): elaborate its contents
      elab_verilog_sub_blocks(c, ctx);
      return;
   }
}

static void elab_verilog_for_generate(vlog_node_t v, const elab_ctx_t *ctx)
{
   mir_unit_t *init = vlog_lower_thunk(ctx->mir, ctx->cloned, vlog_left(v));
   mir_unit_t *test = vlog_lower_thunk(ctx->mir, ctx->cloned, vlog_value(v));
   mir_unit_t *step = vlog_lower_thunk(ctx->mir, ctx->cloned, vlog_right(v));

   void *context = *mptr_get(ctx->scope->privdata);

   jit_call_thunk2(ctx->jit, init, context, NULL, NULL);

   vlog_node_t genvar = vlog_ref(vlog_target(vlog_stmt(vlog_left(v), 0)));
   assert(vlog_kind(genvar) == V_GENVAR_DECL);

   vlog_node_t ref = vlog_new(V_REF);
   vlog_set_ref(ref, genvar);
   vlog_set_loc(ref, vlog_loc(genvar));

   mir_unit_t *get_var = vlog_lower_thunk(ctx->mir, ctx->cloned, ref);

   vlog_node_t s0 = vlog_stmt(v, 0);
   assert(vlog_kind(s0) == V_GEN_BLOCK);

   while (jit_call_thunk2(ctx->jit, test, context,
                          elab_verilog_for_generate_test_cb, NULL)) {
      int32_t index;
      jit_call_thunk2(ctx->jit, get_var, context,
                      elab_verilog_for_generate_index_cb, &index);

      // TODO: cache this in elab_instance_t
      vlog_node_t copy = vlog_generate_instance(s0, genvar, index, ctx->dotted);
      vlog_fold(copy, ctx->mir, ctx->jit);

      elab_verilog_block(copy, ctx);

      jit_call_thunk2(ctx->jit, step, context, NULL, NULL);
   }

   mir_unit_free(get_var);
   mir_unit_free(init);
   mir_unit_free(test);
   mir_unit_free(step);
}

static bool elab_is_scope_init(vlog_node_t s)
{
   // Added by the parser to initialise packages and specialised classes

   if (vlog_kind(s) != V_INITIAL || vlog_stmts(s) != 1)
      return false;

   vlog_node_t call = vlog_stmt(s, 0);
   return vlog_kind(call) == V_SYS_TCALL
      && ident_starts_with(vlog_ident(call), ident_new("$__init:"));
}

static void elab_verilog_processes(vlog_node_t v, const elab_ctx_t *ctx)
{
   const int nstmts = vlog_stmts(v);

   // Everything must be initialised before any other process runs
   for (int i = 0; i < nstmts; i++) {
      vlog_node_t s = vlog_stmt(v, i);
      if (elab_is_scope_init(s)) {
         tree_t w = tree_new(T_VERILOG);
         tree_set_ident(w, vlog_ident(s));
         tree_set_loc(w, vlog_loc(s));
         tree_set_vlog(w, s);

         tree_add_stmt(ctx->out, w);
      }
   }

   for (int i = 0; i < nstmts; i++) {
      vlog_node_t s = vlog_stmt(v, i);

      if (elab_is_scope_init(s))
         continue;

      switch (vlog_kind(s)) {
      case V_INITIAL:
      case V_ALWAYS:
      case V_FINAL:
      case V_ASSIGN:
      case V_UDP_TABLE:
         {
            tree_t w = tree_new(T_VERILOG);
            tree_set_ident(w, vlog_ident(s));
            tree_set_loc(w, vlog_loc(s));
            tree_set_vlog(w, s);

            tree_add_stmt(ctx->out, w);
         }
         break;
      case V_GATE_INST:
         if (vlog_gate_dimension(s) == NULL) {
            tree_t w = tree_new(T_VERILOG);
            tree_set_ident(w, vlog_ident(s));
            tree_set_loc(w, vlog_loc(s));
            tree_set_vlog(w, s);

            tree_add_stmt(ctx->out, w);
         }
         break;
      case V_SPECIFY:
         {
            int n_stmts = vlog_stmts(s);
            for (int i = 0; i < n_stmts; i++) {
               vlog_node_t stmt = vlog_stmt(s, i);

               if (vlog_kind(stmt) == V_TCHECK) {
                  tree_t wrap = tree_new(T_VERILOG);
                  tree_set_loc(wrap, vlog_loc(stmt));
                  tree_set_vlog(wrap, stmt);
                  tree_add_stmt(ctx->out, wrap);
               }
            }
         }
         break;
      case V_INST_LIST:
      case V_GEN_BLOCK:
      case V_IF_GENERATE:
      case V_FOR_GENERATE:
      case V_DEFPARAM:
         break;
      default:
         fatal_at(vlog_loc(s), "sorry, this Verilog statement is not "
                  "currently supported");
      }
   }
}

static void elab_verilog_sub_blocks(vlog_node_t v, const elab_ctx_t *ctx)
{
   const int nstmts = vlog_stmts(v);
   for (int i = 0; i < nstmts; i++) {
      vlog_node_t s = vlog_stmt(v, i);

      const vlog_kind_t kind = vlog_kind(s);
      switch (kind) {
      case V_INST_LIST:
         elab_verilog_instance_list(s, ctx);
         break;
      case V_GEN_BLOCK:
         elab_verilog_block(s, ctx);
         break;
      case V_IF_GENERATE:
         elab_verilog_if_generate(s, ctx);
         break;
      case V_FOR_GENERATE:
         elab_verilog_for_generate(s, ctx);
         break;
      default:
         break;
      }
   }
}

static vlog_node_t elab_mixed_generics(tree_t comp, vlog_node_t mod,
                                       const elab_ctx_t *ctx)
{
   vlog_node_t stmt = vlog_new(V_MOD_INST);
   vlog_set_ident(stmt, vlog_ident2(mod));
   vlog_set_loc(stmt, tree_loc(comp));

   vlog_node_t list = vlog_new(V_INST_LIST);
   vlog_set_ident(list, vlog_ident2(mod));
   vlog_add_stmt(list, stmt);
   vlog_set_loc(list, tree_loc(comp));

   const int vhdl_ngenerics = tree_generics(comp);
   const int vlog_ndecls = vlog_decls(mod);

   LOCAL_BIT_MASK have;
   mask_init(&have, vhdl_ngenerics);

   bool named = false;
   for (int i = 0; i < vlog_ndecls; i++) {
      vlog_node_t d = vlog_decl(mod, i);
      if (vlog_kind(d) != V_PARAM_DECL)
         continue;

      ident_t name = vlog_ident(d);

      tree_t cg = NULL;
      int cpos = -1;
      for (int j = 0; j < vhdl_ngenerics; j++) {
         tree_t gj = tree_generic(comp, j);
         if (ident_casecmp(tree_ident(gj), name)) {
            cg = gj;
            mask_set(&have, (cpos = j));
            break;
         }
      }

      if (cg == NULL && vlog_has_value(d)) {
         named = true;
         continue;
      }
      else if (cg == NULL) {
         error_at(tree_loc(comp), "missing matching VHDL generic declaration "
                  "for Verilog parameter %pQ with no default value in "
                  "component %pI", name, tree_ident(comp));
         return NULL;
      }

      type_t ctype = tree_type(cg);

      tree_t value = find_generic_map(ctx->out, cpos, cg);
      if (value == NULL)
         fatal_at(tree_loc(ctx->out), "cannot get value of generic %pI",
                  tree_ident(cg));
      else if (tree_kind(value) == T_OPEN && tree_has_value(cg))
         value = tree_value(cg);

      switch (tree_kind(value)) {
      case T_LITERAL:
      case T_STRING:
         break;
      case T_REF:
         if (tree_has_ref(value) && tree_kind(tree_ref(value)) == T_ENUM_LIT)
            break;
         // Fall-through
      default:
         // The actual may call a function declared in the parent
         value = elab_eval_expr(value, ctx->parent);
         break;
      }

      number_t n;
      int64_t ival;
      bool bval;
      if (type_eq(ctype, std_type(NULL, STD_INTEGER)))
         n = number_from_int(assume_int(value));
      else if (type_eq(ctype, std_type(NULL, STD_BOOLEAN))
               && folded_bool(value, &bval))
         n = number_from_bool(bval);
      else if (elab_is_enum_generic(ctype)
               && elab_eval_generic_expr(value, NULL, NULL, &ival, ctx))
         n = number_from_int(ival);
      else if (type_eq(ctype, std_type(NULL, STD_STRING))
               && tree_kind(value) == T_STRING) {
         const int nchars = tree_chars(value);
         char *bytes LOCAL = xmalloc(nchars + 1);
         for (int j = 0; j < nchars; j++) {
            tree_t ch = tree_char(value, j);
            if (tree_kind(ch) != T_REF || !tree_has_ref(ch)
                || tree_kind(tree_ref(ch)) != T_ENUM_LIT) {
               error_at(tree_loc(cg), "sorry, the value of generic %pI cannot "
                        "be converted to a Verilog parameter",
                        tree_ident(cg));
               return NULL;
            }
            bytes[j] = istr(tree_ident(tree_ref(ch)))[1];
         }

         n = number_from_string(bytes, nchars);
      }
      else if (elab_is_vector_generic(ctype) && tree_kind(value) == T_STRING
               && tree_chars(value) > 0) {
         LOCAL_TEXT_BUF tb = tb_new();
         tb_printf(tb, "%d'b", tree_chars(value));
         bool ok = true;
         for (int j = 0, nchars = tree_chars(value); j < nchars; j++) {
            tree_t ch = tree_char(value, j);
            if (tree_kind(ch) != T_REF || !tree_has_ref(ch)
                || tree_kind(tree_ref(ch)) != T_ENUM_LIT) {
               ok = false;
               break;
            }
            tb_append(tb, elab_logic_char(tree_ref(ch)));
         }

         if (!ok) {
            error_at(tree_loc(cg), "sorry, the value of generic %pI cannot "
                     "be converted to a Verilog parameter", tree_ident(cg));
            return NULL;
         }

         n = number_new(tb_get(tb), tree_loc(cg));
      }
      else {
         error_at(tree_loc(cg), "generic %pI should have type INTEGER, "
                  "BOOLEAN, an enumeration type, or a bit vector to match "
                  "corresponding Verilog parameter", tree_ident(cg));
         return NULL;
      }

      vlog_node_t num = vlog_new(V_NUMBER);
      vlog_set_number(num, n);

      vlog_node_t pa = vlog_new(V_PARAM_ASSIGN);
      vlog_set_value(pa, num);
      if (named) vlog_set_ident(pa, vlog_ident(d));

      vlog_add_param(list, pa);
   }

   for (int i = 0; i < vhdl_ngenerics; i++) {
      if (!mask_test(&have, i)) {
         tree_t g = tree_generic(comp, i);
         diag_t *d = diag_new(DIAG_ERROR, tree_loc(g));
         diag_printf(d, "generic %pI not found in Verilog module %pQ",
                     tree_ident(g), vlog_ident2(mod));
         diag_emit(d);
      }
   }

   return list;
}

static void elab_mixed_instance(tree_t inst, tree_t comp, vlog_node_t mod,
                                const elab_ctx_t *ctx)
{
   ident_t ndotted = ident_prefix(ctx->dotted, tree_ident(inst), '.');

   elab_ctx_t new_ctx = {
      .dotted = ndotted,
      .inst   = inst,
   };
   elab_inherit_context(&new_ctx, ctx);

   tree_t b = tree_new(T_BLOCK);
   tree_set_ident(b, tree_ident(inst));
   tree_set_loc(b, tree_loc(inst));

   tree_add_stmt(ctx->out, b);
   new_ctx.out = b;

   elab_push_scope(comp, &new_ctx);
   elab_generics(comp, inst, &new_ctx);
   elab_ports(comp, inst, &new_ctx);

   if (elab_new_errors(&new_ctx) == 0)
      elab_lower(b, &new_ctx);

   vlog_node_t list = elab_mixed_generics(comp, mod, &new_ctx);
   if (list != NULL)
      elab_verilog_module(comp, vlog_ident2(mod), mod, list, NULL, NULL,
                          &new_ctx);

   elab_pop_scope(&new_ctx);
}

typedef struct {
   tree_t gen;
   tree_t config;
} elab_index_config_t;

typedef struct {
   tree_t inst;
   tree_t spec;
} elab_saved_spec_t;

typedef struct {
   elab_saved_spec_t *items;
   unsigned           count, limit;
} elab_spec_save_t;

// Set while elaborating an iteration with overridden component bindings
// as the instance trees are shared so cannot be used as cache keys
static bool                 bindings_overridden = false;
static elab_index_config_t *index_configs = NULL;
static unsigned             n_index_configs = 0, max_index_configs = 0;

static void elab_note_index_config(tree_t gen, tree_t config)
{
   // A block configuration with an index specification applies to only
   // some iterations of a for generate statement
   if (n_index_configs == max_index_configs) {
      max_index_configs = MAX(16, max_index_configs * 2);
      index_configs = xrealloc_array(index_configs, max_index_configs,
                                     sizeof(elab_index_config_t));
   }

   index_configs[n_index_configs++] = (elab_index_config_t){ gen, config };
}

static void elab_bind_components_save(tree_t block, tree_t config,
                                      elab_spec_save_t *save);

static void elab_bind_components(tree_t block, tree_t config)
{
   elab_bind_components_save(block, config, NULL);
}

static void elab_bind_components_save(tree_t block, tree_t config,
                                      elab_spec_save_t *save)
{
   const int nstmts = tree_stmts(block);
   for (int i = 0; i < nstmts; i++) {
      tree_t s = tree_stmt(block, i);
      switch (tree_kind(s)) {
      case T_INSTANCE:
         {
            if (tree_class(s) != C_COMPONENT)
               break;
            else if (tree_has_spec(s) && save == NULL)
               break;

            tree_t spec = NULL;
            const int ndecls = tree_decls(config);
            for (int j = 0; j < ndecls; j++) {
               tree_t d = tree_decl(config, j);
               if (tree_kind(d) != T_SPEC)
                  continue;
               else if (tree_ident2(d) != tree_ident2(s))
                  continue;

               bool apply = false;
               if (tree_has_ident(d)) {
                  ident_t match = tree_ident(d);
                  if (match == well_known(W_ALL))
                     apply = true;
                  else if (ident_casecmp(match, tree_ident(s)))
                     apply = true;
               }
               else if (spec == NULL)
                  apply = true;

               if (apply) spec = d;
            }

            if (spec != NULL) {
               if (save != NULL) {
                  if (save->count == save->limit) {
                     save->limit = MAX(8, save->limit * 2);
                     save->items = xrealloc_array(save->items, save->limit,
                                                  sizeof(elab_saved_spec_t));
                  }

                  save->items[save->count++] = (elab_saved_spec_t) {
                     s, tree_has_spec(s) ? tree_spec(s) : NULL
                  };
               }

               tree_set_spec(s, spec);
            }
         }
         break;

      case T_FOR_GENERATE:
      case T_BLOCK:
         {
            const int ndecls = tree_decls(config);
            for (int j = 0; j < ndecls; j++) {
               tree_t d = tree_decl(config, j);
               if (tree_kind(d) != T_BLOCK_CONFIG)
                  continue;
               else if (!ident_casecmp(tree_ident(d), tree_ident(s)))
                  continue;
               else if (tree_kind(s) == T_FOR_GENERATE
                        && (tree_has_value(d) || tree_ranges(d) > 0)) {
                  // Applies to only some of the iterations
                  if (save == NULL)
                     elab_note_index_config(s, d);
                  continue;
               }

               elab_bind_components_save(s, d, save);
               break;
            }
         }
         break;

      case T_IF_GENERATE:
         {
            const int nconds = tree_conds(s);
            const int ndecls = tree_decls(config);
            for (int j = 0; j < ndecls; j++) {
               tree_t d = tree_decl(config, j);
               if (tree_kind(d) != T_BLOCK_CONFIG)
                  continue;
               else if (!ident_casecmp(tree_ident(d), tree_ident(s)))
                  continue;

               tree_t alt = NULL;
               if (tree_has_value(d) && tree_kind(tree_value(d)) == T_REF) {
                  ident_t label = tree_ident(tree_value(d));
                  for (int k = 0; k < nconds; k++) {
                     tree_t c = tree_cond(s, k);
                     if (tree_has_ident(c)
                         && ident_casecmp(tree_ident(c), label)) {
                        alt = c;
                        break;
                     }
                  }
               }
               else if (!tree_has_value(d))
                  alt = tree_cond(s, 0);

               if (alt != NULL)
                  elab_bind_components_save(alt, d, save);
            }
         }
         break;

      case T_CASE_GENERATE:
         {
            const int nalts = tree_stmts(s);
            const int ndecls = tree_decls(config);
            for (int j = 0; j < ndecls; j++) {
               tree_t d = tree_decl(config, j);
               if (tree_kind(d) != T_BLOCK_CONFIG)
                  continue;
               else if (!ident_casecmp(tree_ident(d), tree_ident(s)))
                  continue;
               else if (!tree_has_value(d)
                        || tree_kind(tree_value(d)) != T_REF)
                  continue;

               ident_t label = tree_ident(tree_value(d));
               for (int k = 0; k < nalts; k++) {
                  tree_t a = tree_stmt(s, k);
                  if (tree_has_ident(a) && ident_casecmp(tree_ident(a), label))
                     elab_bind_components_save(a, d, save);
               }
            }
         }
         break;

      default:
         break;
      }
   }
}

static void elab_cover_block(elab_ctx_t *ctx, tree_t unit)
{
   if (ctx->cover == NULL)
      return;

   tree_t block = ctx->out;
   cover_obj_t parent = ctx->cscope;

   if (ctx->parent != NULL && tree_decls(ctx->parent->out) > 0) {
      tree_t hier = tree_decl(ctx->parent->out, 0);
      assert(tree_kind(hier) == T_HIER);

      if (tree_kind(tree_ref(hier)) == T_COMPONENT) {
         // Collapse this coverage scope with the block for the
         // component above
         block = ctx->parent->out;
         parent = ctx->parent->cscope;
      }
   }

   ident_t block_name = ident_rfrom(tree_ident(unit), '.');

   cover_obj_t parent_inst = cover_get_obj(ctx->cover, parent, COV_ATTR_INST);

   if (cover_is_null(parent))
      parent = cover_get_obj(ctx->cover, parent_inst, COV_ATTR_ROOT);

   cover_obj_t inst = cover_inst_new(ctx->cover, tree_ident(block), parent_inst,
                                     block_name, lib_name(ctx->library),
                                     ctx->dotted);
   ctx->cscope = cover_scope_new(ctx->cover, inst, parent, CSCOPE_INSTANCE,
                                 tree_ident(block), *tree_loc(block));
}

typedef struct {
   mem_pool_t *pool;
   A(elab_vunit_t) found;
} vunit_search_t;

static int elab_compare_vunit(const void *a, const void *b)
{
   const elab_vunit_t *va = a, *vb = b;
   return strcmp(istr(tree_ident(va->unit)), istr(tree_ident(vb->unit)));
}

static void elab_find_vunit_cb(lib_t lib, ident_t name, int kind, void *context)
{
   vunit_search_t *params = context;

   if (kind != T_VUNIT)
      return;

   tree_t unit = lib_get(lib, name);
   if (unit == NULL || !tree_has_ident2(unit))
      return;   // Not bound to a design unit

   // The binding is LIB.ENTITY[-ARCH]{.LABEL}
   char *spec LOCAL = xstrdup(istr(tree_ident2(unit)));

   char *save;
   const char *lib_s = strtok_r(spec, ".", &save);
   char *entity = strtok_r(NULL, ".", &save);
   if (lib_s == NULL || entity == NULL)
      return;

   char *arch = strchr(entity, '-');
   if (arch != NULL)
      *arch++ = '\0';

   elab_vunit_t v = {
      .unit   = unit,
      .entity = ident_sprintf("%s.%s", lib_s, entity),
      .arch   = arch != NULL ? ident_new(arch) : NULL,
   };

   A(ident_t) labels = AINIT;
   for (char *label; (label = strtok_r(NULL, ".", &save)); )
      APUSH(labels, ident_new(label));

   v.nlabels = labels.count;
   v.labels = pool_malloc_array(params->pool, MAX(labels.count, 1),
                                sizeof(ident_t));
   for (unsigned i = 0; i < labels.count; i++)
      v.labels[i] = labels.items[i];

   ACLEAR(labels);

   APUSH(params->found, v);
}

static const elab_vunit_list_t *elab_find_vunits(lib_t lib,
                                                 const elab_ctx_t *ctx)
{
   elab_vunit_list_t *list = hash_get(ctx->vucache, lib);
   if (list != NULL)
      return list;

   vunit_search_t params = { .pool = ctx->pool };
   lib_walk_index(lib, elab_find_vunit_cb, &params);

   list = pool_calloc(ctx->pool, sizeof(elab_vunit_list_t));
   list->count = params.found.count;
   list->items = pool_malloc_array(ctx->pool, MAX(list->count, 1),
                                   sizeof(elab_vunit_t));

   if (list->count > 0) {
      memcpy(list->items, params.found.items,
             list->count * sizeof(elab_vunit_t));
      qsort(list->items, list->count, sizeof(elab_vunit_t),
            elab_compare_vunit);
   }

   ACLEAR(params.found);

   hash_put(ctx->vucache, lib, list);
   return list;
}

static bool elab_vunit_arch_matches(const elab_vunit_t *v, tree_t arch,
                                    bool final)
{
   // An explicit architecture name restricts the first level of the
   // binding; the final design unit must be the one that the names in
   // the verification unit were resolved against
   if (v->arch != NULL && v->nlabels == 0)
      return ident_prefix(v->entity, v->arch, '-') == tree_ident(arch);
   else if (final)
      return tree_ident(tree_primary(v->unit)) == tree_ident(arch);
   else
      return v->arch == NULL
         || ident_prefix(v->entity, v->arch, '-') == tree_ident(arch);
}

static void elab_bind_vunits(tree_t inst, tree_t arch, elab_ctx_t *ctx,
                             tree_t **vunits, unsigned *nvunits)
{
   // Find the verification units to bind to this instance and the
   // partially matched paths to pass on to sub-blocks

   const elab_vunit_list_t *list = elab_find_vunits(ctx->library, ctx->parent);

   ident_t entity = tree_ident(tree_primary(arch));
   ident_t label = tree_ident(inst);

   *vunits = NULL;
   *nvunits = 0;

   if (list->count == 0 && ctx->parent->npending == 0)
      return;

   elab_vunit_pending_t *pending =
      pool_malloc_array(ctx->pool, list->count + ctx->parent->npending + 1,
                        sizeof(elab_vunit_pending_t));
   unsigned npending = 0;

   tree_t *bound = pool_malloc_array(ctx->pool, list->count
                                     + ctx->parent->npending + 1,
                                     sizeof(tree_t));
   unsigned nbound = 0;

   for (unsigned i = 0; i < ctx->parent->npending; i++) {
      const elab_vunit_pending_t *p = &(ctx->parent->pending[i]);
      if (ctx->parent->via_component) {
         // The component instance label has already been matched
         if (p->pos < p->vunit->nlabels)
            pending[npending++] = *p;
         else if (elab_vunit_arch_matches(p->vunit, arch, true))
            bound[nbound++] = p->vunit->unit;
      }
      else if (p->vunit->labels[p->pos] != label)
         continue;
      else if (p->pos + 1 == p->vunit->nlabels) {
         if (elab_vunit_arch_matches(p->vunit, arch, true))
            bound[nbound++] = p->vunit->unit;
      }
      else
         pending[npending++] = (elab_vunit_pending_t){ p->vunit, p->pos + 1 };
   }

   for (unsigned i = 0; i < list->count; i++) {
      const elab_vunit_t *v = &(list->items[i]);
      if (v->entity != entity)
         continue;
      else if (v->nlabels == 0) {
         if (elab_vunit_arch_matches(v, arch, false))
            bound[nbound++] = v->unit;
      }
      else if (elab_vunit_arch_matches(v, arch, false))
         pending[npending++] = (elab_vunit_pending_t){ v, 0 };
   }

   ctx->pending = pending;
   ctx->npending = npending;

   *vunits = bound;
   *nvunits = nbound;
}

static void elab_pass_vunits(ident_t label, elab_ctx_t *ctx, bool component)
{
   // Pass on the instance paths of verification units which continue
   // through a block, generate statement or component instance

   const elab_ctx_t *parent = ctx->parent;
   if (parent->npending == 0)
      return;

   elab_vunit_pending_t *pending =
      pool_malloc_array(ctx->pool, parent->npending,
                        sizeof(elab_vunit_pending_t));
   unsigned npending = 0;

   for (unsigned i = 0; i < parent->npending; i++) {
      const elab_vunit_pending_t *p = &(parent->pending[i]);
      if (p->vunit->labels[p->pos] != label)
         continue;
      else if (component || p->pos + 1 < p->vunit->nlabels)
         pending[npending++] = (elab_vunit_pending_t){ p->vunit, p->pos + 1 };
   }

   ctx->pending = pending;
   ctx->npending = npending;
   ctx->via_component = component;
}

static void elab_config_vunits(tree_t config, elab_ctx_t *ctx,
                               tree_t **vunits, unsigned *nvunits)
{
   // Add the verification units bound by a configuration (IEEE
   // 1076-2008 section 7.3.4) to those bound in their declaration

   unsigned count = 0;
   const int ndecls = tree_decls(config);
   for (int i = 0; i < ndecls; i++) {
      if (tree_kind(tree_decl(config, i)) == T_VUNIT)
         count++;
   }

   if (count == 0)
      return;

   tree_t *all = pool_malloc_array(ctx->pool, *nvunits + count,
                                   sizeof(tree_t));
   for (unsigned i = 0; i < *nvunits; i++)
      all[i] = (*vunits)[i];

   for (int i = 0; i < ndecls; i++) {
      tree_t d = tree_decl(config, i);
      if (tree_kind(d) == T_VUNIT)
         all[(*nvunits)++] = d;
   }

   *vunits = all;
}

static void elab_architecture(tree_t inst, tree_t arch, const elab_ctx_t *ctx)
{
   ident_t label = tree_ident(inst);
   ident_t ndotted = ident_prefix(ctx->dotted, label, '.');

   elab_ctx_t new_ctx = {
      .dotted = ndotted,
      .inst   = ctx->parent != NULL ? inst : NULL,
   };
   elab_inherit_context(&new_ctx, ctx);

   tree_t b = tree_new(T_BLOCK);
   tree_set_ident(b, label);
   tree_set_loc(b, tree_loc(inst));

   tree_add_stmt(ctx->out, b);
   new_ctx.out = b;

   new_ctx.library = lib_require(ident_until(tree_ident(arch), '.'));

   elab_cover_block(&new_ctx, arch);

   mod_cache_t *mc = elab_cached_module(tree_to_object(arch), &new_ctx);
   mc->count++;

   tree_t *vunits;
   unsigned nvunits;
   elab_bind_vunits(inst, arch, &new_ctx, &vunits, &nvunits);

   elab_instance_t *ei = ghash_get(mc->instances, inst);
   if (nvunits == 0 && ei != NULL && !ei->has_vunits
       && elab_can_clone_instance(ei, &new_ctx))
      new_ctx.cloned = tree_ident(ei->block);
   else {
      ei = pool_calloc(ctx->pool, sizeof(elab_instance_t));
      ei->block = vhdl_architecture_instance(arch, inst, new_ctx.dotted,
                                             vunits, nvunits);
      ei->cscope = new_ctx.cscope;
      ei->first = b;
      ei->has_vunits = nvunits > 0;

      elab_fold_generics(ei->block, &new_ctx);

      elab_context(arch);
      elab_context(tree_primary(arch));

      ghash_put(mc->instances, inst, ei);
      mc->unique++;
   }

   elab_push_scope(arch, &new_ctx);
   elab_generics(ei->block, inst, &new_ctx);
   elab_ports(ei->block, inst, &new_ctx);
   elab_decls(ei->block, &new_ctx);
   elab_vhdl_processes(ei->block, &new_ctx);

   if (elab_new_errors(&new_ctx) == 0) {
      vhdl_cover_block(b, new_ctx.cover, new_ctx.cscope);
      elab_lower(b, &new_ctx);
   }

   if (elab_new_errors(&new_ctx) == 0)
      elab_vhdl_sub_blocks(ei->block, &new_ctx);

   elab_pop_scope(&new_ctx);
}

static void elab_configuration(tree_t inst, tree_t unit, const elab_ctx_t *ctx)
{
   ident_t label = tree_ident(inst);
   ident_t ndotted = ident_prefix(ctx->dotted, label, '.');

   elab_ctx_t new_ctx = {
      .dotted = ndotted,
      .inst   = ctx->parent != NULL ? inst : NULL,
   };
   elab_inherit_context(&new_ctx, ctx);

   tree_t b = tree_new(T_BLOCK);
   tree_set_ident(b, label);
   tree_set_loc(b, tree_loc(inst));

   tree_add_stmt(ctx->out, b);
   new_ctx.out = b;

   new_ctx.library = lib_require(ident_until(tree_ident(unit), '.'));

   tree_t config = tree_decl(unit, 0);
   assert(tree_kind(config) == T_BLOCK_CONFIG);

   tree_t arch = tree_ref(config);
   assert(tree_kind(arch) == T_ARCH);

   elab_cover_block(&new_ctx, arch);

   mod_cache_t *mc = elab_cached_module(tree_to_object(unit), &new_ctx);
   mc->count++;

   tree_t *vunits;
   unsigned nvunits;
   elab_bind_vunits(inst, arch, &new_ctx, &vunits, &nvunits);
   elab_config_vunits(config, &new_ctx, &vunits, &nvunits);

   elab_instance_t *ei = ghash_get(mc->instances, inst);
   if (nvunits == 0 && ei != NULL && !ei->has_vunits
       && elab_can_clone_instance(ei, &new_ctx))
      new_ctx.cloned = tree_ident(ei->block);
   else {
      ei = pool_calloc(ctx->pool, sizeof(elab_instance_t));
      ei->block = vhdl_config_instance(config, inst, new_ctx.dotted,
                                       vunits, nvunits);
      ei->cscope = new_ctx.cscope;
      ei->first = b;
      ei->has_vunits = nvunits > 0;

      elab_bind_components(ei->block, ei->block);
      elab_fold_generics(ei->block, &new_ctx);

      elab_context(arch);
      elab_context(tree_primary(arch));

      ghash_put(mc->instances, inst, ei);
      mc->unique++;
   }

   elab_push_scope(arch, &new_ctx);
   elab_generics(ei->block, inst, &new_ctx);
   elab_ports(ei->block, inst, &new_ctx);
   elab_decls(ei->block, &new_ctx);
   elab_vhdl_processes(ei->block, &new_ctx);

   if (error_count() == 0) {
      vhdl_cover_block(b, new_ctx.cover, new_ctx.cscope);
      elab_lower(b, &new_ctx);
      elab_vhdl_sub_blocks(ei->block, &new_ctx);
   }

   elab_pop_scope(&new_ctx);
}

static void elab_component(tree_t inst, tree_t comp, const elab_ctx_t *ctx)
{
   if (tree_has_spec(inst) && !tree_has_value(tree_spec(inst))
       && tree_has_ident2(tree_spec(inst))
       && strncmp(istr(tree_ident2(tree_spec(inst))), "(MISSING).", 10) == 0) {
      // An entity that was missing when the instance was analysed with
      // --black-box-missing, its ports those of the port map
      ident_t full = ident_until(ident_from(tree_ident2(tree_spec(inst)),
                                            '.'), '-');
      ident_t lib_i = ident_until(full, '.');
      ident_t lib_n = lib_i != full ? lib_i : lib_name(ctx->library);
      if (!elab_black_box_allowed(lib_n)) {
         error_at(tree_loc(inst), "cannot find entity %pI in library %pI: it "
                  "was missing when the design was analysed with "
                  "--black-box-missing, elaborate with the option too",
                  ident_from(full, '.') ?: full, lib_n);
         return;
      }

      elab_black_box(tree_loc(inst), ident_from(full, '.') ?: full, lib_n);
   }
   else if (!tree_has_spec(inst)) {
      tree_t bind = elab_default_binding(inst, ctx);
      if (bind == NULL) {
         // The component may be declared in a package of another library
         // which also contains the module
         lib_t lib = ctx->library;
         ident_t full_i = tree_ident(comp);
         ident_t lib_i = ident_until(full_i, '.');
         if (lib_i != full_i && lib_i != well_known(W_WORK))
            lib = elab_find_lib(lib_i, ctx);

         ident_t prefix = lib_name(lib);
         ident_t qual = ident_prefix(prefix, tree_ident2(inst), '.');

         object_t *obj = lib_get_generic(lib, qual, NULL);
         if (obj == NULL) {
            // Any other library containing a module with this name
            lib_t other;
            obj = lib_search_generic(ident_rfrom(qual, '.'),
                                     elab_is_instantiable, &other);
         }

         vlog_node_t mod = vlog_from_object(obj);
         if (mod != NULL) {
            elab_mixed_instance(inst, comp, mod, ctx);
            return;
         }
      }

      if (bind == NULL) {
         ident_t full_i = tree_ident(comp);
         ident_t lib_i = ident_until(full_i, '.');
         ident_t lib_n = lib_i != full_i ? lib_i : lib_name(ctx->library);
         if (elab_black_box_allowed(lib_n))
            elab_black_box(tree_loc(inst), tree_ident2(inst), lib_n);
      }

      tree_t spec = tree_new(T_SPEC);
      tree_set_loc(spec, tree_loc(inst));
      tree_set_ident(spec, tree_ident(inst));
      tree_set_value(spec, bind);

      tree_set_spec(inst, spec);
   }

   ident_t ndotted = ident_prefix(ctx->dotted, tree_ident(inst), '.');

   elab_ctx_t new_ctx = {
      .dotted = ndotted,
      .inst   = inst,
   };
   elab_inherit_context(&new_ctx, ctx);
   elab_pass_vunits(tree_ident(inst), &new_ctx, true);

   mod_cache_t *mc = elab_cached_module(tree_to_object(comp), &new_ctx);
   mc->count++;

   elab_instance_t *ei =
      bindings_overridden ? NULL : ghash_get(mc->instances, inst);
   if (elab_can_clone_instance(ei, &new_ctx))
      new_ctx.cloned = tree_ident(ei->block);
   else {
      ei = pool_calloc(ctx->pool, sizeof(elab_instance_t));
      ei->block = vhdl_component_instance(comp, inst, ndotted);
      ei->cscope = new_ctx.cscope;

      elab_fold_generics(ei->block, ctx);

      if (!bindings_overridden)
         ghash_put(mc->instances, inst, ei);
      mc->unique++;

      // XXX: workaround for potentially different layouts
      if (!tree_frozen(comp))
         tree_set_global_flags(ctx->out, TREE_GF_INSTANCE_NAME
                               | TREE_GF_PATH_NAME);
   }

   tree_t b = tree_new(T_BLOCK);
   tree_set_ident(b, tree_ident(inst));
   tree_set_loc(b, tree_loc(inst));

   tree_add_stmt(ctx->out, b);
   new_ctx.out = b;

   elab_push_scope(comp, &new_ctx);
   elab_generics(ei->block, inst, &new_ctx);
   elab_ports(ei->block, inst, &new_ctx);

   if (elab_new_errors(&new_ctx) == 0) {
      elab_lower(b, &new_ctx);
      elab_vhdl_sub_blocks(ei->block, &new_ctx);
   }

   elab_pop_scope(&new_ctx);
}

static void elab_instance(tree_t t, const elab_ctx_t *ctx)
{
   if (ctx->depth == MAX_DEPTH) {
      diag_t *d = diag_new(DIAG_ERROR, tree_loc(t));
      diag_printf(d, "maximum instantiation depth of %d reached", MAX_DEPTH);
      diag_hint(d, NULL, "this is likely caused by unbounded recursion");
      diag_emit(d);
      return;
   }

   tree_t new = elab_override_instance_generics(t, ctx);

   tree_t ref = tree_ref(t);
   switch (tree_kind(ref)) {
   case T_ENTITY:
      {
         tree_t arch = elab_pick_arch(ref, tree_loc(new), ctx);
         elab_architecture(new, arch, ctx);
      }
      break;
   case T_ARCH:
      elab_architecture(new, ref, ctx);
      break;
   case T_COMPONENT:
      elab_component(new, ref, ctx);
      break;
   case T_CONFIGURATION:
      elab_configuration(new, ref, ctx);
      break;
   default:
      should_not_reach_here();
   }
}

static void elab_decls(tree_t t, const elab_ctx_t *ctx)
{
   const int ndecls = tree_decls(t);
   for (int i = 0; i < ndecls; i++) {
      tree_t d = tree_decl(t, i);

      switch (tree_kind(d)) {
      case T_SIGNAL_DECL:
      case T_IMPLICIT_SIGNAL:
      case T_ALIAS:
      case T_FILE_DECL:
      case T_VAR_DECL:
      case T_CONST_DECL:
      case T_FUNC_BODY:
      case T_PROC_BODY:
      case T_FUNC_INST:
      case T_PROC_INST:
      case T_PROT_DECL:
      case T_PROT_BODY:
      case T_TYPE_DECL:
      case T_SUBTYPE_DECL:
      case T_PACK_BODY:
      case T_PACKAGE:
      case T_PACK_INST:
      case T_PSL_DECL:
      case T_ATTR_SPEC:
      case T_VERILOG:
         tree_add_decl(ctx->out, d);
         break;
      case T_FUNC_DECL:
      case T_PROC_DECL:
         if (!is_open_coded_builtin(tree_subkind(d)))
            tree_add_decl(ctx->out, d);
         break;
      default:
         break;
      }
   }
}

static void elab_push_scope(tree_t t, elab_ctx_t *ctx)
{
   tree_t h = tree_new(T_HIER);
   tree_set_loc(h, tree_loc(t));
   tree_set_subkind(h, tree_kind(t));
   tree_set_ref(h, t);

   tree_set_ident(h, ctx->dotted);
   tree_set_ident2(h, ctx->cloned ?: ctx->dotted);

   tree_add_decl(ctx->out, h);
}

static void elab_pop_scope(elab_ctx_t *ctx)
{
   if (ctx->lowered != NULL)
      unit_registry_finalise(ctx->registry, ctx->lowered);
}

static tree_t elab_eval_expr(tree_t t, const elab_ctx_t *ctx)
{
   void *context = *mptr_get(ctx->scope->privdata);
   return eval_must_fold(ctx->jit, t, ctx->registry, ctx->lowered, context);
}

static void elab_generate_range(tree_t r, int64_t *low, int64_t *high,
                                const elab_ctx_t *ctx)
{
   if (tree_subkind(r) == RANGE_EXPR) {
      tree_t value = tree_value(r);
      assert(tree_kind(value) == T_ATTR_REF);

      tree_t tmp = tree_new(T_ATTR_REF);
      tree_set_name(tmp, tree_name(value));
      tree_set_type(tmp, tree_type(r));
      tree_set_subkind(tmp, ATTR_LOW);

      tree_t tlow = elab_eval_expr(tmp, ctx);
      if (folded_int(tlow, low)) {
         tree_set_subkind(tmp, ATTR_HIGH);

         tree_t thigh = elab_eval_expr(tmp, ctx);
         if (folded_int(thigh, high))
            return;
      }

      error_at(tree_loc(r), "generate range is not static");
      *low = *high = 0;
   }
   else if (!folded_bounds(r, low, high)) {
      tree_t left  = elab_eval_expr(tree_left(r), ctx);
      tree_t right = elab_eval_expr(tree_right(r), ctx);

      int64_t ileft, iright;
      if (folded_int(left, &ileft) && folded_int(right, &iright)) {
         const bool asc = (tree_subkind(r) == RANGE_TO);
         *low = asc ? ileft : iright;
         *high = asc ? iright : ileft;
      }
      else {
         error_at(tree_loc(r), "generate range is not static");
         *low = *high = 0;
      }
   }
}

static tree_t elab_find_index_config(tree_t gen, int64_t index,
                                    const elab_ctx_t *ctx)
{
   for (unsigned i = 0; i < n_index_configs; i++) {
      if (index_configs[i].gen != gen)
         continue;

      tree_t d = index_configs[i].config;

      if (tree_has_value(d)) {
         tree_t value = tree_value(d);

         if (tree_kind(value) == T_REF && tree_has_ref(value)
             && is_type_decl(tree_ref(value))) {
            // Subtype name as discrete range
            int64_t low, high;
            elab_generate_range(range_of(tree_type(value), 0), &low, &high,
                                ctx);

            if (index >= low && index <= high)
               return d;
            else
               continue;
         }

         int64_t folded;
         if (!folded_int(value, &folded)) {
            tree_t result = elab_eval_expr(value, ctx);
            if (!folded_int(result, &folded))
               continue;
         }

         if (folded == index)
            return d;
      }
      else if (tree_ranges(d) > 0) {
         int64_t low, high;
         elab_generate_range(tree_range(d, 0), &low, &high, ctx);

         if (index >= low && index <= high)
            return d;
      }
   }

   return NULL;
}

static void elab_for_generate(tree_t t, const elab_ctx_t *ctx)
{
   int64_t low, high;
   elab_generate_range(tree_range(t, 0), &low, &high, ctx);

   // XXX: workaround for potentially different layouts
   tree_set_global_flags(ctx->out, TREE_GF_INSTANCE_NAME
                         | TREE_GF_PATH_NAME);

   ident_t label = tree_ident(t), first = NULL;

   for (int64_t i = low; i <= high; i++) {
      ident_t id = ident_sprintf("%s(%"PRIi64")", istr(label), i);
      ident_t ndotted = ident_prefix(ctx->dotted, id, '.');

      tree_t b = tree_new(T_BLOCK);
      tree_set_ident(b, id);
      tree_set_loc(b, tree_loc(t));

      tree_add_stmt(ctx->out, b);

      tree_t g = tree_decl(t, 0);
      assert(tree_kind(g) == T_GENERIC_DECL);

      tree_t map = tree_new(T_PARAM);
      tree_set_subkind(map, P_POS);
      tree_set_loc(map, tree_loc(g));
      tree_set_value(map, get_int_lit(g, NULL, i));

      tree_add_generic(b, g);
      tree_add_genmap(b, map);

      elab_ctx_t new_ctx = {
         .out     = b,
         .dotted  = ndotted,
      };
      elab_inherit_context(&new_ctx, ctx);
      elab_pass_vunits(label, &new_ctx, false);

      if (i == low)
         first = ndotted;
      else
         new_ctx.cloned = first;

      elab_cover_block(&new_ctx, t);
      elab_push_scope(t, &new_ctx);

      if (elab_new_errors(&new_ctx) == 0) {
         elab_decls(t, &new_ctx);
         elab_vhdl_processes(t, &new_ctx);
      }

      if (elab_new_errors(&new_ctx) == 0) {
         vhdl_cover_block(b, new_ctx.cover, new_ctx.cscope);
         elab_lower(b, &new_ctx);

         // A block configuration for this iteration overrides the
         // bindings of the instances in the statement for its duration
         elab_spec_save_t save = {};
         tree_t icfg = elab_find_index_config(t, i, ctx);
         const bool was_overridden = bindings_overridden;
         if (icfg != NULL) {
            elab_bind_components_save(t, icfg, &save);
            bindings_overridden = true;
         }

         elab_vhdl_sub_blocks(t, &new_ctx);

         bindings_overridden = was_overridden;

         for (unsigned k = save.count; k > 0; k--) {
            elab_saved_spec_t *it = &(save.items[k - 1]);
            if (it->spec != NULL)
               tree_set_spec(it->inst, it->spec);
            else
               tree_clear_spec(it->inst);
         }

         free(save.items);
      }

      elab_pop_scope(&new_ctx);
   }
}

static bool elab_generate_test(tree_t value, const elab_ctx_t *ctx)
{
   bool test;
   if (folded_bool(value, &test))
      return test;

   tree_t folded = elab_eval_expr(value, ctx);

   if (folded_bool(folded, &test))
      return test;

   error_at(tree_loc(value), "generate expression is not static");
   return false;
}

static void elab_if_generate(tree_t t, const elab_ctx_t *ctx)
{
   const int nconds = tree_conds(t);
   for (int i = 0; i < nconds; i++) {
      tree_t cond = tree_cond(t, i);
      if (!tree_has_value(cond) || elab_generate_test(tree_value(cond), ctx)) {
         tree_t b = tree_new(T_BLOCK);
         tree_set_loc(b, tree_loc(cond));
         tree_set_ident(b, tree_ident(cond));

         tree_add_stmt(ctx->out, b);

         ident_t name = tree_ident(cond);
         ident_t ndotted = ident_prefix(ctx->dotted, name, '.');

         elab_ctx_t new_ctx = {
            .out    = b,
            .dotted = ndotted,
         };
         elab_inherit_context(&new_ctx, ctx);
         elab_pass_vunits(tree_ident(t), &new_ctx, false);

         elab_cover_block(&new_ctx, t);
         elab_push_scope(t, &new_ctx);
         elab_decls(cond, &new_ctx);
         elab_vhdl_processes(cond, &new_ctx);

         if (error_count() == 0) {
            vhdl_cover_block(b, new_ctx.cover, new_ctx.cscope);
            elab_lower(b, &new_ctx);
            elab_vhdl_sub_blocks(cond, &new_ctx);
         }

         elab_pop_scope(&new_ctx);
         return;
      }
   }
}

static void elab_case_generate(tree_t t, const elab_ctx_t *ctx)
{
   void *context = *mptr_get(ctx->scope->privdata);
   tree_t chosen = eval_case(ctx->jit, t, ctx->lowered, context);
   if (chosen == NULL)
      return;

   ident_t id = tree_has_ident(chosen) ? tree_ident(chosen) : tree_ident(t);

   tree_t b = tree_new(T_BLOCK);
   tree_set_loc(b, tree_loc(chosen));
   tree_set_ident(b, id);

   tree_add_stmt(ctx->out, b);

   ident_t ndotted = ident_prefix(ctx->dotted, id, '.');

   elab_ctx_t new_ctx = {
      .out    = b,
      .dotted = ndotted,
   };
   elab_inherit_context(&new_ctx, ctx);
   elab_pass_vunits(tree_ident(t), &new_ctx, false);

   elab_cover_block(&new_ctx, t);
   elab_push_scope(t, &new_ctx);
   elab_decls(chosen, &new_ctx);
   elab_vhdl_processes(chosen, &new_ctx);

   if (error_count() == 0) {
      vhdl_cover_block(b, new_ctx.cover, new_ctx.cscope);
      elab_lower(b, &new_ctx);
      elab_vhdl_sub_blocks(chosen, &new_ctx);
   }

   elab_pop_scope(&new_ctx);
}

static void elab_vhdl_processes(tree_t t, const elab_ctx_t *ctx)
{
   const int nstmts = tree_stmts(t);
   for (int i = 0; i < nstmts; i++) {
      tree_t s = tree_stmt(t, i);

      switch (tree_kind(s)) {
      case T_PROCESS:
      case T_PSL_DIRECT:
         tree_add_stmt(ctx->out, s);
         break;
      default:
         break;
      }
   }
}

static void elab_vhdl_sub_blocks(tree_t t, const elab_ctx_t *ctx)
{
   const int nstmts = tree_stmts(t);
   for (int i = 0; i < nstmts; i++) {
      tree_t s = tree_stmt(t, i);

      switch (tree_kind(s)) {
      case T_INSTANCE:
         elab_instance(s, ctx);
         break;
      case T_BLOCK:
         elab_vhdl_block(s, ctx);
         break;
      case T_FOR_GENERATE:
         elab_for_generate(s, ctx);
         break;
      case T_IF_GENERATE:
         elab_if_generate(s, ctx);
         break;
      case T_CASE_GENERATE:
         elab_case_generate(s, ctx);
         break;
      case T_PROCESS:
      case T_PSL_DIRECT:
         break;
      default:
         fatal_trace("unexpected statement %s", tree_kind_str(tree_kind(s)));
      }
   }
}

static void elab_vhdl_block(tree_t t, const elab_ctx_t *ctx)
{
   ident_t id = tree_ident(t);

   tree_t b = tree_new(T_BLOCK);
   tree_set_ident(b, id);
   tree_set_loc(b, tree_loc(t));

   tree_add_stmt(ctx->out, b);

   ident_t ndotted = ident_prefix(ctx->dotted, id, '.');

   elab_ctx_t new_ctx = {
      .out    = b,
      .dotted = ndotted,
   };
   elab_inherit_context(&new_ctx, ctx);
   elab_pass_vunits(tree_ident(t), &new_ctx, false);

   elab_cover_block(&new_ctx, t);
   elab_push_scope(t, &new_ctx);
   elab_generics(t, t, &new_ctx);
   elab_ports(t, t, &new_ctx);
   elab_decls(t, &new_ctx);
   elab_vhdl_processes(t, &new_ctx);

   if (elab_new_errors(&new_ctx) == 0) {
      vhdl_cover_block(b, new_ctx.cover, new_ctx.cscope);
      elab_lower(b, &new_ctx);
      elab_vhdl_sub_blocks(t, &new_ctx);
   }

   elab_pop_scope(&new_ctx);
}

static tree_t elab_top_level_binding(tree_t arch, const elab_ctx_t *ctx)
{
   tree_t inst = tree_new(T_INSTANCE);
   tree_set_ident(inst, ident_rfrom(tree_ident(tree_primary(arch)), '.'));
   tree_set_loc(inst, tree_loc(arch));
   tree_set_ref(inst, arch);
   tree_set_class(inst, C_ARCHITECTURE);

   tree_t entity = tree_primary(arch);
   const int ngenerics = tree_generics(entity);

   for (int i = 0; i < ngenerics; i++) {
      tree_t g = tree_generic(entity, i);
      ident_t name = tree_ident(g);

      if (tree_flags(g) & TREE_F_PREDEFINED)
         continue;    // Predefined generic subprograms
      else if (tree_class(g) != C_CONSTANT) {
         error_at(tree_loc(g), "only constant top-level generics are "
                  "supported");
         continue;
      }

      tree_t value = elab_find_generic_override(g, ctx);
      if (value == NULL && tree_has_value(g))
         value = tree_value(g);
      else if (value == NULL) {
         error_at(tree_loc(g), "generic %pI of top-level entity must have "
                  "default value or be specified using -gNAME=VALUE", name);
         continue;
      }

      tree_t map = tree_new(T_PARAM);
      tree_set_subkind(map, P_POS);
      tree_set_pos(map, i);
      tree_set_value(map, value);

      tree_add_genmap(inst, map);
   }

   const int nports = tree_ports(entity);
   for (int i = 0; i < nports; i++) {
      tree_t p = tree_port(entity, i);

      tree_t m = tree_new(T_PARAM);
      tree_set_subkind(m, P_POS);
      tree_set_pos(m, i);

      if (tree_class(p) == C_VARIABLE) {
         diag_t *d = diag_new(DIAG_ERROR, tree_loc(p));
         diag_printf(d, "top-level port %pI with variable class cannot be "
                     "unconnected", tree_ident(p));
         diag_lrm(d, STD_19, "6.5.6.3");
         diag_emit(d);
      }
      else if (tree_subkind(p) == PORT_IN && tree_has_value(p))
         tree_set_value(m, tree_value(p));
      else {
         type_t type = tree_type(p);
         if (type_is_unconstrained(type))
            error_at(tree_loc(p), "unconnected top-level port %pI cannot have "
                     "unconstrained type %pT", tree_ident(p), type);

         tree_t open = tree_new(T_OPEN);
         tree_set_type(open, type);
         tree_set_loc(open, tree_loc(p));

         tree_set_value(m, open);
      }

      tree_add_param(inst, m);
   }

   return inst;
}

void elab_set_generic(const char *name, const char *value)
{
   ident_t id = ident_new(name);

   for (generic_list_t *it = generic_override; it != NULL; it = it->next) {
      if (it->name == id)
         fatal("generic %s already has value '%s'", name, it->value);
   }

   generic_list_t *new = xmalloc(sizeof(generic_list_t));
   new->name  = id;
   new->value = xstrdup(value);
   new->next  = generic_override;

   generic_override = new;
}

typedef struct {
   const elab_ctx_t *ctx;
   hset_t           *seen;
   unsigned          log10;
   vlog_node_t       scaled;     // A module with a time unit
   vlog_node_t       unscaled;   // A module using the default time unit
} elab_precision_t;

static void elab_scan_vhdl_precision(tree_t arch, elab_precision_t *p);

static void elab_vhdl_root(tree_t t, const elab_ctx_t *ctx)
{
   tree_t arch = t;
   switch (tree_kind(t)) {
   case T_ENTITY:
      arch = elab_pick_arch(t, tree_loc(t), ctx);
      // Fall-through
   case T_ARCH:
      {
         tree_t bind = elab_top_level_binding(arch, ctx);

         if (error_count() == 0)
            elab_architecture(bind, arch, ctx);
      }
      break;
   case T_CONFIGURATION:
      {
         tree_t config = tree_decl(t, 0);
         assert(tree_kind(config) == T_BLOCK_CONFIG);

         tree_t inst = elab_top_level_binding(tree_ref(config), ctx);

         if (error_count() == 0)
            elab_configuration(inst, t, ctx);
      }
      break;
   default:
      fatal("%pI is not a suitable top-level unit", tree_ident(t));
   }
}

static void elab_scan_precision(vlog_node_t mod, elab_precision_t *p);
static void elab_scan_vhdl_precision(tree_t arch, elab_precision_t *p);

static void elab_scan_package_precision(ident_t name, elab_precision_t *p)
{
   // A package or compilation unit that is used by a module and every
   // package it imports

   if (name == NULL)
      return;

   lib_t lib = lib_find(ident_until(name, '.'));
   if (lib == NULL)
      return;

   object_t *obj = lib_get_generic(lib, name, NULL);
   vlog_node_t pack = obj != NULL ? vlog_from_object(obj) : NULL;
   if (pack == NULL || hset_contains(p->seen, pack))
      return;

   switch (vlog_kind(pack)) {
   case V_PACKAGE:
      {
         const unsigned log10 = VLOG_TIME_LOG10(vlog_ival(pack));
         if (log10 != VLOG_TIME_UNSCALED)
            p->log10 = MIN(p->log10, log10);
         else
            p->log10 = MIN(p->log10, VLOG_DEFAULT_LOG10);
      }
      break;
   case V_NAMESPACE:
      break;
   default:
      return;
   }

   hset_insert(p->seen, pack);

   const int ndecls = vlog_decls(pack);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(pack, i);
      if (vlog_kind(d) == V_IMPORT_DECL && vlog_has_ref(d))
         elab_scan_package_precision(vlog_ident(vlog_ref(d)), p);
   }
}

static void elab_scan_precision_cb(vlog_node_t v, void *arg)
{
   elab_precision_t *p = arg;

   ident_t qual;
   object_t *obj = elab_find_instance_unit(v, p->ctx, &qual);
   if (obj == NULL)
      return;

   vlog_node_t mod = vlog_from_object(obj);
   if (mod != NULL)
      elab_scan_precision(mod, p);
   else {
      tree_t entity = tree_from_object(obj);
      if (entity != NULL && tree_kind(entity) == T_ENTITY
          && !hset_contains(p->seen, entity)) {
         hset_insert(p->seen, entity);
         elab_scan_vhdl_precision(elab_pick_arch(entity, tree_loc(entity),
                                                 p->ctx), p);
      }
   }
}

static void elab_scan_precision(vlog_node_t mod, elab_precision_t *p)
{
   // The time precision of the design is the smallest of any module
   // that might be instantiated

   if (hset_contains(p->seen, mod))
      return;

   hset_insert(p->seen, mod);

   if (vlog_kind(mod) == V_MODULE) {
      const unsigned log10 = VLOG_TIME_LOG10(vlog_ival(mod));
      if (log10 != VLOG_TIME_UNSCALED) {
         p->log10 = MIN(p->log10, log10);
         if (p->scaled == NULL)
            p->scaled = mod;
      }
      else {
         p->log10 = MIN(p->log10, VLOG_DEFAULT_LOG10);
         if (p->unscaled == NULL)
            p->unscaled = mod;
      }

      const int nstmts = vlog_stmts(mod);
      for (int i = 0; i < nstmts; i++) {
         vlog_node_t s = vlog_stmt(mod, i);
         if (vlog_kind(s) != V_INITIAL || vlog_stmts(s) != 1)
            continue;

         vlog_node_t call = vlog_stmt(s, 0);
         if (vlog_kind(call) != V_SYS_TCALL)
            continue;

         const char *name = istr(vlog_ident(call));
         if (strncmp(name, "$__init:", 8) == 0)
            elab_scan_package_precision(ident_new(name + 8), p);
      }
   }

   vlog_visit_only(mod, elab_scan_precision_cb, p, V_INST_LIST);
}

static void elab_scan_vhdl_instance_cb(tree_t t, void *arg)
{
   elab_precision_t *p = arg;

   if (tree_kind(t) != T_INSTANCE || !tree_has_ref(t))
      return;

   tree_t ref = tree_ref(t);
   ident_t name = tree_ident(ref), qual;
   if (tree_kind(ref) == T_COMPONENT)
      name = ident_rfrom(name, '.') ?: name;
   else if (tree_kind(ref) != T_ENTITY)
      return;

   object_t *obj = elab_find_unit_by_name(name, p->ctx, &qual);
   if (obj == NULL)
      return;

   vlog_node_t mod = vlog_from_object(obj);
   if (mod != NULL) {
      elab_scan_precision(mod, p);
      return;
   }

   tree_t entity = tree_from_object(obj);
   if (entity != NULL && tree_kind(entity) == T_ENTITY
       && !hset_contains(p->seen, entity)) {
      hset_insert(p->seen, entity);
      elab_scan_vhdl_precision(elab_pick_arch(entity, tree_loc(entity),
                                              p->ctx), p);
   }
}

static void elab_scan_vhdl_precision(tree_t arch, elab_precision_t *p)
{
   tree_visit_only(arch, elab_scan_vhdl_instance_cb, p, T_INSTANCE);
}

static vlog_node_t elab_vlog_generic_value(vlog_node_t decl, const char *str)
{
   // Value of a parameter given on the command line: a string in
   // quotes or for a string parameter, a real number for a real
   // parameter or with a fraction or exponent, otherwise a Verilog number

   vlog_node_t type = vlog_has_type(decl) ? vlog_get_type(decl) : NULL;
   const bool is_string = type != NULL && vlog_kind(type) == V_DATA_TYPE
      && vlog_subkind(type) == DT_STRING;
   const size_t len = strlen(str);

   if (str[0] == '"' || is_string) {
      const bool quoted = str[0] == '"' && len >= 2 && str[len - 1] == '"';
      vlog_node_t v = vlog_new(V_STRING);
      vlog_set_number(v, quoted ? number_from_string(str + 1, len - 2)
                      : number_from_string(str, len));
      vlog_set_loc(v, vlog_loc(decl));
      return v;
   }

   char *end;
   const double real = strtod(str, &end);
   if (*end == '\0' && end != str
       && ((type != NULL && vlog_is_real(type)) || strpbrk(str, ".eE"))) {
      vlog_node_t v = vlog_new(V_REAL);
      vlog_set_dval(v, real);
      vlog_set_loc(v, vlog_loc(decl));
      return v;
   }

   vlog_node_t v = vlog_new(V_NUMBER);
   vlog_set_number(v, number_new(str, vlog_loc(decl)));
   vlog_set_loc(v, vlog_loc(decl));
   return v;
}

static void elab_verilog_root(vlog_node_t v, const elab_ctx_t *ctx)
{
   ident_t label = vlog_ident2(v);

   vlog_node_t stmt = vlog_new(V_MOD_INST);
   vlog_set_ident(stmt, label);

   // Parameters of the top-level module may be given with -g
   A(vlog_node_t) overrides = AINIT;
   const int ndecls = vlog_decls(v);
   for (int i = 0; i < ndecls && generic_override != NULL; i++) {
      vlog_node_t d = vlog_decl(v, i);
      if (vlog_kind(d) != V_PARAM_DECL)
         continue;

      ident_t name = vlog_ident(d);

      generic_list_t **it, *tmp;
      for (it = &generic_override;
           *it && (*it)->name != name && !ident_casecmp((*it)->name, name);
           it = &((*it)->next))
         ;

      if (*it == NULL)
         continue;

      vlog_node_t assign = vlog_new(V_PARAM_ASSIGN);
      vlog_set_ident(assign, name);
      vlog_set_value(assign, elab_vlog_generic_value(d, (*it)->value));
      vlog_set_loc(assign, vlog_loc(d));
      APUSH(overrides, assign);

      *it = (tmp = (*it))->next;
      free(tmp->value);
      free(tmp);
   }

   vlog_node_t list = vlog_new(V_INST_LIST);
   vlog_set_ident(list, label);
   vlog_add_stmt(list, stmt);
   vlog_set_loc(list, vlog_loc(v));

   for (int i = 0; i < overrides.count; i++)
      vlog_add_param(list, overrides.items[i]);
   ACLEAR(overrides);

   elab_verilog_module(NULL, label, v, list, stmt, NULL, ctx);
}

static vlog_node_t elab_hier_ref_decl(tree_t b, ident_t id)
{
   const int ndecls = tree_decls(b);
   for (int i = 0; i < ndecls; i++) {
      tree_t wrap = tree_decl(b, i);
      if (tree_kind(wrap) != T_VERILOG)
         continue;

      vlog_node_t vd = tree_vlog(wrap);
      if (vlog_kind(vd) != V_PORT_MAP && vlog_has_ident(vd)
          && vlog_ident(vd) == id)
         return vd;
   }

   return NULL;
}

static bool elab_has_child_block(tree_t b, ident_t id)
{
   const int nstmts = tree_stmts(b);
   for (int i = 0; i < nstmts; i++) {
      tree_t s = tree_stmt(b, i);
      if (tree_kind(s) == T_BLOCK && ident_casecmp(tree_ident(s), id))
         return true;
   }

   return false;
}

static tree_t elab_find_vhdl_signal(tree_t b, ident_t id)
{
   b = vlog_hier_target_block(b, id);

   tree_t obj = vlog_hier_vhdl_port(b, id);
   const int ndecls = tree_decls(b);
   for (int i = 0; obj == NULL && i < ndecls; i++) {
      tree_t d = tree_decl(b, i);
      if (tree_kind(d) == T_SIGNAL_DECL && ident_casecmp(tree_ident(d), id))
         obj = d;
   }

   return obj;
}

static bool elab_mod_ref_has_block(tree_t b, vlog_node_t mr)
{
   // The name selects an instance or a VHDL generate block, which may be
   // one level down in the shim of a VHDL entity instantiated from Verilog

   ident_t id = vlog_ident(mr);

   ident_t alt = id;
   int64_t index;
   if (vlog_params(mr) > 0 && vlog_get_const(vlog_param(mr, 0), &index))
      alt = ident_sprintf("%s(%"PRIi64")", istr(id), index);

   if (elab_has_child_block(b, id) || elab_has_child_block(b, alt))
      return true;

   return tree_stmts(b) == 1 && tree_kind(tree_stmt(b, 0)) == T_BLOCK
      && (elab_has_child_block(tree_stmt(b, 0), id)
          || elab_has_child_block(tree_stmt(b, 0), alt));
}

static bool elab_is_vhdl_record_signal(tree_t b, ident_t id)
{
   tree_t obj = elab_find_vhdl_signal(b, id);
   if (obj == NULL)
      return false;

   type_t type = tree_type(obj);
   while (type_is_array(type))
      type = type_elem(type);

   return type_is_record(type);
}

static vlog_node_t elab_vhdl_dim(const loc_t *loc, vlog_dimension_kind_t kind,
                                 int64_t left, int64_t right)
{
   vlog_node_t l = vlog_new(V_NUMBER);
   vlog_set_number(l, number_from_int(left));
   vlog_set_loc(l, loc);

   vlog_node_t r = vlog_new(V_NUMBER);
   vlog_set_number(r, number_from_int(right));
   vlog_set_loc(r, loc);

   vlog_node_t dim = vlog_new(V_DIMENSION);
   vlog_set_subkind(dim, kind);
   vlog_set_left(dim, l);
   vlog_set_right(dim, r);
   vlog_set_loc(dim, loc);
   return dim;
}

static bool elab_vhdl_is_logic_array(type_t type)
{
   // An array of standard logic with any number of nested dimensions

   type_t std_ulogic = ieee_type(IEEE_STD_ULOGIC);

   while (type_is_array(type))
      type = type_elem(type);

   return type_eq(type_base_recur(type), std_ulogic);
}

static vlog_node_t elab_vhdl_data_type(const loc_t *loc, ident_t name,
                                       type_t type, vlog_node_t *bounds,
                                       bool quiet)
{
   // Standard logic scalars and vectors have a Verilog equivalent

   type_t std_ulogic = ieee_type(IEEE_STD_ULOGIC);

   vlog_node_t dt = vlog_new(V_DATA_TYPE);
   vlog_set_subkind(dt, DT_LOGIC);
   vlog_set_loc(dt, loc);

   *bounds = NULL;

   if (type_is_array(type) && elab_vhdl_is_logic_array(type)) {
      // Each dimension of an array of vectors is a packed dimension
      for (type_t t = type; type_is_array(t); t = type_elem(t)) {
         const int ndims = dimension_of(t);
         for (int i = 0; i < ndims; i++) {
            int64_t low, high;
            tree_t r;
            if (!type_const_bounds(t) || (r = range_of(t, i)) == NULL
                || tree_subkind(r) == RANGE_EXPR
                || !folded_bounds(r, &low, &high) || low > high) {
               if (!quiet)
                  error_at(loc, "sorry, the bounds of VHDL signal %pI must be "
                           "known during elaboration to be the target of a "
                           "hierarchical reference", name);
               return NULL;
            }

            const bool downto = tree_subkind(r) == RANGE_DOWNTO;
            vlog_add_range(dt, elab_vhdl_dim(loc, V_DIM_PACKED,
                                             downto ? high : low,
                                             downto ? low : high));
         }
      }
   }
   else if (type_is_integer(type)) {
      // The bounds are kept to bind to the signal
      int64_t low, high;
      tree_t r = range_of(type, 0);
      if (r == NULL || tree_subkind(r) == RANGE_EXPR
          || !folded_bounds(r, &low, &high)) {
         if (!quiet)
            error_at(loc, "sorry, the bounds of VHDL signal %pI must be "
                     "known during elaboration to be the target of a "
                     "hierarchical reference", name);
         return NULL;
      }

      vlog_set_subkind(dt, DT_INT);
      vlog_set_flags(dt, VLOG_F_SIGNED);

      *bounds = elab_vhdl_dim(loc, V_DIM_PACKED, low, high);
   }
   else if (!type_eq(type_base_recur(type), std_ulogic)) {
      if (!quiet)
         error_at(loc, "sorry, only VHDL signals of type STD_LOGIC, "
                  "STD_LOGIC_VECTOR or INTEGER can be the target of a "
                  "hierarchical reference");
      return NULL;
   }

   return dt;
}

static vlog_node_t elab_hier_ref_vhdl(vlog_node_t v, tree_t obj)
{
   vlog_node_t d_bounds;
   vlog_node_t dt = elab_vhdl_data_type(vlog_loc(v), tree_ident(obj),
                                        tree_type(obj), &d_bounds, false);
   if (dt == NULL)
      return v;

   vlog_node_t d = vlog_new(V_VAR_DECL);
   if (d_bounds != NULL)
      vlog_set_value(d, d_bounds);
   vlog_set_ident(d, tree_ident(obj));
   vlog_set_type(d, dt);
   vlog_set_flags(d, VLOG_F_VHDL);
   vlog_set_loc(d, vlog_loc(v));

   vlog_set_ident(v, tree_ident(obj));
   vlog_set_ref(v, d);
   return v;
}

static bool elab_vhdl_array_dim(const loc_t *loc, ident_t name, type_t type,
                                vlog_node_t *dim)
{
   // A one dimensional array of records with constant bounds

   int64_t low, high;
   tree_t r;
   if (dimension_of(type) != 1 || !type_is_record(type_elem(type))
       || !type_const_bounds(type) || (r = range_of(type, 0)) == NULL
       || tree_subkind(r) == RANGE_EXPR || !folded_bounds(r, &low, &high)
       || low > high) {
      error_at(loc, "sorry, %pI must be a one dimensional array of records "
               "with bounds known during elaboration to be the target of a "
               "hierarchical reference", name);
      return false;
   }

   const bool downto = tree_subkind(r) == RANGE_DOWNTO;
   *dim = elab_vhdl_dim(loc, V_DIM_UNPACKED, downto ? high : low,
                        downto ? low : high);
   return true;
}

static vlog_node_t elab_vhdl_record_decl(const loc_t *loc, type_t type)
{
   // Describe a VHDL record as an unpacked structure so that the layout
   // of the record of signals can be recreated

   type_t base = type_base_recur(type);

   vlog_node_t s = vlog_new(V_STRUCT_DECL);
   vlog_set_ident(s, type_ident(base));
   vlog_set_loc(s, loc);

   const int nfields = type_fields(base);
   for (int i = 0; i < nfields; i++) {
      tree_t f = type_field(base, i);
      type_t ft = tree_type(f);

      vlog_node_t fd = vlog_new(V_VAR_DECL);
      vlog_set_ident(fd, tree_ident(f));
      vlog_set_flags(fd, VLOG_F_VHDL);
      vlog_set_loc(fd, loc);

      vlog_node_t ftype, bounds = NULL;
      if (type_is_record(ft))
         ftype = elab_vhdl_record_decl(loc, ft);
      else if (type_is_array(ft) && type_is_record(type_elem_recur(ft))) {
         vlog_node_t dim;
         if (!elab_vhdl_array_dim(loc, tree_ident(f), ft, &dim))
            return NULL;

         vlog_add_range(fd, dim);
         ftype = elab_vhdl_record_decl(loc, type_elem(ft));
      }
      else if (type_is_array(ft) && !type_const_bounds(ft)) {
         error_at(loc, "sorry, element %pI of VHDL record type %pI has "
                  "bounds which are not known during elaboration", tree_ident(f),
                  type_ident(base));
         return NULL;
      }
      else {
         // Elements which cannot be named still take up space in the record
         ftype = elab_vhdl_data_type(loc, tree_ident(f), ft, &bounds, true);
         if (ftype == NULL) {
            ftype = vlog_new(V_DATA_TYPE);
            vlog_set_subkind(ftype, DT_LOGIC);
            vlog_set_loc(ftype, loc);
         }
      }

      if (ftype == NULL)
         return NULL;

      vlog_set_type(fd, ftype);
      if (bounds != NULL)
         vlog_set_value(fd, bounds);

      vlog_add_decl(s, fd);
   }

   return s;
}

static vlog_node_t elab_vhdl_path_step(vlog_node_t src, ident_t id,
                                       vlog_node_t decl, vlog_node_t prev)
{
   vlog_node_t step = vlog_new(V_MOD_REF);
   vlog_set_ident(step, id);
   vlog_set_ref(step, decl);
   vlog_set_loc(step, vlog_loc(src));
   if (prev != NULL)
      vlog_set_value(step, prev);

   if (vlog_kind(src) == V_MOD_REF) {
      const int nparams = vlog_params(src);
      for (int i = 0; i < nparams; i++)
         vlog_add_param(step, vlog_param(src, i));
   }

   return step;
}

static bool elab_hier_ref_vhdl_path(vlog_node_t v, tree_t obj,
                                    vlog_node_t *steps, int nsteps)
{
   // The first step is the VHDL signal and the others select an element
   // of it or an element of an array of records

   const loc_t *loc = vlog_loc(v);
   type_t type = tree_type(obj);

   vlog_node_t root = vlog_new(V_VAR_DECL);
   vlog_set_ident(root, tree_ident(obj));
   vlog_set_flags(root, VLOG_F_VHDL);
   vlog_set_loc(root, loc);

   type_t rec_type = type;
   if (type_is_array(type)) {
      vlog_node_t dim;
      if (!elab_vhdl_array_dim(loc, tree_ident(obj), type, &dim))
         return false;

      vlog_add_range(root, dim);
      rec_type = type_elem(type);
   }

   vlog_node_t s = elab_vhdl_record_decl(loc, rec_type);
   if (s == NULL)
      return false;

   vlog_set_type(root, s);

   vlog_node_t path = elab_vhdl_path_step(steps[0], tree_ident(obj), root, NULL);
   if (vlog_params(path) != vlog_ranges(root)) {
      error_at(vlog_loc(steps[0]), "VHDL signal %pQ %s", tree_ident(obj),
               vlog_ranges(root) > 0 ? "is an array of records and must be "
               "indexed" : "is not an array and cannot be indexed");
      return false;
   }

   vlog_node_t fd = root;
   for (int i = 1; i < nsteps; i++) {
      vlog_node_t src = steps[i];
      ident_t id = vlog_ident(src);

      if (vlog_kind(vlog_type(fd)) != V_STRUCT_DECL) {
         error_at(vlog_loc(src), "%pQ is not a record element", id);
         return false;
      }

      vlog_node_t st = vlog_type(fd);
      type_t base = type_base_recur(rec_type);
      const int nfields = vlog_decls(st);

      fd = NULL;
      for (int j = 0; j < nfields; j++) {
         vlog_node_t f = vlog_decl(st, j);
         if (ident_casecmp(vlog_ident(f), id)) {
            fd = f;
            rec_type = tree_type(type_field(base, j));
            break;
         }
      }

      if (fd == NULL) {
         error_at(vlog_loc(src), "VHDL record type %pI has no element named "
                  "%pQ", type_ident(base), id);
         return false;
      }

      path = elab_vhdl_path_step(src, vlog_ident(fd), fd, path);

      if (vlog_params(path) != vlog_ranges(fd)) {
         error_at(vlog_loc(src), "element %pQ of VHDL record %s", id,
                  vlog_ranges(fd) > 0 ? "is an array of records and must be "
                  "indexed" : "is not an array and cannot be indexed");
         return false;
      }

      if (type_is_array(rec_type) && vlog_ranges(fd) > 0)
         rec_type = type_elem(rec_type);
   }

   if (vlog_kind(vlog_type(fd)) == V_STRUCT_DECL) {
      error_at(loc, "sorry, a hierarchical reference to a whole VHDL record "
               "signal %pQ is not supported", vlog_ident(path));
      return false;
   }

   vlog_node_t bounds;
   vlog_node_t dt = elab_vhdl_data_type(loc, vlog_ident(fd), rec_type, &bounds,
                                        false);
   if (dt == NULL)
      return false;

   vlog_node_t d = vlog_new(V_VAR_DECL);
   vlog_set_ident(d, vlog_ident(fd));
   vlog_set_type(d, dt);
   vlog_set_flags(d, VLOG_F_VHDL | VLOG_F_VHDL_PATH);
   vlog_set_ref(d, root);
   vlog_set_value(d, path);
   vlog_set_loc(d, loc);

   vlog_set_ident(v, tree_ident(obj));
   vlog_set_ref(v, d);
   return true;
}

static vlog_node_t elab_hier_ref_modport(tree_t b, ident_t id)
{
   if (tree_decls(b) == 0)
      return NULL;

   tree_t h = tree_decl(b, 0);
   if (tree_kind(h) != T_HIER || !tree_has_ref(h)
       || tree_kind(tree_ref(h)) != T_VERILOG)
      return NULL;

   vlog_node_t body = tree_vlog(tree_ref(h));
   const int ndecls = vlog_decls(body);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(body, i);
      if (vlog_kind(d) == V_MODPORT && vlog_ident(d) == id)
         return d;
   }

   return NULL;
}

static vlog_node_t elab_hier_ref_generic(tree_t b, ident_t id, const loc_t *loc)
{
   // The value of an integer generic of a VHDL instance

   if (tree_kind(b) != T_BLOCK)
      return NULL;

   const int ngenerics = tree_generics(b);
   for (int i = 0; i < ngenerics; i++) {
      tree_t g = tree_generic(b, i);
      int64_t value;
      if (tree_class(g) == C_CONSTANT && ident_casecmp(tree_ident(g), id)
          && type_is_integer(tree_type(g))
          && folded_int(tree_value(tree_genmap(b, i)), &value)) {
         vlog_node_t n = vlog_new(V_NUMBER);
         vlog_set_number(n, number_from_int(value));
         vlog_set_loc(n, loc);
         return n;
      }
   }

   return NULL;
}

static vlog_node_t elab_hier_ref_param(tree_t b, ident_t id)
{
   // The value of a parameter is known for each instance

   if (tree_decls(b) == 0)
      return NULL;

   tree_t h = tree_decl(b, 0);
   if (tree_kind(h) != T_HIER || !tree_has_ref(h)
       || tree_kind(tree_ref(h)) != T_VERILOG)
      return NULL;

   vlog_node_t body = tree_vlog(tree_ref(h));
   const int ndecls = vlog_decls(body);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(body, i);
      const vlog_kind_t kind = vlog_kind(d);
      if ((kind == V_PARAM_DECL || kind == V_LOCALPARAM)
          && vlog_ident(d) == id && vlog_has_value(d))
         return vlog_value(d);
   }

   return NULL;
}

static tree_t elab_walk_mod_refs(vlog_node_t v, tree_t where, hash_t *parents)
{
   // A name declared in an enclosing scope of a generate block

   vlog_node_t root = v;
   while (vlog_has_value(root))
      root = vlog_value(root);

   if (vlog_has_ref(root)) {
      const bool is_port = vlog_kind(vlog_ref(root)) == V_IFACE_PORT;
      for (tree_t it = where; it != NULL; it = hash_get(parents, it)) {
         if (tree_kind(it) != T_BLOCK)
            break;
         else if (is_port
                  ? vlog_iface_port_target(
                     it, vlog_ref(root), vlog_ranges(vlog_ref(root)) > 0
                     ? vlog_mod_ref_name(root) : NULL) != NULL
                  : elab_has_child_block(it, vlog_mod_ref_name(root)))
            return vlog_walk_mod_refs(v, it);
      }
   }
   else if (vlog_kind(root) == V_MOD_REF) {
      // The name of a generate block in this scope or one which encloses it
      ident_t name = vlog_mod_ref_name(root);
      for (tree_t it = where; it != NULL; it = hash_get(parents, it)) {
         if (tree_kind(it) != T_BLOCK)
            break;
         else if (elab_has_child_block(it, name))
            return vlog_walk_mod_refs(v, it);
      }

      // An upwards name can also be that of an enclosing instance or of
      // its module
      for (tree_t it = where; it != NULL; it = hash_get(parents, it)) {
         if (tree_kind(it) != T_BLOCK)
            break;
         else if (vlog_block_is_named(it, name)) {
            vlog_set_mod_ref_alias(name, it);
            tree_t result = vlog_walk_mod_refs(v, where);
            vlog_set_mod_ref_alias(NULL, NULL);
            return result;
         }
      }
   }

   return vlog_walk_mod_refs(v, where);
}

static void elab_check_net_assign(vlog_node_t v)
{
   // A net cannot be the target of a procedural assignment

   vlog_node_t target = vlog_target(v);
   while (vlog_kind(target) == V_BIT_SELECT || vlog_kind(target) == V_PART_SELECT)
      target = vlog_value(target);

   if (vlog_kind(target) == V_HIER_REF && vlog_has_ref(target)
       && vlog_is_net(target))
      error_at(vlog_loc(target), "illegal reference to net %pQ in a "
               "procedural assignment", vlog_ident(target));
}

static void elab_check_handle_value(vlog_node_t value, vlog_node_t type,
                                    const elab_href_ctx_t *hctx)
{
   // A virtual interface variable can only refer to an instance of the
   // interface with the same parameters as its type

   if (vlog_kind(value) == V_COND_EXPR) {
      elab_check_handle_value(vlog_left(value), type, hctx);
      elab_check_handle_value(vlog_right(value), type, hctx);
      return;
   }

   if (vlog_kind(value) != V_HIER_REF || vlog_ident(value) != ident_new("$self"))
      return;

   if (type == NULL || vlog_kind(type) != V_DATA_TYPE
       || vlog_subkind(type) != DT_VIRTUAL)
      return;

   vlog_node_t spec = vlog_virtual_spec(type);
   const bool unresolved = spec == NULL && vlog_virtual_unresolved(type);
   if (spec == NULL && !unresolved)
      return;

   tree_t b = elab_walk_mod_refs(vlog_value(value), hctx->where,
                                 hctx->parents);
   if (b == NULL || tree_decls(b) == 0)
      return;

   tree_t h = tree_decl(b, 0);
   if (tree_kind(h) != T_HIER || !tree_has_ref(h)
       || tree_kind(tree_ref(h)) != T_VERILOG)
      return;

   vlog_node_t body = tree_vlog(tree_ref(h));
   if (!(vlog_flags(body) & VLOG_F_INTERFACE))
      return;
   else if (unresolved)
      warn_at(vlog_loc(value), "the parameters of the virtual interface type "
              "are not known so cannot check that instance of %pQ has the "
              "same layout", vlog_ident2(body));
   else if (vlog_ident(body) != vlog_ident(spec)
            && vlog_ident2(body) == vlog_ident2(spec))
      error_at(vlog_loc(value), "cannot assign an instance of interface %pQ "
               "to a virtual interface with different parameter values",
               vlog_ident2(body));
}

static vlog_node_t elab_virtual_element(vlog_node_t type)
{
   // A virtual interface type or the element type of an array of them

   if (type == NULL || vlog_kind(type) != V_DATA_TYPE)
      return NULL;
   else if (vlog_subkind(type) == DT_VIRTUAL)
      return type;
   else if ((vlog_subkind(type) == DT_QUEUE || vlog_subkind(type) == DT_DYNARRAY)
            && vlog_has_type(type))
      return elab_virtual_element(vlog_get_type(vlog_type(type)));

   return NULL;
}

static void elab_check_handle_pattern(vlog_node_t pattern, vlog_node_t type,
                                      const elab_href_ctx_t *hctx)
{
   const int nparams = vlog_params(pattern);
   for (int i = 0; i < nparams; i++) {
      vlog_node_t item = vlog_param(pattern, i);
      if (vlog_kind(item) != V_PATTERN_ITEM)
         continue;

      vlog_node_t expr = vlog_right(item);
      if (vlog_kind(expr) == V_PATTERN_EXPR)
         elab_check_handle_pattern(expr, type, hctx);
      else
         elab_check_handle_value(expr, type, hctx);
   }
}

static void elab_check_array_copy(vlog_node_t value, vlog_node_t target_type,
                                  const elab_href_ctx_t *hctx)
{
   // Handles in an assignment pattern for an array of virtual interfaces

   vlog_node_t ttype = elab_virtual_element(target_type);
   if (ttype != NULL && vlog_kind(value) == V_PATTERN_EXPR)
      elab_check_handle_pattern(value, ttype, hctx);
}

static vlog_node_t elab_rewrite_hier_ref_cb(vlog_node_t v, void *arg);

static void elab_expand_instance_array(vlog_node_t v,
                                       const elab_href_ctx_t *hctx)
{
   // An array of virtual interfaces takes the elements of an array of
   // instances or interface ports

   vlog_node_t value = vlog_value(v), ttype = vlog_get_type(vlog_target(v));
   if (vlog_kind(value) != V_HIER_REF || vlog_ident(value) != ident_new("$self"))
      return;
   else if (elab_virtual_element(ttype) == NULL
            || elab_virtual_element(ttype) == ttype)
      return;

   vlog_node_t mr = vlog_value(value);
   if (vlog_kind(mr) != V_MOD_REF || vlog_params(mr) != 0 || !vlog_has_ref(mr)
       || vlog_has_value(mr))
      return;

   vlog_node_t decl = vlog_ref(mr);
   if ((vlog_kind(decl) != V_MOD_INST && vlog_kind(decl) != V_IFACE_PORT)
       || vlog_ranges(decl) != 1)
      return;

   int64_t left, right;
   if (!vlog_bounds(vlog_range(decl, 0), &left, &right))
      return;

   const int step = left <= right ? 1 : -1;

   vlog_node_t pattern = vlog_new(V_PATTERN_EXPR);
   vlog_set_type(pattern, ttype);
   vlog_set_loc(pattern, vlog_loc(value));

   for (int64_t i = left; step > 0 ? i <= right : i >= right; i += step) {
      vlog_node_t index = vlog_new(V_NUMBER);
      vlog_set_number(index, number_from_int(i));
      vlog_set_loc(index, vlog_loc(value));

      vlog_node_t emr = vlog_new(V_MOD_REF);
      vlog_set_ident(emr, vlog_ident(mr));
      vlog_set_ref(emr, decl);
      vlog_set_loc(emr, vlog_loc(mr));
      vlog_add_param(emr, index);

      vlog_node_t elem = vlog_new(V_HIER_REF);
      vlog_set_value(elem, emr);
      vlog_set_ident(elem, ident_new("$self"));
      vlog_set_loc(elem, vlog_loc(value));
      vlog_set_global_flags(elem, VLOG_GF_HIER_REF);

      vlog_node_t item = vlog_new(V_PATTERN_ITEM);
      vlog_set_right(item, elab_rewrite_hier_ref_cb(elem, (void *)hctx));
      vlog_add_param(pattern, item);
   }

   vlog_set_value(v, pattern);
}

static void elab_check_handle_assign(vlog_node_t v,
                                     const elab_href_ctx_t *hctx)
{
   elab_check_net_assign(v);
   elab_expand_instance_array(v, hctx);
   elab_check_handle_value(vlog_value(v), vlog_get_type(vlog_target(v)), hctx);
   elab_check_array_copy(vlog_value(v), vlog_get_type(vlog_target(v)), hctx);
}

static vlog_node_t elab_find_ctor(vlog_node_t class)
{
   for (vlog_node_t c = class;; c = vlog_ref(c)) {
      const int ndecls = vlog_decls(c);
      for (int i = 0; i < ndecls; i++) {
         if (vlog_kind(vlog_decl(c, i)) == V_CONSTRUCTOR)
            return vlog_decl(c, i);
      }

      if (!vlog_has_ref(c))
         return NULL;
   }
}

static void elab_check_handle_call(vlog_node_t v, const elab_href_ctx_t *hctx)
{
   // The arguments of a call are checked against the types of the formals

   vlog_node_t decl = NULL;
   switch (vlog_kind(v)) {
   case V_USER_FCALL:
   case V_USER_TCALL:
      decl = vlog_has_ref(v) ? vlog_ref(v) : NULL;
      break;
   case V_METHOD_CALL:
      if (vlog_subkind(v) == V_METHOD_CLASS && vlog_has_ref(v))
         decl = vlog_ref(v);
      else if (vlog_subkind(v) == V_METHOD_ARR_INSERT && vlog_params(v) == 2) {
         vlog_node_t qtype = vlog_get_type(vlog_value(v));
         if (qtype != NULL && vlog_kind(qtype) == V_DATA_TYPE
             && vlog_has_type(qtype))
            elab_check_handle_value(vlog_param(v, 1), vlog_get_type(
                                       vlog_type(qtype)), hctx);
      }
      else if ((vlog_subkind(v) == V_METHOD_PUSH_BACK
                || vlog_subkind(v) == V_METHOD_PUSH_FRONT)
               && vlog_params(v) == 1) {
         vlog_node_t qtype = vlog_get_type(vlog_value(v));
         if (qtype != NULL && vlog_kind(qtype) == V_DATA_TYPE
             && vlog_has_type(qtype))
            elab_check_handle_value(vlog_param(v, 0), vlog_get_type(
                                       vlog_type(qtype)), hctx);
      }
      break;
   case V_CLASS_NEW:
      if (vlog_has_type(v) && vlog_kind(vlog_type(v)) == V_CLASS_DECL)
         decl = elab_find_ctor(vlog_type(v));
      break;
   default:
      break;
   }

   if (decl == NULL)
      return;

   const bool is_ctor = vlog_kind(decl) == V_CONSTRUCTOR;
   if (vlog_kind(decl) != V_FUNC_DECL && vlog_kind(decl) != V_TASK_DECL
       && !is_ctor)
      return;

   const int nformals = is_ctor ? vlog_params(decl) : vlog_ports(decl);
   const int nparams = MIN(vlog_params(v), nformals);
   for (int i = 0; i < nparams; i++) {
      vlog_node_t actual = vlog_param(v, i);
      int fpos = i;
      if (vlog_kind(actual) == V_NAMED_ARG) {
         ident_t name = vlog_ident(actual);
         actual = vlog_has_value(actual) ? vlog_value(actual) : NULL;
         for (fpos = 0; fpos < nformals; fpos++) {
            vlog_node_t f = is_ctor ? vlog_param(decl, fpos)
               : vlog_port(decl, fpos);
            if (vlog_kind(f) == V_REF && vlog_has_ref(f))
               f = vlog_ref(f);
            if (vlog_has_ident(f) && vlog_ident(f) == name)
               break;
         }

         if (actual == NULL || fpos == nformals)
            continue;
      }

      vlog_node_t formal = is_ctor ? vlog_param(decl, fpos)
         : vlog_port(decl, fpos);
      if (vlog_kind(formal) == V_REF && vlog_has_ref(formal))
         formal = vlog_ref(formal);

      if (vlog_kind(formal) != V_TF_PORT_DECL && vlog_kind(formal) != V_PORT_DECL
          && vlog_kind(formal) != V_VAR_DECL)
         continue;

      elab_check_handle_value(actual, vlog_get_type(formal), hctx);
      elab_check_array_copy(actual, vlog_get_type(formal), hctx);
   }
}

static void elab_rewrite_hier_ref_pre_cb(vlog_node_t v, void *arg)
{
   // A nested generate block is rewritten from its own scope
   elab_href_ctx_t *hctx = arg;
   if (vlog_kind(v) == V_GEN_BLOCK && v != hctx->root)
      hctx->gen_depth++;
   else if (vlog_kind(v) == V_DEFPARAM) {
      // The target is resolved when the parameter is overridden
      if (defparam_targets == NULL)
         defparam_targets = hset_new(16);
      hset_insert(defparam_targets, vlog_target(v));
   }
   else if ((vlog_kind(v) == V_METHOD_CALL || vlog_kind(v) == V_DISABLE)
            && vlog_has_value(v)
            && vlog_kind(vlog_value(v)) == V_HIER_REF) {
      if (call_values == NULL)
         call_values = hset_new(16);
      hset_insert(call_values, vlog_value(v));
   }
}

static vlog_node_t elab_hier_ref_vhdl_record(vlog_node_t v,
                                             const elab_href_ctx_t *hctx)
{
   // A name which selects an element of a VHDL record signal or of an
   // array of records

   vlog_node_t mods[16];
   int nmods = 0;
   for (vlog_node_t it = vlog_value(v); ; it = vlog_value(it)) {
      if (nmods == ARRAY_LEN(mods))
         return NULL;

      mods[nmods++] = it;
      if (!vlog_has_value(it))
         break;
   }

   // The names were collected from the end of the path
   for (int i = 0; i < nmods / 2; i++) {
      vlog_node_t tmp = mods[i];
      mods[i] = mods[nmods - 1 - i];
      mods[nmods - 1 - i] = tmp;
   }

   tree_t pb = elab_walk_mod_refs(mods[0], hctx->where, hctx->parents);
   for (int i = 1; pb != NULL && i < nmods; i++) {
      ident_t id = vlog_ident(mods[i]);
      if (elab_mod_ref_has_block(pb, mods[i])) {
         pb = elab_walk_mod_refs(mods[i], hctx->where, hctx->parents);
         continue;
      }
      else if (!elab_is_vhdl_record_signal(pb, id))
         return NULL;

      tree_t obj = elab_find_vhdl_signal(pb, id);
      assert(obj != NULL);

      vlog_node_t steps[nmods - i + 1];
      for (int j = i; j < nmods; j++)
         steps[j - i] = mods[j];
      steps[nmods - i] = v;

      if (elab_hier_ref_vhdl_path(v, obj, steps, nmods - i + 1))
         vlog_set_value(v, mods[i - 1]);

      return v;
   }

   return NULL;
}

static vlog_node_t elab_hier_ref_member(vlog_node_t base, ident_t id,
                                        const loc_t *loc)
{
   // A member of a structure, union or class reached through a variable

   vlog_node_t type = vlog_get_type(base), f = NULL;
   if (type != NULL && vlog_kind(type) == V_CLASS_DECL)
      f = vlog_class_member(type, id);
   else if (type != NULL
            && (vlog_kind(type) == V_STRUCT_DECL
                || vlog_kind(type) == V_UNION_DECL)) {
      const int nfields = vlog_decls(type);
      for (int i = 0; i < nfields && f == NULL; i++) {
         if (vlog_ident(vlog_decl(type, i)) == id)
            f = vlog_decl(type, i);
      }
   }

   if (f == NULL)
      return NULL;

   vlog_node_t member = vlog_new(V_MEMBER_REF);
   vlog_set_value(member, base);
   vlog_set_ident(member, id);
   vlog_set_ref(member, f);
   vlog_set_loc(member, loc);
   return member;
}

static vlog_node_t elab_hier_ref_select(vlog_node_t base, vlog_node_t mr)
{
   // The indices select an element of an unpacked array

   const int nindex = vlog_params(mr);
   if (nindex == 0)
      return base;

   vlog_node_t bs = vlog_new(V_BIT_SELECT);
   vlog_set_value(bs, base);
   vlog_set_loc(bs, vlog_loc(mr));
   for (int i = 0; i < nindex; i++)
      vlog_add_param(bs, vlog_param(mr, i));

   return bs;
}

static vlog_node_t elab_hier_ref_variable(vlog_node_t v,
                                          const elab_href_ctx_t *hctx)
{
   // The path names a variable of an instance followed by members of it

   vlog_node_t mods[16];
   int nmods = 0;
   for (vlog_node_t it = vlog_value(v); ; it = vlog_value(it)) {
      if (nmods == ARRAY_LEN(mods))
         return NULL;

      mods[nmods++] = it;
      if (!vlog_has_value(it))
         break;
   }

   // The names were collected from the end of the path
   for (int i = 0; i < nmods / 2; i++) {
      vlog_node_t tmp = mods[i];
      mods[i] = mods[nmods - 1 - i];
      mods[nmods - 1 - i] = tmp;
   }

   for (int i = 1; i < nmods; i++) {
      tree_t pb = elab_walk_mod_refs(mods[i - 1], hctx->where, hctx->parents);
      if (pb == NULL)
         return v;

      ident_t id = vlog_ident(mods[i]);
      if (elab_has_child_block(pb, id))
         continue;

      vlog_node_t vd = elab_hier_ref_decl(pb, id);
      if (vd == NULL)
         return NULL;
      else if (vlog_kind(vd) == V_IFACE_PORT)
         continue;

      vlog_node_t base = vlog_new(V_HIER_REF);
      vlog_set_value(base, mods[i - 1]);
      vlog_set_ident(base, id);
      vlog_set_ref(base, vd);
      vlog_set_loc(base, vlog_loc(v));
      vlog_set_global_flags(base, VLOG_GF_HIER_REF);

      base = elab_hier_ref_select(base, mods[i]);

      for (int j = i + 1; j < nmods; j++) {
         vlog_node_t m = elab_hier_ref_member(base, vlog_ident(mods[j]),
                                              vlog_loc(mods[j]));
         if (m == NULL) {
            error_at(vlog_loc(mods[j]), "%pQ has no member named %pQ",
                     vlog_ident(mods[j - 1]), vlog_ident(mods[j]));
            return v;
         }

         base = elab_hier_ref_select(m, mods[j]);
      }

      vlog_node_t m = elab_hier_ref_member(base, vlog_ident(v), vlog_loc(v));
      if (m != NULL)
         return m;
      else if (call_values != NULL && hset_contains(call_values, v))
         return base;   // A built-in method of the variable

      error_at(vlog_loc(v), "%pQ has no member named %pQ",
               vlog_ident(mods[nmods - 1]), vlog_ident(v));
      return v;
   }

   return NULL;
}

static vlog_node_t elab_rewrite_hier_ref_cb(vlog_node_t v, void *arg)
{
   elab_href_ctx_t *hctx = arg;

   if (vlog_kind(v) == V_GEN_BLOCK && v != hctx->root) {
      hctx->gen_depth--;
      return v;
   }
   else if (hctx->gen_depth > 0)
      return v;
   else if (defparam_targets != NULL && hset_contains(defparam_targets, v))
      return v;
   else if (vlog_kind(v) == V_METHOD_CALL) {
      // A method of a class handle reached by a hierarchical reference
      vlog_node_t member = vlog_value(v);
      const bool resolved = vlog_subkind(v) != V_METHOD_USER || vlog_has_ref(v);
      if ((vlog_kind(member) == V_HIER_REF || vlog_kind(member) == V_MEMBER_REF)
          && vlog_has_ref(member)) {
         vlog_node_t decl = vlog_ref(member);
         if (vlog_kind(decl) == V_FUNC_DECL || vlog_kind(decl) == V_TASK_DECL) {
            if (vlog_kind(member) == V_MEMBER_REF) {
               vlog_set_value(v, vlog_value(member));
               vlog_set_ref(v, decl);
               vlog_set_subkind(v, V_METHOD_CLASS);
            }
         }
         else if (!resolved)
            vlog_resolve_method(v);
      }
      elab_check_handle_call(v, hctx);
      return v;
   }
   else if (vlog_kind(v) == V_BASSIGN || vlog_kind(v) == V_NBASSIGN) {
      elab_check_handle_assign(v, hctx);
      return v;
   }
   else if (vlog_kind(v) == V_SYS_TCALL || vlog_kind(v) == V_SYS_FCALL) {
      // The %p of a structure reached by a hierarchical name is expanded
      // now that the name is bound
      return vlog_simp_format(v);
   }
   else if (vlog_kind(v) == V_RETURN) {
      if (vlog_has_value(v) && vlog_has_ref(v))
         elab_check_handle_value(vlog_value(v), vlog_get_type(vlog_ref(v)),
                                 hctx);
      return v;
   }
   else if (vlog_kind(v) == V_VAR_DECL || vlog_kind(v) == V_LOCAL_DECL) {
      if (vlog_has_value(v)) {
         elab_check_handle_value(vlog_value(v), vlog_get_type(v), hctx);
         elab_check_array_copy(vlog_value(v), vlog_get_type(v), hctx);
      }
      return v;
   }
   else if (vlog_kind(v) == V_USER_FCALL || vlog_kind(v) == V_USER_TCALL
            || vlog_kind(v) == V_CLASS_NEW) {
      elab_check_handle_call(v, hctx);
      return v;
   }
   else if (vlog_kind(v) != V_HIER_REF)
      return v;
   else if (vlog_has_ref(v) && vlog_kind(vlog_ref(v)) == V_VAR_DECL
            && (vlog_flags(vlog_ref(v)) & VLOG_F_VHDL_PATH))
      return v;

   if (vlog_ident(v) == ident_new("$global_clock$v") && !vlog_has_ref(v)) {
      // The global clocking of this instance or the closest enclosing one
      tree_t found = NULL;
      for (tree_t it = hctx->where; it != NULL && found == NULL;
           it = hash_get(hctx->parents, it)) {
         if (tree_kind(it) != T_BLOCK)
            break;
         else if (elab_hier_ref_decl(it, vlog_ident(v)) != NULL)
            found = it;
      }

      if (found == NULL) {
         error_at(vlog_loc(v), "no global clocking declaration is visible "
                  "for $global_clock");
         return v;
      }

      vlog_node_t mr = vlog_new(V_MOD_REF);
      vlog_set_ident(mr, tree_ident(found));
      vlog_set_loc(mr, vlog_loc(v));
      vlog_set_value(v, mr);
   }

   vlog_node_t prefix = vlog_value(v);

   // The whole of an instance array is selected by the elements of the
   // instance it is connected to
   if (vlog_kind(prefix) == V_MOD_REF && vlog_params(prefix) == 0
       && vlog_has_ref(prefix)
       && (vlog_kind(vlog_ref(prefix)) == V_MOD_INST
           || vlog_kind(vlog_ref(prefix)) == V_IFACE_PORT)
       && vlog_ranges(vlog_ref(prefix)) > 0)
      return v;

   // A name before the member may be a structure variable
   if (vlog_has_value(prefix)) {
      vlog_node_t rec = elab_hier_ref_vhdl_record(v, hctx);
      if (rec != NULL)
         return rec;

      rec = elab_hier_ref_variable(v, hctx);
      if (rec != NULL)
         return rec;
   }

   tree_t b = elab_walk_mod_refs(prefix, hctx->where, hctx->parents);
   if (b == NULL)
      return v;

   ident_t id = vlog_ident(v);

   if (id == ident_new("$self"))
      return v;   // Context of the instance itself

   vlog_node_t mp = elab_hier_ref_modport(b, id);
   if (mp != NULL) {
      // A modport selected from an interface instance used as a value
      vlog_set_ident(v, ident_new("$self"));
      return v;
   }

   if (vlog_iface_port_target(b, NULL, id) != NULL
       || elab_has_child_block(b, id)) {
      // An instance or interface port used as a value
      vlog_node_t next = vlog_new(V_MOD_REF);
      vlog_set_ident(next, id);
      vlog_set_value(next, prefix);
      vlog_set_loc(next, vlog_loc(v));

      vlog_set_value(v, next);
      vlog_set_ident(v, ident_new("$self"));
      // Rewritten again and checked with the new prefix
      return v;
   }

   b = vlog_hier_target_block(b, id);

   tree_t port = vlog_hier_vhdl_port(b, id);
   if (port != NULL)
      return elab_hier_ref_vhdl(v, port);

   vlog_node_t param = elab_hier_ref_param(b, id);
   if (param != NULL) {
      // The value may contain names that are only bound relative to the
      // instance it belongs to and that instance may not be rewritten yet
      elab_href_ctx_t sub = *hctx;
      sub.where = b;
      sub.root = NULL;
      sub.gen_depth = 0;
      return vlog_rewrite(param, elab_rewrite_hier_ref_pre_cb,
                          elab_rewrite_hier_ref_cb, &sub);
   }

   param = elab_hier_ref_generic(b, id, vlog_loc(v));
   if (param != NULL)
      return param;

   const int ndecls = tree_decls(b);
   for (int i = 0; i < ndecls; i++) {
      tree_t wrap = tree_decl(b, i);

      if (tree_kind(wrap) == T_SIGNAL_DECL
          && ident_casecmp(tree_ident(wrap), id))
         return elab_hier_ref_vhdl(v, wrap);

      if (tree_kind(wrap) != T_VERILOG)
         continue;

      vlog_node_t vd = tree_vlog(wrap);
      if (vlog_kind(vd) == V_PORT_MAP || vlog_ident(vd) != id)
         continue;

      switch (vlog_kind(vd)) {
      case V_FUNC_DECL:
      case V_TASK_DECL:
         vlog_set_ref(v, vd);

         if (call_values == NULL || !hset_contains(call_values, v)) {
            // A subroutine called without parentheses
            vlog_node_t call = vlog_new(V_METHOD_CALL);
            vlog_set_value(call, v);
            vlog_set_ident(call, vlog_ident(v));
            vlog_set_loc(call, vlog_loc(v));
            vlog_set_global_flags(call, VLOG_GF_HIER_REF);
            return call;
         }

         return v;
      case V_NET_DECL:
      case V_VAR_DECL:
         vlog_set_ref(v, vd);
         return v;
      default:
         {
            diag_t *d = diag_new(DIAG_ERROR, vlog_loc(v));
            diag_printf(d, "target of hierarchical reference must be a net "
                        "or variable");
            diag_hint(d, vlog_loc(vd), "%pQ refers to this object", id);
            diag_emit(d);
         }
      }
   }

   tree_t h = tree_decl(b, 0);
   assert(tree_kind(h) == T_HIER);

   diag_t *d = diag_new(DIAG_ERROR, vlog_loc(v));
   diag_printf(d, "name %pQ not found in %pQ", id, tree_ident(b));
   diag_hint(d, tree_loc(b), "%pQ is an instance of %pI", tree_ident(b),
             tree_ident(tree_ref(h)));
   diag_emit(d);

   return v;
}

static void elab_fold_hrefs(mod_cache_t *mc, const elab_ctx_t *ctx)
{
   vlog_node_t v = vlog_from_object(mc->object);
   if (v == NULL)
      return;
   else if (!(vlog_global_flags(v) & VLOG_GF_HIER_REF)
            && !vlog_bind_has_hier_ref(v))
      return;

   const void *key;
   void *value;
   for (hash_iter_t it = HASH_BEGIN;
        ghash_iter(mc->instances, &it, &key, &value); ) {
      for (elab_instance_t *ei = value; ei != NULL; ei = ei->chain) {
         elab_href_ctx_t hctx = {
            .where   = ei->first,
            .parents = ctx->block_parents,
            .iface_first  = ctx->iface_first,
            .iface_values = ctx->iface_values,
            .root    = ei->body,
         };
         vlog_rewrite(ei->body, elab_rewrite_hier_ref_pre_cb,
                      elab_rewrite_hier_ref_cb, &hctx);
         vlog_fold(ei->body, ctx->mir, ctx->jit);
      }
   }
}

static tree_t elab_iface_conn_target(const elab_iface_conn_t *c,
                                     const elab_ctx_t *ctx)
{
   ident_t name = vlog_ident(c->port);

   if (c->value == NULL) {
      // Implicit connection to an instance or port with the same name
      const int nstmts = tree_stmts(c->parent);
      for (int i = 0; i < nstmts; i++) {
         tree_t s = tree_stmt(c->parent, i);
         if (tree_kind(s) == T_BLOCK && ident_casecmp(tree_ident(s), name))
            return s;
      }

      tree_t target = vlog_iface_port_target(c->parent, NULL, name);
      if (target == NULL)
         error_at(&c->loc, "interface port %pQ must be connected", name);

      return target;
   }

   if (vlog_kind(c->value) != V_HIER_REF) {
      error_at(&c->loc, "interface port %pQ must be connected to an instance "
               "of an interface or another interface port", name);
      return NULL;
   }

   hset_insert(ctx->iface_values, c->value);

   vlog_node_t prefix = vlog_value(c->value);
   ident_t id = vlog_ident(c->value);

   if (id == ident_new("$self"))
      return elab_walk_mod_refs(prefix, c->parent, ctx->block_parents);

   vlog_node_t next = vlog_new(V_MOD_REF);
   vlog_set_ident(next, id);
   vlog_set_value(next, prefix);
   vlog_set_loc(next, &c->loc);

   return elab_walk_mod_refs(next, c->parent, ctx->block_parents);
}

static void elab_iface_conn_resolve(const elab_iface_conn_t *c,
                                    const elab_ctx_t *ctx)
{
   tree_t target = elab_iface_conn_target(c, ctx);
   if (target == NULL)
      return;

   tree_t h = tree_decl(target, 0);
   vlog_node_t body = NULL;
   if (tree_kind(h) == T_HIER && tree_has_ref(h)
       && tree_kind(tree_ref(h)) == T_VERILOG)
      body = tree_vlog(tree_ref(h));

   if (body == NULL || !(vlog_flags(body) & VLOG_F_INTERFACE)) {
      error_at(&c->loc, "interface port %pQ must be connected to an "
               "instance of an interface", vlog_ident(c->port));
      return;
   }

   ident_t expect = vlog_ident2(c->port);
   if (expect != ident_new("interface") && expect != vlog_ident2(body)) {
      error_at(&c->loc, "interface port %pQ has interface %pQ but is "
               "connected to an instance of %pQ", vlog_ident(c->port),
               expect, vlog_ident2(body));
      return;
   }

   tree_set_ref(c->wrap, target);
}

static void elab_iface_conns(const elab_ctx_t *ctx)
{
   // The order is such that the port of a parent is resolved first
   for (int i = 0; i < ctx->iface_conns->count; i++)
      elab_iface_conn_resolve(&(ctx->iface_conns->items[i]), ctx);
}

static void elab_ignore_diag(diag_t *d, void *context)
{
}

static void elab_early_iface_conns(const elab_ctx_t *ctx, int first)
{
   // The interface instance may be known before the end of elaboration
   // which allows hierarchical names through the port to be bound when
   // the instances below this one are created.  Report any errors later.

   void *saved_ctx;
   diag_consumer_t saved = diag_get_consumer(&saved_ctx);
   diag_set_consumer(elab_ignore_diag, NULL);

   for (int i = first; i < ctx->iface_conns->count; i++) {
      const elab_iface_conn_t *c = &(ctx->iface_conns->items[i]);
      if (!tree_has_ref(c->wrap))
         elab_iface_conn_resolve(c, ctx);
   }

   diag_set_consumer(saved, saved_ctx);
}

static void elab_set_design_precision(const elab_ctx_t *ctx)
{
   // The time precision of the design is the smallest of all the roots

   elab_precision_t p = {
      .ctx   = ctx,
      .seen  = hset_new(16),
      .log10 = UINT_MAX,
   };

   for (int i = 0; i < ctx->nroots; i++) {
      tree_t vhdl = tree_from_object(ctx->roots[i]);
      if (vhdl != NULL && tree_kind(vhdl) != T_CONFIGURATION) {
         tree_t top = vhdl;
         if (tree_kind(vhdl) == T_ENTITY)
            top = elab_pick_arch(vhdl, tree_loc(vhdl), ctx);

         elab_scan_vhdl_precision(top, &p);
      }

      vlog_node_t vlog = vlog_from_object(ctx->roots[i]);
      if (vlog != NULL)
         elab_scan_precision(vlog, &p);
   }

   hset_free(p.seen);

   if (p.scaled != NULL && p.unscaled != NULL) {
      // 3.14.2.3: a design element without a time unit takes the
      // default of the tool, which is unlikely to be intended when other
      // design elements have one.  IEEE 1364-2005 19.8 makes this an
      // error; it is a warning here so that such designs still run
      diag_t *d = diag_new(DIAG_WARN, vlog_loc(p.unscaled));
      diag_printf(d, "%s has no time unit and uses the default time unit "
                  "and precision of 1ns/1ns but %s has a time unit", istr(vlog_ident2(p.unscaled)),
                  istr(vlog_ident2(p.scaled)));
      diag_hint(d, vlog_loc(p.unscaled), "no `timescale or timeunit");
      diag_hint(d, vlog_loc(p.scaled), "has a time unit");
      diag_emit(d);
   }

   vlog_set_design_precision(p.log10 != UINT_MAX ? p.log10 : 6);
}

static void elab_root_cb(void *arg)
{
   elab_ctx_t *ctx = arg;

   elab_set_design_precision(ctx);

   for (int i = 0; i < ctx->nroots; i++) {
      tree_t vhdl = tree_from_object(ctx->roots[i]);
      if (vhdl != NULL)
         elab_vhdl_root(vhdl, ctx);

      vlog_node_t vlog = vlog_from_object(ctx->roots[i]);
      if (vlog != NULL)
         elab_verilog_root(vlog, ctx);
   }

   elab_iface_conns(ctx);

   const void *key;
   void *value;
   for (hash_iter_t it = HASH_BEGIN;
        hash_iter(ctx->modcache, &it, &key, &value); )
      elab_fold_hrefs(value, ctx);

   for (int i = 0; i < ctx->gen_bodies->count; i++) {
      const elab_gen_body_t *gb = &(ctx->gen_bodies->items[i]);
      elab_href_ctx_t hctx = {
         .where   = gb->where,
         .parents = ctx->block_parents,
         .iface_first  = ctx->iface_first,
         .iface_values = ctx->iface_values,
         .root    = gb->body,
      };
      vlog_rewrite(gb->body, elab_rewrite_hier_ref_pre_cb,
                   elab_rewrite_hier_ref_cb, &hctx);
      vlog_fold(gb->body, ctx->mir, ctx->jit);

      // The instances below the block are elaborated from the original
      hctx.root = gb->block;
      vlog_rewrite(gb->block, elab_rewrite_hier_ref_pre_cb,
                   elab_rewrite_hier_ref_cb, &hctx);
   }
}

static int elab_compar_modcache(const void *a, const void *b)
{
   const mod_cache_t *mc_a = a, *mc_b = b;

   if (mc_b->count < mc_a->count)
      return -1;
   else if (mc_b->count > mc_a->count)
      return 1;
   else
      return 0;
}

static void elab_print_stats(const elab_ctx_t *ctx)
{
   const unsigned count = hash_members(ctx->modcache);
   mod_cache_t *sorted LOCAL = xmalloc_array(count, sizeof(mod_cache_t));

   int pos = 0;
   const void *key;
   mod_cache_t *mc;
   for (hash_iter_t it = HASH_BEGIN;
        hash_iter(ctx->modcache, &it, &key, (void *)&mc); )
      sorted[pos++] = *mc;
   assert(pos == count);

   qsort(sorted, count, sizeof(mod_cache_t), elab_compar_modcache);

   nvc_printf("\n$bold$%-50s %10s %10s$$\n", "Design Unit", "Instances",
              "Unique");

   for (int i = 0; i < count; i++) {
      ident_t name = NULL;

      tree_t vhdl = tree_from_object(sorted[i].object);
      if (vhdl != NULL)
         name = tree_ident(vhdl);

      vlog_node_t vlog = vlog_from_object(sorted[i].object);
      if (vlog != NULL)
         name = vlog_ident(vlog);

      printf("%-50s %10d %10d\n", istr(name), sorted[i].count,
             sorted[i].unique);
   }

   printf("\n");
}

tree_t elab(object_t **top, int ntop, jit_t *jit, unit_registry_t *ur,
            mir_context_t *mc, cover_data_t *cover, sdf_file_t *sdf,
            rt_model_t *m)
{
   assert(ntop >= 1);

   make_new_arena();

   elab_nblack = 0;
   if (elab_black_seen != NULL) {
      hset_free(elab_black_seen);
      elab_black_seen = NULL;
   }


   if (elab_ic_types != NULL) {
      hash_free(elab_ic_types);
      elab_ic_types = NULL;
   }

   ident_t name = NULL;

   tree_t vhdl = tree_from_object(top[0]);
   if (vhdl != NULL)
      name = ident_prefix(tree_ident(vhdl), well_known(W_ELAB), '.');

   vlog_node_t vlog = vlog_from_object(top[0]);
   if (vlog != NULL)
      name = ident_prefix(vlog_ident(vlog), well_known(W_ELAB), '.');

   // A configuration is elaborated as the cells of its design statement
   vlog_node_t config = NULL;
   object_t **cells LOCAL = NULL;
   if (vlog != NULL && vlog_kind(vlog) == V_CONFIG) {
      config = vlog;

      const int ncells = vlog_params(config);
      if (ncells == 0)
         fatal("configuration %pQ has no design statement",
               vlog_ident2(config));

      cells = xmalloc_array(ncells, sizeof(object_t *));
      for (int i = 0; i < ncells; i++) {
         vlog_node_t d = vlog_param(config, i);
         const char *str = istr(vlog_ident(d)), *dot = strchr(str, '.');

         lib_t lib = lib_work();
         if (dot != NULL) {
            char *libname LOCAL = xstrndup(str, dot - str);
            if (ident_casecmp(ident_new(libname), ident_new("work")))
               lib = lib_work();
            else if ((lib = lib_find(ident_new(libname))) == NULL)
               fatal("library %s not found", libname);
            str = dot + 1;
         }

         LOCAL_TEXT_BUF tb = tb_new();
         tb_cat(tb, str);
         tb_upcase(tb);

         ident_t qual = ident_prefix(lib_name(lib), ident_new(tb_get(tb)), '.');
         if ((cells[i] = lib_get_generic(lib, qual, NULL)) == NULL)
            fatal("cannot find unit %pI in library %pI", qual, lib_name(lib));
      }

      top = cells;
      ntop = ncells;
   }

   if (vhdl == NULL && vlog == NULL)
      fatal("top level is not a VHDL design unit or Verilog module");

   vlog_set_fold_context(mc, jit);

   tree_t e = tree_new(T_ELAB);
   tree_set_ident(e, name);
   tree_set_loc(e, &(top[0]->loc));

   lib_t work = lib_work();
   vlog_load_binds(work, top, ntop);

   unsigned nshims = 0;
   iface_conn_list_t iface_conns = AINIT;
   gen_body_list_t gen_bodies = AINIT;

   elab_ctx_t ctx = {
      .out       = e,
      .roots     = top,
      .nroots    = ntop,
      .cover     = cover,
      .library   = work,
      .jit       = jit,
      .sdf       = sdf,
      .registry  = ur,
      .mir       = mc,
      .modcache  = hash_new(16),
      .mracache  = hash_new(16),
      .vucache   = hash_new(16),
      .iface_conns = &iface_conns,
      .gen_bodies = &gen_bodies,
      .block_parents = hash_new(16),
      .iface_first = hash_new(16),
      .iface_units = hash_new(16),
      .iface_values = hset_new(16),
      .nshims    = &nshims,
      .dotted    = lib_name(work),
      .config    = config,
      .model     = m,
      .scope     = create_scope(m, e, NULL),
      .pool      = pool_new(),
   };

   call_with_model(m, elab_root_cb, &ctx);

   tree_set_ival(e, vlog_design_precision());

   lint_flush();

   if (opt_get_int(OPT_ELAB_STATS))
      elab_print_stats(&ctx);

   const void *key;
   void *value;
   for (hash_iter_t it = HASH_BEGIN;
        hash_iter(ctx.modcache, &it, &key, &value); )
      ghash_free(((mod_cache_t *)value)->instances);

   hash_free(ctx.modcache);
   hash_free(ctx.mracache);
   hash_free(ctx.vucache);
   ACLEAR(iface_conns);
   ACLEAR(gen_bodies);
   hash_free(ctx.block_parents);
   hash_free(ctx.iface_first);
   hash_free(ctx.iface_units);
   hset_free(ctx.iface_values);
   pool_free(ctx.pool);

   if (error_count() > 0)
      return NULL;

   if (opt_get_verbose(OPT_ELAB_VERBOSE, NULL))
      dump(e);

   for (generic_list_t *it = generic_override; it != NULL; it = it->next)
      warnf("generic value for %pI not used", it->name);

   ident_t b0_name = tree_ident(tree_stmt(e, 0));
   ident_t vu_name = ident_prefix(lib_name(ctx.library), b0_name, '.');
   unit_registry_flush(ur, vu_name);

   freeze_global_arena();
   return e;
}
