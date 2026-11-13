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
#include "diag.h"
#include "fsm.h"
#include "ident.h"
#include "vlog/vlog-node.h"
#include "vlog/vlog-number.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

// A variable or port that may hold a state
typedef A(ident_t) idents_t;
typedef A(vlog_node_t) nodes_t;

typedef struct {
   vlog_node_t decl;
   ident_t     name;
   vlog_node_t enum_decl;   // Resolved enumeration or NULL
   char       *type_text;   // The type as written
   bool        named;       // Unresolved type name: may be an enumeration
   bool        keyword;     // Declared with a built-in or other plain type
   bool        inner;       // Declared in the scope being scanned
   bool        classified;  // The type text has been looked at
} sobj_t;

typedef struct {
   vlog_node_t node;        // The always construct
   vlog_node_t ctrl;        // Its event control or NULL
   vlog_node_t body;        // The statement under the event control
   bool        clocked;
} proc_t;

typedef struct {
   const char *text;        // Owned
   loc_t       loc;
   bool        neg;
} guard_t;

typedef A(guard_t) guards_t;

typedef struct {
   fsm_out_t     *f;
   const sobj_t  *state;
   const sobj_t  *next;
   vlog_node_t    unit;       // The module
   vlog_node_t    scope;      // The module or generate block
   bool           consts;     // States are constants
   const char    *macro_args; // The register is a macro: its arguments
   ident_t        target;     // The name assigned in the case
   vlog_node_t    cs;         // The case statement
   vlog_node_t    cs_proc;    // The process holding the case
   vlog_node_t    reg_proc;   // The process with the clock edge
   const char    *file;
   idents_t       names;      // The states when the type is not resolved
   bool           have_enum;
   nodes_t        items_named;
   unsigned       nassigns;   // Literal targets found
   unsigned       ntrans;
   idents_t       seen;       // States seen
   bool           has_default_note;
   vlog_node_t    skip;       // The reset branch
} machine_t;

static bool is_stmt_container(vlog_node_t s)
{
   switch (vlog_kind(s)) {
   case V_SEQ_BLOCK: case V_TIMING: case V_FOREVER: case V_REPEAT:
   case V_WHILE: case V_DO_WHILE: case V_FOR_LOOP: case V_WAIT:
   case V_COND: case V_CASE: case V_CASE_ITEM: case V_FORK:
      return true;
   default:
      return false;
   }
}

typedef void (*stmt_fn_t)(vlog_node_t, void *);

static void walk_stmts(vlog_node_t s, stmt_fn_t fn, void *ctx)
{
   if (s == NULL)
      return;

   fn(s, ctx);

   if (vlog_kind(s) == V_IF) {
      for (unsigned i = 0; i < vlog_conds(s); i++)
         walk_stmts(vlog_cond(s, i), fn, ctx);
   }
   else if (is_stmt_container(s)) {
      for (unsigned i = 0; i < vlog_stmts(s); i++)
         walk_stmts(vlog_stmt(s, i), fn, ctx);
   }
}

static bool is_assign(vlog_node_t s);

static void assign_cb(vlog_node_t s, void *arg)
{
   nodes_t *list = arg;
   if (is_assign(s))
      APUSH(*list, s);
}

static void walk_collect_assigns(vlog_node_t body, void *list)
{
   walk_stmts(body, assign_cb, list);
}

static bool is_assign(vlog_node_t s)
{
   return vlog_kind(s) == V_NBASSIGN || vlog_kind(s) == V_BASSIGN;
}

// The name of a simple reference
static ident_t ref_name(vlog_node_t v)
{
   if (v != NULL && vlog_kind(v) == V_REF && vlog_has_ident(v))
      return vlog_ident(v);
   return NULL;
}

static ident_t assign_target(vlog_node_t s)
{
   return ref_name(vlog_target(s));
}

static const char *loc_file(const loc_t *loc)
{
   return loc_invalid_p(loc) ? NULL : loc_file_str(loc);
}

static bool same_file(const loc_t *loc, const char *file)
{
   const char *f = loc_file(loc);
   return f != NULL && file != NULL && !strcmp(f, file);
}

// The location of an expression widened over the brackets which the parser
// leaves out, such as the braces of a concatenation or the end of a select
static loc_t expr_loc(vlog_node_t v)
{
   loc_t loc = *vlog_loc(v);
   if (loc_invalid_p(&loc) || loc.line_delta != 0
       || loc.column_delta >= DELTA_INVALID)
      return loc;

   loc_t at = loc;
   at.first_column = 0;
   const char *line = loc_get_source(&at);
   if (line == NULL)
      return loc;

   const size_t len = strcspn(line, "\n");
   long first = loc.first_column, last = loc.first_column + loc.column_delta;
   if ((size_t)first >= len || (size_t)last >= len)
      return loc;

   // Unmatched brackets in the text
   int nopen = 0, need = 0;
   for (long i = first; i <= last; i++) {
      const char c = line[i];
      if (strchr("([{", c))
         nopen++;
      else if (strchr(")]}", c)) {
         if (nopen > 0)
            nopen--;
         else
            need++;
      }
   }

   // Closing brackets without an opening one: take it from before
   for (long i = first - 1; need > 0 && i >= 0; i--) {
      const char c = line[i];
      if (strchr(")]}", c))
         need++;
      else if (strchr("([{", c) && --need == 0)
         first = i;
   }

   // Opening brackets without a closing one: take them from after
   int missing = nopen;
   for (long i = last + 1; missing > 0 && (size_t)i < len; i++) {
      const char c = line[i];
      if (strchr("([{", c))
         missing++;
      else if (strchr(")]}", c) && --missing == 0)
         last = i;
   }

   if (need == 0 && missing == 0 && last < 4000 && first >= 0)
      return get_loc(loc.first_line, first, loc.first_line, last,
                     loc.file_ref);

   return loc;
}

// Text of an expression built from the tree, for when the location does not
// match the source, as after a macro on the same line.  False if the
// expression has a form that is not handled.
static bool unparse_in(text_buf_t *tb, vlog_node_t v, unsigned parent);

static bool unparse(text_buf_t *tb, vlog_node_t v)
{
   return unparse_in(tb, v, 0);
}

