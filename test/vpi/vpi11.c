#include "vpi_test.h"

#include <string.h>

static const char *get_str(vpiHandle h, PLI_INT32 format)
{
   s_vpi_value val = { .format = format };
   vpi_get_value(h, &val);
   check_error();
   return val.value.str;
}

static int get_int(vpiHandle h)
{
   s_vpi_value val = { .format = vpiIntVal };
   vpi_get_value(h, &val);
   check_error();
   return val.value.integer;
}

static PLI_INT32 at_10ns(p_cb_data cb)
{
   // Members of an unpacked structure
   vpiHandle rec = vpi_test_lookup("vpi11.rec");
   check_int(vpi_get(vpiType, rec), vpiStructVar);
   check_int(vpi_get(vpiPacked, rec), 0);

   static const char *names[] = { "lv", "bv", "r" };
   vpiHandle it = vpi_iterate(vpiMember, rec), m;
   check_handle(it);
   int n = 0;
   while ((m = vpi_scan(it))) {
      fail_unless(n < 3);
      check_string(vpi_get_str(vpiName, m), names[n]);
      n++;
   }
   check_int(n, 3);

   vpiHandle lv = vpi_test_lookup("vpi11.rec.lv");
   check_string(vpi_get_str(vpiFullName, lv), "vpi11.rec.lv");
   check_int(vpi_get(vpiSize, lv), 4);
   check_string(get_str(lv, vpiBinStrVal), "10x1");
   vpiHandle bv = vpi_test_lookup("vpi11.rec.bv");
   check_int(get_int(bv), 0x5a);
   vpiHandle r = vpi_test_lookup("vpi11.rec.r");
   check_int(vpi_get(vpiType, r), vpiRealVar);
   s_vpi_value val = { .format = vpiRealVal };
   vpi_get_value(r, &val);
   fail_unless(val.value.real == 0.5);

   val.format = vpiBinStrVal;
   val.value.str = "0110";
   vpi_put_value(lv, &val, NULL, vpiNoDelay);
   check_error();
   check_string(get_str(lv, vpiBinStrVal), "0110");
   val.format = vpiIntVal;
   val.value.integer = 0xc3;
   vpi_put_value(bv, &val, NULL, vpiNoDelay);
   check_error();
   val.format = vpiRealVal;
   val.value.real = 2.25;
   vpi_put_value(r, &val, NULL, vpiNoDelay);
   check_error();

   // Unknown bits written to a two-state variable are zero
   vpiHandle b2 = vpi_test_lookup("vpi11.b2");
   val.format = vpiBinStrVal;
   val.value.str = "xz";
   vpi_put_value(b2, &val, NULL, vpiNoDelay);
   check_error();
   check_string(get_str(b2, vpiBinStrVal), "00");

   // Named gate instance
   vpiHandle top = vpi_test_lookup("vpi11");
   vpiHandle g = vpi_test_lookup("vpi11.g_and");
   check_int(vpi_get(vpiType, g), vpiGate);
   it = vpi_iterate(vpiPrimitive, top);
   check_handle(it);
   vpiHandle g2 = vpi_scan(it);
   check_handle(g2);
   fail_unless(vpi_compare_objects(g, g2));
   fail_unless(vpi_scan(it) == NULL);

   // Range of an array of interface instances
   vpiHandle ifs = vpi_test_lookup("vpi11.ifs");
   check_int(vpi_get(vpiType, ifs), vpiInterfaceArray);
   check_int(vpi_get(vpiSize, ifs), 3);
   check_int(get_int(vpi_handle(vpiLeftRange, ifs)), 1);
   check_int(get_int(vpi_handle(vpiRightRange, ifs)), 3);
   it = vpi_iterate(vpiRange, ifs);
   check_handle(it);
   vpiHandle rng = vpi_scan(it);
   check_handle(rng);
   check_int(get_int(vpi_handle(vpiLeftRange, rng)), 1);
   vpi_release_handle(it);

   // Array of interface ports
   vpiHandle ref = vpi_test_lookup("vpi11.u_sub.p");
   check_int(vpi_get(vpiType, ref), vpiRefObj);
   vpiHandle actual = vpi_handle(vpiActual, ref);
   check_handle(actual);
   fail_unless(vpi_compare_objects(actual, ifs));
   vpiHandle p2 = vpi_test_lookup("vpi11.u_sub.p[2]");
   check_string(vpi_get_str(vpiFullName, p2), "vpi11.ifs[2]");

   // The compilation-unit scope
   vpiHandle unit = vpi_handle_by_name("$unit::vpi11_unit_param", NULL);
   check_handle(unit);
   check_int(get_int(unit), 4);

   // Unnamed generate block
   vpiHandle gen = vpi_test_lookup("vpi11.genblk1.in_gen");
   check_string(vpi_get_str(vpiFullName, gen), "vpi11.genblk1.in_gen");

   return 0;
}

static PLI_INT32 end_of_sim(p_cb_data cb)
{
   vpi_test_pass();
   return 0;
}

static PLI_INT32 start_of_sim(p_cb_data cb)
{
   vpi_test_cb(cbAfterDelay, at_10ns, 10, NULL);
   return 0;
}

void vpi11_startup(void)
{
   vpi_test_cb(cbStartOfSimulation, start_of_sim, 0, NULL);
   vpi_test_cb(cbEndOfSimulation, end_of_sim, 0, NULL);
}
