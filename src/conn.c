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

#include "util.h"
#include "array.h"
#include "common.h"
#include "conn.h"
#include "conn-priv.h"
#include "diag.h"
#include "driver.h"
#include "hash.h"
#include "ident.h"
#include "phase.h"
#include "tree.h"
#include "type.h"
#include "vlog/vlog-node.h"
#include "vlog/vlog-number.h"
#include "vlog/vlog-phase.h"
#include "vlog/vlog-util.h"

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

typedef A(conn_seg_t) seg_list_t;
typedef A(conn_pconn_t) pconn_list_t;
typedef A(conn_param_t) param_list_t;
typedef A(conn_net_t *) net_list_t;
typedef A(conn_inst_t *) inst_list_t;
typedef A(conn_proc_t *) proc_list_t;

typedef struct _chunk chunk_t;

struct _chunk {
   chunk_t *next;
   size_t   used;
   size_t   size;
   char     data[];
};

// The port and generic declarations of a VHDL entity are shared by every
// instance of it so the mapping from declarations to nets and values has
// to be kept separately for each instance
typedef struct {
   tree_t  value;
   bool    has_int;
   int64_t ival;
} gen_value_t;

typedef struct {
   hash_t *net;
   hash_t *gen;
} inst_maps_t;

struct proc_ref {
   conn_proc_t *proc;
   tree_t       tree;      // T_PROCESS or T_VERILOG
};

struct _conn_design {
   bool           evidence;    // Compute evidence and attributes
   conn_block_t **roots;
   unsigned       nroots;
   conn_block_t **blocks;      // All blocks in creation order
   unsigned       nblocks;
   unsigned       maxblocks;
   unsigned       nnets;       // Nets created so far, for conn_net_t.id
   hash_t        *netmap;      // Declaration -> conn_net_t (current instance)
   hash_t        *genval;      // Generic declaration -> gen_value_t (ditto)
   hash_t        *genvars;     // Genvar of a shared generate body -> value
   shash_t       *names;       // Net names of the current instance
   hash_t        *gmap;        // Declaration -> conn_net_t (all instances)
   hash_t        *genmaps;     // Generate block -> its netmap (see enter_gen)
   hash_t        *netparents;  // Map of a generate block -> enclosing map
   hash_t        *blkmaps;     // conn_block_t -> inst_maps_t
   hash_t        *tree_blocks; // T_BLOCK -> conn_block_t
   hash_t        *specs;       // VHDL declaration -> attribute specifications
   conn_proc_t  **xprocs;      // Processes touching nets of other blocks
   unsigned       nxprocs;
   unsigned       maxxprocs;
   chunk_t       *chunks;
   char         **strings;     // Strings returned from describe functions
   unsigned       nstrings;
   unsigned       maxstrings;
};

#define CHUNK_SIZE 65536

void *dalloc(conn_design_t *cd, size_t size)
{
   size = (size + 15) & ~(size_t)15;

   if (cd->chunks == NULL || cd->chunks->used + size > cd->chunks->size) {
      const size_t csize = MAX(size, CHUNK_SIZE);
      chunk_t *c = xmalloc(sizeof(chunk_t) + csize);
      c->next = cd->chunks;
      c->used = 0;
      c->size = csize;
      cd->chunks = c;
   }

   void *p = cd->chunks->data + cd->chunks->used;
   cd->chunks->used += size;
   memset(p, 0, size);
   return p;
}

char *dstr(conn_design_t *cd, const char *s)
{
   const size_t len = strlen(s);
   char *p = dalloc(cd, len + 1);
   memcpy(p, s, len + 1);
   return p;
}

static char *dstrf(conn_design_t *cd, const char *fmt, ...)
   __attribute__((format(printf, 2, 3)));

static char *dstrf(conn_design_t *cd, const char *fmt, ...)
{
   va_list ap;
   va_start(ap, fmt);
   char *tmp = xvasprintf(fmt, ap);
   va_end(ap);

   char *p = dstr(cd, tmp);
   free(tmp);
   return p;
}

hash_t *conn_specs(conn_design_t *cd)
{
   if (cd->specs == NULL)
      cd->specs = hash_new(64);
   return cd->specs;
}

void conn_add_attr(conn_design_t *cd, conn_attr_t **list, unsigned *count,
                   const char *name, const char *value)
{
   for (unsigned i = 0; i < *count; i++) {
      if (strcmp((*list)[i].name, name) == 0) {
         (*list)[i].value = dstr(cd, value);
         return;
      }
   }

   conn_attr_t *n = dalloc(cd, (*count + 1) * sizeof(conn_attr_t));
   if (*count > 0)
      memcpy(n, *list, *count * sizeof(conn_attr_t));
   n[*count].name = dstr(cd, name);
   n[*count].value = dstr(cd, value);
   *list = n;
   (*count)++;
}

static void add_block_param(conn_design_t *cd, param_list_t *l,
                            const char *name, const char *value,
                            const char *type, const char *kind,
                            bool overridden, bool exact)
{
   while (type != NULL && *type == ' ')
      type++;

   conn_param_t p = {
      .name       = dstr(cd, name),
      .value      = dstr(cd, value),
      .type       = type != NULL && *type != '\0' ? dstr(cd, type) : NULL,
      .kind       = kind,
      .overridden = overridden,
      .exact      = exact,
   };
   APUSH(*l, p);
}

static bool param_is_int(const char *s)
{
   if (*s == '-')
      s++;
   if (*s == '\0')
      return false;
   for (; *s; s++) {
      if (!isdigit((unsigned char)*s))
         return false;
   }
   return true;
}

static bool param_is_quoted(const char *s)
{
   const size_t len = strlen(s);
   return len >= 2 && s[0] == '"' && s[len - 1] == '"';
}

////////////////////////////////////////////////////////////////////////////////
// Helpers

static bool name_eq(const char *a, const char *b)
{
   return strcasecmp(a, b) == 0;
}

static const char *loc_file(conn_design_t *cd, const loc_t *loc)
{
   if (loc == NULL || loc_invalid_p(loc))
      return NULL;

   const char *f = loc_file_str(loc);
   return f ? dstr(cd, f) : NULL;
}

static unsigned loc_line(const loc_t *loc)
{
   return (loc == NULL || loc_invalid_p(loc)) ? 0 : loc->first_line;
}

static void print_vhdl(text_buf_t *tb, tree_t t, int depth)
{
   // Compact source text of an expression.  This does not use the dumper
   // as that cannot print every kind of tree that can appear in a port map.
   if (depth > 6) {
      tb_cat(tb, "...");
      return;
   }

   switch (tree_kind(t)) {
   case T_REF:
      tb_cat(tb, istr(tree_ident(t)));
      break;
   case T_LITERAL:
      {
         int64_t i;
         if (folded_int(t, &i))
            tb_printf(tb, "%"PRIi64, i);
         else
            tb_cat(tb, "<literal>");
      }
      break;
   case T_STRING:
      {
         tb_append(tb, '"');
         const int n = tree_chars(t);
         for (int i = 0; i < n; i++) {
            tree_t c = tree_char(t, i);
            const char *id = tree_kind(c) == T_REF ? istr(tree_ident(c)) : "'?'";
            tb_append(tb, strlen(id) == 3 ? id[1] : '?');
         }
         tb_append(tb, '"');
      }
      break;
   case T_FCALL:
      {
         const char *name = istr(tree_ident(t));
         const int n = tree_params(t);
         if (n == 2 && name[0] == '"') {
            print_vhdl(tb, tree_value(tree_param(t, 0)), depth + 1);
            tb_printf(tb, " %.*s ", (int)strlen(name) - 2, name + 1);
            print_vhdl(tb, tree_value(tree_param(t, 1)), depth + 1);
         }
         else {
            if (name[0] == '"')
               tb_printf(tb, "%.*s ", (int)strlen(name) - 2, name + 1);
            else
               tb_printf(tb, "%s(", name);
            for (int i = 0; i < n; i++) {
               if (i > 0)
                  tb_cat(tb, ", ");
               print_vhdl(tb, tree_value(tree_param(t, i)), depth + 1);
            }
            if (name[0] != '"')
               tb_append(tb, ')');
         }
      }
      break;
   case T_ARRAY_REF:
   case T_ARRAY_SLICE:
   case T_RECORD_REF:
      print_vhdl(tb, tree_value(t), depth + 1);
      tb_cat(tb, tree_kind(t) == T_RECORD_REF ? ".field" : "(...)");
      break;
   case T_QUALIFIED:
   case T_TYPE_CONV:
   case T_INERTIAL:
      print_vhdl(tb, tree_value(t), depth + 1);
      break;
   case T_CONV_FUNC:
      tb_printf(tb, "%s(", istr(tree_ident(t)));
      print_vhdl(tb, tree_value(t), depth + 1);
      tb_append(tb, ')');
      break;
   case T_AGGREGATE:
      tb_cat(tb, "(...)");
      break;
   default:
      tb_cat(tb, "<expression>");
      break;
   }
}

static char *expr_text_vhdl(conn_design_t *cd, tree_t t)
{
   LOCAL_TEXT_BUF tb = tb_new();
   print_vhdl(tb, t, 0);
   return dstr(cd, tb_get(tb));
}

static void print_vlog(text_buf_t *tb, vlog_node_t v, int depth)
{
   if (depth > 6) {
      tb_cat(tb, "...");
      return;
   }

   switch (vlog_kind(v)) {
   case V_REF:
   case V_HIER_REF:
      tb_cat(tb, istr(vlog_ident(v)));
      break;
   case V_NUMBER:
      number_print(vlog_number(v), tb);
      break;
   case V_BIT_SELECT:
   case V_PART_SELECT:
   case V_MEMBER_REF:
      print_vlog(tb, vlog_value(v), depth + 1);
      tb_cat(tb, vlog_kind(v) == V_MEMBER_REF ? ".member" : "[...]");
      break;
   case V_UNARY:
      {
         static const char *const ops[] = {
            [V_UNARY_BITNEG] = "~", [V_UNARY_NOT] = "!", [V_UNARY_NEG] = "-",
            [V_UNARY_IDENTITY] = "+", [V_UNARY_AND] = "&", [V_UNARY_OR] = "|",
            [V_UNARY_XOR] = "^", [V_UNARY_NAND] = "~&", [V_UNARY_NOR] = "~|",
            [V_UNARY_XNOR] = "~^",
         };
         const unsigned op = vlog_subkind(v);
         tb_cat(tb, op < ARRAY_LEN(ops) && ops[op] ? ops[op] : "?");
         print_vlog(tb, vlog_value(v), depth + 1);
      }
      break;
   case V_BINARY:
      {
         static const char *const ops[] = {
            [V_BINARY_OR] = "|", [V_BINARY_AND] = "&", [V_BINARY_PLUS] = "+",
            [V_BINARY_MINUS] = "-", [V_BINARY_TIMES] = "*",
            [V_BINARY_DIVIDE] = "/", [V_BINARY_MOD] = "%",
            [V_BINARY_LOG_EQ] = "==", [V_BINARY_LOG_NEQ] = "!=",
            [V_BINARY_CASE_EQ] = "===", [V_BINARY_CASE_NEQ] = "!==",
            [V_BINARY_LOG_OR] = "||", [V_BINARY_LOG_AND] = "&&",
            [V_BINARY_SHIFT_LL] = "<<", [V_BINARY_SHIFT_RL] = ">>",
            [V_BINARY_SHIFT_LA] = "<<<", [V_BINARY_SHIFT_RA] = ">>>",
            [V_BINARY_LT] = "<", [V_BINARY_GT] = ">", [V_BINARY_LEQ] = "<=",
            [V_BINARY_GEQ] = ">=", [V_BINARY_EXP] = "**",
            [V_BINARY_XOR] = "^", [V_BINARY_XNOR] = "~^",
            [V_BINARY_NAND] = "~&", [V_BINARY_NOR] = "~|",
         };
         const unsigned op = vlog_subkind(v);
         print_vlog(tb, vlog_left(v), depth + 1);
         tb_printf(tb, " %s ", op < ARRAY_LEN(ops) && ops[op] ? ops[op] : "?");
         print_vlog(tb, vlog_right(v), depth + 1);
      }
      break;
   case V_COND_EXPR:
      print_vlog(tb, vlog_value(v), depth + 1);
      tb_cat(tb, " ? ");
      print_vlog(tb, vlog_left(v), depth + 1);
      tb_cat(tb, " : ");
      print_vlog(tb, vlog_right(v), depth + 1);
      break;
   case V_CONCAT:
      {
         tb_append(tb, '{');
         const int n = vlog_params(v);
         for (int i = 0; i < n; i++) {
            if (i > 0)
               tb_cat(tb, ", ");
            print_vlog(tb, vlog_param(v, i), depth + 1);
         }
         tb_append(tb, '}');
      }
      break;
   case V_USER_FCALL:
   case V_SYS_FCALL:
      tb_printf(tb, "%s(...)", vlog_has_ident(v) ? istr(vlog_ident(v)) : "call");
      break;
   default:
      tb_cat(tb, "<expression>");
      break;
   }
}

static char *expr_text_vlog(conn_design_t *cd, vlog_node_t v)
{
   LOCAL_TEXT_BUF tb = tb_new();
   print_vlog(tb, v, 0);
   return dstr(cd, tb_get(tb));
}

void add_range(range_list_t *l, conn_net_t *net, unsigned lo,
                      unsigned hi)
{
   if (net == NULL || hi < lo)
      return;

   conn_range_t r = { net, lo, hi };
   APUSH(*l, r);
}

static int range_cmp(const void *a, const void *b)
{
   const conn_range_t *ra = a, *rb = b;
   // Order by creation, not address, so output does not depend on the
   // allocator
   if (ra->net != rb->net)
      return ra->net->id < rb->net->id ? -1 : 1;
   else if (ra->lo != rb->lo)
      return ra->lo < rb->lo ? -1 : 1;
   else
      return ra->hi < rb->hi ? -1 : (ra->hi > rb->hi ? 1 : 0);
}

static void merge_ranges_adj(range_list_t *l, bool adjacent)
{
   // Sort and merge overlapping or adjacent ranges of the same net
   if (l->count < 2)
      return;

   qsort(l->items, l->count, sizeof(conn_range_t), range_cmp);

   unsigned out = 0;
   for (unsigned i = 1; i < l->count; i++) {
      conn_range_t *cur = &(l->items[out]), *r = &(l->items[i]);
      if (r->net == cur->net && r->lo <= cur->hi + (adjacent ? 1 : 0))
         cur->hi = MAX(cur->hi, r->hi);
      else
         l->items[++out] = *r;
   }

   l->count = out + 1;
}

void merge_ranges(range_list_t *l)
{
   merge_ranges_adj(l, true);
}


// Declarations of cloned instances are shared, so a declaration found
// outside the current instance (a hierarchical reference) is only
// meaningful if it was mapped exactly once
static conn_net_t ambiguous;

// The declaration of a net in the instance or an enclosing generate block
static conn_net_t *scope_net(conn_design_t *cd, const void *decl)
{
   for (hash_t *h = cd->netmap; h != NULL; h = hash_get(cd->netparents, h)) {
      conn_net_t *n = hash_get(h, decl);
      if (n != NULL)
         return n;
   }

   return NULL;
}

static conn_net_t *lookup_net(conn_design_t *cd, const void *decl)
{
   conn_net_t *n = scope_net(cd, decl);
   if (n != NULL)
      return n == &ambiguous ? NULL : n;

   n = hash_get(cd->gmap, decl);
   return n == &ambiguous ? NULL : n;
}

static void map_local(conn_design_t *cd, const void *decl, conn_net_t *n)
{
   // Declarations of a cloned interface are shared by all of its instances
   if (decl == NULL)
      return;

   conn_net_t *old = hash_get(cd->netmap, decl);
   hash_put(cd->netmap, decl, old != NULL && old != n ? &ambiguous : n);
}

static void map_decl(conn_design_t *cd, const void *decl, conn_net_t *n)
{
   if (cd->netmap != NULL)
      hash_put(cd->netmap, decl, n);

   conn_net_t *old = hash_get(cd->gmap, decl);
   hash_put(cd->gmap, decl, old != NULL && old != n ? &ambiguous : n);
}

static void add_design_block(conn_design_t *cd, conn_block_t *b)
{
   if (cd->nblocks == cd->maxblocks) {
      cd->maxblocks = MAX(16, cd->maxblocks * 2);
      cd->blocks = xrealloc_array(cd->blocks, cd->maxblocks,
                                  sizeof(conn_block_t *));
   }
   cd->blocks[cd->nblocks++] = b;
}

////////////////////////////////////////////////////////////////////////////////
// Build context

typedef struct {
   conn_block_t *x;        // Interface instance
   const char   *member;
   conn_net_t   *net;      // Net of the block being built
} ifalias_t;

typedef struct {
   conn_design_t *cd;
   conn_block_t  *block;       // Instance being built
   net_list_t   nets;
   inst_list_t  insts;
   proc_list_t  procs;
   shash_t     *names;     // Net names in use
   A(ifalias_t) aliases;   // Nets standing for signals of interfaces
   unsigned         nports;
} bctx_t;

