#include "vpicls_util.h"

// The component tree of the UVM demo: uvm_top is a constant of the uvm_pkg
// package.  Not part of the regression suite as it needs the UVM sources,
// run it with vpi/uvm-demo.sh.

static int nkids(vpiHandle obj, char *names, size_t len)
{
   vpiHandle kids = vpicls_var(obj, "m_children", 0);
   check_int(vpi_get(vpiArrayType, kids), vpiAssocArray);

   names[0] = '\0';
   const int n = vpi_get(vpiSize, kids);
   vpiHandle it = vpi_iterate(vpiVarSelect, kids);
   vpiHandle k;
   while (it != NULL && (k = vpi_scan(it)) != NULL) {
      vpiHandle ix = check_handle(vpi_handle(vpiIndex, k));
      strncat(names, names[0] ? " " : "", len - strlen(names) - 1);
      strncat(names, vpicls_str(ix, vpiStringVal), len - strlen(names) - 1);
      vpi_release_handle(ix);
      vpi_release_handle(k);
   }

   return n;
}

static vpiHandle child(vpiHandle obj, const char *name)
{
   vpiHandle kids = vpicls_var(obj, "m_children", 0);
   vpiHandle it = vpi_iterate(vpiVarSelect, kids);
   vpiHandle k;
   while (it != NULL && (k = vpi_scan(it)) != NULL) {
      vpiHandle ix = check_handle(vpi_handle(vpiIndex, k));
      const bool match = strcmp(vpicls_str(ix, vpiStringVal), name) == 0;
      vpi_release_handle(ix);
      if (match) {
         vpi_release_handle(it);
         return k;
      }
      vpi_release_handle(k);
   }

   vpi_test_fail(__FILE__, __LINE__, "no child %s", name);
   return NULL;
}

static PLI_INT32 at_100ns(p_cb_data cb)
{
   char names[256];

   vpiHandle pkg = vpi_test_lookup("uvm_pkg");
   vpiHandle top = vpicls_var(pkg, "uvm_top", 0);
   check_int(vpi_get(vpiType, top), vpiClassVar);
   check_string(vpi_nvc_dynamic_type(top), "uvm_root");
   fail_unless(vpicls_oid(top) > 0);

   vpiHandle decl = check_handle(vpi_handle(vpiClassTypespec, top));
   check_string(vpi_get_str(vpiFullName, decl), "uvm_pkg::uvm_root");
   const char *chain[] = { "uvm_component", "uvm_report_object", "uvm_object",
                           "uvm_void" };
   vpiHandle ts = decl;
   for (int i = 0; i < 4; i++) {
      ts = check_handle(vpi_handle(vpiExtends, ts));
      check_string(vpi_get_str(vpiName, ts), chain[i]);
   }

   // The bulk call has the 49 properties of the root and the statics
   vpicls_members_t ms;
   fail_unless(vpicls_members(top, &ms) >= 40);
   int found = 0;
   for (int i = 0; i < ms.n; i++) {
      if (strcmp(ms.item[i].name, "m_children") == 0) {
         check_string(ms.item[i].type_name, "uvm_component[string]");
         check_string(ms.item[i].owner, "uvm_component");
         check_int(ms.item[i].kind, vpiNvcMemAssoc);
         found++;
      }
   }
   check_int(found, 1);
   vpicls_release_members(&ms);

   vpiHandle obj = check_handle(vpi_handle(vpiClassObj, top));
   check_int(nkids(obj, names, sizeof(names)), 1);
   check_string(names, "uvm_test_top");

   vpiHandle test = child(obj, "uvm_test_top");
   check_string(vpi_nvc_dynamic_type(test), "demo_test");
   vpiHandle test_obj = check_handle(vpi_handle(vpiClassObj, test));
   check_string(vpicls_str(vpicls_var(test_obj, "m_name", 0), vpiStringVal),
                "uvm_test_top");
   check_int(vpicls_oid(vpicls_var(test_obj, "m_parent", 0)),
             vpicls_oid(top));

   check_int(nkids(test_obj, names, sizeof(names)), 1);
   check_string(names, "env");
   vpiHandle env = child(test_obj, "env");
   check_string(vpi_nvc_dynamic_type(env), "alu_env");
   vpiHandle env_obj = check_handle(vpi_handle(vpiClassObj, env));
   check_int(nkids(env_obj, names, sizeof(names)), 5);
   check_string(names, "cov drv mon sb sqr");
   check_string(vpi_nvc_dynamic_type(child(env_obj, "drv")), "alu_driver");
   check_string(vpi_nvc_dynamic_type(child(env_obj, "mon")), "alu_monitor");
   check_string(vpi_nvc_dynamic_type(child(env_obj, "sb")), "alu_scoreboard");

   vpi_printf("PASSED\n");
   return 0;
}

static PLI_INT32 start_of_sim(p_cb_data cb)
{
   vpi_test_cb(cbAfterDelay, at_100ns, 100, NULL);
   return 0;
}

void vpicls5_startup(void)
{
   vpi_test_cb(cbStartOfSimulation, start_of_sim, 0, NULL);
}
