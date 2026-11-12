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
#include "common.h"
#include "diag.h"
#include "ident.h"
#include "lib.h"
#include "option.h"
#include "phase.h"
#include "scan.h"
#include "scandeps.h"
#include "vlog/vlog-util.h"
#include "tree.h"
#include "type.h"
#include "vlog/vlog-node.h"
#include "vlog/vlog-phase.h"

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>
#include <strings.h>

typedef struct {
   text_buf_t *tb;
   text_buf_t *inst;
   ident_t    *seen;
   unsigned    nseen, maxseen;
} scan_ctx_t;

static void scan_json_string(text_buf_t *tb, const char *str)
{
   tb_append(tb, '"');
   for (const unsigned char *p = (const unsigned char *)str; *p; p++) {
      switch (*p) {
      case '"':  tb_cat(tb, "\\\""); break;
      case '\\': tb_cat(tb, "\\\\"); break;
      case '\n': tb_cat(tb, "\\n"); break;
      case '\r': tb_cat(tb, "\\r"); break;
      case '\t': tb_cat(tb, "\\t"); break;
      default:
         if (*p < 0x20)
            tb_printf(tb, "\\u%04x", *p);
         else
            tb_append(tb, *p);
      }
   }
   tb_append(tb, '"');
}

static void scan_json_list_item(text_buf_t *tb, unsigned *count,
                                const char *str)
{
   if ((*count)++ > 0)
      tb_append(tb, ',');
   scan_json_string(tb, str);
}

static ident_t scan_unit_name(ident_t name)
{
   // Remove an architecture or package body suffix
   if (strchr(istr(name), '\\') == NULL) {
      ident_t base = ident_runtil(name, '-');
      if (base != name)
         return base;
   }

   return name;
}

static ident_t scan_resolve_work(ident_t name)
{
   // The name WORK is an alias for the working library in an instantiation
   ident_t lib = ident_until(name, '.');
   if (lib != name && ident_casecmp(lib, well_known(W_WORK)))
      return ident_prefix(lib_name(lib_work()), ident_from(name, '.'), '.');

   return name;
}

static void scan_instance_cb(tree_t inst, void *context)
{
   scan_ctx_t *ctx = context;

   ident_t name = NULL;
   switch (tree_class(inst)) {
   case C_COMPONENT:
   case C_ENTITY:
   case C_CONFIGURATION:
      if (tree_has_ident2(inst))
         name = scan_resolve_work(scan_unit_name(tree_ident2(inst)));
      break;
   default:
      break;
   }

   if (name == NULL)
      return;

   if (tree_class(inst) != C_COMPONENT)
      lib_scan_note(name);   // Checked when the unit is analysed

   for (unsigned i = 0; i < ctx->nseen; i++) {
      if (ctx->seen[i] == name)
         return;
   }

   if (ctx->nseen == ctx->maxseen) {
      ctx->maxseen = MAX(8, ctx->maxseen * 2);
      ctx->seen = xrealloc_array(ctx->seen, ctx->maxseen, sizeof(ident_t));
   }

   ctx->seen[ctx->nseen++] = name;

   if (ctx->nseen > 1)
      tb_append(ctx->inst, ',');
   scan_json_string(ctx->inst, istr(name));
}


// ---------------------------------------------------------------------
// Symbols and references (--scan --symbols)

typedef struct {
   text_buf_t *syms;
   text_buf_t *refs;
   text_buf_t *extra;  // Fields for the next symbol, see sym_extra_str
   unsigned    nsyms, nrefs;
   const char *file;
   bool        vhdl;
   bool        any_case;   // Compare names without regard to case
   loc_t       uses[256];
   unsigned    nuses;
} sym_ctx_t;

static bool scan_want_symbols = false;

void scan_set_symbols(bool enable)
{
   scan_want_symbols = enable;
}

static bool sym_word_char(char c)
{
   return isalnum((unsigned char)c) || c == '_' || c == '$';
}

// Find NAME as a whole word within the source range of LOC and return
// its 1-based line and column and length in bytes
static bool sym_name_pos(const loc_t *loc, const char *name, bool ci,
                         bool last, unsigned *line, unsigned *col,
                         unsigned *len)
{
   if (loc_invalid_p(loc))
      return false;

   const char *src = loc_get_source(loc);
   if (src == NULL)
      return false;

   const size_t nlen = strlen(name);
   if (nlen == 0)
      return false;

   const unsigned end_line = loc->first_line + loc->line_delta;
   const unsigned end_col = loc->first_column + loc->column_delta;
   const bool single = loc->line_delta == 0;

   bool found = false;
   unsigned cur = loc->first_line;
   const char *ptr = src;
   while (cur <= end_line && cur < loc->first_line + 40) {
      const char *eol = strchr(ptr, '\n');
      size_t linelen = eol ? (size_t)(eol - ptr) : strlen(ptr);
      size_t from = cur == loc->first_line ? loc->first_column : 0;
      size_t to = linelen;
      if (single && end_col + 1 < to)
         to = end_col + 1;
      for (size_t i = from; i + nlen <= to; i++) {
         if (i > 0 && sym_word_char(ptr[i - 1]))
            continue;
         if (i + nlen < linelen && sym_word_char(ptr[i + nlen]))
            continue;
         if (ci ? strncasecmp(ptr + i, name, nlen) : strncmp(ptr + i, name, nlen))
            continue;
         *line = cur;
         *col = i + 1;
         *len = nlen;
         found = true;
         if (!last)
            return true;
      }
      if (eol == NULL)
         break;
      ptr = eol + 1;
      cur++;
   }

   return found;
}

static void sym_json_pos(text_buf_t *tb, const loc_t *loc, const char *file,
                         const char *name, bool ci, bool last)
{
   unsigned line = 0, col = 0, len = 0;
   if (!sym_name_pos(loc, name, ci, last, &line, &col, &len)) {
      if (loc_invalid_p(loc))
         return;
      line = loc->first_line;
      col = loc->first_column + 1;
      len = loc->line_delta == 0 ? loc->column_delta + 1 : 0;
   }
   tb_printf(tb, ",\"line\":%u,\"col\":%u,\"len\":%u", line, col, len);
   const char *f = loc_file_str(loc);
   if (f != NULL && strcmp(f, file) != 0) {
      tb_cat(tb, ",\"file\":");
      scan_json_string(tb, f);
   }
}

static void sym_json_span(text_buf_t *tb, const loc_t *loc)
{
   if (loc_invalid_p(loc))
      return;
   tb_printf(tb, ",\"span\":[%u,%u", loc->first_line, loc->first_column + 1);
   // The end is clamped for very long constructs
   if (loc->line_delta < DELTA_INVALID)
      tb_printf(tb, ",%u", loc->first_line + loc->line_delta);
   tb_append(tb, ']');
}

