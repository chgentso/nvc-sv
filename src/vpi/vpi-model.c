//
//  Copyright (C) 2024  Nick Gasson
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
#include "jit/jit-ffi.h"
#include "jit/jit-priv.h"
#include "jit/jit.h"
#include "lib.h"
#include "object.h"
#include "option.h"
#include "printf.h"
#include "rt/fileio.h"
#include "rt/model.h"
#include "rt/mspace.h"
#include "rt/structs.h"
#include "tree.h"
#include "vlog/vlog-node.h"
#include "vlog/vlog-number.h"
#include "vlog/vlog-util.h"
#include "vpi/vpi-macros.h"
#include "vpi/vpi-model.h"
#include "vpi/vpi-priv.h"

#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
   PLI_INT32 type;
   loc_t     loc;
} c_vpiObject;

typedef struct {
   c_vpiObject object;
   uint32_t    refcount;
} c_refcounted;

typedef struct {
   unsigned      count;
   unsigned      limit;
   c_vpiObject **items;
} vpiObjectList;

typedef A(vpiHandle) vpiHandleList;

typedef void (*vpiLazyFn)(c_vpiObject *);

typedef struct {
   vpiLazyFn     fn;
   vpiObjectList list;
} vpiLazyList;

typedef struct {
   c_refcounted     refcounted;
   ident_t          name;
   s_vpi_systf_data systf;
} c_callback;

DEF_CLASS(callback, vpiCallback, refcounted.object);

typedef struct _c_abstractScope c_abstractScope;

struct _c_abstractScope {
   c_vpiObject      object;
   vlog_node_t      where;
   jit_handle_t     handle;
   vpiLazyList      decls;     // Nets, variables, parameters and arrays
   vpiLazyList      scopes;    // Instances, generate scopes and blocks
   vpiLazyList      misc;      // Ports, interface ports and modports
   hash_t          *calls;     // System task calls made from this instance
   tree_t           block;
   rt_scope_t      *rtscope;
   c_abstractScope *parent;
   const char      *name;
   const char      *fullname;
   void            *frame;     // Context of a package
};

typedef struct {
   c_abstractScope scope;
} c_module;

DEF_CLASS(module, vpiModule, scope.object);

typedef struct {
   c_abstractScope scope;
} c_genScope;

DEF_CLASS(genScope, vpiGenScope, scope.object);

typedef struct {
   c_vpiObject      object;
   c_abstractScope *parent;
   const char      *name;
   vpiObjectList    elems;
} c_scopeArray;

typedef struct {
   c_vpiObject      object;
   vpiObjectList    args;
   vlog_node_t      where;
   c_abstractScope *scope;
   unsigned         nslots;   // Number of argument slots the code passes
} c_tfCall;

typedef struct {
   c_tfCall    tfcall;
   c_callback *callback;
   vpiHandle   handle;
} c_sysTfCall;

typedef struct {
   c_sysTfCall systfcall;
} c_sysTaskCall;

DEF_CLASS(sysTaskCall, vpiSysTaskCall, systfcall.tfcall.object);

typedef struct {
   c_sysTfCall systfcall;
} c_sysFuncCall;

DEF_CLASS(sysFuncCall, vpiSysFuncCall, systfcall.tfcall.object);

typedef struct {
   c_vpiObject object;
   vlog_node_t where;
   unsigned    argpos;
} c_expr;

typedef struct {
   c_expr    expr;
   PLI_INT32 subtype;
   PLI_INT32 size;
   int64_t   ival;      // Value when there is no node
} c_constant;

DEF_CLASS(constant, vpiConstant, expr.object);

typedef struct {
   c_expr    expr;
   PLI_INT32 subtype;
   unsigned  argslot;
   bool      is_signed;
} c_operation;

DEF_CLASS(operation, vpiOperation, expr.object);

typedef struct {
   c_refcounted   refcounted;
   vpiObjectList *list;
   uint32_t       pos;
} c_iterator;

DEF_CLASS(iterator, vpiIterator, refcounted.object);

typedef struct {
   c_vpiObject      object;
   vlog_node_t      where;     // Declaration
   c_abstractScope *scope;
   UNSAFE_MPTR      mptr;
} c_abstractDecl;

typedef struct {
   c_abstractDecl decl;
} c_net;

typedef struct {
   c_abstractDecl decl;
} c_reg;

typedef struct {
   c_abstractDecl  decl;
   c_vpiObject    *left;
   c_vpiObject    *right;
   uint32_t        elemsz;   // Elements of storage in each array element
   uint32_t        count;
} c_regArray;

typedef struct {
   c_abstractDecl  decl;
   c_vpiObject    *value;
} c_parameter;

DEF_CLASS(parameter, vpiParameter, decl.object);

#define HANDLE_BITS      (sizeof(vpiHandle) * 8)
#define HANDLE_MAX_INDEX ((UINT64_C(1) << (HANDLE_BITS / 2)) - 1)

typedef enum {
   HANDLE_USER,
   HANDLE_INTERNAL,
} handle_kind_t;

typedef struct {
   c_vpiObject  *obj;
   handle_kind_t kind;
   uint32_t      generation;
} handle_slot_t;

STATIC_ASSERT(sizeof(handle_slot_t) <= 16);

typedef struct _vpi_context {
   shash_t       *strtab;
   rt_model_t    *model;
   hash_t        *objcache;
   tree_t         top;
   jit_t         *jit;
   handle_slot_t *handles;
   unsigned       num_handles;
   unsigned       free_hint;
   vpiHandleList  systasks;
   vpiObjectList  syscalls;
   c_sysTfCall   *call;
   jit_scalar_t  *args;
   tlab_t        *tlab;
   text_buf_t    *valuestr;
   mem_pool_t    *pool;
   vpiObjectList  recycle;
   vpiHandleList  pending;     // Callbacks registered before the model
   vpiObjectList  tops;        // Top-level instances and packages
   bool           have_tops;
   int            argc;
   char         **argv;
   int            precision;   // Simulation precision as log10 fs
   s_vpi_vecval  *vecbuf;
   size_t         veclen;
   bool           readonly;    // In a read-only synchronisation callback
} vpi_context_t;

static c_vpiObject *build_expr(vlog_node_t v, c_abstractScope *scope);
static c_abstractScope *cached_scope(tree_t block, rt_scope_t *s);
static c_abstractScope *parent_scope(c_abstractScope *scope);
static void vpi_lazy_decls(c_vpiObject *obj);
static void vpi_lazy_elems(c_vpiObject *obj);
static void vpi_lazy_scopes(c_vpiObject *obj);
static void vpi_lazy_misc(c_vpiObject *obj);
static c_refcounted *is_refcounted(c_vpiObject *obj);

static vpi_context_t *global_context = NULL;   // TODO: thread local

static inline vpi_context_t *vpi_context(void)
{
   assert(global_context != NULL);
   return global_context;
}

static handle_slot_t *decode_handle(vpi_context_t *c, vpiHandle handle)
{
   const uintptr_t bits = (uintptr_t)handle;
   const uint32_t index = bits & HANDLE_MAX_INDEX;
   const uint32_t generation = bits >> HANDLE_BITS/2;

   if (handle == NULL || index >= c->num_handles)
      return NULL;

   handle_slot_t *slot = &(c->handles[index]);
   if (slot->obj == NULL)
      return NULL;
   else if (slot->generation != generation)
      return NULL;   // Use-after-free

   return slot;
}

static inline vpiHandle encode_handle(handle_slot_t *slot, uint32_t index)
{
   const uintptr_t bits = (uintptr_t)slot->generation << HANDLE_BITS/2 | index;
   return (vpiHandle)bits;
}

static vpiHandle handle_for(c_vpiObject *obj, handle_kind_t kind)
{
   assert(obj != NULL);

   vpi_context_t *c = vpi_context();

   uint32_t index = c->free_hint;
   if (index >= c->num_handles || c->handles[index].obj != NULL) {
      for (index = 0; index < c->num_handles; index++) {
         if (c->handles[index].obj == NULL
             && c->handles[index].generation < HANDLE_MAX_INDEX)
            break;
      }
   }

   if (unlikely(index > HANDLE_MAX_INDEX)) {
      vpi_error(vpiSystem, NULL, "too many active handles");
      return NULL;
   }
   else if (index == c->num_handles) {
      const int new_size = MAX(c->num_handles * 2, 128);
      c->handles = xrealloc_array(c->handles, new_size, sizeof(handle_slot_t));
      c->num_handles = new_size;

      for (int i = index; i < new_size; i++) {
         c->handles[i].obj = NULL;
         c->handles[i].generation = 1;
      }
   }

   handle_slot_t *slot = &(c->handles[index]);
   slot->obj  = obj;
   slot->kind = kind;

   c->free_hint = index + 1;

   c_refcounted *rc = is_refcounted(obj);
   if (rc != NULL)
      rc->refcount++;

   return encode_handle(slot, index);
}

static inline vpiHandle user_handle_for(c_vpiObject *obj)
{
   return handle_for(obj, HANDLE_USER);
}

