//
//  Copyright (C) 2023-2024 Nick Gasson
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
#include "hash.h"
#include "ident.h"
#include "mask.h"
#include "option.h"
#include "phase.h"
#include "psl/psl-fsm.h"
#include "psl/psl-node.h"
#include "psl/psl-phase.h"
#include "tree.h"
#include "type.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#define CANNOT_HANDLE(p) do {                                   \
      fatal_at(psl_loc(p), "cannot handle PSL kind %s in %s",   \
               psl_kind_str(psl_kind(p)),  __FUNCTION__);       \
   } while (0)

static fsm_state_t *build_node(psl_fsm_t *fsm, fsm_state_t *state,
                               psl_node_t p);
static bool is_empty_repeat(psl_fsm_t *fsm, psl_node_t p);
static fsm_state_t *build_repeat_ex(psl_fsm_t *fsm, fsm_state_t *state,
                                    psl_node_t p, bool allow_empty);
static fsm_state_t *extend_fsm(psl_fsm_t *fsm, fsm_state_t *last,
                               psl_node_t where);
static fsm_state_t *build_property(psl_fsm_t *fsm, fsm_state_t *state,
                                   psl_node_t p);

typedef fsm_state_t *(*build_cb_t)(psl_fsm_t *fsm, fsm_state_t *initial,
                                   void *context);
static void psl_simplify(psl_fsm_t *fsm);
static void psl_simplify_from(psl_fsm_t *fsm, fsm_state_t *state,
                              bit_mask_t *visited);

static fsm_state_t *add_state(psl_fsm_t *fsm, psl_node_t where)
{
   fsm_state_t *s = pool_calloc(fsm->pool, sizeof(fsm_state_t));
   s->id    = fsm->next_id++;
   s->where = where;
   s->clock = fsm->clock;

   *(fsm->tail) = s;
   fsm->tail = &(s->next);

   return s;
}

static bool same_guard(psl_guard_t g1, psl_guard_t g2)
{
   if (g1 == g2)
      return true;

   const guard_kind_t g1_kind = psl_guard_kind(g1);
   const guard_kind_t g2_kind = psl_guard_kind(g2);

   if (g1_kind != g2_kind)
      return false;
   else if (g1_kind != GUARD_BINOP)
      return false;

   const guard_binop_t *bop1 = psl_guard_binop(g1);
   const guard_binop_t *bop2 = psl_guard_binop(g2);

   if (bop1->kind != bop2->kind)
      return false;

   return same_guard(bop1->left, bop2->left)
      && same_guard(bop1->right, bop2->right);
}

static void add_edge(psl_fsm_t *fsm, fsm_state_t *from, fsm_state_t *to,
                     edge_kind_t kind, psl_guard_t guard)
{
   fsm_edge_t **p = &(from->edges);
   for (; *p; p = &((*p)->next)) {
      if ((*p)->kind != kind || (*p)->dest != to)
         continue;
      else if (same_guard((*p)->guard, guard))
         return;   // Duplicate edge
   }

   fsm_edge_t *e = pool_malloc(fsm->pool, sizeof(fsm_edge_t));
   e->next  = *p;
   e->kind  = kind;
   e->dest  = to;
   e->guard = guard;

   *p = e;
}

static int64_t get_number(psl_fsm_t *fsm, tree_t t)
{
   int64_t result = 0;
   if (tree_kind(t) == T_REF) {
      for (const psl_binding_t *b = fsm->binding; b; b = b->outer) {
         if (tree_has_ref(t) && tree_ref(t) == b->decl)
            return b->value;
      }
   }

   if (!folded_int(t, &result))
      error_at(tree_loc(t), "static value of PSL Number is not known");
   else if (result < 0)
      warn_at(tree_loc(t), "PSL Number %"PRIi64" is negative", result);

   return result;
}

static void get_range(psl_fsm_t *fsm, psl_node_t range, int64_t *lo,
                      int64_t *hi)
{
   *lo = get_number(fsm, psl_tree(psl_left(range)));
   *hi = get_number(fsm, psl_tree(psl_right(range)));

   if (*lo > *hi)
      fatal_at(psl_loc(range), "left bound of PSL range (%"PRIi64") must be "
               "lower than right bound (%"PRIi64")", *lo, *hi);
}

static psl_guard_t make_binop_guard(psl_fsm_t *fsm, binop_kind_t kind,
                                    psl_guard_t left, psl_guard_t right)
{
   if (left == NULL)
      return right;
   else if (right == NULL || left == right)
      return left;

   guard_binop_t *bop = pool_malloc(fsm->pool, sizeof(guard_binop_t));
   bop->kind = kind;
   bop->left = left;
   bop->right = right;

   return tag_pointer(bop, GUARD_BINOP);
}

static inline psl_guard_t or_guard(psl_fsm_t *fsm, psl_guard_t left,
                                   psl_guard_t right)
{
   return make_binop_guard(fsm, BINOP_OR, left, right);
}

static inline psl_guard_t and_guard(psl_fsm_t *fsm, psl_guard_t left,
                                    psl_guard_t right)
{
   return make_binop_guard(fsm, BINOP_AND, left, right);
}

static psl_guard_t not_guard(psl_fsm_t *fsm, psl_guard_t g)
{
   switch (psl_guard_kind(g)) {
   case GUARD_BOUND:
      return tag_pointer(untag_pointer(g, guard_atom_t), GUARD_NOT_BOUND);
   case GUARD_NOT_BOUND:
      return tag_pointer(untag_pointer(g, guard_atom_t), GUARD_BOUND);
   case GUARD_NOT:
      return untag_pointer(g, struct _psl_node);
   case GUARD_FALSE:
      return NULL;
   case GUARD_ACTION:
      return tag_pointer(NULL, GUARD_FALSE);   // An action is always true
   case GUARD_BINOP:
      {
         const guard_binop_t *bop = psl_guard_binop(g);
         psl_guard_t left = not_guard(fsm, bop->left);
         psl_guard_t right = not_guard(fsm, bop->right);

         if (bop->kind == BINOP_OR)
            return and_guard(fsm, left, right);
         else if (left == NULL || right == NULL)
            return NULL;   // A null guard is always true
         else
            return or_guard(fsm, left, right);
      }
   default:
      {
         psl_node_t p = psl_guard_expr(g);
         return tag_pointer(p, p == NULL ? GUARD_FALSE : GUARD_NOT);
      }
   }
}

static psl_guard_t leaf_guard(psl_fsm_t *fsm, psl_node_t p)
{
   // Inside a replicated SERE the value of the expression depends on the
   // parameters
   if (fsm->binding == NULL)
      return p;

   for (guard_atom_t *a = fsm->atoms; a; a = a->next) {
      if (a->expr == p && a->binding == fsm->binding)
         return tag_pointer(a, GUARD_BOUND);
   }

   guard_atom_t *a = pool_malloc(fsm->pool, sizeof(guard_atom_t));
   a->next    = fsm->atoms;
   a->expr    = p;
   a->binding = fsm->binding;

   fsm->atoms = a;

   return tag_pointer(a, GUARD_BOUND);
}

static psl_guard_t action_guard(psl_fsm_t *fsm, psl_node_t block)
{
   guard_action_t *a = pool_malloc(fsm->pool, sizeof(guard_action_t));
   a->block   = block;
   a->binding = fsm->binding;

   return tag_pointer(a, GUARD_ACTION);
}

static psl_guard_t build_else_guard(psl_fsm_t *fsm, fsm_state_t *state)
{
   psl_guard_t g = NULL;

   for (fsm_edge_t *e = state->edges; e; e = e->next)
      g = and_guard(fsm, g, not_guard(fsm, e->guard));

   return g;
}

static void connect_abort(psl_fsm_t *fsm, fsm_state_t *from, fsm_state_t *to,
                          psl_guard_t guard, bit_mask_t *visited)
{
   if (from->edges == NULL)
      return;   // Final state
   else if (mask_test_and_set(visited, from->id))
      return;   // Cycle

   for (fsm_edge_t *e = from->edges; e; e = e->next)
      connect_abort(fsm, e->dest, to, guard, visited);

   add_edge(fsm, from, to, EDGE_EPSILON, guard);
}

static void connect_default(psl_fsm_t *fsm, fsm_state_t *from, fsm_state_t *to,
                            bit_mask_t *visited)
{
   if (from->edges == NULL)
      return;   // Final state
   else if (mask_test_and_set(visited, from->id))
      return;   // Cycle

   bool have_def = false;
   for (fsm_edge_t *e = from->edges; e; e = e->next) {
      connect_default(fsm, e->dest, to, visited);
      have_def |= e->guard == NULL;
   }

   if (!have_def)
      add_edge(fsm, from, to, EDGE_NEXT, build_else_guard(fsm, from));
}

static void intersect_prepare(psl_fsm_t *fsm, fsm_state_t *l_initial,
                              fsm_state_t *l_final, fsm_state_t *r_initial,
                              fsm_state_t *r_final)
{
   // Remove the epsilon transitions of both operands before building the
   // product: the rest of the automaton may still be incomplete
   l_final->accept = true;
   r_final->accept = true;

   LOCAL_BIT_MASK visited;
   mask_init(&visited, fsm->next_id);
   psl_simplify_from(fsm, l_initial, &visited);
   psl_simplify_from(fsm, r_initial, &visited);
}

static void intersect_run(psl_fsm_t *fsm, fsm_state_t *initial,
                          fsm_state_t *l_initial, fsm_state_t *r_initial,
                          fsm_state_t *final)
{
   // See chapter 3.5 of "Martin, J.C. (2003) Introduction to languages
   // and the theory of computation" for basic idea.

   ihash_t *map = ihash_new((fsm->next_id - initial->id) * 2 + 16);

   typedef struct {
      fsm_state_t *left;
      fsm_state_t *right;
      fsm_state_t *into;
   } work_item_t;

   SCOPED_A(work_item_t) worklist = AINIT;

   const work_item_t seed = { l_initial, r_initial, initial };
   APUSH(worklist, seed);

   while (worklist.count > 0) {
      const work_item_t wi = APOP(worklist);

      for (fsm_edge_t *e1 = wi.left->edges; e1; e1 = e1->next) {
         for (fsm_edge_t *e2 = wi.right->edges; e2; e2 = e2->next) {
            fsm_state_t *dest;
            if (e1->dest == e2->dest)
               dest = e1->dest;
            else {
               const uint64_t key = (uint64_t)e1->dest->id << 32 | e2->dest->id;

               if ((dest = ihash_get(map, key)) == NULL) {
                  dest = add_state(fsm, wi.left->where);
                  DEBUG_ONLY(dest->mergekey = key);
                  ihash_put(map, key, dest);

                  const work_item_t new = { e1->dest, e2->dest, dest };
                  APUSH(worklist, new);
               }
            }

            assert(e1->kind == e2->kind);

            psl_guard_t guard = and_guard(fsm, e1->guard, e2->guard);
            add_edge(fsm, wi.into, dest, e1->kind, guard);
         }
      }

      if (wi.left->accept && wi.right->accept) {
         psl_guard_t guard = and_guard(fsm, wi.left->guard, wi.right->guard);
         add_edge(fsm, wi.into, final, EDGE_EPSILON, guard);
      }
   }

   ihash_free(map);
}