// The declaration text in front of NAME (the type, direction and so on of a
// Verilog declaration) up to the previous delimiter on the same line
static char *sym_prefix_text(const loc_t *loc, unsigned line, unsigned col)
{
   loc_t at = *loc;
   at.first_line = line;
   at.line_delta = 0;
   at.first_column = 0;
   const char *src = loc_get_source(&at);
   if (src == NULL || col < 2)
      return NULL;

   const char *end = src + col - 1;
   const char *start = end;
   while (start > src && !strchr("(,;", start[-1]))
      start--;
   while (start < end && isspace((unsigned char)*start))
      start++;
   while (end > start && isspace((unsigned char)end[-1]))
      end--;
   if (end <= start || end - start > 80)
      return NULL;

   char *copy = xmalloc(end - start + 1);
   char *out = copy;
   bool space = false;
   for (const char *p = start; p < end; p++) {
      if (isspace((unsigned char)*p))
         space = true;
      else {
         if (space && out > copy)
            *out++ = ' ';
         space = false;
         *out++ = *p;
      }
   }
   *out = '\0';
   return copy;
}


// Extra fields for the next symbol written by sym_add: call these zero or
// more times and then sym_add, which writes and clears them
static void sym_extra_key(sym_ctx_t *sc, const char *key)
{
   tb_printf(sc->extra, ",\"%s\":", key);
}

static void UNUSED sym_extra_str(sym_ctx_t *sc, const char *key,
                                 const char *text)
{
   if (text == NULL || *text == '\0')
      return;

   sym_extra_key(sc, key);
   scan_json_string(sc->extra, text);
}

static void UNUSED sym_extra_int(sym_ctx_t *sc, const char *key,
                                 int64_t value)
{
   sym_extra_key(sc, key);
   tb_printf(sc->extra, "%lld", (long long)value);
}

static void UNUSED sym_extra_raw(sym_ctx_t *sc, const char *key,
                                 const char *json)
{
   sym_extra_key(sc, key);
   tb_cat(sc->extra, json);
}

// The source text covered by LOC with white space collapsed to single
// spaces and cut at MAX characters (zero for no limit); comments (-- in
// VHDL, // in Verilog, /* */ in both) are removed.  Returns NULL if the
// source cannot be read.
static char UNUSED *sym_loc_text(sym_ctx_t *sc, const loc_t *loc,
                                 unsigned max)
{
   if (loc_invalid_p(loc))
      return NULL;

   loc_t at = *loc;
   at.line_delta = 0;
   at.first_column = 0;
   const char *src = loc_get_source(&at);
   if (src == NULL)
      return NULL;

   // Skip to the first line then to the first column
   const char *p = src + loc->first_column;
   // The end column of a location that spans lines may not be known
   const bool clip = loc->column_delta < DELTA_INVALID;
   const unsigned end_line = loc->line_delta < DELTA_INVALID
      ? loc->first_line + loc->line_delta : UINT_MAX;
   const unsigned end_col = loc->first_column + loc->column_delta;

   text_buf_t *tb = tb_new();
   bool space = false, instr = false;
   unsigned line = loc->first_line, col = loc->first_column;
   for (; *p != '\0' && line <= end_line; p++, col++) {
      if (*p == '\n') {
         line++;
         col = (unsigned)-1;
         space = true;
         instr = false;
         continue;
      }
      if (clip && line == end_line && col > end_col)
         break;

      if (!instr) {
         // A comment is white space
         const bool eol = sc->vhdl ? (p[0] == '-' && p[1] == '-')
            : (p[0] == '/' && p[1] == '/');
         if (eol) {
            while (p[1] != '\0' && p[1] != '\n') {
               p++;
               col++;
            }
            space = true;
            continue;
         }
         else if (p[0] == '/' && p[1] == '*') {
            p += 2;
            col += 2;
            while (*p != '\0' && !(p[0] == '*' && p[1] == '/')) {
               if (*p == '\n') {
                  line++;
                  col = (unsigned)-1;
               }
               p++;
               col++;
            }
            if (*p == '\0')
               break;
            p++;
            col++;
            space = true;
            continue;
         }
      }

      if (isspace((unsigned char)*p)) {
         space = true;
         continue;
      }
      if (space && tb_len(tb) > 0)
         tb_append(tb, ' ');
      space = false;
      if (max > 0 && tb_len(tb) >= max) {
         tb_cat(tb, "...");
         break;
      }
      if (sc->vhdl && !instr && p[0] == '\'' && p[1] != '\0' && p[2] == '\'') {
         // Character literal such as '"' or '-'
         tb_append(tb, p[0]);
         tb_append(tb, p[1]);
         p += 2;
         col += 2;
      }
      else if (*p == '"')
         instr = !instr;
      tb_append(tb, *p);
   }

   return tb_claim(tb);
}

static void sym_add(sym_ctx_t *sc, const char *name, const char *kind,
                    const char *detail, const char *container,
                    const loc_t *loc)
{
   if (loc_invalid_p(loc) || *name == '\0') {
      tb_rewind(sc->extra);
      return;
   }

   unsigned l, c, n;
   if (!sym_name_pos(loc, name, sc->vhdl, false, &l, &c, &n)) {
      tb_rewind(sc->extra);
      return;
   }

   char *prefix LOCAL = NULL;
   if (!sc->vhdl && detail == NULL && strcmp(kind, "typedef") != 0
       && strcmp(kind, "class") != 0 && strcmp(kind, "modport") != 0
       && strcmp(kind, "enum-literal") != 0 && strcmp(kind, "instance") != 0) {
      prefix = sym_prefix_text(loc, l, c);
      detail = prefix;
   }

   if (sc->nsyms++ > 0)
      tb_append(sc->syms, ',');

   tb_cat(sc->syms, "{\"name\":");
   scan_json_string(sc->syms, name);
   tb_printf(sc->syms, ",\"kind\":\"%s\"", kind);
   if (detail != NULL && *detail) {
      tb_cat(sc->syms, ",\"detail\":");
      scan_json_string(sc->syms, detail);
   }
   tb_cat(sc->syms, ",\"container\":");
   scan_json_string(sc->syms, container);
   tb_cat(sc->syms, tb_get(sc->extra));
   tb_rewind(sc->extra);
   sym_json_pos(sc->syms, loc, sc->file, name, sc->vhdl, false);
   sym_json_span(sc->syms, loc);
   tb_append(sc->syms, '}');
}

static void sym_add_ref(sym_ctx_t *sc, const char *name, const char *kind,
                        const loc_t *loc, bool last, const loc_t *target,
                        const char *target_name, const char *hint)
{
   if (loc_invalid_p(loc))
      return;

   unsigned l, c, n;
   if (!sym_name_pos(loc, name, sc->vhdl || sc->any_case, last, &l, &c, &n))
      return;

   if (sc->nrefs++ > 0)
      tb_append(sc->refs, ',');

   tb_cat(sc->refs, "{\"name\":");
   scan_json_string(sc->refs, name);
   tb_printf(sc->refs, ",\"kind\":\"%s\"", kind);
   tb_printf(sc->refs, ",\"line\":%u,\"col\":%u,\"len\":%u", l, c, n);
   const char *f = loc_file_str(loc);
   if (f != NULL && strcmp(f, sc->file) != 0) {
      tb_cat(sc->refs, ",\"file\":");
      scan_json_string(sc->refs, f);
   }

   if (target != NULL && target_name != NULL
       && sym_name_pos(target, target_name, sc->vhdl, false, &l, &c, &n)) {
      tb_printf(sc->refs, ",\"to\":[%u,%u]", l, c);
      const char *tf = loc_file_str(target);
      if (tf != NULL && strcmp(tf, sc->file) != 0) {
         tb_cat(sc->refs, ",\"to_file\":");
         scan_json_string(sc->refs, tf);
      }
   }

   if (hint != NULL) {
      tb_cat(sc->refs, ",\"hint\":");
      scan_json_string(sc->refs, hint);
   }

   tb_append(sc->refs, '}');
}