static inline vpiHandle internal_handle_for(c_vpiObject *obj)
{
   return handle_for(obj, HANDLE_INTERNAL);
}

static void drop_handle(vpi_context_t *c, vpiHandle handle)
{
   handle_slot_t *slot = decode_handle(c, handle);
   if (slot == NULL)
      return;

   c_vpiObject *obj = slot->obj;
   slot->obj = NULL;
   slot->generation++;

   c->free_hint = slot - c->handles;

   c_refcounted *rc = is_refcounted(obj);
   if (rc != NULL) {
      assert(rc->refcount > 0);
      if (--(rc->refcount) == 0)
         APUSH(c->recycle, obj);
   }
}

static inline c_vpiObject *from_handle(vpiHandle handle)
{
   handle_slot_t *slot = decode_handle(vpi_context(), handle);
   if (likely(slot != NULL))
      return slot->obj;

   vpi_error(vpiSystem, NULL, "invalid handle %p", handle);
   return NULL;
}

static c_refcounted *is_refcounted(c_vpiObject *obj)
{
   switch (obj->type) {
   case vpiCallback:
   case vpiIterator:
      return container_of(obj, c_refcounted, object);
   default:
      return NULL;
   }
}

static c_tfCall *is_tfCall(c_vpiObject *obj)
{
   switch (obj->type) {
   case vpiSysTaskCall:
   case vpiSysFuncCall:
      return container_of(obj, c_tfCall, object);
   default:
      return NULL;
   }
}

static c_sysTfCall *is_sysTfCall(c_vpiObject *obj)
{
   switch (obj->type) {
   case vpiSysTaskCall:
   case vpiSysFuncCall:
      return container_of(obj, c_sysTfCall, tfcall.object);
   default:
      return NULL;
   }
}

static bool is_array_type(PLI_INT32 type)
{
   return type == vpiRegArray || type == vpiNetArray;
}

static bool is_value_type(PLI_INT32 type)
{
   switch (type) {
   case vpiNet:
   case vpiNetBit:
   case vpiReg:
   case vpiRegBit:
   case vpiIntegerVar:
   case vpiTimeVar:
   case vpiRealVar:
   case vpiShortRealVar:
   case vpiLongIntVar:
   case vpiShortIntVar:
   case vpiIntVar:
   case vpiByteVar:
   case vpiBitVar:
   case vpiStringVar:
   case vpiEnumVar:
   case vpiStructVar:
   case vpiUnionVar:
   case vpiPackedArrayVar:
   case vpiNamedEvent:
   case vpiChandleVar:
      return true;
   default:
      return false;
   }
}

static c_abstractDecl *is_abstractDecl(c_vpiObject *obj)
{
   if (obj->type == vpiParameter || is_array_type(obj->type)
       || is_value_type(obj->type))
      return container_of(obj, c_abstractDecl, object);
   else
      return NULL;
}

static c_abstractDecl *is_valueDecl(c_vpiObject *obj)
{
   if (is_value_type(obj->type))
      return container_of(obj, c_abstractDecl, object);
   else
      return NULL;
}

static c_regArray *is_regArray(c_vpiObject *obj)
{
   if (is_array_type(obj->type))
      return container_of(obj, c_regArray, decl.object);
   else
      return NULL;
}

static c_abstractScope *is_abstractScope(c_vpiObject *obj)
{
   switch (obj->type) {
   case vpiModule:
   case vpiInterface:
   case vpiProgram:
   case vpiGenScope:
      return container_of(obj, c_abstractScope, object);
   default:
      return NULL;
   }
}

static c_constant *is_constantOrParam(c_vpiObject *obj)
{
   switch (obj->type) {
   case vpiConstant:
      return container_of(obj, c_constant, expr.object);
   case vpiParameter:
      {
         c_vpiObject *value = container_of(obj, c_parameter, decl.object)->value;
         return value != NULL ? is_constantOrParam(value) : NULL;
      }
   default:
      return NULL;
   }
}

static const char *handle_pp(vpiHandle handle)
{
   static __thread text_buf_t *tb = NULL;

   if (handle == NULL)
      return "NULL";

   if (tb == NULL)
      tb = tb_new();
   else
      tb_rewind(tb);

   tb_printf(tb, "%p:{", handle);

   handle_slot_t *slot = decode_handle(vpi_context(), handle);
   if (slot == NULL)
      tb_cat(tb, "INVALID");
   else {
      c_vpiObject *obj = slot->obj;
      tb_cat(tb, vpi_type_str(obj->type));

      c_operation *op = is_operation(obj);
      if (op != NULL)
         tb_printf(tb, " OpType=%s", vpi_op_type_str(op->subtype));
   }

   tb_append(tb, '}');

   return tb_get(tb);
}

static void *new_object(size_t size, PLI_INT32 type)
{
   assert(size >= sizeof(c_vpiObject));

   c_vpiObject *obj = pool_calloc(vpi_context()->pool, size);
   obj->type = type;
   obj->loc  = LOC_INVALID;

   return obj;
}

static void *recycle_object(size_t size, PLI_INT32 type)
{
   vpi_context_t *c = vpi_context();

   for (int i = 0; i < c->recycle.count; i++) {
      c_vpiObject *obj = c->recycle.items[i];
      if (obj->type == type) {
         for (int j = i; j < c->recycle.count - 1; j++)
            c->recycle.items[j] = c->recycle.items[j + 1];
         ATRIM(c->recycle, c->recycle.count - 1);

         memset(obj, '\0', size);
         obj->type = type;
         obj->loc  = LOC_INVALID;

         return obj;
      }
   }

   return new_object(size, type);
}

static void vpi_list_reserve(vpiObjectList *list, unsigned num)
{
   if (list->limit >= num)
      return;

   assert(list->count == 0);
   assert(list->items == NULL);

   mem_pool_t *mp = vpi_context()->pool;

   list->limit = num;
   list->items = pool_malloc_array(mp, num, sizeof(c_vpiObject *));
}

static inline void vpi_list_add(vpiObjectList *list, c_vpiObject *obj)
{
   assert(list->count < list->limit);
   list->items[list->count++] = obj;
}

static vpiObjectList *expand_lazy_list(c_vpiObject *obj, vpiLazyList *lazy)
{
   vpiLazyFn fn = lazy->fn;
   if (fn != NULL) {
      lazy->fn = NULL;   // Avoid infinite recursion
      (*fn)(obj);
   }

   return &(lazy->list);
}

static void vpi_list_push(vpiObjectList *list, c_vpiObject *obj)
{
   if (list->count == list->limit) {
      const unsigned limit = MAX(list->limit * 2, 8);
      c_vpiObject **items =
         pool_malloc_array(vpi_context()->pool, limit, sizeof(c_vpiObject *));
      if (list->count > 0)
         memcpy(items, list->items, list->count * sizeof(c_vpiObject *));
      list->items = items;
      list->limit = limit;
   }

   list->items[list->count++] = obj;
}

static const char *vpi_intern(const char *str)
{
   const size_t len = strlen(str) + 1;
   char *copy = pool_malloc(vpi_context()->pool, len);
   memcpy(copy, str, len);
   return copy;
}

static bool vpi_simple_name(const char *s, bool allow_index)
{
   // Names which need no escaping (5.6.1); the name of an element of an
   // array of instances or generate blocks ends with its index

   if (!isalpha((unsigned char)*s) && *s != '_')
      return false;

   for (; *s != '\0'; s++) {
      if (*s == '[' && allow_index) {
         const char *p = s + 1;
         if (*p == '-')
            p++;
         if (!isdigit((unsigned char)*p))
            return false;
         while (isdigit((unsigned char)*p))
            p++;
         if (*p != ']')
            return false;
         s = p;
      }
      else if (!isalnum((unsigned char)*s) && *s != '_' && *s != '$')
         return false;
   }

   return true;
}

static const char *vpi_name_str(const char *raw, bool allow_index)
{
   if (vpi_simple_name(raw, allow_index))
      return vpi_intern(raw);

   LOCAL_TEXT_BUF tb = tb_new();
   tb_printf(tb, "\\%s ", raw);
   return vpi_intern(tb_get(tb));
}

static const char *vpi_decl_raw_name(vlog_node_t where, text_buf_t *tb)
{
   // A static variable of a named block has a unique suffix

   const char *id = istr(vlog_ident(where));
   const char *suffix = strstr(id, "$static");
   if (suffix == NULL)
      return id;

   tb_catn(tb, id, suffix - id);
   return tb_get(tb);
}

static vlog_node_t vpi_tree_vlog(rt_scope_t *s)
{
   if (s->where == NULL || tree_kind(s->where) != T_BLOCK
       || tree_decls(s->where) == 0)
      return NULL;

   tree_t hier = tree_decl(s->where, 0);
   if (tree_kind(hier) != T_HIER || tree_kind(tree_ref(hier)) != T_VERILOG)
      return NULL;

   return tree_vlog(tree_ref(hier));
}

