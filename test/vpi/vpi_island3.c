#include "vpi_test.h"

static int get_int(vpiHandle h)
{
   s_vpi_value val = { .format = vpiIntVal };
   vpi_get_value(h, &val);
   check_error();
   return val.value.integer;
}

// t=2ns: scheduled inertial deposit into island input din (delay 1ns -> t=3ns)
static PLI_INT32 at_2ns(p_cb_data cb)
{
   vpiHandle din = vpi_test_lookup("vpi_island3.din");
   s_vpi_time t = { .type = vpiSimTime, .high = 0, .low = 1 };
   s_vpi_value v = { .format = vpiIntVal, .value.integer = 0x3a };
   vpi_put_value(din, &v, &t, vpiInertialDelay);
   check_error();
   return 0;
}

// t=7ns: after posedge at 5ns, island comb y and FF q must reflect din=0x3a
static PLI_INT32 at_7ns(p_cb_data cb)
{
   vpiHandle din = vpi_test_lookup("vpi_island3.din");
   vpiHandle y   = vpi_test_lookup("vpi_island3.y");
   vpiHandle q   = vpi_test_lookup("vpi_island3.q");

   check_int(get_int(din), 0x3a);
   check_int(get_int(y), 0x3a ^ 0x55);
   check_int(get_int(q), 0x3a ^ 0x55);

   // Force din = 0xf0, then issue a scheduled inertial deposit din = 0x12
   // (at t=9ns); the active force must hold until released.
   s_vpi_value vf = { .format = vpiIntVal, .value.integer = 0xf0 };
   vpi_put_value(din, &vf, NULL, vpiForceFlag);
   check_error();

   s_vpi_time t = { .type = vpiSimTime, .high = 0, .low = 2 };
   s_vpi_value vd = { .format = vpiIntVal, .value.integer = 0x12 };
   vpi_put_value(din, &vd, &t, vpiInertialDelay);
   check_error();
   return 0;
}

// t=17ns: after posedge at 15ns, forced value 0xf0 must still hold over deposit
static PLI_INT32 at_17ns(p_cb_data cb)
{
   vpiHandle din = vpi_test_lookup("vpi_island3.din");
   vpiHandle y   = vpi_test_lookup("vpi_island3.y");
   vpiHandle q   = vpi_test_lookup("vpi_island3.q");

   check_int(get_int(din), 0xf0);
   check_int(get_int(y), 0xf0 ^ 0x55);
   check_int(get_int(q), 0xf0 ^ 0x55);

   // Release force, then schedule a new inertial deposit din = 0x24
   s_vpi_value vr = { .format = vpiIntVal };
   vpi_put_value(din, &vr, NULL, vpiReleaseFlag);
   check_error();

   s_vpi_time t = { .type = vpiSimTime, .high = 0, .low = 1 };
   s_vpi_value vd = { .format = vpiIntVal, .value.integer = 0x24 };
   vpi_put_value(din, &vd, &t, vpiInertialDelay);
   check_error();
   return 0;
}

// t=27ns: after posedge at 25ns, released signal must take new deposit 0x24
static PLI_INT32 at_27ns(p_cb_data cb)
{
   vpiHandle din = vpi_test_lookup("vpi_island3.din");
   vpiHandle y   = vpi_test_lookup("vpi_island3.y");
   vpiHandle q   = vpi_test_lookup("vpi_island3.q");

   check_int(get_int(din), 0x24);
   check_int(get_int(y), 0x24 ^ 0x55);
   check_int(get_int(q), 0x24 ^ 0x55);
   return 0;
}

static PLI_INT32 start_of_sim(p_cb_data cb)
{
   vpi_test_cb(cbAfterDelay, at_2ns, 2, NULL);
   vpi_test_cb(cbAfterDelay, at_7ns, 7, NULL);
   vpi_test_cb(cbAfterDelay, at_17ns, 17, NULL);
   vpi_test_cb(cbAfterDelay, at_27ns, 27, NULL);
   return 0;
}

static PLI_INT32 end_of_sim(p_cb_data cb)
{
   vpi_test_pass();
   return 0;
}

void vpi_island3_startup(void)
{
   vpi_test_cb(cbStartOfSimulation, start_of_sim, 0, NULL);
   vpi_test_cb(cbEndOfSimulation, end_of_sim, 0, NULL);
}
