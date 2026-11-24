#include "vpi_test.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
   const char *name;
   void (*startup)(void);
} vpi_test_t;

static const vpi_test_t tests[] = {
   { "vpi1",  vpi1_startup },
   { "vpi2",  vpi2_startup },
   { "vpi3",  vpi3_startup },
   { "vpi4",  vpi4_startup },
   { "vpi5",  vpi5_startup },
   { "vpi6",  vpi6_startup },
   { "vpi7",  vpi7_startup },
   { "vpi8",  vpi8_startup },
   { "vpi9",  vpi9_startup },
   { "vpi10", vpi10_startup },
   { "vpi11", vpi11_startup },
   { "vpi12", vpi12_startup },
   { "vpi13", vpi13_startup },
   { "vpi14", vpi14_startup },
   { "vpi15", vpi15_startup },
   { "vpi16", vpi16_startup },
   { "vpi17", vpi17_startup },
   { "vpi18", vpi18_startup },
   { "vpi19", vpi19_startup },
   { "vpi20", vpi20_startup },
   { "vpi21", vpi21_startup },
   { "vpi22", vpi22_startup },
   { "vpi23", vpi23_startup },
   { "vpi24", vpi24_startup },
   { "vpi25", vpi25_startup },
   { "vpi26", vpi26_startup },
   { "vpi27", vpi27_startup },
   { "vpi28", vpi28_startup },
   { "vpi29", vpi29_startup },
   { "vpicls1", vpicls1_startup },
   { "vpicls2", vpicls2_startup },
   { "vpicls3", vpicls3_startup },
   { "vpicls4", vpicls4_startup },
   { "vpicls5", vpicls5_startup },
   { "vpicls6", vpicls6_startup },
   { "vpi_packed1", vpi_packed1_startup },
   { "vpi_fast_direct1", vpi_fast_direct1_startup },
   { NULL,    NULL },
};

static bool failed = false;

void vpi_test_fail(const char *file, int line, const char *fmt, ...)
{
   char buf[512];
   va_list ap;
   va_start(ap, fmt);
   vsnprintf(buf, sizeof(buf), fmt, ap);
   va_end(ap);

   // Use stdio as well in case vpi_printf is what is broken
   printf("FAILED: %s:%d: %s\n", file, line, buf);
   fflush(stdout);

   if (!failed) {
      failed = true;
      vpi_control(vpiFinish, 1);
   }
}

void vpi_test_pass(void)
{
   if (!failed) {
      vpi_printf("PASSED\n");
      fflush(stdout);
   }
}

void __check_error(const char *file, int lineno)
{
   s_vpi_error_info info;
   if (vpi_chk_error(&info))
      vpi_test_fail(file, lineno, "unexpected error '%s'", info.message);
}

vpiHandle __check_handle(vpiHandle h, const char *file, int lineno)
{
   __check_error(file, lineno);

   if (h == NULL)
      vpi_test_fail(file, lineno, "unexpected NULL handle");

   return h;
}

void __check_string(const char *v, const char *s, const char *file,
                    int lineno)
{
   if (v == NULL)
      vpi_test_fail(file, lineno, "(null) != \"%s\"", s);
   else if (strcmp(v, s) != 0)
      vpi_test_fail(file, lineno, "\"%s\" != \"%s\"", v, s);
}

void __check_int(PLI_INT64 v, PLI_INT64 n, const char *file, int lineno)
{
   if (v != n)
      vpi_test_fail(file, lineno, "%" PRIi64 " != %" PRIi64, v, n);
}

static void collect_conn(const vpi_nvc_conn_t *c, void *user)
{
   vpi_test_conns_t *out = user;
   int *n = c->is_load ? &(out->nloads) : &(out->n);
   if (*n >= 16)
      return;

   vpi_test_conn_t *it = c->is_load ? &(out->load[(*n)++])
      : &(out->item[(*n)++]);
   it->c = *c;

   // The strings are only valid during the callback
#define COPY(field, buf)                                                \
   do {                                                                 \
      snprintf(it->buf, sizeof(it->buf), "%s", c->field ?: "");         \
      it->c.field = c->field ? it->buf : NULL;                          \
   } while (0)
   COPY(name, name);
   COPY(proc_kind, kind);
   COPY(scope, scope);
   COPY(file, file);
   COPY(other, other);
#undef COPY
}

