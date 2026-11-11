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
#include "common.h"
#include "diag.h"
#include "ident.h"
#include "jit/jit-ffi.h"
#include "jit/jit-frame.h"
#include "jit/jit-layout.h"
#include "jit/jit-priv.h"
#include "tree.h"
#include "type.h"
#include "vlog/vlog-node.h"

#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ELEMS  32     // Elements of an array shown in the text
#define MAX_DEPTH  3      // Levels of records and arrays shown in the text
#define MAX_CHARS  2048   // Length of any text

////////////////////////////////////////////////////////////////////////////////
// Declarations

typedef struct {
   ident_t     name;
   vlog_node_t found;
} vlog_find_t;

static void vlog_find_cb(vlog_node_t v, void *ctx)
{
   vlog_find_t *f = ctx;
   if (f->found != NULL)
      return;

   switch (vlog_kind(v)) {
   case V_VAR_DECL:
   case V_LOCAL_DECL:
   case V_TF_PORT_DECL:
      if (vlog_ident(v) == f->name)
         f->found = v;
      break;
   default:
      break;
   }
}

static vlog_node_t find_vlog_decl(vlog_node_t sub, ident_t name)
{
   switch (vlog_kind(sub)) {
   case V_FUNC_DECL:
   case V_TASK_DECL:
      break;
   default:
      return NULL;
   }

   // The variable that holds the result has the name of the function
   if (vlog_ident(sub) == name)
      return sub;

   vlog_find_t f = { .name = name };
   vlog_visit(sub, vlog_find_cb, &f);
   return f.found;
}

static tree_t find_tree_decl(tree_t sub, ident_t name)
{
   switch (tree_kind(sub)) {
   case T_FUNC_BODY:
   case T_PROC_BODY:
      break;
   default:
      return NULL;
   }

   const int nports = tree_ports(sub);
   for (int i = 0; i < nports; i++) {
      tree_t p = tree_port(sub, i);
      if (tree_ident(p) == name)
         return p;
   }

   const int ndecls = tree_decls(sub);
   for (int i = 0; i < ndecls; i++) {
      tree_t d = tree_decl(sub, i);
      switch (tree_kind(d)) {
      case T_VAR_DECL:
      case T_CONST_DECL:
         if (tree_ident(d) == name)
            return d;
         break;
      default:
         break;
      }
   }

   return NULL;
}

bool frame_find_decl(object_t *subprogram, ident_t name, bool *is_param)
{
   if (subprogram == NULL)
      return false;

   tree_t t = tree_from_object(subprogram);
   if (t != NULL) {
      tree_t d = find_tree_decl(t, name);
      if (d != NULL && is_param != NULL)
         *is_param = tree_kind(d) == T_PARAM_DECL;
      return d != NULL;
   }

   vlog_node_t v = vlog_from_object(subprogram);
   if (v != NULL) {
      vlog_node_t d = find_vlog_decl(v, name);
      if (d != NULL && is_param != NULL)
         *is_param = vlog_kind(d) == V_TF_PORT_DECL;
      return d != NULL;
   }

   return false;
}

////////////////////////////////////////////////////////////////////////////////
// VHDL values

typedef struct {
   text_buf_t *tb;
} fmt_t;

static bool layoutable(type_t type)
{
   if (type_is_scalar(type))
      return type_is_integer(type) || type_is_enum(type)
         || type_is_real(type) || type_is_physical(type);
   else if (type_is_array(type))
      return layoutable(type_elem_recur(type));
   else if (type_is_record(type)) {
      const int nf = type_fields(type);
      for (int i = 0; i < nf; i++) {
         if (!layoutable(tree_type(type_field(type, i))))
            return false;
      }
      return true;
   }
   else
      return false;
}

static bool scalar_signed(type_t type)
{
   if (type_is_real(type))
      return true;

   type_t base = type_base_recur(type);
   int64_t low, high;
   if (!folded_bounds(type_dim(base, 0), &low, &high))
      return true;

   return low < 0;
}

