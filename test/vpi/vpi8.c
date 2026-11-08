#include "vpi_test.h"

#include <string.h>

static vpiHandle find_child(vpiHandle scope, PLI_INT32 type, const char *name)
{
   vpiHandle it = vpi_iterate(type, scope), found = NULL, h;
   check_error();
   while (it != NULL && (h = vpi_scan(it))) {
      const char *n = vpi_get_str(vpiName, h);
      if (n != NULL && strcmp(n, name) == 0)
         found = h;
   }
   if (found == NULL)
      vpi_test_fail(__FILE__, __LINE__, "%s not found", name);
   return found;
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
   vpiHandle top = vpi_test_lookup("vpi8");

   // Interface instance
   vpiHandle bus = find_child(top, vpiInterface, "bus");
   check_int(vpi_get(vpiType, bus), vpiInterface);
   check_string(vpi_get_str(vpiDefName, bus), "vpi8_if");
   check_string(vpi_get_str(vpiFullName, bus), "vpi8.bus");

   vpiHandle data = find_child(bus, vpiVariables, "data");
   check_int(vpi_get(vpiType, data), vpiLogicVar);
   check_string(vpi_get_str(vpiFullName, data), "vpi8.bus.data");
   check_int(vpi_get(vpiSize, data), 8);

   vpiHandle mp = find_child(bus, vpiModport, "mp");
   check_int(vpi_get(vpiType, mp), vpiModport);

   // Array of interface instances
   vpiHandle arr = find_child(top, vpiInterfaceArray, "arr");
   check_int(vpi_get(vpiType, arr), vpiInterfaceArray);
   check_int(vpi_get(vpiSize, arr), 2);
   vpiHandle arr1 = vpi_handle_by_index(arr, 1);
   check_handle(arr1);
   check_int(vpi_get(vpiType, arr1), vpiInterface);
   check_string(vpi_get_str(vpiFullName, arr1), "vpi8.arr[1]");
   check_int(get_int(vpi_test_lookup("vpi8.arr[1].data")), 0x9e);

   // Interface port of a module
   vpiHandle sub = find_child(top, vpiModule, "u_sub");
   vpiHandle b = vpi_handle_by_name("b", sub);
   check_handle(b);
   check_int(vpi_get(vpiType, b), vpiRefObj);
   vpiHandle actual = vpi_handle(vpiActual, b);
   check_handle(actual);
   fail_unless(vpi_compare_objects(actual, bus));

   // Write through the interface; the module sees it through the port
   s_vpi_value val = { .format = vpiIntVal, .value.integer = 0x6d };
   vpi_put_value(data, &val, NULL, vpiNoDelay);
   check_error();

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

void vpi8_startup(void)
{
   vpi_test_cb(cbStartOfSimulation, start_of_sim, 0, NULL);
   vpi_test_cb(cbEndOfSimulation, end_of_sim, 0, NULL);
}
