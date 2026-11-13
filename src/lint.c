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
#include "hash.h"
#include "ident.h"
#include "lint.h"
#include "option.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
   loc_t       loc;
   lint_rule_t rule;
   ident_t     name;
   const char *what;
   bool        written;   // Removing it would leave assignments
} lint_decl_t;

typedef A(lint_decl_t) decl_list_t;

static decl_list_t  pending = AINIT;
static hset_t      *hier_names = NULL;
static hset_t      *visited = NULL;

static const struct {
   const char  *name;
   lint_rule_t  rule;
   const char  *languages;   // vhdl, verilog or both
   const char  *phase;       // analysis or elaboration
   const char  *description;
} rule_names[] = {
   { "unused",   LINT_UNUSED,   "vhdl,verilog", "elaboration",
     "Declaration that is never read" },
   { "undriven", LINT_UNDRIVEN, "vhdl,verilog", "elaboration",
     "Signal or variable that is read but never assigned" },
   { "width",    LINT_WIDTH,    "verilog",      "analysis",
     "Assignment or connection that truncates a value" },
   { "latch",    LINT_LATCH,    "vhdl,verilog", "analysis",
     "Combinational process that infers a latch" },
   { "sensitivity", LINT_SENSITIVITY, "vhdl", "analysis",
     "Signal read in a process but missing from its sensitivity list" },
};

void lint_list_rules(void)
{
   // One line per rule: name, languages, phase, default and description
   // separated by tabs

   for (int i = 0; i < ARRAY_LEN(rule_names); i++)
      printf("%s\t%s\t%s\toff\t%s\n", rule_names[i].name,
             rule_names[i].languages, rule_names[i].phase,
             rule_names[i].description);
}

static const char *rule_name(lint_rule_t rule)
{
   for (int i = 0; i < ARRAY_LEN(rule_names); i++) {
      if (rule_names[i].rule == rule)
         return rule_names[i].name;
   }

   return "all";
}

static bool lookup_rule(const char *name, size_t len, lint_rule_t *rule)
{
   if (len == 3 && strncasecmp(name, "all", 3) == 0) {
      *rule = LINT_ALL;
      return true;
   }

   for (int i = 0; i < ARRAY_LEN(rule_names); i++) {
      if (strlen(rule_names[i].name) == len
          && strncasecmp(name, rule_names[i].name, len) == 0) {
         *rule = rule_names[i].rule;
         return true;
      }
   }

   return false;
}

void lint_parse_option(const char *arg)
{
   int mask = 0;
   bool first = true;

   if (arg != NULL && strcmp(arg, "list") == 0) {
      lint_list_rules();
      exit(EXIT_SUCCESS);
   }
   else if (arg == NULL || *arg == '\0')
      mask = LINT_ALL;
   else {
      for (const char *p = arg; *p != '\0'; first = false) {
         const char *end = strchr(p, ',') ?: p + strlen(p);

         bool remove = false;
         const char *word = p;
         if (*word == '-' || *word == '+') {
            remove = *word == '-';
            word++;
         }

         lint_rule_t rule;
         if (end - word == 4 && strncasecmp(word, "none", 4) == 0) {
            mask = 0;
            rule = 0;
         }
         else if (!lookup_rule(word, end - word, &rule))
            fatal("invalid lint rule '%.*s': valid rules are unused, "
                  "undriven, width, latch, sensitivity, all and none",
                  (int)(end - p), p);

         if (remove) {
            // A list starting with a removal means all rules but those
            if (first)
               mask = LINT_ALL;
            mask &= ~rule;
         }
         else
            mask |= rule;

         p = *end == ',' ? end + 1 : end;
      }
   }

   opt_set_int(OPT_LINT, mask);
}

bool lint_enabled(lint_rule_t rule)
{
   return (opt_get_int(OPT_LINT) & rule) != 0;
}

