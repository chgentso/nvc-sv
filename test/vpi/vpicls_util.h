#ifndef _VPICLS_UTIL_H
#define _VPICLS_UTIL_H

// Helpers shared by the class object tests; include sv_vpi_user.h first

#include "sv_vpi_user.h"
#include "vpi_test.h"

#include <stdio.h>
#include <string.h>

// The variable of a scope, object or class typespec with this name; the nth
// one if there are several, and fails if there is none
static inline vpiHandle vpicls_var(vpiHandle scope, const char *name, int nth)
{
   vpiHandle it = vpi_iterate(vpiVariables, scope);
   fail_if(it == NULL);

   vpiHandle v;
   while ((v = vpi_scan(it)) != NULL) {
      const char *n = vpi_get_str(vpiName, v);
      if (n != NULL && strcmp(n, name) == 0 && nth-- == 0) {
         vpi_release_handle(it);
         return v;
      }
      vpi_release_handle(v);
   }

   vpi_test_fail(__FILE__, __LINE__, "no variable %s", name);
   return NULL;
}

// The names of everything an iteration finds, separated by spaces, and the
// number of them
static inline int vpicls_names(PLI_INT32 type, vpiHandle scope, char *buf,
                               size_t len)
{
   buf[0] = '\0';
   int n = 0;
   vpiHandle it = vpi_iterate(type, scope);
   if (it == NULL)
      return 0;

   vpiHandle v;
   while ((v = vpi_scan(it)) != NULL) {
      const char *name = vpi_get_str(vpiName, v);
      if (strlen(buf) + strlen(name) + 2 < len)
         strcat(strcat(buf, n > 0 ? " " : ""), name);
      n++;
      vpi_release_handle(v);
   }

   return n;
}

static inline int vpicls_int(vpiHandle h)
{
   s_vpi_value v = { .format = vpiIntVal };
   vpi_get_value(h, &v);
   check_error();
   return v.value.integer;
}

static inline double vpicls_real(vpiHandle h)
{
   s_vpi_value v = { .format = vpiRealVal };
   vpi_get_value(h, &v);
   check_error();
   return v.value.real;
}

static inline const char *vpicls_str(vpiHandle h, PLI_INT32 format)
{
   static char buf[256];
   s_vpi_value v = { .format = format };
   vpi_get_value(h, &v);
   check_error();
   snprintf(buf, sizeof(buf), "%s", v.value.str);
   return buf;
}

// Object number of a variable or object
static inline PLI_INT64 vpicls_oid(vpiHandle h)
{
   PLI_INT64 id = vpi_get64(vpiObjId, h);
   check_error();
   return id;
}

// The members that vpi_nvc_object_members reports, copied
typedef struct {
   int  n;
   struct {
      char      name[32];
      char      type_name[48];
      char      owner[32];
      PLI_INT32 kind, size, flags, unknown;
      PLI_INT64 ival;
      double    rval;
      char      sval[64];
      PLI_INT64 addr;
      vpiHandle handle;
   } item[40];
} vpicls_members_t;

static inline void vpicls_collect(const vpi_nvc_member_t *m, void *user)
{
   vpicls_members_t *out = user;
   if (out->n >= 40)
      return;

   __typeof__(out->item[0]) *it = &(out->item[out->n++]);
   snprintf(it->name, sizeof(it->name), "%s", m->name);
   snprintf(it->type_name, sizeof(it->type_name), "%s", m->type_name);
   snprintf(it->owner, sizeof(it->owner), "%s", m->owner);
   snprintf(it->sval, sizeof(it->sval), "%s", m->sval ?: "");
   it->kind    = m->kind;
   it->size    = m->size;
   it->flags   = m->flags;
   it->unknown = m->unknown;
   it->ival    = m->ival;
   it->rval    = m->rval;
   it->addr    = m->addr;
   it->handle  = m->handle;
}

static inline int vpicls_members(vpiHandle obj, vpicls_members_t *out)
{
   out->n = 0;
   const int n = vpi_nvc_object_members(obj, vpicls_collect, out);
   check_error();
   return n;
}

static inline void vpicls_release_members(vpicls_members_t *ms)
{
   for (int i = 0; i < ms->n; i++) {
      if (ms->item[i].handle != NULL)
         vpi_release_handle(ms->item[i].handle);
   }
}

#endif  // _VPICLS_UTIL_H
