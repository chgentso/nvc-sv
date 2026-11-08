//
//  Copyright (C) 2023-2025  Nick Gasson
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
#include "cov/cov-api.h"
#include "hash.h"
#include "lib.h"
#include "lower.h"
#include "option.h"
#include "psl/psl-fsm.h"
#include "psl/psl-node.h"
#include "psl/psl-phase.h"
#include "tree.h"
#include "type.h"
#include "vcode.h"
#include "vhdl/vhdl-phase.h"

#include <assert.h>
#include <stdlib.h>

#define PSL_BLOCK_CASE  1
#define PSL_BLOCK_ABORT 2
#define PSL_BLOCK_PREV  3

typedef A(psl_node_t) clock_list_t;

typedef struct {
   psl_node_t    main_clock;
   bool          multiclock;
   vcode_block_t prev_tail;
   psl_fsm_t    *fsm;
   unsigned      copies;
   vcode_var_t  *locals;
   vcode_var_t  *busy;
   vcode_var_t  *busy_next;
} psl_ctx_t;

static char psl_ctx_key;

static void psl_wait_cb(tree_t t, void *ctx)
{
   lower_unit_t *lu = ctx;

   vcode_reg_t nets_reg = lower_lvalue(lu, t);
   vcode_reg_t count_reg = emit_const(vtype_offset(), type_width(tree_type(t)));
   emit_sched_event(nets_reg, count_reg);
}

static vcode_reg_t psl_lower_clock_now(lower_unit_t *lu, psl_node_t clk);

static vcode_reg_t psl_lower_boolean(lower_unit_t *lu, psl_node_t p)
{
   if (psl_kind(p) == P_CLOCKED || psl_kind(p) == P_CLOCK_DECL)
      return psl_lower_clock_now(lu, p);

   assert(psl_kind(p) == P_HDL_EXPR);
   vcode_reg_t test_reg = lower_rvalue(lu, psl_tree(p));

   if (!vtype_eq(vcode_reg_type(test_reg), vtype_bool())) {
      vcode_type_t std_logic = vtype_int(0, 8);
      vcode_reg_t one_reg = emit_const(std_logic, 3);
      return emit_cmp(VCODE_CMP_EQ, test_reg, one_reg);
   }
   else
      return test_reg;
}

static vcode_reg_t lower_get_rand_bool(void)
{
   ident_t func = ident_new("NVC.RANDOM.GET_RANDOM()B");

   vcode_reg_t context_reg = emit_link_package(ident_new("NVC.RANDOM"));
   vcode_reg_t args[] = { context_reg };

   vcode_type_t vbool = vtype_bool();
   return emit_fcall(func, vbool, VCODE_INVALID_STAMP, args, 1);
}

static vcode_reg_t lower_get_rand_int(void)
{
   ident_t func = ident_new("NVC.RANDOM.GET_RANDOM()19NVC.RANDOM.T_UINT32");

   vcode_reg_t context_reg = emit_link_package(ident_new("NVC.RANDOM"));
   vcode_reg_t args[] = { context_reg };

   vcode_type_t vint = vtype_int(0, UINT32_MAX);
   return emit_fcall(func, vint, VCODE_INVALID_STAMP, args, 1);
}

vcode_reg_t psl_lower_union(lower_unit_t *lu, psl_node_t p)
{
   vcode_reg_t lhs = lower_rvalue(lu, psl_tree(psl_operand(p, 0)));
   vcode_reg_t rhs = lower_rvalue(lu, psl_tree(psl_operand(p, 1)));
   vcode_reg_t sel = lower_get_rand_bool();

   return emit_select(sel, lhs, rhs);
}

static vcode_reg_t psl_debug_locus(psl_node_t p)
{
   return emit_debug_locus(psl_to_object(p));
}

static vcode_reg_t psl_assert_severity(void)
{
   return emit_const(vtype_int(0, 3), 2);
}

static void psl_bind_params(lower_unit_t *lu, const psl_binding_t *binding)
{
   for (const psl_binding_t *b = binding; b; b = b->outer) {
      type_t type = tree_type(b->decl);

      vcode_reg_t value_reg;
      if (b->set != NULL) {
         vcode_type_t velem = lower_type(type_elem(type));
         vcode_reg_t *elems LOCAL =
            xmalloc_array(b->nelems, sizeof(vcode_reg_t));

         int64_t comb = b->value;
         for (int i = 0; i < b->nelems; i++) {
            elems[i] = emit_const(velem, b->set[comb % b->setsize]);
            comb /= b->setsize;
         }

         vcode_type_t varray = vtype_carray(b->nelems, velem);
         vcode_reg_t array_reg = emit_const_array(varray, elems, b->nelems);
         value_reg = emit_address_of(array_reg);
      }
      else
         value_reg = emit_const(lower_type(type), b->value);

      lower_put_vcode_obj(b->decl, value_reg, lu);
   }
}

static void psl_unbind_params(lower_unit_t *lu, const psl_binding_t *binding)
{
   for (const psl_binding_t *b = binding; b; b = b->outer)
      lower_put_vcode_obj(b->decl, VCODE_INVALID_REG, lu);
}

static vcode_reg_t psl_lower_bound_boolean(lower_unit_t *lu,
                                           const guard_atom_t *atom)
{
   // Evaluate the expression with the values of parameters of the
   // replicated SERE in which it appears

   psl_bind_params(lu, atom->binding);
   vcode_reg_t result_reg = psl_lower_boolean(lu, atom->expr);
   psl_unbind_params(lu, atom->binding);

   return result_reg;
}

static vcode_reg_t psl_lower_action(lower_unit_t *lu,
                                    const guard_action_t *action)
{
   // Execute a procedural block when the Boolean before it holds

   psl_bind_params(lu, action->binding);
   lower_block_stmts(lu, psl_tree(action->block));
   psl_unbind_params(lu, action->binding);

   return emit_const(vtype_bool(), 1);
}