static conn_net_t *new_net(bctx_t *bc, const char *name, conn_dir_t dir,
                           unsigned width, const loc_t *loc)
{
   conn_design_t *cd = bc->cd;
   conn_net_t *n = dalloc(cd, sizeof(conn_net_t));

   // Names must be unique in the block to be usable as keys of netnames
   char *uniq = NULL;
   for (int k = 2; shash_get(bc->names, uniq ?: name) != NULL; k++) {
      free(uniq);
      uniq = xasprintf("%s#%d", name, k);
   }

   n->name  = dstr(cd, uniq ?: name);
   shash_put(bc->names, n->name, n);
   free(uniq);
   n->id    = cd->nnets++;
   n->block = bc->block;
   n->dir   = dir;
   n->width = MAX(width, 1);
   n->src   = loc_file(cd, loc);
   n->line  = loc_line(loc);

   APUSH(bc->nets, n);
   return n;
}

static conn_net_t *find_net_by_name(bctx_t *bc, const char *name)
{
   for (unsigned i = 0; i < bc->nets.count; i++) {
      if (name_eq(bc->nets.items[i]->name, name))
         return bc->nets.items[i];
   }

   return NULL;
}

////////////////////////////////////////////////////////////////////////////////
// VHDL evaluation and layout

static bool eval_int(conn_design_t *cd, tree_t e, int64_t *out, int depth);
static bool vhdl_dim_len(conn_design_t *cd, tree_t name, int64_t *out,
                         int depth);

static bool eval_range(conn_design_t *cd, tree_t r, int64_t *left,
                       int64_t *right, bool *down)
{
   switch (tree_subkind(r)) {
   case RANGE_TO:
      *down = false;
      break;
   case RANGE_DOWNTO:
      *down = true;
      break;
   default:
      return false;
   }

   return eval_int(cd, tree_left(r), left, 0)
      && eval_int(cd, tree_right(r), right, 0);
}

static bool eval_int(conn_design_t *cd, tree_t e, int64_t *out, int depth)
{
   if (depth > 32)
      return false;

   if (folded_int(e, out))
      return true;

   switch (tree_kind(e)) {
   case T_REF:
      {
         if (!tree_has_ref(e))
            return false;

         tree_t decl = tree_ref(e);
         switch (tree_kind(decl)) {
         case T_GENERIC_DECL:
            {
               gen_value_t *gv = cd->genval ? hash_get(cd->genval, decl) : NULL;
               if (gv != NULL && gv->has_int) {
                  *out = gv->ival;
                  return true;
               }

               tree_t v = gv ? gv->value : NULL;
               if (v == NULL && tree_has_value(decl))
                  v = tree_value(decl);
               return v != NULL && eval_int(cd, v, out, depth + 1);
            }
         case T_CONST_DECL:
            return tree_has_value(decl)
               && eval_int(cd, tree_value(decl), out, depth + 1);
         case T_ENUM_LIT:
            *out = tree_pos(decl);
            return true;
         default:
            return false;
         }
      }
   case T_QUALIFIED:
   case T_TYPE_CONV:
      return eval_int(cd, tree_value(e), out, depth + 1);
   case T_ATTR_REF:
      // The length of an object whose value is known is only evaluated with
      // evidence: the width of the nets it sizes must not change the output
      // without it
      return cd->evidence && tree_subkind(e) == ATTR_LENGTH
         && tree_params(e) == 0 && vhdl_dim_len(cd, tree_name(e), out, depth);
   case T_FCALL:
      {
         if (!tree_has_ref(e))
            return false;

         tree_t decl = tree_ref(e);
         const subprogram_kind_t kind = tree_subkind(decl);
         const int nparams = tree_params(e);

         int64_t a, b = 0;
         if (nparams < 1 || nparams > 2
             || !eval_int(cd, tree_value(tree_param(e, 0)), &a, depth + 1))
            return false;
         if (nparams == 2
             && !eval_int(cd, tree_value(tree_param(e, 1)), &b, depth + 1))
            return false;

         switch (kind) {
         case S_ADD: *out = a + b; return nparams == 2;
         case S_SUB: *out = a - b; return nparams == 2;
         case S_MUL: *out = a * b; return nparams == 2;
         case S_DIV: if (b == 0) return false; *out = a / b; return true;
         case S_MOD:
            if (b == 0)
               return false;
            *out = a % b;
            if (*out != 0 && ((*out < 0) != (b < 0)))
               *out += b;
            return true;
         case S_REM: if (b == 0) return false; *out = a % b; return true;
         case S_NEGATE: *out = -a; return nparams == 1;
         case S_IDENTITY: *out = a; return nparams == 1;
         case S_ABS: *out = a < 0 ? -a : a; return nparams == 1;
         case S_EXP:
            {
               if (b < 0 || b > 62)
                  return false;
               int64_t r = 1;
               for (; b > 0; b--)
                  r *= a;
               *out = r;
               return true;
            }
         default:
            return false;
         }
      }
   default:
      return false;
   }
}

typedef struct {
   int64_t left, right;
   bool    down;
   int64_t length;
} dim_t;

static bool vhdl_dim(conn_design_t *cd, type_t type, int n, dim_t *d);

// The number of elements of the value of an expression
static bool value_length(conn_design_t *cd, tree_t v, int64_t *out, int depth)
{
   if (depth > 16 || v == NULL)
      return false;

   switch (tree_kind(v)) {
   case T_STRING:
      *out = tree_chars(v);
      return true;
   case T_AGGREGATE:
      {
         const int n = tree_assocs(v);
         for (int i = 0; i < n; i++) {
            if (tree_subkind(tree_assoc(v, i)) != A_POS)
               return false;
         }
         *out = n;
         return true;
      }
   case T_QUALIFIED:
   case T_TYPE_CONV:
      return value_length(cd, tree_value(v), out, depth + 1);
   case T_REF:
      if (tree_has_ref(v)) {
         tree_t d = tree_ref(v);
         if (tree_kind(d) == T_GENERIC_DECL) {
            gen_value_t *gv = cd->genval ? hash_get(cd->genval, d) : NULL;
            tree_t gval = gv ? gv->value : NULL;
            if (gval == NULL && tree_has_value(d))
               gval = tree_value(d);
            return value_length(cd, gval, out, depth + 1);
         }
         else if (tree_kind(d) == T_CONST_DECL && tree_has_value(d))
            return value_length(cd, tree_value(d), out, depth + 1);
      }
      return false;
   default:
      return false;
   }
}

// 'length of a name with a constrained type or a known value
static bool vhdl_dim_len(conn_design_t *cd, tree_t name, int64_t *out,
                         int depth)
{
   if (depth > 16 || name == NULL)
      return false;

   type_t type = tree_type(name);
   if (type_is_array(type) && !type_is_unconstrained(type)) {
      dim_t d;
      if (vhdl_dim(cd, type, 0, &d)) {
         *out = d.length;
         return true;
      }
   }

   return value_length(cd, name, out, depth + 1);
}

static bool vhdl_dim(conn_design_t *cd, type_t type, int n, dim_t *d)
{
   if (type_is_unconstrained(type))
      return false;

   tree_t r = range_of(type, n);
   if (!eval_range(cd, r, &d->left, &d->right, &d->down))
      return false;

   d->length = d->down ? d->left - d->right + 1 : d->right - d->left + 1;
   if (d->length < 0)
      d->length = 0;
   return true;
}

static bool vhdl_width(conn_design_t *cd, type_t type, unsigned *w)
{
   if (type_is_array(type)) {
      unsigned ew;
      if (!vhdl_width(cd, type_elem(type), &ew))
         return false;

      unsigned n = 1;
      const int ndims = dimension_of(type);
      for (int i = 0; i < ndims; i++) {
         dim_t d;
         if (!vhdl_dim(cd, type, i, &d))
            return false;
         n *= d.length;
      }

      *w = n * ew;
      return true;
   }
   else if (type_is_record(type)) {
      unsigned total = 0;
      const int nf = type_fields(type);
      for (int i = 0; i < nf; i++) {
         unsigned fw;
         if (!vhdl_width(cd, tree_type(type_field(type, i)), &fw))
            return false;
         total += fw;
      }

      *w = total;
      return true;
   }
   else {
      *w = 1;
      return true;
   }
}

static bool type_has_record(type_t type)
{
   if (type_is_record(type))
      return true;
   else if (type_is_array(type))
      return type_has_record(type_elem(type));
   else
      return false;
}

static unsigned dim_position(const dim_t *d, int64_t idx)
{
   return d->down ? d->left - idx : idx - d->left;
}

////////////////////////////////////////////////////////////////////////////////
// VHDL name resolution

bool vhdl_select(conn_design_t *cd, tree_t name, conn_range_t *out,
                        bool *approx)
{
   switch (tree_kind(name)) {
   case T_REF:
      {
         if (!tree_has_ref(name))
            return false;

         conn_net_t *net = lookup_net(cd, tree_ref(name));
         if (net == NULL)
            return false;

         out->net = net;
         out->lo = 0;
         out->hi = net->width - 1;
         return true;
      }

   case T_ARRAY_REF:
   case T_ARRAY_SLICE:
      {
         tree_t value = tree_value(name);
         if (!vhdl_select(cd, value, out, approx))
            return false;

         type_t type = tree_type(value);
         if (!type_is_array(type))
            return true;

         unsigned ew;
         if (!vhdl_width(cd, type_elem(type), &ew))
            goto whole;

         if (tree_kind(name) == T_ARRAY_SLICE) {
            dim_t d;
            if (!vhdl_dim(cd, type, 0, &d))
               goto whole;

            int64_t l, r;
            bool down;
            if (!eval_range(cd, tree_range(name, 0), &l, &r, &down))
               goto whole;

            const int64_t low = MIN(l, r), high = MAX(l, r);
            if (high < low)
               goto whole;

            const unsigned p1 = dim_position(&d, low);
            const unsigned p2 = dim_position(&d, high);
            const unsigned plo = MIN(p1, p2), phi = MAX(p1, p2);

            const unsigned base = out->lo;
            if (base + (phi + 1) * ew - 1 > out->hi)
               goto whole;

            out->lo = base + plo * ew;
            out->hi = base + (phi + 1) * ew - 1;
            return true;
         }
         else {
            const int ndims = dimension_of(type);
            if (tree_params(name) != ndims)
               goto whole;

            uint64_t offset = 0;
            for (int i = 0; i < ndims; i++) {
               dim_t d;
               int64_t idx;
               if (!vhdl_dim(cd, type, i, &d)
                   || !eval_int(cd, tree_value(tree_param(name, i)), &idx, 0)
                   || idx < MIN(d.left, d.right)
                   || idx > MAX(d.left, d.right))
                  goto whole;

               offset = offset * d.length + dim_position(&d, idx);
            }

            const unsigned base = out->lo;
            if (base + (offset + 1) * ew - 1 > out->hi)
               goto whole;

            out->lo = base + offset * ew;
            out->hi = out->lo + ew - 1;
            return true;
         }

      whole:
         *approx = true;
         return true;
      }

   case T_RECORD_REF:
      {
         tree_t value = tree_value(name);
         if (!vhdl_select(cd, value, out, approx))
            return false;

         type_t type = tree_type(value);
         if (!type_is_record(type))
            return true;

         tree_t field = tree_ref(name);
         const unsigned pos = tree_pos(field);

         unsigned offset = 0, fw;
         for (unsigned i = 0; i < pos; i++) {
            if (!vhdl_width(cd, tree_type(type_field(type, i)), &fw)) {
               *approx = true;
               return true;
            }
            offset += fw;
         }

         if (!vhdl_width(cd, tree_type(field), &fw)
             || out->lo + offset + fw - 1 > out->hi) {
            *approx = true;
            return true;
         }

         out->lo += offset;
         out->hi = out->lo + fw - 1;
         return true;
      }

   default:
      return false;
   }
}

////////////////////////////////////////////////////////////////////////////////
// VHDL actuals

static char std_logic_bit(ident_t id)
{
   const char *s = istr(id);
   if (strlen(s) != 3 || s[0] != '\'' || s[2] != '\'')
      return 0;

   switch (s[1]) {
   case '0': case 'L': return '0';
   case '1': case 'H': return '1';
   case 'Z': return 'z';
   case 'U': case 'X': case 'W': case '-': return 'x';
   default: return 0;
   }
}

static void push_seg(seg_list_t *l, conn_seg_t s)
{
   APUSH(*l, s);
}

static void push_open(seg_list_t *l, unsigned width)
{
   conn_seg_t s = { .kind = CONN_SEG_OPEN, .width = width };
   push_seg(l, s);
}

static bool vhdl_const_bits(conn_design_t *cd, tree_t e, text_buf_t *tb,
                            unsigned width_hint, int depth)
{
   // Append one character per bit for a constant std_logic-like value
   if (depth > 8)
      return false;

   switch (tree_kind(e)) {
   case T_REF:
      {
         if (!tree_has_ref(e))
            return false;
         tree_t decl = tree_ref(e);
         switch (tree_kind(decl)) {
         case T_ENUM_LIT:
            {
               const char c = std_logic_bit(tree_ident(decl));
               if (c == 0)
                  return false;
               tb_append(tb, c);
               return true;
            }
         case T_CONST_DECL:
            return tree_has_value(decl)
               && vhdl_const_bits(cd, tree_value(decl), tb, width_hint,
                                  depth + 1);
         case T_GENERIC_DECL:
            {
               gen_value_t *gv = cd->genval ? hash_get(cd->genval, decl) : NULL;
               return gv != NULL && gv->value != NULL
                  && vhdl_const_bits(cd, gv->value, tb, width_hint, depth + 1);
            }
         default:
            return false;
         }
      }
   case T_STRING:
      {
         const int n = tree_chars(e);
         for (int i = 0; i < n; i++) {
            tree_t c = tree_char(e, i);
            if (tree_kind(c) != T_REF || !tree_has_ref(c)
                || tree_kind(tree_ref(c)) != T_ENUM_LIT)
               return false;
            const char b = std_logic_bit(tree_ident(tree_ref(c)));
            if (b == 0)
               return false;
            tb_append(tb, b);
         }
         return true;
      }
   case T_QUALIFIED:
   case T_TYPE_CONV:
      return vhdl_const_bits(cd, tree_value(e), tb, width_hint, depth + 1);
   case T_AGGREGATE:
      {
         type_t type = tree_type(e);
         unsigned total = 0;
         const bool have_total = type_is_array(type)
            && !type_is_unconstrained(type) && vhdl_width(cd, type, &total);

         const int nassocs = tree_assocs(e);
         int npos = 0;
         tree_t others = NULL;
         for (int i = 0; i < nassocs; i++) {
            tree_t a = tree_assoc(e, i);
            switch (tree_subkind(a)) {
            case A_POS: npos++; break;
            case A_OTHERS: others = a; break;
            default: return false;
            }
         }

         const unsigned want = have_total ? total : width_hint;

         for (int i = 0; i < nassocs; i++) {
            tree_t a = tree_assoc(e, i);
            if (tree_subkind(a) == A_POS
                && !vhdl_const_bits(cd, tree_value(a), tb, 1, depth + 1))
               return false;
         }

         if (others != NULL) {
            if (want <= (unsigned)npos)
               return want == (unsigned)npos;
            for (unsigned i = npos; i < want; i++) {
               if (!vhdl_const_bits(cd, tree_value(others), tb, 1, depth + 1))
                  return false;
            }
         }

         return true;
      }
   default:
      return false;
   }
}

static void actual_expr_reads(conn_design_t *cd, tree_t e, range_list_t *reads);

static void vhdl_actual(conn_design_t *cd, tree_t e, unsigned width_hint,
                        seg_list_t *segs, const char *conv)
{
   switch (tree_kind(e)) {
   case T_OPEN:
      push_open(segs, width_hint);
      return;

   case T_CONV_FUNC:
   case T_TYPE_CONV:
      {
         const char *name = tree_kind(e) == T_CONV_FUNC
            ? istr(tree_ident(e)) : "type conversion";
         unsigned before = segs->count;
         vhdl_actual(cd, tree_value(e), width_hint, segs, dstr(cd, name));
         (void)before;
         return;
      }

   case T_QUALIFIED:
      vhdl_actual(cd, tree_value(e), width_hint, segs, conv);
      return;

   case T_FCALL:
      if (tree_has_ref(e) && tree_subkind(tree_ref(e)) == S_CONCAT) {
         const int n = tree_params(e);
         for (int i = 0; i < n; i++)
            vhdl_actual(cd, tree_value(tree_param(e, i)), 1, segs, conv);
         return;
      }
      break;

   default:
      break;
   }

   switch (tree_kind(e)) {
   case T_REF:
   case T_ARRAY_REF:
   case T_ARRAY_SLICE:
   case T_RECORD_REF:
      {
         conn_range_t r;
         bool approx = false;
         if (vhdl_select(cd, e, &r, &approx)) {
            conn_seg_t s = {
               .kind   = CONN_SEG_NET,
               .width  = r.hi - r.lo + 1,
               .range  = r,
               .conv   = conv,
               .approx = approx,
            };
            push_seg(segs, s);
            return;
         }
         break;
      }
   default:
      break;
   }

   // Constant bits
   {
      LOCAL_TEXT_BUF tb = tb_new();
      if (vhdl_const_bits(cd, e, tb, width_hint, 0) && tb_len(tb) > 0) {
         conn_seg_t s = {
            .kind  = CONN_SEG_CONST,
            .width = tb_len(tb),
            .bits  = dstr(cd, tb_get(tb)),
            .conv  = conv,
         };
         push_seg(segs, s);
         return;
      }
   }

   // General expression
   {
      SCOPED_LIST(range_list_t, reads);
      actual_expr_reads(cd, e, &reads);
      merge_ranges(&reads);

      unsigned w = 0;
      if (!vhdl_width(cd, tree_type(e), &w) || w == 0)
         w = MAX(width_hint, 1);

      conn_seg_t s = {
         .kind   = CONN_SEG_EXPR,
         .width  = w,
         .text   = expr_text_vhdl(cd, e),
         .reads  = DDUP(cd, reads, conn_range_t),
         .nreads = reads.count,
         .conv   = conv,
      };
      push_seg(segs, s);
   }
}

