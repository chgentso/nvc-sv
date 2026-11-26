#include "vpi_test.h"

#include <string.h>

static int comb_changes = 0;
static int ff_changes   = 0;
static int last_comb    = -1;
static int last_ff      = -1;

static int get_int(vpiHandle h)
{
   s_vpi_value val = { .format = vpiIntVal };
   vpi_get_value(h, &val);
   check_error();
   return val.value.integer;
}

static PLI_INT32 on_comb_change(p_cb_data cb)
{
   comb_changes++;
   last_comb = cb->value->value.integer;
   return 0;
}

static PLI_INT32 on_ff_change(p_cb_data cb)
{
   ff_changes++;
   last_ff = cb->value->value.integer;
   return 0;
}

// t=10ns: read still-pruned outputs before any cbValueChange or put
static PLI_INT32 at_10ns(p_cb_data cb)
{
   vpiHandle p_comb = vpi_test_lookup("vpi_island5.u1.pruned_comb");
   vpiHandle p_ff   = vpi_test_lookup("vpi_island5.u1.pruned_ff");
   vpiHandle u2_ff  = vpi_test_lookup("vpi_island5.u2.pruned_ff");

   check_int(get_int(p_comb), 0x01 ^ 0x3c);
   check_int(get_int(p_ff),   0x20);
   check_int(get_int(u2_ff),  0x20);
   return 0;
}

// t=14ns: attach cbValueChange mid-simulation on u1's pruned outputs (u2 stays pruned!)
static PLI_INT32 at_14ns(p_cb_data cb)
{
   vpiHandle p_comb = vpi_test_lookup("vpi_island5.u1.pruned_comb");
   vpiHandle p_ff   = vpi_test_lookup("vpi_island5.u1.pruned_ff");

   // din changed to 0x20 at 12ns
   check_int(get_int(p_comb), 0x20 ^ 0x3c);
   check_int(get_int(p_ff),   0x20);

   static s_vpi_value v1 = { .format = vpiIntVal };
   s_cb_data cb1 = {
      .reason = cbValueChange,
      .cb_rtn = on_comb_change,
      .obj    = p_comb,
      .value  = &v1,
   };
   check_handle(vpi_register_cb(&cb1));

   static s_vpi_value v2 = { .format = vpiIntVal };
   s_cb_data cb2 = {
      .reason = cbValueChange,
      .cb_rtn = on_ff_change,
      .obj    = p_ff,
      .value  = &v2,
   };
   check_handle(vpi_register_cb(&cb2));
   return 0;
}

// t=18ns: verify posedge at 15ns fired cbValueChange on u1.pruned_ff,
// then test vpiNoDelay deposit on u1.pruned_ff and read u2's still-pruned outputs
static PLI_INT32 at_18ns(p_cb_data cb)
{
   vpiHandle p_ff   = vpi_test_lookup("vpi_island5.u1.pruned_ff");
   vpiHandle obs1   = vpi_test_lookup("vpi_island5.obs1");
   vpiHandle u2_ff  = vpi_test_lookup("vpi_island5.u2.pruned_ff");

   check_int(get_int(p_ff),  0x31);
   check_int(last_ff,        0x31);
   fail_unless(ff_changes >= 1);
   check_int(get_int(obs1),  0x25 + 0x20);
   check_int(get_int(u2_ff), 0x31);

   s_vpi_value vd = { .format = vpiIntVal, .value.integer = 0x66 };
   vpi_put_value(p_ff, &vd, NULL, vpiNoDelay);
   check_error();
   check_int(get_int(p_ff), 0x66);
   return 0;
}

// t=20ns: force u1.pruned_comb and u1.pruned_ff before din changes to 0x40 at 22ns
static PLI_INT32 at_20ns(p_cb_data cb)
{
   vpiHandle p_comb = vpi_test_lookup("vpi_island5.u1.pruned_comb");
   vpiHandle p_ff   = vpi_test_lookup("vpi_island5.u1.pruned_ff");

   s_vpi_value vf = { .format = vpiIntVal, .value.integer = 0xaa };
   vpi_put_value(p_comb, &vf, NULL, vpiForceFlag);
   check_error();

   vf.value.integer = 0xbb;
   vpi_put_value(p_ff, &vf, NULL, vpiForceFlag);
   check_error();
   return 0;
}