static vcode_reg_t psl_lower_guard(lower_unit_t *lu, psl_guard_t g)
{
   switch (psl_guard_kind(g)) {
   case GUARD_EXPR:
      return psl_lower_boolean(lu, psl_guard_expr(g));
   case GUARD_BOUND:
      return psl_lower_bound_boolean(lu, psl_guard_atom(g));
   case GUARD_NOT_BOUND:
      return emit_not(psl_lower_bound_boolean(lu, psl_guard_atom(g)));
   case GUARD_BINOP:
      {
         // The right hand operand is only evaluated if the left hand
         // operand does not determine the result
         const guard_binop_t *bop = psl_guard_binop(g);
         vcode_reg_t left_reg = psl_lower_guard(lu, bop->left);

         vcode_type_t vbool = vtype_bool();
         vcode_var_t tmp_var = emit_var(vbool, VCODE_INVALID_STAMP,
                                        ident_uniq("guard"), VAR_TEMP);
         emit_store(left_reg, tmp_var);

         vcode_block_t right_bb = emit_block();
         vcode_block_t done_bb = emit_block();

         switch (bop->kind) {
         case BINOP_AND:
            emit_cond(left_reg, right_bb, done_bb);
            break;
         case BINOP_OR:
            emit_cond(left_reg, done_bb, right_bb);
            break;
         default:
            should_not_reach_here();
         }

         vcode_select_block(right_bb);
         emit_store(psl_lower_guard(lu, bop->right), tmp_var);
         emit_jump(done_bb);

         vcode_select_block(done_bb);
         return emit_load(tmp_var);
      }
   case GUARD_NOT:
      return emit_not(psl_lower_boolean(lu, psl_guard_expr(g)));
   case GUARD_ACTION:
      return psl_lower_action(lu, psl_guard_action(g));
   case GUARD_FALSE:
      return emit_const(vtype_bool(), 0);
   default:
      should_not_reach_here();
   }
}

static void psl_lower_notify(const char *func_name)
{
   // Record the event for 'SIGNAL and the STD.ENV PSL functions

   ident_t func = ident_new(func_name);

   vcode_reg_t context_reg = emit_link_package(ident_new("NVC.SIM_PKG"));
   vcode_reg_t args[] = { context_reg };
   emit_fcall(func, VCODE_INVALID_TYPE, VCODE_INVALID_STAMP, args,
              ARRAY_LEN(args));
}

static void psl_lower_cover(lower_unit_t *lu, psl_node_t p)
{
   if (standard() >= STD_19)
      psl_lower_notify("NVC.SIM_PKG.PSL_COVER_HIT()B");

   if (psl_has_message(p)) {
      tree_t m = psl_message(p);
      vcode_reg_t msg_reg = lower_rvalue(lu, m);

      vcode_type_t voffset = vtype_offset();
      vcode_reg_t count_reg = emit_const(voffset, type_width(tree_type(m)));

      vcode_reg_t severity_reg = emit_const(vtype_int(0, 3), 0);
      vcode_reg_t locus = psl_debug_locus(p);

      emit_report(msg_reg, count_reg, severity_reg, locus);
   }

   if (!cover_enabled(lu->cover, COVER_MASK_FUNCTIONAL))
      return;

   cover_obj_t item = cover_get_item(lu->cover, lu->cscope,
                                     COV_ITEM_FUNCTIONAL, 0);
   if (cover_is_null(item))
      return;

   cover_obj_t bin = cover_at(lu->cover, item, COV_REL_BINS, 0);
   uint32_t tag = cover_get_u32(lu->cover, bin, COV_ATTR_TAG, -1);

   cover_obj_t inst = cover_get_obj(lu->cover, lu->cscope, COV_ATTR_INST);
   ident_t qual_name = cover_get_ident(lu->cover, inst, COV_ATTR_QUAL_NAME);

   // TODO: move this to initialisation
   vcode_reg_t counters = emit_get_counters(qual_name);
   emit_cover_stmt(counters, tag);
}

static void psl_lower_assert(lower_unit_t *lu, vcode_reg_t taken_reg,
                             vcode_reg_t locus, psl_node_t p)
{
   vcode_reg_t severity_reg = psl_assert_severity();

   vcode_reg_t msg_reg = VCODE_INVALID_REG;
   vcode_reg_t len_reg = VCODE_INVALID_REG;

   if (psl_has_message(p)) {
      tree_t t_msg = psl_message(p);
      vcode_reg_t message_wrapped = lower_rvalue(lu, t_msg);
      if (vcode_reg_kind(message_wrapped) == VCODE_TYPE_UARRAY) {
         msg_reg = emit_unwrap(message_wrapped);
         len_reg = emit_uarray_len(message_wrapped, 0);
      }
      else {
         msg_reg = message_wrapped;
         len_reg = emit_const(vtype_offset(), type_width(tree_type(t_msg)));
      }
   }
   else if (psl_kind(p) == P_ASSUME) {
      static const char text[] = "PSL assumption failed";
      const int len = sizeof(text) - 1;

      vcode_type_t vchar = vtype_int(0, 255);
      vcode_reg_t chars[len];
      for (int i = 0; i < len; i++)
         chars[i] = emit_const(vchar, text[i]);

      vcode_type_t array_type = vtype_carray(len, vchar);
      msg_reg = emit_address_of(emit_const_array(array_type, chars, len));
      len_reg = emit_const(vtype_offset(), len);
   }

   emit_assert(taken_reg, msg_reg, len_reg,
               severity_reg, locus, VCODE_INVALID_REG,
               VCODE_INVALID_REG);

   if (standard() >= STD_19) {
      vcode_block_t fail_bb = emit_block();
      vcode_block_t cont_bb = emit_block();
      emit_cond(taken_reg, cont_bb, fail_bb);

      vcode_select_block(fail_bb);
      psl_lower_notify("NVC.SIM_PKG.PSL_ASSERT_FAIL()B");
      emit_jump(cont_bb);

      vcode_select_block(cont_bb);
   }
}

static psl_ctx_t *psl_get_ctx(lower_unit_t *lu);
static vcode_reg_t psl_lower_clock_now(lower_unit_t *lu, psl_node_t clk);

static void psl_enter_state(lower_unit_t *lu, fsm_state_t *state,
                            unsigned copy)
{
   vcode_reg_t strong_reg = VCODE_INVALID_REG;
   if (state->strong)
      strong_reg = emit_const(vtype_bool(), 1);

   // Each evaluation of a property with local variables uses one copy
   // of the states after the initial state
   psl_ctx_t *ctx = psl_get_ctx(lu);
   unsigned id = state->id;
   if (ctx->copies > 0 && !state->initial) {
      id += copy * ctx->fsm->next_id;
      emit_store(emit_const(vtype_bool(), 1), ctx->busy_next[copy]);
   }

   vcode_type_t vint32 = vtype_int(INT32_MIN, INT32_MAX);
   emit_enter_state(emit_const(vint32, id), strong_reg);
}

