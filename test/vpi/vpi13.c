#include "vpi_test.h"
#include "sv_vpi_user.h"

static int get_int(vpiHandle h)
{
   s_vpi_value val = { .format = vpiIntVal };
   vpi_get_value(h, &val);
   check_error();
   return val.value.integer;
}

static void check_enum(const char *path, int expect_size, const char *names[],
                       const int values[], int count)
{
   vpiHandle var = vpi_test_lookup(path);
   check_int(vpi_get(vpiType, var), vpiEnumVar);
   check_int(vpi_get(vpiSize, var), expect_size);

   vpiHandle ts = vpi_handle(vpiTypespec, var);
   check_handle(ts);
   check_int(vpi_get(vpiType, ts), vpiEnumTypespec);

   vpiHandle it = vpi_iterate(vpiEnumConst, ts);
   check_handle(it);
   for (int i = 0; i < count; i++) {
      vpiHandle c = vpi_scan(it);
      check_handle(c);
      check_int(vpi_get(vpiType, c), vpiEnumConst);
      check_string(vpi_get_str(vpiName, c), names[i]);
      check_int(get_int(c), values[i]);
   }
   if (vpi_scan(it) != NULL)
      vpi_test_fail(__FILE__, __LINE__, "too many enum constants for %s", path);
}

static PLI_INT32 at_1ns(p_cb_data cb)
{
   const char *state_names[] = { "IDLE", "RUN", "DONE" };
   const int state_values[] = { 0, 2, 3 };
   check_enum("vpi13.state", 2, state_names, state_values, 3);
   check_int(get_int(vpi_test_lookup("vpi13.state")), 2);

   const char *colour_names[] = { "RED", "GREEN", "BLUE" };
   const int colour_values[] = { 0, 1, 2 };
   check_enum("vpi13.colour", 32, colour_names, colour_values, 3);
   check_int(get_int(vpi_test_lookup("vpi13.colour")), 2);

   // Only enumeration variables have an enumeration type
   fail_if(vpi_handle(vpiTypespec, vpi_test_lookup("vpi13.plain")) != NULL);
   return 0;
}

static PLI_INT32 end_of_sim(p_cb_data cb)
{
   vpi_test_pass();
   return 0;
}

static PLI_INT32 start_of_sim(p_cb_data cb)
{
   vpi_test_cb(cbAfterDelay, at_1ns, 1, NULL);
   return 0;
}

void vpi13_startup(void)
{
   vpi_test_cb(cbStartOfSimulation, start_of_sim, 0, NULL);
   vpi_test_cb(cbEndOfSimulation, end_of_sim, 0, NULL);
}
