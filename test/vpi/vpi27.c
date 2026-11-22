#include "vpi_test.h"

#include <stdio.h>
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
   for (int i = 0; i < 4; i++) {
      char name[64];
      snprintf(name, sizeof(name), "vpi27.g[%d]", i);

      vpiHandle scope = vpi_handle_by_name(name, NULL);
      check_handle(scope);
      check_int(vpi_get(vpiType, scope), vpiGenScope);
      check_string(vpi_get_str(vpiFullName, scope), name);

      vpiHandle pa = find_child(scope, vpiParameter, "Pa");
      check_int(get_int(pa), 3 * i + 1);

      snprintf(name, sizeof(name), "vpi27.g[%d].t", i);
      vpiHandle t = vpi_handle_by_name(name, NULL);
      check_handle(t);
      check_int(get_int(t), (i == 0 || i == 3));
   }

   // Force a signal of one iteration
   vpiHandle t2 = vpi_handle_by_name("vpi27.g[2].t", NULL);
   check_handle(t2);
   s_vpi_value val = { .format = vpiIntVal, .value.integer = 1 };
   vpi_put_value(t2, &val, NULL, vpiForceFlag);
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

void vpi27_startup(void)
{
   vpi_test_cb(cbStartOfSimulation, start_of_sim, 0, NULL);
   vpi_test_cb(cbEndOfSimulation, end_of_sim, 0, NULL);
}