static void psl_select_locals(lower_unit_t *lu, unsigned copy)
{
   psl_ctx_t *ctx = psl_get_ctx(lu);
   for (int i = 0; i < ctx->fsm->locals.count; i++) {
      vcode_var_t var = ctx->locals[i * ctx->copies + copy];
      lower_put_vcode_obj(ctx->fsm->locals.items[i], var, lu);
   }
}

static void psl_lower_state_body(lower_unit_t *lu, psl_fsm_t *fsm,
                                 fsm_state_t *state, unsigned copy);

static void psl_lower_new_attempt(lower_unit_t *lu, psl_fsm_t *fsm,
                                  fsm_state_t *state)
{
   // Find a copy of the local variables and states not used by an
   // evaluation that is still in progress

   psl_ctx_t *ctx = psl_get_ctx(lu);

   for (unsigned c = 0; c < ctx->copies; c++) {
      vcode_block_t use_bb = emit_block();
      vcode_block_t next_bb = emit_block();

      vcode_reg_t busy_reg = emit_load(ctx->busy[c]);
      emit_cond(busy_reg, next_bb, use_bb);

      vcode_select_block(use_bb);

      emit_store(emit_const(vtype_bool(), 1), ctx->busy[c]);

      psl_select_locals(lu, c);

      for (int i = 0; i < fsm->locals.count; i++) {
         tree_t decl = fsm->locals.items[i];
         type_t type = tree_type(decl);

         tree_t init;
         if (tree_has_value(decl))
            init = tree_value(decl);
         else
            init = tree_left(range_of(type, 0));

         vcode_reg_t init_reg = lower_rvalue(lu, init);
         vcode_var_t var = ctx->locals[i * ctx->copies + c];
         emit_store(emit_cast(lower_type(type), lower_bounds(type), init_reg),
                    var);
      }

      psl_lower_state_body(lu, fsm, state, c);

      vcode_select_block(next_bb);
   }

   vcode_reg_t locus = psl_debug_locus(fsm->src);

   static const char text[] = "sorry, too many evaluations of this PSL "
      "property with local variables are in progress";
   const int len = sizeof(text) - 1;

   vcode_type_t vchar = vtype_int(0, 255);
   vcode_reg_t *chars LOCAL = xmalloc_array(len, sizeof(vcode_reg_t));
   for (int i = 0; i < len; i++)
      chars[i] = emit_const(vchar, text[i]);

   vcode_type_t array_type = vtype_carray(len, vchar);
   vcode_reg_t msg_reg =
      emit_address_of(emit_const_array(array_type, chars, len));
   vcode_reg_t len_reg = emit_const(vtype_offset(), len);
   vcode_reg_t severity_reg = emit_const(vtype_int(0, 3), 3);

   emit_assert(emit_const(vtype_bool(), 0), msg_reg, len_reg, severity_reg,
               locus, VCODE_INVALID_REG, VCODE_INVALID_REG);
   emit_return(VCODE_INVALID_REG);
}

static void psl_lower_state(lower_unit_t *lu, psl_fsm_t *fsm,
                            fsm_state_t *state, unsigned copy)
{
   emit_comment("Property state %d copy %u", state->id, copy);

   psl_ctx_t *ctx = psl_get_ctx(lu);

   if (ctx->copies > 0)
      psl_select_locals(lu, copy);

   if (ctx->multiclock) {
      // Only evaluate the state on the ticks of its clock
      vcode_reg_t tick_reg =
         psl_lower_clock_now(lu, state->clock ?: ctx->main_clock);

      vcode_block_t run_bb = emit_block();
      vcode_block_t wait_bb = emit_block();
      emit_cond(tick_reg, run_bb, wait_bb);

      vcode_select_block(wait_bb);
      psl_enter_state(lu, state, copy);
      emit_return(VCODE_INVALID_REG);

      vcode_select_block(run_bb);
   }

   if (state->initial && psl_fsm_repeating(fsm))
      psl_enter_state(lu, state, copy);

   if (state->initial && ctx->copies > 0)
      psl_lower_new_attempt(lu, fsm, state);
   else
      psl_lower_state_body(lu, fsm, state, copy);
}

static void psl_lower_state_body(lower_unit_t *lu, psl_fsm_t *fsm,
                                 fsm_state_t *state, unsigned copy)
{
   vcode_type_t vbool = vtype_bool();
   vcode_reg_t vfalse = emit_const(vbool, 0);
   vcode_reg_t vtrue = emit_const(vbool, 1);

   if (state->accept) {
      vcode_block_t cont_bb = vcode_active_block();
      if (state->guard != NULL) {
         vcode_block_t accept_bb = emit_block();
         cont_bb = emit_block();

         vcode_reg_t guard_reg = psl_lower_guard(lu, state->guard);
         emit_cond(guard_reg, accept_bb, cont_bb);

         vcode_select_block(accept_bb);
      }

      if (fsm->kind == FSM_COVER)
         psl_lower_cover(lu, fsm->src);
      else if (fsm->kind == FSM_NEVER) {
         vcode_reg_t locus = psl_debug_locus(fsm->src);
         psl_lower_assert(lu, vfalse, locus, fsm->src);
      }
      else if (standard() >= STD_19)
         psl_lower_notify("NVC.SIM_PKG.PSL_ASSERT_PASS()B");

      // Later matches of a sequence are also reported
      const bool more = psl_fsm_matches(fsm) && state->edges != NULL;

      if (more && state->guard != NULL)
         emit_jump(cont_bb);
      else if (!more)
         emit_return(VCODE_INVALID_REG);

      if (state->guard == NULL && !more)
         return;
      else if (state->guard != NULL)
         vcode_select_block(cont_bb);
   }

   vcode_reg_t taken_reg = vfalse;

   for (fsm_edge_t *e = state->edges; e; e = e->next) {
      assert(e->kind == EDGE_NEXT);

      if (e->guard != NULL) {
         vcode_reg_t guard_reg = psl_lower_guard(lu, e->guard);

         vcode_block_t enter_bb = emit_block();
         vcode_block_t skip_bb = emit_block();
         emit_cond(guard_reg, enter_bb, skip_bb);

         vcode_select_block(enter_bb);
         psl_enter_state(lu, e->dest, copy);
         emit_jump(skip_bb);

         vcode_select_block(skip_bb);

         taken_reg = emit_or(taken_reg, guard_reg);
      }
      else {
         psl_enter_state(lu, e->dest, copy);
         taken_reg = vtrue;
      }
   }

   if (fsm->kind != FSM_COVER && fsm->kind != FSM_NEVER) {
      vcode_reg_t locus = psl_debug_locus(state->where);
      psl_lower_assert(lu, taken_reg, locus, fsm->src);
   }

   emit_return(VCODE_INVALID_REG);
}

