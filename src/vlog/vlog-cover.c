//
//  Copyright (C) 2026  Nick Gasson
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
#include "cov/cov-api.h"
#include "diag.h"
#include "hash.h"
#include "ident.h"
#include "vlog/vlog-node.h"
#include "vlog/vlog-phase.h"
#include "vlog/vlog-util.h"

#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>

// Code coverage for the statements and branches of a Verilog instance.  The
// walk below runs in two modes over the same code: create makes the scopes
// and items of an instance in the coverage database when it is elaborated,
// and bind finds them again when the body is lowered, remembering the
// counter tag of each syntax node so that the lowering can ask for it.  The
// tags are relative to the instance so one body serves all its instances.

typedef struct _lazy_cscope lazy_cscope_t;

typedef struct _lazy_cscope {
   lazy_cscope_t *parent;
   cover_obj_t    cscope;
   vlog_node_t    node;
   ident_t        name;
   bool           resolved;
   unsigned       nstmt;
   unsigned       nexpr;
   unsigned       nitem[NUM_COVER_KINDS];
} lazy_cscope_t;

typedef struct {
   cover_data_t *db;
   cover_obj_t   inst;
   bool          bind;
} vlog_cover_t;

typedef struct {
   uint32_t tag;
   uint32_t count;
} toggle_info_t;

static hash_t *stmt_tags = NULL;
static hash_t *branch_tags = NULL;
static hash_t *toggle_tags = NULL;

static void vlog_cover_stmt_list(vlog_cover_t *g, vlog_node_t v,
                                 lazy_cscope_t *parent);

static lazy_cscope_t lazy_cover_scope(vlog_node_t v, lazy_cscope_t *parent,
                                      ident_t name)
{
   lazy_cscope_t lcs = {
      .parent = parent,
      .cscope = COVER_NULL_OBJ,
      .node   = v,
      .name   = name,
   };
   return lcs;
}

static cover_obj_t get_cover_scope(vlog_cover_t *g, lazy_cscope_t *lcs)
{
   if (lcs->resolved)
      return lcs->cscope;

   cover_obj_t parent = get_cover_scope(g, lcs->parent);
   lcs->resolved = true;

   if (cover_is_null(parent))
      return lcs->cscope = COVER_NULL_OBJ;
   else if (g->bind)
      return lcs->cscope = cover_get_child(g->db, parent, lcs->name);

   // A condition or item without a location is where the scope above it is
   loc_t loc = *vlog_loc(lcs->node);
   if (loc.file_ref == FILE_INVALID)
      loc = cover_get_loc(g->db, parent, COV_ATTR_LOC);

   return lcs->cscope = cover_scope_new(g->db, g->inst, parent, CSCOPE_NONE,
                                        lcs->name, loc);
}

static void vlog_cover_remember(hash_t **map, vlog_node_t v, uint32_t tag)
{
   if (*map == NULL)
      *map = hash_new(256);

#ifdef DEBUG
   void *old = hash_get(*map, v);
   if (old != NULL && (uintptr_t)old != (uintptr_t)tag + 1)
      fatal_trace("inconsistent coverage tags for %s at %s:%d",
                  vlog_kind_str(vlog_kind(v)), loc_file_str(vlog_loc(v)),
                  vlog_loc(v)->first_line);
#endif

   hash_put(*map, v, (void *)((uintptr_t)tag + 1));
}

static cover_obj_t vlog_cover_item(vlog_cover_t *g, lazy_cscope_t *lcs,
                                   cover_item_kind_t kind, const loc_t *loc,
                                   int nbins)
{
   cover_obj_t cs = get_cover_scope(g, lcs);
   if (cover_is_null(cs))
      return COVER_NULL_OBJ;

   cover_obj_t item;
   if (g->bind)
      item = cover_get_item(g->db, cs, kind, lcs->nitem[kind]);
   else
      item = cover_item_new(g->db, cs, kind, *loc, nbins);

   if (!cover_is_null(item))
      lcs->nitem[kind]++;

   return item;
}