static fsm_state_t *intersect_fsm(psl_fsm_t *fsm, fsm_state_t *initial,
                                  fsm_state_t *l_initial, fsm_state_t *l_final,
                                  fsm_state_t *r_initial, fsm_state_t *r_final)
{
   intersect_prepare(fsm, l_initial, l_final, r_initial, r_final);

   fsm_state_t *final = add_state(fsm, l_final->where);
   intersect_run(fsm, initial, l_initial, r_initial, final);
   return final;
}

typedef struct _dfa_node dfa_node_t;

typedef struct _dfa_node {
   fsm_state_t *state;
   unsigned     count;
   unsigned    *ids;
} dfa_node_t;

typedef struct {
   fsm_state_t *from;
   fsm_state_t *dest;
   edge_kind_t  kind;
   psl_guard_t  guard;
} dfa_edge_t;

typedef A(psl_guard_t) atom_list_t;

static psl_guard_t atom_of(psl_guard_t g, bool *negated)
{
   // The guard of the expression without any negation

   switch (psl_guard_kind(g)) {
   case GUARD_EXPR:
      *negated = false;
      return g;
   case GUARD_NOT:
      *negated = true;
      return psl_guard_expr(g);
   case GUARD_BOUND:
      *negated = false;
      return g;
   case GUARD_NOT_BOUND:
      *negated = true;
      return tag_pointer(untag_pointer(g, guard_atom_t), GUARD_BOUND);
   default:
      should_not_reach_here();
   }
}

static void collect_atoms(psl_guard_t g, atom_list_t *list)
{
   if (g == NULL)
      return;

   switch (psl_guard_kind(g)) {
   case GUARD_EXPR:
   case GUARD_NOT:
   case GUARD_BOUND:
   case GUARD_NOT_BOUND:
      {
         bool negated;
         psl_guard_t atom = atom_of(g, &negated);
         for (int i = 0; i < list->count; i++) {
            if (list->items[i] == atom)
               return;
         }
         APUSH(*list, atom);
      }
      break;
   case GUARD_BINOP:
      {
         const guard_binop_t *bop = psl_guard_binop(g);
         collect_atoms(bop->left, list);
         collect_atoms(bop->right, list);
      }
      break;
   case GUARD_FALSE:
   case GUARD_ACTION:
      break;
   }
}

static bool eval_guard(psl_guard_t g, const atom_list_t *atoms, unsigned bits)
{
   if (g == NULL)
      return true;

   switch (psl_guard_kind(g)) {
   case GUARD_EXPR:
   case GUARD_NOT:
   case GUARD_BOUND:
   case GUARD_NOT_BOUND:
      {
         bool negated;
         psl_guard_t atom = atom_of(g, &negated);

         bool value = false;
         for (int i = 0; i < atoms->count; i++) {
            if (atoms->items[i] == atom) {
               value = !!(bits & (1 << i));
               break;
            }
         }

         return negated ? !value : value;
      }
   case GUARD_BINOP:
      {
         const guard_binop_t *bop = psl_guard_binop(g);
         const bool left = eval_guard(bop->left, atoms, bits);
         const bool right = eval_guard(bop->right, atoms, bits);
         return bop->kind == BINOP_AND ? (left && right) : (left || right);
      }
   case GUARD_FALSE:
      return false;
   case GUARD_ACTION:
      return true;
   }

   return false;
}

static fsm_state_t *determinize(psl_fsm_t *fsm, fsm_state_t *root,
                                fsm_state_t *nfa_final)
{
   // Convert the non-deterministic automaton starting at the root into
   // a deterministic one by the subset construction so that the property
   // holds if any of the matches of a sequence succeeds.  The root state
   // is replaced by the start state of the new automaton.

   // Only remove the epsilon transitions reachable from the root as
   // those into the root must lead to the new automaton
   nfa_final->accept = true;

   LOCAL_BIT_MASK visited;
   mask_init(&visited, fsm->next_id);
   psl_simplify_from(fsm, root, &visited);

   const unsigned nnfa = fsm->next_id;
   fsm_state_t **by_id LOCAL = xcalloc_array(nnfa, sizeof(fsm_state_t *));
   for (fsm_state_t *s = fsm->states; s; s = s->next)
      by_id[s->id] = s;

   fsm_state_t *final = add_state(fsm, root->where);

   SCOPED_A(dfa_node_t *) nodes = AINIT;
   SCOPED_A(dfa_edge_t) pending = AINIT;

   shash_t *map = shash_new(64);

   dfa_node_t *start = xcalloc(sizeof(dfa_node_t));
   start->state = root;
   start->count = 1;
   start->ids   = xmalloc_array(1, sizeof(unsigned));
   start->ids[0] = root->id;

   {
      char key[32];
      checked_sprintf(key, sizeof(key), "%u", root->id);
      shash_put(map, key, start);
   }

   APUSH(nodes, start);

   for (unsigned done = 0; done < nodes.count; done++) {
      dfa_node_t *node = nodes.items[done];

      atom_list_t atoms = AINIT;
      for (unsigned i = 0; i < node->count; i++) {
         fsm_state_t *s = by_id[node->ids[i]];
         collect_atoms(s->guard, &atoms);
         for (fsm_edge_t *e = s->edges; e; e = e->next)
            collect_atoms(e->guard, &atoms);
      }

      if (atoms.count > 12)
         fatal_at(psl_loc(root->where), "sorry, too many Boolean expressions "
                  "in this sequence");

      for (unsigned bits = 0; bits < (1u << atoms.count); bits++) {
         bit_mask_t next;
         mask_init(&next, nnfa);

         bool accepted = false;
         for (unsigned i = 0; i < node->count; i++) {
            fsm_state_t *s = by_id[node->ids[i]];

            if (s->accept && eval_guard(s->guard, &atoms, bits))
               accepted = true;

            for (fsm_edge_t *e = s->edges; e; e = e->next) {
               if (eval_guard(e->guard, &atoms, bits))
                  mask_set(&next, e->dest->id);
            }
         }

         psl_guard_t minterm = NULL;
         for (unsigned i = 0; i < atoms.count; i++) {
            psl_guard_t lit = atoms.items[i];
            if (!(bits & (1 << i)))
               lit = not_guard(fsm, lit);
            minterm = and_guard(fsm, minterm, lit);
         }

         if (accepted) {
            const dfa_edge_t edge = { node->state, final, EDGE_EPSILON,
                                      minterm };
            APUSH(pending, edge);
            mask_free(&next);
            continue;
         }

         const size_t count = mask_popcount(&next);
         if (count > 0) {
            LOCAL_TEXT_BUF tb = tb_new();
            unsigned *ids = xmalloc_array(count, sizeof(unsigned));

            size_t bit = -1, n = 0;
            while (mask_iter(&next, &bit)) {
               ids[n++] = bit;
               if (n > 1)
                  tb_append(tb, ',');
               tb_printf(tb, "%zu", bit);
            }

            dfa_node_t *dest = shash_get(map, tb_get(tb));
            if (dest == NULL) {
               dest = xcalloc(sizeof(dfa_node_t));
               dest->state = add_state(fsm, root->where);
               dest->count = count;
               dest->ids   = ids;
               shash_put(map, tb_get(tb), dest);
               APUSH(nodes, dest);
            }
            else
               free(ids);

            const dfa_edge_t edge = { node->state, dest->state, EDGE_NEXT,
                                      minterm };
            APUSH(pending, edge);
         }

         mask_free(&next);
      }

      ACLEAR(atoms);
   }

   shash_free(map);

   root->edges = NULL;
   root->accept = false;
   root->guard = NULL;

   for (unsigned i = 0; i < pending.count; i++) {
      const dfa_edge_t *e = &(pending.items[i]);
      add_edge(fsm, e->from, e->dest, e->kind, e->guard);
   }

   for (unsigned i = 0; i < nodes.count; i++) {
      free(nodes.items[i]->ids);
      free(nodes.items[i]);
   }

   return final;
}

static fsm_state_t *determinize_sequence(psl_fsm_t *fsm, fsm_state_t *state,
                                         fsm_state_t *root,
                                         fsm_state_t *nfa_final, psl_node_t p)
{
   // The procedural blocks of a sequence with local variables are
   // executed along each thread so keep one thread per match
   if (psl_visit_only(p, NULL, NULL, P_PROC_BLOCK) > 0)
      return nfa_final;

   const unsigned first = fsm->next_id;
   fsm_state_t *final = determinize(fsm, root, nfa_final);

   const bool strong =
      psl_kind(p) == P_SERE && (psl_flags(p) & PSL_F_STRONG);

   if (strong) {
      // A strong sequence must complete before the end of the
      // simulation (IEEE 1850-2010 section 6.2.1.3.1)
      for (fsm_state_t *s = fsm->states; s; s = s->next) {
         if (s->id >= first || s == root)
            s->strong = true;
      }

      if (!state->initial)
         state->strong = true;
   }

   return final;
}

typedef struct {
   psl_node_t where;
   psl_node_t node;
} property_ctx_t;

static fsm_state_t *build_property_cb(psl_fsm_t *fsm, fsm_state_t *initial,
                                      void *context)
{
   const property_ctx_t *ctx = context;
   return build_property(fsm, initial, ctx->node);
}