////////////////////////////////////////////////////////////////////////////////
// VHDL reads

typedef struct {
   conn_design_t *cd;
   hash_t        *covered;     // Names handled as part of a larger name
   range_list_t  *reads;
   bool           approx;
} vread_t;

static void vhdl_cover_chain(vread_t *vr, tree_t name)
{
   for (;;) {
      switch (tree_kind(name)) {
      case T_ARRAY_REF:
      case T_ARRAY_SLICE:
      case T_RECORD_REF:
         name = tree_value(name);
         hash_put(vr->covered, name, name);
         continue;
      default:
         return;
      }
   }
}

static void vhdl_cover_inner_cb(tree_t t, void *ctx)
{
   // The names inside the name that is read as a whole are not reads of
   // their own, whatever order the tree is visited in
   vread_t *vr = ctx;

   switch (tree_kind(t)) {
   case T_ARRAY_REF:
   case T_ARRAY_SLICE:
   case T_RECORD_REF:
      {
         conn_range_t r;
         bool approx = false;
         if (vhdl_select(vr->cd, t, &r, &approx))
            vhdl_cover_chain(vr, t);
      }
      break;
   default:
      break;
   }
}

static void vhdl_read_names_cb(tree_t t, void *ctx)
{
   vread_t *vr = ctx;

   switch (tree_kind(t)) {
   case T_ARRAY_REF:
   case T_ARRAY_SLICE:
   case T_RECORD_REF:
      {
         if (hash_get(vr->covered, t) != NULL)
            return;   // Part of a longer name

         conn_range_t r;
         bool approx = false;
         if (vhdl_select(vr->cd, t, &r, &approx)) {
            add_range(vr->reads, r.net, r.lo, r.hi);
            vhdl_cover_chain(vr, t);
            hash_put(vr->covered, t, t);
         }
      }
      break;
   default:
      break;
   }
}

static void vhdl_read_refs_cb(tree_t t, void *ctx)
{
   vread_t *vr = ctx;

   if (tree_kind(t) != T_REF || hash_get(vr->covered, t) != NULL)
      return;

   conn_range_t r;
   bool approx = false;
   if (vhdl_select(vr->cd, t, &r, &approx))
      add_range(vr->reads, r.net, r.lo, r.hi);
}

static void vhdl_cover_cb(tree_t t, void *ctx)
{
   // Names appearing as the target of an assignment are not read
   vread_t *vr = ctx;

   switch (tree_kind(t)) {
   case T_SIGNAL_ASSIGN:
   case T_FORCE:
   case T_DUMMY_DRIVER:
   case T_RELEASE:
      {
         tree_t target = tree_target(t);
         hash_put(vr->covered, target, target);
         vhdl_cover_chain(vr, target);
      }
      break;
   default:
      break;
   }
}

static void vhdl_cover_pre(tree_t t, void *ctx)
{
   vhdl_cover_cb(t, ctx);
}

void vhdl_collect_reads(conn_design_t *cd, tree_t root,
                        range_list_t *reads, bool targets_are_writes)
{
   vread_t vr = {
      .cd      = cd,
      .covered = hash_new(64),
      .reads   = reads,
   };

   if (targets_are_writes)
      tree_visit(root, vhdl_cover_pre, &vr);

   // With evidence the element of an array of records read as a field is
   // not also read as a whole.  The output without it keeps the grouping of
   // the pins as it has always been.
   if (cd->evidence)
      tree_visit(root, vhdl_cover_inner_cb, &vr);

   // Names with selects first, so that the plain references inside them
   // are not counted as reads of the whole signal
   tree_visit(root, vhdl_read_names_cb, &vr);
   tree_visit(root, vhdl_read_refs_cb, &vr);

   hash_free(vr.covered);
}

static void actual_expr_reads(conn_design_t *cd, tree_t e, range_list_t *reads)
{
   vhdl_collect_reads(cd, e, reads, false);
}

////////////////////////////////////////////////////////////////////////////////
// Verilog name resolution

static void vlog_net_shape(vlog_node_t decl, unsigned *width, bool *desc,
                           bool *has_bounds, int64_t *left, int64_t *right,
                           bool *record)
{
   *width = 1;
   *desc = true;
   *has_bounds = false;
   *left = *right = 0;
   *record = false;

   if (!vlog_has_type(decl))
      return;

   vlog_node_t dt = vlog_get_type(vlog_type(decl));
   if (dt == NULL)
      dt = vlog_type(decl);

   unsigned w = 1;
   switch (vlog_kind(dt)) {
   case V_DATA_TYPE:
      if (!vlog_is_dynamic_type(dt))
         w = MAX(vlog_size(dt), 1);
      break;
   case V_STRUCT_DECL:
      w = MAX(vlog_size(dt), 1);   // Zero if not packed
      *record = true;
      break;
   default:
      break;
   }

   const unsigned nunpacked = vlog_ranges(decl);
   unsigned total = w;
   for (unsigned i = 0; i < nunpacked; i++)
      total *= MAX(vlog_size(vlog_range(decl, i)), 1);

   *width = MAX(total, 1);

   if (nunpacked == 0 && vlog_kind(dt) == V_DATA_TYPE && vlog_ranges(dt) == 1
       && !vlog_has_type(dt)) {
      int64_t l, r;
      if (vlog_bounds(vlog_range(dt, 0), &l, &r)) {
         *left = l;
         *right = r;
         *desc = l >= r;
         *has_bounds = true;
      }
   }
   else if (vlog_kind(dt) == V_DATA_TYPE && vlog_has_type(dt))
      *record = true;
}

static bool conn_vlog_int(conn_design_t *cd, vlog_node_t v, int64_t *value)
{
   // Constant value of an expression which may use the genvar of the
   // iteration of a loop whose body is shared by its iterations

   if (vlog_get_const(v, value))
      return true;
   else if (cd->genvars == NULL)
      return false;

   int64_t a, b, c;
   switch (vlog_kind(v)) {
   case V_REF:
      if (vlog_has_ref(v)) {
         vlog_node_t d = vlog_ref(v);
         const int64_t *ptr = hash_get(cd->genvars, d);
         if (ptr != NULL) {
            *value = *ptr;
            return true;
         }
         else if (vlog_kind(d) == V_LOCALPARAM && vlog_has_value(d)
                  && vlog_ranges(d) == 0)
            return conn_vlog_int(cd, vlog_value(d), value);
      }
      return false;
   case V_UNARY:
      if (!conn_vlog_int(cd, vlog_value(v), &a))
         return false;
      switch (vlog_subkind(v)) {
      case V_UNARY_NEG: *value = -a; return true;
      case V_UNARY_IDENTITY: *value = a; return true;
      case V_UNARY_NOT: *value = !a; return true;
      default: return false;
      }
   case V_COND_EXPR:
      if (!conn_vlog_int(cd, vlog_value(v), &c)
          || !conn_vlog_int(cd, vlog_left(v), &a)
          || !conn_vlog_int(cd, vlog_right(v), &b))
         return false;
      *value = c ? a : b;
      return true;
   case V_BINARY:
      if (!conn_vlog_int(cd, vlog_left(v), &a)
          || !conn_vlog_int(cd, vlog_right(v), &b))
         return false;
      switch (vlog_subkind(v)) {
      case V_BINARY_PLUS: *value = a + b; return true;
      case V_BINARY_MINUS: *value = a - b; return true;
      case V_BINARY_TIMES: *value = a * b; return true;
      case V_BINARY_DIVIDE:
         if (b == 0)
            return false;
         *value = a / b;
         return true;
      case V_BINARY_MOD:
         if (b == 0)
            return false;
         *value = a % b;
         return true;
      case V_BINARY_SHIFT_LL: *value = a << b; return true;
      case V_BINARY_SHIFT_RL: *value = (uint64_t)a >> b; return true;
      case V_BINARY_LOG_EQ: *value = a == b; return true;
      case V_BINARY_LOG_NEQ: *value = a != b; return true;
      case V_BINARY_LT: *value = a < b; return true;
      case V_BINARY_GT: *value = a > b; return true;
      case V_BINARY_LEQ: *value = a <= b; return true;
      case V_BINARY_GEQ: *value = a >= b; return true;
      case V_BINARY_LOG_AND: *value = a && b; return true;
      case V_BINARY_LOG_OR: *value = a || b; return true;
      default: return false;
      }
   default:
      return false;
   }
}

static int64_t *enter_shared_iter(conn_design_t *cd, tree_t blk,
                                  vlog_node_t *gv)
{
   // The genvar of a generate block shared by the iterations of a loop
   // has the value of this iteration while the block is scanned

   *gv = NULL;

   if (tree_genmaps(blk) != 1 || tree_decls(blk) == 0)
      return NULL;

   tree_t h = tree_decl(blk, 0);
   if (tree_kind(h) != T_HIER || !tree_has_ref(h)
       || tree_kind(tree_ref(h)) != T_VERILOG)
      return NULL;

   vlog_node_t body = tree_vlog(tree_ref(h));
   if (vlog_kind(body) != V_INST_BODY
       || !(vlog_flags(body) & VLOG_F_GEN_SHARED))
      return NULL;

   *gv = vlog_decl(body, vlog_decls(body) - 1);

   if (cd->genvars == NULL)
      cd->genvars = hash_new(16);

   int64_t *saved = hash_get(cd->genvars, *gv);
   int64_t *value = dalloc(cd, sizeof(int64_t));
   *value = tree_ival(tree_value(tree_genmap(blk, 0)));
   hash_put(cd->genvars, *gv, value);
   return saved;
}

static void leave_shared_iter(conn_design_t *cd, vlog_node_t gv,
                              int64_t *saved)
{
   if (gv != NULL)
      hash_put(cd->genvars, gv, saved);
}

bool vlog_select(conn_design_t *cd, vlog_node_t v, conn_range_t *out,
                        bool *approx)
{
   switch (vlog_kind(v)) {
   case V_REF:
   case V_HIER_REF:
      {
         if (vlog_kind(v) == V_HIER_REF && vlog_has_value(v)
             && cd->names != NULL) {
            // The signal of an interface instance or port of this block
            // named with the instance or port as prefix: more reliable
            // than the declaration as that is shared by cloned interfaces
            vlog_node_t prefix = vlog_value(v);
            if (vlog_kind(prefix) == V_MOD_REF && !vlog_has_value(prefix)) {
               char *name = xasprintf("%s.%s", istr(vlog_ident(prefix)),
                                      istr(vlog_ident(v)));
               conn_net_t *net = shash_get(cd->names, name);
               free(name);
               if (net != NULL) {
                  out->net = net;
                  out->lo = 0;
                  out->hi = net->width - 1;
                  return true;
               }
            }
         }

         if (!vlog_has_ref(v))
            return false;

         conn_net_t *net = lookup_net(cd, vlog_ref(v));
         if (net == NULL)
            return false;

         out->net = net;
         out->lo = 0;
         out->hi = net->width - 1;
         return true;
      }

   case V_MEMBER_REF:
      {
         // The names of the members down to the net: ".a.b"
         LOCAL_TEXT_BUF path = tb_new();
         vlog_node_t base = v;
         bool named = true;
         A(vlog_node_t) chain = AINIT;
         while (vlog_kind(base) == V_MEMBER_REF) {
            APUSH(chain, base);
            base = vlog_value(base);
         }
         for (unsigned i = chain.count; i > 0; i--) {
            vlog_node_t m = chain.items[i - 1];
            if (vlog_has_ident(m))
               tb_printf(path, ".%s", istr(vlog_ident(m)));
            else
               named = false;
         }
         ACLEAR(chain);

         if (!vlog_select(cd, base, out, approx))
            return false;

         // Member of a packed structure that is the whole of a net
         const conn_net_t *net = out->net;
         if (named && !*approx && out->lo == 0 && out->hi == net->width - 1) {
            const char *name = tb_get(path);
            const size_t len = strlen(name);
            bool found = false;
            unsigned lo = 0, hi = 0;
            for (unsigned i = 0; i < net->nfields; i++) {
               const char *fn = net->fields[i].name;
               if (strncmp(fn, name, len) != 0
                   || (fn[len] != '\0' && fn[len] != '.'))
                  continue;   // Not this member or a part of it
               if (!found || net->fields[i].lo < lo)
                  lo = net->fields[i].lo;
               if (!found || net->fields[i].hi > hi)
                  hi = net->fields[i].hi;
               found = true;
            }
            if (found) {
               out->lo = lo;
               out->hi = hi;
               return true;
            }
         }

         *approx = true;
         return true;
      }

   case V_BIT_SELECT:
   case V_PART_SELECT:
      {
         if (!vlog_select(cd, vlog_value(v), out, approx))
            return false;

         const conn_net_t *net = out->net;
         if (*approx || !net->has_bounds || out->lo != 0
             || out->hi != net->width - 1) {
            *approx = true;
            return true;
         }

         int64_t a, b;
         if (vlog_kind(v) == V_BIT_SELECT) {
            if (vlog_params(v) != 1
                || !conn_vlog_int(cd, vlog_param(v, 0), &a))
               goto whole;
            b = a;
         }
         else {
            int64_t l, r;
            if (!conn_vlog_int(cd, vlog_left(v), &l)
                || !conn_vlog_int(cd, vlog_right(v), &r))
               goto whole;

            switch (vlog_subkind(v)) {
            case V_RANGE_CONST: a = l; b = r; break;
            case V_RANGE_POS: a = l + r - 1; b = l; break;   // [l +: r]
            case V_RANGE_NEG: a = l; b = l - r + 1; break;   // [l -: r]
            default: goto whole;
            }
         }

         {
            const bool desc = net->left >= net->right;
            const int64_t lo = MIN(a, b), hi = MAX(a, b);
            const int64_t lo_n = MIN(net->left, net->right);
            const int64_t hi_n = MAX(net->left, net->right);
            if (lo < lo_n || hi > hi_n)
               goto whole;

            const unsigned p1 = desc ? net->left - hi : lo - net->left;
            const unsigned p2 = desc ? net->left - lo : hi - net->left;
            out->lo = p1;
            out->hi = p2;
            return true;
         }

      whole:
         *approx = true;
         return true;
      }

   default:
      return false;
   }
}

////////////////////////////////////////////////////////////////////////////////
// Verilog actuals

static void vlog_expr_reads(conn_design_t *cd, vlog_node_t e,
                            range_list_t *reads);

static void vlog_actual(conn_design_t *cd, vlog_node_t e, unsigned width_hint,
                        seg_list_t *segs)
{
   switch (vlog_kind(e)) {
   case V_CONCAT:
      if (!vlog_has_value(e)) {
         const int n = vlog_params(e);
         for (int i = 0; i < n; i++)
            vlog_actual(cd, vlog_param(e, i), 1, segs);
         return;
      }
      break;

   case V_REF:
   case V_HIER_REF:
   case V_BIT_SELECT:
   case V_PART_SELECT:
   case V_MEMBER_REF:
      {
         conn_range_t r;
         bool approx = false;
         if (vlog_select(cd, e, &r, &approx)) {
            conn_seg_t s = {
               .kind   = CONN_SEG_NET,
               .width  = r.hi - r.lo + 1,
               .range  = r,
               .approx = approx,
            };
            push_seg(segs, s);
            return;
         }
         break;
      }

   case V_NUMBER:
      {
         number_t n = vlog_number(e);
         const unsigned w = number_width(n);
         char *bits = dalloc(cd, w + 1);
         for (unsigned i = 0; i < w; i++) {
            static const char digits[] = {
               [LOGIC_0] = '0', [LOGIC_1] = '1', [LOGIC_X] = 'x',
               [LOGIC_Z] = 'z'
            };
            bits[i] = digits[number_bit(n, w - 1 - i)];
         }

         conn_seg_t s = { .kind = CONN_SEG_CONST, .width = w, .bits = bits };
         push_seg(segs, s);
         return;
      }

   default:
      break;
   }

   unsigned w = 0;
   switch (vlog_kind(e)) {
   case V_CONCAT:
   case V_BINARY:
   case V_UNARY:
   case V_COND_EXPR:
   case V_BIT_SELECT:
   case V_PART_SELECT:
   case V_REF:
   case V_SYS_FCALL:
   case V_USER_FCALL:
   case V_CAST:
      w = vlog_width(e);
      break;
   default:
      break;
   }
   if (w == 0)
      w = MAX(width_hint, 1);

   SCOPED_LIST(range_list_t, reads);
   vlog_expr_reads(cd, e, &reads);
   merge_ranges(&reads);

   conn_seg_t s = {
      .kind   = CONN_SEG_EXPR,
      .width  = w,
      .text   = expr_text_vlog(cd, e),
      .reads  = DDUP(cd, reads, conn_range_t),
      .nreads = reads.count,
   };
   push_seg(segs, s);
}

