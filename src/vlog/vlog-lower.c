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
#include <inttypes.h>
#include <float.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

typedef struct _gen_stack gen_stack_t;

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
   mir_block_t exit_bb;
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
static void vlog_lower_deferred(mir_unit_t *mu, object_t *obj);
static inline ident_t vlog_subr_name(vlog_node_t d)
{
   return ident_prefix(vlog_ident2(d), vlog_ident(d), '.');
}

static mir_value_t vlog_offset_to_int(vlog_gen_t *g, mir_value_t value)
{
   mir_type_t t_int64 = mir_int_type(g->mu, INT64_MIN, INT64_MAX);
   mir_type_t t_int = mir_vec2_type(g->mu, 32, true);
   return mir_build_cast(g->mu, t_int, mir_build_cast(g->mu, t_int64, value));
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
         ti->type     = mir_vec4_type(g->mu, ti->size, issigned);
         ti->unpacked = t_unpacked_logic;
         ti->signal   = t_logic_signal;
         ti->elemsz   = 1;
         return ti;
      }
   case V_CLASS_DECL:
      {
         ident_t name = ident_prefix(vlog_ident2(v), vlog_ident(v), '.');
         ti->type = mir_context_type(g->mu, name);
         return ti;
      }
   default:
      CANNOT_HANDLE(v);
   }

   switch (vlog_subkind(v)) {
   case DT_REAL:
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
   assert(vlog_flags(type) & VLOG_F_PACKED);

   if (vlog_kind(type) == V_UNION_DECL)
      return 0;

   unsigned offset = 0;

   const int ndecls = vlog_decls(type);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(type, i);
      if (d == member)
         return offset;

      offset += vlog_size(vlog_type(d));
   }

   should_not_reach_here();
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
   if (mir_equals(type, mir_bool_type(g->mu)))
      return value;
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

