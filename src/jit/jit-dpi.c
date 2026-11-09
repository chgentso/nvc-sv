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

#ifdef __MINGW32__
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "util.h"
#include "hash.h"
#include "ident.h"
#include "tree.h"
#include "jit/jit-dpi.h"
#include "jit/jit-ffi.h"
#include "jit/jit-priv.h"
#include "jit/jit.h"
#include "rt/model.h"
#include "rt/mspace.h"
#include "rt/structs.h"
#include "vpi/svdpi.h"
#include "vpi/vpi-model.h"

#include <assert.h>
#include <stdarg.h>
#include <ctype.h>
#include <strings.h>
#include <ffi.h>
#include <stdlib.h>
#include <string.h>

#ifndef __MINGW32__
#include <regex.h>
#endif

#ifndef __MINGW32__
#include <dlfcn.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>
#define DPI_FIBERS 1
#else
#define DPI_FIBERS 1
#endif

#if ASAN_ENABLED
#include <sanitizer/common_interface_defs.h>
#endif

#define DPI_MAX_ARGS 48

typedef struct {
   char kind;
   char dir;
   bool issigned;
   int  width;
   char ekind;     // Element kind of an array argument
   int  count;     // Number of elements of a sized array argument
} dpi_arg_t;

typedef struct _dpi_entry {
   char      *cname;
   char      *scope;
   void      *fn;
   int        nargs;
   dpi_arg_t  ret;
   dpi_arg_t  args[DPI_MAX_ARGS];
   ffi_cif    cif;
   ffi_type  *types[DPI_MAX_ARGS];
   bool       is_context;
   bool       is_task;
} dpi_entry_t;

typedef union {
   int64_t        i64;
   uint64_t       u64;
   int8_t         i8;
   uint8_t        u8;
   int16_t        i16;
   uint16_t       u16;
   int32_t        i32;
   uint32_t       u32;
   double         d;
   float          f;
   void          *p;
   const char    *s;
} dpi_slot_t;

typedef struct _dpi_lib dpi_lib_t;

struct _dpi_lib {
   dpi_lib_t *next;
   char      *name;
   void      *handle;
};

static dpi_lib_t *dpi_pending = NULL;
static dpi_lib_t *dpi_loaded = NULL;
static shash_t   *dpi_cache = NULL;
static char      *dpi_strings[DPI_MAX_ARGS + 1];

typedef struct _dpi_scope {
   void *context;
   char *name;
   char *fullname;
   void *user_keys[8];
   void *user_data[8];
   int   nuser;
   struct _dpi_scope *next;
} dpi_scope_t;

static dpi_scope_t *dpi_current = NULL;
static const struct _dpi_entry *dpi_import = NULL;
static dpi_scope_t *dpi_all_scopes = NULL;
static hash_t      *dpi_scopes = NULL;

static dpi_scope_t *dpi_scope_get(void *context, const char *name)
{
   if (dpi_scopes == NULL)
      dpi_scopes = hash_new(64);

   dpi_scope_t *s = hash_get(dpi_scopes, context);
   if (s == NULL) {
      s = xcalloc(sizeof(dpi_scope_t));
      s->context = context;
      s->next = dpi_all_scopes;
      dpi_all_scopes = s;
      hash_put(dpi_scopes, context, s);
   }

   if (s->name == NULL && name != NULL)
      s->name = xstrdup(name);

   return s;
}

void dpi_add_lib(const char *name)
{
   dpi_lib_t *lib = xcalloc(sizeof(dpi_lib_t));
   lib->name = xstrdup(name);

   dpi_lib_t **where;
   for (where = &dpi_pending; *where != NULL; where = &((*where)->next));
   *where = lib;
}

#ifdef __MINGW32__
#define dpi_dlopen(path) ((void *)LoadLibraryA(path))
#define dpi_dlsym(h, name) ((void *)GetProcAddress((HMODULE)(h), name))

static const char *dpi_dlerror(void)
{
   static char buf[256];
   const DWORD code = GetLastError();
   if (FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM
                      | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, code, 0, buf,
                      sizeof(buf), NULL) == 0)
      snprintf(buf, sizeof(buf), "error %lu", (unsigned long)code);
   return buf;
}
#else
#define dpi_dlopen(path) dlopen(path, RTLD_LAZY | RTLD_GLOBAL)
#define dpi_dlsym(h, name) dlsym(h, name)
#define dpi_dlerror dlerror
#endif

static void *dpi_try_open(const char *path)
{
   void *handle = dpi_dlopen(path);
   if (handle != NULL)
      return handle;

   LOCAL_TEXT_BUF tb = tb_new();
   tb_cat(tb, path);
   tb_cat(tb, "." DLL_EXT);

   file_info_t info;
   if (!get_file_info(tb_get(tb), &info))
      return NULL;

   return dpi_dlopen(tb_get(tb));
}

void dpi_load_libs(void)
{
   while (dpi_pending != NULL) {
      dpi_lib_t *lib = dpi_pending;
      dpi_pending = lib->next;

      void *handle = NULL;
#ifdef __MINGW32__
      const bool has_dir = strpbrk(lib->name, "/\\:") != NULL;
#else
      const bool has_dir = strchr(lib->name, '/') != NULL;
#endif

      if (has_dir) {
         // dlopen would search the library path for a name with no slash
         handle = dpi_try_open(lib->name);
      }
      else {
         const char *plugin_path = getenv("NVC_PLUGIN_PATH");
         if (plugin_path != NULL) {
            char *copy LOCAL = xstrdup(plugin_path), *saveptr;
            for (char *elem = strtok_r(copy, PATH_SEP, &saveptr);
                 elem != NULL && handle == NULL;
                 elem = strtok_r(NULL, PATH_SEP, &saveptr)) {
               LOCAL_TEXT_BUF tb = tb_new();
               tb_cat(tb, elem);
               tb_cat(tb, DIR_SEP);
               tb_cat(tb, lib->name);
               handle = dpi_try_open(tb_get(tb));
            }
         }

         if (handle == NULL) {
            LOCAL_TEXT_BUF tb = tb_new();
            tb_cat(tb, "./");
            tb_cat(tb, lib->name);
            handle = dpi_try_open(tb_get(tb));
         }

         if (handle == NULL)
            handle = dpi_try_open(lib->name);
      }

      if (handle == NULL)
         fatal("cannot load DPI library '%s': %s", lib->name,
               dpi_dlerror() ?: "file not found");

      lib->handle = handle;
      lib->next = dpi_loaded;
      dpi_loaded = lib;
   }
}

#define UVM_RE_MAX 2048

static char uvm_re_buf[UVM_RE_MAX + 4];

static int uvm_re_match(const char *re, const char *str)
{
   // Regular expressions between slashes or a glob converted by
   // uvm_glob_to_re; zero when the string matches

#ifdef __MINGW32__
   return 1;
#else
   if (re == NULL || str == NULL)
      return 1;

   size_t len = strlen(re);
   if (len > UVM_RE_MAX)
      return 1;

   char *copy LOCAL = xstrdup(re);
   const char *rex = copy;
   if (len > 1 && re[0] == '/' && re[len - 1] == '/') {
      copy[len - 1] = '\0';
      rex++;
   }

   regex_t preg;
   const int err = regcomp(&preg, rex, REG_EXTENDED);
   if (err != 0)
      return err;

   const int result = regexec(&preg, str, 0, NULL, 0);
   regfree(&preg);
   return result;
#endif
}

static const char *uvm_glob_to_re(const char *glob)
{
   if (glob == NULL)
      return NULL;

   size_t len = strlen(glob);
   if (len > 2040)
      return glob;
   else if (len == 0 || (len == 1 && *glob == '/')) {
      uvm_re_buf[0] = '\0';
      return uvm_re_buf;
   }
   else if (glob[0] == '/' && glob[len - 1] == '/') {
      strcpy(uvm_re_buf, glob);
      return uvm_re_buf;
   }

   len = 0;
   uvm_re_buf[len++] = '/';

   if (*glob != '^')
      uvm_re_buf[len++] = '^';

   for (const char *p = glob; *p; p++) {
      switch (*p) {
      case '*':
         uvm_re_buf[len++] = '.';
         uvm_re_buf[len++] = '*';
         break;
      case '+':
         uvm_re_buf[len++] = '.';
         uvm_re_buf[len++] = '+';
         break;
      case '?':
         uvm_re_buf[len++] = '.';
         break;
      case '.':
      case '[':
      case ']':
      case '(':
      case ')':
         uvm_re_buf[len++] = '\\';
         uvm_re_buf[len++] = *p;
         break;
      default:
         uvm_re_buf[len++] = *p;
         break;
      }
   }

   if (uvm_re_buf[len - 1] != '$')
      uvm_re_buf[len++] = '$';

   uvm_re_buf[len++] = '/';
   uvm_re_buf[len] = '\0';
   return uvm_re_buf;
}

static void uvm_dump_re_cache(void)
{
}

static const struct {
   const char *name;
   void       *fn;
} dpi_builtins[] = {
   { "uvm_re_match", uvm_re_match },
   { "uvm_glob_to_re", uvm_glob_to_re },
   { "uvm_dump_re_cache", uvm_dump_re_cache },
   { "uvm_hdl_check_path", uvm_hdl_check_path },
   { "uvm_hdl_read", uvm_hdl_read },
   { "uvm_hdl_deposit", uvm_hdl_deposit },
   { "uvm_hdl_force", uvm_hdl_force },
   { "uvm_hdl_release", uvm_hdl_release },
   { "uvm_hdl_release_and_read", uvm_hdl_release_and_read },
};

static void *dpi_find_symbol(const char *name)
{
   for (int i = 0; i < ARRAY_LEN(dpi_builtins); i++) {
      if (strcmp(dpi_builtins[i].name, name) == 0)
         return dpi_builtins[i].fn;
   }

   for (dpi_lib_t *it = dpi_loaded; it != NULL; it = it->next) {
      void *p = dpi_dlsym(it->handle, name);
      if (p != NULL)
         return p;
   }

#ifdef __MINGW32__
   // Symbols exported by the nvc executable
   return dpi_dlsym(GetModuleHandle(NULL), name);
#else
   return dlsym(RTLD_DEFAULT, name);
#endif
}

