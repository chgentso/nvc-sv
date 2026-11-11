#include "vpi_test.h"

#include <stdio.h>

static int read_int(vpiHandle h)
{
   s_vpi_value val = { .format = vpiIntVal };
   vpi_get_value(h, &val);
   check_error();
   return val.value.integer;
}

static void check_values(const char *sub, const char *leaf, int t, int q,
                         int n)
{
   char name[64];
   snprintf(name, sizeof(name), "%s.t", sub);
   check_int(read_int(vpi_test_lookup(name)), t);
   snprintf(name, sizeof(name), "%s.q", sub);
   check_int(read_int(vpi_test_lookup(name)), q);
   snprintf(name, sizeof(name), "%s.n", leaf);
   check_int(read_int(vpi_test_lookup(name)), n);
}

static PLI_INT32 at_18ns(p_cb_data cb)
{
   // Rising edge of the clock at 15 ns with d = 0101
   check_values("vpi23.u_sub", "vpi23.u_sub.u_leaf", 6, 6, 9);
   check_values("vpi23.u_mid.u_in", "vpi23.u_mid.u_in.u_leaf", 6, 6, 9);
   return 0;
}

static PLI_INT32 end_of_sim(p_cb_data cb)
{
   // d = 0111 since 20 ns and the clock rose at 25 ns and 35 ns
   check_values("vpi23.u_sub", "vpi23.u_sub.u_leaf", 8, 8, 7);
   check_values("vpi23.u_mid.u_in", "vpi23.u_mid.u_in.u_leaf", 8, 8, 7);

   vpi_test_pass();
   return 0;
}

static int count(vpiHandle it)
{
   int n = 0;
   if (it == NULL)
      return 0;
   while (vpi_scan(it) != NULL)
      n++;
   return n;
}

static PLI_INT32 start_of_sim(p_cb_data cb)
{
   // The top level is VHDL: it is seen as a foreign instance which is not
   // a module but the Verilog instances below it are
   fail_unless(vpi_iterate(vpiModule, NULL) == NULL);
   vpi_chk_error(NULL);

   vpiHandle it = vpi_iterate(vpiInstance, NULL);
   check_handle(it);
   vpiHandle top = vpi_scan(it);
   check_handle(top);
   fail_unless(vpi_scan(it) == NULL);
   check_int(vpi_get(vpiType, top), vpiForeignInst);
   check_string(vpi_get_str(vpiFullName, top), "VPI23");
   check_string(vpi_get_str(vpiDefName, top), "VPI23");

   vpiHandle top2 = vpi_handle_by_name("vpi23", NULL);
   check_handle(top2);
   fail_unless(vpi_compare_objects(top, top2));

   // Only the Verilog instance is a module
   vpiHandle mods = vpi_iterate(vpiModule, top);
   check_handle(mods);
   vpiHandle sub = vpi_scan(mods);
   check_handle(sub);
   fail_unless(vpi_scan(mods) == NULL);
   check_int(vpi_get(vpiType, sub), vpiModule);
   check_string(vpi_get_str(vpiName, sub), "U_SUB");
   check_string(vpi_get_str(vpiFullName, sub), "VPI23.U_SUB");
   check_string(vpi_get_str(vpiDefName, sub), "vpi23_sub");

   // The VHDL entity above the other one is a foreign instance
   it = vpi_iterate(vpiInternalScope, top);
   check_handle(it);
   vpiHandle first = vpi_scan(it);
   check_handle(first);
   fail_unless(vpi_compare_objects(first, sub));
   vpiHandle mid = vpi_scan(it);
   check_handle(mid);
   fail_unless(vpi_scan(it) == NULL);
   check_int(vpi_get(vpiType, mid), vpiForeignInst);
   check_string(vpi_get_str(vpiFullName, mid), "VPI23.U_MID");
   check_string(vpi_get_str(vpiDefName, mid), "VPI23_MID");

   it = vpi_iterate(vpiModule, mid);
   check_handle(it);
   vpiHandle in = vpi_scan(it);
   check_handle(in);
   fail_unless(vpi_scan(it) == NULL);
   check_string(vpi_get_str(vpiFullName, in), "VPI23.U_MID.U_IN");

   // Parents are the VHDL instances
   vpiHandle p = vpi_handle(vpiParent, sub);
   check_handle(p);
   fail_unless(vpi_compare_objects(p, top));
   p = vpi_handle(vpiScope, in);
   check_handle(p);
   fail_unless(vpi_compare_objects(p, mid));
   fail_unless(vpi_handle(vpiParent, top) == NULL);

   // Verilog instances below the Verilog ones
   it = vpi_iterate(vpiModule, sub);
   check_handle(it);
   vpiHandle leaf = vpi_scan(it);
   check_handle(leaf);
   fail_unless(vpi_scan(it) == NULL);
   check_string(vpi_get_str(vpiFullName, leaf), "VPI23.U_SUB.u_leaf");
   fail_unless(vpi_compare_objects(vpi_handle_by_name("u_leaf", sub), leaf));
   fail_unless(vpi_compare_objects(vpi_handle_by_name("vpi23.u_sub.u_leaf",
                                                      NULL), leaf));
   fail_unless(vpi_compare_objects(vpi_handle_by_name("u_in", mid), in));

   // Nets, variables, ports and processes of Verilog below VHDL
   vpiHandle t = vpi_handle_by_name("vpi23.u_sub.t", NULL);
   check_handle(t);
   check_int(vpi_get(vpiType, t), vpiNet);
   check_int(vpi_get(vpiSize, t), 4);
   check_string(vpi_get_str(vpiFullName, t), "VPI23.U_SUB.t");
   vpiHandle z = vpi_handle_by_name("z", sub);
   check_handle(z);
   check_int(vpi_get(vpiSize, z), 1);
   check_handle(vpi_handle_by_name("vpi23.u_mid.u_in.u_leaf.n", NULL));
   check_handle(vpi_handle_by_name("u_in.t", mid));
   check_handle(vpi_handle_by_name("u_leaf.n", in));

   check_int(count(vpi_iterate(vpiPort, sub)), 3);
   check_int(count(vpi_iterate(vpiNet, sub)), 3);
   check_int(count(vpi_iterate(vpiReg, sub)), 2);
   check_int(count(vpi_iterate(vpiAlways, sub)), 1);
   check_int(count(vpi_iterate(vpiInitial, sub)), 1);
   check_int(count(vpi_iterate(vpiNet, leaf)), 2);

   // The VHDL objects are only visible through VHPI
   fail_unless(vpi_handle_by_name("vpi23.clk", NULL) == NULL);
   vpi_chk_error(NULL);
   fail_unless(vpi_iterate(vpiNet, top) == NULL);
   fail_unless(vpi_iterate(vpiNet, mid) == NULL);
   fail_unless(vpi_handle_by_name("vpi23.u_sub.vpi23_sub", NULL) == NULL);
   vpi_chk_error(NULL);

   vpi_test_cb(cbAfterDelay, at_18ns, 18, NULL);
   return 0;
}

void vpi23_startup(void)
{
   vpi_test_cb(cbStartOfSimulation, start_of_sim, 0, NULL);
   vpi_test_cb(cbEndOfSimulation, end_of_sim, 0, NULL);
}
