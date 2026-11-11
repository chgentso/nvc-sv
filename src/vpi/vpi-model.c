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

#ifdef __MINGW32__
// This file defines the VPI functions rather than importing them
#define PLI_DLLISPEC
#endif

#include "util.h"
#include "array.h"
#include "common.h"
#include "conn.h"
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
#include "rt/assert.h"
#include "rt/fileio.h"
#include "rt/model.h"
#include "rt/mtprof.h"
#include "rt/mspace.h"
#include "rt/structs.h"
#include "tree.h"
#include "vlog/vlog-node.h"
#include "vlog/vlog-number.h"
#include "vlog/vlog-util.h"
#include "vpi/vpi-macros.h"
#include "vpi/vpi-model.h"
#include "vpi/vpi-priv.h"
#include "vpi/sv_vpi_user.h"
#include "vpi/vpi_ext_nvc.h"
#include "vpi/svdpi.h"

#include <assert.h>
#include <inttypes.h>
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifdef __MINGW32__
// Must be exported before first use
DLLEXPORT void vpi_get_value(vpiHandle handle, p_vpi_value value_p);
#endif

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
   s_vpi_systf_data systf;     // System task or function
   s_cb_data        data;      // Simulation callback
   s_vpi_time       time;
   s_vpi_value      value;
   vpiHandle        handle;    // Internal handle while registered
   vpiHandle        obj;       // Internal handle to the object
   vpiHandle        user;      // Handle given to the application
   rt_watch_t      *watch;
   rt_sampler_t    *sampler;  // Samples a value held in memory
   uint8_t         *snapshot; // Value of that object at the last sample
   size_t           snaplen;
   uint64_t         delay;
   PLI_INT32        format;    // Of the value passed to the callback
   bool             removed;
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
   vpiLazyList      procs;     // Initial, always and final procedures
   hash_t          *calls;     // System task calls made from this instance
   tree_t           block;
   rt_scope_t      *rtscope;
   c_abstractScope *parent;
   const char      *name;
   const char      *fullname;
   void            *frame;     // Context of a package
   bool             vhdl_name; // Named by a VHDL instance: not case sensitive
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

typedef enum {
   STORE_SIGNAL,     // Signal with one element per bit
   STORE_REAL,       // Signal of real values
   STORE_STRING,     // Variable holding a string
   STORE_VEC2,       // Two-state variable of up to 64 bits in a package
   STORE_VEC4,       // Four-state variable of up to 64 bits in a package
   STORE_PREAL,      // Real variable in a package
   STORE_RECORD,     // Unpacked structure held by reference
   STORE_FVEC2,      // Two-state member of an unpacked structure
   STORE_FVEC4,      // Four-state member of an unpacked structure
   STORE_FREAL,      // Real member of an unpacked structure
   STORE_FSTRING,    // String member of an unpacked structure
   STORE_MARRAY,     // Unpacked array held in memory
   STORE_NONE,
} storage_t;

typedef struct {
   uint32_t offset;
   bool     deref;
} vpi_step_t;

typedef struct {
   c_vpiObject      object;
   vlog_node_t      where;     // Declaration
   c_abstractScope *scope;
   UNSAFE_MPTR      mptr;
   c_vpiObject     *parent;    // Array, vector or aggregate containing this
   const char      *name;
   const char      *fullname;
   vlog_node_t      dtype;     // Data type of the object
   uint32_t         offset;    // Elements from the start of the storage
   uint32_t         size;      // Number of bits, or elements of an array
   int              dim;       // First unselected dimension
   int64_t          index;     // Index of an element or bit
   storage_t        storage;
   bool             is_net;
   vpiLazyList      elems;     // Elements, bits or members
   vlog_node_t      rnode;     // Node with the unpacked dimensions
   unsigned         npath;     // Steps to a value held in memory
   const vpi_step_t *path;
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

typedef struct {
   c_vpiObject      object;   // vpiInitial, vpiAlways or vpiFinal
   c_abstractScope *scope;
   vlog_node_t      where;
   const char      *name;
   const char      *fullname;
   vpiLazyList      decls;    // Variables declared in the procedure
} c_process;

typedef struct {
   c_vpiObject      object;
   c_abstractScope *scope;
   vlog_node_t      where;
   c_vpiObject     *low;      // Connection inside the instance
   const char      *name;
   int              index;
} c_port;

DEF_CLASS(port, vpiPort, object);

typedef struct {
   c_vpiObject      object;
   c_abstractScope *scope;
   vlog_node_t      where;
   const char      *name;
   const char      *fullname;
   int64_t          index;     // First index of an array of interface ports
   bool             is_array;
} c_misc;    // vpiRefObj, vpiModport and vpiGate

typedef struct {
   c_vpiObject  object;
   c_vpiObject *left;
   c_vpiObject *right;
} c_range;

typedef struct {
   c_vpiObject object;
   vlog_node_t dtype;     // Enumeration declaration
} c_typespec;      // vpiEnumTypespec

DEF_CLASS(typespec, vpiEnumTypespec, object);

typedef struct {
   c_vpiObject object;
   vlog_node_t where;     // Enumeration name
} c_enumConst;

DEF_CLASS(enumConst, vpiEnumConst, object);

DEF_CLASS(range, vpiRange, object);

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
static c_abstractScope *cached_vhdl_scope(rt_scope_t *s);
static bool vpi_vlog_below(rt_scope_t *s);
static ident_t vpi_scope_ident(rt_scope_t *s);
static bool vpi_is_hier_kind(rt_scope_t *s, tree_kind_t kind);
static c_abstractScope *parent_scope(c_abstractScope *scope);
static void vpi_lazy_decls(c_vpiObject *obj);
static void vpi_lazy_elems(c_vpiObject *obj);
static void vpi_lazy_scopes(c_vpiObject *obj);
static void vpi_lazy_misc(c_vpiObject *obj);
static void vpi_lazy_procs(c_vpiObject *obj);
static rt_proc_t *vpi_find_process(c_process *p);
static void vpi_lazy_proc_decls(c_vpiObject *obj);
static c_refcounted *is_refcounted(c_vpiObject *obj);
static void vpi_arm_callback(vpi_context_t *c, vpiHandle handle);
static c_abstractDecl *build_mem_value(c_abstractDecl *base, vlog_node_t type,
                                       storage_t storage, bool deref,
                                       uint32_t offset);
static c_abstractDecl *build_mem_array(c_abstractDecl *base, vlog_node_t rnode,
                                       vlog_node_t etype, int dim, bool deref,
                                       uint32_t offset);
static storage_t vpi_mem_layout(vlog_node_t type, unsigned *size);

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
   case vpiPackage:
   case vpiNamedBegin:
   case vpiNamedFork:
   case vpiForeignInst:
      return container_of(obj, c_abstractScope, object);
   default:
      return NULL;
   }
}

static c_process *is_process(c_vpiObject *obj)
{
   switch (obj->type) {
   case vpiInitial:
   case vpiAlways:
   case vpiFinal:
      return container_of(obj, c_process, object);
   default:
      return NULL;
   }
}

static c_abstractScope *is_instance(c_vpiObject *obj)
{
   switch (obj->type) {
   case vpiModule:
   case vpiInterface:
   case vpiProgram:
      return container_of(obj, c_abstractScope, object);
   default:
      return NULL;
   }
}

static c_scopeArray *is_scopeArray(c_vpiObject *obj)
{
   switch (obj->type) {
   case vpiGenScopeArray:
   case vpiInterfaceArray:
   case vpiModuleArray:
      return container_of(obj, c_scopeArray, object);
   default:
      return NULL;
   }
}

static c_misc *is_misc(c_vpiObject *obj)
{
   switch (obj->type) {
   case vpiRefObj:
   case vpiModport:
   case vpiGate:
      return container_of(obj, c_misc, object);
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
   scope->procs.fn   = vpi_lazy_procs;
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

static storage_t vpi_storage(c_abstractScope *scope, PLI_INT32 type,
                             vlog_node_t dtype, bool is_net)
{
   const bool in_package = scope != NULL && scope->object.type == vpiPackage;

   if (is_net)
      return in_package ? STORE_NONE : STORE_SIGNAL;

   switch (type) {
   case vpiStringVar:
      return STORE_STRING;
   case vpiRealVar:
   case vpiShortRealVar:
      return in_package ? STORE_PREAL : STORE_REAL;
   case vpiNamedEvent:
   case vpiChandleVar:
   case 0:
      return STORE_NONE;
   default:
      if (vlog_kind(dtype) == V_STRUCT_DECL
          && !(vlog_flags(dtype) & VLOG_F_PACKED))
         return STORE_RECORD;
      else if (!vpi_is_packed_type(dtype))
         return STORE_NONE;
      else if (!in_package)
         return STORE_SIGNAL;
      else if (vlog_is_two_state(dtype))
         return STORE_VEC2;
      else
         return STORE_VEC4;
   }
}

static c_abstractDecl *build_value(vlog_node_t where, c_abstractScope *scope,
                                   vlog_node_t dtype, bool is_net)
{
   PLI_INT32 type = is_net ? vpiNet : vpi_data_type(dtype);
   if (type == 0)
      return NULL;

   c_abstractDecl *decl = new_object(sizeof(c_reg), type);
   init_abstractDecl(decl, where, scope);
   decl->dtype   = dtype;
   decl->is_net  = is_net;
   decl->storage = vpi_storage(scope, type, dtype, is_net);
   decl->elems.fn = vpi_lazy_elems;

   switch (decl->storage) {
   case STORE_REAL:
   case STORE_PREAL:
   case STORE_STRING:
   case STORE_RECORD:
   case STORE_NONE:
      decl->size = 1;
      break;
   default:
      decl->size = vlog_size(dtype);
      break;
   }

   return decl;
}

static void vpi_array_bounds(c_regArray *arr, int64_t *left, int64_t *right)
{
   vlog_node_t d = vlog_range(arr->decl.rnode ?: arr->decl.where,
                              arr->decl.dim);
   if (!vlog_bounds(d, left, right))
      *left = *right = 0;
}

static c_abstractDecl *build_array(vlog_node_t where, c_abstractScope *scope,
                                   int dim, uint32_t offset, c_vpiObject *parent)
{
   const bool is_net = vlog_kind(where) == V_NET_DECL;
   vlog_node_t dtype = vlog_get_type(where);

   c_regArray *arr =
      new_object(sizeof(c_regArray), is_net ? vpiNetArray : vpiRegArray);
   init_abstractDecl(&(arr->decl), where, scope);
   arr->decl.dtype    = dtype;
   arr->decl.is_net   = is_net;
   arr->decl.dim      = dim;
   arr->decl.offset   = offset;
   arr->decl.parent   = parent;
   arr->decl.elems.fn = vpi_lazy_elems;

   const PLI_INT32 etype = is_net ? vpiNet : vpi_data_type(dtype);
   arr->decl.storage = vpi_storage(scope, etype, dtype, is_net);
   if (arr->decl.storage == STORE_RECORD) {
      // An array of unpacked structures is an array of references to
      // them in the frame of the scope
      arr->decl.storage = STORE_MARRAY;
   }
   else if (arr->decl.storage != STORE_SIGNAL
            && arr->decl.storage != STORE_REAL)
      arr->decl.storage = STORE_NONE;

   int64_t left, right;
   vpi_array_bounds(arr, &left, &right);
   arr->count = left > right ? left - right + 1 : right - left + 1;

   uint32_t elemsz = arr->decl.storage == STORE_REAL ? 1
      : arr->decl.storage == STORE_MARRAY ? sizeof(void *) : vlog_size(dtype);
   const int nranges = vlog_ranges(where);
   for (int i = dim + 1; i < nranges; i++)
      elemsz *= vlog_size(vlog_range(where, i));
   arr->elemsz = elemsz;

   arr->decl.size = arr->count;

   arr->left  = &(build_int_constant(left)->expr.object);
   arr->right = &(build_int_constant(right)->expr.object);

   return &(arr->decl);
}

static void build_decl(vlog_node_t v, c_abstractScope *scope)
{
   vlog_node_t type = vlog_type(v);

   c_abstractDecl *decl;
   if (vlog_ranges(v) > 0)
      decl = build_array(v, scope, 0, 0, NULL);
   else if (vlog_kind(type) == V_DATA_TYPE && vlog_subkind(type) == DT_DYNARRAY
            && vlog_ranges(type) == 1 && vlog_has_type(type)
            && vlog_subkind(vlog_range(type, 0)) == V_DIM_UNPACKED) {
      // A fixed size array of a type name is held as the elements of a
      // dynamic array
      c_abstractDecl base = { .where = v, .scope = scope };
      decl = build_mem_array(&base, type, vlog_get_type(vlog_type(type)), 0,
                             true, 0);
      if (decl != NULL)
         decl->parent = NULL;
   }
   else
      decl = build_value(v, scope, vlog_get_type(v),
                         vlog_kind(v) == V_NET_DECL);

   if (decl != NULL)
      vpi_list_push(&scope->decls.list, &(decl->object));
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

static tree_t vpi_vhdl_block(tree_t block)
{
   // The block of a VHDL entity instantiated from Verilog is the only
   // instance in the module which wraps it

   const int nstmts = tree_stmts(block);
   for (int i = 0; i < nstmts; i++) {
      tree_t s = tree_stmt(block, i);
      if (tree_kind(s) == T_BLOCK && tree_decls(s) > 0
          && tree_kind(tree_decl(s, 0)) == T_HIER
          && tree_subkind(tree_decl(s, 0)) == T_ARCH)
         return s;
   }

   return NULL;
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
         if (vpi_vhdl_block(block) != NULL)
            as = build_scope(vpiForeignInst, v, block, s);
         else if (vlog_flags(v) & VLOG_F_GEN_BODY)
            as = build_scope(vpiGenScope, v, block, s);
         else if (vlog_flags(v) & VLOG_F_INTERFACE)
            as = build_scope(vpiInterface, v, block, s);
         else
            as = build_scope(vpiModule, v, block, s);
         break;
      case V_GEN_BLOCK:
         as = build_scope(vpiGenScope, v, block, s);
         break;
      default:
         should_not_reach_here();
      }

      as->name = vpi_name_str(istr(vpi_scope_ident(s)), true);
      as->vhdl_name = s->parent != NULL
         && vpi_is_hier_kind(s->parent, T_COMPONENT);

      hash_put(cache, block, as);
   }

   return as;
}

static bool vpi_is_hier_kind(rt_scope_t *s, tree_kind_t kind)
{
   if (s->kind != SCOPE_INSTANCE || s->where == NULL
       || tree_kind(s->where) != T_BLOCK || tree_decls(s->where) == 0)
      return false;

   tree_t hier = tree_decl(s->where, 0);
   return tree_kind(hier) == T_HIER && tree_kind(tree_ref(hier)) == kind;
}

static bool vpi_is_hidden(rt_scope_t *s)
{
   // The block for a component has the instance of the entity or module
   // bound to it as its only child: the instance takes the name of the
   // component and the block itself is not visible.  The same goes for a
   // VHDL entity instantiated from Verilog which is seen as the Verilog
   // instance that wraps it.

   if (vpi_is_hier_kind(s, T_COMPONENT))
      return true;

   rt_scope_t *p = s->parent;
   return p != NULL && vpi_tree_vlog(p) != NULL
      && vpi_vhdl_block(p->where) == s->where;
}

static rt_scope_t *vpi_logical_parent(rt_scope_t *s)
{
   rt_scope_t *p = s->parent;
   while (p != NULL && p->kind == SCOPE_INSTANCE && vpi_is_hidden(p))
      p = p->parent;
   return p;
}

static ident_t vpi_scope_ident(rt_scope_t *s)
{
   if (s->parent != NULL && vpi_is_hier_kind(s->parent, T_COMPONENT))
      return tree_ident(s->parent->where);
   else
      return tree_ident(s->where);
}

static bool vpi_vlog_below(rt_scope_t *s)
{
   // Whether there is a Verilog instance somewhere below a VHDL scope

   for (int i = 0; i < s->children.count; i++) {
      rt_scope_t *c = child_scope_at(s, i);
      if (c->kind != SCOPE_INSTANCE)
         continue;
      else if (vpi_tree_vlog(c) != NULL || vpi_vlog_below(c))
         return true;
   }

   return false;
}

static c_abstractScope *cached_vhdl_scope(rt_scope_t *s)
{
   // A VHDL scope seen from VPI: it has a name and Verilog instances
   // below it but its contents are only visible through VHPI

   tree_t block = s->where;
   assert(tree_kind(block) == T_BLOCK);

   hash_t *cache = vpi_context()->objcache;
   c_abstractScope *as = hash_get(cache, block);
   if (as == NULL) {
      as = new_object(sizeof(c_module), vpiForeignInst);
      as->object.loc = *tree_loc(block);
      as->handle     = JIT_HANDLE_INVALID;
      as->decls.fn   = vpi_lazy_decls;
      as->scopes.fn  = vpi_lazy_scopes;
      as->misc.fn    = vpi_lazy_misc;
      as->procs.fn   = vpi_lazy_procs;
      as->block      = block;
      as->rtscope    = s;
      as->name       = vpi_name_str(istr(vpi_scope_ident(s)), true);
      as->vhdl_name  = true;

      hash_put(cache, block, as);
   }

   return as;
}

static c_abstractScope *parent_scope(c_abstractScope *scope)
{
   if (scope->parent != NULL || scope->rtscope == NULL)
      return scope->parent;

   rt_scope_t *parent = vpi_logical_parent(scope->rtscope);
   if (parent == NULL || parent->kind != SCOPE_INSTANCE
       || parent->where == NULL || tree_kind(parent->where) != T_BLOCK)
      return NULL;
   else if (vpi_tree_vlog(parent) != NULL)
      return (scope->parent = cached_scope(parent->where, parent));
   else
      return (scope->parent = cached_vhdl_scope(parent));
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

   c_abstractScope *s = storage_scope(decl->scope);
   void *ctx = NULL;

   if (s != NULL && s->object.type == vpiPackage) {
      if (s->handle == JIT_HANDLE_INVALID)
         s->handle = jit_lazy_compile(jit, vlog_ident(s->where));
      if (s->frame == NULL && s->handle != JIT_HANDLE_INVALID)
         s->frame = jit_link(jit, s->handle);
      ctx = s->frame;
   }
   else if (s != NULL && s->rtscope != NULL) {
      if (s->handle == JIT_HANDLE_INVALID) {
         tree_t hier = tree_decl(s->rtscope->where, 0);
         assert(tree_kind(hier) == T_HIER);

         s->handle = jit_lazy_compile(jit, tree_ident2(hier));
      }

      if (s->rtscope->privdata != MPTR_INVALID)
         ctx = *mptr_get(s->rtscope->privdata);
   }

   ident_t name = vlog_ident(decl->where);

   if (s == NULL || s->handle == JIT_HANDLE_INVALID || ctx == NULL) {
      vpi_error(vpiError, &(decl->object.loc), "cannot get the storage of "
                "%s", istr(name));
      return NULL;
   }

   return (decl->mptr = jit_get_frame_var(jit, s->handle, ctx, name));
}

static void vpi_find_named_blocks(vlog_node_t v, vpiObjectList *list)
{
   // Named sequential and parallel blocks in a statement

   switch (vlog_kind(v)) {
   case V_SEQ_BLOCK:
   case V_FORK:
      if (vlog_has_ident(v)) {
         // Blocks nested in this one belong to its scope
         vpi_list_push(list, (c_vpiObject *)v);
         break;
      }
      // Fall-through
   case V_INITIAL:
   case V_ALWAYS:
   case V_FINAL:
   case V_TIMING:
   case V_COND:
   case V_FOREVER:
   case V_REPEAT:
   case V_WHILE:
   case V_DO_WHILE:
   case V_WAIT:
   case V_FOR_LOOP:
   case V_CASE:
   case V_CASE_ITEM:
   case V_RANDCASE:
   case V_RANDCASE_ITEM:
      {
         const int nstmts = vlog_stmts(v);
         for (int i = 0; i < nstmts; i++)
            vpi_find_named_blocks(vlog_stmt(v, i), list);
      }
      break;
   case V_IF:
      {
         const int nconds = vlog_conds(v);
         for (int i = 0; i < nconds; i++)
            vpi_find_named_blocks(vlog_cond(v, i), list);
      }
      break;
   default:
      break;
   }
}

static bool vpi_block_owns(vlog_node_t block, const loc_t *loc)
{
   // The declarations of a block come before its statements

   const loc_t *start = vlog_loc(block);
   if (start->file_ref != loc->file_ref)
      return false;
   else if (loc->first_line < start->first_line
            || (loc->first_line == start->first_line
                && loc->first_column < start->first_column))
      return false;
   else if (vlog_stmts(block) == 0)
      return true;

   const loc_t *first = vlog_loc(vlog_stmt(block, 0));
   return loc->first_line < first->first_line
      || (loc->first_line == first->first_line
          && loc->first_column < first->first_column);
}

static void vpi_collect_blocks(c_abstractScope *s, vlog_node_t container,
                               vlog_node_t outer, vpiObjectList *out)
{
   // Named blocks of the processes in container or in the block outer
   // which are not nested in another named block

   vpiObjectList blocks = {};
   if (outer != NULL) {
      const int nstmts = vlog_stmts(outer);
      for (int i = 0; i < nstmts; i++)
         vpi_find_named_blocks(vlog_stmt(outer, i), &blocks);
   }
   else {
      const int nstmts = vlog_stmts(container);
      for (int i = 0; i < nstmts; i++) {
         vlog_node_t st = vlog_stmt(container, i);
         switch (vlog_kind(st)) {
         case V_INITIAL:
         case V_ALWAYS:
         case V_FINAL:
            vpi_find_named_blocks(st, &blocks);
            break;
         default:
            break;
         }
      }
   }

   for (int i = 0; i < blocks.count; i++) {
      vlog_node_t b = (vlog_node_t)blocks.items[i];

      PLI_INT32 type = vlog_kind(b) == V_FORK ? vpiNamedFork : vpiNamedBegin;
      c_abstractScope *nb = new_object(sizeof(c_module), type);
      init_abstractScope(nb, b);
      nb->parent = s;
      nb->name = vpi_name_str(istr(vlog_ident(b)), false);

      vpi_list_push(out, &(nb->object));
   }
}

static void vpi_named_block_decls(c_abstractScope *s)
{
   // The static variables of a named block are declared in the
   // enclosing instance with a unique name

   c_abstractScope *home = storage_scope(s);
   if (home == NULL)
      return;

   const int ndecls = vlog_decls(home->where);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(home->where, i);
      if (vlog_kind(d) != V_VAR_DECL || !(vlog_flags(d) & VLOG_F_STATIC)
          || strstr(istr(vlog_ident(d)), "$static") == NULL)
         continue;
      else if (vpi_block_owns(s->where, vlog_loc(d)))
         build_decl(d, s);
   }
}

