#include "vpi_test.h"

#include <string.h>

static int comb_xor_changes = 0;
static int ff_q_changes = 0;
static int last_comb_xor = -1;
static int last_ff_q = -1;

static int get_int(vpiHandle h)
{
   s_vpi_value val = { .format = vpiIntVal };
   vpi_get_value(h, &val);
   check_error();
   return val.value.integer;
}

static const char *get_str(vpiHandle h, PLI_INT32 fmt)
{
   s_vpi_value val = { .format = fmt };
   vpi_get_value(h, &val);
   check_error();
   return val.value.str;
}

static PLI_INT32 on_comb_xor_change(p_cb_data cb)
{
   comb_xor_changes++;
   last_comb_xor = cb->value->value.integer;
   return 0;
}

static PLI_INT32 on_ff_q_change(p_cb_data cb)
{
   ff_q_changes++;
   last_ff_q = cb->value->value.integer;
   return 0;
}

// t=10ns: read UNOBSERVED internal island signals before any cbValueChange
static PLI_INT32 at_10ns(p_cb_data cb)
{
   vpiHandle comb_sum = vpi_test_lookup("vpi_island1.u_dut.comb_sum");
   vpiHandle comb_xor = vpi_test_lookup("vpi_island1.u_dut.comb_xor");
   vpiHandle ff_q     = vpi_test_lookup("vpi_island1.u_dut.ff_q");
   vpiHandle ff_2s    = vpi_test_lookup("vpi_island1.u_dut.ff_2s");
   vpiHandle wide_q   = vpi_test_lookup("vpi_island1.u_dut.wide_q");

   check_int(get_int(comb_sum), 0x11);
   check_int(get_int(ff_q), 0x05);
   check_int(get_int(comb_xor), 0x14);
   check_int(get_int(ff_2s), 0x1234);
   check_string(get_str(wide_q, vpiHexStrVal), "0123456789abcdeffedcba98");
   check_string(get_str(comb_xor, vpiBinStrVal), "00010100");
   return 0;
}

// t=14ns: attach cbValueChange MID-SIMULATION (like Surfer live panel)
static PLI_INT32 at_14ns(p_cb_data cb)
{
   vpiHandle comb_xor = vpi_test_lookup("vpi_island1.u_dut.comb_xor");
   vpiHandle ff_q     = vpi_test_lookup("vpi_island1.u_dut.ff_q");

   // Verify live value at attach time after din changed to 0x20 at 12ns
   check_int(get_int(comb_xor), 0x05 ^ 0x30);

   static s_vpi_value v1 = { .format = vpiIntVal };
   s_cb_data cb1 = {
      .reason = cbValueChange,
      .cb_rtn = on_comb_xor_change,
      .obj    = comb_xor,
      .value  = &v1,
   };
   check_handle(vpi_register_cb(&cb1));

   static s_vpi_value v2 = { .format = vpiIntVal };
   s_cb_data cb2 = {
      .reason = cbValueChange,
      .cb_rtn = on_ff_q_change,
      .obj    = ff_q,
      .value  = &v2,
   };
   check_handle(vpi_register_cb(&cb2));
   return 0;
}

// t=18ns: verify callbacks fired at t=15ns posedge, then test vpiNoDelay put
static PLI_INT32 at_18ns(p_cb_data cb)
{
   vpiHandle ff_q = vpi_test_lookup("vpi_island1.u_dut.ff_q");
   vpiHandle comb_xor = vpi_test_lookup("vpi_island1.u_dut.comb_xor");
   vpiHandle dout = vpi_test_lookup("vpi_island1.dout");

   check_int(get_int(ff_q), 0x30);
   check_int(get_int(comb_xor), 0x00);
   fail_unless(ff_q_changes >= 1);
   fail_unless(comb_xor_changes >= 1);

   // Deposit vpiNoDelay onto ff_q -> must update ff_q AND propagate to comb_xor/dout
   s_vpi_value v = { .format = vpiIntVal, .value.integer = 0x55 };
   vpi_put_value(ff_q, &v, NULL, vpiNoDelay);
   check_error();
   check_int(get_int(ff_q), 0x55);
   (void)dout;
   return 0;
}

// t=20ns: check vpiNoDelay propagated through comb_xor & dout, then force ff_q and comb_sum
static PLI_INT32 at_20ns(p_cb_data cb)
{
   vpiHandle ff_q     = vpi_test_lookup("vpi_island1.u_dut.ff_q");
   vpiHandle comb_sum = vpi_test_lookup("vpi_island1.u_dut.comb_sum");
   vpiHandle comb_xor = vpi_test_lookup("vpi_island1.u_dut.comb_xor");
   vpiHandle dout     = vpi_test_lookup("vpi_island1.dout");

   check_int(get_int(comb_xor), 0x55 ^ 0x30);
   check_int(get_int(dout), 0x55 ^ 0x30);

   // Force ff_q = 0xa5 and comb_sum = 0x3c
   s_vpi_value vf = { .format = vpiIntVal, .value.integer = 0xa5 };
   vpi_put_value(ff_q, &vf, NULL, vpiForceFlag);
   check_error();

   vf.value.integer = 0x3c;
   vpi_put_value(comb_sum, &vf, NULL, vpiForceFlag);
   check_error();
   return 0;
}

