#include "vhpi_test.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#define check_int(a, b) fail_unless((long)(a) == (long)(b))

static vhpiHandleT procs[8];
static int nprocs;
static int nchecked;

static int contains_ci(const char *hay, const char *needle)
{
   const size_t n = strlen(needle);
   for (; *hay; hay++) {
      if (!strncasecmp(hay, needle, n))
         return 1;
   }
   return 0;
}

static vhpiHandleT find(const char *name)
{
   for (int i = 0; i < nprocs; i++) {
      const char *n = (const char *)vhpi_get_str(vhpiFullNameP, procs[i]);
      if (n != NULL && contains_ci(n, name))
         return procs[i];
   }
   vhpi_assert(vhpiFailure, "no process %s", name);
   return NULL;
}

static int get_int_var(vhpiHandleT proc, const char *name)
{
   vhpiHandleT it = VHPI_CHECK(vhpi_iterator(vhpiVarDecls, proc));
   vhpiHandleT var;
   while ((var = vhpi_scan(it)) != NULL) {
      if (!strcasecmp((const char *)vhpi_get_str(vhpiNameP, var), name)) {
         vhpiValueT v = { .format = vhpiIntVal };
         fail_if(vhpi_get_value(var, &v));
         vhpi_release_handle(it);
         return v.value.intg;
      }
   }
   vhpi_assert(vhpiFailure, "no variable %s", name);
   return 0;
}

static void get_bits_var(vhpiHandleT proc, const char *name, char *buf)
{
   vhpiHandleT it = VHPI_CHECK(vhpi_iterator(vhpiVarDecls, proc));
   vhpiHandleT var;
   while ((var = vhpi_scan(it)) != NULL) {
      if (!strcasecmp((const char *)vhpi_get_str(vhpiNameP, var), name)) {
         vhpiValueT v = { .format = vhpiBinStrVal, .bufSize = 32,
                          .value.str = (vhpiCharT *)buf };
         fail_if(vhpi_get_value(var, &v));
         vhpi_release_handle(it);
         return;
      }
   }
   vhpi_assert(vhpiFailure, "no variable %s", name);
}

static void at5(const vhpiCbDataT *cb)
{
   vhpiHandleT root = VHPI_CHECK(vhpi_handle(vhpiRootInst, NULL));
   vhpiHandleT it = VHPI_CHECK(vhpi_iterator(vhpiStmts, root));
   vhpiHandleT h;
   while ((h = vhpi_scan(it)) != NULL) {
      if (vhpi_get(vhpiKindP, h) == vhpiProcessStmtK && nprocs < 8)
         procs[nprocs++] = h;
   }
   check_int(nprocs, 3);

   vhpiHandleT p = find("P_COUNT");
   fail_if(strcasecmp((const char *)vhpi_get_str(vhpiNameP, p), "p_count"));
   check_int(vhpi_get(vhpiIsPostponedP, p), 0);
   check_int(vhpi_get(vhpiIsPostponedP, find("P_POST")), 1);
   check_int(vhpi_get(vhpiLineNoP, p), 11);

   // Suspended in wait for 10 ns
   check_int(vhpi_get(vhpiNvcStateP, p), vhpiNvcProcTimeout);
   vhpiPhysT wake = vhpi_get_phys(vhpiNvcWakeTimeP, p);
   check_int(wake.low, 10000000);

   // Waiting for a signal only
   check_int(vhpi_get(vhpiNvcStateP, find("P_SENS")), 0);

   check_int(get_int_var(p, "n"), 5);
   check_int(get_int_var(find("P_SENS"), "t"), 8);   // it ran once at start
   char bits[32];
   get_bits_var(p, "v", bits);
   fail_if(strcmp(bits, "1010"));
   nchecked++;
}

static void at15(const vhpiCbDataT *cb)
{
   vhpiHandleT p = find("P_COUNT");
   check_int(vhpi_get(vhpiNvcStateP, p), 0);   // now waits on s
   check_int(get_int_var(p, "n"), 6);
   char bits[32];
   get_bits_var(p, "v", bits);
   fail_if(strcmp(bits, "0101"));

   check_int(get_int_var(find("P_SENS"), "t"), 8);
   nchecked++;
}

static void add(void (*fn)(const vhpiCbDataT *), unsigned ns)
{
   vhpiTimeT t = { .low = ns * 1000000u };
   vhpiCbDataT cb = { .reason = vhpiCbAfterDelay, .cb_rtn = fn, .time = &t };
   VHPI_CHECK(vhpi_register_cb(&cb, 0));
}

static void start_of_sim(const vhpiCbDataT *cb_data)
{
   add(at5, 5);
   add(at15, 15);
}

static void end_of_sim(const vhpiCbDataT *cb_data)
{
   check_int(nchecked, 2);
}

void vhpi31_startup(void)
{
   vhpiCbDataT cb_data = {
      .reason = vhpiCbStartOfSimulation,
      .cb_rtn = start_of_sim,
   };
   VHPI_CHECK(vhpi_register_cb(&cb_data, 0));

   cb_data.reason = vhpiCbEndOfSimulation;
   cb_data.cb_rtn = end_of_sim;
   VHPI_CHECK(vhpi_register_cb(&cb_data, 0));
}