static fsm_state_t *build_and(psl_fsm_t *fsm, fsm_state_t *state,
                              build_cb_t left_fn, void *left_ctx,
                              build_cb_t right_fn, void *right_ctx)
{
   // Both properties are evaluated in parallel and the conjunction is
   // satisfied when both are.  A property that has already been satisfied
   // is extended to remain satisfied until the other one is.
   psl_node_t where = state->where;

   fsm_state_t *l_initial = add_state(fsm, where);
   fsm_state_t *l_final = (*left_fn)(fsm, l_initial, left_ctx);
   l_final = extend_fsm(fsm, l_final, where);

   fsm_state_t *r_initial = add_state(fsm, where);
   fsm_state_t *r_final = (*right_fn)(fsm, r_initial, right_ctx);
   r_final = extend_fsm(fsm, r_final, where);

   return intersect_fsm(fsm, state, l_initial, l_final, r_initial, r_final);
}

static fsm_state_t *build_logical(psl_fsm_t *fsm, fsm_state_t *state,
                                  psl_node_t p)
{
   if (psl_subkind(p) == PSL_LOGIC_NOT)
      fatal_at(psl_loc(p), "sorry, negation of a property is not supported");

   psl_node_t lhs = psl_operand(p, 0);
   psl_node_t rhs = psl_operand(p, 1);

   switch (psl_subkind(p)) {
   case PSL_LOGIC_IFF:
      {
         // Only legal with Boolean HDL expression
         fsm_state_t *left = add_state(fsm, p);
         fsm_state_t *right = add_state(fsm, p);
         fsm_state_t *accept = add_state(fsm, p);

         psl_guard_t l = leaf_guard(fsm, lhs);
         psl_guard_t r = leaf_guard(fsm, rhs);

         add_edge(fsm, state, left, EDGE_EPSILON, l);
         add_edge(fsm, state, right, EDGE_EPSILON, r);
         add_edge(fsm, left, accept, EDGE_EPSILON, r);
         add_edge(fsm, right, accept, EDGE_EPSILON, l);

         psl_guard_t def = build_else_guard(fsm, state);
         add_edge(fsm, state, accept, EDGE_EPSILON, def);

         return accept;
      }

   case PSL_LOGIC_IF:
      {
         fsm_state_t *left = add_state(fsm, p);
         fsm_state_t *right = build_property(fsm, left, rhs);
         add_edge(fsm, state, left, EDGE_EPSILON, leaf_guard(fsm, lhs));

         psl_guard_t def = build_else_guard(fsm, state);
         add_edge(fsm, state, right, EDGE_EPSILON, def);

         return right;
      }

   case PSL_LOGIC_OR:
      {
         // At least one operand must be Boolean; the other is only
         // checked if the Boolean does not hold
         psl_node_t b = lhs, other = rhs;
         if (psl_kind(lhs) != P_HDL_EXPR) {
            b = rhs;
            other = lhs;
         }

         fsm_state_t *accept = add_state(fsm, p);
         fsm_state_t *start = add_state(fsm, p);

         psl_guard_t guard = leaf_guard(fsm, b);
         add_edge(fsm, state, accept, EDGE_EPSILON, guard);
         add_edge(fsm, state, start, EDGE_EPSILON, not_guard(fsm, guard));

         fsm_state_t *final = build_property(fsm, start, other);

         add_edge(fsm, final, accept, EDGE_EPSILON, NULL);
         return accept;
      }

   case PSL_LOGIC_AND:
      {
         property_ctx_t left = { p, lhs };
         property_ctx_t right = { p, rhs };

         return build_and(fsm, state, build_property_cb, &left,
                          build_property_cb, &right);
      }

   default:
      CANNOT_HANDLE(p);
   }
}

static fsm_state_t *build_until(psl_fsm_t *fsm, fsm_state_t *state,
                                psl_node_t p)
{
   psl_node_t lhs = psl_operand(p, 0);
   psl_node_t rhs = psl_operand(p, 1);

   if (psl_kind(lhs) != P_HDL_EXPR || psl_kind(rhs) != P_HDL_EXPR)
      fatal_at(psl_loc(p), "sorry, the operands of until must be Boolean");

   // The strong forms fail if the right hand operand does not hold
   // before the end of the simulation
   state->strong = !!(psl_flags(p) & PSL_F_STRONG);

   fsm_state_t *final = add_state(fsm, p);

   psl_guard_t l = leaf_guard(fsm, lhs);
   psl_guard_t r = leaf_guard(fsm, rhs);

   if (psl_flags(p) & PSL_F_INCLUSIVE) {
      // The left hand operand must also hold in the cycle where the
      // right hand operand holds
      add_edge(fsm, state, final, EDGE_EPSILON, and_guard(fsm, l, r));
      add_edge(fsm, state, state, EDGE_NEXT,
               and_guard(fsm, l, not_guard(fsm, r)));
   }
   else {
      add_edge(fsm, state, final, EDGE_EPSILON, r);
      add_edge(fsm, state, state, EDGE_NEXT,
               and_guard(fsm, not_guard(fsm, r), l));
   }

   return final;
}

static fsm_state_t *build_abort(psl_fsm_t *fsm, fsm_state_t *state,
                                psl_node_t p)
{
   psl_node_t lhs = psl_operand(p, 0);
   psl_node_t rhs = psl_operand(p, 1);

   fsm_state_t *final = build_node(fsm, state, lhs);

   fsm_state_t *sink = add_state(fsm, p);
   sink->accept = true;

   LOCAL_BIT_MASK visited;
   mask_init(&visited, fsm->next_id);

   connect_abort(fsm, state, sink, leaf_guard(fsm, rhs), &visited);

   return final;
}

static fsm_state_t *extend_fsm(psl_fsm_t *fsm, fsm_state_t *last,
                               psl_node_t where)
{
   // Match {r; [*]} where r ends in the last state

   fsm_state_t *final = add_state(fsm, where);
   fsm_state_t *tail = add_state(fsm, where);

   add_edge(fsm, last, final, EDGE_EPSILON, NULL);
   add_edge(fsm, last, tail, EDGE_NEXT, NULL);
   add_edge(fsm, tail, tail, EDGE_NEXT, NULL);
   add_edge(fsm, tail, final, EDGE_EPSILON, NULL);

   return final;
}

typedef struct {
   psl_node_t where;
   psl_node_t node;
} build_node_ctx_t;

static fsm_state_t *build_node_cb(psl_fsm_t *fsm, fsm_state_t *initial,
                                  void *context)
{
   const build_node_ctx_t *ctx = context;
   return build_node(fsm, initial, ctx->node);
}

static fsm_state_t *build_neq_and(psl_fsm_t *fsm, fsm_state_t *state,
                                  build_cb_t left_fn, void *left_ctx,
                                  build_cb_t right_fn, void *right_ctx)
{
   // Either the left or the right operand is the longer one so extend
   // the shorter one to match
   psl_node_t where = state->where;

   fsm_state_t *l_initial[2], *l_final[2], *r_initial[2], *r_final[2];

   for (int i = 0; i < 2; i++) {
      l_initial[i] = add_state(fsm, where);
      l_final[i] = (*left_fn)(fsm, l_initial[i], left_ctx);
      if (i == 0)
         l_final[i] = extend_fsm(fsm, l_final[i], where);

      r_initial[i] = add_state(fsm, where);
      r_final[i] = (*right_fn)(fsm, r_initial[i], right_ctx);
      if (i == 1)
         r_final[i] = extend_fsm(fsm, r_final[i], where);

      intersect_prepare(fsm, l_initial[i], l_final[i], r_initial[i],
                        r_final[i]);
   }

   fsm_state_t *final = add_state(fsm, where);

   for (int i = 0; i < 2; i++) {
      fsm_state_t *initial = add_state(fsm, where);
      intersect_run(fsm, initial, l_initial[i], r_initial[i], final);
      add_edge(fsm, state, initial, EDGE_EPSILON, NULL);
   }

   return final;
}

static fsm_state_t *build_concat(psl_fsm_t *fsm, fsm_state_t *state,
                                 psl_node_t p, unsigned skip)
{
   // Bit n of skip set leaves out the nth element that can match the
   // empty sequence, otherwise it only matches one or more times

   const psl_sere_kind_t kind = psl_subkind(p);
   const edge_kind_t ekind =
      kind == PSL_SERE_FUSION ? EDGE_EPSILON : EDGE_NEXT;
   const int nops = psl_operands(p);

   fsm_state_t *start = state, *last = NULL;
   unsigned nth = 0;
   for (int i = 0; i < nops; i++) {
      psl_node_t op = psl_operand(p, i);

      const bool can_skip =
         kind == PSL_SERE_CONCAT && is_empty_repeat(fsm, op);
      if (can_skip && (skip & (1u << nth++)))
         continue;

      if (start == NULL) {
         start = add_state(fsm, p);
         add_edge(fsm, last, start, ekind, NULL);
      }

      fsm_state_t *new;
      if (can_skip) {
         if ((new = build_repeat_ex(fsm, start, op, false)) == NULL)
            return NULL;   // Only matches the empty sequence
      }
      else
         new = build_node(fsm, start, op);

      if (new != start) {
         last = new;
         start = NULL;
      }
   }

   return last ?: start;
}

