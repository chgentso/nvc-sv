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

static int get_int(vhpiHandleT h)
{
   vhpiValueT v = { .format = vhpiIntVal };
   vhpi_get_value(h, &v);
   check_error();
   return v.value.intg;
}

static void check_elem(vhpiHandleT h, const char *name, const char *full,
                       int value)
{
   check_handle(h);
   check_string(vhpi_get_str(vhpiNameP, h), name);
   check_string(vhpi_get_str(vhpiCaseNameP, h), name);
   check_string(vhpi_get_str(vhpiFullNameP, h), full);
   fail_unless(get_int(h) == value);
   vhpi_release_handle(h);
}

static vhpiHandleT by_index(vhpiHandleT array, int index)
{
   return vhpi_handle_by_index(vhpiIndexedNames, array, index);
}

static vhpiHandleT nth(vhpiHandleT array, int n)
{
   vhpiHandleT it = vhpi_iterator(vhpiIndexedNames, array);
   check_handle(it);
   vhpiHandleT h = NULL;
   for (int i = 0; i <= n; i++)
      h = vhpi_scan(it);
   vhpi_release_handle(it);
   return h;
}

static void start_of_sim(const vhpiCbDataT *cb_data)
{
   vhpiHandleT root = vhpi_handle(vhpiRootInst, NULL);
   check_handle(root);

   vhpiHandleT up = vhpi_handle_by_name("up", root);
   check_handle(up);
   check_elem(by_index(up, 0), "up(16)", ":vhpi37:up(16)", 100);
   check_elem(by_index(up, 3), "up(19)", ":vhpi37:up(19)", 103);
   check_elem(nth(up, 1), "up(17)", ":vhpi37:up(17)", 101);

   vhpiHandleT down = vhpi_handle_by_name("down", root);
   check_handle(down);
   check_elem(by_index(down, 0), "down(47)", ":vhpi37:down(47)", 200);
   check_elem(by_index(down, 3), "down(44)", ":vhpi37:down(44)", 203);
   check_elem(nth(down, 2), "down(45)", ":vhpi37:down(45)", 202);

   vhpiHandleT neg = vhpi_handle_by_name("neg", root);
   check_handle(neg);
   check_elem(by_index(neg, 0), "neg(-4)", ":vhpi37:neg(-4)", 300);
   check_elem(by_index(neg, 3), "neg(-1)", ":vhpi37:neg(-1)", 303);
   check_elem(nth(neg, 2), "neg(-2)", ":vhpi37:neg(-2)", 302);

   vhpiHandleT mat = vhpi_handle_by_name("mat", root);
   check_handle(mat);
   check_elem(by_index(mat, 0), "mat(1,5)", ":vhpi37:mat(1,5)", 10);
   check_elem(by_index(mat, 1), "mat(1,4)", ":vhpi37:mat(1,4)", 11);
   check_elem(by_index(mat, 2), "mat(2,5)", ":vhpi37:mat(2,5)", 12);
   check_elem(nth(mat, 3), "mat(2,4)", ":vhpi37:mat(2,4)", 13);

   vhpi_release_handle(up);
   vhpi_release_handle(down);
   vhpi_release_handle(neg);
   vhpi_release_handle(mat);
   vhpi_release_handle(root);
}

void vhpi37_startup(void)
{
   vhpiCbDataT cb_data = {
      .reason = vhpiCbStartOfSimulation,
      .cb_rtn = start_of_sim,
   };
   VHPI_CHECK(vhpi_register_cb(&cb_data, 0));
}