static int64_t read_scalar(type_t type, const void *mem)
{
   const int size = layout_of(type)->size;
   const bool sign = scalar_signed(type);
   switch (size) {
   case 1: return sign ? (int64_t)*(const int8_t *)mem
         : (int64_t)*(const uint8_t *)mem;
   case 2:
      {
         int16_t s16; uint16_t u16;
         if (sign) { memcpy(&s16, mem, 2); return s16; }
         memcpy(&u16, mem, 2); return u16;
      }
   case 4:
      {
         int32_t s32; uint32_t u32;
         if (sign) { memcpy(&s32, mem, 4); return s32; }
         memcpy(&u32, mem, 4); return u32;
      }
   default:
      {
         int64_t s64;
         memcpy(&s64, mem, 8);
         return s64;
      }
   }
}

static double read_real(const void *mem)
{
   double d;
   memcpy(&d, mem, sizeof(d));
   return d;
}

static void cat_lower(text_buf_t *tb, const char *name)
{
   // Identifiers are shown in lower case, character literals as written
   if (name[0] == '\'')
      tb_cat(tb, name);
   else {
      for (const char *p = name; *p; p++)
         tb_append(tb, (*p >= 'A' && *p <= 'Z') ? *p - 'A' + 'a' : *p);
   }
}

static char *dup_lower(const char *name)
{
   char *s = xstrdup(name);
   if (s[0] != '\'')
      for (char *p = s; *p; p++)
         if (*p >= 'A' && *p <= 'Z') *p = *p - 'A' + 'a';
   return s;
}

static void fmt_physical(text_buf_t *tb, type_t type, int64_t value)
{
   type_t base = type_base_recur(type);
   const unsigned nunits = type_units(base);

   // The largest unit that has a whole number of it
   int64_t mult = 1;
   tree_t best = nunits > 0 ? type_unit(base, 0) : NULL;
   for (unsigned i = 0; i < nunits; i++) {
      tree_t u = type_unit(base, i);
      const int64_t m = assume_int(tree_value(u));
      if (m > 0 && value % m == 0 && m >= mult) {
         mult = m;
         best = u;
      }
   }

   tb_printf(tb, "%"PRId64, value / mult);
   if (best != NULL) {
      tb_append(tb, ' ');
      cat_lower(tb, istr(tree_ident(best)));
   }
}

static void fmt_enum(text_buf_t *tb, type_t type, int64_t pos)
{
   type_t base = type_base_recur(type);
   if (pos >= 0 && pos < type_enum_literals(base))
      cat_lower(tb, istr(tree_ident(type_enum_literal(base, pos))));
   else
      tb_printf(tb, "%"PRId64, pos);
}

typedef struct {
   const void *data;
   int64_t     count;
   int64_t     left;
   bool        descending;
   bool        known;     // The bounds are known
} geom_t;

static bool array_geom(type_t type, const void *mem, bool image, geom_t *g)
{
   // mem is the data of a constrained array, or if image is set the
   // representation of an unconstrained one: the pointer to the data and
   // for each dimension the left bound and the length
   const int ndims = dimension_of(type);
   if (image || type_is_unconstrained(type) || !type_const_bounds(type)) {
      const void *ptr;
      memcpy(&ptr, mem, sizeof(ptr));
      g->data = ptr;
      g->count = 1;
      g->known = true;
      for (int i = 0; i < ndims; i++) {
         int64_t left, len;
         memcpy(&left, (const char *)mem + 8 + 16 * i, 8);
         memcpy(&len, (const char *)mem + 16 + 16 * i, 8);
         if (i == 0) {
            g->left = left;
            g->descending = ffi_array_dir(len);
         }
         g->count *= ffi_array_length(len);
      }
      return g->data != NULL || g->count == 0;
   }

   g->data = mem;
   g->count = 1;
   g->known = true;
   for (int i = 0; i < ndims; i++) {
      tree_t r = range_of(type, i);
      int64_t len;
      if (!folded_length(r, &len))
         return false;
      g->count *= len;

      if (i == 0) {
         int64_t low, high;
         if (folded_bounds(r, &low, &high)) {
            g->descending = tree_subkind(r) == RANGE_DOWNTO;
            g->left = g->descending ? high : low;
         }
      }
   }

   // Arrays of arrays of constant size are flat
   type_t elem = type_elem(type);
   if (type_is_array(elem) && !type_is_unconstrained(elem)) {
      geom_t ge;
      if (!array_geom(elem, mem, false, &ge))
         return false;
      g->count *= ge.count;
   }

   return true;
}

