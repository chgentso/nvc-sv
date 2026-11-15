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
#include "lint.h"
#include "parse.h"
#include "array.h"
#include "diag.h"
#include "fsm.h"
#include "scandeps.h"
#include "tree.h"
#include "type.h"
#include "vlog/vlog-node.h"

#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

struct _fsm_out {
   text_buf_t *out;
   unsigned   *count;
   text_buf_t *head;
   text_buf_t *procs;
   text_buf_t *states;
   text_buf_t *trans;
   text_buf_t *notes;
   text_buf_t *tfrom;    // Pieces of the transition being built
   text_buf_t *tmain;
   text_buf_t *tguards;
   unsigned    nprocs, nstates, ntrans, nnotes, nfrom, nguards;
   bool        vhdl;
   bool        in_trans;
};

static bool fsm_word_char(char c)
{
   return isalnum((unsigned char)c) || c == '_' || c == '$';
}

// Position of NAME as a whole word within LOC, falling back to the start
static void fsm_name_pos(const fsm_out_t *f, const loc_t *loc,
                         const char *name, unsigned *line, unsigned *col,
                         unsigned *len)
{
   *line = loc->first_line;
   *col = loc->first_column + 1;
   *len = loc->line_delta == 0 ? loc->column_delta + 1 : 0;

   const char *src = loc_get_source(loc);
   const size_t nlen = strlen(name);
   if (src == NULL || nlen == 0)
      return;

   const unsigned end_line = loc->first_line + loc->line_delta;
   const unsigned end_col = loc->first_column + loc->column_delta;
   const bool single = loc->line_delta == 0;

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
         if (i > 0 && fsm_word_char(ptr[i - 1]))
            continue;
         if (i + nlen < linelen && fsm_word_char(ptr[i + nlen]))
            continue;
         if (f->vhdl ? strncasecmp(ptr + i, name, nlen)
             : strncmp(ptr + i, name, nlen))
            continue;
         *line = cur;
         *col = i + 1;
         *len = nlen;
         return;
      }
      if (eol == NULL)
         break;
      ptr = eol + 1;
      cur++;
   }
}

static void fsm_loc_pos(text_buf_t *tb, const loc_t *loc)
{
   if (loc_invalid_p(loc))
      return;
   tb_printf(tb, ",\"line\":%u,\"col\":%u", loc->first_line,
             loc->first_column + 1);
}

static void fsm_loc_span(text_buf_t *tb, const loc_t *loc, bool cols)
{
   if (loc_invalid_p(loc))
      return;
   tb_printf(tb, ",\"line\":%u", loc->first_line);
   if (cols)
      tb_printf(tb, ",\"col\":%u", loc->first_column + 1);
   if (loc->line_delta < DELTA_INVALID) {
      if (loc->line_delta > 0)
         tb_printf(tb, ",\"end_line\":%u", loc->first_line + loc->line_delta);
      if (cols)
         tb_printf(tb, ",\"end_col\":%u",
                   loc->first_column + loc->column_delta + 2);
   }
}

static void fsm_comma(text_buf_t *tb, unsigned *n)
{
   if ((*n)++ > 0)
      tb_append(tb, ',');
}

fsm_out_t *fsm_begin(text_buf_t *tb, unsigned *count, const char *unit,
                     const char *container, const char *kind,
                     const char *style, bool typed)
{
   fsm_out_t *f = xcalloc(sizeof(fsm_out_t));
   f->out = tb;
   f->count = count;
   f->head = tb_new();
   f->procs = tb_new();
   f->states = tb_new();
   f->trans = tb_new();
   f->notes = tb_new();
   f->tfrom = tb_new();
   f->tmain = tb_new();
   f->tguards = tb_new();

   tb_cat(f->head, "{\"unit\":");
   scan_json_string(f->head, unit);
   tb_cat(f->head, ",\"container\":");
   scan_json_string(f->head, container);
   tb_printf(f->head, ",\"kind\":\"%s\",\"style\":\"%s\",\"typed\":%s", kind,
             style, typed ? "true" : "false");
   return f;
}

void fsm_set_vhdl(fsm_out_t *f, bool vhdl)
{
   f->vhdl = vhdl;
}

void fsm_object(fsm_out_t *f, const char *key, const char *name,
                const loc_t *loc, const char *type)
{
   tb_printf(f->head, ",\"%s\":{\"name\":", key);
   scan_json_string(f->head, name);
   if (!loc_invalid_p(loc)) {
      unsigned line, col, len;
      fsm_name_pos(f, loc, name, &line, &col, &len);
      tb_printf(f->head, ",\"line\":%u,\"col\":%u,\"len\":%u", line, col, len);
   }
   if (type != NULL) {
      tb_cat(f->head, ",\"type\":");
      scan_json_string(f->head, type);
   }
   tb_append(f->head, '}');
}

void fsm_clock(fsm_out_t *f, const char *name, bool rising)
{
   tb_cat(f->head, ",\"clock\":{\"name\":");
   scan_json_string(f->head, name);
   tb_printf(f->head, ",\"edge\":\"%s\"}", rising ? "rising" : "falling");
}

void fsm_process(fsm_out_t *f, const char *role, const char *label,
                 const loc_t *loc)
{
   fsm_comma(f->procs, &f->nprocs);
   tb_printf(f->procs, "{\"role\":\"%s\"", role);
   if (label != NULL) {
      tb_cat(f->procs, ",\"label\":");
      scan_json_string(f->procs, label);
   }
   fsm_loc_span(f->procs, loc, false);
   if (!loc_invalid_p(loc) && loc->line_delta == 0)
      tb_printf(f->procs, ",\"end_line\":%u", loc->first_line);
   tb_append(f->procs, '}');
}

void fsm_case(fsm_out_t *f, const loc_t *loc)
{
   tb_cat(f->head, ",\"case\":{");
   if (!loc_invalid_p(loc)) {
      tb_printf(f->head, "\"line\":%u,\"end_line\":%u", loc->first_line,
                loc->line_delta < DELTA_INVALID
                ? loc->first_line + loc->line_delta : loc->first_line);
   }
   tb_append(f->head, '}');
}

void fsm_state(fsm_out_t *f, const char *name, const loc_t *decl,
               const char *value, const loc_t *branch)
{
   fsm_comma(f->states, &f->nstates);
   tb_cat(f->states, "{\"name\":");
   scan_json_string(f->states, name);
   if (decl != NULL && !loc_invalid_p(decl)) {
      unsigned line, col, len;
      fsm_name_pos(f, decl, name, &line, &col, &len);
      tb_printf(f->states, ",\"line\":%u,\"col\":%u,\"len\":%u", line, col,
                len);
   }
   if (value != NULL) {
      tb_cat(f->states, ",\"value\":");
      scan_json_string(f->states, value);
   }
   if (branch != NULL && !loc_invalid_p(branch)) {
      tb_printf(f->states, ",\"branch\":{\"line\":%u,\"end_line\":%u}",
                branch->first_line,
                branch->line_delta < DELTA_INVALID
                ? branch->first_line + branch->line_delta
                : branch->first_line);
   }
   tb_append(f->states, '}');
}