static vlog_select_t vlog_lower_select(vlog_gen_t *g, vlog_node_t v)
{
   PUSH_DEBUG_INFO(g->mu, v);

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
         case V_ENUM_NAME:
            {
               const type_info_t *ti = vlog_type_info(g, vlog_type(decl));

               mir_value_t value = vlog_lower_rvalue(g, vlog_value(decl));
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
         default:
            break;
         }

         mir_value_t ptr = vlog_lower_decl_ptr(g, decl);

         mir_value_t nets = ptr;
         if (mir_points_to(g->mu, ptr, MIR_TYPE_SIGNAL))
            nets = mir_build_load(g->mu, ptr);

         const type_info_t *ti = vlog_type_info(g, vlog_type(decl));

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
         if (!vlog_has_ref(v))
            fatal_at(vlog_loc(v), "unbound hierarchical reference");

         vlog_node_t decl = vlog_ref(v);
         const type_info_t *ti = vlog_type_info(g, vlog_type(decl));

         mir_value_t locus = mir_build_debug_locus(g->mu, vlog_to_object(v));
         ident_t scope = mir_get_parent(g->mu);

         mir_value_t ptr = mir_build_bind_external(g->mu, locus, scope,
                                                   ti->signal, ti->stamp,
                                                   NULL, 0);

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
   case V_BIT_SELECT:
      {
         vlog_node_t value = vlog_value(v);
         assert(vlog_kind(value) == V_REF);

         vlog_select_t prefix = vlog_lower_select(g, value);

         vlog_node_t decl = vlog_ref(value);
         const int nunpacked =
            vlog_kind(decl) == V_LOCAL_DECL ? vlog_ranges(decl) : 0;
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
         mir_value_t array_off = zero, bit_off = zero;

         mir_value_t in_range =
            mir_build_cmp(g->mu, MIR_CMP_NEQ, prefix.count, zero);

         unsigned elems = vlog_size(decl);
         unsigned size = unpacked_var
            ? (real_mem ? 1 : mir_get_size(g->mu, prefix.type))
            : vlog_is_fixed_unpacked(decl) ? prefix.size
            : elems * mir_get_size(g->mu, prefix.type);

         for (int i = 0; i < nparams; i++) {
            vlog_node_t dim = vlog_get_dim(value, i);

            const unsigned dim_size = vlog_size(dim);
            const bool unpacked_dim =
               unpacked_var && vlog_subkind(dim) == V_DIM_UNPACKED;

            if (unpacked_dim) {
               assert(elems % dim_size == 0);
               elems /= dim_size;
            }
            else {
               assert(size % dim_size == 0);
               size /= dim_size;
            }

            mir_value_t index = vlog_lower_rvalue(g, vlog_param(v, i));
            mir_value_t this_off = vlog_lower_array_off(g, dim, index);
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

         vlog_node_t dim = vlog_get_dim(value, 0);

         mir_value_t index = vlog_lower_rvalue(g, vlog_left(v));
         mir_value_t base = vlog_lower_array_off(g, dim, index);

         mir_value_t off = base;
         const unsigned nelems = vlog_size(v);
         if (kind != V_RANGE_CONST) {
            const bool is_up = vlog_is_up(dim);
            const bool same_dir = (kind == V_RANGE_POS && is_up)
               || (kind == V_RANGE_NEG && !is_up);

            if (!same_dir) {
               mir_type_t t_offset = mir_offset_type(g->mu);
               mir_value_t adj = mir_const(g->mu, t_offset, nelems - 1);
               off = mir_build_sub(g->mu, t_offset, base, adj);
            }
         }

         mir_type_t t_offset = mir_offset_type(g->mu);

         const unsigned dim_size = vlog_size(dim);
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
         vlog_select_t prefix = vlog_lower_select(g, vlog_value(v));
         vlog_node_t decl = vlog_ref(v);
         const type_info_t *ti = vlog_type_info(g, vlog_type(decl));

         if (mir_points_to(g->mu, prefix.obj, MIR_TYPE_CONTEXT)) {
            mir_value_t context = mir_build_load(g->mu, prefix.obj);
            mir_value_t link = mir_build_link_var(g->mu, context,
                                                  vlog_ident(v), ti->type);
            mir_type_t t_offset = mir_offset_type(g->mu);
            mir_value_t zero = mir_const(g->mu, t_offset, 0);

            vlog_select_t result = {
               .obj        = link,
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

static unsigned vlog_unpacked_count(vlog_node_t *dims, int ndims)
{
   unsigned count = 1;
   for (int i = 0; i < ndims; i++)
      count *= vlog_size(dims[i]);
   return count;
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

         switch (vlog_kind(nodes[i])) {
         case V_REF:
         case V_MEMBER_REF:
            mir_build_store(g->mu, lvalues[i].obj, part);
            break;
         default:
            {
               mir_value_t cur = mir_build_load(g->mu, lvalues[i].obj);
               mir_value_t ins =
                  mir_build_insert(g->mu, part, cur, lvalues[i].dst_offset);
               mir_build_store(g->mu, lvalues[i].obj, ins);
            }
            break;
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

   mir_type_t type;
   if (lclass == MIR_TYPE_VEC4 || rclass == MIR_TYPE_VEC4)
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

   if (is_short_circuit && vlog_has_side_effects(rhs_expr)) {
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
         case V_ASSIGN_SHIFT_RA: op = MIR_VEC_SRA; break;
         default:
            CANNOT_HANDLE(v);
         }

         value = mir_build_binary(g->mu, op, type, cur, cast);
      }
   }

   vlog_assign_variable(g, target, value, NULL);
   return value;
}

static mir_value_t vlog_lower_systf_param(vlog_gen_t *g, vlog_node_t v)
{
   switch (vlog_kind(v)) {
   case V_REF:
      switch (vlog_kind(vlog_ref(v))) {
      case V_TF_PORT_DECL:
      case V_FUNC_DECL:
      case V_PARAM_DECL:
      case V_LOCALPARAM:
         return vlog_lower_rvalue(g, v);
      case V_VAR_DECL:
      case V_LOCAL_DECL:
         {
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

static mir_value_t vlog_lower_sys_tfcall(vlog_gen_t *g, vlog_node_t v)
{
   const int nparams = vlog_params(v);
   mir_value_t *args LOCAL =
      xmalloc_array((nparams * 3) + 1, sizeof(mir_value_t));
   int actual = 0;
   mir_type_t t_offset = mir_offset_type(g->mu);
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
      default:
         should_not_reach_here();
      }
   }

   mir_value_t locus = mir_build_debug_locus(g->mu, vlog_to_object(v));

   mir_type_t type = MIR_NULL_TYPE;
   if (vlog_kind(v) == V_SYS_FCALL) {
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

static mir_value_t vlog_lower_sys_fcall(vlog_gen_t *g, vlog_node_t v)
{
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

         mir_value_t arg = vlog_lower_rvalue(g, param);
         if (mir_is_vector(g->mu, arg)) {
            const int size = mir_get_size(g->mu, mir_get_type(g->mu, arg));
            return mir_const_vec(g->mu, t_integer, size, 0);
         }
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

static mir_value_t vlog_lower_rvalue_select(vlog_gen_t *g, vlog_node_t v)
{
   vlog_select_t select = vlog_lower_select(g, v);

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

static mir_value_t vlog_context_for_call(vlog_gen_t *g, vlog_node_t v)
{
   if (mir_get_kind(g->mu) == MIR_UNIT_THUNK || g->constfunc)
      return MIR_NULL_VALUE;

   return mir_build_context_upref(g->mu, 1);  // XXX
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

   const int nparams = vlog_params(v);
   mir_value_t *vals LOCAL = xmalloc_array(nparams, sizeof(mir_value_t));
   unsigned *keys LOCAL = xmalloc_array(nparams, sizeof(unsigned));
   mir_value_t def = MIR_NULL_VALUE;
   for (int i = 0; i < nparams; i++) {
      vlog_node_t item = vlog_param(v, i);
      vals[i] = vlog_lower_rvalue(g, vlog_right(item));
      keys[i] = UINT_MAX;

      if (!vlog_has_left(item))
         def = vals[i];
      else if (dim != NULL) {
         mir_value_t index = vlog_lower_rvalue(g, vlog_left(item));
         mir_value_t key = vlog_lower_array_off(g, dim, index);

         int64_t ival;
         if (mir_get_const(g->mu, key, &ival) && ival >= 0 && ival < width)
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
         mir_type_t t_logic = mir_vec4_type(g->mu, 1, false);

         for (int i = 0; i < width; i++) {
            mir_value_t elem = def;
            for (int j = 0; j < nparams; j++) {
               if (keys[j] == i) {
                  elem = vals[j];
                  break;
               }
            }

            mir_value_t cast = vlog_lower_cast(g, t_logic, elem);
            mir_value_t pos = mir_const(g->mu, t_offset, i);
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
            for (int j = 0; j < nparams; j++) {
               if (keys[j] == i) {
                  elem = vals[j];
                  break;
               }
            }

            vlog_node_t f = vlog_decl(dt, i);

            const type_info_t *fti = vlog_type_info(g, vlog_type(f));
            mir_value_t cast = vlog_lower_cast(g, fti->type, elem);

            mir_value_t uns = vlog_cast_unsigned(g, cast);
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

static mir_value_t vlog_lower_with_context(vlog_gen_t *g, vlog_node_t v,
                                           mir_type_t context)
{
   PUSH_DEBUG_INFO(g->mu, v);

   switch (vlog_kind(v)) {
   case V_REF:
   case V_BIT_SELECT:
   case V_PART_SELECT:
   case V_MEMBER_REF:
      return vlog_widen_vector(g, vlog_lower_rvalue_select(g, v), context);
   case V_HIER_REF:
      {
         if (!vlog_has_ref(v))
            fatal_at(vlog_loc(v), "unbound hierarchical reference");

         vlog_node_t decl = vlog_ref(v);
         const type_info_t *ti = vlog_type_info(g, vlog_type(decl));

         mir_value_t locus = mir_build_debug_locus(g->mu, vlog_to_object(v));
         ident_t scope = mir_get_parent(g->mu);

         mir_value_t ptr = mir_build_bind_external(g->mu, locus, scope,
                                                   ti->signal, ti->stamp,
                                                   NULL, 0);

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
         int width = number_width(num);
         const bool issigned = number_signed(num);

         if (!mir_is_null(context)) {
            mir_class_t class = mir_get_class(g->mu, context);
            if (class == MIR_TYPE_VEC2 || class == MIR_TYPE_VEC4)
               width = MAX(width, mir_get_size(g->mu, context));
         }

         mir_type_t t_full = mir_vec4_type(g->mu, width, issigned);

         uint64_t alow, blow;
         number_word(num, 0, &alow, &blow);

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

            if (aword == 0 && bword == 0)
               continue;

            const int part_size = MIN(width - i, 64);

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

         return inc;
      }
   case V_COND_EXPR:
      {
         // See 1800-2023 section 11.4.11 for semantics

         vlog_node_t left_expr = vlog_left(v);
         vlog_node_t right_expr = vlog_right(v);

         mir_value_t value = vlog_lower_rvalue(g, vlog_value(v));

         const bool is_signed =
            vlog_is_signed(left_expr) && vlog_is_signed(right_expr);

         unsigned size = MAX(vlog_width(left_expr), vlog_width(right_expr));
         mir_type_t type;
         if (mir_is_null(context))
            type = mir_vec4_type(g->mu, size, is_signed);
         else {
            mir_class_t class = mir_get_class(g->mu, context);
            if (class == MIR_TYPE_VEC2 || class == MIR_TYPE_VEC4) {
               size = MAX(size, mir_get_size(g->mu, context));
               type = mir_vec4_type(g->mu, size, is_signed);
            }
            else
               type = context;
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
         ident_t func = ident_prefix(vlog_ident2(decl), vlog_ident(decl), '.');
         if (mir_get_kind(g->mu) == MIR_UNIT_THUNK || g->constfunc)
            func = ident_sprintf("%s@const", istr(func));

         const int nparams = vlog_params(v);
         mir_value_t *args LOCAL =
            xmalloc_array(nparams + 1, sizeof(mir_value_t));

         mir_value_t context = vlog_context_for_call(g, decl);

         int pos = 0;
         if (!mir_is_null(context))
            args[pos++] = context;

         for (int i = 0; i < nparams; i++) {
            mir_value_t value = vlog_lower_rvalue(g, vlog_param(v, i));
            vlog_node_t dt = vlog_type(vlog_port(decl, i));
            const type_info_t *ti = vlog_type_info(g, dt);
            args[pos++] = vlog_lower_cast(g, ti->type, value);
         }

         const type_info_t *ti = vlog_type_info(g, vlog_type(decl));
         mir_value_t result = mir_build_fcall(g->mu, func, ti->type,
                                              MIR_NULL_STAMP, args, pos);
         mir_build_consume(g->mu, result);
         return result;
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
      {
         const type_info_t *ti = vlog_type_info(g, vlog_type(v));
         mir_value_t context = mir_build_context_upref(g->mu, 0);  // XXX
         return mir_build_protected_init(g->mu, ti->type, context,
                                         MIR_NULL_VALUE, MIR_NULL_VALUE);
      }
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
         int64_t width = vlog_must_be_const(vlog_left(v));
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

static mir_value_t vlog_lower_time(vlog_gen_t *g, vlog_node_t v)
{
   mir_type_t t_time = mir_time_type(g->mu);

   if (vlog_kind(v) == V_NUMBER) {
      number_t num = vlog_number(v);
      return mir_const(g->mu, t_time, number_integer(num));
   }
   else {
      mir_value_t value = vlog_lower_rvalue(g, v);
      return vlog_lower_cast(g, t_time, value);
   }
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

static void vlog_lower_edge_fn(mir_unit_t *mu, vlog_edge_t edge)
{
   mir_type_t t_context = mir_context_type(mu, ident_new("dummy"));
   mir_type_t t_bool = mir_bool_type(mu);
   mir_type_t t_offset = mir_offset_type(mu);
   mir_type_t t_uint8 = mir_int_type(mu, 0, UINT8_MAX);
   mir_type_t t_logic = mir_vec4_type(mu, 1, false);
   mir_type_t t_signal = mir_signal_type(mu, t_uint8);

   mir_set_result(mu, t_bool);

   mir_add_param(mu, t_context, MIR_NULL_STAMP, ident_new("context"));
   mir_value_t nets = mir_add_param(mu, t_signal, MIR_NULL_STAMP,
                                    ident_new("arg"));

   mir_value_t cur_ptr = mir_build_resolved(mu, nets);
   mir_value_t cur_val = mir_build_load(mu, cur_ptr);
   mir_value_t cur_packed = mir_build_pack(mu, t_logic, cur_val);
   mir_value_t cur_cmp = mir_build_unpack(mu, cur_packed, 0, MIR_NULL_VALUE);

   mir_value_t last_ptr = mir_build_last_value(mu, nets);
   mir_value_t last_val = mir_build_load(mu, last_ptr);
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
   mir_value_t event = mir_build_event_flag(mu, nets, count);
   mir_value_t and = mir_build_and(mu, cmp, event);

   mir_build_return(mu, and);

   mir_optimise(mu, MIR_PASS_O0);
}

static void vlog_lower_posedge_fn(mir_unit_t *mu, object_t *obj)
{
   vlog_lower_edge_fn(mu, POSEDGE);
}

static void vlog_lower_negedge_fn(mir_unit_t *mu, object_t *obj)
{
   vlog_lower_edge_fn(mu, NEGEDGE);
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

               vlog_select_t lvalue = vlog_lower_select(g, v);
               int total_size = lvalue.size;
               if (vlog_kind(v) == V_REF)
                  total_size = vlog_whole_size(g, v, &lvalue);

               mir_value_t count = mir_const(g->mu, t_offset, total_size);

               return mir_build_level_trigger(g->mu, lvalue.obj, count);
            }
         case V_PARAM_DECL:
         case V_LOCALPARAM:
            return MIR_NULL_VALUE;
         default:
            CANNOT_HANDLE(v);
         }
      }
      break;
   case V_BIT_SELECT:
   case V_PART_SELECT:
      {
         mir_value_t trigger;
         vlog_node_t prefix = vlog_longest_static_prefix(v);
         if (prefix == v) {
            vlog_select_t lvalue = vlog_lower_select(g, v);

            mir_value_t nets =
               mir_build_array_ref(g->mu, lvalue.obj, lvalue.dst_offset);

            trigger = mir_build_level_trigger(g->mu, nets, lvalue.count);
         }
         else
            trigger = vlog_lower_trigger(g, prefix);

         if (kind == V_BIT_SELECT) {
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

            ident_t func;
            switch (kind) {
            case V_EVENT_POSEDGE:
               func = ident_new("@posedge");
               mir_defer(mir_get_context(g->mu), func, NULL, MIR_UNIT_FUNCTION,
                         vlog_lower_posedge_fn, NULL);
               break;
            case V_EVENT_NEGEDGE:
               func = ident_new("@negedge");
               mir_defer(mir_get_context(g->mu), func, NULL, MIR_UNIT_FUNCTION,
                         vlog_lower_negedge_fn, NULL);
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

static void vlog_lower_timing(vlog_gen_t *g, vlog_node_t v, gen_stack_t *gs)
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

{
   vlog_node_t ctrl = vlog_value(v);
   switch (vlog_kind(ctrl)) {
   case V_DELAY_CONTROL:
      {
         mir_type_t t_time = mir_time_type(g->mu);
         mir_value_t delay = vlog_lower_rvalue(g, vlog_value(ctrl));
         mir_value_t cast = mir_build_cast(g->mu, t_time, delay);

         int64_t dconst;
         if (mir_get_const(g->mu, cast, &dconst) && dconst == 0)
            mir_build_sched_inactive(g->mu);
         else
            mir_build_sched_process(g->mu, cast);

         mir_block_t wait_bb = mir_add_block(g->mu);
         mir_build_wait(g->mu, wait_bb);

         mir_set_cursor(g->mu, wait_bb, MIR_APPEND);
      }
      break;
   case V_EVENT_CONTROL:
      {
         mir_value_t trigger_var = vlog_lower_trigger_var(g, ctrl);
         if (!mir_is_null(trigger_var)) {
            mir_value_t trigger = mir_build_load(g->mu, trigger_var);
            mir_build_sched_event(g->mu, trigger, MIR_NULL_VALUE);
         }

         mir_block_t wait_bb = mir_add_block(g->mu);
         mir_build_wait(g->mu, wait_bb);

         mir_set_cursor(g->mu, wait_bb, MIR_APPEND);

         if (!mir_is_null(trigger_var)) {
            mir_value_t trigger = mir_build_load(g->mu, trigger_var);
            mir_build_clear_event(g->mu, trigger, MIR_NULL_VALUE);
         }
      }
      break;
   default:
      CANNOT_HANDLE(ctrl);
   }

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

   vlog_assign_variable(g, vlog_target(v), MIR_NULL_VALUE, vlog_value(v));
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

   mir_type_t t_time = mir_time_type(g->mu);
   mir_type_t t_offset = mir_offset_type(g->mu);

   mir_value_t unpacked;
   if (class == MIR_TYPE_VEC2 || class == MIR_TYPE_VEC4) {
      vlog_node_t rhs = vlog_value(v);
      mir_type_t context = vlog_assign_context(g, type, rhs);
      mir_value_t value = vlog_lower_with_context(g, rhs, context);
      mir_value_t resize = vlog_lower_cast(g, type, value);
      assert(mir_is_vector(g->mu, resize));

      mir_value_t tmp = MIR_NULL_VALUE;
      if (targetsz > 1) {
         mir_type_t t_elem = mir_logic_type(g->mu);
         mir_type_t t_array = mir_carray_type(g->mu, targetsz, t_elem);
         tmp = vlog_get_temp(g, t_array);
      }

      const uint8_t strength = vlog_is_net(target) ? ST_STRONG : 0;
      unpacked = mir_build_unpack(g->mu, resize, strength, tmp);
   }
   else {
      mir_value_t value =
         vlog_lower_with_context(g, vlog_value(v), lvalues[0].type);
      unpacked = vlog_lower_cast(g, lvalues[0].type, value);
   }

   mir_value_t after;
   if (vlog_has_delay(v)) {
      vlog_node_t delay = vlog_delay(v);
      if (vlog_kind(delay) != V_DELAY_CONTROL)
         fatal_at(vlog_loc(v), "sorry, an event control in a non-blocking "
                  "assignment is not supported");

      after = vlog_lower_time(g, vlog_value(delay));
   }
   else
      after = mir_const(g->mu, t_time, 0);

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

      if (class == MIR_TYPE_VEC2 || class == MIR_TYPE_VEC4) {
         mir_value_t pos = mir_build_add(g->mu, t_offset,
                                         mir_const(g->mu, t_offset, offset),
                                         lvalues[i].src_offset);
         mir_value_t src = mir_build_array_ref(g->mu, unpacked, pos);
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

static void vlog_lower_disable(vlog_gen_t *g, vlog_node_t v, gen_stack_t *gs)
{
   ident_t name = vlog_ident(v);

   for (gen_stack_t *it = gs; it != NULL; it = it->up) {
      if (it->block != NULL && it->block->name == name) {
         mir_build_jump(g->mu, it->block->exit_bb);
         return;
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

static void vlog_lower_case(vlog_gen_t *g, vlog_node_t v, gen_stack_t *gs)
{
   if (vlog_subkind(v) == V_CASE_INSIDE) {
      vlog_lower_case_inside(g, v, gs);
      return;
   }

   mir_comment(g->mu, "Begin case statement");

   mir_value_t value = vlog_lower_rvalue(g, vlog_value(v));
   bool is_real = mir_is(g->mu, value, MIR_TYPE_REAL);

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

   if (mir_is_null(g->exit))
      mir_build_return(g->mu, result);
   else
      mir_build_jump(g->mu, g->exit);
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

static vlog_select_t vlog_lower_wselect(vlog_gen_t *g, vlog_node_t v)
{
   // Select something which is going to be modified

   g->for_write = true;
   vlog_select_t result = vlog_lower_select(g, v);
   g->for_write = false;
   return result;
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

static void vlog_lower_user_tcall(vlog_gen_t *g, vlog_node_t v)
{
   vlog_node_t decl = vlog_ref(v);
   ident_t func = ident_prefix(vlog_ident2(decl), vlog_ident(decl), '.');

   const int nparams = vlog_params(v);
   mir_value_t *args LOCAL =
      xmalloc_array(nparams + 1, sizeof(mir_value_t));

   SCOPED_A(mir_value_t) temps = AINIT;

   args[0] = vlog_context_for_call(g, decl);
   for (int i = 0; i < nparams; i++) {
      vlog_node_t port = vlog_port(decl, i);
      const type_info_t *ti = vlog_type_info(g, vlog_type(port));

      const v_port_kind_t dir = vlog_subkind(port);
      if (dir == V_PORT_OUTPUT || dir == V_PORT_INOUT) {
         ident_t name = ident_uniq("%s.tmp", istr(vlog_ident(port)));
         mir_value_t tmp = mir_add_var(g->mu, ti->type, ti->stamp, name, 0);

         mir_value_t init;
         if (dir == V_PORT_OUTPUT)
            init = vlog_lower_default_value(g, ti);
         else {
            mir_value_t value = vlog_lower_rvalue(g, vlog_param(v, i));
            init = vlog_lower_cast(g, ti->type, value);
         }

         mir_build_store(g->mu, tmp, init);

         args[i + 1] = tmp;
         APUSH(temps, tmp);
      }
      else {
         mir_value_t value = vlog_lower_rvalue(g, vlog_param(v, i));
         args[i + 1] = vlog_lower_cast(g, ti->type, value);
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

   mir_block_t resume_bb = mir_add_block(g->mu);
   mir_build_pcall(g->mu, func, resume_bb, args, nparams + 1);

   mir_set_cursor(g->mu, resume_bb, MIR_APPEND);
   mir_build_resume(g->mu, func);

   if (temps.count > 0) {
      for (int i = 0, pos = 0; i < nparams; i++) {
         vlog_node_t port = vlog_port(decl, i);
         if (vlog_subkind(port) != V_PORT_INPUT) {
            mir_value_t tmp = AGET(temps, pos++);
            mir_value_t value = mir_build_load(g->mu, tmp);
            vlog_assign_variable(g, vlog_param(v, i), value, NULL);
         }
      }

      ACLEAR(temps);
   }
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

static void vlog_lower_always(vlog_gen_t *g, vlog_node_t v)
{
   mir_block_t start_bb = mir_add_block(g->mu);
   assert(start_bb.id == 1);

   vlog_node_t s0 = vlog_stmt(v, 0);
   if (vlog_kind(s0) == V_TIMING) {
      // Cache the event control trigger once on reset to avoid
      // regenerating it on each invocation
      vlog_node_t ctrl = vlog_value(s0);
      if (vlog_kind(ctrl) == V_EVENT_CONTROL)
         vlog_lower_trigger_var(g, ctrl);
   }

   mir_build_return(g->mu, MIR_NULL_VALUE);

   mir_set_cursor(g->mu, start_bb, MIR_APPEND);

   gen_stack_t gs = {};

   vlog_lower_stmts(g, v, &gs);

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

   mir_type_t t_time = mir_time_type(g->mu);
   mir_build_sched_process(g->mu, mir_const(g->mu, t_time, TIME_HIGH));

   mir_block_t final_bb = mir_add_block(g->mu);
   mir_build_wait(g->mu, final_bb);

   mir_set_cursor(g->mu, final_bb, MIR_APPEND);

   gen_stack_t gs = {};

   vlog_lower_stmts(g, v, &gs);

   if (!mir_block_finished(g->mu, MIR_NULL_BLOCK))
      mir_build_return(g->mu, MIR_NULL_VALUE);
}

static void vlog_lower_sensitivity(vlog_gen_t *g, vlog_node_t v)
{
   const vlog_kind_t kind = vlog_kind(v);
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

               vlog_select_t lvalue = vlog_lower_select(g, v);
               int total_size = lvalue.size;
               if (vlog_kind(v) == V_REF)
                  total_size = vlog_whole_size(g, v, &lvalue);

               mir_value_t count = mir_const(g->mu, t_offset, total_size);
               mir_build_sched_event(g->mu, lvalue.obj, count);
            }
            break;
         case V_PARAM_DECL:
         case V_LOCALPARAM:
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

            mir_build_sched_event(g->mu, nets, lvalue.count);
         }
         else if (prefix != NULL)
            vlog_lower_sensitivity(g, prefix);

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
      {
         const int nparams = vlog_params(v);
         for (int i = 0; i < nparams; i++)
            vlog_lower_sensitivity(g, vlog_param(v, i));
      }
      break;
   default:
      CANNOT_HANDLE(v);
   }
}

static void vlog_lower_assign_process(vlog_gen_t *g, vlog_node_t v)
{
   mir_block_t start_bb = mir_add_block(g->mu);
   assert(start_bb.id == 1);

   vlog_lower_driver(g, vlog_target(v));

   vlog_lower_sensitivity(g, vlog_value(v));

   mir_build_return(g->mu, MIR_NULL_VALUE);

   mir_set_cursor(g->mu, start_bb, MIR_APPEND);

   vlog_node_t target = vlog_target(v);

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
   mir_type_t context = vlog_assign_context(g, type, rhs);
   mir_value_t value = vlog_lower_with_context(g, rhs, context);
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
   if (vlog_has_delay(v)) {
      vlog_node_t delay = vlog_delay(v);
      assert(vlog_kind(delay) == V_DELAY_CONTROL);

      after = vlog_lower_time(g, vlog_value(delay));
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

      if (mir_is_null(after))
         mir_build_put_driver(g->mu, nets, lvalues[i].count, src);
      else
         mir_build_sched_waveform(g->mu, nets, lvalues[i].count, src, after,
                                  after);
   }

   mir_build_wait(g->mu, start_bb);

   if (lvalues != &lvalue1)
      free(lvalues);
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

   {
      mir_value_t upref = mir_build_var_upref(g->mu, hops, var.id);
      mir_value_t nets = mir_build_load(g->mu, upref);
      mir_value_t count = mir_const(g->mu, t_offset, ti->size);
      mir_build_drive_signal(g->mu, nets, count);

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

      mir_value_t unpacked = mir_build_unpack(g->mu, resize, ST_STRONG, tmp);
      mir_build_put_driver(g->mu, nets, count, unpacked);

      mir_build_wait(g->mu, start_bb);
   }
}

static void vlog_lower_gate_inst(vlog_gen_t *g, vlog_node_t v)
{
   mir_block_t start_bb = mir_add_block(g->mu);
   assert(start_bb.id == 1);

   vlog_lower_driver(g, vlog_target(v));

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

   mir_build_put_driver(g->mu, nets, lvalue.count, unpacked);
   mir_build_wait(g->mu, start_bb);
}

static void vlog_lower_port_map(vlog_gen_t *g, vlog_node_t v)
{
   vlog_node_t port = vlog_ref(v);
   assert(vlog_kind(port) == V_PORT_DECL);

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

   {
      switch (vlog_subkind(port)) {
      case V_PORT_INPUT:
         {
            vlog_lower_sensitivity(g, vlog_value(v));

            mir_value_t count = mir_const(g->mu, t_offset, ti->size);
            mir_build_drive_signal(g->mu, signal, count);
         }
         break;
      case V_PORT_OUTPUT:
         {
            vlog_lower_driver(g, vlog_value(v));

            mir_value_t count = mir_const(g->mu, t_offset, ti->size);
            mir_build_sched_event(g->mu, signal, count);
         }
         break;
      default:
         should_not_reach_here();
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
         mir_value_t value =
            vlog_lower_with_context(g, vlog_value(v), ti->type);
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

   if (!mir_is_vector(g->mu, resize)) {
      // A real input port is driven with the converted value
      assert(nlvalues == 1);
      mir_value_t nets = mir_build_array_ref(g->mu, lvalue1.obj,
                                             lvalue1.dst_offset);
      mir_build_put_driver(g->mu, nets, lvalue1.count, resize);
      mir_build_wait(g->mu, start_bb);
      return;
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

   mir_build_wait(g->mu, start_bb);
}

static mir_value_t vlog_lower_tcheck_event(vlog_gen_t *g, vlog_node_t v)
{
   assert (vlog_kind(v) == V_TCHECK_EVENT);

   vlog_node_t sig = vlog_param(v, 0);

   if (vlog_kind(sig) == V_EVENT)
      sig = vlog_value(sig);

   mir_type_t t_offset = mir_offset_type(g->mu);
   mir_type_t t_time = mir_time_type(g->mu);
   mir_value_t t_zero = mir_const(g->mu, t_time, 0);

   vlog_select_t lvalue = vlog_lower_select(g, sig);
   int total_size = lvalue.size * vlog_size(vlog_ref(sig));
   mir_value_t count = mir_const(g->mu, t_offset, total_size);
   mir_value_t last_event = mir_build_last_event(g->mu, lvalue.obj, count);
   mir_value_t cond = mir_build_cmp(g->mu, MIR_CMP_EQ, last_event, t_zero);

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

         mir_value_t limit_val = vlog_lower_rvalue(g, v_limit);
         mir_value_t limit_time = mir_build_cast(g->mu, t_time, limit_val);
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
      vlog_lower_assign_process(&g, v);
      break;
   case V_NET_DECL:
      vlog_lower_net_process(&g, v);
      break;
   case V_GATE_INST:
      vlog_lower_gate_inst(&g, v);
      break;
   case V_PORT_MAP:
      vlog_lower_port_map(&g, v);
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

static void vlog_lower_net_decl(vlog_gen_t *g, vlog_node_t v, tree_t wrap,
                                mir_value_t resfn)
{
   mir_type_t t_net_value = mir_int_type(g->mu, 0, 255);
   mir_type_t t_net_signal = mir_signal_type(g->mu, t_net_value);
   mir_type_t t_offset = mir_offset_type(g->mu);

   assert(wrap != NULL);

   const type_info_t *ti = vlog_type_info(g, vlog_type(v));
   const int total_size = ti->size * vlog_size(v);

   mir_value_t value = mir_const(g->mu, t_net_value, LOGIC_Z);
   mir_value_t count = mir_const(g->mu, t_offset, total_size);
   mir_value_t size = mir_const(g->mu, t_offset, 1);
   mir_value_t flags = mir_const(g->mu, t_offset, NET_F_R_IDENT);
   mir_value_t locus = mir_build_debug_locus(g->mu, tree_to_object(wrap));

   mir_value_t signal = mir_build_init_signal(g->mu, t_net_value, count, size,
                                              value, flags, locus,
                                              MIR_NULL_VALUE);

   mir_build_resolve_signal(g->mu, signal, resfn);

   mir_value_t var = mir_add_var(g->mu, t_net_signal, MIR_NULL_STAMP,
                                 vlog_ident(v), MIR_VAR_SIGNAL);
   mir_build_store(g->mu, var, signal);

   mir_put_object(g->mu, v, var);

   if (vlog_has_value(v)) {
      ident_t parent = mir_get_name(g->mu, MIR_NULL_VALUE);
      ident_t sym = ident_sprintf("%s.%s#assign", istr(parent),
                                  istr(vlog_ident(v)));
      mir_defer(g->mir, sym, parent, MIR_UNIT_PROCESS,
                vlog_lower_deferred, vlog_to_object(v));

      mir_value_t self = mir_build_context_upref(g->mu, 0);
      mir_value_t args[] = { self };
      mir_value_t closure = mir_build_closure(g->mu, sym, t_offset, args, 1);
      mir_value_t locus = mir_build_debug_locus(g->mu, tree_to_object(wrap));

      mir_build_process_init(g->mu, closure, locus);
   }
}

static void vlog_lower_var_decl(vlog_gen_t *g, vlog_node_t v, tree_t wrap)
{
   const type_info_t *ti = vlog_type_info(g, vlog_type(v));

   if (mir_get_class(g->mu, ti->type) == MIR_TYPE_CONTEXT) {
      // Class variable
      mir_value_t null = mir_build_null(g->mu, ti->type);
      mir_value_t var = mir_add_var(g->mu, ti->type, MIR_NULL_STAMP,
                                    vlog_ident(v), 0);
      mir_build_store(g->mu, var, null);

      mir_put_object(g->mu, v, var);
      return;
   }
   else if (vlog_is_unsized(v)) {
      mir_type_t t_uarray = mir_uarray_type(g->mu, vlog_ranges(v), ti->type);
      mir_value_t var = mir_add_var(g->mu, t_uarray, MIR_NULL_STAMP,
                                    vlog_ident(v), 0);

      mir_type_t t_pointer = mir_pointer_type(g->mu, ti->type);
      mir_value_t null = mir_build_null(g->mu, t_pointer);

      mir_type_t t_offset = mir_offset_type(g->mu);
      mir_type_t t_bool = mir_bool_type(g->mu);

      mir_value_t left = mir_const(g->mu, t_offset, 0);
      mir_value_t right = mir_const(g->mu, t_offset, -1);
      mir_value_t dir = mir_const(g->mu, t_bool, RANGE_TO);

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
   mir_value_t flags = mir_const(g->mu, t_offset, 0);
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
   vlog_node_t d = vlog_ref(v);
   if (vlog_kind(d) == V_NET_DECL) {
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
      case V_LOCAL_DECL:
      case V_VAR_DECL:
         {
            const type_info_t *ti = vlog_type_info(g, vlog_type(d));

            const int nelems = vlog_size(d);
            mir_type_t type = ti->type;
            if (nelems > 1)
               type = mir_carray_type(g->mu, nelems, ti->type);

            mir_value_t var = mir_add_var(g->mu, type, ti->stamp,
                                          vlog_ident(d), 0);
            mir_put_object(g->mu, d, var);

            if (vlog_has_value(d)) {
               assert(nelems == 1);  // TODO: unpacked array initialisation
               mir_value_t value = vlog_lower_rvalue(g, vlog_value(d));
               mir_value_t init = vlog_lower_cast(g, ti->type, value);
               mir_build_store(g->mu, var, init);
            }
            else {
               mir_value_t init = vlog_lower_default_value(g, ti);

               if (nelems > 1) {
                  mir_type_t t_offset = mir_offset_type(g->mu);
                  mir_value_t count = mir_const(g->mu, t_offset, nelems);
                  mir_build_set(g->mu, var, init, count);
               }
               else
                  mir_build_store(g->mu, var, init);
            }
         }
         break;
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
   assert(vlog_kind(v) == V_FUNC_DECL);

   vlog_gen_t g = {
      .mu  = mu,
      .mir = mir_get_context(mu),
   };

   const type_info_t *ti = vlog_type_info(&g, vlog_type(v));
   mir_set_result(mu, ti->type);

   ident_t parent = mir_get_parent(mu);
   if (parent == NULL)
      g.constfunc = true;
   else {
      mir_type_t t_context = mir_context_type(mu, mir_get_parent(mu));
      mir_add_param(mu, t_context, MIR_NULL_STAMP, ident_new("context"));
   }

   const int nports = vlog_ports(v);
   for (int i = 0; i < nports; i++) {
      vlog_node_t port = vlog_port(v, i);
      const type_info_t *pti = vlog_type_info(&g, vlog_type(port));
      ident_t name = vlog_ident(port);

      mir_value_t param = mir_add_param(mu, pti->type, pti->stamp,
                                        vlog_ident(port));

      mir_value_t local = mir_add_var(mu, pti->type, pti->stamp, name, 0);
      mir_build_store(mu, local, param);

      mir_put_object(mu, port, local);
   }

   mir_value_t result = mir_add_var(mu, ti->type, ti->stamp, vlog_ident(v), 0);
   mir_put_object(mu, v, result);

   mir_build_store(mu, result, vlog_lower_default_value(&g, ti));

   gen_stack_t gs = {};

   vlog_lower_locals(&g, v);
   vlog_lower_stmts(&g, v, &gs);

   if (!mir_block_finished(mu, MIR_NULL_BLOCK))
      mir_build_return(mu, mir_build_load(mu, result));

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
   vlog_lower_stmts(&g, v, &gs);

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

static void vlog_lower_class_decl(mir_unit_t *mu, object_t *obj)
{
   vlog_node_t v = vlog_from_object(obj);
   assert(vlog_kind(v) == V_CLASS_DECL);

   vlog_gen_t g = {
      .mu  = mu,
      .mir = mir_get_context(mu),
   };

   mir_type_t t_context = mir_context_type(mu, mir_get_parent(mu));
   mir_add_param(mu, t_context, MIR_NULL_STAMP, ident_new("context"));

   vlog_lower_locals(&g, v);

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

static void vlog_lower_converter(mir_unit_t *mu, tree_t cf)
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

      mir_build_drive_signal(mu, out_nets, out_count);

      mir_value_t in_nets = in, in_count = mir_const(mu, t_offset, 1);
      if (mir_is(mu, in, MIR_TYPE_UARRAY)) {
         in_nets = mir_build_unwrap(mu, in);
         in_count = mir_build_uarray_len(mu, in, 0);
      }

      mir_build_sched_event(mu, in_nets, in_count);

      mir_build_return(mu, MIR_NULL_VALUE);
   }

   mir_set_cursor(mu, start_bb, MIR_APPEND);

   tree_t decl = tree_ref(cf);
   assert(tree_kind(decl) == T_FUNC_DECL);
   assert(type_eq(type_result(tree_type(decl)), tree_type(cf)));

   ident_t func = tree_ident2(decl);

   mir_value_t pkg = mir_build_link_package(mu, well_known(W_NVC_VERILOG));

   mir_value_t count = MIR_NULL_VALUE;
   if (mir_is(mu, in, MIR_TYPE_UARRAY)) {
      count = mir_build_uarray_len(mu, in, 0);
      in = mir_build_unwrap(mu, in);
   }

   mir_value_t resolved = mir_build_resolved(mu, in);

   mir_value_t arg;
   if (!mir_is_null(count)) {
      mir_dim_t dims[] = {
         { .left  = mir_const(mu, t_offset, 1),
           .right = count,
           .dir   = mir_const(mu, mir_bool_type(mu), RANGE_TO),
         }
      };
      arg = mir_build_wrap(mu, resolved, dims, 1);
   }
   else {
      arg = mir_build_load(mu, resolved);
      count = mir_const(mu, t_offset, 1);
   }

   mir_value_t args[] = { pkg, arg };

   mir_value_t result = mir_build_fcall(mu, func, t_dst, MIR_NULL_STAMP,
                                        args, ARRAY_LEN(args));

   if (mir_is(mu, result, MIR_TYPE_UARRAY))
      result = mir_build_unwrap(mu, result);

   if (mir_is(mu, out, MIR_TYPE_UARRAY))
      out = mir_build_unwrap(mu, out);

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

   vlog_lower_converter(mu, cf);
}

static void vlog_lower_convert_out(mir_unit_t *mu, object_t *obj)
{
   tree_t map = tree_from_object(obj);
   assert(tree_kind(map) == T_PARAM);

   tree_t cf = tree_name(map);
   assert(tree_kind(cf) == T_CONV_FUNC);

   vlog_lower_converter(mu, cf);
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

      vlog_lower_port_decl(&g, d);
   }

   for (int i = 0, hops; i < vlog_ndecls; i++) {
      vlog_node_t d = vlog_decl(body, i);

      switch (vlog_kind(d)) {
      case V_PORT_DECL:
         break;   // Translated above
      case V_NET_DECL:
         if (mir_is_null(mir_search_object(mu, d, &hops)))
            vlog_lower_net_decl(&g, d, hash_get(map, d), resfn);
         else
            assert(hops == 0); // Port
         break;
      case V_VAR_DECL:
         if (mir_is_null(mir_search_object(mu, d, &hops)))
            vlog_lower_var_decl(&g, d, hash_get(map, d));
         else
            assert(hops == 0); // Port
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
         mir_defer(mc, ident_prefix(vlog_ident2(d), vlog_ident(d), '.'), qual,
                   MIR_UNIT_PROTECTED, vlog_lower_class_decl,
                   vlog_to_object(d));
         break;
      case V_TYPE_DECL:
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
      if (vlog_kind(tree_vlog(t)) == V_PORT_MAP)
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

      vlog_node_t value = vlog_value(v);
      switch (vlog_kind(value)) {
      case V_REF:
      case V_BIT_SELECT:
      case V_PART_SELECT:
         {
            vlog_select_t lvalue = vlog_lower_select(&g, vlog_value(v));
            const type_info_t *ti = vlog_type_info(&g, vlog_type(port));
            if (!mir_is_signal(mu, lvalue.obj))
               break;    // Port tied to constant
            else if (lvalue.size != ti->size)
               break;

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
         vlog_lower_net_decl(&g, d, hash_get(map, d), resfn);
         break;
      case V_VAR_DECL:
         vlog_lower_var_decl(&g, d, hash_get(map, d));
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

      int hops;
      mir_value_t var = mir_search_object(mu, port, &hops);
      if (mir_is_null(var)) {
         switch (vlog_kind(d)) {
         case V_NET_DECL:
            vlog_lower_net_decl(&g, d, hash_get(map, d), resfn);
            break;
         case V_VAR_DECL:
            vlog_lower_var_decl(&g, d, hash_get(map, d));
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

   for (int i = 0; i < vlog_nports; i++) {
      vlog_node_t ref = vlog_port(body, i);
      assert(vlog_kind(ref) == V_REF);

      vlog_node_t port = vlog_ref(ref);
      assert(vlog_kind(port) == V_PORT_DECL);

      int hops;
      mir_value_t nets = mir_search_object(mu, port, &hops);
      if (mir_is_null(nets)) {
         mir_dump(mu);
         fatal_trace("missing variable for port %pi", vlog_ident(port));
      }
      assert(hops == 0);

      args[i + 1] = nets;
   }

   mir_value_t inst = mir_build_instance_init(mu, vlog_ident(body),
                                              args, vlog_nports + 1);

   mir_set_result(mu, mir_get_type(mu, inst));

   mir_type_t t_bool = mir_bool_type(mu);

   for (int i = 0; i < vlog_nports; i++) {
      vlog_node_t ref = vlog_port(body, i);
      assert(vlog_kind(ref) == V_REF);

      vlog_node_t port = vlog_ref(ref);
      assert(vlog_kind(port) == V_PORT_DECL);

      if (i >= vhdl_nports)
         continue;   // Port conversion only for mixed instantiation

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