static void vlog_cover_flag_bins(vlog_cover_t *g, cover_obj_t item,
                                 const cover_flags_t *flags, int nbins,
                                 int expr)
{
   cover_obj_t bins[2];
   assert(nbins <= ARRAY_LEN(bins));
   cover_rel(g->db, item, COV_REL_BINS, 0, bins, nbins);

   for (int i = 0; i < nbins; i++) {
      cover_put_flags(g->db, bins[i], flags[i]);

      ident_t suffix = ident_new(cover_bmask_to_bin_str(flags[i]));
      ident_t prefix = cover_get_ident(g->db, bins[i], COV_ATTR_HIER);

      // A conditional operator is named after its place in the statement
      // without a scope of its own as for expressions in VHDL
      if (expr >= 0)
         prefix = ident_sprintf("%s._E%d", istr(prefix), expr);

      cover_put_ident(g->db, bins[i], COV_ATTR_HIER,
                      ident_prefix(prefix, suffix, '.'));
   }
}

static bool vlog_cover_is_property(vlog_node_t p)
{
   // An always process made for a concurrent assertion

   if (vlog_kind(p) != V_ALWAYS || vlog_stmts(p) != 1)
      return false;

   vlog_node_t s0 = vlog_stmt(p, 0);
   if (vlog_kind(s0) != V_TIMING || vlog_stmts(s0) < 1)
      return false;

   vlog_node_t s1 = vlog_stmt(s0, 0);
   switch (vlog_kind(s1)) {
   case V_ASSERT:
   case V_ASSUME:
   case V_COVER:
      return vlog_has_value(s1) && vlog_kind(vlog_value(s1)) == V_PROP_SPEC;
   default:
      return false;
   }
}

static bool vlog_cover_is_made_up(vlog_node_t v)
{
   // A reference to a variable made by the parser for the procedural
   // continuous assignments and forces, which has a name that cannot be
   // written in the source

   if (vlog_kind(v) != V_REF || !vlog_has_ident(v))
      return false;

   return istr(vlog_ident(v))[0] == '$';
}

static bool vlog_cover_is_pca_if(vlog_node_t s)
{
   // The check made for a release that the other kind of assignment is not
   // active: an if with one condition that tests such a variable

   if (vlog_kind(s) != V_IF || vlog_conds(s) != 1)
      return false;

   vlog_node_t c = vlog_cond(s, 0);
   if (!vlog_has_value(c))
      return false;

   vlog_node_t test = vlog_value(c);
   return vlog_kind(test) == V_BINARY && vlog_cover_is_made_up(vlog_left(test));
}

bool vlog_cover_wants_stmt(vlog_node_t s)
{
   switch (vlog_kind(s)) {
   case V_SEQ_BLOCK:
      return false;
   case V_BASSIGN:
   case V_NBASSIGN:
      // Records which procedural continuous assignment is active
      if (vlog_cover_is_made_up(vlog_target(s)))
         return false;
      break;
   case V_IF:
      if (vlog_cover_is_pca_if(s))
         return false;
      break;
   case V_SYS_TCALL:
   case V_USER_TCALL:
      // Initialisation of a package made by the parser
      if (vlog_has_ident(s)
          && ident_starts_with(vlog_ident(s), ident_new("$__init:")))
         return false;
      break;
   default:
      break;
   }

   return !loc_invalid_p(vlog_loc(s));
}

ident_t vlog_cover_scope_name(vlog_node_t v, int nth)
{
   switch (vlog_kind(v)) {
   case V_FUNC_DECL:
   case V_TASK_DECL:
      return vlog_ident(v);
   case V_SEQ_BLOCK:
   case V_FORK:
      if (vlog_has_ident(v))
         return vlog_ident(v);
      return ident_sprintf("_S%u", nth);
   case V_ALWAYS:
   case V_INITIAL:
   case V_FINAL:
   case V_ASSIGN:
      return ident_sprintf("_P%u", nth);
   case V_NET_DECL:
      return ident_sprintf("_D%s", istr(vlog_ident(v)));
   case V_COND:
      return ident_sprintf("_B%u", nth);
   case V_CASE_ITEM:
      if (vlog_params(v) == 0)
         return ident_new("_B_OTHERS");
      return ident_sprintf("_B%u", nth);
   default:
      return ident_sprintf("_S%u", nth);
   }
}

