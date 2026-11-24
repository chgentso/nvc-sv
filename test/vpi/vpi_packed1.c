#include "vpi_test.h"

#include <string.h>

static int s8_changes = 0;
static int w128_changes = 0;

static const char *get_str(vpiHandle h, PLI_INT32 fmt)
{
   s_vpi_value val = { .format = fmt };
   vpi_get_value(h, &val);
   check_error();
   return val.value.str;
}

static PLI_INT32 on_s8_change(p_cb_data cb)
{
   s8_changes++;
   return 0;
}

static PLI_INT32 on_w128_change(p_cb_data cb)
{
   w128_changes++;
   return 0;
}

static PLI_INT32 at_10ns(p_cb_data cb)
{
   vpiHandle s8 = vpi_test_lookup("vpi_packed1.s8");
   vpiHandle b16 = vpi_test_lookup("vpi_packed1.b16");
   vpiHandle w128 = vpi_test_lookup("vpi_packed1.w128");
   vpiHandle b100 = vpi_test_lookup("vpi_packed1.b100");

   check_string(get_str(s8, vpiBinStrVal), "10zx01xz");
   check_string(get_str(b16, vpiHexStrVal), "cafe");

   s_vpi_value vec = { .format = vpiVectorVal };
   vpi_get_value(w128, &vec);
   check_error();
   check_int(vec.value.vector[0].aval, 0x0fedcba9);
   check_int(vec.value.vector[0].bval, 0);
   check_int(vec.value.vector[1].aval, 0x9abcdef0);
   check_int(vec.value.vector[2].aval, 0x12345678);
   check_int(vec.value.vector[3].aval, 0xdeadbeef);

   check_string(get_str(b100, vpiHexStrVal), "5112233445566778899aabbcc");

   // Write 4-state string, 2-state with X/Z masking, and wide vector
   s_vpi_value v = { .format = vpiBinStrVal };
   v.value.str = "0101xz10";
   vpi_put_value(s8, &v, NULL, vpiNoDelay);
   check_error();

   v.value.str = "xzxzxzxz10101010";
   vpi_put_value(b16, &v, NULL, vpiNoDelay);
   check_error();
   check_string(get_str(b16, vpiHexStrVal), "00aa");

   s_vpi_vecval wvec[4] = {
      { .aval = 0x76543210, .bval = 0 },
      { .aval = 0xfedcba98, .bval = 0 },
      { .aval = 0x89abcdef, .bval = 0 },
      { .aval = 0x01234567, .bval = 0 },
   };
   s_vpi_value wval = { .format = vpiVectorVal, .value.vector = wvec };
   vpi_put_value(w128, &wval, NULL, vpiNoDelay);
   check_error();

   s_vpi_vecval bvec[4] = {
      { .aval = 0x9abcdef0, .bval = 0 },
      { .aval = 0x12345678, .bval = 0 },
      { .aval = 0xffffffff, .bval = 0xffffffff },
      { .aval = 0xa,        .bval = 0 },
   };
   s_vpi_value bval = { .format = vpiVectorVal, .value.vector = bvec };
   vpi_put_value(b100, &bval, NULL, vpiNoDelay);
   check_error();

   return 0;
}

static PLI_INT32 at_18ns(p_cb_data cb)
{
   vpiHandle s8 = vpi_test_lookup("vpi_packed1.s8");
   s_vpi_value v = { .format = vpiIntVal, .value.integer = 0x3c };
   vpi_put_value(s8, &v, NULL, vpiForceFlag);
   check_error();
   return 0;
}

static PLI_INT32 at_28ns(p_cb_data cb)
{
   vpiHandle s8 = vpi_test_lookup("vpi_packed1.s8");
   s_vpi_value v = { .format = vpiIntVal };
   vpi_put_value(s8, &v, NULL, vpiReleaseFlag);
   check_error();
   return 0;
}

static PLI_INT32 start_of_sim(p_cb_data cb)
{
   vpiHandle s8 = vpi_test_lookup("vpi_packed1.s8");
   vpiHandle w128 = vpi_test_lookup("vpi_packed1.w128");

   s_cb_data cb1 = {
      .reason = cbValueChange,
      .cb_rtn = on_s8_change,
      .obj    = s8,
   };
   check_handle(vpi_register_cb(&cb1));

   s_cb_data cb2 = {
      .reason = cbValueChange,
      .cb_rtn = on_w128_change,
      .obj    = w128,
   };
   check_handle(vpi_register_cb(&cb2));

   vpi_test_cb(cbAfterDelay, at_10ns, 10, NULL);
   vpi_test_cb(cbAfterDelay, at_18ns, 18, NULL);
   vpi_test_cb(cbAfterDelay, at_28ns, 28, NULL);
   return 0;
}

static PLI_INT32 end_of_sim(p_cb_data cb)
{
   fail_unless(s8_changes >= 3);
   fail_unless(w128_changes >= 2);
   vpi_test_pass();
   return 0;
}

void vpi_packed1_startup(void)
{
   vpi_test_cb(cbStartOfSimulation, start_of_sim, 0, NULL);
   vpi_test_cb(cbEndOfSimulation, end_of_sim, 0, NULL);
}