static const char *dpi_parse_arg(const char *p, dpi_arg_t *arg, bool is_ret)
{
   arg->dir = 'i';
   if (!is_ret) {
      arg->dir = *p++;
      assert(arg->dir == '<' || arg->dir == '>' || arg->dir == '=');
   }

   arg->kind = *p++;
   arg->width = 0;
   arg->issigned = false;
   arg->ekind = 0;
   arg->count = 0;

   if (arg->kind == 'S')
      arg->ekind = *p++;

   while (isdigit((unsigned char)*p))
      arg->width = arg->width * 10 + (*p++ - '0');

   if (*p == 's') {
      arg->issigned = true;
      p++;
   }

   if (arg->kind == 'S' && *p == 'x') {
      p++;
      while (isdigit((unsigned char)*p))
         arg->count = arg->count * 10 + (*p++ - '0');
   }

   return p;
}

static ffi_type *dpi_ffi_type(const dpi_arg_t *a)
{
   if (a->dir != '<' && a->dir != 'i')
      return &ffi_type_pointer;

   switch (a->kind) {
   case 'v': return &ffi_type_void;
   case 'b':
   case 'l': return &ffi_type_uint8;
   case 'i':
      switch (a->width) {
      case 8: return a->issigned ? &ffi_type_sint8 : &ffi_type_uint8;
      case 16: return a->issigned ? &ffi_type_sint16 : &ffi_type_uint16;
      case 32: return a->issigned ? &ffi_type_sint32 : &ffi_type_uint32;
      default: return a->issigned ? &ffi_type_sint64 : &ffi_type_uint64;
      }
   case 'r': return &ffi_type_double;
   case 'f': return &ffi_type_float;
   default: return &ffi_type_pointer;
   }
}

static dpi_entry_t *dpi_lookup(const char *sig)
{
   if (dpi_cache == NULL)
      dpi_cache = shash_new(64);

   dpi_entry_t *e = shash_get(dpi_cache, sig);
   if (e != NULL)
      return e;

   dpi_load_libs();

   e = xcalloc(sizeof(dpi_entry_t));

   const char *space = strchr(sig, ' ');
   assert(space != NULL);
   e->cname = xstrndup(sig, space - sig);

   const char *p = dpi_parse_arg(space + 1, &e->ret, true);
   while (*p == ' ') {
      if (p[1] == '@') {
         e->scope = xstrdup(p + 2);
         break;
      }
      else if (p[1] == '#') {
         for (p += 2; *p != ' ' && *p != '\0'; p++) {
            if (*p == 'c')
               e->is_context = true;
            else if (*p == 't')
               e->is_task = true;
         }
         continue;
      }

      if (e->nargs == DPI_MAX_ARGS)
         jit_msg(NULL, DIAG_FATAL, "too many arguments in call to DPI "
                 "function '%s'", e->cname);
      p = dpi_parse_arg(p + 1, &(e->args[e->nargs++]), false);
   }

   e->fn = dpi_find_symbol(e->cname);
   if (e->fn == NULL)
      jit_msg(NULL, DIAG_FATAL, "cannot find the definition of DPI function "
              "'%s': check that the library containing it is given with "
              "--sv-lib", e->cname);

   for (int i = 0; i < e->nargs; i++)
      e->types[i] = dpi_ffi_type(&(e->args[i]));

   if (ffi_prep_cif(&e->cif, FFI_DEFAULT_ABI, e->nargs, dpi_ffi_type(&e->ret),
                    e->types) != FFI_OK)
      jit_msg(NULL, DIAG_FATAL, "ffi_prep_cif failed for DPI function '%s'",
              e->cname);

   shash_put(dpi_cache, sig, e);
   return e;
}

static inline uint64_t dpi_mask(int width)
{
   return width >= 64 ? ~UINT64_C(0) : (UINT64_C(1) << width) - 1;
}

static inline uint64_t dpi_extend(uint64_t value, const dpi_arg_t *a)
{
   value &= dpi_mask(a->width);
   if (a->issigned && a->width > 0 && a->width < 64
       && (value & (UINT64_C(1) << (a->width - 1))))
      value |= ~dpi_mask(a->width);
   return value;
}

static void *dpi_vec_load(const dpi_arg_t *a, const void *mem);
static void dpi_vec_store(void *mem, const dpi_arg_t *a, const void *buf);

static void dpi_load_input(dpi_slot_t *slot, const dpi_arg_t *a, void *mem)
{
   const uint64_t *w = mem;

   switch (a->kind) {
   case 'b':
      slot->u8 = w[0] & 1;
      break;
   case 'l':
      slot->u8 = (w[0] & 1) | ((w[1] & 1) << 1);
      break;
   case 'i':
      switch (a->width) {
      case 8: slot->u8 = w[0]; break;
      case 16: slot->u16 = w[0]; break;
      case 32: slot->u32 = w[0]; break;
      default: slot->u64 = w[0]; break;
      }
      break;
   case 'B':
   case 'L':
      slot->p = dpi_vec_load(a, mem);
      break;
   case 'r':
      memcpy(&slot->d, mem, sizeof(double));
      break;
   case 'f':
      {
         double d;
         memcpy(&d, mem, sizeof(double));
         slot->f = d;
      }
      break;
   case 'p':
      memcpy(&slot->p, mem, sizeof(void *));
      break;
   case 's':
      {
         const void *const *ptr = mem;
         const int64_t *dims = mem;
         const int64_t len = ffi_array_length(dims[2]);
         char *copy = xmalloc(len + 1);
         if (len > 0)
            memcpy(copy, *ptr, len);
         copy[len] = '\0';
         slot->s = copy;
      }
      break;
   default:
      should_not_reach_here();
   }
}

static void *dpi_vec_load(const dpi_arg_t *a, const void *mem)
{
   // The C representation of a vector is an array of 32-bit words

   const bool four_state = a->kind == 'L';
   const int nwords = (a->width + 31) / 32;
   uint32_t *buf = xcalloc_array(nwords, four_state ? 8 : 4);

   const uint64_t *apart, *bpart = NULL;
   uint64_t inl[2];
   if (a->width > 64) {
      const uint64_t *const *ptrs = mem;
      apart = ptrs[0];
      bpart = four_state ? ptrs[1] : NULL;
   }
   else {
      memcpy(inl, mem, four_state ? 16 : 8);
      apart = &inl[0];
      bpart = four_state ? &inl[1] : NULL;
   }

   for (int i = 0; i < nwords; i++) {
      const uint64_t m = a->width - i * 32 >= 32 ? 0xffffffff
         : dpi_mask(a->width - i * 32);
      const uint32_t av = (apart[i / 2] >> (32 * (i % 2))) & m;
      if (four_state) {
         const uint32_t bv = bpart != NULL
            ? (bpart[i / 2] >> (32 * (i % 2))) & m : 0;
         buf[2*i] = av;
         buf[2*i + 1] = bv;
      }
      else
         buf[i] = av;
   }

   return buf;
}

static void dpi_vec_store(void *mem, const dpi_arg_t *a, const void *buf)
{
   const bool four_state = a->kind == 'L';
   const int nwords = (a->width + 31) / 32;
   const uint32_t *words = buf;
   const int nw64 = (a->width + 63) / 64;

   uint64_t *apart = xcalloc_array(nw64, sizeof(uint64_t));
   uint64_t *bpart = four_state ? xcalloc_array(nw64, sizeof(uint64_t)) : NULL;

   for (int i = 0; i < nwords; i++) {
      const uint64_t m = a->width - i * 32 >= 32 ? 0xffffffff
         : dpi_mask(a->width - i * 32);
      if (four_state) {
         apart[i / 2] |= (uint64_t)(words[2*i] & m) << (32 * (i % 2));
         bpart[i / 2] |= (uint64_t)(words[2*i + 1] & m) << (32 * (i % 2));
      }
      else
         apart[i / 2] |= (uint64_t)(words[i] & m) << (32 * (i % 2));
   }

   if (a->width > 64) {
      uint64_t *ma = jit_mspace_alloc(nw64 * sizeof(uint64_t));
      memcpy(ma, apart, nw64 * sizeof(uint64_t));
      uint64_t **ptrs = mem;
      ptrs[0] = ma;
      if (four_state) {
         uint64_t *mb = jit_mspace_alloc(nw64 * sizeof(uint64_t));
         memcpy(mb, bpart, nw64 * sizeof(uint64_t));
         ptrs[1] = mb;
      }
   }
   else {
      uint64_t *w = mem;
      w[0] = dpi_extend(apart[0], a);
      if (four_state)
         w[1] = dpi_extend(bpart[0], a);
   }

   free(apart);
   free(bpart);
}

static void dpi_store_value(void *mem, const dpi_arg_t *a, const dpi_slot_t *s,
                            int index)
{
   uint64_t *w = mem;

   switch (a->kind) {
   case 'b':
      w[0] = s->u8 & 1;
      break;
   case 'l':
      w[0] = s->u8 & 1;
      w[1] = (s->u8 >> 1) & 1;
      break;
   case 'i':
      switch (a->width) {
      case 8: w[0] = a->issigned ? (uint64_t)(int64_t)s->i8 : s->u8; break;
      case 16: w[0] = a->issigned ? (uint64_t)(int64_t)s->i16 : s->u16; break;
      case 32: w[0] = a->issigned ? (uint64_t)(int64_t)s->i32 : s->u32; break;
      default: w[0] = s->u64; break;
      }
      break;
   case 'B':
   case 'L':
      dpi_vec_store(mem, a, s->p);
      break;
   case 'r':
      memcpy(mem, &s->d, sizeof(double));
      break;
   case 'f':
      {
         const double d = s->f;
         memcpy(mem, &d, sizeof(double));
      }
      break;
   case 'p':
      memcpy(mem, &s->p, sizeof(void *));
      break;
   case 's':
      free(dpi_strings[index]);
      dpi_strings[index] = s->s != NULL ? xstrdup(s->s) : NULL;
      break;
   default:
      should_not_reach_here();
   }
}


////////////////////////////////////////////////////////////////////////////////
// Open and sized array arguments

#define DPI_MAX_DIMS 8

typedef struct {
   int64_t left;
   int64_t right;
   int64_t size;
} dpi_dim_t;