static psl_node_t psl_outer_async_abort(psl_node_t p)
{
   switch (psl_kind(p)) {
   case P_ABORT:
      if (psl_subkind(p) == PSL_ABORT_ASYNC)
         return psl_operand(p, 1);
      else
         return NULL;

   case P_ALWAYS:
   case P_NEVER:
   case P_ASSUME:
   case P_COVER:
   case P_CLOCKED:
      return psl_outer_async_abort(psl_value(p));

   default:
      return NULL;
   }
}

static vcode_reg_t psl_lower_async_abort(unit_registry_t *ur,
                                         lower_unit_t *parent,
                                         psl_node_t hdl_expr,
                                         ident_t name_prefix)
{
   vcode_state_t state;
   vcode_state_save(&state);

   vcode_unit_t context = parent->vunit;
   vcode_select_unit(context);

   ident_t prefix = vcode_unit_name(context);
   ident_t name = ident_prefix(name_prefix, ident_new("async_abort"), '$');

   vcode_unit_t vu = emit_function(name, psl_to_object(hdl_expr), context);
   vcode_set_result(vtype_bool());

   vcode_type_t vcontext = vtype_context(prefix);
   emit_param(vcontext, VCODE_INVALID_STAMP, ident_new("context"));

   lower_unit_t *lu = lower_unit_new(ur, parent, vu, NULL, NULL);
   unit_registry_put(ur, lu);

   vcode_reg_t result_reg = psl_lower_boolean(lu, hdl_expr);
   emit_return(result_reg);

   unit_registry_finalise(ur, lu);

   vcode_state_restore(&state);

   vcode_reg_t context_reg = emit_context_upref(1);
   vcode_reg_t args[] = { context_reg };
   return emit_function_trigger(name, args, ARRAY_LEN(args));
}

static psl_ctx_t *psl_get_ctx(lower_unit_t *lu)
{
   return hash_get(lu->objects, &psl_ctx_key);
}

static psl_node_t psl_clock_operand(psl_node_t p)
{
   const int nops = psl_operands(p);
   if (nops > 0 && psl_kind(psl_operand(p, nops - 1)) == P_CLOCK_DECL)
      return psl_operand(p, nops - 1);
   else
      return NULL;
}

static vcode_reg_t psl_lower_clock_edge(lower_unit_t *lu, psl_node_t p);

static vcode_reg_t psl_lower_clock_now(lower_unit_t *lu, psl_node_t clk)
{
   tree_t expr = psl_tree(clk);
   if (tree_kind(expr) == T_PSL_FCALL)
      return psl_lower_clock_edge(lu, tree_psl(expr));
   else
      return lower_rvalue(lu, expr);
}

static vcode_reg_t psl_lower_prev_shift_reg(lower_unit_t *lu, tree_t expr,
                                            int cycles, psl_node_t clock)
{
   vcode_state_t state;
   vcode_state_save(&state);

   psl_ctx_t *ctx = psl_get_ctx(lu);

   vcode_select_block(ctx != NULL ? ctx->prev_tail : PSL_BLOCK_PREV);

   type_t type = tree_type(expr);

   vcode_type_t vtype = lower_type(type);
   vcode_type_t vbounds = lower_bounds(type);

   vcode_var_t *vars LOCAL = xmalloc_array(cycles, sizeof(vcode_var_t));
   for (int i = 0; i < cycles; i++)
      vars[i] = emit_var(vtype, vbounds, ident_uniq("prev"), 0);

   vcode_reg_t cur_reg = lower_rvalue(lu, expr);
   vcode_reg_t count_reg = VCODE_INVALID_REG;

   const bool is_array = type_is_array(type);
   if (is_array) {
      int64_t length;
      if (!folded_length(range_of(type, 0), &length))
         fatal_at(tree_loc(expr), "sorry, only constant length arrays "
                  "are supported");

      count_reg = emit_const(vtype_offset(), length);
   }

   const bool is_record = type_is_record(type);
   vcode_reg_t locus_reg = VCODE_INVALID_REG;
   if (is_record)
      locus_reg = emit_debug_locus(tree_to_object(expr));

   // The shift register only advances on the ticks of its clock
   if (clock == NULL && ctx != NULL && ctx->multiclock)
      clock = ctx->main_clock;

   vcode_block_t join_bb = VCODE_INVALID_BLOCK;
   if (clock != NULL) {
      vcode_reg_t tick_reg = psl_lower_clock_now(lu, clock);

      vcode_block_t shift_bb = emit_block();
      join_bb = emit_block();
      emit_cond(tick_reg, shift_bb, join_bb);

      vcode_select_block(shift_bb);
   }

   for (int i = 0; i < cycles - 1; i++) {
      if (is_array) {
         vcode_reg_t src_ptr = emit_index(vars[i + 1], VCODE_INVALID_REG);
         vcode_reg_t dst_ptr = emit_index(vars[i], VCODE_INVALID_REG);
         emit_copy(dst_ptr, src_ptr, count_reg);
      }
      else if (is_record) {
         vcode_reg_t src_ptr = emit_index(vars[i + 1], VCODE_INVALID_REG);
         vcode_reg_t dst_ptr = emit_index(vars[i], VCODE_INVALID_REG);
         lower_copy_record(lu, type, dst_ptr, src_ptr, locus_reg);
      }
      else {
         vcode_reg_t tmp = emit_load(vars[i + 1]);
         emit_store(tmp, vars[i]);
      }
   }

   if (is_array) {
      vcode_reg_t dst_ptr = emit_index(vars[cycles - 1], VCODE_INVALID_REG);
      emit_copy(dst_ptr, cur_reg, count_reg);
   }
   else if (is_record) {
      vcode_reg_t dst_ptr = emit_index(vars[cycles - 1], VCODE_INVALID_REG);
      lower_copy_record(lu, type, dst_ptr, cur_reg, locus_reg);
   }
   else
      emit_store(cur_reg, vars[cycles - 1]);

   if (join_bb != VCODE_INVALID_BLOCK) {
      emit_jump(join_bb);
      vcode_select_block(join_bb);
   }

   if (ctx != NULL)
      ctx->prev_tail = vcode_active_block();

   vcode_state_restore(&state);

   if (is_array || is_record)
      return emit_index(vars[0], VCODE_INVALID_REG);
   else
      return emit_load(vars[0]);
}