static const char *sym_simple(ident_t id)
{
   // The part after the last dot, for a name qualified with its library
   const char *s = istr(id);
   if (s[0] == '\\')
      return s;
   const char *dot = strrchr(s, '.');
   return dot ? dot + 1 : s;
}

static const char *sym_mode_str(unsigned mode)
{
   switch (mode) {
   case PORT_IN:      return "in";
   case PORT_OUT:     return "out";
   case PORT_INOUT:   return "inout";
   case PORT_BUFFER:  return "buffer";
   case PORT_LINKAGE: return "linkage";
   default:           return "";
   }
}

// Text of a VHDL interface declaration read from the source: the subtype
// indication (without mode) and the default expression, whitespace and
// comments squeezed out.  Constraints and defaults are not kept in a form
// that prints back to source so the text is taken from the file.
static void vhdl_sym_squeeze(text_buf_t *tb, const char *start, const char *end)
{
   bool space = false;
   for (const char *p = start; p < end; p++) {
      if (p[0] == '-' && p + 1 < end && p[1] == '-') {
         while (p < end && *p != '\n')
            p++;
         space = true;
         continue;
      }
      if (isspace((unsigned char)*p))
         space = true;
      else {
         if (space && tb_len(tb) > 0)
            tb_append(tb, ' ');
         space = false;
         tb_append(tb, *p);
      }
   }
}

static bool vhdl_sym_word_at(const char *p, const char *end, const char *word)
{
   const size_t n = strlen(word);
   return p + n <= end && strncasecmp(p, word, n) == 0
      && (p + n == end || !sym_word_char(p[n]))
      && !sym_word_char(p[-1]);
}

static bool vhdl_sym_decl_text(const loc_t *loc, const char *name,
                           text_buf_t *type)
{
   unsigned l, c, n;
   if (!sym_name_pos(loc, name, true, false, &l, &c, &n))
      return false;

   loc_t at = *loc;
   at.first_line = l;
   at.line_delta = 0;
   at.first_column = 0;
   const char *src = loc_get_source(&at);
   if (src == NULL)
      return false;

   const char *p = src + c - 1 + n;
   const char *limit = p + 4000;
   const char *end = p;
   while (end < limit && *end != '\0')
      end++;

   // Skip the rest of the identifier list up to the colon
   while (p < end && *p != ':') {
      if (*p == ';' || *p == ')' || *p == '(')
         return false;
      p++;
   }
   if (p >= end)
      return false;
   p++;

   // The extent of the declaration: to a semicolon or unbalanced
   // parenthesis at depth zero
   const char *tstart = p, *istart = NULL, *stop = end;
   int depth = 0;
   bool instr = false;
   for (const char *q = p; q < end; q++) {
      if (q[0] == '-' && q + 1 < end && q[1] == '-') {
         while (q < end && *q != '\n')
            q++;
         continue;
      }
      if (*q == '"') {
         instr = !instr;
         continue;
      }
      if (instr)
         continue;
      if (*q == '\'' && q + 2 < end && q[2] == '\'') {
         q += 2;
         continue;
      }
      if (*q == '(')
         depth++;
      else if (*q == ')') {
         if (depth-- == 0) {
            stop = q;
            break;
         }
      }
      else if (*q == ';' && depth == 0) {
         stop = q;
         break;
      }
      else if (*q == ':' && depth == 0 && q + 1 < end && q[1] == '='
               && istart == NULL)
         istart = q;
   }

   const char *tend = istart ?: stop;

   // Mode and class words in front of the subtype indication
   static const char *const words[] =
      { "in", "out", "inout", "buffer", "linkage", "signal", "constant",
        "variable" };
   bool more = true;
   while (more) {
      more = false;
      while (tstart < tend && isspace((unsigned char)*tstart))
         tstart++;
      for (size_t i = 0; i < ARRAY_LEN(words); i++) {
         if (vhdl_sym_word_at(tstart, tend, words[i])) {
            tstart += strlen(words[i]);
            more = true;
            break;
         }
      }
   }
   if (tend - tstart > 400)
      return false;
   vhdl_sym_squeeze(type, tstart, tend);
   if (tb_len(type) >= 4) {
      const char *t = tb_get(type);
      const size_t len = tb_len(type);
      if (len > 4 && strcasecmp(t + len - 4, " bus") == 0)
         tb_trim(type, len - 4);
   }

   return true;
}

// Is TYPE made of bits: BIT or STD_ULOGIC or an array of them
static bool vhdl_sym_bits_p(type_t type)
{
   if (type_is_array(type))
      type = type_elem(type);

   if (type_is_array(type) || !type_is_enum(type))
      return false;

   ident_t id = type_ident(type_base_recur(type));
   const char *name = istr(id);
   return strcmp(name, "STD.STANDARD.BIT") == 0
      || strcmp(name, "IEEE.STD_LOGIC_1164.STD_ULOGIC") == 0;
}

// Value of an integer expression made of literals and constants whose
// value is such an expression; the scan does not fold them
static bool vhdl_sym_int(tree_t e, int64_t *value, int depth)
{
   if (depth > 16)
      return false;

   if (folded_int(e, value))
      return true;

   switch (tree_kind(e)) {
   case T_REF:
      if (tree_has_ref(e)) {
         tree_t d = tree_ref(e);
         if (tree_kind(d) == T_CONST_DECL && tree_has_value(d))
            return vhdl_sym_int(tree_value(d), value, depth + 1);
      }
      return false;

   case T_QUALIFIED:
   case T_TYPE_CONV:
      return vhdl_sym_int(tree_value(e), value, depth + 1);

   case T_FCALL:
      {
         if (!tree_has_ident(e))
            return false;
         const char *op = istr(tree_ident(e));
         const unsigned n = tree_params(e);
         int64_t a, b = 0;
         if (n < 1 || n > 2 || !vhdl_sym_int(tree_value(tree_param(e, 0)),
                                             &a, depth + 1))
            return false;
         if (n == 2 && !vhdl_sym_int(tree_value(tree_param(e, 1)), &b,
                                     depth + 1))
            return false;
         if (n == 2 && strcmp(op, "\"+\"") == 0)
            *value = a + b;
         else if (n == 2 && strcmp(op, "\"-\"") == 0)
            *value = a - b;
         else if (n == 2 && strcmp(op, "\"*\"") == 0)
            *value = a * b;
         else if (n == 1 && strcmp(op, "\"-\"") == 0)
            *value = -a;
         else
            return false;
         return true;
      }

   default:
      return false;
   }
}

static bool vhdl_sym_length(tree_t r, int64_t *length)
{
   if (tree_kind(r) != T_RANGE)
      return false;

   const range_kind_t dir = tree_subkind(r);
   if (dir != RANGE_TO && dir != RANGE_DOWNTO)
      return false;

   int64_t left, right;
   if (!vhdl_sym_int(tree_left(r), &left, 0)
       || !vhdl_sym_int(tree_right(r), &right, 0))
      return false;

   const int64_t n = (dir == RANGE_TO ? right - left : left - right) + 1;
   *length = MAX(n, 0);
   return true;
}