typedef struct {
   dpi_arg_t elem;           // Kind, width and sign of one element
   int       dir;            // 0 input, 1 output, 2 inout
   int       ndims;          // Unpacked dimensions
   bool      has_packed;     // Dimension zero is the packed dimension
   dpi_dim_t packed;
   dpi_dim_t dim[DPI_MAX_DIMS];
   size_t    count;          // Total number of elements
   char     *mem;            // Elements held by the simulator
   size_t    stride;         // Bytes between elements in mem
   size_t    esize;          // Bytes in one element in canonical form
   void     *buf;            // All the elements in canonical form
} dpi_oa_t;

static size_t dpi_elem_size(const dpi_arg_t *e)
{
   switch (e->kind) {
   case 'b':
   case 'l': return 1;
   case 'i': return e->width <= 8 ? 1 : e->width <= 16 ? 2
                : e->width <= 32 ? 4 : 8;
   case 'r': return sizeof(double);
   case 'f': return sizeof(float);
   case 'B': return 4 * ((e->width + 31) / 32);
   case 'L': return 8 * ((e->width + 31) / 32);
   default: should_not_reach_here();
   }
}

static size_t dpi_elem_stride(const dpi_arg_t *e)
{
   return e->kind == 'l' || e->kind == 'L' ? 2 * sizeof(uint64_t)
      : sizeof(uint64_t);
}

static void dpi_oa_pack(dpi_oa_t *oa)
{
   // Convert the elements to the layout seen by foreign code

   const dpi_arg_t *e = &(oa->elem);

   oa->buf = xcalloc_array(MAX(oa->count, 1), oa->esize);

   if (oa->dir == 1)
      return;   // An output has no value to pass in

   for (size_t k = 0; k < oa->count; k++) {
      char *dst = (char *)oa->buf + k * oa->esize;
      void *src = oa->mem + k * oa->stride;

      dpi_slot_t slot = {};
      dpi_load_input(&slot, e, src);

      switch (e->kind) {
      case 'b':
      case 'l':
         *(uint8_t *)dst = slot.u8;
         break;
      case 'i':
         switch (oa->esize) {
         case 1: *(uint8_t *)dst = slot.u8; break;
         case 2: *(uint16_t *)dst = slot.u16; break;
         case 4: *(uint32_t *)dst = slot.u32; break;
         default: *(uint64_t *)dst = slot.u64; break;
         }
         break;
      case 'r':
         *(double *)dst = slot.d;
         break;
      case 'f':
         *(float *)dst = slot.f;
         break;
      case 'B':
      case 'L':
         memcpy(dst, slot.p, oa->esize);
         free(slot.p);
         break;
      default:
         should_not_reach_here();
      }
   }
}

static void dpi_oa_unpack(dpi_oa_t *oa)
{
   // Copy elements changed by foreign code back to the simulator

   const dpi_arg_t *e = &(oa->elem);

   for (size_t k = 0; k < oa->count; k++) {
      const char *src = (char *)oa->buf + k * oa->esize;
      void *dst = oa->mem + k * oa->stride;

      dpi_slot_t slot = {};
      switch (e->kind) {
      case 'b':
      case 'l':
         slot.u8 = *(const uint8_t *)src;
         break;
      case 'i':
         switch (oa->esize) {
         case 1: slot.u8 = *(const uint8_t *)src; break;
         case 2: slot.u16 = *(const uint16_t *)src; break;
         case 4: slot.u32 = *(const uint32_t *)src; break;
         default: slot.u64 = *(const uint64_t *)src; break;
         }
         break;
      case 'r':
         slot.d = *(const double *)src;
         break;
      case 'f':
         slot.f = *(const float *)src;
         break;
      case 'B':
      case 'L':
         slot.p = (void *)src;
         break;
      default:
         should_not_reach_here();
      }

      dpi_store_value(dst, e, &slot, 0);
   }
}

static void dpi_oa_free(dpi_oa_t *oa)
{
   free(oa->buf);
   oa->buf = NULL;
}

int64_t dpi_oa_make(const jit_scalar_t *args)
{
   // Describe an array argument whose formal has unsized dimensions

   dpi_oa_t *oa = xcalloc(sizeof(dpi_oa_t));
   oa->elem.kind = args[0].integer;
   oa->elem.width = args[1].integer;
   oa->elem.issigned = args[2].integer != 0;
   oa->dir = args[3].integer;
   oa->mem = args[4].pointer;
   oa->ndims = args[5].integer;
   oa->has_packed = args[6].integer != 0;
   oa->packed.left = args[7].integer;
   oa->packed.right = args[8].integer;
   oa->packed.size = labs(oa->packed.left - oa->packed.right) + 1;

   assert(oa->ndims <= DPI_MAX_DIMS);

   oa->count = 1;
   for (int i = 0; i < oa->ndims; i++) {
      oa->dim[i].left = args[9 + 3*i].integer;
      oa->dim[i].right = args[10 + 3*i].integer;
      oa->dim[i].size = args[11 + 3*i].integer;
      oa->count *= oa->dim[i].size;
   }

   oa->esize = dpi_elem_size(&oa->elem);
   oa->stride = dpi_elem_stride(&oa->elem);
   return (intptr_t)oa;
}

static void dpi_sized_make(dpi_oa_t *oa, const dpi_arg_t *a, void *mem)
{
   memset(oa, 0, sizeof(*oa));
   oa->elem.kind = a->ekind;
   oa->elem.width = a->width;
   oa->elem.issigned = a->issigned;
   oa->dir = a->dir == '<' ? 0 : a->dir == '>' ? 1 : 2;
   oa->mem = mem;
   oa->count = a->count;
   oa->esize = dpi_elem_size(&oa->elem);
   oa->stride = dpi_elem_stride(&oa->elem);
}

typedef struct _dpi_call dpi_call_t;

struct _dpi_call {
   dpi_entry_t  *e;
   dpi_slot_t    slots[DPI_MAX_ARGS];
   void         *ptrs[DPI_MAX_ARGS];
   void         *avalues[DPI_MAX_ARGS];
   char         *owned[DPI_MAX_ARGS];
   dpi_oa_t      sized[DPI_MAX_ARGS];
   dpi_oa_t     *oas[DPI_MAX_ARGS];
   void         *argv[DPI_MAX_ARGS];
   dpi_slot_t    ret;
   void         *result;
   dpi_scope_t  *scope;
#ifdef DPI_FIBERS
#ifdef __MINGW32__
   LPVOID        uc;           // Fiber running the call
   LPVOID        caller;       // Fiber to switch back to
   void         *stack_top;
#else
   ucontext_t    uc;
   ucontext_t    caller;
   void         *stack;
#endif
   mptr_t        root;
   mspace_t     *ms;
   rt_proc_t    *proc;
   dpi_call_t   *next;
   const void   *park_lo;
   const void   *main_bottom;
   size_t        main_size;
   bool          started;
   bool          done;
   bool          parked;
   bool          disabled;
#endif
};

#ifdef DPI_FIBERS

// An imported context task may call exported tasks which suspend the
// calling process, so it runs on a stack of its own which is switched out
// while the process waits

#define DPI_STACK_SIZE (1024 * 1024)
#define DPI_STACK_GUARD 4096
#define DPI_STACK_POOL  16

static dpi_call_t *dpi_fiber = NULL;       // Call running on its own stack
static dpi_call_t *dpi_suspended = NULL;
static void       *dpi_stack_pool[DPI_STACK_POOL];
static int         dpi_stack_count = 0;

#ifdef __MINGW32__
// Fibers allocate their own stacks
#define DPI_STACK_TOP(c) ((c)->stack_top)
#else
#define DPI_STACK_TOP(c) ((char *)(c)->stack + DPI_STACK_SIZE)