static const char *vpi_scope_fullname(c_abstractScope *s)
{
   if (s->fullname != NULL)
      return s->fullname;

   if (s->object.type == vpiPackage) {
      LOCAL_TEXT_BUF tb = tb_new();
      tb_printf(tb, "%s::", s->name);
      return (s->fullname = vpi_intern(tb_get(tb)));
   }

   c_abstractScope *parent = parent_scope(s);
   if (parent == NULL)
      return (s->fullname = s->name);

   LOCAL_TEXT_BUF tb = tb_new();
   tb_printf(tb, "%s.%s", vpi_scope_fullname(parent), s->name);
   return (s->fullname = vpi_intern(tb_get(tb)));
}

static void init_expr(c_expr *expr, vlog_node_t v)
{
   expr->where = v;
}

static c_operation *build_operation(vlog_node_t v);

static bool is_declared_value(vlog_node_t d)
{
   switch (vlog_kind(d)) {
   case V_VAR_DECL:
   case V_LOCAL_DECL:
   case V_NET_DECL:
   case V_PORT_DECL:
      return true;
   default:
      return false;
   }
}

static void init_tfCall(c_tfCall *call, vlog_node_t v, c_abstractScope *scope)
{
   call->where = v;
   call->scope = scope;

   const int nparams = vlog_params(v);
   vpi_list_reserve(&(call->args), nparams);

   for (int i = 0, argslot = 1; i < nparams; i++) {
      vlog_node_t p = vlog_param(v, i);
      c_vpiObject *obj;
      if (vlog_is_scan_target(v, i)
          || ((vlog_flags(v) & VLOG_F_IN_SUBR) && vlog_kind(p) == V_REF
              && is_declared_value(vlog_ref(p))))
         obj = &(build_operation(p)->expr.object);   // Caller's scope
      else
         obj = build_expr(p, scope);
      vpi_list_add(&(call->args), obj);

      c_operation *op = is_operation(obj);
      if (op != NULL && op->subtype != vpiNullOp) {
         op->argslot = argslot;
         argslot += 3;
      }

      call->nslots = argslot;
   }
}

static void init_sysTfCall(c_sysTfCall *call, vlog_node_t v,
                           c_callback *callback, c_abstractScope *scope)
{
   init_tfCall(&call->tfcall, v, scope);
   call->callback = callback;

   vpi_context_t *c = vpi_context();
   APUSH(c->syscalls, &(call->tfcall.object));
}

static void init_abstractDecl(c_abstractDecl *decl, vlog_node_t v,
                              c_abstractScope *scope)
{
   decl->object.loc = *vlog_loc(v);
   decl->where = v;
   decl->scope = scope;
}

static void init_abstractScope(c_abstractScope *scope, vlog_node_t v)
{
   scope->object.loc = *vlog_loc(v);
   scope->where      = v;
   scope->handle     = JIT_HANDLE_INVALID;
   scope->decls.fn   = vpi_lazy_decls;
   scope->scopes.fn  = vpi_lazy_scopes;
   scope->misc.fn    = vpi_lazy_misc;
}

static c_constant *build_int_constant(int64_t value)
{
   c_constant *con = new_object(sizeof(c_constant), vpiConstant);
   con->subtype = vpiDecConst;
   con->size    = 32;
   con->ival    = value;
   return con;
}

static PLI_INT32 vpi_data_type(vlog_node_t dtype)
{
   // The object type of a variable with this data type (37.17)

   if (dtype == NULL)
      return 0;

   switch (vlog_kind(dtype)) {
   case V_DATA_TYPE:
      switch (vlog_subkind(dtype)) {
      case DT_IMPLICIT:
      case DT_LOGIC:     return vpiReg;
      case DT_INTEGER:   return vpiIntegerVar;
      case DT_TIME:      return vpiTimeVar;
      case DT_REAL:
      case DT_REALTIME:  return vpiRealVar;
      case DT_SHORTREAL: return vpiShortRealVar;
      case DT_BYTE:      return vpiByteVar;
      case DT_SHORTINT:  return vpiShortIntVar;
      case DT_INT:       return vpiIntVar;
      case DT_LONGINT:   return vpiLongIntVar;
      case DT_BIT:       return vpiBitVar;
      case DT_STRING:    return vpiStringVar;
      case DT_EVENT:     return vpiNamedEvent;
      case DT_CHANDLE:   return vpiChandleVar;
      default:           return 0;
      }
   case V_ENUM_DECL:
      return vpiEnumVar;
   case V_STRUCT_DECL:
      return vpiStructVar;
   case V_UNION_DECL:
      return vpiUnionVar;
   default:
      return 0;
   }
}

static bool vpi_is_packed_type(vlog_node_t dtype)
{
   switch (vlog_kind(dtype)) {
   case V_STRUCT_DECL:
   case V_UNION_DECL:
      return !!(vlog_flags(dtype) & VLOG_F_PACKED);
   case V_DATA_TYPE:
   case V_ENUM_DECL:
      return true;
   default:
      return false;
   }
}

static c_constant *build_constant(vlog_node_t v)
{
   c_constant *con = new_object(sizeof(c_constant), vpiConstant);
   init_expr(&con->expr, v);

   switch (vlog_kind(v)) {
   case V_STRING:
      con->subtype = vpiStringConst;
      con->size = number_width(vlog_number(v));
      assert(con->size % 8 == 0);
      break;
   case V_NUMBER:
      con->subtype = vpiBinaryConst;
      con->size = number_width(vlog_number(v));
      break;
   case V_REAL:
      con->subtype = vpiRealConst;
      con->size = 64;
      break;
   default:
      should_not_reach_here();
   }

   return con;
}

static c_operation *build_operation(vlog_node_t v)
{
   c_operation *op = new_object(sizeof(c_operation), vpiOperation);
   init_expr(&op->expr, v);
   op->is_signed = vlog_is_signed(v);

   switch (vlog_kind(v)) {
   case V_EMPTY:
      op->subtype = vpiNullOp;
      break;
   case V_POSTFIX:
      op->subtype = vlog_subkind(v) == V_INCDEC_PLUS
         ? vpiPostIncOp : vpiPostDecOp;
      break;
   case V_PREFIX:
      op->subtype = vlog_subkind(v) == V_INCDEC_PLUS
         ? vpiPreIncOp : vpiPreDecOp;
      break;
   case V_CONCAT:
      op->subtype = vlog_has_value(v) ? vpiMultiConcatOp : vpiConcatOp;
      break;
   default:
      break;
   }

   return op;
}

static c_vpiObject *build_expr(vlog_node_t v, c_abstractScope *scope)
{
   switch (vlog_kind(v)) {
   case V_STRING:
   case V_NUMBER:
   case V_REAL:
      return &(build_constant(v)->expr.object);
   case V_REF:
      {
         vlog_node_t d = vlog_ref(v);
         const vlog_kind_t kind = vlog_kind(d);
         if (kind == V_PORT_DECL)
            d = vlog_ref(d);
         else if (kind == V_TF_PORT_DECL || kind == V_FUNC_DECL
                  || kind == V_MODULE)
            return &(build_operation(v)->expr.object);  /// XXX: hack
         else if (kind == V_VAR_DECL && (vlog_is_dynamic_type(vlog_type(d))
                                         || (vlog_flags(d) & VLOG_F_STATIC)))
            return &(build_operation(v)->expr.object);
         else if (kind == V_PARAM_DECL || kind == V_LOCALPARAM
                  || kind == V_ENUM_NAME)
            return &(build_operation(v)->expr.object);

         // A generate block can refer to declarations of the enclosing scopes
         for (c_abstractScope *s = scope; s != NULL; s = parent_scope(s)) {
            vpiObjectList *list =
               expand_lazy_list(&(s->object), &(s->decls));
            for (int i = 0; i < list->count; i++) {
               c_abstractDecl *decl = is_abstractDecl(list->items[i]);
               assert(decl != NULL);

               // The code passes no value for a declaration held in
               // a signal
               if (decl->where == d && (decl->storage == STORE_SIGNAL
                                        || decl->storage == STORE_REAL))
                  return &(decl->object);
            }
         }

         // Task and function locals, and declarations whose value is not
         // held in a signal
         return &(build_operation(v)->expr.object);
      }
   case V_BINARY:
   case V_UNARY:
   case V_SYS_FCALL:
   case V_EMPTY:
   case V_PREFIX:
   case V_POSTFIX:
   case V_CONCAT:
   case V_COND_EXPR:
   case V_PART_SELECT:
   case V_BIT_SELECT:   // XXX: check this
   case V_MEMBER_REF:
   case V_HIER_REF:
   case V_USER_FCALL:
   case V_METHOD_CALL:
   case V_CAST:
   case V_INSIDE:
   case V_STREAM:
   case V_OP_ASSIGN:
   case V_NULL:
      return &(build_operation(v)->expr.object);
   default:
      fatal_trace("cannot build VPI expr for node kind %s",
                  vlog_kind_str(vlog_kind(v)));
   }

   return NULL;
}

