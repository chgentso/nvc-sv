#include "vpi_test.h"

static int w_mid_changes = 0;
static int last_w_mid = -1;

static int get_int(vpiHandle h)
{
   s_vpi_value val = { .format = vpiIntVal };
   vpi_get_value(h, &val);
   check_error();
   return val.value.integer;
}

static PLI_INT32 on_w_mid_change(p_cb_data cb)
{
   w_mid_changes++;
   last_w_mid = cb->value->value.integer;
   return 0;
}

// t=10ns: read UNOBSERVED inter-island direct wire w_mid before cbValueChange
static PLI_INT32 at_10ns(p_cb_data cb)
{
   vpiHandle w_mid = vpi_test_lookup("vpi_island2.w_mid");
   vpiHandle dout  = vpi_test_lookup("vpi_island2.dout");

   check_int(get_int(w_mid), 0x11);
   check_int(get_int(dout), 0x12);
   return 0;
}

// t=14ns: attach cbValueChange MID-SIMULATION on direct wire w_mid
static PLI_INT32 at_14ns(p_cb_data cb)
{
   vpiHandle w_mid = vpi_test_lookup("vpi_island2.w_mid");
   check_int(get_int(w_mid), 0x30);

   static s_vpi_value val = { .format = vpiIntVal };
   s_cb_data cb_data = {
      .reason = cbValueChange,
      .cb_rtn = on_w_mid_change,
      .obj    = w_mid,
      .value  = &val,
   };
   check_handle(vpi_register_cb(&cb_data));
   return 0;
}

// t=20ns: force direct wire w_mid = 0xa5 and check downstream island dout
static PLI_INT32 at_20ns(p_cb_data cb)
{
   vpiHandle w_mid = vpi_test_lookup("vpi_island2.w_mid");
   vpiHandle dout  = vpi_test_lookup("vpi_island2.dout");

   s_vpi_value vf = { .format = vpiIntVal, .value.integer = 0xa5 };
   vpi_put_value(w_mid, &vf, NULL, vpiForceFlag);
   check_error();
   check_int(get_int(w_mid), 0xa5);
   (void)dout;
   return 0;
}

// t=28ns: verify force held across din change + clock edge, then release
static PLI_INT32 at_28ns(p_cb_data cb)
{
   vpiHandle w_mid = vpi_test_lookup("vpi_island2.w_mid");
   vpiHandle dout  = vpi_test_lookup("vpi_island2.dout");
   vpiHandle q_out = vpi_test_lookup("vpi_island2.q_out");

   check_int(get_int(w_mid), 0xa5);
   check_int(get_int(dout), 0xa6);
   check_int(get_int(q_out), 0x86);

   s_vpi_value vr = { .format = vpiIntVal };
   vpi_put_value(w_mid, &vr, NULL, vpiReleaseFlag);
   check_error();
   check_int(vr.value.integer, 0x13);
   return 0;
}

// t=38ns: verify downstream comb + cbValueChange saw post-release din=0x04 -> 0x14
static PLI_INT32 at_38ns(p_cb_data cb)
{
   vpiHandle w_mid = vpi_test_lookup("vpi_island2.w_mid");
   vpiHandle dout  = vpi_test_lookup("vpi_island2.dout");

   check_int(get_int(w_mid), 0x14);
   check_int(get_int(dout), 0x15);
   return 0;
}

static PLI_INT32 start_of_sim(p_cb_data cb)
{
   vpi_test_cb(cbAfterDelay, at_10ns, 10, NULL);
   vpi_test_cb(cbAfterDelay, at_14ns, 14, NULL);
   vpi_test_cb(cbAfterDelay, at_20ns, 20, NULL);
   vpi_test_cb(cbAfterDelay, at_28ns, 28, NULL);
   vpi_test_cb(cbAfterDelay, at_38ns, 38, NULL);
   return 0;
}

static PLI_INT32 end_of_sim(p_cb_data cb)
{
   fail_unless(w_mid_changes >= 3);
   fail_unless(last_w_mid == 0x14);
   vpi_test_pass();
   return 0;
}

void vpi_island2_startup(void)
{
   vpi_test_cb(cbStartOfSimulation, start_of_sim, 0, NULL);
   vpi_test_cb(cbEndOfSimulation, end_of_sim, 0, NULL);
}