static void *dpi_stack_alloc(void)
{
   if (dpi_stack_count > 0)
      return dpi_stack_pool[--dpi_stack_count];

   void *mem = mmap(NULL, DPI_STACK_SIZE + DPI_STACK_GUARD,
                    PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
   if (mem == MAP_FAILED)
      jit_msg(NULL, DIAG_FATAL, "cannot allocate a stack for a DPI call");

   mprotect(mem, DPI_STACK_GUARD, PROT_NONE);
   return (char *)mem + DPI_STACK_GUARD;
}

static void dpi_stack_free(void *stack)
{
   if (dpi_stack_count < DPI_STACK_POOL)
      dpi_stack_pool[dpi_stack_count++] = stack;
   else
      munmap((char *)stack - DPI_STACK_GUARD,
             DPI_STACK_SIZE + DPI_STACK_GUARD);
}
#endif

#ifdef __MINGW32__
static VOID CALLBACK dpi_fiber_main(PVOID arg)
{
   dpi_call_t *c = arg;

   ULONG_PTR lo, hi;
   GetCurrentThreadStackLimits(&lo, &hi);
   c->stack_top = (void *)hi;
   if (mspace_get_stack_limit() != NULL)
      mspace_stack_limit(c->stack_top);

   ffi_call(&c->e->cif, FFI_FN(c->e->fn), &c->ret, c->avalues);
   c->done = true;

   // A fiber must never return as that would exit the thread
   SwitchToFiber(c->caller);
   abort();
}
#else
static void dpi_fiber_main(unsigned lo, unsigned hi)
{
   dpi_call_t *c = (dpi_call_t *)(((uintptr_t)hi << 32) | lo);

#if ASAN_ENABLED
   __sanitizer_finish_switch_fiber(NULL, &c->main_bottom, &c->main_size);
#endif

   ffi_call(&c->e->cif, FFI_FN(c->e->fn), &c->ret, c->avalues);
   c->done = true;

#if ASAN_ENABLED
   __sanitizer_start_switch_fiber(NULL, c->main_bottom, c->main_size);
#endif
}
#endif   // __MINGW32__

__attribute__((noinline))
static void dpi_fiber_yield(dpi_call_t *c)
{
   // Switch back to the process: it continues when the call is resumed

   void *fake = NULL;
   (void)fake;

   const intptr_t *lo = (const intptr_t *)__builtin_frame_address(0) - 128;
   c->park_lo = lo;
   mspace_park_range(lo, DPI_STACK_TOP(c));
   mspace_park_range(&c->uc, &c->uc + 1);
   c->parked = true;

#if ASAN_ENABLED
   __sanitizer_start_switch_fiber(&fake, c->main_bottom, c->main_size);
#endif

#ifdef __MINGW32__
   SwitchToFiber(c->caller);
#else
   swapcontext(&c->uc, &c->caller);
#endif

#if ASAN_ENABLED
   __sanitizer_finish_switch_fiber(fake, &c->main_bottom, &c->main_size);
#endif
}

__attribute__((noinline))
static void dpi_fiber_enter(dpi_call_t *c)
{
   // Runs the call until it finishes or suspends

   dpi_scope_t *saved_scope = dpi_current;
   const dpi_entry_t *saved_import = dpi_import;
   dpi_call_t *saved_fiber = dpi_fiber;

   dpi_current = c->scope;
   dpi_import = c->e;
   dpi_fiber = c;

   if (!c->started) {
      c->started = true;
#ifdef __MINGW32__
      c->stack_top = NULL;
      c->uc = CreateFiberEx(64 * 1024, DPI_STACK_SIZE, 0, dpi_fiber_main, c);
      if (c->uc == NULL)
         jit_msg(NULL, DIAG_FATAL, "cannot allocate a stack for a DPI call");
#else
      c->stack = dpi_stack_alloc();
      getcontext(&c->uc);
      c->uc.uc_stack.ss_sp = c->stack;
      c->uc.uc_stack.ss_size = DPI_STACK_SIZE;
      c->uc.uc_link = &c->caller;
      makecontext(&c->uc, (void (*)(void))dpi_fiber_main, 2,
                  (unsigned)(uintptr_t)c, (unsigned)((uintptr_t)c >> 32));
#endif
   }

#ifdef __MINGW32__
   if (!IsThreadAFiber())
      ConvertThreadToFiber(NULL);
   c->caller = GetCurrentFiber();
#endif

   if (c->parked) {
      mspace_unpark_range(c->park_lo);
      mspace_unpark_range(&c->uc);
      c->parked = false;
   }

   void *limit = mspace_get_stack_limit();
   const intptr_t *lo = (const intptr_t *)__builtin_frame_address(0) - 128;
   if (limit != NULL) {
      mspace_park_range(lo, limit);
      mspace_park_range(&c->caller, &c->caller + 1);
      if (DPI_STACK_TOP(c) != NULL)
         mspace_stack_limit(DPI_STACK_TOP(c));
   }

   void *fake = NULL;
   (void)fake;

#if ASAN_ENABLED
   __sanitizer_start_switch_fiber(&fake, c->stack, DPI_STACK_SIZE);
#endif

#ifdef __MINGW32__
   SwitchToFiber(c->uc);
#else
   swapcontext(&c->caller, &c->uc);
#endif

#if ASAN_ENABLED
   __sanitizer_finish_switch_fiber(fake, NULL, NULL);
#endif

   if (limit != NULL) {
      mspace_stack_limit(limit);
      mspace_unpark_range(lo);
      mspace_unpark_range(&c->caller);
   }

   c->scope = dpi_current;
   dpi_current = saved_scope;
   dpi_import = saved_import;
   dpi_fiber = saved_fiber;

   if (c->done) {
#ifdef __MINGW32__
      DeleteFiber(c->uc);
      c->uc = NULL;
#else
      dpi_stack_free(c->stack);
      c->stack = NULL;
#endif
   }
}

#endif   // DPI_FIBERS

static void dpi_call_setup(dpi_call_t *c, void **argv)
{
   const dpi_entry_t *e = c->e;

   memset(c->slots, 0, sizeof(c->slots));
   memset(c->oas, 0, sizeof(c->oas));

   for (int i = 0; i < e->nargs; i++) {
      const dpi_arg_t *a = &(e->args[i]);
      dpi_slot_t *s = &(c->slots[i]);

      c->argv[i] = argv[i];
      c->owned[i] = NULL;

      if (a->kind == 'A') {
         c->oas[i] = *(dpi_oa_t *const *)argv[i];
         dpi_oa_pack(c->oas[i]);
         s->p = c->oas[i];
         c->avalues[i] = &s->p;
         continue;
      }
      else if (a->kind == 'S') {
         c->oas[i] = &(c->sized[i]);
         dpi_sized_make(c->oas[i], a, argv[i]);
         dpi_oa_pack(c->oas[i]);
         s->p = c->oas[i]->buf;
         c->avalues[i] = &s->p;
         continue;
      }

      dpi_load_input(s, a, argv[i]);

      c->owned[i] = a->kind == 's' ? (char *)s->s
         : a->kind == 'B' || a->kind == 'L' ? s->p : NULL;

      if (a->kind == 'B' || a->kind == 'L')
         c->avalues[i] = &s->p;
      else if (a->dir != '<') {
         c->ptrs[i] = s;
         c->avalues[i] = &(c->ptrs[i]);
      }
      else
         c->avalues[i] = s;
   }
}

static void dpi_call_finish(dpi_call_t *c, bool store)
{
   const dpi_entry_t *e = c->e;

#ifdef DPI_FIBERS
   if (c->root != NULL)
      mptr_free(c->ms, &c->root);
#endif

   for (int i = 0; i < e->nargs; i++) {
      const dpi_arg_t *a = &(e->args[i]);
      if (c->oas[i] != NULL) {
         if (store && c->oas[i]->dir != 0)
            dpi_oa_unpack(c->oas[i]);
         dpi_oa_free(c->oas[i]);
         if (a->kind == 'A')
            free(c->oas[i]);
         continue;
      }

      if (store && a->dir != '<')
         dpi_store_value(c->argv[i], a, &(c->slots[i]), i);
      free(c->owned[i]);
   }

   if (!store || e->ret.kind == 'v')
      return;

   // Small integers are returned widened to the register size
   dpi_slot_t narrow = {};
   switch (e->ret.kind) {
   case 'b':
   case 'l':
      narrow.u8 = c->ret.u64;
      break;
   case 'i':
      switch (e->ret.width) {
      case 8: narrow.u8 = c->ret.u64; break;
      case 16: narrow.u16 = c->ret.u64; break;
      case 32: narrow.u32 = c->ret.u64; break;
      default: narrow.u64 = c->ret.u64; break;
      }
      break;
   case 'r':
      narrow.d = c->ret.d;
      break;
   case 'f':
      narrow.f = c->ret.f;
      break;
   default:
      narrow.p = c->ret.p;
      break;
   }

   dpi_store_value(c->result, &e->ret, &narrow, e->nargs);
}

int64_t dpi_call(const char *sig, int64_t siglen, void *context,
                 void *result, void **argv)
{
   char *key LOCAL = xstrndup(sig, siglen);
   dpi_entry_t *e = dpi_lookup(key);

#ifdef DPI_FIBERS
   if (e->is_task && e->is_context) {
      dpi_call_t *c = xcalloc(sizeof(dpi_call_t));
      c->e = e;
      c->result = result;
      c->proc = get_active_proc();
      c->scope = context != NULL ? dpi_scope_get(context, e->scope)
         : dpi_current;
      dpi_call_setup(c, argv);

      dpi_fiber_enter(c);

      if (!c->done) {
         c->next = dpi_suspended;
         dpi_suspended = c;
         return (intptr_t)c;
      }

      dpi_call_finish(c, true);
      free(c);
      return 0;
   }
#endif

   dpi_call_t call;
#ifdef DPI_FIBERS
   call.root = NULL;
#endif
   call.e = e;
   call.result = result;
   dpi_call_setup(&call, argv);
   call.ret = (dpi_slot_t){};

   dpi_scope_t *saved = dpi_current;
   if (context != NULL)
      dpi_current = dpi_scope_get(context, e->scope);

   const dpi_entry_t *saved_import = dpi_import;
   dpi_import = e;

   ffi_call(&e->cif, FFI_FN(e->fn), &call.ret, call.avalues);

   dpi_import = saved_import;
   dpi_current = saved;

   dpi_call_finish(&call, true);
   return 0;
}

int64_t dpi_resume(int64_t handle, void **argv)
{
   // Continue a call which was suspended by an exported task

#ifdef DPI_FIBERS
   dpi_call_t *c = (dpi_call_t *)(intptr_t)handle;

   for (dpi_call_t **p = &dpi_suspended; *p != NULL; p = &((*p)->next)) {
      if (*p == c) {
         *p = c->next;
         break;
      }
   }

   for (int i = 0; i < c->e->nargs; i++) {
      c->argv[i] = argv[i];
      if (c->e->args[i].kind == 'S')
         c->oas[i]->mem = argv[i];
   }

   dpi_fiber_enter(c);

   if (!c->done) {
      c->next = dpi_suspended;
      dpi_suspended = c;
      return handle;
   }

   dpi_call_finish(c, true);
   free(c);
#endif

   return 0;
}

void dpi_proc_killed(rt_proc_t *proc)
{
   // A process which is blocked in an exported task has been disabled: the
   // exported task returns non-zero and the call is run to completion
   // so the foreign code can clean up (IEEE 1800-2017 35.9)

#ifdef DPI_FIBERS
   for (dpi_call_t **p = &dpi_suspended; *p != NULL; p = &((*p)->next)) {
      dpi_call_t *c = *p;
      if (c->proc != proc)
         continue;

      *p = c->next;
      c->disabled = true;
      dpi_fiber_enter(c);
      assert(c->done);

      dpi_call_finish(c, false);
      free(c);
      return;
   }
#endif
}

int64_t dpi_string_length(int64_t index)
{
   assert(index >= 0 && index <= DPI_MAX_ARGS);
   return dpi_strings[index] != NULL ? strlen(dpi_strings[index]) : 0;
}

void dpi_string_copy(int64_t index, char *dst)
{
   assert(index >= 0 && index <= DPI_MAX_ARGS);
   if (dpi_strings[index] != NULL)
      memcpy(dst, dpi_strings[index], strlen(dpi_strings[index]));
}

////////////////////////////////////////////////////////////////////////////////
// Exported subroutines

typedef struct _dpi_target dpi_target_t;

struct _dpi_target {
   dpi_target_t *next;
   dpi_scope_t  *scope;
   jit_handle_t  handle;
};

typedef struct {
   char         *cname;
   bool          is_task;
   int           nargs;
   dpi_arg_t     ret;
   dpi_arg_t     args[DPI_MAX_ARGS];
   ffi_cif       cif;
   ffi_type     *types[DPI_MAX_ARGS];
   ffi_closure  *closure;
   void         *code;
   dpi_target_t *targets;
   jit_t        *jit;
} dpi_export_t;

static shash_t *dpi_exports = NULL;
static char    *dpi_xstrings[DPI_MAX_ARGS + 1];

static int dpi_nslots(const dpi_arg_t *a)
{
   switch (a->kind) {
   case 'v': return 0;
   case 'l': return 2;
   case 'L': return 2;
   case 's': return 3;
   default: return 1;
   }
}

static void dpi_read_c(dpi_slot_t *slot, const dpi_arg_t *a, const void *cval)
{
   switch (a->kind) {
   case 'b':
   case 'l':
      slot->u8 = *(const uint8_t *)cval;
      break;
   case 'i':
      switch (a->width) {
      case 8: slot->u8 = *(const uint8_t *)cval; break;
      case 16: slot->u16 = *(const uint16_t *)cval; break;
      case 32: slot->u32 = *(const uint32_t *)cval; break;
      default: slot->u64 = *(const uint64_t *)cval; break;
      }
      break;
   case 'r':
      slot->d = *(const double *)cval;
      break;
   case 'f':
      slot->f = *(const float *)cval;
      break;
   case 'p':
      slot->p = *(void *const *)cval;
      break;
   case 'B':
   case 'L':
      slot->p = (void *)cval;
      break;
   default:
      should_not_reach_here();
   }
}

static void dpi_c_to_mem(jit_scalar_t *mem, const dpi_arg_t *a,
                         const void *cval)
{
   if (a->kind == 's') {
      const char *str = *(const char *const *)cval;
      const size_t len = str != NULL ? strlen(str) : 0;
      char *copy = jit_mspace_alloc(len + 1);
      if (len > 0)
         memcpy(copy, str, len);
      mem[0].pointer = copy;
      mem[1].integer = 0;
      mem[2].integer = len;
      return;
   }

   dpi_slot_t slot = {};
   dpi_read_c(&slot, a, cval);
   dpi_store_value(mem, a, &slot, 0);
}

static void dpi_mem_to_c(void *dest, const dpi_arg_t *a, jit_scalar_t *mem,
                         int index)
{
   dpi_slot_t slot = {};
   dpi_load_input(&slot, a, mem);

   switch (a->kind) {
   case 'b':
   case 'l':
      *(uint8_t *)dest = slot.u8;
      break;
   case 'i':
      switch (a->width) {
      case 8: *(uint8_t *)dest = slot.u8; break;
      case 16: *(uint16_t *)dest = slot.u16; break;
      case 32: *(uint32_t *)dest = slot.u32; break;
      default: *(uint64_t *)dest = slot.u64; break;
      }
      break;
   case 'r':
      *(double *)dest = slot.d;
      break;
   case 'f':
      *(float *)dest = slot.f;
      break;
   case 'p':
      *(void **)dest = slot.p;
      break;
   case 'B':
   case 'L':
      memcpy(dest, slot.p, ((a->width + 31) / 32) * (a->kind == 'L' ? 8 : 4));
      free(slot.p);
      break;
   case 's':
      free(dpi_xstrings[index]);
      dpi_xstrings[index] = (char *)slot.s;
      *(const char **)dest = slot.s;
      break;
   default:
      should_not_reach_here();
   }
}

static dpi_target_t *dpi_find_target(dpi_export_t *e)
{
   for (dpi_target_t *t = e->targets; t != NULL; t = t->next) {
      if (t->scope == dpi_current)
         return t;
   }

   if (dpi_current == NULL && e->targets != NULL
       && e->targets->next == NULL)
      return e->targets;

   if (dpi_current == NULL)
      jit_msg(NULL, DIAG_FATAL, "call to exported DPI subroutine '%s' "
              "with no current scope: use svSetScope to select one of the "
              "%s", e->cname, "instances which export it");

   jit_msg(NULL, DIAG_FATAL, "DPI subroutine '%s' is not exported from "
           "the current scope '%s'", e->cname,
           dpi_current->name ?: dpi_current->fullname ?: "(unnamed)");
   return NULL;
}

static void dpi_export_trampoline(ffi_cif *cif, void *ret, void **cargs,
                                  void *user)
{
   dpi_export_t *e = user;

   if (dpi_import != NULL) {
      if (!dpi_import->is_context)
         jit_msg(NULL, DIAG_FATAL, "call to exported DPI subroutine '%s' "
                 "from non-context import", e->cname);
      else if (e->is_task && !dpi_import->is_task)
         jit_msg(NULL, DIAG_FATAL, "exported task '%s' cannot be called "
                 "from imported function", e->cname);
   }

   dpi_target_t *t = dpi_find_target(e);

   jit_scalar_t in[JIT_MAX_ARGS];
   jit_scalar_t mems[DPI_MAX_ARGS][3];
   int pos = 0;

   if (e->is_task)
      in[pos++].pointer = NULL;   // Not resuming a suspended procedure

   in[pos++].pointer = t->scope->context;

   for (int i = 0; i < e->nargs; i++) {
      const dpi_arg_t *a = &(e->args[i]);

      if (pos + 3 >= JIT_MAX_ARGS)
         jit_msg(NULL, DIAG_FATAL, "too many arguments in call to exported "
                 "DPI subroutine '%s'", e->cname);

      memset(mems[i], 0, sizeof(mems[i]));

      if (a->dir == '<') {
         const void *cval = cargs[i];
         if (a->kind == 'B' || a->kind == 'L')
            cval = *(void *const *)cargs[i];
         dpi_c_to_mem(&in[pos], a, cval);
         pos += dpi_nslots(a);
      }
      else {
         void *cptr = *(void **)cargs[i];
         if (a->dir == '=') {
            const void *cval = cptr;
            if (a->kind == 'B' || a->kind == 'L')
               cval = cptr;
            dpi_c_to_mem(mems[i], a, cval);
         }
         in[pos++].pointer = mems[i];
      }
   }

   jit_scalar_t out[4];
   memset(out, 0, sizeof(out));

   dpi_scope_t *saved = dpi_current;

#ifdef DPI_FIBERS
   dpi_call_t *fiber = e->is_task ? dpi_fiber : NULL;
   if (fiber != NULL && fiber->disabled) {
      *(ffi_arg *)ret = 1;   // Calls made while disabled return immediately
      return;
   }
#endif

   jit_thread_local_t *thread = jit_thread_local();

   for (;;) {
      void *saved_jmp = thread->jmp_buf;
      jit_anchor_t *saved_anchor = thread->anchor;

      tlab_t tlab = jit_null_tlab(e->jit);
      if (!jit_vfastcall(e->jit, t->handle, in, pos, out, ARRAY_LEN(out),
                         &tlab))
         jit_abort_with_status(1);

      thread->jmp_buf = saved_jmp;
      thread->anchor = saved_anchor;

      if (!e->is_task || out[0].pointer == NULL)
         break;

#ifdef DPI_FIBERS
      if (fiber == NULL || fiber->proc == NULL)
         jit_msg(NULL, DIAG_FATAL, "exported task '%s' consumed time when "
                 "called from foreign code which is not an imported "
                 "context task", e->cname);

      // The state of the suspended task is only referenced from here
      if (fiber->root == NULL) {
         fiber->ms = jit_get_mspace(e->jit);
         fiber->root = mptr_new(fiber->ms, "dpi task state");
      }
      *mptr_get(fiber->root) = out[0].pointer;

      dpi_fiber_yield(fiber);

      dpi_current = saved;   // Resumed by the process

      void *state = *mptr_get(fiber->root);
      *mptr_get(fiber->root) = NULL;

      if (fiber->disabled) {
         *(ffi_arg *)ret = 1;
         return;
      }

      in[0].pointer = state;
#else
      jit_msg(NULL, DIAG_FATAL, "sorry, the exported task '%s' consumed time "
              "when called from foreign code", e->cname);
#endif
   }

   dpi_current = saved;

   for (int i = 0; i < e->nargs; i++) {
      const dpi_arg_t *a = &(e->args[i]);
      if (a->dir != '<')
         dpi_mem_to_c(*(void **)cargs[i], a, mems[i], i);
   }

   if (e->is_task) {
      *(ffi_arg *)ret = 0;
      return;
   }

   switch (e->ret.kind) {
   case 'v':
      break;
   case 'b':
   case 'l':
      {
         uint8_t v;
         dpi_mem_to_c(&v, &e->ret, out, e->nargs);
         *(ffi_arg *)ret = v;
      }
      break;
   case 'i':
      {
         uint64_t v = 0;
         dpi_mem_to_c(&v, &e->ret, out, e->nargs);
         *(ffi_arg *)ret = (ffi_arg)v;
         if (e->ret.issigned) {
            switch (e->ret.width) {
            case 8: *(ffi_sarg *)ret = (int8_t)v; break;
            case 16: *(ffi_sarg *)ret = (int16_t)v; break;
            case 32: *(ffi_sarg *)ret = (int32_t)v; break;
            }
         }
         else {
            switch (e->ret.width) {
            case 8: *(ffi_arg *)ret = (uint8_t)v; break;
            case 16: *(ffi_arg *)ret = (uint16_t)v; break;
            case 32: *(ffi_arg *)ret = (uint32_t)v; break;
            }
         }
      }
      break;
   default:
      dpi_mem_to_c(ret, &e->ret, out, e->nargs);
      break;
   }
}

static void *dpi_stub_load(const char *name, void ***slot)
{
   // A shared library exporting only the name of a subroutine which jumps
   // through a slot filled in later: foreign code can then be linked
   // against a function which does not exist until the design is elaborated

#if defined __linux__ && defined __x86_64__
   const size_t namelen = strlen(name);
   const size_t strsz = 1 + namelen + 1 + sizeof("nvc_dpi_slot");
   const size_t hash_off = 64 + 4 * 56;
   const size_t sym_off = hash_off + 24;
   const size_t str_off = sym_off + 3 * 24;
   const size_t code_off = (str_off + strsz + 15) & ~(size_t)15;
   const size_t text_end = code_off + 6;
   const size_t data_off = (text_end + 4095) & ~(size_t)4095;
   const size_t dyn_size = 6 * 16;
   const size_t slot_off = data_off + dyn_size;
   const size_t total = slot_off + 8;

   uint8_t *img LOCAL = xcalloc(total);

   uint8_t *eh = img;
   memcpy(eh, "\x7f" "ELF\2\1\1", 7);
   *(uint16_t *)(eh + 16) = 3;              // ET_DYN
   *(uint16_t *)(eh + 18) = 62;             // EM_X86_64
   *(uint32_t *)(eh + 20) = 1;
   *(uint64_t *)(eh + 32) = 64;             // e_phoff
   *(uint16_t *)(eh + 52) = 64;
   *(uint16_t *)(eh + 54) = 56;
   *(uint16_t *)(eh + 56) = 4;
   *(uint16_t *)(eh + 58) = 64;

   struct {
      uint32_t type, flags;
      uint64_t offset, vaddr, paddr, filesz, memsz, align;
   } ph[4] = {
      { 1, 5, 0, 0, 0, text_end, text_end, 4096 },
      { 1, 6, data_off, data_off, data_off, 8 + dyn_size, 8 + dyn_size, 4096 },
      { 2, 6, data_off, data_off, data_off, dyn_size, dyn_size, 8 },
      { 0x6474e551, 6, 0, 0, 0, 0, 0, 16 },
   };
   memcpy(img + 64, ph, sizeof(ph));

   uint32_t *hash = (uint32_t *)(img + hash_off);
   hash[0] = 1;       // nbucket
   hash[1] = 3;       // nchain
   hash[2] = 1;       // bucket[0]
   hash[3] = 0;       // chain[0]
   hash[4] = 2;       // chain[1]
   hash[5] = 0;       // chain[2]

   char *str = (char *)img + str_off;
   memcpy(str + 1, name, namelen);
   memcpy(str + 1 + namelen + 1, "nvc_dpi_slot", sizeof("nvc_dpi_slot"));

   struct {
      uint32_t name;
      uint8_t info, other;
      uint16_t shndx;
      uint64_t value, size;
   } sym[3] = {
      { 0 },
      { 1, 0x12, 0, 1, code_off, 6 },
      { (uint32_t)(1 + namelen + 1), 0x11, 0, 1, slot_off, 8 },
   };
   memcpy(img + sym_off, sym, sizeof(sym));

   img[code_off] = 0xff;
   img[code_off + 1] = 0x25;
   *(int32_t *)(img + code_off + 2) = (int32_t)(slot_off - (code_off + 6));

   uint64_t dyn[] = {
      4, hash_off, 5, str_off, 6, sym_off, 10, strsz, 11, 24, 0, 0
   };
   memcpy(img + data_off, dyn, sizeof(dyn));

   int fd = memfd_create("nvc-dpi", MFD_CLOEXEC);
   if (fd < 0)
      return NULL;

   if (write(fd, img, total) != total) {
      close(fd);
      return NULL;
   }

   char path[64];
   snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);

   // The descriptor stays open as the library is identified by its path
   void *handle = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
   if (handle == NULL) {
      close(fd);
      return NULL;
   }

   *slot = dlsym(handle, "nvc_dpi_slot");
   return *slot != NULL ? handle : NULL;
#else
   return NULL;
#endif
}