// Number of bits when the type is made of bits and every bound is known
static bool vhdl_sym_width(type_t type, int64_t *width)
{
   if (!vhdl_sym_bits_p(type))
      return false;

   if (!type_is_array(type)) {
      *width = 1;
      return true;
   }

   while (type_kind(type) == T_SUBTYPE && !type_has_constraint(type))
      type = type_base(type);

   if (type_kind(type) != T_SUBTYPE)
      return false;

   tree_t c = type_constraint(type);
   if (tree_subkind(c) != C_INDEX)
      return false;

   int64_t total = 1;
   const int nr = tree_ranges(c);
   for (int i = 0; i < nr; i++) {
      int64_t len;
      if (!vhdl_sym_length(tree_range(c, i), &len))
         return false;
      total *= len;
   }

   *width = total;
   return *width >= 1;
}

static const char *vhdl_sym_direction(unsigned mode)
{
   switch (mode) {
   case PORT_IN:      return "input";
   case PORT_OUT:     return "output";
   case PORT_INOUT:   return "inout";
   case PORT_BUFFER:  return "buffer";
   case PORT_LINKAGE: return "linkage";
   default:           return NULL;
   }
}

// The parameter of the for-generate being listed: it is declared as a
// generic but has no type indication of its own in the source
static tree_t vhdl_sym_gen_param = NULL;

// Fields of a port or generic taken from the source text and the tree
static void vhdl_sym_interface(sym_ctx_t *sc, tree_t d, const char *name)
{
   if (tree_kind(d) == T_PORT_DECL)
      sym_extra_str(sc, "direction", vhdl_sym_direction(tree_subkind(d)));
   else {
      const char *class = NULL;
      switch (tree_class(d)) {
      case C_TYPE:      class = "type"; break;
      case C_PACKAGE:   class = "package"; break;
      case C_FUNCTION:
      case C_PROCEDURE: class = "subprogram"; break;
      default: break;
      }
      if (class != NULL) {
         sym_extra_str(sc, "class", class);
         return;
      }
   }

   LOCAL_TEXT_BUF type = tb_new();
   if (vhdl_sym_decl_text(tree_loc(d), name, type))
      sym_extra_str(sc, "type", tb_get(type));

   int64_t width;
   if (tree_kind(d) == T_PORT_DECL && tree_has_type(d)
       && vhdl_sym_width(tree_type(d), &width))
      sym_extra_int(sc, "width", width);

   if (tree_kind(d) == T_GENERIC_DECL && tree_has_value(d)) {
      char *text LOCAL = sym_loc_text(sc, tree_loc(tree_value(d)), 200);
      sym_extra_str(sc, "default", text);
   }
}


static void vhdl_sym_scope(sym_ctx_t *sc, tree_t t, const char *path);
static void vhdl_sym_stmt(sym_ctx_t *sc, tree_t s, const char *path);

static void vhdl_sym_decl(sym_ctx_t *sc, tree_t d, const char *path)
{
   const char *kind = NULL, *detail = NULL;
   char *tmp LOCAL = NULL;
   bool scope = false;

   switch (tree_kind(d)) {
   case T_PORT_DECL:
      kind = "port";
      tmp = xasprintf("%s %s", sym_mode_str(tree_subkind(d)),
                      type_pp(tree_type(d)));
      detail = tmp;
      break;
   case T_PARAM_DECL:
      kind = "param";
      tmp = xasprintf("%s %s", sym_mode_str(tree_subkind(d)),
                      type_pp(tree_type(d)));
      detail = tmp;
      break;
   case T_GENERIC_DECL:
      kind = "generic";
      if (tree_has_type(d))
         detail = type_pp(tree_type(d));
      break;
   case T_SIGNAL_DECL:
      kind = "signal";
      detail = type_pp(tree_type(d));
      break;
   case T_VAR_DECL:
      kind = "variable";
      detail = type_pp(tree_type(d));
      break;
   case T_CONST_DECL:
      kind = "constant";
      detail = type_pp(tree_type(d));
      break;
   case T_FILE_DECL:
      kind = "file";
      detail = type_pp(tree_type(d));
      break;
   case T_ALIAS:
      kind = "alias";
      if (tree_has_type(d))
         detail = type_pp(tree_type(d));
      break;
   case T_ATTR_DECL:
      kind = "attribute";
      detail = type_pp(tree_type(d));
      break;
   case T_TYPE_DECL:
   case T_SUBTYPE_DECL:
      kind = tree_kind(d) == T_TYPE_DECL ? "type" : "subtype";
      scope = true;
      break;
   case T_FUNC_DECL:
   case T_FUNC_BODY:
      if (tree_subkind(d) != S_USER)
         return;
      kind = "function";
      scope = true;
      detail = type_pp(tree_type(d));
      break;
   case T_PROC_DECL:
   case T_PROC_BODY:
      if (tree_subkind(d) != S_USER)
         return;
      kind = "procedure";
      scope = true;
      detail = type_pp(tree_type(d));
      break;
   case T_COMPONENT:
      kind = "component";
      scope = true;
      break;
   case T_PROT_DECL:
   case T_PROT_BODY:
      kind = "protected";
      scope = true;
      break;
   case T_PACKAGE:
   case T_PACK_INST:
      kind = "package";
      scope = true;
      break;
   case T_ENUM_LIT:
      kind = "enum-literal";
      break;
   case T_FIELD_DECL:
      kind = "field";
      detail = type_pp(tree_type(d));
      break;
   case T_VIEW_DECL:
      kind = "view";
      break;
   default:
      return;
   }

   const char *name = sym_simple(tree_ident(d));

   if ((tree_kind(d) == T_PORT_DECL || tree_kind(d) == T_GENERIC_DECL)
       && d != vhdl_sym_gen_param)
      vhdl_sym_interface(sc, d, name);

   sym_add(sc, name, kind, detail, path, tree_loc(d));

   if (!scope)
      return;

   char *sub LOCAL = xasprintf("%s.%s", path, name);
   if (tree_kind(d) == T_TYPE_DECL || tree_kind(d) == T_SUBTYPE_DECL) {
      type_t type = tree_type(d);
      switch (type_kind(type)) {
      case T_RECORD:
         for (unsigned i = 0; i < type_fields(type); i++)
            vhdl_sym_decl(sc, type_field(type, i), sub);
         break;
      case T_ENUM:
         for (unsigned i = 0; i < type_enum_literals(type); i++)
            vhdl_sym_decl(sc, type_enum_literal(type, i), sub);
         break;
      default:
         break;
      }
   }
   else if (tree_kind(d) != T_PACK_INST)
      vhdl_sym_scope(sc, d, sub);
}