static c_sysTaskCall *build_sysTaskCall(vlog_node_t where, c_callback *callback,
                                        c_abstractScope *scope)
{
   c_sysTaskCall *call = new_object(sizeof(c_sysTaskCall), vpiSysTaskCall);
   init_sysTfCall(&call->systfcall, where, callback, scope);

   return call;
}

static c_sysFuncCall *build_sysFuncCall(vlog_node_t where, c_callback *callback,
                                        c_abstractScope *scope)
{
   c_sysFuncCall *call = new_object(sizeof(c_sysTaskCall), vpiSysFuncCall);
   init_sysTfCall(&call->systfcall, where, callback, scope);

   return call;
}

static void build_parameter(vlog_node_t v, c_abstractScope *scope)
{
   c_parameter *param = new_object(sizeof(c_parameter), vpiParameter);
   init_abstractDecl(&(param->decl), v, scope);
   param->decl.dtype = vlog_has_type(v) ? vlog_get_type(v) : NULL;
   param->decl.storage = STORE_NONE;

   if (vlog_has_value(v)) {
      vlog_node_t value = vlog_value(v);
      int64_t ival;
      switch (vlog_kind(value)) {
      case V_NUMBER:
      case V_STRING:
      case V_REAL:
         param->value = &(build_constant(value)->expr.object);
         break;
      default:
         if (vlog_get_const(value, &ival))
            param->value = &(build_int_constant(ival)->expr.object);
         break;
      }
   }

   vpi_list_push(&scope->decls.list, &(param->decl.object));
}

static c_abstractScope *build_scope(PLI_INT32 type, vlog_node_t v,
                                    tree_t block, rt_scope_t *s)
{
   const size_t size =
      type == vpiGenScope ? sizeof(c_genScope) : sizeof(c_module);
   c_abstractScope *as = new_object(size, type);
   init_abstractScope(as, v);
   as->block   = block;
   as->rtscope = s;

   return as;
}

static c_genScope *build_genScope(vlog_node_t v, tree_t block, rt_scope_t *s)
{
   c_genScope *m = new_object(sizeof(c_genScope), vpiGenScope);
   init_abstractScope(&m->scope, v);
   m->block = block;
   m->rtscope = s;

   return m;
}

static c_abstractScope *cached_scope(tree_t block, rt_scope_t *s)
{
   assert(tree_kind(block) == T_BLOCK);

   hash_t *cache = vpi_context()->objcache;
   c_abstractScope *as = hash_get(cache, block);
   if (as == NULL) {
      tree_t hier = tree_decl(block, 0);
      assert(tree_kind(hier) == T_HIER);

      tree_t wrap = tree_ref(hier);
      assert(tree_kind(wrap) == T_VERILOG);

      vlog_node_t v = tree_vlog(wrap);

      switch (vlog_kind(v)) {
      case V_INST_BODY:
         as = &(build_module(v, block, s)->scope);
         break;
      case V_GEN_BLOCK:
         as = build_scope(vpiGenScope, v, block, s);
         break;
      default:
         should_not_reach_here();
      }

      as->name = vpi_name_str(istr(tree_ident(block)), true);

      hash_put(cache, block, as);
   }

   return as;
}

static c_abstractScope *parent_scope(c_abstractScope *scope)
{
   if (scope->parent != NULL || scope->rtscope == NULL)
      return scope->parent;

   rt_scope_t *rtscope = scope->rtscope;
   if (rtscope->parent == NULL || rtscope->parent->where == NULL
       || tree_kind(rtscope->parent->where) != T_BLOCK
       || vpi_tree_vlog(rtscope->parent) == NULL)
      return NULL;

   return (scope->parent =
           cached_scope(rtscope->parent->where, rtscope->parent));
}

static c_abstractScope *storage_scope(c_abstractScope *s)
{
   // Static variables of a named block belong to the enclosing instance

   while (s != NULL && s->rtscope == NULL && s->object.type != vpiPackage)
      s = s->parent;

   return s;
}

static rt_model_t *vpi_get_model(vpi_context_t *c)
{
   if (c->model != NULL)
      return c->model;

   return get_model();
}

static jit_t *vpi_get_jit(vpi_context_t *c)
{
   if (c->jit != NULL)
      return c->jit;

   return jit_for_thread();
}

static void *vpi_get_ptr(c_abstractDecl *decl)
{
   if (decl->mptr != NULL)
      return decl->mptr;

   jit_t *jit = vpi_get_jit(vpi_context());

   if (decl->scope->handle == JIT_HANDLE_INVALID) {
      c_module *mod = is_module(&(decl->scope->object));
      if (mod != NULL) {
         tree_t hier = tree_decl(mod->rtscope->where, 0);
         assert(tree_kind(hier) == T_HIER);

         decl->scope->handle = jit_lazy_compile(jit, tree_ident2(hier));
      }
   }

   ident_t name = vlog_ident(decl->where);

   void *ctx = NULL;
   c_module *mod = is_module(&(decl->scope->object));
   if (mod != NULL)
      ctx = *mptr_get(mod->rtscope->privdata);

   if (decl->scope->handle == JIT_HANDLE_INVALID || ctx == NULL)
      fatal_at(&(decl->object.loc), "cannot get pointer to %s", istr(name));

   return (decl->mptr = jit_get_frame_var(jit, decl->scope->handle, ctx, name));
}

static bool vpi_get_range(c_vpiObject *obj, int64_t *ileft, int64_t *iright)
{
   c_constant *left = NULL, *right = NULL;

   c_regArray *arr = is_regArray(obj);
   if (arr != NULL) {
      left = is_constantOrParam(arr->left);
      right = is_constantOrParam(arr->right);
   }

   if (right == NULL || left == NULL) {
      vpi_error(vpiError, &(obj->loc), "object has unknown range");
      return false;
   }

   *ileft = number_integer(vlog_number(left->expr.where));
   *iright = number_integer(vlog_number(right->expr.where));

   return true;
}

static void vpi_lazy_decls(c_vpiObject *obj)
{
   c_abstractScope *s = is_abstractScope(obj);
   assert(s != NULL);

   const int ndecls = vlog_decls(s->where);

   vpi_list_reserve(&s->decls.list, ndecls);

   for (int i = 0; i < ndecls; i++) {
      vlog_node_t v = vlog_decl(s->where, i);
      switch (vlog_kind(v)) {
      case V_NET_DECL:
         build_net(v, s);
         break;
      case V_VAR_DECL:
         build_reg(v, s);
         break;
      case V_LOCALPARAM:
         build_parameter(v, s);
         break;
      default:
         break;
      }
   }
}

static void vpi_lazy_elems(c_vpiObject *obj)
{
   c_regArray *arr = is_regArray(obj);
   assert(arr != NULL);

   int64_t left, right;
   if (!vpi_get_range(obj, &left, &right))
      return;

   PLI_UINT32 count = left > right ? left - right + 1 : right - left + 1;

   vpi_list_reserve(&arr->elems.list, count);

   PLI_UINT32 size = vlog_size(vlog_type(arr->decl.where));

   for (int64_t i = 0; i < count; i++) {
      c_reg *reg = new_object(sizeof(c_reg), vpiReg);
      init_abstractDecl(&(reg->decl), arr->decl.where, arr->decl.scope);
      reg->size = size;
      reg->offset = left > right ? (count - 1 - i) * size : i * size;

      vpi_list_add(&arr->elems.list, &(reg->decl.object));
   }
}

////////////////////////////////////////////////////////////////////////////////
// Public API

DLLEXPORT
vpiHandle vpi_register_systf(p_vpi_systf_data systf_data_p)
{
   vpi_clear_error();

   VPI_TRACE("tfname=%s", systf_data_p->tfname);

   assert(systf_data_p->tfname[0] == '$');  // TODO: add test

   c_callback *cb = recycle_object(sizeof(c_callback), vpiCallback);
   cb->systf = *systf_data_p;
   cb->name  = ident_new(systf_data_p->tfname);

   vpiHandle handle = internal_handle_for(&cb->refcounted.object);
   APUSH(vpi_context()->systasks, handle);

   return handle;
}

DLLEXPORT
PLI_INT32 vpi_release_handle(vpiHandle object)
{
   vpi_clear_error();

   VPI_TRACE("handle=%s", handle_pp(object));

   vpi_context_t *c = vpi_context();
   handle_slot_t *slot = decode_handle(c, object);
   if (slot == NULL) {
      vpi_error(vpiError, NULL, "invalid handle %p", object);
      return 0;
   }
   else if (slot->kind == HANDLE_INTERNAL) {
      vpi_error(vpiError, &(slot->obj->loc), "cannot release this handle as "
                "it is owned by the system");
      return 0;
   }

   drop_handle(c, object);
   return 1;
}

DLLEXPORT
vpiHandle vpi_handle(PLI_INT32 type, vpiHandle refHandle)
{
   vpi_clear_error();

   VPI_TRACE("type=%s refHandle=%s", vpi_method_str(type),
             handle_pp(refHandle));

   vpi_context_t *c = vpi_context();

   if (refHandle == NULL) {
      switch (type) {
      case vpiSysTfCall:
         if (c->call != NULL)
            return user_handle_for(&c->call->tfcall.object);
         else
            return NULL;
      }
   }

   return NULL;
}