static vcode_reg_t psl_lower_edge(lower_unit_t *lu, psl_node_t p, bool rising)
{
   tree_t expr = psl_tree(psl_operand(p, 0));
   type_t type = tree_type(expr);

   vcode_reg_t prev_reg =
      psl_lower_prev_shift_reg(lu, expr, 1, psl_clock_operand(p));
   vcode_reg_t cur_reg = lower_rvalue(lu, expr);

   const bool is_bit = type_eq(type, std_type(NULL, STD_BIT));
   const int64_t one = is_bit ? 1 : 3;
   const int64_t zero = is_bit ? 0 : 2;

   vcode_type_t vtype = vcode_reg_type(cur_reg);
   vcode_reg_t from_reg = emit_const(vtype, rising ? zero : one);
   vcode_reg_t to_reg = emit_const(vtype, rising ? one : zero);

   vcode_reg_t was_reg = emit_cmp(VCODE_CMP_EQ, prev_reg, from_reg);
   vcode_reg_t now_reg = emit_cmp(VCODE_CMP_EQ, cur_reg, to_reg);
   return emit_and(was_reg, now_reg);
}

static vcode_reg_t psl_lower_ended(lower_unit_t *lu, psl_node_t p)
{
   // The sequence is run as a non-deterministic automaton whose active
   // states are kept in variables updated on each tick of the clock

   psl_node_t clock = psl_clock_operand(p);
   psl_ctx_t *ctx = psl_get_ctx(lu);

   psl_fsm_t *fsm = psl_fsm_new_sequence(psl_operand(p, 0), ident_uniq("ended"));

   vcode_type_t vbool = vtype_bool();
   vcode_reg_t vfalse = emit_const(vbool, 0);
   vcode_reg_t vtrue = emit_const(vbool, 1);

   const int nstates = fsm->next_id;
   vcode_var_t *vars LOCAL = xmalloc_array(nstates, sizeof(vcode_var_t));
   for (int i = 0; i < nstates; i++)
      vars[i] = emit_var(vbool, VCODE_INVALID_STAMP, ident_uniq("ended"), 0);

   vcode_reg_t result_reg = vfalse;
   for (fsm_state_t *s = fsm->states; s; s = s->next) {
      if (!s->accept)
         continue;

      vcode_reg_t active_reg = s->initial ? vtrue : emit_load(vars[s->id]);
      if (s->guard != NULL)
         active_reg = emit_and(active_reg, psl_lower_guard(lu, s->guard));

      result_reg = emit_or(result_reg, active_reg);
   }

   if (clock == NULL && ctx != NULL && ctx->multiclock)
      clock = ctx->main_clock;

   if (clock != NULL)
      result_reg = emit_and(result_reg, psl_lower_clock_now(lu, clock));

   vcode_state_t state;
   vcode_state_save(&state);

   vcode_select_block(ctx != NULL ? ctx->prev_tail : PSL_BLOCK_PREV);

   vcode_block_t join_bb = VCODE_INVALID_BLOCK;
   if (clock != NULL) {
      vcode_reg_t tick_reg = psl_lower_clock_now(lu, clock);

      vcode_block_t update_bb = emit_block();
      join_bb = emit_block();
      emit_cond(tick_reg, update_bb, join_bb);

      vcode_select_block(update_bb);
   }

   // The constants above are not defined in this block
   vfalse = emit_const(vbool, 0);
   vtrue = emit_const(vbool, 1);

   vcode_reg_t *next_regs LOCAL = xmalloc_array(nstates, sizeof(vcode_reg_t));
   for (int i = 0; i < nstates; i++)
      next_regs[i] = vfalse;

   for (fsm_state_t *s = fsm->states; s; s = s->next) {
      vcode_reg_t active_reg = s->initial ? vtrue : emit_load(vars[s->id]);

      for (fsm_edge_t *e = s->edges; e; e = e->next) {
         vcode_reg_t take_reg = active_reg;
         if (e->guard != NULL)
            take_reg = emit_and(take_reg, psl_lower_guard(lu, e->guard));

         next_regs[e->dest->id] = emit_or(next_regs[e->dest->id], take_reg);
      }
   }

   for (int i = 0; i < nstates; i++)
      emit_store(next_regs[i], vars[i]);

   if (join_bb != VCODE_INVALID_BLOCK) {
      emit_jump(join_bb);
      vcode_select_block(join_bb);
   }

   if (ctx != NULL)
      ctx->prev_tail = vcode_active_block();

   vcode_state_restore(&state);

   psl_fsm_free(fsm);
   return result_reg;
}

static vcode_reg_t psl_lower_nondet_value(lower_unit_t *lu, psl_node_t vs)
{
   if (psl_subkind(vs) == PSL_VALUE_SET_BOOLEAN)
      return lower_get_rand_bool();

   int64_t candidates = 0;
   int n_ops = psl_operands(vs);

   for (int i = 0; i < n_ops; i++) {
      psl_node_t op = psl_operand(vs, i);
      if (psl_kind(op) == P_RANGE) {
         const int64_t lhs = assume_int(psl_tree(psl_left(op)));
         const int64_t rhs = assume_int(psl_tree(psl_right(op)));
         candidates += rhs - lhs + 1;
      }
      else
         candidates++;
   }

   vcode_type_t vint = vtype_int(INT64_MIN, INT64_MAX);

   vcode_reg_t rnd_raw = lower_get_rand_int();
   vcode_reg_t rnd_64bit = emit_cast(vint, VCODE_INVALID_STAMP, rnd_raw);
   vcode_reg_t mod = emit_const(vint, candidates);
   vcode_reg_t rnd = emit_mod(rnd_64bit, mod);

   vcode_block_t test_bb[n_ops];
   vcode_block_t ret_bb[n_ops];
   vcode_block_t exit_bb;

   for (int i = 0; i < n_ops; i++) {
      test_bb[i] = emit_block();
      ret_bb[i] = emit_block();
   }

   exit_bb = emit_block();
   emit_jump(test_bb[0]);

   vcode_var_t rvar = emit_var(vint, VCODE_INVALID_STAMP,
                               ident_new("RV"), VAR_TEMP);
   int accum = 0;

   for (int i = 0; i < n_ops; i++) {
      psl_node_t op = psl_operand(vs, i);

      vcode_select_block(test_bb[i]);
      vcode_block_t next_bb =
         (i < (n_ops - 1)) ? test_bb[i + 1] : exit_bb;

      vcode_reg_t vaccum = emit_const(vint, accum);

      if (psl_kind(op) == P_RANGE) {
         const int64_t lhs = assume_int(psl_tree(psl_left(op)));
         const int64_t rhs = assume_int(psl_tree(psl_right(op)));
         int64_t r = rhs - lhs;

         vcode_reg_t ge = emit_cmp(VCODE_CMP_GEQ, rnd, vaccum);
         vcode_reg_t le =
            emit_cmp(VCODE_CMP_LEQ, rnd, emit_const(vint, accum + r));
         vcode_reg_t sel = emit_and(ge, le);

         emit_cond(sel, ret_bb[i], next_bb);

         vcode_select_block(ret_bb[i]);
         vcode_reg_t low = emit_sub(rnd, vaccum);
         vcode_reg_t rv = emit_add(low, emit_const(vint, lhs));
         emit_store(rv, rvar);
         emit_jump(exit_bb);

         accum += rhs - lhs + 1;
      }
      else {
         vcode_reg_t sel = emit_cmp(VCODE_CMP_EQ, vaccum, rnd);
         emit_cond(sel, ret_bb[i], next_bb);

         vcode_select_block(ret_bb[i]);

         emit_store(emit_const(vint, assume_int(psl_tree(op))), rvar);
         emit_jump(exit_bb);

         accum++;
      }
   }

   vcode_select_block(exit_bb);
   return emit_load(rvar);
}