////////////////////////////////////////////////////////////////////////////////
// Verilog read and write sets

typedef struct {
   conn_design_t *cd;
   hash_t        *skel;      // Nodes making up the target of an assignment
   hash_t        *covered;   // References counted as part of a select
   range_list_t  *reads;
   range_list_t  *drives;
   bool           approx;
   A(vlog_node_t) callees;
   hash_t        *seen;
} vscan_t;

static void vscan_mark_skel(vscan_t *vs, vlog_node_t v)
{
   for (;;) {
      hash_put(vs->skel, v, v);

      switch (vlog_kind(v)) {
      case V_BIT_SELECT:
      case V_PART_SELECT:
      case V_MEMBER_REF:
         v = vlog_value(v);
         continue;
      default:
         return;
      }
   }
}

static void vscan_lvalue(vscan_t *vs, vlog_node_t t, bool also_read)
{
   switch (vlog_kind(t)) {
   case V_CONCAT:
      {
         hash_put(vs->skel, t, t);
         const int n = vlog_params(t);
         for (int i = 0; i < n; i++)
            vscan_lvalue(vs, vlog_param(t, i), also_read);
      }
      break;

   case V_REF:
   case V_HIER_REF:
   case V_BIT_SELECT:
   case V_PART_SELECT:
   case V_MEMBER_REF:
      {
         vscan_mark_skel(vs, t);

         conn_range_t r;
         bool approx = false;
         if (vlog_select(vs->cd, t, &r, &approx)) {
            add_range(vs->drives, r.net, r.lo, r.hi);
            if (also_read)
               add_range(vs->reads, r.net, r.lo, r.hi);
         }
         // Else a local variable or an unresolved reference
      }
      break;

   default:
      vs->approx = true;
      break;
   }
}

static void vscan_writes_cb(vlog_node_t v, void *ctx)
{
   vscan_t *vs = ctx;

   switch (vlog_kind(v)) {
   case V_BASSIGN:
   case V_NBASSIGN:
   case V_ASSIGN:
   case V_FORCE:
   case V_GATE_INST:
      if (vlog_has_target(v))
         vscan_lvalue(vs, vlog_target(v), false);
      break;
   case V_OP_ASSIGN:
   case V_PREFIX:
   case V_POSTFIX:
      vscan_lvalue(vs, vlog_target(v), true);
      break;
   case V_SYS_TCALL:
      {
         const int n = vlog_params(v);
         for (int i = 0; i < n; i++) {
            if (vlog_is_scan_target(v, i))
               vscan_lvalue(vs, vlog_param(v, i), false);
         }
      }
      break;
   case V_USER_FCALL:
   case V_USER_TCALL:
      if (vlog_has_ref(v) && hash_get(vs->seen, vlog_ref(v)) == NULL) {
         hash_put(vs->seen, vlog_ref(v), vlog_ref(v));
         APUSH(vs->callees, vlog_ref(v));
      }
      break;
   case V_METHOD_CALL:
   case V_CLASS_NEW:
   case V_DYNAMIC_NEW:
      vs->approx = true;
      break;
   case V_HIER_REF:
      if (!vlog_has_ref(v))
         vs->approx = true;
      break;
   default:
      break;
   }
}

static void vscan_cover_chain(vscan_t *vs, vlog_node_t v)
{
   for (;;) {
      switch (vlog_kind(v)) {
      case V_BIT_SELECT:
      case V_PART_SELECT:
      case V_MEMBER_REF:
         v = vlog_value(v);
         hash_put(vs->covered, v, v);
         continue;
      default:
         return;
      }
   }
}

static void vscan_selects_cb(vlog_node_t v, void *ctx)
{
   vscan_t *vs = ctx;

   switch (vlog_kind(v)) {
   case V_BIT_SELECT:
   case V_PART_SELECT:
   case V_MEMBER_REF:
      {
         if (hash_get(vs->skel, v) != NULL || hash_get(vs->covered, v) != NULL)
            return;

         conn_range_t r;
         bool approx = false;
         if (vlog_select(vs->cd, v, &r, &approx)) {
            add_range(vs->reads, r.net, r.lo, r.hi);
            vscan_cover_chain(vs, v);
            hash_put(vs->covered, v, v);
         }
      }
      break;
   default:
      break;
   }
}

static void vscan_refs_cb(vlog_node_t v, void *ctx)
{
   vscan_t *vs = ctx;

   switch (vlog_kind(v)) {
   case V_REF:
   case V_HIER_REF:
      {
         if (hash_get(vs->skel, v) != NULL || hash_get(vs->covered, v) != NULL)
            return;

         conn_range_t r;
         bool approx = false;
         if (vlog_select(vs->cd, v, &r, &approx))
            add_range(vs->reads, r.net, r.lo, r.hi);
      }
      break;
   default:
      break;
   }
}

static void vscan_run(vscan_t *vs, vlog_node_t root)
{
   vlog_visit(root, vscan_writes_cb, vs);
   vlog_visit(root, vscan_selects_cb, vs);
   vlog_visit(root, vscan_refs_cb, vs);
}

static void vlog_scan(conn_design_t *cd, vlog_node_t root, range_list_t *reads,
                      range_list_t *drives, bool *approx)
{
   vscan_t vs = {
      .cd      = cd,
      .skel    = hash_new(64),
      .covered = hash_new(64),
      .reads   = reads,
      .drives  = drives,
      .seen    = hash_new(16),
   };

   vscan_run(&vs, root);

   // Subroutines called may access signals of the enclosing module
   for (unsigned i = 0; i < vs.callees.count; i++) {
      vlog_node_t callee = vs.callees.items[i];
      switch (vlog_kind(callee)) {
      case V_FUNC_DECL:
      case V_TASK_DECL:
         vscan_run(&vs, callee);
         break;
      default:
         break;
      }
   }

   ACLEAR(vs.callees);
   hash_free(vs.skel);
   hash_free(vs.covered);
   hash_free(vs.seen);

   *approx |= vs.approx;
}

static void vlog_expr_reads(conn_design_t *cd, vlog_node_t e,
                            range_list_t *reads)
{
   range_list_t drives = AINIT;
   bool approx = false;
   vlog_scan(cd, e, reads, &drives, &approx);
   ACLEAR(drives);
}

////////////////////////////////////////////////////////////////////////////////
// Block construction

static void register_generics(conn_design_t *cd, tree_t b, hash_t *dst)
{
   // Values are evaluated in the context of the parent but stored in DST
   const int ngenmaps = tree_genmaps(b);
   for (int i = 0; i < ngenmaps; i++) {
      tree_t m = tree_genmap(b, i);

      tree_t decl = NULL;
      if (tree_subkind(m) == P_POS) {
         if (tree_pos(m) < tree_generics(b))
            decl = tree_generic(b, tree_pos(m));
      }
      else {
         tree_t ref = name_to_ref(tree_name(m));
         if (ref != NULL && tree_has_ref(ref))
            decl = tree_ref(ref);
      }

      if (decl == NULL)
         continue;

      gen_value_t *gv = hash_get(dst, decl);
      if (gv == NULL) {
         gv = xcalloc(sizeof(gen_value_t));
         hash_put(dst, decl, gv);
      }

      gv->value = tree_value(m);
      gv->has_int = eval_int(cd, gv->value, &gv->ival, 0);
   }
}

////////////////////////////////////////////////////////////////////////////////
// VHDL instance parameters

// The source text a location covers, white space collapsed and cut at 200
// characters; comments are not removed so only used for single values
static char *vhdl_param_source(const loc_t *loc)
{
   if (loc_invalid_p(loc) || loc->line_delta != 0)
      return NULL;

   const char *src = loc_get_source(loc);
   if (src == NULL)
      return NULL;

   text_buf_t *tb = tb_new();
   bool space = false;
   const char *p = src + loc->first_column;
   for (unsigned i = 0; i <= loc->column_delta && p[i] != '\0'
           && p[i] != '\n'; i++) {
      if (isspace((unsigned char)p[i]))
         space = true;
      else {
         if (space && tb_len(tb) > 0)
            tb_append(tb, ' ');
         space = false;
         if (tb_len(tb) >= 200) {
            tb_cat(tb, "...");
            break;
         }
         tb_append(tb, p[i]);
      }
   }

   return tb_claim(tb);
}

static void vhdl_param_real(text_buf_t *tb, double value)
{
   // The shortest decimal text that reads back as the same number
   char buf[40];
   for (int prec = 1; prec <= 17; prec++) {
      snprintf(buf, sizeof(buf), "%.*g", prec, value);
      if (strtod(buf, NULL) == value)
         break;
   }
   tb_cat(tb, buf);
}

static bool vhdl_param_char(tree_t e, char *c)
{
   // An element of a string: a character literal
   if (tree_kind(e) != T_REF || !tree_has_ref(e)
       || tree_kind(tree_ref(e)) != T_ENUM_LIT)
      return false;

   const char *name = istr(tree_ident(tree_ref(e)));
   if (strlen(name) != 3 || name[0] != '\'' || name[2] != '\'')
      return false;

   *c = name[1];
   return true;
}

static void vhdl_param_lower(text_buf_t *tb, const char *name)
{
   if (name[0] == '\'' || name[0] == '\\')
      tb_cat(tb, name);   // Character literal or extended identifier
   else {
      for (const char *p = name; *p; p++)
         tb_append(tb, tolower((unsigned char)*p));
   }
}

// The characters of a string made of literals, constants and generics
// joined by &
static bool vhdl_param_string(conn_design_t *cd, tree_t e, text_buf_t *tb,
                              int depth)
{
   char c;
   if (depth > 16)
      return false;


   switch (tree_kind(e)) {
   case T_STRING:
      for (int i = 0; i < tree_chars(e); i++) {
         if (!vhdl_param_char(tree_char(e, i), &c))
            return false;
         tb_append(tb, c);
      }
      return true;

   case T_REF:
      if (vhdl_param_char(e, &c)) {
         tb_append(tb, c);
         return true;
      }
      else if (tree_has_ref(e)) {
         tree_t d = tree_ref(e);
         tree_t v = NULL;
         if (tree_kind(d) == T_GENERIC_DECL) {
            gen_value_t *gv = cd->genval ? hash_get(cd->genval, d) : NULL;
            v = gv ? gv->value : NULL;
            if (v == NULL || tree_kind(v) == T_OPEN)
               v = tree_has_value(d) ? tree_value(d) : NULL;
         }
         else if (tree_kind(d) == T_CONST_DECL && tree_has_value(d))
            v = tree_value(d);
         return v != NULL && vhdl_param_string(cd, v, tb, depth + 1);
      }
      return false;

   case T_FCALL:
      return tree_has_ident(e) && tree_params(e) == 2
         && strcmp(istr(tree_ident(e)), "\"&\"") == 0
         && vhdl_param_string(cd, tree_value(tree_param(e, 0)), tb, depth + 1)
         && vhdl_param_string(cd, tree_value(tree_param(e, 1)), tb, depth + 1);

   case T_AGGREGATE:
      // Concatenation as elaboration leaves it
      for (int i = 0; i < tree_assocs(e); i++) {
         tree_t a = tree_assoc(e, i);
         if (tree_subkind(a) != A_CONCAT
             || !vhdl_param_string(cd, tree_value(a), tb, depth + 1))
            return false;
      }
      return tree_assocs(e) > 0;

   case T_QUALIFIED:
   case T_TYPE_CONV:
      return vhdl_param_string(cd, tree_value(e), tb, depth + 1);

   default:
      return false;
   }
}

static void vhdl_param_text(conn_design_t *cd, text_buf_t *tb, tree_t value)
{
   int64_t ival;

   if (tree_kind(value) == T_FCALL
       || (tree_kind(value) == T_AGGREGATE && tree_assocs(value) > 0
           && tree_subkind(tree_assoc(value, 0)) == A_CONCAT)) {
      // Concatenation of strings
      LOCAL_TEXT_BUF str = tb_new();
      if (vhdl_param_string(cd, value, str, 0)) {
         tb_append(tb, '"');
         for (const char *p = tb_get(str); *p; p++) {
            if (*p == '"')
               tb_append(tb, '"');
            tb_append(tb, *p);
         }
         tb_append(tb, '"');
         return;
      }
   }

   switch (tree_kind(value)) {
   case T_LITERAL:
      switch (tree_subkind(value)) {
      case L_INT:
         tb_printf(tb, "%"PRIi64, tree_ival(value));
         return;
      case L_REAL:
         vhdl_param_real(tb, tree_dval(value));
         return;
      case L_PHYSICAL:
         {
            // As written if it is a simple literal
            char *src LOCAL = vhdl_param_source(tree_loc(value));
            if (src != NULL && isdigit((unsigned char)src[0])
                && strchr(src, ' ') != NULL && strchr(src, '(') == NULL) {
               tb_cat(tb, src);
               return;
            }

            tb_printf(tb, "%"PRIi64, tree_ival(value));
            if (tree_has_ident(value)) {
               tb_append(tb, ' ');
               vhdl_param_lower(tb, istr(tree_ident(value)));
            }
            return;
         }
      default:
         break;
      }
      break;

   case T_STRING:
      {
         tb_append(tb, '"');
         const int nchars = tree_chars(value);
         for (int i = 0; i < nchars; i++) {
            char c;
            if (!vhdl_param_char(tree_char(value, i), &c))
               c = '?';
            if (c == '"')
               tb_append(tb, '"');
            tb_append(tb, c);
         }
         tb_append(tb, '"');
      }
      return;

   case T_AGGREGATE:
      {
         // Positional aggregate of character literals is a string
         const int nassocs = tree_assocs(value);
         bool chars = nassocs > 0;
         for (int i = 0; i < nassocs && chars; i++) {
            tree_t a = tree_assoc(value, i);
            char c;
            chars = tree_subkind(a) == A_POS
               && vhdl_param_char(tree_value(a), &c);
         }

         if (chars) {
            tb_append(tb, '"');
            for (int i = 0; i < nassocs; i++) {
               char c = '?';
               vhdl_param_char(tree_value(tree_assoc(value, i)), &c);
               tb_append(tb, c);
            }
            tb_append(tb, '"');
            return;
         }
      }
      break;

   case T_REF:
      if (tree_has_ref(value)) {
         tree_t decl = tree_ref(value);
         switch (tree_kind(decl)) {
         case T_ENUM_LIT:
            vhdl_param_lower(tb, istr(tree_ident(decl)));
            return;
         case T_GENERIC_DECL:
         case T_CONST_DECL:
            if (eval_int(cd, value, &ival, 0)) {
               tb_printf(tb, "%"PRIi64, ival);
               return;
            }
            break;
         default:
            break;
         }
      }
      break;

   case T_TYPE_CONV:
   case T_QUALIFIED:
      vhdl_param_text(cd, tb, tree_value(value));
      return;

   default:
      break;
   }

   if (eval_int(cd, value, &ival, 0)) {
      tb_printf(tb, "%"PRIi64, ival);
      return;
   }

   // Anything else as written
   char *src LOCAL = vhdl_param_source(tree_loc(value));
   if (src != NULL && *src != '\0')
      tb_cat(tb, src);
   else
      tb_cat(tb, "...");
}

static vlog_node_t block_vlog_body(tree_t b);

static bool shim_param_given(tree_t outer, ident_t name, bool *found)
{
   // A VHDL entity instantiated from Verilog: whether the Verilog instance
   // gave a value to the parameter bound to this generic
   *found = false;
   vlog_node_t body = outer != NULL ? block_vlog_body(outer) : NULL;
   if (body == NULL)
      return false;

   const int ndecls = vlog_decls(body);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(body, i);
      if (vlog_kind(d) == V_LOCALPARAM
          && strcasecmp(istr(vlog_ident(d)), istr(name)) == 0) {
         *found = true;
         return !!(vlog_flags(d) & VLOG_F_OVERRIDDEN);
      }
   }

   return false;
}

