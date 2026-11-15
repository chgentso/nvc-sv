//
//  Copyright (C) 2026  Nick Gasson
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#include "vhpi_test.h"

#include <string.h>
#include <stdbool.h>

static vhpiHandleT pack, root, info, counter, local, col;
static vhpiHandleT deep_h;
static int calls;

static int get_int(vhpiHandleT h)
{
   vhpiValueT v = { .format = vhpiIntVal };
   vhpi_get_value(h, &v);
   check_error();
   return v.value.intg;
}

static double get_real(vhpiHandleT h)
{
   vhpiValueT v = { .format = vhpiRealVal };
   vhpi_get_value(h, &v);
   check_error();
   return v.value.real;
}

static void get_str(vhpiHandleT h, char *buf, size_t size)
{
   vhpiValueT v = {
      .format    = vhpiStrVal,
      .bufSize   = size,
      .value.str = (vhpiCharT *)buf,
   };
   vhpi_get_value(h, &v);
   check_error();
}

static vhpiHandleT sub(vhpiHandleT parent, const char *name)
{
   vhpiHandleT h = vhpi_handle_by_name(name, parent);
   check_handle(h);
   return h;
}

static vhpiHandleT elem(vhpiHandleT array, int index)
{
   vhpiHandleT h = vhpi_handle_by_index(vhpiIndexedNames, array, index);
   check_handle(h);
   return h;
}

static void check_rec(vhpiHandleT rec, int id, const char *name, int x,
                      bool ok, double r)
{
   vhpiHandleT h = sub(rec, "id");
   fail_unless(get_int(h) == id);
   vhpi_release_handle(h);

   char buf[8];
   h = sub(rec, "name");
   get_str(h, buf, sizeof(buf));
   fail_unless(strcmp(buf, name) == 0);
   vhpi_release_handle(h);

   vhpiHandleT inner = sub(rec, "inner");
   h = sub(inner, "x");
   fail_unless(get_int(h) == x);
   vhpi_release_handle(h);
   h = sub(inner, "ok");
   fail_unless(get_int(h) == ok);
   vhpi_release_handle(h);
   vhpi_release_handle(inner);

   h = sub(rec, "r");
   fail_unless(get_real(h) == r);
   vhpi_release_handle(h);
}

static void trigger(const vhpiCbDataT *cb_data)
{
   vhpiHandleT trig = cb_data->obj;
   const int phase = get_int(trig);
   if (phase == 0)
      return;

   calls++;

   // Elements reached by index
   vhpiHandleT e0 = elem(info, 0), e1 = elem(info, 1);

   // Elements reached by iteration
   vhpiHandleT it = vhpi_iterator(vhpiIndexedNames, info);
   check_handle(it);
   vhpiHandleT i0 = vhpi_scan(it), i1 = vhpi_scan(it);
   fail_unless(vhpi_scan(it) == NULL);
   vhpi_release_handle(it);

   if (phase == 1) {
      check_rec(e0, 3, "zero", 10, false, 0.5);
      check_rec(e1, 7, "abcd", 42, true, 2.25);
      check_rec(i0, 3, "zero", 10, false, 0.5);
      check_rec(i1, 7, "abcd", 42, true, 2.25);
      fail_unless(get_int(counter) == 5);
      fail_unless(get_int(col) == 1);   // green
   }
   else {
      check_rec(e0, 3, "zero", 10, true, 0.5);
      check_rec(e1, 7, "wxyz", -8, true, 2.25);
      check_rec(i1, 7, "wxyz", -8, true, 2.25);
      fail_unless(get_int(counter) == 6);
      fail_unless(get_int(col) == 2);   // blue
   }

   fail_unless(get_int(deep_h) == 77);

   // Architecture shared variable, an array
   vhpiHandleT l2 = elem(local, 1);
   fail_unless(get_int(l2) == (phase == 1 ? 22 : 99));
   vhpi_release_handle(l2);

   vhpi_release_handle(e0);
   vhpi_release_handle(e1);
   vhpi_release_handle(i0);
   vhpi_release_handle(i1);
}

static void start_of_sim(const vhpiCbDataT *cb_data)
{
   root = vhpi_handle(vhpiRootInst, NULL);
   check_handle(root);

   pack = vhpi_handle_by_name(":vhpi36_pkg", NULL);
   check_handle(pack);

   // By name from the package
   info = sub(pack, "info");
   fail_unless(vhpi_get(vhpiKindP, info) == vhpiVarDeclK);
   fail_unless(vhpi_get(vhpiIsSharedP, info));
   check_string(vhpi_get_str(vhpiNameP, info), "INFO");
   counter = sub(pack, "counter");
   fail_unless(vhpi_get(vhpiKindP, counter) == vhpiVarDeclK);

   // By full name
   vhpiHandleT info2 = vhpi_handle_by_name(":vhpi36_pkg:info", NULL);
   check_handle(info2);
   fail_unless(vhpi_compare_handles(info, info2));
   vhpi_release_handle(info2);

   // Iteration over the declarations of the package
   int found = 0;
   vhpiHandleT it = vhpi_iterator(vhpiVarDecls, pack);
   check_handle(it);
   for (vhpiHandleT h = vhpi_scan(it); h != NULL; h = vhpi_scan(it)) {
      fail_unless(vhpi_get(vhpiKindP, h) == vhpiVarDeclK);
      found++;
      vhpi_release_handle(h);
   }
   vhpi_release_handle(it);
   fail_unless(found == 2);

   // Through the design unit of the package
   vhpiHandleT du = vhpi_handle(vhpiDesignUnit, pack);
   check_handle(du);
   vhpi_release_handle(du);

   // Architecture shared variables
   local = sub(root, "local");
   fail_unless(vhpi_get(vhpiKindP, local) == vhpiVarDeclK);
   col = sub(root, "col");

   found = 0;
   it = vhpi_iterator(vhpiVarDecls, root);
   check_handle(it);
   for (vhpiHandleT h = vhpi_scan(it); h != NULL; h = vhpi_scan(it)) {
      found++;
      vhpi_release_handle(h);
   }
   vhpi_release_handle(it);
   fail_unless(found == 2);

   // Shared variable of a nested instance
   vhpiHandleT u = sub(root, "u");
   vhpiHandleT deep = sub(u, "deep");
   fail_unless(vhpi_get(vhpiKindP, deep) == vhpiVarDeclK);
   deep_h = deep;
   vhpi_release_handle(u);

   vhpiHandleT trig = sub(pack, "trig");
   vhpiCbDataT cb = {
      .reason = vhpiCbValueChange,
      .cb_rtn = trigger,
      .obj    = trig,
   };
   check_handle(vhpi_register_cb(&cb, vhpiReturnCb));
}

static void end_of_sim(const vhpiCbDataT *cb_data)
{
   fail_unless(calls == 2);
   vhpi_printf("shared variables read %d times", calls);
}

void vhpi36_startup(void)
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