static shash_t *dpi_stubs = NULL;

static void dpi_stub_bind(const char *name, void *code)
{
#ifdef __MINGW32__
   // Foreign code looks the address up with vpi_nvc_get_export instead
   return;
#endif

   if (dpi_stubs == NULL)
      dpi_stubs = shash_new(16);

   void **slot = shash_get(dpi_stubs, name);
   if (slot == NULL) {
      if (dpi_stub_load(name, &slot) == NULL)
         jit_msg(NULL, DIAG_FATAL, "exporting DPI subroutines to foreign "
                 "code is not supported on this platform");
      shash_put(dpi_stubs, name, slot);
   }

   *slot = code;
}

DLLEXPORT
void *vpi_nvc_get_export(const char *name)
{
   if (dpi_exports == NULL)
      return NULL;

   dpi_export_t *e = shash_get(dpi_exports, name);
   return e != NULL ? e->code : NULL;
}

void dpi_export(jit_t *j, void *context, const char *sig, int64_t siglen,
                const char *unit, int64_t unitlen)
{
   char *key LOCAL = xstrndup(sig, siglen);
   char *uname LOCAL = xstrndup(unit, unitlen);

   // Kind of subroutine, C name, return type, arguments and scope name
   const bool is_task = key[0] == 'T';
   const char *space = strchr(key + 2, ' ');
   assert(space != NULL);
   char *cname LOCAL = xstrndup(key + 2, space - (key + 2));

   if (dpi_exports == NULL)
      dpi_exports = shash_new(16);

   dpi_export_t *e = shash_get(dpi_exports, cname);
   const char *scope_name = NULL;

   dpi_arg_t ret;
   dpi_arg_t args[DPI_MAX_ARGS];
   int nargs = 0;

   const char *p = dpi_parse_arg(space + 1, &ret, true);
   while (*p == ' ') {
      if (p[1] == '@') {
         scope_name = p + 2;
         break;
      }
      p = dpi_parse_arg(p + 1, &(args[nargs++]), false);
   }

   if (e == NULL) {
      e = xcalloc(sizeof(dpi_export_t));
      e->cname = xstrdup(cname);
      e->is_task = is_task;
      e->ret = ret;
      e->nargs = nargs;
      e->jit = j;
      memcpy(e->args, args, nargs * sizeof(dpi_arg_t));

      for (int i = 0; i < nargs; i++)
         e->types[i] = dpi_ffi_type(&(e->args[i]));

      ffi_type *rtype = is_task ? &ffi_type_sint32 : dpi_ffi_type(&e->ret);
      if (ffi_prep_cif(&e->cif, FFI_DEFAULT_ABI, nargs, rtype, e->types)
          != FFI_OK)
         jit_msg(NULL, DIAG_FATAL, "ffi_prep_cif failed for exported DPI "
                 "subroutine '%s'", cname);

      e->closure = ffi_closure_alloc(sizeof(ffi_closure), &e->code);
      if (e->closure == NULL
          || ffi_prep_closure_loc(e->closure, &e->cif, dpi_export_trampoline,
                                  e, e->code) != FFI_OK)
         jit_msg(NULL, DIAG_FATAL, "cannot create a closure for exported "
                 "DPI subroutine '%s'", cname);

      shash_put(dpi_exports, cname, e);
      dpi_stub_bind(cname, e->code);
   }
   else if (e->nargs != nargs || e->is_task != is_task)
      jit_msg(NULL, DIAG_FATAL, "exported DPI subroutines named '%s' have "
              "different prototypes", cname);

   dpi_target_t *t = xcalloc(sizeof(dpi_target_t));
   t->scope = dpi_scope_get(context, scope_name);
   t->handle = jit_lazy_compile(j, ident_new(uname));
   t->next = e->targets;
   e->targets = t;
}