// t=21ns: verify both forces took effect (after delta cycle)
static PLI_INT32 at_21ns(p_cb_data cb)
{
   vpiHandle p_comb = vpi_test_lookup("vpi_island5.u1.pruned_comb");
   vpiHandle p_ff   = vpi_test_lookup("vpi_island5.u1.pruned_ff");

   check_int(get_int(p_comb), 0xaa);
   check_int(get_int(p_ff),   0xbb);
   return 0;
}

// t=28ns: after 22ns din=0x40 and 25ns posedge, forces must hold while obs1 updated
static PLI_INT32 at_28ns(p_cb_data cb)
{
   vpiHandle p_comb = vpi_test_lookup("vpi_island5.u1.pruned_comb");
   vpiHandle p_ff   = vpi_test_lookup("vpi_island5.u1.pruned_ff");
   vpiHandle obs1   = vpi_test_lookup("vpi_island5.obs1");
   vpiHandle u2_c   = vpi_test_lookup("vpi_island5.u2.pruned_comb");
   vpiHandle u2_f   = vpi_test_lookup("vpi_island5.u2.pruned_ff");

   check_int(get_int(p_comb), 0xaa);
   check_int(get_int(p_ff),   0xbb);
   check_int(get_int(obs1),   0x45 + 0x40);
   check_int(get_int(u2_c),   0x40 ^ 0x3c);
   check_int(get_int(u2_f),   0x51);

   s_vpi_value vr = { .format = vpiIntVal };
   vpi_put_value(p_comb, &vr, NULL, vpiReleaseFlag);
   check_error();
   check_int(get_int(p_comb), 0x40 ^ 0x3c);

   vpi_put_value(p_ff, &vr, NULL, vpiReleaseFlag);
   check_error();
   check_int(get_int(p_ff), 0xbb);
   return 0;
}

// t=38ns: after 32ns din=0x20 and 35ns posedge, u1.pruned_comb, u1.pruned_ff, and obs1
// must all have updated back to din=0x20 values (verifying LAST_OUT_VALID invalidation!)
static PLI_INT32 at_38ns(p_cb_data cb)
{
   vpiHandle p_comb = vpi_test_lookup("vpi_island5.u1.pruned_comb");
   vpiHandle p_ff   = vpi_test_lookup("vpi_island5.u1.pruned_ff");
   vpiHandle obs1   = vpi_test_lookup("vpi_island5.obs1");

   check_int(get_int(p_comb), 0x20 ^ 0x3c);
   check_int(get_int(p_ff),   0x31);
   check_int(get_int(obs1),   0x25 + 0x20);
   fail_unless(comb_changes >= 2);
   return 0;
}

static PLI_INT32 on_start(p_cb_data cb)
{
   vpi_test_cb(cbAfterDelay, at_10ns, 10, NULL);
   vpi_test_cb(cbAfterDelay, at_14ns, 14, NULL);
   vpi_test_cb(cbAfterDelay, at_18ns, 18, NULL);
   vpi_test_cb(cbAfterDelay, at_20ns, 20, NULL);
   vpi_test_cb(cbAfterDelay, at_21ns, 21, NULL);
   vpi_test_cb(cbAfterDelay, at_28ns, 28, NULL);
   vpi_test_cb(cbAfterDelay, at_38ns, 38, NULL);
   return 0;
}

static PLI_INT32 on_end(p_cb_data cb)
{
   vpi_test_pass();
   return 0;
}

void vpi_island5_startup(void)
{
   vpi_test_cb(cbStartOfSimulation, on_start, 0, NULL);
   vpi_test_cb(cbEndOfSimulation, on_end, 0, NULL);
}
