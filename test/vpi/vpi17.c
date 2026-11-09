#include "vpi_test.h"

#include <stdio.h>
#include <string.h>

static const int widths[] = {
1, 2, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 72, 127, 128, 129, 200
};

#define NW (sizeof(widths) / sizeof(widths[0]))

static vpiHandle handles[NW];
static int cb_checked[3];

// The value of bit i in the given phase: 0, 1, 2 for z or 3 for x
static int bit_of(int i, int phase)
{
   if (phase == 1)
      return (int[]){ 0, 1, 2, 3 }[i % 4];
   else
      return (i % 3) == 0;
}

static void check_value(int n, int phase, s_vpi_value *v, int format)
{
   const int w = widths[n];

   switch (format) {
   case vpiBinStrVal:
      {
         const char *str = v->value.str;
         fail_unless(strlen(str) == w);
         for (int i = 0; i < w; i++) {
            const char want = "01zx"[bit_of(w - 1 - i, phase)];
            if (str[i] != want)
               vpi_test_fail(__FILE__, __LINE__, "width %d phase %d "
                             "position %d: got '%c' want '%c'", w, phase, i,
                             str[i], want);
         }
      }
      break;

   case vpiVectorVal:
      for (int i = 0; i < w; i++) {
         const int b = bit_of(i, phase);
         const int aval = (v->value.vector[i / 32].aval >> (i % 32)) & 1;
         const int bval = (v->value.vector[i / 32].bval >> (i % 32)) & 1;
         // vpiZ is aval 0 bval 1 and vpiX is aval 1 bval 1
         if (aval != (b & 1) || bval != (b >> 1))
            vpi_test_fail(__FILE__, __LINE__, "width %d phase %d bit %d: "
                          "aval %d bval %d", w, phase, i, aval, bval);
      }
      // Bits above the width are clear
      if (w % 32 != 0) {
         const s_vpi_vecval top = v->value.vector[(w - 1) / 32];
         fail_unless((top.aval >> (w % 32)) == 0);
         fail_unless((top.bval >> (w % 32)) == 0);
      }
      break;

   case vpiIntVal:
      {
         // Only defined values have an integer value
         uint32_t want = 0;
         for (int i = 0; i < w && i < 32; i++)
            want |= (uint32_t)bit_of(i, phase) << i;
         check_int((uint32_t)v->value.integer, want);
      }
      break;

   case vpiHexStrVal:
      {
         const char *str = v->value.str;
         const int digits = (w + 3) / 4;
         fail_unless(strlen(str) == digits);
         for (int d = 0; d < digits; d++) {
            // A digit is x or z only when all its bits are, else X or Z
            int val = 0, nz = 0, nx = 0;
            for (int j = 0; j < 4 && d * 4 + j < w; j++) {
               const int b = bit_of(d * 4 + j, phase);
               if (b == 2) nz++;
               else if (b == 3) nx++;
               else val |= b << j;
            }
            if (nz == 0 && nx == 0) {
               const char want = "0123456789abcdef"[val];
               check_int(str[digits - 1 - d], want);
            }
            else
               fail_unless(strchr("xXzZ", str[digits - 1 - d]) != NULL);
         }
      }
      break;
   }
}

static void check_all(int phase)
{
   for (int n = 0; n < NW; n++) {
      const int formats[] = { vpiBinStrVal, vpiVectorVal, vpiHexStrVal,
                              vpiIntVal };
      for (int f = 0; f < 4; f++) {
         if (formats[f] == vpiIntVal && phase == 1)
            continue;   // Undefined

         s_vpi_value v = { .format = formats[f] };
         vpi_get_value(handles[n], &v);
         check_error();
         check_value(n, phase, &v, formats[f]);
      }

      // The default format of a vector is a vector
      s_vpi_value v = { .format = vpiObjTypeVal };
      vpi_get_value(handles[n], &v);
      check_int(v.format, widths[n] == 1 ? vpiScalarVal : vpiVectorVal);
      if (widths[n] > 1)
         check_value(n, phase, &v, vpiVectorVal);
   }
}

static int ncallbacks;

static PLI_INT32 changed(p_cb_data cb)
{
   // The value delivered with the callback is the one a read now gives
   ncallbacks++;

   s_vpi_value v = { .format = vpiBinStrVal };
   vpi_get_value(cb->obj, &v);
   check_string(cb->value->value.str, v.value.str);
   return 0;
}

static PLI_INT32 phase1(p_cb_data cb)
{
   check_all(1);
   cb_checked[1]++;
   return 0;
}

static PLI_INT32 phase2(p_cb_data cb)
{
   check_all(2);
   cb_checked[2]++;
   return 0;
}

static PLI_INT32 start_of_sim(p_cb_data cb)
{
   for (int n = 0; n < NW; n++) {
      char name[32];
      snprintf(name, sizeof(name), "vpi17.s%d", widths[n]);
      handles[n] = vpi_test_lookup(name);
   }

   for (int n = 0; n < NW; n++) {
      s_vpi_value value = { .format = vpiBinStrVal };
      s_vpi_time time = { .type = vpiSimTime };
      s_cb_data cb = {
         .reason = cbValueChange,
         .cb_rtn = changed,
         .obj    = handles[n],
         .time   = &time,
         .value  = &value,
      };
      check_handle(vpi_register_cb(&cb));
   }

   vpi_test_cb(cbAfterDelay, phase1, 2, NULL);
   vpi_test_cb(cbAfterDelay, phase2, 4, NULL);
   return 0;
}

static PLI_INT32 end_of_sim(p_cb_data cb)
{
   check_int(cb_checked[1], 1);
   check_int(cb_checked[2], 1);
   fail_unless(ncallbacks >= 2 * NW);
   vpi_test_pass();
   return 0;
}

void vpi17_startup(void)
{
   vpi_test_cb(cbStartOfSimulation, start_of_sim, 0, NULL);
   vpi_test_cb(cbEndOfSimulation, end_of_sim, 0, NULL);
}
