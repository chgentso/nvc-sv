#include "vpi_test.h"

#include <string.h>

static void check_var(const char *name, int type, int size, int is_signed,
                      int line)
{
   vpiHandle h = vpi_handle_by_name((PLI_BYTE8 *)name, NULL);
   if (h == NULL) {
      vpi_test_fail(__FILE__, line, "cannot find %s", name);
      return;
   }
   __check_int(vpi_get(vpiType, h), type, __FILE__, line);
   __check_int(vpi_get(vpiSize, h), size, __FILE__, line);
   __check_int(vpi_get(vpiSigned, h), is_signed, __FILE__, line);
}

#define check_var(n, t, sz, s) check_var((n), (t), (sz), (s), __LINE__)

static const char *get_str(vpiHandle h, PLI_INT32 format)
{
   s_vpi_value val = { .format = format };
   vpi_get_value(h, &val);
   check_error();
   return val.value.str;
}

static PLI_INT32 at_10ns(p_cb_data cb)
{
   check_var("vpi6.i32", vpiIntVar, 32, 1);
   check_var("vpi6.i16", vpiShortIntVar, 16, 1);
   check_var("vpi6.i64", vpiLongIntVar, 64, 1);
   check_var("vpi6.i8", vpiByteVar, 8, 1);
   check_var("vpi6.b1", vpiBitVar, 1, 0);
   check_var("vpi6.bv", vpiBitVar, 8, 0);
   check_var("vpi6.l1", vpiLogicVar, 1, 0);
   check_var("vpi6.st", vpiEnumVar, 2, 0);
   check_var("vpi6.pr", vpiStructVar, 8, 0);
   check_var("vpi6.wu", vpiUnionVar, 8, 0);
   check_int(vpi_get(vpiType, vpi_test_lookup("vpi6.re")), vpiRealVar);

   vpiHandle str = vpi_test_lookup("vpi6.str");
   check_int(vpi_get(vpiType, str), vpiStringVar);
   check_string(get_str(str, vpiStringVal), "sv");

   s_vpi_value val = { .format = vpiIntVal };
   vpi_get_value(vpi_test_lookup("vpi6.i32"), &val);
   check_int(val.value.integer, -100);

   val.format = vpiIntVal;
   vpi_get_value(vpi_test_lookup("vpi6.i16"), &val);
   check_int(val.value.integer, -32767);

   val.format = vpiIntVal;
   vpi_get_value(vpi_test_lookup("vpi6.i8"), &val);
   check_int(val.value.integer, -2);

   vpiHandle i64 = vpi_test_lookup("vpi6.i64");
   check_string(get_str(i64, vpiHexStrVal), "8000000000000003");
   check_string(get_str(i64, vpiDecStrVal), "-9223372036854775805");

   check_string(get_str(vpi_test_lookup("vpi6.bv"), vpiBinStrVal),
                "11000011");
   check_string(get_str(vpi_test_lookup("vpi6.l1"), vpiBinStrVal), "x");

   val.format = vpiRealVal;
   vpi_get_value(vpi_test_lookup("vpi6.re"), &val);
   fail_unless(val.value.real == 1.0e10);

   // Enum variable: the value is the encoding of the label
   vpiHandle st = vpi_test_lookup("vpi6.st");
   val.format = vpiIntVal;
   vpi_get_value(st, &val);
   check_int(val.value.integer, 1);
   val.format = vpiIntVal;
   val.value.integer = 2;
   vpi_put_value(st, &val, NULL, vpiNoDelay);
   check_error();

   // Packed structure and its members
   vpiHandle pr = vpi_test_lookup("vpi6.pr");
   check_int(vpi_get(vpiPacked, pr), 1);
   check_string(get_str(pr, vpiBinStrVal), "10011100");

   static const char *names[] = { "hi", "lo" };
   static const char *values[] = { "1001", "1100" };
   vpiHandle it = vpi_iterate(vpiMember, pr), m;
   check_handle(it);
   int n = 0;
   while ((m = vpi_scan(it))) {
      fail_unless(n < 2);
      check_string(vpi_get_str(vpiName, m), names[n]);
      check_int(vpi_get(vpiSize, m), 4);
      check_string(get_str(m, vpiBinStrVal), values[n]);
      n++;
   }
   check_int(n, 2);

   // A structure is selected by its members, not by index
   fail_unless(vpi_iterate(vpiRange, pr) == NULL);
   fail_unless(vpi_handle_by_index(pr, 0) == NULL);
   vpi_chk_error(NULL);

   vpiHandle lo = vpi_test_lookup("vpi6.pr.lo");
   check_string(vpi_get_str(vpiFullName, lo), "vpi6.pr.lo");
   val.format = vpiIntVal;
   val.value.integer = 1;
   vpi_put_value(lo, &val, NULL, vpiNoDelay);
   check_error();
   check_string(get_str(pr, vpiHexStrVal), "91");

   // Packed union: all members share the bits
   vpiHandle wu = vpi_test_lookup("vpi6.wu");
   check_int(vpi_get(vpiPacked, wu), 1);
   vpiHandle word = vpi_test_lookup("vpi6.wu.word");
   check_string(get_str(word, vpiHexStrVal), "35");
   vpiHandle nib = vpi_test_lookup("vpi6.wu.nib");
   check_string(get_str(nib, vpiBinStrVal), "00110101");

   // Write a 64-bit value with the vector format
   s_vpi_vecval vec[2] = { { 0x00000001, 0 }, { 0xffffffff, 0 } };
   val.format = vpiVectorVal;
   val.value.vector = vec;
   vpi_put_value(i64, &val, NULL, vpiNoDelay);
   check_error();
   check_string(get_str(i64, vpiDecStrVal), "-4294967295");

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

void vpi6_startup(void)
{
   vpi_test_cb(cbStartOfSimulation, start_of_sim, 0, NULL);
   vpi_test_cb(cbEndOfSimulation, end_of_sim, 0, NULL);
}