DLLEXPORT
vpiHandle vpi_handle_by_name(PLI_BYTE8 *name, vpiHandle scope)
{
   VPI_MISSING;
}

DLLEXPORT
vpiHandle vpi_handle_by_index(vpiHandle handle, PLI_INT32 index)
{
   vpi_clear_error();

   VPI_TRACE("handle=%s index=%d", handle_pp(handle), index);

   c_vpiObject *obj = from_handle(handle);
   if (obj == NULL)
      return NULL;

   int64_t left, right;
   if (!vpi_get_range(obj, &left, &right))
      return NULL;

   const int64_t low = left < right ? left : right;
   const int64_t high = left < right ? right : left;

   if (index < low || index > high) {
      vpi_error(vpiError, &(obj->loc), "index %d out of range", index);
      return NULL;
   }

   c_regArray *arr = is_regArray(obj);
   if (arr != NULL) {
      vpiObjectList *elems = expand_lazy_list(&arr->decl.object, &arr->elems);
      assert(index - low < elems->count);
      return user_handle_for(elems->items[index - low]);
   }

   vpi_error(vpiError, &(obj->loc), "handle cannot be indexed");
   return NULL;
}

DLLEXPORT
vpiHandle vpi_iterate(PLI_INT32 type, vpiHandle refHandle)
{
   vpi_clear_error();

   VPI_TRACE("type=%s handle=%s", vpi_method_str(type), handle_pp(refHandle));

   c_vpiObject *obj = NULL;
   if (refHandle != NULL && (obj = from_handle(refHandle)) == NULL)
      return NULL;

   c_iterator *it = recycle_object(sizeof(c_iterator), vpiIterator);
   if (!init_iterator(it, type, obj)) {
      vpi_error(vpiError, obj ? &(obj->loc) : NULL,
                "relation %s not supported for handle %s",
                vpi_method_str(type), handle_pp(refHandle));
      return NULL;
   }

   return user_handle_for(&(it->refcounted.object));
}

DLLEXPORT
vpiHandle vpi_scan(vpiHandle iterator)
{
   vpi_clear_error();

   VPI_TRACE("iterator=%s", handle_pp(iterator));

   c_vpiObject *obj = from_handle(iterator);
   if (obj == NULL)
      return NULL;

   c_iterator *it = cast_iterator(obj);
   if (it == NULL)
      return NULL;

   if (it->pos < it->list->count)
      return user_handle_for(it->list->items[it->pos++]);

   drop_handle(vpi_context(), iterator);
   return NULL;
}

DLLEXPORT
PLI_INT32 vpi_get(PLI_INT32 property, vpiHandle object)
{
   vpi_clear_error();

   VPI_TRACE("property=%s object=%s", vpi_property_str(property),
             handle_pp(object));

   c_vpiObject *obj = from_handle(object);
   if (obj == NULL)
      return vpiUndefined;

   if (property == vpiType)
      return obj->type;

   c_constant *con = is_constantOrParam(obj);
   if (con != NULL) {
      switch (property) {
      case vpiConstType:
         return con->subtype;
      case vpiSize:
         return con->size;
      case vpiSigned:
         return vlog_is_signed(con->expr.where);
      default:
         goto missing_property;
      }
   }

   c_operation *op = is_operation(obj);
   if (op != NULL) {
      switch (property) {
      case vpiSize:
         {
            if (op->subtype == vpiNullOp)
               return 0;

            if (c->args != NULL) {
               const int64_t size = c->args[op->argslot].integer;
               return size == -5
                  ? class_handle_bits(c->args[op->argslot + 1].integer) : size;
            }

            goto missing_property;
         }
      case vpiOpType:
         return op->subtype;
      case vpiSigned:
         return op->is_signed;
      }
   }

   c_reg *reg = is_reg(obj);
   if (reg != NULL) {
      switch (property) {
      case vpiSize:
         return reg->size;
      case vpiSigned:
         return vlog_is_signed(reg->decl.where);
      }
   }

   c_net *net = is_net(obj);
   if (net != NULL) {
      switch (property) {
      case vpiSize:
         return net->size;
      case vpiSigned:
         return vlog_is_signed(net->decl.where);
      }
   }

missing_property:
   vpi_error(vpiError, &(obj->loc), "object does not have property %s",
             vpi_property_str(property));
   return vpiUndefined;
}

DLLEXPORT
void vpi_get_value(vpiHandle handle, p_vpi_value value_p)
{
   vpi_clear_error();

   VPI_TRACE("handle=%s value_p=%p", handle_pp(handle), value_p);

   c_vpiObject *obj = from_handle(handle);
   if (obj == NULL)
      return;

   vpi_context_t *c = vpi_context();
   tb_rewind(c->valuestr);

   c_constant *con = is_constantOrParam(obj);
   if (con != NULL) {
      switch (con->subtype) {
      case vpiStringConst:
         {
            number_t n = vlog_number(con->expr.where);
            for (int i = con->size/8 - 1; i >= 0; i--)
               tb_append(c->valuestr, number_byte(n, i));

            value_p->format = vpiStringVal;
            value_p->value.str = (PLI_BYTE8 *)tb_get(c->valuestr);
         }
         return;

      case vpiBinaryConst:
         {
            number_t n = vlog_number(con->expr.where);

            const uint64_t *abits, *bbits;
            number_get(n, &abits, &bbits);

            vpi_format_number(number_width(n), number_signed(n), abits, bbits,
                              value_p, c->valuestr);
            return;
         }

      case vpiRealConst:
         vpi_format_real(vlog_dval(con->expr.where), value_p, c->valuestr);
         return;
      }
   }

   c_operation *op = is_operation(obj);
   if (op != NULL && c->args != NULL && op->subtype != vpiNullOp) {
      const int64_t size = c->args[op->argslot].integer;
      if (size == -2) {
         const char *str = c->args[op->argslot + 1].pointer;
         const int64_t len = c->args[op->argslot + 2].integer;

         tb_catn(c->valuestr, str, len);

         value_p->format = vpiStringVal;
         value_p->value.str = (PLI_BYTE8 *)tb_get(c->valuestr);
      }
      else if (size == -5) {
         const uint64_t id = c->args[op->argslot + 1].integer;
         if (id == 0) {
            tb_cat(c->valuestr, "null");
            value_p->format = vpiStringVal;
            value_p->value.str = (PLI_BYTE8 *)tb_get(c->valuestr);
         }
         else {
            uint64_t abits[1] = { id }, bbits[1] = { 0 };
            vpi_format_number(class_handle_bits(id), false, abits, bbits, value_p,
                              c->valuestr);
         }
      }
      else if (size < 0) {
         vpi_format_real(c->args[op->argslot + 1].real, value_p,
                         c->valuestr);
      }
      else {
         if (size <= 64) {
            uint64_t abits[1] = { c->args[op->argslot + 1].integer };
            uint64_t bbits[1] = { c->args[op->argslot + 2].integer };

            vpi_format_number(size, op->is_signed, abits, bbits, value_p,
                              c->valuestr);
         }
         else {
            const uint64_t *abits = c->args[op->argslot + 1].pointer;
            const uint64_t *bbits = c->args[op->argslot + 2].pointer;

            vpi_format_number(size, op->is_signed, abits, bbits, value_p,
                              c->valuestr);
         }
      }
      return;
   }

   c_abstractDecl *decl = is_abstractDecl(obj);
   if (decl != NULL) {
      sig_shared_t **ss = vpi_get_ptr(decl);
      rt_signal_t *s = container_of(*ss, rt_signal_t, shared);

      vlog_node_t type = vlog_get_type(decl->where);
      if (type == NULL)
         goto fail;

      if (vlog_kind(type) == V_DATA_TYPE && vlog_subkind(type) == DT_REAL) {
         vpi_format_real(unaligned_load(signal_value(s), double), value_p,
                         c->valuestr);
         return;
      }

      const int width = signal_width(s);
      const int nwords = (width + 63) / 64;
      uint64_t *mem LOCAL = xmalloc_array(nwords * 2, sizeof(uint64_t));
      uint64_t *abits = mem, *bbits = mem + nwords;

      for (int i = 0; i < nwords * 2; i++)
         mem[i] = 0;

      const uint8_t *vals = signal_value(s);

      for (int i = 0; i < width; i++) {
         const int pos = width - 1 - i;
         abits[pos / 64] |= (uint64_t)(vals[i] & 1) << (pos % 64);
         bbits[pos / 64] |= (uint64_t)((vals[i] >> 1) & 1) << (pos % 64);
      }

      const bool is_signed = !!(vlog_flags(type) & VLOG_F_SIGNED);
      vpi_format_number(width, is_signed, abits, bbits, value_p,
                        c->valuestr);
      return;
   }

 fail:
   vpi_error(vpiError, &(obj->loc), "cannot evaluate %s", handle_pp(handle));
}