// The generation scheme of a generate statement as written: "for I in 0
// to N - 1", "if W > 4" or "case N"
static bool vhdl_sym_scheme(const loc_t *loc, const char *name,
                            text_buf_t *tb)
{
   unsigned l, c, n;
   if (!sym_name_pos(loc, name, true, false, &l, &c, &n))
      return false;

   loc_t at = *loc;
   at.first_line = l;
   at.line_delta = 0;
   at.first_column = 0;
   const char *src = loc_get_source(&at);
   if (src == NULL)
      return false;

   const char *p = src + c - 1 + n;
   const char *end = p;
   while (end < p + 400 && *end != '\0')
      end++;

   while (p < end && (isspace((unsigned char)*p) || *p == ':'))
      p++;

   for (const char *q = p; q < end; q++) {
      if (q[0] == '-' && q + 1 < end && q[1] == '-') {
         while (q < end && *q != '\n')
            q++;
         continue;
      }
      if (vhdl_sym_word_at(q, end, "generate")) {
         // An alternative label is not part of the condition
         if (vhdl_sym_word_at(p, q, "if")) {
            const char *a = p + 2;
            while (a < q && isspace((unsigned char)*a))
               a++;
            const char *e = a;
            while (e < q && sym_word_char(*e))
               e++;
            const char *f = e;
            while (f < q && isspace((unsigned char)*f))
               f++;
            if (e > a && f < q && *f == ':' && (f + 1 == q || f[1] != '=')) {
               tb_cat(tb, "if");
               p = f + 1;
            }
         }
         vhdl_sym_squeeze(tb, p, q);
         return tb_len(tb) > 0;
      }
   }

   return false;
}

static void vhdl_sym_branch(sym_ctx_t *sc, tree_t b, const char *gen,
                            const char *path, const char *parent)
{
   // An alternative label replaces the generate label in the instance path
   const char *sub = path;
   char *tmp LOCAL = NULL;
   if (tree_has_ident(b)) {
      const char *name = sym_simple(tree_ident(b));
      if (strcasecmp(name, gen) != 0) {
         sym_extra_str(sc, "gen", tree_kind(b) == T_COND_STMT ? "if" : "case");
         sym_add(sc, name, "generate", "alternative", parent, tree_loc(b));
         tmp = xasprintf("%s.%s", parent, name);
         sub = tmp;
      }
   }

   for (unsigned j = 0; j < tree_decls(b); j++)
      vhdl_sym_decl(sc, tree_decl(b, j), sub);
   for (unsigned j = 0; j < tree_stmts(b); j++)
      vhdl_sym_stmt(sc, tree_stmt(b, j), sub);
}

// The generic map of an instance as written, for the generic_map field
static void vhdl_sym_generic_map(sym_ctx_t *sc, tree_t s)
{
   const int n = tree_genmaps(s);
   if (n == 0)
      return;

   LOCAL_TEXT_BUF tb = tb_new();
   tb_append(tb, '[');
   for (int i = 0; i < n; i++) {
      tree_t m = tree_genmap(s, i);
      if (!tree_has_value(m) || tree_kind(tree_value(m)) == T_OPEN)
         continue;

      // Defaults added by the parser for what is not mapped have the
      // location of the whole instance
      const loc_t *vloc = tree_loc(tree_value(m));
      if (vloc->first_line == tree_loc(s)->first_line
          && vloc->first_column == tree_loc(s)->first_column)
         continue;

      char *value LOCAL = sym_loc_text(sc, vloc, 200);
      if (value == NULL)
         continue;

      if (tb_len(tb) > 1)
         tb_append(tb, ',');
      tb_append(tb, '{');
      if (tree_subkind(m) == P_NAMED) {
         char *name LOCAL = sym_loc_text(sc, tree_loc(tree_name(m)), 100);
         if (name != NULL) {
            tb_cat(tb, "\"name\":");
            scan_json_string(tb, name);
            tb_append(tb, ',');
         }
      }
      tb_cat(tb, "\"value\":");
      scan_json_string(tb, value);
      tb_append(tb, '}');
   }
   tb_append(tb, ']');

   if (tb_len(tb) > 2)
      sym_extra_raw(sc, "generic_map", tb_get(tb));
}

static void vhdl_sym_stmt(sym_ctx_t *sc, tree_t s, const char *path)
{
   const char *kind = NULL;
   switch (tree_kind(s)) {
   case T_PROCESS:      kind = "process"; break;
   case T_BLOCK:        kind = "block"; break;
   case T_FOR_GENERATE:
   case T_IF_GENERATE:
   case T_CASE_GENERATE: kind = "generate"; break;
   case T_INSTANCE:     kind = "instance"; break;
   default:
      return;
   }

   if (!tree_has_ident(s))
      return;

   const char *name = sym_simple(tree_ident(s));
   // Implicit labels given to unlabelled statements
   if (strncmp(name, "line_", 5) == 0 || name[0] == '_')
      return;

   const char *detail = NULL;
   char *tmp LOCAL = NULL;
   if (tree_kind(s) == T_INSTANCE && tree_has_ident2(s)) {
      ident_t unit = scan_unit_name(tree_ident2(s));
      tmp = xasprintf("%s", sym_simple(unit));
      detail = tmp;
      sym_extra_str(sc, "unit", istr(unit));
      vhdl_sym_generic_map(sc, s);
   }

   LOCAL_TEXT_BUF scheme = tb_new();
   switch (tree_kind(s)) {
   case T_FOR_GENERATE:
   case T_IF_GENERATE:
   case T_CASE_GENERATE:
      sym_extra_str(sc, "gen", tree_kind(s) == T_FOR_GENERATE ? "for"
                    : tree_kind(s) == T_IF_GENERATE ? "if" : "case");
      if (tree_kind(s) == T_FOR_GENERATE
          && vhdl_sym_scheme(tree_loc(s), name, scheme)) {
         // Without the keyword
         const char *text = tb_get(scheme);
         if (strncasecmp(text, "for ", 4) == 0)
            text += 4;
         sym_extra_str(sc, "range", text);
      }
      break;
   default:
      break;
   }

   sym_add(sc, name, kind, detail, path, tree_loc(s));

   if (tree_kind(s) != T_INSTANCE) {
      char *sub LOCAL = xasprintf("%s.%s", path, name);
      vhdl_sym_scope(sc, s, sub);
   }
}

static void vhdl_sym_scope(sym_ctx_t *sc, tree_t t, const char *path)
{
   bool generics = false, ports = false, decls = false, stmts = false;
   switch (tree_kind(t)) {
   case T_ENTITY:
   case T_BLOCK:
      generics = ports = decls = stmts = true;
      break;
   case T_ARCH:
      decls = stmts = true;
      break;
   case T_PACKAGE:
      generics = decls = true;
      break;
   case T_PACK_BODY:
   case T_PROT_DECL:
   case T_PROT_BODY:
      decls = true;
      break;
   case T_PROCESS:
   case T_FOR_GENERATE:
      decls = stmts = true;
      break;
   case T_FUNC_BODY:
   case T_PROC_BODY:
      ports = decls = true;
      break;
   case T_FUNC_DECL:
   case T_PROC_DECL:
      ports = true;
      break;
   case T_COMPONENT:
      generics = ports = true;
      break;
   case T_IF_GENERATE:
   case T_CASE_GENERATE:
      {
         const char *dot = strrchr(path, '.');
         char *parent LOCAL = dot ? xstrndup(path, dot - path) : xstrdup(path);
         const char *gen = dot ? dot + 1 : path;
         const bool is_if = tree_kind(t) == T_IF_GENERATE;
         const unsigned n = is_if ? tree_conds(t) : tree_stmts(t);
         for (unsigned i = 0; i < n; i++)
            vhdl_sym_branch(sc, is_if ? tree_cond(t, i) : tree_stmt(t, i),
                            gen, path, parent);
      }
      return;
   default:
      return;
   }

   if (generics)
      for (unsigned i = 0; i < tree_generics(t); i++)
         vhdl_sym_decl(sc, tree_generic(t, i), path);
   if (ports)
      for (unsigned i = 0; i < tree_ports(t); i++)
         vhdl_sym_decl(sc, tree_port(t, i), path);
   if (decls) {
      for (unsigned i = 0; i < tree_decls(t); i++) {
         if (tree_kind(t) == T_FOR_GENERATE && i == 0)
            vhdl_sym_gen_param = tree_decl(t, i);
         vhdl_sym_decl(sc, tree_decl(t, i), path);
      }
      vhdl_sym_gen_param = NULL;
   }
   if (stmts)
      for (unsigned i = 0; i < tree_stmts(t); i++)
         vhdl_sym_stmt(sc, tree_stmt(t, i), path);
}