static void vlog_cover_stmt(vlog_cover_t *g, vlog_node_t s,
                            lazy_cscope_t *lcs)
{
   if (!cover_enabled(g->db, COVER_MASK_STMT) || !vlog_cover_wants_stmt(s))
      return;

   const loc_t *loc = vlog_loc(s);

   cover_obj_t item = vlog_cover_item(g, lcs, COV_ITEM_STMT, loc, 1);
   if (cover_is_null(item))
      return;

   if (g->bind) {
      cover_obj_t bin = cover_at(g->db, item, COV_REL_BINS, 0);
      vlog_cover_remember(&stmt_tags, s, cover_get_tag(g->db, bin));
      return;
   }

   cover_put_loc(g->db, item, COV_ATTR_LOC, *loc);

   cover_src_t src;
   switch (vlog_kind(s)) {
   case V_IF:
      src = COV_SRC_IF_STMT;
      break;
   case V_FOREVER:
   case V_REPEAT:
   case V_WHILE:
   case V_DO_WHILE:
   case V_FOR_LOOP:
   case V_FOREACH:
      src = COV_SRC_LOOP_STMT;
      break;
   case V_TIMING:
      src = COV_SRC_WAIT;
      break;
   case V_BASSIGN:
   case V_OP_ASSIGN:
      src = COV_SRC_VAR_ASSIGN;
      break;
   case V_NBASSIGN:
   case V_ASSIGN:
      src = COV_SRC_SIGNAL_ASSIGN;
      break;
   case V_ASSERT:
   case V_ASSUME:
      src = COV_SRC_ASSERT;
      break;
   default:
      src = COV_SRC_STATEMENT;
      break;
   }

   cover_put_u32(g->db, item, COV_ATTR_SOURCE, src);
}

typedef A(ident_t) bit_names_t;

static bool vlog_cover_has_nettype(vlog_node_t type)
{
   // A net of a user-defined type does not have the representation of
   // an integral type

   for (; type != NULL && vlog_kind(type) == V_TYPE_DECL;
        type = vlog_has_type(type) ? vlog_type(type) : NULL) {
      if (vlog_flags(type) & VLOG_F_NETTYPE)
         return true;
   }

   return false;
}

static bool vlog_cover_bit_names(vlog_node_t type, const char *prefix,
                                 bit_names_t *names);

static bool vlog_cover_bit_range(vlog_node_t type, int nth, const char *prefix,
                                 bit_names_t *names)
{
   // The bits of the packed dimensions of a type from the left

   if (nth == vlog_ranges(type)) {
      if (vlog_has_type(type))
         return vlog_cover_bit_names(vlog_type(type), prefix, names);

      APUSH(*names, ident_new(prefix));
      return true;
   }

   int64_t left, right;
   if (!vlog_bounds(vlog_range(type, nth), &left, &right))
      return false;

   const int step = left >= right ? -1 : 1;

   LOCAL_TEXT_BUF tb = tb_new();
   for (int64_t i = left;; i += step) {
      tb_rewind(tb);
      tb_cat(tb, prefix);
      tb_printf(tb, "[%"PRIi64"]", i);

      if (!vlog_cover_bit_range(type, nth + 1, tb_get(tb), names))
         return false;

      if (i == right)
         break;
   }

   return true;
}