static fsm_state_t *build_sere(psl_fsm_t *fsm, fsm_state_t *state, psl_node_t p)
{
   const psl_sere_kind_t kind = psl_subkind(p);
   const int nops = psl_operands(p);

   switch (kind) {
   case PSL_SERE_FUSION:
      return build_concat(fsm, state, p, 0);

   case PSL_SERE_CONCAT:
      {
         // An element r[*] that can match the empty sequence is either
         // left out or matches one or more times: build each
         // combination as an alternative
         unsigned nempty = 0;
         for (int i = 0; i < nops; i++)
            nempty += is_empty_repeat(fsm, psl_operand(p, i));

         if (nempty == 0)
            return build_concat(fsm, state, p, 0);
         else if (nempty > 4)
            fatal_at(psl_loc(p), "sorry, too many elements of this SERE "
                     "can match the empty sequence");

         fsm_state_t *final = add_state(fsm, p);

         for (unsigned mask = 0; mask < (1u << nempty); mask++) {
            fsm_state_t *initial = add_state(fsm, p);
            add_edge(fsm, state, initial, EDGE_EPSILON, NULL);

            fsm_state_t *last = build_concat(fsm, initial, p, ~mask);
            if (last != NULL)
               add_edge(fsm, last, final, EDGE_EPSILON, NULL);
         }

         return final;
      }

   case PSL_SERE_EQU_AND:
      {
         assert(nops == 2);

         fsm_state_t *l_initial = add_state(fsm, p);
         fsm_state_t *l_final = build_node(fsm, l_initial, psl_operand(p, 0));

         fsm_state_t *r_initial = add_state(fsm, p);
         fsm_state_t *r_final = build_node(fsm, r_initial, psl_operand(p, 1));

         return intersect_fsm(fsm, state, l_initial, l_final,
                              r_initial, r_final);
      }

   case PSL_SERE_OR:
      {
         fsm_state_t *final = add_state(fsm, p);

         for (int i = 0; i < nops; i++) {
            fsm_state_t *initial = add_state(fsm, p);
            add_edge(fsm, state, initial, EDGE_EPSILON, NULL);

            fsm_state_t *last = build_node(fsm, initial, psl_operand(p, i));
            add_edge(fsm, last, final, EDGE_EPSILON, NULL);
         }

         return final;
      }

   case PSL_SERE_NEQ_AND:
      {
         assert(nops == 2);

         build_node_ctx_t left = { p, psl_operand(p, 0) };
         build_node_ctx_t right = { p, psl_operand(p, 1) };

         return build_neq_and(fsm, state, build_node_cb, &left,
                              build_node_cb, &right);
      }

   case PSL_SERE_WITHIN:
      {
         assert(nops == 2);

         // {r1 within r2} is {[*]; r1; [*]} && {r2}
         fsm_state_t *l_initial = add_state(fsm, p);
         fsm_state_t *skip = add_state(fsm, p);
         fsm_state_t *body = add_state(fsm, p);

         add_edge(fsm, l_initial, body, EDGE_EPSILON, NULL);
         add_edge(fsm, l_initial, skip, EDGE_NEXT, NULL);
         add_edge(fsm, skip, skip, EDGE_NEXT, NULL);
         add_edge(fsm, skip, body, EDGE_EPSILON, NULL);

         fsm_state_t *l_last = build_node(fsm, body, psl_operand(p, 0));
         fsm_state_t *l_final = extend_fsm(fsm, l_last, p);

         fsm_state_t *r_initial = add_state(fsm, p);
         fsm_state_t *r_final = build_node(fsm, r_initial, psl_operand(p, 1));

         return intersect_fsm(fsm, state, l_initial, l_final,
                              r_initial, r_final);
      }

   default:
      CANNOT_HANDLE(p);
   }
}

typedef struct {
   const psl_binding_t **bindings;
   int                   count;
   psl_node_t            body;
} param_ctx_t;

static fsm_state_t *build_instance(psl_fsm_t *fsm, fsm_state_t *initial,
                                   const psl_binding_t *binding,
                                   psl_node_t body)
{
   const psl_binding_t *saved = fsm->binding;
   fsm->binding = binding;

   fsm_state_t *last = build_node(fsm, initial, body);

   fsm->binding = saved;
   return last;
}

static fsm_state_t *build_param_range_cb(psl_fsm_t *fsm, fsm_state_t *initial,
                                         void *context)
{
   const param_ctx_t *ctx = context;
   assert(ctx->count > 0);

   if (ctx->count == 1)
      return build_instance(fsm, initial, ctx->bindings[0], ctx->body);

   param_ctx_t left = { ctx->bindings, ctx->count - 1, ctx->body };
   param_ctx_t right = { ctx->bindings + ctx->count - 1, 1, ctx->body };

   return build_neq_and(fsm, initial, build_param_range_cb, &left,
                        build_param_range_cb, &right);
}

static int get_param_values(psl_fsm_t *fsm, psl_node_t decl, int64_t **values)
{
   psl_node_t vs = psl_value(decl);

   if (psl_subkind(vs) == PSL_VALUE_SET_BOOLEAN) {
      *values = xmalloc_array(2, sizeof(int64_t));
      (*values)[0] = 0;
      (*values)[1] = 1;
      return 2;
   }

   SCOPED_A(int64_t) list = AINIT;

   const int nitems = psl_operands(vs);
   for (int i = 0; i < nitems; i++) {
      psl_node_t item = psl_operand(vs, i);
      if (psl_kind(item) == P_RANGE) {
         int64_t lo, hi;
         get_range(fsm, item, &lo, &hi);
         for (int64_t v = lo; v <= hi; v++)
            APUSH(list, v);
      }
      else
         APUSH(list, get_number(fsm, psl_tree(item)));

      if (list.count > 1024)
         fatal_at(psl_loc(decl), "sorry, too many values in this value set");
   }

   *values = xmalloc_array(MAX(list.count, 1), sizeof(int64_t));
   for (int i = 0; i < list.count; i++)
      (*values)[i] = list.items[i];

   return list.count;
}

static int get_param_elements(psl_node_t decl)
{
   // Number of elements of a parameter with an index range or zero

   type_t type = tree_type(psl_tree(decl));
   if (!type_is_array(type))
      return 0;

   int64_t length;
   if (!folded_length(range_of(type, 0), &length))
      fatal_at(psl_loc(decl), "index range of parameter is not static");

   return MAX(length, 0);
}

static size_t enumerate_instances(psl_fsm_t *fsm, psl_node_t p,
                                  const psl_binding_t ***result)
{
   // One instance for each combination of values of the parameters

   const int nparams = psl_operands(p);

   int64_t **values LOCAL = xmalloc_array(nparams, sizeof(int64_t *));
   int *counts LOCAL = xmalloc_array(nparams, sizeof(int));

   int *nelems LOCAL = xmalloc_array(nparams, sizeof(int));
   size_t *ncombs LOCAL = xmalloc_array(nparams, sizeof(size_t));

   size_t total = 1;
   for (int i = 0; i < nparams; i++) {
      psl_node_t decl = psl_operand(p, i);
      counts[i] = get_param_values(fsm, decl, &(values[i]));
      nelems[i] = get_param_elements(decl);
      ncombs[i] = counts[i];

      // A parameter with an index range takes every combination of
      // values for its elements
      for (int j = 1; j < nelems[i] && ncombs[i] <= 1024; j++)
         ncombs[i] *= counts[i];

      total *= ncombs[i];

      if (total > 1024 || ncombs[i] > 1024)
         fatal_at(psl_loc(p), "sorry, too many instances of replicated SERE");
   }

   if (total == 0)
      fatal_at(psl_loc(p), "sorry, empty value set in replicated SERE");

   const psl_binding_t **insts = xmalloc_array(total, sizeof(psl_binding_t *));

   for (size_t n = 0; n < total; n++) {
      const psl_binding_t *chain = fsm->binding;

      size_t rem = n;
      for (int i = 0; i < nparams; i++) {
         psl_binding_t *b = pool_calloc(fsm->pool, sizeof(psl_binding_t));
         b->outer = chain;
         b->decl  = psl_tree(psl_operand(p, i));

         if (nelems[i] > 0) {
            int64_t *set = pool_malloc_array(fsm->pool, counts[i],
                                             sizeof(int64_t));
            for (int j = 0; j < counts[i]; j++)
               set[j] = values[i][j];

            b->value   = rem % ncombs[i];
            b->set     = set;
            b->setsize = counts[i];
            b->nelems  = nelems[i];
         }
         else
            b->value = values[i][rem % counts[i]];

         rem /= ncombs[i];
         chain = b;
      }

      insts[n] = chain;
   }

   for (int i = 0; i < nparams; i++)
      free(values[i]);

   *result = insts;
   insts = NULL;
   return total;
}

static fsm_state_t *build_param_sere(psl_fsm_t *fsm, fsm_state_t *state,
                                     psl_node_t p)
{
   // for i in S : op { r(i) } is r(s1) op r(s2) op ... for each value of S

   const psl_binding_t **insts LOCAL = NULL;
   const size_t total = enumerate_instances(fsm, p, &insts);

   psl_node_t body = psl_value(p);

   switch ((psl_sere_kind_t)psl_subkind(p)) {
   case PSL_SERE_OR:
      {
         fsm_state_t *final = add_state(fsm, p);

         for (size_t n = 0; n < total; n++) {
            fsm_state_t *initial = add_state(fsm, p);
            add_edge(fsm, state, initial, EDGE_EPSILON, NULL);

            fsm_state_t *last = build_instance(fsm, initial, insts[n], body);
            add_edge(fsm, last, final, EDGE_EPSILON, NULL);
         }

         return final;
      }

   case PSL_SERE_EQU_AND:
      {
         fsm_state_t *cur_initial = add_state(fsm, p);
         fsm_state_t *cur_final =
            build_instance(fsm, cur_initial, insts[0], body);

         for (size_t n = 1; n < total; n++) {
            fsm_state_t *r_initial = add_state(fsm, p);
            fsm_state_t *r_final =
               build_instance(fsm, r_initial, insts[n], body);

            fsm_state_t *initial = add_state(fsm, p);
            cur_final = intersect_fsm(fsm, initial, cur_initial, cur_final,
                                      r_initial, r_final);
            cur_initial = initial;
         }

         add_edge(fsm, state, cur_initial, EDGE_EPSILON, NULL);
         return cur_final;
      }

   case PSL_SERE_NEQ_AND:
      {
         param_ctx_t ctx = { insts, total, body };
         return build_param_range_cb(fsm, state, &ctx);
      }

   default:
      CANNOT_HANDLE(p);
   }
}


static fsm_state_t *build_param_and_cb(psl_fsm_t *fsm, fsm_state_t *initial,
                                       void *context)
{
   const param_ctx_t *ctx = context;
   assert(ctx->count > 0);

   if (ctx->count == 1)
      return build_instance(fsm, initial, ctx->bindings[0], ctx->body);

   param_ctx_t left = { ctx->bindings, ctx->count - 1, ctx->body };
   param_ctx_t right = { ctx->bindings + ctx->count - 1, 1, ctx->body };

   return build_and(fsm, initial, build_param_and_cb, &left,
                    build_param_and_cb, &right);
}

