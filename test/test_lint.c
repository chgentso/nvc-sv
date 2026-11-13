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
#include "common.h"
#include "ident.h"
#include "lib.h"
#include "lint.h"
#include "option.h"
#include "phase.h"
#include "rt/model.h"
#include "scan.h"
#include "tree.h"
#include "vhdl/vhdl-phase.h"
#include "vlog/vlog-node.h"
#include "vlog/vlog-phase.h"

static void analyse_verilog(void)
{
   while (true) {
      vlog_node_t v = vlog_parse();
      if (v == NULL)
         break;

      fail_if(error_count() > 0);

      lib_put_vlog(lib_work(), v);
      vlog_check(v);
      fail_if(error_count() > 0);

      vlog_lint(v);
      vlog_simp(v);
      vlog_fold(v, get_mir(), get_jit());
      fail_if(error_count() > 0);
   }
}

static void analyse_vhdl(void)
{
   while (true) {
      tree_t t = parse();
      if (t == NULL)
         break;

      fail_if(error_count() > 0);

      lib_put(lib_work(), t);
      simplify_local(t, get_jit(), get_registry(), get_mir());
      bounds_check(t);
      fail_if(error_count() > 0);

      vhdl_lint(t);
   }
}

static void elaborate_verilog(const char *name)
{
   object_t *obj = lib_get_generic(lib_work(), ident_new(name), NULL);
   ck_assert_ptr_nonnull(obj);

   rt_model_t *m = model_new(get_jit(), NULL);

   object_t *tops[1] = { obj };
   tree_t e = elab(tops, 1, get_jit(), get_registry(), get_mir(),
                   NULL, NULL, m);
   fail_if(e == NULL);

   model_free(m);
}

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

START_TEST(test_unused_vlog)
{
   const error_t expect[] = {
      { 10, "variable 'r_unused' is never read [lint:unused]" },
      { 11, "variable 'r_undriven' is never assigned [lint:undriven]" },
      { 14, "net 'w_unused' is never read [lint:unused]" },
      { 15, "net 'w_undriven' is never assigned [lint:undriven]" },
      { -1, NULL }
   };
   expect_errors(expect);

   lint_parse_option("unused,undriven");

   input_from_file(TESTDIR "/lint/unused1.sv");

   analyse_verilog();
   elaborate_verilog("WORK.UNUSED1");

   check_expected_errors();
}
END_TEST

START_TEST(test_unused_vlog_off)
{
   // Nothing is reported unless the rules are enabled
   expect_errors((const error_t[]){ { -1, NULL } });

   input_from_file(TESTDIR "/lint/unused1.sv");

   analyse_verilog();
   elaborate_verilog("WORK.UNUSED1");

   check_expected_errors();
}
END_TEST

START_TEST(test_width)
{
   const error_t expect[] = {
      { 15, "assignment truncates 8 bits to 4 bits [lint:width]" },
      { 19, "assignment truncates 8 bits to 4 bits [lint:width]" },
      { 23, "assignment truncates 8 bits to 4 bits [lint:width]" },
      { 26, "assignment truncates 8 bits to 4 bits [lint:width]" },
      { 30, "assignment truncates 32 bits to 4 bits [lint:width]" },
      { 33, "continuous assignment truncates 8 bits to 4 bits "
        "[lint:width]" },
      { 34, "connection to input port 'a' truncates 8 bits to 4 bits "
        "[lint:width]" },
      { 34, "connection to output port 'y' truncates 8 bits to 4 bits "
        "[lint:width]" },
      { -1, NULL }
   };
   expect_errors(expect);

   lint_parse_option("width");

   input_from_file(TESTDIR "/lint/width1.sv");

   analyse_verilog();
   elaborate_verilog("WORK.WIDTH1");

   check_expected_errors();
}
END_TEST

START_TEST(test_latch_vlog)
{
   const error_t expect[] = {
      {  5, "latch inferred for 'y1': not assigned on every path through "
        "this always_comb [lint:latch]" },
      { 12, "latch inferred for 'y3': not assigned on every path through "
        "this always @* block [lint:latch]" },
      { -1, NULL }
   };
   expect_errors(expect);

   lint_parse_option("latch");

   input_from_file(TESTDIR "/lint/latch1.sv");

   analyse_verilog();

   check_expected_errors();
}
END_TEST

START_TEST(test_latch_vhdl)
{
   const error_t expect[] = {
      {  9, "latch inferred for signal L1: not assigned on every path "
        "through this process [lint:latch]" },
      { 39, "latch inferred for signal L5: not assigned on every path "
        "through this process [lint:latch]" },
      { -1, NULL }
   };
   expect_errors(expect);

   set_standard(STD_08);
   lint_parse_option("latch");

   input_from_file(TESTDIR "/lint/latch1.vhd");

   analyse_vhdl();

   check_expected_errors();
}
END_TEST

START_TEST(test_unused_vhdl)
{
   const error_t expect[] = {
      { 20, "signal 'S_UNUSED' is never read [lint:unused]" },
      { 21, "signal 'S_UNDRIVEN' is never assigned [lint:undriven]" },
      { -1, NULL }
   };
   expect_errors(expect);

   set_standard(STD_08);
   lint_parse_option("unused,undriven");

   input_from_file(TESTDIR "/lint/unused1.vhd");

   analyse_vhdl();

   check_expected_errors();
}
END_TEST

START_TEST(test_latch_static)
{
   const error_t expect[] = {
      { 37, "latch inferred for signal S4: not assigned on every path "
        "through this process [lint:latch]" },
      { -1, NULL }
   };
   expect_errors(expect);

   set_standard(STD_08);
   lint_parse_option("latch");

   input_from_file(TESTDIR "/lint/latch2.vhd");

   analyse_vhdl();

   check_expected_errors();
}
END_TEST

START_TEST(test_unused_attribute)
{
   const error_t expect[] = {
      { 10, "signal 'S_UNUSED' is never read [lint:unused]" },
      { -1, NULL }
   };
   expect_errors(expect);

   set_standard(STD_08);
   lint_parse_option("unused,undriven");

   input_from_file(TESTDIR "/lint/unused2.vhd");

   analyse_vhdl();

   check_expected_errors();
}
END_TEST

START_TEST(test_sensitivity)
{
   const error_t expect[] = {
      {  6, "signal B is read in this process but is not in its sensitivity "
         "list [lint:sensitivity]" },
      { 23, "signals C, Y are read in this process but are not in its "
        "sensitivity list [lint:sensitivity]" },
      { -1, NULL }
   };
   expect_errors(expect);

   set_standard(STD_08);
   lint_parse_option("sensitivity");

   input_from_file(TESTDIR "/lint/sens1.vhd");

   analyse_vhdl();

   check_expected_errors();
}
END_TEST

Suite *get_lint_tests(void)
{
   Suite *s = suite_create("lint");

   TCase *tc = nvc_unit_test();
   tcase_add_test(tc, test_option);
   tcase_add_test(tc, test_unused_vlog);
   tcase_add_test(tc, test_unused_vlog_off);
   tcase_add_test(tc, test_width);
   tcase_add_test(tc, test_latch_vlog);
   tcase_add_test(tc, test_latch_vhdl);
   tcase_add_test(tc, test_unused_vhdl);
   tcase_add_test(tc, test_latch_static);
   tcase_add_test(tc, test_sensitivity);
   tcase_add_test(tc, test_unused_attribute);
   suite_add_tcase(s, tc);

   return s;
}