void fsm_trans_begin(fsm_out_t *f, const char *kind, const loc_t *assign,
                     const char *to, const char *expr)
{
   assert(!f->in_trans);
   f->in_trans = true;
   f->nfrom = f->nguards = 0;
   tb_rewind(f->tfrom);
   tb_rewind(f->tmain);
   tb_rewind(f->tguards);

   if (to != NULL) {
      tb_cat(f->tmain, ",\"to\":");
      scan_json_string(f->tmain, to);
   }
   if (expr != NULL) {
      tb_cat(f->tmain, ",\"expr\":");
      scan_json_string(f->tmain, expr);
   }
   tb_printf(f->tmain, ",\"kind\":\"%s\"", kind);
   fsm_loc_span(f->tmain, assign, true);
}

void fsm_trans_from(fsm_out_t *f, const char *state)
{
   assert(f->in_trans);
   fsm_comma(f->tfrom, &f->nfrom);
   scan_json_string(f->tfrom, state);
}

void fsm_trans_guard(fsm_out_t *f, const char *text, const loc_t *loc,
                     bool neg)
{
   assert(f->in_trans);
   fsm_comma(f->tguards, &f->nguards);
   tb_cat(f->tguards, "{\"text\":");
   scan_json_string(f->tguards, text);
   fsm_loc_pos(f->tguards, loc);
   tb_printf(f->tguards, ",\"neg\":%s}", neg ? "true" : "false");
}

void fsm_trans_end(fsm_out_t *f)
{
   assert(f->in_trans);
   f->in_trans = false;

   fsm_comma(f->trans, &f->ntrans);
   tb_printf(f->trans, "{\"from\":[%s]%s,\"guards\":[%s]}",
             tb_get(f->tfrom), tb_get(f->tmain), tb_get(f->tguards));
}

void fsm_note(fsm_out_t *f, const char *fmt, ...)
{
   va_list ap;
   va_start(ap, fmt);
   char *text LOCAL = xvasprintf(fmt, ap);
   va_end(ap);

   fsm_comma(f->notes, &f->nnotes);
   scan_json_string(f->notes, text);
}

static void fsm_free(fsm_out_t *f)
{
   tb_free(f->head);
   tb_free(f->procs);
   tb_free(f->states);
   tb_free(f->trans);
   tb_free(f->notes);
   tb_free(f->tfrom);
   tb_free(f->tmain);
   tb_free(f->tguards);
   free(f);
}

void fsm_end(fsm_out_t *f, const char *initial)
{
   assert(!f->in_trans);

   if ((*f->count)++ > 0)
      tb_append(f->out, ',');

   tb_cat(f->out, tb_get(f->head));
   tb_printf(f->out, ",\"processes\":[%s],\"states\":[%s],"
             "\"transitions\":[%s]", tb_get(f->procs), tb_get(f->states),
             tb_get(f->trans));
   if (initial != NULL) {
      tb_cat(f->out, ",\"initial\":");
      scan_json_string(f->out, initial);
   }
   if (f->nnotes > 0)
      tb_printf(f->out, ",\"notes\":[%s]", tb_get(f->notes));
   tb_append(f->out, '}');

   fsm_free(f);
}

void fsm_discard(fsm_out_t *f)
{
   fsm_free(f);
}

static char *fsm_text(const loc_t *loc, bool vhdl)
{
   if (loc_invalid_p(loc))
      return NULL;

   const char *src = loc_get_source(loc);
   if (src == NULL)
      return NULL;

   const unsigned end_line = loc->first_line + loc->line_delta;
   const unsigned end_col = loc->first_column + loc->column_delta;

   LOCAL_TEXT_BUF tb = tb_new();
   bool space = false;
   unsigned cur = loc->first_line;
   const char *ptr = src;
   while (cur <= end_line && cur < loc->first_line + 200) {
      const char *eol = strchr(ptr, '\n');
      size_t linelen = eol ? (size_t)(eol - ptr) : strlen(ptr);
      size_t from = cur == loc->first_line ? loc->first_column : 0;
      size_t to = linelen;
      if (cur == end_line && end_col + 1 < to)
         to = end_col + 1;
      for (size_t i = from; i < to; i++) {
         if (vhdl && ptr[i] == '-' && i + 1 < to && ptr[i + 1] == '-')
            break;   // Comment
         if (isspace((unsigned char)ptr[i])) {
            space = tb_len(tb) > 0;
            continue;
         }
         if (space)
            tb_append(tb, ' ');
         space = false;
         tb_append(tb, ptr[i]);
      }
      space = tb_len(tb) > 0;
      if (eol == NULL)
         break;
      ptr = eol + 1;
      cur++;
   }

   return tb_claim(tb);
}


char *fsm_source_text(const loc_t *loc)
{
   return fsm_text(loc, false);
}

char *fsm_source_text_vhdl(const loc_t *loc)
{
   return fsm_text(loc, true);
}

////////////////////////////////////////////////////////////////////////////////
// VHDL

#define MAX_GUARDS 16
#define MAX_RESETS 16
#define MAX_STATES 512

typedef struct {
   tree_t   assign;
   tree_t   cond;       // Condition of the reset, or NULL
} reset_t;

typedef struct {
   ident_t  key;
   char    *spell;
   char    *value;
   loc_t    decl;
   loc_t    branch;
   bool     has_decl, has_branch;
} state_t;

typedef struct {
   tree_t   proc;
   bool     clocked;
   tree_t   clk;        // The reference to the clock
   bool     rising;
} proc_info_t;

typedef struct {
   tree_t       cs;
   proc_info_t *proc;
   bool         in_region;
} case_info_t;

// A signal or variable, or a field of one
typedef struct {
   tree_t       decl;
   tree_t       field;
} obj_t;

typedef struct {
   obj_t        target;
   obj_t        partner;      // The object assigned to it if there is one
   bool         has_partner;
   bool         in_region;    // Under the clock edge
   proc_info_t *proc;
} reg_assign_t;

typedef struct {
   fsm_out_t    *out;
   obj_t         state;       // Register of the machine
   obj_t         target;      // Object assigned by the transitions
   tree_t        cs;
   bool          typed;
   bool          consts;     // States are named constants
   type_t        type;
   state_t       states[MAX_STATES];
   unsigned      nstates;
   tree_t        guards[MAX_GUARDS];
   bool          negs[MAX_GUARDS];
   unsigned      nguards;
   reset_t       resets[MAX_RESETS];
   unsigned      nresets;
   tree_t        reset_ifs[MAX_RESETS];
   unsigned      nreset_ifs;
   bool          case_seen;
   bool          computed;
   unsigned      ntrans;
   loc_t         fallback;   // For an assignment without a location
} walk_t;

typedef A(case_info_t) case_list_t;
typedef A(reg_assign_t) reg_list_t;
typedef A(proc_info_t *) proc_list_t;

