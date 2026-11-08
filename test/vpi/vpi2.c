#include "vpi_test.h"

#include <string.h>

static bool ends_with(const char *s, const char *suffix)
{
   size_t ls = strlen(s), lx = strlen(suffix);
   return ls >= lx && strcmp(s + ls - lx, suffix) == 0;
}

// Find a child of a given type by name and check it is unique
static vpiHandle find_child(vpiHandle scope, PLI_INT32 type, const char *name)
{
   vpiHandle it = vpi_iterate(type, scope), found = NULL, h;
   check_error();
   fail_if(it == NULL);
   while (it != NULL && (h = vpi_scan(it))) {
      const char *n = vpi_get_str(vpiName, h);
      check_error();
      if (n != NULL && strcmp(n, name) == 0) {
         fail_unless(found == NULL);
         found = h;
      }
      else
         vpi_release_handle(h);
   }
   if (found == NULL)
      vpi_test_fail(__FILE__, __LINE__, "%s not found", name);
   return found;
}

static PLI_INT32 end_of_sim(p_cb_data cb)
{
   vpi_test_pass();
   return 0;
}

static PLI_INT32 start_of_sim(p_cb_data cb)
{
   // Exactly one top-level module
   vpiHandle it = vpi_iterate(vpiModule, NULL);
   check_handle(it);
   vpiHandle top = vpi_scan(it);
   check_handle(top);
   fail_unless(vpi_scan(it) == NULL);   // Iterator freed at end of scan

   check_int(vpi_get(vpiType, top), vpiModule);
   check_string(vpi_get_str(vpiName, top), "vpi2");
   check_string(vpi_get_str(vpiFullName, top), "vpi2");
   check_string(vpi_get_str(vpiDefName, top), "vpi2");
   check_int(vpi_get(vpiTopModule, top), 1);
   check_string(vpi_get_str(vpiType, top), "vpiModule");

   const char *file = vpi_get_str(vpiDefFile, top);
   fail_if(file == NULL);
   fail_unless(ends_with(file, "vpi2.sv"));

   vpiHandle top2 = vpi_handle_by_name("vpi2", NULL);
   check_handle(top2);
   fail_unless(vpi_compare_objects(top, top2));

   // Module instance
   vpiHandle sub = find_child(top, vpiModule, "u_sub");
   check_string(vpi_get_str(vpiFullName, sub), "vpi2.u_sub");
   check_string(vpi_get_str(vpiDefName, sub), "vpi2_sub");
   check_int(vpi_get(vpiTopModule, sub), 0);

   vpiHandle parent = vpi_handle(vpiModule, sub);
   check_handle(parent);
   fail_unless(vpi_compare_objects(parent, top));

   // Ports of the instance
   vpiHandle pa = find_child(sub, vpiPort, "a");
   check_int(vpi_get(vpiType, pa), vpiPort);
   check_int(vpi_get(vpiDirection, pa), vpiInput);
   check_int(vpi_get(vpiSize, pa), 8);
   vpiHandle py = find_child(sub, vpiPort, "y");
   check_int(vpi_get(vpiDirection, py), vpiOutput);

   // Net, variable and integer variable
   vpiHandle w = find_child(top, vpiNet, "w");
   check_int(vpi_get(vpiType, w), vpiNet);
   check_string(vpi_get_str(vpiFullName, w), "vpi2.w");
   check_int(vpi_get(vpiSize, w), 8);
   check_int(vpi_get(vpiVector, w), 1);

   vpiHandle r = find_child(top, vpiReg, "r");
   check_int(vpi_get(vpiType, r), vpiReg);
   check_int(vpi_get(vpiSize, r), 8);

   vpiHandle i = find_child(top, vpiVariables, "i");
   check_int(vpi_get(vpiType, i), vpiIntegerVar);
   check_int(vpi_get(vpiSize, i), 32);
   check_int(vpi_get(vpiSigned, i), 1);

   // Parameters and their values
   vpiHandle pw = find_child(top, vpiParameter, "WIDTH");
   check_int(vpi_get(vpiType, pw), vpiParameter);
   check_int(vpi_get(vpiLocalParam, pw), 0);
   s_vpi_value val = { .format = vpiIntVal };
   vpi_get_value(pw, &val);
   check_error();
   check_int(val.value.integer, 8);

   vpiHandle lp = find_child(top, vpiParameter, "LP");
   check_int(vpi_get(vpiLocalParam, lp), 1);
   val.format = vpiIntVal;
   vpi_get_value(lp, &val);
   check_int(val.value.integer, 16);

   // Overridden parameter of the instance
   vpiHandle subw = find_child(sub, vpiParameter, "W");
   val.format = vpiIntVal;
   vpi_get_value(subw, &val);
   check_int(val.value.integer, 8);

   // Loop generate: an array of three scopes
   vpiHandle gen = find_child(top, vpiGenScopeArray, "gen");
   check_int(vpi_get(vpiType, gen), vpiGenScopeArray);
   check_string(vpi_get_str(vpiFullName, gen), "vpi2.gen");

   static const char *gen_names[] = { "gen[0]", "gen[1]", "gen[2]" };
   vpiHandle git = vpi_iterate(vpiGenScope, gen), gs;
   check_handle(git);
   int ngen = 0;
   while ((gs = vpi_scan(git))) {
      fail_unless(ngen < 3);
      check_int(vpi_get(vpiType, gs), vpiGenScope);
      check_string(vpi_get_str(vpiName, gs), gen_names[ngen]);
      vpiHandle q = find_child(gs, vpiReg, "q");
      check_int(vpi_get(vpiSize, q), ngen + 1);
      ngen++;
   }
   check_int(ngen, 3);

   vpiHandle q1 = vpi_handle_by_name("vpi2.gen[1].q", NULL);
   check_handle(q1);
   check_string(vpi_get_str(vpiFullName, q1), "vpi2.gen[1].q");
   check_int(vpi_get(vpiSize, q1), 2);

   // Conditional generate block
   vpiHandle gif = vpi_handle_by_name("vpi2.g_if", NULL);
   check_handle(gif);
   check_int(vpi_get(vpiType, gif), vpiGenScope);
   vpiHandle inner = find_child(gif, vpiNet, "inner");
   check_string(vpi_get_str(vpiFullName, inner), "vpi2.g_if.inner");

   // Relative lookup
   vpiHandle w2 = vpi_handle_by_name("w", top);
   check_handle(w2);
   fail_unless(vpi_compare_objects(w, w2));
   vpiHandle a2 = vpi_handle_by_name("u_sub.a", top);
   check_handle(a2);
   check_string(vpi_get_str(vpiFullName, a2), "vpi2.u_sub.a");

   // Lookup of something that does not exist is not an error object
   fail_unless(vpi_handle_by_name("vpi2.nothere", NULL) == NULL);
   vpi_chk_error(NULL);

   fail_unless(vpi_release_handle(w2));
   fail_unless(vpi_release_handle(a2));

   return 0;
}

void vpi2_startup(void)
{
   vpi_test_cb(cbStartOfSimulation, start_of_sim, 0, NULL);
   vpi_test_cb(cbEndOfSimulation, end_of_sim, 0, NULL);
}