static bool is_character_type(type_t elem)
{
   type_t base = type_base_recur(elem);
   return type_is_enum(base) && strcmp(istr(type_ident(base)), "STD.STANDARD.CHARACTER") == 0;
}

static bool is_logic_type(type_t elem, bool *is_bit)
{
   type_t base = type_base_recur(elem);
   if (!type_is_enum(base))
      return false;

   ident_t id = type_ident(base);
   if (strcmp(istr(id), "STD.STANDARD.BIT") == 0) {
      *is_bit = true;
      return true;
   }
   else if (strcmp(istr(id), "IEEE.STD_LOGIC_1164.STD_ULOGIC") == 0) {
      *is_bit = false;
      return true;
   }

   return false;
}

static void fmt_value(text_buf_t *tb, type_t type, const void *mem,
                      bool image, int depth);

static void fmt_array(text_buf_t *tb, type_t type, const void *mem,
                      bool image, int depth)
{
   geom_t g;
   if (!array_geom(type, mem, image, &g)) {
      tb_cat(tb, "<unknown>");
      return;
   }

   type_t elem = type_elem_recur(type);
   const jit_layout_t *el = layout_of(elem);
   const int64_t shown = MIN(g.count, MAX_ELEMS * 4);

   bool is_bit;
   if (type_is_enum(elem) && (is_character_type(elem) || is_logic_type(elem, &is_bit))
       && dimension_of(type) == 1) {
      tb_append(tb, '"');
      for (int64_t i = 0; i < shown; i++) {
         const int64_t pos = read_scalar(elem, (const char *)g.data + i * el->size);
         type_t base = type_base_recur(elem);
         if (pos >= 0 && pos < type_enum_literals(base)) {
            const char *lit = istr(tree_ident(type_enum_literal(base, pos)));
            if (lit[0] == '\'' && lit[1] != '\0' && lit[2] == '\'')
               tb_append(tb, lit[1]);
            else
               tb_cat(tb, lit);
         }
         else
            tb_append(tb, '?');
      }
      if (shown < g.count)
         tb_cat(tb, "...");
      tb_append(tb, '"');
      return;
   }

   tb_append(tb, '(');
   const int64_t n = MIN(g.count, MAX_ELEMS);
   for (int64_t i = 0; i < n; i++) {
      if (i > 0)
         tb_cat(tb, ", ");
      fmt_value(tb, elem, (const char *)g.data + i * el->size, false, depth + 1);
   }
   if (n < g.count)
      tb_printf(tb, ", ... (%"PRId64" elements)", g.count);
   tb_append(tb, ')');
}

static void fmt_value(text_buf_t *tb, type_t type, const void *mem,
                      bool image, int depth)
{
   if (tb_len(tb) > MAX_CHARS) {
      return;
   }
   else if (!layoutable(type)) {
      tb_cat(tb, "<unsupported>");
      return;
   }

   if (type_is_enum(type))
      fmt_enum(tb, type, read_scalar(type, mem));
   else if (type_is_integer(type))
      tb_printf(tb, "%"PRId64, read_scalar(type, mem));
   else if (type_is_physical(type))
      fmt_physical(tb, type, read_scalar(type, mem));
   else if (type_is_real(type))
      tb_printf(tb, "%.15g", read_real(mem));
   else if (type_is_array(type)) {
      if (depth >= MAX_DEPTH)
         tb_cat(tb, "(...)");
      else
         fmt_array(tb, type, mem, image, depth);
   }
   else if (type_is_record(type)) {
      if (depth >= MAX_DEPTH) {
         tb_cat(tb, "(...)");
         return;
      }

      const jit_layout_t *l = layout_of(type);
      const int nf = type_fields(type);
      tb_append(tb, '(');
      for (int i = 0; i < nf; i++) {
         tree_t f = type_field(type, i);
         if (i > 0)
            tb_cat(tb, ", ");
         cat_lower(tb, istr(tree_ident(f)));
         tb_cat(tb, " => ");

         type_t ft = tree_type(f);
         const char *fmem = (const char *)mem + l->parts[i].offset;
         if (type_is_array(ft) && type_is_unconstrained(ft))
            fmt_value(tb, ft, fmem, true, depth + 1);
         else
            fmt_value(tb, ft, fmem, false, depth + 1);
      }
      tb_append(tb, ')');
   }
   else
      tb_cat(tb, "<unsupported>");
}