static char *vhdl_text(tree_t t)
{
   return fsm_source_text_vhdl(tree_loc(t));
}

static const char *vhdl_tail(ident_t id)
{
   const char *s = istr(id);
   const char *dot = strrchr(s, '.');
   return dot ? dot + 1 : s;
}

static bool vhdl_ci_eq(const char *s, const char *word)
{
   // Operator names may be quoted
   if (*s == '"')
      s++;
   const size_t n = strlen(word);
   return strncasecmp(s, word, n) == 0 && (s[n] == '\0' || s[n] == '"');
}

static bool vhdl_others_p(tree_t ch)
{
   return !tree_has_name(ch) && tree_ranges(ch) == 0;
}

static tree_t vhdl_strip(tree_t e)
{
   while (e != NULL) {
      switch (tree_kind(e)) {
      case T_QUALIFIED:
      case T_TYPE_CONV:
         e = tree_value(e);
         break;
      default:
         return e;
      }
   }
   return e;
}

// The declaration a plain name refers to
static tree_t vhdl_decl_of(tree_t e)
{
   e = vhdl_strip(e);
   if (e == NULL || tree_kind(e) != T_REF || !tree_has_ref(e))
      return NULL;
   return tree_ref(e);
}

static bool vhdl_object_p(tree_t d)
{
   if (d == NULL)
      return false;
   switch (tree_kind(d)) {
   case T_SIGNAL_DECL:
   case T_PORT_DECL:
   case T_VAR_DECL:
      return true;
   default:
      return false;
   }
}

static bool vhdl_obj(tree_t e, obj_t *o)
{
   e = vhdl_strip(e);
   if (e == NULL)
      return false;

   if (tree_kind(e) == T_RECORD_REF) {
      if (!tree_has_ref(e))
         return false;
      tree_t base = vhdl_decl_of(tree_value(e));
      if (!vhdl_object_p(base) || tree_kind(tree_ref(e)) != T_FIELD_DECL)
         return false;
      o->decl = base;
      o->field = tree_ref(e);
      return true;
   }

   tree_t d = vhdl_decl_of(e);
   if (!vhdl_object_p(d))
      return false;
   o->decl = d;
   o->field = NULL;
   return true;
}

static bool obj_eq(obj_t a, obj_t b)
{
   return a.decl == b.decl && a.field == b.field;
}

static bool vhdl_is_obj(tree_t e, obj_t o)
{
   obj_t x;
   return vhdl_obj(e, &x) && obj_eq(x, o);
}

static type_t obj_type(obj_t o)
{
   return tree_type(o.field ?: o.decl);
}

static bool vhdl_predefined_p(type_t type)
{
   return type_has_ident(type)
      && strncmp(istr(type_ident(type)), "STD.", 4) == 0;
}

static tree_t vhdl_param_value(tree_t call, unsigned n)
{
   if (n >= tree_params(call))
      return NULL;
   tree_t p = tree_param(call, n);
   return tree_has_value(p) ? tree_value(p) : NULL;
}

static bool vhdl_is_literal_one(tree_t e, bool *one)
{
   e = vhdl_strip(e);
   if (e == NULL)
      return false;
   if (tree_kind(e) == T_REF && tree_has_ref(e)
       && tree_kind(tree_ref(e)) == T_ENUM_LIT) {
      // Elaborated trees may no longer have the source text
      const char *id = istr(tree_ident(tree_ref(e)));
      if (!strcmp(id, "'1'") || !strcmp(id, "'0'")) {
         *one = id[1] == '1';
         return true;
      }
      return false;
   }
   if (tree_kind(e) == T_REF || tree_kind(e) == T_LITERAL) {
      char *txt LOCAL = vhdl_text(e);
      if (txt == NULL)
         return false;
      if (!strcmp(txt, "'1'") || !strcmp(txt, "'0'")) {
         *one = txt[1] == '1';
         return true;
      }
   }
   return false;
}

// Does the expression test for a clock edge
static bool vhdl_edge(tree_t e, tree_t *clk, bool *rising)
{
   e = vhdl_strip(e);
   if (e == NULL)
      return false;

   if (tree_kind(e) != T_FCALL || !tree_has_ident(e))
      return false;

   const char *tail = vhdl_tail(tree_ident(e));
   if (vhdl_ci_eq(tail, "rising_edge") || vhdl_ci_eq(tail, "falling_edge")) {
      tree_t arg = vhdl_param_value(e, 0);
      if (arg != NULL && vhdl_strip(arg) != NULL
          && tree_kind(vhdl_strip(arg)) == T_REF) {
         *clk = vhdl_strip(arg);
         *rising = vhdl_ci_eq(tail, "rising_edge");
         return true;
      }
      return false;
   }

   if (vhdl_ci_eq(tail, "and") && tree_params(e) == 2) {
      // clk'event and clk = '1'  or  clk = '1' and clk'event
      for (int i = 0; i < 2; i++) {
         tree_t ev = vhdl_strip(vhdl_param_value(e, i));
         tree_t cmp = vhdl_strip(vhdl_param_value(e, 1 - i));
         if (ev == NULL || cmp == NULL || tree_kind(ev) != T_ATTR_REF
             || !vhdl_ci_eq(istr(tree_ident(ev)), "event"))
            continue;
         if (tree_kind(cmp) != T_FCALL || tree_params(cmp) != 2
             || !tree_has_ident(cmp) || !vhdl_ci_eq(tree_ident(cmp) ? istr(tree_ident(cmp)) : "", "="))
            continue;
         bool one;
         tree_t l = vhdl_strip(vhdl_param_value(cmp, 0));
         tree_t r = vhdl_param_value(cmp, 1);
         if (l != NULL && tree_kind(l) == T_REF && vhdl_is_literal_one(r, &one)) {
            *clk = l;
            *rising = one;
            return true;
         }
      }
   }

   return false;
}

bool fsm_vhdl_edge(tree_t e, tree_t *clk, bool *rising)
{
   return vhdl_edge(e, clk, rising);
}

////////////////////////////////////////////////////////////////////////////////
// Pass 1: processes, cases and registered assignments

typedef struct {
   case_list_t  cases;
   reg_list_t   regs;
   proc_list_t  procs;
} scope_t;

static void vhdl_collect_stmts(scope_t *sc, proc_info_t *pi, tree_t parent,
                               bool in_region);