int vpi_test_connections(vpi_test_conns_t *out, const char *path, int first,
                         int count)
{
   memset(out, 0, sizeof(*out));
   const PLI_INT32 r = vpi_nvc_connections(path, first, count, collect_conn,
                                           out);
   if (r < 0)
      vpi_test_fail(__FILE__, __LINE__, "no signal %s", path);
   return out->n;
}

static void collect_procsig(const vpi_nvc_procsig_t *sig, void *user)
{
   vpi_test_procsigs_t *out = user;
   if (out->n >= 16)
      return;

   int i = out->n++;
   snprintf(out->item[i].path, sizeof(out->item[i].path), "%s", sig->path);
   out->item[i].first = sig->first;
   out->item[i].count = sig->count;
   out->item[i].width = sig->width;
   out->item[i].write = sig->is_write;
}

int vpi_test_process_signals(vpi_test_procsigs_t *out, const char *scope,
                             const char *name)
{
   memset(out, 0, sizeof(*out));
   if (vpi_nvc_process_signals(scope, name, collect_procsig, out) < 0)
      vpi_test_fail(__FILE__, __LINE__, "no process %s in %s", name, scope);
   return out->n;
}

const vpi_test_conn_t *vpi_test_conn_at(const vpi_test_conns_t *cs,
                                        const char *file, int line)
{
   for (int i = 0; i < cs->n; i++) {
      const vpi_test_conn_t *c = &(cs->item[i]);
      const size_t flen = strlen(file), len = strlen(c->file);
      if (c->c.line == line && len >= flen
          && strcmp(c->file + len - flen, file) == 0)
         return c;
   }

   return NULL;
}

const vpi_test_conn_t *vpi_test_load_at(const vpi_test_conns_t *cs,
                                        const char *file, int line)
{
   for (int i = 0; i < cs->nloads; i++) {
      const vpi_test_conn_t *c = &(cs->load[i]);
      const size_t flen = strlen(file), len = strlen(c->file);
      if (c->c.line == line && len >= flen
          && strcmp(c->file + len - flen, file) == 0)
         return c;
   }

   return NULL;
}

vpiHandle vpi_test_cb(PLI_INT32 reason, PLI_INT32 (*fn)(p_cb_data),
                      PLI_UINT64 delay, void *user)
{
   s_vpi_time time = {
      .type = vpiSimTime,
      .high = delay >> 32,
      .low  = delay & 0xffffffff,
   };
   s_cb_data cb = {
      .reason    = reason,
      .cb_rtn    = fn,
      .time      = &time,
      .user_data = user,
   };

   vpiHandle h = vpi_register_cb(&cb);
   check_handle(h);
   return h;
}

vpiHandle vpi_test_lookup(const char *name)
{
   vpiHandle h = vpi_handle_by_name((PLI_BYTE8 *)name, NULL);
   if (h == NULL)
      vpi_test_fail(__FILE__, __LINE__, "cannot find %s", name);
   return h;
}

PLI_UINT64 vpi_test_now(void)
{
   s_vpi_time t = { .type = vpiSimTime };
   vpi_get_time(NULL, &t);
   return ((PLI_UINT64)t.high << 32) | t.low;
}

static void shared_startup(void)
{
   const char *test_name = getenv("TEST_NAME");
   if (test_name == NULL) {
      printf("FAILED: TEST_NAME environment variable not set\n");
      return;
   }

   for (const vpi_test_t *p = tests; p->name; p++) {
      if (strcmp(p->name, test_name) == 0) {
         (*p->startup)();
         return;
      }
   }

   printf("FAILED: unknown test %s\n", test_name);
}

// IEEE 1800-2017 section 36.9.1: the simulator calls each routine in
// this NULL-terminated array when the application is loaded
void (*vlog_startup_routines[])(void) = {
   shared_startup,
   NULL
};