static bool line_suppresses(const loc_t *loc, int delta, lint_rule_t rule)
{
   if (loc->first_line < delta)
      return false;

   loc_t at = *loc;
   at.first_line -= delta;
   at.line_delta = 0;

   const char *line = loc_get_source(&at);
   if (line == NULL)
      return false;

   const char *eol = line;
   while (*eol != '\0' && *eol != '\n')
      eol++;

   static const char key[] = "nvc lint_off";
   const size_t klen = sizeof(key) - 1;

   for (const char *p = line; p + klen <= eol; p++) {
      if (strncmp(p, key, klen) != 0)
         continue;

      // An empty list of rules suppresses everything
      const char *w = p + klen;
      bool any = false;
      for (;;) {
         while (w < eol && (*w == ' ' || *w == '\t' || *w == ','))
            w++;

         const char *start = w;
         while (w < eol && (isalnum((unsigned char)*w) || *w == '_'))
            w++;

         if (w == start)
            break;

         any = true;

         lint_rule_t named;
         if (lookup_rule(start, w - start, &named) && (named & rule))
            return true;
      }

      if (!any)
         return true;
   }

   return false;
}

bool lint_suppressed(const loc_t *loc, lint_rule_t rule)
{
   if (loc_invalid_p(loc))
      return false;

   return line_suppresses(loc, 0, rule) || line_suppresses(loc, 1, rule);
}

bool lint_name_at(const loc_t *loc, const char *name)
{
   // Declarations made up by the compiler are not at a place where
   // their name is written
   const char *line = loc_get_source(loc);
   if (line == NULL)
      return true;

   const char *dollar = strchr(name, '$');
   const size_t len = dollar != NULL ? dollar - name : strlen(name);

   for (unsigned i = 0; i < loc->first_column; i++) {
      if (line[i] == '\0' || line[i] == '\n')
         return true;
   }

   return strncasecmp(line + loc->first_column, name, len) == 0;
}

diag_t *lint_diag(const loc_t *loc, lint_rule_t rule)
{
   if (!lint_enabled(rule) || lint_suppressed(loc, rule))
      return NULL;

   diag_t *d = diag_new(DIAG_WARN, loc);
   diag_rule(d, rule_name(rule));
   return d;
}

void lint_warn(const loc_t *loc, lint_rule_t rule, const char *fmt, ...)
{
   diag_t *d = lint_diag(loc, rule);
   if (d == NULL)
      return;

   va_list ap;
   va_start(ap, fmt);
   diag_vprintf(d, fmt, ap);
   va_end(ap);

   diag_emit(d);
}

void lint_declaration(const loc_t *loc, lint_rule_t rule, ident_t name,
                      const char *what)
{
   lint_declaration_written(loc, rule, name, what, false);
}

void lint_declaration_written(const loc_t *loc, lint_rule_t rule, ident_t name,
                              const char *what, bool written)
{
   if (!lint_enabled(rule))
      return;

   lint_decl_t d = { *loc, rule, name, what, written };
   APUSH(pending, d);
}

void lint_hier_name(ident_t name)
{
   if (hier_names == NULL)
      hier_names = hset_new(64);

   hset_insert(hier_names, name);
}

bool lint_first_visit(const void *key)
{
   if (visited == NULL)
      visited = hset_new(64);

   if (hset_contains(visited, key))
      return false;

   hset_insert(visited, key);
   return true;
}

static bool lint_removable_signal(const loc_t *loc, loc_t *line_loc)
{
   // True if the VHDL signal declaration of a single name is alone on its
   // line so that the line can be removed, and the location of the line
   // without its newline

   const char *line = loc_get_source(loc);
   if (line == NULL)
      return false;

   const char *eol = line;
   while (*eol != '\0' && *eol != '\n')
      eol++;

   // Only "signal" before the name
   const char *p = line;
   while (p < eol && isspace((unsigned char)*p))
      p++;
   if (eol - p < 7 || strncasecmp(p, "signal", 6) != 0
       || !isspace((unsigned char)p[6]))
      return false;

   p += 6;
   while (p < eol && isspace((unsigned char)*p))
      p++;
   if (p != line + loc->first_column)
      return false;

   // The declaration has a single name and ends on this line
   const char *colon = NULL;
   for (const char *q = p; q < eol; q++) {
      if (*q == ',')
         return false;
      else if (*q == ':') {
         colon = q;
         break;
      }
   }

   if (colon == NULL)
      return false;

   const char *semi = memchr(colon, ';', eol - colon);
   if (semi == NULL)
      return false;

   // Nothing but a comment follows
   const char *r = semi + 1;
   while (r < eol && isspace((unsigned char)*r))
      r++;
   if (r != eol && !(eol - r >= 2 && r[0] == '-' && r[1] == '-'))
      return false;

   // No other quote or comment before the semicolon that could hide it
   for (const char *q = colon; q < semi; q++) {
      if (*q == '-' && q[1] == '-')
         return false;
   }

   const size_t len = eol - line;
   if (len == 0)
      return false;

   *line_loc = get_loc(loc->first_line, 0, loc->first_line, len - 1,
                       loc->file_ref);
   return !loc_invalid_p(line_loc);
}