static vcode_reg_t psl_lower_nondet_vector(lower_unit_t *lu, psl_node_t p)
{
   const int64_t width = assume_int(psl_tree(psl_operand(p, 0)));
   psl_node_t vs = psl_operand(p, 1);

   vcode_type_t vint = vtype_int(INT64_MIN, INT64_MAX);
   vcode_reg_t value_reg = psl_lower_nondet_value(lu, vs);
   if (psl_subkind(vs) == PSL_VALUE_SET_BOOLEAN)
      value_reg = emit_cast(vint, VCODE_INVALID_STAMP, value_reg);

   type_t bit = std_type(NULL, STD_BIT);
   vcode_type_t velem = lower_type(bit);
   vcode_type_t varray = vtype_carray(MAX(width, 1), velem);
   vcode_var_t var = emit_var(varray, VCODE_INVALID_STAMP, ident_uniq("nondet"),
                               VAR_TEMP);

   vcode_reg_t base_reg = emit_index(var, VCODE_INVALID_REG);
   vcode_reg_t two_reg = emit_const(vint, 2);

   vcode_reg_t rem_reg = value_reg;
   for (int64_t i = width - 1; i >= 0; i--) {
      vcode_reg_t bit_reg = emit_cast(velem, VCODE_INVALID_STAMP,
                                      emit_mod(rem_reg, two_reg));
      vcode_reg_t ptr_reg =
         emit_array_ref(base_reg, emit_const(vtype_offset(), i));
      emit_store_indirect(bit_reg, ptr_reg);
      rem_reg = emit_div(rem_reg, two_reg);
   }

   vcode_dim_t dim0 = {
      .left  = emit_const(vtype_offset(), 0),
      .right = emit_const(vtype_offset(), width - 1),
      .dir   = emit_const(vtype_bool(), RANGE_TO)
   };
   return emit_wrap(base_reg, &dim0, 1);
}

vcode_reg_t psl_lower_fcall(lower_unit_t *lu, psl_node_t p)
{
   assert(psl_kind(p) == P_BUILTIN_FCALL);

   switch (psl_subkind(p)) {
   case PSL_BUILTIN_PREV:
      {
         int num = 1;
         if (psl_operands(p) > 1 && psl_kind(psl_operand(p, 1)) == P_HDL_EXPR)
            num = assume_int(psl_tree(psl_operand(p, 1)));

         if (num > 512)
            fatal_at(psl_loc(p), "sorry, no more than 512 cycles "
                     "are supported");

         tree_t expr = psl_tree(psl_operand(p, 0));
         return psl_lower_prev_shift_reg(lu, expr, num, psl_clock_operand(p));
      }

   case PSL_BUILTIN_ROSE:
      return psl_lower_edge(lu, p, true);

   case PSL_BUILTIN_FELL:
      return psl_lower_edge(lu, p, false);

   case PSL_BUILTIN_NONDET:
      {
         assert(psl_operands(p) == 1);
         psl_node_t vs = psl_operand(p, 0);
         vcode_reg_t value_reg = psl_lower_nondet_value(lu, vs);

         if (psl_subkind(vs) == PSL_VALUE_SET_BOOLEAN)
            return value_reg;

         psl_node_t first = psl_operand(vs, 0);
         if (psl_kind(first) == P_RANGE)
            first = psl_left(first);

         type_t type = tree_type(psl_tree(first));
         return emit_cast(lower_type(type), lower_bounds(type), value_reg);
      }

   case PSL_BUILTIN_NONDET_VECTOR:
      return psl_lower_nondet_vector(lu, p);

   case PSL_BUILTIN_ENDED:
      return psl_lower_ended(lu, p);

   default:
      fatal_at(psl_loc(p), "sorry, this built-in function is not supported");
   }
}

typedef struct {
   clock_list_t *list;
   psl_node_t    top;
} collect_clocks_t;

static void psl_collect_fcall_clocks_cb(psl_node_t p, void *ctx)
{
   collect_clocks_t *cc = ctx;

   const int nops = psl_operands(p);
   for (int i = 0; i < nops; i++) {
      psl_node_t op = psl_operand(p, i);
      if (psl_kind(op) == P_CLOCK_DECL)
         APUSH(*cc->list, op);
   }
}

static void psl_collect_clocked_cb(psl_node_t p, void *ctx)
{
   collect_clocks_t *cc = ctx;

   if (p != cc->top)
      APUSH(*cc->list, p);
}

static void psl_collect_clocks(psl_node_t p, void *list)
{
   // Clocks other than the one for the whole property

   collect_clocks_t cc = { list, NULL };

   if (psl_kind(p) == P_COVER || psl_kind(p) == P_ASSERT
       || psl_kind(p) == P_ASSUME || psl_kind(p) == P_RESTRICT) {
      psl_node_t top = psl_value(p);
      if (psl_kind(top) == P_CLOCKED)
         cc.top = top;
   }

   psl_visit_only(p, psl_collect_fcall_clocks_cb, &cc, P_BUILTIN_FCALL);
   psl_visit_only(p, psl_collect_clocked_cb, &cc, P_CLOCKED);
}