static void vpi_lazy_decls(c_vpiObject *obj)
{
   c_abstractScope *s = is_abstractScope(obj);
   assert(s != NULL);

   if (obj->type == vpiNamedBegin || obj->type == vpiNamedFork) {
      vpi_named_block_decls(s);
      return;
   }
   else if (obj->type == vpiForeignInst)
      return;   // Only visible through VHPI

   const int ndecls = vlog_decls(s->where);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t v = vlog_decl(s->where, i);
      switch (vlog_kind(v)) {
      case V_NET_DECL:
         build_decl(v, s);
         break;
      case V_VAR_DECL:
         if (vlog_flags(v) & VLOG_F_NESTED_INST)
            break;    // Interface instance
         else if ((vlog_flags(v) & VLOG_F_STATIC)
                  && strstr(istr(vlog_ident(v)), "$static") != NULL)
            break;    // Static variable of a block or subroutine
         build_decl(v, s);
         break;
      case V_LOCALPARAM:
      case V_PARAM_DECL:
         if (!(vlog_flags(v) & VLOG_F_SPECPARAM) || vlog_kind(v) != V_LOCALPARAM)
            build_parameter(v, s);
         break;
      default:
         break;
      }
   }
}

static bool vpi_is_assertion_proc(vlog_node_t v)
{
   if (vlog_kind(v) != V_ALWAYS || vlog_stmts(v) != 1
       || vlog_kind(vlog_stmt(v, 0)) != V_TIMING)
      return false;

   vlog_node_t timing = vlog_stmt(v, 0);
   if (vlog_stmts(timing) != 1)
      return false;

   vlog_node_t st = vlog_stmt(timing, 0);
   switch (vlog_kind(st)) {
   case V_ASSERT:
   case V_ASSUME:
   case V_COVER:
      return vlog_kind(vlog_value(st)) == V_PROP_SPEC;
   default:
      return false;
   }
}

static void vpi_lazy_procs(c_vpiObject *obj)
{
   c_abstractScope *s = is_abstractScope(obj);
   assert(s != NULL);

   switch (obj->type) {
   case vpiModule:
   case vpiInterface:
   case vpiProgram:
   case vpiGenScope:
      break;
   default:
      return;
   }

   const int nstmts = vlog_stmts(s->where);
   for (int i = 0; i < nstmts; i++) {
      vlog_node_t v = vlog_stmt(s->where, i);

      PLI_INT32 type;
      switch (vlog_kind(v)) {
      case V_INITIAL: type = vpiInitial; break;
      case V_ALWAYS:  type = vpiAlways; break;
      case V_FINAL:   type = vpiFinal; break;
      default:
         continue;
      }

      // Processes made by the compiler for assertions and reports
      if (!vlog_has_ident(v) || istr(vlog_ident(v))[0] == '$'
          || vpi_is_assertion_proc(v))
         continue;

      c_process *p = new_object(sizeof(c_process), type);
      p->object.loc = *vlog_loc(v);
      p->scope = s;
      p->where = v;
      p->name  = vpi_intern(istr(vlog_ident(v)));
      p->decls.fn = vpi_lazy_proc_decls;

      vpi_list_push(&(s->procs.list), &(p->object));
   }
}

static vlog_node_t vpi_proc_block(vlog_node_t v)
{
   // The unnamed block which holds the declarations of a process

   for (int i = 0; i < vlog_stmts(v); i++) {
      vlog_node_t st = vlog_stmt(v, i);
      switch (vlog_kind(st)) {
      case V_SEQ_BLOCK:
         return vlog_has_ident(st) ? NULL : st;
      case V_TIMING:
      case V_FOREVER:
         return vpi_proc_block(st);
      default:
         break;
      }
   }

   return NULL;
}

static void vpi_lazy_proc_decls(c_vpiObject *obj)
{
   // The static variables of the block of a process are declared in the
   // enclosing instance with a unique name

   c_process *p = is_process(obj);
   assert(p != NULL);

   vlog_node_t block = vpi_proc_block(p->where);
   c_abstractScope *home = storage_scope(p->scope);
   if (block == NULL || home == NULL)
      return;

   vpiObjectList *list = &(home->decls.list);

   const int ndecls = vlog_decls(home->where);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(home->where, i);
      if (vlog_kind(d) != V_VAR_DECL || !(vlog_flags(d) & VLOG_F_STATIC)
          || strstr(istr(vlog_ident(d)), "$static") == NULL
          || !vpi_block_owns(block, vlog_loc(d)))
         continue;

      // The declaration is only a member of the procedure
      const unsigned before = list->count;
      build_decl(d, p->scope);
      while (list->count > before)
         vpi_list_push(&(p->decls.list), list->items[--list->count]);
   }
}

static bool vpi_split_index(const char *name, size_t *len, int64_t *index)
{
   // Name of an element of an array of instances or generate blocks

   const size_t n = strlen(name);
   if (n < 4 || name[n - 1] != ']')
      return false;

   const char *open = strrchr(name, '[');
   if (open == NULL || open == name)
      return false;

   char *end;
   *index = strtoll(open + 1, &end, 10);
   if (end == open + 1 || *end != ']')
      return false;

   *len = open - name;
   return true;
}

static c_scopeArray *vpi_scope_array(vpiObjectList *list, PLI_INT32 type,
                                     c_abstractScope *parent, const char *name,
                                     size_t len)
{
   for (int i = 0; i < list->count; i++) {
      c_scopeArray *sa = is_scopeArray(list->items[i]);
      if (sa != NULL && sa->object.type == type
          && strncmp(sa->name, name, len) == 0 && sa->name[len] == '\0')
         return sa;
   }

   c_scopeArray *sa = new_object(sizeof(c_scopeArray), type);
   sa->parent = parent;

   char *copy = pool_malloc(vpi_context()->pool, len + 1);
   memcpy(copy, name, len);
   copy[len] = '\0';
   sa->name = copy;

   vpi_list_push(list, &(sa->object));
   return sa;
}

static void vpi_scope_children(c_abstractScope *s, rt_scope_t *from,
                               vpiObjectList *list)
{
   for (int i = 0; i < from->children.count; i++) {
      rt_scope_t *child = child_scope_at(from, i);
      if (child->kind != SCOPE_INSTANCE)
         continue;
      else if (vpi_is_hidden(child)) {
         vpi_scope_children(s, child, list);
         continue;
      }

      // The contents of a VHDL scope are only visible through VHPI but
      // the Verilog instances below it are visible here
      c_abstractScope *cs;
      if (vpi_tree_vlog(child) != NULL)
         cs = cached_scope(child->where, child);
      else if (vpi_vlog_below(child))
         cs = cached_vhdl_scope(child);
      else
         continue;

      cs->parent = s;

      size_t len;
      int64_t index;
      if (cs->object.type != vpiForeignInst
          && vpi_split_index(istr(vpi_scope_ident(child)), &len, &index)) {
         PLI_INT32 atype = vpiModuleArray;
         if (cs->object.type == vpiGenScope)
            atype = vpiGenScopeArray;
         else if (cs->object.type == vpiInterface)
            atype = vpiInterfaceArray;

         c_scopeArray *sa = vpi_scope_array(list, atype, s, cs->name, len);
         vpi_list_push(&(sa->elems), &(cs->object));
      }
      else
         vpi_list_push(list, &(cs->object));
   }
}

static void vpi_lazy_scopes(c_vpiObject *obj)
{
   c_abstractScope *s = is_abstractScope(obj);
   assert(s != NULL);

   vpiObjectList *list = &(s->scopes.list);

   if (s->rtscope != NULL)
      vpi_scope_children(s, s->rtscope, list);

   switch (obj->type) {
   case vpiModule:
   case vpiInterface:
   case vpiProgram:
   case vpiGenScope:
      vpi_collect_blocks(s, s->where, NULL, list);
      break;
   case vpiNamedBegin:
   case vpiNamedFork:
      vpi_collect_blocks(s, NULL, s->where, list);
      break;
   default:
      break;
   }
}

static void vpi_lazy_misc(c_vpiObject *obj)
{
   c_abstractScope *s = is_abstractScope(obj);
   assert(s != NULL);

   if (is_instance(obj) == NULL)
      return;

   int index = 0;
   const int ndecls = vlog_decls(s->where);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(s->where, i);
      switch (vlog_kind(d)) {
      case V_PORT_DECL:
         {
            c_port *p = new_object(sizeof(c_port), vpiPort);
            p->object.loc = *vlog_loc(d);
            p->scope = s;
            p->where = d;
            p->index = index++;
            p->name  = vpi_name_str(istr(vlog_ident(d)), false);

            vpiObjectList *decls = expand_lazy_list(obj, &(s->decls));
            for (int j = 0; j < decls->count; j++) {
               c_abstractDecl *ld = is_abstractDecl(decls->items[j]);
               if (ld->where == vlog_ref(d)) {
                  p->low = &(ld->object);
                  break;
               }
            }

            vpi_list_push(&(s->misc.list), &(p->object));
         }
         break;
      case V_IFACE_PORT:
         {
            index++;

            int64_t left, right;
            if (vlog_ranges(d) == 0
                || !vlog_bounds(vlog_range(d, 0), &left, &right)) {
               c_misc *m = new_object(sizeof(c_misc), vpiRefObj);
               m->object.loc = *vlog_loc(d);
               m->scope = s;
               m->where = d;
               m->name  = vpi_name_str(istr(vlog_ident(d)), false);

               vpi_list_push(&(s->misc.list), &(m->object));
               break;
            }

            // An array of interface ports refers to an array of
            // interface instances
            c_misc *m = new_object(sizeof(c_misc), vpiRefObj);
            m->object.loc = *vlog_loc(d);
            m->scope   = s;
            m->where   = d;
            m->is_array = true;
            m->index   = left;
            m->name    = vpi_name_str(istr(vlog_ident(d)), false);

            vpi_list_push(&(s->misc.list), &(m->object));
         }
         break;
      case V_MODPORT:
         {
            c_misc *m = new_object(sizeof(c_misc), vpiModport);
            m->object.loc = *vlog_loc(d);
            m->scope = s;
            m->where = d;
            m->name  = vpi_name_str(istr(vlog_ident(d)), false);

            vpi_list_push(&(s->misc.list), &(m->object));
         }
         break;
      default:
         break;
      }
   }

   // Named gate instances (28.3)
   const int nstmts = vlog_stmts(s->where);
   for (int i = 0; i < nstmts; i++) {
      vlog_node_t st = vlog_stmt(s->where, i);
      if (vlog_kind(st) != V_GATE_INST || !vlog_has_ident(st)
          || strchr(istr(vlog_ident(st)), '#') != NULL)
         continue;

      c_misc *m = new_object(sizeof(c_misc), vpiGate);
      m->object.loc = *vlog_loc(st);
      m->scope = s;
      m->where = st;
      m->name  = vpi_name_str(istr(vlog_ident(st)), false);

      vpi_list_push(&(s->misc.list), &(m->object));
   }
}

static vlog_node_t vpi_packed_dim(c_abstractDecl *decl, int dim)
{
   // Packed dimension of a vector which is not yet selected

   vlog_node_t t = decl->dtype;
   if (t == NULL || vlog_kind(t) != V_DATA_TYPE || dim >= vlog_ranges(t))
      return NULL;

   return vlog_range(t, dim);
}

static bool vpi_vector_bounds(c_abstractDecl *decl, int64_t *left,
                              int64_t *right)
{
   vlog_node_t d = vpi_packed_dim(decl, decl->dim);
   if (d != NULL)
      return vlog_bounds(d, left, right);
   else if (decl->dim == 0 && decl->size > 1
            && (decl->storage == STORE_SIGNAL || decl->storage == STORE_VEC2
                || decl->storage == STORE_VEC4 || decl->storage == STORE_FVEC2
                || decl->storage == STORE_FVEC4)
            && (decl->dtype == NULL || vlog_kind(decl->dtype) == V_DATA_TYPE
                || vlog_kind(decl->dtype) == V_ENUM_DECL)) {
      // Integer atom types and enumerations: a structure is selected by
      // its members (37.18)
      *left = decl->size - 1;
      *right = 0;
      return true;
   }

   return false;
}

static c_abstractDecl *build_select(c_abstractDecl *base, int64_t index)
{
   // Element of an unpacked array or bit or element of a packed vector

   c_regArray *arr = is_regArray(&(base->object));
   if (arr != NULL) {
      int64_t left, right;
      vpi_array_bounds(arr, &left, &right);

      const int64_t pos = left > right ? left - index : index - left;
      if (pos < 0 || pos >= arr->count)
         return NULL;

      const uint32_t offset = base->offset + pos * arr->elemsz;

      if (base->storage == STORE_MARRAY) {
         vlog_node_t rnode = base->rnode ?: base->where;
         c_abstractDecl *e;
         if (base->dim + 1 < vlog_ranges(rnode))
            e = build_mem_array(base, rnode, base->dtype, base->dim + 1,
                                false, pos * arr->elemsz);
         else {
            unsigned size;
            const storage_t storage = vpi_mem_layout(base->dtype, &size);
            if (storage == STORE_NONE)
               return NULL;

            e = build_mem_value(base, base->dtype, storage, false,
                                pos * arr->elemsz);
         }

         if (e != NULL)
            e->index = index;
         return e;
      }

      c_abstractDecl *e;
      if (base->dim + 1 < vlog_ranges(base->where))
         e = build_array(base->where, base->scope, base->dim + 1, offset,
                         &(base->object));
      else if ((e = build_value(base->where, base->scope, base->dtype,
                                base->is_net)) == NULL)
         return NULL;
      else {
         e->offset = offset;
         e->parent = &(base->object);
         if (base->storage == STORE_NONE)
            e->storage = STORE_NONE;
      }

      e->index = index;
      return e;
   }

   int64_t left, right;
   if (!vpi_vector_bounds(base, &left, &right))
      return NULL;

   const int64_t count = left > right ? left - right + 1 : right - left + 1;
   const int64_t pos = left > right ? left - index : index - left;
   if (pos < 0 || pos >= count || base->size % count != 0)
      return NULL;

   const uint32_t esize = base->size / count;

   c_abstractDecl *e = new_object(sizeof(c_reg), vpiReg);
   init_abstractDecl(e, base->where, base->scope);
   e->dtype   = base->dtype;
   e->is_net  = base->is_net;
   e->storage = base->storage;
   e->parent  = &(base->object);
   e->offset  = base->offset + pos * esize;
   e->size    = esize;
   e->dim     = base->dim + 1;
   e->index   = index;
   e->elems.fn = vpi_lazy_elems;

   if (vpi_packed_dim(base, e->dim) == NULL && esize == 1)
      e->object.type = base->is_net ? vpiNetBit : vpiRegBit;
   else if (base->is_net)
      e->object.type = vpiNet;
   else if (vpi_packed_dim(base, e->dim) == NULL && base->dtype != NULL
            && vlog_kind(base->dtype) == V_DATA_TYPE && vlog_has_type(base->dtype))
      e->dtype = vlog_get_type(vlog_type(base->dtype));   // Packed structure
   else
      e->object.type = vpi_data_type(base->dtype) ?: vpiReg;

   if (e->dtype != base->dtype)
      e->object.type = vpi_data_type(e->dtype) ?: vpiReg;

   return e;
}

static c_abstractDecl *build_member(c_abstractDecl *base, vlog_node_t member)
{
   vlog_node_t st = base->dtype;
   vlog_node_t mtype = vlog_get_type(member);
   if (mtype == NULL)
      return NULL;

   c_abstractDecl *e = build_value(base->where, base->scope, mtype,
                                   base->is_net);
   if (e == NULL)
      return NULL;

   e->parent  = &(base->object);
   e->offset  = base->offset + vlog_packed_offset(st, member);
   e->storage = base->storage;
   e->name    = vpi_name_str(istr(vlog_ident(member)), false);
   e->object.loc = *vlog_loc(member);

   if (base->is_net)
      e->object.type = vpiNet;

   return e;
}