// t=28ns: after 22ns din=0x03 and 25ns posedge, both forces must hold!
static PLI_INT32 at_28ns(p_cb_data cb)
{
   vpiHandle ff_q     = vpi_test_lookup("vpi_island1.u_dut.ff_q");
   vpiHandle comb_sum = vpi_test_lookup("vpi_island1.u_dut.comb_sum");
   vpiHandle comb_xor = vpi_test_lookup("vpi_island1.u_dut.comb_xor");
   vpiHandle dout     = vpi_test_lookup("vpi_island1.dout");

   check_int(get_int(ff_q), 0xa5);
   check_int(get_int(comb_sum), 0x3c);
   check_int(get_int(comb_xor), 0xa5 ^ 0x3c);
   check_int(get_int(dout), 0xa5 ^ 0x3c);

   // Release both: comb_sum immediately returns to din(0x03)+0x10 = 0x13;
   // ff_q keeps 0xa5 until next clock edge
   s_vpi_value vr = { .format = vpiIntVal };
   vpi_put_value(comb_sum, &vr, NULL, vpiReleaseFlag);
   check_error();
   check_int(vr.value.integer, 0x13);

   vpi_put_value(ff_q, &vr, NULL, vpiReleaseFlag);
   check_error();
   check_int(get_int(ff_q), 0xa5);
   return 0;
}

// t=38ns: after 32ns din=0x04 and 35ns posedge, ff_q must update to 0x14!
static PLI_INT32 at_38ns(p_cb_data cb)
{
   vpiHandle ff_q     = vpi_test_lookup("vpi_island1.u_dut.ff_q");
   vpiHandle comb_sum = vpi_test_lookup("vpi_island1.u_dut.comb_sum");
   vpiHandle comb_xor = vpi_test_lookup("vpi_island1.u_dut.comb_xor");
   vpiHandle wide_q   = vpi_test_lookup("vpi_island1.u_dut.wide_q");

   check_int(get_int(comb_sum), 0x14);
   check_int(get_int(ff_q), 0x14);
   check_int(get_int(comb_xor), 0x00);

   // Force wide_q with vpiVectorVal and verify
   s_vpi_vecval wvec[3] = {
      { .aval = 0xdeadbeef, .bval = 0 },
      { .aval = 0xcafebabe, .bval = 0 },
      { .aval = 0x11223344, .bval = 0 },
   };
   s_vpi_value wv = { .format = vpiVectorVal, .value.vector = wvec };
   vpi_put_value(wide_q, &wv, NULL, vpiForceFlag);
   check_error();
   check_string(get_str(wide_q, vpiHexStrVal), "11223344cafebabedeadbeef");
   return 0;
}

// t=48ns: wide_q must still hold forced value across 45ns clock, then release
static PLI_INT32 at_48ns(p_cb_data cb)
{
   vpiHandle wide_q = vpi_test_lookup("vpi_island1.u_dut.wide_q");
   check_string(get_str(wide_q, vpiHexStrVal), "11223344cafebabedeadbeef");

   s_vpi_value vr = { .format = vpiHexStrVal };
   vpi_put_value(wide_q, &vr, NULL, vpiReleaseFlag);
   check_error();
   return 0;
}

// t=58ns: after 55ns posedge with din=0x06, wide_q low byte must be 0x06
static PLI_INT32 at_58ns(p_cb_data cb)
{
   vpiHandle wide_q = vpi_test_lookup("vpi_island1.u_dut.wide_q");
   check_string(get_str(wide_q, vpiHexStrVal), "223344cafebabedeadbeef06");
   return 0;
}

static PLI_INT32 start_of_sim(p_cb_data cb)
{
   vpi_test_cb(cbAfterDelay, at_10ns, 10, NULL);
   vpi_test_cb(cbAfterDelay, at_14ns, 14, NULL);
   vpi_test_cb(cbAfterDelay, at_18ns, 18, NULL);
   vpi_test_cb(cbAfterDelay, at_20ns, 20, NULL);
   vpi_test_cb(cbAfterDelay, at_28ns, 28, NULL);
   vpi_test_cb(cbAfterDelay, at_38ns, 38, NULL);
   vpi_test_cb(cbAfterDelay, at_48ns, 48, NULL);
   vpi_test_cb(cbAfterDelay, at_58ns, 58, NULL);
   return 0;
}

static PLI_INT32 end_of_sim(p_cb_data cb)
{
   fail_unless(comb_xor_changes >= 5);
   fail_unless(ff_q_changes >= 4);
   fail_unless(last_comb_xor == 0x00);
   fail_unless(last_ff_q == 0x16);
   vpi_test_pass();
   return 0;
}

void vpi_island1_startup(void)
{
   vpi_test_cb(cbStartOfSimulation, start_of_sim, 0, NULL);
   vpi_test_cb(cbEndOfSimulation, end_of_sim, 0, NULL);
}
