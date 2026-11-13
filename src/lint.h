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


#ifndef _LINT_H
#define _LINT_H

#include "prim.h"

// Rules of the lint pass enabled by the global --lint option
typedef enum {
   LINT_UNUSED   = (1 << 0),   // Declared but never read
   LINT_UNDRIVEN = (1 << 1),   // Read but never assigned
   LINT_WIDTH    = (1 << 2),   // Assignment or connection truncates
   LINT_LATCH    = (1 << 3),   // Combinational process infers a latch
   LINT_SENSITIVITY = (1 << 4),   // Signal read but not in sensitivity list
   LINT_ALL      = (1 << 5) - 1
} lint_rule_t;

// Parse the argument of --lint: a comma separated list of rule names,
// "all", "none" or a rule name preceded by a minus sign to remove it
void lint_parse_option(const char *arg);

bool lint_enabled(lint_rule_t rule);

// True if a comment "nvc lint_off RULE" is on the line of the location
// or the line before it
bool lint_suppressed(const loc_t *loc, lint_rule_t rule);

// True if the name is written at the location, which is not so for
// declarations the compiler makes up
bool lint_name_at(const loc_t *loc, const char *name);

// Print the available rules, one per line, for --lint=list
void lint_list_rules(void);

// A warning tagged with the rule name for the caller to add hints and
// fixes to and emit, or NULL if the rule is disabled or suppressed
diag_t *lint_diag(const loc_t *loc, lint_rule_t rule);

// Emit a warning tagged with the rule name unless the rule is disabled or
// suppressed by a comment at the location
void lint_warn(const loc_t *loc, lint_rule_t rule, const char *fmt, ...)
   __attribute__((format(printf, 3, 4)));

// Declarations found while elaborating are reported once the whole design
// has been seen so that hierarchical names can be taken into account
void lint_declaration(const loc_t *loc, lint_rule_t rule, ident_t name,
                      const char *what);
void lint_declaration_written(const loc_t *loc, lint_rule_t rule, ident_t name,
                              const char *what, bool written);
void lint_hier_name(ident_t name);
void lint_flush(void);

// True the first time it is called with this key
bool lint_first_visit(const void *key);

#endif  // _LINT_H