static storage_t vpi_mem_layout(vlog_node_t type, unsigned *size)
{
   // How a value of this type is held in memory: an integral value as its
   // value bits followed by its unknown bits for a four-state type, each
   // in 64 bits or as a pointer to the words of a wider value, a real as a
   // double, a string as a pointer and its bounds and an unpacked
   // structure by reference; every field is aligned to eight bytes

   switch (vpi_data_type(type)) {
   case vpiRealVar:
   case vpiShortRealVar:
      *size = sizeof(double);
      return STORE_FREAL;
   case vpiStringVar:
      *size = sizeof(void *) + 2 * sizeof(int64_t);
      return STORE_FSTRING;
   case vpiNamedEvent:
   case vpiChandleVar:
      *size = sizeof(void *);
      return STORE_NONE;
   case vpiStructVar:
   case vpiUnionVar:
      if (!vpi_is_packed_type(type)) {
         *size = sizeof(void *);
         return vlog_kind(type) == V_STRUCT_DECL ? STORE_RECORD : STORE_NONE;
      }
      // Fall-through
   default:
      if (vpi_data_type(type) == 0 || !vpi_is_packed_type(type)) {
         *size = 0;
         return STORE_NONE;
      }
      else if (vlog_is_two_state(type)) {
         *size = sizeof(uint64_t);
         return STORE_FVEC2;
      }
      else {
         *size = 2 * sizeof(uint64_t);
         return STORE_FVEC4;
      }
   }
}

static const vpi_step_t *vpi_path_add(const c_abstractDecl *base, bool deref,
                                      uint32_t offset)
{
   vpi_step_t *path = pool_malloc_array(vpi_context()->pool, base->npath + 1,
                                        sizeof(vpi_step_t));
   if (base->npath > 0)
      memcpy(path, base->path, base->npath * sizeof(vpi_step_t));
   path[base->npath].offset = offset;
   path[base->npath].deref  = deref;
   return path;
}

static c_abstractDecl *build_mem_value(c_abstractDecl *base, vlog_node_t type,
                                       storage_t storage, bool deref,
                                       uint32_t offset)
{
   // A member or element held in memory at the end of the path of base
   // followed by one more step

   c_abstractDecl *e = build_value(base->where, base->scope, type, false);
   if (e == NULL)
      return NULL;

   e->parent  = &(base->object);
   e->storage = storage;
   e->size    = storage == STORE_FVEC2 || storage == STORE_FVEC4
      ? vlog_size(type) : 1;
   e->path    = vpi_path_add(base, deref, offset);
   e->npath   = base->npath + 1;

   return e;
}

static c_abstractDecl *build_mem_array(c_abstractDecl *base, vlog_node_t rnode,
                                       vlog_node_t etype, int dim, bool deref,
                                       uint32_t offset)
{
   // An unpacked array whose elements are held in memory one after the
   // other

   unsigned esize;
   vpi_mem_layout(etype, &esize);
   if (esize == 0)
      return NULL;

   c_regArray *arr = new_object(sizeof(c_regArray), vpiRegArray);
   init_abstractDecl(&(arr->decl), base->where, base->scope);
   arr->decl.dtype    = etype;
   arr->decl.dim      = dim;
   arr->decl.parent   = &(base->object);
   arr->decl.rnode    = rnode;
   arr->decl.storage  = STORE_MARRAY;
   arr->decl.path     = vpi_path_add(base, deref, offset);
   arr->decl.npath    = base->npath + 1;
   arr->decl.elems.fn = vpi_lazy_elems;

   int64_t left, right;
   vpi_array_bounds(arr, &left, &right);
   arr->count = left > right ? left - right + 1 : right - left + 1;

   uint32_t elemsz = ALIGN_UP(esize, 8);
   const int nranges = vlog_ranges(rnode);
   for (int i = dim + 1; i < nranges; i++)
      elemsz *= vlog_size(vlog_range(rnode, i));
   arr->elemsz = elemsz;

   arr->decl.size = arr->count;
   arr->left  = &(build_int_constant(left)->expr.object);
   arr->right = &(build_int_constant(right)->expr.object);

   return &(arr->decl);
}

static void vpi_record_members(c_abstractDecl *base, vpiObjectList *list)
{
   // An unpacked structure is a record on the heap referred to by the
   // object which holds it, with a field for each member

   vlog_node_t st = base->dtype;
   unsigned offset = 0;

   const int ndecls = vlog_decls(st);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t m = vlog_decl(st, i);
      vlog_node_t mtype = vlog_get_type(m);
      if (mtype == NULL)
         return;   // Layout of the remaining fields not known

      unsigned size;
      const storage_t storage = vpi_mem_layout(mtype, &size);
      if (size == 0)
         return;

      c_abstractDecl *e = NULL;
      if (vlog_ranges(m) > 0) {
         // A fixed size array member is held in the record
         if (!vlog_is_fixed_unpacked(m))
            return;

         e = build_mem_array(base, m, mtype, 0, true, offset);
         size = ALIGN_UP(size, 8) * vlog_size(m);
      }
      else if (storage != STORE_NONE)
         e = build_mem_value(base, mtype, storage, true, offset);

      if (e != NULL) {
         e->name = vpi_name_str(istr(vlog_ident(m)), false);
         e->object.loc = *vlog_loc(m);
         vpi_list_push(list, &(e->object));
      }

      offset += ALIGN_UP(size, 8);
   }
}

static void *vpi_field_ptr(c_abstractDecl *d)
{
   // The memory which holds this value: the variable in the frame of
   // its scope followed by the steps of its path

   uint8_t *ptr = vpi_get_ptr(d);
   for (unsigned i = 0; ptr != NULL && i < d->npath; i++) {
      if (d->path[i].deref && (ptr = *(uint8_t **)ptr) == NULL) {
         vpi_error(vpiError, &(d->object.loc), "structure has no storage");
         return NULL;
      }

      ptr += d->path[i].offset;
   }

   return ptr;
}

static void vpi_lazy_elems(c_vpiObject *obj)
{
   c_abstractDecl *decl = is_abstractDecl(obj);
   assert(decl != NULL);

   vpiObjectList *list = &(decl->elems.list);

   c_regArray *arr = is_regArray(obj);
   if (arr != NULL) {
      int64_t left, right;
      vpi_array_bounds(arr, &left, &right);

      for (int64_t i = 0; i < arr->count; i++) {
         const int64_t index = left > right ? left - i : left + i;
         c_abstractDecl *e = build_select(decl, index);
         if (e != NULL)
            vpi_list_push(list, &(e->object));
      }

      return;
   }

   vlog_node_t st = decl->dtype;
   if (decl->storage == STORE_RECORD) {
      vpi_record_members(decl, list);
      return;
   }
   else if (st != NULL && (vlog_kind(st) == V_STRUCT_DECL
                           || vlog_kind(st) == V_UNION_DECL)) {
      if (!(vlog_flags(st) & VLOG_F_PACKED))
         return;

      const int ndecls = vlog_decls(st);
      for (int i = 0; i < ndecls; i++) {
         vlog_node_t m = vlog_decl(st, i);
         if (!vlog_has_ident(m))
            continue;

         c_abstractDecl *e = build_member(decl, m);
         if (e != NULL)
            vpi_list_push(list, &(e->object));
      }

      return;
   }

   int64_t left, right;
   if (!vpi_vector_bounds(decl, &left, &right))
      return;

   const int64_t count = left > right ? left - right + 1 : right - left + 1;
   for (int64_t i = 0; i < count; i++) {
      const int64_t index = left > right ? left - i : left + i;
      c_abstractDecl *e = build_select(decl, index);
      if (e != NULL)
         vpi_list_push(list, &(e->object));
   }
}

////////////////////////////////////////////////////////////////////////////////
// Names, units and top-level objects

static const char *vpi_obj_name(c_vpiObject *obj);

static bool vpi_is_member(c_abstractDecl *d)
{
   if (d->parent == NULL || is_regArray(d->parent) != NULL)
      return false;

   c_abstractDecl *p = is_valueDecl(d->parent);
   return p != NULL && p->dtype != NULL
      && (vlog_kind(p->dtype) == V_STRUCT_DECL
          || vlog_kind(p->dtype) == V_UNION_DECL);
}

static const char *vpi_obj_name(c_vpiObject *obj)
{
   c_abstractScope *s = is_abstractScope(obj);
   if (s != NULL)
      return s->name;

   c_process *proc = is_process(obj);
   if (proc != NULL)
      return proc->name;

   c_scopeArray *sa = is_scopeArray(obj);
   if (sa != NULL)
      return sa->name;

   c_port *p = is_port(obj);
   if (p != NULL)
      return p->name;

   c_misc *m = is_misc(obj);
   if (m != NULL)
      return m->name;

   c_enumConst *ec = is_enumConst(obj);
   if (ec != NULL)
      return istr(vlog_ident(ec->where));

   c_typespec *ts = is_typespec(obj);
   if (ts != NULL)
      return vlog_has_ident(ts->dtype) ? istr(vlog_ident(ts->dtype)) : NULL;

   c_abstractDecl *d = is_abstractDecl(obj);
   if (d != NULL) {
      if (d->name != NULL)
         return d->name;

      LOCAL_TEXT_BUF tb = tb_new();
      if (d->parent == NULL)
         d->name = vpi_name_str(vpi_decl_raw_name(d->where, tb), false);
      else {
         tb_printf(tb, "%s[%"PRIi64"]", vpi_obj_name(d->parent), d->index);
         d->name = vpi_intern(tb_get(tb));
      }

      return d->name;
   }

   return NULL;
}

static const char *vpi_obj_fullname(c_vpiObject *obj)
{
   c_abstractScope *s = is_abstractScope(obj);
   if (s != NULL)
      return vpi_scope_fullname(s);

   LOCAL_TEXT_BUF tb = tb_new();

   c_process *proc = is_process(obj);
   if (proc != NULL) {
      if (proc->fullname == NULL) {
         tb_printf(tb, "%s.%s", vpi_scope_fullname(proc->scope), proc->name);
         proc->fullname = vpi_intern(tb_get(tb));
      }
      return proc->fullname;
   }

   c_scopeArray *sa = is_scopeArray(obj);
   if (sa != NULL) {
      tb_printf(tb, "%s.%s", vpi_scope_fullname(sa->parent), sa->name);
      return vpi_intern(tb_get(tb));
   }

   c_port *p = is_port(obj);
   if (p != NULL) {
      tb_printf(tb, "%s.%s", vpi_scope_fullname(p->scope), p->name);
      return vpi_intern(tb_get(tb));
   }

   c_misc *m = is_misc(obj);
   if (m != NULL) {
      if (m->fullname == NULL) {
         tb_printf(tb, "%s.%s", vpi_scope_fullname(m->scope), m->name);
         m->fullname = vpi_intern(tb_get(tb));
      }
      return m->fullname;
   }

   c_abstractDecl *d = is_abstractDecl(obj);
   if (d != NULL) {
      if (d->fullname != NULL)
         return d->fullname;
      else if (d->parent == NULL) {
         const char *sname = vpi_scope_fullname(d->scope);
         const char *sep = d->scope->object.type == vpiPackage ? "" : ".";
         tb_printf(tb, "%s%s%s", sname, sep, vpi_obj_name(obj));
      }
      else if (vpi_is_member(d))
         tb_printf(tb, "%s.%s", vpi_obj_fullname(d->parent),
                   vpi_obj_name(obj));
      else
         tb_printf(tb, "%s[%"PRIi64"]", vpi_obj_fullname(d->parent),
                   d->index);

      return (d->fullname = vpi_intern(tb_get(tb)));
   }

   return NULL;
}

static c_abstractScope *vpi_obj_scope(c_vpiObject *obj)
{
   c_abstractScope *s = is_abstractScope(obj);
   if (s != NULL)
      return s;

   c_abstractDecl *d = is_abstractDecl(obj);
   if (d != NULL)
      return d->scope;

   c_port *p = is_port(obj);
   if (p != NULL)
      return p->scope;

   c_misc *m = is_misc(obj);
   if (m != NULL)
      return m->scope;

   c_process *proc = is_process(obj);
   if (proc != NULL)
      return proc->scope;

   c_scopeArray *sa = is_scopeArray(obj);
   if (sa != NULL)
      return sa->parent;

   return NULL;
}

static int64_t vpi_scope_ival(c_abstractScope *s)
{
   // Time unit and precision of the module containing this scope

   for (; s != NULL; s = parent_scope(s)) {
      if (s->object.type == vpiPackage)
         return vlog_time_resolve(vlog_ival(s->where));
      else if (s->rtscope != NULL && s->where != NULL
               && vlog_kind(s->where) == V_INST_BODY
               && !(vlog_flags(s->where) & VLOG_F_GEN_BODY))
         return vlog_time_resolve(vlog_ival(s->where));
   }

   return vlog_time_resolve(VLOG_TIME_ENCODE(1, VLOG_TIME_UNSCALED));
}

static int64_t vpi_precision_fs(vpi_context_t *c)
{
   return vlog_time_quantum(c->precision);
}

static int vpi_log10(int64_t value)
{
   int n = 0;
   for (; value >= 10; value /= 10)
      n++;
   return n;
}

static void vpi_add_unit_scope(vlog_node_t v, hset_t *added)
{
   // Declarations outside of any design element belong to the
   // compilation-unit scope $unit (3.12.1) which is seen as a package
   // (37.10)

   if (hset_contains(added, v))
      return;

   hset_insert(added, v);

   bool has_items = false;
   const int ndecls = vlog_decls(v);
   for (int i = 0; i < ndecls && !has_items; i++) {
      switch (vlog_kind(vlog_decl(v, i))) {
      case V_PARAM_DECL:
      case V_LOCALPARAM:
      case V_VAR_DECL:
         has_items = true;
         break;
      default:
         break;
      }
   }

   if (has_items) {
      c_abstractScope *pkg = new_object(sizeof(c_module), vpiPackage);
      init_abstractScope(pkg, v);
      pkg->name = "$unit";
      vpi_list_push(&(vpi_context()->tops), &(pkg->object));
   }
}

typedef struct {
   hset_t *visited;
   hset_t *units;
} vpi_deps_t;

static void vpi_unit_scopes_cb(lib_t lib, ident_t ident, int kind, void *ctx)
{
   if (strchr(istr(ident), '$') == NULL)
      return;

   object_t *obj = lib_get_generic(lib, ident, NULL);
   vlog_node_t v = obj != NULL ? vlog_from_object(obj) : NULL;
   if (v != NULL && vlog_kind(v) == V_NAMESPACE)
      vpi_add_unit_scope(v, ((vpi_deps_t *)ctx)->units);
}

static void vpi_walk_deps(object_t *obj, void *ctx)
{
   vpi_deps_t *deps = ctx;
   if (hset_contains(deps->visited, obj))
      return;

   hset_insert(deps->visited, obj);

   vlog_node_t v = vlog_from_object(obj);
   if (v == NULL)
      return;
   else if (vlog_kind(v) == V_NAMESPACE)
      vpi_add_unit_scope(v, deps->units);

   if (vlog_kind(v) == V_PACKAGE) {
      ident_t lib = ident_until(vlog_ident(v), '.');
      if (lib != well_known(W_NVC) && lib != well_known(W_STD)
          && lib != well_known(W_IEEE)) {
         c_abstractScope *pkg = new_object(sizeof(c_module), vpiPackage);
         init_abstractScope(pkg, v);
         pkg->name = vpi_name_str(istr(vlog_ident2(v)), false);
         vpi_list_push(&(vpi_context()->tops), &(pkg->object));
      }
   }

   arena_walk_deps(object_arena(obj), vpi_walk_deps, ctx);
}


static vpiObjectList *vpi_top_objects(void)
{
   vpi_context_t *c = vpi_context();
   if (c->have_tops)
      return &(c->tops);

   c->have_tops = true;

   rt_model_t *m = vpi_get_model(c);
   rt_scope_t *root = root_scope(m);

   for (int i = 0; root != NULL && i < root->children.count; i++) {
      rt_scope_t *s = child_scope_at(root, i);
      if (s->kind != SCOPE_INSTANCE)
         continue;
      else if (vpi_tree_vlog(s) != NULL)
         vpi_list_push(&(c->tops), &(cached_scope(s->where, s)->object));
      else if (vpi_vlog_below(s))
         vpi_list_push(&(c->tops), &(cached_vhdl_scope(s)->object));
   }

   if (c->top != NULL) {
      vpi_deps_t deps = {
         .visited = hset_new(64),
         .units   = hset_new(16),
      };
      arena_walk_deps(object_arena(tree_to_object(c->top)),
                      vpi_walk_deps, &deps);
      lib_walk_index(lib_work(), vpi_unit_scopes_cb, &deps);
      hset_free(deps.visited);
      hset_free(deps.units);
   }

   return &(c->tops);
}

static c_vpiObject *vpi_actual_iface(c_misc *m)
{
   // The interface instance connected to an interface port

   c_abstractScope *s = m->scope;
   if (s->rtscope == NULL)
      return NULL;

   ident_t id = NULL;
   if (m->is_array)
      id = ident_sprintf("%s[%"PRIi64"]", istr(vlog_ident(m->where)), m->index);

   tree_t target = vlog_iface_port_target(s->rtscope->where, m->where, id);
   if (target == NULL || tree_kind(target) != T_BLOCK)
      return NULL;

   rt_scope_t *rt = find_scope(vpi_get_model(vpi_context()), target);
   if (rt == NULL || vpi_tree_vlog(rt) == NULL)
      return NULL;

   c_abstractScope *cs = cached_scope(target, rt);
   if (!m->is_array)
      return &(cs->object);

   // The array of instances containing the element connected to the
   // first element of the port
   c_abstractScope *ps = parent_scope(cs);
   if (ps == NULL)
      return NULL;

   vpiObjectList *scopes = expand_lazy_list(&(ps->object), &(ps->scopes));
   for (int i = 0; i < scopes->count; i++) {
      c_scopeArray *sa = is_scopeArray(scopes->items[i]);
      for (int j = 0; sa != NULL && j < sa->elems.count; j++) {
         if (sa->elems.items[j] == &(cs->object))
            return &(sa->object);
      }
   }

   return NULL;
}

static int64_t vpi_scope_index(c_abstractScope *s)
{
   size_t len;
   int64_t index;
   if (s->block != NULL
       && vpi_split_index(istr(tree_ident(s->block)), &len, &index))
      return index;

   return 0;
}

static bool vpi_scope_array_bounds(c_scopeArray *sa, int64_t *left,
                                   int64_t *right)
{
   // The elements are created in order from the left index

   if (sa->elems.count == 0)
      return false;

   *left = vpi_scope_index(is_abstractScope(sa->elems.items[0]));
   *right = vpi_scope_index(
      is_abstractScope(sa->elems.items[sa->elems.count - 1]));
   return true;
}

////////////////////////////////////////////////////////////////////////////////
// Values

static bool vpi_is_signed(c_abstractDecl *d)
{
   if (d->object.type == vpiRegBit || d->object.type == vpiNetBit)
      return false;
   else if (d->parent != NULL && is_regArray(d->parent) == NULL
            && !vpi_is_member(d))
      return false;   // Part of a packed vector

   vlog_node_t type = d->dtype;
   if (type == NULL)
      return false;
   else if (vlog_kind(type) == V_DATA_TYPE)
      return !!(vlog_flags(type) & VLOG_F_SIGNED);
   else
      return vlog_is_signed(type);
}

static rt_signal_t *vpi_signal(c_abstractDecl *d)
{
   sig_shared_t **ss = vpi_get_ptr(d);
   if (ss == NULL)
      return NULL;

   return container_of(*ss, rt_signal_t, shared);
}

typedef struct {
   uint64_t *cell;    // Holds the bits or pointers to them
   uint64_t *abits;
   uint64_t *bbits;
   int       width;   // Of the whole value in memory
   int       lsb;     // Of this part of it
   bool      four;
} vpi_vec_t;