static bool vlog_cover_bit_names(vlog_node_t type, const char *prefix,
                                 bit_names_t *names)
{
   // The names of the bits of a static integral type from the left

   switch (vlog_kind(type)) {
   case V_TYPE_DECL:
   case V_ENUM_DECL:
      return vlog_cover_bit_names(vlog_type(type), prefix, names);
   case V_STRUCT_DECL:
      {
         if (!(vlog_flags(type) & VLOG_F_PACKED))
            return false;

         LOCAL_TEXT_BUF tb = tb_new();
         const int ndecls = vlog_decls(type);
         for (int i = 0; i < ndecls; i++) {
            vlog_node_t d = vlog_decl(type, i);

            tb_rewind(tb);
            tb_cat(tb, prefix);
            tb_append(tb, '.');
            tb_istr(tb, vlog_ident(d));

            if (!vlog_cover_bit_names(vlog_type(d), tb_get(tb), names))
               return false;
         }

         return ndecls > 0;
      }
   case V_UNION_DECL:
      {
         // Every member shares the same bits
         if (!(vlog_flags(type) & VLOG_F_PACKED)
             || (vlog_flags(type) & VLOG_F_TAGGED))
            return false;

         LOCAL_TEXT_BUF tb = tb_new();
         const int width = vlog_size(type);
         for (int i = width - 1; i >= 0; i--) {
            tb_rewind(tb);
            tb_cat(tb, prefix);
            tb_printf(tb, "[%d]", i);
            APUSH(*names, ident_new(tb_get(tb)));
         }

         return width > 0;
      }
   case V_DATA_TYPE:
      switch (vlog_subkind(type)) {
      case DT_LOGIC:
      case DT_IMPLICIT:
      case DT_INTEGER:
      case DT_TIME:
      case DT_BIT:
      case DT_INT:
      case DT_SHORTINT:
      case DT_LONGINT:
      case DT_BYTE:
         return vlog_cover_bit_range(type, 0, prefix, names);
      default:
         return false;   // Real, string, event, class handle and so on
      }
   default:
      return false;
   }
}

bool vlog_cover_toggle(vlog_node_t decl, uint32_t *tag, unsigned *count)
{
   if (toggle_tags == NULL)
      return false;

   const toggle_info_t *ti = hash_get(toggle_tags, decl);
   if (ti == NULL)
      return false;

   *tag = ti->tag;
   *count = ti->count;
   return true;
}