static const char *vhdl_param_kind(type_t type, const char *text, bool *exact)
{
   // How to read the text of the value of a generic of this type
   const char *kind = "other";
   *exact = false;

   if (type_is_integer(type))
      kind = "integer", *exact = param_is_int(text);
   else if (type_is_real(type)) {
      kind = "real";
      *exact = isdigit((unsigned char)text[0]) || text[0] == '-';
   }
   else if (type_is_physical(type)) {
      kind = "time";
      *exact = isdigit((unsigned char)text[0]);
   }
   else if (type_is_enum(type)) {
      if (type_ident(type_base_recur(type)) == ident_new("STD.STANDARD.BOOLEAN")) {
         kind = "boolean";
         *exact = !strcmp(text, "true") || !strcmp(text, "false");
      }
      else {
         kind = "enum";
         *exact = true;
      }
   }
   else if (type_is_array(type) && dimension_of(type) == 1) {
      type_t elem = type_base_recur(type_elem(type));
      if (type_is_enum(elem)) {
         const bool character =
            type_ident(elem) == ident_new("STD.STANDARD.CHARACTER");
         kind = character ? "string" : "bits";
         *exact = param_is_quoted(text);
      }
   }

   return kind;
}

static void vhdl_block_params(conn_design_t *cd, conn_block_t *blk, tree_t b,
                              tree_t outer)
{
   // The generics of the instance, defaults included, in declaration order
   param_list_t list = AINIT;

   // The values are written in terms of the generics of the parent
   hash_t *saved_gen = cd->genval;
   if (blk->parent != NULL) {
      inst_maps_t *pm = hash_get(cd->blkmaps, blk->parent);
      if (pm != NULL)
         cd->genval = pm->gen;
   }

   // A component bound to an entity shows the generics of the entity,
   // whose values are written in terms of those of the component
   if (tree_decls(b) > 0 && tree_kind(tree_decl(b, 0)) == T_HIER
       && tree_has_ref(tree_decl(b, 0))
       && tree_kind(tree_ref(tree_decl(b, 0))) == T_COMPONENT) {
      for (int i = 0; i < tree_stmts(b); i++) {
         tree_t s = tree_stmt(b, i);
         if (tree_kind(s) == T_BLOCK && tree_generics(s) > 0) {
            b = s;
            cd->genval = saved_gen;
            break;
         }
      }
   }

   const int ngenerics = tree_generics(b);
   const int ngenmaps = tree_genmaps(b);
   for (int i = 0; i < ngenerics; i++) {
      tree_t g = tree_generic(b, i);

      tree_t value = NULL;
      for (int j = 0; j < ngenmaps && value == NULL; j++) {
         tree_t m = tree_genmap(b, j);
         if (tree_subkind(m) == P_POS && tree_pos(m) == i)
            value = tree_value(m);
      }

      // The value is the default if it was not given or it is the
      // default expression itself
      bool given = value != NULL && tree_kind(value) != T_OPEN
         && !(tree_has_value(g) && value == tree_value(g));

      bool shim;
      const bool shim_given = shim_param_given(outer, tree_ident(g), &shim);
      if (shim)
         given = shim_given;

      LOCAL_TEXT_BUF tb = tb_new();
      const char *kind = "other";
      bool exact = true;
      switch (tree_class(g)) {
      case C_CONSTANT:
         if ((value == NULL || tree_kind(value) == T_OPEN)
             && tree_has_value(g))
            value = tree_value(g);   // Not mapped: the default is used
         if (value == NULL || tree_kind(value) == T_OPEN)
            continue;
         else {
            // The value was evaluated in the context of the parent when
            // the block was registered: it may depend on a generate index
            inst_maps_t *im = hash_get(cd->blkmaps, blk);
            gen_value_t *gv = im ? hash_get(im->gen, g) : NULL;
            if (gv != NULL && gv->has_int && gv->value == value
                && type_is_integer(tree_type(g)))
               tb_printf(tb, "%"PRIi64, gv->ival);
            else
               vhdl_param_text(cd, tb, value);
            kind = vhdl_param_kind(tree_type(g), tb_get(tb), &exact);
         }
         break;

      case C_TYPE:
         if (value == NULL || tree_kind(value) != T_REF
             || !tree_has_type(value))
            continue;
         tb_printf(tb, "%s", type_pp(tree_type(value)));
         kind = "type";
         break;

      default:
         continue;
      }

      const char *tname = NULL;
      if (tree_class(g) == C_CONSTANT)
         tname = type_pp(tree_type(g));

      add_block_param(cd, &list, istr(tree_ident(g)), tb_get(tb), tname, kind,
                      given, exact);
   }

   cd->genval = saved_gen;

   blk->params  = DDUP(cd, list, conn_param_t);
   blk->nparams = list.count;
   ACLEAR(list);
}

static bool is_generate_block(tree_t b)
{
   if (tree_decls(b) == 0)
      return false;

   tree_t hier = tree_decl(b, 0);
   if (tree_kind(hier) != T_HIER)
      return false;

   switch (tree_subkind(hier)) {
   case T_FOR_GENERATE:
   case T_IF_GENERATE:
   case T_CASE_GENERATE:
   case T_COND_STMT:
   case T_ALTERNATIVE:
      return true;
   case T_BLOCK:
      // A generate statement whose condition is constant is replaced by a
      // block; a block statement with ports is an instance in its own right
      return tree_ports(b) == 0 && tree_params(b) == 0
         && tree_generics(b) == 0;
   case T_VERILOG:
      {
         if (!tree_has_ref(hier))
            return false;
         tree_t wrap = tree_ref(hier);
         if (tree_kind(wrap) != T_VERILOG)
            return false;
         return !!(vlog_flags(tree_vlog(wrap)) & VLOG_F_GEN_BODY);
      }
   default:
      return false;
   }
}

static vlog_node_t block_vlog_body(tree_t b)
{
   if (tree_decls(b) == 0)
      return NULL;

   tree_t hier = tree_decl(b, 0);
   if (tree_kind(hier) != T_HIER || tree_subkind(hier) != T_VERILOG
       || !tree_has_ref(hier))
      return NULL;

   tree_t wrap = tree_ref(hier);
   return tree_kind(wrap) == T_VERILOG ? tree_vlog(wrap) : NULL;
}


////////////////////////////////////////////////////////////////////////////////
// Verilog instance parameters

static bool vlog_param_is_integer(vlog_node_t type)
{
   if (type == NULL || vlog_kind(type) != V_DATA_TYPE || vlog_has_type(type))
      return false;

   switch (vlog_subkind(type)) {
   case DT_INT:
   case DT_INTEGER:
   case DT_SHORTINT:
   case DT_LONGINT:
   case DT_BYTE:
      return vlog_ranges(type) <= 1;   // An atom type has its range
   default:
      return false;
   }
}

static void vlog_param_literal(text_buf_t *tb, number_t n, unsigned width)
{
   static const char hex[] = "0123456789abcdef";

   const unsigned nw = number_width(n);
   const bool sign = number_signed(n);
   if (width == 0)
      width = nw;

   // Bits beyond those of the value are zero or a copy of the sign
   vlog_logic_t bits[64 * 4];
   if (width > ARRAY_LEN(bits)) {
      tb_cat(tb, "<wide>");
      return;
   }

   bool has_xz = false;
   for (unsigned i = 0; i < width; i++) {
      if (i < nw)
         bits[i] = number_bit(n, i);
      else if (sign && nw > 0)
         bits[i] = number_bit(n, nw - 1);
      else
         bits[i] = LOGIC_0;
      has_xz |= bits[i] == LOGIC_X || bits[i] == LOGIC_Z;
   }

   tb_printf(tb, "%u'%s", width, sign ? "s" : "");
   if (has_xz) {
      tb_append(tb, 'b');
      for (int i = width - 1; i >= 0; i--)
         tb_append(tb, "01zx"[bits[i]]);
   }
   else {
      tb_append(tb, 'h');
      for (int d = (width + 3) / 4 - 1; d >= 0; d--) {
         unsigned v = 0;
         for (int b = 3; b >= 0; b--) {
            const unsigned pos = d * 4 + b;
            v = (v << 1) | (pos < width && bits[pos] == LOGIC_1);
         }
         tb_append(tb, hex[v]);
      }
   }
}

static void vlog_param_type_text(text_buf_t *tb, vlog_node_t t)
{
   static const char *const names[] = {
      [DT_IMPLICIT] = "", [DT_LOGIC] = "logic", [DT_INTEGER] = "integer",
      [DT_REAL] = "real", [DT_BYTE] = "byte", [DT_SHORTINT] = "shortint",
      [DT_INT] = "int", [DT_LONGINT] = "longint", [DT_TIME] = "time",
      [DT_BIT] = "bit", [DT_SHORTREAL] = "shortreal",
      [DT_REALTIME] = "realtime", [DT_STRING] = "string",
   };

   if (t == NULL)
      return;

   switch (vlog_kind(t)) {
   case V_DATA_TYPE:
      if (vlog_has_type(t))
         vlog_param_type_text(tb, vlog_type(t));
      else if (vlog_subkind(t) < ARRAY_LEN(names) && names[vlog_subkind(t)]) {
         tb_cat(tb, names[vlog_subkind(t)]);
         const int nranges = vlog_ranges(t);
         const bool atom = vlog_subkind(t) != DT_LOGIC
            && vlog_subkind(t) != DT_BIT && vlog_subkind(t) != DT_IMPLICIT;
         for (int i = 0; i < nranges && !atom; i++) {
            int64_t l, r;
            vlog_node_t d = vlog_range(t, i);
            if (vlog_subkind(d) != V_DIM_UNSIZED && vlog_bounds(d, &l, &r))
               tb_printf(tb, "%s[%"PRIi64":%"PRIi64"]", i == 0 ? " " : "", l, r);
         }
      }
      break;
   case V_TYPE_DECL:
   case V_STRUCT_DECL:
   case V_ENUM_DECL:
      if (vlog_has_ident(t))
         tb_cat(tb, istr(ident_rfrom(vlog_ident(t), '.')));
      break;
   default:
      break;
   }
}

// The value of a parameter in this instance as text, see the rules in the
// description of the netlist
static const char *vlog_param_value(conn_design_t *cd, vlog_node_t d)
{
   LOCAL_TEXT_BUF tb = tb_new();

   if (vlog_kind(d) == V_TYPE_DECL) {
      vlog_param_type_text(tb, vlog_type(d));
      return dstr(cd, tb_get(tb));
   }

   vlog_node_t value = vlog_value(d);
   vlog_node_t type = vlog_has_type(d) ? vlog_type(d) : NULL;
   if (type != NULL && vlog_kind(type) == V_DATA_TYPE && vlog_has_type(type)
       && vlog_get_type(type) != NULL)
      type = vlog_get_type(type);

   int64_t v;
   switch (vlog_kind(value)) {
   case V_NUMBER:
      {
         number_t n = vlog_number(value);
         const bool intlike = vlog_param_is_integer(type)
            || (type == NULL && number_width(n) == 32 && number_signed(n))
            || (type != NULL && vlog_kind(type) == V_DATA_TYPE
                && vlog_subkind(type) == DT_IMPLICIT && vlog_ranges(type) == 0
                && number_width(n) == 32 && number_signed(n));
         if (intlike && number_is_defined(n) && number_width(n) <= 64)
            tb_printf(tb, "%"PRIi64, number_integer(n));
         else {
            unsigned width = 0;
            if (type != NULL && vlog_kind(type) == V_DATA_TYPE
                && !vlog_has_type(type) && vlog_ranges(type) > 0) {
               const unsigned w = vlog_size(type);
               if (w > 0)
                  width = w;
            }
            vlog_param_literal(tb, n, width);
         }
      }
      break;
   case V_REAL:
      {
         const double r = vlog_dval(value);
         char buf[32];
         for (int prec = 15; prec <= 17; prec++) {
            snprintf(buf, sizeof(buf), "%.*g", prec, r);
            if (strtod(buf, NULL) == r)
               break;
         }
         tb_cat(tb, buf);
      }
      break;
   case V_STRING:
      {
         number_t n = vlog_number(value);
         tb_append(tb, '"');
         for (int i = number_width(n) / 8 - 1; i >= 0; i--) {
            const uint8_t c = number_byte(n, i);
            if (c == '"' || c == '\\')
               tb_append(tb, '\\');
            if (c >= ' ' && c != 0x7f)
               tb_append(tb, c);
         }
         tb_append(tb, '"');
      }
      break;
   default:
      if (vlog_get_const(value, &v))
         tb_printf(tb, "%"PRIi64, v);
      else {
         LOCAL_TEXT_BUF e = tb_new();
         print_vlog(e, value, 0);
         if (tb_len(e) > 200) {
            tb_trim(e, 200);
            tb_cat(e, "...");
         }
         tb_cat(tb, tb_get(e));
      }
      break;
   }

   return dstr(cd, tb_get(tb));
}

static const char *vlog_param_kind(const char *text, bool *exact)
{
   // How to read the text of the value of a parameter
   *exact = true;
   if (param_is_int(text))
      return "integer";
   else if (param_is_quoted(text))
      return "string";

   const char *p = text;
   while (isdigit((unsigned char)*p))
      p++;
   if (p > text && p[0] == '\'')
      return "bits";

   char *end;
   strtod(text, &end);
   if (end != text && *end == '\0'
       && (isdigit((unsigned char)text[0]) || text[0] == '-'))
      return "real";

   *exact = false;
   return "other";
}

static void vlog_block_params(conn_design_t *cd, conn_block_t *blk,
                              vlog_node_t body)
{
   if (vlog_flags(body) & (VLOG_F_BLACKBOX | VLOG_F_GEN_BODY))
      return;

   param_list_t list = AINIT;

   // A parameter that can be overridden is a local parameter of the
   // instance bound to its value
   const int ndecls = vlog_decls(body);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(body, i);
      switch (vlog_kind(d)) {
      case V_LOCALPARAM:
         if ((vlog_flags(d) & VLOG_F_OVERRIDABLE) && vlog_has_value(d)) {
            const char *text = vlog_param_value(cd, d);
            bool exact;
            const char *kind = vlog_param_kind(text, &exact);

            LOCAL_TEXT_BUF tb = tb_new();
            if (vlog_has_type(d))
               vlog_param_type_text(tb, vlog_type(d));

            if (!strcmp(tb_get(tb), "real") || !strcmp(tb_get(tb), "realtime")
                || !strcmp(tb_get(tb), "shortreal"))
               kind = "real";

            add_block_param(cd, &list, istr(vlog_ident(d)), text,
                            tb_get(tb), kind,
                            !!(vlog_flags(d) & VLOG_F_OVERRIDDEN), exact);
         }
         break;
      case V_TYPE_DECL:
         if (vlog_flags(d) & VLOG_F_TYPEPARAM)
            add_block_param(cd, &list, istr(vlog_ident(d)),
                            vlog_param_value(cd, d), NULL, "type",
                            !!(vlog_flags(d) & VLOG_F_OVERRIDDEN), true);
         break;
      default:
         break;
      }
   }

   if (list.count > 0) {
      blk->params = dalloc(cd, list.count * sizeof(conn_param_t));
      memcpy(blk->params, list.items, list.count * sizeof(conn_param_t));
      blk->nparams = list.count;
   }

   ACLEAR(list);
}

////////////////////////////////////////////////////////////////////////////////
// Fields of records and structures

#define MAX_FIELDS 512

typedef A(conn_field_t) field_list_t;

static void push_field(conn_design_t *cd, field_list_t *l, const char *prefix,
                       const char *name, unsigned lo, unsigned hi)
{
   conn_field_t f = { dstrf(cd, "%s%s", prefix, name), lo, hi };
   APUSH(*l, f);
}

static void vhdl_fields_rec(conn_design_t *cd, field_list_t *l, type_t type,
                            const char *prefix, unsigned off)
{
   if (l->count >= MAX_FIELDS)
      return;

   if (type_is_record(type)) {
      const int nf = type_fields(type);
      for (int i = 0; i < nf; i++) {
         tree_t f = type_field(type, i);
         type_t ft = tree_type(f);
         unsigned fw;
         if (!vhdl_width(cd, ft, &fw))
            return;

         char *name = dstrf(cd, ".%s", istr(tree_ident(f)));
         if (type_has_record(ft)) {
            char *np = dstrf(cd, "%s%s", prefix, name);
            vhdl_fields_rec(cd, l, ft, np, off);
         }
         else if (fw > 0)
            push_field(cd, l, prefix, name, off, off + fw - 1);

         off += fw;
      }
   }
   else if (type_is_array(type) && dimension_of(type) == 1) {
      type_t elem = type_elem(type);
      unsigned ew;
      dim_t d;
      if (!vhdl_width(cd, elem, &ew) || !vhdl_dim(cd, type, 0, &d))
         return;

      for (int64_t k = 0; k < d.length && l->count < MAX_FIELDS; k++) {
         const int64_t idx = d.down ? d.left - k : d.left + k;
         char *np = dstrf(cd, "%s(%"PRIi64")", prefix, idx);
         if (type_has_record(elem) && ew > 0)
            push_field(cd, l, np, "", off + k * ew, off + (k + 1) * ew - 1);
         vhdl_fields_rec(cd, l, elem, np, off + k * ew);
      }
   }
}

static void vhdl_fields(conn_design_t *cd, conn_net_t *n, type_t type)
{
   SCOPED_LIST(field_list_t, fields);
   vhdl_fields_rec(cd, &fields, type, "", 0);

   n->fields = DDUP(cd, fields, conn_field_t);
   n->nfields = fields.count;
}