static bool vpi_mem_vec(c_abstractDecl *d, vpi_vec_t *v)
{
   // An integral value of up to 64 bits is held in memory as its value
   // bits followed by its unknown bits for a four-state type, and a wider
   // one as pointers to the words of each (least significant first)

   c_abstractDecl *root = d;
   if (d->storage == STORE_FVEC2 || d->storage == STORE_FVEC4) {
      // A bit or member of a member of an unpacked structure
      for (c_abstractDecl *p; root->parent != NULL
              && (p = is_valueDecl(root->parent)) != NULL
              && p->storage == d->storage; )
         root = p;

      v->cell  = vpi_field_ptr(root);
      v->width = root->size;
   }
   else {
      v->cell  = vpi_get_ptr(d);
      v->width = vlog_size(vlog_get_type(d->where));
   }

   if (v->cell == NULL)
      return false;

   v->four = d->storage == STORE_VEC4 || d->storage == STORE_FVEC4;
   v->lsb  = v->width - (d->offset - root->offset) - d->size;

   if (v->width <= 64) {
      v->abits = v->cell;
      v->bbits = v->four ? v->cell + 1 : NULL;
   }
   else {
      v->abits = (uint64_t *)v->cell[0];
      v->bbits = v->four ? (uint64_t *)v->cell[1] : NULL;
   }

   return true;
}

static void vpi_mem_vec_write(vpi_vec_t *v, const uint8_t *unpacked, int size)
{
   // A wide value may be shared so the words are copied before they change

   if (v->width > 64) {
      const int nwords = (v->width + 63) / 64;
      mspace_t *mspace = jit_get_mspace(vpi_get_jit(vpi_context()));
      for (int n = 0; n < (v->four ? 2 : 1); n++) {
         uint64_t *old = n == 0 ? v->abits : v->bbits;
         uint64_t *new = mspace_alloc_array(mspace, nwords, sizeof(uint64_t));
         for (int i = 0; i < nwords; i++)
            new[i] = old != NULL ? old[i] : 0;
         v->cell[n] = (uintptr_t)new;
         if (n == 0)
            v->abits = new;
         else
            v->bbits = new;
      }
   }

   for (int i = 0; i < size; i++) {
      const int pos = v->lsb + size - 1 - i;
      const uint64_t bit = UINT64_C(1) << (pos % 64);
      const uint8_t b = v->four ? unpacked[i] : unpacked[i] == 1;
      v->abits[pos / 64] = (v->abits[pos / 64] & ~bit) | ((b & 1) ? bit : 0);
      if (v->four)
         v->bbits[pos / 64] =
            (v->bbits[pos / 64] & ~bit) | ((b & 2) ? bit : 0);
   }
}

static inline void vpi_gather_bits(const uint8_t *vals, int size,
                                   uint64_t *abits, uint64_t *bbits)
{
   // Each byte of a signal's value holds one bit of the value in bit zero
   // and the unknown bit in bit one.  The first element is the most
   // significant bit.  Eight elements are gathered per step.

   const uint64_t lo = UINT64_C(0x0101010101010101);
   const uint64_t magic = UINT64_C(0x8040201008040201);

   for (int i = 0; i < size; i += 8) {
      const int n = MIN(8, size - i);

      uint64_t w = 0;
      memcpy(&w, vals + i, n);

      const uint64_t a = (((w & lo) * magic) >> 56) >> (8 - n);
      const uint64_t b = ((((w >> 1) & lo) * magic) >> 56) >> (8 - n);

      const int pos = size - i - n;   // Position of the least significant
      abits[pos / 64] |= a << (pos % 64);
      bbits[pos / 64] |= b << (pos % 64);
      if (pos % 64 + n > 64) {
         abits[pos / 64 + 1] |= a >> (64 - pos % 64);
         bbits[pos / 64 + 1] |= b >> (64 - pos % 64);
      }
   }
}

static bool vpi_read_bits(c_abstractDecl *d, uint64_t *abits, uint64_t *bbits)
{
   const int nwords = (d->size + 63) / 64;
   for (int i = 0; i < nwords; i++)
      abits[i] = bbits[i] = 0;

   switch (d->storage) {
   case STORE_SIGNAL:
      {
         rt_signal_t *s = vpi_signal(d);
         if (s == NULL)
            return false;

         const uint8_t *vals = (const uint8_t *)signal_value(s) + d->offset;
         vpi_gather_bits(vals, d->size, abits, bbits);
      }
      return true;

   case STORE_FVEC2:
   case STORE_FVEC4:
   case STORE_VEC2:
   case STORE_VEC4:
      {
         vpi_vec_t v;
         if (!vpi_mem_vec(d, &v))
            return false;

         for (int i = 0; i < d->size; i++) {
            const int pos = v.lsb + i;
            const uint64_t bit = UINT64_C(1) << (i % 64);
            if (v.abits != NULL && (v.abits[pos / 64] >> (pos % 64)) & 1)
               abits[i / 64] |= bit;
            if (v.bbits != NULL && (v.bbits[pos / 64] >> (pos % 64)) & 1)
               bbits[i / 64] |= bit;
         }
      }
      return true;

   default:
      return false;
   }
}

static bool vpi_read_real(c_abstractDecl *d, double *value)
{
   if (d->storage == STORE_REAL) {
      rt_signal_t *s = vpi_signal(d);
      if (s == NULL)
         return false;

      *value = unaligned_load((const double *)signal_value(s) + d->offset,
                              double);
      return true;
   }
   else if (d->storage == STORE_PREAL || d->storage == STORE_FREAL) {
      const double *ptr = d->storage == STORE_FREAL
         ? vpi_field_ptr(d) : vpi_get_ptr(d);
      if (ptr == NULL)
         return false;

      *value = *ptr;
      return true;
   }

   return false;
}

static s_vpi_vecval *vpi_vector_buffer(vpi_context_t *c, int nwords)
{
   if (c->veclen < nwords) {
      c->veclen = MAX(nwords, 16);
      c->vecbuf = xrealloc_array(c->vecbuf, c->veclen, sizeof(s_vpi_vecval));
   }

   return c->vecbuf;
}

static void vpi_format_bits(c_abstractDecl *d, int size, bool is_signed,
                            const uint64_t *abits, const uint64_t *bbits,
                            p_vpi_value value_p, text_buf_t *tb)
{
   vpi_context_t *c = vpi_context();

   switch (value_p->format) {
   case vpiScalarVal:
      {
         static const PLI_INT32 map[] = { vpi0, vpi1, vpiZ, vpiX };
         const int bit = (abits[0] & 1) | ((bbits[0] & 1) << 1);
         value_p->value.scalar = map[bit];
      }
      break;

   case vpiVectorVal:
      {
         const int nwords = (size + 31) / 32;
         s_vpi_vecval *vec = vpi_vector_buffer(c, nwords);
         for (int i = 0; i < nwords; i++) {
            vec[i].aval = abits[i / 2] >> (32 * (i % 2));
            vec[i].bval = bbits[i / 2] >> (32 * (i % 2));
            if (i == nwords - 1 && size % 32 != 0) {
               const uint32_t mask = (UINT32_C(1) << (size % 32)) - 1;
               vec[i].aval &= mask;
               vec[i].bval &= mask;
            }
         }
         value_p->value.vector = vec;
      }
      break;

   case vpiTimeVal:
      {
         static s_vpi_time t;
         t.type = vpiSimTime;
         t.low  = abits[0];
         t.high = abits[0] >> 32;
         value_p->value.time = &t;
      }
      break;

   default:
      vpi_format_number(size, is_signed, abits, bbits, value_p, tb);
      break;
   }
}

static bool vpi_value_decl(c_abstractDecl *d, p_vpi_value value_p)
{
   vpi_context_t *c = vpi_context();

   switch (d->storage) {
   case STORE_REAL:
   case STORE_PREAL:
   case STORE_FREAL:
      {
         double value;
         if (!vpi_read_real(d, &value))
            return false;

         if (value_p->format == vpiObjTypeVal)
            value_p->format = vpiRealVal;

         vpi_format_real(value, value_p, c->valuestr);
         return true;
      }

   case STORE_STRING:
   case STORE_FSTRING:
      {
         const ffi_uarray_t *u = d->storage == STORE_FSTRING
            ? vpi_field_ptr(d) : vpi_get_ptr(d);
         if (u == NULL)
            return false;

         const char *str = u->ptr;
         const int64_t len = ffi_array_length(u->dims[0].length);

         if (value_p->format == vpiObjTypeVal)
            value_p->format = vpiStringVal;

         if (value_p->format == vpiStringVal) {
            if (len > 0)
               tb_catn(c->valuestr, str, len);
            value_p->value.str = (PLI_BYTE8 *)tb_get(c->valuestr);
            return true;
         }

         // Other formats see the characters as an integral value
         const int size = MAX(len, 1) * 8;
         const int nwords = (size + 63) / 64;
         uint64_t *mem LOCAL = xcalloc_array(nwords * 2, sizeof(uint64_t));
         for (int64_t i = 0; i < len; i++) {
            const int pos = (len - 1 - i) * 8;
            mem[pos / 64] |= (uint64_t)(uint8_t)str[i] << (pos % 64);
         }

         vpi_format_bits(d, size, false, mem, mem + nwords, value_p,
                         c->valuestr);
         return true;
      }

   case STORE_SIGNAL:
   case STORE_VEC2:
   case STORE_VEC4:
   case STORE_FVEC2:
   case STORE_FVEC4:
      {
         if (d->storage == STORE_SIGNAL && value_p->format == vpiBinStrVal) {
            // The elements of a signal are already the digits
            rt_signal_t *s = vpi_signal(d);
            if (s == NULL)
               return false;

            static const char map[] = "01zx";
            const uint8_t *vals =
               (const uint8_t *)signal_value(s) + d->offset;
            char *p = tb_reserve(c->valuestr, d->size);
            for (int i = 0; i < d->size; i++)
               p[i] = map[vals[i] & 3];

            value_p->value.str = (PLI_BYTE8 *)tb_get(c->valuestr);
            return true;
         }

         const int nwords = (d->size + 63) / 64;
         uint64_t small[8];
         uint64_t *heap LOCAL = NULL;
         uint64_t *mem = small;
         if (nwords * 2 > ARRAY_LEN(small))
            mem = heap = xmalloc_array(nwords * 2, sizeof(uint64_t));
         if (!vpi_read_bits(d, mem, mem + nwords))
            return false;

         if (value_p->format == vpiObjTypeVal) {
            switch (d->object.type) {
            case vpiIntegerVar:
            case vpiIntVar:
            case vpiShortIntVar:
            case vpiByteVar:
            case vpiEnumVar:
               value_p->format = d->size <= 32 ? vpiIntVal : vpiVectorVal;
               break;
            case vpiTimeVar:
               value_p->format = vpiTimeVal;
               break;
            default:
               value_p->format = d->size == 1 ? vpiScalarVal : vpiVectorVal;
               break;
            }
         }

         vpi_format_bits(d, d->size, vpi_is_signed(d), mem, mem + nwords,
                         value_p, c->valuestr);
         return true;
      }

   default:
      return false;
   }
}

static void vpi_format_constant(c_constant *con, p_vpi_value value_p)
{
   vpi_context_t *c = vpi_context();

   if (con->expr.where == NULL) {
      uint64_t abits[1] = { con->ival }, bbits[1] = { 0 };
      if (value_p->format == vpiObjTypeVal)
         value_p->format = vpiIntVal;
      vpi_format_bits(NULL, 32, true, abits, bbits, value_p, c->valuestr);
      return;
   }

   switch (con->subtype) {
   case vpiStringConst:
      {
         number_t n = vlog_number(con->expr.where);
         if (value_p->format == vpiObjTypeVal)
            value_p->format = vpiStringVal;

         if (value_p->format == vpiStringVal) {
            for (int i = con->size/8 - 1; i >= 0; i--)
               tb_append(c->valuestr, number_byte(n, i));
            value_p->value.str = (PLI_BYTE8 *)tb_get(c->valuestr);
            return;
         }

         const uint64_t *abits, *bbits;
         number_get(n, &abits, &bbits);
         vpi_format_bits(NULL, number_width(n), false, abits, bbits,
                         value_p, c->valuestr);
         return;
      }

   case vpiBinaryConst:
      {
         number_t n = vlog_number(con->expr.where);

         const uint64_t *abits, *bbits;
         number_get(n, &abits, &bbits);

         if (value_p->format == vpiObjTypeVal)
            value_p->format = number_width(n) <= 32 ? vpiIntVal : vpiVectorVal;

         vpi_format_bits(NULL, number_width(n), number_signed(n), abits,
                         bbits, value_p, c->valuestr);
         return;
      }

   case vpiRealConst:
      if (value_p->format == vpiObjTypeVal)
         value_p->format = vpiRealVal;
      vpi_format_real(vlog_dval(con->expr.where), value_p, c->valuestr);
      return;
   }
}

static bool unpack_value(p_vpi_value value_p, uint8_t *unpacked, int size,
                         const loc_t *loc)
{
   switch (value_p->format) {
   case vpiIntVal:
      {
         // Extended with the sign of the 32-bit integer
         const int64_t value = value_p->value.integer;
         for (int i = 0; i < size; i++)
            unpacked[size - 1 - i] = i < 64 ? (value >> i) & 1 : value < 0;
      }
      break;

   case vpiScalarVal:
      {
         uint8_t bit;
         switch (value_p->value.scalar) {
         case vpi0: bit = 0; break;
         case vpi1: bit = 1; break;
         case vpiZ: bit = 2; break;
         case vpiH: bit = 1; break;
         case vpiL: bit = 0; break;
         default: bit = 3; break;
         }

         for (int i = 0; i < size; i++)
            unpacked[i] = i == size - 1 ? bit : 0;
      }
      break;

   case vpiVectorVal:
      for (int i = 0; i < size; i++) {
         const PLI_UINT32 a = value_p->value.vector[i / 32].aval;
         const PLI_UINT32 b = value_p->value.vector[i / 32].bval;
         unpacked[size - 1 - i] =
            ((a >> (i % 32)) & 1) | (((b >> (i % 32)) & 1) << 1);
      }
      break;

   case vpiRealVal:
      {
         const double r = value_p->value.real;
         const int64_t value = isfinite(r) ? llround(r) : 0;
         for (int i = 0; i < size; i++)
            unpacked[size - 1 - i] = i < 64 ? (value >> i) & 1 : value < 0;
      }
      break;

   case vpiTimeVal:
      {
         const uint64_t value = (uint64_t)value_p->value.time->high << 32
            | value_p->value.time->low;
         for (int i = 0; i < size; i++)
            unpacked[size - 1 - i] = i < 64 ? (value >> i) & 1 : 0;
      }
      break;

   case vpiStringVal:
      {
         const char *str = value_p->value.str;
         const int len = strlen(str);
         memset(unpacked, 0, size);
         for (int i = 0, bit = 0; i < len && bit < size; i++) {
            const uint8_t ch = str[len - 1 - i];
            for (int j = 0; j < 8 && bit < size; j++, bit++)
               unpacked[size - 1 - bit] = !!(ch & (1 << j));
         }
      }
      break;

   case vpiDecStrVal:
      {
         const char *str = value_p->value.str;
         if (str[0] == 'x' || str[0] == 'X' || str[0] == 'z' || str[0] == 'Z'
             || str[0] == '?') {
            const uint8_t fill = str[0] == 'x' || str[0] == 'X' ? 3 : 2;
            for (int i = 0; i < size; i++)
               unpacked[i] = fill;
            break;
         }

         const bool negative = str[0] == '-';
         const uint64_t mag = strtoull(str + negative, NULL, 10);
         const uint64_t value = negative ? -mag : mag;
         for (int i = 0; i < size; i++)
            unpacked[size - 1 - i] = i < 64 ? (value >> i) & 1 : negative;
      }
      break;

   case vpiBinStrVal:
      {
         int bit = 0;
         uint8_t top = 0;
         for (int i = strlen(value_p->value.str) - 1; i >= 0 && bit < size;
              i--) {
            switch (value_p->value.str[i]) {
            case '0':
            case '1':
               unpacked[size - 1 - bit++] = top = value_p->value.str[i] == '1';
               break;
            case 'x':
            case 'X':
               unpacked[size - 1 - bit++] = top = 3;
               break;
            case 'z':
            case 'Z':
            case '?':
               unpacked[size - 1 - bit++] = top = 2;
               break;
            case '_':
               break;
            default:
               vpi_error(vpiError, NULL, "invalid character %c in binary "
                         "string", value_p->value.str[i]);
               return false;
            }
         }

         // A leading unknown digit extends to the left
         for (; bit < size; bit++)
            unpacked[size - 1 - bit] = top > 1 ? top : 0;
      }
      break;

   case vpiOctStrVal:
      {
         int bit = 0;
         uint8_t top = 0;
         for (int i = strlen(value_p->value.str) - 1; i >= 0 && bit < size;
              i--) {
            uint8_t digit = 0, unknown = 0;
            switch (value_p->value.str[i]) {
            case '0'...'7': digit = value_p->value.str[i] - '0'; break;
            case 'x': case 'X': unknown = 3; break;
            case 'z': case 'Z': case '?': unknown = 2; break;
            case '_': continue;
            default:
               vpi_error(vpiError, NULL, "invalid character %c in octal "
                         "string", value_p->value.str[i]);
               return false;
            }

            top = unknown;
            for (int j = 0; j < 3 && bit < size; j++, bit++)
               unpacked[size - 1 - bit] = unknown ?: !!(digit & (1 << j));
         }

         for (; bit < size; bit++)
            unpacked[size - 1 - bit] = top > 1 ? top : 0;
      }
      break;

   case vpiHexStrVal:
      {
         int bit = 0;
         uint8_t top = 0;
         for (int i = strlen(value_p->value.str) - 1; i >= 0 && bit < size;
              i--) {
            uint8_t nibble = 0, unknown = 0;
            switch (value_p->value.str[i]) {
            case '0'...'9': nibble = value_p->value.str[i] - '0'; break;
            case 'a'...'f': nibble = 10 + value_p->value.str[i] - 'a'; break;
            case 'A'...'F': nibble = 10 + value_p->value.str[i] - 'A'; break;
            case 'x': case 'X': unknown = 3; break;
            case 'z': case 'Z': case '?': unknown = 2; break;
            case '_': continue;
            default:
               vpi_error(vpiError, NULL, "invalid character %c in hex string",
                         value_p->value.str[i]);
               return false;
            }

            top = unknown;
            for (int j = 0; j < 4 && bit < size; j++, bit++)
               unpacked[size - 1 - bit] = unknown ?: !!(nibble & (1 << j));
         }

         for (; bit < size; bit++)
            unpacked[size - 1 - bit] = top > 1 ? top : 0;
      }
      break;

   default:
      vpi_error(vpiError, loc, "unsupported format %d", value_p->format);
      return false;
   }

   return true;
}

static int64_t vpi_time_to_fs(c_vpiObject *obj, p_vpi_time time_p)
{
   vpi_context_t *c = vpi_context();

   if (time_p == NULL)
      return 0;

   switch (time_p->type) {
   case vpiScaledRealTime:
      {
         c_abstractScope *s = obj != NULL ? vpi_obj_scope(obj) : NULL;
         const int64_t unit = s != NULL
            ? MAX(VLOG_TIME_UNIT(vpi_scope_ival(s)), 1)
            : vpi_precision_fs(c);
         return llround(time_p->real * unit);
      }
   case vpiSimTime:
      return ((int64_t)time_p->high << 32 | time_p->low) * vpi_precision_fs(c);
   default:
      return 0;
   }
}