static bool vhdl_sym_decl_p(tree_t d)
{
   switch (tree_kind(d)) {
   case T_PORT_DECL:
   case T_PARAM_DECL:
   case T_GENERIC_DECL:
   case T_SIGNAL_DECL:
   case T_VAR_DECL:
   case T_CONST_DECL:
   case T_FILE_DECL:
   case T_ALIAS:
   case T_ATTR_DECL:
   case T_TYPE_DECL:
   case T_SUBTYPE_DECL:
   case T_COMPONENT:
   case T_ENUM_LIT:
   case T_FIELD_DECL:
   case T_PROT_DECL:
   case T_PROT_BODY:
      return true;
   case T_FUNC_DECL:
   case T_FUNC_BODY:
   case T_PROC_DECL:
   case T_PROC_BODY:
      return tree_subkind(d) == S_USER;
   default:
      return false;
   }
}

static void vhdl_ref_cb(tree_t t, void *context)
{
   sym_ctx_t *sc = context;

   switch (tree_kind(t)) {
   case T_REF:
      {
         if (!tree_has_ident(t))
            return;
         const char *name = sym_simple(tree_ident(t));
         const loc_t *target = NULL;
         const char *tname = NULL;
         if (tree_has_ref(t)) {
            tree_t d = tree_ref(t);
            if (vhdl_sym_decl_p(d) && tree_has_ident(d)) {
               target = tree_loc(d);
               tname = sym_simple(tree_ident(d));
            }
         }
         sym_add_ref(sc, name, "name", tree_loc(t), false, target, tname, NULL);
      }
      break;

   case T_FCALL:
   case T_PCALL:
   case T_PROT_FCALL:
   case T_PROT_PCALL:
      {
         if (!tree_has_ident(t))
            return;
         const char *name = sym_simple(tree_ident(t));
         if (!isalpha((unsigned char)name[0]))
            return;   // Operator
         const loc_t *target = NULL;
         const char *tname = NULL;
         if (tree_has_ref(t)) {
            tree_t d = tree_ref(t);
            if (vhdl_sym_decl_p(d)) {
               target = tree_loc(d);
               tname = sym_simple(tree_ident(d));
            }
         }
         sym_add_ref(sc, name, "call", tree_loc(t), false, target, tname, NULL);
      }
      break;

   case T_RECORD_REF:
      {
         if (!tree_has_ref(t))
            return;
         tree_t d = tree_ref(t);
         if (tree_kind(d) != T_FIELD_DECL)
            return;
         const char *name = sym_simple(tree_ident(d));
         sym_add_ref(sc, name, "field", tree_loc(t), true, tree_loc(d), name,
                     NULL);
      }
      break;

   case T_INSTANCE:
      {
         if (!tree_has_ident2(t))
            return;
         ident_t unit = scan_resolve_work(scan_unit_name(tree_ident2(t)));
         const char *simple = sym_simple(unit);
         // Skip the label which comes first
         loc_t loc = *tree_loc(t);
         const char *kind = tree_class(t) == C_COMPONENT ? "component"
            : "entity";
         sym_add_ref(sc, simple, kind, &loc, tree_class(t) == C_COMPONENT,
                     NULL, NULL, istr(unit));
      }
      break;

   case T_USE:
      {
         // The context clause is shared by the units which follow it
         const loc_t *loc = tree_loc(t);
         for (unsigned i = 0; i < sc->nuses; i++) {
            if (sc->uses[i].first_line == loc->first_line
                && sc->uses[i].first_column == loc->first_column
                && sc->uses[i].file_ref == loc->file_ref)
               return;
         }
         if (sc->nuses < ARRAY_LEN(sc->uses))
            sc->uses[sc->nuses++] = *loc;
         ident_t unit = tree_ident(t);
         ident_t u = scan_resolve_work(unit);
         const char *simple = sym_simple(u);
         sym_add_ref(sc, simple, "use", tree_loc(t), false, NULL, NULL, istr(u));
      }
      break;

   default:
      break;
   }
}

static void vlog_sym_scope(sym_ctx_t *sc, vlog_node_t v, const char *path);

static void vlog_sym_decl(sym_ctx_t *sc, vlog_node_t d, const char *path)
{
   const char *kind = NULL, *detail = NULL;
   bool scope = false;

   switch (vlog_kind(d)) {
   case V_PORT_DECL:
      kind = "port";
      switch (vlog_subkind(d)) {
      case V_PORT_INPUT:  detail = "input"; break;
      case V_PORT_OUTPUT: detail = "output"; break;
      case V_PORT_INOUT:  detail = "inout"; break;
      default: break;
      }
      detail = NULL;   // Taken from the source text
      break;
   case V_TF_PORT_DECL:
      kind = "param";
      break;
   case V_PARAM_DECL:
      kind = "parameter";
      break;
   case V_LOCALPARAM:
      kind = "localparam";
      break;
   case V_NET_DECL:
      kind = "net";
      break;
   case V_VAR_DECL:
      kind = "variable";
      break;
   case V_GENVAR_DECL:
      kind = "genvar";
      break;
   case V_TYPE_DECL:
      kind = "typedef";
      break;
   case V_FUNC_DECL:
      kind = "function";
      scope = true;
      break;
   case V_TASK_DECL:
      kind = "task";
      scope = true;
      break;
   case V_CLASS_DECL:
      kind = "class";
      scope = true;
      break;
   case V_MODPORT:
      kind = "modport";
      break;
   case V_ENUM_NAME:
      kind = "enum-literal";
      break;
   case V_ENUM_DECL:
      // Members of an anonymous enumeration are declared in the scope
      for (unsigned i = 0; i < vlog_decls(d); i++)
         vlog_sym_decl(sc, vlog_decl(d, i), path);
      return;
   case V_CONSTRUCTOR:
      kind = "function";
      scope = true;
      break;
   case V_IMPORT_DECL:
      // Not reached by vlog_visit, so the reference is made here
      if (vlog_has_ref(d) && vlog_has_ident(vlog_ref(d))) {
         const char *pname = sym_simple(vlog_ident(vlog_ref(d)));
         char *hint LOCAL = xasprintf("?%s", pname);
         // The location may be only the part after the package name
         loc_t line = *vlog_loc(d);
         line.first_column = 0;
         line.line_delta = 0;
         line.column_delta = DELTA_INVALID;
         sc->any_case = true;   // Package names are folded to upper case
         sym_add_ref(sc, pname, "import", &line, false, NULL, NULL, hint);
         sc->any_case = false;
      }
      return;
   default:
      return;
   }

   if (!vlog_has_ident(d))
      return;

   const char *name = sym_simple(vlog_ident(d));
   sym_add(sc, name, kind, detail, path, vlog_loc(d));

   if (scope) {
      char *sub LOCAL = xasprintf("%s.%s", path, name);
      vlog_sym_scope(sc, d, sub);
   }
}