DLLEXPORT
vpiHandle vpi_put_value(vpiHandle handle, p_vpi_value value_p,
                        p_vpi_time time_p, PLI_INT32 flags)
{
   vpi_clear_error();

   VPI_TRACE("handle=%s value_p=%p", handle_pp(handle), value_p);

   c_vpiObject *obj = from_handle(handle);
   if (obj == NULL)
      return NULL;

   vpi_context_t *c = vpi_context();

   c_operation *op = is_operation(obj);
   if (op != NULL && c->args != NULL && op->subtype != vpiNullOp
       && c->args[op->argslot].integer == -4) {
      // An integral variable set by the task: its bits and a flag
      const int width = c->args[op->argslot + 2].integer;
      uint8_t *buf = c->args[op->argslot + 1].pointer;
      memset(buf, 0, width);
      if (value_p->format == vpiRealVal && width == 64) {
         // The bits of the real value most significant first
         uint64_t bits;
         memcpy(&bits, &value_p->value.real, sizeof(bits));
         for (int i = 0; i < 64; i++)
            buf[i] = (bits >> (63 - i)) & 1;

         buf[width] = 1;
         return NULL;
      }

      if (!unpack_value(value_p, buf, width, &(obj->loc)))
         return NULL;

      buf[width] = 1;
      return NULL;
   }

   if (op != NULL && c->args != NULL && op->subtype != vpiNullOp
       && c->args[op->argslot].integer == -3) {
      // A string variable set by the task: a buffer and its length
      if (value_p->format != vpiStringVal) {
         vpi_error(vpiError, &(obj->loc), "unsupported format %d",
                   value_p->format);
         return NULL;
      }

      char *buf = c->args[op->argslot + 1].pointer;
      const size_t len = MIN(strlen(value_p->value.str), STR_TARGET_MAX - 1);
      memcpy(buf, value_p->value.str, len);
      buf[len] = '\0';
      return NULL;
   }

   c_sysFuncCall *fcall = is_sysFuncCall(obj);
   if (fcall != NULL && c->args != NULL) {
      switch (fcall->systfcall.callback->systf.sysfunctype) {
      case vpiTimeFunc:
         {
            assert(value_p->format == vpiTimeVal);

            const p_vpi_time tm = value_p->value.time;
            c->args[0].integer = (uint64_t)tm->high << 32 | tm->low;
            c->args[1].integer = 0;

            return NULL;
         }

      case vpiIntFunc:
         {
            switch (value_p->format) {
            case vpiIntVal:
               // TODO: mask should be based on result size
               c->args[0].integer = value_p->value.integer & 0xffffffff;
               c->args[1].integer = 0;
               break;
            case vpiVectorVal:
               c->args[0].integer = value_p->value.vector[0].aval;
               c->args[1].integer = value_p->value.vector[0].bval;
               break;
            default:
               vpi_error(vpiError, &(obj->loc), "unsupported format %d",
                         value_p->format);
            }

            return NULL;
         }

      case vpiRealFunc:
         {
            assert(value_p->format == vpiRealVal);

            c->args[0].real = value_p->value.real;
            return NULL;
         }
      }
   }

   c_reg *reg = is_reg(obj);
   if (reg != NULL) {
      sig_shared_t **ss = vpi_get_ptr(&reg->decl);
      rt_signal_t *s = container_of(*ss, rt_signal_t, shared);

      uint8_t *unpacked LOCAL = xcalloc_array(reg->size, sizeof(uint8_t));

      switch (value_p->format) {
      case vpiIntVal:
         for (int i = 0; i < reg->size && i < 32; i++)
            unpacked[reg->size - 1 - i] =
               !!(value_p->value.integer & (1u << i));
         break;

      case vpiStringVal:
         {
            const char *str = value_p->value.str;
            const int len = strlen(str);
            for (int i = 0, bit = 0; i < len && bit < reg->size; i++) {
               const uint8_t ch = str[len - 1 - i];
               for (int j = 0; j < 8 && bit < reg->size; j++, bit++)
                  unpacked[reg->size - 1 - bit] = !!(ch & (1 << j));
            }
         }
         break;

      case vpiDecStrVal:
         {
            const uint64_t value = strtoull(value_p->value.str, NULL, 10);
            for (int i = 0; i < reg->size && i < 64; i++)
               unpacked[reg->size - 1 - i] = !!(value & (UINT64_C(1) << i));
         }
         break;

      case vpiBinStrVal:
         for (int i = strlen(value_p->value.str) - 1, bit = 0;
              i >= 0 && bit < reg->size; i--) {
            switch (value_p->value.str[i]) {
            case '0':
            case '1':
               unpacked[reg->size - 1 - bit++] = value_p->value.str[i] == '1';
               break;
            case '_':
               break;
            default:
               vpi_error(vpiError, NULL, "invalid character %c in binary "
                         "string", value_p->value.str[i]);
               return NULL;
            }
         }
         break;

      case vpiOctStrVal:
         for (int i = strlen(value_p->value.str) - 1, bit = 0;
              i >= 0 && bit < reg->size; i--) {
            uint8_t digit;
            switch (value_p->value.str[i]) {
            case '0'...'7': digit = value_p->value.str[i] - '0'; break;
            case '_': continue;
            default:
               vpi_error(vpiError, NULL, "invalid character %c in octal "
                         "string", value_p->value.str[i]);
               return NULL;
            }

            for (int j = 0; j < 3 && bit < reg->size; j++, bit++)
               unpacked[reg->size - 1 - bit] = !!(digit & (1 << j));
         }
         break;

      case vpiHexStrVal:
         for (int i = strlen(value_p->value.str) - 1, bit = 0;
              i >= 0 && bit < reg->size; i--) {
            uint8_t nibble;
            switch (value_p->value.str[i]) {
            case '0'...'9': nibble = value_p->value.str[i] - '0'; break;
            case 'a'...'f': nibble = 10 + value_p->value.str[i] - 'a'; break;
            case 'A'...'F': nibble = 10 + value_p->value.str[i] - 'A'; break;
            case '_': continue;
            default:
               vpi_error(vpiError, NULL, "invalid character %c in hex string",
                         value_p->value.str[i]);
               return NULL;
            }

            for (int j = 0; j < 4 && bit < reg->size; j++, bit++)
               unpacked[reg->size - 1 - bit] = !!(nibble & (1 << j));
         }
         break;

      default:
         vpi_error(vpiError, &(obj->loc), "unsupported format %d",
                   value_p->format);
         return NULL;
      }

      deposit_signal(vpi_get_model(c), s, unpacked, reg->offset, reg->size);
      return NULL;
   }

   vpi_error(vpiError, &(obj->loc), "cannot change value of %s",
             handle_pp(handle));
   return NULL;
}

vpi_context_t *vpi_context_new(void)
{
   assert(global_context == NULL);

   vpi_context_t *c = global_context = xcalloc(sizeof(vpi_context_t));
   c->objcache = hash_new(128);
   c->valuestr = tb_new();
   c->pool     = pool_new();

   vpi_register_builtins();

   return c;
}

void vpi_context_initialise(vpi_context_t *c, tree_t top, rt_model_t *model,
                            jit_t *jit, int argc, char **argv)
{
   assert(c->model == NULL);
   assert(c->top == NULL);
   assert(c->jit == NULL);

   c->model = model;
   c->top   = top;
   c->jit   = jit;
}

static void vpi_handles_diag(vpi_context_t *c, diag_t *d, handle_kind_t kind)
{
   for (int i = 0; i < c->num_handles; i++) {
      handle_slot_t *slot = &(c->handles[i]);
      if (slot->obj == NULL || slot->kind != kind)
         continue;

      diag_printf(d, "\n%s", handle_pp(encode_handle(slot, i)));

      c_refcounted *rc = is_refcounted(slot->obj);
      if (rc != NULL)
         diag_printf(d, " with %d reference%s", rc->refcount,
                     rc->refcount != 1 ? "s" : "");
   }
}

static void vpi_check_leaks(vpi_context_t *c)
{
   int nuser = 0, ninternal UNUSED = 0;
   for (int i = 0; i < c->num_handles; i++) {
      if (c->handles[i].obj == NULL)
         continue;
      else if (c->handles[i].kind == HANDLE_INTERNAL)
         ninternal++;
      else
         nuser++;
   }

   if (nuser > 0) {
      diag_t *d = diag_new(DIAG_WARN, NULL);
      diag_printf(d, "VPI program exited with %d active handles", nuser);
      vpi_handles_diag(c, d, HANDLE_USER);
      diag_emit(d);
   }

#ifdef DEBUG
   if (ninternal > 0) {
      diag_t *d = diag_new(DIAG_DEBUG, NULL);
      diag_printf(d, "VPI program exited with %d active internal handles",
                  ninternal);
      vpi_handles_diag(c, d, HANDLE_INTERNAL);
      diag_emit(d);
   }
#endif
}