static fsm_state_t *build_param_prop(psl_fsm_t *fsm, fsm_state_t *state,
                                     psl_node_t p)
{
   // for i in S : and (f(i)) is f(s1) and f(s2) and ... and similarly for
   // or where the operands must be Boolean

   const psl_binding_t **insts LOCAL = NULL;
   const size_t total = enumerate_instances(fsm, p, &insts);

   psl_node_t body = psl_value(p);

   switch ((psl_logic_t)psl_subkind(p)) {
   case PSL_LOGIC_AND:
      {
         param_ctx_t ctx = { insts, total, body };
         return build_param_and_cb(fsm, state, &ctx);
      }

   case PSL_LOGIC_OR:
      {
         if (psl_kind(body) != P_HDL_EXPR)
            fatal_at(psl_loc(p), "sorry, the operand of a replicated or "
                     "property must be Boolean");

         fsm_state_t *accept = add_state(fsm, p);

         for (size_t n = 0; n < total; n++) {
            const psl_binding_t *saved = fsm->binding;
            fsm->binding = insts[n];
            psl_guard_t guard = leaf_guard(fsm, body);
            fsm->binding = saved;

            add_edge(fsm, state, accept, EDGE_EPSILON, guard);
         }

         return accept;
      }

   default:
      CANNOT_HANDLE(p);
   }
}

static void get_repeat_bounds(psl_fsm_t *fsm, psl_node_t p, int *low,
                              int *high, bool *infinite, bool *noncon,
                              bool *goto_rep)
{
   psl_repeat_t kind = psl_subkind(p);

   *low = 0;
   *high = 0;
   *noncon = false;
   *goto_rep = false;
   *infinite = false;

   switch (kind) {
   case PSL_PLUS_REPEAT:
      *low = 1;
      *high = INT32_MAX;
      break;

   case PSL_GOTO_REPEAT:
      *goto_rep = true;
      // Fall-through
   case PSL_EQUAL_REPEAT:
      *noncon = true;
      // Fall-through
   case PSL_TIMES_REPEAT:
      if (psl_has_delay(p)) {
         psl_node_t count = psl_delay(p);
         switch (psl_kind(count)) {
         case P_RANGE:
            *low = get_number(fsm, psl_tree(psl_left(count)));
            *high = get_number(fsm, psl_tree(psl_right(count)));
            break;
         case P_HDL_EXPR:
            *low = *high = get_number(fsm, psl_tree(count));
            break;
         default:
            should_not_reach_here();
         }
      }
      else {
         *low = 0;
         *high = INT32_MAX;
      }
      break;

   default:
      CANNOT_HANDLE(p);
   }

   if (*high == INT32_MAX) {
      *infinite = true;
      *high = *low;
   }
}

static bool is_empty_repeat(psl_fsm_t *fsm, psl_node_t p)
{
   // True for r[*n to m] that can match the empty sequence

   if (psl_kind(p) != P_REPEAT || psl_subkind(p) != PSL_TIMES_REPEAT)
      return false;

   int low, high;
   bool infinite, noncon, goto_rep;
   get_repeat_bounds(fsm, p, &low, &high, &infinite, &noncon, &goto_rep);

   return low == 0;
}

static fsm_state_t *build_repeat_ex(psl_fsm_t *fsm, fsm_state_t *state,
                                    psl_node_t p, bool allow_empty)
{
   int low, high;
   bool infinite, noncon, goto_rep;

   get_repeat_bounds(fsm, p, &low, &high, &infinite, &noncon, &goto_rep);

   if (!allow_empty && low == 0) {
      // Only the non-empty matches: the caller handles the empty one
      if (high == 0 && !infinite)
         return NULL;

      low = 1;
      high = MAX(high, 1);
   }
   else if (infinite && low == 0)
      high = 1;   // One or more repetitions or the empty sequence

   if (infinite) {
      // Repetitions loop back to a new state so that the other
      // transitions of the incoming state are not taken again
      fsm_state_t *start = add_state(fsm, p);
      add_edge(fsm, state, start, EDGE_EPSILON, NULL);
      state = start;
   }

   fsm_state_t *skip = (high > low) ? add_state(fsm, p) : NULL;
   fsm_state_t *initial = state;
   fsm_state_t *last_but_one = NULL;

   psl_node_t seq = psl_value(p);

   for (int i = 0; i < high; i++) {
      bool is_last = (i == high - 1) ? true : false;

      last_but_one = state;
      state = build_node(fsm, state, seq);

      if (noncon) {
         fsm_state_t *wait = add_state(fsm, p);
         psl_guard_t guard = build_else_guard(fsm, last_but_one);
         add_edge(fsm, last_but_one, wait, EDGE_EPSILON, guard);
         add_edge(fsm, wait, last_but_one, EDGE_NEXT, NULL);
      }

      if (!is_last) {
         if (i >= low - 1)
            add_edge(fsm, state, skip, EDGE_EPSILON, NULL);

         fsm_state_t *curr = state;
         state = add_state(fsm, p);
         add_edge(fsm, curr, state, EDGE_NEXT, NULL);
      }
      else if (noncon && !goto_rep) {
         assert(psl_kind(seq) == P_HDL_EXPR);
         fsm_state_t *aux = add_state(fsm, p);
         fsm_state_t *dead = add_state(fsm, p);
         fsm_state_t *wait = add_state(fsm, p);
         add_edge(fsm, state, aux, EDGE_NEXT, NULL);
         add_edge(fsm, aux, dead, EDGE_EPSILON, leaf_guard(fsm, seq));
         add_edge(fsm, aux, wait, EDGE_EPSILON, build_else_guard(fsm, aux));
         add_edge(fsm, wait, aux, EDGE_NEXT, NULL);
         add_edge(fsm, wait, state, EDGE_EPSILON, NULL);
      }
   }

   if (skip)
      add_edge(fsm, skip, state, EDGE_EPSILON, NULL);

   if (initial != state) {
      if (low == 0)
         add_edge(fsm, initial, state, EDGE_EPSILON, NULL);
      if (infinite)
         add_edge(fsm, state, last_but_one, EDGE_NEXT, NULL);
   }

   return state;
}

static fsm_state_t *build_repeat(psl_fsm_t *fsm, fsm_state_t *state,
                                 psl_node_t p)
{
   return build_repeat_ex(fsm, state, p, true);
}

static fsm_state_t *build_next(psl_fsm_t *fsm, fsm_state_t *state, psl_node_t p)
{
   if (psl_has_delay(p)) {
      const int cycles = get_number(fsm, psl_tree(psl_delay(p)));
      for (int i = 0; i < cycles; i++) {
         fsm_state_t *new = add_state(fsm, p);
         add_edge(fsm, state, new, EDGE_NEXT, NULL);
         state = new;
      }
   }
   else {
      fsm_state_t *new = add_state(fsm, p);
      add_edge(fsm, state, new, EDGE_NEXT, NULL);
      state = new;
   }

   return build_property(fsm, state, psl_value(p));
}

static fsm_state_t *build_property(psl_fsm_t *fsm, fsm_state_t *state,
                                   psl_node_t p)
{
   // A sequence used as a property holds if any of its matches succeeds
   // starting from the state

   fsm_state_t *initial = add_state(fsm, p);
   add_edge(fsm, state, initial, EDGE_EPSILON, NULL);

   fsm_state_t *final = build_node(fsm, initial, p);

   switch (psl_kind(p)) {
   case P_SERE:
   case P_REPEAT:
   case P_PARAM_SERE:
      return determinize_sequence(fsm, state, initial, final, p);
   default:
      return final;
   }
}

typedef fsm_state_t *(*build_nth_t)(psl_fsm_t *, fsm_state_t *, psl_node_t,
                                    int64_t);

typedef struct {
   psl_node_t  node;
   int64_t     lo;
   int64_t     hi;
   build_nth_t build_nth;
} range_ctx_t;

static fsm_state_t *build_range_and_cb(psl_fsm_t *fsm, fsm_state_t *initial,
                                       void *context)
{
   // The conjunction of the properties for each value from lo to hi is
   // built as a product automaton as separate threads in the same state
   // would stop when the first one is satisfied

   const range_ctx_t *ctx = context;

   if (ctx->lo == ctx->hi)
      return (*ctx->build_nth)(fsm, initial, ctx->node, ctx->lo);

   range_ctx_t left = { ctx->node, ctx->lo, ctx->lo, ctx->build_nth };
   range_ctx_t right = { ctx->node, ctx->lo + 1, ctx->hi, ctx->build_nth };

   return build_and(fsm, initial, build_range_and_cb, &left,
                    build_range_and_cb, &right);
}

static fsm_state_t *build_next_a_cb(psl_fsm_t *fsm, fsm_state_t *state,
                                    psl_node_t p, int64_t k)
{
   // The operand holds k cycles after the current one

   const bool strong = !!(psl_flags(p) & PSL_F_STRONG);

   fsm_state_t *curr = add_state(fsm, p);
   add_edge(fsm, state, curr, EDGE_EPSILON, NULL);

   for (int64_t i = 0; i < k; i++) {
      fsm_state_t *new = add_state(fsm, p);
      new->strong = strong;
      add_edge(fsm, curr, new, EDGE_NEXT, NULL);
      curr = new;
   }

   return build_property(fsm, curr, psl_value(p));
}

static fsm_state_t *build_next_a(psl_fsm_t *fsm, fsm_state_t *state, psl_node_t p)
{
   psl_node_t guard_node = psl_value(p);
   psl_guard_t guard = leaf_guard(fsm, guard_node);
   bool strong = (psl_flags(p) & PSL_F_STRONG) ? true : false;

   int64_t lo,hi = 0;
   psl_node_t range = psl_delay(p);
   get_range(fsm, range, &lo, &hi);

   if (psl_kind(guard_node) != P_HDL_EXPR) {
      // The operand must hold in each cycle from lo to hi
      range_ctx_t ctx = { p, lo, hi, build_next_a_cb };
      return build_range_and_cb(fsm, state, &ctx);
   }

   fsm_state_t *curr = state;

   for (int i = 0; i < hi; i++) {
      fsm_state_t *new = add_state(fsm, p);
      new->strong = strong;
      if (i < lo)
         add_edge(fsm, curr, new, EDGE_NEXT, NULL);
      else
         add_edge(fsm, curr, new, EDGE_NEXT, guard);
      curr = new;
   }

   fsm_state_t *final = add_state(fsm, p);
   add_edge(fsm, curr, final, EDGE_EPSILON, guard);

   return final;
}

