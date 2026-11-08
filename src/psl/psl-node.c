//
//  Copyright (C) 2022-2024  Nick Gasson
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
#include "object.h"
#include "tree.h"
#include "psl/psl-node.h"

#include <stdlib.h>

static const imask_t has_map[P_LAST_PSL_KIND] = {
   // P_ASSERT
   (I_VALUE | I_MESSAGE),

   // P_ASSUME
   (I_SUBKIND | I_VALUE | I_MESSAGE),

   // P_RESTRICT
   (I_SUBKIND | I_VALUE | I_MESSAGE),

   // P_FAIRNESS
   (I_PARAMS | I_MESSAGE | I_FLAGS),

   // P_COVER
   (I_VALUE | I_MESSAGE),

   // P_ALWAYS
   (I_VALUE),

   // P_HDL_EXPR
   (I_FOREIGN | I_CLASS),

   // P_PROPERTY_DECL
   (I_VALUE | I_IDENT | I_PORTS),

   // P_SEQUENCE_DECL
   (I_VALUE | I_IDENT | I_PORTS),

   // P_CLOCK_DECL
   (I_FOREIGN),

   // P_NEXT
   (I_VALUE | I_DELAY | I_FLAGS),

   // P_NEVER
   (I_VALUE),

   // P_EVENTUALLY
   (I_VALUE | I_FLAGS),

   // P_NEXT_A
   (I_VALUE | I_DELAY | I_FLAGS),

   // P_NEXT_E
   (I_VALUE | I_DELAY | I_FLAGS),

   // P_NEXT_EVENT
   (I_VALUE | I_DELAY | I_FLAGS | I_PARAMS),

   // P_SERE
   (I_SUBKIND | I_PARAMS | I_FLAGS),

   // P_REPEAT
   (I_SUBKIND | I_DELAY | I_VALUE),

   // P_PROPERTY_INST
   (I_REF | I_PARAMS),

   // P_SEQUENCE_INST
   (I_REF | I_PARAMS),

   // P_UNION
   (I_PARAMS),

   // P_BUILTIN_FCALL
   (I_SUBKIND | I_PARAMS),

   // P_VALUE_SET
   (I_SUBKIND | I_PARAMS),

   // P_PARAM
   (I_FOREIGN | I_VALUE),

   // P_UNTIL
   (I_PARAMS | I_FLAGS),

   // P_ABORT
   (I_PARAMS | I_SUBKIND),

   // P_BEFORE
   (I_PARAMS | I_FLAGS),

   // P_SUFFIX_IMPL
   (I_SUBKIND | I_PARAMS),

   // P_LOGICAL
   (I_SUBKIND | I_PARAMS),

   // P_RANGE
   (I_LEFT | I_RIGHT),

   // P_PROC_BLOCK
   (I_FOREIGN | I_VALUE),

   // P_PARAM_SERE
   (I_SUBKIND | I_PARAMS | I_VALUE),

   // P_CLOCKED
   (I_FOREIGN | I_VALUE | I_REF),

   // P_ENDPOINT_DECL
   (I_IDENT | I_VALUE),

   // P_NEXT_EVENT_A
   (I_VALUE | I_DELAY | I_FLAGS | I_PARAMS),

   // P_NEXT_EVENT_E
   (I_VALUE | I_DELAY | I_FLAGS | I_PARAMS),

   // P_PARAM_PROP
   (I_SUBKIND | I_PARAMS | I_VALUE),
};

static const char *kind_text_map[P_LAST_PSL_KIND] = {
   "P_ASSERT", "P_ASSUME", "P_RESTRICT", "P_FAIRNESS", "P_COVER", "P_ALWAYS",
   "P_HDL_EXPR", "P_PROPERTY_DECL", "P_SEQUENCE_DECL", "P_CLOCK_DECL", "P_NEXT",
   "P_NEVER", "P_EVENTUALLY", "P_NEXT_A", "P_NEXT_E", "P_NEXT_EVENT",
   "P_SERE", "P_REPEAT", "P_PROPERTY_INST", "P_SEQUENCE_INST", "P_UNION",
   "P_BUILTIN_FCALL", "P_VALUE_SET", "P_PARAM_DECL", "P_UNTIL", "P_ABORT",
   "P_BEFORE", "P_SUFFIX_IMPL", "P_LOGICAL", "P_RANGE", "P_PROC_BLOCK",
   "P_PARAM_SERE", "P_CLOCKED", "P_ENDPOINT_DECL", "P_NEXT_EVENT_A",
   "P_NEXT_EVENT_E", "P_PARAM_PROP",
};

