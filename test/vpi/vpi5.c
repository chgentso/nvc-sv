#include "vpi_test.h"

#include <stdio.h>

static void check_range(vpiHandle obj, int which, int left, int right)
{
   vpiHandle it = vpi_iterate(vpiRange, obj), r = NULL;
   check_handle(it);
   for (int n = 0; n <= which; n++)
      r = vpi_scan(it);
   check_handle(r);
   vpi_release_handle(it);

   s_vpi_value val = { .format = vpiIntVal };
   vpi_get_value(vpi_handle(vpiLeftRange, r), &val);
   check_error();
   check_int(val.value.integer, left);
   val.format = vpiIntVal;
   vpi_get_value(vpi_handle(vpiRightRange, r), &val);
   check_error();
   check_int(val.value.integer, right);
}

static int get_int(vpiHandle h)
{
   s_vpi_value val = { .format = vpiIntVal };
   vpi_get_value(h, &val);
   check_error();
   return val.value.integer;
}

static void put_int(vpiHandle h, int value)
{
   s_vpi_value val = { .format = vpiIntVal, .value.integer = value };
   vpi_put_value(h, &val, NULL, vpiNoDelay);
   check_error();
}

static PLI_INT32 at_10ns(p_cb_data cb)
{
   // Unpacked array of variables
   vpiHandle mem = vpi_test_lookup("vpi5.mem");
   const int type = vpi_get(vpiType, mem);
   fail_unless(type == vpiRegArray || type == vpiMemory);
   check_int(vpi_get(vpiSize, mem), 4);
   check_range(mem, 0, 0, 3);

   vpiHandle it = vpi_iterate(vpiReg, mem), e;
   check_handle(it);
   int n = 0;
   while ((e = vpi_scan(it))) {
      char name[16];
      snprintf(name, sizeof(name), "mem[%d]", n);
      check_string(vpi_get_str(vpiName, e), name);
      check_int(vpi_get(vpiType, e), vpiReg);
      check_int(vpi_get(vpiSize, e), 8);
      check_int(get_int(e), 0x10 + n);
      n++;
   }
   check_int(n, 4);

   vpiHandle m2e = vpi_handle_by_index(mem, 2);
   check_handle(m2e);
   check_string(vpi_get_str(vpiFullName, m2e), "vpi5.mem[2]");
   check_int(get_int(m2e), 0x12);
   put_int(m2e, 0xab);
   check_int(get_int(m2e), 0xab);

   // Unpacked array of nets
   vpiHandle wa = vpi_test_lookup("vpi5.wa");
   check_int(vpi_get(vpiType, wa), vpiNetArray);
   check_int(vpi_get(vpiSize, wa), 2);
   check_range(wa, 0, 1, 0);
   vpiHandle wa1 = vpi_handle_by_index(wa, 1);
   check_handle(wa1);
   check_int(vpi_get(vpiType, wa1), vpiNet);
   check_int(get_int(wa1), 0xc);

   it = vpi_iterate(vpiNet, wa);
   check_handle(it);
   n = 0;
   while ((e = vpi_scan(it)))
      n++;
   check_int(n, 2);

   // Two-dimensional unpacked array
   vpiHandle m2 = vpi_test_lookup("vpi5.m2");
   check_range(m2, 0, 0, 1);
   check_range(m2, 1, 0, 2);
   check_int(get_int(vpi_test_lookup("vpi5.m2[1][2]")), 0xee);
   vpiHandle m201 = vpi_test_lookup("vpi5.m2[0][1]");
   check_int(vpi_get(vpiSize, m201), 8);
   put_int(m201, 0x42);

   // Bit-select of a vector
   vpiHandle vec = vpi_test_lookup("vpi5.vec");
   vpiHandle bit = vpi_handle_by_index(vec, 3);
   check_handle(bit);
   check_int(vpi_get(vpiType, bit), vpiRegBit);
   check_string(vpi_get_str(vpiFullName, bit), "vpi5.vec[3]");
   check_int(vpi_get(vpiSize, bit), 1);
   s_vpi_value val = { .format = vpiScalarVal, .value.scalar = vpi1 };
   vpi_put_value(bit, &val, NULL, vpiNoDelay);
   check_error();
   check_int(get_int(vec), 0x08);

   // Two-dimensional packed array of logic
   vpiHandle p2 = vpi_test_lookup("vpi5.p2");
   check_int(vpi_get(vpiType, p2), vpiLogicVar);
   check_int(vpi_get(vpiSize, p2), 8);
   check_range(p2, 0, 1, 0);
   check_range(p2, 1, 3, 0);
   check_int(get_int(p2), 0x5a);

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

void vpi5_startup(void)
{
   vpi_test_cb(cbStartOfSimulation, start_of_sim, 0, NULL);
   vpi_test_cb(cbEndOfSimulation, end_of_sim, 0, NULL);
}
