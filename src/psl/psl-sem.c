//
//  Copyright (C) 2022-2025  Nick Gasson
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
#include "names.h"
#include "phase.h"
#include "psl/psl-node.h"
#include "psl/psl-phase.h"
#include "type.h"

#include <assert.h>
#include <inttypes.h>

static void psl_check_clocked(psl_node_t p, nametab_t *tab);

static void psl_check_static(tree_t t)
{
   switch (tree_kind(t)) {
   case T_LITERAL:
      return;
   case T_FCALL:
      if (tree_flags(t) & (TREE_F_LOCALLY_STATIC | TREE_F_GLOBALLY_STATIC))
         return;
      break;
   case T_REF:
      if (class_of(tree_ref(t)) == C_CONSTANT)
         return;
      break;
   case T_RANGE:
      {
         const range_kind_t kind = tree_subkind(t);
         if (kind == RANGE_TO || kind == RANGE_DOWNTO) {
            psl_check_static(tree_left(t));
            psl_check_static(tree_right(t));
            return;
         }
      }
      break;

   default:
      break;
   }

   error_at(tree_loc(t), "expression must be static");
}

static void psl_check_clock_decl(psl_node_t p)
{

}

static void psl_check_property_decl(psl_node_t p, nametab_t *tab)
{
   psl_check(psl_value(p), tab);
}

static void psl_check_sequence_decl(psl_node_t p, nametab_t *tab)
{
   psl_check(psl_value(p), tab);
}

static void psl_check_endpoint_decl(psl_node_t p, nametab_t *tab)
{
   psl_check(psl_value(p), tab);

   error_at(psl_loc(p), "PSL endpoint declarations are not supported as they "
            "were not part of IEEE Std 1850-2010 or later standards");
}

static void psl_check_top_level(psl_node_t p, nametab_t *tab)
{
   if (psl_kind(p) == P_CLOCKED)
      psl_check_clocked(p, tab);
   else {
      psl_check(p, tab);

      static bool warned = false;
      if (!warned) {
         diag_t *d = diag_new(DIAG_ERROR, psl_loc(p));
         diag_printf(d, "sorry, unclocked properties are not supported");

         if (!query_name(tab, well_known(W_DEFAULT_CLOCK), NULL))
            diag_hint(d, NULL, "there is no default clock declaration in "
                      "this design unit");

         diag_emit(d);
         warned = true;
      }
   }
}

static void psl_check_assert(psl_node_t p, nametab_t *tab)
{
   psl_check_top_level(psl_value(p), tab);
}

static void psl_check_assume(psl_node_t p, nametab_t *tab)
{
   psl_check_top_level(psl_value(p), tab);
}

static void psl_check_restrict(psl_node_t p, nametab_t *tab)
{
   psl_check_top_level(psl_value(p), tab);
}

static void psl_check_fairness(psl_node_t p)
{

}

static void psl_check_cover(psl_node_t p, nametab_t *tab)
{
   psl_check_top_level(psl_value(p), tab);
}

static bool psl_is_sequence(psl_node_t p)
{
   // A Boolean or a Sequence
   switch (psl_kind(p)) {
   case P_HDL_EXPR:
   case P_SERE:
   case P_REPEAT:
   case P_PARAM_SERE:
      return true;
   case P_CLOCKED:
      return psl_is_sequence(psl_value(p));
   default:
      return false;
   }
}

static void psl_check_always(psl_node_t p, nametab_t *tab)
{
   psl_check(psl_value(p), tab);
}

static void psl_check_never(psl_node_t p, nametab_t *tab)
{
   psl_node_t value = psl_value(p);
   psl_check(value, tab);

   if (!psl_is_sequence(value))
      error_at(psl_loc(p), "property is not in the simple subset as the "
               "operand of never is not a Boolean or Sequence");
}

static void psl_check_eventually(psl_node_t p, nametab_t *tab)
{
   psl_node_t value = psl_value(p);
   psl_check(value, tab);

   if (!psl_is_sequence(value))
      error_at(psl_loc(p), "property is not in the simple subset as the "
               "operand of %s is not a Boolean or Sequence",
               (psl_flags(p) & PSL_F_LTL) ? "F" : "eventually!");
}