static void vlog_fields_rec(conn_design_t *cd, field_list_t *l,
                            vlog_node_t st, const char *prefix, unsigned off)
{
   if (st == NULL || vlog_kind(st) != V_STRUCT_DECL
       || !(vlog_flags(st) & VLOG_F_PACKED))
      return;

   const int n = vlog_decls(st);
   for (int i = 0; i < n && l->count < MAX_FIELDS; i++) {
      vlog_node_t m = vlog_decl(st, i);
      if (!vlog_has_type(m))
         return;

      const unsigned w = vlog_size(vlog_type(m));
      char *name = dstrf(cd, ".%s", istr(vlog_ident(m)));

      vlog_node_t sub = vlog_get_type(vlog_type(m));
      if (sub != NULL && vlog_kind(sub) == V_STRUCT_DECL)
         vlog_fields_rec(cd, l, sub, dstrf(cd, "%s%s", prefix, name), off);
      else if (w > 0)
         push_field(cd, l, prefix, name, off, off + w - 1);

      off += w;
   }
}

static void vlog_fields(conn_design_t *cd, conn_net_t *n, vlog_node_t decl)
{
   if (!vlog_has_type(decl))
      return;

   vlog_node_t dt = vlog_get_type(vlog_type(decl));
   if (dt == NULL || vlog_kind(dt) != V_STRUCT_DECL)
      return;

   SCOPED_LIST(field_list_t, fields);
   vlog_fields_rec(cd, &fields, dt, "", 0);

   n->fields = DDUP(cd, fields, conn_field_t);
   n->nfields = fields.count;
}

static void set_vhdl_bounds(conn_design_t *cd, conn_net_t *n, type_t type)
{
   n->record = type_has_record(type);

   if (type_is_array(type) && dimension_of(type) == 1
       && !type_is_array(type_elem(type)) && !type_is_record(type_elem(type))) {
      dim_t d;
      if (vhdl_dim(cd, type, 0, &d)) {
         n->has_bounds = true;
         n->left = d.left;
         n->right = d.right;
         n->descending = d.down;
      }
   }
}

static conn_net_t *add_vhdl_net(bctx_t *bc, tree_t decl, const char *prefix,
                                conn_dir_t dir)
{
   conn_design_t *cd = bc->cd;
   type_t type = tree_type(decl);

   unsigned width = 1;
   if (!vhdl_width(cd, type, &width))
      width = 1;

   char *name = dstrf(cd, "%s%s", prefix, istr(tree_ident(decl)));
   conn_net_t *n = new_net(bc, name, dir, width, tree_loc(decl));
   n->descending = true;
   n->decl = decl;
   set_vhdl_bounds(cd, n, type);
   if (n->record)
      vhdl_fields(cd, n, type);

   if (type_is_array(type) && dimension_of(type) == 1
       && (type_is_array(type_elem(type)) || type_is_record(type_elem(type)))) {
      unsigned ew = 0;
      dim_t d;
      if (vhdl_width(cd, type_elem(type), &ew) && ew > 0
          && vhdl_dim(cd, type, 0, &d)) {
         n->mem.valid = true;
         n->mem.elem_width = ew;
         n->mem.nelems = n->width / ew;
         n->mem.start = MIN(d.left, d.right);
      }
   }

   map_decl(cd, decl, n);

   if (cd->evidence)
      vhdl_net_attrs(cd, n, decl);

   return n;
}

static conn_dir_t vhdl_port_dir(tree_t p)
{
   switch (tree_subkind(p)) {
   case PORT_IN: return CONN_IN;
   case PORT_OUT:
   case PORT_BUFFER: return CONN_OUT;
   default: return CONN_INOUT;
   }
}

static void vlog_memory_shape(conn_design_t *cd, conn_net_t *n,
                              vlog_node_t decl)
{
   const unsigned nunpacked = vlog_ranges(decl);
   if (nunpacked == 0)
      return;

   LOCAL_TEXT_BUF dims = tb_new();
   uint64_t count = 1;
   int64_t start = 0;
   for (unsigned i = 0; i < nunpacked; i++) {
      vlog_node_t r = vlog_range(decl, i);
      count *= MAX(vlog_size(r), 1);

      int64_t l, h;
      if (vlog_subkind(r) != V_DIM_UNSIZED && vlog_bounds(r, &l, &h)) {
         tb_printf(dims, "[%"PRIi64":%"PRIi64"]", l, h);
         if (i == 0)
            start = MIN(l, h);
      }
   }

   n->mem.valid = true;
   n->mem.nelems = count;
   n->mem.start = start;
   n->mem.elem_width = MAX(n->width / count, 1);
   if (nunpacked > 1 && tb_len(dims) > 0)
      n->mem.dims = dstr(cd, tb_get(dims));
}

static conn_net_t *add_vlog_net(bctx_t *bc, vlog_node_t decl,
                                const char *prefix, conn_dir_t dir)
{
   conn_design_t *cd = bc->cd;

   unsigned width;
   bool desc, has_bounds, record;
   int64_t left, right;
   vlog_net_shape(decl, &width, &desc, &has_bounds, &left, &right, &record);

   char *name = dstrf(cd, "%s%s", prefix, istr(vlog_ident(decl)));
   conn_net_t *n = new_net(bc, name, dir, width, vlog_loc(decl));
   n->descending = desc;
   n->has_bounds = has_bounds;
   n->left = left;
   n->right = right;
   n->record = record;
   n->decl = decl;
   vlog_fields(cd, n, decl);
   vlog_memory_shape(cd, n, decl);

   map_decl(cd, decl, n);

   if (cd->evidence)
      vlog_net_attrs(cd, n, decl);

   return n;
}

static bool vlog_decl_is_netlike(vlog_node_t d)
{
   if (!vlog_has_type(d))
      return true;

   vlog_node_t dt = vlog_get_type(vlog_type(d));
   if (dt == NULL)
      dt = vlog_type(d);

   if (vlog_kind(dt) != V_DATA_TYPE)
      return vlog_kind(dt) != V_CLASS_DECL;

   if (vlog_is_dynamic_type(dt))
      return false;

   switch (vlog_subkind(dt)) {
   case DT_REAL:
   case DT_SHORTREAL:
   case DT_REALTIME:
   case DT_EVENT:
   case DT_STRING:
   case DT_DYNARRAY:
   case DT_QUEUE:
   case DT_MAILBOX:
   case DT_VIRTUAL:
   case DT_ASSOC:
   case DT_PROCESS:
   case DT_CHANDLE:
   case DT_SEMAPHORE:
      return false;
   default:
      return true;
   }
}

static void build_ports(bctx_t *bc, tree_t b, vlog_node_t body)
{
   conn_design_t *cd = bc->cd;

   if (body != NULL) {
      const int nports = vlog_ports(body);
      for (int i = 0; i < nports; i++) {
         vlog_node_t ref = vlog_port(body, i);
         if (vlog_kind(ref) != V_REF || !vlog_has_ref(ref))
            continue;

         vlog_node_t pd = vlog_ref(ref);
         if (vlog_kind(pd) != V_PORT_DECL || !vlog_has_ref(pd))
            continue;

         conn_dir_t dir = CONN_INOUT;
         switch ((v_port_kind_t)vlog_subkind(pd)) {
         case V_PORT_INPUT: dir = CONN_IN; break;
         case V_PORT_OUTPUT: dir = CONN_OUT; break;
         default: break;
         }

         vlog_node_t nd = vlog_ref(pd);
         conn_net_t *n = add_vlog_net(bc, nd, "", dir);
         n->decl2 = pd;
         map_decl(cd, pd, n);
         n->name = dstr(cd, istr(vlog_ident(pd)));
      }
   }

   // Ports of the block as seen from VHDL: for a shim of a Verilog module
   // these have the same names as the Verilog ports
   const int nports = tree_ports(b);
   for (int i = 0; i < nports; i++) {
      tree_t p = tree_port(b, i);
      conn_net_t *n = find_net_by_name(bc, istr(tree_ident(p)));
      if (n != NULL)
         map_decl(cd, p, n);
      else
         add_vhdl_net(bc, p, "", vhdl_port_dir(p));
   }

   // Interface ports: one port for each signal of the interface visible
   // through the modport, standing for the signal of the instance
   const int ndecls = tree_decls(b);
   for (int i = 0; i < ndecls; i++) {
      tree_t wrap = tree_decl(b, i);
      if (tree_kind(wrap) != T_VERILOG)
         continue;

      vlog_node_t ip = tree_vlog(wrap);
      if (vlog_kind(ip) != V_IFACE_PORT || !tree_has_ref(wrap))
         continue;

      conn_block_t *x = hash_get(cd->tree_blocks, tree_ref(wrap));
      if (x == NULL || !x->interface)
         continue;   // Not found or not built yet

      const char *pname = istr(tree_ident(wrap));
      vlog_node_t mp = vlog_has_ref(ip) ? vlog_ref(ip) : NULL;

      const int nmp = mp ? vlog_decls(mp) : (int)x->nnets;
      for (int j = 0; j < nmp; j++) {
         conn_net_t *xn = NULL;
         conn_dir_t dir = CONN_INOUT;
         if (mp != NULL) {
            vlog_node_t e = vlog_decl(mp, j);
            if (vlog_kind(e) != V_MODPORT_PORT)
               continue;

            switch ((vlog_modport_dir_t)vlog_subkind(e)) {
            case V_MODPORT_INPUT: dir = CONN_IN; break;
            case V_MODPORT_OUTPUT: dir = CONN_OUT; break;
            case V_MODPORT_INOUT:
            case V_MODPORT_REF: break;
            default: continue;   // Task import or export
            }

            for (unsigned k = 0; k < x->nnets; k++) {
               if (name_eq(x->nets[k]->name, istr(vlog_ident(e))))
                  xn = x->nets[k];
            }
         }
         else
            xn = x->nets[j];

         if (xn == NULL)
            continue;

         char *name = dstrf(cd, "%s.%s", pname, xn->name);
         conn_net_t *n = new_net(bc, name, dir, xn->width, NULL);
         n->descending = xn->descending;
         n->has_bounds = xn->has_bounds;
         n->left = xn->left;
         n->right = xn->right;
         n->record = xn->record;
         n->mem = xn->mem;
         n->fields = xn->fields;
         n->nfields = xn->nfields;
         n->iface = true;
         n->ifblock = x;
         n->member = xn->name;
         n->decl = xn->decl;
         n->decl2 = xn->decl2;
         n->src = xn->src;
         n->line = xn->line;

         ifalias_t al = { x, xn->name, n };
         APUSH(bc->aliases, al);

         map_local(cd, xn->decl, n);
         map_local(cd, xn->decl2, n);
      }
   }

   bc->nports = bc->nets.count;
}

// Each iteration of a Verilog generate loop has its own nets but the
// elaborated blocks share the declarations in them, so the map from a
// declaration to its net has a level for each generate block.  Returns the
// map to restore when leaving the block.
static hash_t *enter_gen(conn_design_t *cd, tree_t blk)
{
   hash_t *saved = cd->netmap;
   hash_t *map = hash_get(cd->genmaps, blk);
   if (map == NULL) {
      map = hash_new(16);
      hash_put(cd->genmaps, blk, map);
      hash_put(cd->netparents, map, saved);
   }

   cd->netmap = map;
   return saved;
}

static void scan_decls(bctx_t *bc, tree_t blk, const char *prefix)
{
   conn_design_t *cd = bc->cd;

   const int ndecls = tree_decls(blk);
   for (int i = 0; i < ndecls; i++) {
      tree_t d = tree_decl(blk, i);
      switch (tree_kind(d)) {
      case T_SIGNAL_DECL:
         add_vhdl_net(bc, d, prefix, CONN_LOCAL);
         break;
      case T_VERILOG:
         {
            vlog_node_t v = tree_vlog(d);
            const vlog_kind_t kind = vlog_kind(v);
            if (kind != V_NET_DECL && kind != V_VAR_DECL)
               break;
            else if (scope_net(cd, v) != NULL)
               break;   // Already declared as a port

            conn_net_t *n = prefix[0] == '\0'
               ? find_net_by_name(bc, istr(vlog_ident(v))) : NULL;
            if (n != NULL)
               map_decl(cd, v, n);
            else if (vlog_decl_is_netlike(v))
               add_vlog_net(bc, v, prefix, CONN_LOCAL);
         }
         break;
      default:
         break;
      }
   }

   const int nstmts = tree_stmts(blk);
   for (int i = 0; i < nstmts; i++) {
      tree_t s = tree_stmt(blk, i);
      if (tree_kind(s) == T_BLOCK && is_generate_block(s)) {
         register_generics(cd, s, cd->genval);
         char *np = dstrf(cd, "%s%s.", prefix, istr(tree_ident(s)));
         hash_t *saved = enter_gen(cd, s);
         vlog_node_t gv;
         int64_t *saved_gv = enter_shared_iter(cd, s, &gv);
         scan_decls(bc, s, np);
         leave_shared_iter(cd, gv, saved_gv);
         cd->netmap = saved;
      }
   }
}

static const char *block_label(tree_t b)
{
   const char *id = istr(tree_ident(b));
   const char *dot = strrchr(id, '.');
   return dot ? dot + 1 : id;
}

static void compute_process_sets(conn_design_t *cd, struct proc_ref *pp);

static conn_proc_t *add_process(bctx_t *bc, const char *prefix, ident_t id,
                                conn_proc_kind_t kind, tree_t where)
{
   conn_design_t *cd = bc->cd;
   conn_proc_t *p = dalloc(cd, sizeof(conn_proc_t));
   p->block = bc->block;
   p->kind  = kind;
   p->src   = loc_file(cd, tree_loc(where));
   p->line  = loc_line(tree_loc(where));

   // Compiler generated names such as "_P3" or "always#12#4" are replaced
   // by the kind of process and the line number
   const char *base = id ? istr(id) : "";
   const bool generated = base[0] == '\0' || strchr(base, '#') != NULL
      || (base[0] == '_' && (base[1] == 'P' || base[1] == 'p'))
      || strncmp(base, "line__", 6) == 0;

   static const char *const kind_names[] = {
      [CONN_PROC_VHDL] = "process", [CONN_PROC_ALWAYS] = "always",
      [CONN_PROC_ALWAYS_FF] = "always_ff",
      [CONN_PROC_ALWAYS_COMB] = "always_comb",
      [CONN_PROC_ALWAYS_LATCH] = "always_latch",
      [CONN_PROC_INITIAL] = "initial", [CONN_PROC_FINAL] = "final",
      [CONN_PROC_ASSIGN] = "assign", [CONN_PROC_GATE] = "gate",
      [CONN_PROC_OTHER] = "other",
   };

   if (generated)
      p->name = dstrf(cd, "%s%s @%u", prefix, kind_names[kind], p->line);
   else
      p->name = dstrf(cd, "%s%s", prefix, base);

   APUSH(bc->procs, p);

   // Evaluated now as the declarations of cloned instances are shared
   struct proc_ref pp = { p, where };
   compute_process_sets(cd, &pp);

   return p;
}

static conn_block_t *build_instance(conn_design_t *cd, tree_t b,
                                    conn_block_t *parent, const char *label,
                                    tree_t outer);

static void normalise_segs(conn_design_t *cd, seg_list_t *segs, unsigned want)
{
   unsigned total = 0;
   unsigned keep = 0;
   for (; keep < segs->count && total < want; keep++) {
      conn_seg_t *s = &(segs->items[keep]);
      if (total + s->width > want) {
         // Truncate the last segment
         s->width = want - total;
         if (s->kind == CONN_SEG_NET)
            s->range.hi = s->range.lo + s->width - 1;
         else if (s->kind == CONN_SEG_CONST)
            ((char *)s->bits)[s->width] = '\0';
      }
      total += s->width;
   }

   segs->count = keep;
   if (total < want)
      push_open(segs, want - total);

   (void)cd;
}

static void add_conn(bctx_t *bc, pconn_list_t *conns, conn_net_t *formal,
                     unsigned lo, unsigned hi, seg_list_t *segs,
                     const loc_t *loc)
{
   conn_design_t *cd = bc->cd;

   unsigned total = 0;
   for (unsigned i = 0; i < segs->count; i++)
      total += segs->items[i].width;

   if (total > 0 && total != hi - lo + 1) {
      // The actual has a different number of elements to the formal, for
      // example through a conversion from integer to a vector: show it as
      // a single expression reading all the bits
      SCOPED_LIST(range_list_t, reads);
      const char *conv = NULL;
      for (unsigned i = 0; i < segs->count; i++) {
         const conn_seg_t *s = &(segs->items[i]);
         if (s->kind == CONN_SEG_NET)
            add_range(&reads, s->range.net, s->range.lo, s->range.hi);
         else if (s->kind == CONN_SEG_EXPR) {
            for (unsigned j = 0; j < s->nreads; j++)
               add_range(&reads, s->reads[j].net, s->reads[j].lo,
                         s->reads[j].hi);
         }
         if (s->conv != NULL)
            conv = s->conv;
      }
      merge_ranges(&reads);

      conn_seg_t e = {
         .kind   = CONN_SEG_EXPR,
         .width  = hi - lo + 1,
         .text   = conv ?: "resize",
         .reads  = DDUP(cd, reads, conn_range_t),
         .nreads = reads.count,
         .conv   = conv,
      };
      segs->count = 0;
      APUSH(*segs, e);
   }

   normalise_segs(cd, segs, hi - lo + 1);

   conn_pconn_t c = {
      .formal = formal,
      .lo     = lo,
      .hi     = hi,
      .segs   = DDUP(cd, *segs, conn_seg_t),
      .nsegs  = segs->count,
      .src    = loc_file(cd, loc),
      .line   = loc_line(loc),
   };
   APUSH(*conns, c);
}