static fsm_state_t *build_next_e(psl_fsm_t *fsm, fsm_state_t *state, psl_node_t p)
{
   psl_guard_t guard = leaf_guard(fsm, psl_value(p));
   bool strong = (psl_flags(p) & PSL_F_STRONG) ? true : false;

   psl_guard_t guard_n = not_guard(fsm, guard);
   fsm_state_t *curr = state;

   int64_t lo,hi = 0;
   psl_node_t range = psl_delay(p);
   get_range(fsm, range, &lo, &hi);

   for (int i = 0; i < lo - 1; i++) {
      fsm_state_t *new = add_state(fsm, p);
      new->strong = strong;
      add_edge(fsm, curr, new, EDGE_NEXT, NULL);
      curr = new;
   }

   const int n_states = hi - lo + 1;
   fsm_state_t LOCAL **states = xmalloc_array(n_states, sizeof(fsm_state_t*));

   for (int i = 0; i < n_states; i++) {
      states[i] = add_state(fsm, p);
      states[i]->strong = strong;
   }

   add_edge(fsm, curr, states[n_states - 1], EDGE_NEXT, guard);
   add_edge(fsm, curr, states[0], EDGE_NEXT, guard_n);

   for (int i = 0; i < n_states - 1; i++) {
      add_edge(fsm, states[i], states[n_states - 1], EDGE_NEXT, guard);
      if (i < n_states - 2)
         add_edge(fsm, states[i], states[i + 1], EDGE_NEXT, guard_n);
   }

   return states[n_states - 1];
}

static fsm_state_t *build_next_event(psl_fsm_t *fsm, fsm_state_t *state,
                                     psl_node_t p)
{
   // The property holds at the nth cycle, counting from the current one,
   // in which the Boolean holds

   psl_guard_t b = leaf_guard(fsm, psl_operand(p, 0));
   psl_guard_t not_b = not_guard(fsm, b);

   const bool strong = !!(psl_flags(p) & PSL_F_STRONG);

   int64_t count = 1;
   if (psl_has_delay(p))
      count = get_number(fsm, psl_tree(psl_delay(p)));

   if (count < 1)
      fatal_at(psl_loc(p), "number of occurrences in next_event must be "
               "positive");

   fsm_state_t *curr = state;
   for (int64_t i = 0; i < count - 1; i++) {
      fsm_state_t *wait = add_state(fsm, p);
      wait->strong = strong;
      add_edge(fsm, curr, curr, EDGE_NEXT, not_b);
      add_edge(fsm, curr, wait, EDGE_NEXT, b);
      curr->strong = strong;
      curr = wait;
   }

   curr->strong = strong;
   add_edge(fsm, curr, curr, EDGE_NEXT, not_b);

   fsm_state_t *start = add_state(fsm, p);
   add_edge(fsm, curr, start, EDGE_EPSILON, b);

   return build_property(fsm, start, psl_value(p));
}

static fsm_state_t *build_next_event_nth(psl_fsm_t *fsm, fsm_state_t *state,
                                         psl_node_t p, int64_t k)
{
   // The property holds at the kth cycle, counting from the current one,
   // in which the Boolean holds

   psl_guard_t b = leaf_guard(fsm, psl_operand(p, 0));
   psl_guard_t not_b = not_guard(fsm, b);

   const bool strong = !!(psl_flags(p) & PSL_F_STRONG);

   fsm_state_t *curr = add_state(fsm, p);
   add_edge(fsm, state, curr, EDGE_EPSILON, NULL);

   for (int64_t i = 0; i < k - 1; i++) {
      fsm_state_t *wait = add_state(fsm, p);
      wait->strong = strong;
      curr->strong = strong;
      add_edge(fsm, curr, curr, EDGE_NEXT, not_b);
      add_edge(fsm, curr, wait, EDGE_NEXT, b);
      curr = wait;
   }

   curr->strong = strong;
   add_edge(fsm, curr, curr, EDGE_NEXT, not_b);

   fsm_state_t *start = add_state(fsm, p);
   add_edge(fsm, curr, start, EDGE_EPSILON, b);

   return build_property(fsm, start, psl_value(p));
}

static fsm_state_t *build_next_event_a(psl_fsm_t *fsm, fsm_state_t *state,
                                       psl_node_t p)
{
   // The property holds at each of the ith to jth cycles in which the
   // Boolean holds

   int64_t lo, hi;
   get_range(fsm, psl_delay(p), &lo, &hi);

   if (lo < 1)
      fatal_at(psl_loc(p), "number of occurrences in next_event_a must be "
               "positive");

   range_ctx_t ctx = { p, lo, hi, build_next_event_nth };
   return build_range_and_cb(fsm, state, &ctx);
}

static fsm_state_t *build_next_event_e(psl_fsm_t *fsm, fsm_state_t *state,
                                       psl_node_t p)
{
   // The Boolean operand holds at at least one of the ith to jth cycles
   // in which the first Boolean holds

   psl_guard_t b = leaf_guard(fsm, psl_operand(p, 0));
   psl_guard_t not_b = not_guard(fsm, b);
   psl_guard_t f = leaf_guard(fsm, psl_value(p));
   psl_guard_t not_f = not_guard(fsm, f);

   const bool strong = !!(psl_flags(p) & PSL_F_STRONG);

   int64_t lo, hi;
   get_range(fsm, psl_delay(p), &lo, &hi);

   if (lo < 1)
      fatal_at(psl_loc(p), "number of occurrences in next_event_e must be "
               "positive");

   fsm_state_t *final = add_state(fsm, p);
   fsm_state_t *curr = state;

   for (int64_t k = 1; k <= hi; k++) {
      curr->strong = strong;
      add_edge(fsm, curr, curr, EDGE_NEXT, not_b);

      if (k < lo) {
         fsm_state_t *wait = add_state(fsm, p);
         add_edge(fsm, curr, wait, EDGE_NEXT, b);
         curr = wait;
      }
      else {
         add_edge(fsm, curr, final, EDGE_EPSILON, and_guard(fsm, b, f));

         if (k < hi) {
            fsm_state_t *wait = add_state(fsm, p);
            add_edge(fsm, curr, wait, EDGE_NEXT, and_guard(fsm, b, not_f));
            curr = wait;
         }
      }
   }

   return final;
}

static fsm_state_t *build_eventually(psl_fsm_t *fsm, fsm_state_t *state,
                                     psl_node_t p)
{
   fsm_state_t *wait = add_state(fsm, p);
   fsm_state_t *accept = build_node(fsm, wait, psl_value(p));

   LOCAL_BIT_MASK visited;
   mask_init(&visited, fsm->next_id);

   connect_default(fsm, wait, wait, &visited);

   add_edge(fsm, state, wait, EDGE_NEXT, NULL);
   wait->strong = true;

   return accept;
}

static fsm_state_t *build_before(psl_fsm_t *fsm, fsm_state_t *state,
                                 psl_node_t p)
{
   fsm_state_t *accept = add_state(fsm, p);

   state->strong = !!(psl_flags(p) & PSL_F_STRONG);

   psl_guard_t a = psl_operand(p, 0);
   psl_guard_t b = leaf_guard(fsm, psl_operand(p, 1));

   psl_guard_t accept_guard = a;
   if (!(psl_flags(p) & PSL_F_INCLUSIVE))
      accept_guard = and_guard(fsm, a, not_guard(fsm, b));

   // The property fails if b holds before or, unless inclusive, in the
   // same cycle as a
   add_edge(fsm, state, accept, EDGE_EPSILON, accept_guard);
   add_edge(fsm, state, state, EDGE_NEXT,
            and_guard(fsm, not_guard(fsm, a), not_guard(fsm, b)));

   return accept;
}

static fsm_state_t *build_suffix_impl(psl_fsm_t *fsm, fsm_state_t *state,
                                      psl_node_t p)
{
   fsm_state_t *left = build_node(fsm, state, psl_operand(p, 0));
   fsm_state_t *right = add_state(fsm, p);
   fsm_state_t *vacuous = add_state(fsm, p);

   if (psl_subkind(p) == PSL_SUFFIX_OVERLAP)
      add_edge(fsm, left, right, EDGE_EPSILON, NULL);
   else
      add_edge(fsm, left, right, EDGE_NEXT, NULL);

   LOCAL_BIT_MASK visited;
   mask_init(&visited, fsm->next_id);

   connect_default(fsm, state, vacuous, &visited);

   psl_node_t rhs = psl_operand(p, 1);
   fsm_state_t *final = build_node(fsm, right, rhs);

   switch (psl_kind(rhs)) {
   case P_SERE:
   case P_REPEAT:
   case P_PARAM_SERE:
      // The property holds if any match of the sequence succeeds
      final = determinize_sequence(fsm, right, right, final, rhs);
      break;
   default:
      break;
   }

   add_edge(fsm, vacuous, final, EDGE_EPSILON, NULL);

   return final;
}

static fsm_state_t *build_never(psl_fsm_t *fsm, fsm_state_t *state,
                               psl_node_t p)
{
   // An operand of another property never r is checked as {r} |-> false
   // started in every cycle: the property fails when a match of r ends
   // as there is no edge out of the final state of r

   fsm->kind = FSM_ALWAYS;

   (void)build_node(fsm, state, psl_value(p));

   fsm_state_t *vacuous = add_state(fsm, p);

   LOCAL_BIT_MASK visited;
   mask_init(&visited, fsm->next_id);

   connect_default(fsm, state, vacuous, &visited);

   return vacuous;
}

static fsm_state_t *build_clocked(psl_fsm_t *fsm, fsm_state_t *state,
                                  psl_node_t p)
{
   // The operand is first evaluated at the first tick of its clock at or
   // after the current cycle and its states then wait for ticks of
   // that clock

   psl_node_t saved = fsm->clock;
   fsm->clock = p;

   fsm_state_t *entry = add_state(fsm, p);

   psl_guard_t tick = p;
   add_edge(fsm, state, entry, EDGE_EPSILON, tick);
   add_edge(fsm, state, entry, EDGE_NEXT, not_guard(fsm, tick));

   fsm_state_t *last = build_node(fsm, entry, psl_value(p));

   fsm->clock = saved;
   return last;
}