static void vlog_cover_toggle_decl(vlog_cover_t *g, vlog_node_t d,
                                   lazy_cscope_t *root, bool port)
{
   if (!cover_enabled(g->db, COVER_MASK_TOGGLE))
      return;

   const loc_t *loc = vlog_loc(d);
   if (loc_invalid_p(loc) || !vlog_has_type(d))
      return;

   vlog_node_t type = vlog_type(d);
   if (vlog_cover_has_nettype(type))
      return;

   // An array of elements is a memory
   unsigned nelems = 1;
   int64_t mem_left = 0, mem_step = 1;
   if (vlog_ranges(d) > 0) {
      int64_t mem_right;
      if (port || vlog_ranges(d) > 1
          || !cover_enabled(g->db, COVER_MASK_TOGGLE_INCLUDE_MEMS)
          || !vlog_bounds(vlog_range(d, 0), &mem_left, &mem_right))
         return;
      mem_step = mem_left > mem_right ? -1 : 1;
      nelems = vlog_size(d);
   }

   bit_names_t bits = AINIT;
   if (!vlog_cover_bit_names(type, "", &bits) || bits.count == 0
       || nelems == 0) {
      ACLEAR(bits);
      return;
   }

   const unsigned width = bits.count * nelems;
   if (!cover_should_emit_array_toggle(g->db, width)) {
      ACLEAR(bits);
      return;
   }

   lazy_cscope_t lcs = lazy_cover_scope(d, root, vlog_ident(d));

   cover_obj_t item = vlog_cover_item(g, &lcs, COV_ITEM_TOGGLE, loc, width * 2);
   if (cover_is_null(item)) {
      ACLEAR(bits);
      return;
   }

   cover_obj_t first = cover_at(g->db, item, COV_REL_BINS, 0);

   if (g->bind) {
      toggle_info_t *ti = xmalloc(sizeof(toggle_info_t));
      ti->tag = cover_get_tag(g->db, first);
      ti->count = width;

      if (toggle_tags == NULL)
         toggle_tags = hash_new(256);

      hash_put(toggle_tags, d, ti);
      ACLEAR(bits);
      return;
   }

   // The first element of the signal is the left most bit
   const int nbits = bits.count;

   cover_obj_t parent = get_cover_scope(g, root);
   ident_t parent_hier = cover_get_ident(g->db, parent, COV_ATTR_HIER);
   cover_put_i64(g->db, item, COV_ATTR_METADATA, ident_len(parent_hier) + 1);

   cover_obj_t *bins LOCAL = xmalloc_array(width * 2, sizeof(cover_obj_t));
   cover_rel(g->db, item, COV_REL_BINS, 0, bins, width * 2);

   const cover_flags_t flags =
      port ? COV_FLAG_TOGGLE_PORT : COV_FLAG_TOGGLE_SIGNAL;
   const char *binstr[2] = {
      cover_bmask_to_bin_str(COV_FLAG_TOGGLE_TO_1),
      cover_bmask_to_bin_str(COV_FLAG_TOGGLE_TO_0),
   };

   LOCAL_TEXT_BUF tb = tb_new();
   for (unsigned i = 0; i < width; i++) {
      tb_rewind(tb);
      if (nelems > 1)
         tb_printf(tb, "[%"PRIi64"]", mem_left + mem_step * (i / nbits));
      tb_istr(tb, bits.items[i % nbits]);

      cover_obj_t *pair = bins + i * 2;

      cover_put_flags(g->db, pair[0], COV_FLAG_TOGGLE_TO_1 | flags);
      cover_put_flags(g->db, pair[1], COV_FLAG_TOGGLE_TO_0 | flags);

      for (int j = 0; j < 2; j++) {
         ident_t hier = cover_get_ident(g->db, pair[j], COV_ATTR_HIER);
         cover_put_ident(g->db, pair[j], COV_ATTR_HIER,
                         ident_sprintf("%s%s.%s", istr(hier), tb_get(tb),
                                       binstr[j]));
      }
   }

   ACLEAR(bits);
}

static void vlog_cover_branch(vlog_cover_t *g, hash_t **map, vlog_node_t key,
                              lazy_cscope_t *lcs, const loc_t *loc,
                              bool choice, cover_src_t src, bool expr)
{
   if (!cover_enabled(g->db, COVER_MASK_BRANCH) || loc_invalid_p(loc))
      return;

   cover_obj_t item = vlog_cover_item(g, lcs, COV_ITEM_BRANCH, loc,
                                      choice ? 1 : 2);
   if (cover_is_null(item))
      return;

   const int nexpr = expr ? lcs->nexpr++ : 0;

   if (g->bind) {
      cover_obj_t bin = cover_at(g->db, item, COV_REL_BINS, 0);
      vlog_cover_remember(map, key, cover_get_tag(g->db, bin));
      return;
   }

   cover_put_u32(g->db, item, COV_ATTR_SOURCE, src);

   static const cover_flags_t choice_flags[] = { COV_FLAG_CHOICE };
   static const cover_flags_t bool_flags[] = { COV_FLAG_TRUE, COV_FLAG_FALSE };

   if (choice)
      vlog_cover_flag_bins(g, item, choice_flags, 1, -1);
   else
      vlog_cover_flag_bins(g, item, bool_flags, 2, expr ? nexpr : -1);
}

typedef A(vlog_node_t) node_array_t;

static void vlog_cover_collect_cb(vlog_node_t v, void *ctx)
{
   node_array_t *list = ctx;
   APUSH(*list, v);
}

static void vlog_cover_nested_cb(vlog_node_t v, void *ctx)
{
   hset_insert(ctx, v);
}