static void vlog_sym_scope(sym_ctx_t *sc, vlog_node_t v, const char *path)
{
   bool ports = false, decls = false, stmts = false;
   switch (vlog_kind(v)) {
   case V_MODULE:
   case V_PROGRAM:
      ports = decls = stmts = true;
      break;
   case V_PACKAGE:
   case V_CLASS_DECL:
      decls = true;
      break;
   case V_FUNC_DECL:
   case V_TASK_DECL:
   case V_CONSTRUCTOR:
      ports = decls = true;
      break;
   default:
      return;
   }

   if (ports && vlog_kind(v) != V_CONSTRUCTOR)
      for (unsigned i = 0; i < vlog_ports(v); i++)
         vlog_sym_decl(sc, vlog_port(v, i), path);
   if (decls)
      for (unsigned i = 0; i < vlog_decls(v); i++)
         vlog_sym_decl(sc, vlog_decl(v, i), path);
   if (stmts) {
      for (unsigned i = 0; i < vlog_stmts(v); i++) {
         vlog_node_t s = vlog_stmt(v, i);
         if (vlog_kind(s) != V_INST_LIST)
            continue;
         for (unsigned j = 0; j < vlog_stmts(s); j++) {
            vlog_node_t inst = vlog_stmt(s, j);
            if (vlog_kind(inst) == V_MOD_INST && vlog_has_ident(inst))
               sym_add(sc, sym_simple(vlog_ident(inst)), "instance",
                       vlog_has_ident(s) ? istr(vlog_ident(s)) : NULL, path,
                       vlog_loc(inst));
         }
      }
   }
}

static void vlog_ref_cb(vlog_node_t v, void *context)
{
   sym_ctx_t *sc = context;

   switch (vlog_kind(v)) {
   case V_REF:
   case V_USER_FCALL:
   case V_USER_TCALL:
   case V_MEMBER_REF:
      {
         if (!vlog_has_ident(v))
            return;
         const char *name = sym_simple(vlog_ident(v));
         const loc_t *target = NULL;
         const char *tname = NULL;
         if (vlog_has_ref(v)) {
            vlog_node_t d = vlog_ref(v);
            if (vlog_has_ident(d)) {
               target = vlog_loc(d);
               tname = sym_simple(vlog_ident(d));
            }
         }
         sym_add_ref(sc, name,
                     vlog_kind(v) == V_REF ? "name"
                     : vlog_kind(v) == V_MEMBER_REF ? "field" : "call",
                     vlog_loc(v), vlog_kind(v) == V_MEMBER_REF, target, tname,
                     NULL);
      }
      break;

   case V_INST_LIST:
      {
         if (!vlog_has_ident(v) || vlog_stmts(v) == 0)
            return;
         char *hint LOCAL = xasprintf("?%s", istr(vlog_ident(v)));
         sym_add_ref(sc, istr(vlog_ident(v)), "module", vlog_loc(v), false,
                     NULL, NULL, hint);
      }
      break;

   default:
      break;
   }
}

static void scan_vhdl_unit(tree_t unit, text_buf_t *units, unsigned *nunits,
                           ident_t **provided, unsigned *nprovided,
                           sym_ctx_t *sc)
{
   const char *kind = NULL;
   bool has_ports = false;
   ident_t name = tree_ident(unit);

   switch (tree_kind(unit)) {
   case T_ENTITY:
      kind = "entity";
      has_ports = tree_ports(unit) > 0;
      break;
   case T_ARCH:
      kind = "architecture";
      lib_scan_note(scan_unit_name(name));
      break;
   case T_PACKAGE:
   case T_PACK_INST:
      kind = "package";
      break;
   case T_PACK_BODY:
      kind = "package-body";
      lib_scan_note(scan_unit_name(name));
      break;
   case T_CONFIGURATION:
      kind = "configuration";
      if (tree_has_ident2(unit)) {
         ident_t entity = scan_unit_name(tree_ident2(unit));
         if (ident_runtil(entity, '.') == entity)
            entity = ident_prefix(ident_until(name, '.'), entity, '.');
         lib_scan_note(entity);
      }
      break;
   case T_CONTEXT:
      kind = "context";
      break;
   case T_VUNIT:
      kind = "vunit";
      break;
   default:
      return;
   }

   scan_ctx_t ctx = { .tb = units, .inst = tb_new() };
   tree_visit_only(unit, scan_instance_cb, &ctx, T_INSTANCE);

   if ((*nunits)++ > 0)
      tb_append(units, ',');

   tb_cat(units, "{\"name\":");
   scan_json_string(units, istr(name));
   tb_printf(units, ",\"kind\":\"%s\"", kind);
   if (tree_kind(unit) == T_ENTITY)
      tb_printf(units, ",\"ports\":%s", has_ports ? "true" : "false");
   tb_printf(units, ",\"instantiates\":[%s]", tb_get(ctx.inst));

   if (sc != NULL) {
      const char *simple = sym_simple(name);
      if (tree_kind(unit) == T_ARCH) {
         const char *dash = strrchr(simple, '-');
         if (dash != NULL)
            simple = dash + 1;
      }
      else if (tree_kind(unit) == T_PACK_BODY)
         simple = sym_simple(scan_unit_name(name));
      sym_json_pos(units, tree_loc(unit), sc->file, simple, true, false);
      sym_json_span(units, tree_loc(unit));
      vhdl_sym_scope(sc, unit, istr(name));
      tree_visit(unit, vhdl_ref_cb, sc);
   }

   tb_append(units, '}');

   tb_free(ctx.inst);
   free(ctx.seen);

   // Units which are not primary (and so cannot be required) are not
   // recorded as provided
   ident_t prov = NULL;
   switch (tree_kind(unit)) {
   case T_ENTITY:
   case T_PACKAGE:
   case T_PACK_INST:
   case T_CONFIGURATION:
   case T_CONTEXT:
   case T_VUNIT:
      prov = name;
      break;
   default:
      break;
   }

   if (prov != NULL) {
      *provided = xrealloc_array(*provided, *nprovided + 1, sizeof(ident_t));
      (*provided)[(*nprovided)++] = prov;
   }
}

static void scan_inst_list_cb(vlog_node_t v, void *context)
{
   scan_ctx_t *ctx = context;

   if (vlog_stmts(v) == 0 || !vlog_has_ident(v))
      return;   // Not an instantiation

   ident_t name = vlog_ident(v);
   if (name == error_marker())
      return;

   for (unsigned i = 0; i < ctx->nseen; i++) {
      if (ctx->seen[i] == name)
         return;
   }

   if (ctx->nseen == ctx->maxseen) {
      ctx->maxseen = MAX(8, ctx->maxseen * 2);
      ctx->seen = xrealloc_array(ctx->seen, ctx->maxseen, sizeof(ident_t));
   }

   ctx->seen[ctx->nseen++] = name;

   if (ctx->nseen > 1)
      tb_append(ctx->inst, ',');
   scan_json_string(ctx->inst, istr(name));
}