static void vpi_fill_time(c_vpiObject *obj, int64_t now, p_vpi_time time_p)
{
   vpi_context_t *c = vpi_context();

   switch (time_p->type) {
   case vpiSimTime:
      {
         const uint64_t t = now / vpi_precision_fs(c);
         time_p->high = t >> 32;
         time_p->low  = t & 0xffffffff;
         time_p->real = 0.0;
      }
      break;
   case vpiScaledRealTime:
      {
         c_abstractScope *s = obj != NULL ? vpi_obj_scope(obj) : NULL;
         const int64_t unit = s != NULL
            ? MAX(VLOG_TIME_UNIT(vpi_scope_ival(s)), 1)
            : vpi_precision_fs(c);
         time_p->real = (double)now / unit;
         time_p->high = time_p->low = 0;
      }
      break;
   default:
      break;
   }
}

static void vpi_write_signal(c_abstractDecl *d, rt_signal_t *s,
                             const void *values, PLI_INT32 flags,
                             int64_t delay)
{
   rt_model_t *m = vpi_get_model(vpi_context());

   const bool is_var = !d->is_net && !(vlog_flags(d->where) & VLOG_F_VHDL);
   const int count = d->storage == STORE_REAL ? 1 : d->size;

   switch (flags & 0xff) {
   case vpiForceFlag:
      if (is_var) {
         // The forced value of a variable is seen immediately
         release_signal(m, s, d->offset, count);
         deposit_signal(m, s, values, d->offset, count);
      }
      force_signal(m, s, values, d->offset, count);
      break;
   case vpiReleaseFlag:
      if (is_var) {
         // A variable keeps the forced value until the next assignment
         const uint8_t *cur = signal_value(s);
         const size_t elemsz = d->storage == STORE_REAL ? sizeof(double) : 1;
         sched_deposit(m, s, cur + d->offset * elemsz, d->offset, count,
                       -1, true);
         release_signal(m, s, d->offset, count);
      }
      else
         release_signal_now(m, s, d->offset, count);
      break;
   case vpiInertialDelay:
   case vpiTransportDelay:
   case vpiPureTransportDelay:
      sched_deposit(m, s, values, d->offset, count, delay, false);
      break;
   default:
      deposit_signal(m, s, values, d->offset, count);
      break;
   }
}

static bool vpi_put_decl(c_abstractDecl *d, p_vpi_value value_p,
                         p_vpi_time time_p, PLI_INT32 flags)
{
   vpi_context_t *c = vpi_context();
   rt_model_t *m = vpi_get_model(c);

   const PLI_INT32 mode = flags & 0xff;
   const bool is_signal =
      d->storage == STORE_SIGNAL || d->storage == STORE_REAL;

   if (is_signal && (c->readonly || !model_can_create_delta(m))
       && c->call == NULL) {
      vpi_error(vpiError, &(d->object.loc), "cannot change the value of %s "
                "in a read-only synchronisation callback",
                vpi_obj_fullname(&(d->object)));
      return false;
   }

   int64_t delay = 0;
   if (mode == vpiInertialDelay || mode == vpiTransportDelay
       || mode == vpiPureTransportDelay)
      delay = vpi_time_to_fs(&(d->object), time_p);

   if (mode == vpiReleaseFlag) {
      if (!is_signal)
         goto unsupported;

      rt_signal_t *s = vpi_signal(d);
      if (s == NULL)
         return false;

      vpi_write_signal(d, s, NULL, flags, 0);
      return true;
   }

   switch (d->storage) {
   case STORE_REAL:
   case STORE_PREAL:
   case STORE_FREAL:
      {
         double value;
         switch (value_p->format) {
         case vpiRealVal:
            value = value_p->value.real;
            break;
         case vpiIntVal:
            value = value_p->value.integer;
            break;
         default:
            {
               uint8_t bits[64];
               if (!unpack_value(value_p, bits, 64, &(d->object.loc)))
                  return false;

               int64_t ival = 0;
               for (int i = 0; i < 64; i++)
                  ival = (ival << 1) | (bits[i] & 1);
               value = ival;
            }
            break;
         }

         if (d->storage == STORE_PREAL || d->storage == STORE_FREAL) {
            double *ptr = d->storage == STORE_FREAL
               ? vpi_field_ptr(d) : vpi_get_ptr(d);
            if (ptr == NULL)
               return false;
            *ptr = value;
            return true;
         }

         rt_signal_t *s = vpi_signal(d);
         if (s == NULL)
            return false;

         vpi_write_signal(d, s, &value, flags, delay);
         return true;
      }

   case STORE_STRING:
   case STORE_FSTRING:
      {
         if (value_p->format != vpiStringVal)
            goto unsupported;

         ffi_uarray_t *u = d->storage == STORE_FSTRING
            ? vpi_field_ptr(d) : vpi_get_ptr(d);
         if (u == NULL)
            return false;

         const size_t len = strlen(value_p->value.str);
         mspace_t *mspace = jit_get_mspace(vpi_get_jit(vpi_context()));
         char *mem = mspace_alloc(mspace, MAX(len, 1));
         memcpy(mem, value_p->value.str, len);

         u->ptr = mem;
         u->dims[0].left = 0;
         u->dims[0].length = len;
         return true;
      }

   case STORE_SIGNAL:
      {
         rt_signal_t *s = vpi_signal(d);
         if (s == NULL)
            return false;

         uint8_t *unpacked LOCAL = xcalloc_array(d->size, sizeof(uint8_t));
         if (!unpack_value(value_p, unpacked, d->size, &(d->object.loc)))
            return false;

         if (!d->is_net && d->dtype != NULL && vlog_is_two_state(d->dtype)) {
            // An unknown bit becomes zero in a two-state variable (6.3.2.1)
            for (int i = 0; i < d->size; i++)
               unpacked[i] = unpacked[i] == 1;
         }

         vpi_write_signal(d, s, unpacked, flags, delay);
         return true;
      }

   case STORE_VEC2:
   case STORE_VEC4:
   case STORE_FVEC2:
   case STORE_FVEC4:
      {
         vpi_vec_t v;
         if (!vpi_mem_vec(d, &v))
            return false;

         uint8_t *unpacked LOCAL = xcalloc_array(d->size, sizeof(uint8_t));
         if (!unpack_value(value_p, unpacked, d->size, &(d->object.loc)))
            return false;

         vpi_mem_vec_write(&v, unpacked, d->size);
         return true;
      }

   default:
      break;
   }

 unsupported:
   vpi_error(vpiError, &(d->object.loc), "cannot change value of %s",
             vpi_obj_fullname(&(d->object)));
   return false;
}

////////////////////////////////////////////////////////////////////////////////
// Callbacks

static rt_watch_t *vpi_watch_decl(vpi_context_t *c, c_abstractDecl *d,
                                  vpiHandle handle);

static void vpi_fire_value_change(uint64_t now, void *user)
{
   vpi_context_t *c = vpi_context();
   handle_slot_t *slot = decode_handle(c, user);
   if (slot == NULL)
      return;

   c_callback *cb = is_callback(slot->obj);
   if (cb == NULL || cb->removed)
      return;

   handle_slot_t *oslot = decode_handle(c, cb->obj);
   c_vpiObject *obj = oslot != NULL ? oslot->obj : NULL;

   if (cb->data.time != NULL)
      vpi_fill_time(obj, now, cb->data.time);

   if (cb->data.value != NULL && cb->format != vpiSuppressVal) {
      s_vpi_value value = { .format = cb->format };
      vpi_get_value(cb->obj, &value);
      *(cb->data.value) = value;
   }

   (*cb->data.cb_rtn)(&(cb->data));
}

static void vpi_value_change_cb(uint64_t now, rt_signal_t *signal,
                                rt_watch_t *watch, void *user)
{
   vpi_fire_value_change(now, user);
}

static void vpi_release_callback(vpi_context_t *c, c_callback *cb)
{
   if (cb->sampler != NULL) {
      model_remove_sampler(vpi_get_model(c), cb->sampler);
      cb->sampler = NULL;
   }

   free(cb->snapshot);
   cb->snapshot = NULL;

   if (cb->watch != NULL) {
      watch_free(vpi_get_model(c), cb->watch);
      cb->watch = NULL;
   }

   if (cb->obj != NULL) {
      drop_handle(c, cb->obj);
      cb->obj = NULL;
   }

   cb->removed = true;

   if (cb->handle != NULL) {
      vpiHandle h = cb->handle;
      cb->handle = NULL;
      drop_handle(c, h);
   }
}

static void vpi_global_cb(rt_model_t *m, void *user)
{
   vpi_context_t *c = vpi_context();
   vpiHandle handle = user;

   handle_slot_t *slot = decode_handle(c, handle);
   if (slot == NULL)
      return;

   c_callback *cb = is_callback(slot->obj);
   if (cb == NULL || cb->removed || cb->handle != handle)
      return;

   if (cb->data.time != NULL)
      vpi_fill_time(NULL, model_now(m, NULL), cb->data.time);

   // A callback for a time or simulation event happens once
   cb->removed = true;

   const bool readonly = c->readonly;
   c->readonly = cb->data.reason == cbReadOnlySynch;
   (*cb->data.cb_rtn)(&(cb->data));
   c->readonly = readonly;

   // The handle may have been removed by the callback
   if ((slot = decode_handle(c, handle)) != NULL
       && slot->obj == &(cb->refcounted.object)) {
      // The callback object of a callback which happens once is freed
      // when it has happened
      vpiHandle user = cb->user;
      cb->user = NULL;
      vpi_release_callback(c, cb);

      if ((slot = decode_handle(c, user)) != NULL
          && slot->obj == &(cb->refcounted.object))
         drop_handle(c, user);
   }
}

static void vpi_arm_callback(vpi_context_t *c, vpiHandle handle)
{
   c_callback *cb = is_callback(decode_handle(c, handle)->obj);
   assert(cb != NULL);

   rt_model_t *m = c->model;

   mtp_tag_fn(vpi_global_cb, MTP_VPI);

   switch (cb->data.reason) {
   case cbStartOfSimulation:
      model_set_phase_cb(m, START_OF_SIMULATION, vpi_global_cb, handle);
      break;
   case cbEndOfSimulation:
      model_set_phase_cb(m, END_OF_SIMULATION, vpi_global_cb, handle);
      break;
   case cbReadWriteSynch:
      model_set_phase_cb(m, LAST_KNOWN_DELTA_CYCLE, vpi_global_cb, handle);
      break;
   case cbReadOnlySynch:
      model_set_phase_cb(m, END_TIME_STEP, vpi_global_cb, handle);
      break;
   case cbNvcEndOfDelta:
      model_set_phase_cb(m, END_OF_PROCESSES, vpi_global_cb, handle);
      break;
   case cbNextSimTime:
      model_set_phase_cb(m, NEXT_TIME_STEP, vpi_global_cb, handle);
      break;
   case cbAfterDelay:
   case cbAtStartOfSimTime:
      {
         const uint64_t now = model_now(m, NULL);
         uint64_t when = cb->delay;
         if (cb->data.reason == cbAfterDelay)
            when += now;

         if (when > now)
            model_set_timeout_cb(m, when, vpi_global_cb, handle);
         else
            model_set_phase_cb(m, LAST_KNOWN_DELTA_CYCLE, vpi_global_cb,
                               handle);
      }
      break;
   }
}

static rt_watch_t *vpi_watch_decl(vpi_context_t *c, c_abstractDecl *d,
                                  vpiHandle handle)
{
   if (d->storage != STORE_SIGNAL && d->storage != STORE_REAL)
      return NULL;

   rt_signal_t *s = vpi_signal(d);
   if (s == NULL)
      return NULL;

   uint32_t count = d->size;
   c_regArray *arr = is_regArray(&(d->object));
   if (arr != NULL)
      count = arr->count * arr->elemsz;
   else if (d->storage == STORE_REAL)
      count = 1;

   rt_model_t *m = vpi_get_model(c);
   mtp_tag_fn(vpi_value_change_cb, MTP_VPI);
   rt_watch_t *w = watch_new(m, vpi_value_change_cb, handle, WATCH_EVENT, 1);
   model_set_event_cb(m, s, d->offset, count, w);
   return w;
}

typedef struct {
   uint8_t *data;
   size_t   len;
   size_t   cap;
   uint8_t  small[128];   // Avoids allocating for the usual small value
} vpi_snap_t;

static void vpi_snap_init(vpi_snap_t *sn)
{
   sn->data = sn->small;
   sn->len  = 0;
   sn->cap  = sizeof(sn->small);
}

static void vpi_snap_put(vpi_snap_t *sn, const void *data, size_t len)
{
   if (sn->len + len > sn->cap) {
      const size_t cap = MAX(sn->cap * 2, sn->len + len);
      if (sn->data == sn->small) {
         sn->data = xmalloc(cap);
         memcpy(sn->data, sn->small, sn->len);
      }
      else
         sn->data = xrealloc(sn->data, cap);
      sn->cap = cap;
   }

   if (len > 0)
      memcpy(sn->data + sn->len, data, len);
   sn->len += len;
}

static void vpi_snap_free(vpi_snap_t *sn)
{
   if (sn->data != sn->small)
      free(sn->data);
}

static void vpi_snap_decl(c_abstractDecl *d, vpi_snap_t *sn)
{
   // Append everything that makes up the value of this object held in
   // memory so two snapshots are the same if and only if it did not change

   switch (d->storage) {
   case STORE_REAL:
   case STORE_PREAL:
   case STORE_FREAL:
      {
         double value = 0.0;
         if (!vpi_read_real(d, &value))
            value = 0.0;
         vpi_snap_put(sn, &value, sizeof(value));
      }
      break;

   case STORE_STRING:
   case STORE_FSTRING:
      {
         const ffi_uarray_t *u = d->storage == STORE_FSTRING
            ? vpi_field_ptr(d) : vpi_get_ptr(d);
         const int64_t len = u != NULL ? ffi_array_length(u->dims[0].length) : -1;
         vpi_snap_put(sn, &len, sizeof(len));
         if (len > 0)
            vpi_snap_put(sn, u->ptr, len);
      }
      break;

   case STORE_SIGNAL:
   case STORE_VEC2:
   case STORE_VEC4:
   case STORE_FVEC2:
   case STORE_FVEC4:
      {
         vpi_vec_t v;
         if (d->storage != STORE_SIGNAL && d->size <= 64
             && vpi_mem_vec(d, &v) && v.width <= 64) {
            // The common case of a small value: its bits and unknown bits
            const uint64_t mask = d->size == 64
               ? UINT64_MAX : (UINT64_C(1) << d->size) - 1;
            const uint64_t bits[2] = {
               (v.abits[0] >> v.lsb) & mask,
               v.bbits != NULL ? (v.bbits[0] >> v.lsb) & mask : 0
            };
            vpi_snap_put(sn, bits, sizeof(bits));
            break;
         }

         const int nwords = (d->size + 63) / 64;
         uint64_t *mem LOCAL = xmalloc_array(nwords * 2, sizeof(uint64_t));
         if (!vpi_read_bits(d, mem, mem + nwords))
            memset(mem, '\xff', nwords * 2 * sizeof(uint64_t));
         vpi_snap_put(sn, mem, nwords * 2 * sizeof(uint64_t));
      }
      break;

   case STORE_RECORD:
   case STORE_MARRAY:
      {
         vpiObjectList *list = expand_lazy_list(&(d->object), &(d->elems));
         for (unsigned i = 0; i < list->count; i++) {
            c_abstractDecl *e = is_abstractDecl(list->items[i]);
            if (e != NULL)
               vpi_snap_decl(e, sn);
         }
      }
      break;

   default:
      break;
   }
}

static bool vpi_is_memory_decl(c_abstractDecl *d)
{
   switch (d->storage) {
   case STORE_STRING:
   case STORE_VEC2:
   case STORE_VEC4:
   case STORE_PREAL:
   case STORE_RECORD:
   case STORE_FVEC2:
   case STORE_FVEC4:
   case STORE_FREAL:
   case STORE_FSTRING:
   case STORE_MARRAY:
      return true;
   default:
      return false;
   }
}

static void vpi_sample_cb(rt_model_t *m, void *user)
{
   // An object held in memory has nothing to notify the model when it
   // changes, so it is compared with its last value once the delta cycles
   // of each time step have settled

   vpi_context_t *c = vpi_context();
   handle_slot_t *slot = decode_handle(c, user);
   if (slot == NULL)
      return;

   c_callback *cb = is_callback(slot->obj);
   if (cb == NULL || cb->removed)
      return;

   handle_slot_t *oslot = decode_handle(c, cb->obj);
   c_abstractDecl *d = oslot != NULL ? is_abstractDecl(oslot->obj) : NULL;
   if (d == NULL)
      return;

   vpi_snap_t now;
   vpi_snap_init(&now);
   vpi_snap_decl(d, &now);

   if (now.len == cb->snaplen
       && (now.len == 0 || memcmp(now.data, cb->snapshot, now.len) == 0)) {
      vpi_snap_free(&now);
      return;
   }

   cb->snapshot = xrealloc(cb->snapshot, MAX(now.len, 1));
   memcpy(cb->snapshot, now.data, now.len);
   cb->snaplen = now.len;
   vpi_snap_free(&now);

   vpi_fire_value_change(model_now(m, NULL), user);
}

static rt_sampler_t *vpi_sample_decl(vpi_context_t *c, c_callback *cb,
                                     c_abstractDecl *d, vpiHandle handle)
{
   if (!vpi_is_memory_decl(d))
      return NULL;

   vpi_snap_t sn;
   vpi_snap_init(&sn);
   vpi_snap_decl(d, &sn);
   cb->snapshot = xmalloc(MAX(sn.len, 1));
   memcpy(cb->snapshot, sn.data, sn.len);
   cb->snaplen = sn.len;
   vpi_snap_free(&sn);

   return model_add_sampler(vpi_get_model(c), vpi_sample_cb, handle);
}

////////////////////////////////////////////////////////////////////////////////
// Public API

DLLEXPORT
vpiHandle vpi_register_systf(p_vpi_systf_data systf_data_p)
{
   vpi_clear_error();

   VPI_TRACE("tfname=%s", systf_data_p->tfname);

   if (systf_data_p->tfname == NULL || systf_data_p->tfname[0] != '$') {
      vpi_error(vpiError, NULL, "system task or function name must start "
                "with $");
      return NULL;
   }

   c_callback *cb = recycle_object(sizeof(c_callback), vpiCallback);
   cb->systf = *systf_data_p;
   cb->name  = ident_new(systf_data_p->tfname);

   vpiHandle handle = internal_handle_for(&cb->refcounted.object);
   APUSH(vpi_context()->systasks, handle);

   return handle;
}

static vpi_nvc_diag_fn_t  diag_fn;
static void              *diag_context;

static void vpi_diag_hook(vhdl_severity_t severity, const char *text,
                          const loc_t *loc, void *context)
{
   const bool valid = loc != NULL && !loc_invalid_p(loc);
   (*diag_fn)(severity, text, valid ? loc_file_str(loc) : NULL,
              valid ? loc->first_line : 0, diag_context);
}

static rt_proc_t *vpi_find_process(c_process *p)
{
   // The runtime process which the procedure became

   rt_scope_t *rts = storage_scope(p->scope) != NULL
      ? storage_scope(p->scope)->rtscope : NULL;
   if (rts == NULL)
      return NULL;

   for (int i = 0; i < rts->procs.count; i++) {
      rt_proc_t *rp = rts->procs.items[i];
      if (tree_kind(rp->where) == T_VERILOG && tree_vlog(rp->where) == p->where)
         return rp;
   }

   return NULL;
}

