#include "vpi_test.h"

static const char *get_str(vpiHandle h, PLI_INT32 format)
{
   s_vpi_value val = { .format = format };
   vpi_get_value(h, &val);
   check_error();
   return val.value.str;
}

static int get_int(vpiHandle h)
{
   s_vpi_value val = { .format = vpiIntVal };
   vpi_get_value(h, &val);
   check_error();
   return val.value.integer;
}

static void put_str(vpiHandle h, PLI_INT32 format, const char *str)
{
   s_vpi_value val = { .format = format, .value.str = (PLI_BYTE8 *)str };
   vpi_put_value(h, &val, NULL, vpiNoDelay);
   check_error();
}

static PLI_INT32 at_10ns(p_cb_data cb)
{
   // Member which is an unpacked array
   vpiHandle b = vpi_test_lookup("vpi12.rec.b");
   check_int(vpi_get(vpiType, b), vpiRegArray);
   check_int(vpi_get(vpiSize, b), 3);
   vpiHandle b1 = vpi_test_lookup("vpi12.rec.b[1]");
   check_string(vpi_get_str(vpiFullName, b1), "vpi12.rec.b[1]");
   check_int(get_int(b1), 0x11);
   vpiHandle b2 = vpi_handle_by_index(b, 2);
   check_handle(b2);
   check_int(get_int(b2), 0x12);
   put_str(b1, vpiHexStrVal, "77");

   // Nested structure
   vpiHandle in = vpi_test_lookup("vpi12.rec.in");
   check_int(vpi_get(vpiType, in), vpiStructVar);
   check_int(get_int(vpi_test_lookup("vpi12.rec.in.n")), -7);
   vpiHandle x = vpi_test_lookup("vpi12.rec.in.x");
   check_string(get_str(x, vpiHexStrVal), "5");
   put_str(x, vpiBinStrVal, "1100");

   // Wide member
   vpiHandle w = vpi_test_lookup("vpi12.rec.w");
   check_int(vpi_get(vpiSize, w), 72);
   check_string(get_str(w, vpiHexStrVal), "ab0123456789abcdef");
   put_str(w, vpiHexStrVal, "cdfedcba9876543210");
   check_string(get_str(w, vpiHexStrVal), "cdfedcba9876543210");

   // Array of structures
   vpiHandle arr = vpi_test_lookup("vpi12.arr");
   check_int(vpi_get(vpiType, arr), vpiRegArray);
   check_int(vpi_get(vpiSize, arr), 2);
   vpiHandle a1 = vpi_handle_by_index(arr, 1);
   check_handle(a1);
   check_int(vpi_get(vpiType, a1), vpiStructVar);
   check_string(vpi_get_str(vpiFullName, a1), "vpi12.arr[1]");
   check_int(get_int(vpi_test_lookup("vpi12.arr[1].a")), 1);
   check_int(get_int(vpi_test_lookup("vpi12.arr[1].b[2]")), 0x42);
   vpiHandle n1 = vpi_test_lookup("vpi12.arr[1].in.n");
   check_int(get_int(n1), 99);
   s_vpi_value val = { .format = vpiIntVal, .value.integer = 1234 };
   vpi_put_value(n1, &val, NULL, vpiNoDelay);
   check_error();
   put_str(vpi_test_lookup("vpi12.arr[0].b[0]"), vpiHexStrVal, "3c");

   // Wide package variables
   vpiHandle w4 = vpi_test_lookup("vpi12_pkg::wide4");
   check_int(vpi_get(vpiSize, w4), 100);
   check_string(get_str(w4, vpiHexStrVal), "90123456789abcdeffedcba98");
   put_str(w4, vpiHexStrVal, "3000000000000000000000000");
   vpiHandle w2 = vpi_test_lookup("vpi12_pkg::wide2");
   check_string(get_str(w2, vpiHexStrVal), "123456789abcdef01357");
   val.format = vpiIntVal;
   val.value.integer = 1;
   vpi_put_value(w2, &val, NULL, vpiNoDelay);
   check_error();
   check_string(get_str(w2, vpiHexStrVal), "00000000000000000001");

   // Wide module variable
   s_vpi_vecval vec[4] = { { 2, 0 }, { 0, 0 }, { 1, 0 }, { 0, 0 } };
   val.format = vpiVectorVal;
   val.value.vector = vec;
   vpi_put_value(vpi_test_lookup("vpi12.mwide"), &val, NULL, vpiNoDelay);
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

void vpi12_startup(void)
{
   vpi_test_cb(cbStartOfSimulation, start_of_sim, 0, NULL);
   vpi_test_cb(cbEndOfSimulation, end_of_sim, 0, NULL);
}