static const change_allowed_t change_allowed[] = {
   { -1, -1 }
};

struct _psl_node {
   object_t object;
};

struct _tree {
   object_t object;
};

struct _type {
   object_t object;
};

object_class_t psl_object = {
   .name           = "psl",
   .change_allowed = change_allowed,
   .has_map        = has_map,
   .kind_text_map  = kind_text_map,
   .has_loc        = true,
   .tag            = OBJECT_TAG_PSL,
   .last_kind      = P_LAST_PSL_KIND
};

static inline psl_node_t psl_array_nth(item_t *item, unsigned n)
{
   object_t *o = obj_array_nth(item->obj_array, n);
   return container_of(o, struct _psl_node, object);
}

static inline void psl_array_add(item_t *item, psl_node_t p)
{
   obj_array_add(&(item->obj_array), &(p->object));
}

static inline tree_t tree_array_nth(item_t *item, unsigned n)
{
   object_t *o = obj_array_nth(item->obj_array, n);
   return container_of(o, struct _tree, object);
}

static inline void tree_array_add(item_t *item, tree_t t)
{
   obj_array_add(&(item->obj_array), &(t->object));
}

psl_node_t psl_new(psl_kind_t kind)
{
   object_t *o = object_new(NULL, &psl_object, kind);
   return container_of(o, struct _psl_node, object);
}

psl_kind_t psl_kind(psl_node_t p)
{
   return p->object.kind;
}

const char *psl_kind_str(psl_kind_t kind)
{
   return kind_text_map[kind];
}

const loc_t *psl_loc(psl_node_t p)
{
   assert(p != NULL);
   return &(p->object.loc);
}

void psl_set_loc(psl_node_t p, const loc_t *loc)
{
   assert(p != NULL);
   assert(loc != NULL);
   p->object.loc = *loc;
}

unsigned psl_subkind(psl_node_t p)
{
   return lookup_item(&psl_object, p, I_SUBKIND)->ival;
}

void psl_set_subkind(psl_node_t p, unsigned sub)
{
   lookup_item(&psl_object, p, I_SUBKIND)->ival = sub;
}

psl_type_t psl_type(psl_node_t p)
{
   return lookup_item(&psl_object, p, I_CLASS)->ival;
}

void psl_set_type(psl_node_t p, psl_type_t type)
{
   lookup_item(&psl_object, p, I_CLASS)->ival = type;
}

bool psl_has_tree(psl_node_t p)
{
   return lookup_item(&psl_object, p, I_FOREIGN)->object != NULL;
}

tree_t psl_tree(psl_node_t p)
{
   item_t *item = lookup_item(&psl_object, p, I_FOREIGN);
   assert(item->object != NULL);
   return container_of(item->object, struct _tree, object);
}

void psl_set_tree(psl_node_t p, tree_t t)
{
   assert(p != NULL);
   lookup_item(&psl_object, p, I_FOREIGN)->object = &(t->object);
   object_write_barrier(&(p->object), &(t->object));
}

psl_node_t psl_value(psl_node_t p)
{
   item_t *item = lookup_item(&psl_object, p, I_VALUE);
   assert(item->object != NULL);
   return container_of(item->object, struct _psl_node, object);
}

void psl_set_value(psl_node_t p, psl_node_t v)
{
   lookup_item(&psl_object, p, I_VALUE)->object = &(v->object);
   object_write_barrier(&(p->object), &(v->object));
}

psl_node_t psl_delay(psl_node_t p)
{
   item_t *item = lookup_item(&psl_object, p, I_DELAY);
   assert(item->object != NULL);
   return container_of(item->object, struct _psl_node, object);
}

void psl_set_delay(psl_node_t p, psl_node_t d)
{
   lookup_item(&psl_object, p, I_DELAY)->object = &(d->object);
   object_write_barrier(&(p->object), &(d->object));
}

bool psl_has_delay(psl_node_t p)
{
   return lookup_item(&psl_object, p, I_DELAY)->object != NULL;
}

unsigned psl_operands(psl_node_t p)
{
   item_t *item = lookup_item(&psl_object, p, I_PARAMS);
   return obj_array_count(item->obj_array);
}