DLLEXPORT
PLI_INT32 vpi_nvc_get_wake_time(vpiHandle process, p_vpi_time time_p)
{
   vpi_clear_error();

   c_vpiObject *obj = from_handle(process);
   if (obj == NULL || is_process(obj) == NULL)
      return 0;

   rt_proc_t *rp = vpi_find_process(is_process(obj));
   if (rp == NULL || !rp->wakeable.delayed)
      return 0;

   vpi_fill_time(obj, rp->wake_at, time_p);
   return 1;
}

DLLEXPORT
PLI_INT32 vpi_nvc_get_delta(void)
{
   vpi_context_t *c = vpi_context();
   if (c->model == NULL)
      return 0;

   unsigned deltas;
   model_now(c->model, &deltas);
   return deltas;
}

static vpi_nvc_stmt_fn_t stmt_fn;
static void             *stmt_context;

static void vpi_stmt_hook(const char *file, int line, rt_proc_t *proc,
                          void *context)
{
   LOCAL_TEXT_BUF scope = tb_new();
   if (proc != NULL && proc->scope != NULL)
      get_path_name(proc->scope, scope);

   (*stmt_fn)(file, line, proc != NULL ? istr(proc->name) : NULL,
              tb_get(scope), stmt_context);
}

DLLEXPORT
void vpi_nvc_set_stmt_cb(vpi_nvc_stmt_fn_t fn, void *user_data)
{
   stmt_fn = fn;
   stmt_context = user_data;
   set_stmt_hook(fn != NULL ? vpi_stmt_hook : NULL, NULL);
}

DLLEXPORT
PLI_INT32 vpi_nvc_stmt_breakpoint_add(const char *file, int line)
{
   stmt_break_add(file, line);
   return opt_get_int(OPT_DEBUG_STMTS) ? 1 : 0;
}

DLLEXPORT
PLI_INT32 vpi_nvc_stmt_step(PLI_INT32 enable)
{
   stmt_step_set(enable != 0);
   return opt_get_int(OPT_DEBUG_STMTS) ? 1 : 0;
}

DLLEXPORT
PLI_INT32 vpi_nvc_stmt_breakpoint_remove(const char *file, int line)
{
   return stmt_break_remove(file, line) ? 1 : 0;
}

DLLEXPORT
void vpi_nvc_set_diag_cb(vpi_nvc_diag_fn_t fn, void *user_data)
{
   diag_fn = fn;
   diag_context = user_data;
   set_assert_hook(fn != NULL ? vpi_diag_hook : NULL, NULL);
}

typedef struct {
   vpi_nvc_conn_fn_t fn;
   void             *user;
   uint32_t          total;
} conn_query_t;

static void conn_set_loc(vpi_nvc_conn_t *out, tree_t where)
{
   const loc_t *loc = tree_loc(where);
   if (loc != NULL && !loc_invalid_p(loc)) {
      out->file = loc_file_str(loc);
      out->line = loc->first_line;
   }
}

static void conn_report(const rt_conn_t *c, void *arg)
{
   conn_query_t *q = arg;

   LOCAL_TEXT_BUF scope = tb_new();
   LOCAL_TEXT_BUF other = tb_new();

   vpi_nvc_conn_t out = {
      .first = c->first,
      .count = c->count,
      .total = q->total,
   };

   if (c->scope != NULL)
      model_scope_path(c->scope, scope);

   switch (c->kind) {
   case RT_CONN_DRIVER:
      {
         if (c->proc == NULL)
            return;

         out.kind = vpiNvcConnProcess;
         out.proc_kind = model_proc_kind(c->proc);

         // The name without the path of the scope
         const char *full = istr(c->proc->name);
         const char *colon = strrchr(full, ':');
         out.name = colon != NULL ? colon + 1 : full;
         conn_set_loc(&out, c->proc->where);
      }
      break;

   case RT_CONN_PORT:
      out.kind = vpiNvcConnPort;
      model_item_path(c->other_scope, c->other_name, other);
      out.other = tb_get(other);
      out.other_first = c->other_first;
      out.other_width = c->other_width;
      if (c->scope != NULL && c->scope->kind == SCOPE_INSTANCE)
         conn_set_loc(&out, c->scope->where);
      break;

   case RT_CONN_FORCE:
      out.kind = vpiNvcConnForce;
      break;

   case RT_CONN_DEPOSIT:
      out.kind = vpiNvcConnDeposit;
      break;
   }

   if (c->approx)
      out.flags |= vpiNvcConnApprox;
   if (c->collapsed)
      out.flags |= vpiNvcConnCollapsed;
   if (c->from_child)
      out.flags |= vpiNvcConnFromChild;

   out.scope = tb_get(scope);
   (*q->fn)(&out, q->user);
}

static conn_design_t *conn_design(vpi_context_t *c)
{
   // The static description of the design, built when first asked for

   static conn_design_t *cd;
   static rt_model_t    *built_for;

   if (cd == NULL || built_for != c->model) {
      if (cd != NULL)
         conn_free(cd);
      cd = conn_build(root_scope(c->model)->where);
      built_for = c->model;
   }

   return cd;
}

static void conn_space_path(text_buf_t *tb, const char *dotted, bool verilog)
{
   // "TOP.U_SUB.Q" as the path of the protocol: spaces and, for VHDL, lower
   // case
   for (const char *p = dotted; *p; p++) {
      char ch = *p == '.' ? ' ' : *p;
      if (!verilog && ch >= 'A' && ch <= 'Z')
         ch += 'a' - 'A';
      tb_append(tb, ch);
   }
}

static void conn_block_path(text_buf_t *tb, const conn_block_t *b)
{
   // The path of a block in the protocol.  The entity or module bound to
   // a component is seen with the name of the component instance.

   if (b->parent != NULL && b->parent->component) {
      conn_block_path(tb, b->parent);
      return;
   }

   if (b->parent != NULL) {
      conn_block_path(tb, b->parent);
      tb_append(tb, ' ');
   }

   const bool fold = b->lang != CONN_VERILOG;
   for (const char *p = b->label; *p; p++)
      tb_append(tb, fold && *p >= 'A' && *p <= 'Z' ? *p + 'a' - 'A' : *p);
}

typedef struct {
   conn_query_t       *q;
   vpi_context_t      *ctx;
   conn_design_t      *cd;
} conn_load_t;

static void conn_load_report(const conn_end_t *e, void *arg)
{
   conn_load_t *l = arg;

   LOCAL_TEXT_BUF scope = tb_new();
   LOCAL_TEXT_BUF other = tb_new();


   vpi_nvc_conn_t out = {
      .is_load = 1,
      .first   = e->lo,
      .count   = e->hi - e->lo + 1,
      .total   = l->q->total,
      .file    = e->src,
      .line    = e->line,
   };

   if (e->approx)
      out.flags |= vpiNvcConnApprox;

   switch (e->kind) {
   case CONN_END_PROC:
      {
         out.kind = vpiNvcConnProcess;
         conn_block_path(scope, e->block);

         // Use the process of the model, which has the name that the
         // other queries take
         rt_proc_t *rp = model_lookup_proc_at(l->ctx->model, tb_get(scope),
                                              e->proc->src, e->proc->line);
         const char *name = NULL;
         if (rp != NULL) {
            tb_rewind(scope);
            if (rp->scope != NULL)
               model_scope_path(rp->scope, scope);
            const char *full = istr(rp->name);
            const char *colon = strrchr(full, ':');
            name = colon != NULL ? colon + 1 : full;
            out.proc_kind = model_proc_kind(rp);
         }
         else {
            name = e->proc->name;
            out.proc_kind = "process";
         }

         out.name = name;
         if (e->proc->src != NULL) {
            out.file = e->proc->src;
            out.line = e->proc->line;
         }
      }
      break;

   case CONN_END_CHILD:
   case CONN_END_EXPR:
      // The net feeds a port of an instance below
      out.kind = vpiNvcConnPort;
      out.flags |= vpiNvcConnFromChild;
      conn_block_path(scope, e->port->block);
      conn_block_path(other, e->port->block);
      tb_append(other, ' ');
      conn_space_path(other, e->port->name, e->port->block->lang == CONN_VERILOG);
      out.other = tb_get(other);
      out.other_first = e->kind == CONN_END_CHILD ? e->other.lo : 0;
      out.other_width = e->port->width;
      break;

   case CONN_END_PARENT:
      // The port is connected to an actual in the block above
      out.kind = vpiNvcConnPort;
      conn_block_path(scope, e->block);
      if (e->other.net != NULL) {
         const conn_block_t *pb = e->other.net->block;
         const bool pv = pb->lang == CONN_VERILOG;
         conn_block_path(other, pb);
         tb_append(other, ' ');
         conn_space_path(other, e->other.net->name, pv);
         out.other = tb_get(other);
         out.other_first = e->other.lo;
         out.other_width = e->other.net->width;
      }
      break;
   }

   out.scope = tb_get(scope);
   (*l->q->fn)(&out, l->q->user);
}

static bool conn_loads(vpi_context_t *c, const char *path, conn_query_t *q,
                       PLI_INT32 first, PLI_INT32 count)
{
   // The readers of the range, from the static description of the design:
   // the model knows only what is waiting at the moment

   conn_design_t *cd = conn_design(c);
   if (cd == NULL)
      return false;

   LOCAL_TEXT_BUF dotted = tb_new();
   for (const char *p = path; *p; p++)
      tb_append(dotted, *p == ' ' ? '.' : *p);

   const conn_net_t *net = conn_find_net(cd, tb_get(dotted));
   if (net == NULL || net->width == 0 || (unsigned)first >= net->width)
      return false;

   const unsigned hi = count > 0
      ? MIN((unsigned)(first + count - 1), net->width - 1) : net->width - 1;

   conn_load_t l = { .q = q, .ctx = c, .cd = cd };
   conn_readers(cd, net, first, hi, conn_load_report, &l);
   return true;
}

DLLEXPORT
PLI_INT32 vpi_nvc_connections(const char *path, PLI_INT32 first,
                              PLI_INT32 count, vpi_nvc_conn_fn_t fn,
                              void *user_data)
{
   vpi_clear_error();

   vpi_context_t *c = vpi_context();
   if (c->model == NULL || path == NULL || fn == NULL || first < 0)
      return -1;

   rt_found_t found;
   if (!model_lookup_signal(c->model, path, &found))
      return -1;

   const uint32_t width = signal_width(found.signal);
   if (found.offset + first >= width)
      return 0;

   const uint32_t avail = width - found.offset - first;
   conn_query_t q = {
      .fn    = fn,
      .user  = user_data,
      .total = count > 0 ? MIN((uint32_t)count, avail) : avail,
   };

   model_item_drivers(&found, first, count, conn_report, &q);

   // A signal that was found by an alias path (a merged port) is asked for by
   // the name given
   return conn_loads(c, path, &q, first, count) ? 0 : vpiNvcConnLoadsMissing;
}

typedef struct {
   vpi_nvc_procsig_fn_t fn;
   void                *user;
} procsig_query_t;

static void procsig_report(rt_signal_t *s, uint32_t first, uint32_t count,
                           bool write, void *ctx)
{
   const procsig_query_t *q = ctx;

   LOCAL_TEXT_BUF path = tb_new();
   model_item_path(s->parent, tree_ident(s->where), path);

   vpi_nvc_procsig_t out = {
      .path     = tb_get(path),
      .first    = first,
      .count    = count,
      .width    = signal_width(s),
      .is_write = write,
   };
   (*q->fn)(&out, q->user);
}

DLLEXPORT
PLI_INT32 vpi_nvc_process_signals(const char *scope_path, const char *name,
                                  vpi_nvc_procsig_fn_t fn, void *user_data)
{
   vpi_clear_error();

   vpi_context_t *c = vpi_context();
   if (c->model == NULL || scope_path == NULL || name == NULL || fn == NULL)
      return -1;

   rt_proc_t *p = model_lookup_proc(c->model, scope_path, name);
   if (p == NULL)
      return -1;

   procsig_query_t q = { fn, user_data };
   model_process_signals(c->model, p, procsig_report, &q);
   return 0;
}

DLLEXPORT
vpiHandle vpi_register_cb(p_cb_data cb_data_p)
{
   vpi_clear_error();

   VPI_TRACE("reason=%d", cb_data_p->reason);

   vpi_context_t *c = vpi_context();

   if (cb_data_p->cb_rtn == NULL) {
      vpi_error(vpiError, NULL, "missing callback routine");
      return NULL;
   }

   switch (cb_data_p->reason) {
   case cbStartOfSimulation:
   case cbEndOfSimulation:
      break;
   case cbReadWriteSynch:
   case cbReadOnlySynch:
   case cbNvcEndOfDelta:
   case cbNextSimTime:
   case cbAfterDelay:
   case cbAtStartOfSimTime:
   case cbValueChange:
      if (c->model == NULL) {
         vpi_error(vpiError, NULL, "callback reason %d cannot be registered "
                   "before simulation starts", cb_data_p->reason);
         return NULL;
      }
      break;
   default:
      vpi_error(vpiError, NULL, "unsupported callback reason %d",
                cb_data_p->reason);
      return NULL;
   }

   c_vpiObject *obj = NULL;
   if (cb_data_p->reason == cbValueChange) {
      if ((obj = from_handle(cb_data_p->obj)) == NULL)
         return NULL;

      c_port *p = is_port(obj);
      if (p != NULL && p->low != NULL)
         obj = p->low;
   }

   if ((cb_data_p->reason == cbAfterDelay
        || cb_data_p->reason == cbAtStartOfSimTime)
       && cb_data_p->time == NULL) {
      vpi_error(vpiError, NULL, "missing time for callback reason %d",
                cb_data_p->reason);
      return NULL;
   }

   c_callback *cb = recycle_object(sizeof(c_callback), vpiCallback);
   cb->data = *cb_data_p;

   if (cb_data_p->time != NULL) {
      cb->time = *cb_data_p->time;
      cb->data.time = &(cb->time);
      if (cb->time.type == vpiSuppressTime)
         cb->data.time = NULL;
   }

   if (cb_data_p->value != NULL) {
      cb->value = *cb_data_p->value;
      cb->data.value = &(cb->value);
      cb->format = cb->value.format;
   }

   if (cb_data_p->reason == cbAfterDelay
       || cb_data_p->reason == cbAtStartOfSimTime)
      cb->delay = vpi_time_to_fs(obj, cb_data_p->time);

   vpiHandle handle = cb->handle = internal_handle_for(&cb->refcounted.object);

   if (obj != NULL) {
      c_abstractDecl *d = is_abstractDecl(obj);
      if (d != NULL && vpi_is_memory_decl(d))
         cb->sampler = vpi_sample_decl(c, cb, d, handle);
      else if (d != NULL)
         cb->watch = vpi_watch_decl(c, d, handle);

      if (cb->watch == NULL && cb->sampler == NULL) {
         vpi_error(vpiError, &(obj->loc), "cannot register a value change "
                   "callback for %s", handle_pp(cb_data_p->obj));
         drop_handle(c, handle);
         return NULL;
      }

      cb->obj = cb->data.obj = internal_handle_for(obj);
   }
   else if (c->model == NULL)
      APUSH(c->pending, handle);
   else
      vpi_arm_callback(c, handle);

   return (cb->user = user_handle_for(&cb->refcounted.object));
}

DLLEXPORT
PLI_INT32 vpi_remove_cb(vpiHandle cb_obj)
{
   vpi_clear_error();

   VPI_TRACE("cb_obj=%s", handle_pp(cb_obj));

   c_vpiObject *obj = from_handle(cb_obj);
   if (obj == NULL)
      return 0;

   c_callback *cb = cast_callback(obj);
   if (cb == NULL || cb->systf.tfname != NULL)
      return 0;

   if (cb->removed && cb->handle == NULL) {
      vpi_error(vpiError, NULL, "callback has already been removed or "
                "has happened");
      return 0;
   }

   vpi_context_t *c = vpi_context();
   vpi_release_callback(c, cb);
   drop_handle(c, cb_obj);
   return 1;
}

DLLEXPORT
void vpi_get_cb_info(vpiHandle object, p_cb_data cb_data_p)
{
   vpi_clear_error();

   c_vpiObject *obj = from_handle(object);
   if (obj == NULL)
      return;

   c_callback *cb = cast_callback(obj);
   if (cb != NULL)
      *cb_data_p = cb->data;
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
PLI_INT32 vpi_free_object(vpiHandle object)
{
   return vpi_release_handle(object);
}

static c_vpiObject *vpi_range_object(c_vpiObject *left, c_vpiObject *right)
{
   c_range *r = new_object(sizeof(c_range), vpiRange);
   r->left = left;
   r->right = right;
   return &(r->object);
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
      default:
         return NULL;
      }
   }

   c_vpiObject *obj = from_handle(refHandle);
   if (obj == NULL)
      return NULL;

   switch (type) {
   case vpiLeftRange:
   case vpiRightRange:
      {
         c_regArray *arr = is_regArray(obj);
         if (arr != NULL)
            return user_handle_for(type == vpiLeftRange
                                   ? arr->left : arr->right);

         c_range *r = is_range(obj);
         if (r != NULL)
            return user_handle_for(type == vpiLeftRange ? r->left : r->right);

         c_scopeArray *sa = is_scopeArray(obj);
         int64_t sleft, sright;
         if (sa != NULL && vpi_scope_array_bounds(sa, &sleft, &sright)) {
            c_constant *con =
               build_int_constant(type == vpiLeftRange ? sleft : sright);
            return user_handle_for(&(con->expr.object));
         }

         c_abstractDecl *d = is_valueDecl(obj);
         int64_t left, right;
         if (d != NULL && vpi_vector_bounds(d, &left, &right)) {
            c_constant *con =
               build_int_constant(type == vpiLeftRange ? left : right);
            return user_handle_for(&(con->expr.object));
         }

         return NULL;
      }

   case vpiTypespec:
      {
         // The enumeration type of a variable
         c_abstractDecl *d = is_valueDecl(obj);
         if (d == NULL || d->dtype == NULL
             || vlog_kind(d->dtype) != V_ENUM_DECL)
            return NULL;

         c_typespec *ts = new_object(sizeof(c_typespec), vpiEnumTypespec);
         ts->dtype = d->dtype;
         ts->object.loc = *vlog_loc(d->dtype);
         return user_handle_for(&(ts->object));
      }

   case vpiModule:
      {
         c_abstractScope *s = is_abstractScope(obj);
         s = s != NULL ? parent_scope(s) : vpi_obj_scope(obj);
         for (; s != NULL; s = parent_scope(s)) {
            if (s->object.type == vpiModule)
               return user_handle_for(&(s->object));
         }
         return NULL;
      }

   case vpiScope:
      {
         c_abstractScope *s = is_abstractScope(obj);
         s = s != NULL ? parent_scope(s) : vpi_obj_scope(obj);
         return s != NULL ? user_handle_for(&(s->object)) : NULL;
      }

   case vpiParent:
      {
         c_abstractDecl *d = is_abstractDecl(obj);
         if (d != NULL && d->parent != NULL)
            return user_handle_for(d->parent);

         c_abstractScope *s = is_abstractScope(obj);
         s = s != NULL ? parent_scope(s) : vpi_obj_scope(obj);
         return s != NULL ? user_handle_for(&(s->object)) : NULL;
      }

   case vpiActual:
   case vpiInterface:
      {
         c_misc *m = is_misc(obj);
         if (m != NULL && m->object.type == vpiRefObj) {
            c_vpiObject *actual = vpi_actual_iface(m);
            return actual != NULL ? user_handle_for(actual) : NULL;
         }
         else if (m != NULL && type == vpiInterface)
            return user_handle_for(&(m->scope->object));

         return NULL;
      }

   case vpiLowConn:
      {
         c_port *p = is_port(obj);
         return p != NULL && p->low != NULL ? user_handle_for(p->low) : NULL;
      }

   case vpiExpr:
      {
         c_port *p = is_port(obj);
         return p != NULL && p->low != NULL ? user_handle_for(p->low) : NULL;
      }

   default:
      return NULL;
   }
}