static void psl_check_boolean(psl_node_t p, nametab_t *tab)
{
   tree_t value = solve_psl_condition(tab, psl_tree(p));

   type_t type = get_type_or_null(value);
   if (type != NULL && type_is_none(type))
      return;

   psl_set_tree(p, value);   // May be replaced with condition conversion

   if (!sem_check(value, tab))
      return;

   type_t std_bool = std_type(NULL, STD_BOOLEAN);
   bool ok = type_eq(type, std_bool);

   if (!ok && standard() < STD_08) {
      // Later standards use condition conversion operator
      type_t std_bit = std_type(NULL, STD_BIT);
      type_t ulogic = ieee_type(IEEE_STD_ULOGIC);

      ok = type_eq(type, std_bit) || type_eq(type, ulogic);
   }

   if (!ok)
      error_at(tree_loc(value), "expression must be a PSL Boolean but "
               "have type %pT", type);
}

static void psl_check_number(psl_node_t p, nametab_t *tab)
{
   type_t std_int = std_type(NULL, STD_INTEGER);
   tree_t value = solve_types(tab, psl_tree(p), std_int);
   psl_set_tree(p, value);

   type_t type = get_type_or_null(value);
   if (type != NULL && type_is_none(type))
      return;   // Prevent cascading errors

   if (!sem_check(value, tab))
      return;

   if (!type_eq(type, std_int)) {
      error_at(tree_loc(value), "expression must be a PSL Number but have "
               "type %pT", type);
      return;
   }

   psl_check_static(value);
}

static void psl_check_bit(psl_node_t p, nametab_t *tab)
{
   type_t std_bit = std_type(NULL, STD_BIT);
   type_t std_ulogic = ieee_type(IEEE_STD_ULOGIC);
   tree_t value = solve_psl_bit(tab, psl_tree(p));
   psl_set_tree(p, value);

   type_t type = get_type_or_null(value);
   if (type != NULL && type_is_none(type))
      return;   // Prevent cascading errors

   if (!sem_check(value, tab))
      return;

   if (!type_eq(type, std_ulogic) && !type_eq(type, std_bit))
      error_at(tree_loc(value), "expression must be a PSL Bit but have "
               "type %pT", type);
}

static void psl_check_any(psl_node_t p, nametab_t *tab)
{
   tree_t value = solve_types(tab, psl_tree(p), NULL);
   psl_set_tree(p, value);

   type_t type = get_type_or_null(value);
   if (type != NULL && type_is_none(type))
      return;   // Prevent cascading errors

   if (!sem_check(value, tab))
      return;
}

static void psl_check_hdl_expr(psl_node_t p, nametab_t *tab)
{
   switch (psl_type(p)) {
   case PSL_TYPE_BOOLEAN:
      psl_check_boolean(p, tab);
      break;
   case PSL_TYPE_NUMERIC:
      psl_check_number(p, tab);
      break;
   case PSL_TYPE_BIT:
      psl_check_bit(p, tab);
      break;
   case PSL_TYPE_ANY:
      psl_check_any(p, tab);
      break;
   default:
      should_not_reach_here();
   }
}

static void psl_check_range(psl_node_t p, nametab_t *tab)
{
   psl_node_t low = psl_left(p);
   psl_check(low, tab);

   psl_node_t high = psl_right(p);
   psl_check(high, tab);

   int64_t low_i, high_i;

   bool is_num = true;
   is_num &= folded_int(psl_tree(low), &low_i);
   is_num &= folded_int(psl_tree(high), &high_i);

   if (is_num && (low_i > high_i))
      error_at(psl_loc(p), "left bound of PSL range (%" PRIi64 ") must be "
                           "lower than right bound (%" PRIi64 ")",
                           low_i, high_i);
}

static void psl_check_property_inst(psl_node_t p)
{

}

static void psl_check_sequence_inst(psl_node_t p)
{

}

static void psl_check_sere(psl_node_t p, nametab_t *tab)
{
   const int nops = psl_operands(p);
   for (int i = 0; i < nops; i++)
      psl_check(psl_operand(p, i), tab);
}

