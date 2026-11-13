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

static void lint_error_cb(diag_t *d, void *ctx)
{
   if (diag_level(d, NULL) >= DIAG_ERROR)
      lint_note_error(diag_get_loc(d));
}

static void analyse_vhdl_errors(void)
{
   // As analyse_file does with --lint
   lint_errors_reset();
   diag_add_hint_fn(lint_error_cb, NULL);

   while (true) {
      const int base = error_count();
      tree_t t = parse();
      if (t == NULL)
         break;

      if (error_count() == base) {
         lib_put(lib_work(), t);
         simplify_local(t, get_jit(), get_registry(), get_mir());
         bounds_check(t);
         vhdl_lint(t);
      }
      else {
         lib_put_error(lib_work(), t);
         vhdl_lint_partial(t);
      }
   }

   diag_remove_hint_fn(lint_error_cb, NULL);
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

static loc_t make_loc(file_ref_t file, unsigned line, unsigned delta,
                      unsigned col)
{
   loc_t loc = LOC_INVALID;
   loc.file_ref = file;
   loc.first_line = line;
   loc.line_delta = delta;
   loc.first_column = col;
   loc.column_delta = 1;
   return loc;
}

START_TEST(test_regions)
{
   lint_parse_option(NULL);
   lint_errors_reset();

   const file_ref_t f = loc_file_ref("regions.sv", NULL);
   const loc_t unit = make_loc(f, 10, 20, 0);
   const loc_t before = make_loc(f, 12, 2, 4);
   const loc_t inside = make_loc(f, 20, 1, 4);
   const loc_t after = make_loc(f, 25, 2, 4);

   ck_assert(lint_trusted(&unit, &before));
   ck_assert(!lint_span_has_error(&before));

   const loc_t err = make_loc(f, 21, 0, 8);
   lint_note_error(&err);
   ck_assert(lint_span_has_error(&inside));
   ck_assert(!lint_span_has_error(&before));
   ck_assert(!lint_span_has_syntax_error(&inside));
   ck_assert(!lint_trusted(&unit, &inside));
   ck_assert(lint_trusted(&unit, &after));   // Not a syntax error

   const loc_t syn = make_loc(f, 21, 0, 6);
   lint_note_syntax_error(&syn);
   ck_assert(lint_span_has_syntax_error(&inside));
   ck_assert(lint_trusted(&unit, &before));
   ck_assert(lint_after_syntax_error(&unit, &after));
   ck_assert(!lint_trusted(&unit, &after));

   // A syntax error in another unit of the file does not matter
   const loc_t other = make_loc(f, 40, 5, 0);
   ck_assert(!lint_after_syntax_error(&other, &after));

   // An error without a location makes everything untrusted
   lint_errors_reset();
   ck_assert(lint_trusted(&unit, &after));
   lint_note_error(NULL);
   ck_assert(!lint_trusted(&unit, &after));

   lint_errors_reset();
   ck_assert(lint_trusted(&unit, &after));

   // Nothing is recorded when lint is off
   lint_parse_option("none");
   lint_note_error(&err);
   lint_note_syntax_error(&syn);
   ck_assert(lint_trusted(&unit, &after));
}
END_TEST

START_TEST(test_partial_vhdl)
{
   const error_t expect[] = {
      { 16, "no visible declaration for NOSUCH" },
      { 24, "no visible declaration for NOSUCH2" },
      {  8, "signal B is read in this process but is not in its "
        "sensitivity list [lint:sensitivity]" },
      { 11, "latch inferred for signal L: not assigned on every path "
        "through this process [lint:latch]" },
      { 19, "latch inferred for signal M: not assigned on every path "
        "through this process [lint:latch]" },
      { 22, "latch inferred for signal N: not assigned on every path "
        "through this process [lint:latch]" },
      {  5, "lint is partial for this unit: whole-unit rules and 2 "
        "constructs with errors were not checked [lint:partial]" },
      { 34, "signal B is read in this process but is not in its "
        "sensitivity list [lint:sensitivity]" },
      { 32, "signal 'UNUSED_S' is never read [lint:unused]" },
      { -1, NULL }
   };
   expect_errors(expect);

   set_standard(STD_08);
   lint_parse_option(NULL);

   input_from_file(TESTDIR "/lint/vhderr1.vhd");

   analyse_vhdl_errors();

   check_expected_errors();
}
END_TEST

START_TEST(test_partial_syntax)
{
   const error_t expect[] = {
      { 12, "unexpected if while parsing signal assignment statement, "
        "expecting one of **, after, , or ;" },
      {  7, "signal B is read in this process but is not in its "
        "sensitivity list [lint:sensitivity]" },
      {  5, "lint is partial for this unit: whole-unit rules and 1 "
        "construct with errors were not checked [lint:partial]" },
      { 13, "unexpected ; while parsing library unit, expecting one of "
        "entity, configuration, architecture, package or context" },
      { -1, NULL }
   };
   expect_errors(expect);

   set_standard(STD_08);
   lint_parse_option(NULL);

   input_from_file(TESTDIR "/lint/vhderr2.vhd");

   analyse_vhdl_errors();

   check_expected_errors();
}
END_TEST

START_TEST(test_partial_decl)
{
   const error_t expect[] = {
      {  6, "no visible declaration for NOSUCH_TYPE" },
      { 13, "signal B is read in this process but is not in its "
        "sensitivity list [lint:sensitivity]" },
      {  5, "lint is partial for this unit: whole-unit rules and 1 "
        "construct with errors were not checked [lint:partial]" },
      { -1, NULL }
   };
   expect_errors(expect);

   set_standard(STD_08);
   lint_parse_option(NULL);

   input_from_file(TESTDIR "/lint/vhderr3.vhd");

   analyse_vhdl_errors();

   check_expected_errors();
}
END_TEST

START_TEST(test_partial_none)
{
   // A rule that is not enabled stays quiet in a unit with errors
   const error_t expect[] = {
      { 16, "no visible declaration for NOSUCH" },
      { 24, "no visible declaration for NOSUCH2" },
      {  8, "signal B is read in this process but is not in its "
        "sensitivity list [lint:sensitivity]" },
      { 34, "signal B is read in this process but is not in its "
        "sensitivity list [lint:sensitivity]" },
      { 32, "signal 'UNUSED_S' is never read [lint:unused]" },
      { -1, NULL }
   };
   expect_errors(expect);

   set_standard(STD_08);
   lint_parse_option("sensitivity,unused");

   input_from_file(TESTDIR "/lint/vhderr1.vhd");

   analyse_vhdl_errors();

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
   tcase_add_test(tc, test_regions);
   tcase_add_test(tc, test_partial_vhdl);
   tcase_add_test(tc, test_partial_syntax);
   tcase_add_test(tc, test_partial_decl);
   tcase_add_test(tc, test_partial_none);
   suite_add_tcase(s, tc);

   return s;
}