static c_vpiObject *vpi_select_index(c_vpiObject *obj, int64_t index)
{
   c_misc *ref = is_misc(obj);
   if (ref != NULL && obj->type == vpiRefObj) {
      c_vpiObject *actual = vpi_actual_iface(ref);
      return actual != NULL && is_scopeArray(actual) != NULL
         ? vpi_select_index(actual, index) : NULL;
   }

   c_scopeArray *sa = is_scopeArray(obj);
   if (sa != NULL) {
      for (int i = 0; i < sa->elems.count; i++) {
         c_abstractScope *s = is_abstractScope(sa->elems.items[i]);
         if (vpi_scope_index(s) == index)
            return &(s->object);
      }

      return NULL;
   }

   c_port *p = is_port(obj);
   if (p != NULL && p->low != NULL)
      obj = p->low;

   c_abstractDecl *d = is_abstractDecl(obj);
   if (d == NULL || d->object.type == vpiParameter)
      return NULL;

   vpiObjectList *elems = expand_lazy_list(obj, &(d->elems));
   for (int i = 0; i < elems->count; i++) {
      c_abstractDecl *e = is_abstractDecl(elems->items[i]);
      if (e != NULL && !vpi_is_member(e) && e->index == index)
         return &(e->object);
   }

   return NULL;
}

DLLEXPORT
vpiHandle vpi_handle_by_index(vpiHandle handle, PLI_INT32 index)
{
   vpi_clear_error();

   VPI_TRACE("handle=%s index=%d", handle_pp(handle), index);

   c_vpiObject *obj = from_handle(handle);
   if (obj == NULL)
      return NULL;

   c_vpiObject *elem = vpi_select_index(obj, index);
   if (elem == NULL) {
      vpi_error(vpiError, &(obj->loc), "index %d out of range", index);
      return NULL;
   }

   return user_handle_for(elem);
}

DLLEXPORT
vpiHandle vpi_handle_by_multi_index(vpiHandle obj, PLI_INT32 num_index,
                                    PLI_INT32 *index_array)
{
   vpi_clear_error();

   c_vpiObject *o = from_handle(obj);
   for (int i = 0; o != NULL && i < num_index; i++)
      o = vpi_select_index(o, index_array[i]);

   return o != NULL ? user_handle_for(o) : NULL;
}

static size_t vpi_match_name(const char *path, const char *name, bool fold)
{
   // Length of the name at the start of the path or zero if it does not
   // match a whole path element

   const size_t len = strlen(name);
   if (len == 0 || (fold ? strncasecmp(path, name, len)
                    : strncmp(path, name, len)) != 0)
      return 0;
   else if (name[0] == '\\')
      return len;   // Escaped name ends with a space
   else if (path[len] == '\0' || path[len] == '.' || path[len] == '['
            || (path[len] == ':' && path[len + 1] == ':'))
      return len;
   else
      return 0;
}

static void vpi_consider(c_vpiObject *cand, const char *path,
                         c_vpiObject **best, size_t *bestlen)
{
   const char *name = vpi_obj_name(cand);
   if (name == NULL)
      return;

   c_abstractScope *as = is_abstractScope(cand);
   const bool fold = as != NULL && as->vhdl_name;

   size_t len = vpi_match_name(path, name, fold);
   if (len == 0 && name[0] == '\\') {
      // Also match the identifier without escaping
      const size_t n = strlen(name);
      char *raw LOCAL = xstrndup(name + 1, n - 2);
      len = vpi_match_name(path, raw, fold);
   }

   if (len > *bestlen) {
      *best = cand;
      *bestlen = len;
   }
}

static c_vpiObject *vpi_find_child(c_vpiObject *parent, const char *path,
                                   size_t *matched)
{
   c_vpiObject *best = NULL;
   size_t bestlen = 0;

   if (parent == NULL) {
      vpiObjectList *tops = vpi_top_objects();
      for (int i = 0; i < tops->count; i++)
         vpi_consider(tops->items[i], path, &best, &bestlen);
   }
   else if (is_abstractScope(parent) != NULL) {
      c_abstractScope *s = is_abstractScope(parent);

      vpiObjectList *decls = expand_lazy_list(parent, &(s->decls));
      for (int i = 0; i < decls->count; i++)
         vpi_consider(decls->items[i], path, &best, &bestlen);

      vpiObjectList *scopes = expand_lazy_list(parent, &(s->scopes));
      for (int i = 0; i < scopes->count; i++) {
         vpi_consider(scopes->items[i], path, &best, &bestlen);

         c_scopeArray *sa = is_scopeArray(scopes->items[i]);
         for (int j = 0; sa != NULL && j < sa->elems.count; j++)
            vpi_consider(sa->elems.items[j], path, &best, &bestlen);
      }

      vpiObjectList *misc = expand_lazy_list(parent, &(s->misc));
      for (int i = 0; i < misc->count; i++) {
         if (best == NULL || is_port(misc->items[i]) == NULL)
            vpi_consider(misc->items[i], path, &best, &bestlen);
      }
   }
   else if (is_valueDecl(parent) != NULL) {
      c_abstractDecl *d = is_valueDecl(parent);
      vpiObjectList *elems = expand_lazy_list(parent, &(d->elems));
      for (int i = 0; i < elems->count; i++) {
         c_abstractDecl *e = is_abstractDecl(elems->items[i]);
         if (e != NULL && vpi_is_member(e))
            vpi_consider(elems->items[i], path, &best, &bestlen);
      }
   }
   else if (is_misc(parent) != NULL && parent->type == vpiRefObj) {
      c_vpiObject *actual = vpi_actual_iface(is_misc(parent));
      if (actual != NULL)
         return vpi_find_child(actual, path, matched);
   }

   *matched = bestlen;
   return best;
}

static c_vpiObject *vpi_lookup_name(const char *path, c_vpiObject *scope)
{
   c_vpiObject *obj = scope;

   if (scope == NULL && strncmp(path, "$root.", 6) == 0)
      path += 6;

   for (;;) {
      size_t len;
      c_vpiObject *child = vpi_find_child(obj, path, &len);
      if (child == NULL)
         return NULL;

      obj = child;
      path += len;

      while (*path == '[') {
         char *end;
         const int64_t index = strtoll(path + 1, &end, 10);
         if (end == path + 1 || *end != ']')
            return NULL;

         if ((obj = vpi_select_index(obj, index)) == NULL)
            return NULL;

         path = end + 1;
      }

      if (*path == '\0')
         return obj;
      else if (*path == '.')
         path++;
      else if (path[0] == ':' && path[1] == ':')
         path += 2;
      else
         return NULL;
   }
}

DLLEXPORT
vpiHandle vpi_handle_by_name(PLI_BYTE8 *name, vpiHandle scope)
{
   vpi_clear_error();

   VPI_TRACE("name=%s scope=%s", name, handle_pp(scope));

   c_vpiObject *sobj = NULL;
   if (scope != NULL && (sobj = from_handle(scope)) == NULL)
      return NULL;

   c_vpiObject *obj = vpi_lookup_name(name, sobj);
   return obj != NULL ? user_handle_for(obj) : NULL;
}

static bool vpi_type_matches(PLI_INT32 rel, c_vpiObject *obj)
{
   switch (rel) {
   case vpiVariables:
      return is_valueDecl(obj) != NULL && !is_valueDecl(obj)->is_net
         && obj->type != vpiNamedEvent;
   default:
      return obj->type == rel;
   }
}

