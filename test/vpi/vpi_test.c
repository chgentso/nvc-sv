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