static void vlog_cover_nested(vlog_node_t s, hset_t *set)
{
   // The ternaries in statements nested inside this one

   switch (vlog_kind(s)) {
   case V_IF:
      {
         const int nconds = vlog_conds(s);
         for (int i = 0; i < nconds; i++) {
            vlog_node_t c = vlog_cond(s, i);
            const int nstmts = vlog_stmts(c);
            for (int j = 0; j < nstmts; j++)
               vlog_visit_only(vlog_stmt(c, j), vlog_cover_nested_cb, set,
                               V_COND_EXPR);
         }
      }
      break;
   case V_CASE:
   case V_RANDCASE:
      {
         const int nitems = vlog_stmts(s);
         for (int i = 0; i < nitems; i++) {
            vlog_node_t item = vlog_stmt(s, i);
            const int nstmts = vlog_stmts(item);
            for (int j = 0; j < nstmts; j++)
               vlog_visit_only(vlog_stmt(item, j), vlog_cover_nested_cb, set,
                               V_COND_EXPR);
         }
      }
      break;
   case V_SEQ_BLOCK:
   case V_FORK:
   case V_TIMING:
   case V_FOREVER:
   case V_REPEAT:
   case V_WHILE:
   case V_DO_WHILE:
   case V_FOR_LOOP:
   case V_FOREACH:
      {
         const int nstmts = vlog_stmts(s);
         for (int i = 0; i < nstmts; i++)
            vlog_visit_only(vlog_stmt(s, i), vlog_cover_nested_cb, set,
                            V_COND_EXPR);
      }
      break;
   default:
      break;
   }
}

static void vlog_cover_ternaries(vlog_cover_t *g, vlog_node_t s,
                                 lazy_cscope_t *lcs)
{
   // Each ?: operator evaluated by this statement and not by one inside it

   if (!cover_enabled(g->db, COVER_MASK_BRANCH))
      return;

   switch (vlog_kind(s)) {
   case V_ASSERT:
   case V_ASSUME:
   case V_COVER:
      // A property is not evaluated as a value
      if (vlog_has_value(s) && vlog_kind(vlog_value(s)) == V_PROP_SPEC)
         return;
      break;
   default:
      break;
   }

   node_array_t all = AINIT;
   vlog_visit_only(s, vlog_cover_collect_cb, &all, V_COND_EXPR);

   if (all.count > 0) {
      hset_t *skip = hset_new(16);
      vlog_cover_nested(s, skip);

      // The expression of an event control is not evaluated as a value
      if (vlog_kind(s) == V_TIMING)
         vlog_visit_only(vlog_value(s), vlog_cover_nested_cb, skip,
                         V_COND_EXPR);

      for (int i = 0; i < all.count; i++) {
         vlog_node_t c = all.items[i];
         if (!hset_contains(skip, c))
            vlog_cover_branch(g, &branch_tags, c, lcs,
                              vlog_loc(vlog_value(c)), false,
                              COV_SRC_CONDITION, true);
      }

      hset_free(skip);
   }

   ACLEAR(all);
}

static void vlog_cover_if(vlog_cover_t *g, vlog_node_t s, lazy_cscope_t *lcs)
{
   const int nconds = vlog_conds(s);
   for (int i = 0; i < nconds; i++) {
      vlog_node_t c = vlog_cond(s, i);
      lazy_cscope_t cl =
         lazy_cover_scope(c, lcs, vlog_cover_scope_name(c, i));

      if (vlog_has_value(c))
         vlog_cover_branch(g, &branch_tags, c, &cl,
                           vlog_loc(vlog_value(c)), false,
                           COV_SRC_IF_CONDITION, false);

      vlog_cover_stmt_list(g, c, &cl);
   }
}