static void psl_check_repeat(psl_node_t p, nametab_t *tab)
{
   psl_node_t value = psl_value(p);
   psl_check(value, tab);

   if (psl_has_delay(p))
      psl_check(psl_delay(p), tab);

   switch (psl_subkind(p)) {
   case PSL_GOTO_REPEAT:
      if (psl_kind(value) != P_HDL_EXPR)
         error_at(psl_loc(p), "operand of goto repetition operator must "
                  "be Boolean");
      break;

   case PSL_EQUAL_REPEAT:
      if (psl_kind(value) != P_HDL_EXPR)
         error_at(psl_loc(p), "operand of non-consecutive repetition "
                  "operator must be Boolean");
      break;
   }
}

static void psl_check_logical(psl_node_t p, nametab_t *tab)
{
   if (psl_subkind(p) == PSL_LOGIC_NOT) {
      psl_node_t operand = psl_operand(p, 0);
      psl_check(operand, tab);

      if (psl_kind(operand) != P_HDL_EXPR)
         error_at(psl_loc(p), "property is not in the simple subset as the "
                  "operand of this logical not is non-Boolean");
      return;
   }

   assert(psl_operands(p) == 2);

   psl_node_t left = psl_operand(p, 0);
   psl_check(left, tab);

   psl_node_t right = psl_operand(p, 1);
   psl_check(right, tab);

   switch (psl_subkind(p)) {
   case PSL_LOGIC_IFF:
      if (psl_kind(right) != P_HDL_EXPR)
         error_at(psl_loc(p), "property is not in the simple subset as the "
                  "right hand side of this implication is non-Boolean");
      // Fall-through
   case PSL_LOGIC_IF:
      if (psl_kind(left) != P_HDL_EXPR)
         error_at(psl_loc(p), "property is not in the simple subset as the "
                  "left hand side of this implication is non-Boolean");
      break;

   case PSL_LOGIC_OR:
      if (psl_kind(left) != P_HDL_EXPR && psl_kind(right) != P_HDL_EXPR)
         error_at(psl_loc(p), "property is not in the simple subset as both "
                  "operands of this logical OR are non-Boolean");
      break;

   case PSL_LOGIC_AND:
   case PSL_LOGIC_NOT:
      break;
   }
}

static void psl_check_next(psl_node_t p, nametab_t *tab)
{
   if (psl_has_delay(p))
      psl_check(psl_delay(p), tab);

   psl_check(psl_value(p), tab);
}

static void psl_check_next_event(psl_node_t p, nametab_t *tab)
{
   psl_check(psl_operand(p, 0), tab);

   if (psl_has_delay(p))
      psl_check(psl_delay(p), tab);

   psl_check(psl_value(p), tab);
}

static void psl_check_next_event_e(psl_node_t p, nametab_t *tab)
{
   psl_check_next_event(p, tab);

   if (psl_kind(psl_value(p)) != P_HDL_EXPR)
      error_at(psl_loc(p), "property is not in the simple subset as the "
               "operand of this next_event_e operator is non-Boolean");
}

static void psl_check_next_e(psl_node_t p, nametab_t *tab)
{
   psl_check(psl_delay(p), tab);

   psl_node_t value = psl_value(p);
   psl_check(value, tab);

   if (psl_kind(value) != P_HDL_EXPR)
      error_at(psl_loc(p), "property is not in the simple subset as the "
               "operand of this next_e operator is non-Boolean");
}

static void psl_check_until(psl_node_t p, nametab_t *tab)
{
   assert(psl_operands(p) == 2);

   psl_node_t left = psl_operand(p, 0);
   psl_check(left, tab);

   psl_node_t right = psl_operand(p, 1);
   psl_check(right, tab);

   const psl_flags_t flags = psl_flags(p);
   if (flags & PSL_F_LTL) {
      const char *name = (flags & PSL_F_STRONG) ? "U" : "W";
      if (psl_kind(right) != P_HDL_EXPR)
         error_at(psl_loc(p), "property is not in the simple subset as the "
                  "right hand side of this %s operator is non-Boolean", name);
   }
   else if ((flags & PSL_F_INCLUSIVE) && psl_kind(right) != P_HDL_EXPR)
      error_at(psl_loc(p), "property is not in the simple subset as the right "
               "hand side of this overlapping until operator is non-Boolean");
   else if (psl_kind(left) != P_HDL_EXPR)
      error_at(psl_loc(p), "property is not in the simple subset as the left "
               "hand side of this until operator is non-Boolean");
}

