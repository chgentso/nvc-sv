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
#include "jit/jit-priv.h"
#include "jit/jit.h"
#include "array.h"
#include "option.h"
#include "rt/crv.h"
#include "thread.h"

#include <assert.h>
#include <stdlib.h>

// Connects generated code to the solver.  Variables are found by the object
// that holds them and a key derived from their declaration.

typedef struct {
   int64_t object;
   int64_t key;
   int32_t id;
   bool    is_free;
   bool    is_array;
   int64_t size;
   unsigned width;
   bool    is_signed;
} crv_entry_t;

typedef struct {
   crv_array_t array;
   unsigned    width;
   bool        is_signed;
} crv_uarray_t;

typedef struct {
   crv_t             *solver;
   A(crv_entry_t)     entries;
   A(int64_t)         listed;
   A(crv_node_t)      unique;
   A(crv_uarray_t)    unique_arrays;
   bool               only_listed;
   bool               failed;
} crv_gen_t;

static crv_entry_t *crv_gen_find(crv_gen_t *g, int64_t object, int64_t key,
                                 bool is_array)
{
   for (int i = g->entries.count - 1; i >= 0; i--) {
      crv_entry_t *e = &(g->entries.items[i]);
      if (e->object == object && e->key == key && e->is_array == is_array)
         return e;
   }

   return NULL;
}

static inline unsigned crv_flags_width(int64_t flags)
{
   return flags & 0xffff;
}

static inline bool crv_flags_signed(int64_t flags)
{
   return !!(flags & 0x10000);
}

static __thread jit_anchor_t *crv_caller;

void crv_set_caller(void *anchor)
{
   crv_caller = anchor;
}

static bool crv_gen_caller_loc(loc_t *out, char *buf, size_t len)
{
   // The innermost frame with a source line is the code that contains the
   // randomize call.  Generated code does not pass the location down so
   // this walks the stack: only done for statistics and for a warning.
   jit_thread_local_t *thread = jit_thread_local();
   jit_anchor_t *saved = thread->anchor;
   thread->anchor = crv_caller;
   jit_stack_trace_t *stack LOCAL = jit_stack_trace();
   thread->anchor = saved;

   for (size_t i = 0; i < stack->count; i++) {
      const loc_t *loc = &(stack->frames[i].loc);
      if (loc_invalid_p(loc) || loc->first_line == 0)
         continue;

      if (buf != NULL)
         snprintf(buf, len, "%s:%d:%d", loc_file_str(loc), loc->first_line,
                  loc->first_column);
      *out = *loc;
      return true;
   }

   return false;
}

static void crv_gen_label_site(void)
{
   char buf[256];
   loc_t loc;
   crv_stats_site(crv_gen_caller_loc(&loc, buf, sizeof(buf)) ? buf : "?");
}

static void crv_gen_warn_giveup(void)
{
   loc_t loc;
   if (crv_gen_caller_loc(&loc, NULL, 0))
      warn_at(&loc, "randomize() gave up without finding a solution: "
              "the constraints may be satisfiable");
   else
      warnf("randomize() gave up without finding a solution: "
            "the constraints may be satisfiable");
}

