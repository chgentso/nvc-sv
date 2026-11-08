//
//  Copyright (C) 2023-2026  Nick Gasson
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
#include "hash.h"
#include "ident.h"
#include "mir/mir-node.h"
#include "mir/mir-unit.h"
#include "printf.h"
#include "rt/assert.h"
#include "rt/defs.h"
#include "type.h"
#include "vlog/vlog-defs.h"
#include "vlog/vlog-node.h"
#include "vlog/vlog-number.h"
#include "vlog/vlog-phase.h"
#include "vlog/vlog-util.h"
#include "vpi/vpi-model.h"
#include "vpi/vpi_user.h"

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <float.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

typedef struct _gen_stack gen_stack_t;

typedef struct {
   vlog_node_t fork;
   vlog_node_t up;
   ident_t    *blocks;
   int         nblocks;
} fork_up_t;

typedef struct {
   mir_type_t  type;
   mir_stamp_t stamp;
   unsigned    size;
   mir_type_t  signal;
   mir_type_t  unpacked;
   unsigned    elemsz;
   vlog_node_t node;
} type_info_t;

typedef struct {
   mir_value_t obj;
   mir_value_t dst_offset;
   mir_value_t src_offset;
   mir_value_t count;
   mir_type_t  type;
   unsigned    size;
   bool        two_state;
} vlog_select_t;

typedef struct {
   ident_t     name;
   mir_block_t test_bb;
   mir_block_t exit_bb;
} loop_info_t;

typedef struct {
   ident_t     name;
   ident_t     label;     // Identifies the block to other processes
   mir_block_t exit_bb;
   bool        has_fork;
} block_info_t;

struct _gen_stack {
   gen_stack_t  *up;
   loop_info_t  *loop;
   block_info_t *block;
   cover_obj_t   cscope;
};

typedef struct {
   mir_unit_t    *mu;
   mir_context_t *mir;
   ihash_t       *temps;
   mir_block_t    exit;
   bool           constfunc;
   bool           for_write;
   int            stream_ubits;
   bool           ref_bind;
   vlog_node_t    update_key;  // Index of the target of an update
   mir_value_t    this_val;
   mir_value_t    crv_ctx;
   mir_value_t    crv_obj;
   vlog_node_t    crv_class;
   vlog_node_t    crv_std;
   unsigned       crv_width;
   bool           crv_signed;
   int            crv_nindex;
   struct {
      vlog_node_t decl;
      mir_value_t node;
      vlog_node_t assoc;   // Array this is the index of if associative
      mir_value_t flat;
      mir_value_t key;     // Key if this is unrolled at run time
      vlog_node_t aref;    // Reference to the associative array
      bool        known;   // Has a value at run time
   }              crv_index[8];
   mir_value_t    last_index;
   vlog_node_t    subr;
   const type_info_t *unbound;
   unsigned           bind_size;   // Number of nets expected when binding
   vlog_node_t    fork_root;
   gen_stack_t   *wait_gs;
   hash_t        *past;
   bool           in_sva;
   vlog_node_t    sva_clock;
   vlog_node_t    sva_clk;     // Clock edge when an idle assertion may sleep
   vlog_node_t    sva_stmt;
   mir_block_t    sva_chk;
   mir_value_t    sva_steady;
   bool           sens_clear;
} vlog_gen_t;

static void vlog_lower_locals(vlog_gen_t *g, vlog_node_t v);
static void vlog_lower_sensitivity(vlog_gen_t *g, vlog_node_t v);
static bool vlog_is_param_select(vlog_node_t v);

typedef enum {
   NOEDGE = 0x0,
   POSEDGE = 0x1,
   NEGEDGE = 0x2,
} vlog_edge_t;

#define PUSH_DEBUG_INFO(mu, v)                                          \
   __attribute__((cleanup(_mir_pop_debug_info), unused))                \
   const mir_saved_loc_t _old_loc =                                     \
      _mir_push_debug_info((mu), vlog_loc((v)));

static void vlog_lower_stmts(vlog_gen_t *g, vlog_node_t v, gen_stack_t *gs);
static bool vlog_lower_one_stmt(vlog_gen_t *g, vlog_node_t s, gen_stack_t *gs);
static bool vlog_is_context_operand(vlog_node_t v)
{
   // An operator whose operands are widened to the size of the destination
   switch (vlog_kind(v)) {
   case V_BINARY:
   case V_UNARY:
   case V_COND_EXPR:
      return true;
   default:
      return false;
   }
}

static mir_value_t vlog_lower_rvalue(vlog_gen_t *g, vlog_node_t v);
static mir_value_t vlog_lower_with_context(vlog_gen_t *g, vlog_node_t v,
                                           mir_type_t context);
static mir_value_t vlog_lower_net_delay(vlog_gen_t *g, vlog_node_t ctrl,
                                        mir_value_t value, unsigned size);
static vlog_node_t vlog_assign_delay(vlog_node_t v);
static vlog_node_t vlog_decl_delay(vlog_node_t v);
static void vlog_lower_deferred(mir_unit_t *mu, object_t *obj);
static void vlog_lower_port_xinit(vlog_gen_t *g, vlog_node_t v);
static void vlog_lower_cleanup(vlog_gen_t *g);
static void vlog_lower_one_local(vlog_gen_t *g, vlog_node_t d, ident_t name);
static inline bool mir_is_vector_type(mir_unit_t *mu, mir_type_t type);
static void vlog_lower_class_decl(mir_unit_t *mu, object_t *obj);
static void vlog_lower_cast_unit(mir_unit_t *mu, object_t *obj);
static void vlog_lower_func_decl(mir_unit_t *mu, object_t *obj);
static void vlog_lower_task_decl(mir_unit_t *mu, object_t *obj);
static void vlog_lower_namespace(mir_unit_t *mu, object_t *obj);
static void vlog_dpi_kind(vlog_gen_t *g, vlog_node_t where, vlog_node_t type,
                          text_buf_t *tb);
static void vlog_lower_dispatch(mir_unit_t *mu, object_t *obj);
static void vlog_lower_crv_dispatch(mir_unit_t *mu, object_t *obj);
static mir_value_t vlog_lower_randomize(vlog_gen_t *g, vlog_node_t v);
static mir_value_t vlog_lower_std_randomize(vlog_gen_t *g, vlog_node_t v);
static mir_value_t vlog_lower_crv_mode(vlog_gen_t *g, vlog_node_t v);
static mir_value_t vlog_crv_to_offset(vlog_gen_t *g, mir_value_t value);
static mir_value_t vlog_crv_from_offset(vlog_gen_t *g, mir_value_t value,
                                        mir_type_t type);
static mir_value_t vlog_crv_hidden(vlog_gen_t *g, mir_value_t obj,
                                   const char *name);
static bool vlog_crv_has_hidden(vlog_node_t class);
static mir_value_t vlog_lower_inside(vlog_gen_t *g, vlog_node_t v);
static mir_value_t vlog_lower_inside_scalar(vlog_gen_t *g, vlog_node_t v,
                                            bool is_real);
static void vlog_inside_context(vlog_gen_t *g, vlog_node_t item,
                                unsigned *width, bool *is_signed);
static mir_value_t vlog_inside_match(vlog_gen_t *g, vlog_node_t item,
                                     mir_value_t value, mir_type_t t_vec);
static void vlog_lower_randcase(vlog_gen_t *g, vlog_node_t v, gen_stack_t *gs);
static void vlog_lower_capture(mir_unit_t *mu, object_t *obj);
static void vlog_lower_fork_process(mir_unit_t *mu, object_t *obj);
static mir_value_t vlog_lower_decl_ptr(vlog_gen_t *g, vlog_node_t decl);
static mir_value_t vlog_crv_guard_null(vlog_gen_t *g, mir_value_t base,
                                          vlog_node_t where);
static bool vlog_is_ref_property(vlog_node_t decl);
static bool vlog_is_const_ref(vlog_node_t port);
static mir_type_t vlog_ref_cell_type(mir_unit_t *mu, const type_info_t *ti);
static mir_value_t vlog_read_ref_cell(vlog_gen_t *g, mir_value_t cell,
                                      const type_info_t *ti);
static mir_value_t vlog_lower_ref_cell(vlog_gen_t *g, vlog_node_t actual,
                                       const type_info_t *ti);
static mir_value_t vlog_link_property(vlog_gen_t *g, mir_value_t base,
                                      vlog_node_t decl, ident_t name,
                                      const type_info_t *ti);
static mir_value_t vlog_lower_dyn_element(vlog_gen_t *g, vlog_node_t v,
                                          int nparams, const type_info_t **out);
static mir_value_t vlog_lower_queue_slice(vlog_gen_t *g, vlog_node_t v);
static mir_value_t vlog_queue_concat(vlog_gen_t *g, vlog_node_t v,
                                     mir_type_t context);
static mir_value_t vlog_assoc_element(vlog_gen_t *g, mir_value_t slot,
                                      vlog_node_t aty, mir_value_t key,
                                      bool for_write, vlog_node_t where);
static mir_value_t vlog_assoc_key_value(vlog_gen_t *g, vlog_node_t aty,
                                        vlog_node_t expr);
static mir_value_t vlog_lower_assoc_checked(vlog_gen_t *g, mir_value_t slot,
                                            vlog_node_t type,
                                            vlog_node_t kexpr, bool for_write,
                                            const type_info_t *eti);
static mir_value_t vlog_lower_method_call(vlog_gen_t *g, vlog_node_t v);
typedef struct {
   vlog_gen_t  *g;
   vlog_node_t  subr;
   mir_value_t *argvars;
   int          nbound;
} bind_defaults_t;

static void vlog_bind_default_cb(vlog_node_t v, void *ctx)
{
   const bind_defaults_t *b = ctx;
   if (vlog_kind(v) != V_REF || !vlog_has_ref(v)
       || vlog_kind(vlog_ref(v)) != V_TF_PORT_DECL)
      return;

   for (int j = 0; j < b->nbound; j++) {
      if (!mir_is_null(b->argvars[j])
          && vlog_ident(vlog_ref(v)) == vlog_ident(vlog_port(b->subr, j)))
         mir_put_object(b->g->mu, vlog_ref(v), b->argvars[j]);
   }
}

static mir_value_t vlog_lower_subr_call(vlog_gen_t *g, vlog_node_t decl,
                                        vlog_node_t call, mir_value_t this_val,
                                        bool nonvirtual);
static mir_value_t vlog_lower_this(vlog_gen_t *g);
static mir_value_t vlog_scope_context(vlog_gen_t *g, ident_t scope);
static mir_value_t vlog_copy_value(vlog_gen_t *g, mir_value_t value,
                                   vlog_node_t type);
static mir_value_t vlog_lower_default_value(vlog_gen_t *g,
                                            const type_info_t *ti);
static void vlog_defer_scope(mir_context_t *mc, ident_t name);
static mir_value_t vlog_empty_array(vlog_gen_t *g, mir_type_t type);
static mir_value_t vlog_wrap_array(vlog_gen_t *g, mir_value_t data,
                                   mir_value_t count);
static mir_value_t vlog_assoc_has_default(vlog_gen_t *g, mir_value_t arr);
static mir_value_t vlog_assoc_extra(vlog_gen_t *g, mir_value_t has_default);
static mir_value_t vlog_assoc_wrap(vlog_gen_t *g, mir_value_t data,
                                   mir_value_t count, mir_value_t has_default);
static mir_value_t vlog_assoc_copy_default(vlog_gen_t *g, mir_value_t has,
                                           mir_value_t dst, mir_value_t src,
                                           mir_value_t len, vlog_node_t etype);
static void vlog_assoc_set_default(vlog_gen_t *g, mir_value_t slot,
                                   mir_value_t value);
static mir_value_t vlog_alloc_array(vlog_gen_t *g, mir_type_t elem,
                                    mir_value_t count);
static void vlog_init_elements(vlog_gen_t *g, mir_value_t data,
                               mir_value_t start, mir_value_t count,
                               const type_info_t *eti);
static mir_value_t vlog_string_const(vlog_gen_t *g, number_t n);
static mir_value_t vlog_string_concat(vlog_gen_t *g, vlog_node_t v);
static mir_value_t vlog_vector_bytes(vlog_gen_t *g, mir_value_t value,
                                     int nbytes);
static mir_value_t vlog_drop_nuls(vlog_gen_t *g, mir_value_t data,
                                  mir_value_t total);
static bool vlog_is_string(vlog_node_t v);

static hash_t *sv_classes = NULL;
static uint32_t vlog_class_id(vlog_node_t c);
static void vlog_lower_event_wait(vlog_gen_t *g, mir_value_t event);
static void vlog_lower_event_trigger(vlog_gen_t *g, mir_value_t event);
static void vlog_lower_queue_wait(vlog_gen_t *g, mir_value_t event);
static mir_value_t vlog_str_arg(vlog_gen_t *g, vlog_node_t v);
static mir_value_t vlog_str_offset_arg(vlog_gen_t *g, vlog_node_t v);
static mir_value_t vlog_str_copy(vlog_gen_t *g, mir_value_t str);
static mir_value_t vlog_str_call(vlog_gen_t *g, const char *name, mir_type_t type,
                                 mir_value_t *args, int nargs);
static void vlog_lower_super_call(vlog_gen_t *g, vlog_node_t v);
static void vlog_lower_fork(vlog_gen_t *g, vlog_node_t v, gen_stack_t *gs);
static void vlog_lower_implicit_super(vlog_gen_t *g, vlog_node_t ctor);
static void vlog_defer_class(mir_context_t *mc, vlog_node_t c, ident_t parent);
static void vlog_lower_class_statics(vlog_gen_t *g, vlog_node_t c);
static vlog_node_t vlog_find_scope(ident_t name);
static hash_t *sv_spec_scopes = NULL;   // Scope of class specialisation
static void vlog_note_spec(vlog_node_t class);
static void vlog_lower_event_or_delay(vlog_gen_t *g, vlog_node_t ctrl);
static mir_value_t vlog_add_local_var(vlog_gen_t *g, vlog_node_t d,
                                      ident_t name);
static void vlog_lower_var_init_process(vlog_gen_t *g, vlog_node_t v);
static void vlog_store_local_default(vlog_gen_t *g, vlog_node_t d,
                                     mir_value_t var);
static void vlog_store_local_init(vlog_gen_t *g, vlog_node_t d, mir_value_t var);
static void vlog_lower_spec_scope(mir_unit_t *mu, object_t *obj);
static vlog_node_t vlog_nettype_of(vlog_node_t decl);
static vlog_node_t vlog_struct_nettype_of(vlog_node_t decl);
static bool vlog_lower_struct_net_driver(vlog_gen_t *g, vlog_node_t v);
static void vlog_lower_struct_net_process(vlog_gen_t *g, vlog_node_t v);
static hash_t *sv_fork_caps = NULL;     // Forked statement to captured decls
static hash_t *sv_fork_up = NULL;       // Forked statement to enclosing fork

static hash_t *sv_iface_bodies = NULL;   // Interface to its first instance

static ident_t vlog_iface_unit(vlog_node_t type)
{
   // A virtual interface has the layout of an instance of the interface
   // with the same parameters, which may not have been elaborated when
   // the reference to it is lowered.

   vlog_node_t spec = vlog_virtual_spec(type);
   if (spec != NULL)
      return vlog_ident(spec);

   vlog_node_t iface = vlog_virtual_iface(type);
   if (iface == NULL)
      fatal_at(vlog_loc(type), "no interface named %pQ", vlog_ident(type));

   return ident_sprintf("%s#0", istr(vlog_ident(iface)));
}

static inline ident_t vlog_subr_name(vlog_node_t d)
{
   return ident_prefix(vlog_ident2(d), vlog_ident(d), '.');
}

static ident_t vlog_static_name(vlog_node_t cls, ident_t name)
{
   // Classes with the same name can be nested in different classes
   ident_t cname = vlog_has_ident2(cls) ? vlog_subr_name(cls) : vlog_ident(cls);
   return ident_prefix(cname, name, '.');
}

static mir_value_t vlog_offset_to_int(vlog_gen_t *g, mir_value_t value)
{
   mir_type_t t_int64 = mir_int_type(g->mu, INT64_MIN, INT64_MAX);
   mir_type_t t_int = mir_vec2_type(g->mu, 32, true);
   return mir_build_cast(g->mu, t_int, mir_build_cast(g->mu, t_int64, value));
}

static int vlog_instance_hops(vlog_gen_t *g)
{
   int hops = 0;
   ident_t unit = mir_get_name(g->mu, MIR_NULL_VALUE);
   ident_t it = unit;
   while (it != NULL && ((sv_classes != NULL && hash_get(sv_classes, it) != NULL)
                         || mir_get_kind(g->mu) != MIR_UNIT_INSTANCE)) {
      if (it != unit && (sv_classes == NULL || hash_get(sv_classes, it) == NULL)
          && ident_runtil(it, '$') == it)
         break;   // Not a class or a fork frame so must be the instance

      it = hops == 0 ? mir_get_parent(g->mu)
         : mir_get_shape_parent(mir_get_shape(g->mir, it));
      hops++;
   }

   // A class shared by all instances is not declared in any of them
   return it == NULL ? -1 : hops;
}

static mir_value_t vlog_bind_hier(vlog_gen_t *g, vlog_node_t v, mir_type_t type,
                                  mir_stamp_t stamp)
{
   // Names are relative to the instance containing the reference which
   // may not be the instance of the running process

   mir_value_t locus = mir_build_debug_locus(g->mu, vlog_to_object(v));

   int hops = 0;
   ident_t unit = mir_get_name(g->mu, MIR_NULL_VALUE);
   ident_t it = unit;
   while (it != NULL && ((sv_classes != NULL && hash_get(sv_classes, it) != NULL)
                         || mir_get_kind(g->mu) != MIR_UNIT_INSTANCE)) {
      if (it != unit && (sv_classes == NULL || hash_get(sv_classes, it) == NULL)
          && ident_runtil(it, '$') == it)
         break;   // Not a class or a fork frame so must be the instance

      it = hops == 0 ? mir_get_parent(g->mu)
         : mir_get_shape_parent(mir_get_shape(g->mir, it));
      hops++;
   }

   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_value_t args[2] = {
      mir_build_context_upref(g->mu, hops),
      mir_const(g->mu, t_offset, g->bind_size)
   };
   return mir_build_bind_external(g->mu, locus, mir_get_parent(g->mu), type,
                                  stamp, args, 2);
}

static mir_type_t vlog_std_logic_type(vlog_gen_t *g)
{
   return mir_int_type(g->mu, 0, 8);
}

static bool vlog_is_vhdl_path(vlog_node_t decl)
{
   return vlog_kind(decl) == V_VAR_DECL && (vlog_flags(decl) & VLOG_F_VHDL_PATH);
}

static vlog_node_t vlog_vhdl_int_bounds(vlog_node_t decl)
{
   // The range of an integer VHDL signal or NULL for any other type

   if (vlog_kind(decl) != V_VAR_DECL || !(vlog_flags(decl) & VLOG_F_VHDL))
      return NULL;

   if (vlog_is_vhdl_path(decl)) {
      vlog_node_t field = vlog_ref(vlog_value(decl));
      return vlog_has_value(field) ? vlog_value(field) : NULL;
   }

   return vlog_has_value(decl) ? vlog_value(decl) : NULL;
}

static bool vlog_is_vhdl_integer(vlog_node_t decl)
{
   return vlog_vhdl_int_bounds(decl) != NULL;
}

static bool vlog_is_vhdl_signal(vlog_gen_t *g, mir_value_t obj)
{
   if (!mir_is_signal(g->mu, obj))
      return false;

   mir_type_t base = mir_get_base(g->mu, mir_get_type(g->mu, obj));
   return mir_equals(base, vlog_std_logic_type(g));
}

static void vlog_convert_std_logic(vlog_gen_t *g, const char *func,
                                   mir_value_t dst, mir_value_t src,
                                   mir_value_t count)
{
   mir_value_t args[] = { dst, src, count };
   mir_value_t res = mir_build_fcall(g->mu, ident_new(func),
                                     mir_offset_type(g->mu), MIR_NULL_STAMP,
                                     args, ARRAY_LEN(args));
   mir_build_consume(g->mu, res);
}

static mir_value_t vlog_lvalue_source(vlog_gen_t *g, const vlog_select_t *lvalue,
                                      mir_value_t src)
{
   // Drivers and deposits on a VHDL signal take STD_LOGIC values

   if (!vlog_is_vhdl_signal(g, lvalue->obj))
      return src;

   if (!mir_is(g->mu, src, MIR_TYPE_POINTER)) {
      mir_value_t spill = mir_add_var(g->mu, mir_logic_type(g->mu),
                                      MIR_NULL_STAMP, ident_uniq("tmp"),
                                      MIR_VAR_TEMP);
      mir_build_store(g->mu, spill, src);
      src = spill;
   }

   mir_type_t t_array = mir_carray_type(g->mu, lvalue->size,
                                        vlog_std_logic_type(g));
   mir_value_t tmp = mir_add_var(g->mu, t_array, MIR_NULL_STAMP,
                                 ident_uniq("tmp"), MIR_VAR_TEMP);

   vlog_convert_std_logic(g, "__sv_logic_to_std", tmp, src, lvalue->count);

   return lvalue->size == 1 ? mir_build_load(g->mu, tmp) : tmp;
}

static ident_t vlog_type_name(vlog_node_t v)
{
   // The same declaration must have the same type in every unit

   const loc_t *loc = vlog_loc(v);
   return ident_sprintf("type$%u_%u_%u", loc->file_ref, loc->first_line,
                        loc->first_column);
}

static void vlog_type_sig(text_buf_t *tb, vlog_node_t type)
{
   // Describe a type so that types with the same shape are the same

   vlog_node_t t = vlog_get_type(type);
   if (vlog_kind(t) != V_DATA_TYPE) {
      const loc_t *loc = vlog_loc(t);
      tb_printf(tb, "u%u_%u_%u", loc->file_ref, loc->first_line,
                loc->first_column);
      return;
   }

   switch (vlog_subkind(t)) {
   case DT_ASSOC:
      {
         vlog_node_t key = vlog_has_ref(t) ? vlog_ref(t) : NULL;
         tb_cat(tb, "a(");
         if (key == NULL)
            tb_cat(tb, "*");
         else
            vlog_type_sig(tb, key);
         tb_cat(tb, ",");
         vlog_type_sig(tb, vlog_type(t));
         tb_cat(tb, ")");
      }
      break;
   case DT_DYNARRAY:
   case DT_QUEUE:
      tb_cat(tb, vlog_subkind(t) == DT_QUEUE ? "q(" : "d(");
      vlog_type_sig(tb, vlog_type(t));
      tb_cat(tb, ")");
      break;
   default:
      tb_printf(tb, "t%d_%u_%d", vlog_subkind(t), vlog_size(t),
                !!(vlog_flags(t) & VLOG_F_SIGNED));
      break;
   }
}

static mir_type_t vlog_flat_array_type(vlog_gen_t *g, vlog_node_t decl,
                                       const type_info_t *ti, unsigned size)
{
   // An array of two-state packed structures stays two-state

   vlog_node_t type = vlog_get_type(decl);
   if (type != NULL && vlog_kind(type) == V_STRUCT_DECL
       && mir_get_class(g->mu, ti->type) == MIR_TYPE_VEC2)
      return mir_vec2_type(g->mu, size, false);

   return mir_vec4_type(g->mu, size, false);
}

static const type_info_t *vlog_type_info(vlog_gen_t *g, vlog_node_t v)
{
   const vlog_kind_t kind = vlog_kind(v);
   if (kind == V_TYPE_DECL || kind == V_ENUM_DECL)
      return vlog_type_info(g, vlog_type(v));

   type_info_t *ti = mir_get_priv(g->mu, v);
   if (ti != NULL)
      return ti;

   ti = mir_malloc(g->mu, sizeof(type_info_t));
   ti->stamp  = MIR_NULL_STAMP;
   ti->type   = MIR_NULL_TYPE;
   ti->signal = MIR_NULL_TYPE;
   ti->size   = 0;
   ti->elemsz = 0;
   ti->node   = v;

   mir_put_priv(g->mu, v, ti);

   switch (kind) {
   case V_DATA_TYPE:
      switch (vlog_subkind(v)) {
      case DT_STRING:
         ti->size = 1;
         ti->type = mir_uarray_type(g->mu, 1, mir_char_type(g->mu));
         return ti;
      case DT_DYNARRAY:
      case DT_QUEUE:
         {
            const type_info_t *elem = vlog_type_info(g, vlog_type(v));
            ti->size = 1;
            ti->type = mir_uarray_type(g->mu, 1, elem->type);
            return ti;
         }
      case DT_ASSOC:
         {
            // Sorted array of key and value pairs
            const type_info_t *elem = vlog_type_info(g, vlog_type(v));
            bool is_string;
            mir_type_t fields[] = {
               vlog_assoc_key_type(g, v, &is_string),
               elem->type,
            };
            LOCAL_TEXT_BUF tb = tb_new();
            tb_cat(tb, "assoc$");
            vlog_type_sig(tb, v);
            mir_type_t t_rec = mir_record_type(g->mu, ident_new(tb_get(tb)),
                                               fields, ARRAY_LEN(fields));
            ti->size = 1;
            ti->type = mir_uarray_type(g->mu, 1, t_rec);
            return ti;
         }
      case DT_PROCESS:
      case DT_CHANDLE:
         {
            // A handle to the state of a process in the simulation kernel
            // or a pointer from foreign code which is opaque here
            mir_type_t t_cell = mir_int_type(g->mu, INT64_MIN, INT64_MAX);
            ti->size = 1;
            ti->type = mir_access_type(g->mu, t_cell);
            return ti;
         }
      case DT_EVENT:
         {
            // An event is the address of a heap cell which identifies
            // the synchronisation queue and is shared by copies of it
            mir_type_t t_cell = mir_int_type(g->mu, INT64_MIN, INT64_MAX);
            ti->size = 1;
            ti->type = mir_access_type(g->mu, t_cell);
            return ti;
         }
      case DT_MAILBOX:
      case DT_SEMAPHORE:
         {
            const type_info_t *elem = vlog_type_info(g, vlog_type(v));
            mir_type_t t_cell = mir_int_type(g->mu, INT64_MIN, INT64_MAX);
            mir_type_t fields[] = {
               mir_uarray_type(g->mu, 1, elem->type),
               mir_access_type(g->mu, t_cell),
               t_cell,   // Bound where zero means none
               t_cell,   // Next ticket of a semaphore
               t_cell,   // Ticket being served
               t_cell,   // Keys wanted by the ticket being served
            };
            mir_type_t t_rec = mir_record_type(g->mu, vlog_type_name(v),
                                               fields, ARRAY_LEN(fields));
            ti->size = 1;
            ti->type = mir_access_type(g->mu, t_rec);
            return ti;
         }
      case DT_VIRTUAL:
         {
            ti->size = 1;
            ti->type = mir_context_type(g->mu, vlog_iface_unit(v));
            return ti;
         }
      default:
         break;
      }
      break;
   case V_STRUCT_DECL:
   case V_UNION_DECL:
      if (!(vlog_flags(v) & VLOG_F_PACKED)) {
         // Unpacked structures are heap allocated and copied on assignment
         const int ndecls = vlog_decls(v);
         mir_type_t *fields LOCAL = xmalloc_array(ndecls, sizeof(mir_type_t));
         for (int i = 0; i < ndecls; i++) {
            vlog_node_t d = vlog_decl(v, i);
            fields[i] = vlog_type_info(g, vlog_type(d))->type;

            // A fixed size array member has room for all the elements
            if (vlog_is_fixed_unpacked(d) && vlog_size(d) > 1)
               fields[i] = mir_carray_type(g->mu, vlog_size(d), fields[i]);
         }

         mir_type_t t_rec = mir_record_type(g->mu, vlog_type_name(v),
                                            fields, ndecls);
         ti->size = 1;
         ti->type = mir_access_type(g->mu, t_rec);
         return ti;
      }
      else {

         mir_type_t t_unpacked_logic = mir_int_type(g->mu, 0, 3);
         mir_type_t t_logic_signal = mir_signal_type(g->mu, t_unpacked_logic);

         const bool issigned = !!(vlog_flags(v) & VLOG_F_SIGNED);

         ti->size     = vlog_size(v);
         ti->type     = vlog_is_two_state(v)
            ? mir_vec2_type(g->mu, ti->size, issigned)
            : mir_vec4_type(g->mu, ti->size, issigned);
         ti->unpacked = t_unpacked_logic;
         ti->signal   = t_logic_signal;
         ti->elemsz   = 1;
         return ti;
      }
   case V_CLASS_DECL:
      {
         ident_t name = ident_prefix(vlog_ident2(v), vlog_ident(v), '.');
         vlog_note_spec(v);
         vlog_defer_scope(g->mir, vlog_ident2(v));
         ti->size = 1;
         ti->type = mir_context_type(g->mu, name);
         return ti;
      }
   default:
      CANNOT_HANDLE(v);
   }

   switch (vlog_subkind(v)) {
   case DT_REAL:
   case DT_SHORTREAL:
   case DT_REALTIME:
      ti->size   = 1;
      ti->type   = ti->unpacked = mir_real_type(g->mu, -DBL_MAX, DBL_MAX);
      ti->signal = mir_signal_type(g->mu, ti->type);
      ti->elemsz = sizeof(double);
      return ti;
   default:
      break;   // Vector type
   }

   mir_type_t t_unpacked_logic = mir_int_type(g->mu, 0, 3);
   mir_type_t t_logic_signal = mir_signal_type(g->mu, t_unpacked_logic);

   ti->unpacked = t_unpacked_logic;
   ti->signal   = t_logic_signal;
   ti->elemsz   = 1;

   const bool issigned = !!(vlog_flags(v) & VLOG_F_SIGNED);

   switch (vlog_subkind(v)) {
   case DT_LOGIC:
   case DT_IMPLICIT:
   case DT_INTEGER:
   case DT_TIME:
      ti->size = vlog_size(v);
      ti->type = mir_vec4_type(g->mu, ti->size, issigned);
      break;
   case DT_BIT:
   case DT_INT:
   case DT_SHORTINT:
   case DT_LONGINT:
   case DT_BYTE:
      ti->size = vlog_size(v);
      ti->type = mir_vec2_type(g->mu, ti->size, issigned);
      break;
   default:
      CANNOT_HANDLE(v);
   }

   return ti;
}

static void vlog_report_error(vlog_gen_t *g, vlog_node_t v, const char *msg);
static void vlog_report_warning(vlog_gen_t *g, vlog_node_t v, const char *msg);

static void vlog_check_tag(vlog_gen_t *g, vlog_node_t v, mir_value_t rec,
                           vlog_node_t type, int field)
{
   // Reading a member other than the one the union is tagged with

   mir_type_t t_offset = mir_offset_type(g->mu);

   const int nmembers = vlog_decls(type) - 1;

   mir_value_t tag = mir_build_cast(
      g->mu, t_offset,
      mir_build_load(g->mu, mir_build_record_ref(g->mu, rec, nmembers)));
   mir_value_t test = mir_build_cmp(g->mu, MIR_CMP_NEQ, tag,
                                    mir_const(g->mu, t_offset, field));

   mir_block_t bad_bb = mir_add_block(g->mu);
   mir_block_t ok_bb = mir_add_block(g->mu);
   mir_build_cond(g->mu, test, bad_bb, ok_bb);

   mir_set_cursor(g->mu, bad_bb, MIR_APPEND);

   char msg[160];
   for (int i = 0; i < nmembers; i++) {
      if (i == field)
         continue;

      mir_block_t hit_bb = mir_add_block(g->mu);
      mir_block_t next_bb = mir_add_block(g->mu);
      mir_build_cond(g->mu, mir_build_cmp(g->mu, MIR_CMP_EQ, tag,
                                          mir_const(g->mu, t_offset, i)),
                     hit_bb, next_bb);

      mir_set_cursor(g->mu, hit_bb, MIR_APPEND);
      checked_sprintf(msg, sizeof(msg), "read of tagged union member '%s' "
                      "while the active member is '%s'",
                      istr(vlog_ident(v)),
                      istr(vlog_ident(vlog_decl(type, i))));
      vlog_report_error(g, v, msg);
      mir_build_jump(g->mu, ok_bb);

      mir_set_cursor(g->mu, next_bb, MIR_APPEND);
   }

   checked_sprintf(msg, sizeof(msg), "read of tagged union member '%s' "
                   "before any member was set", istr(vlog_ident(v)));
   vlog_report_error(g, v, msg);
   mir_build_jump(g->mu, ok_bb);

   mir_set_cursor(g->mu, ok_bb, MIR_APPEND);
}

static mir_value_t vlog_get_temp(vlog_gen_t *g, mir_type_t type)
{
   if (g->temps == NULL)
      g->temps = ihash_new(16);
   else {
      mir_value_t exist = { .bits = (uintptr_t)ihash_get(g->temps, type.bits) };
      if (!mir_is_null(exist))
         return exist;
   }

   mir_value_t temp = mir_add_var(g->mu, type, MIR_NULL_STAMP,
                                  ident_uniq("tmp"), MIR_VAR_TEMP);

   ihash_put(g->temps, type.bits, (void *)(uintptr_t)temp.bits);
   return temp;
}

static gen_stack_t vlog_push_block(gen_stack_t *gs, block_info_t *block)
{
   gen_stack_t this = {
      .block  = block,
      .cscope = gs->cscope,
      .up     = gs,
   };
   return this;
}

static mir_value_t vlog_lower_zero(vlog_gen_t *g, mir_type_t type);

static mir_value_t vlog_lower_x(vlog_gen_t *g, mir_type_t type)
{
   if (mir_get_class(g->mu, type) == MIR_TYPE_VEC2)
      return vlog_lower_zero(g, type);   // Two-state packed structure

   assert(mir_get_class(g->mu, type) == MIR_TYPE_VEC4);

   if (mir_get_size(g->mu, type) <= 64)
      return mir_const_vec(g->mu, type, ~UINT64_C(0), ~UINT64_C(0));
   else {
      const int width = mir_get_size(g->mu, type);
      mir_type_t t_low = mir_vec4_type(g->mu, 64, false);
      mir_value_t low = mir_const_vec(g->mu, t_low, ~UINT64_C(0), ~UINT64_C(0));
      mir_value_t full = mir_build_cast(g->mu, type, low);

      mir_type_t t_offset = mir_offset_type(g->mu);
      for (int i = 64; i < width; i += 64) {
         const int part_size = MIN(width - i, 64);
         const uint64_t mask =
            part_size < 64 ? (UINT64_C(1) << part_size) - 1 : ~UINT64_C(0);
         mir_type_t t_part = mir_vec4_type(g->mu, part_size, false);
         mir_value_t part = mir_const_vec(g->mu, t_part, mask, mask);
         mir_value_t pos = mir_const(g->mu, t_offset, width - i - part_size);
         full = mir_build_insert(g->mu, part, full, pos);
      }

      return full;
   }
}

static mir_value_t vlog_lower_zero(vlog_gen_t *g, mir_type_t type)
{
   if (mir_get_class(g->mu, type) == MIR_TYPE_REAL)
      return mir_const_real(g->mu, type, 0.0);
   else if (mir_get_size(g->mu, type) <= 64)
      return mir_const_vec(g->mu, type, 0, 0);
   else {
      mir_type_t t_logic = mir_vec4_type(g->mu, 1, false);
      mir_value_t zero = mir_const_vec(g->mu, t_logic, 0, 0);
      return mir_build_cast(g->mu, type, zero);
   }
}

static mir_value_t vlog_convert_uarray(vlog_gen_t *g, mir_type_t type,
                                       mir_value_t value);

static mir_value_t vlog_lower_cast(vlog_gen_t *g, mir_type_t type,
                                   mir_value_t value)
{
   const mir_type_t value_type = mir_get_type(g->mu, value);
   const mir_class_t from = mir_get_class(g->mu, value_type);
   const mir_class_t to = mir_get_class(g->mu, type);
   const bool from_vec = from == MIR_TYPE_VEC4 || from == MIR_TYPE_VEC2;
   const bool to_vec = to == MIR_TYPE_VEC4 || to == MIR_TYPE_VEC2;

   if (mir_equals(value_type, type))
      return value;
   else if (to == MIR_TYPE_UARRAY && from == MIR_TYPE_UARRAY) {
      // The elements of a dynamic array or queue differ in being two-state
      // or four-state
      return vlog_convert_uarray(g, type, value);
   }
   else if (to == MIR_TYPE_UARRAY && from_vec) {
      // Integral value converted to a string
      mir_value_t bytes = vlog_vector_bytes(
         g, value, (mir_get_size(g->mu, value_type) + 7) / 8);
      return vlog_drop_nuls(g, mir_build_unwrap(g->mu, bytes),
                            mir_build_uarray_len(g->mu, bytes, 0));
   }

   mir_value_t finite = MIR_NULL_VALUE;
   if (from == MIR_TYPE_REAL && to_vec) {
      // A NaN or infinity gives x for a four-state type and zero for a
      // two-state type with a warning, as 6.12.1 does not define the
      // result and x is the four-state unknown value
      mir_type_t t_int64 = mir_int_type(g->mu, INT64_MIN, INT64_MAX);
      mir_type_t t_offset = mir_offset_type(g->mu);
      mir_value_t args[] = {
         value, mir_build_cast(g->mu, t_int64,
                               mir_const(g->mu, t_offset, to == MIR_TYPE_VEC4)),
      };
      mir_value_t ok = mir_build_fcall(g->mu, ident_new("__sv_real_finite"),
                                       t_int64, MIR_NULL_STAMP, args,
                                       ARRAY_LEN(args));
      mir_build_consume(g->mu, ok);
      if (to == MIR_TYPE_VEC4)
         finite = mir_build_cmp(g->mu, MIR_CMP_NEQ, ok,
                                mir_build_cast(g->mu, t_int64,
                                               mir_const(g->mu, t_offset, 0)));
   }

   if (from == MIR_TYPE_REAL && to_vec && mir_get_size(g->mu, type) > 64) {
      // The rounded value is built from 64-bit words starting from the
      // least significant
      mir_type_t t_int64 = mir_int_type(g->mu, INT64_MIN, INT64_MAX);
      mir_type_t t_offset = mir_offset_type(g->mu);
      const int width = mir_get_size(g->mu, type);

      mir_value_t full = MIR_NULL_VALUE;
      for (int i = 0; i < width; i += 64) {
         mir_value_t args[] = {
            value, mir_build_cast(g->mu, t_int64, mir_const(g->mu, t_offset,
                                                            i / 64)),
         };
         mir_value_t word =
            mir_build_fcall(g->mu, ident_new("__sv_real_to_word"), t_int64,
                            MIR_NULL_STAMP, args, ARRAY_LEN(args));
         mir_build_consume(g->mu, word);

         if (i == 0)
            full = mir_build_cast(g->mu, type, word);
         else {
            const int part_size = MIN(width - i, 64);
            mir_type_t t_part = to == MIR_TYPE_VEC4
               ? mir_vec4_type(g->mu, part_size, false)
               : mir_vec2_type(g->mu, part_size, false);
            mir_value_t part = mir_build_cast(g->mu, t_part, word);
            mir_value_t pos = mir_const(g->mu, t_offset, width - i - part_size);
            full = mir_build_insert(g->mu, part, full, pos);
         }
      }

      if (!mir_is_null(finite))
         return mir_build_select(g->mu, type, finite, full,
                                 vlog_lower_x(g, type));

      return full;
   }
   else if (from == MIR_TYPE_REAL && to_vec) {
      // The value is rounded and then wraps in the width of the result
      mir_type_t t_int64 = mir_int_type(g->mu, INT64_MIN, INT64_MAX);
      mir_value_t intg = mir_build_fcall(g->mu, ident_new("__sv_real_to_int"),
                                         t_int64, MIR_NULL_STAMP, &value, 1);
      mir_build_consume(g->mu, intg);
      mir_value_t result = mir_build_cast(g->mu, type, intg);

      if (!mir_is_null(finite))
         return mir_build_select(g->mu, type, finite, result,
                                 vlog_lower_x(g, type));

      return result;
   }
   else if (to == MIR_TYPE_REAL && from_vec
            && mir_get_size(g->mu, value_type) > 64) {
      // The top 128 bits are converted exactly with the nearest-even
      // rounding of the host and any further words are added in below
      mir_type_t t_int64 = mir_int_type(g->mu, INT64_MIN, INT64_MAX);
      mir_type_t t_offset = mir_offset_type(g->mu);
      const int width = mir_get_size(g->mu, value_type);
      const bool issigned = mir_get_signed(g->mu, value_type);

      mir_type_t t_vec2 = mir_vec2_type(g->mu, width, issigned);
      mir_value_t v2 = mir_build_cast(g->mu, t_vec2, value);

      mir_type_t t_top = mir_vec2_type(g->mu, 128, issigned);
      mir_value_t high = v2;
      if (width > 128)
         high = mir_build_extract(g->mu, t_top, v2,
                                  mir_const(g->mu, t_offset, 0));
      else if (width < 128)
         high = mir_build_cast(g->mu, t_top, v2);

      mir_type_t t_word = mir_vec2_type(g->mu, 64, false);
      mir_value_t hi = vlog_lower_cast(g, t_int64, mir_build_extract(
         g->mu, t_word, high, mir_const(g->mu, t_offset, 0)));
      mir_value_t lo = vlog_lower_cast(g, t_int64, mir_build_extract(
         g->mu, t_word, high, mir_const(g->mu, t_offset, 64)));
      mir_value_t sgn = mir_build_cast(
         g->mu, t_int64, mir_const(g->mu, t_offset, issigned));
      mir_value_t args[] = { hi, lo, sgn };
      mir_value_t real = mir_build_fcall(g->mu, ident_new("__sv_words_to_real"),
                                         type, MIR_NULL_STAMP, args,
                                         ARRAY_LEN(args));
      mir_build_consume(g->mu, real);

      for (int i = 128; i < width; i += 64) {
         const int n = MIN(64, width - i);
         mir_value_t w = mir_build_extract(
            g->mu, mir_vec2_type(g->mu, n, false), v2,
            mir_const(g->mu, t_offset, i));
         mir_value_t wi = vlog_lower_cast(
            g, t_int64, mir_build_cast(g->mu, t_word, w));
         mir_value_t wargs[] = {
            mir_build_cast(g->mu, t_int64, mir_const(g->mu, t_offset, 0)),
            wi, mir_build_cast(g->mu, t_int64, mir_const(g->mu, t_offset, 0))
         };
         mir_value_t wr = mir_build_fcall(
            g->mu, ident_new("__sv_words_to_real"), type, MIR_NULL_STAMP,
            wargs, ARRAY_LEN(wargs));
         mir_build_consume(g->mu, wr);
         real = mir_build_add(g->mu, type, mir_build_mul(
            g->mu, type, real, mir_const_real(g->mu, type, ldexp(1.0, n))),
            wr);
      }

      return real;
   }
   else if (to == MIR_TYPE_REAL && from_vec) {
      mir_type_t t_int64 = mir_int_type(g->mu, INT64_MIN, INT64_MAX);
      mir_value_t intg = vlog_lower_cast(g, t_int64, value);
      mir_value_t real = mir_build_cast(g->mu, type, intg);

      if (mir_get_size(g->mu, value_type) == 64
          && !mir_get_signed(g->mu, value_type)) {
         // The top bit is part of the value rather than the sign
         mir_value_t zero = mir_build_cast(g->mu, t_int64, mir_const(
            g->mu, mir_offset_type(g->mu), 0));
         mir_value_t wrap = mir_build_add(
            g->mu, type, real, mir_const_real(g->mu, type,
                                              18446744073709551616.0));
         return mir_build_select(g->mu, type,
                                 mir_build_cmp(g->mu, MIR_CMP_LT, intg, zero),
                                 wrap, real);
      }

      return real;
   }
   else if (from == MIR_TYPE_VEC4 && (to == MIR_TYPE_INT
                                      || to == MIR_TYPE_OFFSET)) {
      // TODO: how to handle X here
      const int size = mir_get_size(g->mu, value_type);
      const bool issigned = mir_get_signed(g->mu, value_type);
      mir_type_t t_vec2 = mir_vec2_type(g->mu, size, issigned);
      mir_value_t vec2 = mir_build_cast(g->mu, t_vec2, value);
      return mir_build_cast(g->mu, type, vec2);
   }
   else
      return mir_build_cast(g->mu, type, value);
}

static mir_value_t vlog_lower_test(vlog_gen_t *g, mir_value_t value)
{
   if (mir_is_vector(g->mu, value))
      return mir_build_test(g->mu, value);

   mir_type_t type = mir_get_type(g->mu, value);
   const mir_class_t class = mir_get_class(g->mu, type);
   if (mir_equals(type, mir_bool_type(g->mu)))
      return value;
   else if (class == MIR_TYPE_ACCESS || class == MIR_TYPE_POINTER
            || class == MIR_TYPE_CONTEXT) {
      mir_value_t null = mir_build_null(g->mu, type);
      return mir_build_not(g->mu,
                           mir_build_cmp(g->mu, MIR_CMP_EQ, value, null));
   }
   else {
      mir_value_t zero = vlog_lower_zero(g, type);
      mir_value_t cmp = mir_build_cmp(g->mu, MIR_CMP_EQ, value, zero);
      return mir_build_not(g->mu, cmp);
   }
}

static vlog_node_t vlog_sized_dimension(vlog_node_t type)
{
   // The bounds of an array declared with a fixed size in a typedef

   if (type != NULL && vlog_kind(type) == V_DATA_TYPE
       && vlog_subkind(type) == DT_DYNARRAY && vlog_ranges(type) == 1) {
      vlog_node_t dim = vlog_range(type, 0);
      if (vlog_kind(dim) == V_DIMENSION && vlog_subkind(dim) != V_DIM_UNSIZED)
         return dim;
   }

   return NULL;
}

static mir_value_t vlog_lower_array_off(vlog_gen_t *g, vlog_node_t r,
                                        mir_value_t index)
{
   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_value_t cast = vlog_lower_cast(g, t_offset, index);

   assert(vlog_kind(r) == V_DIMENSION);

   if (vlog_subkind(r) == V_DIM_UNSIZED)
      return cast;  // Dynamic arrays are always zero based
   else {
      int64_t lconst, rconst;
      vlog_bounds(r, &lconst, &rconst);

      mir_value_t left = mir_const(g->mu, t_offset, lconst);

      if (lconst < rconst)
         return mir_build_sub(g->mu, t_offset, cast, left);
      else
         return mir_build_sub(g->mu, t_offset, left, cast);
   }
}

static mir_value_t vlog_lower_struct_off(vlog_gen_t *g, unsigned size,
                                         mir_value_t index)
{
   // Offset of a bit of a packed structure declared [size-1:0]
   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_value_t cast = vlog_lower_cast(g, t_offset, index);
   mir_value_t left = mir_const(g->mu, t_offset, (int64_t)size - 1);
   return mir_build_sub(g->mu, t_offset, left, cast);
}

static mir_type_t vlog_vhdl_signal_type(vlog_gen_t *g, vlog_node_t field)
{
   if (vlog_has_value(field)) {
      vlog_node_t bounds = vlog_value(field);
      int64_t low, high;
      if (!vlog_get_const(vlog_left(bounds), &low)
          || !vlog_get_const(vlog_right(bounds), &high))
         should_not_reach_here();

      return mir_signal_type(g->mu, mir_int_type(g->mu, low, high));
   }

   return mir_signal_type(g->mu, vlog_std_logic_type(g));
}

static mir_type_t vlog_vhdl_record_type(vlog_gen_t *g, vlog_node_t st)
{
   // The record of signals which the VHDL signal is stored in

   const int nfields = vlog_decls(st);
   mir_type_t *fields LOCAL = xmalloc_array(nfields, sizeof(mir_type_t));

   for (int i = 0; i < nfields; i++) {
      vlog_node_t f = vlog_decl(st, i);
      vlog_node_t type = vlog_type(f);
      if (vlog_kind(type) == V_STRUCT_DECL) {
         fields[i] = vlog_vhdl_record_type(g, type);
         if (vlog_ranges(f) > 0)
            fields[i] = mir_carray_type(g->mu, vlog_size(vlog_range(f, 0)),
                                        fields[i]);
      }
      else
         fields[i] = vlog_vhdl_signal_type(g, f);
   }

   return mir_record_type(g->mu, vlog_ident(st), fields, nfields);
}

static mir_value_t vlog_lower_vhdl_path(vlog_gen_t *g, vlog_node_t v,
                                        mir_value_t *in_range)
{
   // Select the signal for an element of a VHDL record signal or array
   // of records reached by a hierarchical reference

   vlog_node_t decl = vlog_ref(v);
   vlog_node_t root = vlog_ref(decl);

   mir_type_t t_root = vlog_vhdl_record_type(g, vlog_type(root));
   if (vlog_ranges(root) > 0)
      t_root = mir_carray_type(g->mu, vlog_size(vlog_range(root, 0)), t_root);

   mir_value_t cur = vlog_bind_hier(g, v, t_root, MIR_NULL_STAMP);

   int nsteps = 0;
   vlog_node_t steps[16];
   for (vlog_node_t it = vlog_value(decl); ; it = vlog_value(it)) {
      assert(nsteps < ARRAY_LEN(steps));
      steps[nsteps++] = it;
      if (!vlog_has_value(it))
         break;
   }

   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_value_t zero = mir_const(g->mu, t_offset, 0);

   *in_range = MIR_NULL_VALUE;

   vlog_node_t parent = NULL;
   for (int i = nsteps - 1; i >= 0; i--) {
      vlog_node_t step = steps[i], field = vlog_ref(step);

      if (parent != NULL) {
         int pos = 0;
         for (; vlog_decl(parent, pos) != field; pos++);
         cur = mir_build_record_ref(g->mu, cur, pos);
      }

      if (vlog_params(step) > 0) {
         vlog_node_t dim = vlog_range(field, 0);
         mir_value_t index = vlog_lower_rvalue(g, vlog_param(step, 0));
         mir_value_t off = vlog_lower_array_off(g, dim, index);
         mir_value_t known = mir_build_defined(g->mu, index);

         // An index outside the bounds of a VHDL array is a fatal error
         // but an unknown index reads as unknown
         mir_value_t count = mir_const(g->mu, t_offset, vlog_size(dim));
         mir_value_t cmp_low = mir_build_cmp(g->mu, MIR_CMP_GEQ, off, zero);
         mir_value_t cmp_high = mir_build_cmp(g->mu, MIR_CMP_LT, off, count);
         mir_value_t inside = mir_build_and(g->mu, cmp_low, cmp_high);
         mir_value_t pass = mir_build_or(g->mu, inside,
                                         mir_build_not(g->mu, known));

         static const char msg[] = "index of VHDL array is out of range";
         mir_type_t t_severity = mir_int_type(g->mu, 0, 3);
         mir_value_t buf = mir_const_string(g->mu, msg);
         mir_build_assert(g->mu, pass, mir_build_address_of(g->mu, buf),
                          mir_const(g->mu, t_offset, sizeof(msg) - 1),
                          mir_const(g->mu, t_severity, SEVERITY_FAILURE),
                          mir_build_debug_locus(
                             g->mu, vlog_to_object(vlog_param(step, 0))),
                          MIR_NULL_VALUE, MIR_NULL_VALUE);

         *in_range = mir_is_null(*in_range) ? known
            : mir_build_and(g->mu, *in_range, known);

         off = mir_build_select(g->mu, t_offset, known, off, zero);
         cur = mir_build_array_ref(g->mu, cur, off);
      }

      parent = vlog_type(field);
   }

   return mir_build_load(g->mu, cur);
}

static mir_value_t vlog_lower_vhdl_integer_nets(vlog_gen_t *g, vlog_node_t v,
                                               mir_value_t *in_range)
{
   vlog_node_t decl = vlog_ref(v);
   if (vlog_is_vhdl_path(decl))
      return vlog_lower_vhdl_path(g, v, in_range);

   vlog_node_t bounds = vlog_vhdl_int_bounds(decl);
   int64_t low, high;
   if (!vlog_get_const(vlog_left(bounds), &low)
       || !vlog_get_const(vlog_right(bounds), &high))
      should_not_reach_here();

   mir_type_t t_int = mir_int_type(g->mu, low, high);
   mir_value_t ptr = vlog_bind_hier(g, v, mir_signal_type(g->mu, t_int),
                                    MIR_NULL_STAMP);

   *in_range = MIR_NULL_VALUE;
   return mir_build_load(g->mu, ptr);
}

static mir_value_t vlog_lower_vhdl_integer(vlog_gen_t *g, vlog_node_t v)
{
   // The value of an integer VHDL signal reached by a hierarchical
   // reference

   mir_type_t t_result = mir_vec2_type(g->mu, 32, true);

   mir_value_t in_range;
   mir_value_t nets = vlog_lower_vhdl_integer_nets(g, v, &in_range);
   mir_value_t value = mir_build_load(g->mu, mir_build_resolved(g->mu, nets));
   mir_value_t result = mir_build_cast(g->mu, t_result, value);

   if (!mir_is_null(in_range))
      result = mir_build_select(g->mu, t_result, in_range, result,
                                mir_const_vec(g->mu, t_result, 0, 0));

   return result;
}

static mir_value_t vlog_lower_offset_min(vlog_gen_t *g, mir_value_t left,
                                         mir_value_t right)
{
   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_value_t cmp = mir_build_cmp(g->mu, MIR_CMP_LT, left, right);
   return mir_build_select(g->mu, t_offset, cmp, left, right);
}

static mir_value_t vlog_lower_offset_max(vlog_gen_t *g, mir_value_t left,
                                         mir_value_t right)
{
   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_value_t cmp = mir_build_cmp(g->mu, MIR_CMP_GT, left, right);
   return mir_build_select(g->mu, t_offset, cmp, left, right);
}

static bool vlog_is_fill_literal(vlog_node_t v);

static vlog_select_t vlog_lower_param_value(vlog_gen_t *g, vlog_node_t decl)
{
   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_value_t zero = mir_const(g->mu, t_offset, 0);

   const type_info_t *ti = vlog_type_info(g, vlog_type(decl));

   mir_value_t value;
   if (vlog_kind(vlog_value(decl)) == V_DOLLAR) {
      // An unbounded parameter has all bits set
      mir_type_t t_all = mir_vec4_type(g->mu, 32, true);
      value = mir_const_vec(g->mu, t_all, UINT32_MAX, 0);
   }
   else if (vlog_is_fill_literal(vlog_value(decl))
       && !is_implicit_data_type(vlog_type(decl)))
      value = vlog_lower_with_context(g, vlog_value(decl), ti->type);
   else
      value = vlog_lower_rvalue(g, vlog_value(decl));

   if (vlog_is_dynamic_type(vlog_type(decl))) {
      // Spill so that elements can be selected from the value
      mir_type_t type = mir_get_type(g->mu, value);
      mir_value_t tmp = mir_add_var(g->mu, type, MIR_NULL_STAMP,
                                    ident_uniq("spill"), 0);
      mir_build_store(g->mu, tmp, value);

      vlog_select_t result = {
         .obj        = tmp,
         .type       = type,
         .dst_offset = zero,
         .count      = mir_const(g->mu, t_offset, 1),
         .src_offset = zero,
         .size       = 1,
      };
      return result;
   }

   // A parameter with no type takes the type of its value
   vlog_node_t dt = vlog_type(decl);
   if (is_implicit_data_type(dt) && vlog_ranges(dt) == 0
       && mir_is_vector(g->mu, value)) {
      mir_type_t vtype = mir_get_type(g->mu, value);
      const int size = mir_get_size(g->mu, vtype);

      vlog_select_t result = {
         .obj        = value,
         .type       = vtype,
         .dst_offset = zero,
         .count      = mir_const(g->mu, t_offset, size),
         .src_offset = zero,
         .size       = size,
      };
      return result;
   }

   mir_value_t cast = vlog_lower_cast(g, ti->type, value);

   vlog_select_t result = {
      .obj        = cast,
      .type       = ti->type,
      .dst_offset = zero,
      .count      = mir_const(g->mu, t_offset, ti->size),
      .src_offset = zero,
      .size       = ti->size,
   };
   return result;
}

static vlog_node_t vlog_virtual_param(vlog_node_t v)
{
   // A parameter selected through a virtual interface has the value it
   // has in the instance the interface type was specialised for

   if (vlog_kind(v) != V_MEMBER_REF || !vlog_has_ref(v))
      return NULL;

   vlog_node_t decl = vlog_ref(v);
   if (vlog_kind(decl) != V_PARAM_DECL && vlog_kind(decl) != V_LOCALPARAM)
      return NULL;

   vlog_node_t ptype = vlog_get_type(vlog_value(v));
   if (ptype == NULL || vlog_kind(ptype) != V_DATA_TYPE
       || vlog_subkind(ptype) != DT_VIRTUAL)
      return NULL;

   vlog_node_t body = vlog_virtual_spec(ptype);
   if (body == NULL && sv_iface_bodies != NULL)
      body = hash_get(sv_iface_bodies, vlog_ident2(vlog_virtual_iface(ptype)));

   if (body == NULL)
      fatal_at(vlog_loc(v), "virtual interface %pQ has no instance in the "
               "design", vlog_ident(ptype));

   const int ndecls = vlog_decls(body);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(body, i);
      if ((vlog_kind(d) == V_PARAM_DECL || vlog_kind(d) == V_LOCALPARAM)
          && vlog_ident(d) == vlog_ident(decl))
         return d;
   }

   return decl;
}

static int vlog_dyn_depth(vlog_node_t value, vlog_node_t *elem)
{
   // Number of indices which select an element of an unpacked array of
   // dynamic arrays or queues and then of the dynamic array or queue itself

   int depth = 0;
   if (vlog_kind(value) == V_REF && vlog_has_ref(value)) {
      vlog_node_t decl = vlog_ref(value);
      switch (vlog_kind(decl)) {
      case V_VAR_DECL:
      case V_LOCAL_DECL:
         depth = vlog_ranges(decl);
         break;
      default:
         break;
      }
   }

   vlog_node_t t = vlog_get_type(value);
   while (t != NULL && vlog_kind(t) == V_DATA_TYPE
          && (vlog_subkind(t) == DT_DYNARRAY || vlog_subkind(t) == DT_QUEUE
              || vlog_subkind(t) == DT_ASSOC)) {
      depth++;
      t = vlog_get_type(vlog_type(t));
   }

   *elem = t;
   return depth;
}

static mir_type_t vlog_sel_vec_type(vlog_gen_t *g, mir_type_t prefix,
                                   unsigned size)
{
   // Bits selected from a two-state value are two-state

   if (mir_get_class(g->mu, prefix) == MIR_TYPE_VEC2)
      return mir_vec2_type(g->mu, size, false);

   return mir_vec4_type(g->mu, size, false);
}

static vlog_select_t vlog_lower_select(vlog_gen_t *g, vlog_node_t v)
{
   PUSH_DEBUG_INFO(g->mu, v);

   if (vlog_kind(v) == V_MEMBER_REF) {
      vlog_node_t param = vlog_virtual_param(v);
      if (param != NULL) {
         if (vlog_ranges(param) > 0)
            fatal_at(vlog_loc(v), "sorry, an unpacked array parameter of a "
                     "virtual interface is not supported");

         return vlog_lower_param_value(g, param);
      }
   }

   switch (vlog_kind(v)) {
   case V_REF:
      {
         mir_type_t t_offset = mir_offset_type(g->mu);
         mir_value_t zero = mir_const(g->mu, t_offset, 0);

         vlog_node_t decl = vlog_ref(v);
         vlog_node_t array_decl = decl;   // Port declaration has the ranges
         switch (vlog_kind(decl)) {
         case V_PORT_DECL:
            decl = vlog_ref(decl);
            break;
         case V_PARAM_DECL:
         case V_LOCALPARAM:
            if (vlog_ranges(decl) > 0)
               break;   // Unpacked array is initialised on each use
            // Fall-through
         case V_ENUM_NAME:
            return vlog_lower_param_value(g, decl);
         default:
            break;
         }

         mir_value_t ptr = vlog_lower_decl_ptr(g, decl);

         mir_value_t nets = ptr;
         if (mir_points_to(g->mu, ptr, MIR_TYPE_SIGNAL))
            nets = mir_build_load(g->mu, ptr);

         const type_info_t *ti = vlog_type_info(g, vlog_type(decl));

         // A whole unpacked array in a signal is a flat vector
         unsigned size = ti->size;
         mir_type_t type = ti->type;
         if (vlog_is_fixed_unpacked(array_decl)
             && !mir_points_to_vector(g->mu, nets)
             && (mir_get_class(g->mu, ti->type) == MIR_TYPE_VEC2
                 || mir_get_class(g->mu, ti->type) == MIR_TYPE_VEC4)) {
            size *= vlog_size(array_decl);
            type = vlog_flat_array_type(g, array_decl, ti, size);
         }
         else if (vlog_is_fixed_unpacked(array_decl)
                  && !mir_points_to_vector(g->mu, nets)
                  && mir_get_class(g->mu, ti->type) == MIR_TYPE_REAL)
            size *= vlog_size(array_decl);

         vlog_select_t result = {
            .obj        = nets,
            .type       = type,
            .dst_offset = zero,
            .count      = mir_const(g->mu, t_offset, size),
            .src_offset = zero,
            .size       = size,
            .two_state  = mir_get_class(g->mu, ti->type) == MIR_TYPE_VEC2,
         };
         return result;
      }

   case V_HIER_REF:
      {
         if (g->in_sva) {
            int hops = 0;
            mir_value_t var = mir_search_object(g->mu, v, &hops);
            if (!mir_is_null(var) && hops == 0) {
               mir_type_t t_offset = mir_offset_type(g->mu);
               mir_value_t zero = mir_const(g->mu, t_offset, 0);
               mir_type_t type = mir_get_type(g->mu, mir_build_load(g->mu, var));
               const unsigned size = mir_get_size(g->mu, type);

               vlog_select_t result = {
                  .obj        = var,
                  .type       = type,
                  .dst_offset = zero,
                  .count      = mir_const(g->mu, t_offset, size),
                  .src_offset = zero,
                  .size       = size,
               };
               return result;
            }
         }

         if (!vlog_has_ref(v) && g->unbound != NULL) {
            // The name is bound when the instance is created and has the
            // type of the port it is connected to
            const type_info_t *ti = g->unbound;
            mir_value_t ptr = vlog_bind_hier(g, v, ti->signal, ti->stamp);

            mir_type_t t_offset = mir_offset_type(g->mu);
            mir_value_t zero = mir_const(g->mu, t_offset, 0);

            vlog_select_t result = {
               .obj        = mir_build_load(g->mu, ptr),
               .type       = ti->type,
               .dst_offset = zero,
               .count      = mir_const(g->mu, t_offset, ti->size),
               .src_offset = zero,
               .size       = ti->size,
            };
            return result;
         }
         else if (!vlog_has_ref(v))
            fatal_at(vlog_loc(v), "unbound hierarchical reference");

         vlog_node_t decl = vlog_ref(v);
         if (vlog_is_vhdl_integer(decl))
            fatal_at(vlog_loc(v), "sorry, an integer VHDL signal can only be "
                     "read as a whole in a hierarchical reference");

         const type_info_t *ti = vlog_type_info(g, vlog_type(decl));

         if (vlog_is_vhdl_path(decl)) {
            mir_type_t t_offset = mir_offset_type(g->mu);
            mir_value_t zero = mir_const(g->mu, t_offset, 0);

            mir_value_t in_range;
            mir_value_t nets = vlog_lower_vhdl_path(g, v, &in_range);
            mir_value_t count = mir_const(g->mu, t_offset, ti->size);
            if (!mir_is_null(in_range))
               count = mir_build_select(g->mu, t_offset, in_range, count, zero);

            vlog_select_t result = {
               .obj        = nets,
               .type       = ti->type,
               .dst_offset = zero,
               .count      = count,
               .src_offset = zero,
               .size       = ti->size,
            };
            return result;
         }

         const bool dynamic = vlog_is_dynamic_type(vlog_type(decl));

         mir_value_t ptr;
         if (vlog_kind(decl) == V_VAR_DECL && (vlog_flags(decl) & VLOG_F_VHDL)) {
            mir_type_t t_std = vlog_std_logic_type(g);
            ptr = vlog_bind_hier(g, v, mir_signal_type(g->mu, t_std),
                                 MIR_NULL_STAMP);
         }
         else
            ptr = vlog_bind_hier(g, v, dynamic ? ti->type : ti->signal,
                                 ti->stamp);

         mir_type_t t_offset = mir_offset_type(g->mu);
         mir_value_t zero = mir_const(g->mu, t_offset, 0);

         // A whole unpacked array in a signal is a flat vector
         unsigned size = ti->size;
         mir_type_t type = ti->type;
         if (!dynamic && vlog_is_fixed_unpacked(decl)
             && vlog_kind(decl) != V_PORT_DECL
             && (mir_get_class(g->mu, ti->type) == MIR_TYPE_VEC2
                 || mir_get_class(g->mu, ti->type) == MIR_TYPE_VEC4)) {
            size *= vlog_size(decl);
            type = vlog_flat_array_type(g, decl, ti, size);
         }
         else if (!dynamic && vlog_is_fixed_unpacked(decl)
                  && vlog_kind(decl) != V_PORT_DECL
                  && mir_get_class(g->mu, ti->type) == MIR_TYPE_REAL)
            size *= vlog_size(decl);

         vlog_select_t result = {
            .obj        = dynamic ? ptr : mir_build_load(g->mu, ptr),
            .type       = type,
            .dst_offset = zero,
            .count      = mir_const(g->mu, t_offset, size),
            .src_offset = zero,
            .size       = size,
         };
         return result;
      }
   case V_THIS:
   case V_METHOD_CALL:
   case V_USER_FCALL:
   case V_CAST:
   case V_SYS_FCALL:
   case V_BINARY:
   case V_UNARY:
   case V_COND_EXPR:
      {
         // Value with no storage: spill so it can be selected from
         mir_type_t t_offset = mir_offset_type(g->mu);
         mir_value_t zero = mir_const(g->mu, t_offset, 0);

         mir_value_t value;
         const vlog_kind_t vkind = vlog_kind(v);
         if (vkind == V_BINARY || vkind == V_UNARY || vkind == V_COND_EXPR) {
            // Evaluated in the width of the expression itself
            mir_type_t t_self = mir_vec4_type(g->mu, vlog_width(v),
                                              vlog_is_signed(v));
            value = vlog_lower_cast(g, t_self,
                                    vlog_lower_with_context(g, v, t_self));
         }
         else
            value = vlog_lower_rvalue(g, v);

         mir_type_t type = mir_get_type(g->mu, value);
         mir_value_t tmp = mir_add_var(g->mu, type, MIR_NULL_STAMP,
                                       ident_uniq("spill"), 0);
         mir_build_store(g->mu, tmp, value);

         const bool vec = mir_is_vector(g->mu, value);
         const unsigned size = vec ? mir_get_size(g->mu, type) : 1;

         vlog_select_t result = {
            .obj        = tmp,
            .type       = type,
            .dst_offset = zero,
            .count      = mir_const(g->mu, t_offset, size),
            .src_offset = zero,
            .size       = size,
         };
         return result;
      }
   case V_BIT_SELECT:
      {
         vlog_node_t value = vlog_value(v);

         vlog_node_t vtype = vlog_get_type(value);
         if (vtype != NULL && vlog_kind(vtype) == V_DATA_TYPE
             && (vlog_subkind(vtype) == DT_DYNARRAY
                 || vlog_subkind(vtype) == DT_QUEUE
                 || vlog_subkind(vtype) == DT_ASSOC)) {
            mir_type_t t_offset = mir_offset_type(g->mu);
            mir_value_t zero = mir_const(g->mu, t_offset, 0);

            vlog_node_t etype = NULL;
            const int depth = vlog_dyn_depth(value, &etype);
            const int nparams = vlog_params(v);
            const int nelem = MIN(depth, nparams);

            const type_info_t *ti = NULL;
            mir_value_t ptr = vlog_lower_dyn_element(g, v, nelem, &ti);

            mir_value_t count = mir_const(g->mu, t_offset, ti->size);
            mir_value_t bit_off = zero;
            unsigned size = ti->size;

            // The remaining indices select bits of the element
            for (int i = nelem; i < nparams; i++) {
               if (etype == NULL || vlog_kind(etype) != V_DATA_TYPE
                   || vlog_ranges(etype) <= i - nelem)
                  fatal_at(vlog_loc(v), "sorry, selecting from this element "
                           "of a dynamic array, queue or associative array is "
                           "not supported");

               vlog_node_t dim = vlog_range(etype, i - nelem);
               const unsigned dim_size = vlog_size(dim);
               assert(size % dim_size == 0);
               size /= dim_size;

               mir_value_t index = vlog_lower_rvalue(g, vlog_param(v, i));
               mir_value_t off = vlog_lower_array_off(g, dim, index);
               mir_value_t known = mir_build_defined(g->mu, index);
               mir_value_t in_range = mir_build_and(
                  g->mu, mir_build_cmp(g->mu, MIR_CMP_GEQ, off, zero),
                  mir_build_cmp(g->mu, MIR_CMP_LT, off,
                                mir_const(g->mu, t_offset, dim_size)));
               in_range = mir_build_and(g->mu, known, in_range);

               if (size != 1)
                  off = mir_build_mul(g->mu, t_offset, off,
                                      mir_const(g->mu, t_offset, size));

               bit_off = mir_build_add(g->mu, t_offset, bit_off, off);
               count = mir_build_select(g->mu, t_offset, in_range,
                                        mir_const(g->mu, t_offset, size),
                                        zero);
            }

            vlog_select_t result = {
               .obj        = ptr,
               .type       = nelem < nparams
                             ? mir_vec4_type(g->mu, size, false) : ti->type,
               .dst_offset = bit_off,
               .count      = count,
               .src_offset = zero,
               .size       = size,
            };
            return result;
         }

         assert(vlog_kind(value) == V_REF || vlog_kind(value) == V_MEMBER_REF
                || vlog_kind(value) == V_HIER_REF
                || vlog_kind(value) == V_USER_FCALL
                || vlog_kind(value) == V_METHOD_CALL);

         vlog_select_t prefix = vlog_lower_select(g, value);

         vlog_node_t decl = vlog_kind(value) == V_MEMBER_REF
            ? vlog_member_decl(value) : vlog_ref(value);
         // Only a variable in memory has its unpacked dimensions separate
         const int nunpacked = vlog_kind(decl) == V_LOCAL_DECL
            || vlog_kind(decl) == V_VAR_DECL
            || vlog_kind(decl) == V_TF_PORT_DECL
            || vlog_kind(decl) == V_PARAM_DECL
            || vlog_kind(decl) == V_LOCALPARAM ? vlog_ranges(decl) : 0;
         const int nparams = vlog_params(v);

         // Elements of real type are held in memory as is
         const bool real_mem =
            mir_get_class(g->mu, prefix.type) == MIR_TYPE_REAL
            && mir_is(g->mu, prefix.obj, MIR_TYPE_POINTER)
            && !mir_is_signal(g->mu, prefix.obj);

         const bool unpacked_var =
            nunpacked > 0 && (mir_points_to_vector(g->mu, prefix.obj)
                              || real_mem);

         mir_type_t t_offset = mir_offset_type(g->mu);
         mir_value_t zero = mir_const(g->mu, t_offset, 0);
         mir_value_t array_off = zero, bit_off = prefix.dst_offset;

         mir_value_t in_range =
            mir_build_cmp(g->mu, MIR_CMP_NEQ, prefix.count, zero);

         unsigned elems = vlog_size(decl);
         unsigned size = unpacked_var
            ? (real_mem ? 1 : mir_get_size(g->mu, prefix.type))
            : vlog_is_fixed_unpacked(decl) ? prefix.size
            : elems * mir_get_size(g->mu, prefix.type);

         for (int i = 0; i < nparams; i++) {
            vlog_node_t dim = vlog_get_dim(value, i);

            const unsigned dim_size = dim != NULL ? vlog_size(dim) : size;
            const bool unpacked_dim =
               dim != NULL && unpacked_var
               && vlog_subkind(dim) == V_DIM_UNPACKED;

            if (unpacked_dim) {
               assert(elems % dim_size == 0);
               elems /= dim_size;
            }
            else {
               assert(size % dim_size == 0);
               size /= dim_size;
            }

            mir_value_t index = vlog_lower_rvalue(g, vlog_param(v, i));
            mir_value_t this_off = dim != NULL
               ? vlog_lower_array_off(g, dim, index)
               : vlog_lower_struct_off(g, dim_size, index);
            mir_value_t known = mir_build_defined(g->mu, index);

            mir_value_t count = mir_const(g->mu, t_offset, dim_size);
            mir_value_t cmp_low =
               mir_build_cmp(g->mu, MIR_CMP_GEQ, this_off, zero);
            mir_value_t cmp_high =
               mir_build_cmp(g->mu, MIR_CMP_LT, this_off, count);
            mir_value_t this_in_range = mir_build_and(g->mu, cmp_low, cmp_high);
            this_in_range = mir_build_and(g->mu, known, this_in_range);

            const unsigned scale_by = unpacked_dim ? elems : size;
            if (scale_by != 1) {
               mir_value_t scale = mir_const(g->mu, t_offset, scale_by);
               this_off = mir_build_mul(g->mu, t_offset, this_off, scale);
            }

            in_range = mir_build_and(g->mu, in_range, this_in_range);
            if (unpacked_dim)
               array_off = mir_build_add(g->mu, t_offset, array_off, this_off);
            else
               bit_off = mir_build_add(g->mu, t_offset, bit_off, this_off);
         }

         mir_value_t count = mir_build_select(g->mu, t_offset, in_range,
                                              mir_const(g->mu, t_offset, size),
                                              zero);

         mir_value_t obj = prefix.obj;
         if (unpacked_var)
            obj = mir_build_array_ref(g->mu, prefix.obj, array_off);

         const bool is_real = mir_get_class(g->mu, prefix.type)
            == MIR_TYPE_REAL;

         vlog_select_t result = {
            .obj        = obj,
            .dst_offset = bit_off,
            .count      = count,
            .src_offset = mir_const(g->mu, t_offset, 0),
            .type       = is_real ? prefix.type
                          : vlog_sel_vec_type(g, prefix.type, size),
            .size       = size,
            .two_state  = prefix.two_state
               || mir_get_class(g->mu, prefix.type) == MIR_TYPE_VEC2,
         };
         return result;
      }
   case V_PART_SELECT:
      {
         vlog_node_t value = vlog_value(v);
         vlog_select_t prefix = vlog_lower_select(g, value);

         const vlog_range_kind_t kind = vlog_subkind(v);

         // An expression has no declaration so is selected from as
         // though declared [size-1:0]
         const vlog_kind_t vkind = vlog_kind(value);
         const bool anonymous = vkind == V_BINARY || vkind == V_UNARY
            || vkind == V_COND_EXPR;

         vlog_node_t dim = NULL;
         mir_value_t base, index = MIR_NULL_VALUE;
         if (anonymous) {
            int64_t left;
            if (kind != V_RANGE_CONST || !vlog_get_const(vlog_left(v), &left))
               fatal_at(vlog_loc(v), "sorry, only a constant part select "
                        "can be taken from an expression");

            base = mir_const(g->mu, mir_offset_type(g->mu),
                             (int64_t)prefix.size - 1 - left);
         }
         else {
            vlog_node_t ptype = vlog_get_type(value);
            if (ptype != NULL && vlog_kind(ptype) == V_DATA_TYPE
                && (vlog_subkind(ptype) == DT_DYNARRAY
                    || vlog_subkind(ptype) == DT_QUEUE))
               fatal_at(vlog_loc(v), "sorry, a slice of a dynamic array or "
                        "queue can only be read");

            dim = vlog_get_dim(value, 0);

            index = vlog_lower_rvalue(g, vlog_left(v));
            if (dim != NULL)
               base = vlog_lower_array_off(g, dim, index);
            else
               base = vlog_lower_struct_off(g, prefix.size, index);
         }

         mir_value_t off = base;
         const unsigned nelems = vlog_size(v);
         if (kind != V_RANGE_CONST) {
            const bool is_up = dim != NULL && vlog_is_up(dim);
            const bool same_dir = (kind == V_RANGE_POS && is_up)
               || (kind == V_RANGE_NEG && !is_up);

            if (!same_dir) {
               mir_type_t t_offset = mir_offset_type(g->mu);
               mir_value_t adj = mir_const(g->mu, t_offset, nelems - 1);
               off = mir_build_sub(g->mu, t_offset, base, adj);
            }
         }

         mir_type_t t_offset = mir_offset_type(g->mu);

         const unsigned dim_size =
            dim != NULL ? vlog_size(dim) : prefix.size;
         assert(prefix.size % dim_size == 0);

         const unsigned stride = prefix.size / dim_size;
         const unsigned size = nelems * stride;

         if (stride != 1) {
            mir_value_t scale = mir_const(g->mu, t_offset, stride);
            off = mir_build_mul(g->mu, t_offset, off, scale);
         }

         mir_value_t zero = mir_const(g->mu, t_offset, 0);
         mir_value_t count = mir_const(g->mu, t_offset, size);
         mir_value_t high = mir_build_add(g->mu, t_offset, off, count);

         mir_value_t prefix_valid_high =
            mir_build_add(g->mu, t_offset, prefix.src_offset, prefix.count);

         mir_value_t valid_start =
            vlog_lower_offset_max(g, off, prefix.src_offset);
         mir_value_t valid_stop =
            vlog_lower_offset_min(g, high, prefix_valid_high);
         mir_value_t raw_count =
            mir_build_sub(g->mu, t_offset, valid_stop, valid_start);
         mir_value_t valid_count = vlog_lower_offset_max(g, raw_count, zero);

         if (kind != V_RANGE_CONST) {
            mir_value_t known = mir_build_defined(g->mu, index);
            valid_count = mir_build_select(g->mu, t_offset, known, valid_count,
                                           zero);
         }

         mir_value_t value_offset =
            mir_build_sub(g->mu, t_offset, valid_start, off);

         mir_value_t dst_offset =
            mir_build_add(g->mu, t_offset, prefix.dst_offset,
                          mir_build_sub(g->mu, t_offset,
                                        valid_start, prefix.src_offset));

         vlog_select_t result = {
            .obj        = prefix.obj,
            .dst_offset = dst_offset,
            .count      = valid_count,
            .src_offset = value_offset,
            .type       = vlog_sel_vec_type(g, prefix.type, size),
            .size       = size,
            .two_state  = prefix.two_state
               || mir_get_class(g->mu, prefix.type) == MIR_TYPE_VEC2,
         };
         return result;
      }
   case V_MEMBER_REF:
      {
         vlog_node_t decl = vlog_member_decl(v);

         // A parameter or enumeration name of a class
         if (vlog_kind(decl) == V_PARAM_DECL || vlog_kind(decl) == V_LOCALPARAM
             || vlog_kind(decl) == V_ENUM_NAME)
            return vlog_lower_param_value(g, decl);

         const type_info_t *ti = vlog_type_info(g, vlog_type(decl));

         if (vlog_kind(decl) == V_VAR_DECL
             && (vlog_flags(decl) & VLOG_F_NESTED_INST)) {
            // The context of an interface instance inside the interface
            mir_type_t t_offset = mir_offset_type(g->mu);
            mir_value_t zero = mir_const(g->mu, t_offset, 0);

            mir_value_t args[] = {
               vlog_lower_rvalue(g, vlog_value(v)),
               mir_const(g->mu, mir_int_type(g->mu, INT64_MIN, INT64_MAX),
                         vlog_label_hash(vlog_ident(v)))
            };
            mir_value_t child = mir_build_fcall(g->mu,
                                                ident_new("__sv_vif_child"),
                                                ti->type, MIR_NULL_STAMP,
                                                args, 2);
            mir_value_t var = mir_add_var(g->mu, ti->type, MIR_NULL_STAMP,
                                          ident_uniq("vif"), MIR_VAR_TEMP);
            mir_build_store(g->mu, var, child);

            vlog_select_t result = {
               .obj        = var,
               .dst_offset = zero,
               .count      = mir_const(g->mu, t_offset, ti->size),
               .src_offset = zero,
               .type       = ti->type,
               .size       = ti->size,
            };
            return result;
         }

         vlog_node_t ptype = vlog_get_type(vlog_value(v));
         if (ptype != NULL && vlog_is_dynamic_type(ptype)) {
            mir_type_t t_offset = mir_offset_type(g->mu);
            mir_value_t zero = mir_const(g->mu, t_offset, 0);

            const bool for_write = g->for_write;
            g->for_write = false;

            mir_value_t ptr;
            if (vlog_kind(decl) == V_VAR_DECL
                && (vlog_flags(decl) & VLOG_F_STATIC))
               ptr = vlog_lower_decl_ptr(g, decl);
            else {
               // Writing to a member creates the entry of an associative
               // array which holds the structure
               g->for_write = for_write;
               mir_value_t base = vlog_lower_rvalue(g, vlog_value(v));
               g->for_write = false;
               switch (vlog_kind(ptype)) {
               case V_CLASS_DECL:
                  if (!mir_is_null(g->crv_ctx))
                     base = vlog_crv_guard_null(g, base, v);
                  else
                     mir_build_null_check(g->mu, base, mir_build_debug_locus(
                                             g->mu, vlog_to_object(v)));
                  ptr = vlog_link_property(g, base, decl,
                                           vlog_prop_name(ptype, decl), ti);
                  break;
               case V_STRUCT_DECL:
               case V_UNION_DECL:
                  {
                     int field = 0;
                     for (; vlog_decl(ptype, field) != decl; field++);

                     mir_value_t rec = mir_build_all(g->mu, base);
                     if (!for_write && vlog_kind(ptype) == V_UNION_DECL
                         && (vlog_flags(ptype) & VLOG_F_TAGGED))
                        vlog_check_tag(g, v, rec, ptype, field);
                     ptr = mir_build_record_ref(g->mu, rec, field);
                  }
                  break;
               default:   // Virtual interface
                  {
                     vlog_node_t tdecl = vlog_type(decl);
                     if (vlog_kind(tdecl) == V_DATA_TYPE
                         && !vlog_is_dynamic_type(tdecl)
                         && vlog_size(tdecl) == 0)
                        fatal_at(vlog_loc(v), "sorry, a member of a virtual "
                                 "interface with a width that depends on a "
                                 "parameter is not supported");

                     if (vlog_is_dynamic_type(tdecl)) {
                        // Events, strings, and the like are plain variables
                        ptr = mir_build_link_var(g->mu, base, vlog_ident(decl),
                                                 ti->type);
                        break;
                     }

                     mir_type_t t_signal = ti->signal;
                     if (vlog_kind(decl) == V_PORT_DECL
                         && vlog_kind(vlog_ref(decl)) == V_NET_DECL)
                        t_signal = mir_signal_type(g->mu,
                                                   mir_int_type(g->mu, 0, 255));

                     mir_value_t link = mir_build_link_var(g->mu, base,
                                                           vlog_ident(decl),
                                                           t_signal);
                     ptr = mir_build_load(g->mu, link);
                  }
                  break;
               }
            }

            g->for_write = for_write;

            // A whole unpacked array in a signal is a flat vector
            unsigned size = ti->size;
            mir_type_t type = ti->type;
            if (vlog_is_fixed_unpacked(decl) && !mir_points_to_vector(g->mu, ptr)
                && (mir_get_class(g->mu, ti->type) == MIR_TYPE_VEC2
                    || mir_get_class(g->mu, ti->type) == MIR_TYPE_VEC4)) {
               size *= vlog_size(decl);
               type = vlog_flat_array_type(g, decl, ti, size);
            }

            vlog_select_t result = {
               .obj        = ptr,
               .dst_offset = zero,
               .count      = mir_const(g->mu, t_offset, size),
               .src_offset = zero,
               .type       = type,
               .size       = size,
            };
            return result;
         }

         vlog_select_t prefix = vlog_lower_select(g, vlog_value(v));

         vlog_node_t type = vlog_get_type(vlog_value(v));
         assert(vlog_flags(type) & VLOG_F_PACKED);

         mir_type_t t_offset = mir_offset_type(g->mu);
         mir_value_t offset =
            mir_const(g->mu, t_offset, vlog_packed_offset(type, decl));
         mir_value_t dst_offset =
            mir_build_add(g->mu, t_offset, prefix.dst_offset, offset);

         vlog_select_t result = {
            .obj        = prefix.obj,
            .dst_offset = dst_offset,
            .count      = mir_const(g->mu, t_offset, ti->size),
            .src_offset = mir_const(g->mu, t_offset, 0),
            .type       = ti->type,
            .size       = ti->size,
         };
         return result;
      }
   default:
      CANNOT_HANDLE(v);
   }
}

static unsigned vlog_count_lvalues(vlog_node_t v)
{
   if (vlog_kind(v) != V_CONCAT)
      return 1;

   unsigned count = 0;
   const int nparams = vlog_params(v);
   for (int i = 0; i < nparams; i++)
      count += vlog_count_lvalues(vlog_param(v, i));

   return count;
}

static void vlog_lower_lvalues(vlog_gen_t *g, vlog_node_t v,
                               vlog_select_t *lvalues, vlog_node_t *nodes,
                               unsigned *index)
{
   if (vlog_kind(v) == V_CONCAT) {
      const int nparams = vlog_params(v);
      for (int i = 0; i < nparams; i++)
         vlog_lower_lvalues(g, vlog_param(v, i), lvalues, nodes, index);
   }
   else {
      lvalues[*index] = vlog_lower_select(g, v);
      if (nodes != NULL)
         nodes[*index] = v;
      (*index)++;
   }
}

static mir_type_t vlog_assign_context(vlog_gen_t *g, mir_type_t type,
                                      vlog_node_t rhs)
{
   // Assignment context propagates width but not signedness

   mir_class_t class = mir_get_class(g->mu, type);
   if (class != MIR_TYPE_VEC2 && class != MIR_TYPE_VEC4)
      return type;

   const bool is_signed = vlog_is_signed(rhs);
   if (is_signed == mir_get_signed(g->mu, type))
      return type;

   const int size = mir_get_size(g->mu, type);

   if (class == MIR_TYPE_VEC2)
      return mir_vec2_type(g->mu, size, is_signed);
   else
      return mir_vec4_type(g->mu, size, is_signed);
}

#define MAX_UNPACKED_DIMS 8

static int vlog_unpacked_dims(vlog_node_t v, vlog_node_t *dims)
{
   // Returns the unpacked dimensions left after any selects in v

   switch (vlog_kind(v)) {
   case V_REF:
   case V_MEMBER_REF:
   case V_HIER_REF:
      {
         if (!vlog_has_ref(v) || !vlog_is_fixed_unpacked(vlog_ref(v)))
            return 0;

         vlog_node_t decl = vlog_ref(v);
         const int ndims = MIN(vlog_ranges(decl), MAX_UNPACKED_DIMS);
         for (int i = 0; i < ndims; i++)
            dims[i] = vlog_range(decl, i);
         return ndims;
      }
   case V_BIT_SELECT:
      {
         vlog_node_t tmp[MAX_UNPACKED_DIMS];
         const int ndims = vlog_unpacked_dims(vlog_value(v), tmp);
         const int skip = vlog_params(v);
         if (skip >= ndims)
            return 0;

         for (int i = skip; i < ndims; i++)
            dims[i - skip] = tmp[i];
         return ndims - skip;
      }
   case V_PART_SELECT:
      {
         // The selected range stands for the first dimension
         vlog_node_t tmp[MAX_UNPACKED_DIMS];
         const int ndims = vlog_unpacked_dims(vlog_value(v), tmp);
         if (ndims == 0)
            return 0;

         dims[0] = v;
         for (int i = 1; i < ndims; i++)
            dims[i] = tmp[i];
         return ndims;
      }
   default:
      return 0;
   }
}

static void vlog_array_pattern_slots(vlog_node_t v, vlog_node_t dim,
                                     vlog_node_t *which, vlog_node_t *def)
{
   // Match the items of an array assignment pattern to elements

   const unsigned nelems = vlog_size(dim);
   const int nparams = vlog_params(v);
   for (int i = 0; i < nelems; i++)
      which[i] = NULL;

   *def = NULL;
   bool keyed = false;
   unsigned next = 0;
   for (int i = 0; i < nparams; i++) {
      vlog_node_t item = vlog_param(v, i);
      if (vlog_subkind(item) == 2) {
         const int64_t count = vlog_must_be_const(vlog_left(item));
         vlog_node_t list = vlog_right(item);
         const int nlist = vlog_params(list);
         for (int64_t j = 0; j < count; j++) {
            for (int k = 0; k < nlist; k++) {
               if (next >= nelems)
                  fatal_at(vlog_loc(item), "too many elements in assignment "
                           "pattern");
               which[next++] = vlog_right(vlog_param(list, k));
            }
         }
      }
      else if (vlog_has_left(item)) {
         keyed = true;
         int64_t key;
         if (!vlog_get_const(vlog_left(item), &key))
            fatal_at(vlog_loc(item), "sorry, only constant array pattern "
                     "keys are supported");

         int64_t lconst, rconst;
         vlog_bounds(dim, &lconst, &rconst);
         const int64_t pos = lconst < rconst ? key - lconst : lconst - key;
         if (pos < 0 || pos >= nelems)
            fatal_at(vlog_loc(item), "array pattern index %"PRIi64" is "
                     "out of range", key);
         which[pos] = vlog_right(item);
      }
      else if (vlog_subkind(item) == 1)
         *def = vlog_right(item);
      else if (keyed || next >= nelems)
         fatal_at(vlog_loc(item), "too many elements in assignment pattern");
      else
         which[next++] = vlog_right(item);
   }

   if (!keyed && *def == NULL && next != nelems)
      fatal_at(vlog_loc(v), "assignment pattern has %u elements but the "
               "array has %u", next, nelems);
}

static mir_value_t vlog_lower_array_pattern(vlog_gen_t *g, vlog_node_t v,
                                            vlog_node_t *dims, int ndims,
                                            const type_info_t *eti)
{
   // Positional or keyed assignment pattern for a fixed-size unpacked
   // array held as a flat vector with the first element most significant

   assert(vlog_kind(v) == V_PATTERN_EXPR);

   const unsigned nelems = vlog_size(dims[0]);
   unsigned stride = eti->size;
   for (int i = 1; i < ndims; i++)
      stride *= vlog_size(dims[i]);

   const unsigned total = nelems * stride;
   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_type_t t_total = mir_vec4_type(g->mu, total, false);

   vlog_node_t *which LOCAL = xmalloc_array(nelems, sizeof(vlog_node_t)), def;
   vlog_array_pattern_slots(v, dims[0], which, &def);

   mir_value_t result;
   if (total <= 64)
      result = mir_const_vec(g->mu, t_total, 0, 0);
   else {
      mir_type_t t_bit = mir_vec4_type(g->mu, 1, false);
      result = mir_build_cast(g->mu, t_total,
                              mir_const_vec(g->mu, t_bit, 0, 0));
   }

   mir_type_t t_vec4 = mir_vec4_type(g->mu, stride, false);

   for (int i = 0; i < nelems; i++) {
      vlog_node_t expr = which[i] != NULL ? which[i] : def;
      if (expr == NULL)
         continue;

      mir_value_t elem;
      if (ndims > 1 && vlog_kind(expr) == V_PATTERN_EXPR)
         elem = vlog_lower_array_pattern(g, expr, dims + 1, ndims - 1, eti);
      else {
         mir_value_t value = vlog_lower_with_context(
            g, expr, vlog_assign_context(g, eti->type, expr));
         elem = vlog_lower_cast(g, eti->type, value);

         if (ndims > 1) {
            // A single value is repeated for each of the inner dimensions
            mir_type_t t_single = mir_vec4_type(g->mu, eti->size, false);
            if (mir_get_class(g->mu, mir_get_type(g->mu, elem))
                != MIR_TYPE_VEC4
                || mir_get_signed(g->mu, mir_get_type(g->mu, elem)))
               elem = mir_build_cast(g->mu, t_single, elem);

            mir_value_t rep = vlog_lower_cast(
               g, t_vec4, mir_const_vec(g->mu, t_single, 0, 0));
            for (unsigned j = 0; j < stride / eti->size; j++)
               rep = mir_build_insert(
                  g->mu, elem, rep, mir_const(g->mu, t_offset, j * eti->size));
            elem = rep;
         }
      }

      if (mir_get_class(g->mu, mir_get_type(g->mu, elem)) != MIR_TYPE_VEC4
          || mir_get_signed(g->mu, mir_get_type(g->mu, elem)))
         elem = mir_build_cast(g->mu, t_vec4, elem);

      mir_value_t pos = mir_const(g->mu, t_offset, i * stride);
      result = mir_build_insert(g->mu, elem, result, pos);
   }

   return result;
}

static void vlog_fill_real_pattern(vlog_gen_t *g, vlog_node_t v,
                                   vlog_node_t *dims, int ndims,
                                   const type_info_t *eti, mir_value_t base)
{
   // Store the elements of an assignment pattern for an unpacked array
   // of reals, which are held one per element, starting at base

   const unsigned nelems = vlog_size(dims[0]);
   unsigned stride = 1;
   for (int i = 1; i < ndims; i++)
      stride *= vlog_size(dims[i]);

   vlog_node_t *which LOCAL = xmalloc_array(nelems, sizeof(vlog_node_t)), def;
   vlog_array_pattern_slots(v, dims[0], which, &def);

   mir_type_t t_offset = mir_offset_type(g->mu);

   for (int i = 0; i < nelems; i++) {
      vlog_node_t expr = which[i] != NULL ? which[i] : def;
      if (expr == NULL)
         continue;

      mir_value_t ptr = mir_build_array_ref(
         g->mu, base, mir_const(g->mu, t_offset, i * stride));

      if (ndims > 1 && vlog_kind(expr) == V_PATTERN_EXPR)
         vlog_fill_real_pattern(g, expr, dims + 1, ndims - 1, eti, ptr);
      else {
         mir_value_t value = vlog_lower_cast(
            g, eti->type, vlog_lower_with_context(g, expr, eti->type));
         if (stride == 1)
            mir_build_store(g->mu, ptr, value);
         else {
            // A single value is repeated for each of the inner dimensions
            mir_build_set(g->mu, ptr, value,
                          mir_const(g->mu, t_offset, stride));
         }
      }
   }
}

static mir_value_t vlog_real_array_pattern(vlog_gen_t *g, vlog_node_t v,
                                           vlog_node_t *dims, int ndims,
                                           const type_info_t *eti)
{
   unsigned total = 1;
   for (int i = 0; i < ndims; i++)
      total *= vlog_size(dims[i]);

   mir_type_t t_array = mir_carray_type(g->mu, total, eti->type);
   mir_value_t tmp = mir_add_var(g->mu, t_array, MIR_NULL_STAMP,
                                 ident_uniq("tmp"), MIR_VAR_TEMP);

   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_value_t base = mir_build_array_ref(g->mu, tmp,
                                          mir_const(g->mu, t_offset, 0));
   mir_build_set(g->mu, base, mir_const_real(g->mu, eti->type, 0.0),
                 mir_const(g->mu, t_offset, total));
   vlog_fill_real_pattern(g, v, dims, ndims, eti, base);
   return tmp;
}

static const type_info_t *vlog_array_elem_info(vlog_gen_t *g,
                                               vlog_node_t v)
{
   while (vlog_kind(v) == V_BIT_SELECT || vlog_kind(v) == V_PART_SELECT)
      v = vlog_value(v);

   return vlog_type_info(g, vlog_type(vlog_ref(v)));
}

static unsigned vlog_unpacked_count(vlog_node_t *dims, int ndims)
{
   unsigned count = 1;
   for (int i = 0; i < ndims; i++)
      count *= vlog_size(dims[i]);
   return count;
}

static mir_value_t vlog_load_flat_array(vlog_gen_t *g, mir_value_t obj,
                                        unsigned count,
                                        const type_info_t *eti, bool reverse)
{
   // Gather elements held in memory into one vector

   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_type_t t_total = mir_vec4_type(g->mu, count * eti->size, false);
   mir_type_t t_elem = mir_vec4_type(g->mu, eti->size, false);

   mir_value_t result;
   if (count * eti->size <= 64)
      result = mir_const_vec(g->mu, t_total, 0, 0);
   else {
      mir_type_t t_bit = mir_vec4_type(g->mu, 1, false);
      result = mir_build_cast(g->mu, t_total,
                              mir_const_vec(g->mu, t_bit, 0, 0));
   }

   for (int i = 0; i < count; i++) {
      mir_value_t ptr = mir_build_array_ref(g->mu, obj,
                                            mir_const(g->mu, t_offset, i));
      mir_value_t elem = mir_build_cast(g->mu, t_elem, mir_build_load(g->mu, ptr));
      const int pos = reverse ? count - 1 - i : i;
      result = mir_build_insert(g->mu, elem, result,
                                mir_const(g->mu, t_offset, pos * eti->size));
   }

   return result;
}

static void vlog_store_flat_array(vlog_gen_t *g, mir_value_t obj,
                                  unsigned count, const type_info_t *eti,
                                  mir_value_t flat)
{
   // Scatter a vector into elements held in memory

   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_type_t t_elem = mir_vec4_type(g->mu, eti->size, false);

   for (int i = 0; i < count; i++) {
      mir_value_t part = mir_build_extract(g->mu, t_elem, flat,
                                           mir_const(g->mu, t_offset,
                                                     i * eti->size));
      mir_value_t ptr = mir_build_array_ref(g->mu, obj,
                                            mir_const(g->mu, t_offset, i));
      mir_build_store(g->mu, ptr, vlog_lower_cast(g, eti->type, part));
   }
}

static mir_value_t vlog_flat_to_dynamic(vlog_gen_t *g, vlog_node_t src,
                                        const type_info_t *eti,
                                        unsigned count, mir_value_t flat);

static mir_value_t vlog_fixed_to_dynamic(vlog_gen_t *g, vlog_node_t dyn_type,
                                         vlog_node_t src, mir_value_t flat)
{
   // Copy a fixed size unpacked array held as a vector to a dynamic array

   vlog_node_t dims[MAX_UNPACKED_DIMS];
   const int ndims = vlog_unpacked_dims(src, dims);
   if (ndims != 1)
      fatal_at(vlog_loc(src), "sorry, only a one dimensional fixed size "
               "array can be converted to a dynamic array");

   const type_info_t *eti = vlog_type_info(g, vlog_type(dyn_type));
   return vlog_flat_to_dynamic(g, src, eti, vlog_unpacked_count(dims, ndims),
                               flat);
}

static mir_value_t vlog_flat_to_dynamic(vlog_gen_t *g, vlog_node_t src,
                                        const type_info_t *eti,
                                        unsigned count, mir_value_t flat)
{
   if (mir_get_size(g->mu, mir_get_type(g->mu, flat)) != count * eti->size)
      fatal_at(vlog_loc(src), "sorry, the element type of this array does "
               "not match the dynamic array");

   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_value_t n = mir_const(g->mu, t_offset, count);
   mir_value_t data = vlog_alloc_array(g, eti->type, n);
   vlog_store_flat_array(g, data, count, eti, flat);
   return vlog_wrap_array(g, data, n);
}

static bool vlog_is_real_unpacked(vlog_gen_t *g, vlog_node_t src)
{
   vlog_node_t dims[MAX_UNPACKED_DIMS];
   return vlog_kind(src) != V_PATTERN_EXPR
      && vlog_unpacked_dims(src, dims) > 0
      && mir_get_class(g->mu, vlog_array_elem_info(g, src)->type)
         == MIR_TYPE_REAL;
}

static mir_value_t vlog_real_unpacked_to_dynamic(vlog_gen_t *g,
                                                 vlog_node_t src)
{
   // Copy a fixed size unpacked array of reals, which holds each element
   // separately, to a dynamic array

   vlog_node_t dims[MAX_UNPACKED_DIMS];
   const int ndims = vlog_unpacked_dims(src, dims);
   if (ndims != 1)
      fatal_at(vlog_loc(src), "sorry, only a one dimensional fixed size "
               "array can be converted to a dynamic array");

   const type_info_t *eti = vlog_array_elem_info(g, src);
   const unsigned count = vlog_unpacked_count(dims, ndims);

   vlog_select_t sel = vlog_lower_select(g, src);
   mir_value_t ptr = sel.obj;
   if (mir_is_signal(g->mu, ptr))
      ptr = mir_build_resolved(g->mu, ptr);
   ptr = mir_build_array_ref(g->mu, ptr, sel.dst_offset);

   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_value_t n = mir_const(g->mu, t_offset, count);
   mir_value_t data = vlog_alloc_array(g, eti->type, n);
   mir_build_copy(g->mu, data, ptr, n);
   return vlog_wrap_array(g, data, n);
}

static vlog_node_t vlog_virtual_element(vlog_node_t type)
{
   // A virtual interface type or the element type of an array of them

   if (type == NULL || vlog_kind(type) != V_DATA_TYPE)
      return NULL;
   else if (vlog_subkind(type) == DT_VIRTUAL)
      return type;
   else if ((vlog_subkind(type) == DT_QUEUE || vlog_subkind(type) == DT_DYNARRAY)
            && vlog_has_type(type))
      return vlog_virtual_element(vlog_get_type(vlog_type(type)));

   return NULL;
}

static void vlog_check_virtual_types(vlog_node_t to, vlog_node_t from,
                                     const loc_t *loc)
{
   // The layout of a virtual interface depends on the values of the
   // parameters of the interface so these must be the same

   vlog_node_t tt = vlog_virtual_element(to), ft = vlog_virtual_element(from);
   if (tt == NULL || ft == NULL || tt == ft)
      return;

   vlog_node_t tspec = vlog_virtual_spec(tt), fspec = vlog_virtual_spec(ft);
   if (tspec != NULL && fspec != NULL && vlog_ident(tspec) != vlog_ident(fspec))
      fatal_at(loc, "cannot assign a virtual interface of %pQ to one with "
               "different parameter values", vlog_ident2(tspec));
   else if ((tspec == NULL && vlog_virtual_unresolved(tt))
            || (fspec == NULL && vlog_virtual_unresolved(ft)))
      warn_at(loc, "the parameters of the virtual interface type are not "
              "known so cannot check that the layouts are the same");
}

static void vlog_assign_variable(vlog_gen_t *g, vlog_node_t target,
                                 mir_value_t value, vlog_node_t rhs);

typedef struct {
   mir_value_t index;
   mir_value_t limit;
   mir_block_t test_bb;
   mir_block_t body_bb;
   mir_block_t next_bb;
   mir_block_t exit_bb;
   mir_value_t i;
} vlog_loop_t;

static void vlog_loop_begin(vlog_gen_t *g, vlog_loop_t *l, mir_value_t start,
                            mir_value_t limit)
{
   mir_type_t t_offset = mir_offset_type(g->mu);

   l->index = mir_add_var(g->mu, t_offset, MIR_NULL_STAMP, ident_uniq("i"),
                          MIR_VAR_TEMP);
   l->limit = limit;
   l->test_bb = mir_add_block(g->mu);
   l->body_bb = mir_add_block(g->mu);
   l->next_bb = mir_add_block(g->mu);
   l->exit_bb = mir_add_block(g->mu);

   mir_build_store(g->mu, l->index, start);
   mir_build_jump(g->mu, l->test_bb);

   mir_set_cursor(g->mu, l->test_bb, MIR_APPEND);
   mir_value_t i = mir_build_load(g->mu, l->index);
   mir_build_cond(g->mu, mir_build_cmp(g->mu, MIR_CMP_LT, i, limit),
                  l->body_bb, l->exit_bb);

   mir_set_cursor(g->mu, l->body_bb, MIR_APPEND);
   l->i = mir_build_load(g->mu, l->index);
}

static void vlog_loop_end(vlog_gen_t *g, vlog_loop_t *l)
{
   mir_type_t t_offset = mir_offset_type(g->mu);

   mir_build_jump(g->mu, l->next_bb);

   mir_set_cursor(g->mu, l->next_bb, MIR_APPEND);
   mir_value_t i = mir_build_load(g->mu, l->index);
   mir_build_store(g->mu, l->index,
                   mir_build_add(g->mu, t_offset, i,
                                 mir_const(g->mu, t_offset, 1)));
   mir_build_jump(g->mu, l->test_bb);

   mir_set_cursor(g->mu, l->exit_bb, MIR_APPEND);
}

static mir_value_t vlog_convert_uarray(vlog_gen_t *g, mir_type_t type,
                                       mir_value_t value)
{
   mir_type_t from_elem = mir_get_elem(g->mu, mir_get_type(g->mu, value));
   mir_type_t to_elem = mir_get_elem(g->mu, type);

   const mir_class_t fclass = mir_get_class(g->mu, from_elem);
   const mir_class_t tclass = mir_get_class(g->mu, to_elem);
   if ((fclass != MIR_TYPE_VEC2 && fclass != MIR_TYPE_VEC4)
       || (tclass != MIR_TYPE_VEC2 && tclass != MIR_TYPE_VEC4))
      return mir_build_cast(g->mu, type, value);

   mir_type_t t_offset = mir_offset_type(g->mu);

   mir_value_t len = mir_build_uarray_len(g->mu, value, 0);
   mir_value_t src = mir_build_unwrap(g->mu, value);
   mir_value_t data = vlog_alloc_array(g, to_elem, len);

   vlog_loop_t l;
   vlog_loop_begin(g, &l, mir_const(g->mu, t_offset, 0), len);

   mir_value_t elem = mir_build_load(g->mu, mir_build_array_ref(g->mu, src, l.i));
   mir_build_store(g->mu, mir_build_array_ref(g->mu, data, l.i),
                   vlog_lower_cast(g, to_elem, elem));

   vlog_loop_end(g, &l);

   return vlog_wrap_array(g, data, len);
}

static mir_value_t vlog_stream_reorder(vlog_gen_t *g, vlog_node_t v,
                                       mir_value_t value)
{
   // Slices are taken from the right and the order of the slices reversed

   if (vlog_subkind(v) == V_STREAM_RIGHT)
      return value;

   mir_type_t t_offset = mir_offset_type(g->mu);

   const int width = mir_get_size(g->mu, mir_get_type(g->mu, value));
   int64_t slice = 1;
   if (vlog_has_value(v)) {
      slice = vlog_must_be_const(vlog_value(v));
      if (slice < 1)
         fatal_at(vlog_loc(v), "slice size of a streaming concatenation must "
                  "be positive");
   }

   if (slice >= width)
      return value;

   mir_type_t t_vec = mir_vec4_type(g->mu, width, false);
   mir_value_t result = vlog_lower_x(g, t_vec);

   for (int lo = 0, dst = 0; lo < width; lo += slice, dst += slice) {
      const int size = MIN(slice, width - lo);
      mir_type_t t_slice = mir_vec4_type(g->mu, size, false);
      mir_value_t part = mir_build_extract(
         g->mu, t_slice, value, mir_const(g->mu, t_offset, width - lo - size));
      result = mir_build_insert(g->mu, part, result,
                                mir_const(g->mu, t_offset, dst));
   }

   return result;
}

static mir_value_t vlog_stream_string_queue(vlog_gen_t *g, vlog_node_t v)
{
   // The elements of a queue of strings joined into one string

   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_type_t t_char = mir_char_type(g->mu);
   mir_value_t zero = mir_const(g->mu, t_offset, 0);

   mir_value_t q = vlog_lower_rvalue(g, vlog_param(v, 0));
   mir_value_t count = mir_build_uarray_len(g->mu, q, 0);
   mir_value_t qdata = mir_build_unwrap(g->mu, q);

   mir_value_t total = mir_add_var(g->mu, t_offset, MIR_NULL_STAMP,
                                   ident_uniq("total"), MIR_VAR_TEMP);
   mir_build_store(g->mu, total, zero);

   vlog_loop_t l1;
   vlog_loop_begin(g, &l1, zero, count);
   mir_value_t len1 = mir_build_uarray_len(
      g->mu, mir_build_load(g->mu, mir_build_array_ref(g->mu, qdata, l1.i)), 0);
   mir_build_store(g->mu, total, mir_build_add(g->mu, t_offset,
                                               mir_build_load(g->mu, total),
                                               len1));
   vlog_loop_end(g, &l1);

   mir_value_t size = mir_build_load(g->mu, total);
   mir_value_t data = vlog_alloc_array(g, t_char, size);

   mir_build_store(g->mu, total, zero);

   vlog_loop_t l2;
   vlog_loop_begin(g, &l2, zero, count);
   mir_value_t s = mir_build_load(g->mu,
                                  mir_build_array_ref(g->mu, qdata, l2.i));
   mir_value_t len2 = mir_build_uarray_len(g->mu, s, 0);
   mir_value_t pos = mir_build_load(g->mu, total);
   mir_build_copy(g->mu, mir_build_array_ref(g->mu, data, pos),
                  mir_build_unwrap(g->mu, s), len2);
   mir_build_store(g->mu, total, mir_build_add(g->mu, t_offset, pos, len2));
   vlog_loop_end(g, &l2);

   return vlog_wrap_array(g, data, size);
}

static bool vlog_stream_with(vlog_node_t p)
{
   // An operand which is a slice of a dynamic array or queue

   if (vlog_kind(p) != V_PART_SELECT)
      return false;

   vlog_node_t t = vlog_get_type(vlog_value(p));
   return t != NULL && vlog_kind(t) == V_DATA_TYPE
      && (vlog_subkind(t) == DT_DYNARRAY || vlog_subkind(t) == DT_QUEUE);
}

static void vlog_stream_with_range(vlog_gen_t *g, vlog_node_t p,
                                   mir_value_t *lo, mir_value_t *count)
{
   mir_type_t t_offset = mir_offset_type(g->mu);

   mir_value_t left = vlog_lower_cast(g, t_offset,
                                      vlog_lower_rvalue(g, vlog_left(p)));
   mir_value_t right = vlog_lower_cast(g, t_offset,
                                       vlog_lower_rvalue(g, vlog_right(p)));
   mir_value_t one = mir_const(g->mu, t_offset, 1);

   switch (vlog_subkind(p)) {
   case V_RANGE_POS:
      *lo = left;
      *count = right;
      break;
   case V_RANGE_NEG:
      *lo = mir_build_add(g->mu, t_offset,
                          mir_build_sub(g->mu, t_offset, left, right), one);
      *count = right;
      break;
   default:
      *lo = left;
      *count = mir_build_add(g->mu, t_offset,
                             mir_build_sub(g->mu, t_offset, right, left), one);
      break;
   }
}

static bool vlog_stream_is_dynamic(vlog_node_t v)
{
   // An operand has a length that is not known until the simulation runs

   const int nparams = vlog_params(v);
   for (int i = 0; i < nparams; i++) {
      vlog_node_t p = vlog_param(v, i);
      if (vlog_kind(p) == V_STREAM) {
         if (vlog_stream_is_dynamic(p))
            return true;
         continue;
      }

      if (vlog_is_string(p) || vlog_stream_with(p))
         return true;

      vlog_node_t t = vlog_get_type(p);
      if (t != NULL && vlog_kind(t) == V_DATA_TYPE
          && (vlog_subkind(t) == DT_DYNARRAY || vlog_subkind(t) == DT_QUEUE))
         return true;
   }

   return false;
}

static int vlog_stream_ubits(vlog_gen_t *g, vlog_node_t v)
{
   // A variable length stream is held in bytes unless a slice size or an
   // operand is not a whole number of bytes, when it is held as bits

   if (vlog_kind(v) == V_STREAM && vlog_subkind(v) == V_STREAM_LEFT) {
      int64_t slice = 1;
      if (vlog_has_value(v))
         slice = vlog_must_be_const(vlog_value(v));
      if (slice % 8 != 0)
         return 1;
   }

   const int nparams = vlog_params(v);
   for (int i = 0; i < nparams; i++) {
      vlog_node_t p = vlog_param(v, i);
      if (vlog_kind(p) == V_STREAM) {
         if (vlog_stream_ubits(g, p) != 8)
            return 1;
         continue;
      }

      if (vlog_is_string(p))
         continue;

      vlog_node_t t = vlog_get_type(vlog_stream_with(p) ? vlog_value(p) : p);
      if (t != NULL && vlog_kind(t) == V_DATA_TYPE
          && (vlog_subkind(t) == DT_DYNARRAY || vlog_subkind(t) == DT_QUEUE)) {
         if (vlog_type_info(g, vlog_type(t))->size % 8 != 0)
            return 1;
      }
      else if (vlog_width(p) % 8 != 0)
         return 1;
   }

   return 8;
}

static void vlog_stream_store_bytes(vlog_gen_t *g, mir_value_t vec,
                                    mir_value_t out, mir_value_t base,
                                    const loc_t *loc)
{
   // Most significant unit first where a unit is a byte, or a single bit
   // if any operand of the stream is not a whole number of bytes

   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_type_t t_char = mir_char_type(g->mu);
   mir_type_t t_int = mir_int_type(g->mu, INT64_MIN, INT64_MAX);

   const int ubits = g->stream_ubits;
   const int size = mir_get_size(g->mu, mir_get_type(g->mu, vec));
   if (size % ubits != 0)
      fatal_at(loc, "sorry, the operands of a streaming concatenation of "
               "variable length must be a whole number of bytes");

   mir_value_t v4 = mir_build_cast(g->mu, mir_vec4_type(g->mu, size, false),
                                   vec);
   const int nunits = size / ubits;
   for (int j = 0; j < nunits; j++) {
      mir_value_t part = mir_build_extract(
         g->mu, mir_vec4_type(g->mu, ubits, false), v4,
         mir_const(g->mu, t_offset, j * ubits));
      mir_value_t ch = mir_build_cast(g->mu, t_char,
                                      vlog_lower_cast(g, t_int, part));
      mir_value_t idx = mir_build_add(g->mu, t_offset, base,
                                      mir_const(g->mu, t_offset, j));
      mir_build_store(g->mu, mir_build_array_ref(g->mu, out, idx), ch);
   }
}

static mir_value_t vlog_stream_value_bytes(vlog_gen_t *g, mir_value_t value,
                                           const loc_t *loc)
{
   // The units of a string, an array of integral values or a vector

   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_type_t t_char = mir_char_type(g->mu);
   mir_type_t t_int = mir_int_type(g->mu, INT64_MIN, INT64_MAX);

   const int ubits = g->stream_ubits;

   if (mir_is(g->mu, value, MIR_TYPE_UARRAY)) {
      mir_type_t t_elem = mir_get_elem(g->mu, mir_get_type(g->mu, value));
      mir_value_t n = mir_build_uarray_len(g->mu, value, 0);
      mir_value_t data = mir_build_unwrap(g->mu, value);

      int ew;
      if (mir_equals(t_elem, t_char)) {
         if (ubits == 8)
            return value;
         ew = 8;
      }
      else
         ew = mir_get_size(g->mu, t_elem);

      if (ew % ubits != 0)
         fatal_at(loc, "sorry, the elements of a streamed array must be "
                  "a whole number of bytes");

      mir_value_t nb = mir_const(g->mu, t_offset, ew / ubits);
      mir_value_t total = mir_build_mul(g->mu, t_offset, n, nb);
      mir_value_t out = vlog_alloc_array(g, t_char, total);

      vlog_loop_t l;
      vlog_loop_begin(g, &l, mir_const(g->mu, t_offset, 0), n);
      mir_value_t elem = mir_build_load(g->mu,
                                        mir_build_array_ref(g->mu, data, l.i));
      if (mir_equals(t_elem, t_char))
         elem = vlog_lower_cast(g, mir_vec4_type(g->mu, 8, false),
                                mir_build_cast(g->mu, t_int, elem));
      vlog_stream_store_bytes(g, elem, out,
                              mir_build_mul(g->mu, t_offset, l.i, nb), loc);
      vlog_loop_end(g, &l);

      return vlog_wrap_array(g, out, total);
   }

   const int size = mir_get_size(g->mu, mir_get_type(g->mu, value));
   mir_value_t total = mir_const(g->mu, t_offset, size / ubits);
   mir_value_t out = vlog_alloc_array(g, t_char, total);
   vlog_stream_store_bytes(g, value, out, mir_const(g->mu, t_offset, 0), loc);
   return vlog_wrap_array(g, out, total);
}

static mir_value_t vlog_stream_dyn(vlog_gen_t *g, vlog_node_t v);

static mir_value_t vlog_stream_reverse(vlog_gen_t *g, vlog_node_t v,
                                       mir_value_t bytes)
{
   // Slices are taken from the right and the order of the slices reversed

   if (vlog_subkind(v) == V_STREAM_RIGHT)
      return bytes;

   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_type_t t_char = mir_char_type(g->mu);

   int64_t bits = 1;
   if (vlog_has_value(v))
      bits = vlog_must_be_const(vlog_value(v));

   if (bits < 1 || bits % g->stream_ubits != 0)
      fatal_at(vlog_loc(v), "sorry, the slice size of a streaming "
               "concatenation of variable length must be a multiple of 8");

   mir_value_t slice = mir_const(g->mu, t_offset, bits / g->stream_ubits);
   mir_value_t n = mir_build_uarray_len(g->mu, bytes, 0);
   mir_value_t data = mir_build_unwrap(g->mu, bytes);
   mir_value_t out = vlog_alloc_array(g, t_char, n);

   mir_value_t zero = mir_const(g->mu, t_offset, 0);
   mir_value_t nfull = mir_build_div(g->mu, t_offset, n, slice);
   mir_value_t rest = mir_build_rem(g->mu, t_offset, n, slice);

   vlog_loop_t l;
   vlog_loop_begin(g, &l, zero, nfull);
   mir_value_t dst = mir_build_mul(g->mu, t_offset, l.i, slice);
   mir_value_t src = mir_build_sub(g->mu, t_offset, n,
                                   mir_build_add(g->mu, t_offset, dst, slice));
   mir_build_copy(g->mu, mir_build_array_ref(g->mu, out, dst),
                  mir_build_array_ref(g->mu, data, src), slice);
   vlog_loop_end(g, &l);

   mir_value_t tail = mir_build_sub(g->mu, t_offset, n, rest);
   mir_build_copy(g->mu, mir_build_array_ref(g->mu, out, tail),
                  mir_build_array_ref(g->mu, data, zero), rest);

   return vlog_wrap_array(g, out, n);
}

static mir_value_t vlog_stream_dyn(vlog_gen_t *g, vlog_node_t v)
{
   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_type_t t_char = mir_char_type(g->mu);

   const int nparams = vlog_params(v);
   mir_value_t *parts LOCAL = xmalloc_array(nparams, sizeof(mir_value_t));

   mir_value_t total = mir_const(g->mu, t_offset, 0);
   for (int i = 0; i < nparams; i++) {
      vlog_node_t p = vlog_param(v, i);
      if (vlog_kind(p) == V_STREAM)
         parts[i] = vlog_stream_dyn(g, p);
      else if (vlog_stream_with(p)) {
         mir_value_t lo, count;
         vlog_stream_with_range(g, p, &lo, &count);

         mir_value_t arr = vlog_lower_rvalue(g, vlog_value(p));
         mir_value_t data = mir_build_unwrap(g->mu, arr);
         mir_value_t slice = vlog_wrap_array(
            g, mir_build_array_ref(g->mu, data, lo), count);
         parts[i] = vlog_stream_value_bytes(g, slice, vlog_loc(p));
      }
      else
         parts[i] = vlog_stream_value_bytes(g, vlog_lower_rvalue(g, p),
                                            vlog_loc(p));

      total = mir_build_add(g->mu, t_offset, total,
                            mir_build_uarray_len(g->mu, parts[i], 0));
   }

   mir_value_t out = vlog_alloc_array(g, t_char, total);
   mir_value_t pos = mir_const(g->mu, t_offset, 0);
   for (int i = 0; i < nparams; i++) {
      mir_value_t len = mir_build_uarray_len(g->mu, parts[i], 0);
      mir_build_copy(g->mu, mir_build_array_ref(g->mu, out, pos),
                     mir_build_unwrap(g->mu, parts[i]), len);
      pos = mir_build_add(g->mu, t_offset, pos, len);
   }

   return vlog_stream_reverse(g, v, vlog_wrap_array(g, out, total));
}

static mir_value_t vlog_stream_compose(vlog_gen_t *g, mir_value_t data,
                                       mir_value_t base, int nunits)
{
   // A vector made from consecutive units, most significant first

   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_type_t t_int = mir_int_type(g->mu, INT64_MIN, INT64_MAX);

   const int ubits = g->stream_ubits;
   mir_type_t t_unit = mir_vec4_type(g->mu, ubits, false);

   mir_value_t vec = vlog_lower_x(g, mir_vec4_type(g->mu, nunits * ubits,
                                                    false));
   for (int j = 0; j < nunits; j++) {
      mir_value_t idx = mir_build_add(g->mu, t_offset, base,
                                      mir_const(g->mu, t_offset, j));
      mir_value_t ch = mir_build_load(g->mu,
                                      mir_build_array_ref(g->mu, data, idx));
      mir_value_t part = vlog_lower_cast(g, t_unit,
                                         mir_build_cast(g->mu, t_int, ch));
      vec = mir_build_insert(g->mu, part, vec,
                             mir_const(g->mu, t_offset, j * ubits));
   }

   return vec;
}

static mir_value_t vlog_stream_to_array(vlog_gen_t *g, mir_value_t data,
                                        mir_value_t from, mir_value_t count,
                                        mir_type_t t_elem)
{
   // Convert bytes to an array of integral values

   mir_type_t t_offset = mir_offset_type(g->mu);

   const int esize = mir_get_size(g->mu, t_elem);
   const int ubits = g->stream_ubits;
   if (esize < ubits && ubits % esize == 0) {
      // Several elements are packed in each byte, the first in the most
      // significant bits
      const int per = ubits / esize;
      mir_type_t t_slice = mir_vec4_type(g->mu, esize, false);
      mir_value_t perv = mir_const(g->mu, t_offset, per);
      mir_value_t n = mir_build_mul(g->mu, t_offset, count, perv);
      mir_value_t out = vlog_alloc_array(g, t_elem, n);

      vlog_loop_t l;
      vlog_loop_begin(g, &l, mir_const(g->mu, t_offset, 0), count);
      mir_value_t byte = vlog_stream_compose(
         g, data, mir_build_add(g->mu, t_offset, from, l.i), 1);
      for (int k = 0; k < per; k++) {
         mir_value_t part = mir_build_extract(
            g->mu, t_slice, byte, mir_const(g->mu, t_offset, k * esize));
         mir_value_t idx = mir_build_add(
            g->mu, t_offset, mir_build_mul(g->mu, t_offset, l.i, perv),
            mir_const(g->mu, t_offset, k));
         mir_build_store(g->mu, mir_build_array_ref(g->mu, out, idx),
                         vlog_lower_cast(g, t_elem, part));
      }
      vlog_loop_end(g, &l);

      return vlog_wrap_array(g, out, n);
   }
   else if (esize % ubits != 0)
      fatal("sorry, streaming into elements of %d bits is not "
            "supported", esize);

   const int nb = esize / ubits;
   mir_value_t nbytes = mir_const(g->mu, t_offset, nb);
   mir_value_t n = mir_build_div(g->mu, t_offset, count, nbytes);
   mir_value_t out = vlog_alloc_array(g, t_elem, n);

   vlog_loop_t l;
   vlog_loop_begin(g, &l, mir_const(g->mu, t_offset, 0), n);
   mir_value_t base = mir_build_add(g->mu, t_offset, from,
                                    mir_build_mul(g->mu, t_offset, l.i,
                                                  nbytes));
   mir_value_t vec = vlog_stream_compose(g, data, base, nb);
   mir_build_store(g->mu, mir_build_array_ref(g->mu, out, l.i),
                   vlog_lower_cast(g, t_elem, vec));
   vlog_loop_end(g, &l);

   return vlog_wrap_array(g, out, n);
}

static mir_value_t vlog_lower_stream_inner(vlog_gen_t *g, vlog_node_t v,
                                           mir_type_t context)
{
   mir_type_t t_offset = mir_offset_type(g->mu);

   if (vlog_params(v) == 1 && vlog_subkind(v) == V_STREAM_RIGHT) {
      vlog_node_t qt = vlog_get_type(vlog_param(v, 0));
      if (qt != NULL && vlog_kind(qt) == V_DATA_TYPE
          && vlog_subkind(qt) == DT_QUEUE) {
         vlog_node_t et = vlog_get_type(vlog_type(qt));
         if (et != NULL && vlog_kind(et) == V_DATA_TYPE
             && vlog_subkind(et) == DT_STRING)
            return vlog_stream_string_queue(g, v);
      }
   }

   if (vlog_stream_is_dynamic(v)) {
      mir_value_t bytes = vlog_stream_dyn(g, v);
      if (!mir_is_null(context)
          && mir_get_class(g->mu, context) == MIR_TYPE_UARRAY) {
         mir_type_t t_elem = mir_get_elem(g->mu, context);
         if (!mir_equals(t_elem, mir_char_type(g->mu))) {
            mir_value_t data = mir_build_unwrap(g->mu, bytes);
            return vlog_stream_to_array(
               g, data, mir_const(g->mu, t_offset, 0),
               mir_build_uarray_len(g->mu, bytes, 0), t_elem);
         }
      }

      if (!mir_is_null(context)
          && (mir_get_class(g->mu, context) == MIR_TYPE_VEC2
              || mir_get_class(g->mu, context) == MIR_TYPE_VEC4)) {
         // The bytes are the leftmost bits of a vector target
         const int ubits = g->stream_ubits;
         const int size = mir_get_size(g->mu, context);
         assert(size % ubits == 0);

         mir_value_t zero = mir_const(g->mu, t_offset, 0);
         mir_value_t nb = mir_const(g->mu, t_offset, size / ubits);
         mir_value_t len = mir_build_uarray_len(g->mu, bytes, 0);
         mir_value_t take = mir_build_select(
            g->mu, t_offset, mir_build_cmp(g->mu, MIR_CMP_LT, len, nb), len, nb);

         mir_type_t t_char = mir_char_type(g->mu);
         mir_value_t buf = vlog_alloc_array(g, t_char, nb);
         mir_build_set(g->mu, buf, mir_const(g->mu, t_char, 0), nb);
         mir_build_copy(g->mu, buf, mir_build_unwrap(g->mu, bytes), take);

         mir_value_t vec = vlog_stream_compose(g, buf, zero, size / ubits);
         return mir_build_cast(g->mu, context, vec);
      }

      return bytes;
   }

   const int nparams = vlog_params(v);
   mir_value_t *parts LOCAL = xmalloc_array(nparams, sizeof(mir_value_t));

   int width = 0;
   for (int i = 0; i < nparams; i++) {
      mir_value_t value = vlog_lower_rvalue(g, vlog_param(v, i));
      if (!mir_is_vector(g->mu, value))
         fatal_at(vlog_loc(vlog_param(v, i)), "sorry, only integral operands "
                  "are supported in a streaming concatenation");

      const int size = mir_get_size(g->mu, mir_get_type(g->mu, value));
      parts[i] = mir_build_cast(g->mu, mir_vec4_type(g->mu, size, false),
                                value);
      width += size;
   }

   mir_type_t t_vec = mir_vec4_type(g->mu, MAX(width, 1), false);
   mir_value_t packed = vlog_lower_x(g, t_vec);
   for (int i = 0, pos = 0; i < nparams; i++) {
      packed = mir_build_insert(g->mu, parts[i], packed,
                                mir_const(g->mu, t_offset, pos));
      pos += mir_get_size(g->mu, mir_get_type(g->mu, parts[i]));
   }

   mir_value_t result = vlog_stream_reorder(g, v, packed);

   if (!mir_is_null(context) && mir_get_class(g->mu, context) == MIR_TYPE_UARRAY) {
      // The slices are the elements of a dynamic array or queue
      mir_type_t t_elem = mir_get_elem(g->mu, context);
      const mir_class_t eclass = mir_get_class(g->mu, t_elem);
      if (eclass == MIR_TYPE_VEC2 || eclass == MIR_TYPE_VEC4) {
         const int esize = mir_get_size(g->mu, t_elem);
         const int count = (width + esize - 1) / esize;
         const int padded = count * esize;

         if (padded > width) {
            mir_type_t t_wide = mir_vec4_type(g->mu, padded, false);
            mir_value_t zero = mir_build_cast(
               g->mu, t_wide, mir_const_vec(g->mu, mir_vec4_type(g->mu, 1, false),
                                            0, 0));
            result = mir_build_insert(g->mu, result, zero,
                                      mir_const(g->mu, t_offset, 0));
         }

         mir_type_t t_part = mir_vec4_type(g->mu, esize, false);
         mir_value_t n = mir_const(g->mu, t_offset, count);
         mir_value_t out = vlog_alloc_array(g, t_elem, n);
         for (int i = 0; i < count; i++) {
            mir_value_t part = mir_build_extract(
               g->mu, t_part, result,
               mir_const(g->mu, t_offset, i * esize));
            mir_build_store(g->mu, mir_build_array_ref(
                               g->mu, out, mir_const(g->mu, t_offset, i)),
                            vlog_lower_cast(g, t_elem, part));
         }

         return vlog_wrap_array(g, out, n);
      }
   }

   // A wider target is padded on the right
   if (!mir_is_null(context)) {
      const mir_class_t class = mir_get_class(g->mu, context);
      if (class == MIR_TYPE_VEC2 || class == MIR_TYPE_VEC4) {
         const int tsize = mir_get_size(g->mu, context);
         if (tsize < width)
            fatal_at(vlog_loc(v), "streaming concatenation of %d bits is "
                     "wider than its %d bit target", width, tsize);
         else if (tsize > width) {
            mir_type_t t_wide = mir_vec4_type(g->mu, tsize, false);
            mir_value_t zero = mir_build_cast(
               g->mu, t_wide, mir_const_vec(g->mu, mir_vec4_type(g->mu, 1, false),
                                            0, 0));
            result = mir_build_insert(g->mu, result, zero,
                                      mir_const(g->mu, t_offset, 0));
         }
      }
   }

   return result;
}

static mir_value_t vlog_lower_stream(vlog_gen_t *g, vlog_node_t v,
                                     mir_type_t context)
{
   const int saved = g->stream_ubits;
   g->stream_ubits = vlog_stream_ubits(g, v);

   if (!mir_is_null(context) && vlog_stream_is_dynamic(v)
       && (mir_get_class(g->mu, context) == MIR_TYPE_VEC2
           || mir_get_class(g->mu, context) == MIR_TYPE_VEC4)
       && mir_get_size(g->mu, context) % g->stream_ubits != 0)
      g->stream_ubits = 1;

   mir_value_t result = vlog_lower_stream_inner(g, v, context);
   g->stream_ubits = saved;
   return result;
}

static void vlog_assign_stream_dyn_inner(vlog_gen_t *g, vlog_node_t target,
                                         mir_value_t value)
{
   mir_type_t t_offset = mir_offset_type(g->mu);

   if (vlog_params(target) == 1 && mir_is_vector(g->mu, value)) {
      // A vector unpacked into the elements of a dynamic array
      vlog_node_t operand = vlog_param(target, 0);
      vlog_node_t type = vlog_get_type(operand);
      if (type != NULL && vlog_kind(type) == V_DATA_TYPE
          && (vlog_subkind(type) == DT_DYNARRAY
              || vlog_subkind(type) == DT_QUEUE)) {
         const type_info_t *eti = vlog_type_info(g, vlog_type(type));
         const mir_class_t class = mir_get_class(g->mu, eti->type);
         const int width = mir_get_size(g->mu, mir_get_type(g->mu, value));
         if ((class == MIR_TYPE_VEC2 || class == MIR_TYPE_VEC4)
             && width % eti->size == 0) {
            mir_value_t packed = mir_build_cast(
               g->mu, mir_vec4_type(g->mu, width, false), value);
            mir_value_t reordered = vlog_stream_reorder(g, target, packed);

            mir_type_t t_elem = mir_vec4_type(g->mu, eti->size, false);
            mir_value_t n = mir_const(g->mu, t_offset, width / eti->size);
            mir_value_t out = vlog_alloc_array(g, eti->type, n);
            for (int i = 0; i < width / eti->size; i++) {
               mir_value_t part = mir_build_extract(
                  g->mu, t_elem, reordered,
                  mir_const(g->mu, t_offset, i * eti->size));
               mir_build_store(
                  g->mu,
                  mir_build_array_ref(g->mu, out,
                                      mir_const(g->mu, t_offset, i)),
                  vlog_lower_cast(g, eti->type, part));
            }

            vlog_assign_variable(g, operand, vlog_wrap_array(g, out, n), NULL);
            return;
         }
      }
   }

   mir_value_t bytes = vlog_stream_reverse(
      g, target, vlog_stream_value_bytes(g, value, vlog_loc(target)));
   mir_value_t data = mir_build_unwrap(g->mu, bytes);
   mir_value_t n = mir_build_uarray_len(g->mu, bytes, 0);

   const int ubits = g->stream_ubits;
   mir_value_t pos = mir_const(g->mu, t_offset, 0);
   const int nparams = vlog_params(target);
   for (int i = 0; i < nparams; i++) {
      vlog_node_t operand = vlog_param(target, i);
      vlog_node_t type = vlog_get_type(operand);

      if (vlog_is_string(operand)) {
         mir_value_t rest = mir_build_sub(g->mu, t_offset, n, pos);
         mir_value_t copy;
         if (ubits == 8) {
            copy = vlog_alloc_array(g, mir_char_type(g->mu), rest);
            mir_build_copy(g->mu, copy, mir_build_array_ref(g->mu, data, pos),
                           rest);
         }
         else {
            // Eight bits for each character
            mir_type_t t_char = mir_char_type(g->mu);
            mir_type_t t_int = mir_int_type(g->mu, INT64_MIN, INT64_MAX);
            rest = mir_build_div(g->mu, t_offset, rest,
                                 mir_const(g->mu, t_offset, 8));
            copy = vlog_alloc_array(g, t_char, rest);

            vlog_loop_t l;
            vlog_loop_begin(g, &l, mir_const(g->mu, t_offset, 0), rest);
            mir_value_t at = mir_build_add(
               g->mu, t_offset, pos,
               mir_build_mul(g->mu, t_offset, l.i,
                             mir_const(g->mu, t_offset, 8)));
            mir_value_t byte = vlog_stream_compose(g, data, at, 8);
            mir_build_store(g->mu, mir_build_array_ref(g->mu, copy, l.i),
                            mir_build_cast(g->mu, t_char,
                                           vlog_lower_cast(g, t_int, byte)));
            vlog_loop_end(g, &l);
         }
         // A string holds no null characters (6.16)
         vlog_assign_variable(g, operand, vlog_drop_nuls(g, copy, rest),
                              NULL);
         pos = n;
      }
      else if (vlog_stream_with(operand)) {
         int64_t first;
         if (!vlog_get_const(vlog_left(operand), &first) || first != 0)
            fatal_at(vlog_loc(operand), "sorry, only a slice of a streamed "
                     "array which starts at index zero is supported");

         mir_value_t lo, count;
         vlog_stream_with_range(g, operand, &lo, &count);

         vlog_node_t atype = vlog_get_type(vlog_value(operand));
         const type_info_t *eti = vlog_type_info(g, vlog_type(atype));
         const int nb = mir_get_size(g->mu, eti->type) / ubits;
         mir_value_t bytes = mir_build_mul(g->mu, t_offset, count,
                                           mir_const(g->mu, t_offset, nb));
         vlog_assign_variable(
            g, vlog_value(operand),
            vlog_stream_to_array(g, data, pos, bytes, eti->type), NULL);
         pos = mir_build_add(g->mu, t_offset, pos, bytes);
      }
      else if (type != NULL && vlog_kind(type) == V_DATA_TYPE
               && (vlog_subkind(type) == DT_DYNARRAY
                   || vlog_subkind(type) == DT_QUEUE)) {
         const type_info_t *eti = vlog_type_info(g, vlog_type(type));
         mir_value_t rest = mir_build_sub(g->mu, t_offset, n, pos);
         vlog_assign_variable(
            g, operand, vlog_stream_to_array(g, data, pos, rest, eti->type),
            NULL);
         pos = n;
      }
      else {
         const int size = vlog_width(operand);
         assert(size % ubits == 0);

         vlog_assign_variable(
            g, operand, vlog_stream_compose(g, data, pos, size / ubits), NULL);
         pos = mir_build_add(g->mu, t_offset, pos,
                             mir_const(g->mu, t_offset, size / ubits));
      }
   }
}

static void vlog_assign_stream_dyn(vlog_gen_t *g, vlog_node_t target,
                                   mir_value_t value)
{
   const int saved = g->stream_ubits;
   g->stream_ubits = vlog_stream_ubits(g, target);

   mir_type_t vtype = mir_get_type(g->mu, value);
   if (mir_is_vector(g->mu, value)
       && mir_get_size(g->mu, vtype) % g->stream_ubits != 0)
      g->stream_ubits = 1;
   else if (mir_is(g->mu, value, MIR_TYPE_UARRAY)) {
      mir_type_t t_elem = mir_get_elem(g->mu, vtype);
      const mir_class_t class = mir_get_class(g->mu, t_elem);
      if ((class == MIR_TYPE_VEC2 || class == MIR_TYPE_VEC4)
          && mir_get_size(g->mu, t_elem) % 8 != 0)
         g->stream_ubits = 1;
   }

   vlog_assign_stream_dyn_inner(g, target, value);
   g->stream_ubits = saved;
}

static int vlog_stream_bits(vlog_node_t operand)
{
   // An unpacked array is streamed element by element

   vlog_node_t dims[MAX_UNPACKED_DIMS];
   const int ndims = vlog_unpacked_dims(operand, dims);
   if (ndims == 0)
      return vlog_width(operand);

   return vlog_width(operand) * vlog_unpacked_count(dims, ndims);
}

static void vlog_assign_stream(vlog_gen_t *g, vlog_node_t target,
                               mir_value_t value)
{
   mir_type_t t_offset = mir_offset_type(g->mu);

   if (vlog_stream_is_dynamic(target)
       || mir_is(g->mu, value, MIR_TYPE_UARRAY)) {
      vlog_assign_stream_dyn(g, target, value);
      return;
   }

   const int nparams = vlog_params(target);
   int total = 0;
   for (int i = 0; i < nparams; i++)
      total += vlog_stream_bits(vlog_param(target, i));

   const int width = mir_get_size(g->mu, mir_get_type(g->mu, value));
   if (width < total)
      fatal_at(vlog_loc(target), "the source of a streaming concatenation "
               "assignment must be at least as wide as the target");

   mir_value_t packed = mir_build_cast(
      g->mu, mir_vec4_type(g->mu, width, false), value);
   mir_value_t reordered = vlog_stream_reorder(g, target, packed);

   for (int i = 0, pos = 0; i < nparams; i++) {
      vlog_node_t operand = vlog_param(target, i);
      const int size = vlog_stream_bits(operand);
      mir_type_t t_part = mir_vec4_type(g->mu, size, false);
      mir_value_t part = mir_build_extract(g->mu, t_part, reordered,
                                           mir_const(g->mu, t_offset, pos));
      vlog_assign_variable(g, operand, part, NULL);
      pos += size;
   }
}

static void vlog_assign_variable(vlog_gen_t *g, vlog_node_t target,
                                 mir_value_t value, vlog_node_t rhs)
{
   if (vlog_kind(target) == V_STREAM) {
      if (mir_is_null(value))
         value = vlog_lower_rvalue(g, rhs);
      vlog_assign_stream(g, target, value);
      return;
   }

   if (vlog_kind(target) == V_PART_SELECT
       && vlog_subkind(target) == V_RANGE_CONST) {
      vlog_node_t ptype = vlog_get_type(vlog_value(target));
      if (ptype != NULL && vlog_kind(ptype) == V_DATA_TYPE
          && (vlog_subkind(ptype) == DT_DYNARRAY
              || vlog_subkind(ptype) == DT_QUEUE)) {
         // The elements of a dynamic array are replaced in place
         mir_type_t t_offset = mir_offset_type(g->mu);
         mir_value_t zero = mir_const(g->mu, t_offset, 0);

         if (mir_is_null(value) && vlog_kind(rhs) == V_CONCAT) {
            const type_info_t *eti = vlog_type_info(g, vlog_type(ptype));
            value = vlog_queue_concat(
               g, rhs, mir_uarray_type(g->mu, 1, eti->type));
         }
         else if (mir_is_null(value))
            value = vlog_lower_rvalue(g, rhs);

         mir_value_t arr = vlog_lower_rvalue(g, vlog_value(target));
         mir_value_t len = mir_build_uarray_len(g->mu, arr, 0);
         vlog_node_t sized = vlog_sized_dimension(ptype);
         mir_value_t left = vlog_lower_rvalue(g, vlog_left(target));
         mir_value_t right = vlog_lower_rvalue(g, vlog_right(target));
         if (sized != NULL) {
            left = vlog_lower_array_off(g, sized, left);
            right = vlog_lower_array_off(g, sized, right);
         }
         else {
            left = vlog_lower_cast(g, t_offset, left);
            right = vlog_lower_cast(g, t_offset, right);
         }

         mir_value_t span = mir_build_add(
            g->mu, t_offset, mir_build_sub(g->mu, t_offset, right, left),
            mir_const(g->mu, t_offset, 1));
         mir_value_t count = vlog_lower_offset_min(
            g, vlog_lower_offset_max(g, span, zero),
            mir_build_uarray_len(g->mu, value, 0));

         mir_value_t last = mir_build_add(g->mu, t_offset, left, count);
         mir_value_t ok = mir_build_and(
            g->mu, mir_build_cmp(g->mu, MIR_CMP_GEQ, left, zero),
            mir_build_cmp(g->mu, MIR_CMP_LEQ, last, len));
         count = mir_build_select(g->mu, t_offset, ok, count, zero);

         mir_build_copy(g->mu,
                        mir_build_array_ref(g->mu, mir_build_unwrap(g->mu, arr),
                                            vlog_lower_offset_max(g, left, zero)),
                        mir_build_unwrap(g->mu, value), count);
         return;
      }
   }

   if (vlog_kind(target) == V_BIT_SELECT) {
      vlog_node_t ptype = vlog_get_type(vlog_value(target));
      if (ptype != NULL && vlog_kind(ptype) == V_DATA_TYPE
          && vlog_subkind(ptype) == DT_STRING) {
         // One character of a string
         if (mir_is_null(value))
            value = vlog_lower_rvalue(g, rhs);

         mir_value_t slot = vlog_lower_select(g, vlog_value(target)).obj;
         mir_value_t str = vlog_str_arg(g, vlog_value(target));
         mir_value_t len = mir_build_uarray_len(g->mu, str, 0);
         mir_value_t data = vlog_str_copy(g, str);
         mir_value_t args[] = {
            data, len, vlog_str_offset_arg(g, vlog_param(target, 0)),
            vlog_lower_cast(g, mir_offset_type(g->mu), value)
         };
         vlog_str_call(g, "__sv_str_putc", mir_offset_type(g->mu), args, 4);
         mir_build_store(g->mu, slot, vlog_wrap_array(g, data, len));
         return;
      }
   }

   unsigned nlvalues = 1, targetsz = 0;
   mir_type_t type;
   vlog_select_t lvalue1, *lvalues = &lvalue1;
   vlog_node_t node1, *nodes = &node1;
   if (vlog_kind(target) == V_CONCAT) {
      nlvalues = vlog_count_lvalues(target);
      lvalues = xmalloc_array(nlvalues, sizeof(vlog_select_t));
      nodes = xmalloc_array(nlvalues, sizeof(vlog_node_t));

      unsigned index = 0;
      vlog_lower_lvalues(g, target, lvalues, nodes, &index);
      assert(index == nlvalues);

      for (int i = 0; i < nlvalues; i++)
         targetsz += lvalues[i].size;

      type = mir_vec4_type(g->mu, targetsz, false);
   }
   else {
      g->for_write = true;
      lvalue1 = vlog_lower_select(g, target);
      g->for_write = false;
      node1 = target;
      targetsz = lvalue1.size;
      type = lvalue1.type;
   }

   const bool copy = mir_is_null(value);

   if (mir_is_null(value)) {
      vlog_node_t dims[MAX_UNPACKED_DIMS];
      int ndims;
      if (vlog_kind(target) != V_CONCAT
          && (ndims = vlog_unpacked_dims(target, dims)) > 0) {
         const type_info_t *eti = vlog_array_elem_info(g, target);
         const unsigned count = vlog_unpacked_count(dims, ndims);

         vlog_node_t sdims[MAX_UNPACKED_DIMS];
         int sn;
         if (mir_get_class(g->mu, eti->type) == MIR_TYPE_REAL
             && (vlog_kind(rhs) == V_PATTERN_EXPR
                 || ((sn = vlog_unpacked_dims(rhs, sdims)) > 0
                     && vlog_unpacked_count(sdims, sn) == count))) {
            // Each real is held as one element
            mir_type_t t_offset = mir_offset_type(g->mu);
            mir_value_t src;
            if (vlog_kind(rhs) == V_PATTERN_EXPR) {
               mir_value_t tmp =
                  vlog_real_array_pattern(g, rhs, dims, ndims, eti);
               src = mir_build_array_ref(g->mu, tmp,
                                         mir_const(g->mu, t_offset, 0));
            }
            else {
               vlog_select_t sel = vlog_lower_select(g, rhs);
               src = sel.obj;
               if (mir_is_signal(g->mu, src))
                  src = mir_build_resolved(g->mu, src);
               src = mir_build_array_ref(g->mu, src, sel.dst_offset);
            }
            mir_value_t dst = mir_build_array_ref(g->mu, lvalue1.obj,
                                                  lvalue1.dst_offset);
            mir_value_t n = mir_const(g->mu, t_offset, count);
            if (mir_is_signal(g->mu, lvalue1.obj))
               mir_build_deposit_signal(g->mu, dst, n, src);
            else
               mir_build_copy(g->mu, dst, src, n);
            return;
         }
         else if (vlog_kind(rhs) == V_PATTERN_EXPR)
            value = vlog_lower_array_pattern(g, rhs, dims, ndims, eti);
         else if (mir_points_to_vector(g->mu, lvalue1.obj)) {
            vlog_node_t sdims[MAX_UNPACKED_DIMS];
            const int sn = vlog_unpacked_dims(rhs, sdims);
            if (sn > 0 && vlog_unpacked_count(sdims, sn) == count) {
               vlog_select_t src = vlog_lower_select(g, rhs);
               if (mir_points_to_vector(g->mu, src.obj)) {
                  mir_type_t t_offset = mir_offset_type(g->mu);
                  mir_build_copy(g->mu, lvalue1.obj, src.obj,
                                 mir_const(g->mu, t_offset, count));
                  return;
               }
            }
            value = vlog_lower_rvalue(g, rhs);

            // An array declared with a typedef is held as a dynamic array
            if (mir_get_class(g->mu, mir_get_type(g->mu, value))
                == MIR_TYPE_UARRAY)
               value = vlog_load_flat_array(
                  g, mir_build_unwrap(g->mu, value), count, eti, false);
         }

         if (!mir_is_null(value) && mir_points_to_vector(g->mu, lvalue1.obj)) {
            mir_type_t t_flat = mir_vec4_type(g->mu, count * eti->size, false);
            vlog_store_flat_array(g, lvalue1.obj, count, eti,
                                  vlog_lower_cast(g, t_flat, value));
            return;
         }
      }

      if (mir_is_null(value)) {
         mir_type_t context = vlog_assign_context(g, type, rhs);
         value = vlog_lower_with_context(g, rhs, context);

         // An array declared with a typedef is held as a dynamic array
         int tn;
         vlog_node_t tdims[MAX_UNPACKED_DIMS];
         if (vlog_kind(target) != V_CONCAT
             && mir_get_class(g->mu, mir_get_type(g->mu, value))
             == MIR_TYPE_UARRAY
             && (tn = vlog_unpacked_dims(target, tdims)) > 0)
            value = vlog_load_flat_array(
               g, mir_build_unwrap(g->mu, value),
               vlog_unpacked_count(tdims, tn),
               vlog_array_elem_info(g, target), false);
      }
   }

   vlog_node_t ttype = vlog_kind(target) == V_CONCAT
      ? NULL : vlog_get_type(target);
   if (ttype != NULL && rhs != NULL)
      vlog_check_virtual_types(ttype, vlog_get_type(rhs), vlog_loc(rhs));

   vlog_node_t rdims[MAX_UNPACKED_DIMS];
   if (ttype != NULL && rhs != NULL && vlog_kind(ttype) == V_DATA_TYPE
       && (vlog_subkind(ttype) == DT_DYNARRAY || vlog_subkind(ttype) == DT_QUEUE)
       && mir_is_vector(g->mu, value) && vlog_unpacked_dims(rhs, rdims) > 0)
      value = vlog_fixed_to_dynamic(g, ttype, rhs, value);
   else if (ttype != NULL && rhs != NULL && vlog_kind(ttype) == V_DATA_TYPE
            && (vlog_subkind(ttype) == DT_DYNARRAY
                || vlog_subkind(ttype) == DT_QUEUE)
            && !mir_is(g->mu, value, MIR_TYPE_UARRAY)
            && vlog_is_real_unpacked(g, rhs))
      value = vlog_real_unpacked_to_dynamic(g, rhs);

   if (ttype != NULL && vlog_is_dynamic_type(ttype)) {
      mir_value_t cast = vlog_lower_cast(g, type, value);
      if (copy && vlog_kind(rhs) != V_DYNAMIC_NEW && vlog_kind(rhs) != V_CONCAT
          && vlog_kind(rhs) != V_PATTERN_EXPR)
         cast = vlog_copy_value(g, cast, ttype);
      mir_build_store(g->mu, lvalues[0].obj, cast);
      return;
   }

   if (mir_is(g->mu, value, MIR_TYPE_UARRAY)) {
      mir_build_store(g->mu, lvalues[0].obj, value);
      return;
   }

   mir_value_t cast = vlog_lower_cast(g, type, value);

   if (mir_is_signal(g->mu, lvalues[0].obj)) {
      mir_value_t unpacked;
      if (mir_is_vector(g->mu, cast)) {
         mir_value_t tmp = MIR_NULL_VALUE;
         if (targetsz > 1) {
            mir_type_t t_elem = mir_logic_type(g->mu);
            mir_type_t t_array = mir_carray_type(g->mu, targetsz, t_elem);
            tmp = vlog_get_temp(g, t_array);
         }

         unpacked = mir_build_unpack(g->mu, cast, 0, tmp);
      }
      else
         unpacked = cast;

      mir_type_t t_offset = mir_offset_type(g->mu);

      for (int i = 0, offset = 0; i < nlvalues;
           offset += lvalues[i].size, i++) {
         mir_value_t nets = mir_build_array_ref(g->mu, lvalues[i].obj,
                                                lvalues[i].dst_offset);

         mir_block_t merge_bb = MIR_NULL_BLOCK;

         int64_t count_const;
         if (mir_get_const(g->mu, lvalues[i].count, &count_const)) {
            if (count_const == 0) {
               mir_comment(g->mu, "Out-of-range assignment");
               continue;
            }
         }
         else {
            mir_block_t guarded_bb = mir_add_block(g->mu);
            merge_bb = mir_add_block(g->mu);

            mir_value_t overlaps = mir_build_cmp(g->mu, MIR_CMP_NEQ,
                                                 lvalues[i].count,
                                                 mir_const(g->mu, t_offset, 0));
            mir_build_cond(g->mu, overlaps, guarded_bb, merge_bb);

            mir_set_cursor(g->mu, guarded_bb, MIR_APPEND);
         }

         mir_value_t pos = mir_build_add(g->mu, t_offset,
                                         mir_const(g->mu, t_offset, offset),
                                         lvalues[i].src_offset);
         mir_value_t src = mir_build_array_ref(g->mu, unpacked, pos);

         src = vlog_lvalue_source(g, &(lvalues[i]), src);

         mir_build_deposit_signal(g->mu, nets, lvalues[i].count, src);

         if (!mir_is_null(merge_bb)) {
            mir_build_jump(g->mu, merge_bb);
            mir_set_cursor(g->mu, merge_bb, MIR_APPEND);
         }
      }
   }
   else if (mir_points_to_vector(g->mu, lvalues[0].obj)) {
      mir_type_t t_offset = mir_offset_type(g->mu);

      for (int i = 0, offset = 0; i < nlvalues;
           offset += lvalues[i].size, i++) {
         mir_block_t merge_bb = MIR_NULL_BLOCK;

         mir_type_t part_type;
         int64_t count_const;
         if (mir_get_const(g->mu, lvalues[i].count, &count_const)) {
            if (count_const == 0) {
               mir_comment(g->mu, "Out-of-range assignment");
               continue;
            }

            part_type = mir_vector_slice(g->mu, lvalues[i].type, count_const);
         }
         else {
            mir_block_t guarded_bb = mir_add_block(g->mu);
            merge_bb = mir_add_block(g->mu);

            mir_value_t overlaps = mir_build_cmp(g->mu, MIR_CMP_NEQ,
                                                 lvalues[i].count,
                                                 mir_const(g->mu, t_offset, 0));
            mir_build_cond(g->mu, overlaps, guarded_bb, merge_bb);

            mir_set_cursor(g->mu, guarded_bb, MIR_APPEND);

            part_type = lvalues[i].type;
         }

         mir_value_t pos = mir_build_add(g->mu, t_offset,
                                         mir_const(g->mu, t_offset, offset),
                                         lvalues[i].src_offset);
         mir_type_t t_cast = mir_get_type(g->mu, cast);
         mir_value_t part = mir_build_extract(
            g->mu, mir_vector_slice(g->mu, t_cast,
                                    mir_get_size(g->mu, part_type)),
            cast, pos);
         if (mir_get_class(g->mu, t_cast) != mir_get_class(g->mu, part_type))
            part = mir_build_cast(g->mu, part_type, part);

         bool whole = vlog_kind(nodes[i]) == V_REF;
         if (vlog_kind(nodes[i]) == V_MEMBER_REF) {
            vlog_node_t ptype = vlog_get_type(vlog_value(nodes[i]));
            whole = ptype != NULL && vlog_is_dynamic_type(ptype);
         }

         if (whole)
            mir_build_store(g->mu, lvalues[i].obj, part);
         else {
            mir_value_t cur = mir_build_load(g->mu, lvalues[i].obj);
            mir_type_t t_cur = mir_get_type(g->mu, cur);
            mir_type_t t_part = mir_get_type(g->mu, part);
            if (mir_get_class(g->mu, t_cur) != mir_get_class(g->mu, t_part))
               part = mir_build_cast(
                  g->mu, mir_vector_slice(g->mu, t_cur,
                                          mir_get_size(g->mu, t_part)),
                  part);

            mir_value_t ins =
               mir_build_insert(g->mu, part, cur, lvalues[i].dst_offset);
            mir_build_store(g->mu, lvalues[i].obj, ins);
         }

         if (!mir_is_null(merge_bb)) {
            mir_build_jump(g->mu, merge_bb);
            mir_set_cursor(g->mu, merge_bb, MIR_APPEND);
         }
      }
   }
   else {
      assert(nlvalues == 1);

      int64_t count_const;
      if (mir_get_class(g->mu, mir_get_type(g->mu, cast)) == MIR_TYPE_REAL
          && !mir_get_const(g->mu, lvalues[0].count, &count_const)) {
         // An index out of range does not assign to an element of an array
         mir_type_t t_offset = mir_offset_type(g->mu);
         mir_block_t store_bb = mir_add_block(g->mu);
         mir_block_t done_bb = mir_add_block(g->mu);
         mir_build_cond(g->mu, mir_build_cmp(g->mu, MIR_CMP_NEQ,
                                             lvalues[0].count,
                                             mir_const(g->mu, t_offset, 0)),
                        store_bb, done_bb);

         mir_set_cursor(g->mu, store_bb, MIR_APPEND);
         mir_build_store(g->mu, lvalues[0].obj, cast);
         mir_build_jump(g->mu, done_bb);

         mir_set_cursor(g->mu, done_bb, MIR_APPEND);
      }
      else
         mir_build_store(g->mu, lvalues[0].obj, cast);
   }
}

static mir_value_t vlog_lower_unary(vlog_gen_t *g, vlog_node_t v,
                                    mir_type_t context)
{
   const vlog_unary_t op = vlog_subkind(v);
   const bool propagate_context =
      op == V_UNARY_BITNEG || op == V_UNARY_NEG || op == V_UNARY_IDENTITY;

   mir_value_t input;
   if (propagate_context)
      input = vlog_lower_with_context(g, vlog_value(v), context);
   else
      input = vlog_lower_rvalue(g, vlog_value(v));

   if (op == V_UNARY_IDENTITY)
      return input;

   if (mir_is_vector(g->mu, input)) {
      bool negate = false;
      mir_vec_op_t mop;
      switch (op) {
      case V_UNARY_BITNEG: mop = MIR_VEC_BIT_NOT; break;
      case V_UNARY_NOT:    mop = MIR_VEC_LOG_NOT; break;
      case V_UNARY_NEG:    mop = MIR_VEC_SUB; break;
      case V_UNARY_OR:     mop = MIR_VEC_BIT_OR; break;
      case V_UNARY_XOR:    mop = MIR_VEC_BIT_XOR; break;
      case V_UNARY_AND:    mop = MIR_VEC_BIT_AND; break;
      case V_UNARY_NAND:   mop = MIR_VEC_BIT_AND; negate = true; break;
      case V_UNARY_NOR:    mop = MIR_VEC_BIT_OR; negate = true; break;
      case V_UNARY_XNOR:   mop = MIR_VEC_BIT_XOR; negate = true; break;
      default:
         CANNOT_HANDLE(v);
      }

      mir_type_t itype = mir_get_type(g->mu, input);

      const mir_class_t class = mir_get_class(g->mu, itype);
      const bool is_signed = mir_get_signed(g->mu, itype);

      int size = mir_get_size(g->mu, itype);
      if (propagate_context && !mir_is_null(context))
         size = MAX(mir_get_size(g->mu, context), size);

      mir_type_t type;
      if (class == MIR_TYPE_VEC4)
         type = mir_vec4_type(g->mu, size, is_signed);
      else
         type = mir_vec2_type(g->mu, size, is_signed);

      mir_value_t cast = mir_build_cast(g->mu, type, input);
      mir_value_t result = mir_build_unary(g->mu, mop, type, cast);
      if (!negate)
         return result;

      mir_type_t otype = mir_get_type(g->mu, result);
      assert(mir_get_size(g->mu, otype) == 1);

      return mir_build_unary(g->mu, MIR_VEC_BIT_NOT, otype, result);
   }
   else {
      mir_type_t type = mir_get_type(g->mu, input);

      vlog_node_t vtype = vlog_get_type(vlog_value(v));
      if (op == V_UNARY_NOT && vtype != NULL
          && vlog_kind(vtype) == V_CLASS_DECL)
      {
         mir_value_t is_null = mir_build_cmp(g->mu, MIR_CMP_EQ, input,
                                             mir_build_null(g->mu, type));
         return vlog_lower_cast(g, mir_vec4_type(g->mu, 1, false), is_null);
      }

      switch (vlog_subkind(v)) {
      case V_UNARY_NEG:
         return mir_build_neg(g->mu, type, input);
      case V_UNARY_NOT:
         return mir_build_not(g->mu, vlog_lower_test(g, input));
      default:
         CANNOT_HANDLE(v);
      }
   }
}

static void vlog_matches_cb(vlog_node_t v, void *context)
{
   if (vlog_kind(v) == V_MATCHES)
      *(bool *)context = true;
}

static bool vlog_contains_matches(vlog_node_t v)
{
   // The right hand side of a guard uses the variables bound by a pattern
   // so it must not be evaluated if the pattern does not match

   bool found = false;
   vlog_visit(v, vlog_matches_cb, &found);
   return found;
}

static bool vlog_has_side_effects(vlog_node_t v)
{
   switch (vlog_kind(v)) {
   case V_BIT_SELECT:
      {
         // Reading an entry of an associative array which does not exist
         // is reported
         vlog_node_t type = vlog_get_type(vlog_value(v));
         return type != NULL && vlog_kind(type) == V_DATA_TYPE
            && vlog_subkind(type) == DT_ASSOC;
      }
   case V_REF:
   case V_PART_SELECT:
   case V_NUMBER:
      return false;
   case V_UNARY:
      return vlog_has_side_effects(vlog_value(v));
   case V_BINARY:
      return vlog_has_side_effects(vlog_left(v))
         || vlog_has_side_effects(vlog_right(v));
   default:
      return true;
   }
}

static mir_value_t vlog_cast_unsigned(vlog_gen_t *g, mir_value_t value)
{
   if (!mir_is_vector(g->mu, value))
      return value;

   mir_type_t type = mir_get_type(g->mu, value);
   if (!mir_get_signed(g->mu, type))
      return value;

   mir_type_t utype = mir_vec4_type(g->mu, mir_get_size(g->mu, type), false);
   return mir_build_cast(g->mu, utype, value);
}

static mir_value_t vlog_widen_vector(vlog_gen_t *g, mir_value_t value,
                                     mir_type_t context)
{
   if (mir_is_null(context))
      return value;
   else if (!mir_is_vector(g->mu, value))
      return value;

   mir_class_t class = mir_get_class(g->mu, context);
   if (class != MIR_TYPE_VEC2 && class != MIR_TYPE_VEC4)
      return value;

   mir_type_t type = mir_get_type(g->mu, value);

   const bool value_issigned = mir_get_signed(g->mu, type);
   const int value_size = mir_get_size(g->mu, type);

   const bool context_issigned = mir_get_signed(g->mu, context);
   const int context_size = mir_get_size(g->mu, context);

   if (context_size <= value_size && context_issigned == value_issigned)
      return value;

   const int size = MAX(context_size, value_size);
   mir_comment(g->mu, "Widen to %d from context", size);

   if (!context_issigned && value_issigned)
      value = vlog_cast_unsigned(g, value);

   mir_type_t wide;
   if (class == MIR_TYPE_VEC2)
      wide = mir_vec2_type(g->mu, size, context_issigned);
   else
      wide = mir_vec4_type(g->mu, size, context_issigned);

   return mir_build_cast(g->mu, wide, value);
}

static mir_value_t vlog_lower_vector_shift(vlog_gen_t *g, vlog_binary_t binop,
                                           mir_value_t left, mir_value_t right,
                                           mir_type_t context)
{
   mir_type_t ltype = mir_get_type(g->mu, left);
   mir_type_t rtype = mir_get_type(g->mu, right);

   const mir_class_t lclass = mir_get_class(g->mu, ltype);
   const mir_class_t rclass = mir_get_class(g->mu, rtype);

   const int lsize = mir_get_size(g->mu, ltype);
   const int rsize = mir_get_size(g->mu, rtype);

   const bool lsigned = mir_get_signed(g->mu, ltype);

   mir_type_t type;
   if (lclass == MIR_TYPE_VEC4 || rclass == MIR_TYPE_VEC4)
      type = mir_vec4_type(g->mu, lsize, lsigned);
   else
      type = mir_vec2_type(g->mu, lsize, lsigned);

   mir_type_t urtype = mir_vec4_type(g->mu, rsize, false);
   mir_value_t zext = mir_build_cast(g->mu, urtype, right);

   if (rsize > lsize) {
      // Handle large shifts that cannot be represented in LHS type
      mir_type_t t_uint32 = mir_vec2_type(g->mu, 32, false);
      mir_value_t limit = mir_const_vec(g->mu, t_uint32, lsize, 0);
      mir_value_t cast = mir_build_cast(g->mu, urtype, limit);
      mir_value_t geq = mir_build_binary(g->mu, MIR_VEC_GEQ, urtype,
                                         zext, cast);
      zext = mir_build_select(g->mu, urtype, vlog_lower_test(g, geq),
                              cast, zext);
   }

   mir_value_t lcast = mir_build_cast(g->mu, type, left);
   mir_value_t rcast = mir_build_cast(g->mu, type, zext);

   mir_vec_op_t mop;
   switch (binop) {
   case V_BINARY_SHIFT_LL:
   case V_BINARY_SHIFT_LA: mop = MIR_VEC_SLL; break;
   case V_BINARY_SHIFT_RL: mop = MIR_VEC_SRL; break;
   case V_BINARY_SHIFT_RA: mop = lsigned ? MIR_VEC_SRA : MIR_VEC_SRL; break;
   case V_BINARY_EXP:      mop = MIR_VEC_EXP; break;
   default:                should_not_reach_here();
   }

   return mir_build_binary(g->mu, mop, type, lcast, rcast);
}

static mir_value_t vlog_lower_vector_binary(vlog_gen_t *g, vlog_binary_t binop,
                                            mir_value_t left, mir_value_t right,
                                            mir_type_t context)
{
   mir_type_t ltype = mir_get_type(g->mu, left);
   mir_type_t rtype = mir_get_type(g->mu, right);

   const mir_class_t lclass = mir_get_class(g->mu, ltype);
   const mir_class_t rclass = mir_get_class(g->mu, rtype);

   const int lsize = mir_get_size(g->mu, ltype);
   const int rsize = mir_get_size(g->mu, rtype);

   const bool is_exp = binop == V_BINARY_EXP;

   const bool lsigned = mir_get_signed(g->mu, ltype);
   const bool rsigned = mir_get_signed(g->mu, rtype);

   bool is_signed = lsigned && (is_exp || rsigned);

   int size = MAX(lsize, rsize);
   if (!mir_is_null(context)) {
      mir_class_t class = mir_get_class(g->mu, context);
      if (class == MIR_TYPE_VEC2 || class == MIR_TYPE_VEC4)
         size = MAX(mir_get_size(g->mu, context), size);
   }

   // Division by zero is unknown even if both operands are two-state
   const bool may_be_unknown = binop == V_BINARY_DIVIDE || binop == V_BINARY_MOD;

   mir_type_t type;
   if (lclass == MIR_TYPE_VEC4 || rclass == MIR_TYPE_VEC4 || may_be_unknown)
      type = mir_vec4_type(g->mu, size, is_signed);
   else
      type = mir_vec2_type(g->mu, size, is_signed);

   const bool unsigned_numeric =
      binop == V_BINARY_PLUS || binop == V_BINARY_MINUS
      || binop == V_BINARY_TIMES || binop == V_BINARY_DIVIDE
      || binop == V_BINARY_MOD
      || binop == V_BINARY_LT || binop == V_BINARY_LEQ
      || binop == V_BINARY_GT || binop == V_BINARY_GEQ
      || binop == V_BINARY_LOG_EQ || binop == V_BINARY_LOG_NEQ;

   if (unsigned_numeric && !is_signed) {
      // Mixed signedness binary expressions are evaluated as unsigned, so
      // signed operands must not be sign-extended when resized
      left = vlog_cast_unsigned(g, left);
      right = vlog_cast_unsigned(g, right);
   }

   mir_value_t lcast = mir_build_cast(g->mu, type, left);
   mir_value_t rcast = mir_build_cast(g->mu, type, right);

   bool negate = false;
   mir_vec_op_t mop;
   switch (binop) {
   case V_BINARY_AND:      mop = MIR_VEC_BIT_AND; break;
   case V_BINARY_OR:       mop = MIR_VEC_BIT_OR; break;
   case V_BINARY_XOR:      mop = MIR_VEC_BIT_XOR; break;
   case V_BINARY_XNOR:     mop = MIR_VEC_BIT_XOR; negate = true; break;
   case V_BINARY_NAND:     mop = MIR_VEC_BIT_AND; negate = true; break;
   case V_BINARY_NOR:      mop = MIR_VEC_BIT_OR; negate = true; break;
   case V_BINARY_LOG_AND:  mop = MIR_VEC_LOG_AND; break;
   case V_BINARY_LOG_OR:   mop = MIR_VEC_LOG_OR; break;
   case V_BINARY_LT:       mop = MIR_VEC_LT; break;
   case V_BINARY_LEQ:      mop = MIR_VEC_LEQ; break;
   case V_BINARY_GT:       mop = MIR_VEC_GT; break;
   case V_BINARY_GEQ:      mop = MIR_VEC_GEQ; break;
   case V_BINARY_LOG_EQ:   mop = MIR_VEC_LOG_EQ; break;
   case V_BINARY_LOG_NEQ:  mop = MIR_VEC_LOG_NEQ; break;
   case V_BINARY_CASE_EQ:  mop = MIR_VEC_CASE_EQ; break;
   case V_BINARY_CASE_NEQ: mop = MIR_VEC_CASE_NEQ; break;
   case V_BINARY_PLUS:     mop = MIR_VEC_ADD; break;
   case V_BINARY_MINUS:    mop = MIR_VEC_SUB; break;
   case V_BINARY_TIMES:    mop = MIR_VEC_MUL; break;
   case V_BINARY_DIVIDE:   mop = MIR_VEC_DIV; break;
   case V_BINARY_MOD:      mop = MIR_VEC_MOD; break;
   case V_BINARY_EXP:      mop = MIR_VEC_EXP; break;
   default:                should_not_reach_here();
   }

   mir_value_t result = mir_build_binary(g->mu, mop, type, lcast, rcast);
   if (negate) {
      mir_type_t otype = mir_get_type(g->mu, result);
      return mir_build_unary(g->mu, MIR_VEC_BIT_NOT, otype, result);
   }

   return result;
}

static mir_value_t vlog_lower_array_compare(vlog_gen_t *g, vlog_node_t v,
                                            vlog_node_t type)
{
   // Equal if the sizes match and each pair of elements is identical

   const type_info_t *ti = vlog_type_info(g, type);
   const type_info_t *eti = vlog_type_info(g, vlog_type(type));

   const mir_class_t eclass = mir_get_class(g->mu, eti->type);
   if (eclass != MIR_TYPE_VEC2 && eclass != MIR_TYPE_VEC4
       && eclass != MIR_TYPE_REAL && eclass != MIR_TYPE_CONTEXT
       && eclass != MIR_TYPE_UARRAY)
      fatal_at(vlog_loc(v), "sorry, only arrays of integral, real, string or "
               "class handle types can be compared");

   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_type_t t_bool = mir_bool_type(g->mu);
   mir_type_t t_bit = mir_vec2_type(g->mu, 1, false);

   mir_value_t l = vlog_lower_with_context(g, vlog_left(v), ti->type);
   mir_value_t r = vlog_lower_with_context(g, vlog_right(v), ti->type);
   mir_value_t len = mir_build_uarray_len(g->mu, l, 0);

   mir_value_t result = mir_add_var(g->mu, t_bool, MIR_NULL_STAMP,
                                    ident_uniq("eq"), MIR_VAR_TEMP);
   mir_value_t index = mir_add_var(g->mu, t_offset, MIR_NULL_STAMP,
                                   ident_uniq("i"), MIR_VAR_TEMP);

   mir_build_store(g->mu, result, mir_const(g->mu, t_bool, 0));
   mir_build_store(g->mu, index, mir_const(g->mu, t_offset, 0));

   mir_block_t test_bb = mir_add_block(g->mu);
   mir_block_t body_bb = mir_add_block(g->mu);
   mir_block_t next_bb = mir_add_block(g->mu);
   mir_block_t same_bb = mir_add_block(g->mu);
   mir_block_t exit_bb = mir_add_block(g->mu);

   mir_value_t same_size =
      mir_build_cmp(g->mu, MIR_CMP_EQ, len, mir_build_uarray_len(g->mu, r, 0));
   mir_build_cond(g->mu, same_size, test_bb, exit_bb);

   mir_set_cursor(g->mu, test_bb, MIR_APPEND);

   mir_value_t i = mir_build_load(g->mu, index);
   mir_build_cond(g->mu, mir_build_cmp(g->mu, MIR_CMP_LT, i, len), body_bb,
                  same_bb);

   mir_set_cursor(g->mu, body_bb, MIR_APPEND);

   mir_value_t lelem = mir_build_load(
      g->mu, mir_build_array_ref(g->mu, mir_build_unwrap(g->mu, l), i));
   mir_value_t relem = mir_build_load(
      g->mu, mir_build_array_ref(g->mu, mir_build_unwrap(g->mu, r), i));
   mir_value_t same;
   switch (eclass) {
   case MIR_TYPE_REAL:
      same = mir_build_cmp(g->mu, MIR_CMP_EQ, lelem, relem);
      break;
   case MIR_TYPE_CONTEXT:
      same = mir_build_cmp(g->mu, MIR_CMP_EQ, lelem,
                           mir_build_cast(g->mu, eti->type, relem));
      break;
   case MIR_TYPE_UARRAY:
      {
         mir_value_t zero = mir_const(g->mu, t_offset, 0);
         mir_value_t args[] = { lelem, relem, zero };
         mir_value_t sc = vlog_str_call(g, "__sv_str_cmp", t_offset, args, 3);
         same = mir_build_cmp(g->mu, MIR_CMP_EQ, sc, zero);
      }
      break;
   default:
      same = mir_build_test(
         g->mu, mir_build_binary(g->mu, MIR_VEC_CASE_EQ, eti->type, lelem,
                                 relem));
      break;
   }

   mir_build_cond(g->mu, same, next_bb, exit_bb);

   mir_set_cursor(g->mu, next_bb, MIR_APPEND);

   mir_build_store(g->mu, index,
                   mir_build_add(g->mu, t_offset, i,
                                 mir_const(g->mu, t_offset, 1)));
   mir_build_jump(g->mu, test_bb);

   mir_set_cursor(g->mu, same_bb, MIR_APPEND);

   mir_build_store(g->mu, result, mir_const(g->mu, t_bool, 1));
   mir_build_jump(g->mu, exit_bb);

   mir_set_cursor(g->mu, exit_bb, MIR_APPEND);

   const vlog_binary_t op = vlog_subkind(v);
   const int want = op == V_BINARY_LOG_EQ || op == V_BINARY_CASE_EQ;
   mir_value_t test = mir_build_cmp(g->mu, MIR_CMP_EQ,
                                    mir_build_load(g->mu, result),
                                    mir_const(g->mu, t_bool, want));
   return mir_build_select(g->mu, t_bit, test,
                           mir_const_vec(g->mu, t_bit, 1, 0),
                           mir_const_vec(g->mu, t_bit, 0, 0));
}

typedef struct {
   mir_value_t diff;      // Some member is known to differ
   mir_value_t unknown;   // Some member compares as x
} vlog_cmp_t;

static vlog_cmp_t vlog_struct_compare(vlog_gen_t *g, vlog_node_t v,
                                      vlog_node_t type, mir_value_t left,
                                      mir_value_t right, bool casecmp);

static vlog_cmp_t vlog_cmp_join(vlog_gen_t *g, vlog_cmp_t a, vlog_cmp_t b)
{
   if (mir_is_null(a.diff))
      return b;

   return (vlog_cmp_t){
      .diff = mir_build_or(g->mu, a.diff, b.diff),
      .unknown = mir_build_or(g->mu, a.unknown, b.unknown),
   };
}

static vlog_cmp_t vlog_member_compare(vlog_gen_t *g, vlog_node_t v,
                                      vlog_node_t ft, mir_value_t lf,
                                      mir_value_t rf, bool casecmp)
{
   const type_info_t *fti = vlog_type_info(g, ft);
   mir_type_t t_bool = mir_bool_type(g->mu);

   switch (mir_get_class(g->mu, fti->type)) {
   case MIR_TYPE_VEC2:
   case MIR_TYPE_VEC4:
      if (casecmp) {
         mir_value_t eq = mir_build_test(
            g->mu, mir_build_binary(g->mu, MIR_VEC_CASE_EQ, fti->type,
                                    lf, rf));
         return (vlog_cmp_t){
            .diff = mir_build_not(g->mu, eq),
            .unknown = mir_const(g->mu, t_bool, 0),
         };
      }
      else {
         mir_type_t t_logic = mir_vec4_type(g->mu, 1, false);
         mir_value_t eq =
            mir_build_binary(g->mu, MIR_VEC_LOG_EQ, fti->type, lf, rf);
         mir_value_t zero = mir_const_vec(g->mu, t_logic, 0, 0);
         mir_value_t x = mir_const_vec(g->mu, t_logic, 1, 1);
         return (vlog_cmp_t){
            .diff = mir_build_test(
               g->mu, mir_build_binary(g->mu, MIR_VEC_CASE_EQ, t_logic,
                                       eq, zero)),
            .unknown = mir_build_test(
               g->mu, mir_build_binary(g->mu, MIR_VEC_CASE_EQ, t_logic,
                                       eq, x)),
         };
      }
   case MIR_TYPE_REAL:
      // A NaN member is unequal to everything
      return (vlog_cmp_t){
         .diff = mir_build_not(g->mu, mir_build_cmp(g->mu, MIR_CMP_EQ,
                                                    lf, rf)),
         .unknown = mir_const(g->mu, t_bool, 0),
      };
   default:
      {
         vlog_node_t sub = vlog_get_type(ft);
         if (mir_get_class(g->mu, fti->type) == MIR_TYPE_ACCESS
             && sub != NULL && (vlog_kind(sub) == V_STRUCT_DECL
                                || vlog_kind(sub) == V_UNION_DECL))
            return vlog_struct_compare(g, v, sub, lf, rf, casecmp);

         fatal_at(vlog_loc(v), "sorry, only members of integral, real, "
                  "and structure types can be compared");
      }
   }
}

static vlog_cmp_t vlog_array_member_compare(vlog_gen_t *g, vlog_node_t v,
                                            vlog_node_t d, mir_value_t lptr,
                                            mir_value_t rptr, bool casecmp)
{
   // The elements of a member which is a fixed size array

   const unsigned count = vlog_size(d);
   if (count > 256)
      fatal_at(vlog_loc(v), "sorry, a member with more than 256 elements "
               "cannot be compared");

   mir_type_t t_offset = mir_offset_type(g->mu);

   vlog_cmp_t result = { MIR_NULL_VALUE, MIR_NULL_VALUE };
   for (unsigned k = 0; k < count; k++) {
      mir_value_t idx = mir_const(g->mu, t_offset, k);
      mir_value_t lf = mir_build_load(g->mu, mir_build_array_ref(g->mu, lptr,
                                                                idx));
      mir_value_t rf = mir_build_load(g->mu, mir_build_array_ref(g->mu, rptr,
                                                                idx));

      vlog_cmp_t cmp = vlog_member_compare(g, v, vlog_type(d), lf, rf,
                                           casecmp);
      result = vlog_cmp_join(g, result, cmp);
   }

   return result;
}

static vlog_cmp_t vlog_record_member_compare(vlog_gen_t *g, vlog_node_t v,
                                             vlog_node_t d, mir_value_t lrec,
                                             mir_value_t rrec, int i,
                                             bool casecmp)
{
   if (vlog_is_fixed_unpacked(d) && vlog_size(d) > 1)
      return vlog_array_member_compare(
         g, v, d, mir_build_record_ref(g->mu, lrec, i),
         mir_build_record_ref(g->mu, rrec, i), casecmp);

   mir_value_t lf = mir_build_load(g->mu, mir_build_record_ref(g->mu, lrec, i));
   mir_value_t rf = mir_build_load(g->mu, mir_build_record_ref(g->mu, rrec, i));
   return vlog_member_compare(g, v, vlog_type(d), lf, rf, casecmp);
}

static vlog_cmp_t vlog_tagged_compare(vlog_gen_t *g, vlog_node_t v,
                                      vlog_node_t type, mir_value_t lrec,
                                      mir_value_t rrec, bool casecmp)
{
   // Two tagged unions are equal if they have the same tag and the member
   // it selects is equal

   mir_type_t t_offset = mir_offset_type(g->mu);

   const int tag = vlog_decls(type) - 1;
   mir_value_t ltag = mir_build_load(g->mu, mir_build_record_ref(g->mu, lrec, tag));
   mir_value_t rtag = mir_build_load(g->mu, mir_build_record_ref(g->mu, rrec, tag));
   mir_value_t ltagn = mir_build_cast(g->mu, t_offset, ltag);

   mir_value_t tagdiff = mir_build_cmp(g->mu, MIR_CMP_NEQ, ltagn,
                                       mir_build_cast(g->mu, t_offset, rtag));

   vlog_cmp_t result = {
      .diff = tagdiff,
      .unknown = mir_const(g->mu, mir_bool_type(g->mu), 0),
   };

   for (int i = 0; i < tag; i++) {
      vlog_node_t d = vlog_decl(type, i);
      vlog_cmp_t cmp =
         vlog_record_member_compare(g, v, d, lrec, rrec, i, casecmp);

      mir_value_t this = mir_build_cmp(g->mu, MIR_CMP_EQ, ltagn,
                                       mir_const(g->mu, t_offset, i));
      cmp.diff = mir_build_and(g->mu, this, cmp.diff);
      cmp.unknown = mir_build_and(g->mu, this, cmp.unknown);
      result = vlog_cmp_join(g, result, cmp);
   }

   result.unknown = mir_build_and(g->mu, result.unknown,
                                  mir_build_not(g->mu, tagdiff));
   return result;
}

static vlog_cmp_t vlog_struct_compare(vlog_gen_t *g, vlog_node_t v,
                                      vlog_node_t type, mir_value_t left,
                                      mir_value_t right, bool casecmp)
{
   // Members are compared logically so the result is unknown when no
   // member is known to differ but some member compares as x (11.4.5)

   mir_value_t lrec = mir_build_all(g->mu, left);
   mir_value_t rrec = mir_build_all(g->mu, right);

   if (vlog_kind(type) == V_UNION_DECL)
      return vlog_tagged_compare(g, v, type, lrec, rrec, casecmp);

   vlog_cmp_t result = { MIR_NULL_VALUE, MIR_NULL_VALUE };

   const int ndecls = vlog_decls(type);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(type, i);
      vlog_cmp_t cmp =
         vlog_record_member_compare(g, v, d, lrec, rrec, i, casecmp);
      result = vlog_cmp_join(g, result, cmp);
   }

   if (mir_is_null(result.diff)) {
      mir_value_t f = mir_const(g->mu, mir_bool_type(g->mu), 0);
      result = (vlog_cmp_t){ f, f };
   }

   return result;
}

static mir_value_t vlog_lower_struct_compare(vlog_gen_t *g, vlog_node_t v,
                                             vlog_node_t type)
{
   const type_info_t *ti = vlog_type_info(g, type);

   mir_value_t l = vlog_lower_with_context(g, vlog_left(v), ti->type);
   mir_value_t r = vlog_lower_with_context(g, vlog_right(v), ti->type);

   const vlog_binary_t op = vlog_subkind(v);
   const bool ne = op == V_BINARY_LOG_NEQ || op == V_BINARY_CASE_NEQ;

   const bool casecmp = op == V_BINARY_CASE_EQ || op == V_BINARY_CASE_NEQ;
   vlog_cmp_t cmp = vlog_struct_compare(g, v, type, l, r, casecmp);

   if (casecmp) {
      mir_type_t t_bit = mir_vec2_type(g->mu, 1, false);
      return mir_build_select(g->mu, t_bit, cmp.diff,
                              mir_const_vec(g->mu, t_bit, ne, 0),
                              mir_const_vec(g->mu, t_bit, !ne, 0));
   }

   // The result of != is the logical negation of == (11.4.5)
   mir_type_t t_logic = mir_vec4_type(g->mu, 1, false);
   mir_value_t known =
      mir_build_select(g->mu, t_logic, cmp.unknown,
                       mir_const_vec(g->mu, t_logic, 1, 1),
                       mir_const_vec(g->mu, t_logic, !ne, 0));
   return mir_build_select(g->mu, t_logic, cmp.diff,
                           mir_const_vec(g->mu, t_logic, ne, 0), known);
}

static mir_value_t vlog_wildcard_bits(vlog_gen_t *g, mir_value_t value,
                                      unsigned width)
{
   mir_type_t t_logic = mir_logic_type(g->mu);
   mir_type_t t_array = mir_carray_type(g->mu, width, t_logic);
   mir_value_t tmp = mir_add_var(g->mu, t_array, MIR_NULL_STAMP,
                                 ident_uniq("wild"), MIR_VAR_TEMP);

   mir_value_t bits = mir_build_unpack(g->mu, value, 0, tmp);
   if (!mir_is(g->mu, bits, MIR_TYPE_POINTER)) {
      mir_build_store(g->mu, mir_build_array_ref(
                         g->mu, tmp, mir_const(g->mu, mir_offset_type(g->mu),
                                               0)), bits);
      bits = mir_build_array_ref(g->mu, tmp,
                                 mir_const(g->mu, mir_offset_type(g->mu), 0));
   }

   return bits;
}

static mir_value_t vlog_lower_wildcard_eq(vlog_gen_t *g, vlog_node_t v)
{
   // The result is one if every bit which is not x or z in the right operand
   // matches the left, zero if one differs and x if the left has an x or z
   // in a bit which must match

   vlog_node_t lexpr = vlog_left(v), rexpr = vlog_right(v);
   const unsigned width = MAX(vlog_width(lexpr), vlog_width(rexpr));
   mir_type_t type = mir_vec4_type(g->mu, width, false);

   mir_value_t l = vlog_lower_cast(g, type, vlog_lower_with_context(
                                      g, lexpr, type));
   mir_value_t r = vlog_lower_cast(g, type, vlog_lower_with_context(
                                      g, rexpr, type));

   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_value_t args[] = {
      vlog_wildcard_bits(g, l, width),
      vlog_wildcard_bits(g, r, width),
      mir_const(g->mu, t_offset, width),
   };
   mir_value_t res = mir_build_fcall(g->mu, ident_new("__sv_wildcard_eq"),
                                     t_offset, MIR_NULL_STAMP, args, 3);
   mir_build_consume(g->mu, res);

   const bool ne = vlog_subkind(v) == V_BINARY_WILD_NEQ;

   mir_type_t t_bit = mir_vec4_type(g->mu, 1, false);
   mir_value_t zero = mir_const(g->mu, t_offset, 0);
   mir_value_t one = mir_const(g->mu, t_offset, 1);
   mir_value_t yes = mir_const_vec(g->mu, t_bit, !ne, 0);
   mir_value_t no = mir_const_vec(g->mu, t_bit, ne, 0);
   mir_value_t unknown = mir_const_vec(g->mu, t_bit, 1, 1);

   mir_value_t sel = mir_build_select(
      g->mu, t_bit, mir_build_cmp(g->mu, MIR_CMP_EQ, res, one), yes,
      mir_build_select(g->mu, t_bit,
                       mir_build_cmp(g->mu, MIR_CMP_EQ, res, zero), no,
                       unknown));
   return sel;
}

static mir_value_t vlog_lower_binary(vlog_gen_t *g, vlog_node_t v,
                                     mir_type_t context)
{
   if (vlog_subkind(v) == V_BINARY_WILD_EQ
       || vlog_subkind(v) == V_BINARY_WILD_NEQ)
      return vlog_widen_vector(g, vlog_lower_wildcard_eq(g, v), context);

   // A string literal compared with an integral value is a number
   vlog_node_t lexpr = vlog_left(v), rexpr = vlog_right(v);
   const bool lstr = vlog_is_string(lexpr) && vlog_kind(lexpr) != V_STRING;
   const bool rstr = vlog_is_string(rexpr) && vlog_kind(rexpr) != V_STRING;
   if ((lstr && (rstr || vlog_kind(rexpr) == V_STRING))
       || (rstr && vlog_kind(lexpr) == V_STRING)) {
      const vlog_binary_t op = vlog_subkind(v);
      if (op == V_BINARY_LT || op == V_BINARY_LEQ || op == V_BINARY_GT
          || op == V_BINARY_GEQ) {
         mir_type_t t_str = mir_uarray_type(g->mu, 1, mir_char_type(g->mu));
         mir_type_t t_offset = mir_offset_type(g->mu);
         mir_value_t l = vlog_lower_with_context(g, vlog_left(v), t_str);
         mir_value_t r = vlog_lower_with_context(g, vlog_right(v), t_str);
         mir_value_t zero = mir_const(g->mu, t_offset, 0);
         mir_value_t args[] = { l, r, zero };
         mir_value_t sc = vlog_str_call(g, "__sv_str_cmp", t_offset, args, 3);

         mir_cmp_t cmp = MIR_CMP_LT;
         if (op == V_BINARY_LEQ)
            cmp = MIR_CMP_LEQ;
         else if (op == V_BINARY_GT)
            cmp = MIR_CMP_GT;
         else if (op == V_BINARY_GEQ)
            cmp = MIR_CMP_GEQ;

         mir_type_t t_bit = mir_vec2_type(g->mu, 1, false);
         mir_value_t sel =
            mir_build_select(g->mu, t_bit, mir_build_cmp(g->mu, cmp, sc, zero),
                             mir_const_vec(g->mu, t_bit, 1, 0),
                             mir_const_vec(g->mu, t_bit, 0, 0));
         return vlog_widen_vector(g, sel, context);
      }
      else if (op == V_BINARY_LOG_EQ || op == V_BINARY_LOG_NEQ
          || op == V_BINARY_CASE_EQ || op == V_BINARY_CASE_NEQ) {
         mir_type_t t_str = mir_uarray_type(g->mu, 1, mir_char_type(g->mu));
         mir_type_t t_offset = mir_offset_type(g->mu);
         mir_value_t l = vlog_lower_with_context(g, vlog_left(v), t_str);
         mir_value_t r = vlog_lower_with_context(g, vlog_right(v), t_str);
         mir_value_t args[] = { l, r };
         mir_value_t eq = mir_build_fcall(g->mu, ident_new("__sv_str_eq"),
                                          t_offset, MIR_NULL_STAMP, args, 2);
         mir_build_consume(g->mu, eq);

         mir_type_t t_bit = mir_vec2_type(g->mu, 1, false);
         mir_value_t want = mir_const(g->mu, t_offset,
                                      op == V_BINARY_LOG_EQ
                                      || op == V_BINARY_CASE_EQ);
         mir_value_t test = mir_build_cmp(g->mu, MIR_CMP_EQ, eq, want);
         mir_value_t sel =
            mir_build_select(g->mu, t_bit, test,
                             mir_const_vec(g->mu, t_bit, 1, 0),
                             mir_const_vec(g->mu, t_bit, 0, 0));
         return vlog_widen_vector(g, sel, context);
      }
   }

   vlog_node_t ltype = vlog_get_type(vlog_left(v));
   if (ltype != NULL && (vlog_kind(ltype) == V_STRUCT_DECL
                         || (vlog_kind(ltype) == V_UNION_DECL
                             && (vlog_flags(ltype) & VLOG_F_TAGGED)))
       && !(vlog_flags(ltype) & VLOG_F_PACKED)) {
      const vlog_binary_t sop = vlog_subkind(v);
      if (sop == V_BINARY_LOG_EQ || sop == V_BINARY_LOG_NEQ
          || sop == V_BINARY_CASE_EQ || sop == V_BINARY_CASE_NEQ)
         return vlog_widen_vector(g, vlog_lower_struct_compare(g, v, ltype),
                                  context);
   }

   if (ltype != NULL && vlog_kind(ltype) == V_DATA_TYPE
       && (vlog_subkind(ltype) == DT_DYNARRAY
           || vlog_subkind(ltype) == DT_QUEUE)) {
      const vlog_binary_t aop = vlog_subkind(v);
      if (aop != V_BINARY_LOG_EQ && aop != V_BINARY_LOG_NEQ
          && aop != V_BINARY_CASE_EQ && aop != V_BINARY_CASE_NEQ)
         fatal_at(vlog_loc(v), "sorry, this operator is not supported for "
                  "arrays");
      return vlog_widen_vector(g, vlog_lower_array_compare(g, v, ltype),
                               context);
   }

   const vlog_binary_t op = vlog_subkind(v);
   const bool propagate_context =
      op == V_BINARY_AND || op == V_BINARY_OR || op == V_BINARY_XOR
      || op == V_BINARY_XNOR || op == V_BINARY_NAND || op == V_BINARY_NOR
      || op == V_BINARY_PLUS || op == V_BINARY_MINUS || op == V_BINARY_TIMES
      || op == V_BINARY_DIVIDE || op == V_BINARY_MOD || op == V_BINARY_EXP;
   const bool rhs_self_determined =
      op == V_BINARY_SHIFT_LA || op == V_BINARY_SHIFT_LL
      || op == V_BINARY_SHIFT_RA || op == V_BINARY_SHIFT_RL;

   vlog_node_t lhs_expr = vlog_left(v), rhs_expr = vlog_right(v);

   const bool is_logical = op == V_BINARY_LOG_AND || op == V_BINARY_LOG_OR;

   mir_type_t type = context;
   if (!rhs_self_determined) {
      int size = MAX(vlog_width(lhs_expr), vlog_width(rhs_expr));
      // The operands of a relational or equality operator are signed
      // only if both are, although its result is unsigned
      bool issigned = propagate_context ? vlog_is_signed(v)
         : vlog_is_signed(lhs_expr) && vlog_is_signed(rhs_expr);
      if (propagate_context && !mir_is_null(context)) {
         mir_class_t class = mir_get_class(g->mu, context);
         if (class == MIR_TYPE_VEC2 || class == MIR_TYPE_VEC4) {
            size = MAX(size, mir_get_size(g->mu, context));
            issigned &= mir_get_signed(g->mu, context);
         }
      }

      type = mir_vec4_type(g->mu, size, issigned);
   }

   // The type of null comes from the other operand
   mir_value_t left;
   if (vlog_kind(lhs_expr) == V_NULL && vlog_kind(rhs_expr) != V_NULL) {
      mir_type_t t_other = vlog_type_info(g, vlog_get_type(rhs_expr))->type;
      left = mir_build_null(g->mu, t_other);
   }
   else if (is_logical && !mir_is_null(type)) {
      // The operands of a logical operator are self-determined
      left = vlog_lower_rvalue(g, lhs_expr);
      if (mir_is_vector(g->mu, left))
         left = vlog_lower_cast(g, type, left);
   }
   else
      left = vlog_lower_with_context(g, lhs_expr, type);

   // For short-circuiting operators check if the RHS can have side effects
   mir_block_t guard_bb = MIR_NULL_BLOCK, skip_bb = MIR_NULL_BLOCK;
   mir_value_t var = MIR_NULL_VALUE;
   mir_type_t t_logic = MIR_NULL_TYPE;
   const bool is_short_circuit =
      op == V_BINARY_LOG_AND || op == V_BINARY_LOG_OR;

   if (is_short_circuit && (vlog_has_side_effects(rhs_expr)
                            || vlog_contains_matches(lhs_expr))) {
      guard_bb = mir_add_block(g->mu);
      skip_bb = mir_add_block(g->mu);

      // TODO: use a phi
      t_logic = mir_vec4_type(g->mu, 1, false);
      var = mir_add_var(g->mu, t_logic, MIR_NULL_STAMP,
                        ident_uniq("shortcircuit"), MIR_VAR_TEMP);

      mir_value_t def = mir_const_vec(g->mu, t_logic, op == V_BINARY_LOG_OR, 0);
      mir_build_store(g->mu, var, def);

      mir_value_t test = vlog_lower_test(g, left);
      if (op == V_BINARY_LOG_OR)
         mir_build_cond(g->mu, test, skip_bb, guard_bb);
      else
         mir_build_cond(g->mu, test, guard_bb, skip_bb);

      mir_set_cursor(g->mu, guard_bb, MIR_APPEND);
   }

   mir_value_t right;
   if (rhs_self_determined)
      right = vlog_lower_rvalue(g, rhs_expr);
   else if (vlog_kind(rhs_expr) == V_NULL && !mir_is_vector(g->mu, left))
      right = mir_build_null(g->mu, mir_get_type(g->mu, left));
   else if (is_logical && !mir_is_null(type)) {
      right = vlog_lower_rvalue(g, rhs_expr);
      if (mir_is_vector(g->mu, right))
         right = vlog_lower_cast(g, type, right);
   }
   else
      right = vlog_lower_with_context(g, rhs_expr, type);

   mir_value_t result;
   if (mir_is_vector(g->mu, left) && mir_is_vector(g->mu, right)) {
      switch (op) {
      case V_BINARY_SHIFT_LL:
      case V_BINARY_SHIFT_LA:
      case V_BINARY_SHIFT_RL:
      case V_BINARY_SHIFT_RA:
         result = vlog_lower_vector_shift(g, op, left, right, context);
         break;
      default:
         result = vlog_lower_vector_binary(g, op, left, right, context);
         break;
      }
   }
   else {
      mir_type_t ltype = mir_get_type(g->mu, left);
      mir_type_t rtype = mir_get_type(g->mu, right);

      if (mir_get_class(g->mu, ltype) == MIR_TYPE_REAL)
         right = vlog_lower_cast(g, (rtype = ltype), right);
      else if (mir_get_class(g->mu, rtype) == MIR_TYPE_REAL)
         left = vlog_lower_cast(g, (ltype = rtype), left);
      else if (mir_get_class(g->mu, ltype) == MIR_TYPE_CONTEXT
               && mir_get_class(g->mu, rtype) == MIR_TYPE_CONTEXT)
         right = mir_build_cast(g->mu, (rtype = ltype), right);   // Handles

      switch (op) {
      case V_BINARY_LOG_EQ:
      case V_BINARY_CASE_EQ:
         result = mir_build_cmp(g->mu, MIR_CMP_EQ, left, right);
         break;
      case V_BINARY_LOG_NEQ:
      case V_BINARY_CASE_NEQ:
         // A NaN is unequal to everything including itself
         if (mir_get_class(g->mu, ltype) == MIR_TYPE_REAL)
            result = mir_build_not(g->mu, mir_build_cmp(g->mu, MIR_CMP_EQ,
                                                        left, right));
         else
            result = mir_build_cmp(g->mu, MIR_CMP_NEQ, left, right);
         break;
      case V_BINARY_LT:
         result = mir_build_cmp(g->mu, MIR_CMP_LT, left, right);
         break;
      case V_BINARY_LEQ:
         result = mir_build_cmp(g->mu, MIR_CMP_LEQ, left, right);
         break;
      case V_BINARY_GT:
         result = mir_build_cmp(g->mu, MIR_CMP_GT, left, right);
         break;
      case V_BINARY_GEQ:
         result = mir_build_cmp(g->mu, MIR_CMP_GEQ, left, right);
         break;
      case V_BINARY_TIMES:
         result = mir_build_mul(g->mu, ltype, left, right);
         break;
      case V_BINARY_LOG_AND:
         result = mir_build_and(g->mu, vlog_lower_test(g, left),
                                vlog_lower_test(g, right));
         break;
      case V_BINARY_LOG_OR:
         result = mir_build_or(g->mu, vlog_lower_test(g, left),
                               vlog_lower_test(g, right));
         break;
      case V_BINARY_DIVIDE:
         result = mir_build_div(g->mu, ltype, left, right);
         break;
      case V_BINARY_EXP:
         result = mir_build_exp(g->mu, ltype, left, right);
         break;
      case V_BINARY_PLUS:
         result = mir_build_add(g->mu, ltype, left, right);
         break;
      case V_BINARY_MINUS:
         result = mir_build_sub(g->mu, ltype, left, right);
         break;
      default:
         CANNOT_HANDLE(v);
      }
   }

   // The result of comparing reals or class handles is not a vector
   if (mir_get_class(g->mu, mir_get_type(g->mu, result)) == MIR_TYPE_INT)
      result = mir_build_cast(g->mu, mir_vec4_type(g->mu, 1, false), result);

   mir_value_t loaded;
   if (mir_is_null(var))
      loaded = result;
   else {
      if (!mir_is_vector(g->mu, result))
         result = mir_build_cast(g->mu, t_logic, result);

      mir_build_store(g->mu, var, result);
      mir_build_jump(g->mu, skip_bb);

      mir_set_cursor(g->mu, skip_bb, MIR_APPEND);

      loaded = mir_build_load(g->mu, var);
   }

   return vlog_widen_vector(g, loaded, context);
}

static mir_value_t vlog_lower_update_target(vlog_gen_t *g, vlog_node_t target)
{
   // An operator assignment or increment of an associative array entry
   // which does not exist starts from the default value without a warning
   // (7.8.7)
   vlog_node_t saved = g->update_key;
   if (vlog_kind(target) == V_BIT_SELECT && vlog_params(target) > 0)
      g->update_key = vlog_param(target, vlog_params(target) - 1);
   mir_value_t value = vlog_lower_rvalue(g, target);
   g->update_key = saved;
   return value;
}

static mir_value_t vlog_lower_operator_assignment(vlog_gen_t *g, vlog_node_t v)
{
   vlog_node_t target = vlog_target(v);

   mir_value_t cur = vlog_lower_update_target(g, target);

   // The right hand side is sized by the target except for a shift count
   const vlog_assign_t kind = vlog_subkind(v);
   mir_type_t cur_type = mir_get_type(g->mu, cur);
   mir_value_t value;
   switch (kind) {
   case V_ASSIGN_SHIFT_LL:
   case V_ASSIGN_SHIFT_LA:
   case V_ASSIGN_SHIFT_RL:
   case V_ASSIGN_SHIFT_RA:
      value = vlog_lower_rvalue(g, vlog_value(v));
      break;
   default:
      if (mir_is_vector_type(g->mu, cur_type))
         value = vlog_lower_with_context(
            g, vlog_value(v), vlog_assign_context(g, cur_type, vlog_value(v)));
      else
         value = vlog_lower_rvalue(g, vlog_value(v));
      break;
   }

   if (kind != V_ASSIGN_EQUALS) {
      mir_type_t type = cur_type;
      mir_value_t cast = vlog_lower_cast(g, type, value);

      if (mir_get_class(g->mu, type) == MIR_TYPE_REAL) {
         switch (kind) {
         case V_ASSIGN_PLUS:
            value = mir_build_add(g->mu, type, cur, cast);
            break;
         case V_ASSIGN_MINUS:
            value = mir_build_sub(g->mu, type, cur, cast);
            break;
         case V_ASSIGN_TIMES:
            value = mir_build_mul(g->mu, type, cur, cast);
            break;
         case V_ASSIGN_DIVIDE:
            value = mir_build_div(g->mu, type, cur, cast);
            break;
         default:
            CANNOT_HANDLE(v);
         }
      }
      else {
         mir_vec_op_t op;
         switch (kind) {
         case V_ASSIGN_PLUS:     op = MIR_VEC_ADD; break;
         case V_ASSIGN_MINUS:    op = MIR_VEC_SUB; break;
         case V_ASSIGN_TIMES:    op = MIR_VEC_MUL; break;
         case V_ASSIGN_DIVIDE:   op = MIR_VEC_DIV; break;
         case V_ASSIGN_MOD:      op = MIR_VEC_MOD; break;
         case V_ASSIGN_AND:      op = MIR_VEC_BIT_AND; break;
         case V_ASSIGN_OR:       op = MIR_VEC_BIT_OR; break;
         case V_ASSIGN_XOR:      op = MIR_VEC_BIT_XOR; break;
         case V_ASSIGN_SHIFT_LL:
         case V_ASSIGN_SHIFT_LA: op = MIR_VEC_SLL; break;
         case V_ASSIGN_SHIFT_RL: op = MIR_VEC_SRL; break;
         case V_ASSIGN_SHIFT_RA:
            // Arithmetic only if the result is signed
            op = vlog_is_signed(vlog_target(v)) ? MIR_VEC_SRA : MIR_VEC_SRL;
            break;
         default:
            CANNOT_HANDLE(v);
         }

         value = mir_build_binary(g->mu, op, type, cur, cast);
      }
   }

   vlog_assign_variable(g, target, value, NULL);
   return value;
}

static bool vlog_has_hier_name(vlog_node_t v)
{
   // The parser marks a %m format with the name of the enclosing blocks

   const int nparams = vlog_params(v);
   for (int i = 0; i < nparams; i++) {
      vlog_node_t p = vlog_param(v, i);
      if (vlog_kind(p) != V_STRING)
         continue;

      number_t n = vlog_number(p);
      const unsigned nbytes = number_width(n) / 8;
      for (unsigned j = 1; j < nbytes; j++) {
         // Bytes are numbered from the least significant end
         if (number_byte(n, j) == '%' && number_byte(n, j - 1) == '{')
            return true;
      }
   }

   return false;
}

static mir_value_t vlog_lower_handle_id(vlog_gen_t *g, mir_value_t obj)
{
   // The number of an object as shown by a system task which is zero
    // for a null handle

   mir_type_t t_id = mir_int_type(g->mu, INT64_MIN, INT64_MAX);
   mir_type_t t_obj = mir_get_type(g->mu, obj);

   mir_value_t result = mir_add_var(g->mu, t_id, MIR_NULL_STAMP,
                                    ident_uniq("oid"), MIR_VAR_TEMP);
   mir_build_store(g->mu, result, mir_const(g->mu, t_id, 0));

   mir_value_t objvar = mir_add_var(g->mu, t_obj, MIR_NULL_STAMP,
                                    ident_uniq("obj"), MIR_VAR_TEMP);
   mir_build_store(g->mu, objvar, obj);

   mir_block_t load_bb = mir_add_block(g->mu);
   mir_block_t done_bb = mir_add_block(g->mu);

   mir_build_cond(g->mu, mir_build_cmp(g->mu, MIR_CMP_EQ, obj,
                                       mir_build_null(g->mu, t_obj)),
                  done_bb, load_bb);

   mir_set_cursor(g->mu, load_bb, MIR_APPEND);
   mir_value_t link = mir_build_link_var(g->mu, mir_build_load(g->mu, objvar),
                                         ident_new("$oid"), t_id);
   mir_build_store(g->mu, result, mir_build_load(g->mu, link));
   mir_build_jump(g->mu, done_bb);

   mir_set_cursor(g->mu, done_bb, MIR_APPEND);
   return mir_build_load(g->mu, result);
}

static mir_value_t vlog_lower_systf_param(vlog_gen_t *g, vlog_node_t v,
                                          bool in_subr)
{
   switch (vlog_kind(v)) {
   case V_REF:
   case V_MEMBER_REF:
   case V_USER_FCALL:
      {
         vlog_node_t type = vlog_get_type(v);
         if (type != NULL && (vlog_kind(type) == V_STRUCT_DECL
                              || vlog_kind(type) == V_UNION_DECL)
             && !(vlog_flags(type) & VLOG_F_PACKED))
            fatal_at(vlog_loc(v), "sorry, an unpacked structure or union as "
                     "the argument of a system task is not supported");
         else if (type != NULL && vlog_kind(type) == V_DATA_TYPE
                  && vlog_subkind(type) == DT_CHANDLE)
            return vlog_lower_rvalue(g, v);   // Never a net
      }
      break;
   default:
      break;
   }

   switch (vlog_kind(v)) {
   case V_REF:
      switch (vlog_kind(vlog_ref(v))) {
      case V_NET_DECL:
      case V_PORT_DECL:
         // The VPI scope of the caller of a subroutine may not contain
         // the declaration
         return in_subr ? vlog_lower_rvalue(g, v) : MIR_NULL_VALUE;
      case V_TF_PORT_DECL:
      case V_FUNC_DECL:
      case V_PARAM_DECL:
      case V_LOCALPARAM:
      case V_ENUM_NAME:
         return vlog_lower_rvalue(g, v);
      case V_VAR_DECL:
      case V_LOCAL_DECL:
         {
            if (in_subr)
               return vlog_lower_rvalue(g, v);

            // May be called from another instance so has no VPI handle
            if (vlog_kind(vlog_ref(v)) == V_VAR_DECL
                && (vlog_flags(vlog_ref(v)) & VLOG_F_STATIC))
               return vlog_lower_rvalue(g, v);

            int hops;
            mir_value_t var = mir_search_object(g->mu, vlog_ref(v), &hops);
            if (mir_is_null(var)) {
               if (!vlog_has_ident2(vlog_ref(v)))
                  return MIR_NULL_VALUE;   // Signal in another scope
            }
            else if (hops > 0) {
               mir_value_t ptr = mir_build_var_upref(g->mu, hops, var.id);
               if (mir_points_to(g->mu, ptr, MIR_TYPE_SIGNAL))
                  return MIR_NULL_VALUE;
            }

            return vlog_lower_rvalue(g, v);
         }
      default:
         return MIR_NULL_VALUE;
      }
   case V_STRING:
   case V_NUMBER:
   case V_EMPTY:
   case V_REAL:
      return MIR_NULL_VALUE;
   case V_HIER_REF:
      // The name of an instance is a scope with no value
      if (!vlog_has_ref(v))
         return MIR_NULL_VALUE;
      return vlog_lower_rvalue(g, v);
   case V_UNARY:
   case V_BINARY:
   case V_SYS_FCALL:
   case V_PREFIX:
   case V_POSTFIX:
   case V_BIT_SELECT:
   case V_PART_SELECT:
   case V_CONCAT:
   case V_COND_EXPR:
   case V_MEMBER_REF:
   case V_USER_FCALL:
   case V_METHOD_CALL:
   case V_CAST:
   case V_INSIDE:
   case V_STD_RANDOMIZE:
   case V_STREAM:
      // TODO: these should not be evaluated until vpi_get_value is called
      return vlog_lower_rvalue(g, v);
   default:
      CANNOT_HANDLE(v);
   }
}

static mir_value_t vlog_lower_dist(vlog_gen_t *g, vlog_node_t v, int kind)
{
   // The seed is updated by assignment as for $random

   mir_type_t t_offset = mir_offset_type(g->mu);

   const int nparams = vlog_params(v);
   vlog_node_t target = vlog_param(v, 0);

   mir_value_t args[4] = {
      mir_const(g->mu, t_offset, kind),
      vlog_lower_cast(g, t_offset, vlog_lower_rvalue(g, target)),
      mir_const(g->mu, t_offset, 0),
      mir_const(g->mu, t_offset, 0),
   };

   for (int i = 1; i < nparams && i < 3; i++)
      args[i + 1] = vlog_lower_cast(g, t_offset,
                                    vlog_lower_rvalue(g, vlog_param(v, i)));

   mir_value_t value = mir_build_fcall(g->mu, ident_new("__sv_dist"),
                                       t_offset, MIR_NULL_STAMP, args, 4);
   mir_value_t next = mir_build_fcall(g->mu, ident_new("__sv_dist_next"),
                                      t_offset, MIR_NULL_STAMP, args, 4);
   mir_build_consume(g->mu, value);
   mir_build_consume(g->mu, next);

   vlog_assign_variable(g, target, vlog_offset_to_int(g, next), NULL);

   return vlog_lower_cast(g, mir_vec4_type(g->mu, 32, true),
                          vlog_offset_to_int(g, value));
}

static void vlog_report_error(vlog_gen_t *g, vlog_node_t v, const char *msg)
{
   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_type_t t_severity = mir_int_type(g->mu, 0, 3);

   mir_value_t severity = mir_const(g->mu, t_severity, SEVERITY_ERROR);
   mir_value_t buf = mir_const_string(g->mu, msg);
   mir_value_t ptr = mir_build_address_of(g->mu, buf);
   mir_value_t len = mir_const(g->mu, t_offset, strlen(msg));
   mir_value_t locus = mir_build_debug_locus(g->mu, vlog_to_object(v));

   mir_build_report(g->mu, ptr, len, severity, locus);
}

static void vlog_cast_type_name(text_buf_t *tb, vlog_node_t type)
{
   if (vlog_kind(type) == V_CLASS_DECL) {
      const char *name = istr(vlog_ident(type));
      const char *at = strchr(name, '@');
      tb_cat(tb, "class ");
      tb_catn(tb, name, at != NULL ? at - name : strlen(name));
   }
   else if (vlog_kind(type) == V_ENUM_DECL && vlog_has_ident(type)) {
      const char *name = istr(vlog_ident(type));
      const char *dot = strrchr(name, '.');
      tb_printf(tb, "enum %s", dot != NULL ? dot + 1 : name);
   }
   else
      tb_cat(tb, "the enumeration");
}

static mir_value_t vlog_lower_dyn_cast(vlog_gen_t *g, vlog_node_t v)
{
   // $cast assigns the source to the destination if that is legal and
   // returns one, or does nothing and returns zero.  Used as a task a
   // failure is an error.

   mir_type_t t_int = mir_vec2_type(g->mu, 32, true);

   vlog_node_t dest = vlog_param(v, 0), src = vlog_param(v, 1);
   vlog_node_t dt = vlog_get_type(dest);

   mir_value_t ok = mir_add_var(g->mu, t_int, MIR_NULL_STAMP,
                                ident_uniq("cast"), MIR_VAR_TEMP);
   mir_build_store(g->mu, ok, mir_const_vec(g->mu, t_int, 0, 0));

   mir_block_t yes_bb = mir_add_block(g->mu);
   mir_block_t done_bb = mir_add_block(g->mu);

   if (vlog_kind(dt) == V_CLASS_DECL) {
      mir_value_t obj = vlog_lower_rvalue(g, src);
      mir_type_t t_obj = mir_get_type(g->mu, obj);

      mir_value_t objvar = mir_add_var(g->mu, t_obj, MIR_NULL_STAMP,
                                       ident_uniq("obj"), MIR_VAR_TEMP);
      mir_build_store(g->mu, objvar, obj);

      mir_block_t null_bb = mir_add_block(g->mu);
      mir_block_t test_bb = mir_add_block(g->mu);

      // A null handle can be assigned to any class
      mir_value_t is_null = mir_build_cmp(g->mu, MIR_CMP_EQ, obj,
                                          mir_build_null(g->mu, t_obj));
      mir_build_cond(g->mu, is_null, null_bb, test_bb);

      mir_set_cursor(g->mu, test_bb, MIR_APPEND);

      ident_t name = ident_sprintf("%s$cast", istr(vlog_subr_name(dt)));
      mir_defer(g->mir, name, NULL, MIR_UNIT_FUNCTION, vlog_lower_cast_unit,
                vlog_to_object(dt));

      mir_type_t t_dest = mir_context_type(g->mu, vlog_subr_name(dt));
      mir_value_t args[] = {
         mir_build_cast(g->mu, t_dest, mir_build_load(g->mu, objvar))
      };
      mir_value_t res = mir_build_fcall(g->mu, name, mir_offset_type(g->mu),
                                        MIR_NULL_STAMP, args, 1);
      mir_build_consume(g->mu, res);

      mir_value_t zero = mir_const(g->mu, mir_offset_type(g->mu), 0);
      mir_build_cond(g->mu, mir_build_cmp(g->mu, MIR_CMP_NEQ, res, zero),
                     yes_bb, done_bb);

      mir_set_cursor(g->mu, null_bb, MIR_APPEND);
      mir_build_jump(g->mu, yes_bb);

      mir_set_cursor(g->mu, yes_bb, MIR_APPEND);
      vlog_assign_variable(g, dest, mir_build_load(g->mu, objvar), NULL);
   }
   else if (vlog_kind(dt) == V_ENUM_DECL) {
      // The value must be one of the names of the enumeration
      mir_value_t value = vlog_lower_rvalue(g, src);
      mir_type_t t_val = mir_get_type(g->mu, value);

      mir_value_t valvar = mir_add_var(g->mu, t_val, MIR_NULL_STAMP,
                                       ident_uniq("val"), MIR_VAR_TEMP);
      mir_build_store(g->mu, valvar, value);

      const type_info_t *ti = vlog_type_info(g, dt);
      mir_value_t cast = vlog_lower_cast(g, ti->type, value);

      const int nnames = vlog_decls(dt);
      for (int i = 0; i < nnames; i++) {
         vlog_node_t n = vlog_decl(dt, i);
         mir_value_t nv = vlog_lower_cast(g, ti->type,
                                          vlog_lower_rvalue(g, vlog_value(n)));

         mir_block_t next_bb = mir_add_block(g->mu);
         mir_build_cond(g->mu,
                        vlog_lower_test(g, mir_build_binary(
                                           g->mu, MIR_VEC_CASE_EQ,
                                           mir_vec4_type(
                                              g->mu, ti->size, false),
                                           mir_build_cast(
                                              g->mu,
                                              mir_vec4_type(
                                                 g->mu, ti->size, false),
                                              cast),
                                           mir_build_cast(
                                              g->mu,
                                              mir_vec4_type(
                                                 g->mu, ti->size, false),
                                              nv))),
                        yes_bb, next_bb);
         mir_set_cursor(g->mu, next_bb, MIR_APPEND);
      }

      mir_build_jump(g->mu, done_bb);

      mir_set_cursor(g->mu, yes_bb, MIR_APPEND);
      vlog_assign_variable(g, dest, mir_build_load(g->mu, valvar), NULL);
   }
   else {
      // Any value of a type which can be assigned
      mir_build_jump(g->mu, yes_bb);

      mir_set_cursor(g->mu, yes_bb, MIR_APPEND);
      vlog_assign_variable(g, dest, MIR_NULL_VALUE, src);
   }

   mir_build_store(g->mu, ok, mir_const_vec(g->mu, t_int, 1, 0));
   mir_build_jump(g->mu, done_bb);

   mir_set_cursor(g->mu, done_bb, MIR_APPEND);

   if (vlog_kind(v) == V_SYS_TCALL) {
      mir_block_t fail_bb = mir_add_block(g->mu);
      mir_block_t end_bb = mir_add_block(g->mu);

      mir_build_cond(g->mu, vlog_lower_test(g, mir_build_load(g->mu, ok)),
                     end_bb, fail_bb);

      mir_set_cursor(g->mu, fail_bb, MIR_APPEND);

      // The message names the types and the location of the call as
      // the report shows the process (8.16)
      LOCAL_TEXT_BUF tb = tb_new();
      const loc_t *loc = vlog_loc(v);
      tb_printf(tb, "$cast at %s:%d failed: ", loc_file_str(loc),
                loc->first_line);

      vlog_node_t st = vlog_get_type(src);
      if (vlog_kind(dt) == V_CLASS_DECL && st != NULL
          && vlog_kind(st) == V_CLASS_DECL) {
         tb_cat(tb, "the ");
         vlog_cast_type_name(tb, st);
         tb_cat(tb, " handle does not refer to an object of ");
         vlog_cast_type_name(tb, dt);
         tb_cat(tb, " or a class derived from it");
      }
      else if (vlog_kind(dt) == V_ENUM_DECL) {
         tb_cat(tb, "the value is not a member of ");
         vlog_cast_type_name(tb, dt);
      }
      else
         tb_cat(tb, "the source cannot be assigned to the destination");

      vlog_report_error(g, v, tb_get(tb));
      mir_build_jump(g->mu, end_bb);

      mir_set_cursor(g->mu, end_bb, MIR_APPEND);
   }

   return mir_build_load(g->mu, ok);
}

static mir_value_t vlog_lower_plusarg_string(vlog_gen_t *g, vlog_node_t v)
{
   // The system task interface cannot write to a variable of type string

   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_type_t t_char = mir_char_type(g->mu);
   mir_type_t t_int = mir_vec2_type(g->mu, 32, true);
   mir_value_t zero = mir_const(g->mu, t_offset, 0);
   mir_value_t one = mir_const(g->mu, t_offset, 1);

   mir_value_t result = mir_add_var(g->mu, t_int, MIR_NULL_STAMP,
                                    ident_uniq("found"), MIR_VAR_TEMP);
   mir_build_store(g->mu, result, mir_const_vec(g->mu, t_int, 0, 0));

   mir_value_t args[] = { vlog_str_arg(g, vlog_param(v, 0)) };
   mir_value_t len = vlog_str_call(g, "__sv_plusarg_find", t_offset, args, 1);

   mir_block_t copy_bb = mir_add_block(g->mu);
   mir_block_t done_bb = mir_add_block(g->mu);

   mir_build_cond(g->mu, mir_build_cmp(g->mu, MIR_CMP_NEQ, len, zero),
                  copy_bb, done_bb);

   mir_set_cursor(g->mu, copy_bb, MIR_APPEND);

   mir_value_t count = mir_build_sub(g->mu, t_offset, len, one);
   mir_value_t data = vlog_alloc_array(g, t_char, count);
   mir_value_t cargs[] = { data };
   vlog_str_call(g, "__sv_plusarg_copy", t_offset, cargs, 1);

   vlog_assign_variable(g, vlog_param(v, 1), vlog_wrap_array(g, data, count),
                        NULL);
   mir_build_store(g->mu, result, mir_const_vec(g->mu, t_int, 1, 0));
   mir_build_jump(g->mu, done_bb);

   mir_set_cursor(g->mu, done_bb, MIR_APPEND);
   return mir_build_load(g->mu, result);
}

static bool vlog_is_string_target(vlog_node_t v, int n)
{
   // A string variable which the system task assigns

   const vlog_node_t p = vlog_param(v, n);
   switch (vlog_kind(p)) {
   case V_REF:
   case V_MEMBER_REF:
   case V_HIER_REF:
   case V_BIT_SELECT:
      break;
   default:
      return false;
   }

   if (!vlog_is_string(p))
      return false;

   ident_t id = vlog_ident(v);
   if (id == ident_new("$fscanf") || id == ident_new("$sscanf"))
      return n >= 2;
   else if (id == ident_new("$fgets"))
      return n == 0;
   else if (id == ident_new("$ferror"))
      return n == 1;
   else
      return false;
}

static mir_value_t vlog_lower_sys_tfcall(vlog_gen_t *g, vlog_node_t v)
{
   const int nparams = vlog_params(v);

   if (nparams > 0) {
      switch (vlog_kind(vlog_param(v, 0))) {
      case V_REF:
      case V_HIER_REF:
      case V_MEMBER_REF:
      case V_BIT_SELECT:
         if (vlog_ident(v) == ident_new("$random") && nparams == 1) {
            mir_type_t t_offset = mir_offset_type(g->mu);
            return vlog_lower_seeded_random(
               g, v, mir_const(g->mu, t_offset, INT32_MIN),
               mir_const(g->mu, t_offset, INT32_MAX));
         }
         else if (nparams >= 2) {
            static const struct {
               const char *name;
               int         kind;
               int         nparams;
            } dists[] = {
               { "$dist_normal",      1, 3 },
               { "$dist_exponential", 2, 2 },
               { "$dist_poisson",     3, 2 },
               { "$dist_chi_square",  4, 2 },
               { "$dist_t",           5, 2 },
               { "$dist_erlang",      6, 3 },
            };

            for (int i = 0; i < ARRAY_LEN(dists); i++) {
               if (vlog_ident(v) == ident_new(dists[i].name)
                   && nparams == dists[i].nparams)
                  return vlog_lower_dist(g, v, dists[i].kind);
            }
         }

         if (vlog_ident(v) == ident_new("$dist_uniform")
                  && nparams == 3) {
            mir_type_t t_offset = mir_offset_type(g->mu);
            mir_value_t lo = vlog_lower_cast(
               g, t_offset, vlog_lower_rvalue(g, vlog_param(v, 1)));
            mir_value_t hi = vlog_lower_cast(
               g, t_offset, vlog_lower_rvalue(g, vlog_param(v, 2)));
            return vlog_lower_seeded_random(g, v, lo, hi);
         }
         break;
      default:
         break;
      }
   }

   mir_value_t *args LOCAL =
      xmalloc_array((nparams * 3) + 1, sizeof(mir_value_t));
   int actual = 0;
   mir_type_t t_offset = mir_offset_type(g->mu);

   // A string variable which is set by the task is passed as a buffer
   // and its length, copied back to the variable after the call
   mir_value_t *strbuf LOCAL = xmalloc_array(nparams, sizeof(mir_value_t));
   mir_value_t *intbuf LOCAL = xmalloc_array(nparams, sizeof(mir_value_t));
   for (int i = 0; i < nparams; i++)
      strbuf[i] = intbuf[i] = MIR_NULL_VALUE;
   mir_type_t t_char = mir_char_type(g->mu);
   mir_type_t t_sbuf = mir_carray_type(g->mu, STR_TARGET_MAX, t_char);

   for (int i = 0; i < nparams; i++) {
      if (vlog_is_string_target(v, i)) {
         strbuf[i] = mir_add_var(g->mu, t_sbuf, MIR_NULL_STAMP,
                                 ident_uniq("strbuf"), 0);

         // The variable keeps its value if the task does not set it
         mir_value_t cur = vlog_lower_rvalue(g, vlog_param(v, i));
         mir_value_t clen = mir_build_uarray_len(g->mu, cur, 0);
         mir_value_t max = mir_const(g->mu, t_offset, STR_TARGET_MAX - 1);
         mir_value_t keep = mir_build_select(
            g->mu, t_offset, mir_build_cmp(g->mu, MIR_CMP_GT, clen, max), max,
            clen);
         mir_value_t base = mir_build_array_ref(
            g->mu, strbuf[i], mir_const(g->mu, t_offset, 0));
         mir_build_copy(g->mu, base, mir_build_unwrap(g->mu, cur), keep);
         mir_build_store(g->mu, mir_build_array_ref(g->mu, strbuf[i], keep),
                         mir_const(g->mu, t_char, 0));

         args[actual++] = mir_const(g->mu, t_offset, -3);
         args[actual++] = strbuf[i];
         args[actual++] = mir_const(g->mu, t_offset, 0);
         continue;
      }

      if (vlog_is_scan_target(v, i)) {
         // The variable may be a local which has no VPI handle so the
         // value is returned in a buffer with one more element which is
         // set when the task assigns it
         const int width = vlog_is_real(vlog_param(v, i))
            ? 64 : vlog_width(vlog_param(v, i));
         mir_type_t t_logic = mir_logic_type(g->mu);
         mir_type_t t_ibuf = mir_carray_type(g->mu, width + 1, t_logic);
         intbuf[i] = mir_add_var(g->mu, t_ibuf, MIR_NULL_STAMP,
                                 ident_uniq("intbuf"), MIR_VAR_TEMP);

         mir_value_t flag = mir_build_array_ref(
            g->mu, intbuf[i], mir_const(g->mu, t_offset, width));
         mir_build_store(g->mu, flag, mir_const(g->mu, t_logic, 0));

         args[actual++] = mir_const(g->mu, t_offset, -4);
         args[actual++] = mir_build_array_ref(
            g->mu, intbuf[i], mir_const(g->mu, t_offset, 0));
         args[actual++] = mir_const(g->mu, t_offset, width);
         continue;
      }

      if (vlog_kind(vlog_param(v, i)) == V_NULL) {
         args[actual++] = mir_const(g->mu, t_offset, -5);
         args[actual++] = mir_const(g->mu, t_offset, 0);
         args[actual++] = mir_const(g->mu, t_offset, 0);   // Padding
         continue;
      }

      mir_value_t arg = vlog_lower_systf_param(g, vlog_param(v, i),
                                               !!(vlog_flags(v) & VLOG_F_IN_SUBR));
      if (mir_is_null(arg))
         continue;

      mir_type_t type = mir_get_type(g->mu, arg);
      switch (mir_get_class(g->mu, type)) {
      case MIR_TYPE_VEC4:
         args[actual++] = mir_const(g->mu, t_offset, mir_get_size(g->mu, type));
         args[actual++] = arg;
         break;
      case MIR_TYPE_VEC2:
         {
            const int size = mir_get_size(g->mu, type);
            mir_type_t t_vec4 = mir_vec4_type(g->mu, size, false);
            args[actual++] = mir_const(g->mu, t_offset, size);
            args[actual++] = mir_build_cast(g->mu, t_vec4, arg);
         }
         break;
      case MIR_TYPE_REAL:
         args[actual++] = mir_const(g->mu, t_offset, -1);
         args[actual++] = arg;
         args[actual++] = mir_const(g->mu, t_offset, 0); // Padding
         break;
      case MIR_TYPE_UARRAY:
         args[actual++] = mir_const(g->mu, t_offset, -2);
         args[actual++] = mir_build_unwrap(g->mu, arg);
         args[actual++] = mir_build_uarray_len(g->mu, arg, 0);
         break;
      case MIR_TYPE_CONTEXT:
         args[actual++] = mir_const(g->mu, t_offset, -5);
         args[actual++] = vlog_lower_handle_id(g, arg);
         args[actual++] = mir_const(g->mu, t_offset, 0); // Padding
         break;
      case MIR_TYPE_ACCESS:
         // A chandle is shown as a handle with the value of the pointer,
         // or null
         args[actual++] = mir_const(g->mu, t_offset, -5);
         args[actual++] = arg;
         args[actual++] = mir_const(g->mu, t_offset, 0); // Padding
         break;
      default:
         should_not_reach_here();
      }
   }

   mir_value_t locus = mir_build_debug_locus(g->mu, vlog_to_object(v));

   mir_type_t type = MIR_NULL_TYPE;
   if (vlog_is_sformatf(vlog_ident(v)))
      type = mir_uarray_type(g->mu, 1, mir_char_type(g->mu));
   else if (vlog_kind(v) == V_SYS_FCALL) {
      switch (vpi_func_type(vlog_ident(v))) {
      case vpiRealFunc:
         type = mir_double_type(g->mu);
         break;
      case vpiTimeFunc:
         type = mir_vec2_type(g->mu, 64, false);
         break;
      default:
         warn_at(vlog_loc(v), "result type of system function %pQ is "
                 "not known", vlog_ident(v));
         // Fall-through
      case vpiIntFunc:
         type = mir_vec4_type(g->mu, 32, true);
         break;
      }
   }

   const int inst_hops = vlog_has_hier_name(v) ? vlog_instance_hops(g) : -1;
   if (inst_hops >= 0) {
      // The name is that of the instance the code is declared in
      mir_type_t t_int64 = mir_int_type(g->mu, INT64_MIN, INT64_MAX);
      mir_value_t ctx[] = { mir_build_context_upref(g->mu, inst_hops) };
      mir_value_t r = mir_build_fcall(g->mu, ident_new("__sv_hier_scope"),
                                      t_int64, MIR_NULL_STAMP, ctx, 1);
      mir_build_consume(g->mu, r);
   }

   mir_value_t result = mir_build_syscall(g->mu, vlog_ident(v), type,
                                          MIR_NULL_STAMP, locus, args, actual);
   mir_build_consume(g->mu, result);

   for (int i = 0; i < nparams; i++) {
      if (mir_is_null(intbuf[i]))
         continue;

      vlog_node_t target = vlog_param(v, i);
      const bool is_real = vlog_is_real(target);
      const int width = is_real ? 64 : vlog_width(target);
      mir_type_t t_logic = mir_logic_type(g->mu);

      mir_value_t flag = mir_build_load(g->mu, mir_build_array_ref(
         g->mu, intbuf[i], mir_const(g->mu, t_offset, width)));
      mir_block_t set_bb = mir_add_block(g->mu);
      mir_block_t done_bb = mir_add_block(g->mu);

      mir_build_cond(g->mu, mir_build_cmp(g->mu, MIR_CMP_NEQ, flag,
                                          mir_const(g->mu, t_logic, 0)),
                     set_bb, done_bb);

      mir_set_cursor(g->mu, set_bb, MIR_APPEND);

      mir_type_t t_vec = mir_vec4_type(g->mu, width, false);
      mir_value_t data = mir_build_array_ref(
         g->mu, intbuf[i], mir_const(g->mu, t_offset, 0));
      if (width == 1)
         data = mir_build_load(g->mu, data);

      mir_value_t packed = mir_build_pack(g->mu, t_vec, data);
      if (is_real) {
         // The bits of the value which the task converted
         mir_type_t t_real = mir_double_type(g->mu);
         mir_value_t bits = vlog_lower_cast(g, t_offset, packed);
         mir_value_t real = mir_build_fcall(g->mu, ident_new("__sv_bits_to_real"),
                                            t_real, MIR_NULL_STAMP, &bits, 1);
         mir_build_consume(g->mu, real);
         packed = real;
      }

      vlog_assign_variable(g, target, packed, NULL);
      mir_build_jump(g->mu, done_bb);

      mir_set_cursor(g->mu, done_bb, MIR_APPEND);
   }

   for (int i = 0; i < nparams; i++) {
      if (mir_is_null(strbuf[i]))
         continue;

      mir_value_t zero = mir_const(g->mu, t_offset, 0);
      mir_value_t base = mir_build_array_ref(g->mu, strbuf[i], zero);
      mir_value_t cargs[] = { base };
      mir_value_t len = vlog_str_call(g, "__sv_cstr_len", t_offset, cargs, 1);

      mir_value_t data = vlog_alloc_array(g, t_char, len);
      mir_build_copy(g->mu, data, base, len);
      vlog_assign_variable(g, vlog_param(v, i), vlog_wrap_array(g, data, len),
                           NULL);
   }

   return result;
}

static mir_value_t vlog_lower_fread(vlog_gen_t *g, vlog_node_t v)
{
   // The target may be a local variable so it cannot go through VPI

   vlog_node_t target = vlog_param(v, 0);
   const int width = vlog_width(target);

   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_type_t t_int64 = mir_int_type(g->mu, INT64_MIN, INT64_MAX);

   mir_value_t args[] = {
      vlog_lower_cast(g, t_int64, vlog_lower_rvalue(g, vlog_param(v, 1))),
      mir_const(g->mu, t_int64, width / 8),
   };
   mir_value_t data = mir_build_fcall(g->mu, ident_new("__sv_fread"), t_int64,
                                      MIR_NULL_STAMP, args, 2);
   mir_build_consume(g->mu, data);

   mir_type_t t_vec = mir_vec2_type(g->mu, width, false);
   vlog_assign_variable(g, target, mir_build_cast(g->mu, t_vec, data), NULL);

   mir_value_t count = mir_build_fcall(g->mu, ident_new("__sv_fread_count"),
                                       t_offset, MIR_NULL_STAMP, NULL, 0);
   mir_build_consume(g->mu, count);

   return vlog_offset_to_int(g, count);
}

typedef struct {
   vlog_node_t dim;
   bool        dynamic;
} vlog_qdim_t;

#define MAX_QUERY_DIMS 16

static int vlog_query_dims(vlog_node_t v, vlog_qdim_t *dims)
{
   // Dimensions of an array or vector expression with the unpacked
   // dimensions first

   switch (vlog_kind(v)) {
   case V_REF:
   case V_MEMBER_REF:
   case V_HIER_REF:
      {
         vlog_node_t decl;
         if (vlog_has_ref(v))
            decl = vlog_ref(v);
         else if (vlog_kind(v) == V_MEMBER_REF && vlog_virtual_pending(v))
            decl = vlog_virtual_decl(v);
         else
            return 0;

         switch (vlog_kind(decl)) {
         case V_VAR_DECL:
         case V_NET_DECL:
         case V_PORT_DECL:
         case V_LOCAL_DECL:
         case V_TF_PORT_DECL:
         case V_PARAM_DECL:
         case V_LOCALPARAM:
            break;
         default:
            return 0;
         }

         int n = 0;
         const int nranges = vlog_ranges(decl);
         for (int i = 0; i < nranges && n < MAX_QUERY_DIMS; i++)
            dims[n++] = (vlog_qdim_t){ vlog_range(decl, i), false };

         vlog_node_t t = vlog_get_type(vlog_type(decl));
         while (t != NULL && vlog_kind(t) == V_DATA_TYPE
                && (vlog_subkind(t) == DT_QUEUE
                    || vlog_subkind(t) == DT_DYNARRAY)) {
            vlog_node_t sized = vlog_sized_dimension(t);
            if (n < MAX_QUERY_DIMS)
               dims[n++] = (vlog_qdim_t){ sized, sized == NULL };
            t = vlog_get_type(vlog_type(t));
         }

         if (t != NULL && vlog_kind(t) == V_DATA_TYPE) {
            const int nr = vlog_ranges(t);
            for (int i = 0; i < nr && n < MAX_QUERY_DIMS; i++)
               dims[n++] = (vlog_qdim_t){ vlog_range(t, i), false };
         }

         return n;
      }
   case V_BIT_SELECT:
      {
         vlog_qdim_t tmp[MAX_QUERY_DIMS];
         const int n = vlog_query_dims(vlog_value(v), tmp);
         const int skip = vlog_params(v);
         if (skip >= n)
            return 0;

         for (int i = skip; i < n; i++)
            dims[i - skip] = tmp[i];
         return n - skip;
      }
   default:
      return 0;
   }
}

static int vlog_query_unpacked(vlog_node_t v, const vlog_qdim_t *dims, int n)
{
   int count = 0;
   for (int i = 0; i < n; i++) {
      if (dims[i].dynamic
          || vlog_subkind(dims[i].dim) == V_DIM_UNPACKED)
         count++;
      else
         break;
   }

   return count;
}

static bool vlog_is_array_query(vlog_node_t v)
{
   static const char *const names[] = {
      "$dimensions", "$unpacked_dimensions", "$left", "$right", "$low",
      "$high", "$increment", "$size",
   };

   for (int i = 0; i < ARRAY_LEN(names); i++) {
      if (vlog_ident(v) == ident_new(names[i]))
         return true;
   }

   return false;
}

static mir_value_t vlog_lower_array_query(vlog_gen_t *g, vlog_node_t v)
{
   mir_type_t t_integer = mir_vec4_type(g->mu, 32, true);

   const int nparams = vlog_params(v);
   if (nparams < 1 || nparams > 2)
      fatal_at(vlog_loc(v), "wrong number of arguments to %pQ", vlog_ident(v));

   vlog_node_t arg = vlog_param(v, 0);

   vlog_qdim_t dims[MAX_QUERY_DIMS];
   const int n = vlog_query_dims(arg, dims);

   ident_t id = vlog_ident(v);
   if (id == ident_new("$dimensions"))
      return mir_const_vec(g->mu, t_integer, n, 0);
   else if (id == ident_new("$unpacked_dimensions"))
      return mir_const_vec(g->mu, t_integer, vlog_query_unpacked(arg, dims, n),
                           0);

   int64_t which = 1;
   if (nparams == 2 && !vlog_get_const(vlog_param(v, 1), &which))
      fatal_at(vlog_loc(v), "sorry, the dimension argument of %pQ must be "
               "constant", id);

   if (which < 1 || which > n)
      return vlog_lower_x(g, t_integer);

   const vlog_qdim_t *q = &(dims[which - 1]);

   int64_t left, right;
   mir_value_t len = MIR_NULL_VALUE;
   if (q->dynamic) {
      if (which != 1 || vlog_kind(arg) == V_BIT_SELECT)
         fatal_at(vlog_loc(v), "sorry, this dimension of a dynamic array "
                  "cannot be queried");

      len = vlog_offset_to_int(g, mir_build_uarray_len(
                                  g->mu, vlog_lower_rvalue(g, arg), 0));
      left = 0;
      right = 0;
   }
   else if (!vlog_bounds(q->dim, &left, &right))
      return vlog_lower_x(g, t_integer);

   mir_value_t one = mir_const_vec(g->mu, t_integer, 1, 0);

   if (id == ident_new("$increment"))
      return mir_const_vec(g->mu, t_integer,
                           q->dynamic || left < right ? UINT32_MAX : 1, 0);

   if (q->dynamic) {
      mir_value_t last = mir_build_binary(
         g->mu, MIR_VEC_SUB, t_integer, vlog_lower_cast(g, t_integer, len),
         one);
      mir_value_t zero = mir_const_vec(g->mu, t_integer, 0, 0);
      if (id == ident_new("$size"))
         return vlog_lower_cast(g, t_integer, len);
      else if (id == ident_new("$left") || id == ident_new("$low"))
         return zero;
      else
         return last;
   }

   int64_t result;
   if (id == ident_new("$left"))
      result = left;
   else if (id == ident_new("$right"))
      result = right;
   else if (id == ident_new("$low"))
      result = MIN(left, right);
   else if (id == ident_new("$high"))
      result = MAX(left, right);
   else
      result = llabs(left - right) + 1;

   return mir_const_vec(g->mu, t_integer, (uint64_t)result & UINT32_MAX, 0);
}

static void vlog_typename_node(vlog_node_t where, text_buf_t *tb,
                               vlog_node_t type);

static void vlog_typename_ranges_from(vlog_node_t where, text_buf_t *tb,
                                      vlog_node_t type, int from)
{
   // Dimensions are listed in declaration order (20.6.1)

   const int nranges = vlog_ranges(type);
   for (int i = from; i < nranges; i++) {
      vlog_node_t r = vlog_range(type, i);
      if (vlog_subkind(r) == V_DIM_UNSIZED)
         tb_cat(tb, "[]");
      else if (vlog_subkind(r) == V_DIM_ASSOC) {
         tb_append(tb, '[');
         if (vlog_flags(r) & VLOG_F_WILDCARD)
            tb_append(tb, '*');
         else
            vlog_typename_node(where, tb, vlog_left(r));
         tb_append(tb, ']');
      }
      else {
         int64_t left, right;
         if (!vlog_bounds(r, &left, &right))
            fatal_at(vlog_loc(r), "sorry, cannot determine the type name");
         tb_printf(tb, "[%"PRIi64":%"PRIi64"]", left, right);
      }
   }
}

static void vlog_typename_ranges(vlog_node_t where, text_buf_t *tb,
                                 vlog_node_t type, bool unpacked)
{
   vlog_typename_ranges_from(where, tb, type, 0);
}

static void vlog_typename_node(vlog_node_t where, text_buf_t *tb,
                               vlog_node_t type);

static void vlog_typename_class(vlog_node_t where, text_buf_t *tb,
                                vlog_node_t c)
{
   // A class is named with the scope that holds it (20.6.1): the name of
   // a module followed by . or a package or enclosing class followed by ::

   const char *name = istr(vlog_ident(c));
   const char *at = strchr(name, '@');
   const bool spec = at != NULL;

   tb_cat(tb, "class ");

   if (vlog_has_ident2(c)) {
      const char *scope = istr(vlog_ident2(c));
      const char *dot = strchr(scope, '.');
      if (dot != NULL)
         scope = dot + 1;   // Strip the library name

      if (strncmp(scope, "$spec$", 6) == 0) {
         // A class of a module: $spec$module$name
         scope += 6;
         const char *end = strchr(scope, '$');
         const size_t len = end != NULL ? end - scope : strlen(scope);

         // A specialisation of a class from a package or the compilation
         // unit has its own name here rather than that of a module
         if (!spec || memchr(scope, '@', len) == NULL) {
            tb_catn(tb, scope, len);
            tb_append(tb, '.');
            if (end != NULL && strchr(end + 1, '.') != NULL) {
               // Nested in another class
               tb_catn(tb, end + 1, strchr(end + 1, '.') - end - 1);
               tb_cat(tb, "::");
            }
         }
      }
      else if (!spec && isxdigit((unsigned char)scope[0])
               && strchr(scope, '$') != NULL) {
         // The compilation-unit scope is called $unit (3.12.1, 26.3)
         tb_cat(tb, "$unit::");
      }
      else if (!spec) {
         // A class of a package, perhaps nested in other classes: the
         // library unit name of the package is upper case so take the
         // name as declared from the package itself
         const char *full = istr(vlog_ident2(c));
         const char *pend = strchr(scope, '.');
         const size_t plen = pend != NULL ? pend - full : strlen(full);
         vlog_node_t pkg = vlog_find_scope(ident_new_n(full, plen));
         if (pkg != NULL && vlog_kind(pkg) == V_PACKAGE)
            tb_cat(tb, istr(vlog_ident2(pkg)));
         else
            tb_catn(tb, scope, pend != NULL ? pend - scope : strlen(scope));
         for (const char *p = pend; p != NULL; p = strchr(p + 1, '.')) {
            const char *next = strchr(p + 1, '.');
            tb_cat(tb, "::");
            tb_catn(tb, p + 1, next != NULL ? next - p - 1 : strlen(p + 1));
         }
         tb_cat(tb, "::");
      }
   }

   tb_catn(tb, name, spec ? at - name : strlen(name));

   if (!spec)
      return;

   tb_cat(tb, "#(");
   bool first = true;
   const int ndecls = vlog_decls(c);
   const loc_t *params_loc = NULL;
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(c, i);
      if (vlog_kind(d) != V_LOCALPARAM && vlog_kind(d) != V_TYPE_DECL)
         break;

      // The parameters come first and have the location of the class
      if (params_loc == NULL)
         params_loc = vlog_loc(d);
      else if (vlog_loc(d)->first_line != params_loc->first_line)
         break;

      if (!first)
         tb_append(tb, ',');
      first = false;

      if (vlog_kind(d) == V_LOCALPARAM) {
         int64_t value;
         if (!vlog_has_value(d) || !vlog_get_const(vlog_value(d), &value))
            fatal_at(vlog_loc(where), "sorry, cannot determine the type name");
         tb_printf(tb, "%"PRIi64, value);
      }
      else
         vlog_typename_node(where, tb, vlog_type(d));
   }
   tb_append(tb, ')');
}

static void vlog_typename_enum(vlog_node_t where, text_buf_t *tb,
                               vlog_node_t type)
{
   // The base type unless it is int, then the names with their values
   // as sized literals (20.6.1)

   tb_cat(tb, "enum");

   vlog_node_t base = vlog_type(type);
   while (vlog_kind(base) == V_TYPE_DECL)
      base = vlog_type(base);

   const bool is_int = vlog_kind(base) == V_DATA_TYPE
      && vlog_subkind(base) == DT_INT && (vlog_flags(base) & VLOG_F_SIGNED);
   if (!is_int) {
      tb_append(tb, ' ');
      vlog_typename_node(where, tb, base);
   }

   const bool is_signed = vlog_is_signed(base);
   const int width = vlog_size(base);

   tb_append(tb, '{');
   const int nnames = vlog_decls(type);
   for (int i = 0; i < nnames; i++) {
      vlog_node_t d = vlog_decl(type, i);
      if (i > 0)
         tb_append(tb, ',');
      tb_printf(tb, "%s=", istr(vlog_ident(d)));

      vlog_node_t value = vlog_value(d);
      int64_t ival;
      if (vlog_kind(value) == V_NUMBER
          && !number_is_defined(vlog_number(value))) {
         number_t n = vlog_number(value);
         tb_printf(tb, "%d'%sb", width, is_signed ? "s" : "");
         for (int j = width - 1; j >= 0; j--) {
            const int bit = j < (int)number_width(n) ? number_bit(n, j) : 0;
            tb_append(tb, "01zx"[bit & 3]);
         }
      }
      else if (vlog_get_const(value, &ival)) {
         if (!is_signed && width < 64)
            ival &= (INT64_C(1) << width) - 1;
         if (ival < 0)
            tb_printf(tb, "-%d'sd%"PRIu64, width, -(uint64_t)ival);
         else
            tb_printf(tb, "%d'%sd%"PRIi64, width, is_signed ? "s" : "",
                      ival);
      }
      else
         fatal_at(vlog_loc(where), "sorry, cannot determine the type name");
   }
   tb_append(tb, '}');
}

static void vlog_typename_struct(vlog_node_t where, text_buf_t *tb,
                                 vlog_node_t type)
{
   // The members are listed with their types (20.6.1)

   const vlog_flags_t flags = vlog_flags(type);

   tb_cat(tb, vlog_kind(type) == V_UNION_DECL ? "union" : "struct");
   if (flags & VLOG_F_TAGGED)
      tb_cat(tb, " tagged");
   if ((flags & VLOG_F_PACKED) && !(flags & VLOG_F_UNPACKED_VIEW)) {
      tb_cat(tb, " packed");
      if (flags & VLOG_F_SIGNED)
         tb_cat(tb, " signed");
   }

   tb_append(tb, '{');
   const int ndecls = vlog_decls(type);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(type, i);
      if (strchr(istr(vlog_ident(d)), '$') != NULL)
         continue;   // The tag of a tagged union
      else if (vlog_flags(d) & VLOG_F_VOID)
         tb_cat(tb, "void");
      else
         vlog_typename_node(where, tb, vlog_type(d));
      tb_printf(tb, " %s", istr(vlog_ident(d)));
      vlog_typename_ranges(where, tb, d, true);
      tb_append(tb, ';');
   }
   tb_append(tb, '}');
}

static void vlog_typename_node(vlog_node_t where, text_buf_t *tb,
                               vlog_node_t type)
{
   vlog_node_t named = NULL;
   while (vlog_kind(type) == V_TYPE_DECL) {
      vlog_node_t next = vlog_type(type);
      switch (vlog_kind(next)) {
      case V_ENUM_DECL:
      case V_STRUCT_DECL:
      case V_UNION_DECL:
         named = type;
         break;
      default:
         break;
      }

      type = next;
   }

   switch (vlog_kind(type)) {
   case V_ENUM_DECL:
      if (named == NULL && !vlog_has_ident(type))
         fatal_at(vlog_loc(where), "sorry, $typename is not supported for an "
                  "anonymous enumeration");
      vlog_typename_enum(where, tb, type);
      if (named == NULL)
         tb_cat(tb, istr(vlog_ident(type)));
      else
         tb_printf(tb, "%s%s", istr(vlog_ident2(named)),
                   istr(vlog_ident(named)));
      return;
   case V_STRUCT_DECL:
   case V_UNION_DECL:
      if (named == NULL && !vlog_has_ident(type))
         fatal_at(vlog_loc(where), "sorry, $typename is not supported for an "
                  "anonymous structure or union");
      vlog_typename_struct(where, tb, type);
      if (named == NULL)
         tb_cat(tb, istr(vlog_ident(type)));
      else
         tb_printf(tb, "%s%s", istr(vlog_ident2(named)),
                   istr(vlog_ident(named)));
      return;
   case V_CLASS_DECL:
      vlog_typename_class(where, tb, type);
      return;
   case V_DATA_TYPE:
      break;
   default:
      fatal_at(vlog_loc(where), "sorry, $typename is not supported for this "
               "type");
   }

   const bool is_signed = !!(vlog_flags(type) & VLOG_F_SIGNED);
   bool atom = false, vector = false;
   switch (vlog_subkind(type)) {
   case DT_BIT:       tb_cat(tb, "bit"); vector = true; break;
   case DT_IMPLICIT:
   case DT_LOGIC:
      tb_cat(tb, "logic");
      vector = true;
      break;
   case DT_BYTE:      tb_cat(tb, "byte"); atom = true; break;
   case DT_SHORTINT:  tb_cat(tb, "shortint"); atom = true; break;
   case DT_INT:       tb_cat(tb, "int"); atom = true; break;
   case DT_LONGINT:   tb_cat(tb, "longint"); atom = true; break;
   case DT_INTEGER:   tb_cat(tb, "integer"); atom = true; break;
   case DT_TIME:      tb_cat(tb, "time"); atom = true; break;
   case DT_REAL:
   case DT_REALTIME:  tb_cat(tb, "real"); break;
   case DT_SHORTREAL: tb_cat(tb, "shortreal"); break;
   case DT_STRING:    tb_cat(tb, "string"); break;
   case DT_EVENT:     tb_cat(tb, "event"); break;
   case DT_CHANDLE:   tb_cat(tb, "chandle"); break;
   case DT_MAILBOX:
      tb_cat(tb, "class std::mailbox #(");
      if (vlog_has_type(type)) {
         vlog_node_t et = vlog_type(type);
         if (!(vlog_kind(et) == V_DATA_TYPE && vlog_subkind(et) == DT_INT
               && !(vlog_flags(et) & VLOG_F_SIGNED)))
            vlog_typename_node(where, tb, et);
      }
      tb_append(tb, ')');
      return;
   case DT_SEMAPHORE:
      tb_cat(tb, "class std::semaphore");
      return;
   case DT_QUEUE:
   case DT_DYNARRAY:
      vlog_typename_node(where, tb, vlog_type(type));
      tb_append(tb, '$');
      if (vlog_subkind(type) == DT_QUEUE)
         tb_cat(tb, "[$]");
      else if (vlog_ranges(type) > 0)
         vlog_typename_ranges(where, tb, type, true);
      else
         tb_cat(tb, "[]");
      return;
   case DT_ASSOC:
      vlog_typename_node(where, tb, vlog_type(type));
      tb_cat(tb, "$[");
      if (vlog_has_ref(type))
         vlog_typename_node(where, tb, vlog_ref(type));
      else
         tb_append(tb, '*');
      tb_append(tb, ']');
      return;
   default:
      fatal_at(vlog_loc(where), "sorry, $typename is not supported for this "
               "type");
   }

   (void)vector;

   if (atom) {
      if (!is_signed)
         tb_cat(tb, " unsigned");
   }
   else if (is_signed && vlog_subkind(type) != DT_REAL)
      tb_cat(tb, " signed");

   if (!atom && vlog_has_type(type)) {
      // A packed array of other types
      vlog_typename_node(where, tb, vlog_type(type));
   }

   if (!atom)
      vlog_typename_ranges(where, tb, type, false);
}

typedef struct {
   char text[128];
   bool real;
   bool str;
   bool four;
   bool sign;
   int  width;
} typename_t;

static void vlog_typename_vector(typename_t *t, bool four, bool sign, int width)
{
   t->real = t->str = false;
   t->four = four;
   t->sign = sign;
   t->width = width;
   snprintf(t->text, sizeof(t->text), "%s%s", four ? "logic" : "bit",
            sign ? " signed" : "");
   if (width > 1)
      snprintf(t->text + strlen(t->text), sizeof(t->text) - strlen(t->text),
               "[%d:0]", width - 1);
}

static void vlog_typename_of(vlog_node_t where, vlog_node_t e, typename_t *t);

static void vlog_typename_same(typename_t *t, const typename_t *a,
                               const typename_t *b, bool four, bool sign,
                               int width)
{
   // The result has the type of an operand with the same properties
   if (!a->real && !a->str && a->four == four && a->sign == sign
       && a->width == width)
      *t = *a;
   else if (!b->real && !b->str && b->four == four && b->sign == sign
            && b->width == width)
      *t = *b;
   else
      vlog_typename_vector(t, four, sign, width);
}

static void vlog_typename_of(vlog_node_t where, vlog_node_t e, typename_t *t)
{
   memset(t, 0, sizeof(*t));

   switch (vlog_kind(e)) {
   case V_NUMBER:
      {
         number_t n = vlog_number(e);
         const int width = number_width(n);
         vlog_typename_vector(t, !number_is_defined(n), number_signed(n),
                              width);
         if (width == 1 && !t->four && !t->sign)
            snprintf(t->text, sizeof(t->text), "bit");
         return;
      }
   case V_REAL:
      t->real = true;
      snprintf(t->text, sizeof(t->text), "real");
      return;
   case V_STRING:
      t->str = true;
      snprintf(t->text, sizeof(t->text), "string");
      return;
   case V_UNARY:
      {
         typename_t a;
         vlog_typename_of(where, vlog_value(e), &a);
         switch (vlog_subkind(e)) {
         case V_UNARY_IDENTITY:
         case V_UNARY_NEG:
         case V_UNARY_BITNEG:
            *t = a;
            return;
         default:
            vlog_typename_vector(t, a.four, false, 1);
            return;
         }
      }
   case V_BINARY:
      {
         typename_t a, b;
         vlog_typename_of(where, vlog_left(e), &a);
         vlog_typename_of(where, vlog_right(e), &b);
         if (a.real || b.real) {
            t->real = true;
            snprintf(t->text, sizeof(t->text), "real");
            return;
         }

         const bool four = a.four || b.four;
         switch (vlog_subkind(e)) {
         case V_BINARY_LOG_EQ:
         case V_BINARY_LOG_NEQ:
         case V_BINARY_CASE_EQ:
         case V_BINARY_CASE_NEQ:
         case V_BINARY_LOG_OR:
         case V_BINARY_LOG_AND:
         case V_BINARY_LT:
         case V_BINARY_GT:
         case V_BINARY_LEQ:
         case V_BINARY_GEQ:
            vlog_typename_vector(t, vlog_subkind(e) >= V_BINARY_CASE_EQ
                                 && vlog_subkind(e) <= V_BINARY_CASE_NEQ
                                 ? false : four, false, 1);
            return;
         case V_BINARY_SHIFT_LL:
         case V_BINARY_SHIFT_RL:
         case V_BINARY_SHIFT_LA:
         case V_BINARY_SHIFT_RA:
            *t = a;
            return;
         case V_BINARY_EXP:
            // The right operand is self-determined (11.6.1)
            *t = a;
            return;
         default:
            vlog_typename_same(t, &a, &b, four, a.sign && b.sign,
                               MAX(a.width, b.width));
            return;
         }
      }
   case V_COND_EXPR:
      {
         typename_t a, b;
         vlog_typename_of(where, vlog_left(e), &a);
         vlog_typename_of(where, vlog_right(e), &b);
         if (a.real || b.real || a.str || b.str) {
            *t = a;
            return;
         }
         vlog_typename_same(t, &a, &b, a.four || b.four, a.sign && b.sign,
                            MAX(a.width, b.width));
         return;
      }
   case V_CONCAT:
      {
         int width = 0;
         bool four = false, str = false;
         for (int i = 0; i < vlog_params(e); i++) {
            typename_t p;
            vlog_typename_of(where, vlog_param(e, i), &p);
            width += p.width;
            four |= p.four;
            str |= p.str;
         }

         if (str) {
            t->str = true;
            snprintf(t->text, sizeof(t->text), "string");
            return;
         }

         int64_t repeat;
         if (vlog_has_value(e) && vlog_get_const(vlog_value(e), &repeat))
            width *= repeat;

         vlog_typename_vector(t, four, false, width);
         return;
      }
   case V_PREFIX:
   case V_POSTFIX:
      vlog_typename_of(where, vlog_target(e), t);
      return;
   case V_SYS_FCALL:
      {
         const ident_t id = vlog_ident(e);
         if (id == ident_new("$time")) {
            t->four = true;
            t->width = 64;
            snprintf(t->text, sizeof(t->text), "time");
         }
         else if (id == ident_new("$realtime") || id == ident_new("$bitstoreal")
                  || id == ident_new("$itor")) {
            t->real = true;
            snprintf(t->text, sizeof(t->text), "real");
         }
         else if (id == ident_new("$random") || id == ident_new("$clog2"))
            vlog_typename_vector(t, true, true, 32);
         else if (id == ident_new("$urandom")
                  || id == ident_new("$urandom_range")) {
            // Declared as returning int unsigned (18.13.1, 18.13.2)
            t->width = 32;
            snprintf(t->text, sizeof(t->text), "int unsigned");
         }
         else if (id == ident_new("$bits"))
            vlog_typename_vector(t, false, true, 32);
         else if ((id == ident_new("$signed") || id == ident_new("$unsigned"))
                  && vlog_params(e) == 1) {
            typename_t a;
            vlog_typename_of(where, vlog_param(e, 0), &a);
            vlog_typename_vector(t, a.four, id == ident_new("$signed"),
                                 a.width);
         }
         else
            fatal_at(vlog_loc(where), "sorry, $typename is not supported "
                     "for this expression");
         return;
      }
   case V_CAST:
      {
         typename_t a;
         vlog_typename_of(where, vlog_value(e), &a);
         if (vlog_subkind(e) == V_CAST_WIDTH) {
            vlog_typename_vector(t, a.four, a.sign, vlog_width(e));
            return;
         }

         LOCAL_TEXT_BUF tb = tb_new();
         vlog_typename_node(where, tb, vlog_type(e));
         snprintf(t->text, sizeof(t->text), "%s", tb_get(tb));
         t->four = !vlog_is_two_state(vlog_get_type(vlog_type(e)));
         t->sign = vlog_is_signed(e);
         t->width = vlog_width(e);
         return;
      }
   default:
      break;
   }

   // A variable, net, member or element
   vlog_node_t decl = NULL;
   if (vlog_kind(e) == V_REF && vlog_has_ref(e))
      decl = vlog_ref(e);
   else if (vlog_kind(e) == V_MEMBER_REF)
      decl = vlog_member_decl(e);

   if (vlog_kind(e) == V_BIT_SELECT || vlog_kind(e) == V_PART_SELECT) {
      typename_t a;
      vlog_typename_of(where, vlog_value(e), &a);
      if (a.real || a.str) {
         *t = a;
         return;
      }

      if (vlog_kind(e) == V_BIT_SELECT) {
         vlog_node_t type = vlog_get_type(e);
         vlog_node_t btype = vlog_get_type(vlog_value(e));
         if (btype != NULL && vlog_kind(btype) == V_DATA_TYPE
             && (vlog_subkind(btype) == DT_QUEUE
                 || vlog_subkind(btype) == DT_ASSOC
                 || vlog_subkind(btype) == DT_DYNARRAY)) {
            // An element of a container has the type it holds
            LOCAL_TEXT_BUF tb = tb_new();
            vlog_typename_node(where, tb, vlog_type(btype));
            snprintf(t->text, sizeof(t->text), "%s", tb_get(tb));
            t->four = !vlog_is_two_state(vlog_get_type(vlog_type(btype)));
            t->width = vlog_width(e);
            t->sign = vlog_is_signed(e);
            return;
         }

         if (type != NULL && vlog_has_ref(vlog_value(e))
             && vlog_ranges(vlog_ref(vlog_value(e))) >= vlog_params(e)
             && vlog_params(e) > 0) {
            // An element of an unpacked array keeps the dimensions that
            // are not selected
            vlog_node_t decl = vlog_ref(vlog_value(e));
            LOCAL_TEXT_BUF tb = tb_new();
            vlog_typename_node(where, tb, vlog_type(decl));
            if (vlog_ranges(decl) > vlog_params(e)) {
               tb_cat(tb, "$");
               vlog_typename_ranges_from(where, tb, decl, vlog_params(e));
            }
            snprintf(t->text, sizeof(t->text), "%s", tb_get(tb));
            t->four = !vlog_is_two_state(type);
            t->width = vlog_width(e);
            t->sign = vlog_is_signed(e);
            return;
         }

         vlog_typename_vector(t, a.four, false, vlog_width(e));
         return;
      }

      int64_t left, right;
      const bool is_const = vlog_subkind(e) == V_RANGE_CONST
         && vlog_get_const(vlog_left(e), &left)
         && vlog_get_const(vlog_right(e), &right);

      vlog_typename_vector(t, a.four, false, vlog_width(e));

      // The selection is of the first packed dimension of an array so the
      // remaining packed dimensions are kept (7.4.3, 11.5.1)
      const char *tail = a.text + (a.four ? 5 : 3);
      if (strncmp(a.text, a.four ? "logic" : "bit", tail - a.text) == 0) {
         if (strncmp(tail, " signed", 7) == 0)
            tail += 7;
         if (*tail == ' ')
            tail++;

         int ndims = 0;
         int64_t inner = 1;
         for (const char *p = tail; *p == '['; ndims++) {
            int64_t l, r;
            int n;
            if (sscanf(p, "[%"SCNi64":%"SCNi64"]%n", &l, &r, &n) != 2)
               break;
            if (ndims > 0)
               inner *= llabs(l - r) + 1;
            p += n;
            tail = p;
         }

         if (ndims > 1 && (tail - a.text) > 0 && *tail == '\0') {
            const char *dims = strchr(a.text, '[');
            dims = strchr(dims + 1, '[');
            int64_t w = vlog_width(e) / inner;
            if (!is_const && vlog_get_const(vlog_right(e), &w))
               w = MAX(w, 1);
            if (is_const)
               snprintf(t->text, sizeof(t->text), "%s[%"PRIi64":%"PRIi64"]%s",
                        a.four ? "logic" : "bit", left, right, dims);
            else
               snprintf(t->text, sizeof(t->text), "%s[%"PRIi64":0]%s",
                        a.four ? "logic" : "bit", w - 1, dims);
            return;
         }
      }

      if (is_const)
         snprintf(t->text, sizeof(t->text), "%s[%"PRIi64":%"PRIi64"]",
                  a.four ? "logic" : "bit", left, right);
      return;
   }

   if (vlog_kind(e) == V_METHOD_CALL) {
      vlog_typename_vector(t, false, true, 32);
      snprintf(t->text, sizeof(t->text), "int");
      t->width = 32;
      return;
   }

   if (vlog_kind(e) == V_USER_FCALL && vlog_has_ref(e)
       && vlog_kind(vlog_ref(e)) == V_FUNC_DECL && vlog_has_type(vlog_ref(e)))
      decl = vlog_ref(e);

   if (decl == NULL)
      fatal_at(vlog_loc(where), "sorry, $typename is not supported for this "
               "expression");

   LOCAL_TEXT_BUF tb = tb_new();
   const bool is_type = vlog_kind(decl) == V_CLASS_DECL
      || vlog_kind(decl) == V_TYPE_DECL;
   vlog_node_t type = is_type ? decl : vlog_type(decl);
   vlog_typename_node(where, tb, type);

   if (!is_type && vlog_kind(decl) != V_FUNC_DECL && vlog_ranges(decl) > 0) {
      tb_cat(tb, "$");
      vlog_typename_ranges(where, tb, decl, true);
   }

   snprintf(t->text, sizeof(t->text), "%s", tb_get(tb));

   if (is_type && vlog_kind(decl) == V_TYPE_DECL) {
      // A data type given as the argument, perhaps scoped by a class
      t->four = !vlog_is_two_state(decl);
      t->sign = vlog_is_signed(decl);
      t->width = vlog_size(decl);
      return;
   }

   t->real = vlog_is_real(e);
   t->str = vlog_is_string(e);
   vlog_node_t vt = vlog_get_type(e);
   t->four = vt == NULL || !vlog_is_two_state(vt);
   t->sign = vlog_is_signed(e);
   t->width = vlog_width(e);

   if (vlog_kind(e) == V_MEMBER_REF && !t->real && !t->str) {
      // A member of a packed structure is four state if any member is
      vlog_node_t pt = vlog_get_type(vlog_value(e));
      if (pt != NULL && (vlog_kind(pt) == V_STRUCT_DECL
                         || vlog_kind(pt) == V_UNION_DECL)
          && (vlog_flags(pt) & VLOG_F_PACKED) && !vlog_is_two_state(pt))
         vlog_typename_vector(t, true, t->sign, t->width);
   }
}

static mir_value_t vlog_lower_typename(vlog_gen_t *g, vlog_node_t v)
{
   if (vlog_params(v) < 1)
      fatal_at(vlog_loc(v), "wrong number of arguments to %pQ", vlog_ident(v));

   typename_t t;
   vlog_typename_of(v, vlog_param(v, 0), &t);

   number_t n = number_from_string(t.text, strlen(t.text));
   return vlog_string_const(g, n);
}

static mir_value_t vlog_lower_isunbounded(vlog_gen_t *g, vlog_node_t v)
{
   // A parameter is unbounded if it was given the value $

   if (vlog_params(v) != 1)
      fatal_at(vlog_loc(v), "wrong number of arguments to %pQ", vlog_ident(v));

   vlog_node_t arg = vlog_param(v, 0);
   while (vlog_kind(arg) == V_REF && vlog_has_ref(arg)) {
      vlog_node_t decl = vlog_ref(arg);
      if (vlog_kind(decl) != V_PARAM_DECL && vlog_kind(decl) != V_LOCALPARAM)
         break;
      else if (!vlog_has_value(decl))
         break;

      arg = vlog_value(decl);
   }

   mir_type_t t_integer = mir_vec4_type(g->mu, 32, true);
   return mir_const_vec(g->mu, t_integer, vlog_kind(arg) == V_DOLLAR, 0);
}

static mir_value_t vlog_sva_steady(vlog_gen_t *g)
{
   // True if every value remembered for the sampled value functions is the
   // current value of its argument so the next tick sees the same values
   // if nothing changes

   if (g->past == NULL)
      return MIR_NULL_VALUE;

   mir_type_t t_bool = mir_bool_type(g->mu);
   mir_value_t steady = mir_const(g->mu, t_bool, 1);

   hash_iter_t it = HASH_BEGIN;
   const void *key;
   void *value;
   while (hash_iter(g->past, &it, &key, &value)) {
      sva_past_t *st = value;
      mir_value_t cur = mir_build_cast(g->mu, st->type,
                                       vlog_lower_rvalue(g, st->arg));

      for (int j = 0; j < st->n; j++) {
         mir_value_t old = mir_build_load(g->mu, st->hist[j]);
         mir_value_t eq = mir_build_binary(g->mu, MIR_VEC_CASE_EQ, st->type,
                                           cur, old);
         steady = mir_build_and(g->mu, steady, vlog_lower_test(g, eq));
      }
   }

   mir_value_t var = mir_add_var(g->mu, t_bool, MIR_NULL_STAMP,
                                 ident_uniq("steady"), MIR_VAR_TEMP);
   mir_build_store(g->mu, var, steady);
   return var;
}

static mir_value_t vlog_lower_pfmt(vlog_gen_t *g, vlog_node_t v)
{
   // The elements of an array converted by %p joined into one string

   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_type_t t_char = mir_char_type(g->mu);
   mir_type_t t_str = mir_uarray_type(g->mu, 1, t_char);

   int64_t left, step, plain;
   if (!vlog_get_const(vlog_param(v, 3), &left)
       || !vlog_get_const(vlog_param(v, 4), &step)
       || !vlog_get_const(vlog_param(v, 5), &plain))
      should_not_reach_here();

   mir_value_t zero = mir_const(g->mu, t_offset, 0);

   const bool assoc = vlog_params(v) > 6;

   mir_value_t pairs_arr = MIR_NULL_VALUE, count;
   if (assoc) {
      mir_value_t av = vlog_lower_rvalue(g, vlog_param(v, 6));
      count = mir_build_uarray_len(g->mu, av, 0);
      pairs_arr = mir_build_unwrap(g->mu, av);
   }
   else
      count = vlog_lower_cast(g, t_offset,
                              vlog_lower_rvalue(g, vlog_param(v, 0)));

   vlog_node_t idx = vlog_ref(vlog_param(v, 2));
   const type_info_t *ti = vlog_type_info(g, vlog_type(idx));
   mir_value_t idx_var = vlog_add_local_var(g, idx, ident_uniq("pidx"));

   mir_value_t pieces = vlog_alloc_array(g, t_str, count);

   mir_value_t total = mir_add_var(g->mu, t_offset, MIR_NULL_STAMP,
                                   ident_uniq("total"), MIR_VAR_TEMP);
   mir_build_store(g->mu, total, zero);

   vlog_loop_t l1;
   vlog_loop_begin(g, &l1, zero, count);

   if (assoc) {
      mir_value_t key = mir_build_load(g->mu, mir_build_record_ref(
         g->mu, mir_build_array_ref(g->mu, pairs_arr, l1.i), 0));
      if (mir_get_class(g->mu, ti->type) == MIR_TYPE_UARRAY)
         mir_build_store(g->mu, idx_var, key);
      else
         mir_build_store(g->mu, idx_var, mir_build_cast(g->mu, ti->type, key));
   }
   else {
      mir_value_t pos = mir_build_add(
         g->mu, t_offset, mir_const(g->mu, t_offset, left),
         mir_build_mul(g->mu, t_offset, mir_const(g->mu, t_offset, step),
                       l1.i));
      mir_build_store(g->mu, idx_var, mir_build_cast(g->mu, ti->type, pos));
   }

   mir_value_t piece = vlog_lower_with_context(g, vlog_param(v, 1), t_str);
   mir_build_store(g->mu, mir_build_array_ref(g->mu, pieces, l1.i), piece);
   mir_value_t plen = mir_build_uarray_len(g->mu, piece, 0);
   mir_build_store(g->mu, total, mir_build_add(g->mu, t_offset,
                                               mir_build_load(g->mu, total),
                                               plen));
   vlog_loop_end(g, &l1);

   // The strings which surround and separate the elements
   const char *open_s = "'{", *sep_s = plain ? "," : ", ", *close_s = "}";

   mir_value_t open = vlog_string_const(
      g, number_from_string(open_s, strlen(open_s)));
   mir_value_t sep = vlog_string_const(
      g, number_from_string(sep_s, strlen(sep_s)));
   mir_value_t close = vlog_string_const(
      g, number_from_string(close_s, strlen(close_s)));

   mir_value_t olen = mir_build_uarray_len(g->mu, open, 0);
   mir_value_t slen = mir_build_uarray_len(g->mu, sep, 0);
   mir_value_t clen = mir_build_uarray_len(g->mu, close, 0);

   mir_value_t nsep = mir_build_select(
      g->mu, t_offset,
      mir_build_cmp(g->mu, MIR_CMP_GT, count, zero),
      mir_build_sub(g->mu, t_offset, count, mir_const(g->mu, t_offset, 1)),
      zero);

   mir_value_t size = mir_build_add(
      g->mu, t_offset, mir_build_load(g->mu, total),
      mir_build_add(g->mu, t_offset, mir_build_add(g->mu, t_offset, olen, clen),
                    mir_build_mul(g->mu, t_offset, nsep, slen)));

   mir_value_t data = vlog_alloc_array(g, t_char, size);

   mir_build_copy(g->mu, mir_build_array_ref(g->mu, data, zero),
                  mir_build_unwrap(g->mu, open), olen);

   mir_value_t out = mir_add_var(g->mu, t_offset, MIR_NULL_STAMP,
                                 ident_uniq("out"), MIR_VAR_TEMP);
   mir_build_store(g->mu, out, olen);

   vlog_loop_t l2;
   vlog_loop_begin(g, &l2, zero, count);

   mir_block_t sep_bb = mir_add_block(g->mu);
   mir_block_t body_bb = mir_add_block(g->mu);
   mir_build_cond(g->mu, mir_build_cmp(g->mu, MIR_CMP_GT, l2.i, zero),
                  sep_bb, body_bb);

   mir_set_cursor(g->mu, sep_bb, MIR_APPEND);
   mir_value_t at = mir_build_load(g->mu, out);
   mir_build_copy(g->mu, mir_build_array_ref(g->mu, data, at),
                  mir_build_unwrap(g->mu, sep), slen);
   mir_build_store(g->mu, out,
                   mir_build_add(g->mu, t_offset, at, slen));
   mir_build_jump(g->mu, body_bb);

   mir_set_cursor(g->mu, body_bb, MIR_APPEND);
   mir_value_t s = mir_build_load(g->mu,
                                  mir_build_array_ref(g->mu, pieces, l2.i));
   mir_value_t len = mir_build_uarray_len(g->mu, s, 0);
   mir_value_t at2 = mir_build_load(g->mu, out);
   mir_build_copy(g->mu, mir_build_array_ref(g->mu, data, at2),
                  mir_build_unwrap(g->mu, s), len);
   mir_build_store(g->mu, out, mir_build_add(g->mu, t_offset, at2, len));
   vlog_loop_end(g, &l2);

   mir_value_t at3 = mir_build_load(g->mu, out);
   mir_build_copy(g->mu, mir_build_array_ref(g->mu, data, at3),
                  mir_build_unwrap(g->mu, close), clen);

   return vlog_wrap_array(g, data, size);
}

static bool vlog_type_bits(vlog_gen_t *g, vlog_node_t type, int64_t *bits);

static bool vlog_decl_bits(vlog_gen_t *g, vlog_node_t decl, int64_t *bits)
{
   // Bits of a member or variable including its unpacked dimensions

   if (!vlog_has_type(decl) || !vlog_type_bits(g, vlog_type(decl), bits))
      return false;

   const int nranges = vlog_ranges(decl);
   for (int i = 0; i < nranges; i++) {
      vlog_node_t r = vlog_range(decl, i);
      if (vlog_subkind(r) != V_DIM_UNPACKED || vlog_size(r) == 0)
         return false;
      *bits *= vlog_size(r);
   }

   return true;
}

static bool vlog_type_bits(vlog_gen_t *g, vlog_node_t type, int64_t *bits)
{
   switch (vlog_kind(type)) {
   case V_TYPE_DECL:
   case V_ENUM_DECL:
      return vlog_type_bits(g, vlog_type(type), bits);
   case V_CLASS_DECL:
      *bits = 64;
      return true;
   case V_STRUCT_DECL:
   case V_UNION_DECL:
      {
         const bool is_union = vlog_kind(type) == V_UNION_DECL;
         int64_t total = 0;
         const int ndecls = vlog_decls(type);
         for (int i = 0; i < ndecls; i++) {
            int64_t member;
            if (!vlog_decl_bits(g, vlog_decl(type, i), &member))
               return false;
            total = is_union ? MAX(total, member) : total + member;
         }

         if (is_union && (vlog_flags(type) & VLOG_F_TAGGED))
            total += ilog2(ndecls);   // The tag

         *bits = total;
         return true;
      }
   case V_DATA_TYPE:
      if (vlog_subkind(type) == DT_DYNARRAY && vlog_ranges(type) > 0) {
         // Fixed size array from a typedef
         int64_t elem;
         if (!vlog_type_bits(g, vlog_type(type), &elem))
            return false;

         *bits = elem * vlog_size(vlog_range(type, 0));
         return true;
      }

      switch (vlog_subkind(type)) {
      case DT_STRING:
      case DT_DYNARRAY:
      case DT_QUEUE:
      case DT_ASSOC:
         return false;
      case DT_EVENT:
      case DT_CHANDLE:
      case DT_MAILBOX:
      case DT_SEMAPHORE:
         *bits = 64;
         return true;
      case DT_REAL:
      case DT_REALTIME:
         *bits = 64;
         return true;
      case DT_SHORTREAL:
         *bits = 32;
         return true;
      default:
         if (vlog_has_type(type)) {
            // A packed array of another type
            int64_t elem;
            if (!vlog_type_bits(g, vlog_type(type), &elem))
               return false;

            const int nranges = vlog_ranges(type);
            for (int i = 0; i < nranges; i++)
               elem *= vlog_size(vlog_range(type, i));

            *bits = elem;
            return true;
         }

         *bits = vlog_type_info(g, type)->size;
         return true;
      }
   default:
      return false;
   }
}

static mir_value_t vlog_lower_sys_fcall(vlog_gen_t *g, vlog_node_t v)
{
   if (vlog_ident(v) == ident_new("$process_self")) {
      mir_type_t t_cell = mir_int_type(g->mu, INT64_MIN, INT64_MAX);
      mir_type_t t_handle = mir_access_type(g->mu, t_cell);
      mir_value_t self = mir_build_fcall(g->mu, ident_new("__sv_process_self"),
                                         t_handle, MIR_NULL_STAMP, NULL, 0);
      mir_build_consume(g->mu, self);
      return self;
   }
   else if (vlog_ident(v) == ident_new("$__pfmt"))
      return vlog_lower_pfmt(g, v);
   else if (vlog_ident(v) == ident_new("$__clsoid")) {
      // The serial number of this object
      mir_type_t t_id = mir_int_type(g->mu, INT64_MIN, INT64_MAX);
      mir_value_t oid = mir_build_load(
         g->mu, mir_build_link_var(g->mu, vlog_lower_this(g),
                                   ident_new("$oid"), t_id));
      return vlog_offset_to_int(g, oid);
   }
   else if (vlog_ident(v) == ident_new("$fread"))
      return vlog_lower_fread(g, v);
   else if (vlog_is_array_query(v))
      return vlog_lower_array_query(g, v);
   else if (vlog_ident(v) == ident_new("$typename"))
      return vlog_lower_typename(g, v);
   else if (vlog_ident(v) == ident_new("$isunbounded"))
      return vlog_lower_isunbounded(g, v);

   sva_sampled_t sampled;
   if (vlog_sva_sampled_kind(v, &sampled))
      return vlog_lower_sampled(g, v);

   if (vlog_ident(v) == ident_new("$isunknown") && vlog_params(v) == 1) {
      mir_value_t arg = vlog_lower_rvalue(g, vlog_param(v, 0));
      if (mir_is_vector(g->mu, arg)) {
         mir_type_t t_bit = mir_vec2_type(g->mu, 1, false);
         mir_type_t t_arg = mir_get_type(g->mu, arg);
         if (mir_get_class(g->mu, t_arg) != MIR_TYPE_VEC4)
            return mir_const_vec(g->mu, t_bit, 0, 0);

         mir_value_t known = mir_build_defined(g->mu, arg);
         return mir_build_select(g->mu, t_bit, known,
                                 mir_const_vec(g->mu, t_bit, 0, 0),
                                 mir_const_vec(g->mu, t_bit, 1, 0));
      }

      return vlog_lower_sys_tfcall(g, v);
   }

   if (vlog_ident(v) == ident_new("$onehot")
       || vlog_ident(v) == ident_new("$onehot0")) {
      mir_value_t result = vlog_lower_sys_tfcall(g, v);
      return mir_build_cast(g->mu, mir_vec2_type(g->mu, 1, false), result);
   }

   const vlog_systf_t subk = vlog_subkind(v);
   switch (subk) {
   case V_SYSTF_SIGNED:
   case V_SYSTF_UNSIGNED:
      {
         mir_value_t arg = vlog_lower_rvalue(g, vlog_param(v, 0));
         mir_type_t arg_type = mir_get_type(g->mu, arg), type;
         switch (mir_get_class(g->mu, arg_type)) {
         case MIR_TYPE_VEC2:
            type = mir_vec2_type(g->mu, mir_get_size(g->mu, arg_type),
                                 subk == V_SYSTF_SIGNED);
            break;
         case MIR_TYPE_VEC4:
            type = mir_vec4_type(g->mu, mir_get_size(g->mu, arg_type),
                                 subk == V_SYSTF_SIGNED);
            break;
         default:
            type = arg_type;
            break;
         }

         return mir_build_cast(g->mu, type, arg);
      }
   case V_SYSTF_BITS:
      {
         mir_type_t t_integer = mir_vec4_type(g->mu, 32, true);
         vlog_node_t param = vlog_param(v, 0);

         int64_t total;
         if (vlog_kind(param) == V_CAST && vlog_subkind(param) == V_CAST_TYPEREF
             && vlog_type_bits(g, vlog_type(param), &total))
            return mir_const_vec(g->mu, t_integer, total, 0);

         if (vlog_kind(param) == V_REF && vlog_has_ref(param)
             && is_data_type(vlog_ref(param))
             && vlog_type_bits(g, vlog_ref(param), &total))
            return mir_const_vec(g->mu, t_integer, total, 0);

         if (vlog_is_string(param)) {
            // A string has a byte for each character
            mir_type_t t_offset = mir_offset_type(g->mu);
            mir_value_t str = vlog_lower_rvalue(g, param);
            mir_value_t len = mir_build_mul(
               g->mu, t_offset, mir_build_uarray_len(g->mu, str, 0),
               mir_const(g->mu, t_offset, 8));
            return vlog_lower_cast(g, t_integer, len);
         }

         vlog_node_t pdims[MAX_UNPACKED_DIMS];
         const int npdims = vlog_unpacked_dims(param, pdims);
         vlog_node_t ptype = vlog_get_type(param);
         const vlog_kind_t pkind = vlog_kind(param);
         if (ptype != NULL && is_data_type(ptype)
             && (npdims > 0 || vlog_kind(ptype) == V_STRUCT_DECL
                 || vlog_kind(ptype) == V_UNION_DECL
                 || pkind == V_REF || pkind == V_MEMBER_REF
                 || pkind == V_HIER_REF)
             && vlog_type_bits(g, ptype, &total)) {
            if (npdims > 0)
               total *= vlog_unpacked_count(pdims, npdims);
            return mir_const_vec(g->mu, t_integer, total, 0);
         }

         if (vlog_kind(param) == V_REF) {
            vlog_node_t decl = vlog_ref(param);
            if (is_data_type(decl)) {
               const type_info_t *ti = vlog_type_info(g, decl);
               return mir_const_vec(g->mu, t_integer, ti->size, 0);
            }
         }

         if (vlog_has_static_shape(param)) {
            bool plain = true;
            if (vlog_kind(param) == V_REF) {
               vlog_node_t decl = vlog_ref(param);
               switch (vlog_kind(decl)) {
               case V_VAR_DECL:
               case V_NET_DECL:
               case V_PORT_DECL:
                  plain = vlog_ranges(decl) == 0;
                  break;
               default:
                  plain = false;
                  break;
               }
            }

            if (plain)
               return mir_const_vec(g->mu, t_integer, vlog_width(param), 0);
         }

         // The operand is not evaluated
         mir_block_t dead_bb = mir_add_block(g->mu);
         mir_block_t next_bb = mir_add_block(g->mu);
         mir_build_cond(g->mu, mir_const(g->mu, mir_bool_type(g->mu), 0),
                        dead_bb, next_bb);

         mir_set_cursor(g->mu, dead_bb, MIR_APPEND);
         mir_value_t arg = vlog_lower_rvalue(g, param);
         const bool vector = mir_is_vector(g->mu, arg);
         const int size =
            vector ? mir_get_size(g->mu, mir_get_type(g->mu, arg)) : 0;
         mir_build_jump(g->mu, next_bb);

         mir_set_cursor(g->mu, next_bb, MIR_APPEND);
         if (vector)
            return mir_const_vec(g->mu, t_integer, size, 0);
         else
            return mir_const_vec(g->mu, t_integer, ~0, ~0);
      }
   default:
      return vlog_lower_sys_tfcall(g, v);
   }
}

static mir_value_t vlog_lower_full_select(vlog_gen_t *g,
                                          const vlog_select_t *select)
{
   if (mir_is_signal(g->mu, select->obj)) {
      mir_value_t data = mir_build_resolved(g->mu, select->obj);
      mir_value_t ptr = mir_build_array_ref(g->mu, data, select->dst_offset);

      switch (mir_get_class(g->mu, select->type)) {
      case MIR_TYPE_VEC2:
      case MIR_TYPE_VEC4:
         if (mir_get_size(g->mu, select->type) == 1) {
            mir_value_t val = mir_build_load(g->mu, ptr);
            return mir_build_pack(g->mu, select->type, val);
         }
         else
            return mir_build_pack(g->mu, select->type, ptr);
      case MIR_TYPE_REAL:
         return mir_build_load(g->mu, ptr);
      default:
         should_not_reach_here();
      }
   }
   else {
      mir_value_t data = select->obj;
      if (mir_is(g->mu, data, MIR_TYPE_POINTER))
         data = mir_build_load(g->mu, data);

      switch (mir_get_class(g->mu, select->type)) {
      case MIR_TYPE_VEC2:
      case MIR_TYPE_VEC4:
         {
            mir_type_t t_data = mir_get_type(g->mu, data);
            if (mir_get_class(g->mu, t_data)
                != mir_get_class(g->mu, select->type)) {
               // Part of a two-state variable
               mir_type_t t_part = mir_vector_slice(
                  g->mu, t_data, mir_get_size(g->mu, select->type));
               mir_value_t part = mir_build_extract(g->mu, t_part, data,
                                                    select->dst_offset);
               return mir_build_cast(g->mu, select->type, part);
            }

            return mir_build_extract(g->mu, select->type, data,
                                     select->dst_offset);
         }
      case MIR_TYPE_CONTEXT:
      case MIR_TYPE_REAL:
      case MIR_TYPE_UARRAY:
      case MIR_TYPE_ACCESS:
         return data;
      default:
         should_not_reach_here();
      }
   }
}

static mir_value_t vlog_lower_partial_select(vlog_gen_t *g,
                                             const vlog_select_t *select)
{
   mir_type_t t_elem = mir_logic_type(g->mu);
   mir_value_t src;
   if (mir_is_signal(g->mu, select->obj)) {
      mir_value_t data = mir_build_resolved(g->mu, select->obj);
      src = mir_build_array_ref(g->mu, data, select->dst_offset);

      if (vlog_is_vhdl_signal(g, select->obj)) {
         mir_type_t t_conv = mir_carray_type(g->mu, select->size, t_elem);
         mir_value_t conv = mir_add_var(g->mu, t_conv, MIR_NULL_STAMP,
                                        ident_uniq("tmp"), MIR_VAR_TEMP);
         vlog_convert_std_logic(g, "__sv_std_to_logic", conv, src,
                                select->count);
         src = conv;
      }
      else
         t_elem = mir_get_elem(g->mu, mir_get_type(g->mu, src));
   }
   else {
      mir_value_t data = select->obj;
      if (mir_is(g->mu, data, MIR_TYPE_POINTER))
         data = mir_build_load(g->mu, data);

      assert(mir_is_vector(g->mu, data));

      const unsigned size = mir_get_size(g->mu, mir_get_type(g->mu, data));
      mir_type_t t_src_array = mir_carray_type(g->mu, size, t_elem);
      mir_value_t src_tmp = mir_add_var(g->mu, t_src_array, MIR_NULL_STAMP,
                                        ident_uniq("tmp"), MIR_VAR_TEMP);

      mir_build_unpack(g->mu, data, 0, src_tmp);
      src = mir_build_array_ref(g->mu, src_tmp, select->dst_offset);
   }

   mir_type_t t_array = mir_carray_type(g->mu, select->size, t_elem);
   mir_value_t tmp = mir_add_var(g->mu, t_array, MIR_NULL_STAMP,
                                 ident_uniq("tmp"), MIR_VAR_TEMP);

   mir_build_unpack(g->mu, select->two_state ? vlog_lower_zero(g, select->type)
                    : vlog_lower_x(g, select->type), 0, tmp);

   mir_value_t dst = mir_build_array_ref(g->mu, tmp, select->src_offset);
   mir_build_copy(g->mu, dst, src, select->count);

   return mir_build_pack(g->mu, select->type, tmp);
}

static mir_value_t vlog_lower_rvalue_select_bits(vlog_gen_t *g, vlog_node_t v)
{
   vlog_select_t select = vlog_lower_select(g, v);

   if (vlog_is_vhdl_signal(g, select.obj))
      return vlog_lower_partial_select(g, &select);

   vlog_node_t dims[MAX_UNPACKED_DIMS];
   const int ndims = vlog_unpacked_dims(v, dims);
   if (ndims > 0 && mir_points_to_vector(g->mu, select.obj))
      return vlog_load_flat_array(g, select.obj,
                                  vlog_unpacked_count(dims, ndims),
                                  vlog_array_elem_info(g, v), false);

   int64_t dst_count_const;
   if (mir_get_const(g->mu, select.count, &dst_count_const)) {
      if (dst_count_const == select.size)
         return vlog_lower_full_select(g, &select);
      else if (dst_count_const == 0)
         return select.two_state ? vlog_lower_zero(g, select.type)
            : vlog_lower_x(g, select.type);
   }
   else if (mir_get_class(g->mu, select.type) == MIR_TYPE_REAL
            && mir_is_signal(g->mu, select.obj)) {
      // An element of an unpacked array of real selected by a variable
      // is zero when the index is out of range
      mir_type_t t_offset = mir_offset_type(g->mu);
      mir_value_t zero = mir_const(g->mu, t_offset, 0);
      mir_value_t in_range =
         mir_build_cmp(g->mu, MIR_CMP_NEQ, select.count, zero);
      mir_value_t off =
         mir_build_select(g->mu, t_offset, in_range, select.dst_offset, zero);
      mir_value_t data = mir_build_resolved(g->mu, select.obj);
      mir_value_t elem =
         mir_build_load(g->mu, mir_build_array_ref(g->mu, data, off));
      mir_value_t none = mir_const_real(g->mu, select.type, 0.0);
      return mir_build_select(g->mu, select.type, in_range, elem, none);
   }
   else if (mir_get_class(g->mu, select.type) == MIR_TYPE_REAL
            && mir_is(g->mu, select.obj, MIR_TYPE_POINTER)) {
      // An element of a real array in memory is zero when the index is
      // out of range
      mir_type_t t_offset = mir_offset_type(g->mu);
      mir_value_t result = mir_add_var(g->mu, select.type, MIR_NULL_STAMP,
                                       ident_uniq("real"), MIR_VAR_TEMP);
      mir_build_store(g->mu, result, mir_const_real(g->mu, select.type, 0.0));

      mir_block_t load_bb = mir_add_block(g->mu);
      mir_block_t done_bb = mir_add_block(g->mu);
      mir_build_cond(g->mu, mir_build_cmp(g->mu, MIR_CMP_NEQ, select.count,
                                          mir_const(g->mu, t_offset, 0)),
                     load_bb, done_bb);

      mir_set_cursor(g->mu, load_bb, MIR_APPEND);
      mir_build_store(g->mu, result, mir_build_load(g->mu, select.obj));
      mir_build_jump(g->mu, done_bb);

      mir_set_cursor(g->mu, done_bb, MIR_APPEND);
      return mir_build_load(g->mu, result);
   }

   return vlog_lower_partial_select(g, &select);
}

static mir_value_t vlog_lower_rvalue_select(vlog_gen_t *g, vlog_node_t v)
{
   mir_value_t result = vlog_lower_rvalue_select_bits(g, v);

   // An element of an unpacked array of a signed type is signed
   if (vlog_kind(v) == V_BIT_SELECT && vlog_is_signed(v)
       && mir_is_vector(g->mu, result)) {
      mir_type_t type = mir_get_type(g->mu, result);
      if (!mir_get_signed(g->mu, type)) {
         const unsigned size = mir_get_size(g->mu, type);
         mir_type_t t_signed = mir_get_class(g->mu, type) == MIR_TYPE_VEC2
            ? mir_vec2_type(g->mu, size, true)
            : mir_vec4_type(g->mu, size, true);
         return mir_build_cast(g->mu, t_signed, result);
      }
   }

   return result;
}

static mir_value_t vlog_context_for_call(vlog_gen_t *g, vlog_node_t v)
{
   if (mir_get_kind(g->mu) == MIR_UNIT_THUNK || g->constfunc)
      return MIR_NULL_VALUE;

   if (vlog_has_ref(v) && (vlog_flags(vlog_ref(v)) & VLOG_F_SPEC)) {
      vlog_note_spec(vlog_ref(v));
      vlog_defer_scope(g->mir, vlog_ident2(vlog_ref(v)));
   }

   return vlog_scope_context(g, vlog_ident2(v));
}

static void vlog_store_pattern_default(vlog_gen_t *g, mir_value_t rec,
                                       int field, vlog_node_t fd,
                                       vlog_node_t def)
{
   // The default of a structure pattern applies to the members of nested
   // structures too

   const type_info_t *fti = vlog_type_info(g, vlog_type(fd));
   mir_value_t ptr = mir_build_record_ref(g->mu, rec, field);

   vlog_node_t ft = vlog_get_type(vlog_type(fd));
   if (ft != NULL && vlog_kind(ft) == V_STRUCT_DECL
       && !(vlog_flags(ft) & VLOG_F_PACKED)
       && mir_get_class(g->mu, fti->type) == MIR_TYPE_ACCESS) {
      mir_value_t sub = mir_build_all(g->mu, mir_build_load(g->mu, ptr));
      const int n = vlog_decls(ft);
      for (int i = 0; i < n; i++)
         vlog_store_pattern_default(g, sub, i, vlog_decl(ft, i), def);
   }
   else {
      mir_value_t fv = vlog_lower_with_context(
         g, def, vlog_assign_context(g, fti->type, def));
      mir_build_store(g->mu, ptr, vlog_lower_cast(g, fti->type, fv));
   }
}

static mir_value_t vlog_match_field(vlog_gen_t *g, mir_value_t val,
                                    vlog_node_t type, int field)
{
   // The value of a member of an unpacked or packed aggregate

   vlog_node_t d = vlog_decl(type, field);
   const type_info_t *fti = vlog_type_info(g, vlog_type(d));

   if (vlog_flags(type) & VLOG_F_PACKED) {
      mir_type_t t_offset = mir_offset_type(g->mu);
      mir_type_t t_val = mir_get_type(g->mu, val);

      const bool two_state = mir_get_class(g->mu, t_val) == MIR_TYPE_VEC2;
      mir_type_t t_part = two_state
         ? mir_vec2_type(g->mu, fti->size, false)
         : mir_vec4_type(g->mu, fti->size, false);

      mir_value_t offset =
         mir_const(g->mu, t_offset, vlog_packed_offset(type, d));
      mir_value_t part = mir_build_extract(g->mu, t_part, val, offset);
      return vlog_lower_cast(g, fti->type, part);
   }

   mir_value_t rec = mir_build_all(g->mu, val);

   if (vlog_is_fixed_unpacked(d) && vlog_size(d) > 1) {
      // Only a variable can be bound to the elements
      mir_type_t t_offset = mir_offset_type(g->mu);
      return mir_build_array_ref(g->mu, mir_build_record_ref(g->mu, rec, field),
                                 mir_const(g->mu, t_offset, 0));
   }

   return mir_build_load(g->mu, mir_build_record_ref(g->mu, rec, field));
}

static void vlog_match_test(vlog_gen_t *g, mir_value_t test,
                            mir_block_t fail_bb)
{
   mir_block_t ok_bb = mir_add_block(g->mu);
   mir_build_cond(g->mu, test, ok_bb, fail_bb);
   mir_set_cursor(g->mu, ok_bb, MIR_APPEND);
}

static void vlog_lower_match_pattern(vlog_gen_t *g, vlog_node_t pat,
                                     mir_value_t val, vlog_case_kind_t kind,
                                     mir_block_t fail_bb)
{
   switch (vlog_subkind(pat)) {
   case V_PAT_WILD:
      break;
   case V_PAT_BIND:
      {
         vlog_node_t d = vlog_ref(pat);
         const type_info_t *ti = vlog_type_info(g, vlog_type(d));

         int hops = 0;
         mir_value_t var = mir_search_object(g->mu, d, &hops);
         assert(!mir_is_null(var) && hops == 0);

         if (mir_is(g->mu, val, MIR_TYPE_POINTER)) {
            // The elements of an array member
            if (!vlog_is_fixed_unpacked(d) || vlog_size(d) < 2)
               fatal_at(vlog_loc(pat), "sorry, an array member can only be "
                        "bound to an array variable");

            mir_type_t t_offset = mir_offset_type(g->mu);
            mir_build_copy(g->mu, mir_build_array_ref(
                              g->mu, var, mir_const(g->mu, t_offset, 0)),
                           val, mir_const(g->mu, t_offset, vlog_size(d)));
            break;
         }

         mir_value_t cast = vlog_lower_cast(g, ti->type, val);
         mir_build_store(g->mu, var, vlog_copy_value(g, cast, vlog_type(d)));
      }
      break;
   case V_PAT_CONST:
      {
         mir_type_t t_val = mir_get_type(g->mu, val);
         if (!mir_is_vector_type(g->mu, t_val))
            fatal_at(vlog_loc(pat), "sorry, only a value of integral type "
                     "can be matched against a constant");

         mir_value_t c = vlog_lower_with_context(g, vlog_value(pat), t_val);

         mir_vec_op_t op = MIR_VEC_CASE_EQ;
         switch (kind) {
         case V_CASE_X: op = MIR_VEC_CASEX_EQ; break;
         case V_CASE_Z: op = MIR_VEC_CASEZ_EQ; break;
         default: break;
         }

         mir_value_t test = mir_build_test(
            g->mu, mir_build_binary(g->mu, op, t_val, val,
                                    vlog_lower_cast(g, t_val, c)));
         vlog_match_test(g, test, fail_bb);
      }
      break;
   case V_PAT_TAGGED:
      {
         vlog_node_t type = vlog_type(pat), member = vlog_ref(pat);
         const int nmembers = vlog_decls(type);
         int tag = 0;
         while (vlog_decl(type, tag) != member)
            tag++;

         mir_type_t t_offset = mir_offset_type(g->mu);
         if (vlog_flags(type) & VLOG_F_PACKED) {
            const unsigned tagbits = vlog_tag_bits(type);
            mir_type_t t_val = mir_get_type(g->mu, val);
            const bool two_state =
               mir_get_class(g->mu, t_val) == MIR_TYPE_VEC2;
            mir_type_t t_tag = two_state
               ? mir_vec2_type(g->mu, tagbits, false)
               : mir_vec4_type(g->mu, tagbits, false);

            mir_value_t tagv = mir_build_extract(
               g->mu, t_tag, val, mir_const(g->mu, t_offset, 0));
            mir_value_t test = mir_build_test(
               g->mu, mir_build_binary(g->mu, MIR_VEC_CASE_EQ, t_tag, tagv,
                                       mir_const_vec(g->mu, t_tag, tag, 0)));
            vlog_match_test(g, test, fail_bb);
         }
         else {
            mir_value_t rec = mir_build_all(g->mu, val);
            mir_value_t tagv = mir_build_load(
               g->mu, mir_build_record_ref(g->mu, rec, nmembers - 1));
            mir_value_t test = mir_build_cmp(
               g->mu, MIR_CMP_EQ, mir_build_cast(g->mu, t_offset, tagv),
               mir_const(g->mu, t_offset, tag));
            vlog_match_test(g, test, fail_bb);
         }

         if (vlog_params(pat) > 0) {
            mir_value_t sub = vlog_match_field(g, val, type, tag);
            vlog_lower_match_pattern(g, vlog_param(pat, 0), sub, kind,
                                     fail_bb);
         }
      }
      break;
   case V_PAT_STRUCT:
      {
         vlog_node_t type = vlog_type(pat);
         const int nparams = vlog_params(pat);
         for (int i = 0; i < nparams; i++) {
            vlog_node_t sub = vlog_param(pat, i);
            if (vlog_subkind(sub) == V_PAT_WILD)
               continue;

            vlog_lower_match_pattern(g, sub, vlog_match_field(g, val, type, i),
                                     kind, fail_bb);
         }
      }
      break;
   }
}

static mir_value_t vlog_lower_matches(vlog_gen_t *g, vlog_node_t v)
{
   const int ndecls = vlog_decls(v);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(v, i);
      vlog_lower_one_local(g, d, vlog_ident(d));
   }

   mir_value_t val = vlog_lower_rvalue(g, vlog_value(v));

   mir_type_t t_bit = mir_vec2_type(g->mu, 1, false);
   mir_value_t result = mir_add_var(g->mu, t_bit, MIR_NULL_STAMP,
                                    ident_uniq("matches"), MIR_VAR_TEMP);
   mir_build_store(g->mu, result, mir_const_vec(g->mu, t_bit, 0, 0));

   mir_block_t fail_bb = mir_add_block(g->mu);

   vlog_lower_match_pattern(g, vlog_left(v), val, vlog_subkind(v), fail_bb);

   mir_build_store(g->mu, result, mir_const_vec(g->mu, t_bit, 1, 0));
   mir_build_jump(g->mu, fail_bb);

   mir_set_cursor(g->mu, fail_bb, MIR_APPEND);
   return mir_build_load(g->mu, result);
}

static bool vlog_is_fill_literal(vlog_node_t v)
{
   while (vlog_kind(v) == V_MIN_TYP_MAX)
      v = vlog_value(v);

   return vlog_kind(v) == V_NUMBER && (vlog_flags(v) & VLOG_F_FILL);
}

static void vlog_store_array_member(vlog_gen_t *g, mir_value_t rec,
                                    int field, vlog_node_t fd,
                                    vlog_node_t value)
{
   if (vlog_kind(value) != V_PATTERN_EXPR)
      fatal_at(vlog_loc(value), "sorry, only an assignment pattern "
               "can initialise an array in a structure");

   const type_info_t *fti = vlog_type_info(g, vlog_type(fd));

   vlog_node_t dims[MAX_UNPACKED_DIMS];
   const int ndims = MIN(vlog_ranges(fd), MAX_UNPACKED_DIMS);
   for (int j = 0; j < ndims; j++)
      dims[j] = vlog_range(fd, j);

   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_value_t flat = vlog_lower_array_pattern(g, value, dims, ndims, fti);
   mir_value_t ptr = mir_build_array_ref(
      g->mu, mir_build_record_ref(g->mu, rec, field),
      mir_const(g->mu, t_offset, 0));
   mir_type_t t_flat = mir_vec4_type(g->mu, vlog_size(fd) * fti->size, false);
   vlog_store_flat_array(g, ptr, vlog_size(fd), fti,
                         vlog_lower_cast(g, t_flat, flat));
}

static mir_value_t vlog_lower_tagged_expr(vlog_gen_t *g, vlog_node_t v,
                                          const type_info_t *ti,
                                          vlog_node_t base)
{
   // tagged member [ value ]

   vlog_node_t item = vlog_param(v, 0);
   vlog_node_t member = vlog_ref(vlog_left(item));

   const int nmembers = vlog_decls(base);
   int tag = 0;
   while (tag < nmembers && vlog_decl(base, tag) != member)
      tag++;
   assert(tag < nmembers);

   const type_info_t *fti = vlog_type_info(g, vlog_type(member));

   if (!(vlog_flags(base) & VLOG_F_PACKED)) {
      mir_value_t acc = vlog_lower_default_value(g, ti);
      mir_value_t rec = mir_build_all(g->mu, acc);

      if (vlog_is_fixed_unpacked(member) && vlog_size(member) > 1)
         vlog_store_array_member(g, rec, tag, member, vlog_right(item));
      else {
         mir_value_t fv = vlog_lower_with_context(
            g, vlog_right(item),
            vlog_assign_context(g, fti->type, vlog_right(item)));
         mir_build_store(g->mu, mir_build_record_ref(g->mu, rec, tag),
                         vlog_lower_cast(g, fti->type, fv));
      }

      const type_info_t *tti = vlog_type_info(
         g, vlog_type(vlog_decl(base, nmembers - 1)));
      mir_build_store(g->mu, mir_build_record_ref(g->mu, rec, nmembers - 1),
                      mir_const_vec(g->mu, tti->type, tag, 0));
      return acc;
   }

   const unsigned tagbits = vlog_tag_bits(base);
   const unsigned width = ti->size;

   // The bits between the tag and a short member are unknown
   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_value_t result = vlog_lower_default_value(g, ti);

   const bool two_state = mir_get_class(g->mu, ti->type) == MIR_TYPE_VEC2;

   mir_value_t fv = vlog_lower_rvalue(g, vlog_right(item));
   mir_value_t cast = vlog_lower_cast(g, fti->type, fv);
   mir_value_t uns = vlog_cast_unsigned(g, cast);
   uns = mir_build_cast(g->mu, two_state
                        ? mir_vec2_type(g->mu, fti->size, false)
                        : mir_vec4_type(g->mu, fti->size, false), uns);
   result = mir_build_insert(g->mu, uns, result,
                             mir_const(g->mu, t_offset, width - fti->size));

   mir_type_t t_tag = two_state ? mir_vec2_type(g->mu, tagbits, false)
      : mir_vec4_type(g->mu, tagbits, false);
   mir_value_t tagv = mir_const_vec(g->mu, t_tag, tag, 0);
   return mir_build_insert(g->mu, tagv, result, mir_const(g->mu, t_offset, 0));
}

static vlog_node_t *vlog_pattern_items(vlog_node_t v, int *count)
{
   // The items of an assignment pattern with replications expanded

   const int nparams = vlog_params(v);

   int total = 0;
   for (int i = 0; i < nparams; i++) {
      vlog_node_t item = vlog_param(v, i);
      if (vlog_subkind(item) == 2)
         total += vlog_must_be_const(vlog_left(item))
            * vlog_params(vlog_right(item));
      else
         total++;
   }

   vlog_node_t *items = xmalloc_array(MAX(total, 1), sizeof(vlog_node_t));

   int pos = 0;
   for (int i = 0; i < nparams; i++) {
      vlog_node_t item = vlog_param(v, i);
      if (vlog_subkind(item) == 2) {
         vlog_node_t list = vlog_right(item);
         const int64_t reps = vlog_must_be_const(vlog_left(item));
         for (int64_t j = 0; j < reps; j++) {
            for (int k = 0; k < vlog_params(list); k++)
               items[pos++] = vlog_param(list, k);
         }
      }
      else
         items[pos++] = item;
   }

   *count = total;
   return items;
}

static void vlog_type_signature(text_buf_t *tb, vlog_node_t type)
{
   // Types match if they are the same declaration or built from the same
   // data type and dimensions

   switch (vlog_kind(type)) {
   case V_TYPE_DECL:
      vlog_type_signature(tb, vlog_type(type));
      return;
   case V_STRUCT_DECL:
   case V_UNION_DECL:
   case V_ENUM_DECL:
   case V_CLASS_DECL:
      {
         const loc_t *loc = vlog_loc(type);
         tb_printf(tb, "%s@%s:%u",
                   vlog_has_ident(type) ? istr(vlog_ident(type)) : "",
                   loc_file_str(loc), loc->first_line);
         return;
      }
   case V_DATA_TYPE:
      {
         tb_printf(tb, "dt%d%s", vlog_subkind(type),
                   vlog_is_signed(type) ? "s" : "u");
         if (vlog_has_type(type)) {
            tb_append(tb, '(');
            vlog_type_signature(tb, vlog_type(type));
            tb_append(tb, ')');
         }

         const int nranges = vlog_ranges(type);
         for (int i = 0; i < nranges; i++) {
            vlog_node_t r = vlog_range(type, i);
            int64_t left, right;
            if (vlog_range_bound(vlog_left(r), &left)
                && vlog_range_bound(vlog_right(r), &right))
               tb_printf(tb, "[%d:%"PRIi64":%"PRIi64"]", vlog_subkind(r), left,
                         right);
            else
               tb_cat(tb, "[?]");
         }
         return;
      }
   default:
      tb_cat(tb, "?");
      return;
   }
}

static uint32_t vlog_type_hash(vlog_node_t type)
{
   LOCAL_TEXT_BUF tb = tb_new();
   vlog_type_signature(tb, type);

   uint32_t hash = UINT32_C(2166136261);
   for (const char *p = tb_get(tb); *p; p++)
      hash = (hash ^ (uint8_t)*p) * UINT32_C(16777619);

   return hash;
}

static mir_value_t vlog_lower_pattern_expr(vlog_gen_t *g, vlog_node_t v,
                                           mir_type_t context)
{
   vlog_node_t dt = vlog_type(v);
   const type_info_t *ti = vlog_type_info(g, dt);

   vlog_node_t base = vlog_get_type(dt);
   if (vlog_kind(base) == V_UNION_DECL && (vlog_flags(base) & VLOG_F_TAGGED))
      return vlog_lower_tagged_expr(g, v, ti, base);

   int nitems;
   vlog_node_t *items LOCAL = vlog_pattern_items(v, &nitems);

   if (vlog_kind(base) == V_STRUCT_DECL
       && !(vlog_flags(base) & VLOG_F_PACKED)) {
      // Positional or named assignment to a new unpacked structure
      mir_value_t acc = vlog_lower_default_value(g, ti);
      mir_value_t rec = mir_build_all(g->mu, acc);

      const int nparams = nitems;
      const int ndecls = vlog_decls(base);

      bool *covered LOCAL = xcalloc_array(ndecls, sizeof(bool));
      vlog_node_t def = NULL;
      for (int i = 0; i < nparams; i++) {
         vlog_node_t item = items[i];
         if (vlog_subkind(item) == 1)
            def = vlog_right(item);
         else if (vlog_has_left(item)) {
            vlog_node_t key = vlog_left(item);
            for (int f = 0; f < ndecls; f++) {
               if (vlog_decl(base, f) == vlog_ref(key))
                  covered[f] = true;
            }
         }
         else if (i < ndecls)
            covered[i] = true;
      }

      for (int i = 0; i < nparams; i++) {
         vlog_node_t item = items[i];
         if (vlog_subkind(item) == 1)
            continue;

         int field = i;
         if (vlog_has_left(item)) {
            vlog_node_t key = vlog_left(item);
            for (field = 0; vlog_decl(base, field) != vlog_ref(key); field++);
         }

         vlog_node_t fd = vlog_decl(base, field);
         const type_info_t *fti = vlog_type_info(g, vlog_type(fd));

         if (vlog_is_fixed_unpacked(fd) && vlog_size(fd) > 1) {
            vlog_store_array_member(g, rec, field, fd, vlog_right(item));
            continue;
         }

         mir_value_t fv = vlog_lower_with_context(
            g, vlog_right(item),
            vlog_assign_context(g, fti->type, vlog_right(item)));
         mir_build_store(g->mu, mir_build_record_ref(g->mu, rec, field),
                         vlog_lower_cast(g, fti->type, fv));
      }

      if (def != NULL) {
         for (int f = 0; f < ndecls; f++) {
            if (!covered[f])
               vlog_store_pattern_default(g, rec, f, vlog_decl(base, f), def);
         }
      }

      return acc;
   }

   if (vlog_kind(base) == V_DATA_TYPE && vlog_subkind(base) == DT_ASSOC) {
      // Assignment to a new associative array with an index for each element
      mir_value_t tmp = mir_add_var(g->mu, ti->type, MIR_NULL_STAMP,
                                    ident_uniq("assoc"), MIR_VAR_TEMP);
      mir_build_store(g->mu, tmp, vlog_empty_array(g, ti->type));

      const type_info_t *eti = vlog_type_info(g, vlog_type(base));
      vlog_node_t etype = vlog_type(base);

      vlog_node_t default_item = NULL;
      const int nitems = vlog_params(v);
      for (int i = 0; i < nitems; i++) {
         vlog_node_t item = vlog_param(v, i);
         if (vlog_subkind(item) == 1) {
            default_item = item;
            continue;
         }
         else if (!vlog_has_left(item) || vlog_subkind(item) == 2)
            fatal_at(vlog_loc(item), "an associative array literal must "
                     "have an index for each element");

         mir_value_t key = vlog_assoc_key_value(g, base, vlog_left(item));
         mir_value_t elem = vlog_lower_with_context(
            g, vlog_right(item),
            vlog_assign_context(g, eti->type, vlog_right(item)));
         elem = vlog_copy_value(g, vlog_lower_cast(g, eti->type, elem),
                                etype);
         mir_value_t ptr = vlog_assoc_element(g, tmp, base, key, true, NULL);
         mir_build_store(g->mu, ptr, elem);
      }

      if (default_item != NULL) {
         mir_value_t dv = vlog_lower_with_context(
            g, vlog_right(default_item),
            vlog_assign_context(g, eti->type, vlog_right(default_item)));
         dv = vlog_copy_value(g, vlog_lower_cast(g, eti->type, dv), etype);
         vlog_assoc_set_default(g, tmp, dv);
      }

      return mir_build_load(g->mu, tmp);
   }

   if (vlog_kind(base) == V_DATA_TYPE
       && (vlog_subkind(base) == DT_DYNARRAY
           || vlog_subkind(base) == DT_QUEUE)) {
      // Positional assignment to a new dynamic array
      const type_info_t *eti = vlog_type_info(g, vlog_type(base));
      mir_type_t t_offset = mir_offset_type(g->mu);

      if (vlog_ranges(base) == 1) {
         // Fixed size array declared with a type name where the items can
         // be positional or keyed by index
         vlog_node_t dim = vlog_range(base, 0);
         const unsigned nelems = vlog_size(dim);

         vlog_node_t *which LOCAL = xmalloc_array(nelems, sizeof(vlog_node_t));
         vlog_node_t def;
         vlog_array_pattern_slots(v, dim, which, &def);

         mir_value_t count = mir_const(g->mu, t_offset, nelems);
         mir_value_t data = vlog_alloc_array(g, eti->type, count);
         vlog_init_elements(g, data, mir_const(g->mu, t_offset, 0), count, eti);

         for (int i = 0; i < nelems; i++) {
            vlog_node_t expr = which[i] != NULL ? which[i] : def;
            if (expr == NULL)
               continue;

            mir_value_t elem = vlog_lower_with_context(
               g, expr, vlog_assign_context(g, eti->type, expr));
            mir_value_t pos = mir_const(g->mu, t_offset, i);
            mir_build_store(g->mu, mir_build_array_ref(g->mu, data, pos),
                            vlog_copy_value(g, vlog_lower_cast(g, eti->type, elem),
                                            vlog_type(base)));
         }

         return vlog_wrap_array(g, data, count);
      }

      mir_value_t count = mir_const(g->mu, t_offset, nitems);
      mir_value_t data = vlog_alloc_array(g, eti->type, count);

      for (int i = 0; i < nitems; i++) {
         vlog_node_t item = items[i];
         if (vlog_has_left(item))
            fatal_at(vlog_loc(item), "sorry, only positional patterns are "
                     "supported for arrays");

         mir_value_t elem = vlog_lower_with_context(
            g, vlog_right(item),
            vlog_assign_context(g, eti->type, vlog_right(item)));
         mir_value_t pos = mir_const(g->mu, t_offset, i);
         mir_build_store(g->mu, mir_build_array_ref(g->mu, data, pos),
                         vlog_copy_value(g, vlog_lower_cast(g, eti->type, elem),
                                         vlog_type(base)));
      }

      return vlog_wrap_array(g, data, count);
   }

   mir_value_t result = vlog_lower_x(g, ti->type);

   const int width = mir_get_size(g->mu, mir_get_type(g->mu, result));

   vlog_node_t dim = NULL;
   if (vlog_kind(dt) == V_DATA_TYPE)
      dim = vlog_range(dt, 0);

   const int nparams = nitems;
   mir_value_t *vals LOCAL = xmalloc_array(nparams, sizeof(mir_value_t));
   unsigned *keys LOCAL = xmalloc_array(nparams, sizeof(unsigned));
   mir_value_t def = MIR_NULL_VALUE;
   vlog_node_t defnode = NULL;
   for (int i = 0; i < nparams; i++) {
      vlog_node_t item = items[i];
      vals[i] = vlog_lower_rvalue(g, vlog_right(item));
      keys[i] = UINT_MAX;

      if (!vlog_has_left(item) && vlog_subkind(item) == 0)
         keys[i] = i;   // Positional element of a packed array or structure
      else if (!vlog_has_left(item)) {
         def = vals[i];
         defnode = vlog_right(item);
      }
      else if (dim != NULL) {
         mir_value_t index = vlog_lower_rvalue(g, vlog_left(item));
         mir_value_t key = vlog_lower_array_off(g, dim, index);

         int64_t ival;
         if (mir_get_const(g->mu, key, &ival) && ival >= 0
             && ival < vlog_size(dim))
            keys[i] = ival;
      }
      else {
         vlog_node_t f = vlog_ref(vlog_left(item));
         const int ndecls = vlog_decls(dt);
         for (int j = 0; j < ndecls; j++) {
            vlog_node_t d = vlog_decl(dt, j);
            if (d == f) {
               keys[i] = j;
               break;
            }
         }
      }
   }

   switch (vlog_kind(dt)) {
   case V_DATA_TYPE:
      {
         mir_type_t t_offset = mir_offset_type(g->mu);

         // The elements are the first dimension of a packed array
         const int nelems = vlog_size(dim);
         const int ewidth = width / nelems;
         mir_type_t t_logic =
            mir_get_class(g->mu, mir_get_type(g->mu, result)) == MIR_TYPE_VEC2
            ? mir_vec2_type(g->mu, ewidth, false)
            : mir_vec4_type(g->mu, ewidth, false);

         for (int i = 0; i < nelems; i++) {
            mir_value_t elem = def;
            for (int j = 0; j < nparams; j++) {
               if (keys[j] == i) {
                  elem = vals[j];
                  break;
               }
            }

            mir_value_t cast = vlog_lower_cast(g, t_logic, elem);
            mir_value_t pos = mir_const(g->mu, t_offset, i * ewidth);
            result = mir_build_insert(g->mu, cast, result, pos);
         }
      }
      break;
   case V_STRUCT_DECL:
   case V_UNION_DECL:
      {
         mir_type_t t_offset = mir_offset_type(g->mu);

         const int ndecls = vlog_decls(dt);
         for (int i = 0, offset = 0; i < ndecls; i++) {
            mir_value_t elem = def;
            vlog_node_t enode = defnode;
            for (int j = 0; j < nparams; j++) {
               if (keys[j] == i) {
                  elem = vals[j];
                  enode = vlog_right(items[j]);
                  break;
               }
            }

            vlog_node_t f = vlog_decl(dt, i);

            const type_info_t *fti = vlog_type_info(g, vlog_type(f));
            if (enode != NULL && vlog_is_fill_literal(enode))
               elem = vlog_lower_with_context(g, enode, fti->type);
            mir_value_t cast = vlog_lower_cast(g, fti->type, elem);

            mir_value_t uns = vlog_cast_unsigned(g, cast);
            if (mir_get_class(g->mu, mir_get_type(g->mu, result))
                == MIR_TYPE_VEC2)
               uns = mir_build_cast(g->mu,
                                    mir_vec2_type(g->mu, fti->size, false),
                                    uns);
            else
               uns = mir_build_cast(g->mu,
                                    mir_vec4_type(g->mu, fti->size, false),
                                    uns);

            mir_value_t pos =  mir_const(g->mu, t_offset, offset);
            result = mir_build_insert(g->mu, uns, result, pos);

            offset += fti->size;
         }
      }
      break;
   default:
      should_not_reach_here();
   }

   return vlog_widen_vector(g, result, context);
}

static mir_value_t vlog_lower_class_new(vlog_gen_t *g, vlog_node_t v);
static mir_value_t vlog_lower_dynamic_new(vlog_gen_t *g, vlog_node_t v);
static bool vlog_is_array_value(vlog_node_t v)
{
   vlog_node_t type = vlog_get_type(v);
   if (type == NULL && vlog_kind(v) == V_PART_SELECT) {
      // A slice of a dynamic array
      type = vlog_get_type(vlog_value(v));
      return type != NULL && vlog_kind(type) == V_DATA_TYPE
         && vlog_subkind(type) == DT_DYNARRAY;
   }

   return type != NULL && vlog_kind(type) == V_DATA_TYPE
      && (vlog_subkind(type) == DT_QUEUE || vlog_subkind(type) == DT_DYNARRAY);
}

static mir_value_t vlog_queue_concat(vlog_gen_t *g, vlog_node_t v,
                                     mir_type_t context)
{
   // Concatenation of queues and elements

   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_type_t t_elem = mir_get_elem(g->mu, context);
   mir_type_t t_uarray = mir_uarray_type(g->mu, 1, t_elem);

   const int nparams = vlog_params(v);
   mir_value_t *parts LOCAL = xmalloc_array(nparams, sizeof(mir_value_t));
   bool *isarr LOCAL = xmalloc_array(nparams, sizeof(bool));

   mir_value_t total = mir_const(g->mu, t_offset, 0);
   for (int i = 0; i < nparams; i++) {
      vlog_node_t p = vlog_param(v, i);
      isarr[i] = vlog_is_array_value(p);

      vlog_node_t dims[MAX_UNPACKED_DIMS];
      if (!isarr[i] && vlog_unpacked_dims(p, dims) == 1) {
         // A fixed size array contributes each of its elements
         isarr[i] = true;
         mir_value_t flat = vlog_lower_rvalue(g, p);
         parts[i] = vlog_flat_to_dynamic(g, p, vlog_array_elem_info(g, p),
                                         vlog_unpacked_count(dims, 1), flat);
         total = mir_build_add(g->mu, t_offset, total,
                               mir_build_uarray_len(g->mu, parts[i], 0));
         continue;
      }

      mir_value_t value = vlog_lower_with_context(
         g, p, isarr[i] ? t_uarray : t_elem);
      if (isarr[i]) {
         // A slice is a new array
         parts[i] = vlog_get_type(p) != NULL
            ? vlog_copy_value(g, value, vlog_get_type(p)) : value;
         total = mir_build_add(g->mu, t_offset, total,
                               mir_build_uarray_len(g->mu, value, 0));
      }
      else {
         // Kept in a variable as the array is built after all parts
         parts[i] = vlog_lower_cast(g, t_elem, value);
         if (vlog_get_type(p) != NULL)
            parts[i] = vlog_copy_value(g, parts[i], vlog_get_type(p));
         total = mir_build_add(g->mu, t_offset, total,
                               mir_const(g->mu, t_offset, 1));
      }
   }

   mir_value_t data = vlog_alloc_array(g, t_elem, total);
   mir_value_t pos = mir_const(g->mu, t_offset, 0);
   for (int i = 0; i < nparams; i++) {
      if (isarr[i]) {
         mir_value_t len = mir_build_uarray_len(g->mu, parts[i], 0);
         mir_build_copy(g->mu, mir_build_array_ref(g->mu, data, pos),
                        mir_build_unwrap(g->mu, parts[i]), len);
         pos = mir_build_add(g->mu, t_offset, pos, len);
      }
      else {
         mir_build_store(g->mu, mir_build_array_ref(g->mu, data, pos),
                         parts[i]);
         pos = mir_build_add(g->mu, t_offset, pos,
                             mir_const(g->mu, t_offset, 1));
      }
   }

   return vlog_wrap_array(g, data, total);
}

static mir_value_t vlog_lower_concat_select(vlog_gen_t *g, vlog_node_t v)
{
   // Constant bit or part select of a concatenation

   mir_type_t t_offset = mir_offset_type(g->mu);

   vlog_node_t concat = vlog_value(v);
   mir_value_t value = vlog_lower_rvalue(g, concat);
   const int width = mir_get_size(g->mu, mir_get_type(g->mu, value));

   int64_t msb, lsb;
   if (vlog_kind(v) == V_BIT_SELECT) {
      if (!vlog_get_const(vlog_param(v, 0), &msb))
         fatal_at(vlog_loc(v), "sorry, only a constant index is supported "
                  "when selecting from a concatenation");
      lsb = msb;
   }
   else {
      int64_t base, size = vlog_size(v);
      if (!vlog_get_const(vlog_left(v), &base))
         fatal_at(vlog_loc(v), "sorry, only constant bounds are supported "
                  "when selecting from a concatenation");

      switch (vlog_subkind(v)) {
      case V_RANGE_CONST:
         {
            msb = base;
            if (!vlog_get_const(vlog_right(v), &lsb))
               fatal_at(vlog_loc(v), "sorry, only constant bounds are "
                        "supported when selecting from a concatenation");
         }
         break;
      case V_RANGE_POS:
         msb = base + size - 1;
         lsb = base;
         break;
      default:
         msb = base;
         lsb = base - size + 1;
         break;
      }
   }

   const int nbits = msb - lsb + 1;
   mir_type_t t_part = mir_vec4_type(g->mu, MAX(nbits, 1), false);
   if (nbits < 1 || msb >= width || lsb < 0) {
      // Out of range bits read as X
      return vlog_lower_x(g, mir_vec4_type(g->mu, MAX(nbits, 1), false));
   }

   return mir_build_extract(g->mu, t_part, value,
                            mir_const(g->mu, t_offset, width - 1 - msb));
}

static void vlog_cond_branch_cb(vlog_node_t v, void *context)
{
   bool *branch = context;

   switch (vlog_kind(v)) {
   case V_MEMBER_REF:
      {
         // A member of a structure is always valid
         vlog_node_t type = vlog_get_type(vlog_value(v));
         if (type != NULL && vlog_kind(type) == V_CLASS_DECL)
            *branch = true;
      }
      break;
   case V_BIT_SELECT:
      {
         // Reading an entry which does not exist is reported
         vlog_node_t type = vlog_get_type(vlog_value(v));
         if (type != NULL && vlog_kind(type) == V_DATA_TYPE
             && vlog_subkind(type) == DT_ASSOC)
            *branch = true;
      }
      break;
   case V_USER_FCALL:
   case V_METHOD_CALL:
   case V_CLASS_NEW:
   case V_DYNAMIC_NEW:
   case V_OP_ASSIGN:
   case V_PREFIX:
   case V_POSTFIX:
      *branch = true;
      break;
   default:
      break;
   }
}

static inline bool mir_is_vector_type(mir_unit_t *mu, mir_type_t type)
{
   const mir_class_t class = mir_get_class(mu, type);
   return class == MIR_TYPE_VEC2 || class == MIR_TYPE_VEC4;
}

static mir_value_t vlog_lower_with_context(vlog_gen_t *g, vlog_node_t v,
                                           mir_type_t context)
{
   PUSH_DEBUG_INFO(g->mu, v);

   if ((vlog_kind(v) == V_PART_SELECT || vlog_kind(v) == V_BIT_SELECT)
       && vlog_kind(vlog_value(v)) == V_CONCAT)
      return vlog_widen_vector(g, vlog_lower_concat_select(g, v), context);

   if (vlog_kind(v) == V_PART_SELECT) {
      vlog_node_t type = vlog_get_type(vlog_value(v));
      if (type != NULL && vlog_kind(type) == V_DATA_TYPE
          && (vlog_subkind(type) == DT_QUEUE
              || vlog_subkind(type) == DT_DYNARRAY))
         return vlog_lower_queue_slice(g, v);
   }

   switch (vlog_kind(v)) {
   case V_MEMBER_REF:
      if (vlog_has_ref(v) && vlog_kind(vlog_ref(v)) == V_FUNC_DECL) {
         // Method called without parentheses
         mir_value_t this_val = vlog_lower_rvalue(g, vlog_value(v));
         return vlog_widen_vector(g, vlog_lower_subr_call(g, vlog_ref(v), NULL,
                                                          this_val, false),
                                  context);
      }
      // Fall-through
   case V_REF:
      if (vlog_kind(v) == V_REF && vlog_has_ref(v)
          && vlog_kind(vlog_ref(v)) == V_LOCALPARAM) {
         // Parameter of a class holding a string
         vlog_node_t type = vlog_type(vlog_ref(v));
         if (vlog_kind(type) == V_DATA_TYPE
             && vlog_subkind(type) == DT_STRING) {
            vlog_node_t value = vlog_value(vlog_ref(v));
            if (vlog_kind(value) == V_NUMBER)
               return vlog_string_const(g, vlog_number(value));

            mir_type_t t_str = mir_uarray_type(g->mu, 1, mir_char_type(g->mu));
            return vlog_lower_with_context(g, value, t_str);
         }
      }
      // Fall-through
   case V_BIT_SELECT:
      if (vlog_kind(v) == V_BIT_SELECT) {
         vlog_node_t ptype = vlog_get_type(vlog_value(v));
         if (ptype != NULL && vlog_kind(ptype) == V_DATA_TYPE
             && vlog_subkind(ptype) == DT_STRING) {
            // One character of a string
            mir_value_t args[] = {
               vlog_str_arg(g, vlog_value(v)),
               vlog_str_offset_arg(g, vlog_param(v, 0))
            };
            mir_value_t ch = vlog_str_call(g, "__sv_str_getc",
                                           mir_offset_type(g->mu), args, 2);
            mir_type_t t_byte = mir_vec2_type(g->mu, 8, true);
            return vlog_widen_vector(
               g, mir_build_cast(g->mu, t_byte, vlog_offset_to_int(g, ch)),
               context);
         }
      }
      // Fall-through
   case V_PART_SELECT:
      return vlog_widen_vector(g, vlog_lower_rvalue_select(g, v), context);
   case V_THIS:
      return vlog_lower_this(g);
   case V_METHOD_CALL:
      return vlog_widen_vector(g, vlog_lower_method_call(g, v), context);
   case V_STD_RANDOMIZE:
      return vlog_widen_vector(g, vlog_lower_std_randomize(g, v), context);
   case V_INSIDE:
      return vlog_widen_vector(g, vlog_lower_inside(g, v), context);
   case V_MATCHES:
      return vlog_widen_vector(g, vlog_lower_matches(g, v), context);
   case V_DOLLAR:
      {
         if (mir_is_null(g->last_index))
            fatal_at(vlog_loc(v), "$ used outside of a queue index");
         return vlog_offset_to_int(g, g->last_index);
      }
   case V_HIER_REF:
      {
         if (!vlog_has_ref(v)) {
            if (vlog_ident(v) == ident_new("$self")
                && !mir_is_null(context)
                && mir_get_class(g->mu, context) == MIR_TYPE_CONTEXT) {
               mir_value_t ptr = vlog_bind_hier(g, v, context, MIR_NULL_STAMP);
               return mir_build_load(g->mu, ptr);
            }

            fatal_at(vlog_loc(v), "unbound hierarchical reference");
         }

         if (g->in_sva) {
            int hops = 0;
            mir_value_t var = mir_search_object(g->mu, v, &hops);
            if (!mir_is_null(var) && hops == 0)
               return vlog_widen_vector(g, mir_build_load(g->mu, var), context);
         }

         if (vlog_is_dynamic_type(vlog_type(vlog_ref(v))))
            return vlog_lower_rvalue_select(g, v);

         if (vlog_is_vhdl_integer(vlog_ref(v)))
            return vlog_widen_vector(g, vlog_lower_vhdl_integer(g, v), context);

         if (vlog_kind(vlog_ref(v)) == V_VAR_DECL
             && (vlog_flags(vlog_ref(v)) & VLOG_F_VHDL)) {
            mir_value_t value = vlog_lower_rvalue_select(g, v);
            return vlog_widen_vector(g, value, context);
         }

         vlog_node_t decl = vlog_ref(v);
         const type_info_t *ti = vlog_type_info(g, vlog_type(decl));

         mir_value_t ptr = vlog_bind_hier(g, v, ti->signal, ti->stamp);

         mir_value_t nets = mir_build_load(g->mu, ptr);
         mir_value_t data = mir_build_resolved(g->mu, nets);

         if (mir_get_class(g->mu, ti->type) == MIR_TYPE_REAL)
            return mir_build_load(g->mu, data);
         else if (ti->size == 1)
            data = mir_build_load(g->mu, data);

         mir_value_t packed = mir_build_pack(g->mu, ti->type, data);
         return vlog_widen_vector(g, packed, context);
      }
   case V_NUMBER:
   case V_STRING:
      {
         number_t num = vlog_number(v);

         if (vlog_kind(v) == V_NUMBER && (vlog_flags(v) & VLOG_F_TIMEEXPR))
            num = number_from_int(vlog_time_literal(number_integer(num)));

         if (!mir_is_null(context)
             && mir_get_class(g->mu, context) == MIR_TYPE_UARRAY) {
            mir_class_t eclass =
               mir_get_class(g->mu, mir_get_elem(g->mu, context));
            if (vlog_kind(v) == V_NUMBER
                && (eclass == MIR_TYPE_VEC2 || eclass == MIR_TYPE_VEC4)) {
               // A concatenation of one number was simplified to the number
               mir_type_t t_elem = mir_get_elem(g->mu, context);
               mir_value_t elem = vlog_lower_with_context(g, v, t_elem);
               mir_value_t data = vlog_alloc_array(
                  g, t_elem, mir_const(g->mu, mir_offset_type(g->mu), 1));
               mir_build_store(g->mu, data, vlog_lower_cast(g, t_elem, elem));
               return vlog_wrap_array(g, data,
                                      mir_const(g->mu, mir_offset_type(g->mu), 1));
            }

            return vlog_string_const(g, num);
         }

         int width = number_width(num);
         const bool issigned = number_signed(num);

         if (!mir_is_null(context)) {
            mir_class_t class = mir_get_class(g->mu, context);
            if (class == MIR_TYPE_VEC2 || class == MIR_TYPE_VEC4)
               width = MAX(width, mir_get_size(g->mu, context));
         }

         mir_type_t t_full = mir_vec4_type(g->mu, width, issigned);

         const bool fill = vlog_kind(v) == V_NUMBER
            && (vlog_flags(v) & VLOG_F_FILL);

         uint64_t alow, blow;
         number_word(num, 0, &alow, &blow);

         if (fill) {
            // The single bit is replicated across the whole context
            alow = (alow & 1) ? ~UINT64_C(0) : 0;
            blow = (blow & 1) ? ~UINT64_C(0) : 0;
            if (width < 64) {
               alow &= (UINT64_C(1) << width) - 1;
               blow &= (UINT64_C(1) << width) - 1;
            }
         }

         if (width <= 64) {
            mir_value_t vec = mir_const_vec(g->mu, t_full, alow, blow);
            return vlog_widen_vector(g, vec, context);
         }

         mir_type_t t_low = mir_vec4_type(g->mu, 64, false);
         mir_value_t low = mir_const_vec(g->mu, t_low, alow, blow);

         mir_value_t full = mir_build_cast(g->mu, t_full, low);

         mir_type_t t_offset = mir_offset_type(g->mu);

         for (int i = 64; i < width; i += 64) {
            uint64_t aword, bword;
            number_word(num, i / 64, &aword, &bword);
            if (fill) {
               aword = alow;
               bword = blow;
            }

            if (aword == 0 && bword == 0)
               continue;

            const int part_size = MIN(width - i, 64);
            if (fill && part_size < 64) {
               aword &= (UINT64_C(1) << part_size) - 1;
               bword &= (UINT64_C(1) << part_size) - 1;
            }

            mir_type_t t_part = mir_vec4_type(g->mu, part_size, issigned);
            mir_value_t part = mir_const_vec(g->mu, t_part, aword, bword);

            mir_value_t pos = mir_const(g->mu, t_offset, width - i - part_size);
            full = mir_build_insert(g->mu, part, full, pos);
         }

         return vlog_widen_vector(g, full, context);
      }
   case V_BINARY:
      return vlog_lower_binary(g, v, context);
   case V_UNARY:
      return vlog_lower_unary(g, v, context);
   case V_SYS_FCALL:
      return vlog_lower_sys_fcall(g, v);
   case V_MIN_TYP_MAX:
      return vlog_lower_with_context(g, vlog_value(v), context);
   case V_CONCAT:
      if (vlog_params(v) == 0) {
         if (mir_is_null(context)
             || mir_get_class(g->mu, context) != MIR_TYPE_UARRAY)
            fatal_at(vlog_loc(v), "empty queue is not valid here");
         return vlog_empty_array(g, context);
      }
      else if (!mir_is_null(context)
               && mir_get_class(g->mu, context) == MIR_TYPE_UARRAY
               && mir_get_class(g->mu, mir_get_elem(g->mu, context))
                  != MIR_TYPE_INT)
         return vlog_queue_concat(g, v, context);
      else if (vlog_is_string(v)
               || (!mir_is_null(context)
                   && mir_get_class(g->mu, context) == MIR_TYPE_UARRAY))
         return vlog_string_concat(g, v);
      // Fall-through
      {
         int size = 0, repeat = 1;
         const int nparams = vlog_params(v);
         mir_value_t *inputs LOCAL =
            xmalloc_array(nparams, sizeof(mir_value_t));

         for (int i = 0; i < nparams; i++) {
            inputs[i] = vlog_lower_rvalue(g, vlog_param(v, i));
            assert(mir_is_vector(g->mu, inputs[i]));

            mir_type_t part_type = mir_get_type(g->mu, inputs[i]);
            int part_size = mir_get_size(g->mu, part_type);

            if (mir_get_class(g->mu, part_type) != MIR_TYPE_VEC4) {
               mir_type_t t_vec4 = mir_vec4_type(g->mu, part_size, false);
               inputs[i] = mir_build_cast(g->mu, t_vec4, inputs[i]);
            }

            size += part_size;
         }

         if (vlog_has_value(v)) {
            int64_t cval = vlog_must_be_const(vlog_value(v));
            size *= (repeat = MAX(0, cval));
         }

         mir_type_t type = mir_vec4_type(g->mu, size, false);

         mir_value_t result;
         if (size <= 64)
            result = mir_const_vec(g->mu, type, 0, 0);
         else {
            mir_type_t t_bit = mir_vec4_type(g->mu, 1, false);
            mir_value_t zero = mir_const_vec(g->mu, t_bit, 0, 0);
            result = mir_build_cast(g->mu, type, zero);
         }

         mir_type_t t_offset = mir_offset_type(g->mu);

         for (int i = 0, pos = size; i < repeat; i++) {
            for (int j = nparams - 1; j >= 0; j--) {
               pos -= mir_get_size(g->mu, mir_get_type(g->mu, inputs[j]));
               result = mir_build_insert(g->mu, inputs[j], result,
                                         mir_const(g->mu, t_offset, pos));
            }
         }

         return vlog_widen_vector(g, result, context);
      }
   case V_PREFIX:
   case V_POSTFIX:
      {
         vlog_node_t target = vlog_target(v);

         mir_value_t prev = vlog_lower_update_target(g, target);
         mir_type_t type = mir_get_type(g->mu, prev);
         if (mir_get_class(g->mu, type) == MIR_TYPE_REAL) {
            mir_value_t rone = mir_const_real(g->mu, type, 1.0);
            mir_value_t rnext = vlog_subkind(v) == V_INCDEC_MINUS
               ? mir_build_sub(g->mu, type, prev, rone)
               : mir_build_add(g->mu, type, prev, rone);
            vlog_assign_variable(g, target, rnext, NULL);
            return vlog_kind(v) == V_PREFIX ? rnext : prev;
         }

         mir_value_t one;
         if (mir_get_size(g->mu, type) > 64)
            one = vlog_lower_cast(g, type, mir_const_vec(
                                     g->mu, mir_vec4_type(g->mu, 32, false),
                                     1, 0));
         else
            one = mir_const_vec(g->mu, type, 1, 0);
         const mir_vec_op_t op =
            vlog_subkind(v) == V_INCDEC_MINUS ? MIR_VEC_SUB : MIR_VEC_ADD;
         mir_value_t next = mir_build_binary(g->mu, op, type, prev, one);

         vlog_assign_variable(g, target, next, NULL);

         return vlog_kind(v) == V_PREFIX ? next : prev;
      }
   case V_COND_EXPR:
      {
         // See 1800-2023 section 11.4.11 for semantics

         vlog_node_t left_expr = vlog_left(v);
         vlog_node_t right_expr = vlog_right(v);

         mir_value_t value = vlog_lower_rvalue(g, vlog_value(v));

         const bool is_signed =
            vlog_is_signed(left_expr) && vlog_is_signed(right_expr);

         // A class handle or string has the type of its operands
         vlog_node_t optype = vlog_get_type(v);

         unsigned size = 0;
         if (optype == NULL)
            size = MAX(vlog_width(left_expr), vlog_width(right_expr));

         mir_type_t type;
         if (!mir_is_null(context)) {
            mir_class_t class = mir_get_class(g->mu, context);
            if (class == MIR_TYPE_VEC2 || class == MIR_TYPE_VEC4) {
               size = MAX(size, mir_get_size(g->mu, context));
               type = mir_vec4_type(g->mu, size, is_signed);
            }
            else
               type = context;
         }
         else if (optype != NULL)
            type = vlog_type_info(g, optype)->type;
         else if (vlog_is_string(v))
            type = mir_uarray_type(g->mu, 1, mir_char_type(g->mu));
         else
            type = mir_vec4_type(g->mu, size, is_signed);

         // Only one of the operands is evaluated when it may have an effect
         // or may not be valid, like a member of a null handle
         bool branch = !mir_is_vector_type(g->mu, type);
         vlog_visit(left_expr, vlog_cond_branch_cb, &branch);
         vlog_visit(right_expr, vlog_cond_branch_cb, &branch);

         if (branch) {
            mir_value_t cmp = vlog_lower_test(g, value);

            mir_value_t result = mir_add_var(g->mu, type, MIR_NULL_STAMP,
                                             ident_uniq("cond"), MIR_VAR_TEMP);

            mir_block_t true_bb = mir_add_block(g->mu);
            mir_block_t false_bb = mir_add_block(g->mu);
            mir_block_t merge_bb = mir_add_block(g->mu);

            mir_build_cond(g->mu, cmp, true_bb, false_bb);

            mir_set_cursor(g->mu, true_bb, MIR_APPEND);
            mir_build_store(g->mu, result,
                            vlog_lower_cast(
                               g, type,
                               vlog_lower_with_context(g, left_expr, type)));
            mir_build_jump(g->mu, merge_bb);

            mir_set_cursor(g->mu, false_bb, MIR_APPEND);
            mir_build_store(g->mu, result,
                            vlog_lower_cast(
                               g, type,
                               vlog_lower_with_context(g, right_expr, type)));
            mir_build_jump(g->mu, merge_bb);

            mir_set_cursor(g->mu, merge_bb, MIR_APPEND);
            return mir_build_load(g->mu, result);
         }

         mir_value_t left = vlog_lower_with_context(g, left_expr, type);
         mir_value_t right = vlog_lower_with_context(g, right_expr, type);

         if (!is_signed) {
            left = vlog_cast_unsigned(g, left);
            right = vlog_cast_unsigned(g, right);
         }

         mir_value_t lcast = vlog_lower_cast(g, type, left);
         mir_value_t rcast = vlog_lower_cast(g, type, right);

         const bool merge_x = mir_is(g->mu, value, MIR_TYPE_VEC4)
            && mir_is_vector(g->mu, lcast)
            && mir_is_vector(g->mu, rcast);

         if (merge_x) {
            mir_type_t t_test = mir_get_type(g->mu, value);
            mir_value_t zero = vlog_lower_zero(g, t_test);
            mir_value_t cmp = mir_build_binary(g->mu, MIR_VEC_LOG_NEQ, t_test,
                                               value, zero);
            return mir_build_ternary(g->mu, type, cmp, lcast, rcast);
         }
         else {
            mir_value_t cmp = vlog_lower_test(g, value);
            return mir_build_select(g->mu, type, cmp, lcast, rcast);
         }
      }
   case V_USER_FCALL:
      {
         vlog_node_t decl = vlog_ref(v);
         mir_value_t this_val = MIR_NULL_VALUE;
         if (vlog_has_ref(decl) && !(vlog_flags(decl) & VLOG_F_STATIC))
            this_val = vlog_lower_this(g);

         return vlog_widen_vector(g, vlog_lower_subr_call(g, decl, v, this_val,
                                                          false), context);
      }
   case V_REAL:
      {
         mir_type_t t_double = mir_double_type(g->mu);
         return mir_const_real(g->mu, t_double, vlog_dval(v));
      }
   case V_OP_ASSIGN:
      return vlog_lower_operator_assignment(g, v);
   case V_STREAM:
      return vlog_lower_stream(g, v, context);
   case V_CLASS_NEW:
      return vlog_lower_class_new(g, v);
   case V_DYNAMIC_NEW:
      return vlog_lower_dynamic_new(g, v);
   case V_NULL:
      {
         if (!mir_is_null(context)
             && (mir_get_class(g->mu, context) == MIR_TYPE_CONTEXT
                 || mir_get_class(g->mu, context) == MIR_TYPE_ACCESS))
            return mir_build_null(g->mu, context);

         const type_info_t *ti = vlog_type_info(g, vlog_type(v));
         return mir_build_null(g->mu, ti->type);
      }
   case V_PATTERN_EXPR:
      return vlog_lower_pattern_expr(g, v, context);
   case V_CAST:
      {
         if (vlog_subkind(v) == V_CAST_TYPEREF) {
            mir_type_t t_ref = mir_vec2_type(g->mu, 32, false);
            return mir_const_vec(g->mu, t_ref, vlog_type_hash(vlog_type(v)), 0);
         }

         if (vlog_subkind(v) == V_CAST_TYPE) {
            const type_info_t *ti = vlog_type_info(g, vlog_type(v));

            // An interface instance has the type of the handle it is
            // converted to
            vlog_node_t arg = vlog_value(v);
            if (vlog_kind(arg) == V_HIER_REF && !vlog_has_ref(arg))
               return vlog_lower_with_context(g, arg, ti->type);

            if (vlog_is_fill_literal(arg))
               return vlog_lower_cast(g, ti->type, vlog_lower_with_context(
                                         g, arg, ti->type));

            // The operand is evaluated as if assigned to the type
            const mir_class_t class = mir_get_class(g->mu, ti->type);
            if ((class == MIR_TYPE_VEC2 || class == MIR_TYPE_VEC4)
                && !vlog_is_real(arg) && !vlog_is_string(arg)
                && vlog_is_context_operand(arg)
                && mir_get_size(g->mu, ti->type) > vlog_width(arg))
               return vlog_lower_cast(g, ti->type, vlog_lower_with_context(
                                         g, arg, mir_vec4_type(
                                            g->mu, mir_get_size(g->mu, ti->type),
                                            vlog_is_signed(arg))));

            return vlog_lower_cast(g, ti->type, vlog_lower_rvalue(g, arg));
         }

         assert(vlog_subkind(v) == V_CAST_WIDTH);

         int64_t width = vlog_must_be_const(vlog_left(v));

         mir_value_t value;
         vlog_node_t arg = vlog_value(v);
         if (vlog_is_fill_literal(arg))
            value = vlog_lower_with_context(
               g, arg, mir_vec4_type(g->mu, width, false));
         else if (!vlog_is_real(arg) && !vlog_is_string(arg)
                  && vlog_is_context_operand(arg) && width > vlog_width(arg))
            value = vlog_lower_with_context(
               g, arg, mir_vec4_type(g->mu, width, vlog_is_signed(arg)));
         else
            value = vlog_lower_rvalue(g, arg);

         if (mir_is(g->mu, value, MIR_TYPE_REAL)) {
            // Rounded to a signed integer of this width (6.24.1)
            mir_type_t type = mir_vec4_type(g->mu, width, true);
            return vlog_lower_cast(g, type, value);
         }

         bool issigned = mir_get_signed(g->mu, mir_get_type(g->mu, value));
         mir_type_t type = mir_vec4_type(g->mu, width, issigned);

         return mir_build_cast(g->mu, type, value);
      }
   default:
      CANNOT_HANDLE(v);
   }
}

static mir_value_t vlog_lower_rvalue(vlog_gen_t *g, vlog_node_t v)
{
   return vlog_lower_with_context(g, v, MIR_NULL_TYPE);
}

static mir_value_t vlog_lower_scaled_time(vlog_gen_t *g, vlog_node_t v,
                                          int64_t ival)
{
   // Convert a delay to simulator time units

   mir_type_t t_time = mir_time_type(g->mu);

   ival = vlog_time_resolve(ival);
   const int64_t scale = VLOG_TIME_UNIT(ival);

   if (vlog_kind(v) == V_NUMBER) {
      int64_t fs = number_integer(vlog_number(v));
      const unsigned log10 = VLOG_TIME_LOG10(ival);
      if (vlog_flags(v) & VLOG_F_ONESTEP)
         fs = vlog_time_quantum(log10);
      else if (vlog_flags(v) & VLOG_F_TIMELIT)
         fs = vlog_round_time(fs, log10);
      else
         fs *= scale;

      return mir_const(g->mu, t_time, fs);
   }

   mir_value_t value = vlog_lower_rvalue(g, v);

   if (mir_is(g->mu, value, MIR_TYPE_REAL)) {
      mir_type_t t_double = mir_double_type(g->mu);
      mir_value_t cast = mir_build_cast(g->mu, t_double, value);
      mir_value_t real_scale = mir_const_real(g->mu, t_double, scale);
      mir_value_t scaled = mir_build_mul(g->mu, t_double, cast, real_scale);

      // Round to the time precision of the module
      int64_t quantum = 1;
      for (unsigned i = VLOG_TIME_LOG10(ival); i > 0; i--)
         quantum *= 10;

      mir_value_t steps = scaled;
      if (quantum > 1) {
         mir_value_t real_quantum = mir_const_real(g->mu, t_double, quantum);
         steps = mir_build_div(g->mu, t_double, scaled, real_quantum);
      }

      // A decimal value half way between two steps is rounded away from
      // zero even when its binary approximation is slightly below the
      // tie: move the value up by a few ulps while that cannot change an
      // integral number of steps
      mir_value_t nudge = mir_build_mul(
         g->mu, t_double, steps,
         mir_const_real(g->mu, t_double, 1.0 + 4 * DBL_EPSILON));
      mir_value_t small = mir_build_cmp(
         g->mu, MIR_CMP_LT, mir_build_mul(g->mu, t_double, steps, steps),
         mir_const_real(g->mu, t_double, 0x1p98));
      steps = mir_build_select(g->mu, t_double, small, nudge, steps);

      mir_value_t isteps = mir_build_cast(g->mu, t_time, steps);
      if (quantum == 1)
         return isteps;

      return mir_build_mul(g->mu, t_time, isteps,
                           mir_const(g->mu, t_time, quantum));
   }

   mir_value_t cast = vlog_lower_cast(g, t_time, value);
   if (scale == 1)
      return cast;

   return mir_build_mul(g->mu, t_time, cast, mir_const(g->mu, t_time, scale));
}

static mir_value_t vlog_lower_delay(vlog_gen_t *g, vlog_node_t ctrl)
{
   return vlog_lower_scaled_time(g, vlog_value(ctrl), vlog_ival(ctrl));
}

static mir_value_t vlog_or_triggers(vlog_gen_t *g, int n, ...)
{
   va_list ap;
   va_start(ap, n);

   mir_value_t or = MIR_NULL_VALUE;
   for (int i = 0; i < n; i++) {
      mir_value_t t = va_arg(ap, mir_value_t);
      if (mir_is_null(t))
         continue;
      else if (mir_is_null(or))
         or = t;
      else
         or = mir_build_or_trigger(g->mu, or, t);
   }

   va_end(ap);
   return or;
}

static void vlog_lower_edge_fn(mir_unit_t *mu, vlog_edge_t edge, bool vhdl,
                               bool step)
{
   mir_type_t t_context = mir_context_type(mu, ident_new("dummy"));
   mir_type_t t_bool = mir_bool_type(mu);
   mir_type_t t_offset = mir_offset_type(mu);
   mir_type_t t_uint8 = mir_int_type(mu, 0, UINT8_MAX);
   mir_type_t t_logic = mir_vec4_type(mu, 1, false);
   mir_type_t t_signal =
      mir_signal_type(mu, vhdl ? mir_int_type(mu, 0, 8) : t_uint8);

   mir_set_result(mu, t_bool);

   mir_add_param(mu, t_context, MIR_NULL_STAMP, ident_new("context"));
   mir_value_t nets = mir_add_param(mu, t_signal, MIR_NULL_STAMP,
                                    ident_new("arg"));

   mir_value_t cur_ptr = mir_build_resolved(mu, nets);
   mir_value_t cur_val = mir_build_load(mu, cur_ptr);

   mir_value_t last_ptr = mir_build_last_value(mu, nets);
   mir_value_t last_val = mir_build_load(mu, last_ptr);

   if (vhdl) {
      // U X 0 1 Z W L H -
      static const uint8_t std_entries[] = { 3, 3, 0, 1, 2, 3, 0, 1, 3 };

      mir_value_t tmp[ARRAY_LEN(std_entries)];
      for (int i = 0; i < ARRAY_LEN(std_entries); i++)
         tmp[i] = mir_const(mu, t_uint8, std_entries[i]);

      mir_type_t t_map = mir_carray_type(mu, ARRAY_LEN(std_entries), t_uint8);
      mir_value_t map = mir_const_array(mu, t_map, tmp, ARRAY_LEN(tmp));
      mir_value_t base = mir_build_address_of(mu, map);

      cur_val = mir_build_load(mu, mir_build_array_ref(
                                  mu, base, mir_build_cast(mu, t_offset,
                                                           cur_val)));
      last_val = mir_build_load(mu, mir_build_array_ref(
                                   mu, base, mir_build_cast(mu, t_offset,
                                                            last_val)));
   }

   mir_value_t cur_packed = mir_build_pack(mu, t_logic, cur_val);
   mir_value_t cur_cmp = mir_build_unpack(mu, cur_packed, 0, MIR_NULL_VALUE);

   mir_value_t last_packed = mir_build_pack(mu, t_logic, last_val);
   mir_value_t last_cmp = mir_build_unpack(mu, last_packed, 0, MIR_NULL_VALUE);

   // Truth table from 1800-2023 section 9.4.2
   static const uint8_t table_entries[] = {
      NOEDGE,  POSEDGE, POSEDGE, POSEDGE,
      NEGEDGE, NOEDGE,  NEGEDGE, NEGEDGE,
      NEGEDGE, POSEDGE, NOEDGE,  NOEDGE,
      NEGEDGE, POSEDGE, NOEDGE,  NOEDGE,
   };

   mir_value_t tmp[ARRAY_LEN(table_entries)];
   for (int i = 0; i < ARRAY_LEN(table_entries); i++)
      tmp[i] = mir_const(mu, t_uint8, table_entries[i]);

   mir_type_t t_table = mir_carray_type(mu, ARRAY_LEN(table_entries), t_uint8);
   mir_value_t table = mir_const_array(mu, t_table, tmp, ARRAY_LEN(tmp));
   mir_value_t address = mir_build_address_of(mu, table);
   mir_value_t stride = mir_const(mu, t_offset, 4);

   mir_value_t args[2] = { last_cmp, cur_cmp };
   mir_value_t ptr = mir_build_table_ref(mu, address, stride, args, 2);
   mir_value_t entry = mir_build_load(mu, ptr);

   mir_value_t edge_val = mir_const(mu, t_uint8, edge);
   mir_value_t cmp = mir_build_cmp(mu, MIR_CMP_EQ, entry, edge_val);
   mir_value_t count = mir_const(mu, t_offset, 1);
   mir_value_t event;
   if (step) {
      // The last transition happened in the current time step
      mir_type_t t_time = mir_time_type(mu);
      mir_value_t last = mir_build_last_event(mu, nets, count);
      event = mir_build_cmp(mu, MIR_CMP_EQ, last, mir_const(mu, t_time, 0));
   }
   else
      event = mir_build_event_flag(mu, nets, count);
   mir_value_t and = mir_build_and(mu, cmp, event);

   mir_build_return(mu, and);

   mir_optimise(mu, MIR_PASS_O0);
}

static void vlog_lower_posedge_fn(mir_unit_t *mu, object_t *obj)
{
   vlog_lower_edge_fn(mu, POSEDGE, false, false);
}

static void vlog_lower_negedge_fn(mir_unit_t *mu, object_t *obj)
{
   vlog_lower_edge_fn(mu, NEGEDGE, false, false);
}

static void vlog_lower_vhdl_posedge_fn(mir_unit_t *mu, object_t *obj)
{
   vlog_lower_edge_fn(mu, POSEDGE, true, false);
}

static void vlog_lower_vhdl_negedge_fn(mir_unit_t *mu, object_t *obj)
{
   vlog_lower_edge_fn(mu, NEGEDGE, true, false);
}

static void vlog_lower_step_posedge_fn(mir_unit_t *mu, object_t *obj)
{
   vlog_lower_edge_fn(mu, POSEDGE, false, true);
}

static void vlog_lower_step_negedge_fn(mir_unit_t *mu, object_t *obj)
{
   vlog_lower_edge_fn(mu, NEGEDGE, false, true);
}

static void vlog_lower_vhdl_step_posedge_fn(mir_unit_t *mu, object_t *obj)
{
   vlog_lower_edge_fn(mu, POSEDGE, true, true);
}

static void vlog_lower_vhdl_step_negedge_fn(mir_unit_t *mu, object_t *obj)
{
   vlog_lower_edge_fn(mu, NEGEDGE, true, true);
}

static unsigned vlog_whole_size(vlog_gen_t *g, vlog_node_t ref,
                                const vlog_select_t *select)
{
   // Arrays held in signals are already flattened by vlog_lower_select

   vlog_node_t decl = vlog_ref(ref);
   const type_info_t *ti = vlog_type_info(g, vlog_type(decl));
   if (select->size == ti->size)
      return select->size * vlog_size(decl);
   else
      return select->size;
}

static mir_value_t vlog_lower_trigger(vlog_gen_t *g, vlog_node_t v)
{
   const vlog_kind_t kind = vlog_kind(v);

   vlog_node_t type = vlog_get_type(v);
   if (type != NULL && vlog_kind(type) == V_DATA_TYPE
       && vlog_subkind(type) == DT_EVENT)
      fatal_at(vlog_loc(v), "sorry, an event variable cannot be combined "
               "with other events");

   switch (kind) {
   case V_REF:
   case V_HIER_REF:
      {
         switch (vlog_kind(vlog_ref(v))) {
         case V_PORT_DECL:
         case V_NET_DECL:
         case V_VAR_DECL:
         case V_LOCAL_DECL:
            {
               mir_type_t t_offset = mir_offset_type(g->mu);

               if (vlog_kind(v) == V_HIER_REF
                   && vlog_is_vhdl_integer(vlog_ref(v))) {
                  mir_value_t in_range;
                  mir_value_t nets =
                     vlog_lower_vhdl_integer_nets(g, v, &in_range);
                  return mir_build_level_trigger(
                     g->mu, nets, mir_const(g->mu, t_offset, 1));
               }

               vlog_select_t lvalue = vlog_lower_select(g, v);
               int total_size = lvalue.size;
               if (vlog_kind(v) == V_REF)
                  total_size = vlog_whole_size(g, v, &lvalue);

               mir_value_t count = mir_const(g->mu, t_offset, total_size);

               return mir_build_level_trigger(g->mu, lvalue.obj, count);
            }
         case V_PARAM_DECL:
         case V_LOCALPARAM:
         case V_ENUM_NAME:
            return MIR_NULL_VALUE;
         default:
            CANNOT_HANDLE(v);
         }
      }
      break;
   case V_BIT_SELECT:
   case V_PART_SELECT:
   case V_MEMBER_REF:
      {
         mir_value_t trigger = MIR_NULL_VALUE;
         vlog_node_t prefix = vlog_longest_static_prefix(v);
         if (prefix == v) {
            vlog_select_t lvalue = vlog_lower_select(g, v);

            mir_value_t nets =
               mir_build_array_ref(g->mu, lvalue.obj, lvalue.dst_offset);

            trigger = mir_build_level_trigger(g->mu, nets, lvalue.count);
         }
         else if (prefix != NULL)
            trigger = vlog_lower_trigger(g, prefix);
         else if (kind != V_MEMBER_REF || vlog_is_param_select(v))
            trigger = vlog_lower_trigger(g, vlog_value(v));   // An expression

         if (kind == V_MEMBER_REF)
            return trigger;
         else if (kind == V_BIT_SELECT) {
            const int nparams = vlog_params(v);
            for (int i = 0; i < nparams; i++) {
               mir_value_t p = vlog_lower_trigger(g, vlog_param(v, i));
               trigger = vlog_or_triggers(g, 2, trigger, p);
            }
         }
         else {
            mir_value_t left = vlog_lower_trigger(g, vlog_left(v));
            trigger = vlog_or_triggers(g, 2, trigger, left);
         }

         return trigger;
      }
   case V_EVENT:
      {
         const v_event_kind_t kind = vlog_subkind(v);
         if (kind == V_EVENT_LEVEL || kind == V_EVENT_EDGE)
            return vlog_lower_trigger(g, vlog_value(v));
         else {
            vlog_select_t lvalue = vlog_lower_select(g, vlog_value(v));

            const bool vhdl = vlog_is_vhdl_signal(g, lvalue.obj);

            ident_t func;
            switch (kind) {
            case V_EVENT_POSEDGE:
               func = ident_new(vhdl ? "@posedge$vhdl" : "@posedge");
               mir_defer(mir_get_context(g->mu), func, NULL, MIR_UNIT_FUNCTION,
                         vhdl ? vlog_lower_vhdl_posedge_fn
                         : vlog_lower_posedge_fn, NULL);
               break;
            case V_EVENT_NEGEDGE:
               func = ident_new(vhdl ? "@negedge$vhdl" : "@negedge");
               mir_defer(mir_get_context(g->mu), func, NULL, MIR_UNIT_FUNCTION,
                         vhdl ? vlog_lower_vhdl_negedge_fn
                         : vlog_lower_negedge_fn, NULL);
               break;
            default:
               should_not_reach_here();
            }

            mir_type_t t_context = mir_context_type(g->mu, func);
            mir_value_t context = mir_build_null(g->mu, t_context);
            mir_value_t nets =
               mir_build_array_ref(g->mu, lvalue.obj, lvalue.dst_offset);
            mir_value_t args[] = { context, nets };
            return mir_build_function_trigger(g->mu, func, args, 2);
         }
      }
   case V_NUMBER:
      return MIR_NULL_VALUE;
   case V_BINARY:
      {
         mir_value_t left = vlog_lower_trigger(g, vlog_left(v));
         mir_value_t right = vlog_lower_trigger(g, vlog_right(v));

         return vlog_or_triggers(g, 2, left, right);
      }
   case V_UNARY:
      return vlog_lower_trigger(g, vlog_value(v));
   case V_COND_EXPR:
      {
         mir_value_t value = vlog_lower_trigger(g, vlog_value(v));
         mir_value_t left = vlog_lower_trigger(g, vlog_left(v));
         mir_value_t right = vlog_lower_trigger(g, vlog_right(v));

         return vlog_or_triggers(g, 3, value, left, right);
      }
   case V_CONCAT:
   case V_USER_FCALL:
      {
         mir_value_t trigger = MIR_NULL_VALUE;
         const int nparams = vlog_params(v);
         for (int i = 0; i < nparams; i++) {
            mir_value_t p = vlog_lower_trigger(g, vlog_param(v, i));
            trigger = vlog_or_triggers(g, 2, trigger, p);
         }

         return trigger;
      }
   case V_PATTERN_EXPR:
      {
         mir_value_t trigger = MIR_NULL_VALUE;
         const int nparams = vlog_params(v);
         for (int i = 0; i < nparams; i++) {
            mir_value_t p = vlog_lower_trigger(g, vlog_right(vlog_param(v, i)));
            trigger = vlog_or_triggers(g, 2, trigger, p);
         }

         return trigger;
      }
   default:
      CANNOT_HANDLE(v);
   }
}

static mir_value_t vlog_lower_trigger_var(vlog_gen_t *g, vlog_node_t v)
{
   assert(vlog_kind(v) == V_EVENT_CONTROL);

   int hops = 0;
   mir_value_t var = mir_search_object(g->mu, v, &hops);
   assert(mir_is_null(var) || hops == 0);

   if (!mir_is_null(var))
      return var;

   mir_value_t trigger = MIR_NULL_VALUE;

   const int nparams = vlog_params(v);
   for (int i = 0; i < nparams; i++) {
      mir_value_t p = vlog_lower_trigger(g, vlog_param(v, i));
      trigger = vlog_or_triggers(g, 2, trigger, p);
   }

   if (mir_is_null(trigger))
      return MIR_NULL_VALUE;

   mir_type_t t_trigger = mir_trigger_type(g->mu);
   var = mir_add_var(g->mu, t_trigger, MIR_NULL_STAMP,
                     ident_uniq("trigger"), 0);
   mir_build_store(g->mu, var, trigger);

   mir_put_object(g->mu, v, var);
   return var;
}


static int64_t vlog_block_label(const block_info_t *block)
{
   return vlog_label_hash(block->label != NULL ? block->label : block->name);
}

static void vlog_block_has_fork_cb(vlog_node_t v, void *context)
{
   if (vlog_kind(v) == V_FORK && vlog_subkind(v) == V_FORK_JOIN_NONE)
      *(bool *)context = true;
}

static bool vlog_wait_is_marked(gen_stack_t *gs)
{
   // A forked process can disable a block around a wait only if some
   // block around it contains a fork

   for (gen_stack_t *it = gs; it != NULL; it = it->up) {
      if (it->block != NULL && it->block->has_fork)
         return true;
   }

   return false;
}

static void vlog_wait_mark(vlog_gen_t *g)
{
   if (g->wait_gs == NULL || !vlog_wait_is_marked(g->wait_gs))
      return;

   mir_type_t t_offset = mir_offset_type(g->mu);
   bool first = true;
   for (gen_stack_t *it = g->wait_gs; it != NULL; it = it->up) {
      if (it->block == NULL)
         continue;

      mir_value_t args[] = {
         mir_const(g->mu, t_offset, vlog_block_label(it->block)),
         mir_const(g->mu, t_offset, first)
      };
      mir_value_t res = mir_build_fcall(g->mu, ident_new("__sv_wait_mark"),
                                        t_offset, MIR_NULL_STAMP, args, 2);
      mir_build_consume(g->mu, res);
      first = false;
   }
}

static void vlog_lower_disabled_check(vlog_gen_t *g, gen_stack_t *gs)
{
   // Unwinds to the end of a block which a forked process disabled while
   // this process was suspended

   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_value_t label = mir_build_fcall(g->mu,
                                       ident_new("__sv_fork_disabled"),
                                       t_offset, MIR_NULL_STAMP, NULL, 0);

   for (gen_stack_t *it = gs; it != NULL; it = it->up) {
      if (it->block == NULL)
         continue;

      mir_block_t jump_bb = mir_add_block(g->mu);
      mir_block_t next_bb = mir_add_block(g->mu);
      mir_value_t hash =
         mir_const(g->mu, t_offset, vlog_block_label(it->block));
      mir_build_cond(g->mu, mir_build_cmp(g->mu, MIR_CMP_EQ, label, hash),
                     jump_bb, next_bb);

      mir_set_cursor(g->mu, jump_bb, MIR_APPEND);
      mir_build_jump(g->mu, it->block->exit_bb);

      mir_set_cursor(g->mu, next_bb, MIR_APPEND);
   }
}

static void vlog_wait_resume(vlog_gen_t *g)
{
   if (g->wait_gs != NULL && vlog_wait_is_marked(g->wait_gs))
      vlog_lower_disabled_check(g, g->wait_gs);
}

static void vlog_lower_event_wait(vlog_gen_t *g, mir_value_t event)
{
   mir_value_t args[] = { event };
   mir_value_t res = mir_build_fcall(g->mu, ident_new("__sv_event_wait"),
                                     mir_offset_type(g->mu), MIR_NULL_STAMP,
                                     args, 1);
   mir_build_consume(g->mu, res);

   mir_block_t wait_bb = mir_add_block(g->mu);
   vlog_wait_mark(g);
   mir_build_wait(g->mu, wait_bb);

   mir_set_cursor(g->mu, wait_bb, MIR_APPEND);
   vlog_wait_resume(g);
}

static void vlog_lower_queue_wait(vlog_gen_t *g, mir_value_t event)
{
   mir_value_t args[] = { event };
   mir_value_t res = mir_build_fcall(g->mu, ident_new("__sv_queue_wait"),
                                     mir_offset_type(g->mu), MIR_NULL_STAMP,
                                     args, 1);
   mir_build_consume(g->mu, res);

   mir_block_t wait_bb = mir_add_block(g->mu);
   vlog_wait_mark(g);
   mir_build_wait(g->mu, wait_bb);

   mir_set_cursor(g->mu, wait_bb, MIR_APPEND);
   vlog_wait_resume(g);
}

static void vlog_lower_event_trigger(vlog_gen_t *g, mir_value_t event)
{
   mir_value_t args[] = { event };
   mir_value_t res = mir_build_fcall(g->mu, ident_new("__sv_event_trigger"),
                                     mir_offset_type(g->mu), MIR_NULL_STAMP,
                                     args, 1);
   mir_build_consume(g->mu, res);
}

static bool vlog_is_event_wait(vlog_node_t ctrl)
{
   // Waiting on a single event variable is not a signal trigger

   if (vlog_params(ctrl) != 1
       || vlog_subkind(vlog_param(ctrl, 0)) != V_EVENT_LEVEL)
      return false;

   vlog_node_t etype = vlog_get_type(vlog_value(vlog_param(ctrl, 0)));
   return etype != NULL && vlog_kind(etype) == V_DATA_TYPE
      && vlog_subkind(etype) == DT_EVENT;
}

static bool vlog_is_event_item(vlog_node_t e)
{
   // An item of an event expression which is an event variable

   if (vlog_kind(e) != V_EVENT)
      return false;

   vlog_node_t etype = vlog_get_type(vlog_value(e));
   return etype != NULL && vlog_kind(etype) == V_DATA_TYPE
      && vlog_subkind(etype) == DT_EVENT;
}

static bool vlog_is_polled_control(vlog_node_t ctrl)
{
   const int nparams = vlog_params(ctrl);
   for (int i = 0; i < nparams; i++) {
      vlog_node_t p = vlog_param(ctrl, i);
      if (vlog_kind(p) == V_EVENT
          && (vlog_subkind(p) == V_EVENT_EDGE
              || vlog_needs_polling(vlog_value(p))
              || (nparams > 1 && vlog_is_event_item(p))))
         return true;
   }

   return false;
}

static bool vlog_is_sized_dynarray(vlog_node_t v)
{
   // An unpacked array declared with a typedef is a dynamic array which
   // starts with the number of elements in the typedef

   if (!vlog_has_ref(v) || vlog_kind(vlog_ref(v)) != V_VAR_DECL)
      return false;

   vlog_node_t type = vlog_get_type(vlog_ref(v));
   return type != NULL && vlog_kind(type) == V_DATA_TYPE
      && vlog_subkind(type) == DT_DYNARRAY && vlog_ranges(type) > 0;
}

static mir_value_t vlog_flatten_dynarray(vlog_gen_t *g, vlog_node_t v,
                                         mir_value_t value, bool leftmost)
{
   // Gather the elements of a dynamic array declared with a fixed size
   // into one vector

   vlog_node_t type = vlog_get_type(vlog_ref(v));

   unsigned count = 1;
   const int nranges = vlog_ranges(type);
   for (int i = 0; i < nranges; i++)
      count *= vlog_size(vlog_range(type, i));

   // Elements are numbered from the leftmost but a port with an unpacked
   // dimension has the element with the lowest index first
   int64_t left, right;
   const bool reverse = !leftmost && nranges == 1
      && vlog_bounds(vlog_range(type, 0), &left, &right) && left > right;

   const type_info_t *eti = vlog_type_info(g, vlog_type(type));

   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_type_t t_total = mir_vec4_type(g->mu, count * eti->size, false);
   mir_type_t t_elem = mir_vec4_type(g->mu, eti->size, false);
   mir_type_t t_array = mir_carray_type(g->mu, count * eti->size,
                                        mir_logic_type(g->mu));
   mir_value_t tmp = mir_add_var(g->mu, t_array, MIR_NULL_STAMP,
                                 ident_uniq("tmp"), MIR_VAR_TEMP);

   mir_value_t data = mir_build_unwrap(g->mu, value);

   vlog_loop_t loop;
   vlog_loop_begin(g, &loop, mir_const(g->mu, t_offset, 0),
                   mir_const(g->mu, t_offset, count));

   mir_value_t elem = mir_build_cast(
      g->mu, t_elem, mir_build_load(g->mu, mir_build_array_ref(g->mu, data,
                                                               loop.i)));
   mir_value_t pos = loop.i;
   if (reverse)
      pos = mir_build_sub(g->mu, t_offset,
                          mir_const(g->mu, t_offset, count - 1), loop.i);

   mir_value_t off = mir_build_mul(g->mu, t_offset, pos,
                                   mir_const(g->mu, t_offset, eti->size));
   mir_build_unpack(g->mu, elem, 0, mir_build_array_ref(g->mu, tmp, off));

   vlog_loop_end(g, &loop);

   return mir_build_pack(g->mu, t_total, tmp);
}

static mir_value_t vlog_lower_poll_vector(vlog_gen_t *g, vlog_node_t expr)
{
   mir_value_t value = vlog_lower_rvalue(g, expr);
   if (vlog_kind(expr) == V_REF && vlog_is_sized_dynarray(expr))
      value = vlog_flatten_dynarray(g, expr, value, true);

   if (!mir_is_vector(g->mu, value))
      fatal_at(vlog_loc(expr), "sorry, only integral expressions which are "
               "not signals are supported in an event control");

   const int size = mir_get_size(g->mu, mir_get_type(g->mu, value));
   return mir_build_cast(g->mu, mir_vec4_type(g->mu, size, false), value);
}

static mir_value_t vlog_lower_poll_value(vlog_gen_t *g, vlog_node_t event)
{
   mir_value_t cast = vlog_lower_poll_vector(g, vlog_value(event));

   if (vlog_subkind(event) == V_EVENT_LEVEL)
      return cast;

   // An edge is detected on the least significant bit
   mir_type_t t_offset = mir_offset_type(g->mu);
   return mir_build_extract(g->mu, mir_vec4_type(g->mu, 1, false), cast,
                            mir_const(g->mu, t_offset, 0));
}

static mir_value_t vlog_poll_is_bit(vlog_gen_t *g, mir_value_t value, int bit)
{
   mir_type_t t_logic = mir_vec4_type(g->mu, 1, false);
   mir_value_t c = mir_const_vec(g->mu, t_logic, bit, 0);
   return mir_build_test(g->mu, mir_build_binary(g->mu, MIR_VEC_CASE_EQ,
                                                 t_logic, value, c));
}

static mir_value_t vlog_poll_edge(vlog_gen_t *g, mir_value_t old,
                                  mir_value_t new, int bit)
{
   // A rising edge leaves 0 for anything else or reaches 1 from anything else

   mir_value_t left = mir_build_and(
      g->mu, vlog_poll_is_bit(g, old, !bit),
      mir_build_not(g->mu, vlog_poll_is_bit(g, new, !bit)));
   mir_value_t right = mir_build_and(
      g->mu, mir_build_not(g->mu, vlog_poll_is_bit(g, old, bit)),
      vlog_poll_is_bit(g, new, bit));
   return mir_build_or(g->mu, left, right);
}

static void vlog_poll_done(vlog_gen_t *g, mir_value_t changed,
                           mir_block_t loop_bb)
{
   // Continue after a polled condition changed, telling the kernel that
   // this poller did something so the others are checked again

   mir_block_t done_bb = mir_add_block(g->mu);
   mir_build_cond(g->mu, changed, done_bb, loop_bb);

   mir_set_cursor(g->mu, done_bb, MIR_APPEND);

   mir_value_t res = mir_build_fcall(g->mu, ident_new("__sv_poll_fired"),
                                     mir_offset_type(g->mu), MIR_NULL_STAMP,
                                     NULL, 0);
   mir_build_consume(g->mu, res);
}

static vlog_node_t vlog_poll_value_type(vlog_node_t expr)
{
   // The type of an operand of an event control which has no vector
   // representation so a change is found by comparing whole values
   // (9.4.2): strings, class handles, queues, dynamic arrays, unpacked
   // structures and unions and arrays of them.  Returns NULL for an
   // operand compared as a vector.

   vlog_node_t type = vlog_get_type(expr);
   if (type == NULL || !vlog_is_dynamic_type(type))
      return NULL;

   if (vlog_kind(type) == V_DATA_TYPE) {
      switch (vlog_subkind(type)) {
      case DT_STRING:
      case DT_DYNARRAY:
      case DT_QUEUE:
         break;
      default:
         return NULL;
      }
   }
   else if (vlog_kind(type) != V_CLASS_DECL
            && vlog_kind(type) != V_STRUCT_DECL
            && vlog_kind(type) != V_UNION_DECL)
      return NULL;

   if (vlog_kind(expr) == V_REF || vlog_kind(expr) == V_HIER_REF) {
      // A fixed size array of these types is not a single value
      if (vlog_has_ref(expr) && vlog_ranges(vlog_ref(expr)) > 0)
         return NULL;
   }

   return type;
}

static mir_value_t vlog_poll_copy(vlog_gen_t *g, vlog_node_t type,
                                  mir_value_t value)
{
   if (vlog_kind(type) == V_DATA_TYPE && vlog_subkind(type) == DT_STRING) {
      mir_value_t len = mir_build_uarray_len(g->mu, value, 0);
      return vlog_wrap_array(g, vlog_str_copy(g, value), len);
   }

   return vlog_copy_value(g, value, type);
}

static mir_value_t vlog_poll_differs(vlog_gen_t *g, vlog_node_t where,
                                     vlog_node_t type, mir_value_t a,
                                     mir_value_t b);

static mir_value_t vlog_poll_elems_differ(vlog_gen_t *g, vlog_node_t where,
                                          vlog_node_t etype, mir_value_t pa,
                                          mir_value_t pb, mir_value_t count,
                                          mir_value_t init)
{
   // Some element of two arrays of count elements differs

   mir_type_t t_bool = mir_bool_type(g->mu);
   mir_value_t flag = mir_add_var(g->mu, t_bool, MIR_NULL_STAMP,
                                  ident_uniq("differs"), MIR_VAR_TEMP);
   mir_build_store(g->mu, flag, init);

   vlog_loop_t loop;
   vlog_loop_begin(g, &loop, mir_const(g->mu, mir_offset_type(g->mu), 0),
                   count);

   mir_value_t ea = mir_build_load(g->mu, mir_build_array_ref(g->mu, pa,
                                                             loop.i));
   mir_value_t eb = mir_build_load(g->mu, mir_build_array_ref(g->mu, pb,
                                                             loop.i));
   mir_value_t diff = vlog_poll_differs(g, where, etype, ea, eb);
   mir_build_store(g->mu, flag,
                   mir_build_or(g->mu, mir_build_load(g->mu, flag), diff));

   vlog_loop_end(g, &loop);

   return mir_build_load(g->mu, flag);
}

static mir_value_t vlog_poll_differs(vlog_gen_t *g, vlog_node_t where,
                                     vlog_node_t type, mir_value_t a,
                                     mir_value_t b)
{
   // True if two values of a type differ in any bit, including x and z
   // bits, element, member, length or handle

   type = vlog_get_type(type);

   const type_info_t *ti = vlog_type_info(g, type);
   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_type_t t_bool = mir_bool_type(g->mu);

   switch (mir_get_class(g->mu, ti->type)) {
   case MIR_TYPE_VEC2:
   case MIR_TYPE_VEC4:
      return mir_build_test(
         g->mu, mir_build_binary(g->mu, MIR_VEC_CASE_NEQ, ti->type, a, b));
   case MIR_TYPE_REAL:
      return mir_build_cmp(g->mu, MIR_CMP_NEQ, a, b);
   case MIR_TYPE_CONTEXT:
      return mir_build_cmp(g->mu, MIR_CMP_NEQ, mir_build_cast(g->mu, t_offset, a),
                           mir_build_cast(g->mu, t_offset, b));
   case MIR_TYPE_UARRAY:
      if (vlog_kind(type) == V_DATA_TYPE && vlog_subkind(type) == DT_STRING) {
         mir_value_t zero = mir_const(g->mu, t_offset, 0);
         mir_value_t args[] = { a, b, zero };
         mir_value_t sc = vlog_str_call(g, "__sv_str_cmp", t_offset, args, 3);
         return mir_build_cmp(g->mu, MIR_CMP_NEQ, sc, zero);
      }
      else if (vlog_kind(type) == V_DATA_TYPE
               && (vlog_subkind(type) == DT_DYNARRAY
                   || vlog_subkind(type) == DT_QUEUE)) {
         mir_value_t la = mir_build_uarray_len(g->mu, a, 0);
         mir_value_t lb = mir_build_uarray_len(g->mu, b, 0);
         mir_value_t sizes = mir_build_cmp(g->mu, MIR_CMP_NEQ, la, lb);
         mir_value_t count = mir_build_select(g->mu, t_offset, sizes,
                                              mir_const(g->mu, t_offset, 0),
                                              la);
         return vlog_poll_elems_differ(g, where, vlog_type(type),
                                       mir_build_unwrap(g->mu, a),
                                       mir_build_unwrap(g->mu, b),
                                       count, sizes);
      }
      break;
   case MIR_TYPE_ACCESS:
      if (vlog_kind(type) == V_STRUCT_DECL || vlog_kind(type) == V_UNION_DECL) {
         mir_value_t ra = mir_build_all(g->mu, a);
         mir_value_t rb = mir_build_all(g->mu, b);

         mir_value_t any = mir_const(g->mu, t_bool, 0);
         const int ndecls = vlog_decls(type);
         for (int i = 0; i < ndecls; i++) {
            vlog_node_t d = vlog_decl(type, i);
            mir_value_t fa = mir_build_record_ref(g->mu, ra, i);
            mir_value_t fb = mir_build_record_ref(g->mu, rb, i);

            mir_value_t diff;
            if (vlog_is_fixed_unpacked(d) && vlog_size(d) > 1) {
               mir_value_t zero = mir_const(g->mu, t_offset, 0);
               diff = vlog_poll_elems_differ(
                  g, where, vlog_type(d), mir_build_array_ref(g->mu, fa, zero),
                  mir_build_array_ref(g->mu, fb, zero),
                  mir_const(g->mu, t_offset, vlog_size(d)),
                  mir_const(g->mu, t_bool, 0));
            }
            else
               diff = vlog_poll_differs(g, where, vlog_type(d),
                                        mir_build_load(g->mu, fa),
                                        mir_build_load(g->mu, fb));

            any = mir_build_or(g->mu, any, diff);
         }

         return any;
      }
      break;
   default:
      break;
   }

   fatal_at(vlog_loc(where), "sorry, a change of a value of this type cannot "
            "be detected in an event control");
}

static mir_value_t vlog_poll_read(vlog_gen_t *g, vlog_node_t expr,
                                  mir_value_t ptr)
{
   // The operand is an expression or a variable at an address
   return mir_is_null(ptr) ? vlog_lower_rvalue(g, expr)
      : mir_build_load(g->mu, ptr);
}

static mir_value_t vlog_poll_snapshot(vlog_gen_t *g, vlog_node_t expr,
                                      mir_value_t ptr, vlog_node_t type)
{
   // Keep a copy of the current value of an operand compared by value

   mir_value_t value = vlog_poll_read(g, expr, ptr);
   mir_value_t var = mir_add_var(g->mu, mir_get_type(g->mu, value),
                                 MIR_NULL_STAMP, ident_uniq("poll"), 0);
   mir_build_store(g->mu, var, vlog_poll_copy(g, type, value));
   return var;
}

static mir_value_t vlog_poll_value_changed(vlog_gen_t *g, vlog_node_t expr,
                                           mir_value_t ptr, vlog_node_t type,
                                           mir_value_t var)
{
   mir_value_t value = vlog_poll_read(g, expr, ptr);
   mir_value_t changed = vlog_poll_differs(g, expr, type,
                                           mir_build_load(g->mu, var), value);
   mir_build_store(g->mu, var, vlog_poll_copy(g, type, value));
   return changed;
}

static void vlog_lower_poll_value_wait(vlog_gen_t *g, vlog_node_t expr,
                                       mir_value_t ptr, vlog_node_t type)
{
   // Suspend until an operand compared by value changes

   mir_value_t var = vlog_poll_snapshot(g, expr, ptr, type);

   mir_block_t loop_bb = mir_add_block(g->mu);
   mir_build_jump(g->mu, loop_bb);

   mir_set_cursor(g->mu, loop_bb, MIR_APPEND);

   mir_value_t res = mir_build_fcall(g->mu, ident_new("__sv_poll_wait"),
                                     mir_offset_type(g->mu), MIR_NULL_STAMP,
                                     NULL, 0);
   mir_build_consume(g->mu, res);

   mir_block_t resume_bb = mir_add_block(g->mu);
   vlog_wait_mark(g);
   mir_build_wait(g->mu, resume_bb);

   mir_set_cursor(g->mu, resume_bb, MIR_APPEND);
   vlog_wait_resume(g);

   mir_value_t changed = vlog_poll_value_changed(g, expr, ptr, type, var);

   vlog_poll_done(g, changed, loop_bb);
}

static void vlog_lower_poll_control(vlog_gen_t *g, vlog_node_t ctrl)
{
   const int nparams = vlog_params(ctrl);
   mir_value_t *vars LOCAL = xmalloc_array(nparams, sizeof(mir_value_t));

   // Values in registers do not survive suspending so the previous values
   // are kept in variables
   for (int i = 0; i < nparams; i++) {
      vlog_node_t e = vlog_param(ctrl, i);
      if (vlog_kind(e) != V_EVENT)
         CANNOT_HANDLE(e);

      if (vlog_is_event_item(e)) {
         // Remember whether it has already been triggered in this time step
         mir_type_t t_offset = mir_offset_type(g->mu);
         mir_value_t args[] = { vlog_lower_rvalue(g, vlog_value(e)) };
         mir_value_t now = mir_build_fcall(g->mu,
                                           ident_new("__sv_event_triggered"),
                                           t_offset, MIR_NULL_STAMP, args, 1);
         mir_build_consume(g->mu, now);
         vars[i] = mir_add_var(g->mu, t_offset, MIR_NULL_STAMP,
                               ident_uniq("poll"), 0);
         mir_build_store(g->mu, vars[i], now);
         continue;
      }

      vlog_node_t vtype = vlog_poll_value_type(vlog_value(e));
      if (vtype != NULL) {
         if (vlog_subkind(e) != V_EVENT_LEVEL)
            fatal_at(vlog_loc(e), "sorry, an edge of a value of this type "
                     "cannot be detected");
         vars[i] = vlog_poll_snapshot(g, vlog_value(e), MIR_NULL_VALUE,
                                      vtype);
         continue;
      }

      mir_value_t value = vlog_lower_poll_value(g, e);
      vars[i] = mir_add_var(g->mu, mir_get_type(g->mu, value), MIR_NULL_STAMP,
                            ident_uniq("poll"), 0);
      mir_build_store(g->mu, vars[i], value);
   }

   mir_block_t loop_bb = mir_add_block(g->mu);
   mir_build_jump(g->mu, loop_bb);

   mir_set_cursor(g->mu, loop_bb, MIR_APPEND);

   mir_value_t res = mir_build_fcall(g->mu, ident_new("__sv_poll_wait"),
                                     mir_offset_type(g->mu), MIR_NULL_STAMP,
                                     NULL, 0);
   mir_build_consume(g->mu, res);

   mir_block_t resume_bb = mir_add_block(g->mu);
   vlog_wait_mark(g);
   mir_build_wait(g->mu, resume_bb);

   mir_set_cursor(g->mu, resume_bb, MIR_APPEND);
   vlog_wait_resume(g);

   mir_value_t any = MIR_NULL_VALUE;

   for (int i = 0; i < nparams; i++) {
      vlog_node_t e = vlog_param(ctrl, i);

      if (vlog_is_event_item(e)) {
         mir_type_t t_offset = mir_offset_type(g->mu);
         mir_value_t zero = mir_const(g->mu, t_offset, 0);
         mir_value_t args[] = { vlog_lower_rvalue(g, vlog_value(e)) };
         mir_value_t now = mir_build_fcall(g->mu,
                                           ident_new("__sv_event_triggered"),
                                           t_offset, MIR_NULL_STAMP, args, 1);
         mir_build_consume(g->mu, now);
         mir_value_t old = mir_build_load(g->mu, vars[i]);
         mir_value_t changed = mir_build_and(
            g->mu, mir_build_cmp(g->mu, MIR_CMP_NEQ, now, zero),
            mir_build_cmp(g->mu, MIR_CMP_EQ, old, zero));
         mir_build_store(g->mu, vars[i], now);
         any = i == 0 ? changed : mir_build_or(g->mu, any, changed);
         continue;
      }

      vlog_node_t vtype = vlog_poll_value_type(vlog_value(e));
      if (vtype != NULL) {
         mir_value_t changed =
            vlog_poll_value_changed(g, vlog_value(e), MIR_NULL_VALUE, vtype,
                                    vars[i]);
         any = i == 0 ? changed : mir_build_or(g->mu, any, changed);
         continue;
      }

      mir_value_t new = vlog_lower_poll_value(g, e);
      mir_value_t old = mir_build_load(g->mu, vars[i]);

      mir_value_t changed;
      switch (vlog_subkind(e)) {
      case V_EVENT_LEVEL:
         changed = mir_build_test(
            g->mu, mir_build_binary(g->mu, MIR_VEC_CASE_NEQ,
                                    mir_get_type(g->mu, new), old, new));
         break;
      case V_EVENT_POSEDGE:
         changed = vlog_poll_edge(g, old, new, 1);
         break;
      case V_EVENT_NEGEDGE:
         changed = vlog_poll_edge(g, old, new, 0);
         break;
      default:
         changed = mir_build_or(g->mu, vlog_poll_edge(g, old, new, 1),
                                vlog_poll_edge(g, old, new, 0));
         break;
      }

      mir_build_store(g->mu, vars[i], new);

      any = i == 0 ? changed : mir_build_or(g->mu, any, changed);
   }

   vlog_poll_done(g, any, loop_bb);
}

typedef A(vlog_node_t) poll_leaves_t;

static void vlog_poll_leaves(vlog_node_t e, poll_leaves_t *leaves,
                             bool *by_value)
{
   // The names an expression reads, each as a whole

   switch (vlog_kind(e)) {
   case V_REF:
   case V_HIER_REF:
   case V_MEMBER_REF:
   case V_BIT_SELECT:
   case V_PART_SELECT:
      if (vlog_kind(e) == V_REF && vlog_has_ref(e)) {
         switch (vlog_kind(vlog_ref(e))) {
         case V_PARAM_DECL:
         case V_LOCALPARAM:
         case V_ENUM_NAME:
         case V_GENVAR_DECL:
            return;
         default:
            break;
         }
      }
      for (int i = 0; i < leaves->count; i++) {
         if (leaves->items[i] == e)
            return;
      }
      APUSH(*leaves, e);
      *by_value |= vlog_poll_value_type(e) != NULL;
      break;
   case V_NUMBER:
   case V_STRING:
   case V_REAL:
   case V_EMPTY:
      break;
   case V_BINARY:
      vlog_poll_leaves(vlog_left(e), leaves, by_value);
      vlog_poll_leaves(vlog_right(e), leaves, by_value);
      break;
   case V_COND_EXPR:
      vlog_poll_leaves(vlog_value(e), leaves, by_value);
      vlog_poll_leaves(vlog_left(e), leaves, by_value);
      vlog_poll_leaves(vlog_right(e), leaves, by_value);
      break;
   case V_UNARY:
   case V_CAST:
      vlog_poll_leaves(vlog_value(e), leaves, by_value);
      break;
   case V_PATTERN_ITEM:
      if (vlog_has_right(e))
         vlog_poll_leaves(vlog_right(e), leaves, by_value);
      break;
   case V_METHOD_CALL:
      if (vlog_has_value(e))
         vlog_poll_leaves(vlog_value(e), leaves, by_value);
      // Fall-through
   case V_PATTERN_EXPR:
   case V_CONCAT:
   case V_USER_FCALL:
   case V_SYS_FCALL:
      {
         const int nparams = vlog_params(e);
         for (int i = 0; i < nparams; i++)
            vlog_poll_leaves(vlog_param(e, i), leaves, by_value);
      }
      break;
   default:
      APUSH(*leaves, e);
      break;
   }
}

static void vlog_lower_poll_leaves(vlog_gen_t *g, const poll_leaves_t *leaves)
{
   // Suspend until any of the names an expression reads changes

   const int n = leaves->count;
   mir_value_t *vars LOCAL = xmalloc_array(n, sizeof(mir_value_t));
   vlog_node_t *types LOCAL = xmalloc_array(n, sizeof(vlog_node_t));

   for (int i = 0; i < n; i++) {
      vlog_node_t e = leaves->items[i];
      if ((types[i] = vlog_poll_value_type(e)) != NULL)
         vars[i] = vlog_poll_snapshot(g, e, MIR_NULL_VALUE, types[i]);
      else {
         mir_value_t value = vlog_lower_poll_vector(g, e);
         vars[i] = mir_add_var(g->mu, mir_get_type(g->mu, value),
                               MIR_NULL_STAMP, ident_uniq("poll"), 0);
         mir_build_store(g->mu, vars[i], value);
      }
   }

   mir_block_t loop_bb = mir_add_block(g->mu);
   mir_build_jump(g->mu, loop_bb);

   mir_set_cursor(g->mu, loop_bb, MIR_APPEND);

   mir_value_t res = mir_build_fcall(g->mu, ident_new("__sv_poll_wait"),
                                     mir_offset_type(g->mu), MIR_NULL_STAMP,
                                     NULL, 0);
   mir_build_consume(g->mu, res);

   mir_block_t resume_bb = mir_add_block(g->mu);
   vlog_wait_mark(g);
   mir_build_wait(g->mu, resume_bb);

   mir_set_cursor(g->mu, resume_bb, MIR_APPEND);
   vlog_wait_resume(g);

   mir_value_t any = mir_const(g->mu, mir_bool_type(g->mu), 0);
   for (int i = 0; i < n; i++) {
      vlog_node_t e = leaves->items[i];
      mir_value_t changed;
      if (types[i] != NULL)
         changed = vlog_poll_value_changed(g, e, MIR_NULL_VALUE, types[i],
                                           vars[i]);
      else {
         mir_value_t new = vlog_lower_poll_vector(g, e);
         mir_value_t old = mir_build_load(g->mu, vars[i]);
         changed = mir_build_test(
            g->mu, mir_build_binary(g->mu, MIR_VEC_CASE_NEQ,
                                    mir_get_type(g->mu, new), old, new));
         mir_build_store(g->mu, vars[i], new);
      }

      any = mir_build_or(g->mu, any, changed);
   }

   vlog_poll_done(g, any, loop_bb);
}

static void vlog_lower_poll_expr(vlog_gen_t *g, vlog_node_t expr)
{
   // Suspend until the value of an expression which is not a signal changes

   vlog_node_t vtype = vlog_poll_value_type(expr);
   if (vtype != NULL) {
      vlog_lower_poll_value_wait(g, expr, MIR_NULL_VALUE, vtype);
      return;
   }

   // An expression of a type with no vector representation made from
   // such operands waits for any of the operands to change
   poll_leaves_t leaves = AINIT;
   bool by_value = false;
   vlog_poll_leaves(expr, &leaves, &by_value);
   if (by_value) {
      vlog_lower_poll_leaves(g, &leaves);
      ACLEAR(leaves);
      return;
   }
   ACLEAR(leaves);

   mir_value_t value = vlog_lower_poll_vector(g, expr);
   mir_type_t type = mir_get_type(g->mu, value);
   mir_value_t var = mir_add_var(g->mu, type, MIR_NULL_STAMP,
                                 ident_uniq("poll"), 0);
   mir_build_store(g->mu, var, value);

   mir_block_t loop_bb = mir_add_block(g->mu);
   mir_build_jump(g->mu, loop_bb);

   mir_set_cursor(g->mu, loop_bb, MIR_APPEND);

   mir_value_t res = mir_build_fcall(g->mu, ident_new("__sv_poll_wait"),
                                     mir_offset_type(g->mu), MIR_NULL_STAMP,
                                     NULL, 0);
   mir_build_consume(g->mu, res);

   mir_block_t resume_bb = mir_add_block(g->mu);
   vlog_wait_mark(g);
   mir_build_wait(g->mu, resume_bb);

   mir_set_cursor(g->mu, resume_bb, MIR_APPEND);
   vlog_wait_resume(g);

   mir_value_t new = vlog_lower_poll_vector(g, expr);
   mir_value_t old = mir_build_load(g->mu, var);
   mir_value_t changed = mir_build_test(
      g->mu, mir_build_binary(g->mu, MIR_VEC_CASE_NEQ, type, old, new));
   mir_build_store(g->mu, var, new);

   vlog_poll_done(g, changed, loop_bb);
}

static bool vlog_uses_memory_member(vlog_node_t v)
{
   // A member of an unpacked structure or a class is held in memory
   // rather than in a signal

   switch (vlog_kind(v)) {
   case V_REF:
      {
         // A whole unpacked structure is not a vector of signals either,
         // nor is a string, queue, dynamic array or class handle
         vlog_node_t type = vlog_get_type(v);
         if (type != NULL && vlog_poll_value_type(v) != NULL)
            return true;
         else if (type != NULL && (vlog_kind(type) == V_STRUCT_DECL
                                   || vlog_kind(type) == V_UNION_DECL)
                  && !(vlog_flags(type) & VLOG_F_PACKED))
            return true;

         return vlog_is_sized_dynarray(v);
      }
   case V_METHOD_CALL:
      return true;
   case V_MEMBER_REF:
      {
         vlog_node_t type = vlog_get_type(vlog_value(v));
         if (type != NULL && vlog_is_dynamic_type(type))
            return true;

         return vlog_uses_memory_member(vlog_value(v));
      }
   case V_BIT_SELECT:
      if (vlog_uses_memory_member(vlog_value(v)))
         return true;
      // Fall-through
   case V_CONCAT:
   case V_SYS_FCALL:
   case V_USER_FCALL:
      {
         const int nparams = vlog_params(v);
         for (int i = 0; i < nparams; i++) {
            if (vlog_uses_memory_member(vlog_param(v, i)))
               return true;
         }

         return false;
      }
   case V_PART_SELECT:
      return vlog_uses_memory_member(vlog_value(v))
         || vlog_uses_memory_member(vlog_left(v))
         || vlog_uses_memory_member(vlog_right(v));
   case V_BINARY:
      return vlog_uses_memory_member(vlog_left(v))
         || vlog_uses_memory_member(vlog_right(v));
   case V_UNARY:
   case V_CAST:
      return vlog_uses_memory_member(vlog_value(v));
   case V_COND_EXPR:
      return vlog_uses_memory_member(vlog_value(v))
         || vlog_uses_memory_member(vlog_left(v))
         || vlog_uses_memory_member(vlog_right(v));
   case V_PATTERN_EXPR:
      {
         const int nparams = vlog_params(v);
         for (int i = 0; i < nparams; i++) {
            if (vlog_uses_memory_member(vlog_param(v, i)))
               return true;
         }

         return false;
      }
   case V_PATTERN_ITEM:
      return vlog_has_right(v) && vlog_uses_memory_member(vlog_right(v));
   default:
      return false;
   }
}

static void vlog_lower_timing_ctrl(vlog_gen_t *g, vlog_node_t ctrl)
{
   // An event with iff only counts if the expression is true at that time

   bool guarded = false;
   if (vlog_kind(ctrl) == V_EVENT_CONTROL) {
      const int nparams = vlog_params(ctrl);
      for (int i = 0; i < nparams; i++) {
         vlog_node_t e = vlog_param(ctrl, i);
         guarded |= vlog_kind(e) == V_EVENT && vlog_has_left(e);
      }
   }

   if (!guarded) {
      vlog_lower_event_or_delay(g, ctrl);
      return;
   }

   mir_block_t wait_bb = mir_add_block(g->mu);
   mir_block_t done_bb = mir_add_block(g->mu);

   mir_build_jump(g->mu, wait_bb);
   mir_set_cursor(g->mu, wait_bb, MIR_APPEND);

   vlog_lower_event_or_delay(g, ctrl);

   const int nparams = vlog_params(ctrl);
   for (int i = 0; i < nparams; i++) {
      vlog_node_t e = vlog_param(ctrl, i);
      if (!vlog_has_left(e)) {
         mir_build_jump(g->mu, done_bb);
         break;
      }

      mir_block_t next_bb = i + 1 == nparams ? wait_bb : mir_add_block(g->mu);
      mir_build_cond(g->mu,
                     vlog_lower_test(g, vlog_lower_rvalue(g, vlog_left(e))),
                     done_bb, next_bb);

      if (i + 1 < nparams)
         mir_set_cursor(g->mu, next_bb, MIR_APPEND);
   }

   mir_set_cursor(g->mu, done_bb, MIR_APPEND);
}

static void vlog_lower_event_or_delay(vlog_gen_t *g, vlog_node_t ctrl)
{
   switch (vlog_kind(ctrl)) {
   case V_DELAY_CONTROL:
      {
         mir_value_t cast = vlog_lower_delay(g, ctrl);

         int64_t dconst;
         if (mir_get_const(g->mu, cast, &dconst) && dconst == 0)
            mir_build_sched_inactive(g->mu);
         else
            mir_build_sched_process(g->mu, cast);

         mir_block_t wait_bb = mir_add_block(g->mu);
         vlog_wait_mark(g);
         mir_build_wait(g->mu, wait_bb);

         mir_set_cursor(g->mu, wait_bb, MIR_APPEND);
         vlog_wait_resume(g);
      }
      break;
   case V_EVENT_CONTROL:
      if (vlog_is_event_wait(ctrl)) {
         mir_value_t event =
            vlog_lower_rvalue(g, vlog_value(vlog_param(ctrl, 0)));
         vlog_lower_event_wait(g, event);
         break;
      }
      else if (vlog_is_polled_control(ctrl)) {
         vlog_lower_poll_control(g, ctrl);
         break;
      }

      {
         mir_value_t trigger_var = vlog_lower_trigger_var(g, ctrl);
         if (!mir_is_null(trigger_var)) {
            mir_value_t trigger = mir_build_load(g->mu, trigger_var);
            mir_build_sched_event(g->mu, trigger, MIR_NULL_VALUE);
         }

         mir_block_t wait_bb = mir_add_block(g->mu);
         vlog_wait_mark(g);
         mir_build_wait(g->mu, wait_bb);

         mir_set_cursor(g->mu, wait_bb, MIR_APPEND);

         if (!mir_is_null(trigger_var)) {
            mir_value_t trigger = mir_build_load(g->mu, trigger_var);
            mir_build_clear_event(g->mu, trigger, MIR_NULL_VALUE);
         }

         vlog_wait_resume(g);
      }
      break;
   default:
      CANNOT_HANDLE(ctrl);
   }
}

static void vlog_lower_timing(vlog_gen_t *g, vlog_node_t v, gen_stack_t *gs)
{
   gen_stack_t *saved = g->wait_gs;
   g->wait_gs = gs;
   vlog_lower_timing_ctrl(g, vlog_value(v));
   g->wait_gs = saved;

   vlog_lower_stmts(g, v, gs);
}

static mir_value_t vlog_lower_default_value(vlog_gen_t *g,
                                            const type_info_t *ti)
{
   switch (mir_get_class(g->mu, ti->type)) {
   case MIR_TYPE_VEC2:
      if (ti->size <= 64)
         return mir_const_vec(g->mu, ti->type, 0, 0);
      else {
         mir_type_t t_bit = mir_vec2_type(g->mu, 1, false);
         mir_value_t zero = mir_const_vec(g->mu, t_bit, 0, 0);
         return mir_build_cast(g->mu, ti->type, zero);
      }
   case MIR_TYPE_VEC4:
      return vlog_lower_x(g, ti->type);
   case MIR_TYPE_REAL:
      return mir_const_real(g->mu, ti->type, 0.0);
   case MIR_TYPE_UARRAY:
      if (vlog_kind(ti->node) == V_DATA_TYPE
          && vlog_subkind(ti->node) == DT_DYNARRAY
          && vlog_ranges(ti->node) > 0) {
         // Fixed size array from a typedef
         mir_type_t t_offset = mir_offset_type(g->mu);
         const type_info_t *eti = vlog_type_info(g, vlog_type(ti->node));
         mir_value_t count =
            mir_const(g->mu, t_offset, vlog_size(vlog_range(ti->node, 0)));
         mir_value_t data = vlog_alloc_array(g, eti->type, count);
         vlog_init_elements(g, data, mir_const(g->mu, t_offset, 0), count,
                            eti);
         return vlog_wrap_array(g, data, count);
      }
      return vlog_empty_array(g, ti->type);
   case MIR_TYPE_CONTEXT:
      return mir_build_null(g->mu, ti->type);
   case MIR_TYPE_ACCESS:
      {
         vlog_node_t node = ti->node;
         if (vlog_kind(node) == V_DATA_TYPE) {
            if (vlog_subkind(node) == DT_EVENT) {
               mir_type_t t_cell = mir_int_type(g->mu, INT64_MIN, INT64_MAX);
               return mir_build_new(g->mu, t_cell, MIR_NULL_STAMP,
                                    MIR_NULL_VALUE);
            }

            return mir_build_null(g->mu, ti->type);   // Mailbox
         }

         mir_type_t t_rec = mir_get_elem(g->mu, ti->type);
         mir_value_t acc = mir_build_new(g->mu, t_rec, MIR_NULL_STAMP,
                                         MIR_NULL_VALUE);
         mir_value_t rec = mir_build_all(g->mu, acc);

         const int ndecls = vlog_decls(node);
         for (int i = 0; i < ndecls; i++) {
            vlog_node_t d = vlog_decl(node, i);
            const type_info_t *fti = vlog_type_info(g, vlog_type(d));

            if (vlog_is_fixed_unpacked(d) && vlog_size(d) > 1) {
               // Every element of an array member starts with the default
               // or the initial value of the member
               vlog_store_local_init(g, d, mir_build_record_ref(g->mu, rec, i));
               continue;
            }

            mir_value_t init;
            if (vlog_has_value(d))
               init = vlog_lower_cast(g, fti->type, vlog_lower_with_context(
                                         g, vlog_value(d), fti->type));
            else
               init = vlog_lower_default_value(g, fti);
            mir_build_store(g->mu, mir_build_record_ref(g->mu, rec, i), init);
         }

         return acc;
      }
   default:
      should_not_reach_here();
   }
}

static mir_value_t vlog_force_source(vlog_gen_t *g,
                                     const vlog_select_t *lvalue,
                                     mir_value_t cast)
{
   mir_value_t unpacked = cast;
   if (mir_is_vector(g->mu, cast)) {
      mir_value_t tmp = MIR_NULL_VALUE;
      if (lvalue->size > 1) {
         mir_type_t t_elem = mir_logic_type(g->mu);
         mir_type_t t_array = mir_carray_type(g->mu, lvalue->size, t_elem);
         tmp = vlog_get_temp(g, t_array);
      }

      unpacked = mir_build_unpack(g->mu, cast, 0, tmp);
   }

   mir_value_t src = mir_build_array_ref(g->mu, unpacked, lvalue->src_offset);
   return vlog_lvalue_source(g, lvalue, src);
}

static void vlog_lower_force(vlog_gen_t *g, vlog_node_t v)
{
   vlog_node_t target = vlog_target(v);
   if (vlog_kind(target) == V_CONCAT)
      fatal_at(vlog_loc(v), "sorry, force and release of a concatenation "
               "are not supported");

   vlog_select_t lvalue = vlog_lower_select(g, target);
   if (!mir_is_signal(g->mu, lvalue.obj))
      fatal_at(vlog_loc(v), "sorry, force and release of this kind of "
               "variable are not supported");

   const bool is_var = vlog_is_variable_target(target);

   mir_value_t nets = mir_build_array_ref(g->mu, lvalue.obj,
                                          lvalue.dst_offset);

   if (vlog_kind(v) == V_RELEASE) {
      if (is_var) {
         // Keeps the forced value until the next assignment
         mir_value_t cur =
            vlog_lower_cast(g, lvalue.type, vlog_lower_rvalue(g, target));
         mir_type_t t_time = mir_time_type(g->mu);
         mir_build_sched_deposit(g->mu, nets, lvalue.count,
                                 vlog_force_source(g, &lvalue, cur),
                                 mir_const(g->mu, t_time, -1));
      }

      mir_build_release(g->mu, nets, lvalue.count);
      return;
   }

   vlog_node_t rhs = vlog_value(v);
   mir_value_t value = vlog_lower_with_context(
      g, rhs, vlog_assign_context(g, lvalue.type, rhs));
   mir_value_t cast = vlog_lower_cast(g, lvalue.type, value);

   // The forced value of a variable is seen immediately
   if (is_var) {
      mir_build_release(g->mu, nets, lvalue.count);
      vlog_assign_variable(g, target, cast, NULL);
   }

   mir_value_t src = vlog_force_source(g, &lvalue, cast);

   mir_build_force(g->mu, nets, lvalue.count, src);
}

static void vlog_lower_blocking_assignment(vlog_gen_t *g, vlog_node_t v)
{
   if (vlog_has_delay(v)) {
      vlog_node_t delay = vlog_delay(v);
      assert(vlog_kind(delay) == V_DELAY_CONTROL);

   if (!vlog_has_delay(v)) {
      vlog_assign_variable(g, vlog_target(v), MIR_NULL_VALUE, vlog_value(v));
      return;
   }

   // The right hand side is evaluated before the delay or event

   vlog_node_t target = vlog_target(v), rhs = vlog_value(v);
   if (vlog_kind(target) == V_CONCAT || vlog_kind(rhs) == V_PATTERN_EXPR
       || vlog_is_dynamic_type(vlog_get_type(target)))
      fatal_at(vlog_loc(v), "sorry, this kind of intra-assignment timing "
               "control is not supported");

   vlog_select_t lvalue = vlog_lower_select(g, target);
   const mir_class_t class = mir_get_class(g->mu, lvalue.type);
   if (class != MIR_TYPE_VEC2 && class != MIR_TYPE_VEC4)
      fatal_at(vlog_loc(v), "sorry, this kind of intra-assignment timing "
               "control is not supported");

   mir_type_t type = lvalue.type;
   mir_value_t value = vlog_lower_with_context(
      g, rhs, vlog_assign_context(g, type, rhs));
   mir_value_t cast = vlog_lower_cast(g, type, value);

   // Not a temporary as it must survive suspending the process
   mir_value_t saved = mir_add_var(g->mu, type, MIR_NULL_STAMP,
                                   ident_uniq("rhs"), 0);
   mir_build_store(g->mu, saved, cast);

   vlog_node_t delay = vlog_delay(v);
   if (vlog_kind(delay) == V_DELAY_CONTROL) {
      mir_build_sched_process(g->mu, vlog_lower_delay(g, delay));

      mir_block_t delay_bb = mir_add_block(g->mu);
      mir_build_wait(g->mu, delay_bb);

      mir_set_cursor(g->mu, delay_bb, MIR_APPEND);
   }
   else {
      assert(vlog_kind(delay) == V_EVENT_CONTROL);

      mir_type_t t_offset = mir_offset_type(g->mu);
      mir_value_t zero = mir_const(g->mu, t_offset, 0);
      mir_value_t one = mir_const(g->mu, t_offset, 1);

      // A repeat count of zero or less does not wait
      mir_value_t count = one;
      if (vlog_has_value(delay))
         count = vlog_lower_cast(g, t_offset,
                                 vlog_lower_rvalue(g, vlog_value(delay)));

      mir_value_t remain = mir_add_var(g->mu, t_offset, MIR_NULL_STAMP,
                                       ident_uniq("repeat"), 0);
      mir_build_store(g->mu, remain, count);

      mir_block_t test_bb = mir_add_block(g->mu);
      mir_block_t body_bb = mir_add_block(g->mu);
      mir_block_t done_bb = mir_add_block(g->mu);

      mir_build_jump(g->mu, test_bb);

      mir_set_cursor(g->mu, test_bb, MIR_APPEND);
      mir_value_t left = mir_build_load(g->mu, remain);
      mir_build_cond(g->mu, mir_build_cmp(g->mu, MIR_CMP_GT, left, zero),
                     body_bb, done_bb);

      mir_set_cursor(g->mu, body_bb, MIR_APPEND);
      mir_build_store(g->mu, remain,
                      mir_build_sub(g->mu, t_offset, left, one));

      vlog_lower_timing_ctrl(g, delay);

      mir_build_jump(g->mu, test_bb);

      mir_set_cursor(g->mu, done_bb, MIR_APPEND);
   }

   vlog_assign_variable(g, target, mir_build_load(g->mu, saved), NULL);
}

static void vlog_lower_nba_store(vlog_gen_t *g, vlog_node_t v,
                                 const vlog_select_t *lvalue,
                                 mir_value_t value, int offset,
                                 mir_value_t after)
{
   // The target is plain memory so the runtime keeps a copy of the new
   // value and writes it during the non-blocking assignment region

   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_type_t t_dest = mir_get_pointer(g->mu,
                                       mir_get_type(g->mu, lvalue->obj));

   int64_t bytes = 0;
   switch (mir_get_class(g->mu, t_dest)) {
   case MIR_TYPE_VEC2:
   case MIR_TYPE_REAL:
   case MIR_TYPE_ACCESS:
   case MIR_TYPE_POINTER:
   case MIR_TYPE_CONTEXT:
      bytes = sizeof(uint64_t);
      break;
   case MIR_TYPE_VEC4:
      bytes = 2 * sizeof(uint64_t);
      break;
   case MIR_TYPE_UARRAY:
      if (mir_get_dims(g->mu, t_dest) == 1)
         bytes = sizeof(void *) + 2 * sizeof(int64_t);
      break;
   default:
      break;
   }

   if (bytes == 0)
      fatal_at(vlog_loc(v), "sorry, a non-blocking assignment to a variable "
               "of this type is not supported");

   mir_value_t count = mir_const(g->mu, t_offset, -1);   // The whole variable
   mir_value_t dst_offset = mir_const(g->mu, t_offset, 0);
   int64_t wide = 0;

   if (mir_is_vector(g->mu, value)) {
      const int size = mir_get_size(g->mu, t_dest);

      int64_t count_const, offset_const;
      mir_type_t part_type = mir_get_type(g->mu, value);
      const bool const_count = mir_get_const(g->mu, lvalue->count, &count_const);
      if (const_count)
         part_type = mir_vector_slice(g->mu, part_type, count_const);

      const bool whole = const_count && count_const == size
         && mir_get_const(g->mu, lvalue->dst_offset, &offset_const)
         && offset_const == 0;

      if (!whole) {
         if (size > 64)
            wide = mir_get_class(g->mu, t_dest) == MIR_TYPE_VEC4 ? size : -size;

         // The offset of a select counts from the most significant bit
         count = lvalue->count;
         dst_offset = mir_build_sub(g->mu, t_offset,
                                    mir_build_sub(g->mu, t_offset,
                                                  mir_const(g->mu, t_offset,
                                                            size),
                                                  lvalue->dst_offset),
                                    count);
      }

      mir_value_t pos = mir_build_add(g->mu, t_offset,
                                      mir_const(g->mu, t_offset, offset),
                                      lvalue->src_offset);
      mir_value_t part = mir_build_extract(g->mu, part_type, value, pos);
      value = mir_build_cast(g->mu, t_dest, part);
   }

   mir_value_t tmp = mir_add_var(g->mu, t_dest, MIR_NULL_STAMP,
                                 ident_uniq("nba"), MIR_VAR_TEMP);
   mir_build_store(g->mu, tmp, value);

   mir_value_t args[] = {
      lvalue->obj, tmp, mir_const(g->mu, t_offset, bytes), dst_offset,
      count, mir_build_cast(g->mu, t_offset, after),
      mir_const(g->mu, t_offset, wide)
   };
   mir_value_t res = mir_build_fcall(g->mu, ident_new("__sv_nba_store"),
                                     t_offset, MIR_NULL_STAMP, args,
                                     ARRAY_LEN(args));
   mir_build_consume(g->mu, res);
}

static void vlog_lower_non_blocking_assignment(vlog_gen_t *g, vlog_node_t v)
{
   vlog_node_t target = vlog_target(v);

   mir_type_t type;
   mir_class_t class = MIR_TYPE_VEC4;
   unsigned nlvalues = 1, targetsz = 0;
   vlog_select_t lvalue1, *lvalues = &lvalue1;
   if (vlog_kind(target) == V_CONCAT) {
      nlvalues = vlog_count_lvalues(target);
      lvalues = xmalloc_array(nlvalues, sizeof(vlog_select_t));

      unsigned index = 0;
      vlog_lower_lvalues(g, target, lvalues, NULL, &index);
      assert(index == nlvalues);

      for (int i = 0; i < nlvalues; i++)
         targetsz += lvalues[i].size;

      type = mir_vec4_type(g->mu, targetsz, false);
   }
   else {
      lvalue1 = vlog_lower_select(g, target);
      targetsz = lvalue1.size;
      class = mir_get_class(g->mu, lvalue1.type);
      type = lvalue1.type;
   }

   bool any_signal = false;
   for (int i = 0; i < nlvalues; i++) {
      if (mir_is_signal(g->mu, lvalues[i].obj))
         any_signal = true;
   }

   mir_type_t t_time = mir_time_type(g->mu);
   mir_type_t t_offset = mir_offset_type(g->mu);

   vlog_node_t dims[MAX_UNPACKED_DIMS];
   int ndims;

   mir_value_t unpacked = MIR_NULL_VALUE, source;
   if (class == MIR_TYPE_VEC2 || class == MIR_TYPE_VEC4) {
      vlog_node_t rhs = vlog_value(v);
      mir_type_t context = vlog_assign_context(g, type, rhs);
      mir_value_t value = vlog_lower_with_context(g, rhs, context);
      mir_value_t resize = vlog_lower_cast(g, type, value);
      assert(mir_is_vector(g->mu, resize));

      source = resize;

      if (any_signal) {
         mir_value_t tmp = MIR_NULL_VALUE;
         if (targetsz > 1) {
            mir_type_t t_elem = mir_logic_type(g->mu);
            mir_type_t t_array = mir_carray_type(g->mu, targetsz, t_elem);
            tmp = vlog_get_temp(g, t_array);
         }

         const uint8_t strength = vlog_is_net(target) ? ST_STRONG : 0;
         unpacked = mir_build_unpack(g->mu, resize, strength, tmp);
      }
   }
   else if (class == MIR_TYPE_REAL && nlvalues == 1 && any_signal
            && vlog_kind(vlog_value(v)) == V_PATTERN_EXPR
            && (ndims = vlog_unpacked_dims(target, dims)) > 0) {
      mir_value_t tmp = vlog_real_array_pattern(
         g, vlog_value(v), dims, ndims, vlog_array_elem_info(g, target));
      unpacked = source =
         mir_build_array_ref(g->mu, tmp, mir_const(g->mu, t_offset, 0));
   }
   else {
      vlog_node_t rhs = vlog_value(v);
      mir_value_t value =
         vlog_lower_with_context(g, rhs, lvalues[0].type);
      unpacked = source = vlog_lower_cast(g, lvalues[0].type, value);

      // The target takes its own copy at the time of the assignment
      vlog_node_t ttype = vlog_get_type(target);
      if (ttype != NULL && vlog_is_dynamic_type(ttype)
          && vlog_kind(rhs) != V_DYNAMIC_NEW && vlog_kind(rhs) != V_CONCAT
          && vlog_kind(rhs) != V_PATTERN_EXPR)
         source = vlog_copy_value(g, source, ttype);
   }

   mir_value_t after;
   if (vlog_has_delay(v)) {
      vlog_node_t delay = vlog_delay(v);
      if (vlog_kind(delay) != V_DELAY_CONTROL)
         fatal_at(vlog_loc(v), "sorry, an event control in a non-blocking "
                  "assignment is not supported");

      after = vlog_lower_delay(g, delay);
   }
   else
      after = mir_const(g->mu, t_time, 0);

   for (int i = 0, offset = 0; i < nlvalues;
        offset += lvalues[i].size, i++) {
      const bool is_signal = mir_is_signal(g->mu, lvalues[i].obj);

      mir_block_t merge_bb = MIR_NULL_BLOCK;

      int64_t count_const;
      if (mir_get_const(g->mu, lvalues[i].count, &count_const)) {
         if (count_const == 0) {
            mir_comment(g->mu, "Out-of-range assignment");
            continue;
         }
      }
      else {
         mir_block_t guarded_bb = mir_add_block(g->mu);
         merge_bb = mir_add_block(g->mu);

         mir_value_t overlaps = mir_build_cmp(g->mu, MIR_CMP_NEQ,
                                              lvalues[i].count,
                                              mir_const(g->mu, t_offset, 0));
         mir_build_cond(g->mu, overlaps, guarded_bb, merge_bb);

         mir_set_cursor(g->mu, guarded_bb, MIR_APPEND);
      }

      if (!is_signal) {
         vlog_lower_nba_store(g, v, &(lvalues[i]), source, offset, after);

         if (!mir_is_null(merge_bb)) {
            mir_build_jump(g->mu, merge_bb);
            mir_set_cursor(g->mu, merge_bb, MIR_APPEND);
         }
         continue;
      }

      mir_value_t nets = mir_build_array_ref(g->mu, lvalues[i].obj,
                                             lvalues[i].dst_offset);

      if (class == MIR_TYPE_VEC2 || class == MIR_TYPE_VEC4) {
         mir_value_t pos = mir_build_add(g->mu, t_offset,
                                         mir_const(g->mu, t_offset, offset),
                                         lvalues[i].src_offset);
         mir_value_t src = mir_build_array_ref(g->mu, unpacked, pos);
         src = vlog_lvalue_source(g, &(lvalues[i]), src);
         mir_build_sched_deposit(g->mu, nets, lvalues[i].count, src, after);
      }
      else
         mir_build_sched_deposit(g->mu, nets, lvalues[i].count,
                                 unpacked, after);

      if (!mir_is_null(merge_bb)) {
         mir_build_jump(g->mu, merge_bb);
         mir_set_cursor(g->mu, merge_bb, MIR_APPEND);
      }
   }
}

static void vlog_report_warning(vlog_gen_t *g, vlog_node_t v, const char *msg)
{
   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_type_t t_severity = mir_int_type(g->mu, 0, 3);

   mir_value_t severity = mir_const(g->mu, t_severity, SEVERITY_WARNING);
   mir_value_t buf = mir_const_string(g->mu, msg);
   mir_value_t ptr = mir_build_address_of(g->mu, buf);
   mir_value_t len = mir_const(g->mu, t_offset, strlen(msg));
   mir_value_t locus = mir_build_debug_locus(g->mu, vlog_to_object(v));

   mir_build_report(g->mu, ptr, len, severity, locus);
}

static void vlog_lower_unique_checks(vlog_gen_t *g, vlog_node_t v,
                                     mir_value_t *tests, int ntests,
                                     bool has_default)
{
   // Run time violations of unique, unique0, and priority are warnings

   const vlog_flags_t flags = vlog_flags(v);
   const bool is_case = vlog_kind(v) != V_IF;

   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_value_t zero = mir_const(g->mu, t_offset, 0);
   mir_value_t one = mir_const(g->mu, t_offset, 1);

   mir_value_t count = zero;
   for (int i = 0; i < ntests; i++)
      count = mir_build_add(g->mu, t_offset, count,
                            mir_build_select(g->mu, t_offset, tests[i], one,
                                             zero));

   if (flags & (VLOG_F_UNIQUE | VLOG_F_UNIQUE0)) {
      mir_block_t warn_bb = mir_add_block(g->mu);
      mir_block_t cont_bb = mir_add_block(g->mu);
      mir_build_cond(g->mu, mir_build_cmp(g->mu, MIR_CMP_GT, count, one),
                     warn_bb, cont_bb);

      mir_set_cursor(g->mu, warn_bb, MIR_APPEND);
      LOCAL_TEXT_BUF tb = tb_new();
      tb_printf(tb, "%s %s violation: more than one %s matches",
                (flags & VLOG_F_UNIQUE0) ? "unique0" : "unique",
                is_case ? "case" : "if", is_case ? "case item" : "condition");
      vlog_report_warning(g, v, tb_get(tb));
      mir_build_jump(g->mu, cont_bb);

      mir_set_cursor(g->mu, cont_bb, MIR_APPEND);
   }

   if ((flags & (VLOG_F_UNIQUE | VLOG_F_PRIORITY)) && !has_default) {
      mir_block_t warn_bb = mir_add_block(g->mu);
      mir_block_t cont_bb = mir_add_block(g->mu);
      mir_build_cond(g->mu, mir_build_cmp(g->mu, MIR_CMP_EQ, count, zero),
                     warn_bb, cont_bb);

      mir_set_cursor(g->mu, warn_bb, MIR_APPEND);
      LOCAL_TEXT_BUF tb = tb_new();
      tb_printf(tb, "%s %s violation: no %s matches and there is no %s",
                (flags & VLOG_F_PRIORITY) ? "priority" : "unique",
                is_case ? "case" : "if", is_case ? "case item" : "condition",
                is_case ? "default" : "else");
      vlog_report_warning(g, v, tb_get(tb));
      mir_build_jump(g->mu, cont_bb);

      mir_set_cursor(g->mu, cont_bb, MIR_APPEND);
   }
}

static void vlog_lower_if(vlog_gen_t *g, vlog_node_t v, gen_stack_t *gs)
{
   mir_block_t exit_bb = MIR_NULL_BLOCK;

   const int nconds = vlog_conds(v);

   // All the conditions are evaluated first to check for violations
   mir_value_t *tests LOCAL = xmalloc_array(nconds, sizeof(mir_value_t));
   const bool checked = (vlog_flags(v) & (VLOG_F_UNIQUE | VLOG_F_UNIQUE0
                                          | VLOG_F_PRIORITY)) != 0;
   if (checked) {
      int ntests = 0;
      for (int i = 0; i < nconds; i++) {
         vlog_node_t c = vlog_cond(v, i);
         if (vlog_has_value(c))
            tests[ntests++] = vlog_lower_test(
               g, vlog_lower_rvalue(g, vlog_value(c)));
      }

      vlog_lower_unique_checks(g, v, tests, ntests, ntests < nconds);
   }

   for (int i = 0, ntest = 0; i < nconds; i++) {
      vlog_node_t c = vlog_cond(v, i);
      mir_block_t next_bb = MIR_NULL_BLOCK;

      if (vlog_has_value(c)) {
         mir_value_t cmp = tests[ntest];
         if (!checked) {
            mir_value_t value = vlog_lower_rvalue(g, vlog_value(c));
            cmp = vlog_lower_test(g, value);
         }
         ntest++;

         mir_block_t btrue = mir_add_block(g->mu);

         if (i == nconds - 1) {
            if (mir_is_null(exit_bb))
               exit_bb = mir_add_block(g->mu);
            next_bb = exit_bb;
         }
         else
            next_bb = mir_add_block(g->mu);

         mir_build_cond(g->mu, cmp, btrue, next_bb);

         mir_set_cursor(g->mu, btrue, MIR_APPEND);
      }

      vlog_lower_stmts(g, c, gs);

      if (!mir_block_finished(g->mu, MIR_NULL_BLOCK)) {
         if (mir_is_null(exit_bb))
            exit_bb = mir_add_block(g->mu);
         mir_build_jump(g->mu, exit_bb);
      }

      if (mir_is_null(next_bb))
         break;

      mir_set_cursor(g->mu, next_bb, MIR_APPEND);
   }

   if (!mir_is_null(exit_bb))
      mir_set_cursor(g->mu, exit_bb, MIR_APPEND);
}

static gen_stack_t vlog_push_loop(gen_stack_t *gs, loop_info_t *loop);

static void vlog_lower_do_while(vlog_gen_t *g, vlog_node_t v,
                                gen_stack_t *gs)
{
   mir_block_t body_bb = mir_add_block(g->mu);
   mir_block_t test_bb = mir_add_block(g->mu);
   mir_block_t cont_bb = mir_add_block(g->mu);

   mir_build_jump(g->mu, body_bb);

   mir_set_cursor(g->mu, body_bb, MIR_APPEND);

   loop_info_t loop = { .test_bb = test_bb, .exit_bb = cont_bb };
   gen_stack_t this = vlog_push_loop(gs, &loop);

   vlog_lower_stmts(g, v, &this);

   if (!mir_block_finished(g->mu, MIR_NULL_BLOCK))
      mir_build_jump(g->mu, test_bb);

   mir_set_cursor(g->mu, test_bb, MIR_APPEND);

   mir_value_t rvalue = vlog_lower_rvalue(g, vlog_value(v));
   mir_build_cond(g->mu, vlog_lower_test(g, rvalue), body_bb, cont_bb);

   mir_set_cursor(g->mu, cont_bb, MIR_APPEND);
}

static gen_stack_t vlog_push_loop(gen_stack_t *gs, loop_info_t *loop)
{
   gen_stack_t this = {
      .loop   = loop,
      .cscope = gs->cscope,
      .up     = gs,
   };
   return this;
}

static void vlog_lower_forever(vlog_gen_t *g, vlog_node_t v, gen_stack_t *gs)
{
   mir_block_t body_bb = mir_add_block(g->mu);
   mir_block_t exit_bb = mir_add_block(g->mu);
   mir_build_jump(g->mu, body_bb);

   mir_set_cursor(g->mu, body_bb, MIR_APPEND);

   loop_info_t loop = { .test_bb = body_bb, .exit_bb = exit_bb };
   gen_stack_t this = vlog_push_loop(gs, &loop);

   vlog_lower_stmts(g, v, &this);

   if (!mir_block_finished(g->mu, MIR_NULL_BLOCK))
      mir_build_jump(g->mu, body_bb);

   mir_set_cursor(g->mu, exit_bb, MIR_APPEND);
}

static void vlog_lower_repeat(vlog_gen_t *g, vlog_node_t v, gen_stack_t *gs)
{
   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_value_t i_var = mir_add_var(g->mu, t_offset, MIR_NULL_STAMP,
                                   ident_uniq("i"), 0);
   mir_value_t zero = mir_const(g->mu, t_offset, 0);
   mir_build_store(g->mu, i_var, zero);

   mir_value_t limit_var = mir_add_var(g->mu, t_offset, MIR_NULL_STAMP,
                                       ident_uniq("limit"), MIR_VAR_CONST);

   mir_value_t rvalue = vlog_lower_rvalue(g, vlog_value(v));
   mir_value_t limit = vlog_lower_cast(g, t_offset, rvalue);
   mir_build_store(g->mu, limit_var, limit);

   mir_value_t enter = mir_build_cmp(g->mu, MIR_CMP_LT, zero, limit);

   mir_block_t body_bb = mir_add_block(g->mu);
   mir_block_t cont_bb = mir_add_block(g->mu);
   mir_build_cond(g->mu, enter, body_bb, cont_bb);

   mir_set_cursor(g->mu, body_bb, MIR_APPEND);

   mir_block_t step_bb = mir_add_block(g->mu);

   loop_info_t loop = { .test_bb = step_bb, .exit_bb = cont_bb };
   gen_stack_t this = vlog_push_loop(gs, &loop);

   vlog_lower_stmts(g, v, &this);

   if (!mir_block_finished(g->mu, MIR_NULL_BLOCK))
      mir_build_jump(g->mu, step_bb);

   mir_set_cursor(g->mu, step_bb, MIR_APPEND);

   mir_value_t i_val = mir_build_load(g->mu, i_var);
   mir_value_t one = mir_const(g->mu, t_offset, 1);
   mir_value_t next = mir_build_add(g->mu, t_offset, i_val, one);
   mir_build_store(g->mu, i_var, next);

   mir_value_t limit_load = mir_build_load(g->mu, limit_var);
   mir_value_t done = mir_build_cmp(g->mu, MIR_CMP_LT, next, limit_load);
   mir_build_cond(g->mu, done, body_bb, cont_bb);

   mir_set_cursor(g->mu, cont_bb, MIR_APPEND);
}

static void vlog_lower_while(vlog_gen_t *g, vlog_node_t v, gen_stack_t *gs)
{
   mir_block_t test_bb = mir_add_block(g->mu);
   mir_block_t body_bb = mir_add_block(g->mu);
   mir_block_t cont_bb = mir_add_block(g->mu);

   mir_build_jump(g->mu, test_bb);

   mir_set_cursor(g->mu, test_bb, MIR_APPEND);

   mir_value_t rvalue = vlog_lower_rvalue(g, vlog_value(v));
   mir_value_t test = vlog_lower_test(g, rvalue);
   mir_build_cond(g->mu, test, body_bb, cont_bb);

   mir_set_cursor(g->mu, body_bb, MIR_APPEND);

   loop_info_t loop = { .test_bb = test_bb, .exit_bb = cont_bb };
   gen_stack_t this = vlog_push_loop(gs, &loop);

   vlog_lower_stmts(g, v, &this);

   if (!mir_block_finished(g->mu, MIR_NULL_BLOCK))
      mir_build_jump(g->mu, test_bb);

   mir_set_cursor(g->mu, cont_bb, MIR_APPEND);
}

static void vlog_lower_for_loop(vlog_gen_t *g, vlog_node_t v, gen_stack_t *gs)
{
   mir_comment(g->mu, "Begin for loop");

   vlog_node_t init = vlog_left(v);
   assert(vlog_kind(init) == V_FOR_INIT);

   vlog_lower_locals(g, init);
   vlog_lower_stmts(g, init, gs);

   mir_block_t body_bb = mir_add_block(g->mu);
   mir_block_t step_bb = mir_add_block(g->mu);
   mir_block_t test_bb = mir_add_block(g->mu);
   mir_block_t exit_bb = mir_add_block(g->mu);

   mir_build_jump(g->mu, test_bb);

   mir_set_cursor(g->mu, test_bb, MIR_APPEND);

   mir_comment(g->mu, "For loop test");

   mir_value_t expr = vlog_lower_rvalue(g, vlog_value(v));
   mir_value_t test = vlog_lower_test(g, expr);
   mir_build_cond(g->mu, test, body_bb, exit_bb);

   mir_set_cursor(g->mu, body_bb, MIR_APPEND);

   mir_comment(g->mu, "For loop body");

   loop_info_t loop = { .test_bb = step_bb, .exit_bb = exit_bb };
   gen_stack_t this = vlog_push_loop(gs, &loop);

   vlog_lower_stmts(g, v, &this);

   if (!mir_block_finished(g->mu, MIR_NULL_BLOCK))
      mir_build_jump(g->mu, step_bb);

   mir_set_cursor(g->mu, step_bb, MIR_APPEND);

   mir_comment(g->mu, "For loop step");

   vlog_node_t step = vlog_right(v);
   assert(vlog_kind(step) == V_FOR_STEP);

   const int nstmts = vlog_stmts(step);
   for (int i = 0; i < nstmts; i++)
      vlog_lower_rvalue(g, vlog_stmt(step, i));

   mir_build_jump(g->mu, test_bb);

   mir_set_cursor(g->mu, exit_bb, MIR_APPEND);

   mir_comment(g->mu, "End for loop");
}

static bool vlog_fork_in_block(const fork_up_t *up, ident_t name)
{
   for (int i = 0; i < up->nblocks; i++) {
      if (up->blocks[i] == name)
         return true;
   }

   return false;
}

static bool vlog_lower_disable(vlog_gen_t *g, vlog_node_t v, gen_stack_t *gs)
{
   ident_t name = vlog_ident(v);
   ident_t label = name;

   vlog_node_t task = NULL;
   if (vlog_has_value(v)) {
      vlog_node_t target = vlog_value(v);
      if (!vlog_has_ref(target))
         fatal_at(vlog_loc(v), "unbound hierarchical reference");
      else if (vlog_kind(vlog_ref(target)) == V_TASK_DECL)
         task = vlog_ref(target);
   }
   else if (vlog_has_ref(v))
      task = vlog_ref(v);

   if (task != NULL)
      label = vlog_subr_name(task);

   // The deferred assertions pending in the block or labelled with this
   // name are cancelled (16.4.4)
   mir_type_t t_defer = mir_offset_type(g->mu);
   mir_value_t dargs[] = {
      mir_const(g->mu, t_defer, vlog_label_hash(name))
   };
   mir_value_t dres = mir_build_fcall(g->mu, ident_new("__sv_defer_disable"),
                                      t_defer, MIR_NULL_STAMP, dargs, 1);
   mir_build_consume(g->mu, dres);

   for (gen_stack_t *it = gs; it != NULL; it = it->up) {
      if (it->block != NULL && it->block->name == name) {
         mir_build_jump(g->mu, it->block->exit_bb);
         return true;
      }
   }
}

static void vlog_lower_assert(vlog_gen_t *g, vlog_node_t v, gen_stack_t *gs)
{
   mir_value_t expr = vlog_lower_rvalue(g, vlog_value(v));
   mir_value_t test = vlog_lower_test(g, expr);
   mir_value_t locus = mir_build_debug_locus(g->mu, vlog_to_object(v));

   mir_type_t t_severity = mir_int_type(g->mu, 0, 3);
   mir_value_t severity = mir_const(g->mu, t_severity, SEVERITY_ERROR);

   const int nconds = vlog_conds(v);
   assert(nconds <= 2);

   if (nconds == 0)
      mir_build_assert(g->mu, test, MIR_NULL_VALUE, MIR_NULL_VALUE, severity,
                       locus, MIR_NULL_VALUE, MIR_NULL_VALUE);
   else {
      mir_block_t then_bb = mir_add_block(g->mu);
      mir_block_t else_bb = mir_add_block(g->mu);
      mir_block_t exit_bb = mir_add_block(g->mu);

      mir_build_cond(g->mu, test, then_bb, else_bb);

      mir_set_cursor(g->mu, then_bb, MIR_APPEND);

      vlog_lower_stmts(g, vlog_cond(v, 0), gs);

      if (!mir_block_finished(g->mu, MIR_NULL_BLOCK))
         mir_build_jump(g->mu, exit_bb);

      mir_set_cursor(g->mu, else_bb, MIR_APPEND);

      if (nconds == 2)
         vlog_lower_stmts(g, vlog_cond(v, 1), gs);
      else
         mir_build_assert(g->mu, test, MIR_NULL_VALUE, MIR_NULL_VALUE,
                          severity, locus, MIR_NULL_VALUE, MIR_NULL_VALUE);

      if (!mir_block_finished(g->mu, MIR_NULL_BLOCK))
         mir_build_jump(g->mu, exit_bb);

      mir_set_cursor(g->mu, exit_bb, MIR_APPEND);
   }
}

static mir_value_t vlog_case_match(vlog_gen_t *g, vlog_node_t item,
                                   mir_value_t *exprs, int *nexpr,
                                   mir_type_t type, mir_value_t value,
                                   mir_vec_op_t op, bool is_signed,
                                   bool is_real)
{
   mir_value_t comb = MIR_NULL_VALUE;
   const int nparams = vlog_params(item);
   for (int j = 0; j < nparams; j++) {
      mir_value_t expr = exprs[(*nexpr)++];
      mir_type_t expr_type = mir_get_type(g->mu, expr);
      if (!is_signed && !is_real && mir_is_vector(g->mu, expr)) {
         // No sign extension here
         const int size = mir_get_size(g->mu, expr_type);
         mir_type_t us_type = mir_vec4_type(g->mu, size, false);
         expr = mir_build_cast(g->mu, us_type, expr);
      }

      mir_value_t cast = vlog_lower_cast(g, type, expr);

      mir_value_t test;
      if (mir_is_vector(g->mu, cast)) {
         mir_value_t eq = mir_build_binary(g->mu, op, type, cast, value);
         test = mir_build_test(g->mu, eq);
      }
      else
         test = mir_build_cmp(g->mu, MIR_CMP_EQ, cast, value);

      if (mir_is_null(comb))
         comb = test;
      else
         comb = mir_build_or(g->mu, comb, test);
   }

   return comb;
}

static void vlog_lower_case_object(vlog_gen_t *g, vlog_node_t v,
                                   gen_stack_t *gs, mir_value_t value)
{
   // Case on a string or class handle

   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_type_t type = mir_get_type(g->mu, value);
   const bool is_string = mir_get_class(g->mu, type) == MIR_TYPE_UARRAY;

   mir_block_t exit_bb = mir_add_block(g->mu);
   vlog_node_t default_item = NULL;

   const int nitems = vlog_stmts(v);
   for (int i = 0; i < nitems; i++) {
      vlog_node_t item = vlog_stmt(v, i);

      const int nparams = vlog_params(item);
      if (nparams == 0) {
         default_item = item;
         continue;
      }

      mir_block_t body_bb = mir_add_block(g->mu);
      mir_block_t next_bb = mir_add_block(g->mu);

      for (int j = 0; j < nparams; j++) {
         vlog_node_t p = vlog_param(item, j);

         mir_value_t test;
         if (is_string) {
            mir_value_t args[] = { value, vlog_str_arg(g, p) };
            mir_value_t eq = mir_build_fcall(g->mu, ident_new("__sv_str_eq"),
                                             t_offset, MIR_NULL_STAMP, args, 2);
            mir_build_consume(g->mu, eq);
            test = mir_build_cmp(g->mu, MIR_CMP_NEQ, eq,
                                 mir_const(g->mu, t_offset, 0));
         }
         else {
            mir_value_t expr = vlog_lower_with_context(g, p, type);
            test = mir_build_cmp(g->mu, MIR_CMP_EQ, value,
                                 mir_build_cast(g->mu, type, expr));
         }

         mir_block_t more_bb = j + 1 == nparams ? next_bb : mir_add_block(g->mu);
         mir_build_cond(g->mu, test, body_bb, more_bb);

         mir_set_cursor(g->mu, more_bb, MIR_APPEND);
      }

      mir_set_cursor(g->mu, body_bb, MIR_APPEND);
      vlog_lower_stmts(g, item, gs);

      if (!mir_block_finished(g->mu, MIR_NULL_BLOCK))
         mir_build_jump(g->mu, exit_bb);

      mir_set_cursor(g->mu, next_bb, MIR_APPEND);
   }

   if (default_item != NULL)
      vlog_lower_stmts(g, default_item, gs);

   if (!mir_block_finished(g->mu, MIR_NULL_BLOCK))
      mir_build_jump(g->mu, exit_bb);

   mir_set_cursor(g->mu, exit_bb, MIR_APPEND);
}

static void vlog_lower_case(vlog_gen_t *g, vlog_node_t v, gen_stack_t *gs)
{
   if (vlog_subkind(v) == V_CASE_INSIDE) {
      vlog_lower_case_inside(g, v, gs);
      return;
   }

   mir_comment(g->mu, "Begin case statement");

   mir_value_t value = vlog_lower_rvalue(g, vlog_value(v));
   bool is_real = mir_is(g->mu, value, MIR_TYPE_REAL);

   if (!is_real && !mir_is_vector(g->mu, value)) {
      vlog_lower_case_object(g, v, gs, value);
      return;
   }

   const int nitems = vlog_stmts(v);
   mir_block_t *blocks LOCAL = xmalloc_array(nitems, sizeof(mir_block_t));

   mir_vec_op_t op;
   switch (vlog_subkind(v)) {
   case V_CASE_NORMAL: op = MIR_VEC_CASE_EQ; break;
   case V_CASE_X:      op = MIR_VEC_CASEX_EQ; break;
   case V_CASE_Z:      op = MIR_VEC_CASEZ_EQ; break;
   default:            should_not_reach_here();
   }

   int nexprs = 0, default_item = -1;
   for (int i = 0; i < nitems; i++) {
      vlog_node_t item = vlog_stmt(v, i);
      assert(vlog_kind(item) == V_CASE_ITEM);

      const int nparams = vlog_params(item);
      if (nparams == 0)
         default_item = i;

      nexprs += nparams;
   }

   mir_value_t *exprs LOCAL = xmalloc_array(nexprs, sizeof(mir_value_t));

   // See 1800-2023 section 12.5 for type rules

   mir_type_t value_type = mir_get_type(g->mu, value);

   bool is_signed = is_real ? false : mir_get_signed(g->mu, value_type);
   int width = is_real ? 0 : mir_get_size(g->mu, value_type);

   for (int i = 0, nexpr = 0; i < nitems; i++) {
      vlog_node_t item = vlog_stmt(v, i);
      assert(vlog_kind(item) == V_CASE_ITEM);

      blocks[i] = mir_add_block(g->mu);

      const int nparams = vlog_params(item);
      for (int j = 0; j < nparams; j++) {
         mir_value_t expr = vlog_lower_rvalue(g, vlog_param(item, j));
         exprs[nexpr++] = expr;

         if (mir_is(g->mu, expr, MIR_TYPE_REAL))
            is_real = true;
         else {
            mir_type_t expr_type = mir_get_type(g->mu, expr);
            is_signed &= mir_get_signed(g->mu, expr_type);
            width = MAX(width, mir_get_size(g->mu, expr_type));
         }
      }
   }

   mir_type_t type;
   if (is_real)
      type = mir_real_type(g->mu, -DBL_MAX, DBL_MAX);
   else
      type = mir_vec4_type(g->mu, width, is_signed);

   if (!is_signed && !is_real && mir_get_signed(g->mu, value_type)) {
      // No sign extension here
      const int size = mir_get_size(g->mu, value_type);
      mir_type_t us_type = mir_vec4_type(g->mu, size, false);
      value = mir_build_cast(g->mu, us_type, value);
   }

   value = vlog_lower_cast(g, type, value);

   if (vlog_flags(v) & (VLOG_F_UNIQUE | VLOG_F_UNIQUE0 | VLOG_F_PRIORITY)) {
      mir_value_t *tests LOCAL = xmalloc_array(nitems, sizeof(mir_value_t));
      int ntests = 0;
      for (int i = 0, nexpr = 0; i < nitems; i++) {
         vlog_node_t item = vlog_stmt(v, i);
         if (i == default_item)
            continue;

         mir_value_t comb = vlog_case_match(g, item, exprs, &nexpr, type, value,
                                            op, is_signed, is_real);
         if (!mir_is_null(comb))
            tests[ntests++] = comb;
      }

      vlog_lower_unique_checks(g, v, tests, ntests, default_item != -1);
   }

   for (int i = 0, nexpr = 0; i < nitems; i++) {
      vlog_node_t item = vlog_stmt(v, i);
      assert(vlog_kind(item) == V_CASE_ITEM);

      if (i == default_item)
         continue;

      mir_block_t else_bb = mir_add_block(g->mu);

      mir_value_t comb = vlog_case_match(g, item, exprs, &nexpr, type, value, op,
                                         is_signed, is_real);

      if (mir_is_null(comb))
         mir_build_jump(g->mu, blocks[i]);
      else
         mir_build_cond(g->mu, comb, blocks[i], else_bb);

      mir_set_cursor(g->mu, else_bb, MIR_APPEND);
   }

   mir_block_t exit_bb;
   if (default_item == -1)
      exit_bb = mir_get_cursor(g->mu, NULL);
   else {
      mir_build_jump(g->mu, blocks[default_item]);
      exit_bb = mir_add_block(g->mu);
   }

   for (int i = 0; i < nitems; i++) {
      vlog_node_t item = vlog_stmt(v, i);
      assert(vlog_kind(item) == V_CASE_ITEM);

      mir_set_cursor(g->mu, blocks[i], MIR_APPEND);
      vlog_lower_stmts(g, item, gs);

      if (!mir_block_finished(g->mu, MIR_NULL_BLOCK))
         mir_build_jump(g->mu, exit_bb);
   }

   mir_set_cursor(g->mu, exit_bb, MIR_APPEND);

   mir_comment(g->mu, "End case statement");
}

static void vlog_lower_return(vlog_gen_t *g, vlog_node_t v)
{
   mir_value_t result = MIR_NULL_VALUE;
   if (vlog_has_value(v)) {
      const type_info_t *ti = vlog_type_info(g, vlog_type(vlog_ref(v)));
      vlog_node_t expr = vlog_value(v);
      mir_value_t value = vlog_lower_with_context(
         g, expr, vlog_assign_context(g, ti->type, expr));
      result = vlog_lower_cast(g, ti->type, value);
   }

   if (mir_is_null(g->exit)) {
      if (mir_is_null(result)
          && mir_get_class(g->mu, mir_get_result(g->mu)) == MIR_TYPE_OFFSET)
         result = mir_const(g->mu, mir_offset_type(g->mu), 0);

      mir_build_return(g->mu, result);
   }
   else
      mir_build_jump(g->mu, g->exit);
}

static hset_t *sv_scopes = NULL;
static mir_context_t *sv_context = NULL;

static bool vlog_lower_one_stmt(vlog_gen_t *g, vlog_node_t s, gen_stack_t *gs);

static uint32_t vlog_class_id(vlog_node_t c)
{
   return ident_hash(vlog_subr_name(c)) | 1;
}

static bool vlog_is_string(vlog_node_t v)
{
   switch (vlog_kind(v)) {
   case V_CONCAT:
      for (int i = 0; i < vlog_params(v); i++) {
         if (vlog_is_string(vlog_param(v, i)))
            return true;
      }
      return false;
   case V_COND_EXPR:
      return vlog_is_string(vlog_left(v)) || vlog_is_string(vlog_right(v));
   case V_BIT_SELECT:
      {
         // One character of a string is not a string
         vlog_node_t pt = vlog_get_type(vlog_value(v));
         if (pt != NULL && vlog_kind(pt) == V_DATA_TYPE
             && vlog_subkind(pt) == DT_STRING)
            return false;
      }
      // Fall-through
   case V_REF:
   case V_HIER_REF:
   case V_MEMBER_REF:
   case V_USER_FCALL:
   case V_METHOD_CALL:
   case V_TF_PORT_DECL:
      {
         if (vlog_kind(v) == V_METHOD_CALL
             && vlog_subkind(v) == V_METHOD_ENUM_NAME)
            return true;

         vlog_node_t t = vlog_get_type(v);
         return t != NULL && vlog_kind(t) == V_DATA_TYPE
            && vlog_subkind(t) == DT_STRING;
      }
   case V_SYS_FCALL:
      return vlog_is_sformatf(vlog_ident(v))
         || vlog_ident(v) == ident_new("$typename")
         || vlog_ident(v) == ident_new("$__pfmt");
   case V_CAST:
      if (vlog_subkind(v) == V_CAST_TYPE) {
         // A cast to string is a string (6.24.1)
         vlog_node_t t = vlog_get_type(vlog_type(v));
         return t != NULL && vlog_kind(t) == V_DATA_TYPE
            && vlog_subkind(t) == DT_STRING;
      }
      return false;
   default:
      return false;
   }
}

static mir_value_t vlog_wrap_array(vlog_gen_t *g, mir_value_t data,
                                   mir_value_t count)
{
   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_type_t t_bool = mir_bool_type(g->mu);

   mir_value_t left = mir_const(g->mu, t_offset, 0);
   mir_value_t right =
      mir_build_sub(g->mu, t_offset, count, mir_const(g->mu, t_offset, 1));
   mir_value_t dir = mir_const(g->mu, t_bool, RANGE_TO);

   mir_dim_t dims[1] = { { left, right, dir } };
   return mir_build_wrap(g->mu, data, dims, 1);
}

static mir_value_t vlog_empty_array(vlog_gen_t *g, mir_type_t type)
{
   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_type_t elem = mir_get_elem(g->mu, type);
   mir_value_t null = mir_build_null(g->mu, mir_pointer_type(g->mu, elem));
   return vlog_wrap_array(g, null, mir_const(g->mu, t_offset, 0));
}

static mir_value_t vlog_alloc_array(vlog_gen_t *g, mir_type_t elem,
                                    mir_value_t count)
{
   // Allocating an array of array headers directly would allocate the
   // storage for one access to unconstrained array instead

   if (mir_get_class(g->mu, elem) == MIR_TYPE_UARRAY) {
      mir_type_t t_rec = mir_record_type(g->mu, ident_uniq("cell"), &elem, 1);
      mir_value_t acc = mir_build_new(g->mu, t_rec, MIR_NULL_STAMP, count);
      return mir_build_record_ref(g->mu, mir_build_all(g->mu, acc), 0);
   }

   mir_value_t acc = mir_build_new(g->mu, elem, MIR_NULL_STAMP, count);
   return mir_build_all(g->mu, acc);
}

static bool vlog_needs_copy(vlog_node_t type)
{
   vlog_node_t t = vlog_get_type(type);
   if (t == NULL)
      return false;

   switch (vlog_kind(t)) {
   case V_STRUCT_DECL:
   case V_UNION_DECL:
      return !(vlog_flags(t) & VLOG_F_PACKED);
   case V_DATA_TYPE:
      return vlog_subkind(t) == DT_DYNARRAY || vlog_subkind(t) == DT_QUEUE
         || vlog_subkind(t) == DT_ASSOC;
   default:
      return false;
   }
}

static mir_value_t vlog_copy_value(vlog_gen_t *g, mir_value_t value,
                                   vlog_node_t type)
{
   // Structures, queues, and dynamic arrays have value semantics

   if (!vlog_needs_copy(type))
      return value;

   vlog_node_t t = vlog_get_type(type);
   const type_info_t *ti = vlog_type_info(g, t);

   if (vlog_kind(t) != V_DATA_TYPE) {
      mir_type_t t_rec = mir_get_elem(g->mu, ti->type);
      mir_value_t acc = mir_build_new(g->mu, t_rec, MIR_NULL_STAMP,
                                      MIR_NULL_VALUE);
      mir_value_t dst = mir_build_all(g->mu, acc);
      mir_value_t src = mir_build_all(g->mu, value);

      const int ndecls = vlog_decls(t);
      for (int i = 0; i < ndecls; i++) {
         vlog_node_t d = vlog_decl(t, i);
         vlog_node_t ft = vlog_type(d);

         if (vlog_is_fixed_unpacked(d) && vlog_size(d) > 1) {
            if (vlog_needs_copy(ft))
               fatal_at(vlog_loc(d), "sorry, copying an array of structures "
                        "or queues in a structure is not supported");

            mir_type_t t_offset = mir_offset_type(g->mu);
            mir_value_t zero = mir_const(g->mu, t_offset, 0);
            mir_value_t dp = mir_build_array_ref(
               g->mu, mir_build_record_ref(g->mu, dst, i), zero);
            mir_value_t sp = mir_build_array_ref(
               g->mu, mir_build_record_ref(g->mu, src, i), zero);
            mir_build_copy(g->mu, dp, sp,
                           mir_const(g->mu, t_offset, vlog_size(d)));
            continue;
         }

         mir_value_t fv = mir_build_load(g->mu,
                                         mir_build_record_ref(g->mu, src, i));
         mir_build_store(g->mu, mir_build_record_ref(g->mu, dst, i),
                         vlog_copy_value(g, fv, ft));
      }

      return acc;
   }

   const bool assoc = vlog_subkind(t) == DT_ASSOC;
   const type_info_t *eti = vlog_type_info(g, vlog_type(t));
   mir_type_t t_offset = mir_offset_type(g->mu);

   // An associative array holds pairs of the key and element
   mir_type_t t_pair = assoc ? mir_get_elem(g->mu, ti->type) : eti->type;

   mir_value_t len = mir_build_uarray_len(g->mu, value, 0);
   mir_value_t src = mir_build_unwrap(g->mu, value);

   mir_value_t has = MIR_NULL_VALUE;
   mir_value_t count = len;
   if (assoc) {
      has = vlog_assoc_has_default(g, value);
      count = mir_build_add(g->mu, t_offset, len, vlog_assoc_extra(g, has));
   }

   mir_value_t dst = vlog_alloc_array(g, t_pair, count);

   if (!vlog_needs_copy(vlog_type(t)))
      mir_build_copy(g->mu, dst, src, len);
   else if (assoc) {
      mir_build_copy(g->mu, dst, src, len);

      mir_value_t ivar = mir_add_var(g->mu, t_offset, MIR_NULL_STAMP,
                                     ident_uniq("i"), 0);
      mir_build_store(g->mu, ivar, mir_const(g->mu, t_offset, 0));

      mir_block_t test_bb = mir_add_block(g->mu);
      mir_block_t body_bb = mir_add_block(g->mu);
      mir_block_t done_bb = mir_add_block(g->mu);

      mir_build_jump(g->mu, test_bb);
      mir_set_cursor(g->mu, test_bb, MIR_APPEND);

      mir_value_t i = mir_build_load(g->mu, ivar);
      mir_build_cond(g->mu, mir_build_cmp(g->mu, MIR_CMP_LT, i, len),
                     body_bb, done_bb);

      mir_set_cursor(g->mu, body_bb, MIR_APPEND);

      mir_value_t sp = mir_build_record_ref(
         g->mu, mir_build_array_ref(g->mu, src, i), 1);
      mir_value_t dp = mir_build_record_ref(
         g->mu, mir_build_array_ref(g->mu, dst, i), 1);
      mir_build_store(g->mu, dp,
                      vlog_copy_value(g, mir_build_load(g->mu, sp),
                                      vlog_type(t)));
      mir_build_store(g->mu, ivar,
                      mir_build_add(g->mu, t_offset, i,
                                    mir_const(g->mu, t_offset, 1)));
      mir_build_jump(g->mu, test_bb);

      mir_set_cursor(g->mu, done_bb, MIR_APPEND);
   }
   else {
      mir_value_t ivar = mir_add_var(g->mu, t_offset, MIR_NULL_STAMP,
                                     ident_uniq("i"), 0);
      mir_build_store(g->mu, ivar, mir_const(g->mu, t_offset, 0));

      mir_block_t test_bb = mir_add_block(g->mu);
      mir_block_t body_bb = mir_add_block(g->mu);
      mir_block_t done_bb = mir_add_block(g->mu);

      mir_build_jump(g->mu, test_bb);
      mir_set_cursor(g->mu, test_bb, MIR_APPEND);

      mir_value_t i = mir_build_load(g->mu, ivar);
      mir_build_cond(g->mu, mir_build_cmp(g->mu, MIR_CMP_LT, i, len),
                     body_bb, done_bb);

      mir_set_cursor(g->mu, body_bb, MIR_APPEND);

      mir_value_t ev = mir_build_load(g->mu, mir_build_array_ref(g->mu, src, i));
      mir_build_store(g->mu, mir_build_array_ref(g->mu, dst, i),
                      vlog_copy_value(g, ev, vlog_type(t)));
      mir_build_store(g->mu, ivar,
                      mir_build_add(g->mu, t_offset, i,
                                    mir_const(g->mu, t_offset, 1)));
      mir_build_jump(g->mu, test_bb);

      mir_set_cursor(g->mu, done_bb, MIR_APPEND);
   }

   if (assoc) {
      vlog_assoc_copy_default(g, has, dst, src, len, vlog_type(t));
      return vlog_assoc_wrap(g, dst, len, has);
   }

   return vlog_wrap_array(g, dst, len);
}

static void vlog_init_elements(vlog_gen_t *g, mir_value_t data,
                               mir_value_t from, mir_value_t to,
                               const type_info_t *eti)
{
   mir_type_t t_offset = mir_offset_type(g->mu);

   mir_value_t ivar = mir_add_var(g->mu, t_offset, MIR_NULL_STAMP,
                                  ident_uniq("i"), 0);
   mir_build_store(g->mu, ivar, from);

   mir_block_t test_bb = mir_add_block(g->mu);
   mir_block_t body_bb = mir_add_block(g->mu);
   mir_block_t done_bb = mir_add_block(g->mu);

   mir_build_jump(g->mu, test_bb);
   mir_set_cursor(g->mu, test_bb, MIR_APPEND);

   mir_value_t i = mir_build_load(g->mu, ivar);
   mir_build_cond(g->mu, mir_build_cmp(g->mu, MIR_CMP_LT, i, to),
                  body_bb, done_bb);

   mir_set_cursor(g->mu, body_bb, MIR_APPEND);

   mir_build_store(g->mu, mir_build_array_ref(g->mu, data, i),
                   vlog_lower_default_value(g, eti));
   mir_build_store(g->mu, ivar,
                   mir_build_add(g->mu, t_offset, i,
                                 mir_const(g->mu, t_offset, 1)));
   mir_build_jump(g->mu, test_bb);

   mir_set_cursor(g->mu, done_bb, MIR_APPEND);
}

static mir_value_t vlog_string_const(vlog_gen_t *g, number_t n)
{
   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_type_t t_char = mir_char_type(g->mu);

   // A string cannot hold a null character
   const int width = number_width(n) / 8;
   int nbytes = 0;
   for (int i = 0; i < width; i++) {
      if (number_byte(n, width - 1 - i) != 0)
         nbytes++;
   }

   mir_value_t count = mir_const(g->mu, t_offset, nbytes);
   mir_value_t data = vlog_alloc_array(g, t_char, count);

   for (int i = 0, pos = 0; i < width; i++) {
      const uint8_t ch = number_byte(n, width - 1 - i);
      if (ch == 0)
         continue;

      mir_value_t idx = mir_const(g->mu, t_offset, pos++);
      mir_build_store(g->mu, mir_build_array_ref(g->mu, data, idx),
                      mir_const(g->mu, t_char, ch));
   }

   return vlog_wrap_array(g, data, count);
}

static mir_value_t vlog_vector_bytes(vlog_gen_t *g, mir_value_t value,
                                     int nbytes)
{
   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_type_t t_char = mir_char_type(g->mu);

   mir_value_t count = mir_const(g->mu, t_offset, nbytes);
   mir_value_t data = vlog_alloc_array(g, t_char, count);
   mir_type_t t_int = mir_int_type(g->mu, INT64_MIN, INT64_MAX);
   mir_value_t ival = vlog_lower_cast(g, t_int, value);
   for (int j = 0; j < nbytes; j++) {
      mir_value_t shift = mir_const(g->mu, t_int,
                                    INT64_C(1) << ((nbytes - 1 - j) * 8));
      mir_value_t b = mir_build_rem(g->mu, t_int,
                                    mir_build_div(g->mu, t_int, ival, shift),
                                    mir_const(g->mu, t_int, 256));
      mir_value_t idx = mir_const(g->mu, t_offset, j);
      mir_build_store(g->mu, mir_build_array_ref(g->mu, data, idx),
                      mir_build_cast(g->mu, t_char, b));
   }

   return vlog_wrap_array(g, data, count);
}

static mir_value_t vlog_drop_nuls(vlog_gen_t *g, mir_value_t data,
                                  mir_value_t total)
{
   // A string cannot hold a null character

   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_type_t t_char = mir_char_type(g->mu);

   mir_value_t zero = mir_const(g->mu, t_offset, 0);
   mir_value_t wvar = mir_add_var(g->mu, t_offset, MIR_NULL_STAMP,
                                  ident_uniq("w"), MIR_VAR_TEMP);
   mir_build_store(g->mu, wvar, zero);

   vlog_loop_t c;
   vlog_loop_begin(g, &c, zero, total);

   mir_value_t ch =
      mir_build_load(g->mu, mir_build_array_ref(g->mu, data, c.i));
   mir_value_t nonzero = mir_build_cmp(g->mu, MIR_CMP_NEQ, ch,
                                       mir_const(g->mu, t_char, 0));

   mir_block_t keep_bb = mir_add_block(g->mu);
   mir_block_t skip_bb = mir_add_block(g->mu);
   mir_build_cond(g->mu, nonzero, keep_bb, skip_bb);

   mir_set_cursor(g->mu, keep_bb, MIR_APPEND);
   mir_value_t w = mir_build_load(g->mu, wvar);
   mir_build_store(g->mu, mir_build_array_ref(g->mu, data, w), ch);
   mir_build_store(g->mu, wvar,
                   mir_build_add(g->mu, t_offset, w,
                                 mir_const(g->mu, t_offset, 1)));
   mir_build_jump(g->mu, skip_bb);

   mir_set_cursor(g->mu, skip_bb, MIR_APPEND);
   vlog_loop_end(g, &c);

   return vlog_wrap_array(g, data, mir_build_load(g->mu, wvar));
}

static mir_value_t vlog_string_concat(vlog_gen_t *g, vlog_node_t v)
{
   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_type_t t_char = mir_char_type(g->mu);
   mir_type_t t_str = mir_uarray_type(g->mu, 1, t_char);

   const int nparams = vlog_params(v);
   mir_value_t *parts LOCAL = xmalloc_array(nparams, sizeof(mir_value_t));

   mir_value_t total = mir_const(g->mu, t_offset, 0);
   bool has_vector = false;
   for (int i = 0; i < nparams; i++) {
      vlog_node_t p = vlog_param(v, i);
      if (vlog_kind(p) == V_STRING || vlog_is_string(p)
          || (vlog_kind(p) == V_COND_EXPR
              && (vlog_kind(vlog_left(p)) == V_STRING
                  || vlog_kind(vlog_right(p)) == V_STRING)))
         parts[i] = vlog_lower_with_context(g, p, t_str);
      else {
         mir_value_t value = vlog_lower_rvalue(g, p);
         if (mir_is(g->mu, value, MIR_TYPE_UARRAY)) {
            parts[i] = value;
            total = mir_build_add(g->mu, t_offset, total,
                                  mir_build_uarray_len(g->mu, value, 0));
            continue;
         }

         // Vector operands contribute one character per byte
         has_vector = true;
         parts[i] = vlog_vector_bytes(g, value, vlog_width(p) / 8);
      }

      total = mir_build_add(g->mu, t_offset, total,
                            mir_build_uarray_len(g->mu, parts[i], 0));
   }

   mir_value_t once = total;
   mir_value_t repeat = mir_const(g->mu, t_offset, 1);
   if (vlog_has_value(v)) {
      repeat = vlog_lower_cast(g, t_offset,
                               vlog_lower_rvalue(g, vlog_value(v)));
      total = mir_build_mul(g->mu, t_offset, total, repeat);
   }

   mir_value_t data = vlog_alloc_array(g, t_char, total);

   vlog_loop_t l;
   vlog_loop_begin(g, &l, mir_const(g->mu, t_offset, 0), repeat);

   mir_value_t pos = mir_build_mul(g->mu, t_offset, l.i, once);
   for (int i = 0; i < nparams; i++) {
      mir_value_t len = mir_build_uarray_len(g->mu, parts[i], 0);
      mir_build_copy(g->mu, mir_build_array_ref(g->mu, data, pos),
                     mir_build_unwrap(g->mu, parts[i]), len);
      pos = mir_build_add(g->mu, t_offset, pos, len);
   }

   vlog_loop_end(g, &l);

   if (has_vector)
      return vlog_drop_nuls(g, data, total);

   return vlog_wrap_array(g, data, total);
}

static mir_value_t vlog_lower_queue_slice(vlog_gen_t *g, vlog_node_t v)
{
   // Slice of a queue is a new queue: indices are clamped to the bounds
   // and a slice with the first index above the second is empty

   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_value_t zero = mir_const(g->mu, t_offset, 0);
   mir_value_t one = mir_const(g->mu, t_offset, 1);

   vlog_node_t value = vlog_value(v);
   vlog_node_t type = vlog_get_type(value);
   const type_info_t *eti = vlog_type_info(g, vlog_type(type));

   mir_value_t arr = vlog_lower_rvalue(g, value);
   mir_value_t len = mir_build_uarray_len(g->mu, arr, 0);
   mir_value_t last = mir_build_sub(g->mu, t_offset, len, one);

   vlog_node_t sized = vlog_sized_dimension(type);

   const mir_value_t saved_index = g->last_index;
   g->last_index = last;

   mir_value_t left = vlog_lower_rvalue(g, vlog_left(v));
   mir_value_t right = vlog_lower_rvalue(g, vlog_right(v));

   g->last_index = saved_index;

   if (sized != NULL) {
      left = vlog_lower_array_off(g, sized, left);
      right = vlog_lower_array_off(g, sized, right);
   }
   else {
      left = vlog_lower_cast(g, t_offset, left);
      right = vlog_lower_cast(g, t_offset, right);
   }

   if (vlog_subkind(type) == DT_DYNARRAY) {
      // The slice has as many elements as the range and those outside the
      // array have the default value
      mir_value_t empty = mir_build_cmp(g->mu, MIR_CMP_GT, left, right);
      mir_value_t span = mir_build_add(g->mu, t_offset,
                                       mir_build_sub(g->mu, t_offset,
                                                     right, left),
                                       one);
      mir_value_t count = mir_build_select(g->mu, t_offset, empty, zero, span);

      mir_value_t data = vlog_alloc_array(g, eti->type, count);
      vlog_init_elements(g, data, zero, count, eti);

      mir_value_t lo = vlog_lower_offset_max(g, left, zero);
      mir_value_t hi = vlog_lower_offset_min(g, right, last);
      mir_value_t raw = mir_build_add(g->mu, t_offset,
                                      mir_build_sub(g->mu, t_offset, hi, lo),
                                      one);
      mir_value_t ncopy = vlog_lower_offset_max(g, raw, zero);
      ncopy = mir_build_select(g->mu, t_offset, empty, zero, ncopy);

      mir_value_t dst = mir_build_array_ref(
         g->mu, data, mir_build_sub(g->mu, t_offset, lo, left));
      mir_value_t src = mir_build_array_ref(g->mu, mir_build_unwrap(g->mu, arr),
                                            lo);
      mir_build_copy(g->mu, dst, src, ncopy);

      return vlog_wrap_array(g, data, count);
   }

   mir_value_t lo = vlog_lower_offset_max(g, left, zero);
   mir_value_t hi = vlog_lower_offset_min(g, right, last);

   mir_value_t empty = mir_build_cmp(g->mu, MIR_CMP_GT, lo, hi);
   mir_value_t span = mir_build_add(g->mu, t_offset,
                                    mir_build_sub(g->mu, t_offset, hi, lo),
                                    one);
   mir_value_t count = mir_build_select(g->mu, t_offset, empty, zero, span);

   mir_value_t data = vlog_alloc_array(g, eti->type, count);
   mir_value_t src = mir_build_array_ref(g->mu, mir_build_unwrap(g->mu, arr),
                                         lo);
   mir_build_copy(g->mu, data, src, count);

   return vlog_wrap_array(g, data, count);
}

static mir_value_t vlog_lower_dyn_element(vlog_gen_t *g, vlog_node_t v,
                                          int nparams, const type_info_t **out)
{
   // Pointer to an element of a dynamic array or queue

   const bool for_write = g->for_write;
   g->for_write = false;

   mir_type_t t_offset = mir_offset_type(g->mu);

   vlog_node_t value = vlog_value(v);
   mir_value_t slot = vlog_lower_select(g, value).obj;

   vlog_node_t type = vlog_get_type(value);

   // The first indices select an element of an unpacked array of queues
   // or dynamic arrays
   int nfixed = 0;
   if (vlog_kind(value) == V_REF && vlog_has_ref(value)) {
      vlog_node_t decl = vlog_ref(value);
      switch (vlog_kind(decl)) {
      case V_VAR_DECL:
      case V_LOCAL_DECL:
         nfixed = MIN(vlog_ranges(decl), nparams);
         break;
      default:
         break;
      }
   }

   if (nfixed > 1)
      fatal_at(vlog_loc(v), "sorry, only one unpacked dimension is supported "
               "for an array of queues or dynamic arrays");

   for (int i = 0; i < nfixed; i++) {
      vlog_node_t dim = vlog_range(vlog_ref(value), i);
      mir_value_t index = vlog_lower_rvalue(g, vlog_param(v, i));
      mir_value_t off = vlog_lower_array_off(g, dim, index);
      slot = mir_build_array_ref(g->mu, slot, off);
      *out = vlog_type_info(g, type);
   }

   for (int i = nfixed; i < nparams; i++) {
      if (vlog_kind(type) != V_DATA_TYPE
          || (vlog_subkind(type) != DT_DYNARRAY
              && vlog_subkind(type) != DT_QUEUE
              && vlog_subkind(type) != DT_ASSOC))
         fatal_at(vlog_loc(v), "sorry, selecting from an element of a "
                  "dynamic array, queue or associative array is not "
                  "supported");

      vlog_node_t etype = vlog_type(type);
      const type_info_t *eti = vlog_type_info(g, etype);

      if (vlog_subkind(type) == DT_ASSOC) {
         // Writing to any element creates the entries for the indices
         slot = vlog_lower_assoc_checked(g, slot, type, vlog_param(v, i),
                                         for_write, eti);
         type = vlog_get_type(etype);
         *out = eti;
         continue;
      }

      mir_value_t arr = mir_build_load(g->mu, slot);
      mir_value_t len = mir_build_uarray_len(g->mu, arr, 0);

      vlog_node_t sized = vlog_sized_dimension(type);

      const mir_value_t saved_index = g->last_index;
      if (sized != NULL) {
         int64_t left, right;
         vlog_bounds(sized, &left, &right);
         g->last_index = mir_const(g->mu, t_offset, right);
      }
      else
         g->last_index = mir_build_sub(g->mu, t_offset, len,
                                       mir_const(g->mu, t_offset, 1));

      mir_value_t index = vlog_lower_rvalue(g, vlog_param(v, i));
      mir_value_t off = sized != NULL
         ? vlog_lower_array_off(g, sized, index)
         : vlog_lower_cast(g, t_offset, index);

      // An index with unknown bits selects no element (7.4.6)
      mir_value_t known = mir_const(g->mu, mir_bool_type(g->mu), 1);
      if (mir_is(g->mu, index, MIR_TYPE_VEC4)) {
         known = mir_build_defined(g->mu, index);
         off = mir_build_select(g->mu, t_offset, known, off,
                                mir_const(g->mu, t_offset, -1));
      }

      g->last_index = saved_index;

      if (for_write && vlog_subkind(type) == DT_QUEUE && i == nparams - 1) {
         // Writing just past the end of a queue extends it
         mir_block_t grow_bb = mir_add_block(g->mu);
         mir_block_t cont_bb = mir_add_block(g->mu);

         mir_build_cond(g->mu, mir_build_cmp(g->mu, MIR_CMP_EQ, off, len),
                        grow_bb, cont_bb);

         mir_set_cursor(g->mu, grow_bb, MIR_APPEND);

         mir_value_t newlen = mir_build_add(g->mu, t_offset, len,
                                            mir_const(g->mu, t_offset, 1));
         mir_value_t data = vlog_alloc_array(g, eti->type, newlen);
         mir_build_copy(g->mu, data, mir_build_unwrap(g->mu, arr), len);
         vlog_init_elements(g, data, len, newlen, eti);
         mir_build_store(g->mu, slot, vlog_wrap_array(g, data, newlen));
         mir_build_jump(g->mu, cont_bb);

         mir_set_cursor(g->mu, cont_bb, MIR_APPEND);

         arr = mir_build_load(g->mu, slot);
         len = mir_build_uarray_len(g->mu, arr, 0);
      }

      // An access outside the array reads the default value of the element
      // type and any write to it is discarded
      mir_type_t t_ptr = mir_pointer_type(g->mu, eti->type);
      mir_value_t ptrvar = mir_add_var(g->mu, t_ptr, MIR_NULL_STAMP,
                                       ident_uniq("elem"), MIR_VAR_TEMP);
      mir_value_t dummy = mir_add_var(g->mu, eti->type, eti->stamp,
                                      ident_uniq("outside"), 0);

      mir_block_t in_bb = mir_add_block(g->mu);
      mir_block_t out_bb = mir_add_block(g->mu);
      mir_block_t join_bb = mir_add_block(g->mu);

      mir_value_t zero = mir_const(g->mu, t_offset, 0);
      mir_value_t in_range =
         mir_build_and(g->mu, mir_build_cmp(g->mu, MIR_CMP_GEQ, off, zero),
                       mir_build_cmp(g->mu, MIR_CMP_LT, off, len));
      mir_build_cond(g->mu, in_range, in_bb, out_bb);

      mir_set_cursor(g->mu, in_bb, MIR_APPEND);
      mir_build_store(g->mu, ptrvar,
                      mir_build_array_ref(g->mu, mir_build_unwrap(g->mu, arr),
                                          off));
      mir_build_jump(g->mu, join_bb);

      mir_set_cursor(g->mu, out_bb, MIR_APPEND);
      if (for_write && i == nparams - 1) {
         // Writing to an element which does not exist has no effect
         // (7.4.6, 7.10.1)
         mir_value_t args[] = {
            mir_const(g->mu, t_offset, vlog_subkind(type) == DT_QUEUE),
            mir_build_cast(g->mu, t_offset, known), off, len,
         };
         mir_value_t res = mir_build_fcall(
            g->mu, ident_new("__sv_array_badwrite"), t_offset,
            MIR_NULL_STAMP, args, ARRAY_LEN(args));
         mir_build_consume(g->mu, res);
      }
      mir_build_store(g->mu, dummy, vlog_lower_default_value(g, eti));
      mir_build_store(g->mu, ptrvar, dummy);
      mir_build_jump(g->mu, join_bb);

      mir_set_cursor(g->mu, join_bb, MIR_APPEND);
      slot = mir_build_load(g->mu, ptrvar);
      type = vlog_get_type(etype);
      *out = eti;
   }

   return slot;
}

static void vlog_queue_push(vlog_gen_t *g, mir_value_t slot,
                            const type_info_t *eti, mir_value_t value,
                            bool front)
{
   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_value_t one = mir_const(g->mu, t_offset, 1);

   mir_value_t arr = mir_build_load(g->mu, slot);
   mir_value_t len = mir_build_uarray_len(g->mu, arr, 0);
   mir_value_t newlen = mir_build_add(g->mu, t_offset, len, one);
   mir_value_t data = vlog_alloc_array(g, eti->type, newlen);
   mir_value_t src = mir_build_unwrap(g->mu, arr);

   if (front) {
      mir_build_copy(g->mu, mir_build_array_ref(g->mu, data, one), src, len);
      mir_build_store(g->mu, data, value);
   }
   else {
      mir_build_copy(g->mu, data, src, len);
      mir_build_store(g->mu, mir_build_array_ref(g->mu, data, len), value);
   }

   mir_build_store(g->mu, slot, vlog_wrap_array(g, data, newlen));
}

static mir_value_t vlog_queue_pop(vlog_gen_t *g, mir_value_t slot,
                                  const type_info_t *eti, bool front)
{
   // Popping from an empty queue returns the default value

   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_value_t one = mir_const(g->mu, t_offset, 1);
   mir_value_t zero = mir_const(g->mu, t_offset, 0);

   mir_value_t result = mir_add_var(g->mu, eti->type, eti->stamp,
                                    ident_uniq("popped"), 0);

   mir_block_t pop_bb = mir_add_block(g->mu);
   mir_block_t empty_bb = mir_add_block(g->mu);
   mir_block_t join_bb = mir_add_block(g->mu);

   mir_value_t arr = mir_build_load(g->mu, slot);
   mir_value_t len = mir_build_uarray_len(g->mu, arr, 0);
   mir_build_cond(g->mu, mir_build_cmp(g->mu, MIR_CMP_GT, len, zero),
                  pop_bb, empty_bb);

   mir_set_cursor(g->mu, pop_bb, MIR_APPEND);

   mir_value_t newlen = mir_build_sub(g->mu, t_offset, len, one);
   mir_value_t data = mir_build_unwrap(g->mu, arr);

   mir_value_t value, rest;
   if (front) {
      value = mir_build_load(g->mu, data);
      rest = mir_build_array_ref(g->mu, data, one);
   }
   else {
      value = mir_build_load(g->mu, mir_build_array_ref(g->mu, data, newlen));
      rest = data;
   }

   mir_build_store(g->mu, result, value);
   mir_build_store(g->mu, slot, vlog_wrap_array(g, rest, newlen));
   mir_build_jump(g->mu, join_bb);

   mir_set_cursor(g->mu, empty_bb, MIR_APPEND);
   mir_build_store(g->mu, result, vlog_lower_default_value(g, eti));
   mir_build_jump(g->mu, join_bb);

   mir_set_cursor(g->mu, join_bb, MIR_APPEND);
   return mir_build_load(g->mu, result);
}

static mir_value_t vlog_mailbox_has_room(vlog_gen_t *g, mir_value_t rec)
{
   mir_type_t t_cell = mir_int_type(g->mu, INT64_MIN, INT64_MAX);

   mir_value_t q = mir_build_load(g->mu, mir_build_record_ref(g->mu, rec, 0));
   mir_value_t len = mir_build_cast(g->mu, t_cell,
                                    mir_build_uarray_len(g->mu, q, 0));
   mir_value_t bound =
      mir_build_load(g->mu, mir_build_record_ref(g->mu, rec, 2));

   return mir_build_or(g->mu,
                       mir_build_cmp(g->mu, MIR_CMP_LEQ, bound,
                                     mir_const(g->mu, t_cell, 0)),
                       mir_build_cmp(g->mu, MIR_CMP_LT, len, bound));
}

static mir_value_t vlog_str_call(vlog_gen_t *g, const char *name,
                                 mir_type_t type, mir_value_t *args,
                                 int nargs)
{
   mir_value_t result = mir_build_fcall(g->mu, ident_new(name), type,
                                        MIR_NULL_STAMP, args, nargs);
   mir_build_consume(g->mu, result);
   return result;
}

static mir_value_t vlog_str_arg(vlog_gen_t *g, vlog_node_t v)
{
   mir_type_t t_str = mir_uarray_type(g->mu, 1, mir_char_type(g->mu));
   return vlog_lower_with_context(g, v, t_str);
}

static mir_value_t vlog_str_int_arg(vlog_gen_t *g, vlog_node_t v,
                                    bool is_signed)
{
   // Methods take an int argument so wider values are truncated

   mir_type_t t_int64 = mir_int_type(g->mu, INT64_MIN, INT64_MAX);
   mir_type_t t_int = mir_vec2_type(g->mu, 32, is_signed);
   mir_value_t value = vlog_lower_rvalue(g, v);
   return vlog_lower_cast(g, t_int64, vlog_lower_cast(g, t_int, value));
}

static mir_value_t vlog_str_offset_arg(vlog_gen_t *g, vlog_node_t v)
{
   mir_type_t t_offset = mir_offset_type(g->mu);
   return vlog_lower_cast(g, t_offset, vlog_lower_rvalue(g, v));
}

static mir_value_t vlog_str_copy(vlog_gen_t *g, mir_value_t str)
{
   mir_type_t t_char = mir_char_type(g->mu);
   mir_value_t len = mir_build_uarray_len(g->mu, str, 0);
   mir_value_t data = vlog_alloc_array(g, t_char, len);
   mir_build_copy(g->mu, data, mir_build_unwrap(g->mu, str), len);
   return data;
}

static mir_value_t vlog_assoc_key_cast(vlog_gen_t *g, vlog_node_t aty,
                                       mir_value_t value)
{
   // The index is converted to the type of the index of the array

   mir_type_t t_offset = mir_offset_type(g->mu);

   vlog_node_t kt = vlog_assoc_key(aty);
   if (vlog_assoc_key_is_struct(kt))
      return value;
   else if (kt != NULL && vlog_kind(kt) == V_CLASS_DECL)
      return mir_build_cast(g->mu, t_offset,
                            vlog_lower_cast(g, vlog_type_info(g, kt)->type,
                                            value));
   else if (vlog_assoc_key_is_wide(kt)) {
      value = vlog_lower_cast(g, vlog_type_info(g, kt)->type, value);
      return vlog_lower_cast(g, mir_vec2_type(g->mu, vlog_size(kt), false),
                             value);
   }
   else if (kt != NULL)
      value = vlog_lower_cast(g, vlog_type_info(g, kt)->type, value);

   return vlog_lower_cast(g, t_offset, value);
}

static mir_value_t vlog_assoc_key_value(vlog_gen_t *g, vlog_node_t aty,
                                        vlog_node_t expr)
{
   bool is_string;
   (void)vlog_assoc_key_type(g, aty, &is_string);

   if (is_string)
      return vlog_str_arg(g, expr);

   vlog_node_t kt = vlog_assoc_key(aty);
   if (vlog_assoc_key_is_struct(kt))
      return vlog_copy_value(g, vlog_lower_rvalue(g, expr), kt);

   return vlog_assoc_key_cast(g, aty, vlog_lower_rvalue(g, expr));
}

static bool vlog_assoc_key_is_string(vlog_node_t kt)
{
   return kt != NULL && vlog_kind(kt) == V_DATA_TYPE
      && vlog_subkind(kt) == DT_STRING;
}

static bool vlog_assoc_key_is_struct(vlog_node_t kt)
{
   return kt != NULL && vlog_kind(kt) == V_STRUCT_DECL
      && !(vlog_flags(kt) & VLOG_F_PACKED);
}

static bool vlog_assoc_key_is_wide(vlog_node_t kt)
{
   if (kt == NULL || vlog_assoc_key_is_struct(kt))
      return false;

   switch (vlog_kind(kt)) {
   case V_CLASS_DECL:
      return false;
   case V_DATA_TYPE:
      if (vlog_subkind(kt) == DT_STRING)
         return false;
      break;
   default:
      break;
   }

   return vlog_size(kt) > 64;
}

static bool vlog_assoc_key_is_integral(vlog_node_t ft)
{
   if (ft == NULL)
      return false;

   switch (vlog_kind(ft)) {
   case V_ENUM_DECL:
      return true;
   case V_STRUCT_DECL:
      return !!(vlog_flags(ft) & VLOG_F_PACKED) && vlog_size(ft) <= 64;
   case V_DATA_TYPE:
      switch (vlog_subkind(ft)) {
      case DT_BIT:
      case DT_LOGIC:
      case DT_IMPLICIT:
      case DT_BYTE:
      case DT_SHORTINT:
      case DT_INT:
      case DT_LONGINT:
      case DT_INTEGER:
      case DT_TIME:
         return vlog_size(ft) <= 64;
      default:
         return false;
      }
   default:
      return false;
   }
}

static mir_value_t vlog_struct_key_compare(vlog_gen_t *g, vlog_node_t st,
                                           mir_value_t a, mir_value_t b,
                                           mir_value_t *less)
{
   // Members are compared in order, returning whether all are equal

   mir_type_t t_bool = mir_bool_type(g->mu);
   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_value_t zero = mir_const(g->mu, t_offset, 0);

   mir_value_t rec_a = mir_build_all(g->mu, a);
   mir_value_t rec_b = mir_build_all(g->mu, b);

   mir_value_t lt = mir_const(g->mu, t_bool, 0);
   mir_value_t eq = mir_const(g->mu, t_bool, 1);

   const int ndecls = vlog_decls(st);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t f = vlog_decl(st, i);
      if (vlog_ranges(f) > 0)
         fatal_at(vlog_loc(f), "sorry, a structure with an unpacked array "
                  "member cannot be the index of an associative array");

      vlog_node_t ft = vlog_get_type(vlog_type(f));
      mir_value_t fa = mir_build_load(
         g->mu, mir_build_record_ref(g->mu, rec_a, i));
      mir_value_t fb = mir_build_load(
         g->mu, mir_build_record_ref(g->mu, rec_b, i));

      mir_value_t flt, feq;
      if (vlog_assoc_key_is_struct(ft))
         feq = vlog_struct_key_compare(g, ft, fa, fb, &flt);
      else if (vlog_assoc_key_is_string(ft)) {
         mir_value_t args[] = { fa, fb, zero };
         mir_value_t cmp = vlog_str_call(g, "__sv_str_cmp", t_offset, args, 3);
         flt = mir_build_cmp(g->mu, MIR_CMP_LT, cmp, zero);
         feq = mir_build_cmp(g->mu, MIR_CMP_EQ, cmp, zero);
      }
      else if (ft != NULL && vlog_kind(ft) == V_DATA_TYPE
               && (vlog_subkind(ft) == DT_REAL
                   || vlog_subkind(ft) == DT_SHORTREAL
                   || vlog_subkind(ft) == DT_REALTIME)) {
         flt = mir_build_cmp(g->mu, MIR_CMP_LT, fa, fb);
         feq = mir_build_cmp(g->mu, MIR_CMP_EQ, fa, fb);
      }
      else if (vlog_assoc_key_is_integral(ft)) {
         mir_value_t ia = vlog_lower_cast(g, t_offset, fa);
         mir_value_t ib = vlog_lower_cast(g, t_offset, fb);
         flt = mir_build_cmp(g->mu, MIR_CMP_LT, ia, ib);
         feq = mir_build_cmp(g->mu, MIR_CMP_EQ, ia, ib);
      }
      else
         fatal_at(vlog_loc(f), "sorry, a structure with this member type "
                  "cannot be the index of an associative array");

      lt = mir_build_or(g->mu, lt, mir_build_and(g->mu, eq, flt));
      eq = mir_build_and(g->mu, eq, feq);
   }

   *less = lt;
   return eq;
}

static mir_value_t vlog_assoc_key_less(vlog_gen_t *g, vlog_node_t kt,
                                       mir_value_t a, mir_value_t b)
{
   if (vlog_assoc_key_is_struct(kt)) {
      mir_value_t less;
      (void)vlog_struct_key_compare(g, kt, a, b, &less);
      return less;
   }
   else if (vlog_assoc_key_is_string(kt)) {
      mir_type_t t_offset = mir_offset_type(g->mu);
      mir_value_t zero = mir_const(g->mu, t_offset, 0);
      mir_value_t args[] = { a, b, zero };
      mir_value_t cmp = vlog_str_call(g, "__sv_str_cmp", t_offset, args, 3);
      return mir_build_cmp(g->mu, MIR_CMP_LT, cmp, zero);
   }
   else if (vlog_assoc_key_is_wide(kt))
      return mir_build_test(
         g->mu, mir_build_binary(g->mu, MIR_VEC_LT,
                                 mir_get_type(g->mu, a), a, b));
   else
      return mir_build_cmp(g->mu, MIR_CMP_LT, a, b);
}

static mir_value_t vlog_assoc_key_equal(vlog_gen_t *g, vlog_node_t kt,
                                        mir_value_t a, mir_value_t b)
{
   if (vlog_assoc_key_is_struct(kt)) {
      mir_value_t less;
      return vlog_struct_key_compare(g, kt, a, b, &less);
   }
   else if (vlog_assoc_key_is_string(kt)) {
      mir_type_t t_offset = mir_offset_type(g->mu);
      mir_value_t zero = mir_const(g->mu, t_offset, 0);
      mir_value_t args[] = { a, b, zero };
      mir_value_t cmp = vlog_str_call(g, "__sv_str_cmp", t_offset, args, 3);
      return mir_build_cmp(g->mu, MIR_CMP_EQ, cmp, zero);
   }
   else if (vlog_assoc_key_is_wide(kt))
      return mir_build_test(
         g->mu, mir_build_binary(g->mu, MIR_VEC_LOG_EQ,
                                 mir_get_type(g->mu, a), a, b));
   else
      return mir_build_cmp(g->mu, MIR_CMP_EQ, a, b);
}

static void vlog_assoc_search(vlog_gen_t *g, mir_value_t arr, vlog_node_t kt,
                              mir_value_t key, mir_value_t *pos,
                              mir_value_t *found)
{
   // Binary search for the first entry which is not less than the key

   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_type_t t_bool = mir_bool_type(g->mu);

   mir_value_t zero = mir_const(g->mu, t_offset, 0);
   mir_value_t one = mir_const(g->mu, t_offset, 1);
   mir_value_t two = mir_const(g->mu, t_offset, 2);

   mir_value_t len = mir_build_uarray_len(g->mu, arr, 0);
   mir_value_t data = mir_build_unwrap(g->mu, arr);

   mir_value_t lo = mir_add_var(g->mu, t_offset, MIR_NULL_STAMP,
                                ident_uniq("lo"), MIR_VAR_TEMP);
   mir_value_t hi = mir_add_var(g->mu, t_offset, MIR_NULL_STAMP,
                                ident_uniq("hi"), MIR_VAR_TEMP);
   mir_value_t hit = mir_add_var(g->mu, t_bool, MIR_NULL_STAMP,
                                 ident_uniq("hit"), MIR_VAR_TEMP);

   mir_build_store(g->mu, lo, zero);
   mir_build_store(g->mu, hi, len);
   mir_build_store(g->mu, hit, mir_const(g->mu, t_bool, 0));

   mir_block_t test_bb = mir_add_block(g->mu);
   mir_block_t body_bb = mir_add_block(g->mu);
   mir_block_t up_bb = mir_add_block(g->mu);
   mir_block_t down_bb = mir_add_block(g->mu);
   mir_block_t done_bb = mir_add_block(g->mu);
   mir_block_t chk_bb = mir_add_block(g->mu);
   mir_block_t end_bb = mir_add_block(g->mu);

   mir_build_jump(g->mu, test_bb);

   mir_set_cursor(g->mu, test_bb, MIR_APPEND);
   mir_value_t lo_v = mir_build_load(g->mu, lo);
   mir_value_t hi_v = mir_build_load(g->mu, hi);
   mir_build_cond(g->mu, mir_build_cmp(g->mu, MIR_CMP_LT, lo_v, hi_v),
                  body_bb, done_bb);

   mir_set_cursor(g->mu, body_bb, MIR_APPEND);
   mir_value_t lo2 = mir_build_load(g->mu, lo);
   mir_value_t hi2 = mir_build_load(g->mu, hi);
   mir_value_t mid = mir_build_div(g->mu, t_offset,
                                   mir_build_add(g->mu, t_offset, lo2, hi2),
                                   two);
   mir_value_t at = mir_build_load(
      g->mu, mir_build_record_ref(g->mu, mir_build_array_ref(g->mu, data, mid),
                                  0));
   mir_build_cond(g->mu, vlog_assoc_key_less(g, kt, at, key),
                  up_bb, down_bb);

   mir_set_cursor(g->mu, up_bb, MIR_APPEND);
   mir_build_store(g->mu, lo, mir_build_add(g->mu, t_offset, mid, one));
   mir_build_jump(g->mu, test_bb);

   mir_set_cursor(g->mu, down_bb, MIR_APPEND);
   mir_build_store(g->mu, hi, mid);
   mir_build_jump(g->mu, test_bb);

   mir_set_cursor(g->mu, done_bb, MIR_APPEND);
   mir_value_t p = mir_build_load(g->mu, lo);
   mir_build_cond(g->mu, mir_build_cmp(g->mu, MIR_CMP_LT, p, len),
                  chk_bb, end_bb);

   mir_set_cursor(g->mu, chk_bb, MIR_APPEND);
   mir_value_t p2 = mir_build_load(g->mu, lo);
   mir_value_t at2 = mir_build_load(
      g->mu, mir_build_record_ref(g->mu, mir_build_array_ref(g->mu, data, p2),
                                  0));
   mir_build_store(g->mu, hit, vlog_assoc_key_equal(g, kt, at2, key));
   mir_build_jump(g->mu, end_bb);

   mir_set_cursor(g->mu, end_bb, MIR_APPEND);
   *pos = mir_build_load(g->mu, lo);
   *found = mir_build_load(g->mu, hit);
}

static mir_value_t vlog_assoc_has_default(vlog_gen_t *g, mir_value_t arr)
{
   // An associative array with a default value has the value held after
   // the last entry and is marked by a descending range

   mir_type_t t_bool = mir_bool_type(g->mu);
   return mir_build_cmp(g->mu, MIR_CMP_EQ, mir_build_uarray_dir(g->mu, arr, 0),
                        mir_const(g->mu, t_bool, RANGE_DOWNTO));
}

static mir_value_t vlog_assoc_wrap(vlog_gen_t *g, mir_value_t data,
                                   mir_value_t count, mir_value_t has_default)
{
   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_type_t t_bool = mir_bool_type(g->mu);

   mir_value_t zero = mir_const(g->mu, t_offset, 0);
   mir_value_t last =
      mir_build_sub(g->mu, t_offset, count, mir_const(g->mu, t_offset, 1));
   mir_value_t left =
      mir_build_select(g->mu, t_offset, has_default, last, zero);
   mir_value_t right =
      mir_build_select(g->mu, t_offset, has_default, zero, last);
   mir_value_t dir = mir_build_select(g->mu, t_bool, has_default,
                                      mir_const(g->mu, t_bool, RANGE_DOWNTO),
                                      mir_const(g->mu, t_bool, RANGE_TO));

   mir_dim_t dims[1] = { { left, right, dir } };
   return mir_build_wrap(g->mu, data, dims, 1);
}

static mir_value_t vlog_assoc_extra(vlog_gen_t *g, mir_value_t has_default)
{
   mir_type_t t_offset = mir_offset_type(g->mu);
   return mir_build_select(g->mu, t_offset, has_default,
                           mir_const(g->mu, t_offset, 1),
                           mir_const(g->mu, t_offset, 0));
}

static void vlog_assoc_set_default(vlog_gen_t *g, mir_value_t slot,
                                   mir_value_t value)
{
   // Keep the value returned for an entry which does not exist

   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_type_t t_bool = mir_bool_type(g->mu);

   mir_value_t arr = mir_build_load(g->mu, slot);
   mir_value_t len = mir_build_uarray_len(g->mu, arr, 0);
   mir_value_t src = mir_build_unwrap(g->mu, arr);

   mir_type_t t_pair = mir_get_elem(g->mu, mir_get_type(g->mu, arr));

   mir_value_t has = mir_const(g->mu, t_bool, 1);
   mir_value_t newlen = mir_build_add(g->mu, t_offset, len,
                                      mir_const(g->mu, t_offset, 1));
   mir_value_t data = vlog_alloc_array(g, t_pair, newlen);
   mir_build_copy(g->mu, data, src, len);
   mir_build_store(g->mu, mir_build_record_ref(
                      g->mu, mir_build_array_ref(g->mu, data, len), 1), value);
   mir_build_store(g->mu, slot, vlog_assoc_wrap(g, data, len, has));
}

static mir_value_t vlog_assoc_copy_default(vlog_gen_t *g, mir_value_t has,
                                           mir_value_t dst, mir_value_t src,
                                           mir_value_t len, vlog_node_t etype)
{
   // Copy the default value from the end of one array to another

   mir_block_t copy_bb = mir_add_block(g->mu);
   mir_block_t done_bb = mir_add_block(g->mu);
   mir_build_cond(g->mu, has, copy_bb, done_bb);

   mir_set_cursor(g->mu, copy_bb, MIR_APPEND);
   mir_value_t sp = mir_build_record_ref(
      g->mu, mir_build_array_ref(g->mu, src, len), 1);
   mir_value_t dp = mir_build_record_ref(
      g->mu, mir_build_array_ref(g->mu, dst, len), 1);
   mir_build_store(g->mu, dp, vlog_copy_value(g, mir_build_load(g->mu, sp),
                                              etype));
   mir_build_jump(g->mu, done_bb);

   mir_set_cursor(g->mu, done_bb, MIR_APPEND);
   return MIR_NULL_VALUE;
}

static void vlog_assoc_clear(vlog_gen_t *g, mir_value_t slot, mir_type_t type)
{
   // Remove every entry but keep the default value

   mir_type_t t_offset = mir_offset_type(g->mu);

   mir_value_t arr = mir_build_load(g->mu, slot);
   mir_value_t has = vlog_assoc_has_default(g, arr);
   mir_value_t len = mir_build_uarray_len(g->mu, arr, 0);
   mir_value_t src = mir_build_unwrap(g->mu, arr);

   mir_type_t t_pair = mir_get_elem(g->mu, type);
   mir_value_t zero = mir_const(g->mu, t_offset, 0);
   mir_value_t data = vlog_alloc_array(g, t_pair, vlog_assoc_extra(g, has));
   mir_build_copy(g->mu, data, mir_build_array_ref(g->mu, src, len),
                  vlog_assoc_extra(g, has));

   mir_build_store(g->mu, slot, vlog_assoc_wrap(g, data, zero, has));
}

static mir_value_t vlog_assoc_insert(vlog_gen_t *g, mir_value_t slot,
                                     const type_info_t *vti, mir_value_t pos,
                                     mir_value_t key)
{
   // Insert a new entry with the default value before position pos

   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_value_t one = mir_const(g->mu, t_offset, 1);

   mir_value_t arr = mir_build_load(g->mu, slot);
   mir_value_t len = mir_build_uarray_len(g->mu, arr, 0);
   mir_value_t src = mir_build_unwrap(g->mu, arr);

   mir_type_t t_pair = mir_get_elem(g->mu, mir_get_type(g->mu, arr));

   mir_value_t has = vlog_assoc_has_default(g, arr);
   mir_value_t newlen = mir_build_add(g->mu, t_offset, len, one);
   mir_value_t data = vlog_alloc_array(
      g, t_pair, mir_build_add(g->mu, t_offset, newlen,
                               vlog_assoc_extra(g, has)));

   mir_value_t after = mir_build_add(g->mu, t_offset, pos, one);
   mir_value_t rest = mir_build_sub(g->mu, t_offset, len, pos);

   mir_build_copy(g->mu, data, src, pos);
   mir_build_copy(g->mu, mir_build_array_ref(g->mu, data, after),
                  mir_build_array_ref(g->mu, src, pos), rest);

   mir_value_t entry = mir_build_array_ref(g->mu, data, pos);
   mir_build_store(g->mu, mir_build_record_ref(g->mu, entry, 0), key);
   mir_value_t vptr = mir_build_record_ref(g->mu, entry, 1);

   // A new entry starts with the default value of the array
   mir_value_t hdr = mir_build_record_ref(
      g->mu, mir_build_array_ref(g->mu, src, len), 1);
   mir_value_t init = mir_add_var(g->mu, vti->type, vti->stamp,
                                  ident_uniq("init"), MIR_VAR_TEMP);
   mir_build_store(g->mu, init, vlog_lower_default_value(g, vti));

   mir_block_t def_bb = mir_add_block(g->mu);
   mir_block_t join_bb = mir_add_block(g->mu);
   mir_build_cond(g->mu, has, def_bb, join_bb);

   mir_set_cursor(g->mu, def_bb, MIR_APPEND);
   mir_build_store(g->mu, init, mir_build_load(g->mu, hdr));
   mir_build_jump(g->mu, join_bb);

   mir_set_cursor(g->mu, join_bb, MIR_APPEND);
   mir_build_store(g->mu, vptr, mir_build_load(g->mu, init));

   mir_build_copy(g->mu, mir_build_array_ref(g->mu, data, newlen),
                  mir_build_array_ref(g->mu, src, len),
                  vlog_assoc_extra(g, has));

   mir_build_store(g->mu, slot, vlog_assoc_wrap(g, data, newlen, has));
   return vptr;
}

static void vlog_assoc_remove(vlog_gen_t *g, mir_value_t slot, mir_value_t pos)
{
   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_value_t one = mir_const(g->mu, t_offset, 1);

   mir_value_t arr = mir_build_load(g->mu, slot);
   mir_value_t len = mir_build_uarray_len(g->mu, arr, 0);
   mir_value_t src = mir_build_unwrap(g->mu, arr);

   mir_type_t t_pair = mir_get_elem(g->mu, mir_get_type(g->mu, arr));

   mir_value_t has = vlog_assoc_has_default(g, arr);
   mir_value_t newlen = mir_build_sub(g->mu, t_offset, len, one);
   mir_value_t data = vlog_alloc_array(
      g, t_pair, mir_build_add(g->mu, t_offset, newlen,
                               vlog_assoc_extra(g, has)));

   mir_value_t after = mir_build_add(g->mu, t_offset, pos, one);
   mir_value_t rest = mir_build_sub(g->mu, t_offset, newlen, pos);

   mir_build_copy(g->mu, data, src, pos);
   mir_build_copy(g->mu, mir_build_array_ref(g->mu, data, pos),
                  mir_build_array_ref(g->mu, src, after), rest);
   mir_build_copy(g->mu, mir_build_array_ref(g->mu, data, newlen),
                  mir_build_array_ref(g->mu, src, len),
                  vlog_assoc_extra(g, has));

   mir_build_store(g->mu, slot, vlog_assoc_wrap(g, data, newlen, has));
}

static mir_value_t vlog_assoc_element(vlog_gen_t *g, mir_value_t slot,
                                      vlog_node_t aty, mir_value_t key,
                                      bool for_write, vlog_node_t where)
{
   // Pointer to the element with the given key.  Reading an element
   // which does not exist gives the default value and writing to it
   // creates it.

   bool is_string;
   mir_type_t t_key = vlog_assoc_key_type(g, aty, &is_string);
   mir_type_t t_offset = mir_offset_type(g->mu);

   const type_info_t *vti = vlog_type_info(g, vlog_type(aty));

   mir_type_t t_ptr = mir_pointer_type(g->mu, vti->type);
   mir_value_t ptrvar = mir_add_var(g->mu, t_ptr, MIR_NULL_STAMP,
                                    ident_uniq("elem"), MIR_VAR_TEMP);
   mir_value_t keyvar = mir_add_var(g->mu, t_key, MIR_NULL_STAMP,
                                    ident_uniq("key"), MIR_VAR_TEMP);
   mir_build_store(g->mu, keyvar, key);

   mir_value_t pos, found;
   vlog_assoc_search(g, mir_build_load(g->mu, slot), vlog_assoc_key(aty),
                     mir_build_load(g->mu, keyvar), &pos, &found);

   mir_value_t posvar = mir_add_var(g->mu, t_offset, MIR_NULL_STAMP,
                                    ident_uniq("pos"), MIR_VAR_TEMP);
   mir_build_store(g->mu, posvar, pos);

   mir_block_t hit_bb = mir_add_block(g->mu);
   mir_block_t miss_bb = mir_add_block(g->mu);
   mir_block_t join_bb = mir_add_block(g->mu);

   mir_build_cond(g->mu, found, hit_bb, miss_bb);

   mir_set_cursor(g->mu, hit_bb, MIR_APPEND);
   {
      mir_value_t data = mir_build_unwrap(g->mu, mir_build_load(g->mu, slot));
      mir_value_t entry = mir_build_array_ref(
         g->mu, data, mir_build_load(g->mu, posvar));
      mir_build_store(g->mu, ptrvar, mir_build_record_ref(g->mu, entry, 1));
      mir_build_jump(g->mu, join_bb);
   }

   mir_set_cursor(g->mu, miss_bb, MIR_APPEND);
   if (for_write) {
      mir_value_t vptr = vlog_assoc_insert(g, slot, vti,
                                           mir_build_load(g->mu, posvar),
                                           mir_build_load(g->mu, keyvar));
      mir_build_store(g->mu, ptrvar, vptr);
   }
   else {
      mir_value_t dummy = mir_add_var(g->mu, vti->type, vti->stamp,
                                      ident_uniq("missing"), 0);
      mir_build_store(g->mu, dummy, vlog_lower_default_value(g, vti));

      mir_value_t arr = mir_build_load(g->mu, slot);
      mir_block_t def_bb = mir_add_block(g->mu);
      mir_block_t none_bb = mir_add_block(g->mu);
      if (where != NULL && where == g->update_key)
         where = NULL;
      mir_block_t warn_bb = where != NULL ? mir_add_block(g->mu) : none_bb;
      mir_build_cond(g->mu, vlog_assoc_has_default(g, arr), def_bb, warn_bb);

      if (where != NULL) {
         mir_set_cursor(g->mu, warn_bb, MIR_APPEND);
         vlog_report_warning(g, where, "associative array has no entry for "
                             "this index so its default value is read");
         mir_build_jump(g->mu, none_bb);
      }

      mir_set_cursor(g->mu, def_bb, MIR_APPEND);
      mir_value_t hdr = mir_build_record_ref(
         g->mu, mir_build_array_ref(g->mu, mir_build_unwrap(g->mu, arr),
                                    mir_build_uarray_len(g->mu, arr, 0)), 1);
      mir_build_store(g->mu, dummy, mir_build_load(g->mu, hdr));
      mir_build_jump(g->mu, none_bb);

      mir_set_cursor(g->mu, none_bb, MIR_APPEND);
      mir_build_store(g->mu, ptrvar, dummy);
   }
   mir_build_jump(g->mu, join_bb);

   mir_set_cursor(g->mu, join_bb, MIR_APPEND);
   return mir_build_load(g->mu, ptrvar);
}

static bool vlog_assoc_key_four_state(vlog_gen_t *g, vlog_node_t kt)
{
   if (kt == NULL || vlog_assoc_key_is_string(kt)
       || vlog_assoc_key_is_struct(kt) || vlog_kind(kt) == V_CLASS_DECL
       || vlog_assoc_key_is_wide(kt))
      return false;

   return mir_get_class(g->mu, vlog_type_info(g, kt)->type) == MIR_TYPE_VEC4;
}

static void vlog_report_assoc_key(vlog_gen_t *g, vlog_node_t kt,
                                  mir_value_t key)
{
   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_value_t args[] = { mir_const(g->mu, t_offset, vlog_size(kt)), key };
   mir_value_t res = mir_build_fcall(g->mu, ident_new("__sv_assoc_badkey"),
                                     t_offset, MIR_NULL_STAMP, args,
                                     ARRAY_LEN(args));
   mir_build_consume(g->mu, res);
}

static mir_value_t vlog_lower_assoc_checked(vlog_gen_t *g, mir_value_t slot,
                                            vlog_node_t type,
                                            vlog_node_t kexpr, bool for_write,
                                            const type_info_t *eti)
{
   // An element selected with a key that has unknown bits does not exist:
   // a write is ignored and a read gives the default value

   vlog_node_t kt = vlog_assoc_key(type);
   if (!vlog_assoc_key_four_state(g, kt)) {
      mir_value_t key = vlog_assoc_key_value(g, type, kexpr);
      return vlog_assoc_element(g, slot, type, key, for_write, kexpr);
   }

   mir_value_t raw = vlog_lower_rvalue(g, kexpr);
   mir_value_t kv = vlog_lower_cast(g, vlog_type_info(g, kt)->type, raw);
   mir_value_t known = mir_build_defined(g->mu, kv);
   mir_value_t key = vlog_assoc_key_cast(g, type, raw);

   mir_type_t t_ptr = mir_pointer_type(g->mu, eti->type);
   mir_value_t ptrvar = mir_add_var(g->mu, t_ptr, MIR_NULL_STAMP,
                                    ident_uniq("elem"), MIR_VAR_TEMP);

   mir_block_t ok_bb = mir_add_block(g->mu);
   mir_block_t bad_bb = mir_add_block(g->mu);
   mir_block_t join_bb = mir_add_block(g->mu);
   mir_build_cond(g->mu, known, ok_bb, bad_bb);

   mir_set_cursor(g->mu, ok_bb, MIR_APPEND);
   mir_build_store(g->mu, ptrvar,
                   vlog_assoc_element(g, slot, type, key, for_write, kexpr));
   mir_build_jump(g->mu, join_bb);

   mir_set_cursor(g->mu, bad_bb, MIR_APPEND);
   vlog_report_assoc_key(g, kt, kv);
   mir_value_t dummy = mir_add_var(g->mu, eti->type, eti->stamp,
                                   ident_uniq("badkey"), 0);
   mir_build_store(g->mu, dummy, vlog_lower_default_value(g, eti));
   mir_build_store(g->mu, ptrvar, dummy);
   mir_build_jump(g->mu, join_bb);

   mir_set_cursor(g->mu, join_bb, MIR_APPEND);
   return mir_build_load(g->mu, ptrvar);
}

static mir_value_t vlog_lower_assoc_method(vlog_gen_t *g, vlog_node_t v)
{
   mir_type_t t_int = mir_vec2_type(g->mu, 32, true);
   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_type_t t_key64 = mir_vec2_type(g->mu, 64, true);

   vlog_node_t prefix = vlog_value(v);
   vlog_node_t aty = vlog_get_type(prefix);

   bool is_string;
   (void)vlog_assoc_key_type(g, aty, &is_string);

   const vlog_method_t method = vlog_subkind(v);

   mir_value_t zero = mir_const(g->mu, t_offset, 0);
   mir_value_t one = mir_const(g->mu, t_offset, 1);

   mir_value_t slot = vlog_lower_select(g, prefix).obj;

   switch (method) {
   case V_METHOD_DELETE:
      {
         // delete(key) is not an error if there is no such entry
         mir_value_t key = vlog_assoc_key_value(g, aty, vlog_param(v, 0));

         mir_value_t pos, found;
         vlog_assoc_search(g, mir_build_load(g->mu, slot), vlog_assoc_key(aty),
                           key, &pos, &found);

         mir_block_t del_bb = mir_add_block(g->mu);
         mir_block_t done_bb = mir_add_block(g->mu);
         mir_build_cond(g->mu, found, del_bb, done_bb);

         mir_set_cursor(g->mu, del_bb, MIR_APPEND);
         vlog_assoc_remove(g, slot, pos);
         mir_build_jump(g->mu, done_bb);

         mir_set_cursor(g->mu, done_bb, MIR_APPEND);
         return MIR_NULL_VALUE;
      }

   case V_METHOD_ASSOC_EXISTS:
      {
         vlog_node_t kt = vlog_assoc_key(aty);
         mir_value_t known = MIR_NULL_VALUE, key, key_value = MIR_NULL_VALUE;
         if (vlog_assoc_key_four_state(g, kt)) {
            mir_value_t raw = vlog_lower_rvalue(g, vlog_param(v, 0));
            key_value = vlog_lower_cast(g, vlog_type_info(g, kt)->type, raw);
            known = mir_build_defined(g->mu, key_value);
            key = vlog_assoc_key_cast(g, aty, raw);
         }
         else
            key = vlog_assoc_key_value(g, aty, vlog_param(v, 0));

         mir_value_t pos, found;
         vlog_assoc_search(g, mir_build_load(g->mu, slot), vlog_assoc_key(aty),
                           key, &pos, &found);

         if (!mir_is_null(known)) {
            mir_block_t bad_bb = mir_add_block(g->mu);
            mir_block_t join_bb = mir_add_block(g->mu);
            mir_build_cond(g->mu, known, join_bb, bad_bb);

            mir_set_cursor(g->mu, bad_bb, MIR_APPEND);
            vlog_report_assoc_key(g, kt, key_value);
            mir_build_jump(g->mu, join_bb);

            mir_set_cursor(g->mu, join_bb, MIR_APPEND);
            found = mir_build_and(g->mu, found, known);
         }

         return mir_build_select(g->mu, t_int, found,
                                 mir_const_vec(g->mu, t_int, 1, 0),
                                 mir_const_vec(g->mu, t_int, 0, 0));
      }

   case V_METHOD_ASSOC_FIRST:
   case V_METHOD_ASSOC_LAST:
   case V_METHOD_ASSOC_NEXT:
   case V_METHOD_ASSOC_PREV:
      {
         vlog_node_t arg = vlog_param(v, 0);

         mir_value_t result = mir_add_var(g->mu, t_int, MIR_NULL_STAMP,
                                          ident_uniq("trav"), MIR_VAR_TEMP);
         mir_build_store(g->mu, result, mir_const_vec(g->mu, t_int, 0, 0));

         mir_value_t arr = mir_build_load(g->mu, slot);
         mir_value_t len = mir_build_uarray_len(g->mu, arr, 0);

         mir_value_t idx;
         switch (method) {
         case V_METHOD_ASSOC_FIRST:
            idx = zero;
            break;
         case V_METHOD_ASSOC_LAST:
            idx = mir_build_sub(g->mu, t_offset, len, one);
            break;
         default:
            {
               mir_value_t key;
               if (is_string)
                  key = vlog_str_arg(g, arg);
               else
                  key = vlog_assoc_key_cast(g, aty, vlog_lower_rvalue(g, arg));

               mir_value_t pos, found;
               vlog_assoc_search(g, arr, vlog_assoc_key(aty), key, &pos, &found);

               if (method == V_METHOD_ASSOC_NEXT) {
                  // Entry after this one or the first one which is greater
                  mir_value_t after = mir_build_add(g->mu, t_offset, pos, one);
                  idx = mir_build_select(g->mu, t_offset, found, after, pos);
               }
               else
                  idx = mir_build_sub(g->mu, t_offset, pos, one);
            }
            break;
         }

         mir_value_t idxvar = mir_add_var(g->mu, t_offset, MIR_NULL_STAMP,
                                          ident_uniq("idx"), MIR_VAR_TEMP);
         mir_build_store(g->mu, idxvar, idx);

         mir_block_t chk_bb = mir_add_block(g->mu);
         mir_block_t get_bb = mir_add_block(g->mu);
         mir_block_t done_bb = mir_add_block(g->mu);

         // The index is negative when there is no previous entry
         mir_build_cond(g->mu, mir_build_cmp(g->mu, MIR_CMP_GEQ, idx, zero),
                        chk_bb, done_bb);

         mir_set_cursor(g->mu, chk_bb, MIR_APPEND);
         mir_value_t len2 = mir_build_uarray_len(
            g->mu, mir_build_load(g->mu, slot), 0);
         mir_build_cond(g->mu,
                        mir_build_cmp(g->mu, MIR_CMP_LT,
                                      mir_build_load(g->mu, idxvar), len2),
                        get_bb, done_bb);

         mir_set_cursor(g->mu, get_bb, MIR_APPEND);
         mir_value_t data = mir_build_unwrap(
            g->mu, mir_build_load(g->mu, slot));
         mir_value_t entry = mir_build_array_ref(
            g->mu, data, mir_build_load(g->mu, idxvar));
         mir_value_t found_key = mir_build_load(
            g->mu, mir_build_record_ref(g->mu, entry, 0));
         if (is_string || vlog_assoc_key_is_struct(vlog_assoc_key(aty)))
            vlog_assign_variable(g, arg, found_key, NULL);
         else if (vlog_assoc_key(aty) != NULL
                  && vlog_kind(vlog_assoc_key(aty)) == V_CLASS_DECL) {
            vlog_node_t kt = vlog_assoc_key(aty);
            vlog_assign_variable(
               g, arg, mir_build_cast(g->mu, vlog_type_info(g, kt)->type,
                                      found_key), NULL);
         }
         else if (vlog_assoc_key_is_wide(vlog_assoc_key(aty))) {
            vlog_node_t kt = vlog_assoc_key(aty);
            vlog_assign_variable(
               g, arg, vlog_lower_cast(g, vlog_type_info(g, kt)->type,
                                       found_key), NULL);
         }
         else {
            vlog_node_t kt = vlog_assoc_key(aty);
            mir_value_t kv = vlog_lower_cast(g, t_key64, found_key);
            if (kt != NULL)
               kv = vlog_lower_cast(g, vlog_type_info(g, kt)->type, kv);
            vlog_assign_variable(g, arg, kv, NULL);
         }
         mir_build_store(g->mu, result, mir_const_vec(g->mu, t_int, 1, 0));
         mir_build_jump(g->mu, done_bb);

         mir_set_cursor(g->mu, done_bb, MIR_APPEND);
         return mir_build_load(g->mu, result);
      }

   default:
      should_not_reach_here();
   }
}

static mir_value_t vlog_lower_semaphore(vlog_gen_t *g, vlog_node_t v)
{
   // The number of keys is held where a mailbox holds its limit

   mir_type_t t_cell = mir_int_type(g->mu, INT64_MIN, INT64_MAX);
   mir_type_t t_int = mir_vec2_type(g->mu, 32, true);

   vlog_node_t prefix = vlog_value(v);

   mir_value_t count = mir_add_var(g->mu, t_cell, MIR_NULL_STAMP,
                                   ident_uniq("keys"), 0);
   if (vlog_params(v) > 0)
      mir_build_store(g->mu, count,
                      mir_build_cast(g->mu, t_cell,
                                     vlog_lower_cast(
                                        g, mir_offset_type(g->mu),
                                        vlog_lower_rvalue(g,
                                                          vlog_param(v, 0)))));
   else
      mir_build_store(g->mu, count, mir_const(g->mu, t_cell, 1));

   switch ((vlog_method_t)vlog_subkind(v)) {
   case V_METHOD_SEM_PUT:
      {
         mir_value_t rec = mir_build_all(g->mu, vlog_lower_rvalue(g, prefix));
         mir_value_t keys = mir_build_record_ref(g->mu, rec, 2);
         mir_build_store(g->mu, keys,
                         mir_build_add(g->mu, t_cell,
                                       mir_build_load(g->mu, keys),
                                       mir_build_load(g->mu, count)));
         vlog_lower_event_trigger(g, mir_build_load(
                                     g->mu, mir_build_record_ref(g->mu, rec, 1)));
         return MIR_NULL_VALUE;
      }
   case V_METHOD_SEM_GET:
      {
         // Blocked processes are served in the order they arrived but a
         // new request may overtake them while the oldest cannot proceed
         mir_value_t mine = mir_add_var(g->mu, t_cell, MIR_NULL_STAMP,
                                        ident_uniq("ticket"), 0);

         mir_block_t queue_bb = mir_add_block(g->mu);
         mir_block_t test_bb = mir_add_block(g->mu);
         mir_block_t take_bb = mir_add_block(g->mu);
         mir_block_t wait_bb = mir_add_block(g->mu);
         mir_block_t pass_bb = mir_add_block(g->mu);
         mir_block_t done_bb = mir_add_block(g->mu);

         mir_value_t rec0 = mir_build_all(g->mu, vlog_lower_rvalue(g, prefix));
         mir_value_t keys0 = mir_build_load(
            g->mu, mir_build_record_ref(g->mu, rec0, 2));
         mir_value_t next = mir_build_load(
            g->mu, mir_build_record_ref(g->mu, rec0, 3));
         mir_value_t idle = mir_build_cmp(
            g->mu, MIR_CMP_EQ, next,
            mir_build_load(g->mu, mir_build_record_ref(g->mu, rec0, 4)));
         mir_value_t need = mir_build_load(
            g->mu, mir_build_record_ref(g->mu, rec0, 5));
         mir_value_t stuck = mir_build_and(
            g->mu,
            mir_build_cmp(g->mu, MIR_CMP_GT, need,
                          mir_const(g->mu, t_cell, 0)),
            mir_build_cmp(g->mu, MIR_CMP_LT, keys0, need));
         mir_value_t overtake = mir_build_and(
            g->mu,
            mir_build_cmp(g->mu, MIR_CMP_GEQ, keys0,
                          mir_build_load(g->mu, count)),
            mir_build_or(g->mu, idle, stuck));
         mir_build_cond(g->mu, overtake, pass_bb, queue_bb);

         mir_set_cursor(g->mu, pass_bb, MIR_APPEND);
         mir_value_t rec1 = mir_build_all(g->mu, vlog_lower_rvalue(g, prefix));
         mir_value_t keys1 = mir_build_record_ref(g->mu, rec1, 2);
         mir_build_store(g->mu, keys1,
                         mir_build_sub(g->mu, t_cell,
                                       mir_build_load(g->mu, keys1),
                                       mir_build_load(g->mu, count)));
         mir_build_jump(g->mu, done_bb);

         mir_set_cursor(g->mu, queue_bb, MIR_APPEND);
         mir_value_t rec2 = mir_build_all(g->mu, vlog_lower_rvalue(g, prefix));
         mir_value_t tickets = mir_build_record_ref(g->mu, rec2, 3);
         mir_value_t ticket = mir_build_load(g->mu, tickets);
         mir_build_store(g->mu, mine, ticket);
         mir_build_store(g->mu, tickets,
                         mir_build_add(g->mu, t_cell, ticket,
                                       mir_const(g->mu, t_cell, 1)));
         mir_build_jump(g->mu, test_bb);

         mir_set_cursor(g->mu, test_bb, MIR_APPEND);
         mir_value_t rec = mir_build_all(g->mu, vlog_lower_rvalue(g, prefix));
         mir_value_t keys = mir_build_record_ref(g->mu, rec, 2);
         mir_value_t serving = mir_build_record_ref(g->mu, rec, 4);
         mir_value_t oldest = mir_build_cmp(
            g->mu, MIR_CMP_EQ, mir_build_load(g->mu, serving),
            mir_build_load(g->mu, mine));
         mir_value_t wanted = mir_build_record_ref(g->mu, rec, 5);
         mir_build_store(g->mu, wanted,
                         mir_build_select(g->mu, t_cell, oldest,
                                          mir_build_load(g->mu, count),
                                          mir_build_load(g->mu, wanted)));
         mir_value_t ready = mir_build_and(
            g->mu, oldest,
            mir_build_cmp(g->mu, MIR_CMP_GEQ, mir_build_load(g->mu, keys),
                          mir_build_load(g->mu, count)));
         mir_build_cond(g->mu, ready, take_bb, wait_bb);

         mir_set_cursor(g->mu, wait_bb, MIR_APPEND);
         vlog_lower_queue_wait(g, mir_build_load(
                                  g->mu, mir_build_record_ref(g->mu, rec, 1)));
         mir_build_jump(g->mu, test_bb);

         mir_set_cursor(g->mu, take_bb, MIR_APPEND);
         mir_value_t rec3 = mir_build_all(g->mu, vlog_lower_rvalue(g, prefix));
         mir_value_t keys3 = mir_build_record_ref(g->mu, rec3, 2);
         mir_build_store(g->mu, keys3,
                         mir_build_sub(g->mu, t_cell,
                                       mir_build_load(g->mu, keys3),
                                       mir_build_load(g->mu, count)));
         mir_value_t serving3 = mir_build_record_ref(g->mu, rec3, 4);
         mir_build_store(g->mu, serving3,
                         mir_build_add(g->mu, t_cell,
                                       mir_build_load(g->mu, serving3),
                                       mir_const(g->mu, t_cell, 1)));
         mir_build_store(g->mu, mir_build_record_ref(g->mu, rec3, 5),
                         mir_const(g->mu, t_cell, 0));
         vlog_lower_event_trigger(g, mir_build_load(
                                     g->mu, mir_build_record_ref(g->mu, rec3, 1)));
         mir_build_jump(g->mu, done_bb);

         mir_set_cursor(g->mu, done_bb, MIR_APPEND);
         return MIR_NULL_VALUE;
      }
   default:
      {
         mir_value_t result = mir_add_var(g->mu, t_int, MIR_NULL_STAMP,
                                          ident_uniq("try_get"), MIR_VAR_TEMP);
         mir_build_store(g->mu, result, mir_const_vec(g->mu, t_int, 0, 0));

         mir_block_t take_bb = mir_add_block(g->mu);
         mir_block_t done_bb = mir_add_block(g->mu);

         mir_value_t rec = mir_build_all(g->mu, vlog_lower_rvalue(g, prefix));
         mir_value_t keys = mir_build_record_ref(g->mu, rec, 2);
         mir_value_t idle = mir_build_cmp(
            g->mu, MIR_CMP_EQ,
            mir_build_load(g->mu, mir_build_record_ref(g->mu, rec, 3)),
            mir_build_load(g->mu, mir_build_record_ref(g->mu, rec, 4)));
         mir_build_cond(g->mu,
                        mir_build_and(g->mu,
                                      mir_build_cmp(g->mu, MIR_CMP_GEQ,
                                                    mir_build_load(g->mu, keys),
                                                    mir_build_load(g->mu, count)),
                                      idle),
                        take_bb, done_bb);

         mir_set_cursor(g->mu, take_bb, MIR_APPEND);
         mir_build_store(g->mu, keys,
                         mir_build_sub(g->mu, t_cell,
                                       mir_build_load(g->mu, keys),
                                       mir_build_load(g->mu, count)));
         mir_build_store(g->mu, result, mir_const_vec(g->mu, t_int, 1, 0));
         mir_build_jump(g->mu, done_bb);

         mir_set_cursor(g->mu, done_bb, MIR_APPEND);
         return mir_build_load(g->mu, result);
      }
   }
}

static mir_value_t vlog_lower_string_method(vlog_gen_t *g, vlog_node_t v)
{
   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_type_t t_char = mir_char_type(g->mu);

   vlog_node_t prefix = vlog_value(v);

   switch ((vlog_method_t)vlog_subkind(v)) {
   case V_METHOD_STR_LEN:
      return vlog_offset_to_int(g, mir_build_uarray_len(
                                   g->mu, vlog_str_arg(g, prefix), 0));
   case V_METHOD_STR_GETC:
      {
         mir_value_t args[] = {
            vlog_str_arg(g, prefix), vlog_str_offset_arg(g, vlog_param(v, 0))
         };
         mir_value_t ch = vlog_str_call(g, "__sv_str_getc", t_offset, args, 2);
         mir_type_t t_byte = mir_vec2_type(g->mu, 8, true);
         return mir_build_cast(g->mu, t_byte, vlog_offset_to_int(g, ch));
      }
   case V_METHOD_STR_PUTC:
      {
         mir_value_t slot = vlog_lower_select(g, prefix).obj;
         mir_value_t str = vlog_str_arg(g, prefix);
         mir_value_t len = mir_build_uarray_len(g->mu, str, 0);
         mir_value_t data = vlog_str_copy(g, str);
         mir_value_t args[] = {
            data, len, vlog_str_offset_arg(g, vlog_param(v, 0)),
            vlog_str_offset_arg(g, vlog_param(v, 1))
         };
         vlog_str_call(g, "__sv_str_putc", t_offset, args, 4);
         mir_build_store(g->mu, slot, vlog_wrap_array(g, data, len));
         return MIR_NULL_VALUE;
      }
   case V_METHOD_STR_TOUPPER:
   case V_METHOD_STR_TOLOWER:
      {
         mir_value_t str = vlog_str_arg(g, prefix);
         mir_value_t len = mir_build_uarray_len(g->mu, str, 0);
         mir_value_t data = vlog_alloc_array(g, t_char, len);
         mir_value_t upper = mir_const(
            g->mu, t_offset, vlog_subkind(v) == V_METHOD_STR_TOUPPER);
         mir_value_t args[] = { str, data, upper };
         vlog_str_call(g, "__sv_str_case", t_offset, args, 3);
         return vlog_wrap_array(g, data, len);
      }
   case V_METHOD_STR_COMPARE:
   case V_METHOD_STR_ICOMPARE:
      {
         mir_value_t icase = mir_const(
            g->mu, t_offset, vlog_subkind(v) == V_METHOD_STR_ICOMPARE);
         mir_value_t args[] = {
            vlog_str_arg(g, prefix), vlog_str_arg(g, vlog_param(v, 0)), icase
         };
         mir_value_t cmp = vlog_str_call(g, "__sv_str_cmp", t_offset, args, 3);
         return vlog_offset_to_int(g, cmp);
      }
   case V_METHOD_STR_SUBSTR:
      {
         mir_value_t zero = mir_const(g->mu, t_offset, 0);
         mir_value_t one = mir_const(g->mu, t_offset, 1);

         mir_value_t str = vlog_str_arg(g, prefix);
         mir_value_t len = mir_build_uarray_len(g->mu, str, 0);
         mir_value_t first = vlog_str_offset_arg(g, vlog_param(v, 0));
         mir_value_t last = vlog_str_offset_arg(g, vlog_param(v, 1));

         mir_value_t valid = mir_build_and(
            g->mu,
            mir_build_and(g->mu,
                          mir_build_cmp(g->mu, MIR_CMP_GEQ, first, zero),
                          mir_build_cmp(g->mu, MIR_CMP_GEQ, last, first)),
            mir_build_cmp(g->mu, MIR_CMP_LT, last, len));
         mir_value_t span = mir_build_add(
            g->mu, t_offset, mir_build_sub(g->mu, t_offset, last, first), one);
         mir_value_t count = mir_build_select(g->mu, t_offset, valid, span,
                                              zero);
         mir_value_t start = mir_build_select(g->mu, t_offset, valid, first,
                                              zero);

         mir_value_t data = vlog_alloc_array(g, t_char, count);
         mir_value_t src = mir_build_array_ref(
            g->mu, mir_build_unwrap(g->mu, str), start);
         mir_build_copy(g->mu, data, src, count);
         return vlog_wrap_array(g, data, count);
      }
   case V_METHOD_STR_ATOI:
   case V_METHOD_STR_ATOHEX:
   case V_METHOD_STR_ATOOCT:
   case V_METHOD_STR_ATOBIN:
      {
         int base = 10;
         switch ((vlog_method_t)vlog_subkind(v)) {
         case V_METHOD_STR_ATOHEX: base = 16; break;
         case V_METHOD_STR_ATOOCT: base = 8; break;
         case V_METHOD_STR_ATOBIN: base = 2; break;
         default: break;
         }

         mir_value_t args[] = {
            vlog_str_arg(g, prefix), mir_const(g->mu, t_offset, base)
         };
         mir_value_t value = vlog_str_call(g, "__sv_str_ato", t_offset, args, 2);
         return vlog_offset_to_int(g, value);
      }
   case V_METHOD_STR_ATOREAL:
      {
         mir_type_t t_real = mir_real_type(g->mu, -DBL_MAX, DBL_MAX);
         mir_value_t args[] = { vlog_str_arg(g, prefix) };
         return vlog_str_call(g, "__sv_str_atoreal", t_real, args, 1);
      }
   case V_METHOD_STR_ITOA:
   case V_METHOD_STR_HEXTOA:
   case V_METHOD_STR_OCTTOA:
   case V_METHOD_STR_BINTOA:
   case V_METHOD_STR_REALTOA:
      {
         mir_value_t slot = vlog_lower_select(g, prefix).obj;

         mir_type_t t_buf = mir_carray_type(g->mu, 72, t_char);
         mir_value_t buf = vlog_get_temp(g, t_buf);

         mir_value_t len;
         if (vlog_subkind(v) == V_METHOD_STR_REALTOA) {
            mir_type_t t_real = mir_real_type(g->mu, -DBL_MAX, DBL_MAX);
            mir_value_t real = vlog_lower_with_context(g, vlog_param(v, 0),
                                                       t_real);
            mir_value_t args[] = { vlog_lower_cast(g, t_real, real), buf };
            len = vlog_str_call(g, "__sv_str_realtoa", t_offset, args, 2);
         }
         else {
            int base = 10;
            switch ((vlog_method_t)vlog_subkind(v)) {
            case V_METHOD_STR_HEXTOA: base = 16; break;
            case V_METHOD_STR_OCTTOA: base = 8; break;
            case V_METHOD_STR_BINTOA: base = 2; break;
            default: break;
            }

            mir_value_t args[] = {
               vlog_str_int_arg(g, vlog_param(v, 0), base == 10),
               mir_const(g->mu, t_offset, base), buf
            };
            len = vlog_str_call(g, "__sv_str_toa", t_offset, args, 3);
         }

         mir_value_t data = vlog_alloc_array(g, t_char, len);
         mir_build_copy(g->mu, data, buf, len);
         mir_build_store(g->mu, slot, vlog_wrap_array(g, data, len));
         return MIR_NULL_VALUE;
      }
   default:
      should_not_reach_here();
   }
}

static mir_value_t vlog_arr_elem(vlog_gen_t *g, mir_value_t data,
                                 mir_value_t index)
{
   return mir_build_load(g->mu, mir_build_array_ref(g->mu, data, index));
}

static mir_value_t vlog_arr_less(vlog_gen_t *g, const vlog_arr_t *a,
                                 mir_type_t type, mir_value_t left,
                                 mir_value_t right)
{
   if (mir_get_class(g->mu, type) == MIR_TYPE_REAL)
      return mir_build_cmp(g->mu, MIR_CMP_LT, left, right);
   else if (mir_get_class(g->mu, type) == MIR_TYPE_UARRAY) {
      mir_type_t t_offset = mir_offset_type(g->mu);
      mir_value_t zero = mir_const(g->mu, t_offset, 0);
      mir_value_t args[] = { left, right, zero };
      mir_value_t sc = vlog_str_call(g, "__sv_str_cmp", t_offset, args, 3);
      return mir_build_cmp(g->mu, MIR_CMP_LT, sc, zero);
   }

   mir_value_t cmp = mir_build_binary(g->mu, MIR_VEC_LT, type, left, right);
   return mir_build_test(g->mu, cmp);
}

static mir_value_t vlog_arr_equal(vlog_gen_t *g, mir_type_t type,
                                  mir_value_t left, mir_value_t right)
{
   if (mir_get_class(g->mu, type) == MIR_TYPE_REAL)
      return mir_build_cmp(g->mu, MIR_CMP_EQ, left, right);
   else if (mir_get_class(g->mu, type) == MIR_TYPE_UARRAY) {
      mir_type_t t_offset = mir_offset_type(g->mu);
      mir_value_t zero = mir_const(g->mu, t_offset, 0);
      mir_value_t args[] = { left, right, zero };
      mir_value_t sc = vlog_str_call(g, "__sv_str_cmp", t_offset, args, 3);
      return mir_build_cmp(g->mu, MIR_CMP_EQ, sc, zero);
   }

   mir_value_t cmp = mir_build_binary(g->mu, MIR_VEC_CASE_EQ, type, left,
                                      right);
   return mir_build_test(g->mu, cmp);
}

static mir_value_t vlog_arr_const(vlog_gen_t *g, mir_type_t type, int kind)
{
   // Identity of the reduction: 0 for sum, or, xor; 1 for product; all ones
   // for and

   if (mir_get_class(g->mu, type) == MIR_TYPE_REAL)
      return mir_const_real(g->mu, type, kind == 1 ? 1.0 : 0.0);

   const int size = mir_get_size(g->mu, type);
   if (size > 64)
      fatal_at(NULL, "sorry, array reductions on elements wider than 64 bits "
               "are not supported");

   uint64_t bits = 0;
   if (kind == 1)
      bits = 1;
   else if (kind == 2)
      bits = size == 64 ? ~UINT64_C(0) : (UINT64_C(1) << size) - 1;

   return mir_const_vec(g->mu, type, bits, 0);
}

static mir_value_t vlog_arr_to_uarray(vlog_gen_t *g, vlog_node_t prefix,
                                      const type_info_t *eti, bool fixed)
{
   if (!fixed)
      return vlog_lower_rvalue(g, prefix);

   // Fixed-size array: copy the elements out of the flat value
   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_type_t t_elem = mir_vec4_type(g->mu, eti->size, false);

   const unsigned count = vlog_size(vlog_ref(prefix));
   mir_value_t flat = vlog_lower_rvalue(g, prefix);
   mir_value_t num = mir_const(g->mu, t_offset, count);
   mir_value_t data = vlog_alloc_array(g, eti->type, num);

   for (int i = 0; i < count; i++) {
      mir_value_t part = mir_build_extract(
         g->mu, t_elem, flat, mir_const(g->mu, t_offset, i * eti->size));
      mir_build_store(g->mu, mir_build_array_ref(
                         g->mu, data, mir_const(g->mu, t_offset, i)),
                      vlog_lower_cast(g, eti->type, part));
   }

   return vlog_wrap_array(g, data, num);
}

static void vlog_arr_store(vlog_gen_t *g, vlog_node_t prefix,
                           const type_info_t *eti, bool fixed, mir_value_t data,
                           mir_value_t count)
{
   if (!fixed) {
      mir_value_t slot = vlog_lower_select(g, prefix).obj;
      mir_build_store(g->mu, slot, vlog_wrap_array(g, data, count));
      return;
   }

   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_type_t t_elem = mir_vec4_type(g->mu, eti->size, false);

   const unsigned n = vlog_size(vlog_ref(prefix));
   mir_type_t t_flat = mir_vec4_type(g->mu, n * eti->size, false);

   mir_value_t flat;
   if (n * eti->size <= 64)
      flat = mir_const_vec(g->mu, t_flat, 0, 0);
   else {
      mir_type_t t_bit = mir_vec4_type(g->mu, 1, false);
      flat = mir_build_cast(g->mu, t_flat, mir_const_vec(g->mu, t_bit, 0, 0));
   }

   for (int i = 0; i < n; i++) {
      mir_value_t elem = vlog_arr_elem(g, data, mir_const(g->mu, t_offset, i));
      flat = mir_build_insert(g->mu, mir_build_cast(g->mu, t_elem, elem), flat,
                              mir_const(g->mu, t_offset, i * eti->size));
   }

   vlog_assign_variable(g, prefix, flat, NULL);
}

static mir_value_t vlog_arr_copy(vlog_gen_t *g, const vlog_arr_t *a,
                                 mir_value_t src, mir_value_t len)
{
   mir_value_t data = vlog_alloc_array(g, a->eti->type, len);
   mir_build_copy(g->mu, data, src, len);
   return data;
}

static void vlog_arr_insertion_sort(vlog_gen_t *g, const vlog_arr_t *a,
                                    mir_value_t data, mir_value_t keys,
                                    bool move_keys, mir_type_t ktype,
                                    mir_value_t len, bool descending)
{
   // Stable insertion sort of data by keys

   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_value_t zero = mir_const(g->mu, t_offset, 0);
   mir_value_t one = mir_const(g->mu, t_offset, 1);

   mir_value_t jvar = mir_add_var(g->mu, t_offset, MIR_NULL_STAMP,
                                  ident_uniq("j"), MIR_VAR_TEMP);

   vlog_loop_t l;
   vlog_loop_begin(g, &l, one, len);

   mir_value_t cur = vlog_arr_elem(g, data, l.i);
   mir_value_t curk = vlog_arr_elem(g, keys, l.i);
   mir_build_store(g->mu, jvar, l.i);

   mir_block_t test_bb = mir_add_block(g->mu);
   mir_block_t cmp_bb = mir_add_block(g->mu);
   mir_block_t shift_bb = mir_add_block(g->mu);
   mir_block_t place_bb = mir_add_block(g->mu);

   mir_build_jump(g->mu, test_bb);

   mir_set_cursor(g->mu, test_bb, MIR_APPEND);
   mir_value_t j = mir_build_load(g->mu, jvar);
   mir_build_cond(g->mu, mir_build_cmp(g->mu, MIR_CMP_GT, j, zero),
                  cmp_bb, place_bb);

   mir_set_cursor(g->mu, cmp_bb, MIR_APPEND);
   mir_value_t j1 = mir_build_load(g->mu, jvar);
   mir_value_t prev = mir_build_sub(g->mu, t_offset, j1, one);
   mir_value_t prevk = vlog_arr_elem(g, keys, prev);
   mir_value_t out_of_order = descending
      ? vlog_arr_less(g, a, ktype, prevk, curk)
      : vlog_arr_less(g, a, ktype, curk, prevk);
   mir_build_cond(g->mu, out_of_order, shift_bb, place_bb);

   mir_set_cursor(g->mu, shift_bb, MIR_APPEND);
   mir_value_t j2 = mir_build_load(g->mu, jvar);
   mir_value_t prev2 = mir_build_sub(g->mu, t_offset, j2, one);
   mir_build_store(g->mu, mir_build_array_ref(g->mu, data, j2),
                   vlog_arr_elem(g, data, prev2));
   if (move_keys)
      mir_build_store(g->mu, mir_build_array_ref(g->mu, keys, j2),
                      vlog_arr_elem(g, keys, prev2));
   mir_build_store(g->mu, jvar, prev2);
   mir_build_jump(g->mu, test_bb);

   mir_set_cursor(g->mu, place_bb, MIR_APPEND);
   mir_value_t j3 = mir_build_load(g->mu, jvar);
   mir_build_store(g->mu, mir_build_array_ref(g->mu, data, j3), cur);
   if (move_keys)
      mir_build_store(g->mu, mir_build_array_ref(g->mu, keys, j3), curk);

   vlog_loop_end(g, &l);
}

static mir_value_t vlog_lower_array_method(vlog_gen_t *g, vlog_node_t v)
{
   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_value_t zero = mir_const(g->mu, t_offset, 0);
   mir_value_t one = mir_const(g->mu, t_offset, 1);

   const vlog_method_t method = vlog_subkind(v);
   vlog_node_t prefix = vlog_value(v);
   vlog_node_t type = vlog_get_type(prefix);

   const bool assoc = vlog_kind(type) == V_DATA_TYPE
      && vlog_subkind(type) == DT_ASSOC;
   const bool fixed = !assoc && (vlog_kind(type) != V_DATA_TYPE
      || (vlog_subkind(type) != DT_QUEUE && vlog_subkind(type) != DT_DYNARRAY));
   vlog_node_t etype = fixed ? type : vlog_type(type);

   vlog_arr_t a = { .eti = vlog_type_info(g, etype), .up = true };

   vlog_node_t dims[MAX_UNPACKED_DIMS];
   vlog_node_t dim = fixed && vlog_unpacked_dims(prefix, dims) > 0
      ? dims[0] : vlog_sized_dimension(type);

   int64_t left, right;
   if (dim != NULL && vlog_bounds(dim, &left, &right)) {
      a.left = left;
      a.up = left <= right;
   }

   const mir_class_t eclass = mir_get_class(g->mu, a.eti->type);
   a.is_real = eclass == MIR_TYPE_REAL;
   if (eclass != MIR_TYPE_VEC2 && eclass != MIR_TYPE_VEC4 && !a.is_real) {
      // Only the methods that move elements work with other types
      const bool is_locator = method == V_METHOD_ARR_FIND
         || method == V_METHOD_ARR_FIND_INDEX
         || method == V_METHOD_ARR_FIND_FIRST
         || method == V_METHOD_ARR_FIND_FIRST_INDEX
         || method == V_METHOD_ARR_FIND_LAST
         || method == V_METHOD_ARR_FIND_LAST_INDEX;

      const bool keyed = method == V_METHOD_ARR_SORT
         || method == V_METHOD_ARR_RSORT
         || method == V_METHOD_ARR_MIN
         || method == V_METHOD_ARR_MAX
         || method == V_METHOD_ARR_UNIQUE
         || method == V_METHOD_ARR_UNIQUE_INDEX
         || method == V_METHOD_ARR_SUM
         || method == V_METHOD_ARR_PRODUCT
         || method == V_METHOD_ARR_AND
         || method == V_METHOD_ARR_OR
         || method == V_METHOD_ARR_XOR;

      const bool ordered = eclass == MIR_TYPE_UARRAY
         && (method == V_METHOD_ARR_SORT || method == V_METHOD_ARR_RSORT
             || method == V_METHOD_ARR_MIN || method == V_METHOD_ARR_MAX
             || method == V_METHOD_ARR_UNIQUE
             || method == V_METHOD_ARR_UNIQUE_INDEX);

      if (!ordered && (fixed || (method != V_METHOD_ARR_REVERSE
                    && method != V_METHOD_ARR_SHUFFLE
                    && method != V_METHOD_ARR_INSERT
                    && !((is_locator || keyed) && vlog_stmts(v) == 3))))
         fatal_at(vlog_loc(v), "sorry, this array method is only supported "
                  "for arrays of integral or real elements");
   }

   a.ktype = a.eti->type;

   if (vlog_stmts(v) == 3) {
      vlog_node_t item = vlog_stmt(v, 0), index = vlog_stmt(v, 1);
      a.with = vlog_stmt(v, 2);

      a.iti = vlog_type_info(g, vlog_type(index));
      a.item_var = mir_add_var(g->mu, a.eti->type, a.eti->stamp,
                               vlog_ident(item), 0);
      a.index_var = mir_add_var(g->mu, a.iti->type, a.iti->stamp,
                                ident_new("index"), 0);
      mir_put_object(g->mu, item, a.item_var);
      mir_put_object(g->mu, index, a.index_var);

      if (vlog_is_real(a.with))
         fatal_at(vlog_loc(a.with), "sorry, real valued with expressions "
                  "are not supported");

      if (vlog_is_string(a.with))
         a.ktype = mir_uarray_type(g->mu, 1, mir_char_type(g->mu));
      else
         a.ktype = mir_vec4_type(g->mu, MAX(vlog_width(a.with), 1),
                                 vlog_is_signed(a.with));
   }

   switch (method) {
   case V_METHOD_ARR_FIND:
   case V_METHOD_ARR_FIND_INDEX:
   case V_METHOD_ARR_FIND_FIRST:
   case V_METHOD_ARR_FIND_FIRST_INDEX:
   case V_METHOD_ARR_FIND_LAST:
   case V_METHOD_ARR_FIND_LAST_INDEX:
      if (a.with == NULL)
         fatal_at(vlog_loc(v), "the %pQ method requires a with clause",
                  vlog_ident(v));
      break;
   default:
      break;
   }

   if (method == V_METHOD_ARR_INSERT) {
      mir_value_t slot = vlog_lower_select(g, prefix).obj;
      mir_value_t arr = mir_build_load(g->mu, slot);
      mir_value_t len = mir_build_uarray_len(g->mu, arr, 0);
      mir_value_t src = mir_build_unwrap(g->mu, arr);

      mir_value_t idx = vlog_str_offset_arg(g, vlog_param(v, 0));
      mir_value_t value = vlog_lower_with_context(g, vlog_param(v, 1),
                                                  a.eti->type);
      value = vlog_copy_value(g, vlog_lower_cast(g, a.eti->type, value),
                              etype);

      mir_value_t valid = mir_build_and(
         g->mu, mir_build_cmp(g->mu, MIR_CMP_GEQ, idx, zero),
         mir_build_cmp(g->mu, MIR_CMP_LEQ, idx, len));

      mir_block_t do_bb = mir_add_block(g->mu);
      mir_block_t done_bb = mir_add_block(g->mu);
      mir_build_cond(g->mu, valid, do_bb, done_bb);

      mir_set_cursor(g->mu, do_bb, MIR_APPEND);
      mir_value_t newlen = mir_build_add(g->mu, t_offset, len, one);
      mir_value_t data = vlog_alloc_array(g, a.eti->type, newlen);
      mir_build_copy(g->mu, data, src, idx);
      mir_build_store(g->mu, mir_build_array_ref(g->mu, data, idx), value);
      mir_value_t rest = mir_build_sub(g->mu, t_offset, len, idx);
      mir_build_copy(g->mu, mir_build_array_ref(
                        g->mu, data, mir_build_add(g->mu, t_offset, idx, one)),
                     mir_build_array_ref(g->mu, src, idx), rest);
      mir_build_store(g->mu, slot, vlog_wrap_array(g, data, newlen));
      mir_build_jump(g->mu, done_bb);

      mir_set_cursor(g->mu, done_bb, MIR_APPEND);
      return MIR_NULL_VALUE;
   }

   mir_value_t arr = vlog_arr_to_uarray(g, prefix, a.eti, fixed);
   mir_value_t len = mir_build_uarray_len(g->mu, arr, 0);
   mir_value_t src = mir_build_unwrap(g->mu, arr);

   if (assoc) {
      // The entries are pairs of the key and element, followed by the
      // default value if there is one
      len = mir_build_sub(g->mu, t_offset, len,
                          vlog_assoc_extra(g, vlog_assoc_has_default(g, arr)));
   }

   const bool locator = method == V_METHOD_ARR_FIND
      || method == V_METHOD_ARR_FIND_INDEX
      || method == V_METHOD_ARR_FIND_FIRST
      || method == V_METHOD_ARR_FIND_FIRST_INDEX
      || method == V_METHOD_ARR_FIND_LAST
      || method == V_METHOD_ARR_FIND_LAST_INDEX;

   mir_value_t entries = src;

   if (assoc && !locator) {
      // Work on the elements in key order
      mir_value_t data = vlog_alloc_array(g, a.eti->type, len);
      if (a.with != NULL)
         a.keys = vlog_alloc_array(g, a.iti->type, len);

      vlog_loop_t l;
      vlog_loop_begin(g, &l, zero, len);
      mir_value_t entry = mir_build_array_ref(g->mu, src, l.i);
      mir_build_store(g->mu, mir_build_array_ref(g->mu, data, l.i),
                      mir_build_load(g->mu, mir_build_record_ref(g->mu, entry, 1)));
      if (a.with != NULL) {
         mir_value_t key = mir_build_load(
            g->mu, mir_build_record_ref(g->mu, entry, 0));
         mir_build_store(g->mu, mir_build_array_ref(g->mu, a.keys, l.i),
                         vlog_lower_cast(g, a.iti->type, key));
      }
      vlog_loop_end(g, &l);
      src = data;
   }

   switch (method) {
   case V_METHOD_ARR_REVERSE:
      {
         mir_value_t data = vlog_alloc_array(g, a.eti->type, len);
         mir_value_t last = mir_build_sub(g->mu, t_offset, len, one);

         vlog_loop_t l;
         vlog_loop_begin(g, &l, zero, len);
         mir_value_t from = mir_build_sub(g->mu, t_offset, last, l.i);
         mir_build_store(g->mu, mir_build_array_ref(g->mu, data, l.i),
                         vlog_arr_elem(g, src, from));
         vlog_loop_end(g, &l);

         vlog_arr_store(g, prefix, a.eti, fixed, data, len);
         return MIR_NULL_VALUE;
      }

   case V_METHOD_ARR_SHUFFLE:
      {
         mir_value_t data = vlog_arr_copy(g, &a, src, len);

         // Fisher-Yates from the end
         vlog_loop_t l;
         vlog_loop_begin(g, &l, zero, len);
         mir_value_t i = mir_build_sub(
            g->mu, t_offset, mir_build_sub(g->mu, t_offset, len, one), l.i);
         mir_value_t bound = mir_build_add(g->mu, t_offset, i, one);
         mir_value_t args[] = { bound };
         mir_value_t j = vlog_str_call(g, "__sv_urandom_below", t_offset,
                                       args, 1);
         mir_value_t pi = mir_build_array_ref(g->mu, data, i);
         mir_value_t pj = mir_build_array_ref(g->mu, data, j);
         mir_value_t tmp = mir_build_load(g->mu, pi);
         mir_build_store(g->mu, pi, mir_build_load(g->mu, pj));
         mir_build_store(g->mu, pj, tmp);
         vlog_loop_end(g, &l);

         vlog_arr_store(g, prefix, a.eti, fixed, data, len);
         return MIR_NULL_VALUE;
      }

   case V_METHOD_ARR_SORT:
   case V_METHOD_ARR_RSORT:
      {
         const bool descending = method == V_METHOD_ARR_RSORT;

         mir_value_t data = vlog_arr_copy(g, &a, src, len);
         mir_value_t keys = data;
         if (a.with != NULL) {
            keys = vlog_alloc_array(g, a.ktype, len);

            vlog_loop_t l;
            vlog_loop_begin(g, &l, zero, len);
            mir_value_t key = vlog_arr_key(g, &a, vlog_arr_elem(g, src, l.i),
                                           l.i);
            mir_build_store(g->mu, mir_build_array_ref(g->mu, keys, l.i), key);
            vlog_loop_end(g, &l);
         }

         vlog_arr_insertion_sort(g, &a, data, keys, a.with != NULL,
                                 a.ktype, len, descending);

         vlog_arr_store(g, prefix, a.eti, fixed, data, len);
         return MIR_NULL_VALUE;
      }

   case V_METHOD_ARR_SUM:
   case V_METHOD_ARR_PRODUCT:
   case V_METHOD_ARR_AND:
   case V_METHOD_ARR_OR:
   case V_METHOD_ARR_XOR:
      {
         mir_type_t rtype = a.ktype;
         int kind = 0;
         if (method == V_METHOD_ARR_PRODUCT)
            kind = 1;
         else if (method == V_METHOD_ARR_AND)
            kind = 2;

         mir_value_t acc = mir_add_var(g->mu, rtype, MIR_NULL_STAMP,
                                       ident_uniq("acc"), MIR_VAR_TEMP);
         mir_build_store(g->mu, acc, vlog_arr_const(g, rtype, kind));

         vlog_loop_t l;
         vlog_loop_begin(g, &l, zero, len);
         mir_value_t key = vlog_arr_key(g, &a, vlog_arr_elem(g, src, l.i), l.i);
         mir_value_t cur = mir_build_load(g->mu, acc);

         mir_value_t next;
         if (a.is_real) {
            if (method == V_METHOD_ARR_SUM)
               next = mir_build_add(g->mu, rtype, cur, key);
            else if (method == V_METHOD_ARR_PRODUCT)
               next = mir_build_mul(g->mu, rtype, cur, key);
            else
               fatal_at(vlog_loc(v), "bitwise reduction of real values");
         }
         else {
            mir_vec_op_t op = MIR_VEC_ADD;
            switch (method) {
            case V_METHOD_ARR_PRODUCT: op = MIR_VEC_MUL; break;
            case V_METHOD_ARR_AND:     op = MIR_VEC_BIT_AND; break;
            case V_METHOD_ARR_OR:      op = MIR_VEC_BIT_OR; break;
            case V_METHOD_ARR_XOR:     op = MIR_VEC_BIT_XOR; break;
            default: break;
            }

            next = mir_build_binary(g->mu, op, rtype, cur, key);
         }

         mir_build_store(g->mu, acc, next);
         vlog_loop_end(g, &l);

         return mir_build_load(g->mu, acc);
      }

   case V_METHOD_ARR_MIN:
   case V_METHOD_ARR_MAX:
      {
         const bool want_max = method == V_METHOD_ARR_MAX;

         mir_value_t best = mir_add_var(g->mu, t_offset, MIR_NULL_STAMP,
                                        ident_uniq("best"), MIR_VAR_TEMP);
         mir_value_t bestk = mir_add_var(g->mu, a.ktype, MIR_NULL_STAMP,
                                         ident_uniq("bestk"), MIR_VAR_TEMP);
         mir_build_store(g->mu, best, zero);

         vlog_loop_t l;
         vlog_loop_begin(g, &l, zero, len);

         mir_value_t key = vlog_arr_key(g, &a, vlog_arr_elem(g, src, l.i), l.i);

         mir_block_t take_bb = mir_add_block(g->mu);
         mir_block_t test_bb = mir_add_block(g->mu);

         mir_build_cond(g->mu, mir_build_cmp(g->mu, MIR_CMP_EQ, l.i, zero),
                        take_bb, test_bb);

         mir_set_cursor(g->mu, test_bb, MIR_APPEND);
         mir_value_t cur = mir_build_load(g->mu, bestk);
         mir_value_t better = want_max
            ? vlog_arr_less(g, &a, a.ktype, cur, key)
            : vlog_arr_less(g, &a, a.ktype, key, cur);
         mir_block_t cont_bb = mir_add_block(g->mu);
         mir_build_cond(g->mu, better, take_bb, cont_bb);

         mir_set_cursor(g->mu, take_bb, MIR_APPEND);
         mir_build_store(g->mu, bestk, key);
         mir_build_store(g->mu, best, l.i);
         mir_build_jump(g->mu, cont_bb);

         mir_set_cursor(g->mu, cont_bb, MIR_APPEND);
         vlog_loop_end(g, &l);

         mir_value_t count = mir_build_select(
            g->mu, t_offset, mir_build_cmp(g->mu, MIR_CMP_GT, len, zero), one,
            zero);
         mir_value_t data = vlog_alloc_array(g, a.eti->type, count);

         mir_block_t one_bb = mir_add_block(g->mu);
         mir_block_t done_bb = mir_add_block(g->mu);
         mir_build_cond(g->mu, mir_build_cmp(g->mu, MIR_CMP_GT, len, zero),
                        one_bb, done_bb);

         mir_set_cursor(g->mu, one_bb, MIR_APPEND);
         mir_build_store(g->mu, data,
                         vlog_arr_elem(g, src, mir_build_load(g->mu, best)));
         mir_build_jump(g->mu, done_bb);

         mir_set_cursor(g->mu, done_bb, MIR_APPEND);
         return vlog_wrap_array(g, data, count);
      }

   case V_METHOD_ARR_UNIQUE:
   case V_METHOD_ARR_UNIQUE_INDEX:
      {
         // The LRM does not specify the order of the result (7.12.1), so
         // it is ordered by key

         const bool want_index = method == V_METHOD_ARR_UNIQUE_INDEX;
         mir_type_t t_bool = mir_bool_type(g->mu);

         mir_value_t keys = src;
         if (a.with != NULL) {
            keys = vlog_alloc_array(g, a.ktype, len);

            vlog_loop_t l;
            vlog_loop_begin(g, &l, zero, len);
            mir_value_t key = vlog_arr_key(g, &a, vlog_arr_elem(g, src, l.i),
                                           l.i);
            mir_build_store(g->mu, mir_build_array_ref(g->mu, keys, l.i), key);
            vlog_loop_end(g, &l);
         }

         // First occurrences and their keys
         mir_value_t firsts = vlog_alloc_array(g, t_offset, len);
         mir_value_t fkeys = vlog_alloc_array(g, a.ktype, len);
         mir_value_t count = mir_add_var(g->mu, t_offset, MIR_NULL_STAMP,
                                         ident_uniq("count"), MIR_VAR_TEMP);
         mir_value_t seen = mir_add_var(g->mu, t_bool, MIR_NULL_STAMP,
                                        ident_uniq("seen"), MIR_VAR_TEMP);
         mir_build_store(g->mu, count, zero);

         vlog_loop_t outer;
         vlog_loop_begin(g, &outer, zero, len);
         mir_build_store(g->mu, seen, mir_const(g->mu, t_bool, 0));

         mir_value_t ki = vlog_arr_elem(g, keys, outer.i);

         vlog_loop_t inner;
         vlog_loop_begin(g, &inner, zero, outer.i);
         mir_value_t kj = vlog_arr_elem(g, keys, inner.i);
         mir_block_t dup_bb = mir_add_block(g->mu);
         mir_build_cond(g->mu, vlog_arr_equal(g, a.ktype, ki, kj), dup_bb,
                        inner.next_bb);
         mir_set_cursor(g->mu, dup_bb, MIR_APPEND);
         mir_build_store(g->mu, seen, mir_const(g->mu, t_bool, 1));
         vlog_loop_end(g, &inner);

         mir_block_t add_bb = mir_add_block(g->mu);
         mir_build_cond(g->mu, mir_build_load(g->mu, seen), outer.next_bb,
                        add_bb);

         mir_set_cursor(g->mu, add_bb, MIR_APPEND);
         mir_value_t n = mir_build_load(g->mu, count);
         mir_build_store(g->mu, mir_build_array_ref(g->mu, firsts, n), outer.i);
         mir_build_store(g->mu, mir_build_array_ref(g->mu, fkeys, n), ki);
         mir_build_store(g->mu, count, mir_build_add(g->mu, t_offset, n, one));
         vlog_loop_end(g, &outer);

         mir_value_t total = mir_build_load(g->mu, count);
         vlog_arr_insertion_sort(g, &a, firsts, fkeys, true, a.ktype, total,
                                 false);

         mir_type_t t_res = vlog_type_info(
            g, vlog_type(vlog_ref(v)))->type;   // Element of result queue
         mir_value_t data = vlog_alloc_array(g, t_res, total);

         vlog_loop_t l;
         vlog_loop_begin(g, &l, zero, total);
         mir_value_t from = vlog_arr_elem(g, firsts, l.i);
         mir_value_t index = MIR_NULL_VALUE;
         if (want_index && assoc) {
            mir_value_t entry = mir_build_array_ref(g->mu, entries, from);
            index = mir_build_load(g->mu, mir_build_record_ref(g->mu, entry, 0));
         }
         else if (want_index)
            index = vlog_offset_to_int(g, vlog_arr_index(g, &a, from));

         mir_value_t out = vlog_lower_cast(
            g, t_res, want_index ? index : vlog_arr_elem(g, src, from));
         mir_build_store(g->mu, mir_build_array_ref(g->mu, data, l.i), out);
         vlog_loop_end(g, &l);

         return vlog_wrap_array(g, data, total);
      }

   case V_METHOD_ARR_FIND:
   case V_METHOD_ARR_FIND_INDEX:
   case V_METHOD_ARR_FIND_FIRST:
   case V_METHOD_ARR_FIND_FIRST_INDEX:
   case V_METHOD_ARR_FIND_LAST:
   case V_METHOD_ARR_FIND_LAST_INDEX:
      {
         const bool want_index = method == V_METHOD_ARR_FIND_INDEX
            || method == V_METHOD_ARR_FIND_FIRST_INDEX
            || method == V_METHOD_ARR_FIND_LAST_INDEX;
         const bool first = method == V_METHOD_ARR_FIND_FIRST
            || method == V_METHOD_ARR_FIND_FIRST_INDEX;
         const bool last = method == V_METHOD_ARR_FIND_LAST
            || method == V_METHOD_ARR_FIND_LAST_INDEX;

         mir_type_t t_res = vlog_type_info(
            g, vlog_type(vlog_ref(v)))->type;   // Element of result queue
         mir_value_t data = vlog_alloc_array(g, t_res, len);
         mir_value_t count = mir_add_var(g->mu, t_offset, MIR_NULL_STAMP,
                                         ident_uniq("count"), MIR_VAR_TEMP);
         mir_build_store(g->mu, count, zero);

         vlog_loop_t l;
         vlog_loop_begin(g, &l, zero, len);

         mir_value_t elem, index;
         if (assoc) {
            mir_value_t entry = mir_build_array_ref(g->mu, src, l.i);
            elem = mir_build_load(g->mu, mir_build_record_ref(g->mu, entry, 1));
            index = vlog_lower_cast(
               g, a.iti->type,
               mir_build_load(g->mu, mir_build_record_ref(g->mu, entry, 0)));
         }
         else {
            elem = vlog_arr_elem(g, src, l.i);
            index = vlog_offset_to_int(g, vlog_arr_index(g, &a, l.i));
         }

         mir_build_store(g->mu, a.item_var, elem);
         mir_build_store(g->mu, a.index_var, index);

         mir_value_t test = vlog_lower_test(g, vlog_lower_rvalue(g, a.with));

         mir_block_t hit_bb = mir_add_block(g->mu);
         mir_build_cond(g->mu, test, hit_bb, l.next_bb);

         mir_set_cursor(g->mu, hit_bb, MIR_APPEND);
         mir_value_t n = mir_build_load(g->mu, count);

         mir_value_t pos = n;
         if (last)
            pos = zero;

         mir_value_t out = vlog_lower_cast(g, t_res, want_index ? index : elem);

         if (first) {
            mir_block_t store_bb = mir_add_block(g->mu);
            mir_build_cond(g->mu, mir_build_cmp(g->mu, MIR_CMP_EQ, n, zero),
                           store_bb, l.next_bb);
            mir_set_cursor(g->mu, store_bb, MIR_APPEND);
         }

         mir_build_store(g->mu, mir_build_array_ref(g->mu, data, pos), out);
         mir_value_t newn = (first || last) ? one
            : mir_build_add(g->mu, t_offset, n, one);
         mir_build_store(g->mu, count, newn);

         vlog_loop_end(g, &l);

         return vlog_wrap_array(g, data, mir_build_load(g->mu, count));
      }

   default:
      should_not_reach_here();
   }
}

static mir_value_t vlog_enum_equal(vlog_gen_t *g, const type_info_t *ti,
                                   mir_value_t a, mir_value_t b)
{
   mir_type_t t_vec = mir_vec4_type(g->mu, ti->size, false);
   mir_value_t eq = mir_build_binary(g->mu, MIR_VEC_CASE_EQ, t_vec,
                                     mir_build_cast(g->mu, t_vec, a),
                                     mir_build_cast(g->mu, t_vec, b));
   return vlog_lower_test(g, eq);
}

static mir_value_t vlog_lower_enum_method(vlog_gen_t *g, vlog_node_t v)
{
   // The methods of an enumeration visit the names in declaration order

   vlog_node_t prefix = vlog_value(v);
   vlog_node_t type = vlog_get_type(prefix);
   if (type == NULL || vlog_kind(type) != V_ENUM_DECL)
      fatal_at(vlog_loc(v), "%pQ is only a method of an enumeration but the "
               "type here is not one", vlog_ident(v));

   const type_info_t *ti = vlog_type_info(g, type);
   const int nnames = vlog_decls(type);

   mir_type_t t_int = mir_vec2_type(g->mu, 32, true);

   mir_value_t *vals LOCAL = xmalloc_array(nnames, sizeof(mir_value_t));
   for (int i = 0; i < nnames; i++)
      vals[i] = vlog_lower_cast(g, ti->type, vlog_lower_rvalue(
                                   g, vlog_value(vlog_decl(type, i))));

   const vlog_method_t method = vlog_subkind(v);
   switch (method) {
   case V_METHOD_ENUM_FIRST:
      return vals[0];
   case V_METHOD_ENUM_LAST:
      return vals[nnames - 1];
   case V_METHOD_ENUM_NUM:
      return mir_const_vec(g->mu, t_int, nnames, 0);
   case V_METHOD_ENUM_NEXT:
   case V_METHOD_ENUM_PREV:
      {
         int64_t step = 1;
         if (vlog_params(v) > 0 && !vlog_get_const(vlog_param(v, 0), &step))
            fatal_at(vlog_loc(v), "sorry, the argument of this enumeration "
                     "method must be constant");

         if (method == V_METHOD_ENUM_PREV)
            step = -step;

         mir_value_t cur = vlog_lower_rvalue(g, prefix);
         mir_value_t res = mir_add_var(g->mu, ti->type, ti->stamp,
                                       ident_uniq("enum"), MIR_VAR_TEMP);
         mir_build_store(g->mu, res, vals[method == V_METHOD_ENUM_NEXT
                                          ? 0 : nnames - 1]);

         mir_block_t done_bb = mir_add_block(g->mu);
         for (int i = 0; i < nnames; i++) {
            const int to = (((i + step) % nnames) + nnames) % nnames;

            mir_block_t hit_bb = mir_add_block(g->mu);
            mir_block_t next_bb = mir_add_block(g->mu);
            mir_build_cond(g->mu, vlog_enum_equal(g, ti, cur, vals[i]),
                           hit_bb, next_bb);

            mir_set_cursor(g->mu, hit_bb, MIR_APPEND);
            mir_build_store(g->mu, res, vals[to]);
            mir_build_jump(g->mu, done_bb);

            mir_set_cursor(g->mu, next_bb, MIR_APPEND);
         }

         mir_build_jump(g->mu, done_bb);
         mir_set_cursor(g->mu, done_bb, MIR_APPEND);
         return mir_build_load(g->mu, res);
      }
   case V_METHOD_ENUM_NAME:
      {
         mir_type_t t_str = mir_uarray_type(g->mu, 1, mir_char_type(g->mu));
         mir_value_t cur = vlog_lower_rvalue(g, prefix);
         mir_value_t res = mir_add_var(g->mu, t_str, MIR_NULL_STAMP,
                                       ident_uniq("name"), MIR_VAR_TEMP);

         // Formatting a value which is not a name uses its number
         mir_build_store(g->mu, res, vlog_params(v) > 0
                         ? vlog_lower_rvalue(g, vlog_param(v, 0))
                         : vlog_empty_array(g, t_str));

         mir_block_t done_bb = mir_add_block(g->mu);
         for (int i = 0; i < nnames; i++) {
            mir_block_t hit_bb = mir_add_block(g->mu);
            mir_block_t next_bb = mir_add_block(g->mu);
            mir_build_cond(g->mu, vlog_enum_equal(g, ti, cur, vals[i]),
                           hit_bb, next_bb);

            mir_set_cursor(g->mu, hit_bb, MIR_APPEND);
            const char *name = istr(vlog_ident(vlog_decl(type, i)));
            number_t n = number_from_string(name, strlen(name));
            mir_build_store(g->mu, res, vlog_string_const(g, n));
            mir_build_jump(g->mu, done_bb);

            mir_set_cursor(g->mu, next_bb, MIR_APPEND);
         }

         mir_build_jump(g->mu, done_bb);
         mir_set_cursor(g->mu, done_bb, MIR_APPEND);
         return mir_build_load(g->mu, res);
      }
   default:
      should_not_reach_here();
   }
}

static vlog_select_t vlog_lower_wselect(vlog_gen_t *g, vlog_node_t v)
{
   // Select something which is going to be modified

   g->for_write = true;
   vlog_select_t result = vlog_lower_select(g, v);
   g->for_write = false;
   return result;
}

static mir_value_t vlog_lower_builtin_method(vlog_gen_t *g, vlog_node_t v)
{
   mir_type_t t_int = mir_vec2_type(g->mu, 32, true);
   mir_type_t t_offset = mir_offset_type(g->mu);

   vlog_node_t prefix = vlog_value(v);
   vlog_node_t type = vlog_get_type(prefix);

   switch ((vlog_method_t)vlog_subkind(v)) {
   case V_METHOD_STR_LEN:
   case V_METHOD_STR_PUTC:
   case V_METHOD_STR_GETC:
   case V_METHOD_STR_TOUPPER:
   case V_METHOD_STR_TOLOWER:
   case V_METHOD_STR_COMPARE:
   case V_METHOD_STR_ICOMPARE:
   case V_METHOD_STR_SUBSTR:
   case V_METHOD_STR_ATOI:
   case V_METHOD_STR_ATOHEX:
   case V_METHOD_STR_ATOOCT:
   case V_METHOD_STR_ATOBIN:
   case V_METHOD_STR_ATOREAL:
   case V_METHOD_STR_ITOA:
   case V_METHOD_STR_HEXTOA:
   case V_METHOD_STR_OCTTOA:
   case V_METHOD_STR_BINTOA:
   case V_METHOD_STR_REALTOA:
      return vlog_lower_string_method(g, v);
   case V_METHOD_ARR_INSERT:
   case V_METHOD_ARR_REVERSE:
   case V_METHOD_ARR_SORT:
   case V_METHOD_ARR_RSORT:
   case V_METHOD_ARR_SHUFFLE:
   case V_METHOD_ARR_SUM:
   case V_METHOD_ARR_PRODUCT:
   case V_METHOD_ARR_AND:
   case V_METHOD_ARR_OR:
   case V_METHOD_ARR_XOR:
   case V_METHOD_ARR_MIN:
   case V_METHOD_ARR_MAX:
   case V_METHOD_ARR_UNIQUE:
   case V_METHOD_ARR_UNIQUE_INDEX:
   case V_METHOD_ARR_FIND:
   case V_METHOD_ARR_FIND_INDEX:
   case V_METHOD_ARR_FIND_FIRST:
   case V_METHOD_ARR_FIND_FIRST_INDEX:
   case V_METHOD_ARR_FIND_LAST:
   case V_METHOD_ARR_FIND_LAST_INDEX:
      return vlog_lower_array_method(g, v);
   case V_METHOD_ENUM_FIRST:
   case V_METHOD_ENUM_LAST:
   case V_METHOD_ENUM_NEXT:
   case V_METHOD_ENUM_PREV:
   case V_METHOD_ENUM_NUM:
   case V_METHOD_ENUM_NAME:
      return vlog_lower_enum_method(g, v);
   case V_METHOD_PROC_STATUS:
      {
         mir_value_t args[] = { vlog_lower_rvalue(g, prefix) };
         mir_value_t status = mir_build_fcall(
            g->mu, ident_new("__sv_process_status"), t_offset, MIR_NULL_STAMP,
            args, 1);
         mir_build_consume(g->mu, status);
         return vlog_offset_to_int(g, status);
      }
   case V_METHOD_PROC_KILL:
      {
         mir_value_t args[] = { vlog_lower_rvalue(g, prefix) };
         mir_value_t self = mir_build_fcall(
            g->mu, ident_new("__sv_process_kill"), t_offset, MIR_NULL_STAMP,
            args, 1);
         mir_build_consume(g->mu, self);

         // A process which kills itself does not run any more statements
         if (mir_get_kind(g->mu) != MIR_UNIT_FUNCTION) {
            mir_block_t stop_bb = mir_add_block(g->mu);
            mir_block_t cont_bb = mir_add_block(g->mu);
            mir_value_t zero = mir_const(g->mu, t_offset, 0);
            mir_build_cond(g->mu, mir_build_cmp(g->mu, MIR_CMP_EQ, self, zero),
                           cont_bb, stop_bb);

            mir_set_cursor(g->mu, stop_bb, MIR_APPEND);
            mir_block_t resume_bb = mir_add_block(g->mu);
            mir_build_wait(g->mu, resume_bb);

            mir_set_cursor(g->mu, resume_bb, MIR_APPEND);
            mir_build_jump(g->mu, cont_bb);

            mir_set_cursor(g->mu, cont_bb, MIR_APPEND);
         }

         return MIR_NULL_VALUE;
      }
   case V_METHOD_PROC_AWAIT:
      {
         if (mir_get_kind(g->mu) == MIR_UNIT_FUNCTION)
            fatal_at(vlog_loc(v), "sorry, process::await cannot be used in a "
                     "function");

         // Registers do not survive the process suspending
         mir_type_t t_handle = mir_get_type(g->mu, vlog_lower_rvalue(g, prefix));
         mir_value_t var = mir_add_var(g->mu, t_handle, MIR_NULL_STAMP,
                                       ident_uniq("await"), 0);
         mir_build_store(g->mu, var, vlog_lower_rvalue(g, prefix));

         mir_block_t test_bb = mir_add_block(g->mu);
         mir_block_t wait_bb = mir_add_block(g->mu);
         mir_block_t done_bb = mir_add_block(g->mu);

         mir_build_jump(g->mu, test_bb);
         mir_set_cursor(g->mu, test_bb, MIR_APPEND);

         mir_value_t args[] = { mir_build_load(g->mu, var) };
         mir_value_t blocked = mir_build_fcall(
            g->mu, ident_new("__sv_process_await"), t_offset, MIR_NULL_STAMP,
            args, 1);
         mir_build_consume(g->mu, blocked);

         mir_value_t zero = mir_const(g->mu, t_offset, 0);
         mir_build_cond(g->mu, mir_build_cmp(g->mu, MIR_CMP_EQ, blocked, zero),
                        done_bb, wait_bb);

         mir_set_cursor(g->mu, wait_bb, MIR_APPEND);

         mir_block_t resume_bb = mir_add_block(g->mu);
         mir_build_wait(g->mu, resume_bb);

         mir_set_cursor(g->mu, resume_bb, MIR_APPEND);
         mir_build_jump(g->mu, test_bb);

         mir_set_cursor(g->mu, done_bb, MIR_APPEND);
         return MIR_NULL_VALUE;
      }
   case V_METHOD_PROC_RESUME:
      {
         mir_value_t args[] = { vlog_lower_rvalue(g, prefix) };
         mir_value_t res = mir_build_fcall(
            g->mu, ident_new("__sv_process_resume"), t_offset, MIR_NULL_STAMP,
            args, 1);
         mir_build_consume(g->mu, res);
         return MIR_NULL_VALUE;
      }
   case V_METHOD_PROC_SUSPEND:
      {
         if (mir_get_kind(g->mu) == MIR_UNIT_FUNCTION)
            fatal_at(vlog_loc(v), "sorry, process::suspend cannot be used in "
                     "a function");

         mir_value_t args[] = { vlog_lower_rvalue(g, prefix) };
         mir_value_t self = mir_build_fcall(
            g->mu, ident_new("__sv_process_suspend"), t_offset, MIR_NULL_STAMP,
            args, 1);
         mir_build_consume(g->mu, self);

         // A process which suspends itself stops at once
         mir_block_t stop_bb = mir_add_block(g->mu);
         mir_block_t cont_bb = mir_add_block(g->mu);
         mir_value_t zero = mir_const(g->mu, t_offset, 0);
         mir_build_cond(g->mu, mir_build_cmp(g->mu, MIR_CMP_EQ, self, zero),
                        cont_bb, stop_bb);

         mir_set_cursor(g->mu, stop_bb, MIR_APPEND);
         mir_block_t resume_bb = mir_add_block(g->mu);
         mir_build_wait(g->mu, resume_bb);

         mir_set_cursor(g->mu, resume_bb, MIR_APPEND);
         mir_build_jump(g->mu, cont_bb);

         mir_set_cursor(g->mu, cont_bb, MIR_APPEND);
         return MIR_NULL_VALUE;
      }
   case V_METHOD_SEM_GET:
   case V_METHOD_SEM_PUT:
   case V_METHOD_SEM_TRY_GET:
      return vlog_lower_semaphore(g, v);
   case V_METHOD_EVENT_TRIGGERED:
      {
         mir_value_t args[] = { vlog_lower_rvalue(g, prefix) };
         mir_type_t t_offset = mir_offset_type(g->mu);
         mir_value_t res = mir_build_fcall(g->mu,
                                           ident_new("__sv_event_triggered"),
                                           t_offset, MIR_NULL_STAMP, args, 1);
         mir_build_consume(g->mu, res);
         return vlog_offset_to_int(g, res);
      }
   case V_METHOD_SRANDOM:
      {
         vlog_node_t class = vlog_get_type(prefix);
         if (class != NULL && vlog_kind(class) == V_CLASS_DECL
             && vlog_crv_has_hidden(class)) {
            mir_value_t obj = vlog_kind(prefix) == V_THIS
               ? vlog_lower_this(g) : vlog_lower_rvalue(g, prefix);
            mir_type_t t_longint = mir_vec2_type(g->mu, 64, true);
            mir_value_t seed = vlog_lower_cast(
               g, t_longint, vlog_lower_rvalue(g, vlog_param(v, 0)));

            // An odd value as zero means the object has no seed
            mir_value_t two = mir_const_vec(g->mu, t_longint, 2, 0);
            mir_value_t one = mir_const_vec(g->mu, t_longint, 1, 0);
            mir_value_t odd = mir_build_binary(
               g->mu, MIR_VEC_ADD, t_longint,
               mir_build_binary(g->mu, MIR_VEC_MUL, t_longint, seed, two), one);

            mir_build_store(g->mu, vlog_crv_hidden(g, obj, "$seed"), odd);
         }
         return MIR_NULL_VALUE;
      }
   case V_METHOD_RANDSTATE_SET:
   case V_METHOD_NOP:
      return MIR_NULL_VALUE;
   case V_METHOD_RANDSTATE_GET:
      {
         mir_type_t t_str = mir_uarray_type(g->mu, 1, mir_char_type(g->mu));
         return vlog_empty_array(g, t_str);
      }
   case V_METHOD_ASSOC_EXISTS:
   case V_METHOD_ASSOC_FIRST:
   case V_METHOD_ASSOC_LAST:
   case V_METHOD_ASSOC_NEXT:
   case V_METHOD_ASSOC_PREV:
      return vlog_lower_assoc_method(g, v);
   case V_METHOD_SIZE:
      return vlog_offset_to_int(g, mir_build_uarray_len(
                                   g->mu, vlog_lower_rvalue(g, prefix), 0));
   case V_METHOD_DELETE:
      {
         if (vlog_params(v) > 0 && vlog_subkind(type) == DT_ASSOC)
            return vlog_lower_assoc_method(g, v);
         else if (vlog_params(v) > 0) {
            if (vlog_subkind(type) != DT_QUEUE)
               fatal_at(vlog_loc(v), "sorry, delete with an index is only "
                        "supported for queues and associative arrays");

            // Deleting an element which does not exist does nothing
            mir_value_t slot = vlog_lower_wselect(g, prefix).obj;
            mir_value_t idx = vlog_lower_cast(
               g, t_offset, vlog_lower_rvalue(g, vlog_param(v, 0)));
            mir_value_t zero = mir_const(g->mu, t_offset, 0);

            mir_value_t idxvar = mir_add_var(g->mu, t_offset, MIR_NULL_STAMP,
                                             ident_uniq("del"), MIR_VAR_TEMP);
            mir_build_store(g->mu, idxvar, idx);

            mir_value_t len = mir_build_uarray_len(
               g->mu, mir_build_load(g->mu, slot), 0);
            mir_value_t in_range =
               mir_build_and(g->mu, mir_build_cmp(g->mu, MIR_CMP_GEQ, idx,
                                                  zero),
                             mir_build_cmp(g->mu, MIR_CMP_LT, idx, len));

            mir_block_t del_bb = mir_add_block(g->mu);
            mir_block_t done_bb = mir_add_block(g->mu);
            mir_build_cond(g->mu, in_range, del_bb, done_bb);

            mir_set_cursor(g->mu, del_bb, MIR_APPEND);
            vlog_assoc_remove(g, slot, mir_build_load(g->mu, idxvar));
            mir_build_jump(g->mu, done_bb);

            mir_set_cursor(g->mu, done_bb, MIR_APPEND);
            return MIR_NULL_VALUE;
         }

         const type_info_t *ti = vlog_type_info(g, type);
         mir_value_t slot = vlog_lower_wselect(g, prefix).obj;
         if (vlog_subkind(type) == DT_ASSOC) {
            vlog_assoc_clear(g, slot, ti->type);
            return MIR_NULL_VALUE;
         }

         mir_build_store(g->mu, slot, vlog_empty_array(g, ti->type));
         return MIR_NULL_VALUE;
      }
   case V_METHOD_PUSH_BACK:
   case V_METHOD_PUSH_FRONT:
      {
         vlog_node_t etype = vlog_type(type);
         const type_info_t *eti = vlog_type_info(g, etype);
         mir_value_t slot = vlog_lower_wselect(g, prefix).obj;
         mir_value_t value = vlog_lower_with_context(g, vlog_param(v, 0),
                                                     eti->type);
         value = vlog_copy_value(g, vlog_lower_cast(g, eti->type, value),
                                 etype);
         vlog_queue_push(g, slot, eti, value,
                         vlog_subkind(v) == V_METHOD_PUSH_FRONT);
         return MIR_NULL_VALUE;
      }
   case V_METHOD_POP_BACK:
   case V_METHOD_POP_FRONT:
      return vlog_queue_pop(g, vlog_lower_wselect(g, prefix).obj,
                            vlog_type_info(g, vlog_type(type)),
                            vlog_subkind(v) == V_METHOD_POP_FRONT);
   case V_METHOD_MBOX_NUM:
      {
         mir_value_t rec = mir_build_all(g->mu, vlog_lower_rvalue(g, prefix));
         mir_value_t q = mir_build_load(g->mu,
                                        mir_build_record_ref(g->mu, rec, 0));
         mir_value_t len = mir_build_uarray_len(g->mu, q, 0);

         // A message is already taken by a process waiting for it
         mir_value_t waiting = mir_build_cast(
            g->mu, t_offset,
            mir_build_load(g->mu, mir_build_record_ref(g->mu, rec, 3)));
         mir_value_t avail = mir_build_sub(g->mu, t_offset, len, waiting);
         mir_value_t zero = mir_const(g->mu, t_offset, 0);
         return vlog_offset_to_int(
            g, mir_build_select(g->mu, t_offset,
                                mir_build_cmp(g->mu, MIR_CMP_GT, avail, zero),
                                avail, zero));
      }
   case V_METHOD_MBOX_PUT:
      {
         vlog_node_t etype = vlog_type(type);
         const type_info_t *eti = vlog_type_info(g, etype);

         // Kept in a variable as registers do not survive suspending
         mir_value_t value = vlog_lower_with_context(g, vlog_param(v, 0),
                                                     eti->type);
         value = vlog_copy_value(g, vlog_lower_cast(g, eti->type, value),
                                 etype);
         mir_value_t saved = mir_add_var(g->mu, eti->type, eti->stamp,
                                         ident_uniq("put"), 0);
         mir_build_store(g->mu, saved, value);

         mir_block_t test_bb = mir_add_block(g->mu);
         mir_block_t room_bb = mir_add_block(g->mu);
         mir_block_t wait_bb = mir_add_block(g->mu);

         mir_build_jump(g->mu, test_bb);
         mir_set_cursor(g->mu, test_bb, MIR_APPEND);

         mir_value_t rec = mir_build_all(g->mu, vlog_lower_rvalue(g, prefix));
         mir_build_cond(g->mu, vlog_mailbox_has_room(g, rec), room_bb, wait_bb);

         mir_set_cursor(g->mu, wait_bb, MIR_APPEND);
         vlog_lower_queue_wait(g, mir_build_load(
                                  g->mu, mir_build_record_ref(g->mu, rec, 1)));
         mir_build_jump(g->mu, test_bb);

         mir_set_cursor(g->mu, room_bb, MIR_APPEND);
         mir_value_t rec2 = mir_build_all(g->mu, vlog_lower_rvalue(g, prefix));
         vlog_queue_push(g, mir_build_record_ref(g->mu, rec2, 0), eti,
                         mir_build_load(g->mu, saved), false);
         vlog_lower_event_trigger(g, mir_build_load(
                                     g->mu,
                                     mir_build_record_ref(g->mu, rec2, 1)));
         return MIR_NULL_VALUE;
      }
   case V_METHOD_MBOX_TRY_PUT:
      {
         vlog_node_t etype = vlog_type(type);
         const type_info_t *eti = vlog_type_info(g, etype);

         mir_value_t result = mir_add_var(g->mu, t_int, MIR_NULL_STAMP,
                                          ident_uniq("try_put"), 0);
         mir_build_store(g->mu, result, mir_const_vec(g->mu, t_int, 0, 0));

         mir_block_t room_bb = mir_add_block(g->mu);
         mir_block_t done_bb = mir_add_block(g->mu);

         mir_value_t rec = mir_build_all(g->mu, vlog_lower_rvalue(g, prefix));
         mir_build_cond(g->mu, vlog_mailbox_has_room(g, rec), room_bb, done_bb);

         mir_set_cursor(g->mu, room_bb, MIR_APPEND);

         mir_value_t value = vlog_lower_with_context(g, vlog_param(v, 0),
                                                     eti->type);
         value = vlog_copy_value(g, vlog_lower_cast(g, eti->type, value),
                                 etype);
         vlog_queue_push(g, mir_build_record_ref(g->mu, rec, 0), eti, value,
                         false);
         vlog_lower_event_trigger(g, mir_build_load(
                                     g->mu,
                                     mir_build_record_ref(g->mu, rec, 1)));
         mir_build_store(g->mu, result, mir_const_vec(g->mu, t_int, 1, 0));
         mir_build_jump(g->mu, done_bb);

         mir_set_cursor(g->mu, done_bb, MIR_APPEND);
         return mir_build_load(g->mu, result);
      }
   case V_METHOD_MBOX_PEEK:
      {
         mir_block_t test_bb = mir_add_block(g->mu);
         mir_block_t take_bb = mir_add_block(g->mu);
         mir_block_t wait_bb = mir_add_block(g->mu);

         mir_build_jump(g->mu, test_bb);
         mir_set_cursor(g->mu, test_bb, MIR_APPEND);

         mir_value_t rec = mir_build_all(g->mu, vlog_lower_rvalue(g, prefix));
         mir_value_t q = mir_build_load(g->mu,
                                        mir_build_record_ref(g->mu, rec, 0));
         mir_value_t len = mir_build_uarray_len(g->mu, q, 0);
         mir_value_t zero = mir_const(g->mu, t_offset, 0);
         mir_build_cond(g->mu, mir_build_cmp(g->mu, MIR_CMP_GT, len, zero),
                        take_bb, wait_bb);

         mir_set_cursor(g->mu, wait_bb, MIR_APPEND);
         vlog_lower_queue_wait(g, mir_build_load(
                                  g->mu, mir_build_record_ref(g->mu, rec, 1)));
         mir_build_jump(g->mu, test_bb);

         mir_set_cursor(g->mu, take_bb, MIR_APPEND);
         mir_value_t rec2 = mir_build_all(g->mu, vlog_lower_rvalue(g, prefix));
         mir_value_t q2 = mir_build_load(g->mu,
                                         mir_build_record_ref(g->mu, rec2, 0));
         mir_value_t front = mir_build_load(g->mu, mir_build_unwrap(g->mu, q2));
         vlog_assign_variable(g, vlog_param(v, 0), front, NULL);
         return MIR_NULL_VALUE;
      }
   case V_METHOD_MBOX_TRY_PEEK:
      {
         mir_value_t result = mir_add_var(g->mu, t_int, MIR_NULL_STAMP,
                                          ident_uniq("try_peek"), 0);
         mir_build_store(g->mu, result, mir_const_vec(g->mu, t_int, 0, 0));

         mir_block_t take_bb = mir_add_block(g->mu);
         mir_block_t done_bb = mir_add_block(g->mu);

         mir_value_t rec = mir_build_all(g->mu, vlog_lower_rvalue(g, prefix));
         mir_value_t q = mir_build_load(g->mu,
                                        mir_build_record_ref(g->mu, rec, 0));
         mir_value_t len = mir_build_uarray_len(g->mu, q, 0);
         mir_value_t zero = mir_const(g->mu, t_offset, 0);
         mir_build_cond(g->mu, mir_build_cmp(g->mu, MIR_CMP_GT, len, zero),
                        take_bb, done_bb);

         mir_set_cursor(g->mu, take_bb, MIR_APPEND);
         mir_value_t front = mir_build_load(g->mu, mir_build_unwrap(g->mu, q));
         vlog_assign_variable(g, vlog_param(v, 0), front, NULL);
         mir_build_store(g->mu, result, mir_const_vec(g->mu, t_int, 1, 0));
         mir_build_jump(g->mu, done_bb);

         mir_set_cursor(g->mu, done_bb, MIR_APPEND);
         return mir_build_load(g->mu, result);
      }
   case V_METHOD_MBOX_GET:
      {
         mir_type_t t_cell = mir_int_type(g->mu, INT64_MIN, INT64_MAX);

         mir_block_t test_bb = mir_add_block(g->mu);
         mir_block_t take_bb = mir_add_block(g->mu);
         mir_block_t wait_bb = mir_add_block(g->mu);

         mir_value_t rec0 = mir_build_all(g->mu, vlog_lower_rvalue(g, prefix));
         mir_value_t waiting = mir_build_record_ref(g->mu, rec0, 3);
         mir_build_store(g->mu, waiting,
                         mir_build_add(g->mu, t_cell,
                                       mir_build_load(g->mu, waiting),
                                       mir_const(g->mu, t_cell, 1)));

         mir_build_jump(g->mu, test_bb);
         mir_set_cursor(g->mu, test_bb, MIR_APPEND);

         mir_value_t rec = mir_build_all(g->mu, vlog_lower_rvalue(g, prefix));
         mir_value_t qs = mir_build_record_ref(g->mu, rec, 0);
         mir_value_t len = mir_build_uarray_len(g->mu,
                                                mir_build_load(g->mu, qs), 0);
         mir_value_t zero = mir_const(g->mu, t_offset, 0);
         mir_build_cond(g->mu, mir_build_cmp(g->mu, MIR_CMP_GT, len, zero),
                        take_bb, wait_bb);

         mir_set_cursor(g->mu, wait_bb, MIR_APPEND);
         vlog_lower_queue_wait(g, mir_build_load(
                                  g->mu, mir_build_record_ref(g->mu, rec, 1)));
         mir_build_jump(g->mu, test_bb);

         mir_set_cursor(g->mu, take_bb, MIR_APPEND);
         mir_value_t rec2 = mir_build_all(g->mu, vlog_lower_rvalue(g, prefix));
         mir_value_t waiting2 = mir_build_record_ref(g->mu, rec2, 3);
         mir_build_store(g->mu, waiting2,
                         mir_build_sub(g->mu, t_cell,
                                       mir_build_load(g->mu, waiting2),
                                       mir_const(g->mu, t_cell, 1)));
         mir_value_t value =
            vlog_queue_pop(g, mir_build_record_ref(g->mu, rec2, 0),
                           vlog_type_info(g, vlog_type(type)), true);
         vlog_lower_event_trigger(g, mir_build_load(
                                     g->mu,
                                     mir_build_record_ref(g->mu, rec2, 1)));
         vlog_assign_variable(g, vlog_param(v, 0), value, NULL);
         return MIR_NULL_VALUE;
      }
   case V_METHOD_MBOX_TRY_GET:
      {
         mir_value_t result = mir_add_var(g->mu, t_int, MIR_NULL_STAMP,
                                          ident_uniq("try_get"), 0);
         mir_build_store(g->mu, result, mir_const_vec(g->mu, t_int, 0, 0));

         mir_block_t take_bb = mir_add_block(g->mu);
         mir_block_t done_bb = mir_add_block(g->mu);

         mir_value_t rec = mir_build_all(g->mu, vlog_lower_rvalue(g, prefix));
         mir_value_t qs = mir_build_record_ref(g->mu, rec, 0);
         mir_value_t len = mir_build_uarray_len(g->mu,
                                                mir_build_load(g->mu, qs), 0);
         mir_value_t waiting = mir_build_cast(
            g->mu, t_offset,
            mir_build_load(g->mu, mir_build_record_ref(g->mu, rec, 3)));
         mir_build_cond(g->mu, mir_build_cmp(g->mu, MIR_CMP_GT, len, waiting),
                        take_bb, done_bb);

         mir_set_cursor(g->mu, take_bb, MIR_APPEND);
         mir_value_t value = vlog_queue_pop(g, qs,
                                            vlog_type_info(g, vlog_type(type)),
                                            true);
         vlog_lower_event_trigger(g, mir_build_load(
                                     g->mu,
                                     mir_build_record_ref(g->mu, rec, 1)));
         vlog_assign_variable(g, vlog_param(v, 0), value, NULL);
         mir_build_store(g->mu, result, mir_const_vec(g->mu, t_int, 1, 0));
         mir_build_jump(g->mu, done_bb);

         mir_set_cursor(g->mu, done_bb, MIR_APPEND);
         return mir_build_load(g->mu, result);
      }
   default:
      fatal_at(vlog_loc(v), "sorry, this built-in method is not supported");
   }
}

static mir_value_t vlog_lower_method_call(vlog_gen_t *g, vlog_node_t v)
{
   vlog_node_t prefix = vlog_value(v);

   if (vlog_kind(prefix) == V_HIER_REF && vlog_subkind(v) != V_METHOD_CLASS
       && (!vlog_has_ref(prefix) || vlog_kind(vlog_ref(prefix)) == V_FUNC_DECL
           || vlog_kind(vlog_ref(prefix)) == V_TASK_DECL)) {
      // Subroutine in another instance
      if (!vlog_has_ref(prefix))
         fatal_at(vlog_loc(v), "unbound hierarchical reference");

      vlog_node_t decl = vlog_ref(prefix);

      mir_type_t t_ctx = mir_context_type(g->mu, vlog_ident2(decl));
      mir_value_t ptr = vlog_bind_hier(g, prefix, t_ctx, MIR_NULL_STAMP);
      return vlog_lower_subr_call(g, decl, v, mir_build_load(g->mu, ptr), true);
   }

   switch (vlog_subkind(v)) {
   case V_METHOD_RANDOMIZE:
      return vlog_lower_randomize(g, v);
   case V_METHOD_RAND_MODE:
   case V_METHOD_CONSTRAINT_MODE:
      return vlog_lower_crv_mode(g, v);
   case V_METHOD_CLASS:
      break;
   default:
      return vlog_lower_builtin_method(g, v);
   }

   vlog_node_t decl = vlog_method_decl(v);

   vlog_node_t ptype = vlog_get_type(prefix);
   if (ptype != NULL && vlog_kind(ptype) == V_DATA_TYPE
       && vlog_subkind(ptype) == DT_VIRTUAL) {
      // The method of the first instance is called with the context of
      // the instance selected by the handle
      vlog_node_t iface = vlog_virtual_iface(ptype);
      vlog_node_t body = vlog_virtual_spec(ptype);
      if (body == NULL && sv_iface_bodies != NULL)
         body = hash_get(sv_iface_bodies, vlog_ident2(iface));

      if (body == NULL)
         fatal_at(vlog_loc(v), "virtual interface %pQ has no instance in "
                  "the design", vlog_ident2(iface));

      vlog_node_t idecl = NULL;
      const int ndecls = vlog_decls(body);
      for (int i = 0; idecl == NULL && i < ndecls; i++) {
         vlog_node_t d = vlog_decl(body, i);
         if (vlog_kind(d) == vlog_kind(decl)
             && vlog_ident(d) == vlog_ident(decl))
            idecl = d;
      }

      assert(idecl != NULL);

      mir_value_t context = vlog_lower_rvalue(g, prefix);
      return vlog_lower_subr_call(g, idecl, v, context, true);
   }

   const bool is_super =
      vlog_kind(prefix) == V_THIS && (vlog_flags(prefix) & VLOG_F_STATIC);

   return vlog_lower_subr_call(g, decl, v, vlog_lower_rvalue(g, prefix),
                               is_super);
}

static bool vlog_method_is_virtual(vlog_node_t decl)
{
   if (vlog_flags(decl) & VLOG_F_VIRTUAL)
      return true;

   vlog_node_t class = vlog_ref(decl);
   if (!vlog_has_ref(class))
      return false;

   vlog_node_t over = vlog_class_member(vlog_ref(class), vlog_ident(decl));
   return over != NULL && vlog_kind(over) == vlog_kind(decl)
      && vlog_method_is_virtual(over);
}

static mir_value_t vlog_lower_array_arg(vlog_gen_t *g, vlog_node_t port,
                                         vlog_node_t actual,
                                         const type_info_t *eti,
                                         v_port_kind_t dir, bool *copy_back)
{
   // The elements of an unpacked array argument are passed by address

   vlog_node_t dims[MAX_UNPACKED_DIMS];
   const int ndims = MIN(vlog_ranges(port), MAX_UNPACKED_DIMS);
   for (int i = 0; i < ndims; i++)
      dims[i] = vlog_range(port, i);

   const unsigned count = vlog_unpacked_count(dims, ndims);
   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_value_t ncount = mir_const(g->mu, t_offset, count);

   const mir_class_t eclass = mir_get_class(g->mu, eti->type);
   if (eclass != MIR_TYPE_VEC2 && eclass != MIR_TYPE_VEC4)
      fatal_at(vlog_loc(port), "sorry, unpacked array arguments with this "
               "element type are not supported");

   vlog_node_t adims[MAX_UNPACKED_DIMS];
   const int andims = vlog_kind(actual) == V_PATTERN_EXPR
      ? 0 : vlog_unpacked_dims(actual, adims);

   if (dir == V_PORT_REF) {
      if (andims > 0 && vlog_unpacked_count(adims, andims) == count) {
         vlog_select_t sel = vlog_lower_select(g, actual);
         if (mir_points_to_vector(g->mu, sel.obj))
            return sel.obj;

         // An array held in a signal is copied in and out
         *copy_back = true;
      }
      else
         fatal_at(vlog_loc(actual), "sorry, only a variable which is an "
                  "unpacked array can be passed to a ref argument which is "
                  "an unpacked array");
   }

   mir_type_t t_array = mir_carray_type(g->mu, count, eti->type);
   mir_value_t tmp = mir_add_var(
      g->mu, t_array, eti->stamp, ident_uniq("%s.tmp", istr(vlog_ident(port))),
      0);

   mir_value_t base = mir_build_array_ref(
      g->mu, tmp, mir_const(g->mu, t_offset, 0));

   if (dir == V_PORT_OUTPUT) {
      mir_build_set(g->mu, tmp, vlog_lower_default_value(g, eti), ncount);
      return base;
   }

   if (vlog_kind(actual) == V_PATTERN_EXPR) {
      mir_value_t flat = vlog_lower_array_pattern(g, actual, dims, ndims, eti);
      vlog_store_flat_array(g, tmp, count, eti, flat);
      return base;
   }

   if (vlog_kind(actual) == V_CONCAT && ndims == 1) {
      // Unpacked array concatenation of elements and fixed size arrays
      const int nparts = vlog_params(actual);
      unsigned total = 0;
      for (int i = 0; i < nparts; i++) {
         vlog_node_t pdims[MAX_UNPACKED_DIMS];
         const int npd = vlog_unpacked_dims(vlog_param(actual, i), pdims);
         total += npd == 1 ? vlog_unpacked_count(pdims, 1) : 1;
      }

      if (total == count) {
         unsigned pos = 0;
         for (int i = 0; i < nparts; i++) {
            vlog_node_t p = vlog_param(actual, i);
            mir_value_t dst = mir_build_array_ref(
               g->mu, tmp, mir_const(g->mu, t_offset, pos));

            vlog_node_t pdims[MAX_UNPACKED_DIMS];
            if (vlog_unpacked_dims(p, pdims) == 1) {
               const unsigned n = vlog_unpacked_count(pdims, 1);
               vlog_select_t sel = vlog_lower_select(g, p);
               if (mir_points_to_vector(g->mu, sel.obj))
                  mir_build_copy(g->mu, dst, sel.obj,
                                 mir_const(g->mu, t_offset, n));
               else {
                  mir_type_t t_flat = mir_vec4_type(g->mu, n * eti->size, false);
                  vlog_store_flat_array(
                     g, dst, n, eti,
                     vlog_lower_cast(g, t_flat, vlog_lower_rvalue(g, p)));
               }
               pos += n;
            }
            else {
               mir_value_t value = vlog_lower_with_context(
                  g, p, vlog_assign_context(g, eti->type, p));
               mir_build_store(g->mu, dst, vlog_lower_cast(g, eti->type, value));
               pos++;
            }
         }

         return base;
      }
   }

   if (andims > 0 && vlog_unpacked_count(adims, andims) == count) {
      vlog_select_t sel = vlog_lower_select(g, actual);
      if (mir_points_to_vector(g->mu, sel.obj))
         mir_build_copy(g->mu, base, sel.obj, ncount);
      else {
         mir_type_t t_flat = mir_vec4_type(g->mu, count * eti->size, false);
         vlog_store_flat_array(
            g, tmp, count, eti,
            vlog_lower_cast(g, t_flat, vlog_lower_rvalue(g, actual)));
      }
      return base;
   }

   mir_value_t value = vlog_lower_rvalue(g, actual);
   if (mir_get_class(g->mu, mir_get_type(g->mu, value)) != MIR_TYPE_UARRAY)
      fatal_at(vlog_loc(actual), "sorry, this value cannot be passed to an "
               "unpacked array argument");

   mir_value_t len = mir_build_uarray_len(g->mu, value, 0);
   mir_block_t ok_bb = mir_add_block(g->mu);
   mir_block_t bad_bb = mir_add_block(g->mu);
   mir_build_cond(g->mu, mir_build_cmp(g->mu, MIR_CMP_EQ, len, ncount),
                  ok_bb, bad_bb);

   mir_set_cursor(g->mu, bad_bb, MIR_APPEND);
   mir_build_unreachable(g->mu, mir_build_debug_locus(g->mu,
                                                      vlog_to_object(actual)));

   mir_set_cursor(g->mu, ok_bb, MIR_APPEND);
   mir_build_copy(g->mu, base, mir_build_unwrap(g->mu, value), ncount);
   return base;
}

static void vlog_store_array_arg(vlog_gen_t *g, vlog_node_t target,
                                 mir_value_t tmp)
{
   // Copy the elements of an unpacked array output argument back

   vlog_node_t dims[MAX_UNPACKED_DIMS];
   const int ndims = vlog_unpacked_dims(target, dims);
   if (ndims == 0)
      fatal_at(vlog_loc(target), "sorry, an unpacked array output argument "
               "must be connected to an unpacked array");

   const unsigned count = vlog_unpacked_count(dims, ndims);
   const type_info_t *eti = vlog_array_elem_info(g, target);

   g->for_write = true;
   vlog_select_t sel = vlog_lower_select(g, target);
   g->for_write = false;

   if (mir_points_to_vector(g->mu, sel.obj)) {
      mir_build_copy(g->mu, sel.obj, tmp,
                     mir_const(g->mu, mir_offset_type(g->mu), count));
      return;
   }

   mir_value_t flat = vlog_load_flat_array(g, tmp, count, eti, false);
   vlog_assign_variable(g, target, flat, NULL);
}

static mir_value_t vlog_lower_subr_call(vlog_gen_t *g, vlog_node_t decl,
                                        vlog_node_t call, mir_value_t this_val,
                                        bool nonvirtual)
{
   const vlog_kind_t kind = vlog_kind(decl);
   const bool is_ctor = kind == V_CONSTRUCTOR;

   const bool thunk = mir_get_kind(g->mu) == MIR_UNIT_THUNK || g->constfunc;

   ident_t func = vlog_subr_name(decl);
   if (thunk) {
      func = ident_sprintf("%s@const", istr(func));

      // A function of the compilation unit or a package is not among the
      // declarations of the instance being folded
      if (kind == V_FUNC_DECL)
         mir_defer(mir_get_context(g->mu), func, NULL, MIR_UNIT_FUNCTION,
                   vlog_lower_func_decl, vlog_to_object(decl));
   }

   const int nports = is_ctor ? vlog_params(decl) : vlog_ports(decl);
   const int nparams = call != NULL ? vlog_params(call) : 0;

   mir_value_t *args LOCAL = xmalloc_array(nports + 1, sizeof(mir_value_t));

   copyback_t *back LOCAL = xmalloc_array(nports + 1, sizeof(copyback_t));
   int nback = 0, pos = 0;

   mir_value_t context = this_val;
   if (mir_is_null(context))
      context = vlog_context_for_call(g, decl);

   if (!mir_is_null(context))
      args[pos++] = context;

   const mir_value_t saved_this = g->this_val;

   const bool can_sigref = !thunk && !is_ctor && kind == V_TASK_DECL
      && !(vlog_has_ref(decl) && vlog_method_is_virtual(decl) && !nonvirtual);
   uint32_t sigref_mask = 0;

   mir_value_t *argvars LOCAL = xmalloc_array(nports + 1, sizeof(mir_value_t));
   bool any_default = false;
   for (int i = 0; i <= nports; i++) {
      argvars[i] = MIR_NULL_VALUE;
      if (!is_ctor && i < nports && i > 0 && vlog_has_value(vlog_port(decl, i)))
         any_default = true;
   }

   for (int i = 0; i < nports; i++) {
      vlog_node_t port = is_ctor ? vlog_param(decl, i) : vlog_port(decl, i);
      const bool is_open = !is_ctor && vlog_dpi_open_port(decl, port);
      const type_info_t *ti = is_open ? NULL
         : vlog_type_info(g, vlog_type(port));
      const v_port_kind_t dir = vlog_subkind(port);

      vlog_node_t actual = NULL;
      for (int j = 0; j < nparams; j++) {
         // Arguments by name follow those by position
         vlog_node_t p = vlog_param(call, j);
         if (vlog_kind(p) != V_NAMED_ARG) {
            if (j == i)
               actual = p;
         }
         else if (vlog_ident(p) == vlog_ident(port)) {
            if (vlog_has_value(p))
               actual = vlog_value(p);
            break;
         }
      }

      if (actual != NULL && vlog_kind(actual) == V_EMPTY)
         actual = NULL;

      if (actual == NULL) {
         if (!vlog_has_value(port))
            fatal_at(call != NULL ? vlog_loc(call) : vlog_loc(decl),
                     "missing value for argument %pQ", vlog_ident(port));

         // The default is evaluated in the scope of the subroutine
         actual = vlog_value(port);
         if (!is_ctor && i > 0) {
            // It can use the arguments before it
            bind_defaults_t b = {
               .g = g, .subr = decl, .argvars = argvars, .nbound = i
            };
            vlog_visit(actual, vlog_bind_default_cb, &b);
         }

         if (!mir_is_null(this_val))
            g->this_val = this_val;
      }


      if (is_open) {
         args[pos++] = vlog_lower_dpi_open_arg(g, port, actual, back, &nback);
         g->this_val = saved_this;
         continue;
      }

      if (vlog_is_const_ref(port) && !vlog_is_fixed_unpacked(port)) {
         args[pos++] = vlog_lower_ref_cell(g, actual, ti);
         g->this_val = saved_this;
         continue;
      }

      if (!is_ctor && vlog_is_fixed_unpacked(port)) {
         bool copy_back = dir == V_PORT_OUTPUT || dir == V_PORT_INOUT;
         mir_value_t array = vlog_lower_array_arg(g, port, actual, ti, dir,
                                                  &copy_back);
         args[pos++] = array;
         if (copy_back)
            back[nback++] = (copyback_t){ array, actual, true };

         g->this_val = saved_this;
         continue;
      }

      if (dir == V_PORT_REF && can_sigref && i < 32
          && (vlog_kind(actual) == V_REF || vlog_kind(actual) == V_BIT_SELECT)
          && !mir_is_null(ti->signal)) {
         // A variable held in a signal is passed as a signal so that writes
         // by other processes are seen while the task waits
         vlog_select_t sel = vlog_lower_select(g, actual);
         if (mir_is_signal(g->mu, sel.obj) && sel.size == ti->size) {
            mir_value_t sig = mir_build_array_ref(g->mu, sel.obj,
                                                  sel.dst_offset);
            if (mir_equals(mir_get_type(g->mu, sig), ti->signal)) {
               args[pos++] = sig;
               sigref_mask |= UINT32_C(1) << i;
               g->this_val = saved_this;
               continue;
            }
         }
      }

      if (dir == V_PORT_INPUT) {
         vlog_check_virtual_types(vlog_get_type(port), vlog_get_type(actual),
                                  vlog_loc(actual));

         mir_value_t value = vlog_lower_with_context(g, actual, vlog_assign_context(g, ti->type, actual));
         if (mir_get_class(g->mu, ti->type) == MIR_TYPE_UARRAY
             && mir_is_vector(g->mu, value)) {
            vlog_node_t ptype = vlog_get_type(port);
            if (vlog_kind(ptype) != V_DATA_TYPE
                || (vlog_subkind(ptype) != DT_DYNARRAY
                    && vlog_subkind(ptype) != DT_QUEUE))
               fatal_at(vlog_loc(actual), "sorry, an integral value cannot be "
                        "passed to a string argument");

            value = vlog_fixed_to_dynamic(g, ptype, actual, value);
         }
         else if (mir_get_class(g->mu, ti->type) == MIR_TYPE_UARRAY
                  && !mir_is(g->mu, value, MIR_TYPE_UARRAY)
                  && vlog_is_real_unpacked(g, actual))
            value = vlog_real_unpacked_to_dynamic(g, actual);
         mir_value_t cast = vlog_lower_cast(g, ti->type, value);
         args[pos] = vlog_copy_value(g, cast, vlog_type(port));

         if (any_default && !is_ctor) {
            argvars[i] = mir_add_var(g->mu, mir_get_type(g->mu, args[pos]),
                                     MIR_NULL_STAMP, ident_uniq("arg"),
                                     MIR_VAR_TEMP);
            mir_build_store(g->mu, argvars[i], args[pos]);
         }

         pos++;
      }
      else {
         bool direct = false;
         if (dir == V_PORT_REF && vlog_kind(actual) != V_NUMBER) {
            // An element of an associative array is created if missing
            g->for_write = true;
            vlog_select_t sel = vlog_lower_select(g, actual);
            g->for_write = false;
            if (mir_is(g->mu, sel.obj, MIR_TYPE_POINTER)
                && !mir_points_to(g->mu, sel.obj, MIR_TYPE_SIGNAL)
                && mir_equals(mir_get_pointer(g->mu,
                                              mir_get_type(g->mu, sel.obj)),
                              ti->type)) {
               args[pos++] = sel.obj;
               direct = true;
            }
         }

         if (!direct) {
            // Copy in and out of a temporary
            ident_t name = ident_uniq("%s.tmp", istr(vlog_ident(port)));
            mir_value_t tmp = mir_add_var(g->mu, ti->type, ti->stamp, name, 0);

            mir_value_t init;
            if (dir == V_PORT_OUTPUT)
               init = vlog_lower_default_value(g, ti);
            else {
               mir_value_t value = vlog_lower_with_context(g, actual, vlog_assign_context(g, ti->type, actual));
               init = vlog_lower_cast(g, ti->type, value);
            }

            mir_build_store(g->mu, tmp, init);

            args[pos++] = tmp;
            back[nback++] = (copyback_t){ tmp, actual, false };
         }
      }

      g->this_val = saved_this;
   }

   if (!thunk && !is_ctor && call != NULL && !mir_is_null(this_val)
       && !(vlog_has_ref(decl) && (vlog_flags(decl) & VLOG_F_STATIC)))
      mir_build_null_check(g->mu, this_val, mir_build_debug_locus(
                              g->mu, vlog_to_object(call)));

   if (!thunk && !nonvirtual && !is_ctor && vlog_has_ref(decl)
       && vlog_method_is_virtual(decl)) {
      // Dispatched on the dynamic type of the object

      ident_t name = ident_sprintf("%s$v", istr(func));
      mir_defer(g->mir, name, NULL, kind == V_TASK_DECL
                ? MIR_UNIT_PROCEDURE : MIR_UNIT_FUNCTION,
                vlog_lower_dispatch, vlog_to_object(decl));
      func = name;
   }

   if (sigref_mask != 0) {
      ident_t base = func;
      func = ident_sprintf("%s$sr%x", istr(base), sigref_mask);
      mir_defer(g->mir, func, vlog_ident2(decl), MIR_UNIT_PROCEDURE,
                vlog_lower_task_decl, vlog_to_object(decl));
   }

   mir_value_t result = MIR_NULL_VALUE;
   if (kind == V_TASK_DECL) {
      mir_block_t resume_bb = mir_add_block(g->mu);
      mir_build_pcall(g->mu, func, resume_bb, args, pos);

      mir_set_cursor(g->mu, resume_bb, MIR_APPEND);
      mir_build_resume(g->mu, func);
   }
   else {
      mir_type_t rtype = mir_offset_type(g->mu);
      if (!is_ctor && vlog_has_type(decl))
         rtype = vlog_type_info(g, vlog_type(decl))->type;

      result = mir_build_fcall(g->mu, func, rtype, MIR_NULL_STAMP, args, pos);
      mir_build_consume(g->mu, result);
   }

   for (int i = 0; i < nback; i++) {
      if (back[i].array) {
         vlog_store_array_arg(g, back[i].target, back[i].tmp);
         continue;
      }

      mir_value_t value = mir_build_load(g->mu, back[i].tmp);
      vlog_assign_variable(g, back[i].target, value, NULL);
   }

   return result;
}

static void vlog_lower_user_tcall(vlog_gen_t *g, vlog_node_t v)
{
   vlog_node_t decl = vlog_ref(v);

   mir_value_t this_val = MIR_NULL_VALUE;
   if (vlog_has_ref(decl) && !(vlog_flags(decl) & VLOG_F_STATIC))
      this_val = vlog_lower_this(g);

   vlog_lower_subr_call(g, decl, v, this_val, false);
}

static bool vlog_is_deferred_prop(vlog_node_t d)
{
   // The explicit initial value of a property is assigned after the base
   // class constructor returns (8.7)
   return vlog_kind(d) == V_VAR_DECL && !(vlog_flags(d) & VLOG_F_STATIC)
      && vlog_has_value(d) && !vlog_is_ref_property(d)
      && !vlog_is_fixed_unpacked(d);
}

static void vlog_lower_prop_inits(vlog_gen_t *g, vlog_node_t class,
                                  vlog_node_t stop, mir_value_t this_val)
{
   // Initialise the properties of each class from the one below stop
   // down to class, in that order

   if (vlog_has_ref(class) && vlog_ref(class) != stop)
      vlog_lower_prop_inits(g, vlog_ref(class), stop, this_val);

   const int ndecls = vlog_decls(class);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(class, i);
      if (!vlog_is_deferred_prop(d))
         continue;

      const type_info_t *ti = vlog_type_info(g, vlog_type(d));
      ident_t name = vlog_prop_name_in(class, d);
      mir_value_t ptr = mir_build_link_var(g->mu, this_val, name, ti->type);
      vlog_store_local_init(g, d, ptr);
   }
}

static bool vlog_has_deferred_props(vlog_node_t class, vlog_node_t stop)
{
   for (vlog_node_t c = class; c != NULL && c != stop;
        c = vlog_has_ref(c) ? vlog_ref(c) : NULL) {
      const int ndecls = vlog_decls(c);
      for (int i = 0; i < ndecls; i++) {
         if (vlog_is_deferred_prop(vlog_decl(c, i)))
            return true;
      }
   }

   return false;
}

static void vlog_bind_props(vlog_gen_t *g, vlog_node_t class, mir_value_t obj,
                            mir_value_t *saved, int *pos, bool restore)
{
   // Make the properties of the new object visible by name while its
   // initial values are evaluated outside the class

   if (vlog_has_ref(class))
      vlog_bind_props(g, vlog_ref(class), obj, saved, pos, restore);

   const int ndecls = vlog_decls(class);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(class, i);
      if (vlog_kind(d) != V_VAR_DECL || (vlog_flags(d) & VLOG_F_STATIC)
          || vlog_is_ref_property(d))
         continue;

      const int n = (*pos)++;
      if (restore) {
         mir_put_object(g->mu, d, saved[n]);
         continue;
      }

      int hops;
      mir_value_t old = mir_search_object(g->mu, d, &hops);
      saved[n] = hops == 0 ? old : MIR_NULL_VALUE;

      const type_info_t *ti = vlog_type_info(g, vlog_type(d));
      mir_type_t type = ti->type;
      if (vlog_is_fixed_unpacked(d) && vlog_size(d) > 1)
         type = mir_carray_type(g->mu, vlog_size(d), ti->type);

      mir_put_object(g->mu, d, mir_build_link_var(
                        g->mu, obj, vlog_prop_name_in(class, d), type));
   }
}

static int vlog_count_props(vlog_node_t class)
{
   int count = 0;
   for (vlog_node_t c = class; c != NULL;
        c = vlog_has_ref(c) ? vlog_ref(c) : NULL) {
      const int ndecls = vlog_decls(c);
      for (int i = 0; i < ndecls; i++)
         count += vlog_kind(vlog_decl(c, i)) == V_VAR_DECL;
   }
   return count;
}

static vlog_node_t vlog_find_ctor(vlog_node_t class)
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

static void vlog_copy_properties(vlog_gen_t *g, vlog_node_t class,
                                 mir_value_t dst, mir_value_t src)
{
   if (vlog_has_ref(class))
      vlog_copy_properties(g, vlog_ref(class), dst, src);

   const int ndecls = vlog_decls(class);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(class, i);
      if (vlog_kind(d) != V_VAR_DECL || (vlog_flags(d) & VLOG_F_STATIC))
         continue;

      const type_info_t *fti = vlog_type_info(g, vlog_type(d));
      ident_t name = vlog_prop_name_in(class, d);

      if (vlog_is_fixed_unpacked(d) && vlog_size(d) > 1) {
         if (vlog_needs_copy(vlog_type(d)))
            fatal_at(vlog_loc(d), "sorry, copying an object with a fixed size "
                     "array property of strings, queues or structures is "
                     "not supported");

         mir_type_t t_offset = mir_offset_type(g->mu);
         mir_type_t t_array = mir_carray_type(g->mu, vlog_size(d), fti->type);
         mir_value_t zero = mir_const(g->mu, t_offset, 0);
         mir_value_t dp = mir_build_array_ref(
            g->mu, mir_build_link_var(g->mu, dst, name, t_array), zero);
         mir_value_t sp = mir_build_array_ref(
            g->mu, mir_build_link_var(g->mu, src, name, t_array), zero);
         mir_build_copy(g->mu, dp, sp,
                        mir_const(g->mu, t_offset, vlog_size(d)));
         continue;
      }

      if (vlog_is_ref_property(d)) {
         mir_type_t t_ref = vlog_ref_cell_type(g->mu, fti);
         mir_value_t dp = mir_build_link_var(g->mu, dst, name, t_ref);
         mir_value_t sp = mir_build_link_var(g->mu, src, name, t_ref);
         mir_build_store(g->mu, dp, mir_build_load(g->mu, sp));
         continue;
      }

      mir_value_t dp = mir_build_link_var(g->mu, dst, name, fti->type);
      mir_value_t sp = mir_build_link_var(g->mu, src, name, fti->type);
      mir_value_t value = mir_build_load(g->mu, sp);
      mir_build_store(g->mu, dp, vlog_copy_value(g, value, vlog_type(d)));
   }
}

static mir_value_t vlog_lower_class_new(vlog_gen_t *g, vlog_node_t v)
{
   if (!vlog_has_type(v))
      fatal_at(vlog_loc(v), "cannot determine the type of this new expression");

   vlog_node_t type = vlog_type(v);
   const type_info_t *ti = vlog_type_info(g, type);

   if (vlog_kind(type) == V_DATA_TYPE) {
      // Mailbox
      mir_type_t t_rec = mir_get_elem(g->mu, ti->type);
      mir_value_t acc = mir_build_new(g->mu, t_rec, MIR_NULL_STAMP,
                                      MIR_NULL_VALUE);
      mir_value_t rec = mir_build_all(g->mu, acc);

      const type_info_t *eti = vlog_type_info(g, vlog_type(type));
      mir_type_t t_q = mir_uarray_type(g->mu, 1, eti->type);
      mir_build_store(g->mu, mir_build_record_ref(g->mu, rec, 0),
                      vlog_empty_array(g, t_q));

      mir_type_t t_cell = mir_int_type(g->mu, INT64_MIN, INT64_MAX);
      mir_build_store(g->mu, mir_build_record_ref(g->mu, rec, 1),
                      mir_build_new(g->mu, t_cell, MIR_NULL_STAMP,
                                    MIR_NULL_VALUE));

      mir_value_t bound = mir_const(g->mu, t_cell, 0);
      if (vlog_params(v) > 0)
         bound = mir_build_cast(
            g->mu, t_cell, vlog_lower_cast(g, mir_offset_type(g->mu),
                                           vlog_lower_rvalue(g, vlog_param(v, 0))));
      mir_build_store(g->mu, mir_build_record_ref(g->mu, rec, 2), bound);
      mir_build_store(g->mu, mir_build_record_ref(g->mu, rec, 3),
                      mir_const(g->mu, t_cell, 0));
      mir_build_store(g->mu, mir_build_record_ref(g->mu, rec, 4),
                      mir_const(g->mu, t_cell, 0));
      mir_build_store(g->mu, mir_build_record_ref(g->mu, rec, 5),
                      mir_const(g->mu, t_cell, 0));

      return acc;
   }

   vlog_note_spec(type);

   mir_value_t parent;
   if (vlog_has_ident2(type) && vlog_find_scope(vlog_ident2(type)) != NULL)
      parent = mir_build_null(g->mu, ti->type);
   else
      parent = vlog_scope_context(g, vlog_ident2(type));

   if (vlog_has_value(v)) {
      // Shallow copy of the properties of another object
      mir_value_t src = vlog_lower_rvalue(g, vlog_value(v));
      mir_build_null_check(g->mu, src, mir_build_debug_locus(
                              g->mu, vlog_to_object(v)));

      // The copy has the dynamic type of the original
      vlog_node_t clone = vlog_class_member(type, ident_new("__cl"));
      if (clone != NULL && (g->subr == NULL
                            || vlog_ident(g->subr) != ident_new("__cl")))
         return vlog_lower_subr_call(g, clone, NULL, src, false);

      mir_value_t obj = mir_build_protected_init(
         g->mu, ti->type, parent, MIR_NULL_VALUE, MIR_NULL_VALUE);
      vlog_copy_properties(g, type, obj, src);
      return obj;
   }

   mir_value_t obj = mir_build_protected_init(g->mu, ti->type, parent,
                                              MIR_NULL_VALUE, MIR_NULL_VALUE);

   vlog_node_t ctor = vlog_find_ctor(type);
   if (ctor != NULL)
      vlog_lower_subr_call(g, ctor, v, obj, true);

   // Classes below the one whose constructor ran have no constructor of
   // their own so their properties are initialised here
   vlog_node_t stop = ctor != NULL ? vlog_ref(ctor) : NULL;
   if (stop != type && vlog_has_deferred_props(type, stop)) {
      mir_value_t *saved LOCAL =
         xmalloc_array(vlog_count_props(type) + 1, sizeof(mir_value_t));
      int pos = 0;
      vlog_bind_props(g, type, obj, saved, &pos, false);

      mir_value_t saved_this = g->this_val;
      g->this_val = obj;
      vlog_lower_prop_inits(g, type, stop, obj);
      g->this_val = saved_this;

      pos = 0;
      vlog_bind_props(g, type, obj, saved, &pos, true);
   }

   return obj;
}

static mir_value_t vlog_lower_dynamic_new(vlog_gen_t *g, vlog_node_t v)
{
   vlog_node_t type = vlog_type(v);
   if (vlog_kind(type) != V_DATA_TYPE || !vlog_has_type(type))
      fatal_at(vlog_loc(v), "new[] requires a dynamic array type");

   const type_info_t *eti = vlog_type_info(g, vlog_type(type));
   mir_type_t t_offset = mir_offset_type(g->mu);

   mir_value_t count =
      vlog_lower_cast(g, t_offset, vlog_lower_rvalue(g, vlog_left(v)));
   mir_value_t data = vlog_alloc_array(g, eti->type, count);

   mir_value_t start = mir_const(g->mu, t_offset, 0);

   if (vlog_has_value(v)) {
      // Keep as many of the existing elements as will fit
      vlog_node_t dims[MAX_UNPACKED_DIMS];
      mir_value_t old;
      if (vlog_is_real_unpacked(g, vlog_value(v)))
         old = vlog_real_unpacked_to_dynamic(g, vlog_value(v));
      else if (vlog_unpacked_dims(vlog_value(v), dims) > 0) {
         old = vlog_lower_rvalue(g, vlog_value(v));
         if (mir_is_vector(g->mu, old))
            old = vlog_fixed_to_dynamic(g, type, vlog_value(v), old);
      }
      else
         old = vlog_lower_with_context(g, vlog_value(v), eti->type);
      mir_value_t old_len = mir_build_uarray_len(g->mu, old, 0);
      mir_value_t fits = mir_build_cmp(g->mu, MIR_CMP_LT, old_len, count);
      start = mir_build_select(g->mu, t_offset, fits, old_len, count);
      mir_build_copy(g->mu, data, mir_build_unwrap(g->mu, old), start);
   }

   vlog_init_elements(g, data, start, count, eti);

   return vlog_wrap_array(g, data, count);
}

static void vlog_lower_implicit_super(vlog_gen_t *g, vlog_node_t ctor)
{
   vlog_node_t class = vlog_ref(ctor);
   if (!vlog_has_ref(class)) {
      vlog_lower_prop_inits(g, class, NULL, vlog_lower_this(g));
      return;
   }

   if (vlog_stmts(ctor) > 0 && vlog_kind(vlog_stmt(ctor, 0)) == V_SUPER_CALL)
      return;

   vlog_node_t base = vlog_find_ctor(vlog_ref(class));
   if (base != NULL)
      vlog_lower_subr_call(g, base, vlog_params(class) > 0 ? class : NULL,
                           vlog_lower_this(g), true);

   vlog_lower_prop_inits(g, class, base != NULL ? vlog_ref(base) : NULL,
                         vlog_lower_this(g));
}

static void vlog_lower_super_call(vlog_gen_t *g, vlog_node_t v)
{
   vlog_node_t class = vlog_ref(g->subr);
   vlog_node_t base = vlog_find_ctor(vlog_ref(class));
   if (base != NULL)
      vlog_lower_subr_call(g, base, v, vlog_lower_this(g), true);

   vlog_lower_prop_inits(g, class, base != NULL ? vlog_ref(base) : NULL,
                         vlog_lower_this(g));
}

static void vlog_fork_capture_cb(vlog_node_t v, void *context)
{
   hash_t *set = context;
   if (vlog_kind(v) != V_REF || !vlog_has_ref(v))
      return;

   vlog_node_t d = vlog_ref(v);
   switch (vlog_kind(d)) {
   case V_LOCAL_DECL:
   case V_TF_PORT_DECL:
      hash_put(set, d, d);
      break;
   default:
      break;
   }
}

typedef struct {
   int          count;
   int          capacity;
   bool         shared;
   vlog_node_t *decls;
   bool        *by_ref;
} fork_caps_t;

static void vlog_fork_written_cb(vlog_node_t v, void *context)
{
   // Find variables the forked process assigns to

   hash_t *set = context;
   vlog_node_t target;

   switch (vlog_kind(v)) {
   case V_BASSIGN:
   case V_NBASSIGN:
   case V_OP_ASSIGN:
      target = vlog_target(v);
      break;
   case V_PREFIX:
   case V_POSTFIX:
      target = vlog_target(v);
      break;
   default:
      return;
   }

   if (vlog_kind(target) == V_REF && vlog_has_ref(target))
      hash_put(set, vlog_ref(target), vlog_ref(target));
}

static void vlog_fork_wait_loop(vlog_gen_t *g, mir_value_t group_var, int mode)
{
   // Suspend until the runtime says the wait is satisfied

   if (mir_get_kind(g->mu) == MIR_UNIT_FUNCTION)
      fatal_trace("cannot wait for a forked process in a function");

   mir_type_t t_offset = mir_offset_type(g->mu);

   mir_block_t test_bb = mir_add_block(g->mu);
   mir_block_t wait_bb = mir_add_block(g->mu);
   mir_block_t done_bb = mir_add_block(g->mu);

   mir_build_jump(g->mu, test_bb);
   mir_set_cursor(g->mu, test_bb, MIR_APPEND);

   // Values do not survive the process suspending so load this each time
   mir_value_t group = mir_is_null(group_var)
      ? mir_const(g->mu, t_offset, 0) : mir_build_load(g->mu, group_var);

   mir_value_t args[] = { group, mir_const(g->mu, t_offset, mode) };
   mir_value_t blocked = mir_build_fcall(g->mu, ident_new("__sv_fork_wait"),
                                         t_offset, MIR_NULL_STAMP, args, 2);
   mir_build_consume(g->mu, blocked);

   mir_value_t zero = mir_const(g->mu, t_offset, 0);
   mir_build_cond(g->mu, mir_build_cmp(g->mu, MIR_CMP_EQ, blocked, zero),
                  done_bb, wait_bb);

   mir_set_cursor(g->mu, wait_bb, MIR_APPEND);

   mir_block_t resume_bb = mir_add_block(g->mu);
   mir_build_wait(g->mu, resume_bb);

   mir_set_cursor(g->mu, resume_bb, MIR_APPEND);
   mir_build_jump(g->mu, test_bb);

   mir_set_cursor(g->mu, done_bb, MIR_APPEND);
}

static void vlog_lower_fork_ctrl(vlog_gen_t *g, vlog_node_t v)
{
   mir_type_t t_offset = mir_offset_type(g->mu);

   if (vlog_subkind(v) == V_FORK_WAIT)
      vlog_fork_wait_loop(g, MIR_NULL_VALUE, 2);
   else {
      mir_value_t res = mir_build_fcall(g->mu, ident_new("__sv_fork_disable"),
                                        t_offset, MIR_NULL_STAMP, NULL, 0);
      mir_build_consume(g->mu, res);
   }
}

static void vlog_lower_fork(vlog_gen_t *g, vlog_node_t v, gen_stack_t *gs)
{
   const vlog_fork_kind_t kind = vlog_subkind(v);

   // Declarations in the block belong to the parent like any other local
   vlog_lower_locals(g, v);

   ident_t unit = mir_get_name(g->mu, MIR_NULL_VALUE);
   mir_type_t t_offset = mir_offset_type(g->mu);

   mir_value_t group = mir_build_fcall(g->mu, ident_new("__sv_fork_begin"),
                                       t_offset, MIR_NULL_STAMP, NULL, 0);
   mir_build_consume(g->mu, group);

   mir_value_t group_var = mir_add_var(g->mu, t_offset, MIR_NULL_STAMP,
                                       ident_uniq("fork_group"), 0);
   mir_build_store(g->mu, group_var, group);

   if (vlog_has_ident(v)) {
      mir_value_t name_args[] = {
         group, mir_const(g->mu, t_offset, vlog_label_hash(vlog_ident(v)))
      };
      mir_value_t named = mir_build_fcall(g->mu, ident_new("__sv_fork_name"),
                                          t_offset, MIR_NULL_STAMP,
                                          name_args, 2);
      mir_build_consume(g->mu, named);
   }

   const int nstmts = vlog_stmts(v);
   for (int i = 0; i < nstmts; i++) {
      vlog_node_t s = vlog_stmt(v, i);

      const loc_t *loc = vlog_loc(s);
      ident_t base = ident_sprintf("%s.fork$%u_%u", istr(unit),
                                   loc->first_line, loc->first_column);

      // Statements from a macro expansion all have the same location
      static hset_t *fork_names = NULL;
      if (fork_names == NULL)
         fork_names = hset_new(16);
      for (int n = 1; hset_contains(fork_names, base); n++)
         base = ident_sprintf("%s.fork$%u_%u_%d", istr(unit),
                              loc->first_line, loc->first_column, n);
      hset_insert(fork_names, base);
      ident_t cap_name = ident_prefix(base, ident_new("frame"), '$');

      // With join_none the parent may have returned or changed its
      // variables by the time the child runs so it takes a copy unless it
      // writes to the variable, as the parent can then see the result, and
      // the parent has a frame that remains valid after it suspends.  With
      // join and join_any the parent is suspended and variables are shared.
      hash_t *set = hash_new(16);
      vlog_visit(s, vlog_fork_capture_cb, set);

      hash_t *written = hash_new(16);
      vlog_visit(s, vlog_fork_written_cb, written);

      if (sv_fork_caps == NULL)
         sv_fork_caps = hash_new(16);

      fork_caps_t *caps = xcalloc(sizeof(fork_caps_t));
      caps->shared = kind != V_FORK_JOIN_NONE;

      const void *key;
      void *dval;
      for (hash_iter_t it = HASH_BEGIN; hash_iter(set, &it, &key, &dval); ) {
         int hops;
         vlog_node_t d = dval;
         mir_value_t var = mir_search_object(g->mu, d, &hops);
         if (!mir_is_null(var) && hops == 0) {
            if (caps->count == caps->capacity) {
               caps->capacity = MAX(caps->capacity * 2, 8);
               caps->decls = xrealloc_array(caps->decls, caps->capacity,
                                            sizeof(vlog_node_t));
               caps->by_ref = xrealloc_array(caps->by_ref, caps->capacity,
                                             sizeof(bool));
            }
            caps->by_ref[caps->count] = caps->shared
               || (hash_get(written, d) != NULL
                   && mir_get_kind(g->mu) != MIR_UNIT_FUNCTION);
            caps->decls[caps->count++] = d;
         }
      }

      ACLEAR(temps);
   }

   if (nstmts > 0 && kind != V_FORK_JOIN_NONE)
      vlog_fork_wait_loop(g, group_var, kind == V_FORK_JOIN ? 0 : 1);

   mir_value_t end_args[] = { mir_build_load(g->mu, group_var) };
   mir_value_t res = mir_build_fcall(g->mu, ident_new("__sv_fork_end"),
                                     t_offset, MIR_NULL_STAMP, end_args, 1);
   mir_build_consume(g->mu, res);

   // A forked process may have disabled a named block around this fork
   bool in_block = false;
   for (gen_stack_t *it = gs; it != NULL; it = it->up)
      in_block |= it->block != NULL;

   if (in_block && nstmts > 0 && kind != V_FORK_JOIN_NONE)
      vlog_lower_disabled_check(g, gs);
}

static void vlog_lower_capture(mir_unit_t *mu, object_t *obj)
{
   vlog_node_t s = vlog_from_object(obj);

   vlog_gen_t g = {
      .mu  = mu,
      .mir = mir_get_context(mu),
   };

   mir_add_param(mu, mir_context_type(mu, mir_get_parent(mu)), MIR_NULL_STAMP,
                 ident_new("context"));

   fork_caps_t *caps = hash_get(sv_fork_caps, s);
   for (int j = 0; j < caps->count; j++) {
      vlog_node_t d = caps->decls[j];
      const type_info_t *ti = vlog_type_info(&g, vlog_type(d));
      mir_type_t type = caps->by_ref[j] ? mir_pointer_type(mu, ti->type) : ti->type;
      mir_value_t var =
         mir_add_var(mu, type, ti->stamp,
                     ident_sprintf("%s$%d", istr(vlog_ident(d)), j), 0);
      mir_put_object(mu, d, var);
   }

   mir_build_return(mu, MIR_NULL_VALUE);

   mir_optimise(mu, MIR_PASS_O0);
   vlog_lower_cleanup(&g);
}

static void vlog_lower_fork_process(mir_unit_t *mu, object_t *obj)
{
   vlog_node_t s = vlog_from_object(obj);

   vlog_gen_t g = {
      .mu        = mu,
      .mir       = mir_get_context(mu),
      .fork_root = s,
   };

   mir_block_t start_bb = mir_add_block(mu);
   assert(start_bb.id == 1);

   mir_build_return(mu, MIR_NULL_VALUE);

   mir_set_cursor(mu, start_bb, MIR_APPEND);

   gen_stack_t gs = {};

   vlog_lower_one_stmt(&g, s, &gs);

   if (!mir_block_finished(mu, MIR_NULL_BLOCK)) {
      mir_value_t res = mir_build_fcall(mu, ident_new("__sv_fork_finish"),
                                        mir_offset_type(mu), MIR_NULL_STAMP,
                                        NULL, 0);
      mir_build_consume(mu, res);
      mir_build_return(mu, MIR_NULL_VALUE);
   }

   mir_optimise(mu, MIR_PASS_O1);
   vlog_lower_cleanup(&g);
}

static void vlog_scan_namespace_cb(lib_t lib, ident_t name, int kind, void *ctx)
{
   mir_context_t *mc = ctx;
   if (kind != T_LAST_TREE_KIND && ident_runtil(name, '$') == name)
      return;

   vlog_defer_scope(mc, name);
}

static bool vlog_is_subclass(vlog_node_t c, vlog_node_t base)
{
   for (;; c = vlog_ref(c)) {
      if (vlog_subr_name(c) == vlog_subr_name(base))
         return true;
      else if (vlog_ident(c) == vlog_ident(base)
               && ident_runtil(vlog_ident(c), '$') != vlog_ident(c))
         return true;   // Same specialisation declared in another scope

      const unsigned nifaces = vlog_class_ifaces(c);
      for (unsigned i = 0; i < nifaces; i++) {
         if (vlog_is_subclass(vlog_class_iface(c, i), base))
            return true;
      }

      if (!vlog_has_ref(c))
         return false;
   }
}

static void vlog_lower_cast_unit(mir_unit_t *mu, object_t *obj)
{
   // Whether an object is an instance of a class or one derived from it

   vlog_node_t class = vlog_from_object(obj);

   vlog_gen_t g = {
      .mu  = mu,
      .mir = mir_get_context(mu),
   };

   lib_walk_index(lib_work(), vlog_scan_namespace_cb, g.mir);

   mir_type_t t_offset = mir_offset_type(mu);
   mir_set_result(mu, t_offset);

   mir_type_t t_this = mir_context_type(mu, vlog_subr_name(class));
   mir_value_t this = mir_add_param(mu, t_this, MIR_NULL_STAMP,
                                    ident_new("this"));

   mir_type_t t_vid = mir_int_type(mu, INT64_MIN, INT64_MAX);
   mir_value_t vid = mir_build_load(
      mu, mir_build_link_var(mu, this, ident_new("$vid"), t_vid));

   const void *key;
   void *value;
   for (hash_iter_t it = HASH_BEGIN;
        hash_iter(sv_classes, &it, &key, &value); ) {
      vlog_node_t c = value;
      if (!vlog_is_subclass(c, class))
         continue;

      mir_block_t yes_bb = mir_add_block(mu);
      mir_block_t next_bb = mir_add_block(mu);

      mir_value_t id = mir_const(mu, t_vid, vlog_class_id(c));
      mir_build_cond(mu, mir_build_cmp(mu, MIR_CMP_EQ, vid, id),
                     yes_bb, next_bb);

      mir_set_cursor(mu, yes_bb, MIR_APPEND);
      mir_build_return(mu, mir_const(mu, t_offset, 1));

      mir_set_cursor(mu, next_bb, MIR_APPEND);
   }

   mir_build_return(mu, mir_const(mu, t_offset, 0));

   mir_optimise(mu, MIR_PASS_O0);
   vlog_lower_cleanup(&g);
}

static void vlog_lower_dispatch(mir_unit_t *mu, object_t *obj)
{
   vlog_node_t decl = vlog_from_object(obj);
   vlog_node_t class = vlog_ref(decl);
   const bool is_task = vlog_kind(decl) == V_TASK_DECL;

   vlog_gen_t g = {
      .mu  = mu,
      .mir = mir_get_context(mu),
   };

   // Classes outside modules are found in the library and those inside
   // modules have been seen by now as this runs after elaboration
   lib_walk_index(lib_work(), vlog_scan_namespace_cb, g.mir);

   const int nports = vlog_ports(decl);
   mir_value_t *args LOCAL = xmalloc_array(nports + 1, sizeof(mir_value_t));

   mir_type_t rtype = mir_offset_type(mu);
   if (!is_task) {
      if (vlog_has_type(decl))
         rtype = vlog_type_info(&g, vlog_type(decl))->type;
      mir_set_result(mu, rtype);
   }

   mir_type_t t_this = mir_context_type(mu, vlog_subr_name(class));
   args[0] = mir_add_param(mu, t_this, MIR_NULL_STAMP, ident_new("this"));

   for (int i = 0; i < nports; i++) {
      vlog_node_t port = vlog_port(decl, i);
      const type_info_t *ti = vlog_type_info(&g, vlog_type(port));

      mir_type_t type = vlog_port_param_type(mu, port, ti);
      args[i + 1] = mir_add_param(mu, type, ti->stamp, vlog_ident(port));
   }

   mir_type_t t_vid = mir_int_type(mu, INT64_MIN, INT64_MAX);
   mir_value_t vid = mir_build_load(
      mu, mir_build_link_var(mu, args[0], ident_new("$vid"), t_vid));

   const void *key;
   void *value;
   for (hash_iter_t it = HASH_BEGIN;
        hash_iter(sv_classes, &it, &key, &value); ) {
      vlog_node_t c = value;
      if ((vlog_flags(c) & VLOG_F_VIRTUAL) || !vlog_is_subclass(c, class))
         continue;

      vlog_node_t impl = vlog_class_member(c, vlog_ident(decl));
      if (impl == NULL || vlog_kind(impl) != vlog_kind(decl)
          || (vlog_flags(impl) & VLOG_F_PURE))
         continue;

      mir_block_t call_bb = mir_add_block(mu);
      mir_block_t next_bb = mir_add_block(mu);

      mir_value_t id = mir_const(mu, t_vid, vlog_class_id(c));
      mir_build_cond(mu, mir_build_cmp(mu, MIR_CMP_EQ, vid, id),
                     call_bb, next_bb);

      mir_set_cursor(mu, call_bb, MIR_APPEND);

      ident_t func = vlog_subr_name(impl);
      if (is_task) {
         mir_block_t resume_bb = mir_add_block(mu);
         mir_build_pcall(mu, func, resume_bb, args, nports + 1);

         mir_set_cursor(mu, resume_bb, MIR_APPEND);
         mir_build_resume(mu, func);
         mir_build_return(mu, MIR_NULL_VALUE);
      }
      else {
         mir_value_t result = mir_build_fcall(mu, func, rtype, MIR_NULL_STAMP,
                                              args, nports + 1);
         mir_build_return(mu, result);
      }

      mir_set_cursor(mu, next_bb, MIR_APPEND);
   }

   mir_value_t locus = mir_build_debug_locus(mu, vlog_to_object(decl));
   mir_build_unreachable(mu, locus);

   mir_optimise(mu, MIR_PASS_O0);
   vlog_lower_cleanup(&g);
}

static mir_value_t vlog_crv_guard_null(vlog_gen_t *g, mir_value_t base,
                                       vlog_node_t where)
{
   // A null handle in a constraint fails the call and reads as an object
   // with no values so that the rest of the constraints can be built

   mir_type_t t_base = mir_get_type(g->mu, base);
   mir_value_t tmp = mir_add_var(g->mu, t_base, MIR_NULL_STAMP,
                                 ident_uniq("crvobj"), MIR_VAR_TEMP);
   mir_build_store(g->mu, tmp, base);

   mir_block_t null_bb = mir_add_block(g->mu);
   mir_block_t done_bb = mir_add_block(g->mu);

   mir_build_cond(g->mu, mir_build_cmp(g->mu, MIR_CMP_EQ, base,
                                       mir_build_null(g->mu, t_base)),
                  null_bb, done_bb);

   mir_set_cursor(g->mu, null_bb, MIR_APPEND);

   const loc_t *loc = vlog_loc(where);
   CRV4(g, CRV_OP_FAIL, g->crv_ctx, vlog_crv_int(g, loc->file_ref),
        vlog_crv_int(g, loc->first_line), vlog_crv_int(g, loc->first_column));

   vlog_node_t cls = vlog_get_type(vlog_value(where));
   vlog_note_spec(cls);

   mir_value_t parent;
   if (vlog_has_ident2(cls) && vlog_find_scope(vlog_ident2(cls)) != NULL)
      parent = mir_build_null(g->mu, t_base);
   else
      parent = vlog_scope_context(g, vlog_ident2(cls));

   mir_value_t dummy = mir_build_protected_init(
      g->mu, t_base, parent, MIR_NULL_VALUE, MIR_NULL_VALUE);
   mir_build_store(g->mu, tmp, dummy);
   mir_build_jump(g->mu, done_bb);

   mir_set_cursor(g->mu, done_bb, MIR_APPEND);
   return mir_build_load(g->mu, tmp);
}

static mir_value_t vlog_crv_key_n(vlog_gen_t *g, vlog_node_t decl, int n)
{
   // Hash of the name and location which is the same in every unit

   uint64_t hash = UINT64_C(0xcbf29ce484222325);
   for (const char *p = istr(vlog_ident(decl)); *p != '\0'; p++)
      hash = (hash ^ (unsigned char)*p) * UINT64_C(0x100000001b3);

   const loc_t *loc = vlog_loc(decl);
   hash = (hash ^ loc->first_line) * UINT64_C(0x100000001b3);
   hash = (hash ^ loc->first_column) * UINT64_C(0x100000001b3);
   hash = (hash ^ n) * UINT64_C(0x100000001b3);

   return vlog_crv_int(g, (int64_t)(hash >> 1));
}

static mir_value_t vlog_crv_key(vlog_gen_t *g, vlog_node_t decl)
{
   return vlog_crv_key_n(g, decl, 0);
}

static bool vlog_crv_is_reduction(vlog_node_t v)
{
   switch (vlog_subkind(v)) {
   case V_METHOD_ARR_SUM:
   case V_METHOD_ARR_PRODUCT:
   case V_METHOD_ARR_AND:
   case V_METHOD_ARR_OR:
   case V_METHOD_ARR_XOR:
      {
         if (vlog_stmts(v) != 3)
            return true;

         // With a with clause the elements of a random array are
         // combined by the solver
         vlog_node_t prefix = vlog_value(v);
         return vlog_kind(prefix) == V_REF && vlog_has_ref(prefix)
            && vlog_kind(vlog_ref(prefix)) == V_VAR_DECL
            && (vlog_flags(vlog_ref(prefix)) & VLOG_F_RAND)
            && vlog_ranges(vlog_ref(prefix)) == 1
            && vlog_subkind(vlog_range(vlog_ref(prefix), 0)) == V_DIM_UNPACKED;
      }
   default:
      return false;
   }
}

static void vlog_note_spec(vlog_node_t class)
{
   if (vlog_kind(class) != V_CLASS_DECL || !(vlog_flags(class) & VLOG_F_SPEC))
      return;

   if (sv_spec_scopes == NULL)
      sv_spec_scopes = hash_new(16);

   hash_put(sv_spec_scopes, vlog_ident2(class), class);
}

static void vlog_lower_spec_init(vlog_gen_t *g, vlog_node_t class)
{
   // Static members are initialised even if nothing refers to them

   vlog_note_spec(class);
   vlog_defer_scope(g->mir, vlog_ident2(class));

   mir_value_t pkg = mir_build_package_init(g->mu, vlog_ident2(class),
                                            MIR_NULL_VALUE);
   mir_build_consume(g->mu, pkg);
}

static void vlog_lower_import(vlog_gen_t *g, vlog_node_t v)
{
   // Importing a package causes its initialisation

   if (!vlog_has_ref(v))
      return;

   vlog_node_t pack = vlog_ref(v);
   if (vlog_kind(pack) != V_PACKAGE)
      return;

   ident_t name = vlog_ident(pack);
   if (mir_get_name(g->mu, MIR_NULL_VALUE) == name)
      return;

   vlog_defer_scope(g->mir, name);

   mir_value_t pkg = mir_build_package_init(g->mu, name, MIR_NULL_VALUE);
   mir_build_consume(g->mu, pkg);
}

static void vlog_lower_namespace_vars(vlog_gen_t *g, vlog_node_t container)
{
   // Every variable has its default value before any initial value is
   // evaluated as that may call a function which writes to a later variable

   const int ndecls = vlog_decls(container);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(container, i);
      switch (vlog_kind(d)) {
      case V_VAR_DECL:
         {
            ident_t name = vlog_ident(d);
            if (vlog_kind(container) == V_CLASS_DECL) {
               if (!(vlog_flags(d) & VLOG_F_STATIC))
                  break;
               name = vlog_static_name(container, name);
            }
            vlog_store_local_default(g, d, vlog_add_local_var(g, d, name));
         }
         break;
      case V_CLASS_DECL:
         if (!(vlog_flags(d) & VLOG_F_SPEC))
            vlog_lower_namespace_vars(g, d);
         break;
      default:
         break;
      }
   }
}

static void vlog_lower_namespace_statics(vlog_gen_t *g, vlog_node_t container)
{
   const int ndecls = vlog_decls(container);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(container, i);
      switch (vlog_kind(d)) {
      case V_VAR_DECL:
         if (vlog_has_value(d)
             && (vlog_kind(container) != V_CLASS_DECL
                 || (vlog_flags(d) & VLOG_F_STATIC))) {
            int hops;
            mir_value_t var = mir_search_object(g->mu, d, &hops);
            vlog_store_local_init(g, d, var);
         }
         break;
      case V_CLASS_DECL:
         if (vlog_flags(d) & VLOG_F_SPEC)
            vlog_lower_spec_init(g, d);
         else
            vlog_lower_namespace_statics(g, d);
         break;
      case V_IMPORT_DECL:
         vlog_lower_import(g, d);
         break;
      default:
         break;
      }
   }
}

static void vlog_lower_spec_scope(mir_unit_t *mu, object_t *obj)
{
   vlog_node_t c = vlog_from_object(obj);

   vlog_gen_t g = {
      .mu  = mu,
      .mir = mir_get_context(mu),
   };

   // As for a package, all have their default value first
   vlog_lower_namespace_vars(&g, c);
   vlog_lower_namespace_statics(&g, c);

   mir_build_return(mu, MIR_NULL_VALUE);

   mir_optimise(mu, MIR_PASS_O0);
   vlog_lower_cleanup(&g);
}

static void vlog_defer_class(mir_context_t *mc, vlog_node_t c, ident_t parent)
{
   if (vlog_flags(c) & VLOG_F_GENERIC)
      return;   // Only specialisations are ever lowered

   ident_t name = vlog_subr_name(c);

   vlog_check_context(mc);

   if (vlog_flags(c) & VLOG_F_SPEC) {
      vlog_note_spec(c);
      parent = NULL;
   }

   if (sv_classes == NULL)
      sv_classes = hash_new(32);
   else if (hash_get(sv_classes, name) != NULL)
      return;

   hash_put(sv_classes, name, c);

   mir_defer(mc, name, parent, MIR_UNIT_PROTECTED, vlog_lower_class_decl,
             vlog_to_object(c));

   const int ndecls = vlog_decls(c);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(c, i);
      switch (vlog_kind(d)) {
      case V_FUNC_DECL:
         if (!(vlog_flags(d) & VLOG_F_PURE))
            mir_defer(mc, vlog_subr_name(d), name, MIR_UNIT_FUNCTION,
                      vlog_lower_func_decl, vlog_to_object(d));
         break;
      case V_TASK_DECL:
         if (!(vlog_flags(d) & VLOG_F_PURE))
            mir_defer(mc, vlog_subr_name(d), name, MIR_UNIT_PROCEDURE,
                      vlog_lower_task_decl, vlog_to_object(d));
         break;
      case V_CONSTRUCTOR:
         mir_defer(mc, vlog_subr_name(d), name, MIR_UNIT_FUNCTION,
                   vlog_lower_func_decl, vlog_to_object(d));
         break;
      case V_CLASS_DECL:
         vlog_defer_class(mc, d, name);
         break;
      default:
         break;
      }
   }

   if (vlog_has_ref(c)) {
      vlog_note_spec(vlog_ref(c));
      vlog_defer_scope(mc, vlog_ident2(vlog_ref(c)));
   }
}

static vlog_node_t vlog_find_scope(ident_t name)
{
   // Find a namespace or package by name

   if (sv_spec_scopes != NULL) {
      vlog_node_t spec = hash_get(sv_spec_scopes, name);
      if (spec != NULL)
         return spec;
   }

   lib_t lib = lib_find(ident_until(name, '.'));
   if (lib == NULL)
      return NULL;

   object_t *obj = lib_get_generic(lib, name, NULL);
   if (obj == NULL)
      return NULL;

   vlog_node_t v = vlog_from_object(obj);
   if (v == NULL)
      return NULL;

   switch (vlog_kind(v)) {
   case V_NAMESPACE:
   case V_PACKAGE:
      return v;
   default:
      return NULL;
   }
}

static void vlog_defer_scope(mir_context_t *mc, ident_t name)
{
   vlog_check_context(mc);

   if (sv_scopes == NULL)
      sv_scopes = hset_new(16);
   else if (hset_contains(sv_scopes, name))
      return;

   hset_insert(sv_scopes, name);

   vlog_node_t v = vlog_find_scope(name);
   if (v == NULL)
      return;   // Scope is an instance which was already deferred

   if (vlog_kind(v) == V_CLASS_DECL) {
      mir_defer(mc, name, NULL, MIR_UNIT_PACKAGE, vlog_lower_spec_scope,
                vlog_to_object(v));
      vlog_defer_class(mc, v, NULL);
      return;
   }

   mir_defer(mc, name, NULL, MIR_UNIT_PACKAGE, vlog_lower_namespace,
             vlog_to_object(v));

   const int ndecls = vlog_decls(v);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(v, i);
      switch (vlog_kind(d)) {
      case V_FUNC_DECL:
         mir_defer(mc, vlog_subr_name(d), name, MIR_UNIT_FUNCTION,
                   vlog_lower_func_decl, vlog_to_object(d));
         break;
      case V_TASK_DECL:
         mir_defer(mc, vlog_subr_name(d), name, MIR_UNIT_PROCEDURE,
                   vlog_lower_task_decl, vlog_to_object(d));
         break;
      case V_CLASS_DECL:
         vlog_defer_class(mc, d, NULL);
         break;
      default:
         break;
      }
   }
}

static mir_value_t vlog_scope_context(vlog_gen_t *g, ident_t scope)
{
   if (mir_get_name(g->mu, MIR_NULL_VALUE) == scope)
      return mir_build_context_upref(g->mu, 0);

   int hops = 1;
   for (ident_t it = mir_get_parent(g->mu); it != NULL; hops++) {
      if (it == scope)
         return mir_build_context_upref(g->mu, hops);

      it = mir_get_shape_parent(mir_get_shape(g->mir, it));
   }

   if (scope != NULL && vlog_find_scope(scope) != NULL) {
      vlog_defer_scope(g->mir, scope);
      return mir_build_package_init(g->mu, scope, MIR_NULL_VALUE);
   }

   // A static method called from code with no enclosing scope: there is no
   // object and it finds what it needs by name
   if (mir_get_parent(g->mu) == NULL && scope != NULL)
      return mir_build_null(g->mu, mir_context_type(g->mu, scope));

   if (scope != NULL) {
      // A static method of a class declared in a scope that encloses the
      // caller
      ident_t parent = ident_runtil(scope, '.');
      if (parent != scope) {
         if (mir_get_name(g->mu, MIR_NULL_VALUE) == parent)
            return mir_build_package_init(
               g->mu, scope, mir_build_context_upref(g->mu, 0));

         int phops = 1;
         for (ident_t it = mir_get_parent(g->mu); it != NULL; phops++) {
            if (it == parent)
               return mir_build_package_init(
                  g->mu, scope, mir_build_context_upref(g->mu, phops));

            it = mir_get_shape_parent(mir_get_shape(g->mir, it));
         }
      }
   }

   return mir_build_context_upref(g->mu, 1);
}

static void vlog_lower_seq_block(vlog_gen_t *g, vlog_node_t v, gen_stack_t *gs)
{
   vlog_lower_locals(g, v);

   if (vlog_has_ident(v)) {
      mir_block_t exit_bb = mir_add_block(g->mu);

      block_info_t block = {
         .name    = vlog_ident(v),
         .exit_bb = exit_bb,
      };

      gen_stack_t this = vlog_push_block(gs, &block);

      vlog_lower_stmts(g, v, &this);

      if (!mir_block_finished(g->mu, MIR_NULL_BLOCK))
         mir_build_jump(g->mu, exit_bb);

      mir_set_cursor(g->mu, exit_bb, MIR_APPEND);
   }
   else
      vlog_lower_stmts(g, v, gs);
}

static bool vlog_lower_one_stmt(vlog_gen_t *g, vlog_node_t s, gen_stack_t *gs)
{
   mir_set_loc(g->mu, vlog_loc(s));

   switch (vlog_kind(s)) {
   case V_METHOD_CALL:
      vlog_lower_method_call(g, s);
      break;
   case V_MEMBER_REF:
      vlog_lower_rvalue(g, s);
      break;
   case V_VOID_CALL:
      vlog_lower_rvalue(g, vlog_value(s));
      break;
   case V_SUPER_CALL:
      vlog_lower_super_call(g, s);
      break;
   case V_EVENT_TRIGGER:
      if (vlog_subkind(s) == 0)
         vlog_lower_event_trigger(g, vlog_lower_rvalue(g, vlog_value(s)));
      else {
         mir_value_t event = vlog_lower_rvalue(g, vlog_value(s));
         mir_type_t t_offset = mir_offset_type(g->mu);
         mir_value_t after = mir_const(g->mu, t_offset, 0);
         if (vlog_has_delay(s))
            after = mir_build_cast(g->mu, t_offset,
                                   vlog_lower_delay(g, vlog_delay(s)));
         mir_value_t args[] = { event, after };
         mir_value_t res = mir_build_fcall(g->mu,
                                           ident_new("__sv_event_trigger_nba"),
                                           t_offset, MIR_NULL_STAMP, args, 2);
         mir_build_consume(g->mu, res);
      }
      break;
   case V_FORK:
      vlog_lower_fork(g, s, gs);
      break;
   case V_FORK_CTRL:
      vlog_lower_fork_ctrl(g, s);
      break;
   case V_LOOP_JUMP:
      {
         gen_stack_t *it = gs;
         while (it != NULL && it->loop == NULL)
            it = it->up;

         if (it == NULL)
            fatal_at(vlog_loc(s), "break or continue outside of a loop");

         mir_build_jump(g->mu, vlog_subkind(s) == V_JUMP_BREAK
                        ? it->loop->exit_bb : it->loop->test_bb);
         return true;
      }
   case V_TIMING:
      vlog_lower_timing(g, s, gs);
      break;
   case V_FORCE:
   case V_RELEASE:
      vlog_lower_force(g, s);
      break;
   case V_BASSIGN:
      vlog_lower_blocking_assignment(g, s);
      break;
   case V_NBASSIGN:
      vlog_lower_non_blocking_assignment(g, s);
      break;
   case V_OP_ASSIGN:
      vlog_lower_operator_assignment(g, s);
      break;
   case V_SEQ_BLOCK:
      vlog_lower_seq_block(g, s, gs);
      break;
   case V_SYS_TCALL:
      vlog_lower_sys_tfcall(g, s);
      break;
   case V_IF:
      vlog_lower_if(g, s, gs);
      break;
   case V_FOREVER:
      vlog_lower_forever(g, s, gs);
      break;
   case V_REPEAT:
      vlog_lower_repeat(g, s, gs);
      break;
   case V_WHILE:
      vlog_lower_while(g, s, gs);
      break;
   case V_DO_WHILE:
      vlog_lower_do_while(g, s, gs);
      break;
   case V_FOR_LOOP:
      vlog_lower_for_loop(g, s, gs);
      break;
   case V_CASE:
      vlog_lower_case(g, s, gs);
      break;
   case V_PREFIX:
   case V_POSTFIX:
   case V_STD_RANDOMIZE:
      vlog_lower_rvalue(g, s);
      break;
   case V_RANDCASE:
      vlog_lower_randcase(g, s, gs);
      break;
   case V_RETURN:
      vlog_lower_return(g, s);
      return true;
   case V_USER_TCALL:
      vlog_lower_user_tcall(g, s);
      break;
   case V_DISABLE:
      return vlog_lower_disable(g, s, gs);
   case V_ASSERT:
   case V_ASSUME:
   case V_COVER:
      vlog_lower_assert(g, s, gs);
      break;
   default:
      CANNOT_HANDLE(s);
   }

   return false;
}

static void vlog_lower_stmts(vlog_gen_t *g, vlog_node_t v, gen_stack_t *gs)
{
   const int nstmts = vlog_stmts(v);
   for (int i = 0; i < nstmts; i++) {
      if (vlog_lower_one_stmt(g, vlog_stmt(v, i), gs))
         return;
   }
}

static void vlog_lower_driver(vlog_gen_t *g, vlog_node_t v)
{
   if (vlog_kind(v) == V_CONCAT) {
      const int nparams = vlog_params(v);
      for (int i = 0; i < nparams; i++)
         vlog_lower_driver(g, vlog_param(v, i));
   }
   else {
      mir_type_t t_offset = mir_offset_type(g->mu);

      vlog_node_t prefix = vlog_longest_static_prefix(v);

      vlog_select_t target = vlog_lower_select(g, prefix);

      mir_value_t nets =
         mir_build_array_ref(g->mu, target.obj, target.dst_offset);

      int total_size = target.size;
      if (vlog_kind(prefix) == V_REF)
         total_size = vlog_whole_size(g, prefix, &target);

      mir_value_t count = vlog_kind(prefix) == V_REF
         ? mir_const(g->mu, t_offset, total_size) : target.count;
      mir_build_drive_signal(g->mu, nets, count);
   }
}

static vlog_node_t vlog_assertion_clock(vlog_node_t v)
{
   // The clock edge of an always process which only checks one concurrent
   // assertion, otherwise null

   if (vlog_stmts(v) != 1)
      return NULL;

   vlog_node_t s0 = vlog_stmt(v, 0);
   if (vlog_kind(s0) != V_TIMING || vlog_stmts(s0) < 1)
      return NULL;

   // Only the assignments which clear the flag set by disable iff may follow
   for (int i = 1; i < vlog_stmts(s0); i++) {
      vlog_node_t s = vlog_stmt(s0, i);
      if (vlog_kind(s) != V_BASSIGN || vlog_kind(vlog_value(s)) != V_NUMBER)
         return NULL;
   }

   vlog_node_t ctrl = vlog_value(s0);
   if (vlog_kind(ctrl) != V_EVENT_CONTROL || vlog_is_event_wait(ctrl)
       || vlog_is_polled_control(ctrl) || vlog_params(ctrl) != 1)
      return NULL;

   vlog_node_t e = vlog_param(ctrl, 0);
   if (vlog_kind(e) != V_EVENT || vlog_has_left(e))
      return NULL;

   const v_event_kind_t kind = vlog_subkind(e);
   if (kind == V_EVENT_LEVEL) {
      vlog_node_t value = vlog_value(e);
      if (vlog_kind(value) != V_REF || !vlog_has_ref(value)
          || vlog_kind(vlog_ref(value)) != V_VAR_DECL
          || vlog_width(value) != 1)
         return NULL;
   }
   else if (kind != V_EVENT_POSEDGE && kind != V_EVENT_NEGEDGE)
      return NULL;

   vlog_node_t s1 = vlog_stmt(s0, 0);
   switch (vlog_kind(s1)) {
   case V_ASSERT:
   case V_ASSUME:
   case V_COVER:
      return vlog_kind(vlog_value(s1)) == V_PROP_SPEC ? ctrl : NULL;
   default:
      return NULL;
   }
}

static void vlog_lower_always(vlog_gen_t *g, vlog_node_t v)
{
   mir_block_t start_bb = mir_add_block(g->mu);
   assert(start_bb.id == 1);

   const int nstmts = vlog_stmts(v);
   for (int i = 0; i < nstmts; i++) {
      vlog_node_t s0 = vlog_stmt(v, i);
      if (vlog_kind(s0) != V_TIMING)
         break;

      // Cache the event control trigger once on reset to avoid
      // regenerating it on each invocation
      vlog_node_t ctrl = vlog_value(s0);
      if (vlog_kind(ctrl) == V_EVENT_CONTROL && !vlog_is_event_wait(ctrl)
          && !vlog_is_polled_control(ctrl))
         vlog_lower_trigger_var(g, ctrl);
   }

   mir_build_return(g->mu, MIR_NULL_VALUE);

   mir_set_cursor(g->mu, start_bb, MIR_APPEND);

   gen_stack_t gs = {};

   g->sva_clk = vlog_assertion_clock(v);
   g->sva_stmt = NULL;
   g->sva_steady = MIR_NULL_VALUE;

   vlog_lower_stmts(g, v, &gs);

   if (g->sva_clk != NULL && g->sva_stmt != NULL
       && !mir_block_finished(g->mu, MIR_NULL_BLOCK))
      vlog_sva_sleep(g, g->sva_stmt, g->sva_chk);

   g->sva_clk = NULL;
   g->sva_stmt = NULL;

   if (!mir_block_finished(g->mu, MIR_NULL_BLOCK))
      mir_build_jump(g->mu, start_bb);
}

static void vlog_lower_initial(vlog_gen_t *g, vlog_node_t v)
{
   mir_block_t start_bb = mir_add_block(g->mu);
   assert(start_bb.id == 1);

   mir_build_return(g->mu, MIR_NULL_VALUE);

   mir_set_cursor(g->mu, start_bb, MIR_APPEND);

   gen_stack_t gs = {};

   vlog_lower_stmts(g, v, &gs);

   if (!mir_block_finished(g->mu, MIR_NULL_BLOCK))
      mir_build_return(g->mu, MIR_NULL_VALUE);
}

static void vlog_lower_final(vlog_gen_t *g, vlog_node_t v)
{
   mir_block_t start_bb = mir_add_block(g->mu);
   assert(start_bb.id == 1);

   mir_build_return(g->mu, MIR_NULL_VALUE);

   mir_set_cursor(g->mu, start_bb, MIR_APPEND);

   mir_block_t final_bb = mir_add_block(g->mu);
   mir_build_wait(g->mu, final_bb);

   mir_set_cursor(g->mu, final_bb, MIR_APPEND);

   gen_stack_t gs = {};

   vlog_lower_stmts(g, v, &gs);

   if (!mir_block_finished(g->mu, MIR_NULL_BLOCK))
      mir_build_return(g->mu, MIR_NULL_VALUE);
}

static bool vlog_is_param_select(vlog_node_t v)
{
   // A member or element of a parameter with a run time index

   while (vlog_kind(v) == V_MEMBER_REF || vlog_kind(v) == V_BIT_SELECT)
      v = vlog_value(v);

   if (vlog_kind(v) != V_REF || !vlog_has_ref(v))
      return false;

   const vlog_kind_t dk = vlog_kind(vlog_ref(v));
   return dk == V_PARAM_DECL || dk == V_LOCALPARAM;
}

static void vlog_sensitivity_event(vlog_gen_t *g, mir_value_t on,
                                   mir_value_t count)
{
   if (g->sens_clear)
      mir_build_clear_event(g->mu, on, count);
   else
      mir_build_sched_event(g->mu, on, count);
}

static void vlog_lower_sensitivity(vlog_gen_t *g, vlog_node_t v)
{
   const vlog_kind_t kind = vlog_kind(v);
   switch (kind) {
   case V_REF:
   case V_HIER_REF:
      {
         if (!vlog_has_ref(v))
            fatal_at(vlog_loc(v), "sorry, this hierarchical reference cannot "
                     "be resolved");

         if (vlog_kind(v) == V_HIER_REF && vlog_is_vhdl_path(vlog_ref(v))) {
            // The indices of an array of records are also read
            for (vlog_node_t it = vlog_value(vlog_ref(v)); ;
                 it = vlog_value(it)) {
               const int nparams = vlog_params(it);
               for (int i = 0; i < nparams; i++)
                  vlog_lower_sensitivity(g, vlog_param(it, i));

               if (!vlog_has_value(it))
                  break;
            }
         }

         switch (vlog_kind(vlog_ref(v))) {
         case V_PORT_DECL:
         case V_NET_DECL:
         case V_VAR_DECL:
         case V_LOCAL_DECL:
            {
               mir_type_t t_offset = mir_offset_type(g->mu);

               if (vlog_kind(v) == V_HIER_REF
                   && vlog_is_vhdl_integer(vlog_ref(v))) {
                  mir_value_t in_range;
                  mir_value_t nets =
                     vlog_lower_vhdl_integer_nets(g, v, &in_range);
                  vlog_sensitivity_event(g, nets,
                                         mir_const(g->mu, t_offset, 1));
                  break;
               }

               vlog_select_t lvalue = vlog_lower_select(g, v);
               int total_size = lvalue.size;
               if (vlog_kind(v) == V_REF)
                  total_size = vlog_whole_size(g, v, &lvalue);

               mir_value_t count = mir_const(g->mu, t_offset, total_size);
               vlog_sensitivity_event(g, lvalue.obj, count);
            }
            break;
         case V_PARAM_DECL:
         case V_LOCALPARAM:
         case V_ENUM_NAME:
            break;
         default:
            CANNOT_HANDLE(v);
         }
      }
      break;
   case V_BIT_SELECT:
   case V_PART_SELECT:
   case V_MEMBER_REF:
      {
         vlog_node_t prefix = vlog_longest_static_prefix(v);
         if (prefix == v) {
            vlog_select_t lvalue = vlog_lower_select(g, v);

            mir_value_t nets =
               mir_build_array_ref(g->mu, lvalue.obj, lvalue.dst_offset);

            vlog_sensitivity_event(g, nets, lvalue.count);
         }
         else if (prefix != NULL)
            vlog_lower_sensitivity(g, prefix);
         else if (kind != V_MEMBER_REF || vlog_is_param_select(v))
            vlog_lower_sensitivity(g, vlog_value(v));   // An expression

         if (kind == V_BIT_SELECT) {
            const int nparams = vlog_params(v);
            for (int i = 0; i < nparams; i++)
               vlog_lower_sensitivity(g, vlog_param(v, i));
         }
         else if (kind == V_PART_SELECT)
            vlog_lower_sensitivity(g, vlog_left(v));
      }
      break;
   case V_NUMBER:
   case V_STRING:
   case V_REAL:
      break;
   case V_BINARY:
      vlog_lower_sensitivity(g, vlog_left(v));
      vlog_lower_sensitivity(g, vlog_right(v));
      break;
   case V_UNARY:
   case V_CAST:
      vlog_lower_sensitivity(g, vlog_value(v));
      break;
   case V_COND_EXPR:
      vlog_lower_sensitivity(g, vlog_value(v));
      vlog_lower_sensitivity(g, vlog_left(v));
      vlog_lower_sensitivity(g, vlog_right(v));
      break;
   case V_CONCAT:
   case V_USER_FCALL:
   case V_SYS_FCALL:
   case V_STREAM:
      {
         const int nparams = vlog_params(v);
         for (int i = 0; i < nparams; i++)
            vlog_lower_sensitivity(g, vlog_param(v, i));
      }
      break;
   case V_INSIDE:
      {
         vlog_lower_sensitivity(g, vlog_value(v));

         const int nparams = vlog_params(v);
         for (int i = 0; i < nparams; i++) {
            vlog_node_t item = vlog_param(v, i);
            if (vlog_kind(item) == V_INSIDE_RANGE) {
               vlog_lower_sensitivity(g, vlog_left(item));
               vlog_lower_sensitivity(g, vlog_right(item));
            }
            else
               vlog_lower_sensitivity(g, item);
         }
      }
      break;
   case V_MIN_TYP_MAX:
      vlog_lower_sensitivity(g, vlog_value(v));
      break;
   case V_PATTERN_EXPR:
      {
         const int nparams = vlog_params(v);
         for (int i = 0; i < nparams; i++)
            vlog_lower_sensitivity(g, vlog_right(vlog_param(v, i)));
      }
      break;
   default:
      CANNOT_HANDLE(v);
   }
}

static void vlog_lower_initial_x(vlog_gen_t *g, vlog_node_t v)
{
   // A driver with a delay outputs X until the first update takes effect

   if (vlog_kind(v) == V_CONCAT) {
      const int nparams = vlog_params(v);
      for (int i = 0; i < nparams; i++)
         vlog_lower_initial_x(g, vlog_param(v, i));
      return;
   }

   if (!vlog_is_net(v))
      return;   // A variable keeps its initial value

   vlog_select_t lvalue = vlog_lower_select(g, v);

   mir_value_t tmp = MIR_NULL_VALUE;
   if (lvalue.size > 1) {
      mir_type_t t_array = mir_carray_type(g->mu, lvalue.size,
                                           mir_logic_type(g->mu));
      tmp = mir_add_var(g->mu, t_array, MIR_NULL_STAMP, ident_uniq("tmp"),
                        MIR_VAR_TEMP);
   }

   mir_value_t src = mir_build_unpack(g->mu, vlog_lower_x(g, lvalue.type),
                                      ST_STRONG, tmp);

   mir_value_t nets = mir_build_array_ref(g->mu, lvalue.obj, lvalue.dst_offset);
   mir_build_put_driver(g->mu, nets, lvalue.count,
                        vlog_lvalue_source(g, &lvalue, src));
}

static void vlog_lower_assign_process(vlog_gen_t *g, vlog_node_t v)
{
   mir_block_t start_bb = mir_add_block(g->mu);
   assert(start_bb.id == 1);

   // A member held in memory cannot be driven so is assigned each time
   // the value changes
   vlog_node_t adelay = vlog_assign_delay(v);
   const bool in_memory = vlog_uses_memory_member(vlog_target(v));
   const bool polled = vlog_uses_memory_member(vlog_value(v));

   if (in_memory && adelay != NULL
       && (polled || vlog_params(adelay) > 0 || vlog_width(vlog_target(v)) == 0))
      fatal_at(vlog_loc(v), "sorry, a continuous assignment with this kind "
               "of delay to a member of an unpacked structure or class is "
               "not supported");

   // A net driven with a delay keeps its initial value until the first
   // update takes effect
   if (!in_memory)
      vlog_lower_driver(g, vlog_target(v));

   if (!polled)
      vlog_lower_sensitivity(g, vlog_value(v));

   // A delayed assignment to memory remembers the value and when it takes
   // effect so that a new value before then replaces it
   mir_type_t t_time = mir_time_type(g->mu);
   mir_type_t t_pend = MIR_NULL_TYPE;
   mir_value_t last_var = MIR_NULL_VALUE, pend_var = MIR_NULL_VALUE;
   mir_value_t due_var = MIR_NULL_VALUE, valid_var = MIR_NULL_VALUE;
   if (in_memory && adelay != NULL) {
      t_pend = mir_vec4_type(g->mu, vlog_width(vlog_target(v)), false);
      mir_type_t t_bool = mir_bool_type(g->mu);

      last_var = mir_add_var(g->mu, t_pend, MIR_NULL_STAMP, ident_new("last"),
                             0);
      pend_var = mir_add_var(g->mu, t_pend, MIR_NULL_STAMP, ident_new("pend"),
                             0);
      due_var = mir_add_var(g->mu, t_time, MIR_NULL_STAMP, ident_new("due"), 0);
      valid_var = mir_add_var(g->mu, t_bool, MIR_NULL_STAMP,
                              ident_new("valid"), 0);

      mir_build_store(g->mu, last_var, vlog_lower_x(g, t_pend));
      mir_build_store(g->mu, pend_var, vlog_lower_x(g, t_pend));
      mir_build_store(g->mu, due_var, mir_const(g->mu, t_time, 0));
      mir_build_store(g->mu, valid_var, mir_const(g->mu, t_bool, 0));
   }

   mir_build_return(g->mu, MIR_NULL_VALUE);

   mir_set_cursor(g->mu, start_bb, MIR_APPEND);

   vlog_node_t target = vlog_target(v);

   if (in_memory && adelay != NULL) {
      mir_type_t t_bool = mir_bool_type(g->mu);

      ident_t now_func =
         ident_new("STD.STANDARD.NOW()25STD.STANDARD.DELAY_LENGTH");
      mir_value_t pkg = mir_build_link_package(g->mu, well_known(W_STD_STANDARD));
      mir_value_t nargs[1] = { pkg };
      mir_value_t now = mir_build_fcall(g->mu, now_func, t_time, MIR_NULL_STAMP,
                                        nargs, 1);

      // Apply the pending value once its time has come
      mir_block_t apply_bb = mir_add_block(g->mu);
      mir_block_t eval_bb = mir_add_block(g->mu);

      mir_value_t valid = mir_build_load(g->mu, valid_var);
      mir_value_t due = mir_build_load(g->mu, due_var);
      mir_value_t ready = mir_build_and(
         g->mu, valid, mir_build_cmp(g->mu, MIR_CMP_GEQ, now, due));
      mir_build_cond(g->mu, ready, apply_bb, eval_bb);

      mir_set_cursor(g->mu, apply_bb, MIR_APPEND);
      vlog_assign_variable(g, target, mir_build_load(g->mu, pend_var), NULL);
      mir_build_store(g->mu, valid_var, mir_const(g->mu, t_bool, 0));
      mir_build_jump(g->mu, eval_bb);

      mir_set_cursor(g->mu, eval_bb, MIR_APPEND);

      vlog_node_t rhs = vlog_value(v);
      mir_value_t value = vlog_lower_cast(
         g, t_pend, vlog_lower_with_context(g, rhs, t_pend));

      mir_value_t same = vlog_lower_test(
         g, mir_build_binary(g->mu, MIR_VEC_CASE_EQ, t_pend, value,
                             mir_build_load(g->mu, last_var)));

      mir_block_t new_bb = mir_add_block(g->mu);
      mir_block_t wait_bb = mir_add_block(g->mu);
      mir_build_cond(g->mu, same, wait_bb, new_bb);

      mir_set_cursor(g->mu, new_bb, MIR_APPEND);
      mir_value_t after = vlog_lower_delay(g, adelay);
      mir_build_store(g->mu, last_var, value);
      mir_build_store(g->mu, pend_var, value);
      mir_build_store(g->mu, due_var,
                      mir_build_add(g->mu, t_time, now, after));
      mir_build_store(g->mu, valid_var, mir_const(g->mu, t_bool, 1));
      mir_build_sched_process(g->mu, after);
      mir_build_jump(g->mu, wait_bb);

      mir_set_cursor(g->mu, wait_bb, MIR_APPEND);
      mir_build_wait(g->mu, start_bb);
      return;
   }

   if (in_memory) {
      vlog_assign_variable(g, target, MIR_NULL_VALUE, vlog_value(v));

      if (polled) {
         vlog_lower_poll_expr(g, vlog_value(v));
         mir_build_jump(g->mu, start_bb);
      }
      else
         mir_build_wait(g->mu, start_bb);

      return;
   }

   mir_type_t type;
   unsigned nlvalues = 1, targetsz = 0;
   vlog_select_t lvalue1, *lvalues = &lvalue1;
   if (vlog_kind(target) == V_CONCAT) {
      nlvalues = vlog_count_lvalues(target);
      lvalues = xmalloc_array(nlvalues, sizeof(vlog_select_t));

      unsigned index = 0;
      vlog_lower_lvalues(g, target, lvalues, NULL, &index);
      assert(index == nlvalues);

      for (int i = 0; i < nlvalues; i++)
         targetsz += lvalues[i].size;

      type = mir_vec4_type(g->mu, targetsz, false);
   }
   else {
      lvalue1 = vlog_lower_select(g, target);
      targetsz = lvalue1.size;
      type = lvalue1.type;
   }

   vlog_node_t rhs = vlog_value(v);

   mir_value_t value;
   vlog_node_t dims[MAX_UNPACKED_DIMS];
   int ndims;
   if (vlog_kind(rhs) == V_PATTERN_EXPR && vlog_kind(target) != V_CONCAT
       && (ndims = vlog_unpacked_dims(target, dims)) > 0)
      value = vlog_lower_array_pattern(g, rhs, dims, ndims,
                                       vlog_array_elem_info(g, target));
   else
      value = vlog_lower_with_context(g, rhs, vlog_assign_context(g, type, rhs));

   mir_value_t resize = vlog_lower_cast(g, type, value);
   assert(mir_is_vector(g->mu, resize) || mir_is(g->mu, resize, MIR_TYPE_REAL));

   mir_value_t tmp = MIR_NULL_VALUE;
   if (targetsz != 1) {
      mir_type_t t_elem = mir_logic_type(g->mu);
      mir_type_t t_array = mir_carray_type(g->mu, targetsz, t_elem);
      tmp = vlog_get_temp(g, t_array);
   }

   mir_type_t t_offset = mir_offset_type(g->mu);

   uint8_t strength = ST_STRONG;
   if (vlog_params(v) > 0) {
      vlog_node_t p = vlog_param(v, 0);
      assert(vlog_kind(p) == V_STRENGTH);
      strength = vlog_subkind(p);
   }

   mir_value_t unpacked = mir_build_unpack(g->mu, resize, strength, tmp);

   mir_value_t after = MIR_NULL_VALUE;
   if (adelay != NULL) {
      assert(vlog_kind(adelay) == V_DELAY_CONTROL);

      after = vlog_lower_net_delay(g, adelay, resize, targetsz);

      vlog_node_t ddelay = vlog_decl_delay(v);
      if (vlog_has_delay(v) && ddelay != NULL) {
         mir_type_t t_time = mir_time_type(g->mu);
         after = mir_build_add(g->mu, t_time, after,
                               vlog_lower_net_delay(g, ddelay, resize,
                                                    targetsz));
      }
   }

   for (int i = 0, offset = 0; i < nlvalues; offset += lvalues[i].size, i++) {
      mir_value_t nets = mir_build_array_ref(g->mu, lvalues[i].obj,
                                             lvalues[i].dst_offset);

      mir_value_t src = unpacked;
      if (offset > 0 || !mir_is_null(lvalues[i].src_offset)) {
         mir_value_t pos =
            mir_build_add(g->mu, t_offset, mir_const(g->mu, t_offset, offset),
                          lvalues[i].src_offset);
         src = mir_build_array_ref(g->mu, unpacked, pos);
      }

      src = vlog_lvalue_source(g, &(lvalues[i]), src);

      if (mir_is_null(after))
         mir_build_put_driver(g->mu, nets, lvalues[i].count, src);
      else
         mir_build_sched_waveform(g->mu, nets, lvalues[i].count, src, after,
                                  after);
   }

   if (polled) {
      vlog_lower_poll_expr(g, rhs);
      mir_build_jump(g->mu, start_bb);
   }
   else
      mir_build_wait(g->mu, start_bb);

   if (lvalues != &lvalue1)
      free(lvalues);
}

static void vlog_lower_var_init_process(vlog_gen_t *g, vlog_node_t v)
{
   mir_block_t start_bb = mir_add_block(g->mu);
   assert(start_bb.id == 1);

   mir_build_return(g->mu, MIR_NULL_VALUE);

   mir_set_cursor(g->mu, start_bb, MIR_APPEND);

   int hops;
   mir_value_t var = mir_search_object(g->mu, v, &hops);
   assert(!mir_is_null(var));
   assert(hops > 0);

   const type_info_t *ti = vlog_type_info(g, vlog_type(v));
   mir_value_t value = vlog_lower_with_context(g, vlog_value(v), ti->type);
   mir_value_t upref = mir_build_var_upref(g->mu, hops, var.id);
   mir_build_store(g->mu, upref, vlog_lower_cast(g, ti->type, value));

   mir_build_return(g->mu, MIR_NULL_VALUE);
}

static void vlog_lower_net_process(vlog_gen_t *g, vlog_node_t v)
{
   mir_block_t start_bb = mir_add_block(g->mu);
   assert(start_bb.id == 1);

   int hops;
   mir_value_t var = mir_search_object(g->mu, v, &hops);
   assert(!mir_is_null(var));
   assert(hops > 0);

   const type_info_t *ti = vlog_type_info(g, vlog_type(v));

   mir_type_t t_offset = mir_offset_type(g->mu);

   // A value held in memory is polled as it has no events
   const bool polled = vlog_uses_memory_member(vlog_value(v));

   {
      mir_value_t upref = mir_build_var_upref(g->mu, hops, var.id);
      mir_value_t nets = mir_build_load(g->mu, upref);
      mir_value_t count = mir_const(g->mu, t_offset, ti->size);
      mir_build_drive_signal(g->mu, nets, count);

      if (!polled)
         vlog_lower_sensitivity(g, vlog_value(v));

      mir_build_return(g->mu, MIR_NULL_VALUE);
   }

   mir_set_cursor(g->mu, start_bb, MIR_APPEND);

   {
      mir_value_t value = vlog_lower_with_context(g, vlog_value(v), ti->type);
      mir_value_t resize = vlog_lower_cast(g, ti->type, value);

      mir_value_t tmp = MIR_NULL_VALUE;
      if (ti->size != 1) {
         mir_type_t t_elem = mir_logic_type(g->mu);
         mir_type_t t_array = mir_carray_type(g->mu, ti->size, t_elem);
         tmp = vlog_get_temp(g, t_array);
      }

      mir_value_t upref = mir_build_var_upref(g->mu, hops, var.id);
      mir_value_t nets = mir_build_load(g->mu, upref);
      mir_value_t count = mir_const(g->mu, t_offset, ti->size);

      uint8_t strength = ST_STRONG;
      if (vlog_params(v) > 0)
         strength = vlog_subkind(vlog_param(v, 0));

      mir_value_t unpacked = mir_build_unpack(g->mu, resize, strength, tmp);
      if (vlog_has_delay(v)) {
         mir_value_t after =
            vlog_lower_net_delay(g, vlog_delay(v), resize, ti->size);
         mir_build_sched_waveform(g->mu, nets, count, unpacked, after, after);
      }
      else
         mir_build_put_driver(g->mu, nets, count, unpacked);

      if (polled) {
         vlog_lower_poll_expr(g, vlog_value(v));
         mir_build_jump(g->mu, start_bb);
      }
      else
         mir_build_wait(g->mu, start_bb);
   }
}

static mir_value_t vlog_lower_gate_delay(vlog_gen_t *g, vlog_node_t ctrl,
                                         mir_value_t value)
{
   // The delay is for a rising, falling, or turn-off transition depending
   // on the new value of the output

   const int64_t ival = vlog_ival(ctrl);
   mir_value_t rise = vlog_lower_scaled_time(g, vlog_value(ctrl), ival);

   const int nextra = vlog_params(ctrl);
   if (nextra == 0)
      return rise;

   mir_type_t t_time = mir_time_type(g->mu);
   mir_type_t t_logic = mir_vec4_type(g->mu, 1, false);

   mir_value_t fall = vlog_lower_scaled_time(g, vlog_param(ctrl, 0), ival);

   mir_value_t least = mir_build_select(
      g->mu, t_time, mir_build_cmp(g->mu, MIR_CMP_LT, fall, rise), fall, rise);

   mir_value_t toff = least;
   if (nextra > 1) {
      toff = vlog_lower_scaled_time(g, vlog_param(ctrl, 1), ival);
      least = mir_build_select(
         g->mu, t_time, mir_build_cmp(g->mu, MIR_CMP_LT, toff, least), toff,
         least);
   }

   mir_value_t is1 = vlog_lower_test(
      g, mir_build_binary(g->mu, MIR_VEC_CASE_EQ, t_logic, value,
                          mir_const_vec(g->mu, t_logic, 1, 0)));
   mir_value_t is0 = vlog_lower_test(
      g, mir_build_binary(g->mu, MIR_VEC_CASE_EQ, t_logic, value,
                          mir_const_vec(g->mu, t_logic, 0, 0)));
   mir_value_t isz = vlog_lower_test(
      g, mir_build_binary(g->mu, MIR_VEC_CASE_EQ, t_logic, value,
                          mir_const_vec(g->mu, t_logic, 0, 1)));

   mir_value_t result = mir_build_select(g->mu, t_time, isz, toff, least);
   result = mir_build_select(g->mu, t_time, is0, fall, result);
   return mir_build_select(g->mu, t_time, is1, rise, result);
}

static mir_value_t vlog_lower_net_delay(vlog_gen_t *g, vlog_node_t ctrl,
                                        mir_value_t value, unsigned size)
{
   if (vlog_params(ctrl) == 0)
      return vlog_lower_delay(g, ctrl);
   else if (size != 1)
      fatal_at(vlog_loc(ctrl), "sorry, separate rise, fall, and turn-off "
               "delays are only supported for a single bit");

   return vlog_lower_gate_delay(g, ctrl, value);
}

static vlog_node_t vlog_decl_delay(vlog_node_t v)
{
   vlog_node_t target = vlog_target(v);
   if (vlog_kind(target) == V_REF && vlog_has_ref(target)) {
      vlog_node_t decl = vlog_ref(target);
      if (vlog_kind(decl) == V_NET_DECL && vlog_has_delay(decl))
         return vlog_delay(decl);
   }

   return NULL;
}

static vlog_node_t vlog_assign_delay(vlog_node_t v)
{
   // A delay in a net declaration applies to all drivers of the net
   // in addition to the delay of the assignment

   if (vlog_has_delay(v))
      return vlog_delay(v);

   return vlog_decl_delay(v);
}

static void vlog_lower_gate_inst(vlog_gen_t *g, vlog_node_t v)
{
   mir_block_t start_bb = mir_add_block(g->mu);
   assert(start_bb.id == 1);

   vlog_lower_driver(g, vlog_target(v));

   if (vlog_has_delay(v))
      vlog_lower_initial_x(g, vlog_target(v));

   mir_type_t t_logic = mir_vec4_type(g->mu, 1, false);

   const int nparams = vlog_params(v);
   int first_term = 0;
   for (int i = 0; i < nparams; i++) {
      vlog_node_t p = vlog_param(v, i);
      if (vlog_kind(p) == V_STRENGTH)
         first_term = i + 1;
      else
         vlog_lower_sensitivity(g, p);
   }

   mir_build_return(g->mu, MIR_NULL_VALUE);

   mir_set_cursor(g->mu, start_bb, MIR_APPEND);

   bool negate = false;
   uint8_t strength = ST_STRONG;
   mir_value_t value = MIR_NULL_VALUE;
   const vlog_gate_kind_t kind = vlog_subkind(v);
   switch (kind) {
   case V_GATE_PULLUP:
   case V_GATE_PULLDOWN:
      strength = vlog_subkind(vlog_param(v, 0));
      value = mir_const_vec(g->mu, t_logic, kind == V_GATE_PULLUP, 0);
      break;

   case V_GATE_NAND:
   case V_GATE_NOR:
   case V_GATE_XNOR:
      negate = true;
   case V_GATE_AND:
   case V_GATE_OR:
   case V_GATE_XOR:
      {
         static const mir_vec_op_t op_map[] = {
            [V_GATE_AND] = MIR_VEC_BIT_AND,
            [V_GATE_NAND] = MIR_VEC_BIT_AND,
            [V_GATE_OR] = MIR_VEC_BIT_OR,
            [V_GATE_NOR] = MIR_VEC_BIT_OR,
            [V_GATE_XOR] = MIR_VEC_BIT_XOR,
            [V_GATE_XNOR] = MIR_VEC_BIT_XOR,
         };

         value = vlog_lower_rvalue(g, vlog_param(v, first_term));
         value = vlog_lower_cast(g, t_logic, value);

         const int nelems = nparams - first_term;
         for (int i = 1; i < nelems; i++) {
            vlog_node_t p = vlog_param(v, first_term + i);
            mir_value_t arg = vlog_lower_rvalue(g, p);
            mir_value_t cast = vlog_lower_cast(g, t_logic, arg);
            value = mir_build_binary(g->mu, op_map[kind], t_logic, value, cast);
         }

         if (negate)
            value = mir_build_unary(g->mu, MIR_VEC_BIT_NOT, t_logic, value);
      }
      break;

   case V_GATE_NOT:
      {
         mir_value_t input = vlog_lower_rvalue(g, vlog_param(v, nparams - 1));
         mir_value_t cast = vlog_lower_cast(g, t_logic, input);
         value = mir_build_unary(g->mu, MIR_VEC_BIT_NOT, t_logic, cast);
      }
      break;

   case V_GATE_BUF:
      {
         // Invert twice for correct X/Z behaviour
         mir_value_t input = vlog_lower_rvalue(g, vlog_param(v, nparams - 1));
         mir_value_t cast = vlog_lower_cast(g, t_logic, input);
         value = mir_build_unary(g->mu, MIR_VEC_BIT_NOT, t_logic, cast);
         value = mir_build_unary(g->mu, MIR_VEC_BIT_NOT, t_logic, value);
      }
      break;

   case V_GATE_NOTIF0:
   case V_GATE_NOTIF1:
      negate = true;
      // Fall-through
   case V_GATE_BUFIF0:
   case V_GATE_BUFIF1:
      {
         mir_value_t input = vlog_lower_rvalue(g, vlog_param(v, first_term));
         mir_value_t cast = vlog_lower_cast(g, t_logic, input);

         if (negate)
            cast = mir_build_unary(g->mu, MIR_VEC_BIT_NOT, t_logic, cast);

         mir_value_t enable =
            vlog_lower_rvalue(g, vlog_param(v, first_term + 1));
         mir_value_t enable_cast = vlog_lower_cast(g, t_logic, enable);

         const bool active_high =
            kind == V_GATE_BUFIF1 || kind == V_GATE_NOTIF1;
         mir_value_t active_value =
            mir_const_vec(g->mu, t_logic, active_high, 0);
         mir_value_t active_vec =
            mir_build_binary(g->mu, MIR_VEC_CASE_EQ, t_logic, enable_cast,
                             active_value);
         mir_value_t active = vlog_lower_test(g, active_vec);
         mir_value_t inactive_value =
            mir_const_vec(g->mu, t_logic, !active_high, 0);
         mir_value_t inactive_vec =
            mir_build_binary(g->mu, MIR_VEC_CASE_EQ, t_logic, enable_cast,
                             inactive_value);
         mir_value_t inactive = vlog_lower_test(g, inactive_vec);
         mir_value_t highz = mir_const_vec(g->mu, t_logic, 0, 1);
         mir_value_t unknown = mir_const_vec(g->mu, t_logic, 1, 1);

         mir_value_t off =
            mir_build_select(g->mu, t_logic, inactive, highz, unknown);
         value = mir_build_select(g->mu, t_logic, active, cast, off);
      }
      break;

   default:
      CANNOT_HANDLE(v);
   }

   mir_value_t unpacked =
      mir_build_unpack(g->mu, value, strength, MIR_NULL_VALUE);

   vlog_select_t lvalue = vlog_lower_select(g, vlog_target(v));

   mir_value_t nets =
      mir_build_array_ref(g->mu, lvalue.obj, lvalue.dst_offset);

   if (vlog_has_delay(v)) {
      mir_value_t after = vlog_lower_gate_delay(g, vlog_delay(v), value);
      mir_build_sched_waveform(g->mu, nets, lvalue.count, unpacked, after,
                               after);
   }
   else
      mir_build_put_driver(g->mu, nets, lvalue.count, unpacked);

   mir_build_wait(g->mu, start_bb);
}

static void vlog_lower_real_array_port_map(vlog_gen_t *g, vlog_node_t v,
                                           mir_value_t signal,
                                           mir_block_t start_bb)
{
   // The elements of an unpacked array of real are separate signals so
   // are copied as they are

   vlog_node_t port = vlog_ref(v);
   vlog_node_t value = vlog_value(v);

   if (vlog_kind(value) != V_REF || vlog_uses_memory_member(value))
      fatal_at(vlog_loc(value), "sorry, this expression cannot be connected "
               "to a port which is an unpacked array of real");

   const bool input = vlog_subkind(port) == V_PORT_INPUT;

   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_value_t count = mir_const(g->mu, t_offset, vlog_size(port));

   vlog_select_t lvalue = vlog_lower_select(g, value);
   if (vlog_whole_size(g, value, &lvalue) != vlog_size(port))
      fatal_at(vlog_loc(value), "sorry, the array connected to this port "
               "must have the same number of elements");

   mir_value_t nets = mir_build_array_ref(g->mu, lvalue.obj, lvalue.dst_offset);

   if (input) {
      mir_build_drive_signal(g->mu, signal, count);
      mir_build_sched_event(g->mu, nets, count);
   }
   else {
      mir_build_drive_signal(g->mu, nets, count);
      mir_build_sched_event(g->mu, signal, count);
   }

   mir_build_return(g->mu, MIR_NULL_VALUE);

   mir_set_cursor(g->mu, start_bb, MIR_APPEND);

   // Values in registers do not survive suspending
   lvalue = vlog_lower_select(g, value);
   nets = mir_build_array_ref(g->mu, lvalue.obj, lvalue.dst_offset);

   mir_value_t src = mir_build_resolved(g->mu, input ? nets : signal);
   mir_build_put_driver(g->mu, input ? signal : nets, count, src);
   mir_build_wait(g->mu, start_bb);
}

static bool vlog_lower_net_port_input(vlog_gen_t *g, vlog_node_t v,
                                      mir_value_t signal,
                                      const type_info_t *ti)
{
   // A net connected to an input port passes on its drive strength

   vlog_node_t actual = vlog_value(v);
   switch (vlog_kind(actual)) {
   case V_REF:
   case V_BIT_SELECT:
   case V_PART_SELECT:
      break;
   default:
      return false;
   }

   vlog_node_t root = actual;
   while (vlog_kind(root) == V_BIT_SELECT || vlog_kind(root) == V_PART_SELECT)
      root = vlog_value(root);

   if (vlog_kind(root) != V_REF || !vlog_has_ref(root)
       || vlog_kind(vlog_ref(root)) != V_NET_DECL
       || mir_get_class(g->mu, ti->type) != MIR_TYPE_VEC4)
      return false;

   mir_type_t t_offset = mir_offset_type(g->mu);

   vlog_select_t src = vlog_lower_select(g, actual);
   if (!mir_is_signal(g->mu, src.obj) || src.size != ti->size)
      return false;

   mir_type_t t_net_value = mir_int_type(g->mu, 0, 255);
   if (!mir_equals(mir_get_base(g->mu, mir_get_type(g->mu, src.obj)),
                   t_net_value))
      return false;

   int64_t count_const;
   if (!mir_get_const(g->mu, src.count, &count_const)
       || count_const != src.size)
      return false;

   mir_value_t nets = mir_build_array_ref(g->mu, src.obj, src.dst_offset);
   mir_value_t resolved = mir_build_resolved(g->mu, nets);

   mir_value_t count = mir_const(g->mu, t_offset, ti->size);
   mir_build_put_driver(g->mu, signal, count, resolved);
   return true;
}

static bool vlog_is_memory_port(vlog_node_t port)
{
   // A port of a type which has no representation as a signal is a
   // variable of the instance connected by a process of each instance
   // of the module (23.3.3.2, 23.3.3.3)

   vlog_node_t d = vlog_ref(port);
   if (vlog_kind(d) != V_VAR_DECL || !vlog_is_dynamic_type(vlog_type(d))
       || vlog_nettype_of(port) != NULL)
      return false;

   // An input of event type is connected to an event expression and the
   // formals of a checker may be replaced by their actuals
   vlog_node_t type = vlog_get_type(vlog_type(d));
   if (vlog_subkind(port) == V_PORT_INPUT && vlog_kind(type) == V_DATA_TYPE
       && vlog_subkind(type) == DT_EVENT)
      return false;

   return true;
}

static bool vlog_is_event_port(vlog_node_t port)
{
   vlog_node_t type = vlog_get_type(vlog_type(vlog_ref(port)));
   return type != NULL && vlog_kind(type) == V_DATA_TYPE
      && vlog_subkind(type) == DT_EVENT;
}

static void vlog_lower_memory_port_map(vlog_gen_t *g, vlog_node_t v)
{
   // The process which continuously assigns an input variable port from
   // its actual or the actual from an output variable port

   vlog_node_t port = vlog_ref(v);
   vlog_node_t d = vlog_ref(port);
   vlog_node_t type = vlog_get_type(vlog_type(d));
   vlog_node_t actual = vlog_value(v);

   const type_info_t *ti = vlog_type_info(g, type);

   mir_type_t t_ptr = mir_pointer_type(g->mu, ti->type);
   mir_value_t ptr = mir_add_param(g->mu, t_ptr, MIR_NULL_STAMP,
                                   vlog_ident(port));

   mir_block_t start_bb = mir_add_block(g->mu);
   assert(start_bb.id == 1);

   mir_build_return(g->mu, MIR_NULL_VALUE);

   mir_set_cursor(g->mu, start_bb, MIR_APPEND);

   switch (vlog_subkind(port)) {
   case V_PORT_INPUT:
      {
         mir_value_t value = vlog_lower_with_context(g, actual, ti->type);
         mir_value_t cast = vlog_lower_cast(g, ti->type, value);
         mir_build_store(g->mu, ptr, vlog_poll_copy(g, type, cast));
         vlog_lower_poll_expr(g, actual);
      }
      break;
   case V_PORT_OUTPUT:
      {
         mir_value_t value =
            vlog_poll_copy(g, type, mir_build_load(g->mu, ptr));
         vlog_assign_variable(g, actual, value, NULL);
         vlog_lower_poll_value_wait(g, d, ptr, type);
      }
      break;
   default:
      should_not_reach_here();
   }

   mir_build_jump(g->mu, start_bb);
}

static void vlog_lower_port_map(vlog_gen_t *g, vlog_node_t v)
{
   vlog_node_t port = vlog_ref(v);
   assert(vlog_kind(port) == V_PORT_DECL);

   if (vlog_is_memory_port(port)) {
      vlog_lower_memory_port_map(g, v);
      return;
   }

   if (vlog_has_type(port) && vlog_kind(vlog_type(port)) == V_TYPE_DECL
       && (vlog_flags(vlog_type(port)) & VLOG_F_NETTYPE))
      fatal_at(vlog_loc(v), "sorry, a net of a user-defined net type "
               "cannot be connected through a module port");

   const type_info_t *ti = vlog_type_info(g, vlog_type(vlog_ref(port)));
   if (mir_is_null(ti->signal) && vlog_is_dynamic_type(vlog_type(vlog_ref(port))))
      fatal_at(vlog_loc(v), "sorry, a port of type event, string, class or "
               "dynamic array is not supported");

   const bool real_array = mir_get_class(g->mu, ti->type) == MIR_TYPE_REAL
      && vlog_size(port) > 1;

   if (real_array && vlog_subkind(port) != V_PORT_INPUT
       && vlog_subkind(port) != V_PORT_OUTPUT)
      fatal_at(vlog_loc(v), "sorry, an inout port which is an unpacked array "
               "of real is not supported");

   mir_value_t signal = mir_add_param(g->mu, ti->signal, MIR_NULL_STAMP,
                                      vlog_ident(port));

   mir_type_t t_offset = mir_offset_type(g->mu);

   mir_block_t start_bb = mir_add_block(g->mu);
   assert(start_bb.id == 1);

   if (real_array) {
      vlog_lower_real_array_port_map(g, v, signal, start_bb);
      return;
   }

   // A port connected to a member held in memory cannot use a driver or
   // wait for an event on it
   const bool in_memory = vlog_uses_memory_member(vlog_value(v));

   {
      switch (vlog_subkind(port)) {
      case V_PORT_INPUT:
         {
            if (!in_memory)
               vlog_lower_sensitivity(g, vlog_value(v));

            mir_value_t count = mir_const(g->mu, t_offset, ti->size);
            mir_build_drive_signal(g->mu, signal, count);
         }
         break;
      case V_PORT_OUTPUT:
         {
            if (!in_memory)
               vlog_lower_driver(g, vlog_value(v));

            mir_value_t count = mir_const(g->mu, t_offset, ti->size);
            mir_build_sched_event(g->mu, signal, count);
         }
         break;
      default:
         fatal_at(vlog_loc(v), "sorry, an inout port connected to this "
                  "expression is not supported");
      }

      mir_build_return(g->mu, MIR_NULL_VALUE);
   }

   mir_set_cursor(g->mu, start_bb, MIR_APPEND);

   unsigned nlvalues = 1, targetsz = 0;
   vlog_select_t lvalue1, *lvalues = &lvalue1;
   mir_value_t resize;

   switch(vlog_subkind(port)) {
   case V_PORT_OUTPUT:
      {
         vlog_node_t target = vlog_value(v);

         if (in_memory) {
            mir_value_t resolved = mir_build_resolved(g->mu, signal);
            mir_value_t value;
            if (mir_get_class(g->mu, ti->type) == MIR_TYPE_REAL)
               value = mir_build_load(g->mu, resolved);
            else {
               value = mir_build_pack(g->mu, ti->type, resolved);
               value = mir_build_cast(
                  g->mu, mir_vec4_type(g->mu, vlog_width(target), false),
                  value);
            }

            vlog_assign_variable(g, target, value, NULL);
            mir_build_wait(g->mu, start_bb);
            return;
         }

         if (vlog_kind(target) == V_CONCAT) {
            nlvalues = vlog_count_lvalues(target);
            lvalues = xmalloc_array(nlvalues, sizeof(vlog_select_t));

            unsigned index = 0;
            vlog_lower_lvalues(g, target, lvalues, NULL, &index);
            assert(index == nlvalues);

            for (int i = 0; i < nlvalues; i++)
               targetsz += lvalues[i].size;
         }
         else {
            lvalue1 = vlog_lower_select(g, target);
            targetsz = lvalue1.size;
         }

         mir_type_t t_vec = mir_vec4_type(g->mu, targetsz, false);

         mir_value_t resolved = mir_build_resolved(g->mu, signal);
         mir_value_t value = mir_build_pack(g->mu, ti->type, resolved);

         assert(mir_is_vector(g->mu, value));

         resize = mir_build_cast(g->mu, t_vec, value);
      }
      break;

   case V_PORT_INPUT:
      {
         if (!in_memory && vlog_lower_net_port_input(g, v, signal, ti)) {
            mir_build_wait(g->mu, start_bb);
            return;
         }

         mir_value_t value;
         if (vlog_kind(vlog_value(v)) == V_REF
             && vlog_is_sized_dynarray(vlog_value(v)))
            value = vlog_flatten_dynarray(
               g, vlog_value(v), vlog_lower_rvalue(g, vlog_value(v)),
               vlog_ranges(port) == 0);
         else
            value = vlog_lower_with_context(g, vlog_value(v), ti->type);
         assert(mir_is_vector(g->mu, value)
                || mir_is(g->mu, value, MIR_TYPE_REAL));

         if (mir_get_class(g->mu, ti->type) == MIR_TYPE_REAL)
            resize = vlog_lower_cast(g, ti->type, value);
         else
            resize = mir_build_cast(g->mu, ti->type, value);

         mir_value_t zero = mir_const(g->mu, t_offset, 0);

         targetsz = ti->size;
         lvalue1 = (vlog_select_t){
            .obj        = signal,
            .size       = ti->size,
            .dst_offset = zero,
            .count      = mir_const(g->mu, t_offset, ti->size),
            .src_offset = zero,
            .type       = ti->type,
         };
      }
      break;

   default:
      should_not_reach_here();
   }

   mir_value_t tmp = MIR_NULL_VALUE;
   if (targetsz != 1) {
      mir_type_t t_array = mir_carray_type(g->mu, targetsz, ti->unpacked);
      tmp = vlog_get_temp(g, t_array);
   }

   mir_value_t unpacked = mir_build_unpack(g->mu, resize, ST_STRONG, tmp);

   for (int i = 0, offset = 0; i < nlvalues; offset += lvalues[i].size, i++) {
      mir_value_t nets = mir_build_array_ref(g->mu, lvalues[i].obj,
                                             lvalues[i].dst_offset);

      mir_value_t pos = mir_build_add(g->mu, t_offset,
                                      mir_const(g->mu, t_offset, offset),
                                      lvalues[i].src_offset);
      mir_value_t src = mir_build_array_ref(g->mu, unpacked, pos);

      mir_build_put_driver(g->mu, nets, lvalues[i].count, src);
   }

   if (lvalues != &lvalue1)
      free(lvalues);

   if (in_memory) {
      vlog_lower_poll_expr(g, vlog_value(v));
      mir_build_jump(g->mu, start_bb);
   }
   else
      mir_build_wait(g->mu, start_bb);
}

static mir_value_t vlog_lower_tcheck_event(vlog_gen_t *g, vlog_node_t v)
{
   assert (vlog_kind(v) == V_TCHECK_EVENT);

   vlog_node_t sig = vlog_param(v, 0);

   mir_value_t cond;
   if (vlog_kind(sig) == V_EVENT && vlog_subkind(sig) != V_EVENT_LEVEL) {
      // Only a transition of the given direction in this time step is an
      // event (31.2)
      vlog_select_t lvalue = vlog_lower_select(g, vlog_value(sig));

      const bool vhdl = vlog_is_vhdl_signal(g, lvalue.obj);
      const bool posedge = vlog_subkind(sig) == V_EVENT_POSEDGE;

      ident_t func = ident_new(
         posedge ? (vhdl ? "@posedge$vhdl$step" : "@posedge$step")
         : (vhdl ? "@negedge$vhdl$step" : "@negedge$step"));

      mir_lower_fn_t fn = posedge
         ? (vhdl ? vlog_lower_vhdl_step_posedge_fn : vlog_lower_step_posedge_fn)
         : (vhdl ? vlog_lower_vhdl_step_negedge_fn : vlog_lower_step_negedge_fn);

      mir_defer(mir_get_context(g->mu), func, NULL, MIR_UNIT_FUNCTION, fn,
                NULL);

      mir_type_t t_context = mir_context_type(g->mu, func);
      mir_value_t context = mir_build_null(g->mu, t_context);
      mir_value_t nets =
         mir_build_array_ref(g->mu, lvalue.obj, lvalue.dst_offset);
      mir_value_t args[] = { context, nets };
      cond = mir_build_fcall(g->mu, func, mir_bool_type(g->mu),
                             MIR_NULL_STAMP, args, 2);
   }
   else {
      if (vlog_kind(sig) == V_EVENT)
         sig = vlog_value(sig);

      mir_type_t t_offset = mir_offset_type(g->mu);
      mir_type_t t_time = mir_time_type(g->mu);
      mir_value_t t_zero = mir_const(g->mu, t_time, 0);

      vlog_select_t lvalue = vlog_lower_select(g, sig);
      int total_size = vlog_whole_size(g, sig, &lvalue);
      mir_value_t count = mir_const(g->mu, t_offset, total_size);
      mir_value_t last_event = mir_build_last_event(g->mu, lvalue.obj, count);
      cond = mir_build_cmp(g->mu, MIR_CMP_EQ, last_event, t_zero);
   }

   if (vlog_params(v) > 1) {
      mir_value_t scond = vlog_lower_rvalue(g, vlog_param(v, 1));
      mir_value_t test = vlog_lower_test(g, scond);
      cond = mir_build_and(g->mu, cond, test);
   }

   return cond;
}

static void vlog_lower_tcheck(vlog_gen_t *g, vlog_node_t tcheck)
{
   vlog_tcheck_kind_t tkind = vlog_subkind(tcheck);

   switch (tkind) {
   case V_TCHECK_SETUP:
   case V_TCHECK_HOLD:
   case V_TCHECK_RECOVERY:
   case V_TCHECK_REMOVAL:
      {
         assert(vlog_params(tcheck) >= 3);

         // IEEE 1800-2017, 31.3
         //             ev_0   ev_1     Fail condition
         // -----------------------------------------------------------------
         // $setup     stamp   check   check - limit <  stamp < check
         // $hold      stamp   check   stamp         <= check < stamp + limit
         // $removal   check   stamp   check - limit <  stamp < check
         // $recovery  stamp   check   stamp         <= check < stamp + limit
         //
         // The ineqalities convert to:
         //    limit >  check - stamp  >  0
         //    limit >  check - stamp  >= 0

         int stamp_index = (tkind == V_TCHECK_REMOVAL) ? 1 : 0;
         int check_index = (tkind == V_TCHECK_REMOVAL) ? 0 : 1;

         bool fail_simul = (tkind == V_TCHECK_HOLD ||
                            tkind == V_TCHECK_RECOVERY);

         vlog_node_t v_stamp = vlog_param(tcheck, stamp_index);
         vlog_node_t v_check = vlog_param(tcheck, check_index);
         vlog_node_t v_limit = vlog_param(tcheck, 2);

         assert(vlog_kind(v_stamp) == V_TCHECK_EVENT);
         assert(vlog_kind(v_check) == V_TCHECK_EVENT);

         vlog_node_t sig_stamp = vlog_param(v_stamp, 0);
         vlog_node_t sig_check = vlog_param(v_check, 0);

         mir_value_t trig_stamp = vlog_lower_trigger(g, sig_stamp);
         mir_value_t trig_check = vlog_lower_trigger(g, sig_check);

         mir_value_t trigger = vlog_or_triggers(g, 2, trig_stamp, trig_check);
         assert(!mir_is_null(trigger));

         mir_type_t t_trigger = mir_trigger_type(g->mu);
         mir_value_t trigger_var =
            mir_add_var(g->mu, t_trigger, MIR_NULL_STAMP, ident_new("trigger"), 0);
         mir_build_store(g->mu, trigger_var, trigger);

         mir_type_t t_time = mir_time_type(g->mu);
         mir_value_t t_zero = mir_const(g->mu, t_time, 0);
         mir_value_t stamp_var = mir_add_var(g->mu, t_time,  MIR_NULL_STAMP,
                                             ident_new("stamp"), 0);
         mir_build_store(g->mu, stamp_var, t_zero);
         mir_build_return(g->mu, MIR_NULL_VALUE);

         mir_block_t start_bb = mir_add_block(g->mu);
         assert(start_bb.id == 1);

         mir_set_cursor(g->mu, start_bb, MIR_APPEND);

         mir_value_t t = mir_build_load(g->mu, trigger_var);
         mir_build_sched_event(g->mu, t, MIR_NULL_VALUE);

         mir_block_t wait_bb = mir_add_block(g->mu);
         mir_build_wait(g->mu, wait_bb);
         mir_set_cursor(g->mu, wait_bb, MIR_APPEND);

         mir_value_t t2 = mir_build_load(g->mu, trigger_var);
         mir_build_clear_event(g->mu, t2, MIR_NULL_VALUE);

         // Get current time
         ident_t now_func =
            ident_new("STD.STANDARD.NOW()25STD.STANDARD.DELAY_LENGTH");
         mir_value_t pkg = mir_build_link_package(g->mu, well_known(W_STD_STANDARD));
         mir_value_t args[1] = { pkg };
         mir_value_t now = mir_build_fcall(g->mu, now_func, t_time, MIR_NULL_STAMP,
                                           args, ARRAY_LEN(args));

         // Check if events occured at this simulation time
         mir_value_t stamp_cond = vlog_lower_tcheck_event(g, v_stamp);
         mir_value_t check_cond = vlog_lower_tcheck_event(g, v_check);

         mir_block_t check_bb = mir_add_block(g->mu);
         mir_block_t stamp_bb = mir_add_block(g->mu);
         mir_build_cond(g->mu, check_cond, check_bb, stamp_bb);

         // Check time limit if check was active
         mir_set_cursor(g->mu, check_bb, MIR_APPEND);
         mir_value_t stamp = mir_build_load(g->mu, stamp_var);
         mir_value_t diff_time = mir_build_sub(g->mu, t_time, now, stamp);

         mir_value_t limit_time =
            vlog_lower_scaled_time(g, v_limit, vlog_ival(tcheck));
         mir_value_t ok = mir_build_cmp(g->mu, MIR_CMP_GEQ, diff_time, limit_time);

         mir_value_t stamp_display = stamp;
         if (fail_simul) {
            // A zero limit is an empty interval so cannot be violated
            mir_value_t positive =
               mir_build_cmp(g->mu, MIR_CMP_GT, limit_time, t_zero);
            mir_value_t simul = mir_build_and(g->mu, stamp_cond, positive);
            ok = mir_build_and(g->mu, ok, mir_build_not(g->mu, simul));
            stamp_display = mir_build_select(g->mu, t_time, stamp_cond,
                                             now, stamp);
         }

         mir_block_t fire_bb = mir_add_block(g->mu);
         mir_build_cond(g->mu, ok, stamp_bb, fire_bb);

         mir_set_cursor(g->mu, fire_bb, MIR_APPEND);
         mir_value_t locus = mir_build_debug_locus(g->mu, vlog_to_object(tcheck));
         mir_build_tcheck(g->mu, stamp_display, limit_time, locus);
         if (vlog_params(tcheck) > 3)
            vlog_lower_blocking_assignment(g, vlog_param(tcheck, 3));
         mir_build_jump(g->mu, stamp_bb);

         // Store time if stamp was active
         mir_set_cursor(g->mu, stamp_bb, MIR_APPEND);
         mir_block_t sample_bb = mir_add_block(g->mu);
         mir_build_cond(g->mu, stamp_cond, sample_bb, start_bb);

         mir_set_cursor(g->mu, sample_bb, MIR_APPEND);
         mir_build_store(g->mu, stamp_var, now);
         mir_build_jump(g->mu, start_bb);
      }
      break;

   // TODO: Support other timing check kinds
   default:
      break;
   }
}

static void vlog_lower_cleanup(vlog_gen_t *g)
{
   if (g->temps != NULL)
      ihash_free(g->temps);
}

static void vlog_lower_deferred(mir_unit_t *mu, object_t *obj)
{
   vlog_node_t v = vlog_from_object(obj);
   assert(v != NULL);

   vlog_gen_t g = {
      .mu  = mu,
      .mir = mir_get_context(mu),
   };

   switch (vlog_kind(v)) {
   case V_ALWAYS:
      vlog_lower_always(&g, v);
      break;
   case V_INITIAL:
      vlog_lower_initial(&g, v);
      break;
   case V_FINAL:
      vlog_lower_final(&g, v);
      break;
   case V_ASSIGN:
      if (!vlog_lower_struct_net_driver(&g, v))
         vlog_lower_assign_process(&g, v);
      break;
   case V_NET_DECL:
      vlog_lower_net_process(&g, v);
      break;
   case V_VAR_DECL:
      if (vlog_struct_nettype_of(v) != NULL)
         vlog_lower_struct_net_process(&g, v);
      else
         vlog_lower_var_init_process(&g, v);
      break;
   case V_GATE_INST:
      vlog_lower_gate_inst(&g, v);
      break;
   case V_PORT_MAP:
      vlog_lower_port_map(&g, v);
      break;
   case V_PORT_DECL:
      vlog_lower_port_xinit(&g, v);
      break;
   case V_TCHECK:
      vlog_lower_tcheck(&g, v);
      break;
   default:
      CANNOT_HANDLE(v);
   }

   mir_optimise(mu, MIR_PASS_O1);

   vlog_lower_cleanup(&g);
}

static void vlog_lower_port_xinit(vlog_gen_t *g, vlog_node_t v)
{
   // A variable connected to an output port is X until it is assigned
   // but the net it is connected to is Z

   mir_block_t start_bb = mir_add_block(g->mu);
   assert(start_bb.id == 1);

   const type_info_t *ti = vlog_type_info(g, vlog_type(vlog_ref(v)));

   int hops;
   mir_value_t var = mir_search_object(g->mu, v, &hops);
   assert(!mir_is_null(var));

   mir_build_return(g->mu, MIR_NULL_VALUE);

   mir_set_cursor(g->mu, start_bb, MIR_APPEND);

   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_value_t upref = hops > 0 ? mir_build_var_upref(g->mu, hops, var.id)
      : var;
   mir_value_t nets = mir_build_load(g->mu, upref);

   // Something which already drives the variable, such as an output of a
   // submodule, takes precedence
   mir_type_t t_net_value = mir_int_type(g->mu, 0, 255);
   mir_block_t skip_bb = mir_add_block(g->mu);

   mir_value_t resolved = mir_build_resolved(g->mu, nets);
   mir_value_t zee = mir_const(g->mu, t_net_value, LOGIC_Z);

   vlog_loop_t l;
   vlog_loop_begin(g, &l, mir_const(g->mu, t_offset, 0),
                   mir_const(g->mu, t_offset, ti->size));

   mir_value_t elem = mir_is(g->mu, resolved, MIR_TYPE_POINTER)
      ? mir_build_load(g->mu, mir_build_array_ref(g->mu, resolved, l.i))
      : resolved;

   mir_block_t cont_bb = mir_add_block(g->mu);
   mir_build_cond(g->mu, mir_build_cmp(g->mu, MIR_CMP_EQ, elem, zee),
                  cont_bb, skip_bb);

   mir_set_cursor(g->mu, cont_bb, MIR_APPEND);
   vlog_loop_end(g, &l);

   mir_value_t x = vlog_lower_x(g, ti->type);
   mir_value_t tmp = MIR_NULL_VALUE;
   if (ti->size > 1) {
      mir_type_t t_array = mir_carray_type(g->mu, ti->size,
                                           mir_logic_type(g->mu));
      tmp = mir_add_var(g->mu, t_array, MIR_NULL_STAMP, ident_uniq("tmp"),
                        MIR_VAR_TEMP);
   }

   mir_value_t unpacked = mir_build_unpack(g->mu, x, 0, tmp);
   mir_build_deposit_signal(g->mu, nets, mir_const(g->mu, t_offset, ti->size),
                            unpacked);

   mir_build_return(g->mu, MIR_NULL_VALUE);

   mir_set_cursor(g->mu, skip_bb, MIR_APPEND);
   mir_build_return(g->mu, MIR_NULL_VALUE);
}

typedef struct {
   vlog_node_t decl;
   vlog_node_t port;
   bool        found;
   bool        inside;
} driver_search_t;

static void vlog_find_ref_cb(vlog_node_t v, void *ctx)
{
   driver_search_t *s = ctx;
   if (vlog_kind(v) == V_REF && vlog_has_ref(v)
       && (vlog_ref(v) == s->decl || vlog_ref(v) == s->port))
      s->found = true;
}

static void vlog_find_driver_cb(vlog_node_t v, void *ctx)
{
   driver_search_t *s = ctx;
   switch (vlog_kind(v)) {
   case V_ASSIGN:
   case V_PORT_MAP:
   case V_GATE_INST:
      vlog_visit(v, vlog_find_ref_cb, s);
      break;
   case V_PORT_CONN:
      // An output of a submodule connected to the variable
      if (vlog_has_ref(v) && vlog_has_value(v)
          && vlog_kind(vlog_ref(v)) == V_PORT_DECL
          && vlog_subkind(vlog_ref(v)) != V_PORT_INPUT)
         vlog_visit(vlog_value(v), vlog_find_ref_cb, s);
      break;
   default:
      break;
   }
}

static bool vlog_has_net_driver(vlog_node_t body, vlog_node_t decl,
                                vlog_node_t port)
{
   // The variable is driven like a net by a continuous assignment or by
   // an output of a submodule

   driver_search_t s = { .decl = decl, .port = port };
   vlog_visit(body, vlog_find_driver_cb, &s);
   return s.found;
}

static void vlog_lower_port_xinit_spawn(vlog_gen_t *g, vlog_node_t port,
                                        tree_t wrap)
{
   mir_type_t t_offset = mir_offset_type(g->mu);

   ident_t parent = mir_get_name(g->mu, MIR_NULL_VALUE);
   ident_t sym = ident_sprintf("%s.%s#xinit", istr(parent),
                               istr(vlog_ident(port)));
   mir_defer(g->mir, sym, parent, MIR_UNIT_PROCESS, vlog_lower_deferred,
             vlog_to_object(port));

   mir_value_t self = mir_build_context_upref(g->mu, 0);
   mir_value_t args[] = { self };
   mir_value_t closure = mir_build_closure(g->mu, sym, t_offset, args, 1);
   mir_value_t locus = mir_build_debug_locus(g->mu, tree_to_object(wrap));

   mir_build_process_init(g->mu, closure, locus);
}

static void vlog_lower_net_assign(vlog_gen_t *g, vlog_node_t v, tree_t wrap)
{
   mir_type_t t_offset = mir_offset_type(g->mu);

   ident_t parent = mir_get_name(g->mu, MIR_NULL_VALUE);
   ident_t sym = ident_sprintf("%s.%s#assign", istr(parent),
                               istr(vlog_ident(v)));
   mir_defer(g->mir, sym, parent, MIR_UNIT_PROCESS, vlog_lower_deferred,
             vlog_to_object(v));

   mir_value_t self = mir_build_context_upref(g->mu, 0);
   mir_value_t args[] = { self };
   mir_value_t closure = mir_build_closure(g->mu, sym, t_offset, args, 1);
   mir_value_t locus = mir_build_debug_locus(g->mu, tree_to_object(wrap));

   mir_build_process_init(g->mu, closure, locus);
}

static const char *vlog_net_resolution(vlog_node_t d)
{
   switch (vlog_subkind(d)) {
   case V_NET_WAND:
   case V_NET_TRIAND:
      return "NVC.VERILOG.T_WAND$resolution";
   case V_NET_WOR:
   case V_NET_TRIOR:
      return "NVC.VERILOG.T_WOR$resolution";
   default:
      return NULL;
   }
}

static void vlog_lower_net_signal(vlog_gen_t *g, vlog_node_t v, tree_t wrap,
                                  mir_value_t resfn, unsigned extra_flags)
{
   mir_type_t t_net_value = mir_int_type(g->mu, 0, 255);
   mir_type_t t_net_signal = mir_signal_type(g->mu, t_net_value);
   mir_type_t t_offset = mir_offset_type(g->mu);

   assert(wrap != NULL);

   const type_info_t *ti = vlog_type_info(g, vlog_type(v));
   const int total_size = ti->size * vlog_size(v);

   // An undriven trireg holds x with medium charge strength (6.6.4)
   const bool trireg =
      vlog_kind(v) == V_NET_DECL && vlog_subkind(v) == V_NET_TRIREG;
   const int init = trireg
      ? LOGIC_X | MAKE_STRENGTH(V_STRENGTH_MEDIUM, V_STRENGTH_MEDIUM)
      : LOGIC_Z;

   mir_value_t value = mir_const(g->mu, t_net_value, init);
   mir_value_t count = mir_const(g->mu, t_offset, total_size);
   mir_value_t size = mir_const(g->mu, t_offset, 1);
   mir_value_t flags = mir_const(g->mu, t_offset,
                                 NET_F_R_IDENT | SIG_F_INIT_EVENT | extra_flags);
   mir_value_t locus = mir_build_debug_locus(g->mu, tree_to_object(wrap));

   mir_value_t signal = mir_build_init_signal(g->mu, t_net_value, count, size,
                                              value, flags, locus,
                                              MIR_NULL_VALUE);

   if (vlog_kind(v) == V_NET_DECL) {
      const char *name = vlog_net_resolution(v);
      if (name != NULL) {
         mir_value_t pkg = mir_build_package_init(
            g->mu, well_known(W_NVC_VERILOG), MIR_NULL_VALUE);
         mir_type_t t_res = mir_resolution_type(g->mu, t_net_value);
         resfn = mir_build_link_var(g->mu, pkg, ident_new(name), t_res);
      }
   }

   mir_build_resolve_signal(g->mu, signal, resfn);

   mir_value_t var = mir_add_var(g->mu, t_net_signal, MIR_NULL_STAMP,
                                 vlog_ident(v), MIR_VAR_SIGNAL);
   mir_build_store(g->mu, var, signal);

   mir_put_object(g->mu, v, var);
}

static void vlog_lower_net_decl(vlog_gen_t *g, vlog_node_t v, tree_t wrap,
                                mir_value_t resfn, unsigned extra_flags)
{
   vlog_lower_net_signal(g, v, wrap, resfn, extra_flags);

   if (vlog_has_value(v))
      vlog_lower_net_assign(g, v, wrap);
}

static void vlog_hier_ref_cb(vlog_node_t v, void *ctx)
{
   if (vlog_kind(v) == V_HIER_REF)
      *(bool *)ctx = true;
}

static bool vlog_has_hier_ref(vlog_node_t v)
{
   bool found = false;
   vlog_visit(v, vlog_hier_ref_cb, &found);
   return found;
}

static void vlog_lower_one_local(vlog_gen_t *g, vlog_node_t d, ident_t name);

static hash_t *sv_ic_nettypes = NULL;   // Interconnect to its net type

void vlog_set_interconnect_nettype(vlog_node_t decl, vlog_node_t nettype)
{
   // An interconnect net takes the net type of the ports it connects
   // (6.6.8, 23.3.3.4)

   if (sv_ic_nettypes == NULL)
      sv_ic_nettypes = hash_new(16);

   hash_put(sv_ic_nettypes, decl, nettype);
}

static vlog_node_t vlog_nettype_of(vlog_node_t decl)
{
   // The user-defined net type of a net declaration with a resolution
   // function, following other names for the net type to the original

   if (sv_ic_nettypes != NULL) {
      vlog_node_t nt = hash_get(sv_ic_nettypes, decl);
      if (nt == NULL && vlog_kind(decl) == V_PORT_DECL && vlog_has_ref(decl))
         nt = hash_get(sv_ic_nettypes, vlog_ref(decl));
      if (nt != NULL)
         return vlog_has_ident2(nt) ? nt : NULL;
   }

   if (!vlog_has_type(decl))
      return NULL;

   vlog_node_t t = vlog_type(decl);
   if (vlog_kind(t) != V_TYPE_DECL || !(vlog_flags(t) & VLOG_F_NETTYPE))
      return NULL;

   while (vlog_has_type(t) && vlog_kind(vlog_type(t)) == V_TYPE_DECL
          && (vlog_flags(vlog_type(t)) & VLOG_F_NETTYPE))
      t = vlog_type(t);

   return vlog_has_ident2(t) ? t : NULL;
}

typedef struct {
   mir_type_t elem;     // Type of each element of the signal
   int        count;    // Elements making up one value
   bool       is_real;
   bool       is_struct;
} nettype_flat_t;

static bool vlog_nettype_flat(vlog_gen_t *g, vlog_node_t nt, nettype_flat_t *f)
{
   // The value of a net of a user-defined net type is held in a signal as
   // the bits of an integral value, a real, or the members of a structure
   // which are all integral or all real one after the other

   const type_info_t *ti = vlog_type_info(g, vlog_type(nt));
   const mir_class_t class = mir_get_class(g->mu, ti->type);

   f->is_struct = false;

   switch (class) {
   case MIR_TYPE_REAL:
      f->elem = ti->unpacked;
      f->count = 1;
      f->is_real = true;
      return true;
   case MIR_TYPE_VEC2:
   case MIR_TYPE_VEC4:
      f->elem = ti->unpacked;
      f->count = ti->size;
      f->is_real = false;
      return true;
   case MIR_TYPE_ACCESS:
      break;
   default:
      return false;
   }

   vlog_node_t st = vlog_get_type(vlog_type(nt));
   if (st == NULL || vlog_kind(st) != V_STRUCT_DECL)
      return false;

   int nreal = 0, bits = 0;
   const int ndecls = vlog_decls(st);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(st, i);
      if (vlog_is_fixed_unpacked(d) && vlog_size(d) > 1)
         return false;

      const type_info_t *fti = vlog_type_info(g, vlog_type(d));
      switch (mir_get_class(g->mu, fti->type)) {
      case MIR_TYPE_REAL:
         nreal++;
         break;
      case MIR_TYPE_VEC2:
      case MIR_TYPE_VEC4:
         bits += fti->size;
         break;
      default:
         return false;
      }
   }

   if (ndecls == 0 || (nreal > 0 && bits > 0))
      return false;

   f->is_struct = true;
   f->is_real = nreal > 0;
   f->count = f->is_real ? nreal : bits;
   f->elem = f->is_real ? mir_real_type(g->mu, -DBL_MAX, DBL_MAX)
      : mir_int_type(g->mu, 0, 3);
   return true;
}

static void vlog_nettype_store(vlog_gen_t *g, vlog_node_t nt,
                               const nettype_flat_t *f, mir_value_t value,
                               mir_value_t buf)
{
   // Store a value of the data type of a net type as signal elements

   mir_type_t t_offset = mir_offset_type(g->mu);

   if (!f->is_struct) {
      if (f->is_real)
         mir_build_store(g->mu, buf, value);
      else {
         mir_value_t bits = mir_build_unpack(g->mu, value, 0, buf);
         if (!mir_is(g->mu, bits, MIR_TYPE_POINTER))
            mir_build_store(g->mu, buf, bits);
      }
      return;
   }

   vlog_node_t st = vlog_get_type(vlog_type(nt));
   mir_value_t rec = mir_build_all(g->mu, value);

   int pos = 0;
   const int ndecls = vlog_decls(st);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(st, i);
      const type_info_t *fti = vlog_type_info(g, vlog_type(d));
      mir_value_t fv = mir_build_load(g->mu, mir_build_record_ref(g->mu, rec, i));
      mir_value_t dst = mir_build_array_ref(g->mu, buf,
                                            mir_const(g->mu, t_offset, pos));
      if (f->is_real) {
         mir_build_store(g->mu, dst, fv);
         pos++;
      }
      else {
         mir_value_t bits = mir_build_unpack(g->mu, fv, 0, dst);
         if (!mir_is(g->mu, bits, MIR_TYPE_POINTER))
            mir_build_store(g->mu, dst, bits);
         pos += fti->size;
      }
   }
}

static mir_value_t vlog_nettype_load(vlog_gen_t *g, vlog_node_t nt,
                                     const nettype_flat_t *f, mir_value_t ptr)
{
   // A value of the data type of a net type from its signal elements

   const type_info_t *ti = vlog_type_info(g, vlog_type(nt));
   mir_type_t t_offset = mir_offset_type(g->mu);

   if (!f->is_struct)
      return f->is_real ? mir_build_load(g->mu, ptr)
         : mir_build_pack(g->mu, ti->type, ptr);

   vlog_node_t st = vlog_get_type(vlog_type(nt));
   mir_type_t t_rec = mir_get_elem(g->mu, ti->type);
   mir_value_t acc = mir_build_new(g->mu, t_rec, MIR_NULL_STAMP,
                                   MIR_NULL_VALUE);
   mir_value_t rec = mir_build_all(g->mu, acc);

   int pos = 0;
   const int ndecls = vlog_decls(st);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(st, i);
      const type_info_t *fti = vlog_type_info(g, vlog_type(d));
      mir_value_t src = mir_build_array_ref(g->mu, ptr,
                                            mir_const(g->mu, t_offset, pos));
      mir_value_t fv;
      if (f->is_real) {
         fv = mir_build_load(g->mu, src);
         pos++;
      }
      else {
         fv = mir_build_pack(g->mu, fti->type, src);
         pos += fti->size;
      }
      mir_build_store(g->mu, mir_build_record_ref(g->mu, rec, i), fv);
   }

   return acc;
}

static void vlog_lower_nettype_res(mir_unit_t *mu, object_t *obj)
{
   // Called by the kernel with the values of all the drivers of a net of
   // a user-defined net type, one after the other, and their number:
   // returns the address of the value given by the resolution function
   // for a dynamic array of those values (6.6.7)

   vlog_node_t nt = vlog_from_object(obj);
   vlog_node_t fdecl = vlog_ref(nt);

   vlog_gen_t g = {
      .mu  = mu,
      .mir = mir_get_context(mu),
   };

   nettype_flat_t f;
   if (!vlog_nettype_flat(&g, nt, &f))
      should_not_reach_here();

   const type_info_t *ti = vlog_type_info(&g, vlog_type(nt));

   mir_type_t t_offset = mir_offset_type(mu);
   mir_type_t t_ptr = mir_pointer_type(mu, f.elem);

   mir_set_result(mu, t_ptr);

   mir_type_t t_context = mir_context_type(mu, mir_get_parent(mu));
   mir_add_param(mu, t_context, MIR_NULL_STAMP, ident_new("context"));
   mir_value_t values = mir_add_param(mu, t_ptr, MIR_NULL_STAMP,
                                      ident_new("values"));
   mir_value_t count = mir_add_param(mu, t_offset, MIR_NULL_STAMP,
                                     ident_new("count"));

   mir_value_t data = vlog_alloc_array(&g, ti->type, count);

   vlog_loop_t loop;
   vlog_loop_begin(&g, &loop, mir_const(mu, t_offset, 0), count);

   mir_value_t off = mir_build_mul(mu, t_offset, loop.i,
                                   mir_const(mu, t_offset, f.count));
   mir_value_t elem = vlog_nettype_load(&g, nt, &f,
                                        mir_build_array_ref(mu, values, off));
   mir_build_store(mu, mir_build_array_ref(mu, data, loop.i), elem);

   vlog_loop_end(&g, &loop);

   mir_value_t args[2];
   int nargs = 0;

   mir_value_t context = vlog_context_for_call(&g, fdecl);
   if (!mir_is_null(context))
      args[nargs++] = context;

   args[nargs++] = vlog_wrap_array(&g, data, count);

   const type_info_t *rti = vlog_type_info(&g, vlog_type(fdecl));
   mir_value_t result = mir_build_fcall(mu, vlog_subr_name(fdecl), rti->type,
                                        MIR_NULL_STAMP, args, nargs);
   mir_build_consume(mu, result);

   mir_value_t buf = vlog_alloc_array(&g, f.elem,
                                      mir_const(mu, t_offset, f.count));
   vlog_nettype_store(&g, nt, &f, vlog_lower_cast(&g, ti->type, result), buf);

   mir_build_return(mu, buf);

   mir_optimise(mu, MIR_PASS_O1);
   vlog_lower_cleanup(&g);
}

static mir_value_t vlog_lower_nettype_resolution(vlog_gen_t *g, vlog_node_t v,
                                                 vlog_node_t nt,
                                                 nettype_flat_t *f)
{
   // A net of a user-defined net type with a resolution function is a
   // signal resolved by calling the function with the values of all its
   // drivers, whichever instance they are in.  Returns the initial value
   // of each element, which is the value with no drivers.

   if (!vlog_has_ref(nt))
      fatal_at(vlog_loc(v), "sorry, the resolution function of nettype %s "
               "must be declared before the nettype", istr(vlog_ident(nt)));
   else if (mir_get_kind(g->mu) != MIR_UNIT_INSTANCE)
      fatal_at(vlog_loc(v), "sorry, this net of a user-defined net type is "
               "not supported here");
   else if (!vlog_nettype_flat(g, nt, f))
      fatal_at(vlog_loc(v), "sorry, a net of a user-defined net type of "
               "this data type cannot be resolved across the design");

   ident_t qual = mir_get_name(g->mu, MIR_NULL_VALUE);
   ident_t sym = ident_sprintf("%s.%s$resolve", istr(qual),
                               istr(vlog_ident(nt)));
   mir_defer(g->mir, sym, qual, MIR_UNIT_FUNCTION, vlog_lower_nettype_res,
             vlog_to_object(nt));

   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_type_t t_ptr = mir_pointer_type(g->mu, f->elem);

   mir_value_t self = mir_build_context_upref(g->mu, 0);

   mir_value_t args[] = {
      self,
      mir_build_null(g->mu, t_ptr),
      mir_const(g->mu, t_offset, 0),
   };
   mir_value_t init = mir_build_fcall(g->mu, sym, t_ptr, MIR_NULL_STAMP,
                                      args, ARRAY_LEN(args));
   mir_build_consume(g->mu, init);

   return init;
}

static void vlog_resolve_nettype_signal(vlog_gen_t *g, vlog_node_t nt,
                                        mir_value_t signal,
                                        const nettype_flat_t *f)
{
   ident_t qual = mir_get_name(g->mu, MIR_NULL_VALUE);
   ident_t sym = ident_sprintf("%s.%s$resolve", istr(qual),
                               istr(vlog_ident(nt)));

   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_type_t t_ptr = mir_pointer_type(g->mu, f->elem);

   mir_value_t self = mir_build_context_upref(g->mu, 0);
   mir_value_t closure = mir_build_closure(g->mu, sym, t_ptr, &self, 1);

   // The kernel calls the function with the whole value of each element
   mir_type_t t_blob = mir_carray_type(g->mu, f->count, f->elem);
   mir_value_t wrapper = mir_build_resolution_wrapper(
      g->mu, t_blob, closure, mir_const(g->mu, t_offset, f->count));

   mir_type_t t_res = mir_resolution_type(g->mu, t_blob);
   mir_value_t var = mir_add_var(g->mu, t_res, MIR_NULL_STAMP,
                                 ident_uniq("resolution"), 0);
   mir_build_store(g->mu, var, wrapper);

   mir_build_resolve_signal(g->mu, signal, var);
}

static vlog_node_t vlog_struct_nettype_of(vlog_node_t decl)
{
   // A net of a user-defined net type of a structure type has a signal of
   // its own holding the resolved value, copied into the structure

   vlog_node_t nt = vlog_nettype_of(decl);
   return nt != NULL && vlog_is_dynamic_type(vlog_type(nt)) ? nt : NULL;
}

static ident_t vlog_struct_net_name(vlog_node_t decl)
{
   return ident_prefix(vlog_ident(decl), ident_new("net"), '$');
}

static mir_value_t vlog_struct_net_signal(vlog_gen_t *g, vlog_node_t decl)
{
   // The signal of a structure net of this instance or the parent of
   // this process

   ident_t name = vlog_struct_net_name(decl);

   assert(mir_get_kind(g->mu) != MIR_UNIT_INSTANCE);

   mir_shape_t *shape = mir_get_shape(g->mir, mir_get_parent(g->mu));
   const int nth = mir_find_slot(shape, name);
   if (nth < 0)
      fatal_at(vlog_loc(decl), "sorry, a net of a user-defined net type of a "
               "structure type can only be used in its own instance");

   return mir_build_load(g->mu, mir_build_var_upref(g->mu, 1, nth));
}

static void vlog_lower_struct_net(vlog_gen_t *g, vlog_node_t v, tree_t wrap)
{
   // The structure variable of a net of a structure net type follows the
   // value of its signal

   vlog_lower_one_local(g, v, vlog_ident(v));

   mir_type_t t_offset = mir_offset_type(g->mu);
   ident_t qual = mir_get_name(g->mu, MIR_NULL_VALUE);
   ident_t sym = ident_prefix(qual, vlog_struct_net_name(v), '.');
   mir_defer(g->mir, sym, qual, MIR_UNIT_PROCESS, vlog_lower_deferred,
             vlog_to_object(v));

   mir_value_t self = mir_build_context_upref(g->mu, 0);
   mir_value_t closure = mir_build_closure(g->mu, sym, t_offset, &self, 1);
   mir_value_t locus = mir_build_debug_locus(g->mu, tree_to_object(wrap));
   mir_build_process_init(g->mu, closure, locus);
}

static void vlog_lower_struct_net_process(vlog_gen_t *g, vlog_node_t v)
{
   vlog_node_t nt = vlog_struct_nettype_of(v);

   nettype_flat_t f;
   if (!vlog_nettype_flat(g, nt, &f))
      should_not_reach_here();

   mir_block_t start_bb = mir_add_block(g->mu);
   assert(start_bb.id == 1);

   mir_type_t t_offset = mir_offset_type(g->mu);

   mir_value_t signal = vlog_struct_net_signal(g, v);
   mir_build_sched_event(g->mu, signal, mir_const(g->mu, t_offset, f.count));
   mir_build_return(g->mu, MIR_NULL_VALUE);

   mir_set_cursor(g->mu, start_bb, MIR_APPEND);

   mir_value_t value = vlog_nettype_load(
      g, nt, &f, mir_build_resolved(g->mu, vlog_struct_net_signal(g, v)));

   int hops;
   mir_value_t var = mir_search_object(g->mu, v, &hops);
   assert(!mir_is_null(var));
   if (hops > 0)
      var = mir_build_var_upref(g->mu, hops, var.id);
   mir_build_store(g->mu, var, value);

   mir_build_wait(g->mu, start_bb);
}

static bool vlog_lower_struct_net_driver(vlog_gen_t *g, vlog_node_t v)
{
   // A continuous assignment to a net of a structure net type drives the
   // signal of the net with the members of the value

   vlog_node_t target = vlog_target(v);
   if (vlog_kind(target) != V_REF || !vlog_has_ref(target))
      return false;

   vlog_node_t decl = vlog_ref(target);
   vlog_node_t nt = vlog_struct_nettype_of(decl);
   if (nt == NULL)
      return false;

   nettype_flat_t f;
   if (!vlog_nettype_flat(g, nt, &f))
      fatal_at(vlog_loc(v), "sorry, a net of a user-defined net type of "
               "this data type cannot be driven here");
   else if (vlog_assign_delay(v) != NULL)
      fatal_at(vlog_loc(v), "sorry, a delayed continuous assignment to a net "
               "of a structure net type is not supported");

   mir_block_t start_bb = mir_add_block(g->mu);
   assert(start_bb.id == 1);

   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_value_t count = mir_const(g->mu, t_offset, f.count);

   const bool polled = vlog_uses_memory_member(vlog_value(v));

   mir_build_drive_signal(g->mu, vlog_struct_net_signal(g, decl), count);
   if (!polled)
      vlog_lower_sensitivity(g, vlog_value(v));
   mir_build_return(g->mu, MIR_NULL_VALUE);

   mir_set_cursor(g->mu, start_bb, MIR_APPEND);

   const type_info_t *ti = vlog_type_info(g, vlog_type(nt));
   mir_value_t value = vlog_lower_cast(
      g, ti->type, vlog_lower_with_context(g, vlog_value(v), ti->type));

   mir_type_t t_array = mir_carray_type(g->mu, f.count, f.elem);
   mir_value_t buf = mir_build_array_ref(g->mu, vlog_get_temp(g, t_array),
                                         mir_const(g->mu, t_offset, 0));
   vlog_nettype_store(g, nt, &f, value, buf);

   mir_build_put_driver(g->mu, vlog_struct_net_signal(g, decl), count, buf);

   if (polled) {
      vlog_lower_poll_expr(g, vlog_value(v));
      mir_build_jump(g->mu, start_bb);
   }
   else
      mir_build_wait(g->mu, start_bb);

   return true;
}

static mir_value_t vlog_lower_nettype_signal(vlog_gen_t *g, vlog_node_t v,
                                             vlog_node_t nt, tree_t wrap,
                                             int nelems)
{
   // The signal of a net of a user-defined net type, starting with the
   // value with no drivers

   nettype_flat_t f;
   mir_value_t init = vlog_lower_nettype_resolution(g, v, nt, &f);

   mir_type_t t_offset = mir_offset_type(g->mu);
   const int total = f.count * nelems;

   mir_value_t value = init;
   if (nelems > 1) {
      mir_type_t t_array = mir_carray_type(g->mu, total, f.elem);
      value = mir_build_array_ref(g->mu, vlog_get_temp(g, t_array),
                                  mir_const(g->mu, t_offset, 0));
      for (int i = 0; i < total; i += f.count) {
         mir_value_t dst = mir_build_array_ref(g->mu, value,
                                               mir_const(g->mu, t_offset, i));
         mir_build_copy(g->mu, dst, init, mir_const(g->mu, t_offset, f.count));
      }
   }

   const size_t elemsz = f.is_real ? sizeof(double) : 1;

   mir_value_t count = mir_const(g->mu, t_offset, total);
   mir_value_t size = mir_const(g->mu, t_offset, elemsz);
   mir_value_t flags = mir_const(g->mu, t_offset, 0);
   mir_value_t locus = mir_build_debug_locus(g->mu, tree_to_object(wrap));

   mir_value_t signal = mir_build_init_signal(g->mu, f.elem, count, size,
                                              value, flags, locus,
                                              MIR_NULL_VALUE);

   vlog_resolve_nettype_signal(g, nt, signal, &f);
   return signal;
}

static void vlog_lower_var_decl(vlog_gen_t *g, vlog_node_t v, tree_t wrap,
                                unsigned extra_flags)
{
   vlog_node_t nt = vlog_nettype_of(v);
   const type_info_t *ti = vlog_type_info(g, nt ? vlog_type(nt) : vlog_type(v));

   if (nt != NULL) {
      mir_value_t signal =
         vlog_lower_nettype_signal(g, v, nt, wrap, vlog_size(v));

      if (vlog_is_dynamic_type(vlog_type(nt))) {
         mir_value_t var = mir_add_var(g->mu, mir_get_type(g->mu, signal),
                                       MIR_NULL_STAMP,
                                       vlog_struct_net_name(v),
                                       MIR_VAR_SIGNAL);
         mir_build_store(g->mu, var, signal);
         vlog_lower_struct_net(g, v, wrap);
      }
      else {
         mir_value_t var = mir_add_var(g->mu, ti->signal, MIR_NULL_STAMP,
                                       vlog_ident(v), MIR_VAR_SIGNAL);
         mir_build_store(g->mu, var, signal);
         mir_put_object(g->mu, v, var);
      }
      return;
   }

   if (vlog_is_dynamic_type(vlog_type(v))) {
      if (vlog_has_value(v) && vlog_has_hier_ref(vlog_value(v))) {
         // The instance is not known until it has been elaborated so
         // initialise the variable when the processes are reset
         mir_value_t var = vlog_add_local_var(g, v, vlog_ident(v));
         vlog_store_local_default(g, v, var);

         ident_t parent = mir_get_name(g->mu, MIR_NULL_VALUE);
         ident_t sym = ident_sprintf("%s.%s#init", istr(parent),
                                     istr(vlog_ident(v)));
         mir_defer(g->mir, sym, parent, MIR_UNIT_PROCESS,
                   vlog_lower_deferred, vlog_to_object(v));

         mir_type_t t_offset = mir_offset_type(g->mu);
         mir_value_t self = mir_build_context_upref(g->mu, 0);
         mir_value_t args[] = { self };
         mir_value_t closure =
            mir_build_closure(g->mu, sym, t_offset, args, 1);
         mir_value_t locus = mir_build_debug_locus(g->mu, tree_to_object(wrap));
         mir_build_process_init(g->mu, closure, locus);
         return;
      }

      vlog_lower_one_local(g, v, vlog_ident(v));
      return;
   }

   mir_type_t t_offset = mir_offset_type(g->mu);

   const int total_size = ti->size * vlog_size(v);

   assert(wrap != NULL);

   mir_value_t value;
   if (vlog_has_value(v)) {
      mir_value_t tmp = MIR_NULL_VALUE;
      if (total_size > 1) {
         mir_type_t t_elem = mir_logic_type(g->mu);
         mir_type_t t_array = mir_carray_type(g->mu, total_size, t_elem);
         tmp = vlog_get_temp(g, t_array);
      }

      mir_value_t cast;
      if (vlog_kind(vlog_value(v)) == V_PATTERN_EXPR
          && vlog_is_fixed_unpacked(v)) {
         vlog_node_t dims[MAX_UNPACKED_DIMS];
         const int ndims = MIN(vlog_ranges(v), MAX_UNPACKED_DIMS);
         for (int i = 0; i < ndims; i++)
            dims[i] = vlog_range(v, i);
         if (mir_get_class(g->mu, ti->type) == MIR_TYPE_REAL)
            cast = vlog_real_array_pattern(g, vlog_value(v), dims, ndims, ti);
         else
            cast = vlog_lower_array_pattern(g, vlog_value(v), dims, ndims,
                                            ti);
      }
      else if (vlog_kind(vlog_value(v)) == V_STREAM && total_size > 1
               && vlog_is_fixed_unpacked(v)) {
         // The elements are slices of the packed result
         mir_type_t t_flat = mir_vec4_type(g->mu, total_size, false);
         mir_value_t packed = vlog_lower_stream(g, vlog_value(v), t_flat);
         cast = vlog_lower_cast(g, t_flat, packed);
      }
      else {
         mir_value_t packed =
            vlog_lower_with_context(g, vlog_value(v), ti->type);
         cast = vlog_lower_cast(g, ti->type, packed);
      }
      if (mir_get_class(g->mu, ti->unpacked) == MIR_TYPE_REAL)
         value = cast;
      else
         value = mir_build_unpack(g->mu, cast, 0, tmp);
   }
   else if (mir_get_class(g->mu, ti->unpacked) == MIR_TYPE_REAL)
      value = mir_const_real(g->mu, ti->unpacked, 0.0);
   else if (mir_get_class(g->mu, ti->type) == MIR_TYPE_VEC2)
      value = mir_const(g->mu, ti->unpacked, LOGIC_0);   // Two-state type
   else
      value = mir_const(g->mu, ti->unpacked, LOGIC_X);

   mir_value_t count = mir_const(g->mu, t_offset, total_size);
   mir_value_t size = mir_const(g->mu, t_offset, ti->elemsz);
   mir_value_t flags = mir_const(g->mu, t_offset, extra_flags);
   mir_value_t locus = mir_build_debug_locus(g->mu, tree_to_object(wrap));

   mir_value_t signal = mir_build_init_signal(g->mu, ti->unpacked, count, size,
                                              value, flags, locus,
                                              MIR_NULL_VALUE);

   mir_value_t var = mir_add_var(g->mu, ti->signal, MIR_NULL_STAMP,
                                 vlog_ident(v), MIR_VAR_SIGNAL);
   mir_build_store(g->mu, var, signal);

   mir_put_object(g->mu, v, var);
}

static void vlog_lower_port_decl(vlog_gen_t *g, vlog_node_t v)
{
   const type_info_t *ti = vlog_type_info(g, vlog_type(v));

   mir_type_t type;
   vlog_node_t d = vlog_ref(v), nt = vlog_nettype_of(v);
   if (nt != NULL)
      type = vlog_type_info(g, vlog_type(nt))->signal;
   else if (vlog_kind(d) == V_NET_DECL) {
      mir_type_t t_net_value = mir_int_type(g->mu, 0, 255);
      type = mir_signal_type(g->mu, t_net_value);
   }
   else
      type = ti->signal;

   mir_value_t p = mir_add_param(g->mu, type, MIR_NULL_STAMP, vlog_ident(v));

   mir_value_t var = mir_add_var(g->mu, type, MIR_NULL_STAMP,
                                 vlog_ident(v), MIR_VAR_SIGNAL);
   mir_build_store(g->mu, var, p);

   mir_put_object(g->mu, v, var);
   mir_put_object(g->mu, d, var);

   // A net which is connected to the port takes the wired logic of the
   // net type declared for it
   if (vlog_kind(d) == V_NET_DECL && vlog_subkind(v) != V_PORT_INPUT
       && vlog_net_resolution(d) != NULL) {
      mir_type_t t_net_value = mir_int_type(g->mu, 0, 255);
      mir_value_t pkg = mir_build_package_init(
         g->mu, well_known(W_NVC_VERILOG), MIR_NULL_VALUE);
      mir_type_t t_res = mir_resolution_type(g->mu, t_net_value);
      mir_value_t resfn = mir_build_link_var(
         g->mu, pkg, ident_new(vlog_net_resolution(d)), t_res);
      mir_build_resolve_signal(g->mu, p, resfn);
   }
}

static void vlog_lower_genvar_decl(vlog_gen_t *g, vlog_node_t v)
{
   const type_info_t *ti = vlog_type_info(g, vlog_type(v));

   mir_value_t var = mir_add_var(g->mu, ti->type, MIR_NULL_STAMP,
                                 vlog_ident(v), 0);
   mir_build_store(g->mu, var, mir_const_vec(g->mu, ti->type, ~0, ~0));

   mir_put_object(g->mu, v, var);
}

static void vlog_store_local_init(vlog_gen_t *g, vlog_node_t d, mir_value_t var)
{
   const type_info_t *ti = vlog_type_info(g, vlog_type(d));
   const int nelems = vlog_ranges(d) > 0 ? vlog_size(d) : 1;

   if (nelems > 1 && vlog_has_value(d)) {
      vlog_node_t dims[MAX_UNPACKED_DIMS];
      const int ndims = MIN(vlog_ranges(d), MAX_UNPACKED_DIMS);
      for (int i = 0; i < ndims; i++)
         dims[i] = vlog_range(d, i);

      const mir_class_t eclass = mir_get_class(g->mu, ti->type);
      const bool is_vec = eclass == MIR_TYPE_VEC2 || eclass == MIR_TYPE_VEC4;

      if (vlog_kind(vlog_value(d)) == V_STREAM && is_vec) {
         // The elements are slices of the packed result
         const int total = ti->size * nelems;
         mir_type_t t_flat = mir_vec4_type(g->mu, total, false);
         mir_value_t packed = vlog_lower_stream(g, vlog_value(d), t_flat);
         vlog_store_flat_array(g, var, nelems, ti,
                               vlog_lower_cast(g, t_flat, packed));
         return;
      }

      if (vlog_kind(vlog_value(d)) != V_PATTERN_EXPR)
         fatal_at(vlog_loc(d), "sorry, this unpacked array initialiser is "
                  "not supported");

      vlog_node_t *which LOCAL =
         xmalloc_array(vlog_size(dims[0]), sizeof(vlog_node_t)), def;
      vlog_array_pattern_slots(vlog_value(d), dims[0], which, &def);

      if (is_vec) {
         mir_value_t flat =
            vlog_lower_array_pattern(g, vlog_value(d), dims, ndims, ti);
         vlog_store_flat_array(g, var, nelems, ti, flat);
      }
      else if (ndims == 1) {
         // Each element of an array of reals or strings is initialised alone
         mir_type_t t_offset = mir_offset_type(g->mu);
         const int n = vlog_size(dims[0]);
         for (int i = 0; i < n; i++) {
            vlog_node_t item = which[i] != NULL ? which[i] : def;
            if (item == NULL)
               continue;

            mir_value_t value = vlog_lower_with_context(g, item, ti->type);
            mir_value_t init = vlog_lower_cast(g, ti->type, value);
            if (vlog_is_dynamic_type(vlog_type(d)))
               init = vlog_copy_value(g, init, vlog_type(d));
            mir_build_store(g->mu, mir_build_array_ref(
                               g->mu, var, mir_const(g->mu, t_offset, i)),
                            init);
         }
      }
      else
         fatal_at(vlog_loc(d), "sorry, this unpacked array initialiser is "
                  "not supported");
   }
   else if (vlog_has_value(d)) {
      mir_value_t value = vlog_lower_with_context(g, vlog_value(d), ti->type);
      mir_value_t init = vlog_lower_cast(g, ti->type, value);
      const vlog_kind_t vk = vlog_kind(vlog_value(d));
      if (vlog_is_dynamic_type(vlog_type(d)) && vk != V_CLASS_NEW
          && vk != V_DYNAMIC_NEW && vk != V_PATTERN_EXPR && vk != V_CONCAT)
         init = vlog_copy_value(g, init, vlog_type(d));
      mir_build_store(g->mu, var, init);
   }
   else
      vlog_store_local_default(g, d, var);
}

static void vlog_lower_locals(vlog_gen_t *g, vlog_node_t v)
{
   const int ndecls = vlog_decls(v);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(v, i);
      switch (vlog_kind(d)) {
      case V_VAR_DECL:
         // Lives in the enclosing unit except during constant evaluation
         if ((vlog_flags(d) & VLOG_F_STATIC) && !g->constfunc)
            break;
         // Fall-through
      case V_LOCAL_DECL:
         vlog_lower_one_local(g, d, vlog_ident(d));
         break;
      case V_PARAM_DECL:
      case V_LOCALPARAM:
         {
            const type_info_t *ti = vlog_type_info(g, vlog_type(d));

            mir_value_t var = mir_add_var(g->mu, ti->type, ti->stamp,
                                          vlog_ident(d), MIR_VAR_CONST);
            mir_put_object(g->mu, d, var);

            mir_value_t value = vlog_lower_rvalue(g, vlog_value(d));
            mir_value_t init = vlog_lower_cast(g, ti->type, value);
            mir_build_store(g->mu, var, init);
         }
         break;
      default:
         CANNOT_HANDLE(d);
      }
   }
}

static void vlog_lower_func_decl(mir_unit_t *mu, object_t *obj)
{
   vlog_node_t v = vlog_from_object(obj);
   const bool is_ctor = vlog_kind(v) == V_CONSTRUCTOR;
   assert(is_ctor || vlog_kind(v) == V_FUNC_DECL);

   vlog_gen_t g = {
      .mu   = mu,
      .mir  = mir_get_context(mu),
      .subr = v,
   };

   const bool has_result = !is_ctor && vlog_has_type(v);

   const type_info_t *ti = NULL;
   if (has_result) {
      ti = vlog_type_info(&g, vlog_type(v));
      mir_set_result(mu, ti->type);
   }
   else
      mir_set_result(mu, mir_offset_type(mu));

   ident_t parent = mir_get_parent(mu);
   if (parent == NULL)
      g.constfunc = true;
   else {
      mir_type_t t_context = mir_context_type(mu, mir_get_parent(mu));
      mir_add_param(mu, t_context, MIR_NULL_STAMP, ident_new("context"));
   }

   const int nports = is_ctor ? vlog_params(v) : vlog_ports(v);
   for (int i = 0; i < nports; i++) {
      vlog_node_t port = is_ctor ? vlog_param(v, i) : vlog_port(v, i);
      ident_t name = vlog_ident(port);

      mir_value_t param = mir_add_param(mu, pti->type, pti->stamp,
                                        vlog_ident(port));

      mir_value_t local = mir_add_var(mu, type, pti->stamp, name, 0);
      mir_build_store(mu, local, param);

      mir_put_object(mu, port, local);
   }

   mir_value_t result = MIR_NULL_VALUE;
   if (has_result) {
      result = mir_add_var(mu, ti->type, ti->stamp, vlog_ident(v), 0);
      mir_put_object(mu, v, result);

      mir_build_store(mu, result, vlog_lower_default_value(&g, ti));
   }

   gen_stack_t gs = {};

   // Without foreign code the UVM library cannot read the command line so
   // this is provided here instead of the empty version in the library
   const bool builtin = !is_ctor
      && vlog_ident(v) == ident_new("uvm_dpi_get_next_arg");

   if ((vlog_flags(v) & VLOG_F_DPI) || builtin)
      vlog_lower_dpi_body(&g, v, result);

   vlog_lower_locals(&g, v);

   if (is_ctor)
      vlog_lower_implicit_super(&g, v);

   if (!builtin)
      vlog_lower_stmts(&g, v, &gs);

   if (!mir_block_finished(mu, MIR_NULL_BLOCK)) {
      if (has_result)
         mir_build_return(mu, mir_build_load(mu, result));
      else
         mir_build_return(mu, mir_const(mu, mir_offset_type(mu), 0));
   }

   mir_optimise(mu, MIR_PASS_O1);
   vlog_lower_cleanup(&g);
}

static void vlog_lower_task_decl(mir_unit_t *mu, object_t *obj)
{
   vlog_node_t v = vlog_from_object(obj);
   assert(vlog_kind(v) == V_TASK_DECL);

   vlog_gen_t g = {
      .mu   = mu,
      .mir  = mir_get_context(mu),
      .exit = mir_add_block(mu),
      .subr = v,
   };

   mir_type_t t_context = mir_context_type(mu, mir_get_parent(mu));
   mir_add_param(mu, t_context, MIR_NULL_STAMP, ident_new("context"));

   uint32_t sigref_mask = 0;
   const char *sr = strstr(istr(mir_get_name(mu, MIR_NULL_VALUE)), "$sr");
   if (sr != NULL)
      sigref_mask = strtoul(sr + 3, NULL, 16);

   const int nports = vlog_ports(v);
   for (int i = 0; i < nports; i++) {
      vlog_node_t port = vlog_port(v, i);
      ident_t name = vlog_ident(port);

      if (vlog_dpi_open_port(v, port)) {
         mir_type_t t_desc = mir_offset_type(mu);
         mir_value_t param = mir_add_param(mu, t_desc, MIR_NULL_STAMP, name);
         mir_value_t local = mir_add_var(mu, t_desc, MIR_NULL_STAMP, name, 0);
         mir_build_store(mu, local, param);
         mir_put_object(mu, port, local);
         continue;
      }

      const type_info_t *pti = vlog_type_info(&g, vlog_type(port));

      if (i < 32 && (sigref_mask & (UINT32_C(1) << i))) {
         // Reads, writes and waits use the signal of the actual
         mir_value_t param = mir_add_param(mu, pti->signal, MIR_NULL_STAMP,
                                           name);
         mir_value_t local = mir_add_var(mu, pti->signal, MIR_NULL_STAMP,
                                         name, 0);
         mir_build_store(mu, local, param);
         mir_put_object(mu, port, local);
         continue;
      }

      if (vlog_is_const_ref(port) && !vlog_is_fixed_unpacked(port)) {
         mir_type_t t_cell = vlog_ref_cell_type(mu, pti);
         mir_value_t param = mir_add_param(mu, t_cell, MIR_NULL_STAMP, name);
         mir_value_t local = mir_add_var(mu, t_cell, MIR_NULL_STAMP, name, 0);
         mir_build_store(mu, local, param);
         mir_put_object(mu, port, local);
         continue;
      }

      if (vlog_subkind(port) == V_PORT_REF || vlog_is_fixed_unpacked(port)) {
         // Reads and writes go straight through the pointer
         mir_type_t t_pointer = mir_pointer_type(mu, pti->type);
         mir_value_t param = mir_add_param(mu, t_pointer, pti->stamp, name);
         mir_value_t local = mir_add_var(mu, t_pointer, pti->stamp, name, 0);
         mir_build_store(mu, local, param);
         mir_put_object(mu, port, local);
         continue;
      }

      mir_value_t local = mir_add_var(mu, pti->type, pti->stamp, name, 0);
      mir_put_object(mu, port, local);

      if (vlog_subkind(port) == V_PORT_INPUT) {
         mir_value_t param = mir_add_param(mu, pti->type, pti->stamp, name);
         mir_build_store(mu, local, param);
      }
      else {
         mir_type_t t_pointer = mir_pointer_type(mu, pti->type);
         mir_value_t param = mir_add_param(mu, t_pointer, pti->stamp, name);

         mir_value_t init = mir_build_load(mu, param);
         mir_build_store(mu, local, init);

         // The parameter does not survive the task suspending
         mir_value_t saved = mir_add_var(mu, t_pointer, pti->stamp,
                                         ident_sprintf("%s$out", istr(name)),
                                         0);
         mir_build_store(mu, saved, param);
         mir_put_object(mu, name, saved);
      }
   }

   gen_stack_t gs = {};

   vlog_lower_locals(&g, v);

   if (vlog_flags(v) & VLOG_F_DPI)
      vlog_lower_dpi_call(&g, v, MIR_NULL_VALUE);

   // Disabling the task by name leaves its body
   block_info_t body = {
      .name     = vlog_ident(v),
      .label    = vlog_subr_name(v),
      .exit_bb  = g.exit,
      .has_fork = true,
   };
   gen_stack_t this = vlog_push_block(&gs, &body);

   vlog_lower_stmts(&g, v, &this);

   if (!mir_block_finished(mu, MIR_NULL_BLOCK))
      mir_build_jump(mu, g.exit);

   mir_set_cursor(mu, g.exit, MIR_APPEND);

   for (int i = 0; i < nports; i++) {
      vlog_node_t port = vlog_port(v, i);
      if (vlog_subkind(port) == V_PORT_INPUT)
         continue;

      int hops;
      mir_value_t local = mir_search_object(mu, port, &hops);
      assert(!mir_is_null(local));
      assert(hops == 0);

      mir_value_t saved = mir_search_object(mu, vlog_ident(port), &hops);
      assert(!mir_is_null(saved));

      mir_value_t result = mir_build_load(mu, local);
      mir_build_store(mu, mir_build_load(mu, saved), result);
   }


   mir_build_return(mu, MIR_NULL_VALUE);

   mir_optimise(mu, MIR_PASS_O1);
   vlog_lower_cleanup(&g);
}

static void vlog_lower_class_fields(vlog_gen_t *g, vlog_node_t c)
{
   // A derived class has the same layout as its base with extra fields
   // on the end so a handle can be used as any of its base classes

   if (vlog_has_ref(c))
      vlog_lower_class_fields(g, vlog_ref(c));

   const int ndecls = vlog_decls(c);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(c, i);
      if (vlog_is_deferred_prop(d)) {
         // Initialised by the constructor once the base class
         // constructor has returned
         mir_value_t var = vlog_add_local_var(g, d, vlog_prop_name_in(c, d));
         vlog_store_local_default(g, d, var);
      }
      else if (vlog_kind(d) == V_VAR_DECL && !(vlog_flags(d) & VLOG_F_STATIC))
         vlog_lower_one_local(g, d, vlog_prop_name_in(c, d));
   }
}

static void vlog_lower_class_decl(mir_unit_t *mu, object_t *obj)
{
   vlog_node_t v = vlog_from_object(obj);
   assert(vlog_kind(v) == V_CLASS_DECL);

   vlog_gen_t g = {
      .mu  = mu,
      .mir = mir_get_context(mu),
   };

   ident_t parent = mir_get_parent(mu);
   if (parent != NULL)
      mir_add_param(mu, mir_context_type(mu, parent), MIR_NULL_STAMP,
                    ident_new("context"));
   else
      mir_add_param(mu, mir_context_type(mu, vlog_subr_name(v)),
                    MIR_NULL_STAMP, ident_new("context"));

   mir_type_t t_vid = mir_int_type(mu, INT64_MIN, INT64_MAX);
   mir_value_t vid = mir_add_var(mu, t_vid, MIR_NULL_STAMP,
                                 ident_new("$vid"), 0);
   mir_build_store(mu, vid, mir_const(mu, t_vid, vlog_class_id(v)));

   mir_value_t oid = mir_add_var(mu, t_vid, MIR_NULL_STAMP, ident_new("$oid"),
                                 0);
   mir_value_t onum = mir_build_fcall(mu, ident_new("__sv_class_oid"), t_vid,
                                      MIR_NULL_STAMP, NULL, 0);
   mir_build_consume(mu, onum);
   mir_build_store(mu, oid, onum);

   vlog_lower_class_fields(&g, v);

   if (!mir_block_finished(mu, MIR_NULL_BLOCK))
      mir_build_return(mu, MIR_NULL_VALUE);

   mir_optimise(mu, MIR_PASS_O0);
   vlog_lower_cleanup(&g);
}

static mir_type_t vlog_lower_vhdl_type(mir_unit_t *mu, type_t type)
{
   if (type_eq(type, ieee_type(IEEE_STD_ULOGIC)))
      return mir_int_type(mu, 0, 8);
   else if (type_eq(type, verilog_type(VERILOG_NET_VALUE)))
      return mir_int_type(mu, 0, 255);
   else if (type_eq(type, verilog_type(VERILOG_LOGIC)))
      return mir_int_type(mu, 0, 3);

   fatal_trace("cannot lower VHDL type %pT", type);
}

typedef enum {
   CONV_DRIVE,         // Drive the target from the resolved source
   CONV_INOUT_OUT,     // Drive the target from the driving value of the source
   CONV_INOUT_IN,      // Deposit the effective value of the target
} vlog_conv_mode_t;

static void vlog_lower_converter(mir_unit_t *mu, tree_t cf,
                                 vlog_conv_mode_t mode)
{
   type_t src_type = tree_type(tree_value(cf));
   type_t src_elem = type_elem_recur(src_type);
   mir_type_t t_src_elem = vlog_lower_vhdl_type(mu, src_elem);
   mir_type_t t_src_elem_signal = mir_signal_type(mu, t_src_elem);

   mir_type_t t_src_signal = t_src_elem_signal;
   if (type_is_array(src_type))
      t_src_signal = mir_uarray_type(mu, 1, t_src_elem_signal);

   mir_type_t t_offset = mir_offset_type(mu);

   type_t dst_type = tree_type(cf);
   type_t dst_elem = type_elem_recur(dst_type);
   mir_type_t t_dst_elem = vlog_lower_vhdl_type(mu, dst_elem);
   mir_type_t t_dst_elem_signal = mir_signal_type(mu, t_dst_elem);

   mir_type_t t_dst_signal = t_dst_elem_signal, t_dst = t_dst_elem;
   if (type_is_array(dst_type)) {
      t_dst = mir_uarray_type(mu, 1, t_dst_elem);
      t_dst_signal = mir_uarray_type(mu, 1, t_dst_elem_signal);
   }

   mir_value_t in = mir_add_param(mu, t_src_signal, MIR_NULL_STAMP,
                                  ident_new("src"));
   mir_value_t out = mir_add_param(mu, t_dst_signal, MIR_NULL_STAMP,
                                   ident_new("dst"));

   mir_block_t start_bb = mir_add_block(mu);
   assert(start_bb.id == 1);

   {
      mir_value_t out_nets = out, out_count = mir_const(mu, t_offset, 1);
      if (mir_is(mu, out, MIR_TYPE_UARRAY)) {
         out_nets = mir_build_unwrap(mu, out);
         out_count = mir_build_uarray_len(mu, out, 0);
      }

      if (mode != CONV_INOUT_IN)
         mir_build_drive_signal(mu, out_nets, out_count);

      mir_value_t in_nets = in, in_count = mir_const(mu, t_offset, 1);
      if (mir_is(mu, in, MIR_TYPE_UARRAY)) {
         in_nets = mir_build_unwrap(mu, in);
         in_count = mir_build_uarray_len(mu, in, 0);
      }

      if (mode == CONV_INOUT_OUT)
         mir_build_sched_active(mu, in_nets, in_count);
      else
         mir_build_sched_event(mu, in_nets, in_count);

      if (mode == CONV_INOUT_IN)
         mir_build_sched_event(mu, out_nets, out_count);

      mir_build_return(mu, MIR_NULL_VALUE);
   }

   mir_set_cursor(mu, start_bb, MIR_APPEND);

   tree_t decl = tree_ref(cf);
   assert(tree_kind(decl) == T_FUNC_DECL);
   assert(type_eq(type_result(tree_type(decl)), tree_type(cf)));

   ident_t func = tree_ident2(decl);

   mir_value_t pkg = mir_build_link_package(mu, well_known(W_NVC_VERILOG));

   const bool in_array = mir_is(mu, in, MIR_TYPE_UARRAY);

   mir_value_t count = mir_const(mu, t_offset, 1);
   if (in_array) {
      count = mir_build_uarray_len(mu, in, 0);
      in = mir_build_unwrap(mu, in);
   }

   mir_value_t resolved;
   if (mode == CONV_INOUT_OUT)
      resolved = mir_build_driving_value(mu, in, count);
   else
      resolved = mir_build_resolved(mu, in);

   mir_value_t arg;
   if (in_array) {
      mir_dim_t dims[] = {
         { .left  = mir_const(mu, t_offset, 1),
           .right = count,
           .dir   = mir_const(mu, mir_bool_type(mu), RANGE_TO),
         }
      };
      arg = mir_build_wrap(mu, resolved, dims, 1);
   }
   else
      arg = mir_build_load(mu, resolved);

   mir_value_t args[] = { pkg, arg };

   mir_value_t result = mir_build_fcall(mu, func, t_dst, MIR_NULL_STAMP,
                                        args, ARRAY_LEN(args));

   if (mir_is(mu, result, MIR_TYPE_UARRAY))
      result = mir_build_unwrap(mu, result);

   if (mir_is(mu, out, MIR_TYPE_UARRAY))
      out = mir_build_unwrap(mu, out);

   if (mode == CONV_INOUT_IN)
      mir_build_deposit_signal(mu, out, count, result);
   else
      mir_build_put_driver(mu, out, count, result);

   mir_build_wait(mu, start_bb);

   mir_optimise(mu, MIR_PASS_O0);
}

static void vlog_lower_convert_in(mir_unit_t *mu, object_t *obj)
{
   tree_t map = tree_from_object(obj);
   assert(tree_kind(map) == T_PARAM);

   tree_t cf = tree_value(map);
   assert(tree_kind(cf) == T_CONV_FUNC);

   const bool inout = tree_subkind(map) == P_NAMED
      && tree_kind(tree_name(map)) == T_CONV_FUNC;

   vlog_lower_converter(mu, cf, inout ? CONV_INOUT_IN : CONV_DRIVE);
}

static void vlog_lower_convert_out(mir_unit_t *mu, object_t *obj)
{
   tree_t map = tree_from_object(obj);
   assert(tree_kind(map) == T_PARAM);

   tree_t cf = tree_name(map);
   assert(tree_kind(cf) == T_CONV_FUNC);

   const bool inout = tree_kind(tree_value(map)) == T_CONV_FUNC;

   vlog_lower_converter(mu, cf, inout ? CONV_INOUT_OUT : CONV_DRIVE);
}

void vlog_lower_instance(mir_context_t *mc, vlog_node_t body, ident_t parent,
                         tree_t trans)
{
   assert(vlog_kind(body) == V_INST_BODY);

   mir_shape_t *shape = NULL;
   if (parent != NULL)
      shape = mir_get_shape(mc, parent);

   ident_t qual = vlog_ident(body);
   mir_unit_t *mu = mir_unit_new(mc, qual, vlog_to_object(body),
                                 MIR_UNIT_INSTANCE, shape);

   hash_t *map = hash_new(16);

   const int vhdl_ndecls = tree_decls(trans);
   const int vlog_ndecls = vlog_decls(body);

   for (int i = 0; i < vhdl_ndecls; i++) {
      tree_t t = tree_decl(trans, i);
      if (tree_kind(t) == T_VERILOG)
         hash_put(map, tree_vlog(t), t);
   }

   ident_t pkg_name = well_known(W_NVC_VERILOG);
   mir_value_t pkg = mir_build_package_init(mu, pkg_name, MIR_NULL_VALUE);

   mir_type_t t_net_value = mir_int_type(mu, 0, 255);
   mir_type_t t_resolution = mir_resolution_type(mu, t_net_value);

   ident_t var_name = ident_new("NVC.VERILOG.T_WIRE$resolution");
   mir_value_t resfn = mir_build_link_var(mu, pkg, var_name, t_resolution);

   vlog_gen_t g = {
      .mu  = mu,
      .mir = mc,
   };

   const int nports = vlog_ports(body);
   for (int i = 0; i < nports; i++) {
      vlog_node_t d = vlog_ref(vlog_port(body, i));
      assert(vlog_kind(d) == V_PORT_DECL);

      vlog_node_t snt = vlog_struct_nettype_of(d);
      if (snt != NULL) {
         // The port is the signal of the net and the structure follows it
         nettype_flat_t f;
         if (!vlog_nettype_flat(&g, snt, &f))
            fatal_at(vlog_loc(d), "sorry, a port of a user-defined net type "
                     "of this data type is not supported");

         mir_type_t t_signal = mir_signal_type(mu, f.elem);
         mir_value_t p = mir_add_param(mu, t_signal, MIR_NULL_STAMP,
                                       vlog_ident(d));
         mir_value_t var = mir_add_var(mu, t_signal, MIR_NULL_STAMP,
                                       vlog_struct_net_name(vlog_ref(d)),
                                       MIR_VAR_SIGNAL);
         mir_build_store(mu, var, p);

         vlog_node_t sv = vlog_ref(d);
         vlog_lower_struct_net(&g, sv, hash_get(map, sv));
      }
      else if (!vlog_is_memory_port(d))
         vlog_lower_port_decl(&g, d);
      else {
         // Created before any temporary which could have the same name
         // as the variable is found by name when it is connected
         vlog_node_t var = vlog_ref(d);
         vlog_lower_var_decl(&g, var, hash_get(map, var), 0);
      }
   }

   if (vlog_flags(body) & VLOG_F_INTERFACE) {
      if (sv_iface_bodies == NULL)
         sv_iface_bodies = hash_new(16);
      if (hash_get(sv_iface_bodies, vlog_ident2(body)) == NULL)
         hash_put(sv_iface_bodies, vlog_ident2(body), body);
   }

   // Virtual interfaces and hierarchical calls need the instance context
   bool need_self = !!(vlog_flags(body) & VLOG_F_INTERFACE);
   for (int i = 0; !need_self && i < vlog_ndecls; i++) {
      const vlog_kind_t dk = vlog_kind(vlog_decl(body, i));
      need_self = dk == V_FUNC_DECL || dk == V_TASK_DECL;
   }

   if (need_self) {
      mir_type_t t_self = mir_context_type(mu, qual);
      mir_value_t self_var = mir_add_var(mu, t_self, MIR_NULL_STAMP,
                                         ident_new("$self"), 0);
      mir_build_store(mu, self_var, mir_build_context_upref(mu, 0));
   }

   for (int i = 0, hops; i < vlog_ndecls; i++) {
      vlog_node_t d = vlog_decl(body, i);

      switch (vlog_kind(d)) {
      case V_PORT_DECL:
         break;   // Translated above
      case V_NET_DECL:
         if (mir_is_null(mir_search_object(mu, d, &hops))) {
            if (vlog_nettype_of(d) != NULL)
               vlog_lower_var_decl(&g, d, hash_get(map, d), 0);
            else
               vlog_lower_net_decl(&g, d, hash_get(map, d), resfn, 0);
         }
         else {
            assert(hops == 0); // Port
            if (vlog_has_value(d))
               vlog_lower_net_assign(&g, d, hash_get(map, d));
         }
         break;
      case V_VAR_DECL:
         if (mir_is_null(mir_search_object(mu, d, &hops)))
            vlog_lower_var_decl(&g, d, hash_get(map, d), 0);
         else {
            assert(hops == 0); // Port

            vlog_node_t port = NULL;
            for (int j = 0; j < nports && port == NULL; j++) {
               vlog_node_t p = vlog_ref(vlog_port(body, j));
               if (vlog_ref(p) == d)
                  port = p;
            }

            if (port != NULL && vlog_subkind(port) == V_PORT_OUTPUT
                && hash_get(map, d) != NULL && vlog_ranges(d) == 0
                && !vlog_has_value(d) && !vlog_has_net_driver(body, d, port)
                && !vlog_is_dynamic_type(vlog_type(d))
                && mir_get_class(mu, vlog_type_info(
                      &g, vlog_type(d))->type) == MIR_TYPE_VEC4)
               vlog_lower_port_xinit_spawn(&g, port, hash_get(map, d));
         }
         break;
      case V_GENVAR_DECL:
         vlog_lower_genvar_decl(&g, d);
         break;
      case V_LOCALPARAM:
         break;  // Always inlined for now
      case V_FUNC_DECL:
         mir_defer(mc, ident_prefix(vlog_ident2(d), vlog_ident(d), '.'), qual,
                   MIR_UNIT_FUNCTION, vlog_lower_func_decl, vlog_to_object(d));
         break;
      case V_TASK_DECL:
         mir_defer(mc, ident_prefix(vlog_ident2(d), vlog_ident(d), '.'), qual,
                   MIR_UNIT_PROCEDURE, vlog_lower_task_decl, vlog_to_object(d));
         break;
      case V_CLASS_DECL:
         // A process initialises a specialisation as this may refer to
         // an interface which has not been elaborated yet
         if (vlog_flags(d) & VLOG_F_SPEC)
            vlog_note_spec(d);
         else {
            vlog_defer_class(mc, d, qual);
            vlog_lower_class_statics(&g, d);
         }
         break;
      default:
         CANNOT_HANDLE(d);
      }
   }

   mir_value_t self = mir_build_context_upref(mu, 0);

   const int nstmts = tree_stmts(trans);
   for (int i = 0; i < nstmts; i++) {
      tree_t wrap = tree_stmt(trans, i);
      if (tree_kind(wrap) != T_VERILOG)
         continue;

      vlog_node_t s = tree_vlog(wrap);

      switch (vlog_kind(s)) {
      case V_FINAL:
      case V_ASSIGN:
      case V_INITIAL:
      case V_ALWAYS:
      case V_GATE_INST:
         {
            ident_t sym = ident_prefix(qual, vlog_ident(s), '.');
            mir_defer(mc, sym, qual, MIR_UNIT_PROCESS,
                      vlog_lower_deferred, vlog_to_object(s));

            mir_type_t t_offset = mir_offset_type(mu);
            mir_value_t args[] = { self };
            mir_value_t closure =
               mir_build_closure(mu, sym, t_offset, args, 1);
            mir_value_t locus = mir_build_debug_locus(mu, tree_to_object(wrap));

            mir_build_process_init(mu, closure, locus);
         }
         break;
      case V_UDP_TABLE:
         {
            ident_t sym = ident_prefix(qual, vlog_ident(s), '.');
            mir_defer(mc, sym, qual, MIR_UNIT_PROCESS,
                      vlog_lower_udp, vlog_to_object(body));

            mir_type_t t_offset = mir_offset_type(mu);
            mir_value_t args[] = { self };
            mir_value_t closure =
               mir_build_closure(mu, sym, t_offset, args, 1);
            mir_value_t locus = mir_build_debug_locus(mu, tree_to_object(wrap));

            mir_build_process_init(mu, closure, locus);
         }
         break;

      case V_TCHECK:
         {
            mir_type_t t_offset = mir_offset_type(mu);

            // TODO: Put name of the t-check to vlog AST ?
            //       Name is retrieved at multiple places!
            ident_t name;
            switch (vlog_subkind(s)) {
            case V_TCHECK_SETUP:
               name = ident_uniq("$setup");
               break;
            case V_TCHECK_HOLD:
               name = ident_uniq("$hold");
               break;
            case V_TCHECK_RECOVERY:
               name = ident_uniq("$recovery");
               break;
            case V_TCHECK_REMOVAL:
               name = ident_uniq("$removal");
               break;
            default:
               should_not_reach_here();
               break;
            }

            ident_t sym = ident_prefix(qual, name, '.');
            mir_defer(mc, sym, qual, MIR_UNIT_PROCESS,
                     vlog_lower_deferred, vlog_to_object(s));

            mir_value_t self = mir_build_context_upref(mu, 0);
            mir_value_t args[] = { self };
            mir_value_t closure = mir_build_closure(mu, sym, t_offset, args, 1);
            mir_value_t locus = mir_build_debug_locus(mu, tree_to_object(wrap));

            mir_build_process_init(mu, closure, locus);
         }
         break;

      default:
         break;
      }
   }

   mir_build_return(mu, MIR_NULL_VALUE);

   vlog_lower_cleanup(&g);

   hash_free(map);

   mir_optimise(mu, MIR_PASS_O1);
   mir_put_unit(mc, mu);
}

static void vlog_lower_inout_nets(vlog_gen_t *g, vlog_node_t port,
                                  vlog_node_t value, tree_t wrap,
                                  mir_value_t resfn)
{
   // The port is a net of its own which takes its effective value from
   // the nets it is connected to and is a source for each of them.  If
   // the widths differ the least significant bits are connected.

   vlog_node_t d = vlog_ref(port);
   if (vlog_kind(d) != V_NET_DECL)
      fatal_at(vlog_loc(value), "sorry, an inout port connected to a "
               "concatenation must be a net");

   vlog_lower_net_decl(g, d, wrap, resfn, NET_F_EFFECTIVE | NET_F_INOUT);

   int hops;
   mir_value_t var = mir_search_object(g->mu, d, &hops);
   assert(!mir_is_null(var));
   assert(hops == 0);

   mir_value_t nets = mir_build_load(g->mu, var);
   mir_put_object(g->mu, port, nets);

   const type_info_t *ti = vlog_type_info(g, vlog_type(d));
   const int port_size = ti->size * vlog_size(d);

   const unsigned nlvalues = vlog_count_lvalues(value);
   vlog_select_t *lvalues = xmalloc_array(nlvalues, sizeof(vlog_select_t));

   unsigned index = 0;
   vlog_lower_lvalues(g, value, lvalues, NULL, &index);
   assert(index == nlvalues);

   int total = 0;
   for (int i = 0; i < nlvalues; i++)
      total += lvalues[i].size;

   mir_type_t t_offset = mir_offset_type(g->mu);

   const int skip = MAX(total - port_size, 0);
   const int base = MAX(port_size - total, 0);

   int pos = 0;
   for (int i = 0; i < nlvalues; pos += lvalues[i].size, i++) {
      int64_t count_const;
      if (!mir_is_signal(g->mu, lvalues[i].obj)
          || !mir_get_const(g->mu, lvalues[i].count, &count_const)
          || count_const != lvalues[i].size)
         fatal_at(vlog_loc(value), "sorry, an inout port can only be "
                  "connected to a concatenation of nets");

      if (pos + lvalues[i].size <= skip)
         continue;

      const int first = MAX(skip - pos, 0);
      const int count = lvalues[i].size - first;

      mir_value_t dst_offset =
         mir_build_add(g->mu, t_offset, lvalues[i].dst_offset,
                       mir_const(g->mu, t_offset, first));
      mir_value_t dst = mir_build_array_ref(g->mu, lvalues[i].obj, dst_offset);
      mir_value_t src =
         mir_build_array_ref(g->mu, nets,
                             mir_const(g->mu, t_offset,
                                       base + pos + first - skip));
      mir_build_map_signal(g->mu, src, dst, mir_const(g->mu, t_offset, count));
   }

   free(lvalues);
}

typedef struct {
   vlog_node_t port;
   bool        pos, neg, any;
} port_events_t;

static void vlog_port_events_cb(vlog_node_t v, void *context)
{
   port_events_t *pe = context;

   vlog_node_t root = vlog_value(v);
   while (vlog_kind(root) == V_BIT_SELECT || vlog_kind(root) == V_PART_SELECT)
      root = vlog_value(root);

   if (vlog_kind(root) != V_REF || !vlog_has_ref(root)
       || vlog_ref(root) != pe->port)
      return;

   switch ((v_event_kind_t)vlog_subkind(v)) {
   case V_EVENT_POSEDGE: pe->pos = true; break;
   case V_EVENT_NEGEDGE: pe->neg = true; break;
   default: pe->any = true; break;
   }
}

static bool vlog_port_sees_init(vlog_node_t body, vlog_node_t port,
                                vlog_node_t var)
{
   // The change of an input port from its initial value to that of the
   // variable it is connected to is only visible to an event control, and
   // sharing the net avoids delaying the port by a delta cycle

   port_events_t pe = { .port = port };
   vlog_visit_only(body, vlog_port_events_cb, &pe, V_EVENT);

   int64_t init = 0;
   bool known = vlog_size(port) == 1 && vlog_size(var) == 1;
   if (known && vlog_has_value(var)) {
      vlog_node_t value = vlog_value(var);
      known = vlog_kind(value) == V_NUMBER
         && number_width(vlog_number(value)) <= 64
         && vlog_get_const(value, &init);
   }

   if (pe.any)
      return true;
   else if (!known)
      return pe.pos || pe.neg;

   return init != 0 ? pe.pos : pe.neg;
}

static void vlog_lower_nettype_port(vlog_gen_t *g, vlog_node_t v, bool vhdl)
{
   // A port of a user-defined net type merges the nets it connects into
   // a single simulated net so the resolution function sees all of their
   // drivers (23.3.3.5)

   vlog_node_t port = vlog_ref(v);
   vlog_node_t value = vlog_value(v);

   vlog_node_t root = value;
   while (vlog_kind(root) == V_BIT_SELECT)
      root = vlog_value(root);

   vlog_node_t nt = vlog_nettype_of(port);
   if (vhdl || vlog_kind(root) != V_REF || !vlog_has_ref(root)
       || vlog_nettype_of(vlog_ref(root)) != nt)
      fatal_at(vlog_loc(v), "sorry, a port of a user-defined net type can "
               "only be connected to a net of the same net type");

   if (vlog_is_dynamic_type(vlog_type(nt))) {
      if (root != value)
         fatal_at(vlog_loc(v), "sorry, an element of an array of nets of a "
                  "structure net type cannot be connected to a port");

      mir_value_t signal = vlog_struct_net_signal(g, vlog_ref(root));
      mir_put_object(g->mu, port, signal);
      return;
   }

   const type_info_t *ti = vlog_type_info(g, vlog_type(nt));

   vlog_select_t lvalue = vlog_lower_select(g, value);

   // An interconnect has no data type of its own
   const bool is_ic = vlog_kind(vlog_ref(root)) == V_NET_DECL;
   if (is_ic && root == value)
      lvalue.size = ti->size * vlog_size(port);

   int64_t count;
   if (!mir_is_signal(g->mu, lvalue.obj)
       || !mir_get_const(g->mu, lvalue.count, &count)
       || (!is_ic && count != lvalue.size)
       || lvalue.size != ti->size * vlog_size(port))
      fatal_at(vlog_loc(v), "sorry, this connection of a port of a "
               "user-defined net type is not supported");

   mir_value_t nets =
      mir_build_array_ref(g->mu, lvalue.obj, lvalue.dst_offset);
   mir_put_object(g->mu, port, nets);
}

static void vlog_lower_memory_port_conn(vlog_gen_t *g, vlog_node_t v,
                                        tree_t wrap, mir_value_t inst,
                                        mir_value_t context, ident_t parent)
{
   // A port held in memory is a variable of the instance which is
   // connected to its actual after the instance is created

   vlog_node_t port = vlog_ref(v);
   vlog_node_t d = vlog_ref(port);
   const type_info_t *ti = vlog_type_info(g, vlog_type(d));

   mir_value_t ptr = mir_build_link_var(g->mu, inst, vlog_ident(d), ti->type);

   if (vlog_subkind(port) == V_PORT_REF || vlog_subkind(port) == V_PORT_INOUT) {
      // An event is the address of a cell shared by the copies of it so
      // a ref port of an event type names the same event as its actual
      if (!vlog_is_event_port(port) || vlog_kind(vlog_value(v)) != V_REF)
         fatal_at(vlog_loc(v), "sorry, a %s port of this type is not "
                  "supported", vlog_subkind(port) == V_PORT_REF
                  ? "ref" : "inout");

      mir_value_t value = vlog_lower_rvalue(g, vlog_value(v));
      mir_build_store(g->mu, ptr, value);
      return;
   }

   ident_t qual = mir_get_name(g->mu, MIR_NULL_VALUE);
   ident_t sym = ident_prefix(qual, vlog_ident(port), '@');
   mir_defer(g->mir, sym, parent, MIR_UNIT_PROCESS,
             vlog_lower_deferred, vlog_to_object(v));

   mir_value_t args[] = { context, ptr };
   mir_value_t closure = mir_build_closure(g->mu, sym, MIR_NULL_TYPE,
                                           args, ARRAY_LEN(args));
   mir_value_t locus = mir_build_debug_locus(g->mu, tree_to_object(wrap));

   mir_build_process_init(g->mu, closure, locus);
}

void vlog_lower_block(mir_context_t *mc, ident_t parent, tree_t b)
{
   tree_t hier = tree_decl(b, 0);
   assert(tree_kind(hier) == T_HIER);

   mir_shape_t *shape = NULL;
   if (parent != NULL)
      shape = mir_get_shape(mc, parent);

   ident_t qual = tree_ident(hier);
   mir_unit_t *mu = mir_unit_new(mc, qual, tree_to_object(b),
                                 MIR_UNIT_FUNCTION, shape);

   mir_value_t context = MIR_NULL_VALUE;
   if (shape != NULL) {
      mir_type_t t_context = mir_context_type(mu, parent);
      context = mir_add_param(mu, t_context, MIR_NULL_STAMP,
                              ident_new("context"));
   }

   tree_t wrap = tree_ref(hier);
   assert(tree_kind(wrap) == T_VERILOG);

   vlog_node_t body = tree_vlog(wrap);
   const vlog_kind_t body_kind = vlog_kind(body);
   assert(body_kind == V_INST_BODY || body_kind == V_GEN_BLOCK);

   vlog_gen_t g = {
      .mu  = mu,
      .mir = mc,
   };

   hash_t *map = hash_new(16);

   const int vhdl_ndecls = tree_decls(b);
   const int vlog_ndecls = vlog_decls(body);

   for (int i = 1, pos = 0; i < vhdl_ndecls; i++) {
      tree_t t = tree_decl(b, i);
      const vlog_kind_t tkind = vlog_kind(tree_vlog(t));
      if (tkind == V_PORT_MAP || tkind == V_IFACE_PORT)
         continue;

      ident_t id = tree_ident(t);
      for (; pos < vlog_ndecls; pos++) {
         vlog_node_t v = vlog_decl(body, pos);
         if (vlog_kind(v) == V_PORT_DECL)
            continue;
         else if (vlog_ident(v) == id) {
            hash_put(map, v, t);
            break;
         }
      }

      if (pos == vlog_ndecls)
         fatal_trace("missing VHDL signal for %s", istr(id));
   }

   const int vhdl_nports = tree_ports(b);
   const int vlog_nports = body_kind == V_INST_BODY ? vlog_ports(body) : 0;

   for (int i = 0; i < vhdl_nports; i++)
      hash_put(map, vlog_ref(vlog_ref(vlog_port(body, i))), tree_port(b, i));

   ident_t pkg_name = well_known(W_NVC_VERILOG);
   mir_value_t pkg = mir_build_package_init(mu, pkg_name, MIR_NULL_VALUE);

   mir_type_t t_net_value = mir_int_type(mu, 0, 255);
   mir_type_t t_resolution = mir_resolution_type(mu, t_net_value);
   mir_type_t t_offset = mir_offset_type(mu);

   ident_t var_name = ident_new("NVC.VERILOG.T_WIRE$resolution");
   mir_value_t resfn = mir_build_link_var(mu, pkg, var_name, t_resolution);

   for (int i = 1; i < vhdl_ndecls; i++) {
      tree_t t = tree_decl(b, i);
      assert(tree_kind(t) == T_VERILOG);

      vlog_node_t v = tree_vlog(t);
      if (vlog_kind(v) != V_PORT_MAP)
         continue;

      vlog_node_t port = vlog_ref(v);
      assert(vlog_kind(port) == V_PORT_DECL);

      if (vlog_nettype_of(port) != NULL) {
         vlog_lower_nettype_port(&g, v, vhdl_nports > 0);
         continue;
      }
      else if (vlog_is_memory_port(port)) {
         if (vhdl_nports > 0)
            fatal_at(vlog_loc(v), "sorry, a port of this type cannot be "
                     "connected to VHDL");
         continue;   // Connected once the instance exists
      }

      vlog_node_t value = vlog_value(v);
      switch (vlog_kind(value)) {
      case V_CONCAT:
         if (vlog_subkind(port) != V_PORT_INOUT || vhdl_nports > 0)
            break;

         vlog_lower_inout_nets(&g, port, value, hash_get(map, vlog_ref(port)),
                               resfn);
         continue;
      case V_REF:
      case V_HIER_REF:
      case V_BIT_SELECT:
      case V_PART_SELECT:
         {
            // Ports visible to VHDL must be nets so cannot be aliased
            // to variables in the parent
            if (vhdl_nports > 0 && vlog_subkind(port) != V_PORT_INOUT)
               break;

            // A hierarchical name is not bound until after elaboration
            vlog_node_t root = value;
            while (vlog_kind(root) == V_BIT_SELECT
                   || vlog_kind(root) == V_PART_SELECT)
               root = vlog_value(root);

            const type_info_t *ti = vlog_type_info(&g, vlog_type(port));
            if (vlog_kind(root) == V_HIER_REF) {
               if (vlog_subkind(port) != V_PORT_INOUT || root != value
                   || vlog_size(port) != 1)
                  break;

               if (vlog_has_ref(root) && vlog_width(root) != ti->size) {
                  vlog_lower_inout_nets(&g, port, value,
                                        hash_get(map, vlog_ref(port)), resfn);
                  continue;
               }

               // An inout port is the same net as the one it is connected
               // to so can only be aliased
               g.unbound = ti;
               g.bind_size = ti->size;
               vlog_select_t lvalue = vlog_lower_select(&g, value);
               g.unbound = NULL;
               g.bind_size = 0;

               mir_put_object(mu, port, lvalue.obj);
               continue;
            }

            vlog_select_t lvalue = vlog_lower_select(&g, vlog_value(v));
            if (!mir_is_signal(mu, lvalue.obj))
               break;    // Port tied to constant

            // A variable which starts with a known value gives the net of
            // an input port an event in the first time step
            if (vlog_subkind(port) == V_PORT_INPUT
                && vlog_kind(root) == V_REF && vlog_has_ref(root)
                && vlog_kind(vlog_ref(root)) == V_VAR_DECL) {
               vlog_node_t rdecl = vlog_ref(root);
               const bool two_state = mir_get_class(mu, vlog_type_info(
                  &g, vlog_type(rdecl))->type) == MIR_TYPE_VEC2;
               if ((vlog_has_value(rdecl) || two_state)
                   && vlog_port_sees_init(body, port, rdecl))
                  break;
            }

            // Unknown bits of a four-state actual become zero in a
            // two-state port so the nets cannot be shared
            vlog_node_t atype = vlog_get_type(root);
            if (atype != NULL && vlog_subkind(port) == V_PORT_INPUT
                && mir_get_class(mu, ti->type) == MIR_TYPE_VEC2
                && mir_get_class(
                   mu, vlog_type_info(&g, atype)->type) == MIR_TYPE_VEC4)
               break;
            else if (lvalue.size != ti->size * vlog_size(port)) {
               if (vlog_subkind(port) != V_PORT_INOUT || vhdl_nports > 0)
                  break;

               vlog_lower_inout_nets(&g, port, value,
                                     hash_get(map, vlog_ref(port)), resfn);
               continue;
            }

            // Cannot collapse port if select not in range
            int64_t count_const;
            if (!mir_get_const(mu, lvalue.count, &count_const))
               break;
            else if (count_const != lvalue.size)
               break;

            mir_value_t nets =
               mir_build_array_ref(mu, lvalue.obj, lvalue.dst_offset);

            mir_put_object(mu, port, nets);
         }
         continue;
      default:
         break;
      }

      vlog_node_t d = vlog_ref(port);
      switch (vlog_kind(d)) {
      case V_NET_DECL:
         vlog_lower_net_signal(&g, d, hash_get(map, d), resfn, 0);
         break;
      case V_VAR_DECL:
         vlog_lower_var_decl(&g, d, hash_get(map, d),
                             vlog_subkind(port) == V_PORT_INPUT
                             ? SIG_F_INIT_EVENT : 0);
         break;
      default:
         should_not_reach_here();
      }

      int hops;
      mir_value_t var = mir_search_object(mu, d, &hops);
      assert(!mir_is_null(var));
      assert(hops == 0);

      mir_value_t nets = mir_build_load(mu, var);
      mir_put_object(mu, port, nets);

      ident_t sym = ident_prefix(qual, vlog_ident(port), '@');
      mir_defer(mc, sym, parent, MIR_UNIT_PROCESS,
                vlog_lower_deferred, vlog_to_object(v));

      mir_value_t args[] = { context, nets };
      mir_value_t closure = mir_build_closure(mu, sym, MIR_NULL_TYPE,
                                              args, ARRAY_LEN(args));
      mir_value_t locus = mir_build_debug_locus(mu, tree_to_object(t));

      mir_build_process_init(mu, closure, locus);
   }

   for (int i = 0; i < vlog_nports; i++) {
      vlog_node_t ref = vlog_port(body, i);
      assert(vlog_kind(ref) == V_REF);

      vlog_node_t port = vlog_ref(ref);
      assert(vlog_kind(port) == V_PORT_DECL);

      vlog_node_t d = vlog_ref(port);

      if (vlog_is_memory_port(port))
         continue;

      int hops;
      mir_value_t var = mir_search_object(mu, port, &hops);
      if (mir_is_null(var)) {
         switch (vlog_kind(d)) {
         case V_NET_DECL:
            vlog_lower_net_signal(&g, d, hash_get(map, d), resfn, 0);
            break;
         case V_VAR_DECL:
            vlog_lower_var_decl(&g, d, hash_get(map, d), 0);
            break;
         default:
            should_not_reach_here();
         }

         int hops;
         mir_value_t var = mir_search_object(mu, d, &hops);
         assert(!mir_is_null(var));
         assert(hops == 0);

         mir_value_t nets = mir_build_load(mu, var);
         mir_put_object(mu, port, nets);
      }
      else if (vlog_kind(d) == V_VAR_DECL && vlog_has_value(d)) {
         const type_info_t *ti = vlog_type_info(&g, vlog_type(d));
         vlog_node_t value = vlog_value(d);
         mir_value_t init = vlog_lower_with_context(&g, value, ti->type);

         if (mir_is_vector(mu, init)) {
            mir_value_t tmp = MIR_NULL_VALUE;
            if (ti->size > 1) {
               mir_type_t t_array = mir_carray_type(mu, ti->size, ti->unpacked);
               tmp = vlog_get_temp(&g, t_array);
            }

            mir_value_t resize = mir_build_cast(mu, ti->type, init);
            init = mir_build_unpack(mu, resize, 0, tmp);
         }

         mir_value_t count = mir_const(mu, t_offset, ti->size);
         mir_build_map_const(mu, init, var, count);
      }
   }

   mir_value_t *args LOCAL =
      xmalloc_array(vlog_nports + 1, sizeof(mir_value_t));

   if (mir_is_null(context))
      args[0] = mir_build_null(mu, mir_context_type(mu, qual));
   else
      args[0] = context;

   int nargs = 1;
   for (int i = 0; i < vlog_nports; i++) {
      vlog_node_t ref = vlog_port(body, i);
      assert(vlog_kind(ref) == V_REF);

      vlog_node_t port = vlog_ref(ref);
      assert(vlog_kind(port) == V_PORT_DECL);

      if (vlog_is_memory_port(port))
         continue;

      int hops;
      mir_value_t nets = mir_search_object(mu, port, &hops);
      if (mir_is_null(nets)) {
         mir_dump(mu);
         fatal_trace("missing variable for port %pi", vlog_ident(port));
      }
      assert(hops == 0);

      args[nargs++] = nets;
   }

   mir_value_t inst = mir_build_instance_init(mu, vlog_ident(body),
                                              args, nargs);

   mir_set_result(mu, mir_get_type(mu, inst));

   for (int i = 1; i < vhdl_ndecls; i++) {
      tree_t t = tree_decl(b, i);
      vlog_node_t v = tree_vlog(t);
      if (vlog_kind(v) != V_PORT_MAP || !vlog_is_memory_port(vlog_ref(v)))
         continue;

      vlog_lower_memory_port_conn(&g, v, t, inst, context, parent);
   }

   mir_type_t t_bool = mir_bool_type(mu);

   for (int i = 0; i < vlog_nports; i++) {
      vlog_node_t ref = vlog_port(body, i);
      assert(vlog_kind(ref) == V_REF);

      vlog_node_t port = vlog_ref(ref);
      assert(vlog_kind(port) == V_PORT_DECL);

      if (i >= vhdl_nports)
         continue;   // Port conversion only for mixed instantiation
      else if (tree_kind(tree_value(tree_param(b, i))) == T_OPEN)
         continue;   // Driven by Verilog port connection, not a VHDL parent

      int hops;
      mir_value_t src = mir_search_object(mu, port, &hops);
      assert(!mir_is_null(src));
      assert(hops == 0);
      assert(src.tag == MIR_TAG_NODE);

      mir_value_t count = mir_const(mu, t_offset, vlog_size(vlog_type(port)));

      tree_t map = tree_param(b, i);
      tree_t value = tree_value(map), actual = value;

      mir_value_t src_nets = src, src_count = count;

      if (tree_kind(value) == T_CONV_FUNC)
         actual = tree_value(value);

      assert(tree_kind(actual) == T_REF);

      tree_t vhdl_port = tree_ref(actual);
      assert(tree_kind(vhdl_port) == T_PORT_DECL);

      int nth = mir_find_slot(shape, tree_ident(vhdl_port));
      assert(nth >= 0);

      mir_value_t upref = mir_build_var_upref(mu, 1, nth);
      mir_value_t dst = mir_build_load(mu, upref), dst_nets = dst;

      type_t vhdl_type = tree_type(vhdl_port);
      assert(dimension_of(vhdl_type) == 1);

      mir_value_t dst_count;
      if (mir_is(mu, dst, MIR_TYPE_UARRAY)) {
         dst_count = mir_build_uarray_len(mu, dst, 0);
         dst_nets = mir_build_unwrap(mu, dst);
      }
      else if (type_is_array(vhdl_type)) {
         int64_t cval;
         if (!folded_length(range_of(vhdl_type, 0), &cval))
            should_not_reach_here();

         dst_count = mir_const(mu, t_offset, cval);

         mir_dim_t dims[] = {
            { .left = mir_const(mu, t_offset, 1),
              .right = dst_count,
              .dir = mir_const(mu, t_bool, RANGE_TO)
            },
         };
         dst = mir_build_wrap(mu, dst_nets, dims, 1);
      }
      else if (type_is_scalar(vhdl_type))
         dst_count = mir_const(mu, t_offset, 1);
      else
         should_not_reach_here();

      type_t vlog_type = tree_type(tree_port(b, i));
      assert(dimension_of(vlog_type) == 1);

      if (type_is_array(vlog_type)) {
         mir_dim_t dims[] = {
            { .left = mir_const(mu, t_offset, 1),
              .right = src_count,
              .dir = mir_const(mu, t_bool, RANGE_TO)
            },
         };
         src = mir_build_wrap(mu, src_nets, dims, 1);
      }

      if (tree_kind(value) == T_CONV_FUNC) {
         ident_t sym = ident_sprintf("%s.%s$verilog_convert_in",
                                     istr(qual), istr(vlog_ident(port)));
         mir_defer(mc, sym, vlog_ident(body), MIR_UNIT_PROCESS,
                   vlog_lower_convert_in, tree_to_object(map));

         mir_value_t args[] = { inst, dst, src };
         mir_value_t closure = mir_build_closure(mu, sym, t_offset,
                                                 args, ARRAY_LEN(args));
         mir_value_t locus = mir_build_debug_locus(mu, tree_to_object(map));
         mir_build_process_init(mu, closure, locus);
      }

      if (tree_subkind(map) == P_NAMED) {
         tree_t name = tree_name(map);
         if (tree_kind(name) == T_CONV_FUNC) {
            ident_t sym = ident_sprintf("%s.%s$verilog_convert_out",
                                        istr(qual), istr(vlog_ident(port)));
            mir_defer(mc, sym, vlog_ident(body), MIR_UNIT_PROCESS,
                      vlog_lower_convert_out, tree_to_object(map));

            mir_value_t args[] = { inst, src, dst };
            mir_value_t closure = mir_build_closure(mu, sym, t_offset,
                                                    args, ARRAY_LEN(args));
            mir_value_t locus = mir_build_debug_locus(mu, tree_to_object(map));
            mir_build_process_init(mu, closure, locus);
         }
      }

      mir_value_t locus = mir_build_debug_locus(mu, tree_to_object(map));
      mir_build_length_check(mu, src_count, dst_count, locus, MIR_NULL_VALUE);
   }

   mir_build_return(mu, inst);

   vlog_lower_cleanup(&g);

   hash_free(map);

   mir_optimise(mu, MIR_PASS_O1);
   mir_put_unit(mc, mu);
}

mir_unit_t *vlog_lower_thunk(mir_context_t *mc, ident_t parent, vlog_node_t v)
{
   mir_shape_t *shape = parent != NULL ? mir_get_shape(mc, parent) : NULL;
   mir_unit_t *mu = mir_unit_new(mc, NULL, vlog_to_object(v), MIR_UNIT_THUNK,
                                 shape);

   vlog_gen_t g = {
      .mu = mu,
   };

   gen_stack_t gs = {};

   switch (vlog_kind(v)) {
   case V_FOR_INIT:
      assert(vlog_decls(v) == 0);   // TODO
      // Fall-through
   case V_FOR_STEP:
      vlog_lower_stmts(&g, v, &gs);
      mir_build_return(mu, MIR_NULL_VALUE);
      break;
   case V_LOCALPARAM:
      {
         vlog_node_t dt = vlog_type(v);
         if (is_implicit_data_type(dt) && vlog_ranges(dt) == 0) {
            // TODO: better to set null type?
            mir_value_t value = vlog_lower_rvalue(&g, vlog_value(v));

            mir_set_result(mu, mir_get_type(mu, value));
            mir_build_return(mu, value);
         }
         else {
            const type_info_t *ti = vlog_type_info(&g, dt);
            mir_value_t value = vlog_lower_with_context(&g, vlog_value(v),
                                                        ti->type);

            mir_value_t cast = vlog_lower_cast(&g, ti->type, value);

            mir_set_result(mu, ti->type);
            mir_build_return(mu, cast);
         }
      }
      break;
   default:
      {
         mir_value_t value = vlog_lower_rvalue(&g, v);
         mir_set_result(mu, mir_get_type(mu, value));
         mir_build_return(mu, value);
      }
      break;
   }

   mir_optimise(mu, MIR_PASS_O0);

   vlog_lower_cleanup(&g);
   return mu;
}

void vlog_lower_const_funcs(mir_context_t *mc, vlog_node_t inst)
{
   const int ndecls = vlog_decls(inst);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(inst, i);
      if (vlog_kind(d) == V_FUNC_DECL && (vlog_flags(d) & VLOG_F_CONST)) {
         ident_t qual = ident_sprintf("%s.%s@const", istr(vlog_ident2(d)),
                                      istr(vlog_ident(d)));
         mir_defer(mc, qual, NULL, MIR_UNIT_FUNCTION, vlog_lower_func_decl,
                   vlog_to_object(d));
      }
   }
}