static conn_range_t formal_range(conn_design_t *cd, conn_block_t *child,
                                 tree_t cb, tree_t p, const char **conv)
{
   // Resolve the formal of a port map element in the context of the child
   conn_range_t fr = {};

   inst_maps_t *im = hash_get(cd->blkmaps, child);
   hash_t *saved_net = cd->netmap, *saved_gen = cd->genval;
   cd->netmap = im->net;
   cd->genval = im->gen;

   if (tree_subkind(p) == P_POS) {
      if (tree_pos(p) < tree_ports(cb)) {
         conn_net_t *n = lookup_net(cd, tree_port(cb, tree_pos(p)));
         if (n != NULL)
            fr = (conn_range_t){ n, 0, n->width - 1 };
      }
   }
   else {
      tree_t name = tree_name(p);
      while (tree_kind(name) == T_CONV_FUNC
             || tree_kind(name) == T_TYPE_CONV) {
         *conv = tree_kind(name) == T_CONV_FUNC
            ? dstr(cd, istr(tree_ident(name))) : "type conversion";
         name = tree_value(name);
      }

      bool approx = false;
      if (!vhdl_select(cd, name, &fr, &approx))
         fr.net = NULL;
   }

   cd->netmap = saved_net;
   cd->genval = saved_gen;
   return fr;
}

static void build_conns(bctx_t *bc, conn_inst_t *inst, tree_t cb, tree_t via)
{
   conn_design_t *cd = bc->cd;
   SCOPED_LIST(pconn_list_t, conns);

   // The ports of the module generated for a VHDL entity are left open at
   // this level: they are connected by the port map of the Verilog instance
   const bool shim = via != NULL && find_vhdl_shim_entity(cb) != NULL;

   const int nparams = shim ? 0 : tree_params(cb);
   for (int i = 0; i < nparams; i++) {
      tree_t p = tree_param(cb, i);

      const char *conv = NULL;
      conn_range_t fr = formal_range(cd, inst->child, cb, p, &conv);
      if (fr.net == NULL)
         continue;

      SCOPED_LIST(seg_list_t, segs);
      vhdl_actual(cd, tree_value(p), fr.hi - fr.lo + 1,
                  &segs, NULL);

      if (conv != NULL) {
         for (unsigned j = 0; j < segs.count; j++) {
            if (segs.items[j].conv == NULL)
               segs.items[j].conv = conv;
         }
      }

      add_conn(bc, &conns, fr.net, fr.lo, fr.hi, &segs,
               tree_loc(p));
   }

   const int ndecls = tree_decls(cb);
   for (int i = 0; i < ndecls; i++) {
      tree_t d = tree_decl(cb, i);
      if (tree_kind(d) != T_VERILOG)
         continue;

      vlog_node_t map = tree_vlog(d);
      if (vlog_kind(map) != V_PORT_MAP || !vlog_has_value(map))
         continue;

      // Find the port by name as the declarations are shared by instances
      // cloned from the same module
      vlog_node_t pd = vlog_ref(map);
      conn_net_t *n = NULL;
      for (unsigned j = 0; j < inst->child->nports; j++) {
         if (name_eq(inst->child->nets[j]->name, istr(vlog_ident(pd))))
            n = inst->child->nets[j];
      }

      if (n == NULL)
         n = lookup_net(cd, pd);

      if (n == NULL)
         continue;

      SCOPED_LIST(seg_list_t, segs);
      vlog_actual(cd, vlog_value(map), n->width, &segs);
      add_conn(bc, &conns, n, 0, n->width - 1, &segs,
               vlog_loc(map));
   }

   if (via != NULL) {
      // The connections from the ports of the wrapper to those of the unit
      // it contains are not shown but they are where the conversions are
      const int nvia = tree_params(via);
      for (int i = 0; i < nvia; i++) {
         tree_t p = tree_param(via, i);

         const char *conv = NULL;
         conn_range_t fr = formal_range(cd, inst->child, via, p, &conv);
         if (fr.net == NULL)
            continue;

         if (conv == NULL) {
            tree_t value = tree_value(p);
            if (tree_kind(value) == T_CONV_FUNC)
               conv = dstr(cd, istr(tree_ident(value)));
            else if (tree_kind(value) == T_TYPE_CONV)
               conv = "type conversion";
         }

         if (conv == NULL)
            continue;

         for (unsigned j = 0; j < conns.count; j++) {
            conn_pconn_t *c = &(conns.items[j]);
            if (c->formal != fr.net)
               continue;

            for (unsigned k = 0; k < c->nsegs; k++) {
               if (c->segs[k].conv == NULL)
                  c->segs[k].conv = conv;
            }
         }
      }
   }

   conn_block_t *child = inst->child;

   if (child->interface) {
      // A port of the interface connected to a whole net of this block
      // is that net as far as users of the interface are concerned
      for (unsigned i = 0; i < conns.count; i++) {
         const conn_pconn_t *c = &(conns.items[i]);
         if (c->nsegs != 1 || c->segs[0].kind != CONN_SEG_NET)
            continue;

         conn_net_t *an = c->segs[0].range.net;
         if (c->segs[0].range.lo != 0 || c->segs[0].range.hi != an->width - 1
             || an->width != c->formal->width)
            continue;

         ifalias_t al = { child, c->formal->name, an };
         APUSH(bc->aliases, al);

         map_local(cd, c->formal->decl, an);
         map_local(cd, c->formal->decl2, an);
      }
   }

   for (unsigned i = 0; i < child->nports; i++) {
      conn_net_t *n = child->nets[i];
      if (!n->iface)
         continue;

      conn_net_t *actual;
      if (child->interface) {
         // The signal of the interface instance is a net of this block
         char *name = dstrf(cd, "%s.%s", inst->label, n->name);
         actual = new_net(bc, name, CONN_LOCAL, n->width, NULL);
         actual->descending = n->descending;
         actual->has_bounds = n->has_bounds;
         actual->left = n->left;
         actual->right = n->right;
         actual->record = n->record;
         actual->mem = n->mem;
         actual->fields = n->fields;
         actual->nfields = n->nfields;
         actual->decl = n->decl;
         actual->src = n->src;
         actual->line = n->line;
         actual->iface = true;
         actual->ifblock = child;
         actual->member = n->name;

         ifalias_t al = { child, n->name, actual };
         APUSH(bc->aliases, al);

         map_local(cd, n->decl, actual);
         map_local(cd, n->decl2, actual);
      }
      else {
         // The net of this block standing for the same signal
         actual = NULL;
         for (unsigned k = 0; k < bc->aliases.count; k++) {
            const ifalias_t *al = &(bc->aliases.items[k]);
            if (al->x == n->ifblock && strcmp(al->member, n->member) == 0)
               actual = al->net;
         }

         if (actual == NULL)
            continue;
      }

      SCOPED_LIST(seg_list_t, segs);
      conn_seg_t seg = {
         .kind  = CONN_SEG_NET,
         .width = actual->width,
         .range = { actual, 0, actual->width - 1 },
      };
      APUSH(segs, seg);
      add_conn(bc, &conns, n, 0, n->width - 1, &segs, NULL);
   }

   inst->conns = DDUP(cd, conns, conn_pconn_t);
   inst->nconns = conns.count;
}

static tree_t wrapped_unit(tree_t b)
{
   // Block for the VHDL entity inside the Verilog module generated to
   // instantiate it from Verilog, or for the Verilog module inside the
   // block of the component it is bound to: the wrapper and the instance
   // inside are one instance as far as the user is concerned.

   tree_t shim = find_vhdl_shim_entity(b);
   if (shim != NULL)
      return shim;

   if (tree_decls(b) == 0 || tree_kind(tree_decl(b, 0)) != T_HIER
       || tree_subkind(tree_decl(b, 0)) != T_COMPONENT)
      return NULL;

   if (tree_stmts(b) != 1)
      return NULL;

   tree_t s = tree_stmt(b, 0);
   if (tree_kind(s) != T_BLOCK || tree_decls(s) == 0
       || tree_kind(tree_decl(s, 0)) != T_HIER
       || tree_subkind(tree_decl(s, 0)) != T_VERILOG)
      return NULL;

   return s;
}

static bool is_interface_block(tree_t b)
{
   vlog_node_t body = block_vlog_body(b);
   return body != NULL && (vlog_flags(body) & VLOG_F_INTERFACE);
}

static void scan_stmts(bctx_t *bc, tree_t blk, const char *prefix, int pass)
{
   conn_design_t *cd = bc->cd;

   const int nstmts = tree_stmts(blk);
   for (int i = 0; i < nstmts; i++) {
      tree_t s = tree_stmt(blk, i);
      switch (tree_kind(s)) {
      case T_PROCESS:
         if (pass == 2)
            add_process(bc, prefix, tree_ident(s), CONN_PROC_VHDL, s);
         break;

      case T_VERILOG:
         if (pass != 2)
            break;
         {
            vlog_node_t v = tree_vlog(s);
            conn_proc_kind_t kind = CONN_PROC_OTHER;
            switch (vlog_kind(v)) {
            case V_ALWAYS:
               switch ((vlog_always_kind_t)vlog_subkind(v)) {
               case V_ALWAYS_FF: kind = CONN_PROC_ALWAYS_FF; break;
               case V_ALWAYS_COMB:
               case V_ALWAYS_COMB_DONE: kind = CONN_PROC_ALWAYS_COMB; break;
               case V_ALWAYS_LATCH:
               case V_ALWAYS_LATCH_DONE: kind = CONN_PROC_ALWAYS_LATCH; break;
               default: kind = CONN_PROC_ALWAYS; break;
               }
               break;
            case V_INITIAL: kind = CONN_PROC_INITIAL; break;
            case V_FINAL: kind = CONN_PROC_FINAL; break;
            case V_ASSIGN: kind = CONN_PROC_ASSIGN; break;
            case V_GATE_INST:
            case V_UDP_TABLE: kind = CONN_PROC_GATE; break;
            case V_TCHECK: continue;   // Timing check
            default: break;
            }

            ident_t id = vlog_has_ident(v) ? vlog_ident(v)
               : (tree_has_ident(s) ? tree_ident(s) : NULL);
            if (id != NULL && istr(id)[0] == '$')
               continue;   // Compiler generated process

            add_process(bc, prefix, id, kind, s);
         }
         break;

      case T_BLOCK:
         if (is_generate_block(s)) {
            // The index of a generate loop is not unique to one iteration
            register_generics(cd, s, cd->genval);
            char *np = dstrf(cd, "%s%s.", prefix, istr(tree_ident(s)));
            hash_t *saved = enter_gen(cd, s);
            vlog_node_t gv;
            int64_t *saved_gv = enter_shared_iter(cd, s, &gv);
            scan_stmts(bc, s, np, pass);
            leave_shared_iter(cd, gv, saved_gv);
            cd->netmap = saved;
         }
         else if (pass != (is_interface_block(s) ? 0 : 1))
            break;
         else {
            const char *label = dstrf(cd, "%s%s", prefix, block_label(s));

            // A wrapper added by elaboration at the boundary between VHDL
            // and Verilog is merged with the unit it contains
            tree_t inner = wrapped_unit(s);

            conn_block_t *child =
               build_instance(cd, inner ?: s, bc->block, label,
                              inner ? s : NULL);

            conn_inst_t *inst = child->inst;
            build_conns(bc, inst, s, inner);
            APUSH(bc->insts, inst);
         }
         break;

      default:
         break;
      }
   }
}

static conn_block_t *build_instance(conn_design_t *cd, tree_t b,
                                    conn_block_t *parent, const char *label,
                                    tree_t outer)
{
   conn_block_t *blk = dalloc(cd, sizeof(conn_block_t));
   blk->label  = dstr(cd, label);
   blk->name   = parent ? dstrf(cd, "%s.%s", parent->name, label) : blk->label;
   blk->parent = parent;
   blk->src    = loc_file(cd, tree_loc(b));
   blk->line   = loc_line(tree_loc(b));
   blk->lang   = CONN_VHDL;
   blk->label_lang = CONN_VHDL;

   conn_inst_t *inst = NULL;
   if (parent != NULL) {
      inst = dalloc(cd, sizeof(conn_inst_t));
      inst->parent = parent;
      inst->child  = blk;
      inst->label  = blk->label;
      inst->src    = blk->src;
      inst->line   = blk->line;
      blk->inst    = inst;

      if (outer != NULL) {
         // Where the wrapper was instantiated
         inst->src  = loc_file(cd, tree_loc(outer));
         inst->line = loc_line(tree_loc(outer));
      }
   }

   add_design_block(cd, blk);
   hash_put(cd->tree_blocks, b, blk);
   if (outer != NULL)
      hash_put(cd->tree_blocks, outer, blk);

   // Declarations are mapped to nets in a table private to this instance
   inst_maps_t *im = xcalloc(sizeof(inst_maps_t));
   im->net = hash_new(64);
   im->gen = hash_new(16);
   hash_put(cd->blkmaps, blk, im);

   register_generics(cd, b, im->gen);
   if (outer != NULL)
      register_generics(cd, outer, im->gen);

   hash_t *saved_net = cd->netmap, *saved_gen = cd->genval;
   cd->netmap = im->net;
   cd->genval = im->gen;

   vlog_node_t body = block_vlog_body(b);
   if (body != NULL) {
      blk->lang = CONN_VERILOG;
      blk->label_lang = CONN_VERILOG;
      blk->interface = !!(vlog_flags(body) & VLOG_F_INTERFACE);
   }

   if (body != NULL)
      vlog_block_params(cd, blk, body);
   else
      vhdl_block_params(cd, blk, b, outer);

   if (cd->evidence) {
      if (body != NULL)
         vlog_block_attrs(cd, blk, body);
      else
         vhdl_block_attrs(cd, blk, b, outer);
   }

   if (tree_decls(b) > 0 && tree_kind(tree_decl(b, 0)) == T_HIER) {
      tree_t hier = tree_decl(b, 0);
      if (body != NULL && (vlog_flags(body) & VLOG_F_BLACKBOX)) {
         // The name of the module is that of the missing unit and a number
         char *name LOCAL = xstrdup(istr(vlog_ident2(body)));
         char *suffix = strstr(name, "$bb");
         if (suffix != NULL)
            *suffix = '\0';
         blk->unit = dstr(cd, name);
         blk->blackbox = true;
         blk->inferred = true;
      }
      else if (body != NULL && vlog_has_ident2(body))
         blk->unit = dstr(cd, istr(vlog_ident2(body)));
      else if (tree_has_ref(hier)) {
         tree_t ref = tree_ref(hier);
         switch (tree_kind(ref)) {
         case T_ARCH:
            blk->unit = dstr(cd, istr(tree_ident2(ref)));
            break;
         case T_COMPONENT:
            blk->unit = dstr(cd, istr(tree_ident(ref)));
            blk->component = true;

            // A component that nothing is bound to
            blk->blackbox = tree_stmts(b) == 0;

            // ... which was made from the port map of an instance
            blk->inferred = blk->blackbox && tree_ports(ref) > 0
               && (tree_flags(tree_port(ref, 0)) & TREE_F_BLACK_BOX);
            break;
         default:
            break;
         }
      }
   }

   bctx_t bc = { .cd = cd, .block = blk, .names = shash_new(64) };
   shash_t *saved_names = cd->names;
   cd->names = bc.names;

   build_ports(&bc, b, body);

   if (outer != NULL) {
      // The wrapper of a Verilog module bound to a component is named by the
      // VHDL label of the component instance and that of a VHDL entity
      // instantiated from Verilog by the Verilog label
      blk->label_lang = blk->lang == CONN_VERILOG ? CONN_VHDL : CONN_VERILOG;

      // The ports of the wrapper are those of the unit inside
      const int nports = tree_ports(outer);
      for (int i = 0; i < nports; i++) {
         tree_t p = tree_port(outer, i);
         conn_net_t *n = find_net_by_name(&bc, istr(tree_ident(p)));
         if (n != NULL)
            map_decl(cd, p, n);
      }
   }

   scan_decls(&bc, b, "");

   if (blk->interface) {
      // The signals of an interface are its ports as far as the parent is
      // concerned: they are connected to nets of the parent
      for (unsigned i = bc.nports; i < bc.nets.count; i++) {
         bc.nets.items[i]->dir = CONN_INOUT;
         bc.nets.items[i]->iface = true;
      }
      bc.nports = bc.nets.count;
   }

   // Interface instances first as other blocks refer to their signals,
   // then the other instances, then processes which can refer to signals
   // of the instances through hierarchical names
   for (int pass = 0; pass < 3; pass++)
      scan_stmts(&bc, b, "", pass);

   blk->nets   = DDUP(cd, bc.nets, conn_net_t *);
   blk->nnets  = bc.nets.count;
   blk->nports = bc.nports;
   blk->insts  = DDUP(cd, bc.insts, conn_inst_t *);
   blk->ninsts = bc.insts.count;
   blk->procs  = DDUP(cd, bc.procs, conn_proc_t *);
   blk->nprocs = bc.procs.count;

   ACLEAR(bc.nets);
   ACLEAR(bc.insts);
   ACLEAR(bc.procs);
   shash_free(bc.names);
   ACLEAR(bc.aliases);
   cd->names = saved_names;

   cd->netmap = saved_net;
   cd->genval = saved_gen;

   return blk;
}