typedef struct {
   frame_info_t info;
   char        *bits;
   const char **literals;
   text_buf_t  *text;
   char        *type_name;
} out_t;

static char *type_display_name(type_t type)
{
   // The most specific name of a type that has one, in lower case without
   // the name of the library and package
   while (type_kind(type) == T_SUBTYPE && is_anonymous_subtype(type))
      type = type_base(type);

   const char *full = istr(type_ident(type));
   const char *dot = strrchr(full, '.');
   return dup_lower(dot != NULL ? dot + 1 : full);
}

static int bits_for_count(unsigned n)
{
   int bits = 1;
   while (bits < 31 && (1u << bits) < n)
      bits++;
   return bits;
}

static char *bits_of(uint64_t v, int width)
{
   char *s = xmalloc(width + 1);
   for (int i = 0; i < width; i++) {
      const int idx = width - 1 - i;
      s[i] = (idx < 64 && ((v >> idx) & 1)) ? '1' : '0';
   }
   s[width] = '\0';
   return s;
}

static void vhdl_scalar_info(out_t *o, type_t type, const void *mem,
                             const jit_scalar_t *slot)
{
   // mem is the variable in memory or NULL if the value is in slot
   frame_info_t *info = &(o->info);
   type_t base = type_base_recur(type);

   if (type_is_enum(type)) {
      bool is_bit;
      int64_t pos = mem ? read_scalar(type, mem) : slot->integer;
      const unsigned nlits = type_enum_literals(base);

      if (is_logic_type(type, &is_bit)) {
         static const char map[] = "ux01zwlh-";
         info->kind = is_bit ? FRAME_K_BIT : FRAME_K_LOGIC;
         info->width = 1;
         char *b = xmalloc(2);
         b[0] = is_bit ? ('0' + (pos & 1)) : (pos >= 0 && pos < 9 ? map[pos] : 'x');
         b[1] = '\0';
         o->bits = b;
      }
      else {
         info->kind = FRAME_K_ENUM;
         info->width = bits_for_count(nlits);
         o->bits = bits_of((uint64_t)pos, info->width);
         o->literals = xmalloc_array(nlits + 1, sizeof(char *));
         for (unsigned i = 0; i < nlits; i++)
            o->literals[i] = dup_lower(istr(tree_ident(type_enum_literal(base, i))));
         info->nliterals = nlits;
         info->literals = o->literals;
      }
   }
   else if (type_is_integer(type)) {
      const int64_t v = mem ? read_scalar(type, mem) : slot->integer;
      info->kind = FRAME_K_INTEGER;
      info->width = MAX(8, layout_of(type)->size * 8);
      info->is_signed = scalar_signed(type);
      o->bits = bits_of((uint64_t)v, info->width);

      int64_t low, high;
      if (folded_bounds(range_of(type, 0), &low, &high)) {
         info->has_range = true;
         info->low = low;
         info->high = high;
      }
   }
   else if (type_is_physical(type)) {
      const int64_t v = mem ? read_scalar(type, mem) : slot->integer;
      info->kind = FRAME_K_TIME;
      info->width = 64;
      info->is_signed = true;
      o->bits = bits_of((uint64_t)v, 64);
   }
   else if (type_is_real(type)) {
      double d = mem ? read_real(mem) : slot->real;
      uint64_t u;
      memcpy(&u, &d, sizeof(u));
      info->kind = FRAME_K_REAL;
      info->width = 64;
      info->is_signed = true;
      o->bits = bits_of(u, 64);
   }
}