static fsm_state_t *build_proc_block(psl_fsm_t *fsm, fsm_state_t *state,
                                     psl_node_t p)
{
   // Local variable declarations at the start of a braced SERE or a
   // parenthesised property, or a procedural block executed when the
   // Boolean before it holds (IEEE 1850-2010 sections 6.3 and 6.4)

   tree_t block = psl_tree(p);

   // A block with a single statement may be simplified to it
   const bool is_seq = tree_kind(block) == T_SEQUENCE;

   const int ndecls = is_seq ? tree_decls(block) : 0;
   for (int i = 0; i < ndecls; i++) {
      tree_t d = tree_decl(block, i);
      if (tree_kind(d) != T_VAR_DECL)
         continue;
      else if (!type_is_scalar(tree_type(d)))
         fatal_at(tree_loc(d), "sorry, PSL local variables must have a "
                  "scalar type");

      APUSH(fsm->locals, d);
   }

   psl_node_t value = psl_value(p);

   if (is_seq && tree_stmts(block) == 0)
      return build_node(fsm, state, value);
   else if (psl_kind(value) != P_HDL_EXPR)
      fatal_at(psl_loc(p), "sorry, a procedural block can only follow a "
               "Boolean");

   psl_guard_t guard = and_guard(fsm, leaf_guard(fsm, value),
                                 action_guard(fsm, p));

   fsm_state_t *new = add_state(fsm, p);
   add_edge(fsm, state, new, EDGE_EPSILON, guard);
   return new;
}

static fsm_state_t *build_node(psl_fsm_t *fsm, fsm_state_t *state, psl_node_t p)
{
   switch (psl_kind(p)) {
   case P_NEVER:
      return build_never(fsm, state, p);
   case P_ALWAYS:
      fsm->kind = FSM_ALWAYS;
      return build_property(fsm, state, psl_value(p));
   case P_HDL_EXPR:
      {
         fsm_state_t *new = add_state(fsm, p);
         add_edge(fsm, state, new, EDGE_EPSILON, leaf_guard(fsm, p));
         return new;
      }
   case P_NEXT:
      return build_next(fsm, state, p);
   case P_NEXT_E:
      return build_next_e(fsm, state, p);
   case P_NEXT_A:
      return build_next_a(fsm, state, p);
   case P_SERE:
      return build_sere(fsm, state, p);
   case P_PARAM_SERE:
      return build_param_sere(fsm, state, p);
   case P_PARAM_PROP:
      return build_param_prop(fsm, state, p);
   case P_REPEAT:
      return build_repeat(fsm, state, p);
   case P_LOGICAL:
      return build_logical(fsm, state, p);
   case P_UNTIL:
      return build_until(fsm, state, p);
   case P_NEXT_EVENT:
      return build_next_event(fsm, state, p);
   case P_NEXT_EVENT_A:
      return build_next_event_a(fsm, state, p);
   case P_NEXT_EVENT_E:
      return build_next_event_e(fsm, state, p);
   case P_EVENTUALLY:
      return build_eventually(fsm, state, p);
   case P_ABORT:
      return build_abort(fsm, state, p);
   case P_BEFORE:
      return build_before(fsm, state, p);
   case P_SUFFIX_IMPL:
      return build_suffix_impl(fsm, state, p);
   case P_CLOCKED:
      return build_clocked(fsm, state, p);
   case P_PROC_BLOCK:
      return build_proc_block(fsm, state, p);
   default:
      CANNOT_HANDLE(p);
   }
}

#ifdef DEBUG
static void psl_loops_dfs(psl_fsm_t *fsm, fsm_state_t *state,
                          bit_mask_t *discovered, bit_mask_t *finished)
{
   mask_set(discovered, state->id);

   bool have_guarded = false, have_unguarded = false;
   for (fsm_edge_t *e = state->edges; e; e = e->next) {
      if (e->kind != EDGE_EPSILON || mask_test(finished, e->dest->id))
         continue;
      else if (mask_test(discovered, e->dest->id)) {
         psl_fsm_dump(fsm, "loop");
         fatal_trace("detected loop in PSL state machine %d -> %d",
                     state->id, e->dest->id);
      }
      else
         psl_loops_dfs(fsm, e->dest, discovered, finished);

      if ((have_unguarded && e->guard != NULL)
          || (have_guarded && e->guard == NULL)) {
         psl_fsm_dump(fsm, "unguarded");
         fatal_trace("unguarded epsilon edge cannot be mixed with guarded "
                     "epsilon edges (%d -> %d)", state->id, e->dest->id);
      }

      have_guarded |= (e->guard != NULL);
      have_unguarded |= (e->guard == NULL);
   }

   mask_clear(discovered, state->id);
   mask_set(finished, state->id);
}

static void psl_detect_loops(psl_fsm_t *fsm)
{
   LOCAL_BIT_MASK discovered, finished;
   mask_init(&discovered, fsm->next_id);
   mask_init(&finished, fsm->next_id);

   for (fsm_state_t *it = fsm->states; it; it = it->next) {
      if (!mask_test(&finished, it->id))
         psl_loops_dfs(fsm, it, &discovered, &finished);
   }

   assert(mask_popcount(&discovered) == 0);
   assert(mask_popcount(&finished) == fsm->next_id);
}
#endif

static void psl_simplify_dfs(psl_fsm_t *fsm, fsm_state_t *state,
                             bit_mask_t *visited)
{
   mask_set(visited, state->id);

   for (fsm_edge_t **p = &(state->edges), *e = *p; e; e = *p) {
      if (e->kind == EDGE_EPSILON) {
         if (!mask_test(visited, e->dest->id))
            psl_simplify_dfs(fsm, e->dest, visited);

         if (e->dest->accept) {
            // A null guard is always true
            psl_guard_t guard = and_guard(fsm, e->guard, e->dest->guard);
            if (!state->accept)
               state->guard = guard;
            else if (state->guard != NULL)
               state->guard = (guard == NULL) ? NULL
                  : or_guard(fsm, state->guard, guard);

            state->accept = true;
         }

         fsm_edge_t *first = e->dest->edges;
         if (first == NULL)
            *p = e->next;
         else {
            for (fsm_edge_t *e2 = first->next; e2; e2 = e2->next) {
               assert(e2->kind != EDGE_EPSILON);
               psl_guard_t guard = and_guard(fsm, e->guard, e2->guard);
               add_edge(fsm, state, e2->dest, e2->kind, guard);
            }

            assert(first->kind != EDGE_EPSILON);
            e->kind  = first->kind;
            e->dest  = first->dest;
            e->guard = and_guard(fsm, e->guard, first->guard);

            p = &(e->next);
         }
      }
      else
         p = &(e->next);
   }
}

static void psl_simplify_from(psl_fsm_t *fsm, fsm_state_t *state,
                              bit_mask_t *visited)
{
   // Remove epsilon transitions from all states reachable from state

   if (!mask_test(visited, state->id))
      psl_simplify_dfs(fsm, state, visited);

   for (fsm_edge_t *e = state->edges; e; e = e->next) {
      if (!mask_test(visited, e->dest->id))
         psl_simplify_from(fsm, e->dest, visited);
   }
}

static void psl_simplify(psl_fsm_t *fsm)
{
   LOCAL_BIT_MASK visited;
   mask_init(&visited, fsm->next_id);

   for (fsm_state_t *it = fsm->states; it; it = it->next) {
      if (!mask_test(&visited, it->id))
         psl_simplify_dfs(fsm, it, &visited);
   }

   assert(mask_popcount(&visited) == fsm->next_id);
}

static void guard_signature(psl_guard_t g, text_buf_t *tb)
{
   if (g == NULL) {
      tb_cat(tb, "T");
      return;
   }

   switch (psl_guard_kind(g)) {
   case GUARD_BINOP:
      {
         const guard_binop_t *bop = psl_guard_binop(g);
         tb_printf(tb, "(%d ", bop->kind);
         guard_signature(bop->left, tb);
         tb_append(tb, ' ');
         guard_signature(bop->right, tb);
         tb_append(tb, ')');
      }
      break;
   default:
      tb_printf(tb, "%p", g);
      break;
   }
}

static int compare_strings(const void *a, const void *b)
{
   return strcmp(*(const char **)a, *(const char **)b);
}

static void psl_merge(psl_fsm_t *fsm)
{
   // Merge states with the same behaviour so that an obligation is
   // not tracked more than once

   const unsigned n = fsm->next_id;
   fsm_state_t **by_id LOCAL = xcalloc_array(n, sizeof(fsm_state_t *));
   fsm_state_t **rep LOCAL = xcalloc_array(n, sizeof(fsm_state_t *));

   for (fsm_state_t *s = fsm->states; s; s = s->next)
      by_id[s->id] = rep[s->id] = s;

   for (bool changed = true; changed; ) {
      changed = false;

      shash_t *map = shash_new(n * 2);

      for (unsigned i = 0; i < n; i++) {
         fsm_state_t *s = by_id[i];
         if (s == NULL || rep[i] != s)
            continue;

         LOCAL_TEXT_BUF tb = tb_new();
         tb_printf(tb, "%d%d%d%d%p ", s->initial, s->accept, s->strong,
                   s->clock != NULL, s->clock);
         guard_signature(s->guard, tb);

         unsigned nedges = 0;
         for (fsm_edge_t *e = s->edges; e; e = e->next)
            nedges++;

         char **sigs LOCAL = xmalloc_array(MAX(nedges, 1), sizeof(char *));
         unsigned k = 0;
         for (fsm_edge_t *e = s->edges; e; e = e->next) {
            LOCAL_TEXT_BUF etb = tb_new();
            fsm_state_t *dest = rep[e->dest->id];
            tb_printf(etb, "%d:%u:", e->kind, dest->id);
            guard_signature(e->guard, etb);
            sigs[k++] = tb_claim(etb);
         }

         qsort(sigs, nedges, sizeof(char *), compare_strings);

         for (unsigned j = 0; j < nedges; j++) {
            if (j == 0 || strcmp(sigs[j], sigs[j - 1]) != 0)
               tb_printf(tb, "|%s", sigs[j]);
         }

         for (unsigned j = 0; j < nedges; j++)
            free(sigs[j]);

         fsm_state_t *exist = shash_get(map, tb_get(tb));
         if (exist != NULL) {
            rep[i] = exist;
            changed = true;
         }
         else
            shash_put(map, tb_get(tb), s);
      }

      shash_free(map);
   }

   for (fsm_state_t *s = fsm->states; s; s = s->next) {
      for (fsm_edge_t *e = s->edges; e; e = e->next)
         e->dest = rep[e->dest->id];

      // Remove duplicate edges
      for (fsm_edge_t *e = s->edges; e; e = e->next) {
         for (fsm_edge_t **p = &(e->next); *p; ) {
            if ((*p)->kind == e->kind && (*p)->dest == e->dest
                && same_guard((*p)->guard, e->guard))
               *p = (*p)->next;
            else
               p = &((*p)->next);
         }
      }
   }
}