static void compute_process_sets(conn_design_t *cd, struct proc_ref *pp)
{
   conn_proc_t *p = pp->proc;
   tree_t t = pp->tree;

   SCOPED_LIST(range_list_t, reads);
   SCOPED_LIST(range_list_t, drives);
   bool approx = false;

   if (tree_kind(t) == T_PROCESS) {
      driver_set_t *ds = find_drivers(t);
      for (driver_info_t *di = get_drivers(ds, t); di; di = di->chain_proc) {
         conn_range_t r;
         bool ap = false;
         if (vhdl_select(cd, di->prefix, &r, &ap))
            add_range(&drives, r.net, r.lo, r.hi);
      }
      free_drivers(ds);

      vhdl_collect_reads(cd, t, &reads, true);
   }
   else {
      vlog_node_t v = tree_vlog(t);
      vlog_scan(cd, v, &reads, &drives,
                &approx);

      switch (vlog_kind(v)) {
      case V_UDP_TABLE:
         approx = true;
         break;
      default:
         break;
      }
   }

   // The reads and drives of adjacent elements or fields are not joined
   // into one pin with evidence, which names the pins that clocks and data
   // are
   merge_ranges_adj(&reads, !cd->evidence);
   merge_ranges_adj(&drives, !cd->evidence);

   p->reads   = DDUP(cd, reads, conn_range_t);
   p->nreads  = reads.count;
   p->drives  = DDUP(cd, drives, conn_range_t);
   p->ndrives = drives.count;
   p->approx  = approx;

   if (cd->evidence) {
      if (tree_kind(t) == T_PROCESS)
         vhdl_proc_evidence(cd, p, t);
      else
         vlog_proc_evidence(cd, p, tree_vlog(t));
   }

   bool foreign = false;
   for (unsigned i = 0; i < reads.count; i++)
      foreign |= reads.items[i].net->block != p->block;
   for (unsigned i = 0; i < drives.count; i++)
      foreign |= drives.items[i].net->block != p->block;

   if (foreign) {
      if (cd->nxprocs == cd->maxxprocs) {
         cd->maxxprocs = MAX(8, cd->maxxprocs * 2);
         cd->xprocs = xrealloc_array(cd->xprocs, cd->maxxprocs,
                                     sizeof(conn_proc_t *));
      }
      cd->xprocs[cd->nxprocs++] = p;
   }
}

conn_design_t *conn_build(tree_t root)
{
   return conn_build_with(root, NULL);
}

conn_design_t *conn_build_with(tree_t root, const conn_build_opts_t *opts)
{
   const tree_kind_t kind = tree_kind(root);
   if (kind != T_ELAB && kind != T_BLOCK)
      return NULL;

   conn_design_t *cd = xcalloc(sizeof(conn_design_t));
   cd->evidence = opts != NULL && opts->evidence;
   cd->gmap = hash_new(1024);
   cd->blkmaps = hash_new(64);
   cd->genmaps = hash_new(64);
   cd->netparents = hash_new(64);
   cd->tree_blocks = hash_new(64);

   A(conn_block_t *) roots = AINIT;

   if (kind == T_BLOCK)
      APUSH(roots, build_instance(cd, root, NULL, block_label(root), NULL));
   else {
      const int nstmts = tree_stmts(root);
      for (int i = 0; i < nstmts; i++) {
         tree_t s = tree_stmt(root, i);
         if (tree_kind(s) == T_BLOCK)
            APUSH(roots, build_instance(cd, s, NULL, block_label(s), NULL));
      }
   }

   cd->roots = DDUP(cd, roots, conn_block_t *);
   cd->nroots = roots.count;
   ACLEAR(roots);


   return cd;
}

void conn_free(conn_design_t *cd)
{
   if (cd == NULL)
      return;

   for (chunk_t *c = cd->chunks, *tmp; c; c = tmp) {
      tmp = c->next;
      free(c);
   }

   for (unsigned i = 0; i < cd->nstrings; i++)
      free(cd->strings[i]);

   if (cd->specs != NULL)
      hash_free(cd->specs);

   free(cd->strings);
   free(cd->blocks);
   free(cd->xprocs);
   const void *key;
   void *value;
   for (hash_iter_t it = HASH_BEGIN;
        hash_iter(cd->blkmaps, &it, &key, &value); ) {
      inst_maps_t *im = value;
      hash_iter_t it2 = HASH_BEGIN;
      const void *k2;
      void *v2;
      while (hash_iter(im->gen, &it2, &k2, &v2))
         free(v2);
      hash_free(im->net);
      hash_free(im->gen);
      free(im);
   }

   for (hash_iter_t it = HASH_BEGIN;
        hash_iter(cd->genmaps, &it, &key, &value); )
      hash_free(value);

   hash_free(cd->genmaps);
   if (cd->genvars != NULL)
      hash_free(cd->genvars);
   hash_free(cd->netparents);
   hash_free(cd->blkmaps);
   hash_free(cd->tree_blocks);
   hash_free(cd->gmap);
   free(cd);
}

unsigned conn_roots(const conn_design_t *cd)
{
   return cd->nroots;
}

conn_block_t *conn_root(const conn_design_t *cd, unsigned n)
{
   assert(n < cd->nroots);
   return cd->roots[n];
}

void conn_walk(const conn_design_t *cd, conn_block_fn_t fn, void *ctx)
{
   for (unsigned i = 0; i < cd->nblocks; i++)
      (*fn)(cd->blocks[i], ctx);
}

////////////////////////////////////////////////////////////////////////////////
// Lookup by path

static const char *skip_sep(const char *p)
{
   while (*p == '.' || *p == ':')
      p++;
   return p;
}

static bool prefix_match(const char *label, const char *path, const char **rest)
{
   // True if PATH starts with LABEL followed by a separator or the end
   const size_t n = strlen(label);
   if (strncasecmp(label, path, n) != 0)
      return false;

   if (path[n] == '\0' || path[n] == '.' || path[n] == ':') {
      *rest = path + n;
      return true;
   }

   return false;
}

static conn_block_t *find_block_in(conn_block_t *b, const char *path,
                                   conn_net_t **net_out)
{
   // PATH is relative to B and has no leading separator
   if (*path == '\0')
      return b;

   if (net_out != NULL) {
      for (unsigned i = 0; i < b->nnets; i++) {
         if (name_eq(b->nets[i]->name, path)) {
            *net_out = b->nets[i];
            return b;
         }
      }

      // The path may use ':' for the separators within a generate prefix
      for (unsigned i = 0; i < b->nnets; i++) {
         const char *n = b->nets[i]->name, *p = path;
         for (; *n && *p; n++, p++) {
            if (tolower((unsigned char)*n) != tolower((unsigned char)*p)
                && !(*n == '.' && *p == ':'))
               break;
         }
         if (*n == '\0' && *p == '\0') {
            *net_out = b->nets[i];
            return b;
         }
      }
   }

   for (unsigned i = 0; i < b->ninsts; i++) {
      const char *rest;
      conn_block_t *c = b->insts[i]->child;
      if (prefix_match(b->insts[i]->label, path, &rest)) {
         conn_block_t *r = find_block_in(c, skip_sep(rest), net_out);
         if (r == NULL && c->ninsts == 1) {
            // A component instance is seen as the entity or module bound
            // to it, which is the only instance in the block
            r = find_block_in(c->insts[0]->child, skip_sep(rest), net_out);
         }
         if (r != NULL)
            return r;
      }
   }

   return NULL;
}

conn_block_t *conn_find_block(const conn_design_t *cd, const char *path)
{
   path = skip_sep(path);

   for (unsigned i = 0; i < cd->nroots; i++) {
      const char *rest;
      if (prefix_match(cd->roots[i]->label, path, &rest)) {
         conn_block_t *b = find_block_in(cd->roots[i], skip_sep(rest), NULL);
         if (b != NULL)
            return b;
      }
   }

   return NULL;
}

conn_net_t *conn_find_net(const conn_design_t *cd, const char *path)
{
   path = skip_sep(path);

   for (unsigned i = 0; i < cd->nroots; i++) {
      const char *rest;
      if (prefix_match(cd->roots[i]->label, path, &rest)) {
         conn_net_t *net = NULL;
         find_block_in(cd->roots[i], skip_sep(rest), &net);
         if (net != NULL)
            return net;
      }
   }

   return NULL;
}

static const char *keep_string(conn_design_t *cd, char *s)
{
   if (cd->nstrings == cd->maxstrings) {
      cd->maxstrings = MAX(16, cd->maxstrings * 2);
      cd->strings = xrealloc_array(cd->strings, cd->maxstrings,
                                   sizeof(char *));
   }

   cd->strings[cd->nstrings++] = s;
   return s;
}

const char *conn_net_path(const conn_design_t *cd, const conn_net_t *net)
{
   return keep_string((conn_design_t *)cd,
                      xasprintf("%s.%s", net->block->name, net->name));
}

////////////////////////////////////////////////////////////////////////////////
// Queries

static bool overlap(unsigned alo, unsigned ahi, unsigned blo, unsigned bhi,
                    unsigned *lo, unsigned *hi)
{
   *lo = MAX(alo, blo);
   *hi = MIN(ahi, bhi);
   return *lo <= *hi;
}

typedef struct {
   const conn_design_t *cd;
   const conn_net_t    *net;
   unsigned             lo, hi;
   conn_end_fn_t        fn;
   void                *ctx;
   unsigned             count;
   bool                 drivers;
} query_t;

static void report(query_t *q, const conn_end_t *end)
{
   q->count++;
   (*q->fn)(end, q->ctx);
}

static void query_proc(query_t *q, conn_proc_t *p)
{
   const conn_range_t *list = q->drivers ? p->drives : p->reads;
   const unsigned n = q->drivers ? p->ndrives : p->nreads;

   for (unsigned i = 0; i < n; i++) {
      unsigned lo, hi;
      if (list[i].net == q->net
          && overlap(list[i].lo, list[i].hi, q->lo, q->hi, &lo, &hi)) {
         conn_end_t end = {
            .kind   = CONN_END_PROC,
            .block  = p->block,
            .proc   = p,
            .src    = p->src,
            .line   = p->line,
            .lo     = lo,
            .hi     = hi,
            .approx = p->approx,
         };
         report(q, &end);
      }
   }
}

static void query_children(query_t *q, conn_block_t *b)
{
   for (unsigned i = 0; i < b->ninsts; i++) {
      conn_inst_t *inst = b->insts[i];
      for (unsigned j = 0; j < inst->nconns; j++) {
         const conn_pconn_t *c = &(inst->conns[j]);
         const conn_dir_t dir = c->formal->dir;

         const bool drives = dir == CONN_OUT || dir == CONN_INOUT;
         const bool reads = dir == CONN_IN || dir == CONN_INOUT;

         unsigned pos = c->lo;
         for (unsigned k = 0; k < c->nsegs; k++) {
            const conn_seg_t *s = &(c->segs[k]);
            unsigned lo, hi;

            if (s->kind == CONN_SEG_NET && s->range.net == q->net
                && (q->drivers ? drives : reads)
                && overlap(s->range.lo, s->range.hi, q->lo, q->hi, &lo, &hi)) {
               const unsigned flo = pos + (lo - s->range.lo);
               conn_end_t end = {
                  .kind   = CONN_END_CHILD,
                  .block  = b,
                  .inst   = inst,
                  .port   = c->formal,
                  .src    = c->src,
                  .line   = c->line,
                  .lo     = lo,
                  .hi     = hi,
                  .other  = { c->formal, flo, flo + (hi - lo) },
                  .approx = s->approx,
               };
               report(q, &end);
            }
            else if (s->kind == CONN_SEG_EXPR && !q->drivers && reads) {
               for (unsigned r = 0; r < s->nreads; r++) {
                  if (s->reads[r].net == q->net
                      && overlap(s->reads[r].lo, s->reads[r].hi,
                                 q->lo, q->hi, &lo, &hi)) {
                     conn_end_t end = {
                        .kind   = CONN_END_EXPR,
                        .block  = b,
                        .inst   = inst,
                        .port   = c->formal,
                        .src    = c->src,
                        .line   = c->line,
                        .lo     = lo,
                        .hi     = hi,
                        .other  = { c->formal, pos, pos + s->width - 1 },
                        .approx = true,
                     };
                     report(q, &end);
                  }
               }
            }

            pos += s->width;
         }
      }
   }
}

static void query_parent(query_t *q)
{
   const conn_net_t *net = q->net;
   conn_block_t *b = net->block;
   conn_inst_t *inst = b->inst;
   if (inst == NULL)
      return;

   bool is_port = false;
   for (unsigned i = 0; i < b->nports; i++)
      is_port |= b->nets[i] == net;

   if (!is_port)
      return;

   if (q->drivers ? (net->dir == CONN_OUT) : (net->dir == CONN_IN))
      return;

   for (unsigned j = 0; j < inst->nconns; j++) {
      const conn_pconn_t *c = &(inst->conns[j]);
      if (c->formal != net)
         continue;

      unsigned pos = c->lo;
      for (unsigned k = 0; k < c->nsegs; k++) {
         const conn_seg_t *s = &(c->segs[k]);
         unsigned lo, hi;
         if (overlap(pos, pos + s->width - 1, q->lo, q->hi, &lo, &hi)
             && s->kind != CONN_SEG_OPEN) {
            conn_end_t end = {
               .kind   = CONN_END_PARENT,
               .block  = inst->parent,
               .inst   = inst,
               .port   = net,
               .src    = c->src,
               .line   = c->line,
               .lo     = lo,
               .hi     = hi,
               .approx = s->approx || s->kind == CONN_SEG_EXPR,
            };

            if (s->kind == CONN_SEG_NET) {
               const unsigned off = lo - pos;
               end.other.net = s->range.net;
               end.other.lo = s->range.lo + off;
               end.other.hi = end.other.lo + (hi - lo);
            }

            report(q, &end);
         }

         pos += s->width;
      }
   }
}

static unsigned run_query(const conn_design_t *cd, const conn_net_t *net,
                          unsigned lo, unsigned hi, conn_end_fn_t fn,
                          void *ctx, bool drivers)
{
   if (hi >= net->width)
      hi = net->width - 1;
   if (lo > hi)
      return 0;

   query_t q = {
      .cd = cd, .net = net, .lo = lo, .hi = hi, .fn = fn, .ctx = ctx,
      .drivers = drivers,
   };

   conn_block_t *b = net->block;
   for (unsigned i = 0; i < b->nprocs; i++)
      query_proc(&q, b->procs[i]);

   for (unsigned i = 0; i < cd->nxprocs; i++) {
      if (cd->xprocs[i]->block != b)
         query_proc(&q, cd->xprocs[i]);
   }

   query_children(&q, b);
   query_parent(&q);

   return q.count;
}

unsigned conn_drivers(const conn_design_t *cd, const conn_net_t *net,
                      unsigned lo, unsigned hi, conn_end_fn_t fn, void *ctx)
{
   return run_query(cd, net, lo, hi, fn, ctx, true);
}

unsigned conn_readers(const conn_design_t *cd, const conn_net_t *net,
                      unsigned lo, unsigned hi, conn_end_fn_t fn, void *ctx)
{
   return run_query(cd, net, lo, hi, fn, ctx, false);
}

const char *conn_end_describe(const conn_design_t *cd, const conn_end_t *end)
{
   conn_design_t *mcd = (conn_design_t *)cd;   // String table is a cache

   char *s = NULL;
   switch (end->kind) {
   case CONN_END_PROC:
      s = xasprintf("process %s.%s", end->block->name, end->proc->name);
      break;
   case CONN_END_CHILD:
      s = xasprintf("port %s of %s", end->port->name, end->inst->child->name);
      break;
   case CONN_END_EXPR:
      s = xasprintf("expression connected to port %s of %s",
                    end->port->name, end->inst->child->name);
      break;
   case CONN_END_PARENT:
      s = xasprintf("connection of port %s in %s", end->port->name,
                    end->block->name);
      break;
   }

   if (end->src != NULL) {
      char *t = xasprintf("%s (%s:%u)", s, end->src, end->line);
      free(s);
      s = t;
   }

   return keep_string(mcd, s);
}