////////////////////////////////////////////////////////////////////////////////
// svdpi.h scope functions

static char *dpi_scope_path(rt_scope_t *s)
{
   LOCAL_TEXT_BUF tb = tb_new();

   rt_scope_t *chain[64];
   int n = 0;
   for (; s != NULL && n < ARRAY_LEN(chain); s = s->parent) {
      if (s->kind == SCOPE_INSTANCE && s->where != NULL
          && tree_kind(s->where) == T_BLOCK)
         chain[n++] = s;
   }

   for (int i = n - 1; i >= 0; i--) {
      tb_cat(tb, istr(tree_ident(chain[i]->where)));
      if (i > 0)
         tb_append(tb, '.');
   }

   return tb_claim(tb);
}

static const char *dpi_scope_fullname(dpi_scope_t *s)
{
   if (s->fullname != NULL)
      return s->fullname;

   rt_model_t *m = get_model_or_null();
   if (m != NULL) {
      rt_scope_t *rts = scope_for_context(root_scope(m), s->context);
      if (rts != NULL)
         return s->fullname = dpi_scope_path(rts);
   }

   if (s->name != NULL) {
      const char *dot = strrchr(s->name, '.');
      return s->fullname = xstrdup(dot != NULL ? dot + 1 : s->name);
   }

   return NULL;
}

static dpi_scope_t *dpi_scope_find(const char *name)
{
   rt_model_t *m = get_model_or_null();
   if (m == NULL)
      return NULL;

   rt_scope_t *rts = root_scope(m);
   const char *p = name;
   if (!strncmp(p, "$root.", 6))
      p += 6;

   for (;;) {
      const char *end = strchr(p, '.');
      const size_t len = end != NULL ? (size_t)(end - p) : strlen(p);

      rt_scope_t *next = NULL;
      for (int i = 0; i < rts->children.count && next == NULL; i++) {
         rt_scope_t *c = child_scope_at(rts, i);
         if (c->kind != SCOPE_INSTANCE || c->where == NULL
             || tree_kind(c->where) != T_BLOCK)
            continue;

         const char *id = istr(tree_ident(c->where));
         if (strlen(id) == len && strncmp(id, p, len) == 0)
            next = c;
      }

      if (next == NULL)
         break;

      rts = next;
      if (end == NULL) {
         void *ctx = *mptr_get(rts->privdata);
         if (ctx == NULL)
            return NULL;
         return dpi_scope_get(ctx, NULL);
      }

      p = end + 1;
   }

   // A package is only known by the subroutines it exports
   for (dpi_scope_t *it = dpi_all_scopes; it != NULL; it = it->next) {
      const char *full = dpi_scope_fullname(it);
      if (full != NULL && !strcasecmp(full, name))
         return it;
   }

   return NULL;
}

DLLEXPORT
svScope svGetScope(void)
{
   return dpi_current;
}

DLLEXPORT
svScope svSetScope(svScope scope)
{
   svScope old = dpi_current;
   dpi_current = scope;
   return old;
}

DLLEXPORT
const char *svGetNameFromScope(svScope scope)
{
   return scope != NULL ? dpi_scope_fullname(scope) : NULL;
}

DLLEXPORT
svScope svGetScopeFromName(const char *name)
{
   return name != NULL ? dpi_scope_find(name) : NULL;
}