static void vlog_cover_case(vlog_cover_t *g, vlog_node_t s,
                            lazy_cscope_t *lcs)
{
   bool has_default = false;

   const int nitems = vlog_stmts(s);
   for (int i = 0; i < nitems; i++) {
      vlog_node_t item = vlog_stmt(s, i);
      assert(vlog_kind(item) == V_CASE_ITEM);

      lazy_cscope_t il =
         lazy_cover_scope(item, lcs, vlog_cover_scope_name(item, i));

      has_default |= vlog_params(item) == 0;

      vlog_cover_branch(g, &branch_tags, item, &il, vlog_loc(item), true,
                        COV_SRC_CASE_CHOICE, false);

      vlog_cover_stmt_list(g, item, &il);
   }

   const vlog_flags_t flags = vlog_flags(s);
   if (!has_default
       && !(flags & (VLOG_F_UNIQUE | VLOG_F_UNIQUE0 | VLOG_F_PRIORITY))) {
      // Nothing matched: there is no item for this so make one
      lazy_cscope_t nl =
         lazy_cover_scope(s, lcs, ident_new("_B_OTHERS"));
      vlog_cover_branch(g, &branch_tags, s, &nl, vlog_loc(s), true,
                        COV_SRC_CASE_CHOICE, false);
   }
}

static void vlog_cover_stmt_one(vlog_cover_t *g, vlog_node_t s,
                                lazy_cscope_t *parent)
{
   const vlog_kind_t kind = vlog_kind(s);

   if (kind == V_SEQ_BLOCK && !vlog_has_ident(s)) {
      // Transparent: the statements belong to the enclosing scope
      vlog_cover_stmt_list(g, s, parent);
      return;
   }
   else if (kind == V_IF && vlog_cover_is_pca_if(s)) {
      // Made up around a release, the statement in it is the user's
      vlog_cover_stmt_list(g, vlog_cond(s, 0), parent);
      return;
   }

   lazy_cscope_t lcs =
      lazy_cover_scope(s, parent, vlog_cover_scope_name(s, parent->nstmt++));

   vlog_cover_stmt(g, s, &lcs);
   vlog_cover_ternaries(g, s, &lcs);

   switch (kind) {
   case V_SEQ_BLOCK:
   case V_FORK:
   case V_TIMING:
   case V_FOREVER:
   case V_REPEAT:
   case V_WHILE:
   case V_DO_WHILE:
   case V_FOR_LOOP:
   case V_FOREACH:
      vlog_cover_stmt_list(g, s, &lcs);
      break;
   case V_IF:
      vlog_cover_if(g, s, &lcs);
      break;
   case V_CASE:
      vlog_cover_case(g, s, &lcs);
      break;
   case V_RANDCASE:
      {
         const int nitems = vlog_stmts(s);
         for (int i = 0; i < nitems; i++)
            vlog_cover_stmt_list(g, vlog_stmt(s, i), &lcs);
      }
      break;
   default:
      break;
   }
}

static void vlog_cover_stmt_list(vlog_cover_t *g, vlog_node_t v,
                                 lazy_cscope_t *parent)
{
   const int nstmts = vlog_stmts(v);
   for (int i = 0; i < nstmts; i++)
      vlog_cover_stmt_one(g, vlog_stmt(v, i), parent);
}

static void vlog_cover_process(vlog_cover_t *g, vlog_node_t p, int nth,
                               lazy_cscope_t *root)
{
   if (vlog_kind(p) == V_ALWAYS && vlog_cover_is_property(p))
      return;

   // Processes made to sample values for assertions have a name that
   // cannot be written in the source
   if (vlog_has_ident(p) && istr(vlog_ident(p))[0] == '$')
      return;

   lazy_cscope_t pl = lazy_cover_scope(p, root, vlog_cover_scope_name(p, nth));

   if (vlog_kind(p) == V_ASSIGN) {
      vlog_cover_stmt(g, p, &pl);
      vlog_cover_ternaries(g, p, &pl);
      return;
   }

   if (vlog_kind(p) == V_ALWAYS
       && (vlog_subkind(p) == V_ALWAYS_COMB_DONE
           || vlog_subkind(p) == V_ALWAYS_LATCH_DONE)) {
      // The event control at the end of the block was made up
      vlog_node_t b = vlog_stmt(p, 0);
      assert(vlog_kind(b) == V_SEQ_BLOCK);

      const int nstmts = vlog_stmts(b);
      for (int i = 0; i < nstmts - 1; i++)
         vlog_cover_stmt_one(g, vlog_stmt(b, i), &pl);
      return;
   }

   vlog_cover_stmt_list(g, p, &pl);
}

