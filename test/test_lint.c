//
//  Copyright (C) 2025  Nick Gasson
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


#include "test_util.h"
#include "lint.h"
#include "option.h"

START_TEST(test_option)
{
   lint_parse_option(NULL);
   ck_assert_int_eq(opt_get_int(OPT_LINT), LINT_ALL);

   lint_parse_option("width,latch");
   ck_assert_int_eq(opt_get_int(OPT_LINT), LINT_WIDTH | LINT_LATCH);
   ck_assert(lint_enabled(LINT_WIDTH));
   ck_assert(!lint_enabled(LINT_UNUSED));

   lint_parse_option("all,-width");
   ck_assert_int_eq(opt_get_int(OPT_LINT), LINT_ALL & ~LINT_WIDTH);

   lint_parse_option("-latch");
   ck_assert_int_eq(opt_get_int(OPT_LINT), LINT_ALL & ~LINT_LATCH);

   lint_parse_option("unused,none");
   ck_assert_int_eq(opt_get_int(OPT_LINT), 0);
}
END_TEST

Suite *get_lint_tests(void)
{
   Suite *s = suite_create("lint");

   TCase *tc = nvc_unit_test();
   tcase_add_test(tc, test_option);
   suite_add_tcase(s, tc);

   return s;
}
