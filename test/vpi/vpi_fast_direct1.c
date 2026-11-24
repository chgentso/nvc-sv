#include "vpi_test.h"

static int fast_changes = 0;

static PLI_INT32 on_fast_change(p_cb_data cb)
{
   fast_changes++;
   return 0;
}

static PLI_INT32 at_10ns(p_cb_data cb)
{
   vpiHandle h = vpi_test_lookup("vpi_fast_direct1.sig_fast");
   vpiHandle b = vpi_test_lookup("vpi_fast_direct1.sig_b32");

   s_vpi_value val = { .format = vpiIntVal };
   vpi_get_value(h, &val);
   check_error();
   check_int(val.value.integer, 0x5a);

   vpi_get_value(b, &val);
   check_error();
   check_int(val.value.integer, 0x12345678);

   // Dynamically attach cbValueChange after fast-direct writes have run
   s_cb_data cb1 = {
      .reason = cbValueChange,
      .cb_rtn = on_fast_change,
      .obj    = h,
   };
   check_handle(vpi_register_cb(&cb1));
   return 0;
}

static PLI_INT32 at_24ns(p_cb_data cb)
{
   check_int(fast_changes, 1);

   vpiHandle h = vpi_test_lookup("vpi_fast_direct1.sig_fast");
   s_vpi_value v = { .format = vpiIntVal, .value.integer = 0x3c };
   vpi_put_value(h, &v, NULL, vpiForceFlag);
   check_error();
   return 0;
}

static PLI_INT32 at_30ns(p_cb_data cb)
{
   vpiHandle h = vpi_test_lookup("vpi_fast_direct1.sig_fast");
   s_vpi_value v = { .format = vpiIntVal };
   vpi_put_value(h, &v, NULL, vpiReleaseFlag);
   check_error();
   return 0;
}

static PLI_INT32 start_of_sim(p_cb_data cb)
{
   vpi_test_cb(cbAfterDelay, at_10ns, 10, NULL);
   vpi_test_cb(cbAfterDelay, at_24ns, 24, NULL);
   vpi_test_cb(cbAfterDelay, at_30ns, 30, NULL);
   return 0;
}

static PLI_INT32 end_of_sim(p_cb_data cb)
{
   fail_unless(fast_changes == 3);
   vpi_test_pass();
   return 0;
}

void vpi_fast_direct1_startup(void)
{
   vpi_test_cb(cbStartOfSimulation, start_of_sim, 0, NULL);
   vpi_test_cb(cbEndOfSimulation, end_of_sim, 0, NULL);
}