static void vlog_cover_walk(vlog_node_t body, cover_data_t *db,
                            cover_obj_t cs, bool bind)
{
   assert(vlog_kind(body) == V_INST_BODY);

   if (db == NULL || cover_is_null(cs))
      return;

   if (!cover_enabled(db, COVER_MASK_STMT | COVER_MASK_BRANCH
                      | COVER_MASK_TOGGLE))
      return;

   vlog_cover_t g = {
      .db   = db,
      .inst = cover_get_obj(db, cs, COV_ATTR_INST),
      .bind = bind,
   };

   lazy_cscope_t root = lazy_cover_scope(body, NULL, NULL);
   root.cscope = cs;
   root.resolved = true;

   const int ndecls = vlog_decls(body);

   // The declaration of the net or variable of a port is the port
   hset_t *port_decls = hset_new(16);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(body, i);
      if (vlog_kind(d) == V_PORT_DECL && vlog_has_ref(d))
         hset_insert(port_decls, vlog_ref(d));
   }

   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(body, i);
      switch (vlog_kind(d)) {
      case V_PORT_DECL:
         vlog_cover_toggle_decl(&g, d, &root, true);
         break;
      case V_VAR_DECL:
         if (!hset_contains(port_decls, d))
            vlog_cover_toggle_decl(&g, d, &root, false);
         break;
      case V_FUNC_DECL:
      case V_TASK_DECL:
         {
            lazy_cscope_t fl =
               lazy_cover_scope(d, &root, vlog_cover_scope_name(d, i));
            vlog_cover_stmt_list(&g, d, &fl);
         }
         break;
      case V_NET_DECL:
         if (!hset_contains(port_decls, d))
            vlog_cover_toggle_decl(&g, d, &root, false);
         if (vlog_has_value(d)) {
            lazy_cscope_t dl =
               lazy_cover_scope(d, &root, vlog_cover_scope_name(d, i));
            vlog_cover_stmt(&g, d, &dl);
            vlog_cover_ternaries(&g, d, &dl);
         }
         break;
      default:
         break;
      }
   }

   hset_free(port_decls);

   if (!cover_enabled(db, COVER_MASK_STMT | COVER_MASK_BRANCH))
      return;

   const int nstmts = vlog_stmts(body);
   for (int i = 0; i < nstmts; i++) {
      vlog_node_t s = vlog_stmt(body, i);
      switch (vlog_kind(s)) {
      case V_ALWAYS:
      case V_INITIAL:
      case V_FINAL:
      case V_ASSIGN:
         vlog_cover_process(&g, s, i, &root);
         break;
      default:
         break;
      }
   }
}

void vlog_cover_block(vlog_node_t body, cover_data_t *db, cover_obj_t cs)
{
   vlog_cover_walk(body, db, cs, false);
}

void vlog_cover_bind(vlog_node_t body, cover_data_t *db, cover_obj_t cs)
{
   vlog_cover_walk(body, db, cs, true);
}

static bool vlog_cover_lookup(hash_t *map, vlog_node_t v, uint32_t *tag)
{
   if (map == NULL)
      return false;

   void *ptr = hash_get(map, v);
   if (ptr == NULL)
      return false;

   *tag = (uintptr_t)ptr - 1;
   return true;
}

bool vlog_cover_tag(vlog_node_t v, cover_item_kind_t kind, uint32_t *tag)
{
   switch (kind) {
   case COV_ITEM_STMT:
      return vlog_cover_lookup(stmt_tags, v, tag);
   case COV_ITEM_BRANCH:
      return vlog_cover_lookup(branch_tags, v, tag);
   default:
      return false;
   }
}
