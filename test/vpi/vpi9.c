#include "vpi_test.h"

static int y_changes;

static PLI_INT32 y_change(p_cb_data cb)
{
   const PLI_UINT64 now = vpi_test_now();
   if (now == 0)
      return 0;    // Initial evaluation of the VHDL process

   y_changes++;
   check_int(now, 10);
   check_int(cb->value->format, vpiIntVal);
   check_int(cb->value->value.integer, 0x43);
   return 0;
}

static PLI_INT32 at_10ns(p_cb_data cb)
{
   vpiHandle a = vpi_test_lookup("vpi9.a");
   vpiHandle y = vpi_test_lookup("vpi9.y");

   s_vpi_value val = { .format = vpiIntVal };
   vpi_get_value(y, &val);
   check_int(val.value.integer, 0x01);

   val.format = vpiIntVal;
   val.value.integer = 0x42;
   vpi_put_value(a, &val, NULL, vpiNoDelay);
   check_error();
   return 0;
}

static PLI_INT32 end_of_sim(p_cb_data cb)
{
   check_int(y_changes, 1);

   s_vpi_value val = { .format = vpiHexStrVal };
   vpi_get_value(vpi_test_lookup("vpi9.y"), &val);
   check_string(val.value.str, "43");

   vpi_test_pass();
   return 0;
}

static PLI_INT32 start_of_sim(p_cb_data cb)
{
   vpiHandle it = vpi_iterate(vpiModule, NULL);
   check_handle(it);
   vpiHandle top = vpi_scan(it);
   check_handle(top);
   check_string(vpi_get_str(vpiName, top), "vpi9");
   fail_unless(vpi_scan(it) == NULL);

   // The VHDL instance is not a module: it is named but its contents are
   // only visible through VHPI
   fail_unless(vpi_iterate(vpiModule, top) == NULL);
   vpiHandle inc = vpi_handle_by_name("u_inc", top);
   check_handle(inc);
   check_int(vpi_get(vpiType, inc), vpiForeignInst);
   check_string(vpi_get_str(vpiFullName, inc), "vpi9.u_inc");
   check_string(vpi_get_str(vpiDefName, inc), "VPI9_INC");   // VHDL name
   fail_unless(vpi_iterate(vpiInternalScope, inc) == NULL);
   fail_unless(vpi_handle_by_name("vpi9.u_inc.y", NULL) == NULL);
   vpi_chk_error(NULL);

   vpiHandle it2 = vpi_iterate(vpiInternalScope, top);
   check_handle(it2);
   vpiHandle inc2 = vpi_scan(it2);
   fail_unless(vpi_compare_objects(inc, inc2));
   fail_unless(vpi_scan(it2) == NULL);

   vpiHandle y = vpi_handle_by_name("y", top);
   check_handle(y);
   check_int(vpi_get(vpiType, y), vpiNet);
   check_int(vpi_get(vpiSize, y), 8);

   s_vpi_time t = { .type = vpiSimTime };
   s_vpi_value v = { .format = vpiIntVal };
   s_cb_data cbd = {
      .reason = cbValueChange,
      .cb_rtn = y_change,
      .obj    = y,
      .time   = &t,
      .value  = &v,
   };
   check_handle(vpi_register_cb(&cbd));

   vpi_test_cb(cbAfterDelay, at_10ns, 10, NULL);
   return 0;
}

void vpi9_startup(void)
{
   vpi_test_cb(cbStartOfSimulation, start_of_sim, 0, NULL);
   vpi_test_cb(cbEndOfSimulation, end_of_sim, 0, NULL);
}