static bool lint_removable_vlog_decl(const loc_t *loc, loc_t *line_loc)
{
   // True if the Verilog net or variable declaration of a single name is
   // alone on its line so that the line can be removed

   const char *line = loc_get_source(loc);
   if (line == NULL)
      return false;

   const char *eol = line;
   while (*eol != '\0' && *eol != '\n')
      eol++;

   const char *name = line + loc->first_column;
   if (name > eol)
      return false;

   const char *p = line;
   while (p < name && isspace((unsigned char)*p))
      p++;

   // The text before the name is a type with no list, port or initial value
   if (p >= name)
      return false;

   static const char *const not_decl[] = {
      "input", "output", "inout", "parameter", "localparam", "ref", "task",
      "function", "typedef", "module", "interface", "class",
   };

   for (unsigned i = 0; i < ARRAY_LEN(not_decl); i++) {
      const size_t n = strlen(not_decl[i]);
      if (strncmp(p, not_decl[i], n) == 0 && !isalnum((unsigned char)p[n])
          && p[n] != '_')
         return false;
   }

   for (const char *q = p; q < name; q++) {
      if (strchr(",;(){}=/", *q))
         return false;
   }

   // Only the end of the declaration follows
   const char *r = name;
   while (r < eol && (isalnum((unsigned char)*r) || *r == '_' || *r == '$'))
      r++;
   while (r < eol && isspace((unsigned char)*r))
      r++;
   if (r >= eol || *r != ';')
      return false;

   r++;
   while (r < eol && isspace((unsigned char)*r))
      r++;
   if (r != eol && !(eol - r >= 2 && r[0] == '/' && r[1] == '/'))
      return false;

   const size_t len = eol - line;
   *line_loc = get_loc(loc->first_line, 0, loc->first_line, len - 1,
                       loc->file_ref);
   return !loc_invalid_p(line_loc);
}

void lint_flush(void)
{
   for (unsigned i = 0; i < pending.count; i++) {
      const lint_decl_t *d = &(pending.items[i]);

      if (hier_names != NULL && hset_contains(hier_names, d->name))
         continue;   // May be referenced from another scope

      if (lint_suppressed(&d->loc, d->rule))
         continue;

      // Names of variables hoisted out of a process have a suffix
      const char *str = istr(d->name);
      const char *dollar = strchr(str, '$');
      const int len = dollar != NULL ? dollar - str : (int)strlen(str);

      const char *msg = d->rule == LINT_UNUSED
         ? "%s '%.*s' is never read" : "%s '%.*s' is never assigned";

      diag_t *diag = diag_new(DIAG_WARN, &d->loc);
      diag_rule(diag, rule_name(d->rule));
      diag_printf(diag, msg, d->what, len, str);

      loc_t line_loc;
      if (d->rule == LINT_UNUSED && strcmp(d->what, "signal") == 0
          && lint_removable_signal(&d->loc, &line_loc))
         diag_fix(diag, &line_loc, "", "Remove the unused signal");
      else if (d->rule == LINT_UNUSED && !d->written
               && (strcmp(d->what, "net") == 0
                   || strcmp(d->what, "variable") == 0)
               && lint_removable_vlog_decl(&d->loc, &line_loc))
         diag_fix(diag, &line_loc, "", d->what[0] == 'n'
                  ? "Remove the unused net" : "Remove the unused variable");

      diag_emit(diag);
   }

   ACLEAR(pending);

   if (hier_names != NULL) {
      hset_free(hier_names);
      hier_names = NULL;
   }

   if (visited != NULL) {
      hset_free(visited);
      visited = NULL;
   }
}