static void psl_check_before(psl_node_t p, nametab_t *tab)
{
   assert(psl_operands(p) == 2);

   psl_node_t left = psl_operand(p, 0);
   psl_check(left, tab);

   psl_node_t right = psl_operand(p, 1);
   psl_check(right, tab);

   if (psl_kind(left) != P_HDL_EXPR)
      error_at(psl_loc(p), "property is not in the simple subset as the left "
               "hand side of this before operator is non-Boolean");
   else if (psl_kind(right) != P_HDL_EXPR)
      error_at(psl_loc(p), "property is not in the simple subset as the right "
               "hand side of this before operator is non-Boolean");
}

static void psl_check_abort(psl_node_t p, nametab_t *tab)
{
   assert(psl_operands(p) == 2);

   psl_node_t left = psl_operand(p, 0);
   psl_check(left, tab);

   psl_node_t right = psl_operand(p, 1);
   psl_check(right, tab);
}

static void psl_check_suffix_impl(psl_node_t p, nametab_t *tab)
{
   assert(psl_operands(p) == 2);

   psl_node_t left = psl_operand(p, 0);
   psl_check(left, tab);

   if (!psl_is_sequence(left))
      error_at(psl_loc(left), "left hand side of suffix implication operator "
               "must be a Sequence");

   psl_node_t right = psl_operand(p, 1);
   psl_check(right, tab);
}

static void psl_check_param_decl(psl_node_t p, nametab_t *tab)
{
   psl_node_t vs = psl_value(p);
   if (psl_subkind(vs) == PSL_VALUE_SET_BOOLEAN)
      return;

   const int nops = psl_operands(vs);
   for (int i = 0; i < nops; i++) {
      psl_node_t item = psl_operand(vs, i);
      if (psl_kind(item) == P_HDL_EXPR)
         psl_set_type(item, PSL_TYPE_NUMERIC);

      psl_check(item, tab);
   }
}

static void psl_check_proc_block(psl_node_t p, nametab_t *tab)
{
   psl_check(psl_value(p), tab);
}

static void psl_check_clocked(psl_node_t p, nametab_t *tab)
{
   psl_check(psl_value(p), tab);

   if (psl_has_tree(p))
      psl_check_boolean(p, tab);
}

static void psl_check_value_item(psl_node_t p, nametab_t *tab)
{
   tree_t value = solve_types(tab, psl_tree(p), NULL);
   psl_set_tree(p, value);

   if (type_is_none(tree_type(value)) || !sem_check(value, tab))
      return;

   psl_check_static(value);
}

static void psl_check_value_set(psl_node_t vs, nametab_t *tab)
{
   if (psl_subkind(vs) == PSL_VALUE_SET_BOOLEAN)
      return;

   const int nops = psl_operands(vs);
   for (int i = 0; i < nops; i++) {
      psl_node_t item = psl_operand(vs, i);
      if (psl_kind(item) == P_RANGE) {
         psl_check_value_item(psl_left(item), tab);
         psl_check_value_item(psl_right(item), tab);
      }
      else
         psl_check_value_item(item, tab);
   }
}

static void psl_check_builtin_fcall(psl_node_t p, nametab_t *tab)
{
   const int nparams = psl_operands(p);
   for (int i = 0; i < nparams; i++)
      psl_check(psl_operand(p, i), tab);

   switch (psl_subkind(p)) {
   case PSL_BUILTIN_PREV:
      if (nparams > 1 && psl_kind(psl_operand(p, 1)) == P_HDL_EXPR) {
         int64_t cycles;
         tree_t count = psl_tree(psl_operand(p, 1));
         if (folded_int(count, &cycles) && cycles < 1)
            error_at(tree_loc(count), "number of cycles for prev must be "
                     "positive");
      }
      break;

   case PSL_BUILTIN_NEXT:
      error_at(psl_loc(p), "built-in function next cannot be used directly "
               "in a property");
      break;

   case PSL_BUILTIN_NONDET:
      psl_check_value_set(psl_operand(p, 0), tab);
      break;

   case PSL_BUILTIN_NONDET_VECTOR:
      psl_check_value_set(psl_operand(p, 1), tab);
      break;

   default:
      break;
   }
}

