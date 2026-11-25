#include "vpi_test.h"

static int get_int(vpiHandle h)
{
   s_vpi_value val = { .format = vpiIntVal };
   vpi_get_value(h, &val);
   check_error();
   return val.value.integer;
}

static void put_delay0(const char *name, int v)
{
   vpiHandle h = vpi_test_lookup(name);
   s_vpi_time t = { .type = vpiSimTime, .high = 0, .low = 0 };
   s_vpi_value val = { .format = vpiIntVal, .value.integer = v };
   vpi_put_value(h, &val, &t, vpiInertialDelay);
   check_error();
}

// t=0 ReadOnlySynch: Z->1 clk deposit from cbStartOfSimulation must NOT fire
// island always @(posedge clk) at 0ms+0, so decl-init q=0x5a must survive.
static PLI_INT32 at_0_ro(p_cb_data cb)
{
   check_int(get_int(vpi_test_lookup("vpi_island4.ready1")), 1);
   check_int(get_int(vpi_test_lookup("vpi_island4.ready2")), 1);
   check_int(get_int(vpi_test_lookup("vpi_island4.q1")), 0x5a);
   check_int(get_int(vpi_test_lookup("vpi_island4.q2")), 0x5a);
   return 0;
}

static PLI_INT32 at_5ns(p_cb_data cb)
{
   put_delay0("vpi_island4.clk", 0);
   put_delay0("vpi_island4.din", 0x10);
   return 0;
}

static PLI_INT32 at_10ns(p_cb_data cb)
{
   put_delay0("vpi_island4.clk", 1);
   return 0;
}

// t=12ns: real posedge at 10ns must sample ejected always @* output (0x10+4)
static PLI_INT32 at_12ns(p_cb_data cb)
{
   check_int(get_int(vpi_test_lookup("vpi_island4.ready1")), 1);
   check_int(get_int(vpi_test_lookup("vpi_island4.q1")), 0x14);
   check_int(get_int(vpi_test_lookup("vpi_island4.q2")), 0x14);
   put_delay0("vpi_island4.clk", 0);
   put_delay0("vpi_island4.din", 0x30);
   return 0;
}

static PLI_INT32 at_20ns(p_cb_data cb)
{
   put_delay0("vpi_island4.clk", 1);
   return 0;
}

static PLI_INT32 at_22ns(p_cb_data cb)
{
   check_int(get_int(vpi_test_lookup("vpi_island4.q1")), 0x34);
   check_int(get_int(vpi_test_lookup("vpi_island4.q2")), 0x34);
   vpi_control(vpiFinish, 0);
   return 0;
}

static PLI_INT32 start_of_sim(p_cb_data cb)
{
   // Continuous assign outputs driven from decl-init regs must already be
   // settled at cbStartOfSimulation (matching --rtl-islands=off).
   check_int(get_int(vpi_test_lookup("vpi_island4.ready1")), 1);
   check_int(get_int(vpi_test_lookup("vpi_island4.ready2")), 1);
   check_int(get_int(vpi_test_lookup("vpi_island4.q1")), 0x5a);
   check_int(get_int(vpi_test_lookup("vpi_island4.q2")), 0x5a);

   // Mirror cocotb Clock.start() + rst.setimmediatevalue(0) at t=0.
   put_delay0("vpi_island4.rst", 0);
   put_delay0("vpi_island4.clk", 1);

   vpi_test_cb(cbReadOnlySynch, at_0_ro, 0, NULL);
   vpi_test_cb(cbAfterDelay, at_5ns, 5, NULL);
   vpi_test_cb(cbAfterDelay, at_10ns, 10, NULL);
   vpi_test_cb(cbAfterDelay, at_12ns, 12, NULL);
   vpi_test_cb(cbAfterDelay, at_20ns, 20, NULL);
   vpi_test_cb(cbAfterDelay, at_22ns, 22, NULL);
   return 0;
}

static PLI_INT32 end_of_sim(p_cb_data cb)
{
   vpi_test_pass();
   return 0;
}

void vpi_island4_startup(void)
{
   vpi_test_cb(cbStartOfSimulation, start_of_sim, 0, NULL);
   vpi_test_cb(cbEndOfSimulation, end_of_sim, 0, NULL);
}