psl_node_t psl_operand(psl_node_t p, unsigned n)
{
   item_t *item = lookup_item(&psl_object, p, I_PARAMS);
   return psl_array_nth(item, n);
}

void psl_add_operand(psl_node_t p, psl_node_t o)
{
   assert(o != NULL);
   psl_array_add(lookup_item(&psl_object, p, I_PARAMS), o);
   object_write_barrier(&(p->object), &(o->object));
}

tree_t psl_message(psl_node_t p)
{
   item_t *item = lookup_item(&psl_object, p, I_MESSAGE);
   assert(item->object != NULL);
   return container_of(item->object, struct _tree, object);
}

bool psl_has_message(psl_node_t p)
{
   return lookup_item(&psl_object, p, I_MESSAGE)->object != NULL;
}

void psl_set_message(psl_node_t p, tree_t m)
{
   lookup_item(&psl_object, p, I_MESSAGE)->object = &(m->object);
   object_write_barrier(&(p->object), &(m->object));
}

unsigned psl_ports(psl_node_t p)
{
   item_t *item = lookup_item(&psl_object, p, I_PORTS);
   return obj_array_count(item->obj_array);
}

tree_t psl_port(psl_node_t p, unsigned n)
{
   item_t *item = lookup_item(&psl_object, p, I_PORTS);
   return tree_array_nth(item, n);
}

void psl_add_port(psl_node_t p, tree_t o)
{
   assert(o != NULL);
   tree_array_add(lookup_item(&psl_object, p, I_PORTS), o);
   object_write_barrier(&(p->object), &(o->object));
}

ident_t psl_ident(psl_node_t p)
{
   item_t *item = lookup_item(&psl_object, p, I_IDENT);
   assert(item->ident != NULL);
   return item->ident;
}

bool psl_has_ident(psl_node_t p)
{
   return lookup_item(&psl_object, p, I_IDENT)->ident != NULL;
}

void psl_set_ident(psl_node_t p, ident_t i)
{
   lookup_item(&psl_object, p, I_IDENT)->ident = i;
}

void psl_set_ref(psl_node_t p, psl_node_t r)
{
   lookup_item(&psl_object, p, I_REF)->object = &(r->object);
   object_write_barrier(&(p->object), &(r->object));
}

bool psl_has_ref(psl_node_t p)
{
   return lookup_item(&psl_object, p, I_REF)->object != NULL;
}

psl_node_t psl_ref(psl_node_t p)
{
   item_t *item = lookup_item(&psl_object, p, I_REF);
   assert(item->object != NULL);
   return container_of(item->object, struct _psl_node, object);
}

psl_node_t psl_left(psl_node_t p)
{
   item_t *item = lookup_item(&psl_object, p, I_LEFT);
   assert(item->object != NULL);
   return container_of(item->object, struct _psl_node, object);
}

void psl_set_left(psl_node_t p, psl_node_t left)
{
   lookup_item(&psl_object, p, I_LEFT)->object = &(left->object);
   object_write_barrier(&(p->object), &(left->object));
}

psl_node_t psl_right(psl_node_t p)
{
   item_t *item = lookup_item(&psl_object, p, I_RIGHT);
   assert(item->object != NULL);
   return container_of(item->object, struct _psl_node, object);
}

void psl_set_right(psl_node_t p, psl_node_t right)
{
   lookup_item(&psl_object, p, I_RIGHT)->object = &(right->object);
   object_write_barrier(&(p->object), &(right->object));
}

psl_flags_t psl_flags(psl_node_t p)
{
   return lookup_item(&psl_object, p, I_FLAGS)->ival;
}

void psl_set_flag(psl_node_t p, psl_flags_t mask)
{
   lookup_item(&psl_object, p, I_FLAGS)->ival |= mask;
}

object_t *psl_to_object(psl_node_t p)
{
   return &(p->object);
}

psl_node_t psl_from_object(object_t *obj)
{
   assert(obj != NULL);
   if (obj->tag == OBJECT_TAG_PSL)
      return container_of(obj, struct _psl_node, object);
   else
      return NULL;
}

unsigned psl_visit_only(psl_node_t p, psl_visit_fn_t fn, void *context,
                        psl_kind_t kind)
{
   assert(p != NULL);

   object_visit_ctx_t ctx = {
      .count      = 0,
      .postorder  = (object_visit_fn_t)fn,
      .preorder   = NULL,
      .context    = context,
      .kind       = kind,
      .tag        = OBJECT_TAG_PSL,
      .generation = object_next_generation(),
      .deep       = false
   };

   object_visit(&(p->object), &ctx);

   return ctx.count;
}