static void psl_check_union(psl_node_t p, nametab_t *tab)
{
   psl_node_t lhs = psl_operand(p, 0);
   psl_node_t rhs = psl_operand(p, 1);

   psl_check(lhs, tab);
   psl_check(rhs, tab);

   type_t ltype = get_type_or_null(psl_tree(lhs));
   type_t rtype = get_type_or_null(psl_tree(rhs));

   if (ltype == NULL || rtype == NULL)
      return;
   else if (type_is_none(ltype) || type_is_none(rtype))
      return;

   if (!type_eq(ltype, rtype)) {
      diag_t *d = diag_new(DIAG_ERROR, psl_loc(p));
      diag_printf(d, "PSL union operands must be the same type");
      diag_hint(d, psl_loc(p), "have %pT and %pT", ltype, rtype);
      diag_emit(d);
   }
}

void psl_check(psl_node_t p, nametab_t *tab)
{
   switch (psl_kind(p)) {
   case P_ALWAYS:
      psl_check_always(p, tab);
      break;
   case P_ASSERT:
      psl_check_assert(p, tab);
      break;
   case P_ASSUME:
      psl_check_assume(p, tab);
      break;
   case P_RESTRICT:
      psl_check_restrict(p, tab);
      break;
   case P_FAIRNESS:
      psl_check_fairness(p);
      break;
   case P_COVER:
      psl_check_cover(p, tab);
      break;
   case P_CLOCK_DECL:
      psl_check_clock_decl(p);
      break;
   case P_PROPERTY_DECL:
      psl_check_property_decl(p, tab);
      break;
   case P_SEQUENCE_DECL:
      psl_check_sequence_decl(p, tab);
      break;
   case P_ENDPOINT_DECL:
      psl_check_endpoint_decl(p, tab);
      break;
   case P_HDL_EXPR:
      psl_check_hdl_expr(p, tab);
      break;
   case P_PROPERTY_INST:
      psl_check_property_inst(p);
      break;
   case P_SEQUENCE_INST:
      psl_check_sequence_inst(p);
      break;
   case P_NEVER:
      psl_check_never(p, tab);
      break;
   case P_EVENTUALLY:
      psl_check_eventually(p, tab);
      break;
   case P_SERE:
      psl_check_sere(p, tab);
      break;
   case P_REPEAT:
      psl_check_repeat(p, tab);
      break;
   case P_LOGICAL:
      psl_check_logical(p, tab);
      break;
   case P_NEXT:
   case P_NEXT_A:
      psl_check_next(p, tab);
      break;
   case P_NEXT_EVENT:
   case P_NEXT_EVENT_A:
      psl_check_next_event(p, tab);
      break;
   case P_NEXT_EVENT_E:
      psl_check_next_event_e(p, tab);
      break;
   case P_NEXT_E:
      psl_check_next_e(p, tab);
      break;
   case P_UNTIL:
      psl_check_until(p, tab);
      break;
   case P_BEFORE:
      psl_check_before(p, tab);
      break;
   case P_ABORT:
      psl_check_abort(p, tab);
      break;
   case P_SUFFIX_IMPL:
      psl_check_suffix_impl(p, tab);
      break;
   case P_PARAM_DECL:
      psl_check_param_decl(p, tab);
      break;
   case P_RANGE:
      psl_check_range(p, tab);
      break;
   case P_PROC_BLOCK:
      psl_check_proc_block(p, tab);
      break;
   case P_PARAM_SERE:
   case P_PARAM_PROP:
      // Already checked by the parser while the parameters are in scope
      break;
   case P_CLOCKED:
      psl_check_clocked(p, tab);
      break;
   case P_BUILTIN_FCALL:
      psl_check_builtin_fcall(p, tab);
      break;
   case P_UNION:
      psl_check_union(p, tab);
      break;
   case P_VALUE_SET:
      break;
   default:
      fatal_trace("cannot check PSL kind %s", psl_kind_str(psl_kind(p)));
   }
}