void vpi_context_free(vpi_context_t *c)
{
   if (opt_get_int(OPT_PLI_DEBUG)) {
      // Release all system call handles to prevent false positives
      for (int i = 0; i < c->syscalls.count; i++) {
         c_sysTfCall *call = is_sysTfCall(c->syscalls.items[i]);
         assert(call != NULL);

         if (call->handle != NULL)
            drop_handle(c, call->handle);
      }

      for (int i = 0; i < c->systasks.count; i++)
         drop_handle(c, c->systasks.items[i]);

      vpi_check_leaks(c);
   }

   assert(c == global_context);
   global_context = NULL;

   if (c->strtab != NULL)
      shash_free(c->strtab);

   ACLEAR(c->syscalls);
   ACLEAR(c->systasks);
   ACLEAR(c->recycle);

#ifdef DEBUG
   size_t alloc, npages;
   pool_stats(c->pool, &alloc, &npages);
   if (npages > 1)
      debugf("VPI allocated %zu bytes in %zu pages", alloc, npages);
#endif

   pool_free(c->pool);
   tb_free(c->valuestr);
   hash_free(c->objcache);
   free(c->handles);
   free(c);
}

////////////////////////////////////////////////////////////////////////////////
// Foreign function interface

vpiHandle vpi_bind_foreign(ident_t name, vlog_node_t where)
{
   vpi_context_t *c = vpi_context();

   c_abstractScope *scope = NULL;
   rt_model_t *m = get_model_or_null();
   if (m != NULL) {
      rt_scope_t *rs = get_active_scope(m);
      if (rs != NULL)
         scope = cached_scope(rs->where, rs);
   }

   // The same node is shared by every instance of a module so the
   // arguments must be bound separately in each scope
   hash_t *cache = c->objcache;
   if (scope != NULL) {
      if (scope->calls == NULL)
         scope->calls = hash_new(16);
      cache = scope->calls;
   }

   c_vpiObject *obj = hash_get(cache, where);
   if (obj != NULL) {
      c_sysTfCall *call = is_sysTfCall(obj);
      assert(call != NULL);
      return call->handle;
   }

   for (int i = 0; i < c->systasks.count; i++) {
      c_vpiObject *obj = from_handle(c->systasks.items[i]);
      if (obj == NULL)
         continue;

      c_callback *cb = is_callback(obj);
      assert(cb != NULL);

      if (cb->name != name)
         continue;

      c_sysTfCall *call;
      if (cb->systf.type == vpiSysTask)
         call = &(build_sysTaskCall(where, cb, scope)->systfcall);
      else
         call = &(build_sysFuncCall(where, cb, scope)->systfcall);

      call->handle = internal_handle_for(&(call->tfcall.object));

      hash_put(cache, where, call);
      return call->handle;
   }

   return NULL;
}

const char *vpi_caller_name(void)
{
   vpi_context_t *c = vpi_context();
   assert(c->call != NULL);

   return c->call->callback->systf.tfname;
}

int64_t vpi_caller_timescale(void)
{
   // Number of femtoseconds in one time unit for the calling module

   vpi_context_t *c = vpi_context();
   assert(c->call != NULL);

   return MAX(VLOG_TIME_UNIT(vlog_time_resolve(vlog_ival(c->call->tfcall.where))), 1);
}

int64_t vpi_caller_precision(void)
{
   // Number of femtoseconds in one precision step for the calling module

   vpi_context_t *c = vpi_context();
   assert(c->call != NULL);

   int64_t result = 1;
   const unsigned log10 =
      VLOG_TIME_LOG10(vlog_time_resolve(vlog_ival(c->call->tfcall.where)));
   for (unsigned i = 0; i < log10; i++)
      result *= 10;

   return result;
}

int64_t vpi_caller_design_precision(void)
{
   // Number of femtoseconds in the smallest precision step in the design

   vpi_context_t *c = vpi_context();
   assert(c->call != NULL);

   const int64_t ival = vlog_time_resolve(vlog_ival(c->call->tfcall.where));
   const int design = VLOG_TIME_DESIGN(ival);
   return vlog_time_quantum(design >= 0 ? design : VLOG_TIME_LOG10(ival));
}

void vpi_return_string(const char *str, size_t len)
{
   vpi_context_t *c = vpi_context();
   assert(c->args != NULL);

   char *mem = jit_mspace_alloc(MAX(len, 1));
   memcpy(mem, str, len);

   c->args[0].pointer = mem;
   c->args[1].integer = 0;
   c->args[2].integer = len;
}

int vpi_string_length(vpiHandle handle)
{
   // Length of a string argument including any null characters or -1
   // if the argument is not a string variable

   c_vpiObject *obj = from_handle(handle);
   if (obj == NULL)
      return -1;

   vpi_context_t *c = vpi_context();
   c_operation *op = is_operation(obj);
   if (op == NULL || c->args == NULL || op->subtype == vpiNullOp
       || c->args[op->argslot].integer != -2)
      return -1;

   return c->args[op->argslot + 2].integer;
}

bool vpi_is_class_handle(vpiHandle handle)
{
   c_vpiObject *obj = from_handle(handle);
   if (obj == NULL)
      return false;

   vpi_context_t *c = vpi_context();
   c_operation *op = is_operation(obj);
   return op != NULL && c->args != NULL && op->subtype != vpiNullOp
      && c->args[op->argslot].integer == -5;
}

void vpi_abort_call(void)
{
   // The system task does not return when it aborts the process

   vpi_context_t *c = vpi_context();
   c->call = NULL;
   c->args = NULL;
   c->tlab = NULL;
}

static int spill_start(const jit_scalar_t *args, unsigned nslots)
{
   // The first slot passed in the memory pointed to by the last slot
   // rather than in its own, or -1 if there is no such slot

   int pslot = 1;
   while (pslot < nslots) {
      if (pslot + 1 >= JIT_MAX_ARGS - 1)
         return pslot;

      const int64_t tag = args[pslot++].integer;

      int parts[2] = { 2, 0 };
      if (tag <= 0) {
         parts[0] = 1;
         parts[1] = 1;
      }

      for (int i = 0; i < 2 && parts[i] > 0; i++) {
         if (pslot + parts[i] >= JIT_MAX_ARGS - 1)
            return pslot;

         pslot += parts[i];
      }
   }

   return -1;
}

void vpi_call_foreign(vpiHandle handle, jit_scalar_t *args, tlab_t *tlab)
{
   c_vpiObject *obj = from_handle(handle);
   if (obj == NULL)
      jit_msg(NULL, DIAG_FATAL, "called invalid system task");

   c_sysTfCall *call = is_sysTfCall(obj);
   assert(call != NULL);

   // A call with many arguments passes some of them in memory
   jit_scalar_t *flat = NULL, *orig_args = args;
   if (call->tfcall.nslots > JIT_MAX_ARGS - 1) {
      const int split = spill_start(args, call->tfcall.nslots);
      if (split >= 0) {
         const jit_scalar_t *spill = args[JIT_MAX_ARGS - 1].pointer;
         flat = xmalloc_array(call->tfcall.nslots, sizeof(jit_scalar_t));
         memcpy(flat, args, split * sizeof(jit_scalar_t));
         memcpy(flat + split, spill,
                (call->tfcall.nslots - split) * sizeof(jit_scalar_t));
         args = flat;
      }
   }

   void *orig_p0 = args[0].pointer;

   vpi_context_t *c = vpi_context();
   assert(c->call == NULL);
   c->call = call;
   c->args = args;
   c->tlab = tlab;

   (*call->callback->systf.calltf)(call->callback->systf.user_data);

   assert(c->call == call);
   c->call = NULL;
   c->args = NULL;
   c->tlab = NULL;

   if (flat != NULL) {
      memcpy(orig_args, flat, 3 * sizeof(jit_scalar_t));
      free(flat);
   }

   if (call->callback->systf.type == vpiSysFunc
       && orig_args[0].pointer == orig_p0)
      jit_msg(NULL, DIAG_FATAL, "system function %s did not return a value",
              call->callback->systf.tfname);
}

PLI_INT32 vpi_func_type(ident_t name)
{
   vpi_context_t *c = vpi_context();

   for (int i = 0; i < c->systasks.count; i++) {
      c_vpiObject *obj = from_handle(c->systasks.items[i]);
      if (obj == NULL)
         continue;

      c_callback *cb = is_callback(obj);
      assert(cb != NULL);

      if (cb->name == name)
         return cb->systf.sysfunctype;
   }

   return 0;
}

////////////////////////////////////////////////////////////////////////////////
// Access by hierarchical name for the UVM backdoor routines

typedef struct {
   rt_signal_t *signal;
   int          offset;
   int          count;
   bool         is_var;
} hdl_target_t;

static bool hdl_scope_name_equal(rt_scope_t *s, const char *name, size_t len)
{
   if (s->where == NULL || tree_kind(s->where) != T_BLOCK)
      return false;

   const char *id = istr(tree_ident(s->where));
   return strlen(id) == len && strncmp(id, name, len) == 0;
}

