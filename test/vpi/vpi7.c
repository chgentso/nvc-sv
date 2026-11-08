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
   // Packages are top-level instances
   vpiHandle pkg = NULL;
   bool found_top = false;
   vpiHandle it = vpi_iterate(vpiInstance, NULL), h;
   check_handle(it);
   while ((h = vpi_scan(it))) {
      const char *name = vpi_get_str(vpiName, h);
      check_error();
      if (strcmp(name, "vpi7_pkg") == 0) {
         check_int(vpi_get(vpiType, h), vpiPackage);
         fail_unless(pkg == NULL);
         pkg = h;
      }
      else if (strcmp(name, "vpi7") == 0) {
         check_int(vpi_get(vpiType, h), vpiModule);
         found_top = true;
      }
   }
   fail_unless(found_top);
   check_handle(pkg);

   // The full name of a package may have a trailing scope operator
   const char *fullname = vpi_get_str(vpiFullName, pkg);
   fail_unless(strcmp(fullname, "vpi7_pkg::") == 0
               || strcmp(fullname, "vpi7_pkg") == 0);

   // Package parameters
   vpiHandle p_int = find_child(pkg, vpiParameter, "P_INT");
   check_int(get_int(p_int), 5);
   check_string(vpi_get_str(vpiFullName, p_int), "vpi7_pkg::P_INT");

   vpiHandle p_real = find_child(pkg, vpiParameter, "P_REAL");
   s_vpi_value val = { .format = vpiRealVal };
   vpi_get_value(p_real, &val);
   check_error();
   fail_unless(val.value.real == 1.5);

   vpiHandle p_str = find_child(pkg, vpiParameter, "P_STR");
   val.format = vpiStringVal;
   vpi_get_value(p_str, &val);
   check_error();
   check_string(val.value.str, "abc");

   vpiHandle p_vec = find_child(pkg, vpiParameter, "P_VEC");
   check_int(vpi_get(vpiLocalParam, p_vec), 1);
   check_int(vpi_get(vpiSize, p_vec), 12);
   val.format = vpiHexStrVal;
   vpi_get_value(p_vec, &val);
   check_string(val.value.str, "a5c");

   // Lookup by name with the package scope operator
   vpiHandle p_int2 = vpi_handle_by_name("vpi7_pkg::P_INT", NULL);
   check_handle(p_int2);
   fail_unless(vpi_compare_objects(p_int, p_int2));

   // Package variable
   vpiHandle pv = vpi_handle_by_name("vpi7_pkg::pkg_var", NULL);
   check_handle(pv);
   check_int(get_int(pv), 0x33);
   val.format = vpiIntVal;
   val.value.integer = 0x44;
   vpi_put_value(pv, &val, NULL, vpiNoDelay);
   check_error();

   // Overridden and default module parameters
   vpiHandle top = vpi_test_lookup("vpi7");
   vpiHandle u1 = find_child(top, vpiModule, "u1");
   vpiHandle u2 = find_child(top, vpiModule, "u2");
   check_int(get_int(find_child(u1, vpiParameter, "N")), 15);
   check_int(get_int(find_child(u1, vpiParameter, "M")), 11);
   check_int(get_int(find_child(u2, vpiParameter, "N")), 1);
   check_int(get_int(vpi_test_lookup("vpi7.u2.M")), 0);

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

void vpi7_startup(void)
{
   vpi_test_cb(cbStartOfSimulation, start_of_sim, 0, NULL);
   vpi_test_cb(cbEndOfSimulation, end_of_sim, 0, NULL);
}