static void psl_prune_dfs(psl_fsm_t *fsm, fsm_state_t *state,
                          bit_mask_t *visited)
{
   if (mask_test_and_set(visited, state->id))
      return;

   state->next = NULL;

   *fsm->tail = state;
   fsm->tail = &(state->next);

   if (state->accept && state->guard == NULL && !psl_fsm_matches(fsm))
      state->edges = NULL;   // Unreachable

   for (fsm_edge_t *e = state->edges; e; e = e->next) {
      assert(e->kind != EDGE_EPSILON);
      psl_prune_dfs(fsm, e->dest, visited);
   }
}

static void psl_prune(psl_fsm_t *fsm)
{
   LOCAL_BIT_MASK visited;
   mask_init(&visited, fsm->next_id);

   fsm_state_t *s0 = fsm->states;
   assert(s0->initial);

   mask_clearall(&visited);
   fsm->next_id = 0;
   fsm->tail    = &(fsm->states);
   fsm->states  = NULL;

   psl_prune_dfs(fsm, s0, &visited);

   for (fsm_state_t *it = fsm->states; it; it = it->next)
      it->id = fsm->next_id++;
}

static psl_fsm_t *psl_fsm_build(psl_node_t src, psl_node_t root,
                                fsm_kind_t kind, ident_t label)
{
   psl_fsm_t *fsm = xcalloc(sizeof(psl_fsm_t));
   fsm->pool  = pool_new();
   fsm->label = label;
   fsm->tail  = &(fsm->states);
   fsm->src   = src;
   fsm->kind  = kind;

   if (psl_kind(root) == P_NEVER) {
      // Every match of the operand of an outermost never is a failure
      fsm->kind = FSM_NEVER;
      root = psl_value(root);
   }

   fsm_state_t *initial = add_state(fsm, src), *final = initial;
   initial->initial = true;

   if (kind == FSM_COVER || fsm->kind == FSM_NEVER)
      final = build_node(fsm, initial, root);
   else
      final = build_property(fsm, initial, root);
   final->accept = true;

   DEBUG_ONLY(psl_detect_loops(fsm));

   const bool verbose = opt_get_verbose(OPT_PSL_VERBOSE, istr(label));
   if (verbose)
      psl_fsm_dump(fsm, "initial");

   psl_simplify(fsm);
   psl_prune(fsm);
   psl_merge(fsm);
   psl_prune(fsm);

   if (verbose)
      psl_fsm_dump(fsm, "final");

   return fsm;
}

psl_fsm_t *psl_fsm_new(psl_node_t p, ident_t label)
{
   const fsm_kind_t kind = psl_kind(p) == P_COVER ? FSM_COVER : FSM_BARE;

   // The outermost clock expression is handled by the caller
   psl_node_t root = psl_value(p);
   if (psl_kind(root) == P_CLOCKED)
      root = psl_value(root);

   return psl_fsm_build(p, root, kind, label);
}

psl_fsm_t *psl_fsm_new_sequence(psl_node_t seq, ident_t label)
{
   return psl_fsm_build(seq, seq, FSM_COVER, label);
}

void psl_fsm_free(psl_fsm_t *fsm)
{
   ACLEAR(fsm->locals);
   pool_free(fsm->pool);
   free(fsm);
}

bool psl_fsm_matches(psl_fsm_t *fsm)
{
   // Every match of the sequence is reported not just the first
   return fsm->kind == FSM_COVER || fsm->kind == FSM_NEVER;
}

bool psl_fsm_repeating(psl_fsm_t *fsm)
{
   return fsm->kind == FSM_COVER || fsm->kind == FSM_ALWAYS
      || fsm->kind == FSM_NEVER;
}

guard_kind_t psl_guard_kind(psl_guard_t g)
{
   const unsigned tag = pointer_tag(g);
   switch (tag) {
   case GUARD_EXPR:
   case GUARD_BINOP:
   case GUARD_NOT:
   case GUARD_FALSE:
   case GUARD_BOUND:
   case GUARD_NOT_BOUND:
   case GUARD_ACTION:
      return tag;
   default:
      should_not_reach_here();
   }
}

const guard_binop_t *psl_guard_binop(psl_guard_t g)
{
   assert(psl_guard_kind(g) == GUARD_BINOP);
   return untag_pointer(g, guard_binop_t);
}

psl_node_t psl_guard_expr(psl_guard_t g)
{
   switch (pointer_tag(g)) {
   case GUARD_EXPR:
   case GUARD_NOT:
      return untag_pointer(g, struct _psl_node);
   case GUARD_BOUND:
   case GUARD_NOT_BOUND:
      return psl_guard_atom(g)->expr;
   default:
      should_not_reach_here();
   }
}

const guard_atom_t *psl_guard_atom(psl_guard_t g)
{
   switch (pointer_tag(g)) {
   case GUARD_BOUND:
   case GUARD_NOT_BOUND:
      return untag_pointer(g, guard_atom_t);
   default:
      should_not_reach_here();
   }
}

const guard_action_t *psl_guard_action(psl_guard_t g)
{
   assert(psl_guard_kind(g) == GUARD_ACTION);
   return untag_pointer(g, guard_action_t);
}

static int attempt_depth(fsm_state_t *s, int *depth, bit_mask_t *active)
{
   // Longest number of cycles a state of an evaluation can be pending or
   // -1 if the automaton has a cycle

   if (depth[s->id] != 0)
      return depth[s->id];
   else if (mask_test(active, s->id))
      return -1;

   mask_set(active, s->id);

   int result = 1;
   for (fsm_edge_t *e = s->edges; e; e = e->next) {
      if (e->dest->initial)
         continue;

      const int d = attempt_depth(e->dest, depth, active);
      if (d < 0) {
         result = -1;
         break;
      }

      result = MAX(result, d + 1);
   }

   mask_clear(active, s->id);
   return (depth[s->id] = result);
}

unsigned psl_fsm_max_attempts(psl_fsm_t *fsm)
{
   // Maximum number of evaluations that can be in progress at once in
   // the states after the initial state, or zero if unbounded

   int *depth LOCAL = xcalloc_array(fsm->next_id, sizeof(int));

   LOCAL_BIT_MASK active;
   mask_init(&active, fsm->next_id);

   int longest = 0;
   for (fsm_edge_t *e = fsm->states->edges; e; e = e->next) {
      if (e->dest->initial)
         continue;

      const int d = attempt_depth(e->dest, depth, &active);
      if (d < 0)
         return 0;

      longest = MAX(longest, d);
   }

   return longest + 1;
}

// LCOV_EXCL_START /////////////////////////////////////////////////////////////

static void psl_dump_guard(psl_guard_t g, text_buf_t *tb, bool nested)
{
   switch (psl_guard_kind(g)) {
   case GUARD_NOT:
   case GUARD_NOT_BOUND:
      tb_cat(tb, "!");
      // Fall-through
   case GUARD_EXPR:
   case GUARD_BOUND:
      {
         psl_node_t p = psl_guard_expr(g);
         assert(psl_kind(p) == P_HDL_EXPR);

         tree_t t = psl_tree(p);

         const bool is_cconv = tree_kind(t) == T_FCALL
            && is_well_known(tree_ident(t)) == W_OP_CCONV;

         vhdl_dump(is_cconv ? tree_param(t, 0) : t, 0);
      }
      break;
   case GUARD_BINOP:
      {
         if (nested) tb_append(tb, '(');

         const guard_binop_t *bop = psl_guard_binop(g);
         psl_dump_guard(bop->left, tb, true);

         switch (bop->kind) {
         case BINOP_AND: tb_cat(tb, " && "); break;
         case BINOP_OR: tb_cat(tb, " || "); break;
         default: should_not_reach_here();
         }

         psl_dump_guard(bop->right, tb, true);

         if (nested) tb_append(tb, ')');
      }
      break;
   case GUARD_FALSE:
      tb_cat(tb, "false");
      break;
   case GUARD_ACTION:
      tb_cat(tb, "[[...]]");
      break;
   default:
      should_not_reach_here();
   }
}

static void psl_dump_label(FILE *f, psl_guard_t g)
{
   LOCAL_TEXT_BUF tb = tb_new();
   capture_syntax(tb);
   psl_dump_guard(g, tb, false);

   for (const char *p = tb_get(tb); *p; p++) {
      if (*p == '"')
         fputs("\\\"", f);
      else
         fputc(*p, f);
   }

   capture_syntax(NULL);
}

void psl_fsm_dump(psl_fsm_t *fsm, const char *tag)
{
#ifdef HAVE_POPEN
   char *cmd LOCAL = xasprintf("dot -Tsvg -o %s.%s.svg", istr(fsm->label), tag);
   FILE *f = popen(cmd, "w");
   if (f == NULL)
      fatal_errno("failed to start dot process");

   fprintf(f, "digraph psl {\n");

   for (fsm_state_t *s = fsm->states; s; s = s->next) {
      if (s->accept || s->initial) {
         fprintf(f, "%d [", s->id);

         if (s->accept) fprintf(f, "peripheries=2,");
         if (s->initial) fprintf(f, "style=bold,");

         if (s->guard != NULL) {
            fprintf(f, "label=\"%d: ", s->id);
            psl_dump_label(f, s->guard);
            fputs("\",", f);
         }

         fputs("];\n", f);
      }
#ifdef DEBUG
      else if (s->mergekey != 0)
         fprintf(f, "%d [label=\"%d (%d ∩ %d)\"]", s->id, s->id,
                 (int)(s->mergekey >> 32), (int)s->mergekey);
#endif

      for (fsm_edge_t *e = s->edges; e; e = e->next) {
         fprintf(f, "%d -> %d [", s->id, e->dest->id);
         if (e->guard != NULL) {
            fprintf(f, "label=\"");
            psl_dump_label(f, e->guard);
            fputs("\",", f);
         }
         if (e->kind == EDGE_EPSILON)
            fputs("style=dashed,", f);
         fprintf(f, "];\n");
      }
   }

   fprintf(f, "}\n");

   if (pclose(f) != 0)
      fatal_errno("dot process failed");

   debugf("wrote PSL state machine graph to %s.%s.svg", istr(fsm->label), tag);
#else
   warnf("PSL state machine graph is not supported on this platform");
#endif
}

// LCOV_EXCL_STOP //////////////////////////////////////////////////////////////
