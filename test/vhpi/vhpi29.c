#include "vhpi_test.h"

#include <stdio.h>
#include <string.h>

#define check_int(a, b) fail_unless((long)(a) == (long)(b))

static vhpiHandleT s, s0, r, rb, x;
static int nchecked;

static int is_forced(vhpiHandleT h)
{
   const vhpiIntT v = vhpi_get(vhpiIsForcedP, h);
   fail_if(v == vhpiUndefined);
   return v;
}

static void check_str(vhpiHandleT h, const char *want)
{
   char buf[64];
   vhpiValueT v = { .format = vhpiBinStrVal, .bufSize = sizeof(buf),
                    .value.str = (vhpiCharT *)buf };
   fail_if(vhpi_get_value(h, &v));
   if (strcmp(buf, want))
      vhpi_assert(vhpiFailure, "got '%s' want '%s'", buf, want);
}

static void put_str(vhpiHandleT h, const char *str, vhpiPutValueModeT mode)
{
   char buf[64];
   strcpy(buf, str);
   vhpiValueT v = { .format = vhpiBinStrVal, .bufSize = strlen(str) + 1,
                    .numElems = (int)strlen(str), .value.str = (vhpiCharT *)buf };
   fail_if(vhpi_put_value(h, &v, mode));
}

static void phase0(const vhpiCbDataT *cb)    // 5 ns: nothing is forced yet
{
   check_int(is_forced(s), 0);
   check_int(is_forced(s0), 0);
   check_int(is_forced(r), 0);
   check_int(is_forced(x), 0);

   put_str(s, "1010", vhpiForcePropagate);

   put_str(rb, "10", vhpiDepositPropagate);
   nchecked++;
}

static void phase1(const vhpiCbDataT *cb)    // 7 ns: the force has taken effect
{
   check_str(s, "1010");
   check_int(is_forced(s), 1);
   check_int(is_forced(s0), 1);
   check_int(is_forced(r), 0);     // only a deposit
   check_int(is_forced(rb), 0);
   check_str(rb, "10");
   check_int(is_forced(x), 0);
   nchecked++;
}

static void phase2(const vhpiCbDataT *cb)    // 15 ns: the driver is overridden
{
   check_str(s, "1010");
   check_int(is_forced(s), 1);
   check_str(rb, "11");            // the deposit was overwritten by a driver
   fail_if(vhpi_put_value(s, NULL, vhpiRelease));
   nchecked++;
}

static void phase3(const vhpiCbDataT *cb)    // 17 ns: released
{
   check_int(is_forced(s), 0);
   check_int(is_forced(s0), 0);
   check_str(s, "0101");           // the value of the driver again
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
   vhpiHandleT root = VHPI_CHECK(vhpi_handle(vhpiRootInst, NULL));
   s = VHPI_CHECK(vhpi_handle_by_name("s", root));
   s0 = VHPI_CHECK(vhpi_handle_by_index(vhpiIndexedNames, s, 0));
   r = VHPI_CHECK(vhpi_handle_by_name("r", root));
   rb = VHPI_CHECK(vhpi_handle_by_name("r.b", root));
   x = VHPI_CHECK(vhpi_handle_by_name("x", root));
   vhpi_release_handle(root);

   add(phase0, 5);
   add(phase1, 7);
   add(phase2, 15);
   add(phase3, 17);
}

static void end_of_sim(const vhpiCbDataT *cb_data)
{
   check_int(nchecked, 4);
}

void vhpi29_startup(void)
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