static void psl_lower_property(lower_unit_t *lu, object_t *obj)
{
   tree_t wrapper = tree_from_object(obj);
   assert(tree_kind(wrapper) == T_PSL_DIRECT);

   psl_node_t p = tree_psl(wrapper);
   ident_t label = tree_ident(wrapper);

   psl_fsm_t *fsm = psl_fsm_new(p, label);

   vcode_unit_t context = vcode_unit_context(lu->vunit);

   ident_t prefix = vcode_unit_name(context);
   ident_t name = vcode_unit_name(lu->vunit);

   lu->cscope = cover_get_child(lu->cover, lu->parent->cscope,
                                vhdl_scope_name(lu->container, 0));

   vcode_type_t vcontext = vtype_context(prefix);
   emit_param(vcontext, VCODE_INVALID_STAMP, ident_new("context"));

   vcode_type_t vint32 = vtype_int(INT32_MIN, INT32_MAX);
   vcode_stamp_t vstamp = vstamp_int(0, INT32_MAX);
   vcode_reg_t state_reg = emit_param(vint32, vstamp, ident_new("state"));

   vcode_block_t case_bb = emit_block();
   vcode_block_t abort_bb = emit_block();
   vcode_block_t prev_bb = emit_block();

   assert(case_bb == PSL_BLOCK_CASE);
   assert(abort_bb == PSL_BLOCK_ABORT);
   assert(prev_bb == PSL_BLOCK_PREV);

   // Only handle a single clock for the whole property
   psl_node_t top = psl_value(p);
   assert(psl_kind(top) == P_CLOCKED);
   psl_node_t clk = psl_has_ref(top) ? psl_ref(top) : top;
   tree_t clk_expr = psl_tree(clk);

   int hops;
   vcode_var_t trigger_var = lower_search_vcode_obj(clk, lu, &hops);
   assert(trigger_var != VCODE_INVALID_VAR);

   SCOPED_A(psl_node_t) clocks = AINIT;
   psl_collect_clocks(p, &clocks);

   psl_ctx_t ctx = {
      .main_clock = clk,
      .multiclock = clocks.count > 0,
      .prev_tail  = prev_bb,
      .fsm        = fsm,
   };
   hash_put(lu->objects, &psl_ctx_key, &ctx);

   if (fsm->locals.count > 0) {
      // Each evaluation in progress needs its own copy of the local
      // variables (IEEE 1850-2010 section 6.3)
      ctx.copies = psl_fsm_max_attempts(fsm) ?: 16;

      if (ctx.copies > 64)
         fatal_at(psl_loc(p), "sorry, too many evaluations of this PSL "
                  "property with local variables can be in progress");

      ctx.locals = xmalloc_array(fsm->locals.count * ctx.copies,
                                 sizeof(vcode_var_t));
      for (int i = 0; i < fsm->locals.count; i++) {
         tree_t decl = fsm->locals.items[i];
         type_t type = tree_type(decl);
         vcode_type_t vtype = lower_type(type);
         vcode_stamp_t vstamp = lower_bounds(type);

         for (unsigned c = 0; c < ctx.copies; c++) {
            ident_t name = ident_sprintf("%s$%u", istr(tree_ident(decl)), c);
            ctx.locals[i * ctx.copies + c] = emit_var(vtype, vstamp, name, 0);
         }
      }

      vcode_type_t vbool = vtype_bool();
      ctx.busy = xmalloc_array(ctx.copies, sizeof(vcode_var_t));
      ctx.busy_next = xmalloc_array(ctx.copies, sizeof(vcode_var_t));
      for (unsigned c = 0; c < ctx.copies; c++) {
         ctx.busy[c] = emit_var(vbool, VCODE_INVALID_STAMP,
                                ident_sprintf("busy$%u", c), 0);
         ctx.busy_next[c] = emit_var(vbool, VCODE_INVALID_STAMP,
                                     ident_sprintf("busy_next$%u", c), 0);
      }
   }

   emit_comment("Reset property");

   build_wait(clk_expr, psl_wait_cb, lu);

   vcode_reg_t trigger_ptr = emit_var_upref(hops, trigger_var);
   vcode_reg_t trigger_reg = emit_load_indirect(trigger_ptr);

   for (int i = 0; i < clocks.count; i++) {
      int extra_hops;
      vcode_var_t var = lower_search_vcode_obj(clocks.items[i], lu,
                                               &extra_hops);
      assert(var != VCODE_INVALID_VAR);

      build_wait(psl_tree(clocks.items[i]), psl_wait_cb, lu);

      vcode_reg_t ptr_reg = emit_var_upref(extra_hops, var);
      trigger_reg = emit_or_trigger(trigger_reg, emit_load_indirect(ptr_reg));
   }

   // Only handle async_abort at the top level
   psl_node_t async_abort = psl_outer_async_abort(top);
   if (async_abort != NULL) {
      build_wait(psl_tree(async_abort), psl_wait_cb, lu);

      vcode_reg_t abort_reg =
         psl_lower_async_abort(lu->registry, lu->parent, async_abort, name);
      trigger_reg = emit_or_trigger(trigger_reg, abort_reg);
   }

   emit_add_trigger(trigger_reg);
   emit_jump(prev_bb);

   vcode_select_block(case_bb);

   const unsigned ncopies = MAX(ctx.copies, 1);
   const unsigned nstates = fsm->next_id * ncopies;

   vcode_block_t *state_bb LOCAL =
      xmalloc_array(nstates + 1, sizeof(vcode_block_t));
   vcode_reg_t *state_ids LOCAL =
      xmalloc_array(nstates + 1, sizeof(vcode_reg_t));

   for (int i = 0; i < nstates; i++) {
      state_bb[i] = emit_block();
      state_ids[i] = emit_const(vint32, i);
   }

   state_bb[nstates] = prev_bb;
   state_ids[nstates] = emit_const(vint32, nstates);

   bool strong = false;
   for (unsigned c = 0; c < ncopies; c++) {
      for (fsm_state_t *s = fsm->states; s; s = s->next) {
         vcode_select_block(state_bb[s->id + c * fsm->next_id]);
         if (s->initial && c > 0)
            emit_unreachable(VCODE_INVALID_REG);
         else
            psl_lower_state(lu, fsm, s, c);
         strong |= s->strong;
      }
   }

   vcode_select_block(abort_bb);

   if (strong) {
      vcode_reg_t false_reg = emit_const(vtype_bool(), 0);
      vcode_reg_t locus = psl_debug_locus(fsm->src);

      psl_lower_assert(lu, false_reg, locus, fsm->src);

      emit_return(VCODE_INVALID_REG);
   }
   else
      emit_unreachable(VCODE_INVALID_REG);

   vcode_select_block(ctx.prev_tail);

   for (unsigned c = 0; c < ctx.copies; c++) {
      // The evaluations in progress are those that entered a state
      // during the last cycle
      emit_store(emit_load(ctx.busy_next[c]), ctx.busy[c]);
      emit_store(emit_const(vtype_bool(), 0), ctx.busy_next[c]);
   }

   vcode_select_block(prev_bb);

   const bool has_prev = vcode_count_ops() > 0 || ctx.copies > 0;

   vcode_select_block(ctx.prev_tail);
   emit_return(emit_const(vint32, nstates + 1));

   vcode_select_block(case_bb);

   if (has_prev)
      emit_enter_state(emit_const(vint32, nstates), VCODE_INVALID_REG);

   emit_case(state_reg, abort_bb, state_ids, state_bb, nstates + 1);

   for (int i = 0; i < fsm->locals.count; i++)
      lower_put_vcode_obj(fsm->locals.items[i], VCODE_INVALID_VAR, lu);

   free(ctx.locals);
   free(ctx.busy);
   free(ctx.busy_next);

   psl_fsm_free(fsm);
}

