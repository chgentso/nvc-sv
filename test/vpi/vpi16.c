#include "vpi_test.h"
#include "sv_vpi_user.h"

typedef struct {
   const char *name;
   vpiHandle   obj;
   int         changes;
} watch_t;

static watch_t watches[] = {
   { "vpi16.rec.a" },
   { "vpi16.rec.b" },
   { "vpi16.rec.r" },
   { "vpi16.rec.s" },
   { "vpi16.rec.arr[1]" },
   { "vpi16.rec.arr[0]" },
   { "vpi16.recs[1].a" },
   { "vpi16.recs[0].a" },
   { "vpi16.str" },
   { "vpi16.t" },
   { "vpi16.plain" },
   { "vpi16_pkg::counter" },
   { "vpi16.rec" },        // Whole structure
   { "vpi16.rec.arr" },    // Whole array
   { "vpi16.recs" },       // Array of structures
};

#define NWATCH (sizeof(watches) / sizeof(watches[0]))

static const char *last_str;
static PLI_UINT64 last_time;
static int last_a;

static PLI_INT32 changed(p_cb_data cb)
{
   watch_t *w = (watch_t *)cb->user_data;
   w->changes++;

   fail_unless(cb->reason == cbValueChange);
   check_int(cb->obj != NULL, 1);
   check_int(cb->time->type, vpiSimTime);

   if (w == &watches[0])
      last_a = cb->value->value.integer;
   else if (w == &watches[8]) {
      check_int(cb->value->format, vpiStringVal);
      last_str = cb->value->value.str;
      check_string(last_str, "world");
   }
   else if (w == &watches[3]) {
      check_int(cb->value->format, vpiStringVal);
      check_string(cb->value->value.str, "hello");
   }
   else if (w == &watches[9]) {
      check_int(cb->value->format, vpiTimeVal);
      last_time = ((PLI_UINT64)cb->value->value.time->high << 32)
         | cb->value->value.time->low;
   }

   return 0;
}

static PLI_INT32 start_of_sim(p_cb_data cb)
{
   for (int i = 0; i < NWATCH; i++) {
      watches[i].obj = vpi_test_lookup(watches[i].name);

      s_vpi_time time = { .type = vpiSimTime };
      s_vpi_value value = { .format = vpiObjTypeVal };
      switch (i) {
      case 8: value.format = vpiStringVal; break;
      case 9: value.format = vpiTimeVal; break;
      case 12: case 13: case 14: value.format = vpiSuppressVal; break;
      }

      s_cb_data data = {
         .reason    = cbValueChange,
         .cb_rtn    = changed,
         .obj       = watches[i].obj,
         .time      = &time,
         .value     = &value,
         .user_data = (PLI_BYTE8 *)&watches[i],
      };
      vpiHandle h = vpi_register_cb(&data);
      check_error();
      check_handle(h);
   }

   return 0;
}

static void expect(int i, int changes)
{
   if (watches[i].changes != changes)
      vpi_test_fail(__FILE__, __LINE__, "%s: expected %d changes but have %d",
                    watches[i].name, changes, watches[i].changes);
}

static PLI_INT32 end_of_sim(p_cb_data cb)
{
   expect(0, 2);    // rec.a, then again to 2
   expect(1, 2);    // rec.b, from X to 0 when initialised, then 55
   expect(2, 1);    // rec.r
   expect(3, 1);    // rec.s
   expect(4, 1);    // rec.arr[1]
   expect(5, 0);    // rec.arr[0]
   expect(6, 1);    // recs[1].a
   expect(7, 0);    // recs[0].a
   expect(8, 1);    // str
   expect(9, 1);    // t
   expect(10, 1);   // plain
   expect(11, 1);   // package variable
   expect(12, 7);   // rec: b (X to 0), a, b, r, s, arr[1], a
   expect(13, 1);   // rec.arr
   expect(14, 1);   // recs

   check_int(last_a, 2);

   // The value of a time variable is in the time unit of its scope
   check_int(last_time, 8);
   check_int(vpi_get(vpiTimeUnit, watches[9].obj), -9);
   check_int(vpi_get(vpiTimePrecision, watches[9].obj), -12);
   check_int(vpi_get(vpiType, watches[9].obj), vpiTimeVar);

   vpi_test_pass();
   return 0;
}

void vpi16_startup(void)
{
   vpi_test_cb(cbStartOfSimulation, start_of_sim, 0, NULL);
   vpi_test_cb(cbEndOfSimulation, end_of_sim, 0, NULL);
}
