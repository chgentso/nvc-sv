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
#include "diag.h"
#include "fsm.h"
#include "scandeps.h"
#include "tree.h"
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

char *fsm_source_text(const loc_t *loc)
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

void vhdl_fsm_scan(tree_t unit, text_buf_t *out, unsigned *count)
{
   // The walker follows in a later commit
}