int64_t crv_generated(crv_op_t op, const int64_t *args)
{
   // These do not have a solver as their first argument
   switch (op) {
   case CRV_OP_OBJECT_ID:
      return args[0];
   case CRV_OP_TEST_BIT:
      if (args[1] == -1)
         return args[0] != 0;
      else if (args[1] < 0 || args[1] >= 64)
         return 0;   // Not one of the properties with a rand_mode bit
      return ((uint64_t)args[0] >> args[1]) & 1;
   case CRV_OP_SET_BIT:
      {
         // Bits record what has been turned off
         const uint64_t mask = args[1] < 0 ? ~UINT64_C(0)
            : UINT64_C(1) << args[1];
         return args[2] ? (uint64_t)args[0] & ~mask : (uint64_t)args[0] | mask;
      }
   case CRV_OP_RANDOM:
      {
         static __thread uint64_t state = 0;
         if (state == 0)
            state = opt_verilog_seed() * UINT64_C(2) + 1;

         // Xorshift: only used to choose the arm of a randcase
         state ^= state << 13;
         state ^= state >> 7;
         state ^= state << 17;

         return args[0] > 0 ? (int64_t)(state % (uint64_t)args[0]) : -1;
      }
   case CRV_OP_SEED_BEGIN:
      crv_push_seed(args[0]);
      return 0;
   case CRV_OP_SEED_END:
      return (int64_t)crv_pop_seed();
   default:
      break;
   }

   crv_gen_t *g = (crv_gen_t *)(intptr_t)args[0];
   crv_t *c = g != NULL ? g->solver : NULL;

   switch (op) {
   case CRV_OP_BEGIN:
      {
         static __thread bool seeded = false;
         if (!seeded) {
            crv_seed(opt_verilog_seed());
            seeded = true;
         }

         crv_gen_t *n = xcalloc(sizeof(crv_gen_t));
         n->solver = crv_new();
         return (intptr_t)n;
      }
   case CRV_OP_END:
      crv_free(c);
      ACLEAR(g->entries);
      ACLEAR(g->listed);
      ACLEAR(g->unique);
      ACLEAR(g->unique_arrays);
      free(g);
      return 0;
   case CRV_OP_SOLVE:
      {
         if (g->failed)
            return 0;
         else if (crv_stats_enabled())
            crv_gen_label_site();

         const crv_result_t r = crv_solve2(c);
         if (r == CRV_GIVEUP)
            crv_gen_warn_giveup();
         return r == CRV_SAT;
      }
   case CRV_OP_FAIL:
      {
         loc_t loc = get_loc(args[2], args[3], args[2], args[3] + 1, args[1]);
         error_at(&loc, "null access dereference in constraint");
         g->failed = true;
         return 0;
      }
   case CRV_OP_ONLY_LISTED:
      g->only_listed = true;
      return 0;
   case CRV_OP_LIST:
      APUSH(g->listed, args[1]);
      APUSH(g->listed, args[2]);
      return 0;
   case CRV_OP_IS_FREE:
      if (g->only_listed) {
         for (int i = 0; i < g->listed.count; i += 2) {
            if (g->listed.items[i] == args[1]
                && g->listed.items[i + 1] == args[2])
               return 1;
         }

         return 0;
      }
      else if (!args[3])
         return 0;
      else if (args[5] < 0 || args[5] >= 64)
         return 1;
      else
         return !((args[4] >> args[5]) & 1);
   case CRV_OP_VAR:
      {
         const unsigned width = crv_flags_width(args[4]);
         const bool is_signed = crv_flags_signed(args[4]);
         crv_var_t v = crv_var(c, width, is_signed, args[3]);
         if (!args[5])
            crv_var_domain(c, v, args[3]);

         crv_entry_t e = {
            .object = args[1], .key = args[2], .id = v, .is_free = !!args[5],
            .width = width, .is_signed = is_signed
         };
         APUSH(g->entries, e);
         return v;
      }
   case CRV_OP_VAR_DOMAIN:
   case CRV_OP_VAR_RANDC:
      for (int i = 0; i < g->entries.count; i++) {
         const crv_entry_t *e = &(g->entries.items[i]);
         if (!e->is_array && e->id == args[1] && e->is_free) {
            if (op == CRV_OP_VAR_DOMAIN)
               crv_var_domain(c, args[1], args[2]);
            else
               crv_var_randc(c, args[1], (uint64_t *)(intptr_t)args[2]);
            break;
         }
      }
      return 0;
   case CRV_OP_ARRAY:
      {
         const unsigned width = crv_flags_width(args[3]);
         const bool is_signed = crv_flags_signed(args[3]);
         const bool is_dyn = !!(args[3] & CRV_FLAG_DYNAMIC);
         const bool is_free = !!args[5];
         crv_array_t a = crv_array(c, width, is_signed, args[4],
                                   is_dyn && is_free, is_free);
         if (args[1] != 0 || args[2] != 0) {
            crv_entry_t e = {
               .object = args[1], .key = args[2], .id = a, .is_free = is_free,
               .is_array = true, .size = args[4], .width = width,
               .is_signed = is_signed
            };
            APUSH(g->entries, e);
         }
         return a;
      }
   case CRV_OP_ARRAY_INIT:
      crv_array_init(c, args[1], args[2], args[3]);
      return 0;
   case CRV_OP_REF:
      {
         const crv_entry_t *e = crv_gen_find(g, args[1], args[2], false);
         if (e != NULL)
            return crv_var_ref(c, e->id);

         return crv_const(c, args[3], crv_flags_width(args[4]),
                          crv_flags_signed(args[4]));
      }
   case CRV_OP_FIND_ARRAY:
      {
         const crv_entry_t *e = crv_gen_find(g, args[1], args[2], true);
         return e != NULL ? e->id : -1;
      }
   case CRV_OP_FIND_FREE:
   case CRV_OP_FIND_FREE_ARRAY:
      {
         const crv_entry_t *e = crv_gen_find(g, args[1], args[2],
                                             op == CRV_OP_FIND_FREE_ARRAY);
         return e != NULL && e->is_free ? e->id : -1;
      }
   case CRV_OP_CONST:
      return crv_const(c, args[1], crv_flags_width(args[2]),
                       crv_flags_signed(args[2]));
   case CRV_OP_BINARY:
      return crv_binary(c, args[1], args[2], args[3], crv_flags_width(args[4]),
                        crv_flags_signed(args[4]));
   case CRV_OP_UNARY:
      return crv_unary(c, args[1], args[2]);
   case CRV_OP_CAST:
      return crv_cast(c, args[1], crv_flags_width(args[2]),
                      crv_flags_signed(args[2]));
   case CRV_OP_COND:
      return crv_cond(c, args[1], args[2], args[3]);
   case CRV_OP_ARRAY_SIZE:
      return crv_array_size(c, args[1]);
   case CRV_OP_ARRAY_SUM:
      return crv_array_sum(c, args[1], crv_flags_width(args[2]),
                           crv_flags_signed(args[2]));
   case CRV_OP_ARRAY_ELEM:
      return crv_array_elem(c, args[1], args[2]);
   case CRV_OP_SET_NEW:
      return crv_set_new(c);
   case CRV_OP_SET_VALUE:
      crv_set_value(c, args[1], args[2]);
      return 0;
   case CRV_OP_SET_RANGE:
      crv_set_range(c, args[1], args[2], args[3]);
      return 0;
   case CRV_OP_SET_ARRAY:
      {
         const unsigned len = crv_array_len(c, args[2]);
         for (unsigned i = 0; i < len; i++) {
            crv_node_t index = crv_const(c, i, 32, true);
            crv_set_value(c, args[1], crv_array_elem(c, args[2], index));
         }
         return 0;
      }
   case CRV_OP_INSIDE:
      return crv_inside(c, args[1], args[2]);
   case CRV_OP_DIST_NEW:
      return crv_dist_new(c);
   case CRV_OP_DIST_PER_VALUE:
   case CRV_OP_DIST_PER_RANGE:
      crv_dist_range(c, args[1], args[2], args[3], args[4],
                     op == CRV_OP_DIST_PER_VALUE);
      return 0;
   case CRV_OP_DIST:
      return crv_dist(c, args[1], args[2]);
   case CRV_OP_REQUIRE:
      crv_require(c, args[1], !!args[2], args[3]);
      return 0;
   case CRV_OP_DISABLE_SOFT:
      crv_disable_soft(c, args[1]);
      return 0;
   case CRV_OP_UNIQUE_PUSH:
      APUSH(g->unique, args[1]);
      return 0;
   case CRV_OP_UNIQUE_ARRAY:
      if (crv_array_resizable(c, args[1])) {
         // The size is only known when solving so compare the elements of
         // the final array once all the other values are known
         crv_uarray_t u = { args[1], args[2], !!args[3] };
         APUSH(g->unique_arrays, u);
         return 0;
      }
      else {
         const unsigned len = crv_array_len(c, args[1]);
         for (unsigned i = 0; i < len; i++) {
            crv_node_t index = crv_const(c, i, 32, true);
            crv_node_t elem = crv_array_elem(c, args[1], index);
            APUSH(g->unique, crv_cast(c, elem, args[2], !!args[3]));
         }
         return 0;
      }
   case CRV_OP_UNIQUE_END:
      for (unsigned a = 0; a < g->unique_arrays.count; a++) {
         const crv_uarray_t *u = &(g->unique_arrays.items[a]);

         // Elements of the array which differ from each other
         crv_node_t i = crv_index(c), j = crv_index(c);
         crv_foreach(c, u->array, i, crv_const(c, 1, 1, false), false);

         crv_node_t ei = crv_cast(c, crv_array_elem(c, u->array, i), u->width,
                                  u->is_signed);
         crv_node_t ej = crv_cast(c, crv_array_elem(c, u->array, j), u->width,
                                  u->is_signed);
         crv_node_t later = crv_binary(c, CRV_LE, j, i, 1, false);
         crv_node_t differ = crv_binary(c, CRV_NE, ei, ej, 1, false);
         crv_foreach(c, u->array, j,
                     crv_binary(c, CRV_LOG_OR, later, differ, 1, false), false);

         // and from the values which are not in an array of unknown size
         for (unsigned k = 0; k < g->unique.count; k++) {
            crv_node_t x = crv_index(c);
            crv_node_t ex = crv_cast(c, crv_array_elem(c, u->array, x),
                                     u->width, u->is_signed);
            crv_foreach(c, u->array, x,
                        crv_binary(c, CRV_NE, ex, g->unique.items[k], 1, false),
                        false);
         }

         // and from the elements of another array of unknown size
         for (unsigned b = a + 1; b < g->unique_arrays.count; b++) {
            const crv_uarray_t *w = &(g->unique_arrays.items[b]);

            crv_node_t x = crv_index(c), y = crv_index(c);
            crv_foreach(c, u->array, x, crv_const(c, 1, 1, false), false);

            crv_node_t ex = crv_cast(c, crv_array_elem(c, u->array, x),
                                     u->width, u->is_signed);
            crv_node_t ey = crv_cast(c, crv_array_elem(c, w->array, y),
                                     w->width, w->is_signed);
            crv_foreach(c, w->array, y,
                        crv_binary(c, CRV_NE, ex, ey, 1, false), false);
         }
      }

      if (g->unique.count > 1)
         crv_unique(c, g->unique.items, g->unique.count);
      ARESIZE(g->unique, 0);
      ARESIZE(g->unique_arrays, 0);
      return 0;
   case CRV_OP_INDEX:
      return crv_index(c);
   case CRV_OP_FOREACH:
      crv_foreach(c, args[1], args[2], args[3], !!args[4]);
      return 0;
   case CRV_OP_SOLVE_BEFORE:
      {
         const crv_entry_t *after = crv_gen_find(g, args[3], args[4], false);
         if (after == NULL)
            return 0;

         const crv_entry_t *before = crv_gen_find(g, args[1], args[2], false);
         if (before != NULL)
            crv_solve_before(c, before->id, after->id);
         else if ((before = crv_gen_find(g, args[1], args[2], true)) != NULL)
            crv_array_solve_before(c, before->id, after->id);
         return 0;
      }
   case CRV_OP_VALUE:
      return crv_value(c, args[1]);
   case CRV_OP_ARRAY_LEN:
      return crv_array_len(c, args[1]);
   case CRV_OP_ARRAY_VALUE:
      return crv_array_value(c, args[1], args[2]);
   default:
      fatal_trace("invalid constraint solver operation %d", op);
   }
}