// PARENT is the kind and subkind of the operator around the expression, which
// needs no brackets when it is the same
static bool unparse_in(text_buf_t *tb, vlog_node_t v, unsigned parent)
{
   if (v == NULL)
      return false;

   switch (vlog_kind(v)) {
   case V_REF:
      if (!vlog_has_ident(v))
         return false;
      tb_cat(tb, istr(vlog_ident(v)));
      return true;
   case V_NUMBER:
      number_print(vlog_number(v), tb);
      return true;
   case V_UNARY:
      {
         static const char *const ops[] = {
            [V_UNARY_BITNEG] = "~", [V_UNARY_NOT] = "!", [V_UNARY_NEG] = "-",
            [V_UNARY_IDENTITY] = "+", [V_UNARY_AND] = "&", [V_UNARY_OR] = "|",
            [V_UNARY_XOR] = "^", [V_UNARY_NAND] = "~&", [V_UNARY_NOR] = "~|",
            [V_UNARY_XNOR] = "~^",
         };
         const unsigned k = vlog_subkind(v);
         if (k >= ARRAY_LEN(ops) || ops[k] == NULL)
            return false;
         tb_cat(tb, ops[k]);
         vlog_node_t o = vlog_value(v);
         const bool simple = o != NULL && (vlog_kind(o) == V_REF
                                           || vlog_kind(o) == V_NUMBER
                                           || vlog_kind(o) == V_BIT_SELECT);
         if (!simple)
            tb_append(tb, '(');
         const bool ok = unparse_in(tb, o, 0);
         if (!simple)
            tb_append(tb, ')');
         return ok;
      }
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
            [V_BINARY_WILD_EQ] = "==?", [V_BINARY_WILD_NEQ] = "!=?",
         };
         const unsigned k = vlog_subkind(v);
         if (k >= ARRAY_LEN(ops) || ops[k] == NULL)
            return false;
         const unsigned me = (V_BINARY << 8) | (k + 1);
         const bool paren = parent != 0 && parent != me;
         if (paren)
            tb_append(tb, '(');
         const bool ok = unparse_in(tb, vlog_left(v), me);
         tb_printf(tb, " %s ", ops[k]);
         const bool ok2 = unparse_in(tb, vlog_right(v), me);
         if (paren)
            tb_append(tb, ')');
         return ok && ok2;
      }
   case V_COND_EXPR:
      {
         const unsigned me = V_COND_EXPR << 8;
         const bool paren = parent != 0;
         if (paren)
            tb_append(tb, '(');
         bool ok = unparse_in(tb, vlog_value(v), me);
         tb_cat(tb, " ? ");
         ok &= unparse_in(tb, vlog_left(v), me);
         tb_cat(tb, " : ");
         ok &= unparse_in(tb, vlog_right(v), me);
         if (paren)
            tb_append(tb, ')');
         return ok;
      }
   case V_BIT_SELECT:
      {
         bool ok = unparse_in(tb, vlog_value(v), 0);
         for (unsigned i = 0; i < vlog_params(v); i++) {
            tb_append(tb, '[');
            ok &= unparse_in(tb, vlog_param(v, i), 0);
            tb_append(tb, ']');
         }
         return ok;
      }
   default:
      return false;
   }
}

// Is the single line location inside the line of source
static bool loc_in_source(const loc_t *loc)
{
   if (loc_invalid_p(loc) || loc->line_delta != 0
       || loc->column_delta >= DELTA_INVALID)
      return true;

   loc_t at = *loc;
   at.first_column = 0;
   const char *line = loc_get_source(&at);
   if (line == NULL)
      return false;

   return loc->first_column + loc->column_delta < strcspn(line, "\n");
}

// Source text of an expression or NULL
static char *expr_text(vlog_node_t v)
{
   if (v == NULL)
      return NULL;

   if (!loc_invalid_p(vlog_loc(v)) && loc_in_source(vlog_loc(v))) {
      loc_t loc = expr_loc(v);
      char *t = fsm_source_text(&loc);
      if (t != NULL && *t != '\0')
         return t;
      free(t);
   }

   LOCAL_TEXT_BUF tb = tb_new();
   return unparse(tb, v) ? tb_claim(tb) : NULL;
}

static bool is_keyword_type(const char *w, size_t n)
{
   static const char *const kw[] = {
      "logic", "reg", "bit", "wire", "integer", "int", "byte", "shortint",
      "longint", "time", "real", "shortreal", "realtime", "string", "signed",
      "unsigned", "var", "event", "enum", "struct", "union", "tri", "supply0",
      "supply1", "wand", "wor", "uwire", "tri0", "tri1", "triand", "trior",
      "trireg", "chandle", "virtual", "packed", "type",
   };
   for (size_t i = 0; i < ARRAY_LEN(kw); i++) {
      if (strlen(kw[i]) == n && !strncmp(kw[i], w, n))
         return true;
   }
   return false;
}

static bool is_dir_word(const char *w, size_t n)
{
   static const char *const kw[] = {
      "input", "output", "inout", "ref", "const", "static", "automatic",
   };
   for (size_t i = 0; i < ARRAY_LEN(kw); i++) {
      if (strlen(kw[i]) == n && !strncmp(kw[i], w, n))
         return true;
   }
   return false;
}

