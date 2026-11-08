#include "vpi_test.h"

#include <string.h>

static void check_str_value(vpiHandle h, PLI_INT32 format, const char *expect,
                            int line)
{
   s_vpi_value val = { .format = format };
   vpi_get_value(h, &val);
   __check_error(__FILE__, line);
   __check_string(val.value.str, expect, __FILE__, line);
}

#define check_str_value(h, f, e) check_str_value((h), (f), (e), __LINE__)

static void put_str(vpiHandle h, PLI_INT32 format, const char *str)
{
   s_vpi_value val = { .format = format, .value.str = (PLI_BYTE8 *)str };
   vpi_put_value(h, &val, NULL, vpiNoDelay);
   check_error();
}

static PLI_INT32 release_cb(p_cb_data cb)
{
   vpiHandle w = vpi_test_lookup("vpi3.w");
   vpiHandle wv = vpi_test_lookup("vpi3.wv");

   s_vpi_value val = { .format = vpiScalarVal };
   vpi_put_value(w, &val, NULL, vpiReleaseFlag);
   check_error();

   val.format = vpiIntVal;
   vpi_put_value(wv, &val, NULL, vpiReleaseFlag);
   check_error();

   // After release a net has the value of its drivers again
   val.format = vpiScalarVal;
   vpi_get_value(w, &val);
   check_int(val.value.scalar, vpi0);

   return 0;
}

static PLI_INT32 at_10ns(p_cb_data cb)
{
   check_int(vpi_test_now(), 10);

   vpiHandle v8 = vpi_test_lookup("vpi3.v8");
   check_int(vpi_get(vpiSize, v8), 8);
   check_int(vpi_get(vpiSigned, v8), 0);
   check_int(vpi_get(vpiVector, v8), 1);

   check_str_value(v8, vpiBinStrVal, "1010x0z1");

   vpiHandle left = vpi_handle(vpiLeftRange, v8);
   check_handle(left);
   s_vpi_value val = { .format = vpiIntVal };
   vpi_get_value(left, &val);
   check_int(val.value.integer, 7);
   vpiHandle right = vpi_handle(vpiRightRange, v8);
   check_handle(right);
   val.format = vpiIntVal;
   vpi_get_value(right, &val);
   check_int(val.value.integer, 0);

   // Vector format: a 0 b 0 -> 0, a 1 b 0 -> 1, a 0 b 1 -> z, a 1 b 1 -> x
   val.format = vpiVectorVal;
   vpi_get_value(v8, &val);
   check_error();
   check_int(val.value.vector[0].aval & 0xff, 0xa9);  // 1010 1001
   check_int(val.value.vector[0].bval & 0xff, 0x0a);  // 0000 1010

   // Integer variable
   vpiHandle i = vpi_test_lookup("vpi3.i");
   check_int(vpi_get(vpiSigned, i), 1);
   val.format = vpiIntVal;
   vpi_get_value(i, &val);
   check_int(val.value.integer, -5);
   check_str_value(i, vpiDecStrVal, "-5");
   check_str_value(i, vpiHexStrVal, "fffffffb");

   // Real variable
   vpiHandle re = vpi_test_lookup("vpi3.re");
   check_int(vpi_get(vpiType, re), vpiRealVar);
   val.format = vpiRealVal;
   vpi_get_value(re, &val);
   fail_unless(val.value.real == 2.5);
   val.format = vpiObjTypeVal;
   vpi_get_value(re, &val);
   check_int(val.format, vpiRealVal);
   fail_unless(val.value.real == 2.5);

   // String variable
   vpiHandle s = vpi_test_lookup("vpi3.s");
   check_str_value(s, vpiStringVal, "hello");

   // Wide vector
   vpiHandle wide = vpi_test_lookup("vpi3.wide");
   check_int(vpi_get(vpiSize, wide), 100);
   check_str_value(wide, vpiHexStrVal, "a123456789abcdef00fedcba9");
   val.format = vpiVectorVal;
   vpi_get_value(wide, &val);
   check_int((PLI_UINT32)val.value.vector[0].aval, 0x0fedcba9);
   check_int((PLI_UINT32)val.value.vector[1].aval, 0x9abcdef0);
   check_int((PLI_UINT32)val.value.vector[2].aval, 0x12345678);
   check_int(val.value.vector[3].aval & 0xf, 0xa);
   for (int n = 0; n < 4; n++)
      check_int(val.value.vector[n].bval & (n == 3 ? 0xf : 0xffffffff), 0);

   // 2-state vector
   vpiHandle b16 = vpi_test_lookup("vpi3.b16");
   check_str_value(b16, vpiHexStrVal, "beef");
   check_str_value(b16, vpiOctStrVal, "137357");
   check_str_value(b16, vpiDecStrVal, "48879");

   // Scalar net
   vpiHandle w = vpi_test_lookup("vpi3.w");
   check_int(vpi_get(vpiType, w), vpiNet);
   check_int(vpi_get(vpiScalar, w), 1);
   val.format = vpiScalarVal;
   vpi_get_value(w, &val);
   check_int(val.value.scalar, vpi0);

   // Writes with no delay are visible immediately
   vpiHandle r4 = vpi_test_lookup("vpi3.r4");
   put_str(r4, vpiBinStrVal, "10z1");
   check_str_value(r4, vpiBinStrVal, "10z1");

   val.format = vpiIntVal;
   val.value.integer = 1234;
   vpi_put_value(i, &val, NULL, vpiNoDelay);
   check_error();

   val.format = vpiRealVal;
   val.value.real = -0.75;
   vpi_put_value(re, &val, NULL, vpiNoDelay);
   check_error();

   put_str(s, vpiStringVal, "world");
   check_str_value(s, vpiStringVal, "world");

   s_vpi_vecval vec[4] = {
      { 0x89abcdef, 0 }, { 0x01234567, 0 }, { 0xdeadbeef, 0 }, { 0x5, 0 }
   };
   val.format = vpiVectorVal;
   val.value.vector = vec;
   vpi_put_value(vpi_test_lookup("vpi3.wide_in"), &val, NULL, vpiNoDelay);
   check_error();

   put_str(b16, vpiHexStrVal, "1234");

   // Force a scalar net and a vector net
   val.format = vpiScalarVal;
   val.value.scalar = vpi1;
   vpi_put_value(w, &val, NULL, vpiForceFlag);
   check_error();

   val.format = vpiIntVal;
   val.value.integer = 0x77;
   vpi_put_value(vpi_test_lookup("vpi3.wv"), &val, NULL, vpiForceFlag);
   check_error();

   // Inertial with zero delay: applied in the current time step
   s_vpi_time t = { .type = vpiSimTime, .high = 0, .low = 0 };
   val.format = vpiIntVal;
   val.value.integer = 0x11;
   vpi_put_value(vpi_test_lookup("vpi3.inr"), &val, &t, vpiInertialDelay);
   check_error();

   // Transport delay of 3 time units: applied at 13 ns
   t.low = 3;
   val.value.integer = 0x22;
   vpi_put_value(vpi_test_lookup("vpi3.intr"), &val, &t, vpiTransportDelay);
   check_error();

   vpi_test_cb(cbAfterDelay, release_cb, 5, NULL);
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

void vpi3_startup(void)
{
   vpi_test_cb(cbStartOfSimulation, start_of_sim, 0, NULL);
   vpi_test_cb(cbEndOfSimulation, end_of_sim, 0, NULL);
}