static void vhdl_array_info(out_t *o, type_t type, const void *mem, bool image)
{
   frame_info_t *info = &(o->info);

   if (dimension_of(type) != 1)
      return;

   geom_t g;
   if (!array_geom(type, mem, image, &g) || g.count <= 0 || g.count > 4096)
      return;

   type_t elem = type_elem_recur(type);
   if (!type_is_enum(elem))
      return;

   const jit_layout_t *el = layout_of(elem);
   bool is_bit;
   if (is_logic_type(elem, &is_bit)) {
      static const char map[] = "ux01zwlh-";
      char *b = xmalloc(g.count + 1);
      for (int64_t i = 0; i < g.count; i++) {
         const int64_t pos =
            read_scalar(elem, (const char *)g.data + i * el->size);
         b[i] = is_bit ? ('0' + (pos & 1))
            : (pos >= 0 && pos < 9 ? map[pos] : 'x');
      }
      b[g.count] = '\0';
      o->bits = b;
      info->kind = is_bit ? FRAME_K_BIT : FRAME_K_LOGIC;
      info->width = g.count;
   }
   else if (is_character_type(elem)) {
      char *b = xmalloc(g.count * 8 + 1);
      for (int64_t i = 0; i < g.count; i++) {
         const uint8_t c =
            *((const uint8_t *)g.data + i * el->size);
         for (int k = 0; k < 8; k++)
            b[i * 8 + k] = '0' + ((c >> (7 - k)) & 1);
      }
      b[g.count * 8] = '\0';
      o->bits = b;
      info->kind = FRAME_K_STRING;
      info->width = g.count * 8;
   }
}

static void describe_vhdl(const frame_var_t *v, tree_t decl,
                          const jit_scalar_t *args, out_t *o)
{
   type_t type = tree_type(decl);
   if (v->class == FRAME_OBJECT || !layoutable(type)) {
      tb_cat(o->text, "<unsupported>");
      return;
   }

   o->type_name = type_display_name(type);

   const jit_scalar_t *slot = &(args[v->first]);

   switch (v->carrier) {
   case FRAME_VALUE:
      if (type_is_real(type)) {
         tb_printf(o->text, "%.15g", slot->real);
         vhdl_scalar_info(o, type, NULL, slot);
      }
      else if (type_is_enum(type)) {
         fmt_enum(o->text, type, slot->integer);
         vhdl_scalar_info(o, type, NULL, slot);
      }
      else if (type_is_physical(type)) {
         fmt_physical(o->text, type, slot->integer);
         vhdl_scalar_info(o, type, NULL, slot);
      }
      else if (type_is_integer(type)) {
         tb_printf(o->text, "%"PRId64, slot->integer);
         vhdl_scalar_info(o, type, NULL, slot);
      }
      else
         tb_cat(o->text, "<unsupported>");
      break;

   case FRAME_SLOTS:
      if (v->class == FRAME_UARRAY && type_is_array(type)) {
         fmt_value(o->text, type, slot, true, 0);
         vhdl_array_info(o, type, slot, true);
      }
      else
         tb_cat(o->text, "<unsupported>");
      break;

   case FRAME_PTR:
   case FRAME_MEM:
      {
         const void *mem = slot->pointer;
         if (mem == NULL) {
            tb_cat(o->text, "<unavailable>");
            break;
         }

         const bool image = v->class == FRAME_UARRAY;
         fmt_value(o->text, type, mem, image, 0);

         if (type_is_scalar(type))
            vhdl_scalar_info(o, type, mem, NULL);
         else if (type_is_array(type))
            vhdl_array_info(o, type, mem, image);
      }
      break;
   }
}

////////////////////////////////////////////////////////////////////////////////
// Verilog values

static const uint64_t *vec_words(const jit_scalar_t *word, int width,
                                 uint64_t *tmp)
{
   // Up to 64 bits are held in the word itself, more by reference
   if (width <= 64) {
      *tmp = word->integer;
      return tmp;
   }
   else
      return word->pointer;
}