DLLEXPORT
int svPutUserData(svScope scope, void *key, void *data)
{
   dpi_scope_t *s = scope;
   if (s == NULL)
      return -1;

   for (int i = 0; i < s->nuser; i++) {
      if (s->user_keys[i] == key) {
         s->user_data[i] = data;
         return 0;
      }
   }

   if (s->nuser == ARRAY_LEN(s->user_keys))
      return -1;

   s->user_keys[s->nuser] = key;
   s->user_data[s->nuser++] = data;
   return 0;
}

DLLEXPORT
void *svGetUserData(svScope scope, void *key)
{
   dpi_scope_t *s = scope;
   if (s == NULL)
      return NULL;

   for (int i = 0; i < s->nuser; i++) {
      if (s->user_keys[i] == key)
         return s->user_data[i];
   }

   return NULL;
}

DLLEXPORT
int svGetCallerInfo(const char **file, int *line)
{
   return 0;
}

DLLEXPORT
int svIsDisabledState(void)
{
#ifdef DPI_FIBERS
   return dpi_fiber != NULL && dpi_fiber->disabled;
#else
   return 0;
#endif
}

DLLEXPORT
void svAckDisabledState(void)
{
#ifdef DPI_FIBERS
   if (dpi_fiber != NULL)
      dpi_fiber->disabled = false;
#endif
}

DLLEXPORT
const char *svDpiVersion(void)
{
   return "1800-2005";
}

DLLEXPORT
svBit svGetBitselBit(const svBitVecVal *s, int i)
{
   return (s[i / 32] >> (i % 32)) & 1;
}

DLLEXPORT
svLogic svGetBitselLogic(const svLogicVecVal *s, int i)
{
   return ((s[i / 32].aval >> (i % 32)) & 1)
      | (((s[i / 32].bval >> (i % 32)) & 1) << 1);
}

DLLEXPORT
void svPutBitselBit(svBitVecVal *d, int i, svBit s)
{
   d[i / 32] = (d[i / 32] & ~(1u << (i % 32))) | ((unsigned)(s & 1) << (i % 32));
}

DLLEXPORT
void svPutBitselLogic(svLogicVecVal *d, int i, svLogic s)
{
   d[i / 32].aval = (d[i / 32].aval & ~(1u << (i % 32)))
      | ((unsigned)(s & 1) << (i % 32));
   d[i / 32].bval = (d[i / 32].bval & ~(1u << (i % 32)))
      | ((unsigned)((s >> 1) & 1) << (i % 32));
}

DLLEXPORT
void svGetPartselBit(svBitVecVal *d, const svBitVecVal *s, int i, int w)
{
   for (int k = 0; k < (w + 31) / 32; k++)
      d[k] = 0;
   for (int k = 0; k < w; k++)
      svPutBitselBit(d, k, svGetBitselBit(s, i + k));
}

DLLEXPORT
svBitVecVal svGetBits(const svBitVecVal *s, int i, int w)
{
   svBitVecVal v = 0;
   for (int k = 0; k < w && k < 32; k++)
      v |= (svBitVecVal)svGetBitselBit(s, i + k) << k;
   return v;
}

DLLEXPORT
void svGetPartselLogic(svLogicVecVal *d, const svLogicVecVal *s, int i, int w)
{
   for (int k = 0; k < (w + 31) / 32; k++)
      d[k].aval = d[k].bval = 0;
   for (int k = 0; k < w; k++)
      svPutBitselLogic(d, k, svGetBitselLogic(s, i + k));
}

DLLEXPORT
void svPutPartselBit(svBitVecVal *d, const svBitVecVal s, int i, int w)
{
   for (int k = 0; k < w && k < 32; k++)
      svPutBitselBit(d, i + k, (s >> k) & 1);
}

DLLEXPORT
void svPutPartselLogic(svLogicVecVal *d, const svLogicVecVal s, int i, int w)
{
   for (int k = 0; k < w && k < 32; k++)
      svPutBitselLogic(d, i + k, ((s.aval >> k) & 1) | (((s.bval >> k) & 1) << 1));
}

////////////////////////////////////////////////////////////////////////////////
// Open array access functions (IEEE 1800-2017 H.12)

static const dpi_dim_t *dpi_oa_dim(const dpi_oa_t *oa, int d)
{
   if (d == 0)
      return oa->has_packed ? &oa->packed : NULL;
   else if (d >= 1 && d <= oa->ndims)
      return &oa->dim[d - 1];
   else
      return NULL;
}

DLLEXPORT
int svLeft(const svOpenArrayHandle h, int d)
{
   const dpi_dim_t *dim = dpi_oa_dim(h, d);
   return dim != NULL ? dim->left : 0;
}

DLLEXPORT
int svRight(const svOpenArrayHandle h, int d)
{
   const dpi_dim_t *dim = dpi_oa_dim(h, d);
   return dim != NULL ? dim->right : 0;
}

DLLEXPORT
int svLow(const svOpenArrayHandle h, int d)
{
   const dpi_dim_t *dim = dpi_oa_dim(h, d);
   return dim != NULL ? MIN(dim->left, dim->right) : 0;
}

DLLEXPORT
int svHigh(const svOpenArrayHandle h, int d)
{
   const dpi_dim_t *dim = dpi_oa_dim(h, d);
   return dim != NULL ? MAX(dim->left, dim->right) : 0;
}

DLLEXPORT
int svIncrement(const svOpenArrayHandle h, int d)
{
   const dpi_dim_t *dim = dpi_oa_dim(h, d);
   return dim != NULL && dim->left < dim->right ? -1 : 1;
}

DLLEXPORT
int svSize(const svOpenArrayHandle h, int d)
{
   const dpi_dim_t *dim = dpi_oa_dim(h, d);
   return dim != NULL ? dim->size : 0;
}

DLLEXPORT
int svDimensions(const svOpenArrayHandle h)
{
   const dpi_oa_t *oa = h;
   return oa->ndims + (oa->has_packed ? 1 : 0);
}

DLLEXPORT
void *svGetArrayPtr(const svOpenArrayHandle h)
{
   const dpi_oa_t *oa = h;
   return oa->buf;
}

DLLEXPORT
int svSizeOfArray(const svOpenArrayHandle h)
{
   const dpi_oa_t *oa = h;
   return oa->count * oa->esize;
}

static void *dpi_oa_find(const dpi_oa_t *oa, const int *idx)
{
   // The indices are those of the actual argument

   size_t pos = 0;
   for (int i = 0; i < oa->ndims; i++) {
      const dpi_dim_t *dim = &(oa->dim[i]);
      const int64_t off = dim->left <= dim->right
         ? idx[i] - dim->left : dim->left - idx[i];
      if (off < 0 || off >= dim->size)
         return NULL;
      pos = pos * dim->size + off;
   }

   return (char *)oa->buf + pos * oa->esize;
}

static void *dpi_oa_find_va(const dpi_oa_t *oa, int indx1, va_list ap)
{
   int idx[DPI_MAX_DIMS] = { indx1 };
   for (int i = 1; i < oa->ndims; i++)
      idx[i] = va_arg(ap, int);

   return dpi_oa_find(oa, idx);
}

DLLEXPORT
void *svGetArrElemPtr(const svOpenArrayHandle h, int indx1, ...)
{
   va_list ap;
   va_start(ap, indx1);
   void *ptr = dpi_oa_find_va(h, indx1, ap);
   va_end(ap);
   return ptr;
}

DLLEXPORT
void *svGetArrElemPtr1(const svOpenArrayHandle h, int indx1)
{
   const int idx[] = { indx1 };
   return dpi_oa_find(h, idx);
}

DLLEXPORT
void *svGetArrElemPtr2(const svOpenArrayHandle h, int indx1, int indx2)
{
   const int idx[] = { indx1, indx2 };
   return dpi_oa_find(h, idx);
}

DLLEXPORT
void *svGetArrElemPtr3(const svOpenArrayHandle h, int indx1, int indx2,
                       int indx3)
{
   const int idx[] = { indx1, indx2, indx3 };
   return dpi_oa_find(h, idx);
}

static int dpi_oa_words(const dpi_oa_t *oa)
{
   return oa->elem.kind == 'B' || oa->elem.kind == 'L'
      ? (oa->elem.width + 31) / 32 : 1;
}

static void dpi_oa_get_bits(const dpi_oa_t *oa, const void *p, svBitVecVal *d)
{
   const int nwords = dpi_oa_words(oa);
   switch (oa->elem.kind) {
   case 'B':
      memcpy(d, p, oa->esize);
      break;
   case 'L':
      for (int i = 0; i < nwords; i++)
         d[i] = ((const svLogicVecVal *)p)[i].aval;
      break;
   case 'b':
   case 'l':
      d[0] = *(const uint8_t *)p & 1;
      break;
   default:
      memset(d, 0, nwords * sizeof(svBitVecVal));
      memcpy(d, p, MIN(oa->esize, nwords * sizeof(svBitVecVal)));
      break;
   }
}

static void dpi_oa_get_logic(const dpi_oa_t *oa, const void *p,
                             svLogicVecVal *d)
{
   const int nwords = dpi_oa_words(oa);
   switch (oa->elem.kind) {
   case 'L':
      memcpy(d, p, oa->esize);
      break;
   case 'l':
      d[0].aval = *(const uint8_t *)p & 1;
      d[0].bval = (*(const uint8_t *)p >> 1) & 1;
      break;
   default:
      {
         svBitVecVal tmp[SV_PACKED_DATA_NELEMS(64)];
         if (nwords > ARRAY_LEN(tmp))
            return;
         dpi_oa_get_bits(oa, p, tmp);
         for (int i = 0; i < nwords; i++) {
            d[i].aval = tmp[i];
            d[i].bval = 0;
         }
      }
      break;
   }
}

static void dpi_oa_put_bits(const dpi_oa_t *oa, void *p, const svBitVecVal *s)
{
   const int nwords = dpi_oa_words(oa);
   switch (oa->elem.kind) {
   case 'B':
      memcpy(p, s, oa->esize);
      break;
   case 'L':
      for (int i = 0; i < nwords; i++) {
         ((svLogicVecVal *)p)[i].aval = s[i];
         ((svLogicVecVal *)p)[i].bval = 0;
      }
      break;
   case 'b':
   case 'l':
      *(uint8_t *)p = s[0] & 1;
      break;
   default:
      memcpy(p, s, oa->esize);
      break;
   }
}

