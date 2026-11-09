#include "vhpi_test.h"

#include <stdio.h>
#include <string.h>

#define check_int(a, b) fail_unless((long)(a) == (long)(b))

static const int widths[] = { 1, 7, 8, 9, 64, 65, 130 };
#define NW (sizeof(widths) / sizeof(widths[0]))

static vhpiHandleT handles[NW], arr;
static int nchecked;

// Element i (counted from the left, which is the highest index) of the
// vector of width w in phase ph; see the pattern function in vhpi28.vhd
static char expect_char(int w, int ph, int left)
{
   const int i = w - 1 - left;
   return "UX01ZWLH-"[(i + ph) % 9];
}

static void check_vec(vhpiHandleT h, int w, int ph)
{
   char buf[256];
   vhpiValueT v = { .format = vhpiBinStrVal, .bufSize = sizeof(buf),
                    .value.str = (vhpiCharT *)buf };
   fail_if(vhpi_get_value(h, &v));
   check_int(v.numElems, w);
   check_int(strlen(buf), w);
   for (int i = 0; i < w; i++)
      if (buf[i] != expect_char(w, ph, i))
         vhpi_assert(vhpiFailure, "width %d phase %d position %d: got '%c' "
                     "want '%c'", w, ph, i, buf[i], expect_char(w, ph, i));

   vhpiEnumT enums[256];
   vhpiValueT e = { .format = vhpiLogicVecVal, .bufSize = sizeof(enums),
                    .value.enumvs = enums };
   fail_if(vhpi_get_value(h, &e));
   check_int(e.numElems, w);
   for (int i = 0; i < w; i++)
      check_int(enums[i], (int)(strchr("UX01ZWLH-", buf[i]) - "UX01ZWLH-"));

   // The default format of a vector of std_logic
   vhpiValueT o = { .format = vhpiObjTypeVal, .bufSize = sizeof(enums),
                    .value.enumvs = enums };
   fail_if(vhpi_get_value(h, &o));
   check_int(o.format, vhpiLogicVecVal);
   check_int(o.numElems, w);
}

static void check_phase(const vhpiCbDataT *cb)
{
   const int ph = (int)(intptr_t)cb->user_data;
   for (int n = 0; n < NW; n++)
      check_vec(handles[n], widths[n], ph);

   check_vec(arr, 9, ph);

   // Elements of a vector
   for (int i = 0; i < 9; i++) {
      vhpiHandleT e = VHPI_CHECK(vhpi_handle_by_index(vhpiIndexedNames, arr, i));
      vhpiValueT v = { .format = vhpiLogicVal };
      fail_if(vhpi_get_value(e, &v));
      check_int(v.value.enumv,
                (int)(strchr("UX01ZWLH-", expect_char(9, ph, i))
                      - "UX01ZWLH-"));
      vhpi_release_handle(e);
   }

   nchecked++;
}

static void start_of_sim(const vhpiCbDataT *cb_data)
{
   vhpiHandleT root = VHPI_CHECK(vhpi_handle(vhpiRootInst, NULL));
   for (int n = 0; n < NW; n++) {
      char name[16];
      snprintf(name, sizeof(name), "s%d", widths[n]);
      handles[n] = VHPI_CHECK(vhpi_handle_by_name(name, root));
   }
   arr = VHPI_CHECK(vhpi_handle_by_name("arr", root));

   for (int ph = 0; ph < 9; ph++) {
      vhpiTimeT t = { .low = (ph * 10 + 5) * 1000000u };
      vhpiCbDataT cb = {
         .reason = vhpiCbAfterDelay,
         .cb_rtn = check_phase,
         .time = &t,
         .user_data = (void *)(intptr_t)ph,
      };
      VHPI_CHECK(vhpi_register_cb(&cb, 0));
   }

   vhpi_release_handle(root);
}

static void end_of_sim(const vhpiCbDataT *cb_data)
{
   check_int(nchecked, 9);
}

void vhpi28_startup(void)
{
   vhpiCbDataT cb_data = {
      .reason = vhpiCbStartOfSimulation,
      .cb_rtn = start_of_sim,
   };
   VHPI_CHECK(vhpi_register_cb(&cb_data, 0));

   cb_data.reason = vhpiCbEndOfSimulation;
   cb_data.cb_rtn = end_of_sim;
   VHPI_CHECK(vhpi_register_cb(&cb_data, 0));
}