// The text of the type written before the name of a declaration, such as
// "pk::st_e" for "pk::st_e a, b;" or "logic [3:0]" for a port, with any
// port direction removed.  The source must be on one line.
static char *decl_type_text(const loc_t *loc)
{
   if (loc_invalid_p(loc))
      return NULL;

   loc_t at = *loc;
   at.line_delta = 0;
   at.first_column = 0;
   const char *src = loc_get_source(&at);
   const unsigned col = loc->first_column;
   if (src == NULL || col == 0)
      return NULL;

   const char *name = src + col;
   const char *start = name;
   int depth = 0;
   while (start > src) {
      const char c = start[-1];
      if (strchr(")]}", c))
         depth++;
      else if (strchr("([{", c)) {
         if (depth == 0)
            break;
         depth--;
      }
      else if (depth == 0 && c == ';')
         break;
      start--;
   }

   // Drop the first declarator of "type a, b" and the ones in between
   const char *end = name;
   const char *comma = NULL;
   depth = 0;
   for (const char *p = start; p < name; p++) {
      if (strchr("([{", *p))
         depth++;
      else if (strchr(")]}", *p))
         depth--;
      else if (depth == 0 && *p == ',') {
         comma = p;
         break;
      }
   }
   if (comma != NULL)
      end = comma;

   if (comma != NULL) {
      // The first part is "type name [dims] [= value]": remove the name
      const char *stop = end;
      depth = 0;
      for (const char *p = start; p < end; p++) {
         if (strchr("([{", *p))
            depth++;
         else if (strchr(")]}", *p))
            depth--;
         else if (depth == 0 && *p == '=') {
            stop = p;
            break;
         }
      }
      end = stop;
      while (end > start && isspace((unsigned char)end[-1]))
         end--;
      while (end > start && end[-1] == ']') {
         int d = 0;
         while (end > start) {
            end--;
            if (*end == ']')
               d++;
            else if (*end == '[' && --d == 0)
               break;
         }
         while (end > start && isspace((unsigned char)end[-1]))
            end--;
      }
      while (end > start && (isalnum((unsigned char)end[-1])
                             || end[-1] == '_' || end[-1] == '$'))
         end--;
   }

   // Remove leading direction words
   for (;;) {
      while (start < end && isspace((unsigned char)*start))
         start++;
      const char *w = start;
      while (w < end && (isalnum((unsigned char)*w) || *w == '_'))
         w++;
      if (w > start && is_dir_word(start, w - start))
         start = w;
      else
         break;
   }

   while (end > start && isspace((unsigned char)end[-1]))
      end--;
   if (end <= start || end - start > 120)
      return NULL;

   char *copy = xmalloc(end - start + 1), *out = copy;
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

// Is TEXT a possibly package-qualified type name
static bool is_type_name_text(const char *text)
{
   if (text == NULL || !(isalpha((unsigned char)*text) || *text == '_'))
      return false;

   for (const char *p = text; *p; p++) {
      if (!(isalnum((unsigned char)*p) || *p == '_' || *p == '$'
            || *p == ':'))
         return false;
   }

   const char *w = text;
   while (*w && *w != ':')
      w++;
   return !is_keyword_type(text, w - text);
}

static vlog_node_t enum_of_type(vlog_node_t t)
{
   for (int depth = 0; t != NULL && depth < 16; depth++) {
      switch (vlog_kind(t)) {
      case V_ENUM_DECL:
         return t;
      case V_TYPE_DECL:
         if (!vlog_has_type(t))
            return NULL;
         t = vlog_type(t);
         break;
      case V_DATA_TYPE:
         if (vlog_has_type(t)) {
            t = vlog_type(t);
            break;
         }
         return NULL;
      default:
         return NULL;
      }
   }
   return NULL;
}

static const char *type_name_of(vlog_node_t t)
{
   if (t != NULL && vlog_kind(t) == V_TYPE_DECL && vlog_has_ident(t))
      return istr(vlog_ident(t));
   return "enum";
}

typedef A(sobj_t) sobjs_t;

static void collect_objects(vlog_node_t unit, sobjs_t *out)
{
   for (unsigned i = 0; i < vlog_decls(unit); i++) {
      vlog_node_t d = vlog_decl(unit, i);
      const vlog_kind_t k = vlog_kind(d);
      if (k != V_VAR_DECL && k != V_PORT_DECL)
         continue;
      else if (!vlog_has_ident(d) || !vlog_has_type(d))
         continue;
      else if (vlog_ranges(d) > 0)
         continue;   // Unpacked array

      sobj_t o = { .decl = d, .name = vlog_ident(d) };
      vlog_node_t t = vlog_type(d);
      o.enum_decl = enum_of_type(t);
      if (o.enum_decl != NULL) {
         o.type_text = xstrdup(type_name_of(t));
         o.classified = true;
      }

      o.inner = true;

      // A port redeclared as a variable is one object, at the variable
      bool replaced = false;
      if (k == V_VAR_DECL) {
         for (unsigned j = 0; j < out->count; j++) {
            if (out->items[j].name == o.name
                && vlog_kind(out->items[j].decl) == V_PORT_DECL) {
               free(out->items[j].type_text);
               out->items[j] = o;
               replaced = true;
               break;
            }
         }
      }

      if (!replaced)
         APUSH(*out, o);
   }
}

static const sobj_t *find_object(const sobjs_t *objs, ident_t name)
{
   if (name == NULL)
      return NULL;

   for (unsigned i = 0; i < objs->count; i++) {
      if (objs->items[i].name == name)
         return &objs->items[i];
   }
   return NULL;
}

// Look at the type as written, which is slow, only for objects which are
// the subject of a case
static bool is_candidate(const sobj_t *co)
{
   sobj_t *o = (sobj_t *)co;
   if (!o->classified) {
      o->classified = true;
      vlog_node_t t = vlog_type(o->decl);
      o->type_text = decl_type_text(vlog_loc(o->decl));
      o->named = vlog_kind(t) == V_DATA_TYPE
         && is_type_name_text(o->type_text);
      o->keyword = !o->named;
   }

   return o->enum_decl != NULL || o->named || o->keyword;
}

// The declaration of a parameter or local parameter in the scope or module
static vlog_node_t const_decl(const machine_t *m, ident_t name)
{
   vlog_node_t scopes[2] = { m->scope, m->unit };
   for (int k = 0; k < 2; k++) {
      if (k == 1 && scopes[1] == scopes[0])
         break;
      for (unsigned i = 0; i < vlog_decls(scopes[k]); i++) {
         vlog_node_t d = vlog_decl(scopes[k], i);
         if ((vlog_kind(d) == V_LOCALPARAM || vlog_kind(d) == V_PARAM_DECL)
             && vlog_has_ident(d) && vlog_ident(d) == name)
            return d;
      }
   }
   return NULL;
}

static void classify_proc(vlog_node_t a, proc_t *p)
{
   memset(p, 0, sizeof(*p));
   p->node = a;

   if (vlog_stmts(a) == 0)
      return;

   vlog_node_t s = vlog_stmt(a, 0);
   if (vlog_kind(s) == V_TIMING && vlog_has_value(s)
       && vlog_kind(vlog_value(s)) == V_EVENT_CONTROL) {
      p->ctrl = vlog_value(s);
      p->body = vlog_stmts(s) > 0 ? vlog_stmt(s, 0) : NULL;
   }
   else
      p->body = s;

   if (p->ctrl != NULL) {
      for (unsigned i = 0; i < vlog_params(p->ctrl); i++) {
         vlog_node_t e = vlog_param(p->ctrl, i);
         if (vlog_kind(e) == V_EVENT
             && (vlog_subkind(e) == V_EVENT_POSEDGE
                 || vlog_subkind(e) == V_EVENT_NEGEDGE))
            p->clocked = true;
      }
   }
}

static bool ident_in(const idents_t *list, ident_t id)
{
   for (unsigned i = 0; i < list->count; i++) {
      if (list->items[i] == id)
         return true;
   }
   return false;
}

// Is NAME a state of the machine
static bool is_state_name(const machine_t *m, ident_t name)
{
   if (name == NULL)
      return false;

   if (m->consts)
      return ident_in(&m->names, name) || const_decl(m, name) != NULL;

   if (m->have_enum) {
      vlog_node_t e = m->state->enum_decl;
      for (unsigned i = 0; i < vlog_decls(e); i++) {
         vlog_node_t l = vlog_decl(e, i);
         if (vlog_has_ident(l) && vlog_ident(l) == name)
            return true;
      }
      return false;
   }

   return ident_in(&m->names, name);
}

static void note_state(machine_t *m, ident_t name)
{
   if (!ident_in(&m->seen, name))
      APUSH(m->seen, name);
}

// ---------------------------------------------------------------------
// Case analysis

typedef struct {
   ident_t       target;
   const machine_t *m;
   bool          found;
} lit_ctx_t;

// The name of the state a value stands for or NULL.  When the type is not
// visible a name which resolves to nothing is taken as a literal of it.
static ident_t literal_name(const machine_t *m, vlog_node_t v)
{
   ident_t n = ref_name(v);
   if (n == NULL || n == m->state->name
       || (m->next != NULL && n == m->next->name))
      return NULL;
   else if (is_state_name(m, n))
      return n;
   else if (!m->have_enum && !m->consts && !vlog_has_ref(v))
      return n;
   return NULL;
}

static bool value_is_literal(const machine_t *m, vlog_node_t v)
{
   if (v != NULL && vlog_kind(v) == V_COND_EXPR) {
      return value_is_literal(m, vlog_left(v))
         || value_is_literal(m, vlog_right(v));
   }

   return literal_name(m, v) != NULL;
}

static void lit_cb(vlog_node_t s, void *arg)
{
   lit_ctx_t *c = arg;
   if (is_assign(s) && assign_target(s) == c->target
       && value_is_literal(c->m, vlog_value(s)))
      c->found = true;
}

static bool item_assigns_literal(const machine_t *m, vlog_node_t cs)
{
   lit_ctx_t c = { .target = m->target, .m = m };
   for (unsigned i = 0; i < vlog_stmts(cs); i++)
      walk_stmts(vlog_stmt(cs, i), lit_cb, &c);
   return c.found;
}

// Add the names used as labels of a case to the states
static void collect_label_names(machine_t *m, vlog_node_t cs)
{
   for (unsigned i = 0; i < vlog_stmts(cs); i++) {
      vlog_node_t it = vlog_stmt(cs, i);
      if (vlog_kind(it) != V_CASE_ITEM)
         continue;
      for (unsigned j = 0; j < vlog_params(it); j++) {
         ident_t n = ref_name(vlog_param(it, j));
         if (n != NULL && !ident_in(&m->names, n))
            APUSH(m->names, n);
      }
   }
}

// ---------------------------------------------------------------------
// Emission

static void guards_push(guards_t *g, vlog_node_t cond, bool neg)
{
   char *t = expr_text(cond);
   if (t == NULL)
      t = xstrdup("?");

   guard_t gd = { .text = t, .loc = expr_loc(cond), .neg = neg };
   APUSH(*g, gd);
}

static void guards_free(guards_t *g, unsigned keep)
{
   for (unsigned i = keep; i < g->count; i++)
      free((char *)g->items[i].text);
   g->count = keep;
}

typedef A(const char *) froms_t;

static void emit_trans(machine_t *m, const char *kind, const loc_t *assign,
                       const char *to, const char *expr,
                       const froms_t *from, const guards_t *g)
{
   fsm_trans_begin(m->f, kind, assign, to, expr);
   for (unsigned i = 0; i < from->count; i++)
      fsm_trans_from(m->f, from->items[i]);
   for (unsigned i = 0; i < g->count; i++)
      fsm_trans_guard(m->f, g->items[i].text, &g->items[i].loc,
                      g->items[i].neg);
   fsm_trans_end(m->f);
   m->ntrans++;
}

// Emit the transitions for the value assigned to the target
static void emit_value(machine_t *m, const char *kind, vlog_node_t s,
                       vlog_node_t value, const froms_t *from, guards_t *g)
{
   if (value == NULL)
      return;

   if (vlog_kind(value) == V_COND_EXPR) {
      const unsigned keep = g->count;
      guards_push(g, vlog_value(value), false);
      emit_value(m, kind, s, vlog_left(value), from, g);
      g->items[keep].neg = true;
      emit_value(m, kind, s, vlog_right(value), from, g);
      guards_free(g, keep);
      return;
   }

   ident_t n = ref_name(value);
   if (n != NULL && (n == m->state->name
                     || (m->next != NULL && n == m->next->name)))
      return;   // Hold

   if ((n = literal_name(m, value)) != NULL) {
      note_state(m, n);
      emit_trans(m, kind, vlog_loc(s), istr(n), NULL, from, g);
      return;
   }

   char *t = expr_text(value);
   emit_trans(m, kind, vlog_loc(s), NULL, t ? t : "?", from, g);
   free(t);
}

// The text of a case label list as a condition on the subject
static char *labels_text(vlog_node_t subject, vlog_node_t item)
{
   char *subj = expr_text(subject);
   if (subj == NULL)
      return NULL;

   LOCAL_TEXT_BUF tb = tb_new();
   for (unsigned i = 0; i < vlog_params(item); i++) {
      char *l = expr_text(vlog_param(item, i));
      if (l == NULL)
         continue;
      if (tb_len(tb) > 0)
         tb_cat(tb, " || ");
      tb_printf(tb, "%s == %s", subj, l);
      free(l);
   }
   free(subj);
   return tb_len(tb) > 0 ? tb_claim(tb) : NULL;
}

static void emit_stmt(machine_t *m, const char *kind, vlog_node_t s,
                      const froms_t *from, guards_t *g);

static void emit_stmts_of(machine_t *m, const char *kind, vlog_node_t s,
                          const froms_t *from, guards_t *g)
{
   for (unsigned i = 0; i < vlog_stmts(s); i++)
      emit_stmt(m, kind, vlog_stmt(s, i), from, g);
}

static void emit_stmt(machine_t *m, const char *kind, vlog_node_t s,
                      const froms_t *from, guards_t *g)
{
   if (s == NULL)
      return;

   switch (vlog_kind(s)) {
   case V_NBASSIGN:
   case V_BASSIGN:
      if (assign_target(s) == m->target)
         emit_value(m, kind, s, vlog_value(s), from, g);
      break;

   case V_IF:
      {
         const unsigned keep = g->count;
         for (unsigned i = 0; i < vlog_conds(s); i++) {
            vlog_node_t c = vlog_cond(s, i);
            const unsigned here = g->count;
            if (vlog_has_value(c))
               guards_push(g, vlog_value(c), false);
            emit_stmts_of(m, kind, c, from, g);
            guards_free(g, here);
            // Later branches see this condition negated
            if (vlog_has_value(c))
               guards_push(g, vlog_value(c), true);
         }
         guards_free(g, keep);
      }
      break;

   case V_CASE:
      {
         const unsigned keep = g->count;
         for (unsigned i = 0; i < vlog_stmts(s); i++) {
            vlog_node_t it = vlog_stmt(s, i);
            if (vlog_kind(it) != V_CASE_ITEM)
               continue;
            const unsigned here = g->count;
            if (vlog_params(it) > 0) {
               char *t = labels_text(vlog_value(s), it);
               if (t != NULL) {
                  guard_t gd = { .text = t, .loc = *vlog_loc(it),
                                 .neg = false };
                  APUSH(*g, gd);
               }
            }
            emit_stmts_of(m, kind, it, from, g);
            guards_free(g, here);
            if (vlog_params(it) > 0) {
               char *t = labels_text(vlog_value(s), it);
               if (t != NULL) {
                  guard_t gd = { .text = t, .loc = *vlog_loc(it),
                                 .neg = true };
                  APUSH(*g, gd);
               }
            }
         }
         guards_free(g, keep);
      }
      break;

   default:
      if (is_stmt_container(s))
         emit_stmts_of(m, kind, s, from, g);
      break;
   }
}

// The extent of a case item: to the end of its last statement, which for a
// block is found from the statements inside as the block records only where
// it begins
static void extent_cb(vlog_node_t s, void *arg)
{
   loc_t *loc = arg;
   const loc_t *l = vlog_loc(s);
   if (loc_invalid_p(l) || l->line_delta >= DELTA_INVALID
       || l->column_delta >= DELTA_INVALID)
      return;

   const unsigned end = l->first_line + l->line_delta;
   const unsigned cur = loc->first_line + loc->line_delta;
   if (end >= cur)
      *loc = get_loc(loc->first_line, loc->first_column, end,
                     end == l->first_line
                     ? l->first_column + l->column_delta : 0,
                     loc->file_ref);
}

static loc_t item_branch_loc(vlog_node_t it)
{
   loc_t loc = *vlog_loc(it);
   for (unsigned i = 0; i < vlog_stmts(it); i++)
      walk_stmts(vlog_stmt(it, i), extent_cb, &loc);
   return loc;
}

// ---------------------------------------------------------------------
// Outside the case

typedef struct {
   machine_t *m;
   vlog_node_t cs;
   bool        seen;
} outside_ctx_t;

static void outside_walk(machine_t *m, vlog_node_t s, bool *seen,
                         guards_t *g, bool uncond, bool nested)
{
   if (s == NULL)
      return;

   if (s == m->cs) {
      *seen = true;
      return;
   }
   else if (s == m->skip)
      return;

   froms_t star = { 0 };
   APUSH(star, "*");

   switch (vlog_kind(s)) {
   case V_NBASSIGN:
   case V_BASSIGN:
      if (assign_target(s) == m->target && value_is_literal(m, vlog_value(s))
          && !(nested && false)) {
         const char *kind = "any";
         if (!*seen && uncond && vlog_kind(s) == V_BASSIGN)
            m->has_default_note = true;
         emit_value(m, kind, s, vlog_value(s), &star, g);
      }
      break;

   case V_IF:
      {
         const unsigned keep = g->count;
         for (unsigned i = 0; i < vlog_conds(s); i++) {
            vlog_node_t c = vlog_cond(s, i);
            const unsigned here = g->count;
            if (vlog_has_value(c))
               guards_push(g, vlog_value(c), false);
            if (c != m->skip) {
               for (unsigned j = 0; j < vlog_stmts(c); j++)
                  outside_walk(m, vlog_stmt(c, j), seen, g, false, true);
            }
            guards_free(g, here);
            if (vlog_has_value(c))
               guards_push(g, vlog_value(c), true);
         }
         guards_free(g, keep);
      }
      break;

   default:
      if (is_stmt_container(s) && vlog_kind(s) != V_CASE) {
         for (unsigned i = 0; i < vlog_stmts(s); i++)
            outside_walk(m, vlog_stmt(s, i), seen, g,
                         uncond && vlog_kind(s) == V_SEQ_BLOCK, true);
      }
      break;
   }

   ACLEAR(star);
}

// ---------------------------------------------------------------------
// Reset

// The first statement of a process body that is an if
static vlog_node_t top_if(vlog_node_t body)
{
   if (body == NULL)
      return NULL;

   if (vlog_kind(body) == V_IF)
      return body;

   if (vlog_kind(body) == V_SEQ_BLOCK) {
      for (unsigned i = 0; i < vlog_stmts(body); i++) {
         vlog_node_t s = vlog_stmt(body, i);
         if (vlog_kind(s) == V_IF)
            return s;
         else if (vlog_kind(s) != V_EMPTY)
            return NULL;
      }
   }

   return NULL;
}

typedef struct {
   machine_t  *m;
   guards_t   *g;
   vlog_node_t reset_if;
   ident_t     initial;
} reset_ctx_t;

static void reset_assign_cb(vlog_node_t s, void *arg)
{
   reset_ctx_t *r = arg;
   machine_t *m = r->m;
   if (is_assign(s) && assign_target(s) == m->state->name
       && value_is_literal(m, vlog_value(s))) {
      froms_t star = { 0 };
      APUSH(star, "*");
      emit_value(m, "reset", s, vlog_value(s), &star, r->g);
      ACLEAR(star);
      ident_t n = ref_name(vlog_value(s));
      if (n != NULL && r->initial == NULL)
         r->initial = n;
   }
}

// Find the reset branch of the register process: the first branch of an
// if at the top of the clocked process which assigns a state literal while
// a later branch holds the case or the next-state assignment
static vlog_node_t find_reset(const machine_t *m, const proc_t *reg)
{
   vlog_node_t top = top_if(reg->body);
   if (top == NULL || vlog_conds(top) < 2)
      return NULL;

   vlog_node_t first = vlog_cond(top, 0);
   if (!vlog_has_value(first))
      return NULL;

   lit_ctx_t lc = { .target = m->state->name, .m = m };
   for (unsigned i = 0; i < vlog_stmts(first); i++)
      walk_stmts(vlog_stmt(first, i), lit_cb, &lc);

   return lc.found ? first : NULL;
}

static ident_t emit_reset(machine_t *m, vlog_node_t first)
{
   if (first == NULL)
      return NULL;

   guards_t g = { 0 };
   guards_push(&g, vlog_value(first), false);
   reset_ctx_t r = { .m = m, .g = &g };
   for (unsigned i = 0; i < vlog_stmts(first); i++)
      walk_stmts(vlog_stmt(first, i), reset_assign_cb, &r);
   guards_free(&g, 0);
   ACLEAR(g);
   return r.initial;
}

// ---------------------------------------------------------------------
// The machine

typedef struct {
   machine_t *m;
   unsigned   count;
} label_ctx_t;

// The branch of the case which handles the state called NAME
static bool branch_for(vlog_node_t cs, ident_t name, loc_t *branch)
{
   for (unsigned j = 0; j < vlog_stmts(cs); j++) {
      vlog_node_t it = vlog_stmt(cs, j);
      for (unsigned k = 0; k < vlog_params(it); k++) {
         if (ref_name(vlog_param(it, k)) == name) {
            *branch = item_branch_loc(it);
            return true;
         }
      }
   }
   return false;
}

static void emit_state(machine_t *m, ident_t name, const loc_t *decl,
                       vlog_node_t value)
{
   loc_t branch = LOC_INVALID;
   const bool have = branch_for(m->cs, name, &branch);
   char *val = value != NULL ? expr_text(value) : NULL;
   fsm_state(m->f, istr(name), decl, val, have ? &branch : NULL);
   free(val);
}

// Split the arguments of a macro use and return the first which names a
// state of the machine
static ident_t macro_initial(const machine_t *m)
{
   if (m->macro_args == NULL)
      return NULL;

   const char *p = m->macro_args;
   int depth = 0;
   const char *start = p;
   for (;; p++) {
      if (*p == '(' || *p == '[' || *p == '{')
         depth++;
      else if ((*p == ')' || *p == ']' || *p == '}') && depth > 0)
         depth--;
      else if ((*p == ',' || *p == ')' || *p == '\0') && depth == 0) {
         const char *a = start, *e = p;
         while (a < e && isspace((unsigned char)*a))
            a++;
         while (e > a && isspace((unsigned char)e[-1]))
            e--;
         if (e > a) {
            char *arg LOCAL = xstrndup(a, e - a);
            ident_t id = ident_new(arg);
            if (id != m->state->name && (m->next == NULL
                                        || id != m->next->name)
                && is_state_name(m, id) && (ident_in(&m->names, id)
                                            || m->have_enum || m->consts))
               return id;
         }
         if (*p != ',')
            break;
         start = p + 1;
      }
   }

   return NULL;
}

static void emit_machine(vlog_node_t unit, vlog_node_t scope,
                         text_buf_t *out, unsigned *count,
                         const sobj_t *state, const sobj_t *next,
                         vlog_node_t cs, const proc_t *cs_proc,
                         const proc_t *reg_proc, const char *macro_args)
{
   machine_t m = {
      .state = state, .next = next, .cs = cs, .cs_proc = cs_proc->node,
      .reg_proc = reg_proc ? reg_proc->node : NULL,
      .target = next ? next->name : state->name,
      .have_enum = state->enum_decl != NULL,
      .consts = state->enum_decl == NULL && !state->named,
      .file = loc_file(vlog_loc(unit)),
      .unit = unit, .scope = scope, .macro_args = macro_args,
   };

   if (!m.have_enum)
      collect_label_names(&m, cs);

   if (m.consts) {
      // Every label of the case must be a constant
      for (unsigned i = 0; i < vlog_stmts(cs); i++) {
         vlog_node_t it = vlog_stmt(cs, i);
         for (unsigned j = 0; j < vlog_params(it); j++) {
            ident_t n = ref_name(vlog_param(it, j));
            if (n == NULL || const_decl(&m, n) == NULL)
               goto done;
         }
      }
   }

   if (!item_assigns_literal(&m, cs))
      goto done;

   m.f = fsm_begin(out, count, istr(vlog_ident(unit)), istr(vlog_ident(unit)),
                   m.consts ? "constants" : "enum",
                   next ? "two-process" : "one-process",
                   m.have_enum || m.consts);

   fsm_object(m.f, "state", istr(state->name), vlog_loc(state->decl),
              state->type_text ? state->type_text : "");
   if (next != NULL)
      fsm_object(m.f, "next", istr(next->name), vlog_loc(next->decl),
                 next->type_text ? next->type_text : "");

   // The clock is the first edge which is not the reset: with two edges
   // the reset is the one named in the first condition of the process
   vlog_node_t clk = NULL;
   bool rising = true;
   if (reg_proc != NULL && reg_proc->ctrl != NULL) {
      vlog_node_t top = top_if(reg_proc->body);
      char *rtext = top && vlog_conds(top) > 0
         && vlog_has_value(vlog_cond(top, 0))
         ? expr_text(vlog_value(vlog_cond(top, 0))) : NULL;
      for (unsigned i = 0; i < vlog_params(reg_proc->ctrl); i++) {
         vlog_node_t e = vlog_param(reg_proc->ctrl, i);
         if (vlog_kind(e) != V_EVENT || vlog_subkind(e) == V_EVENT_LEVEL
             || !vlog_has_value(e))
            continue;
         ident_t n = ref_name(vlog_value(e));
         const bool is_reset = n != NULL && rtext != NULL
            && vlog_params(reg_proc->ctrl) > 1
            && strstr(rtext, istr(n)) != NULL;
         if (clk == NULL || !is_reset) {
            clk = e;
            rising = vlog_subkind(e) == V_EVENT_POSEDGE;
         }
         if (!is_reset)
            break;
      }
      free(rtext);
   }
   if (clk != NULL && ref_name(vlog_value(clk)) != NULL)
      fsm_clock(m.f, istr(ref_name(vlog_value(clk))), rising);

   if (reg_proc != NULL)
      fsm_process(m.f, "register", NULL, vlog_loc(reg_proc->node));
   if (next != NULL)
      fsm_process(m.f, "next", NULL, vlog_loc(cs_proc->node));

   fsm_case(m.f, vlog_loc(cs));

   const unsigned nitems = vlog_stmts(cs);
   if (m.have_enum) {
      vlog_node_t e = state->enum_decl;
      for (unsigned i = 0; i < vlog_decls(e); i++) {
         vlog_node_t l = vlog_decl(e, i);
         if (vlog_has_ident(l))
            emit_state(&m, vlog_ident(l), vlog_loc(l),
                       vlog_has_value(l) ? vlog_value(l) : NULL);
      }
   }

   // Transitions of each alternative
   guards_t g = { 0 };
   froms_t from = { 0 };
   for (unsigned i = 0; i < nitems; i++) {
      vlog_node_t it = vlog_stmt(cs, i);
      if (vlog_kind(it) != V_CASE_ITEM)
         continue;

      from.count = 0;
      bool is_default = vlog_params(it) == 0;
      for (unsigned j = 0; j < vlog_params(it); j++) {
         ident_t n = ref_name(vlog_param(it, j));
         if (n != NULL && is_state_name(&m, n)) {
            note_state(&m, n);
            APUSH(from, istr(n));
         }
      }

      if (is_default) {
         if (m.have_enum) {
            vlog_node_t e = state->enum_decl;
            for (unsigned a = 0; a < vlog_decls(e); a++) {
               vlog_node_t l = vlog_decl(e, a);
               if (!vlog_has_ident(l))
                  continue;
               bool named = false;
               for (unsigned b = 0; b < nitems && !named; b++) {
                  vlog_node_t o = vlog_stmt(cs, b);
                  for (unsigned c = 0; c < vlog_params(o); c++)
                     named |= ref_name(vlog_param(o, c)) == vlog_ident(l);
               }
               if (!named)
                  APUSH(from, istr(vlog_ident(l)));
            }
            if (from.count == 0)
               APUSH(from, "*others");
         }
         else
            APUSH(from, "*others");
      }

      if (from.count == 0)
         continue;

      for (unsigned j = 0; j < vlog_stmts(it); j++)
         emit_stmt(&m, is_default ? "default" : "case", vlog_stmt(it, j),
                   &from, &g);
   }

   vlog_node_t reset = reg_proc ? find_reset(&m, reg_proc) : NULL;
   if (reg_proc != NULL && reg_proc->node == cs_proc->node)
      m.skip = reset;

   // Assignments outside the case in the process holding it
   {
      bool seen = false;
      guards_t og = { 0 };
      outside_walk(&m, cs_proc->body, &seen, &og, true, false);
      ACLEAR(og);
   }

   ident_t initial = emit_reset(&m, reset);
   if (initial == NULL)
      initial = macro_initial(&m);

   if (macro_args != NULL)
      fsm_note(m.f, "the state register is a macro");
   if (m.has_default_note)
      fsm_note(m.f, "default assignment before the case at line %u",
               vlog_loc(cs)->first_line);

   guards_free(&g, 0);
   ACLEAR(g);
   ACLEAR(from);

   if (!m.have_enum) {
      for (unsigned pass = 0; pass < 2; pass++) {
         const idents_t *list = pass == 0 ? &m.names : &m.seen;
         for (unsigned i = 0; i < list->count; i++) {
            ident_t n = list->items[i];
            if (pass == 1 && ident_in(&m.names, n))
               continue;
            vlog_node_t cd = m.consts ? const_decl(&m, n) : NULL;
            emit_state(&m, n, cd ? vlog_loc(cd) : NULL,
                       cd && vlog_has_value(cd) ? vlog_value(cd) : NULL);
         }
      }
   }

   unsigned nstates = m.names.count;
   for (unsigned i = 0; i < m.seen.count; i++) {
      if (!ident_in(&m.names, m.seen.items[i]))
         nstates++;
   }
   if (m.have_enum)
      nstates = m.seen.count;

   const bool few = m.consts ? (nstates < 3 && !(nstates == 2 && reset))
      : nstates < 2;
   if (few) {
      fsm_discard(m.f);
      goto done;
   }

   fsm_end(m.f, initial ? istr(initial) : NULL);

done:
   ACLEAR(m.names);
   ACLEAR(m.seen);
   ACLEAR(m.items_named);
}

typedef struct {
   const sobjs_t *objs;
   vlog_node_t    unit;
   vlog_node_t    scope;
   text_buf_t    *out;
   unsigned      *count;
} drive_t;

typedef struct {
   ident_t       subject;
   vlog_node_t   found[8];
   unsigned      n;
} cases_ctx_t;

static void subject_cb(vlog_node_t s, void *arg)
{
   idents_t *list = arg;
   if (vlog_kind(s) == V_CASE && vlog_has_value(s)) {
      ident_t n = ref_name(vlog_value(s));
      if (n != NULL && !ident_in(list, n))
         APUSH(*list, n);
   }
}

static void case_cb(vlog_node_t s, void *arg)
{
   cases_ctx_t *c = arg;
   if (vlog_kind(s) == V_CASE && vlog_has_value(s)
       && ref_name(vlog_value(s)) == c->subject && c->n < 8)
      c->found[c->n++] = s;
}

static bool try_machine(drive_t *d, const sobj_t *state, const sobj_t *next,
                        const proc_t *reg, const proc_t *cp,
                        const char *macro_args)
{
   cases_ctx_t c = { .subject = state->name };
   walk_stmts(cp->body, case_cb, &c);

   const char *file = loc_file(vlog_loc(d->unit));
   for (unsigned i = 0; i < c.n; i++) {
      if (!same_file(vlog_loc(c.found[i]), file))
         continue;   // From an included file
      const unsigned before = *d->count;
      emit_machine(d->unit, d->scope, d->out, d->count, state, next, c.found[i],
                   cp, reg, macro_args);
      if (*d->count != before)
         return true;   // The first case that makes a machine
   }

   return false;
}

// Find uses of a register macro such as `FF(state_q, state_d, IDLE, clk, rst)
// in the text of the scope: the first two arguments are names of objects.
// Returns the text after the second argument.
static const char *macro_use(const char *text, const char **pos, ident_t state,
                             ident_t next)
{
   const char *p = *pos;
   while ((p = strchr(p, '`')) != NULL) {
      p++;
      const char *id = p;
      while (isalnum((unsigned char)*p) || *p == '_')
         p++;
      if (p == id || *p != '(')
         continue;
      p++;
      const char *a = p;
      while (*p && *p != ',' && *p != ')')
         p++;
      if (*p != ',')
         continue;
      char *first LOCAL = xstrndup(a, p - a);
      p++;
      const char *b = p;
      while (*p && *p != ',' && *p != ')')
         p++;
      char *second LOCAL = xstrndup(b, p - b);
      char *f = first, *s = second;
      while (isspace((unsigned char)*f))
         f++;
      while (isspace((unsigned char)*s))
         s++;
      for (char *e = f + strlen(f); e > f && isspace((unsigned char)e[-1]);)
         *--e = '\0';
      for (char *e = s + strlen(s); e > s && isspace((unsigned char)e[-1]);)
         *--e = '\0';
      if (!strcmp(f, istr(state)) && !strcmp(s, istr(next))) {
         *pos = p;
         return p;
      }
   }
   (void)text;
   return NULL;
}

static void scan_scope(vlog_node_t unit, vlog_node_t scope,
                       const sobjs_t *outer, text_buf_t *out,
                       unsigned *count);

static void scan_children(vlog_node_t unit, vlog_node_t scope,
                          const sobjs_t *objs, text_buf_t *out,
                          unsigned *count)
{
   for (unsigned i = 0; i < vlog_stmts(scope); i++) {
      vlog_node_t s = vlog_stmt(scope, i);
      switch (vlog_kind(s)) {
      case V_GEN_BLOCK:
         scan_scope(unit, s, objs, out, count);
         break;
      case V_IF_GENERATE:
         for (unsigned c = 0; c < vlog_conds(s); c++) {
            vlog_node_t cond = vlog_cond(s, c);
            for (unsigned j = 0; j < vlog_stmts(cond); j++) {
               vlog_node_t b = vlog_stmt(cond, j);
               if (vlog_kind(b) == V_GEN_BLOCK)
                  scan_scope(unit, b, objs, out, count);
            }
         }
         break;
      case V_FOR_GENERATE:
         for (unsigned j = 0; j < vlog_stmts(s); j++) {
            vlog_node_t b = vlog_stmt(s, j);
            if (vlog_kind(b) == V_GEN_BLOCK)
               scan_scope(unit, b, objs, out, count);
         }
         break;
      default:
         break;
      }
   }
}

static void scan_scope(vlog_node_t unit, vlog_node_t scope,
                       const sobjs_t *outer, text_buf_t *out,
                       unsigned *count)
{
   // The objects of this scope come first, then those of the scopes around
   sobjs_t objs = { 0 };
   collect_objects(scope, &objs);
   const unsigned ninner = objs.count;
   if (outer != NULL) {
      for (unsigned i = 0; i < outer->count; i++) {
         sobj_t o = outer->items[i];
         o.inner = false;
         APUSH(objs, o);
      }
   }

   A(proc_t) procs = { 0 };
   idents_t subjects = { 0 };
   for (unsigned i = 0; i < vlog_stmts(scope) && ninner > 0; i++) {
      vlog_node_t s = vlog_stmt(scope, i);
      if (vlog_kind(s) == V_ALWAYS) {
         proc_t p;
         classify_proc(s, &p);
         APUSH(procs, p);
         walk_stmts(p.body, subject_cb, &subjects);
      }
   }

   bool any = false;
   for (unsigned i = 0; i < ninner && !any; i++)
      any = ident_in(&subjects, objs.items[i].name);

   if (any) {

      drive_t d = { .objs = &objs, .unit = unit, .scope = scope, .out = out,
                    .count = count };
      char *text LOCAL = NULL;

      for (unsigned i = 0; i < ninner; i++) {
         const sobj_t *so = &objs.items[i];
         if (!ident_in(&subjects, so->name) || !is_candidate(so))
            continue;

         bool done = false;

         // One process: the case is in a clocked process
         for (unsigned p = 0; p < procs.count && !done; p++) {
            if (!procs.items[p].clocked || procs.items[p].body == NULL)
               continue;
            done = try_machine(&d, so, NULL, &procs.items[p],
                               &procs.items[p], NULL);
         }

         // Two processes: a clocked process assigns S <= N and a case on S
         // in another process assigns N
         for (unsigned p = 0; p < procs.count && !done; p++) {
            const proc_t *reg = &procs.items[p];
            if (!reg->clocked || reg->body == NULL)
               continue;

            nodes_t assigns = { 0 };
            walk_collect_assigns(reg->body, &assigns);
            for (unsigned a = 0; a < assigns.count && !done; a++) {
               vlog_node_t s = assigns.items[a];
               if (assign_target(s) != so->name)
                  continue;
               const sobj_t *next = find_object(&objs, ref_name(vlog_value(s)));
               if (next == NULL || next == so || !is_candidate(next))
                  continue;
               for (unsigned q = 0; q < procs.count && !done; q++) {
                  if (q == p || procs.items[q].clocked
                      || procs.items[q].body == NULL)
                     continue;
                  done = try_machine(&d, so, next, reg, &procs.items[q], NULL);
               }
            }
            ACLEAR(assigns);
         }

         // The register is a macro: `FF(S, N, ...)
         if (!done) {
            if (text == NULL) {
               // From the header of the module to its endmodule
               const loc_t *l = vlog_loc(unit);
               const char *src = NULL;
               if (!loc_invalid_p(l)) {
                  loc_t at = *l;
                  at.line_delta = 0;
                  at.first_column = 0;
                  src = loc_get_source(&at);
               }
               if (src == NULL)
                  text = xstrdup("");
               else {
                  const char *end = strstr(src, "endmodule");
                  text = end ? xstrndup(src, end - src) : xstrdup(src);
               }
            }

            const char *pos = text;
            for (unsigned n = 0; n < objs.count && !done
                    && strchr(text, '`') != NULL; n++) {
               const sobj_t *next = &objs.items[n];
               if (next == so || !is_candidate(next))
                  continue;
               pos = text;
               const char *args;
               while (!done && (args = macro_use(text, &pos, so->name,
                                                 next->name)) != NULL) {
                  for (unsigned q = 0; q < procs.count && !done; q++) {
                     if (procs.items[q].clocked || procs.items[q].body == NULL)
                        continue;
                     done = try_machine(&d, so, next, NULL, &procs.items[q],
                                        args);
                  }
               }
            }
         }
      }
   }

   ACLEAR(procs);
   ACLEAR(subjects);

   scan_children(unit, scope, &objs, out, count);

   for (unsigned i = 0; i < objs.count; i++) {
      if (i < ninner || objs.items[i].type_text
          != outer->items[i - ninner].type_text)
         free(objs.items[i].type_text);
   }
   ACLEAR(objs);
}

void vlog_fsm_scan(vlog_node_t unit, text_buf_t *out, unsigned *count)
{
   const vlog_kind_t uk = vlog_kind(unit);
   if (uk != V_MODULE && uk != V_PROGRAM)
      return;

   scan_scope(unit, unit, NULL, out, count);
}
