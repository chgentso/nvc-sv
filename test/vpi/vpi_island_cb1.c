#include "vpi_test.h"

#include <stdio.h>
#include <string.h>

static void show(const char *what, p_cb_data cb)
{
   s_vpi_time t = { .type = vpiSimTime };
   vpi_get_time(NULL, &t);
   vpi_printf("%u: %s=%02x\n", t.low, what, (unsigned)cb->value->value.integer);
}

static PLI_INT32 on_w1(p_cb_data cb) { show("w1", cb); return 0; }
static PLI_INT32 on_obs(p_cb_data cb) { show("obs", cb); return 0; }
static PLI_INT32 on_q(p_cb_data cb) { show("q", cb); return 0; }

static void put(const char *name, int value, int flags)
{
   vpiHandle h = vpi_test_lookup(name);
   s_vpi_value v = { .format = vpiIntVal, .value.integer = value };
   vpi_put_value(h, &v, NULL, flags);
   check_error();
}

static PLI_INT32 at_12(p_cb_data cb)
{
   vpi_printf("12: force w1\n");
   put("vpi_island_cb1.w1", 0x11, vpiForceFlag);
   return 0;
}

static PLI_INT32 at_22(p_cb_data cb)
{
   vpi_printf("22: release w1\n");
   put("vpi_island_cb1.w1", 0, vpiReleaseFlag);
   return 0;
}

static PLI_INT32 at_27(p_cb_data cb)
{
   vpi_printf("27: force q\n");
   put("vpi_island_cb1.q", 0x22, vpiForceFlag);
   return 0;
}

static PLI_INT32 at_33(p_cb_data cb)
{
   vpi_printf("33: release q\n");
   put("vpi_island_cb1.q", 0, vpiReleaseFlag);
   return 0;
}

static void watch(const char *name, PLI_INT32 (*fn)(p_cb_data))
{
   static s_vpi_value v = { .format = vpiIntVal };
   static s_vpi_time tm = { .type = vpiSuppressTime };
   s_cb_data cbd = {
      .reason = cbValueChange,
      .cb_rtn = fn,
      .obj    = vpi_test_lookup(name),
      .value  = &v,
      .time   = &tm,
   };
   check_handle(vpi_register_cb(&cbd));
}

static PLI_INT32 on_start(p_cb_data cb)
{
   watch("vpi_island_cb1.w1", on_w1);
   watch("vpi_island_cb1.obs", on_obs);
   watch("vpi_island_cb1.q", on_q);
   vpi_test_cb(cbAfterDelay, at_12, 12, NULL);
   vpi_test_cb(cbAfterDelay, at_22, 22, NULL);
   vpi_test_cb(cbAfterDelay, at_27, 27, NULL);
   vpi_test_cb(cbAfterDelay, at_33, 33, NULL);
   return 0;
}

static PLI_INT32 on_end(p_cb_data cb)
{
   vpi_test_pass();
   return 0;
}

void vpi_island_cb1_startup(void)
{
   vpi_test_cb(cbStartOfSimulation, on_start, 0, NULL);
   vpi_test_cb(cbEndOfSimulation, on_end, 0, NULL);
}