static c_abstractScope *hdl_child_scope(rt_scope_t *parent, const char *name,
                                        size_t len)
{
   for (int i = 0; i < parent->children.count; i++) {
      rt_scope_t *s = child_scope_at(parent, i);
      if (!hdl_scope_name_equal(s, name, len))
         continue;

      tree_t hier = tree_decl(s->where, 0);
      if (tree_kind(hier) != T_HIER || tree_kind(tree_ref(hier)) != T_VERILOG)
         continue;

      return cached_scope(s->where, s);
   }

   return NULL;
}

static rt_scope_t *hdl_child_instance(rt_scope_t *parent, const char *name,
                                      size_t len)
{
   for (int i = 0; i < parent->children.count; i++) {
      rt_scope_t *s = child_scope_at(parent, i);
      if (!hdl_scope_name_equal(s, name, len))
         continue;

      tree_t hier = tree_decl(s->where, 0);
      if (tree_kind(hier) == T_HIER && tree_kind(tree_ref(hier)) == T_VERILOG)
         return s;
   }

   return NULL;
}

static rt_scope_t *hdl_descend(rt_scope_t *s, const char *path)
{
   for (;;) {
      const char *end = strchr(path, '.');
      const size_t len = end != NULL ? end - path : strlen(path);
      if (len == 0 || (s = hdl_child_instance(s, path, len)) == NULL)
         return NULL;
      else if (end == NULL)
         return s;

      path = end + 1;
   }
}

static vlog_node_t hdl_scope_vlog(rt_scope_t *s)
{
   if (s->where == NULL || tree_kind(s->where) != T_BLOCK
       || tree_decls(s->where) == 0)
      return NULL;

   tree_t hier = tree_decl(s->where, 0);
   if (tree_kind(hier) != T_HIER || tree_kind(tree_ref(hier)) != T_VERILOG)
      return NULL;

   return tree_vlog(tree_ref(hier));
}

static bool hdl_scope_named(rt_scope_t *s, const char *name, size_t len)
{
   // The name of an instance or of its module (23.8)

   if (hdl_scope_name_equal(s, name, len))
      return true;

   vlog_node_t v = hdl_scope_vlog(s);
   if (v == NULL || vlog_kind(v) != V_INST_BODY
       || (vlog_flags(v) & VLOG_F_GEN_BODY))
      return false;

   const char *id = istr(vlog_ident2(v));
   return strlen(id) == len && strncmp(id, name, len) == 0;
}

bool vpi_instance_timescale(const char *path, text_buf_t *name,
                            int64_t *unit, int64_t *precision)
{
   // Find the module instance with a hierarchical name, searching upwards
   // from the calling scope (23.8), and give its time unit and precision
   // in femtoseconds

   vpi_context_t *c = vpi_context();
   rt_model_t *model = vpi_get_model(c);

   rt_scope_t *found = NULL;
   if (!strncmp(path, "$root.", 6))
      found = hdl_descend(root_scope(model), path + 6);

   const char *dot = strchr(path, '.');
   const size_t len = dot != NULL ? dot - path : strlen(path);

   rt_scope_t *s = get_active_scope(model) ?: root_scope(model);
   for (; s != NULL && found == NULL && *path != '$'; s = s->parent) {
      if ((found = hdl_descend(s, path)) == NULL && s->kind != SCOPE_ROOT
          && hdl_scope_named(s, path, len))
         found = dot != NULL ? hdl_descend(s, dot + 1) : s;
   }

   if (found == NULL)
      return false;

   get_verilog_path_name(found, name);

   // A generate block has the time unit of the module which contains it
   vlog_node_t v;
   for (; (v = hdl_scope_vlog(found)) != NULL; found = found->parent) {
      if (vlog_kind(v) == V_INST_BODY && !(vlog_flags(v) & VLOG_F_GEN_BODY))
         break;
   }

   const int64_t ival = vlog_time_resolve(v != NULL ? vlog_ival(v)
                                          : VLOG_TIME_ENCODE(1, VLOG_TIME_UNSCALED));
   *unit = MAX(VLOG_TIME_UNIT(ival), 1);
   *precision = vlog_time_quantum(VLOG_TIME_LOG10(ival));
   return true;
}

static c_abstractDecl *hdl_find_decl(c_abstractScope *scope, const char *name,
                                     size_t len)
{
   vpiObjectList *list = expand_lazy_list(&(scope->object), &(scope->decls));
   for (int i = 0; i < list->count; i++) {
      c_abstractDecl *decl = is_abstractDecl(list->items[i]);
      if (decl == NULL)
         continue;

      const char *id = istr(vlog_ident(decl->where));
      if (strlen(id) == len && strncmp(id, name, len) == 0)
         return decl;
   }

   return NULL;
}

static bool hdl_parse_select(const char **p, int64_t *hi, int64_t *lo)
{
   // [index] or [msb:lsb]

   char *end;
   *hi = strtoll(*p + 1, &end, 10);
   if (end == *p + 1)
      return false;

   *lo = *hi;
   if (*end == ':') {
      const char *start = end + 1;
      *lo = strtoll(start, &end, 10);
      if (end == start)
         return false;
   }

   if (*end != ']')
      return false;

   *p = end + 1;
   return true;
}

static void hdl_packed_bounds(vlog_node_t decl, int size, int64_t *left,
                              int64_t *right)
{
   *left = size - 1;
   *right = 0;

   vlog_node_t type = vlog_type(decl);
   if (vlog_kind(type) == V_DATA_TYPE && vlog_ranges(type) > 0) {
      int64_t l, r;
      if (vlog_bounds(vlog_range(type, 0), &l, &r))
         *left = l, *right = r;
   }
}

static bool hdl_select(hdl_target_t *t, vlog_node_t decl, int size,
                       int64_t hi, int64_t lo)
{
   int64_t left, right;
   hdl_packed_bounds(decl, size, &left, &right);

   const bool down = left >= right;
   const int64_t first = down ? left - hi : hi - left;
   const int64_t last = down ? left - lo : lo - left;

   if (first < 0 || last < first || last >= size)
      return false;

   t->offset += first;
   t->count = last - first + 1;
   return true;
}

static int hdl_read(hdl_target_t *t, svLogicVecVal *value)
{
   const uint8_t *vals = signal_value(t->signal);

   const int nwords = (t->count + 31) / 32;
   for (int i = 0; i < nwords; i++)
      value[i].aval = value[i].bval = 0;

   for (int i = 0; i < t->count; i++) {
      const int pos = t->count - 1 - i;
      const uint8_t v = vals[t->offset + i];
      value[pos / 32].aval |= (uint32_t)(v & 1) << (pos % 32);
      value[pos / 32].bval |= (uint32_t)((v >> 1) & 1) << (pos % 32);
   }

   return 1;
}

static int hdl_write(hdl_target_t *t, const svLogicVecVal *value, bool force)
{
   uint8_t *vals LOCAL = xmalloc(t->count);
   for (int i = 0; i < t->count; i++) {
      const int pos = t->count - 1 - i;
      const uint32_t a = (value[pos / 32].aval >> (pos % 32)) & 1;
      const uint32_t b = (value[pos / 32].bval >> (pos % 32)) & 1;
      vals[i] = a | (b << 1);
   }

   rt_model_t *model = vpi_get_model(vpi_context());
   if (force) {
      // The forced value of a variable is seen immediately
      if (t->is_var) {
         release_signal(model, t->signal, t->offset, t->count);
         deposit_signal(model, t->signal, vals, t->offset, t->count);
      }

      force_signal(model, t->signal, vals, t->offset, t->count);
   }
   else
      deposit_signal(model, t->signal, vals, t->offset, t->count);

   return 1;
}

static void hdl_release(hdl_target_t *t)
{
   rt_model_t *model = vpi_get_model(vpi_context());

   if (t->is_var) {
      // A variable keeps the forced value until the next assignment
      const uint8_t *cur = signal_value(t->signal);
      sched_deposit(model, t->signal, cur + t->offset, t->offset, t->count,
                    -1, true);
   }

   release_signal(model, t->signal, t->offset, t->count);
}

int uvm_hdl_check_path(const char *path)
{
   hdl_target_t t;
   return hdl_lookup(path, &t);
}

int uvm_hdl_read(const char *path, svLogicVecVal *value)
{
   hdl_target_t t;
   return hdl_lookup(path, &t) && hdl_read(&t, value);
}

int uvm_hdl_deposit(const char *path, const svLogicVecVal *value)
{
   hdl_target_t t;
   return hdl_lookup(path, &t) && hdl_write(&t, value, false);
}

int uvm_hdl_force(const char *path, const svLogicVecVal *value)
{
   hdl_target_t t;
   return hdl_lookup(path, &t) && hdl_write(&t, value, true);
}

int uvm_hdl_release(const char *path)
{
   hdl_target_t t;
   if (!hdl_lookup(path, &t))
      return 0;

   hdl_release(&t);
   return 1;
}

int uvm_hdl_release_and_read(const char *path, svLogicVecVal *value)
{
   hdl_target_t t;
   if (!hdl_lookup(path, &t))
      return 0;

   hdl_release(&t);
   return hdl_read(&t, value);
}