static void describe_vec(const frame_var_t *v, const void *image,
                         out_t *o)
{
   // image is the abits and bbits of a four-state vector or the word of a
   // two-state one
   const int width = v->bits;
   if (width <= 0 || width > 4096)
      return;

   const bool four = v->class == FRAME_VEC4;
   const jit_scalar_t *words = image;

   uint64_t atmp = 0, btmp = 0;
   const uint64_t *a = vec_words(&words[0], width, &atmp);
   const uint64_t *b = four ? vec_words(&words[1], width, &btmp) : NULL;
   if (a == NULL || (four && b == NULL))
      return;

   char *bits = xmalloc(width + 1);
   bool known = true;
   for (int i = 0; i < width; i++) {
      const int pos = width - 1 - i;
      const int av = (a[pos / 64] >> (pos % 64)) & 1;
      const int bv = b ? (b[pos / 64] >> (pos % 64)) & 1 : 0;
      static const char map[] = "01zx";
      bits[i] = map[av | (bv << 1)];
      if (bv)
         known = false;
   }
   bits[width] = '\0';
   o->bits = bits;

   frame_info_t *info = &(o->info);
   info->width = width;
   info->is_signed = v->is_signed;
   if (four)
      info->kind = FRAME_K_LOGIC;
   else if (v->is_signed)
      info->kind = FRAME_K_INTEGER;
   else
      info->kind = FRAME_K_BIT;

   if (known && width <= 64) {
      uint64_t val = a[0];
      if (width < 64)
         val &= (UINT64_C(1) << width) - 1;
      if (v->is_signed) {
         if (width < 64 && (val >> (width - 1)) & 1)
            val |= ~((UINT64_C(1) << width) - 1);
         tb_printf(o->text, "%"PRId64, (int64_t)val);
      }
      else
         tb_printf(o->text, "%"PRIu64, val);
   }
   else if (known) {
      // Wide and known: in hexadecimal
      tb_printf(o->text, "%d'h", width);
      bool started = false;
      for (int pos = (width - 1) / 4 * 4; pos >= 0; pos -= 4) {
         unsigned digit = 0;
         for (int k = 3; k >= 0; k--) {
            const int bit = pos + k;
            digit <<= 1;
            if (bit < width)
               digit |= (a[bit / 64] >> (bit % 64)) & 1;
         }
         if (digit != 0 || started || pos == 0) {
            tb_append(o->text, "0123456789abcdef"[digit]);
            started = true;
         }
      }
   }
   else
      tb_printf(o->text, "%d'b%s", width, bits);
}