static void vhdl_collect_stmt(scope_t *sc, proc_info_t *pi, tree_t s,
                              bool in_region)
{
   switch (tree_kind(s)) {
   case T_IF:
      for (unsigned i = 0; i < tree_conds(s); i++) {
         tree_t c = tree_cond(s, i);
         bool region = in_region;
         tree_t clk;
         bool rising;
         if (tree_has_value(c) && vhdl_edge(tree_value(c), &clk, &rising)) {
            if (!pi->clocked) {
               pi->clocked = true;
               pi->clk = clk;
               pi->rising = rising;
            }
            region = true;
         }
         vhdl_collect_stmts(sc, pi, c, region);
      }
      break;

   case T_CASE:
   case T_MATCH_CASE:
      {
         case_info_t ci = { s, pi, in_region };
         APUSH(sc->cases, ci);
         for (unsigned i = 0; i < tree_stmts(s); i++)
            vhdl_collect_stmts(sc, pi, tree_stmt(s, i), in_region);
      }
      break;

   case T_COND_ASSIGN:
      {
         obj_t o;
         if (tree_has_target(s) && vhdl_obj(tree_target(s), &o)) {
            reg_assign_t ra = { .target = o, .in_region = in_region,
                                .proc = pi };
            APUSH(sc->regs, ra);
         }
         for (unsigned i = 0; i < tree_conds(s); i++)
            vhdl_collect_stmts(sc, pi, tree_cond(s, i), in_region);
      }
      break;

   case T_SIGNAL_ASSIGN:
   case T_VAR_ASSIGN:
      {
         obj_t o;
         if (tree_has_target(s) && vhdl_obj(tree_target(s), &o)) {
            reg_assign_t ra = { .target = o, .in_region = in_region,
                                .proc = pi };
            tree_t value = NULL;
            if (tree_kind(s) == T_VAR_ASSIGN)
               value = tree_has_value(s) ? tree_value(s) : NULL;
            else if (tree_waveforms(s) == 1) {
               tree_t w = tree_waveform(s, 0);
               value = tree_has_value(w) ? tree_value(w) : NULL;
            }
            obj_t p;
            if (value != NULL && vhdl_obj(value, &p) && !obj_eq(p, o)) {
               ra.partner = p;
               ra.has_partner = true;
            }
            APUSH(sc->regs, ra);
         }
      }
      break;

   case T_WAIT:
      break;

   case T_FOR:
   case T_WHILE:
   case T_LOOP:
   case T_SEQUENCE:
      vhdl_collect_stmts(sc, pi, s, in_region);
      break;

   default:
      break;
   }
}

static void vhdl_collect_stmts(scope_t *sc, proc_info_t *pi, tree_t parent,
                               bool in_region)
{
   const unsigned n = tree_stmts(parent);
   bool region = in_region;
   for (unsigned i = 0; i < n; i++) {
      tree_t s = tree_stmt(parent, i);
      if (tree_kind(s) == T_WAIT && tree_has_value(s)) {
         tree_t clk;
         bool rising;
         if (vhdl_edge(tree_value(s), &clk, &rising)) {
            if (!pi->clocked) {
               pi->clocked = true;
               pi->clk = clk;
               pi->rising = rising;
            }
            region = true;
         }
         continue;
      }
      vhdl_collect_stmt(sc, pi, s, region);
   }
}