static bool init_iterator(c_iterator *it, PLI_INT32 type, c_vpiObject *obj)
{
   vpiObjectList *list = NULL;
   vpiObjectList filtered = {};

   if (obj == NULL) {
      vpiObjectList *tops = vpi_top_objects();
      for (int i = 0; i < tops->count; i++) {
         c_vpiObject *t = tops->items[i];
         if (type == vpiInstance || t->type == type)
            vpi_list_push(&filtered, t);
      }
   }
   else if (is_tfCall(obj) != NULL) {
      if (type == vpiArgument)
         list = &(is_tfCall(obj)->args);
   }
   else if (is_typespec(obj) != NULL) {
      if (type == vpiEnumConst) {
         vlog_node_t dtype = is_typespec(obj)->dtype;
         const int ndecls = vlog_decls(dtype);
         for (int i = 0; i < ndecls; i++) {
            c_enumConst *ec = new_object(sizeof(c_enumConst), vpiEnumConst);
            ec->where = vlog_decl(dtype, i);
            ec->object.loc = *vlog_loc(ec->where);
            vpi_list_push(&filtered, &(ec->object));
         }
      }
   }
   else if (is_process(obj) != NULL) {
      c_process *p = is_process(obj);
      vpiObjectList *decls = expand_lazy_list(obj, &(p->decls));
      for (int i = 0; i < decls->count; i++) {
         if (vpi_type_matches(type, decls->items[i]))
            vpi_list_push(&filtered, decls->items[i]);
      }
   }
   else if (is_abstractScope(obj) != NULL) {
      c_abstractScope *s = is_abstractScope(obj);

      switch (type) {
      case vpiProcess:
      case vpiInitial:
      case vpiAlways:
      case vpiFinal:
         {
            vpiObjectList *procs = expand_lazy_list(obj, &(s->procs));
            for (int i = 0; i < procs->count; i++) {
               if (type == vpiProcess || procs->items[i]->type == type)
                  vpi_list_push(&filtered, procs->items[i]);
            }
         }
         break;

      case vpiModule:
      case vpiInterface:
      case vpiProgram:
      case vpiInternalScope:
      case vpiGenScope:
      case vpiNamedBegin:
      case vpiNamedFork:
      case vpiModuleArray:
      case vpiInterfaceArray:
      case vpiGenScopeArray:
         {
            vpiObjectList *scopes = expand_lazy_list(obj, &(s->scopes));
            for (int i = 0; i < scopes->count; i++) {
               c_vpiObject *child = scopes->items[i];
               c_scopeArray *sa = is_scopeArray(child);
               if (sa != NULL) {
                  if (child->type == type)
                     vpi_list_push(&filtered, child);

                  for (int j = 0; j < sa->elems.count; j++) {
                     c_vpiObject *e = sa->elems.items[j];
                     if (type == vpiInternalScope || e->type == type)
                        vpi_list_push(&filtered, e);
                  }
               }
               else if (type == vpiInternalScope || child->type == type)
                  vpi_list_push(&filtered, child);
            }
         }
         break;

      case vpiPort:
      case vpiModport:
      case vpiRefObj:
      case vpiGate:
      case vpiPrimitive:
         {
            vpiObjectList *misc = expand_lazy_list(obj, &(s->misc));
            for (int i = 0; i < misc->count; i++) {
               const PLI_INT32 mtype = misc->items[i]->type;
               if (mtype == type || (type == vpiPrimitive && mtype == vpiGate))
                  vpi_list_push(&filtered, misc->items[i]);
            }
         }
         break;

      case vpiMemory:
         break;

      default:
         {
            vpiObjectList *decls = expand_lazy_list(obj, &(s->decls));
            for (int i = 0; i < decls->count; i++) {
               if (vpi_type_matches(type, decls->items[i]))
                  vpi_list_push(&filtered, decls->items[i]);
            }
         }
         break;
      }
   }
   else if (is_scopeArray(obj) != NULL && type == vpiRange) {
      int64_t left, right;
      if (vpi_scope_array_bounds(is_scopeArray(obj), &left, &right))
         vpi_list_push(&filtered, vpi_range_object(
                          &(build_int_constant(left)->expr.object),
                          &(build_int_constant(right)->expr.object)));
   }
   else if (is_scopeArray(obj) != NULL) {
      c_scopeArray *sa = is_scopeArray(obj);
      for (int i = 0; i < sa->elems.count; i++) {
         c_vpiObject *e = sa->elems.items[i];
         if (type == vpiInternalScope || e->type == type)
            vpi_list_push(&filtered, e);
      }
   }
   else if (is_abstractDecl(obj) != NULL && obj->type != vpiParameter) {
      c_abstractDecl *d = is_abstractDecl(obj);

      if (type == vpiRange) {
         int64_t left, right;
         c_regArray *arr = is_regArray(obj);
         if (arr != NULL) {
            const int nranges = vlog_ranges(d->where);
            for (int i = d->dim; i < nranges; i++) {
               if (vlog_bounds(vlog_range(d->where, i), &left, &right))
                  vpi_list_push(&filtered, vpi_range_object(
                                   &(build_int_constant(left)->expr.object),
                                   &(build_int_constant(right)->expr.object)));
            }
         }
         else {
            for (int i = d->dim; vpi_packed_dim(d, i) != NULL; i++) {
               if (vlog_bounds(vpi_packed_dim(d, i), &left, &right))
                  vpi_list_push(&filtered, vpi_range_object(
                                   &(build_int_constant(left)->expr.object),
                                   &(build_int_constant(right)->expr.object)));
            }

            if (filtered.count == 0 && vpi_vector_bounds(d, &left, &right))
               vpi_list_push(&filtered, vpi_range_object(
                                &(build_int_constant(left)->expr.object),
                                &(build_int_constant(right)->expr.object)));
         }
      }
      else {
         vpiObjectList *elems = expand_lazy_list(obj, &(d->elems));
         for (int i = 0; i < elems->count; i++) {
            c_abstractDecl *e = is_abstractDecl(elems->items[i]);
            const bool member = vpi_is_member(e);
            bool match;
            switch (type) {
            case vpiMember:
               match = member;
               break;
            case vpiBit:
            case vpiNetBit:
            case vpiRegBit:
            case vpiVarSelect:
            case vpiElement:
            case vpiMemoryWord:
               match = !member;
               break;
            case vpiReg:
            case vpiNet:
            case vpiRegArray:
            case vpiNetArray:
               match = !member && is_regArray(obj) != NULL;
               break;
            default:
               match = !member && e->object.type == type;
               break;
            }

            if (match)
               vpi_list_push(&filtered, elems->items[i]);
         }
      }
   }
   else if (is_port(obj) != NULL && is_port(obj)->low != NULL)
      return init_iterator(it, type, is_port(obj)->low);

   if (list == NULL) {
      if (filtered.count == 0)
         return false;

      vpiObjectList *copy = pool_malloc(vpi_context()->pool,
                                        sizeof(vpiObjectList));
      *copy = filtered;
      list = copy;
   }

   it->list = list;
   return true;
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
      if (obj != NULL && is_tfCall(obj) != NULL)
         vpi_error(vpiError, &(obj->loc), "relation %s not supported for "
                   "handle %s", vpi_method_str(type), handle_pp(refHandle));
      APUSH(vpi_context()->recycle, &(it->refcounted.object));
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

static int class_handle_bits(uint64_t id)
{
   // A handle has no declared width so its object number is formatted
   // as a 32-bit unsigned value, or a 64-bit one if it does not fit
   return id < (UINT64_C(1) << 32) ? 32 : 64;
}

static PLI_INT32 vpi_get_scope(PLI_INT32 property, c_abstractScope *s);

static PLI_INT32 vpi_get_decl(PLI_INT32 property, c_abstractDecl *d)
{
   c_regArray *arr = is_regArray(&(d->object));

   switch (property) {
   case vpiTimeUnit:
   case vpiTimePrecision:
      // The time unit and precision of the scope that contains the object
      // (36.19), which is also the unit of the value of a time variable
      if (d->scope != NULL)
         return vpi_get_scope(property, d->scope);
      break;
   case vpiSize:
      if (arr != NULL)
         return arr->count;
      else if (d->storage == STORE_REAL || d->storage == STORE_PREAL)
         return d->object.type == vpiShortRealVar ? 32 : 64;
      else if (d->storage == STORE_STRING) {
         const ffi_uarray_t *u = vpi_get_ptr(d);
         return u != NULL ? ffi_array_length(u->dims[0].length) : 0;
      }
      else
         return d->size;
   case vpiSigned:
      return arr == NULL && vpi_is_signed(d);
   case vpiVector:
      {
         int64_t left, right;
         return arr == NULL && d->storage != STORE_REAL
            && d->storage != STORE_PREAL && d->storage != STORE_STRING
            && vpi_vector_bounds(d, &left, &right);
      }
   case vpiScalar:
      return arr == NULL && d->size == 1 && d->storage != STORE_REAL
         && d->storage != STORE_PREAL && d->storage != STORE_STRING
         && d->storage != STORE_NONE;
   case vpiArray:
      return arr != NULL;
   case vpiArrayType:
      return arr != NULL ? vpiStaticArray : vpiUndefined;
   case vpiPacked:
      return d->dtype != NULL && arr == NULL && vpi_is_packed_type(d->dtype);
   case vpiNetType:
      return d->is_net ? vpiWire : vpiUndefined;
   case vpiConstantSelect:
      return d->parent != NULL;
   case vpiAutomatic:
   case vpiConstantVariable:
   case vpiIsRandomized:
      return 0;
   case vpiRandType:
      return vpiNotRand;
   case vpiVisibility:
      return vpiPublicVis;
   case vpiLineNo:
      return d->object.loc.first_line;
   case vpiLocalParam:
      if (d->object.type == vpiParameter)
         return vlog_kind(d->where) == V_LOCALPARAM
            && !(vlog_flags(d->where) & VLOG_F_OVERRIDABLE);
      break;
   case vpiConstType:
      if (d->object.type == vpiParameter) {
         c_constant *con = is_constantOrParam(&(d->object));
         return con != NULL ? con->subtype : vpiUndefined;
      }
      break;
   case vpiDirection:
      break;
   }

   return vpiUndefined - 1;
}

static PLI_INT32 vpi_get_scope(PLI_INT32 property, c_abstractScope *s)
{
   vpi_context_t *c = vpi_context();

   switch (property) {
   case vpiTopModule:
      return s->object.type == vpiModule && parent_scope(s) == NULL;
   case vpiTimeUnit:
      return vpi_log10(MAX(VLOG_TIME_UNIT(vpi_scope_ival(s)), 1)) - 15;
   case vpiTimePrecision:
      return (int)VLOG_TIME_LOG10(vpi_scope_ival(s)) - 15;
   case vpiLineNo:
      return s->object.loc.first_line;
   case vpiCellInstance:
   case vpiProtected:
   case vpiAutomatic:
      return 0;
   case vpiTop:
      return parent_scope(s) == NULL;
   case vpiDefNetType:
      return vpiWire;
   case vpiUnconnDrive:
      return vpiHighZ;
   case vpiDefDelayMode:
      return vpiDelayModeNone;
   case vpiDefDecayTime:
      return 0;
   case vpiSize:
      return 0;
   default:
      (void)c;
      return vpiUndefined - 1;
   }
}

DLLEXPORT
PLI_INT32 vpi_get(PLI_INT32 property, vpiHandle object)
{
   vpi_clear_error();

   VPI_TRACE("property=%s object=%s", vpi_property_str(property),
             handle_pp(object));

   vpi_context_t *c = vpi_context();

   if (object == NULL) {
      switch (property) {
      case vpiTimePrecision:
         return c->precision - 15;
      case vpiTimeUnit:
         return c->precision - 15;
      default:
         vpi_error(vpiError, NULL, "property %s requires an object",
                   vpi_property_str(property));
         return vpiUndefined;
      }
   }

   c_vpiObject *obj = from_handle(object);
   if (obj == NULL)
      return vpiUndefined;

   if (property == vpiType)
      return obj->type;

   PLI_INT32 result = vpiUndefined - 1;

   c_constant *con = obj->type == vpiConstant ? is_constantOrParam(obj) : NULL;
   if (con != NULL) {
      switch (property) {
      case vpiConstType:
         return con->subtype;
      case vpiSize:
         return con->size;
      case vpiSigned:
         return con->expr.where == NULL || vlog_is_signed(con->expr.where);
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

   c_parameter *param = is_parameter(obj);
   if (param != NULL && property == vpiSize) {
      c_constant *pc = is_constantOrParam(obj);
      if (pc != NULL)
         return pc->size;
   }
   else if (param != NULL && property == vpiSigned) {
      c_constant *pc = is_constantOrParam(obj);
      if (pc != NULL)
         return pc->expr.where == NULL || vlog_is_signed(pc->expr.where);
   }

   c_abstractDecl *d = is_abstractDecl(obj);
   if (d != NULL && (result = vpi_get_decl(property, d)) != vpiUndefined - 1)
      return result;

   c_abstractScope *s = is_abstractScope(obj);
   if (s != NULL && (result = vpi_get_scope(property, s)) != vpiUndefined - 1)
      return result;

   c_process *proc = is_process(obj);
   if (proc != NULL) {
      switch (property) {
      case vpiLineNo:
         return proc->object.loc.first_line;
      case vpiAlwaysType:
         if (obj->type != vpiAlways)
            goto missing_property;
         switch (vlog_subkind(proc->where)) {
         case V_ALWAYS_COMB:
         case V_ALWAYS_COMB_DONE:  return vpiAlwaysComb;
         case V_ALWAYS_FF:    return vpiAlwaysFF;
         case V_ALWAYS_LATCH:
         case V_ALWAYS_LATCH_DONE: return vpiAlwaysLatch;
         default:             return vpiAlways;
         }
      case vpiNvcProcState:
         {
            rt_proc_t *rp = vpi_find_process(proc);
            if (rp == NULL)
               return 0;

            int state = 0;
            if (get_active_proc() == rp)
               state |= vpiNvcProcRunning;
            if (rp->wakeable.pending)
               state |= vpiNvcProcRunnable;
            if (rp->wakeable.delayed)
               state |= vpiNvcProcTimeout;
            if (rp->ended && (vlog_kind(proc->where) == V_INITIAL
                              || vlog_kind(proc->where) == V_FINAL))
               state |= vpiNvcProcEnded;
            return state;
         }
      }
   }

   c_scopeArray *sa = is_scopeArray(obj);
   if (sa != NULL) {
      switch (property) {
      case vpiSize:
         return sa->elems.count;
      case vpiArray:
         return 1;
      }
   }

   c_port *p = is_port(obj);
   if (p != NULL) {
      switch (property) {
      case vpiDirection:
         switch (vlog_subkind(p->where)) {
         case V_PORT_INPUT:  return vpiInput;
         case V_PORT_OUTPUT: return vpiOutput;
         case V_PORT_INOUT:  return vpiInout;
         default:            return vpiNoDirection;
         }
      case vpiSize:
         if (p->low != NULL)
            return vpi_get_decl(vpiSize, is_abstractDecl(p->low));
         else
            return vlog_size(vlog_type(p->where));
      case vpiPortIndex:
         return p->index;
      case vpiScalar:
         return vlog_size(vlog_type(p->where)) == 1;
      case vpiVector:
         return vlog_size(vlog_type(p->where)) > 1;
      case vpiExplicitName:
      case vpiConnByName:
         return 0;
      case vpiPortType:
         return 0;
      }
   }

   c_misc *m = is_misc(obj);
   if (m != NULL) {
      switch (property) {
      case vpiPortType:
         return m->object.type == vpiRefObj ? vpiInterfacePort : vpiUndefined;
      case vpiDirection:
         return vpiNoDirection;
      case vpiHasActual:
         return m->object.type == vpiRefObj && vpi_actual_iface(m) != NULL;
      }
   }

 missing_property:
   vpi_error(vpiError, &(obj->loc), "object does not have property %s",
             vpi_property_str(property));
   return vpiUndefined;
}

DLLEXPORT
PLI_INT64 vpi_get64(PLI_INT32 property, vpiHandle object)
{
   return vpi_get(property, object);
}

DLLEXPORT
PLI_BYTE8 *vpi_get_str(PLI_INT32 property, vpiHandle object)
{
   vpi_clear_error();

   VPI_TRACE("property=%s object=%s", vpi_property_str(property),
             handle_pp(object));

   c_vpiObject *obj = from_handle(object);
   if (obj == NULL)
      return NULL;

   switch (property) {
   case vpiType:
      return (PLI_BYTE8 *)vpi_type_str(obj->type);

   case vpiName:
      return (PLI_BYTE8 *)vpi_obj_name(obj);

   case vpiFullName:
      return (PLI_BYTE8 *)vpi_obj_fullname(obj);

   case vpiDefName:
      {
         c_abstractScope *s = is_abstractScope(obj);
         if (s == NULL)
            break;
         else if (obj->type == vpiForeignInst) {
            tree_t vb = s->where != NULL ? vpi_vhdl_block(s->block) : NULL;
            if (vb != NULL)
               return (PLI_BYTE8 *)istr(tree_ident(vb));

            // A VHDL scope above Verilog instances: the unit it is an
            // instance of
            tree_t u = tree_ref(tree_decl(s->block, 0));
            if (tree_kind(u) == T_ARCH)
               u = tree_primary(u);
            return (PLI_BYTE8 *)istr(ident_rfrom(tree_ident(u), '.')
                                     ?: tree_ident(u));
         }
         else if (is_instance(obj) != NULL || obj->type == vpiPackage)
            return (PLI_BYTE8 *)istr(vlog_ident2(s->where));
         else
            return (PLI_BYTE8 *)s->name;
      }

   case vpiDefFile:
      {
         c_abstractScope *s = is_abstractScope(obj);
         if (s != NULL && s->block != NULL) {
            tree_t hier = tree_decl(s->block, 0);
            return (PLI_BYTE8 *)loc_file_str(tree_loc(tree_ref(hier)));
         }
         else if (s != NULL)
            return (PLI_BYTE8 *)loc_file_str(vlog_loc(s->where));
      }
      break;

   case vpiFile:
      if (!loc_invalid_p(&(obj->loc)))
         return (PLI_BYTE8 *)loc_file_str(&(obj->loc));
      break;

   case vpiDecompile:
      {
         c_abstractDecl *d = is_abstractDecl(obj);
         if (d != NULL)
            return (PLI_BYTE8 *)vpi_obj_name(obj);
      }
      break;
   }

   vpi_error(vpiError, &(obj->loc), "object does not have string property %s",
             vpi_property_str(property));
   return NULL;
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

   if (value_p->format == vpiSuppressVal)
      return;

   c_port *p = is_port(obj);
   if (p != NULL && p->low != NULL)
      obj = p->low;

   c_enumConst *ec = is_enumConst(obj);
   if (ec != NULL) {
      if (vlog_has_value(ec->where) && vlog_kind(vlog_value(ec->where)) == V_NUMBER) {
         number_t n = vlog_number(vlog_value(ec->where));
         const uint64_t *abits, *bbits;
         number_get(n, &abits, &bbits);
         if (value_p->format == vpiObjTypeVal)
            value_p->format = number_width(n) <= 32 ? vpiIntVal : vpiVectorVal;
         vpi_format_bits(NULL, number_width(n), number_signed(n), abits,
                         bbits, value_p, c->valuestr);
      }
      return;
   }

   c_constant *con = is_constantOrParam(obj);
   if (con != NULL) {
      vpi_format_constant(con, value_p);
      return;
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

   c_abstractDecl *decl = is_valueDecl(obj);
   if (decl != NULL && vpi_value_decl(decl, value_p))
      return;

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

   c_port *p = is_port(obj);
   if (p != NULL && p->low != NULL)
      obj = p->low;

   c_abstractDecl *decl = is_valueDecl(obj);
   if (decl != NULL) {
      if (vpi_put_decl(decl, value_p, time_p, flags)
          && (flags & 0xff) == vpiReleaseFlag && value_p != NULL)
         vpi_value_decl(decl, value_p);
      return NULL;
   }

   vpi_error(vpiError, &(obj->loc), "cannot change value of %s",
             handle_pp(handle));
   return NULL;
}

DLLEXPORT
void vpi_get_time(vpiHandle object, p_vpi_time time_p)
{
   vpi_clear_error();

   VPI_TRACE("object=%s time_p=%p", handle_pp(object), time_p);

   vpi_context_t *c = vpi_context();

   c_vpiObject *obj = NULL;
   if (object != NULL && (obj = from_handle(object)) == NULL)
      return;

   const int64_t now = c->model != NULL ? model_now(c->model, NULL) : 0;
   vpi_fill_time(obj, now, time_p);
}

DLLEXPORT
PLI_INT32 vpi_control(PLI_INT32 operation, ...)
{
   vpi_clear_error();

   VPI_TRACE("operation=%d", operation);

   vpi_context_t *c = vpi_context();

   switch (operation) {
   case vpiStop:
   case vpiFinish:
      {
         if (c->model == NULL) {
            vpi_error(vpiError, NULL, "simulation has not started");
            return 0;
         }

         // There is no interactive mode so stopping also ends the
         // simulation
         notef("VPI application requested end of simulation");

         model_finish(c->model);
         model_stop(c->model);
         return 1;
      }
   case vpiReset:
   case vpiSetInteractiveScope:
      vpi_error(vpiError, NULL, "vpi_control operation %d is not supported",
                operation);
      return 0;
   default:
      vpi_error(vpiError, NULL, "invalid vpi_control operation %d",
                operation);
      return 0;
   }
}

DLLEXPORT
PLI_INT32 vpi_get_vlog_info(p_vpi_vlog_info vlog_info_p)
{
   vpi_clear_error();

   vpi_context_t *c = vpi_context();

   vlog_info_p->argc    = c->argc;
   vlog_info_p->argv    = c->argv;
   vlog_info_p->product = (PLI_BYTE8 *)PACKAGE_NAME;
   vlog_info_p->version = (PLI_BYTE8 *)PACKAGE_VERSION;

   return 1;
}

DLLEXPORT
PLI_INT32 vpi_vprintf(PLI_BYTE8 *format, va_list ap)
{
   char *buf LOCAL = xvasprintf(format, ap);
   ostream_puts(nvc_stdout(), buf);
   fflush(stdout);
   return strlen(buf);
}

DLLEXPORT
PLI_INT32 vpi_printf(PLI_BYTE8 *format, ...)
{
   va_list ap;
   va_start(ap, format);
   const PLI_INT32 len = vpi_vprintf(format, ap);
   va_end(ap);
   return len;
}

DLLEXPORT
PLI_INT32 vpi_flush(void)
{
   fflush(stdout);
   return 0;
}

DLLEXPORT
PLI_INT32 vpi_mcd_vprintf(PLI_UINT32 mcd, PLI_BYTE8 *format, va_list ap)
{
   // Only the standard output channel is available
   if (!(mcd & 1))
      return 0;

   return vpi_vprintf(format, ap);
}

DLLEXPORT
PLI_INT32 vpi_mcd_printf(PLI_UINT32 mcd, PLI_BYTE8 *format, ...)
{
   va_list ap;
   va_start(ap, format);
   const PLI_INT32 len = vpi_mcd_vprintf(mcd, format, ap);
   va_end(ap);
   return len;
}

DLLEXPORT
PLI_INT32 vpi_mcd_flush(PLI_UINT32 mcd)
{
   fflush(stdout);
   return 0;
}

DLLEXPORT
void vpi_get_systf_info(vpiHandle object, p_vpi_systf_data systf_data_p)
{
   vpi_clear_error();

   c_vpiObject *obj = from_handle(object);
   if (obj == NULL)
      return;

   c_sysTfCall *call = is_sysTfCall(obj);
   c_callback *cb = call != NULL ? call->callback : is_callback(obj);
   if (cb != NULL && cb->systf.tfname != NULL)
      *systf_data_p = cb->systf;
   else
      vpi_error(vpiError, &(obj->loc), "object is not a system task or "
                "function");
}

DLLEXPORT
PLI_INT32 vpi_compare_objects(vpiHandle object1, vpiHandle object2)
{
   vpi_clear_error();

   c_vpiObject *a = from_handle(object1), *b = from_handle(object2);
   if (a == NULL || b == NULL)
      return 0;
   else if (a == b)
      return 1;
   else if (a->type != b->type)
      return 0;

   c_abstractDecl *da = is_abstractDecl(a), *db = is_abstractDecl(b);
   if (da != NULL && db != NULL)
      return da->where == db->where && da->scope == db->scope
         && da->offset == db->offset && da->size == db->size
         && da->dim == db->dim;

   c_misc *ma = is_misc(a), *mb = is_misc(b);
   if (ma != NULL && mb != NULL)
      return ma->where == mb->where && ma->scope == mb->scope;

   c_port *pa = is_port(a), *pb = is_port(b);
   if (pa != NULL && pb != NULL)
      return pa->where == pb->where && pa->scope == pb->scope;

   return 0;
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

   c->precision = vlog_design_precision();
   if (c->precision < 0 && tree_kind(top) == T_ELAB)
      c->precision = tree_ival(top);
   if (c->precision < 0)
      c->precision = 6;

   // The command line seen by the application is the name of the tool
   // followed by the plusargs
   int nplusargs = 0;
   while (vpi_get_plusarg(nplusargs) != NULL)
      nplusargs++;

   c->argc = nplusargs + 1;
   c->argv = pool_malloc_array(c->pool, nplusargs + 2, sizeof(char *));
   c->argv[0] = (char *)PACKAGE_NAME;
   for (int i = 0; i < nplusargs; i++)
      c->argv[i + 1] = (char *)vpi_get_plusarg(i);
   c->argv[nplusargs + 1] = NULL;

   for (int i = 0; i < c->pending.count; i++) {
      handle_slot_t *slot = decode_handle(c, c->pending.items[i]);
      if (slot != NULL)
         vpi_arm_callback(c, c->pending.items[i]);
   }
   ACLEAR(c->pending);
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
   ACLEAR(c->pending);

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
   free(c->vecbuf);
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

const char *vpi_caller_scope_name(void)
{
   vpi_context_t *c = vpi_context();
   assert(c->call != NULL);

   c_abstractScope *scope = c->call->tfcall.scope;
   c_module *module = scope != NULL ? is_module(&(scope->object)) : NULL;
   if (module == NULL)
      return NULL;

   return istr(tree_ident(module->scope.block));
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

static rt_scope_t *hdl_rt_scope(c_abstractScope *scope)
{
   return scope->rtscope;
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

static bool hdl_lookup(const char *path, hdl_target_t *t)
{
   vpi_context_t *c = vpi_context();
   rt_model_t *model = vpi_get_model(c);

   if (!strncmp(path, "$root.", 6))
      path += 6;

   rt_scope_t *rt = root_scope(model);
   c_abstractScope *scope = NULL;

   for (;;) {
      const char *end = path;
      while (*end != '\0' && *end != '.' && *end != '[')
         end++;

      // A block of a generate loop is named with its index: gen[0].x
      if (*end == '[') {
         const char *close = strchr(end, ']');
         if (close != NULL && close[1] == '.') {
            c_abstractScope *child = hdl_child_scope(rt, path, close + 1 - path);
            rt_scope_t *crt = child ? hdl_rt_scope(child) : NULL;
            if (crt != NULL) {
               scope = child;
               rt = crt;
               path = close + 2;
               continue;
            }
         }
      }

      const size_t len = end - path;
      if (len == 0)
         return false;

      if (*end == '.') {
         c_abstractScope *child = hdl_child_scope(rt, path, len);
         rt_scope_t *crt = child ? hdl_rt_scope(child) : NULL;
         if (crt != NULL) {
            scope = child;
            rt = crt;
            path = end + 1;
            continue;
         }
         else if (scope == NULL)
            return false;

         // Otherwise a member of a structure
      }

      if (scope == NULL)
         return false;

      c_abstractDecl *decl = hdl_find_decl(scope, path, len);
      if (decl == NULL)
         return false;

      int64_t hi, lo;
      sig_shared_t **ss;
      c_regArray *arr = is_regArray(&(decl->object));
      if (arr != NULL) {
         if (*end != '[' || !hdl_parse_select(&end, &hi, &lo) || hi != lo)
            return false;

         if (arr->decl.storage != STORE_SIGNAL)
            return false;

         int64_t aleft, aright;
         vpi_array_bounds(arr, &aleft, &aright);

         const int64_t count = aleft > aright ? aleft - aright + 1
            : aright - aleft + 1;
         const int64_t low = aleft < aright ? aleft : aright;
         if (hi < low || hi >= low + count)
            return false;

         const int size = vlog_size(vlog_type(decl->where));
         if ((ss = vpi_get_ptr(decl)) == NULL)
            return false;
         t->offset = (aleft > aright ? (count - 1 - (hi - low)) : (hi - low))
            * size;
         t->count = size;

         if (*end == '[') {
            if (!hdl_parse_select(&end, &hi, &lo)
                || !hdl_select(t, decl->where, size, hi, lo))
               return false;
         }
      }
      else {
         if (is_valueDecl(&(decl->object)) == NULL
             || decl->storage != STORE_SIGNAL)
            return false;

         int size = decl->size;
         t->offset = decl->offset;

         if ((ss = vpi_get_ptr(decl)) == NULL)
            return false;
         t->count = size;

         vlog_node_t mdecl = decl->where;
         while (*end == '.') {
            // A member of a packed structure
            vlog_node_t st = vlog_get_type(mdecl);
            if (st == NULL || (vlog_kind(st) != V_STRUCT_DECL
                               && vlog_kind(st) != V_UNION_DECL)
                || !(vlog_flags(st) & VLOG_F_PACKED))
               return false;

            const char *name = end + 1;
            const char *mend = name;
            while (*mend != '\0' && *mend != '.' && *mend != '[')
               mend++;

            vlog_node_t member = NULL;
            const int ndecls = vlog_decls(st);
            for (int i = 0; i < ndecls && member == NULL; i++) {
               vlog_node_t d = vlog_decl(st, i);
               if (vlog_has_ident(d)
                   && strlen(istr(vlog_ident(d))) == (size_t)(mend - name)
                   && strncmp(istr(vlog_ident(d)), name, mend - name) == 0)
                  member = d;
            }

            if (member == NULL)
               return false;

            t->offset += vlog_packed_offset(st, member);
            size = vlog_size(vlog_type(member));
            t->count = size;
            mdecl = member;
            end = mend;
         }

         if (*end == '[') {
            if (!hdl_parse_select(&end, &hi, &lo)
                || !hdl_select(t, mdecl, size, hi, lo))
               return false;
         }
      }

      if (*end != '\0')
         return false;

      t->signal = container_of(*ss, rt_signal_t, shared);
      t->is_var = vlog_kind(decl->where) == V_VAR_DECL
         && !(vlog_flags(decl->where) & VLOG_F_VHDL);
      return true;
   }
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