static void dpi_oa_put_logic(const dpi_oa_t *oa, void *p,
                             const svLogicVecVal *s)
{
   const int nwords = dpi_oa_words(oa);
   switch (oa->elem.kind) {
   case 'L':
      memcpy(p, s, oa->esize);
      break;
   case 'l':
      *(uint8_t *)p = (s[0].aval & 1) | ((s[0].bval & 1) << 1);
      break;
   case 'b':
      *(uint8_t *)p = s[0].aval & 1;
      break;
   default:
      {
         svBitVecVal tmp[SV_PACKED_DATA_NELEMS(64)];
         if (nwords > ARRAY_LEN(tmp))
            return;
         for (int i = 0; i < nwords; i++)
            tmp[i] = s[i].aval;
         dpi_oa_put_bits(oa, p, tmp);
      }
      break;
   }
}

static svBit dpi_oa_get_bit(const dpi_oa_t *oa, const void *p)
{
   switch (oa->elem.kind) {
   case 'b':
   case 'l':
      return *(const uint8_t *)p & 1;
   case 'L':
      return ((const svLogicVecVal *)p)[0].aval & 1;
   default:
      return *(const uint8_t *)p & 1;
   }
}

static svLogic dpi_oa_get_logic_bit(const dpi_oa_t *oa, const void *p)
{
   switch (oa->elem.kind) {
   case 'l':
      return *(const uint8_t *)p & 3;
   case 'L':
      return (((const svLogicVecVal *)p)[0].aval & 1)
         | ((((const svLogicVecVal *)p)[0].bval & 1) << 1);
   default:
      return *(const uint8_t *)p & 1;
   }
}

static void dpi_oa_put_bit(const dpi_oa_t *oa, void *p, svBit v)
{
   switch (oa->elem.kind) {
   case 'b':
   case 'l':
      *(uint8_t *)p = v & 1;
      break;
   case 'L':
      ((svLogicVecVal *)p)[0].aval = (((svLogicVecVal *)p)[0].aval & ~1u)
         | (v & 1);
      ((svLogicVecVal *)p)[0].bval &= ~1u;
      break;
   default:
      *(uint32_t *)p = (*(uint32_t *)p & ~1u) | (v & 1);
      break;
   }
}

static void dpi_oa_put_logic_bit(const dpi_oa_t *oa, void *p, svLogic v)
{
   switch (oa->elem.kind) {
   case 'l':
      *(uint8_t *)p = v & 3;
      break;
   case 'L':
      ((svLogicVecVal *)p)[0].aval = (((svLogicVecVal *)p)[0].aval & ~1u)
         | (v & 1);
      ((svLogicVecVal *)p)[0].bval = (((svLogicVecVal *)p)[0].bval & ~1u)
         | ((v >> 1) & 1);
      break;
   default:
      dpi_oa_put_bit(oa, p, v);
      break;
   }
}

// Each access function has a variant with a fixed number of indices; all
// of them find the element and then call the same helper

#define DPI_OA_GET_VEC(NAME, N1, N2, N3, TYPE, CONV)                                \
   static void NAME##_at(TYPE *d, const svOpenArrayHandle h, void *p)   \
   {                                                                    \
      if (p != NULL)                                                    \
         CONV(h, p, d);                                                 \
   }                                                                    \
   DLLEXPORT void NAME(TYPE *d, const svOpenArrayHandle h, int indx1, ...)        \
   {                                                                    \
      va_list ap;                                                       \
      va_start(ap, indx1);                                              \
      void *p = dpi_oa_find_va(h, indx1, ap);                           \
      va_end(ap);                                                       \
      NAME##_at(d, h, p);                                               \
   }                                                                    \
   DLLEXPORT void N1(TYPE *d, const svOpenArrayHandle h, int i1)             \
   {                                                                    \
      NAME##_at(d, h, svGetArrElemPtr1(h, i1));                         \
   }                                                                    \
   DLLEXPORT void N2(TYPE *d, const svOpenArrayHandle h, int i1, int i2)     \
   {                                                                    \
      NAME##_at(d, h, svGetArrElemPtr2(h, i1, i2));                     \
   }                                                                    \
   DLLEXPORT void N3(TYPE *d, const svOpenArrayHandle h, int i1, int i2,     \
                int i3)                                                 \
   {                                                                    \
      NAME##_at(d, h, svGetArrElemPtr3(h, i1, i2, i3));                 \
   }

#define DPI_OA_PUT_VEC(NAME, N1, N2, N3, TYPE, CONV)                                \
   static void NAME##_at(const svOpenArrayHandle h, const TYPE *s,      \
                         void *p)                                       \
   {                                                                    \
      if (p != NULL)                                                    \
         CONV(h, p, s);                                                 \
   }                                                                    \
   DLLEXPORT void NAME(const svOpenArrayHandle h, const TYPE *s, int indx1, ...)  \
   {                                                                    \
      va_list ap;                                                       \
      va_start(ap, indx1);                                              \
      void *p = dpi_oa_find_va(h, indx1, ap);                           \
      va_end(ap);                                                       \
      NAME##_at(h, s, p);                                               \
   }                                                                    \
   DLLEXPORT void N1(const svOpenArrayHandle h, const TYPE *s, int i1)       \
   {                                                                    \
      NAME##_at(h, s, svGetArrElemPtr1(h, i1));                         \
   }                                                                    \
   DLLEXPORT void N2(const svOpenArrayHandle h, const TYPE *s, int i1,       \
                int i2)                                                 \
   {                                                                    \
      NAME##_at(h, s, svGetArrElemPtr2(h, i1, i2));                     \
   }                                                                    \
   DLLEXPORT void N3(const svOpenArrayHandle h, const TYPE *s, int i1,       \
                int i2, int i3)                                         \
   {                                                                    \
      NAME##_at(h, s, svGetArrElemPtr3(h, i1, i2, i3));                 \
   }

#define DPI_OA_GET_SCALAR(NAME, RET, CONV)                              \
   DLLEXPORT RET NAME(const svOpenArrayHandle h, int indx1, ...)                  \
   {                                                                    \
      va_list ap;                                                       \
      va_start(ap, indx1);                                              \
      void *p = dpi_oa_find_va(h, indx1, ap);                           \
      va_end(ap);                                                       \
      return p != NULL ? CONV(h, p) : 0;                                \
   }                                                                    \
   DLLEXPORT RET NAME##1(const svOpenArrayHandle h, int i1)                       \
   {                                                                    \
      void *p = svGetArrElemPtr1(h, i1);                                \
      return p != NULL ? CONV(h, p) : 0;                                \
   }                                                                    \
   DLLEXPORT RET NAME##2(const svOpenArrayHandle h, int i1, int i2)               \
   {                                                                    \
      void *p = svGetArrElemPtr2(h, i1, i2);                            \
      return p != NULL ? CONV(h, p) : 0;                                \
   }                                                                    \
   DLLEXPORT RET NAME##3(const svOpenArrayHandle h, int i1, int i2, int i3)       \
   {                                                                    \
      void *p = svGetArrElemPtr3(h, i1, i2, i3);                        \
      return p != NULL ? CONV(h, p) : 0;                                \
   }

#define DPI_OA_PUT_SCALAR(NAME, TYPE, CONV)                             \
   DLLEXPORT void NAME(const svOpenArrayHandle h, TYPE v, int indx1, ...)         \
   {                                                                    \
      va_list ap;                                                       \
      va_start(ap, indx1);                                              \
      void *p = dpi_oa_find_va(h, indx1, ap);                           \
      va_end(ap);                                                       \
      if (p != NULL)                                                    \
         CONV(h, p, v);                                                 \
   }                                                                    \
   DLLEXPORT void NAME##1(const svOpenArrayHandle h, TYPE v, int i1)              \
   {                                                                    \
      void *p = svGetArrElemPtr1(h, i1);                                \
      if (p != NULL)                                                    \
         CONV(h, p, v);                                                 \
   }                                                                    \
   DLLEXPORT void NAME##2(const svOpenArrayHandle h, TYPE v, int i1, int i2)      \
   {                                                                    \
      void *p = svGetArrElemPtr2(h, i1, i2);                            \
      if (p != NULL)                                                    \
         CONV(h, p, v);                                                 \
   }                                                                    \
   DLLEXPORT void NAME##3(const svOpenArrayHandle h, TYPE v, int i1, int i2,      \
                int i3)                                                 \
   {                                                                    \
      void *p = svGetArrElemPtr3(h, i1, i2, i3);                        \
      if (p != NULL)                                                    \
         CONV(h, p, v);                                                 \
   }

DPI_OA_GET_VEC(svGetBitArrElemVecVal, svGetBitArrElem1VecVal,
               svGetBitArrElem2VecVal, svGetBitArrElem3VecVal, svBitVecVal, dpi_oa_get_bits)
DPI_OA_GET_VEC(svGetLogicArrElemVecVal, svGetLogicArrElem1VecVal,
               svGetLogicArrElem2VecVal, svGetLogicArrElem3VecVal, svLogicVecVal, dpi_oa_get_logic)
DPI_OA_PUT_VEC(svPutBitArrElemVecVal, svPutBitArrElem1VecVal,
               svPutBitArrElem2VecVal, svPutBitArrElem3VecVal, svBitVecVal, dpi_oa_put_bits)
DPI_OA_PUT_VEC(svPutLogicArrElemVecVal, svPutLogicArrElem1VecVal,
               svPutLogicArrElem2VecVal, svPutLogicArrElem3VecVal, svLogicVecVal, dpi_oa_put_logic)
DPI_OA_GET_SCALAR(svGetBitArrElem, svBit, dpi_oa_get_bit)
DPI_OA_GET_SCALAR(svGetLogicArrElem, svLogic, dpi_oa_get_logic_bit)
DPI_OA_PUT_SCALAR(svPutBitArrElem, svBit, dpi_oa_put_bit)
DPI_OA_PUT_SCALAR(svPutLogicArrElem, svLogic, dpi_oa_put_logic_bit)