static vcode_reg_t psl_lower_clock_edge(lower_unit_t *lu, psl_node_t p)
{
   // The clock function is evaluated whenever its signals have an event
   // so a rising or falling edge is an event on the signal with the
   // appropriate new value

   const psl_builtin_kind_t kind = psl_subkind(p);
   if (kind != PSL_BUILTIN_ROSE && kind != PSL_BUILTIN_FELL)
      fatal_at(psl_loc(p), "sorry, this built-in function cannot be used "
               "as a clock expression");

   tree_t expr = psl_tree(psl_operand(p, 0));
   type_t type = tree_type(expr);

   vcode_reg_t nets_reg = lower_lvalue(lu, expr);
   vcode_reg_t count_reg = emit_const(vtype_offset(), 1);
   vcode_reg_t event_reg = emit_event_flag(nets_reg, count_reg);

   const bool is_bit = type_eq(type, std_type(NULL, STD_BIT));
   const int64_t one = is_bit ? 1 : 3;
   const int64_t zero = is_bit ? 0 : 2;

   vcode_reg_t value_reg = lower_rvalue(lu, expr);
   vcode_reg_t level_reg = emit_const(vcode_reg_type(value_reg),
                                      kind == PSL_BUILTIN_ROSE ? one : zero);
   vcode_reg_t test_reg = emit_cmp(VCODE_CMP_EQ, value_reg, level_reg);

   return emit_and(event_reg, test_reg);
}

static void psl_lower_clock_func(lower_unit_t *lu, object_t *obj)
{
   psl_node_t p = psl_from_object(obj);
   assert(psl_kind(p) == P_CLOCK_DECL || psl_kind(p) == P_CLOCKED);

   vcode_unit_t context = vcode_unit_context(lu->vunit);
   ident_t prefix = vcode_unit_name(context);

   vcode_type_t vcontext = vtype_context(prefix);
   emit_param(vcontext, VCODE_INVALID_STAMP, ident_new("context"));

   vcode_set_result(vtype_bool());

   tree_t expr = psl_tree(p);

   vcode_reg_t clk_reg;
   if (tree_kind(expr) == T_PSL_FCALL)
      clk_reg = psl_lower_clock_edge(lu, tree_psl(expr));
   else
      clk_reg = lower_rvalue(lu, expr);

   emit_return(clk_reg);
}

static void psl_lower_clock_decl(unit_registry_t *ur, lower_unit_t *parent,
                                 psl_node_t p, ident_t label)
{
   vcode_unit_t context = parent->vunit;

   ident_t prefix = vcode_unit_name(context);
   ident_t name = ident_prefix(prefix, label, '.');

   // Each verification unit bound to a design entity may have its own
   // default clock declaration
   for (int n = 2; unit_registry_query(ur, name); n++)
      name = ident_sprintf("%s.%s%d", istr(prefix), istr(label), n);

   unit_registry_defer(ur, name, parent, emit_function, psl_lower_clock_func,
                       NULL, psl_to_object(p));

   vcode_type_t vtrigger = vtype_trigger();
   vcode_var_t var = emit_var(vtrigger, VCODE_INVALID_STAMP, label, 0);

   vcode_reg_t context_reg = emit_context_upref(0);
   vcode_reg_t args[] = { context_reg };
   vcode_reg_t trigger_reg = emit_function_trigger(name, args, ARRAY_LEN(args));
   emit_store(trigger_reg, var);

   lower_put_vcode_obj(p, var, parent);
}

void psl_lower_directive(unit_registry_t *ur, lower_unit_t *parent,
                         cover_data_t *cover, tree_t wrapper, ident_t name)
{
   assert(tree_kind(wrapper) == T_PSL_DIRECT);

   psl_node_t decl = tree_psl(wrapper);
   switch (psl_kind(decl)) {
   case P_FAIRNESS:
   case P_RESTRICT:
      return;   // Constraints for formal tools do not affect simulation
   default:
      break;
   }

   psl_node_t top = psl_value(decl);
   assert(psl_kind(top) == P_CLOCKED);

   if (!psl_has_ref(top)) {
      // Inline clock
      ident_t label = ident_sprintf("%s$clock", istr(tree_ident(wrapper)));
      psl_lower_clock_decl(ur, parent, top, label);
   }

   SCOPED_A(psl_node_t) clocks = AINIT;
   psl_collect_clocks(tree_psl(wrapper), &clocks);

   for (int i = 0; i < clocks.count; i++) {
      ident_t label = ident_sprintf("%s$clock%d", istr(tree_ident(wrapper)), i);
      psl_lower_clock_decl(ur, parent, clocks.items[i], label);
   }

   unit_registry_defer(ur, name,
                       parent, emit_property, psl_lower_property,
                       cover, tree_to_object(wrapper));
}

void psl_lower_decl(unit_registry_t *ur, lower_unit_t *parent, psl_node_t p,
                    ident_t label)
{
   switch (psl_kind(p)) {
   case P_CLOCK_DECL:
      psl_lower_clock_decl(ur, parent, p, label);
      break;
   case P_PROPERTY_DECL:
   case P_SEQUENCE_DECL:
      break;   // Nothing to lower until instantiated
   default:
      fatal_at(psl_loc(p), "cannot lower PSL declaration kind %s",
               psl_kind_str(psl_kind(p)));
   }
}