#define PSL_PARAM_SHIFT 25

psl_type_t psl_param_type(tree_t param)
{
   assert(tree_kind(param) == T_PARAM_DECL);
   return (tree_flags(param) >> PSL_PARAM_SHIFT) & 0xf;
}

void psl_set_param_type(tree_t param, psl_type_t type)
{
   assert(tree_kind(param) == T_PARAM_DECL);
   tree_set_flag(param, (tree_flags_t)((unsigned)type << PSL_PARAM_SHIFT));
}

bool psl_has_value(psl_node_t p)
{
   return lookup_item(&psl_object, p, I_VALUE)->object != NULL;
}

typedef struct {
   psl_node_t decl;
   psl_node_t inst;
} subst_ctx_t;

static int subst_formal_index(tree_t t, subst_ctx_t *ctx)
{
   if (tree_kind(t) != T_REF || !tree_has_ref(t))
      return -1;

   tree_t ref = tree_ref(t);
   if (tree_kind(ref) != T_PARAM_DECL)
      return -1;

   const int nports = psl_ports(ctx->decl);
   for (int i = 0; i < nports; i++) {
      if (psl_port(ctx->decl, i) == ref)
         return i;
   }

   return -1;
}

static bool subst_copy_pred(object_t *object, void *__ctx)
{
   tree_t t = container_of(object, struct _tree, object);
   return subst_formal_index(t, __ctx) != -1;
}

static object_t *subst_tree_cb(object_t *object, void *__ctx)
{
   subst_ctx_t *ctx = __ctx;
   tree_t t = container_of(object, struct _tree, object);

   const int pos = subst_formal_index(t, ctx);
   if (pos == -1 || pos >= psl_operands(ctx->inst))
      return object;

   psl_node_t actual = psl_operand(ctx->inst, pos);
   if (psl_kind(actual) != P_HDL_EXPR || !psl_has_tree(actual))
      return object;

   switch (psl_param_type(psl_port(ctx->decl, pos))) {
   case PSL_TYPE_SEQUENCE:
   case PSL_TYPE_PROPERTY:
      return object;
   default:
      return &(psl_tree(actual)->object);
   }
}

static object_t *subst_psl_cb(object_t *object, void *__ctx)
{
   subst_ctx_t *ctx = __ctx;
   psl_node_t p = container_of(object, struct _psl_node, object);

   if (psl_kind(p) != P_HDL_EXPR || !psl_has_tree(p))
      return object;

   const int pos = subst_formal_index(psl_tree(p), ctx);
   if (pos == -1 || pos >= psl_operands(ctx->inst))
      return object;

   switch (psl_param_type(psl_port(ctx->decl, pos))) {
   case PSL_TYPE_SEQUENCE:
   case PSL_TYPE_PROPERTY:
      return &(psl_operand(ctx->inst, pos)->object);
   default:
      return object;
   }
}

psl_node_t psl_expand(psl_node_t decl, psl_node_t inst)
{
   // Replace each use of a formal parameter in the declaration with the
   // corresponding actual parameter of the instance

   psl_node_t body = psl_value(decl);

   subst_ctx_t sctx = { decl, inst };

   object_copy_ctx_t *cctx = xcalloc_flex(sizeof(object_copy_ctx_t), 1,
                                          sizeof(object_t *));
   cctx->generation = object_next_generation();
   cctx->pred_context = &sctx;
   cctx->nroots = 1;
   cctx->roots[0] = &(body->object);
   cctx->should_copy[OBJECT_TAG_TREE] = subst_copy_pred;

   object_copy_begin(cctx);
   object_copy_finish(cctx);

   object_t *copy = cctx->roots[0];
   free(cctx);

   if (copy == &(body->object))
      return body;   // No formal parameters used

   object_rewrite_ctx_t rctx = {
      .generation = object_next_generation(),
      .context    = &sctx,
      .arena      = object_arena(copy),
   };

   rctx.post_fn[OBJECT_TAG_TREE] = subst_tree_cb;
   rctx.post_fn[OBJECT_TAG_PSL] = subst_psl_cb;

   object_t *result = object_rewrite(copy, &rctx);
   free(rctx.cache);

   return container_of(result, struct _psl_node, object);
}