static void vhdl_collect_scope(scope_t *sc, tree_t parent)
{
   const tree_kind_t pk = tree_kind(parent);
   if (pk == T_IF_GENERATE) {
      for (unsigned i = 0; i < tree_conds(parent); i++)
         vhdl_collect_scope(sc, tree_cond(parent, i));
      return;
   }

   for (unsigned i = 0; i < tree_stmts(parent); i++) {
      tree_t s = tree_stmt(parent, i);
      switch (tree_kind(s)) {
      case T_PROCESS:
         {
            proc_info_t *pi = xcalloc(sizeof(proc_info_t));
            pi->proc = s;
            APUSH(sc->procs, pi);
            vhdl_collect_stmts(sc, pi, s, false);
         }
         break;
      case T_BLOCK:
      case T_FOR_GENERATE:
      case T_IF_GENERATE:
      case T_CASE_GENERATE:
      case T_ALTERNATIVE:
      case T_COND_STMT:
         vhdl_collect_scope(sc, s);
         break;
      default:
         break;
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
// Pass 2: the machine

static state_t *vhdl_state(walk_t *w, ident_t key, const char *spell,
                           const loc_t *decl)
{
   for (unsigned i = 0; i < w->nstates; i++)
      if (w->states[i].key == key)
         return &w->states[i];

   if (w->nstates == MAX_STATES)
      return NULL;

   state_t *st = &w->states[w->nstates++];
   st->key = key;
   st->spell = xstrdup(spell);
   st->has_decl = decl != NULL && !loc_invalid_p(decl);
   if (st->has_decl)
      st->decl = *decl;
   st->has_branch = false;
   st->value = NULL;
   return st;
}

// A name that denotes a state: a literal of the type, or an unresolved
// name when the type is not visible
static bool vhdl_state_name(walk_t *w, tree_t e, ident_t *key,
                            char **spell)
{
   e = vhdl_strip(e);
   if (e == NULL || tree_kind(e) != T_REF || !tree_has_ident(e))
      return false;

   if (w->consts) {
      if (!tree_has_ref(e) || tree_kind(tree_ref(e)) != T_CONST_DECL)
         return false;
      tree_t d = tree_ref(e);
      *key = tree_ident(e);
      *spell = vhdl_text(e);
      if (*spell == NULL)
         *spell = xstrdup(istr(tree_ident(e)));
      state_t *st = vhdl_state(w, *key, *spell, tree_loc(d));
      if (st != NULL && st->value == NULL && tree_has_value(d))
         st->value = vhdl_text(tree_value(d));
      return true;
   }

   if (tree_has_ref(e)) {
      tree_t d = tree_ref(e);
      if (tree_kind(d) != T_ENUM_LIT)
         return false;
      if (w->typed && type_eq(tree_type(d), w->type) == false)
         return false;
   }
   else if (w->typed)
      return false;

   const char *name = istr(tree_ident(e));
   if (!isalpha((unsigned char)name[0]) && name[0] != '\\')
      return false;   // Character literal

   *key = tree_ident(e);
   *spell = vhdl_text(e);
   if (*spell == NULL)
      *spell = xstrdup(name);
   return true;
}

static void vhdl_emit(walk_t *w, const char *kind, tree_t assign,
                      const char *to, const char *expr,
                      const ident_t *from, unsigned nfrom,
                      bool others)
{
   fsm_trans_begin(w->out, kind, loc_invalid_p(tree_loc(assign))
                   ? &w->fallback : tree_loc(assign), to, expr);

   if (others) {
      // The states not named by another alternative
      bool listed = false;
      if (w->typed) {
         for (unsigned i = 0; i < w->nstates; i++) {
            if (!w->states[i].has_branch) {
               fsm_trans_from(w->out, w->states[i].spell);
               listed = true;
            }
         }
      }
      if (!listed)
         fsm_trans_from(w->out, "*others");
   }
   else {
      for (unsigned i = 0; i < nfrom; i++) {
         if (from[i] == NULL)
            fsm_trans_from(w->out, "*");
         else {
            for (unsigned j = 0; j < w->nstates; j++) {
               if (w->states[j].key == from[i]) {
                  fsm_trans_from(w->out, w->states[j].spell);
                  break;
               }
            }
         }
      }
   }

   for (unsigned i = 0; i < w->nguards; i++) {
      char *txt LOCAL = vhdl_text(w->guards[i]);
      fsm_trans_guard(w->out, txt ?: "?", tree_loc(w->guards[i]), w->negs[i]);
   }

   fsm_trans_end(w->out);
   w->ntrans++;
}

typedef struct {
   const ident_t *from;
   unsigned       nfrom;
   bool           others;
   const char    *kind;
} from_t;

static void vhdl_value(walk_t *w, tree_t assign, tree_t value,
                       const from_t *from)
{
   if (value == NULL)
      return;

   value = vhdl_strip(value);

   if (vhdl_is_obj(value, w->state))
      return;   // Hold

   ident_t key;
   char *spell = NULL;
   if (vhdl_state_name(w, value, &key, &spell)) {
      state_t *st = vhdl_state(w, key, spell, NULL);
      if (st != NULL)
         vhdl_emit(w, from->kind, assign, st->spell, NULL, from->from,
                   from->nfrom, from->others);
      free(spell);
      return;
   }

   if (vhdl_is_obj(value, w->target))
      return;

   char *txt LOCAL = vhdl_text(value);
   w->computed = true;
   vhdl_emit(w, from->kind, assign, NULL, txt ?: "?", from->from, from->nfrom,
             from->others);
}

static bool vhdl_is_target(walk_t *w, tree_t t)
{
   return vhdl_is_obj(t, w->target);
}

static bool vhdl_reset_assign_p(walk_t *w, tree_t s)
{
   for (unsigned i = 0; i < w->nresets; i++)
      if (w->resets[i].assign == s)
         return true;
   return false;
}

static bool vhdl_reset_if_p(walk_t *w, tree_t s)
{
   for (unsigned i = 0; i < w->nreset_ifs; i++)
      if (w->reset_ifs[i] == s)
         return true;
   return false;
}

static void vhdl_walk_stmts(walk_t *w, tree_t parent, const from_t *from,
                            bool in_case);

static void vhdl_walk_assign(walk_t *w, tree_t s, const from_t *from)
{
   tree_t target = tree_has_target(s) ? tree_target(s) : NULL;
   if (target == NULL || !vhdl_is_target(w, target))
      return;
   if (vhdl_reset_assign_p(w, s))
      return;

   if (tree_kind(s) == T_VAR_ASSIGN) {
      if (tree_has_value(s))
         vhdl_value(w, s, tree_value(s), from);
   }
   else if (tree_waveforms(s) > 0) {
      tree_t wv = tree_waveform(s, 0);
      if (tree_has_value(wv))
         vhdl_value(w, s, tree_value(wv), from);
   }
}

static void vhdl_push_guard(walk_t *w, tree_t cond, bool neg)
{
   if (w->nguards < MAX_GUARDS) {
      w->guards[w->nguards] = cond;
      w->negs[w->nguards++] = neg;
   }
}

static void vhdl_walk_stmt(walk_t *w, tree_t s, const from_t *from)
{
   switch (tree_kind(s)) {
   case T_SIGNAL_ASSIGN:
   case T_VAR_ASSIGN:
      vhdl_walk_assign(w, s, from);
      break;

   case T_COND_ASSIGN:
      {
         if (!tree_has_target(s) || !vhdl_is_target(w, tree_target(s)))
            break;
         const unsigned saved = w->nguards;
         w->fallback = *tree_loc(s);
         for (unsigned i = 0; i < tree_conds(s); i++) {
            tree_t c = tree_cond(s, i);
            const unsigned inner = w->nguards;
            if (tree_has_value(c))
               vhdl_push_guard(w, tree_value(c), false);
            vhdl_walk_stmts(w, c, from, false);
            w->nguards = inner;
            if (tree_has_value(c))
               vhdl_push_guard(w, tree_value(c), true);
         }
         w->nguards = saved;
      }
      break;

   case T_IF:
      {
         const unsigned saved = w->nguards;
         const bool reset_if = vhdl_reset_if_p(w, s);
         for (unsigned i = 0; i < tree_conds(s); i++) {
            tree_t c = tree_cond(s, i);
            const unsigned inner = w->nguards;
            tree_t clk;
            bool rising;
            const bool edge = tree_has_value(c)
               && vhdl_edge(tree_value(c), &clk, &rising);
            const bool skip = edge || (reset_if && i == 0);
            if (tree_has_value(c) && !skip)
               vhdl_push_guard(w, tree_value(c), false);
            vhdl_walk_stmts(w, c, from, false);
            w->nguards = inner;
            if (tree_has_value(c) && !skip)
               vhdl_push_guard(w, tree_value(c), true);
         }
         w->nguards = saved;
      }
      break;

   case T_CASE:
   case T_MATCH_CASE:
      if (s == w->cs) {
         w->case_seen = true;
         for (unsigned i = 0; i < tree_stmts(s); i++) {
            tree_t alt = tree_stmt(s, i);
            ident_t names[64];
            unsigned nnames = 0;
            bool others = false;
            for (unsigned j = 0; j < tree_choices(alt); j++) {
               tree_t ch = tree_choice(alt, j);
               if (vhdl_others_p(ch))
                  others = true;
               else if (tree_has_name(ch)) {
                  ident_t key;
                  char *spell;
                  if (vhdl_state_name(w, tree_name(ch), &key, &spell)) {
                     state_t *st = vhdl_state(w, key, spell, NULL);
                     free(spell);
                     if (st != NULL && nnames < ARRAY_LEN(names))
                        names[nnames++] = key;
                  }
               }
            }
            from_t f = { names, nnames, others, others ? "default" : "case" };
            vhdl_walk_stmts(w, alt, &f, true);
         }
      }
      else {
         // Another case: its choices become guards
         tree_t subject = tree_value(s);
         (void)subject;
         for (unsigned i = 0; i < tree_stmts(s); i++) {
            const unsigned saved = w->nguards;
            tree_t alt = tree_stmt(s, i);
            if (tree_choices(alt) > 0) {
               tree_t ch = tree_choice(alt, 0);
               if (tree_has_name(ch))
                  vhdl_push_guard(w, tree_name(ch), false);
            }
            vhdl_walk_stmts(w, alt, from, false);
            w->nguards = saved;
         }
      }
      break;

   case T_FOR:
   case T_WHILE:
   case T_LOOP:
   case T_SEQUENCE:
      vhdl_walk_stmts(w, s, from, false);
      break;

   default:
      break;
   }
}

static void vhdl_walk_stmts(walk_t *w, tree_t parent, const from_t *from,
                            bool in_case)
{
   (void)in_case;
   const unsigned n = tree_stmts(parent);
   for (unsigned i = 0; i < n; i++)
      vhdl_walk_stmt(w, tree_stmt(parent, i), from);
}

static bool vhdl_literal_assign_p(walk_t *w, tree_t s)
{
   tree_t target = tree_has_target(s) ? tree_target(s) : NULL;
   if (target == NULL || !vhdl_is_target(w, target))
      return false;

   tree_t value = NULL;
   if (tree_kind(s) == T_VAR_ASSIGN)
      value = tree_has_value(s) ? tree_value(s) : NULL;
   else if (tree_kind(s) == T_SIGNAL_ASSIGN && tree_waveforms(s) > 0) {
      tree_t wv = tree_waveform(s, 0);
      value = tree_has_value(wv) ? tree_value(wv) : NULL;
   }

   ident_t key;
   char *spell;
   if (value != NULL && vhdl_state_name(w, value, &key, &spell)) {
      free(spell);
      return true;
   }
   return false;
}

// Does any statement below PARENT assign a state literal to the target
static bool vhdl_assigns_literal(walk_t *w, tree_t parent)
{
   const unsigned n = tree_stmts(parent);
   for (unsigned i = 0; i < n; i++) {
      tree_t s = tree_stmt(parent, i);
      switch (tree_kind(s)) {
      case T_SIGNAL_ASSIGN:
      case T_VAR_ASSIGN:
         if (vhdl_literal_assign_p(w, s))
            return true;
         break;
      case T_IF:
      case T_COND_ASSIGN:
         for (unsigned j = 0; j < tree_conds(s); j++)
            if (vhdl_assigns_literal(w, tree_cond(s, j)))
               return true;
         break;
      case T_CASE:
      case T_MATCH_CASE:
      case T_ALTERNATIVE:
      case T_FOR:
      case T_WHILE:
      case T_LOOP:
      case T_SEQUENCE:
         if (vhdl_assigns_literal(w, s))
            return true;
         break;
      default:
         break;
      }
   }
   return false;
}

// Find the reset assignments to the register of the machine in PARENT
static void vhdl_add_reset(walk_t *w, tree_t assign, tree_t cond)
{
   if (w->nresets < MAX_RESETS) {
      w->resets[w->nresets].assign = assign;
      w->resets[w->nresets++].cond = cond;
   }
}

static void vhdl_reset_literals(walk_t *w, obj_t reg, tree_t parent,
                                tree_t cond)
{
   // The literal assignments to the register directly in PARENT
   const unsigned n = tree_stmts(parent);
   for (unsigned i = 0; i < n; i++) {
      tree_t s = tree_stmt(parent, i);
      if (tree_kind(s) != T_SIGNAL_ASSIGN && tree_kind(s) != T_VAR_ASSIGN)
         continue;
      obj_t save = w->target;
      w->target = reg;
      const bool lit = vhdl_literal_assign_p(w, s);
      w->target = save;
      if (lit)
         vhdl_add_reset(w, s, cond);
   }
}

static bool vhdl_has_edge_below(tree_t parent);

static bool vhdl_stmt_edge(tree_t s)
{
   if (tree_kind(s) != T_IF)
      return false;
   for (unsigned i = 0; i < tree_conds(s); i++) {
      tree_t c = tree_cond(s, i);
      tree_t clk;
      bool rising;
      if (tree_has_value(c) && vhdl_edge(tree_value(c), &clk, &rising))
         return true;
      if (vhdl_has_edge_below(c))
         return true;
   }
   return false;
}

static bool vhdl_has_edge_below(tree_t parent)
{
   for (unsigned i = 0; i < tree_stmts(parent); i++)
      if (vhdl_stmt_edge(tree_stmt(parent, i)))
         return true;
   return false;
}

static void vhdl_find_resets(walk_t *w, obj_t reg, tree_t parent, int depth)
{
   if (depth > 6)
      return;

   for (unsigned i = 0; i < tree_stmts(parent); i++) {
      tree_t s = tree_stmt(parent, i);
      if (tree_kind(s) != T_IF)
         continue;

      // Find the branch with the clock edge, directly or nested
      int edge = -1;
      for (unsigned j = 0; j < tree_conds(s) && edge < 0; j++) {
         tree_t c = tree_cond(s, j);
         tree_t clk;
         bool rising;
         if (tree_has_value(c) && vhdl_edge(tree_value(c), &clk, &rising))
            edge = j;
         else if (vhdl_has_edge_below(c))
            edge = j;
      }

      if (edge < 0)
         continue;

      // Branches before the edge are asynchronous resets
      for (int j = 0; j < edge; j++) {
         tree_t c = tree_cond(s, j);
         vhdl_reset_literals(w, reg, c, tree_has_value(c) ? tree_value(c) : NULL);
      }
      if (edge > 0 && w->nreset_ifs < MAX_RESETS)
         w->reset_ifs[w->nreset_ifs++] = s;

      tree_t ec = tree_cond(s, edge);
      tree_t clk;
      bool rising;
      if (tree_has_value(ec) && vhdl_edge(tree_value(ec), &clk, &rising)) {
         // Synchronous reset: an if directly under the edge whose first
         // branch holds the literal and which has another branch
         for (unsigned j = 0; j < tree_stmts(ec); j++) {
            tree_t inner = tree_stmt(ec, j);
            if (tree_kind(inner) != T_IF || tree_conds(inner) < 2)
               continue;
            tree_t c0 = tree_cond(inner, 0);
            const unsigned before = w->nresets;
            vhdl_reset_literals(w, reg, c0,
                                tree_has_value(c0) ? tree_value(c0) : NULL);
            if (w->nresets > before && w->nreset_ifs < MAX_RESETS)
               w->reset_ifs[w->nreset_ifs++] = inner;
            break;
         }
      }
      else
         vhdl_find_resets(w, reg, ec, depth + 1);
      return;
   }
}

static void vhdl_free_states(walk_t *w)
{
   for (unsigned i = 0; i < w->nstates; i++) {
      free(w->states[i].spell);
      free(w->states[i].value);
   }
}

static char *vhdl_spelling(tree_t decl)
{
   const char *name = istr(tree_ident(decl));
   const loc_t *loc = tree_loc(decl);
   if (name[0] != '\\' && !loc_invalid_p(loc)) {
      // As written at the declaration
      const char *src = loc_get_source(loc);
      const size_t n = strlen(name);
      if (src != NULL) {
         unsigned cur = loc->first_line;
         const unsigned end = loc->first_line + loc->line_delta;
         const char *ptr = src;
         while (cur <= end && cur < loc->first_line + 40) {
            const char *eol = strchr(ptr, '\n');
            size_t linelen = eol ? (size_t)(eol - ptr) : strlen(ptr);
            for (size_t i = cur == loc->first_line ? loc->first_column : 0;
                 i + n <= linelen; i++) {
               if (i > 0 && fsm_word_char(ptr[i - 1]))
                  continue;
               if (i + n < linelen && fsm_word_char(ptr[i + n]))
                  continue;
               if (strncasecmp(ptr + i, name, n) == 0)
                  return xstrndup(ptr + i, n);
            }
            if (eol == NULL)
               break;
            ptr = eol + 1;
            cur++;
         }
      }
   }
   return xstrdup(name);
}

// The label of a process as written, or NULL when it has none
static char *vhdl_label(tree_t proc)
{
   if (!tree_has_ident(proc))
      return NULL;
   const char *name = istr(tree_ident(proc));
   if (name[0] == '_' || strncmp(name, "line_", 5) == 0)
      return NULL;
   return vhdl_spelling(proc);
}

// Name of an object as written: "r.state" for a field
static char *obj_spell(obj_t o)
{
   char *base = vhdl_spelling(o.decl);
   if (o.field == NULL)
      return base;
   char *fld = vhdl_spelling(o.field);
   char *name = xasprintf("%s.%s", base, fld);
   free(base);
   free(fld);
   return name;
}

static bool vhdl_machine(tree_t unit, scope_t *sc, case_info_t *ci,
                         obj_t state, obj_t target, bool two_process,
                         proc_info_t *reg_proc, bool consts, text_buf_t *out,
                         unsigned *count)
{
   walk_t *w = xcalloc(sizeof(walk_t));
   w->consts = consts;
   w->state = state;
   w->target = target;
   w->cs = ci->cs;

   type_t type = obj_type(state);
   w->typed = !consts && type_kind(type) == T_ENUM
      && !vhdl_predefined_p(type);
   w->type = type;

   if (w->typed) {
      for (unsigned i = 0; i < type_enum_literals(type); i++) {
         tree_t lit = type_enum_literal(type, i);
         char *sp = vhdl_spelling(lit);
         vhdl_state(w, tree_ident(lit), sp, tree_loc(lit));
         free(sp);
      }
      if (w->nstates < 2) {
         free(w);
         return false;
      }
   }

   if (!vhdl_assigns_literal(w, ci->cs)) {
      vhdl_free_states(w);
      free(w);
      return false;
   }

   // Branch of each state
   for (unsigned i = 0; i < tree_stmts(ci->cs); i++) {
      tree_t alt = tree_stmt(ci->cs, i);
      for (unsigned j = 0; j < tree_choices(alt); j++) {
         tree_t ch = tree_choice(alt, j);
         ident_t key;
         char *spell;
         if (tree_has_name(ch)
             && vhdl_state_name(w, tree_name(ch), &key, &spell)) {
            state_t *st = vhdl_state(w, key, spell, NULL);
            free(spell);
            if (st != NULL && !st->has_branch) {
               st->branch = *tree_loc(alt);
               st->has_branch = true;
            }
         }
      }
   }

   tree_t proc = ci->proc->proc;
   char *typename LOCAL = NULL;
   {
      LOCAL_TEXT_BUF tb = tb_new();
      tree_t d = state.field ?: state.decl;
      char *nm LOCAL = vhdl_spelling(d);
      if (vhdl_sym_decl_text(tree_loc(d), nm, tb) && tb_len(tb) > 0)
         typename = xstrdup(tb_get(tb));
      else if (w->typed && type_has_ident(type))
         typename = xstrdup(vhdl_tail(type_ident(type)));
   }

   fsm_out_t *f = fsm_begin(out, count, istr(tree_ident(unit)),
                            istr(tree_ident(unit)), w->consts ? "constants" : "enum",
                            two_process ? "two-process" : "one-process",
                            w->typed || w->consts);
   fsm_set_vhdl(f, true);
   w->out = f;

   {
      char *nm LOCAL = obj_spell(state);
      // A field is located where the register is selected in the case
      fsm_object(f, "state", nm, state.field ? tree_loc(tree_value(ci->cs))
                 : tree_loc(state.decl), typename);
   }
   if (!obj_eq(target, state)) {
      char *nm LOCAL = obj_spell(target);
      fsm_object(f, "next", nm, tree_loc(target.decl), NULL);
   }

   proc_info_t *clk_proc = two_process ? reg_proc : ci->proc;
   if (clk_proc->clocked) {
      char *clk LOCAL = vhdl_text(clk_proc->clk);
      fsm_clock(f, clk ?: istr(tree_ident(clk_proc->clk)), clk_proc->rising);
   }

   if (two_process) {
      const loc_t *l = tree_loc(clk_proc->proc);
      char *label LOCAL = vhdl_label(clk_proc->proc);
      fsm_process(f, "register", label, l);
      fsm_process(f, "next", NULL, tree_loc(proc));
   }
   else
      fsm_process(f, "register", NULL, tree_loc(proc));
   fsm_case(f, tree_loc(ci->cs));

   // Reset assignments to the register of the machine
   vhdl_find_resets(w, state, clk_proc->proc, 0);

   // The states first seen as targets are added during the walk, so walk
   // the process once before writing them
   fsm_out_t *saved = w->out;
   (void)saved;

   // Resets
   for (unsigned i = 0; i < w->nresets; i++) {
      tree_t s = w->resets[i].assign;
      tree_t value;
      if (tree_kind(s) == T_VAR_ASSIGN)
         value = tree_value(s);
      else
         value = tree_value(tree_waveform(s, 0));
      ident_t key;
      char *spell;
      if (!vhdl_state_name(w, value, &key, &spell))
         continue;
      state_t *st = vhdl_state(w, key, spell, NULL);
      free(spell);
      if (st == NULL)
         continue;
      fsm_trans_begin(f, "reset", tree_loc(s), st->spell, NULL);
      fsm_trans_from(f, "*");
      if (w->resets[i].cond != NULL) {
         char *txt LOCAL = vhdl_text(w->resets[i].cond);
         fsm_trans_guard(f, txt ?: "?", tree_loc(w->resets[i].cond), false);
      }
      fsm_trans_end(f);
   }

   // Transitions of the machine: the whole process holding the case
   {
      from_t any = { NULL, 1, false, "any" };
      ident_t star[1] = { NULL };
      any.from = star;
      // Statements outside the case: the guards of enclosing conditions
      // apply to the case as well
      vhdl_walk_stmts(w, proc, &any, false);
   }

   // States first seen as targets only
   if (w->consts) {
      // In declaration order
      for (unsigned i = 1; i < w->nstates; i++) {
         for (unsigned j = i; j > 0; j--) {
            state_t *p = &w->states[j - 1], *q = &w->states[j];
            const bool before = q->has_decl && p->has_decl
               && (q->decl.first_line < p->decl.first_line
                   || (q->decl.first_line == p->decl.first_line
                       && q->decl.first_column < p->decl.first_column));
            if (!before)
               break;
            state_t tmp = *p;
            *p = *q;
            *q = tmp;
         }
      }
   }

   for (unsigned i = 0; i < w->nstates; i++) {
      state_t *st = &w->states[i];
      fsm_state(f, st->spell, st->has_decl ? &st->decl : NULL, st->value,
                st->has_branch ? &st->branch : NULL);
   }

   const char *initial = NULL;
   char *init LOCAL = NULL;
   if (state.field == NULL && tree_kind(state.decl) != T_PORT_DECL
       && tree_has_value(state.decl)) {
      ident_t key;
      char *spell;
      if (vhdl_state_name(w, tree_value(state.decl), &key, &spell)) {
         init = spell;
         initial = init;
      }
   }

   if (w->computed)
      fsm_note(f, "a next state is computed");

   if ((w->ntrans == 0 && w->nresets == 0)
       || (w->consts && w->nstates < 3
           && !(w->nstates == 2 && w->nresets > 0))) {
      fsm_discard(f);
      vhdl_free_states(w);
      free(w);
      return false;
   }

   (void)sc;
   fsm_end(f, initial);
   vhdl_free_states(w);
   free(w);
   return true;
}

static bool vhdl_seen(const obj_t *seen, unsigned n, obj_t o)
{
   for (unsigned i = 0; i < n; i++)
      if (obj_eq(seen[i], o))
         return true;
   return false;
}

typedef struct {
   obj_t        target;
   bool         two;
   proc_info_t *proc;
} cand_t;

#define MAX_CANDS 12

static void vhdl_add_cand(cand_t *cands, unsigned *n, obj_t target, bool two,
                          proc_info_t *proc)
{
   for (unsigned i = 0; i < *n; i++)
      if (obj_eq(cands[i].target, target))
         return;
   if (*n < MAX_CANDS)
      cands[(*n)++] = (cand_t){ target, two, proc };
}

static unsigned vhdl_skipped = 0;

void vhdl_fsm_reset_skipped(void)
{
   vhdl_skipped = 0;
}

unsigned vhdl_fsm_skipped(void)
{
   return vhdl_skipped;
}

void vhdl_fsm_scan(tree_t unit, text_buf_t *out, unsigned *count)
{
   if (tree_kind(unit) != T_ARCH)
      return;

   scope_t sc = {};
   vhdl_collect_scope(&sc, unit);

   obj_t *seen = xmalloc_array(MAX(sc.cases.count, 1), sizeof(obj_t));
   unsigned nseen = 0;

   for (unsigned i = 0; i < sc.cases.count; i++) {
      case_info_t *ci = &(sc.cases.items[i]);
      tree_t value = tree_value(ci->cs);
      obj_t subject;
      if (!vhdl_obj(value, &subject))
         continue;

      // A state graph with missing transitions is worse than none
      if (parse_recovery() && lint_span_syntax_damaged(tree_loc(ci->cs))) {
         vhdl_skipped++;
         continue;
      }

      type_t type = obj_type(subject);
      const bool enum_type = type_kind(type) == T_ENUM
         && !vhdl_predefined_p(type);
      bool consts = false;

      // Not an enumeration: a vector or integer whose choices are all
      // named constants
      if (!enum_type) {
         consts = true;
         bool any = false;
         for (unsigned a = 0; a < tree_stmts(ci->cs) && consts; a++) {
            tree_t alt = tree_stmt(ci->cs, a);
            for (unsigned c = 0; c < tree_choices(alt); c++) {
               tree_t ch = tree_choice(alt, c);
               if (vhdl_others_p(ch))
                  continue;
               tree_t v = tree_has_name(ch) ? vhdl_strip(tree_name(ch)) : NULL;
               if (v == NULL || tree_kind(v) != T_REF || !tree_has_ref(v)
                   || tree_kind(tree_ref(v)) != T_CONST_DECL) {
                  consts = false;
                  break;
               }
               any = true;
            }
         }
         consts = consts && any;
      }

      if (!enum_type && !consts && !type_is_none(type))
         continue;

      // The untyped case needs all the choices to be simple names
      if (!enum_type && !consts) {
         bool names = true;
         for (unsigned a = 0; a < tree_stmts(ci->cs) && names; a++) {
            tree_t alt = tree_stmt(ci->cs, a);
            for (unsigned c = 0; c < tree_choices(alt); c++) {
               tree_t ch = tree_choice(alt, c);
               if (vhdl_others_p(ch))
                  continue;
               tree_t v = tree_has_name(ch) ? vhdl_strip(tree_name(ch)) : NULL;
               if (v == NULL || tree_kind(v) != T_REF || tree_has_ref(v)
                   || !tree_has_ident(v)
                   || !isalpha((unsigned char)istr(tree_ident(v))[0])) {
                  names = false;
                  break;
               }
            }
         }
         if (!names)
            continue;
      }

      // A variable holding a copy of a record (v := r) stands for it
      obj_t reg = subject;
      if (subject.field != NULL && tree_kind(subject.decl) == T_VAR_DECL) {
         for (unsigned r = 0; r < sc.regs.count; r++) {
            reg_assign_t *ra = &(sc.regs.items[r]);
            if (ra->has_partner && ra->target.field == NULL
                && ra->target.decl == subject.decl && ra->partner.field == NULL
                && ra->partner.decl != subject.decl) {
               reg = (obj_t){ ra->partner.decl, subject.field };
               break;
            }
         }
      }

      if (vhdl_seen(seen, nseen, reg))
         continue;

      // Registered?  One process: the case is in the clocked region and
      // the register is assigned there.  Two processes: a clocked process
      // assigns the register from a partner, which may itself be copied
      // from another object (a variable holding the next state).
      cand_t cands[MAX_CANDS];
      unsigned ncands = 0;

      if (ci->in_region) {
         for (unsigned r = 0; r < sc.regs.count; r++) {
            if (sc.regs.items[r].proc == ci->proc
                && sc.regs.items[r].in_region
                && obj_eq(sc.regs.items[r].target, reg)) {
               vhdl_add_cand(cands, &ncands, reg, false, NULL);
               break;
            }
         }
      }

      bool have_first = false;
      cand_t first = {};
      obj_t chain[4] = { reg };
      unsigned nchain = 1;
      for (unsigned depth = 0; depth < 3 && depth < nchain; depth++) {
         for (unsigned r = 0; r < sc.regs.count; r++) {
            reg_assign_t *ra = &(sc.regs.items[r]);
            if (!ra->has_partner || (depth == 0 && !ra->in_region))
               continue;
            const bool exact = obj_eq(ra->target, chain[depth]);
            const bool whole = ra->target.field == NULL
               && ra->partner.field == NULL
               && ra->target.decl == chain[depth].decl;
            if (!exact && !whole)
               continue;
            obj_t partner = exact ? ra->partner
               : (obj_t){ ra->partner.decl, chain[depth].field };
            const bool same = ra->proc == ci->proc;
            if (depth == 0) {
               vhdl_add_cand(cands, &ncands, partner, !same,
                             same ? NULL : ra->proc);
               if (!have_first) {
                  first = (cand_t){ partner, !same, same ? NULL : ra->proc };
                  have_first = true;
               }
            }
            else if (have_first)
               vhdl_add_cand(cands, &ncands, partner, first.two, first.proc);
            if (nchain < ARRAY_LEN(chain))
               chain[nchain++] = partner;
         }
      }
      if (!obj_eq(subject, reg))
         vhdl_add_cand(cands, &ncands, subject, false, NULL);

      for (unsigned k = 0; k < ncands; k++) {
         if (vhdl_machine(unit, &sc, ci, reg, cands[k].target, cands[k].two,
                          cands[k].proc, consts, out, count)) {
            seen[nseen++] = reg;
            break;
         }
      }
   }

   free(seen);
   for (unsigned i = 0; i < sc.procs.count; i++)
      free(sc.procs.items[i]);
   ACLEAR(sc.cases);
   ACLEAR(sc.regs);
   ACLEAR(sc.procs);
}