static void scan_vlog_unit(vlog_node_t unit, text_buf_t *units,
                           unsigned *nunits, ident_t **provided,
                           unsigned *nprovided, sym_ctx_t *sc)
{
   const char *kind = NULL;
   bool has_ports = false;

   switch (vlog_kind(unit)) {
   case V_MODULE:
      if (vlog_flags(unit) & VLOG_F_INTERFACE)
         kind = "interface";
      else if (vlog_flags(unit) & VLOG_F_CHECKER)
         kind = "checker";
      else
         kind = "module";
      has_ports = vlog_ports(unit) > 0;
      break;
   case V_PROGRAM:
      kind = "program";
      has_ports = vlog_ports(unit) > 0;
      break;
   case V_PACKAGE:
      kind = "package";
      break;
   case V_PRIMITIVE:
      kind = "primitive";
      break;
   case V_CONFIG:
      kind = "configuration";
      break;
   default:
      return;
   }

   ident_t name = vlog_ident(unit);
   ident_t simple = vlog_has_ident2(unit) ? vlog_ident2(unit)
      : ident_rfrom(name, '.');

   if (simple == error_marker())
      return;   // Syntax error in the name: there is no unit to report

   scan_ctx_t ctx = { .tb = units, .inst = tb_new() };
   vlog_visit_only(unit, scan_inst_list_cb, &ctx, V_INST_LIST);

   if ((*nunits)++ > 0)
      tb_append(units, ',');

   tb_cat(units, "{\"name\":");
   scan_json_string(units, istr(name));
   tb_printf(units, ",\"kind\":\"%s\"", kind);
   if (vlog_kind(unit) == V_MODULE || vlog_kind(unit) == V_PROGRAM)
      tb_printf(units, ",\"ports\":%s", has_ports ? "true" : "false");
   tb_printf(units, ",\"instantiates\":[%s]", tb_get(ctx.inst));

   if (sc != NULL) {
      sym_json_pos(units, vlog_loc(unit), sc->file, istr(simple), false, false);
      sym_json_span(units, vlog_loc(unit));
      vlog_sym_scope(sc, unit, istr(name));
      vlog_visit(unit, vlog_ref_cb, sc);
   }

   tb_append(units, '}');

   tb_free(ctx.inst);
   free(ctx.seen);

   // A reference to the unit by name from the same file is satisfied
   char *ref LOCAL = xasprintf("?%s", istr(simple));
   *provided = xrealloc_array(*provided, *nprovided + 2, sizeof(ident_t));
   (*provided)[(*nprovided)++] = name;
   (*provided)[(*nprovided)++] = ident_new(ref);
}

unsigned scan_file_deps(const char *file, text_buf_t *json)
{
   // Parse the file as analysis would but without any libraries present:
   // references to missing units are recorded instead of being errors

   lib_t prev = lib_work();
   lib_t work = lib_tmp(istr(lib_name(prev)));   // Fresh for each file
   lib_set_work(work);

   lib_set_scan_mode(true);
   diag_set_scan_filter(true);
   const unsigned old_limit = set_error_limit(UINT_MAX);

   // Generated sources such as large ROM initialisers and packages with
   // many covergroups (each expanded to a class) can exceed the default
   // arena size and a scan must not die on those: the address space is
   // only committed when used
   const size_t old_arena = opt_get_size(OPT_ARENA_SIZE);
   opt_set_size(OPT_ARENA_SIZE, MAX(old_arena, (size_t)1 << 28));

   lib_scan_clear();

   const unsigned base_errors = diag_scan_errors();

   LOCAL_TEXT_BUF units = tb_new();
   unsigned nunits = 0, nprovided = 0;
   ident_t *provided = NULL;
   const char *language = "vhdl";

   sym_ctx_t sctx = { .file = file };
   sym_ctx_t *sc = NULL;
   if (scan_want_symbols) {
      sc = &sctx;
      sctx.syms = tb_new();
      sctx.refs = tb_new();
      sctx.extra = tb_new();
   }

   input_from_file(file);

   switch (source_kind()) {
   case SOURCE_VHDL:
      {
         tree_t unit;
         while ((unit = parse())) {
            lib_put(work, unit);
            sctx.vhdl = true;
            scan_vhdl_unit(unit, units, &nunits, &provided, &nprovided, sc);
         }
      }
      break;

   case SOURCE_VERILOG:
      {
         language = "verilog";

         // Each file is scanned as if it were alone unless the files are
         // to be treated as one compilation unit
         if (!opt_get_int(OPT_SINGLE_UNIT))
            vlog_preprocess_reset();

         LOCAL_TEXT_BUF tb = tb_new();
         input_record_includes(true);
         vlog_preprocess(tb, true);
         input_record_includes(false);

         file_ref_t file_ref = loc_file_ref(file, NULL);
         input_from_buffer(tb_get(tb), tb_len(tb), file_ref, SOURCE_VERILOG);

         vlog_node_t unit;
         while ((unit = vlog_parse())) {
            if (is_top_level(unit))
               lib_put_vlog(work, unit);
            sctx.vhdl = false;
            scan_vlog_unit(unit, units, &nunits, &provided, &nprovided, sc);
         }
      }
      break;

   case SOURCE_SDF:
      language = "sdf";
      break;
   }

   tb_cat(json, "{\"file\":");
   scan_json_string(json, file);
   tb_printf(json, ",\"language\":\"%s\",\"units\":[%s],\"requires\":[",
             language, tb_get(units));

   unsigned count, nreq = 0;
   const ident_t *refs = lib_scan_refs(&count);
   ident_t *done = xmalloc_array(MAX(count, 1), sizeof(ident_t));
   for (unsigned i = 0; i < count; i++) {
      ident_t ref = scan_unit_name(refs[i]);
      if (ref == well_known(W_STD_STANDARD))
         continue;

      bool skip = false;
      for (unsigned j = 0; j < nprovided && !skip; j++)
         skip = provided[j] == ref;
      for (unsigned j = 0; j < nreq && !skip; j++)
         skip = done[j] == ref;

      if (!skip) {
         done[nreq] = ref;
         scan_json_list_item(json, &nreq, istr(ref));
      }
   }
   free(done);
   free(provided);

   tb_cat(json, "],\"includes\":[");

   unsigned nincl = 0;
   if (!strcmp(language, "verilog")) {
      unsigned n;
      const char *const *files = input_included_files(&n);
      for (unsigned i = 0; i < n; i++)
         scan_json_list_item(json, &nincl, files[i]);
   }

   tb_cat(json, "]");

   if (sc != NULL) {
      tb_printf(json, ",\"symbols\":[%s],\"refs\":[%s]", tb_get(sctx.syms),
                tb_get(sctx.refs));
      tb_free(sctx.syms);
      tb_free(sctx.refs);
      tb_free(sctx.extra);
   }

   const unsigned errors = diag_scan_errors() - base_errors;
   if (errors > 0)
      tb_printf(json, ",\"errors\":%u", errors);

   tb_cat(json, "}\n");

   opt_set_size(OPT_ARENA_SIZE, old_arena);
   set_error_limit(old_limit);
   diag_set_scan_filter(false);
   lib_set_scan_mode(false);

   lib_set_work(prev);
   lib_free(work);

   return errors;
}