static void describe_verilog(const frame_var_t *v, const jit_scalar_t *args,
                             out_t *o)
{
   const jit_scalar_t *slot = &(args[v->first]);
   const void *image = v->carrier == FRAME_MEM ? slot->pointer : slot;
   if (image == NULL) {
      tb_cat(o->text, "<unavailable>");
      return;
   }

   frame_info_t *info = &(o->info);

   switch (v->class) {
   case FRAME_VEC2:
   case FRAME_VEC4:
      describe_vec(v, image, o);
      break;

   case FRAME_REAL:
      {
         double d = v->carrier == FRAME_VALUE ? slot->real : read_real(image);
         uint64_t u;
         memcpy(&u, &d, sizeof(u));
         tb_printf(o->text, "%.15g", d);
         info->kind = FRAME_K_REAL;
         info->width = 64;
         info->is_signed = true;
         o->bits = bits_of(u, 64);
      }
      break;

   case FRAME_INT:
      {
         int64_t value;
         if (v->carrier == FRAME_VALUE)
            value = slot->integer;
         else {
            switch (v->size) {
            case 1: value = v->is_signed ? (int64_t)*(const int8_t *)image
                  : (int64_t)*(const uint8_t *)image; break;
            case 2:
               {
                  int16_t s; uint16_t u;
                  if (v->is_signed) { memcpy(&s, image, 2); value = s; }
                  else { memcpy(&u, image, 2); value = u; }
               }
               break;
            case 4:
               {
                  int32_t s; uint32_t u;
                  if (v->is_signed) { memcpy(&s, image, 4); value = s; }
                  else { memcpy(&u, image, 4); value = u; }
               }
               break;
            default:
               memcpy(&value, image, 8);
               break;
            }
         }
         tb_printf(o->text, "%"PRId64, value);
         info->kind = FRAME_K_INTEGER;
         info->width = MAX(8, (int)v->bits);
         info->is_signed = v->is_signed;
         o->bits = bits_of((uint64_t)value, info->width);
      }
      break;

   case FRAME_UARRAY:
      {
         // A string: the characters and the bounds
         const char *ptr;
         int64_t len;
         memcpy(&ptr, image, sizeof(ptr));
         memcpy(&len, (const char *)image + 16, 8);
         len = ffi_array_length(len);
         if (v->ndims != 1 || len < 0 || len > 4096 || (ptr == NULL && len > 0))
            break;

         tb_append(o->text, '"');
         for (int64_t i = 0; i < MIN(len, 512); i++) {
            const char c = ptr[i];
            if (c >= 32 && c < 127 && c != '"' && c != '\\')
               tb_append(o->text, c);
            else
               tb_printf(o->text, "\\x%02x", (uint8_t)c);
         }
         if (len > 512)
            tb_cat(o->text, "...");
         tb_append(o->text, '"');

         char *b = xmalloc(len * 8 + 1);
         for (int64_t i = 0; i < len; i++) {
            for (int k = 0; k < 8; k++)
               b[i * 8 + k] = '0' + (((uint8_t)ptr[i] >> (7 - k)) & 1);
         }
         b[len * 8] = '\0';
         o->bits = b;
         info->kind = FRAME_K_STRING;
         info->width = len * 8;
      }
      break;

   case FRAME_OBJECT:
      {
         const void *ptr = v->carrier == FRAME_VALUE
            ? slot->pointer : *(const void *const *)image;
         info->kind = FRAME_K_OBJECT;
         info->addr = (uintptr_t)ptr;
         tb_cat(o->text, ptr == NULL ? "null" : "<object>");
      }
      break;

   default:
      tb_cat(o->text, "<unsupported>");
      break;
   }
}

////////////////////////////////////////////////////////////////////////////////
// Entry point

void frame_describe(jit_func_t *f, const jit_scalar_t *args,
                    frame_fn_t fn, void *ctx)
{
   const frame_layout_t *layout = load_acquire(&(f->frame));
   if (layout == NULL || args == NULL || f->object == NULL)
      return;

   tree_t tree = tree_from_object(f->object);
   vlog_node_t vlog = tree == NULL ? vlog_from_object(f->object) : NULL;

   for (unsigned i = 0; i < layout->count; i++) {
      const frame_var_t *v = &(layout->vars[i]);

      out_t o = { .text = tb_new() };
      o.info.name = istr(v->name);
      o.info.is_param = v->is_param;
      o.info.kind = FRAME_K_NONE;

      if (tree != NULL) {
         tree_t decl = find_tree_decl(tree, v->name);
         if (decl == NULL) {
            tb_free(o.text);
            continue;
         }
         o.info.loc = tree_loc(decl);
         describe_vhdl(v, decl, args, &o);
      }
      else if (vlog != NULL) {
         vlog_node_t decl = find_vlog_decl(vlog, v->name);
         if (decl == NULL) {
            tb_free(o.text);
            continue;
         }
         o.info.loc = vlog_loc(decl);
         describe_verilog(v, args, &o);
      }
      else {
         tb_free(o.text);
         continue;
      }

      o.info.bits = o.bits;
      o.info.text = tb_get(o.text);
      o.info.type_name = o.type_name;
      (*fn)(&o.info, ctx);
      free(o.type_name);

      free(o.bits);
      for (unsigned k = 0; k < o.info.nliterals; k++)
         free((char *)o.literals[k]);
      free(o.literals);
      tb_free(o.text);
   }
}
