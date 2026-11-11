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

#include "test_util.h"
#include "common.h"
#include "diag.h"
#include "lib.h"
#include "option.h"
#include "scan.h"
#include "scandeps.h"

#include <stdlib.h>
#include <string.h>

#define SCAN_FILE(name) TESTDIR "/scan/" name

static unsigned scan(const char *file, const char *expect)
{
   // The JSON is compared as a whole to check the order of the output

   LOCAL_TEXT_BUF tb = tb_new();
   const unsigned errors = scan_file_deps(file, tb);

   if (strcmp(tb_get(tb), expect) != 0) {
      printf("expected: %s", expect);
      printf("actual:   %s", tb_get(tb));
      ck_abort_msg("scan output differs for %s", file);
   }

   return errors;
}

START_TEST(test_vhdl1)
{
   // Units of every kind, with use clauses from two libraries

   set_standard(STD_08);

   const char *expect =
      "{\"file\":\"" SCAN_FILE("vhdl1.vhd") "\",\"language\":\"vhdl\","
      "\"units\":["
      "{\"name\":\"WORK.TOP\",\"kind\":\"entity\",\"ports\":true,"
      "\"instantiates\":[]},"
      "{\"name\":\"WORK.TOP-RTL\",\"kind\":\"architecture\","
      "\"instantiates\":[]},"
      "{\"name\":\"WORK.MYPKG\",\"kind\":\"package\",\"instantiates\":[]},"
      "{\"name\":\"WORK.MYPKG-body\",\"kind\":\"package-body\","
      "\"instantiates\":[]},"
      "{\"name\":\"WORK.CFG\",\"kind\":\"configuration\","
      "\"instantiates\":[]},"
      "{\"name\":\"WORK.MYCTX\",\"kind\":\"context\",\"instantiates\":[]}],"
      "\"requires\":[\"IEEE.STD_LOGIC_1164\",\"UTIL.HELPERS\","
      "\"UTIL.OTHER\"],\"includes\":[]}\n";

   ck_assert_uint_eq(scan(SCAN_FILE("vhdl1.vhd"), expect), 0);
}
END_TEST

START_TEST(test_vhdl2)
{
   // Direct entity and configuration instantiation are also requirements
   // but a component instantiation is not

   set_standard(STD_08);

   const char *expect =
      "{\"file\":\"" SCAN_FILE("vhdl2.vhd") "\",\"language\":\"vhdl\","
      "\"units\":["
      "{\"name\":\"WORK.TB\",\"kind\":\"entity\",\"ports\":false,"
      "\"instantiates\":[]},"
      "{\"name\":\"WORK.TB-SIM\",\"kind\":\"architecture\","
      "\"instantiates\":[\"LEAF\",\"UTIL.CORE\",\"WORK.OTHER\","
      "\"UTIL.CFG2\"]}],"
      "\"requires\":[\"WORK.MYPKG\",\"UTIL.HELPERS\",\"UTIL.CORE\","
      "\"WORK.OTHER\",\"UTIL.CFG2\"],\"includes\":[]}\n";

   ck_assert_uint_eq(scan(SCAN_FILE("vhdl2.vhd"), expect), 0);
}
END_TEST

START_TEST(test_vhdl3)
{
   // The libraries do not exist: only the references are recorded

   const char *expect =
      "{\"file\":\"" SCAN_FILE("vhdl3.vhd") "\",\"language\":\"vhdl\","
      "\"units\":[{\"name\":\"WORK.P\",\"kind\":\"package\","
      "\"instantiates\":[]}],"
      "\"requires\":[\"LIB_A.PKG_A\",\"LIB_B.PKG_B\",\"LIB_C.PKG_C\","
      "\"LIB_A.PKG_D\"],\"includes\":[]}\n";

   ck_assert_uint_eq(scan(SCAN_FILE("vhdl3.vhd"), expect), 0);
}
END_TEST

START_TEST(test_vhdl4)
{
   // A syntax error is reported but the scan continues

   const error_t expect_err[] = {
      { 5, "unexpected end while parsing interface declaration" },
      { -1, NULL }
   };
   expect_errors(expect_err);

   const char *expect =
      "{\"file\":\"" SCAN_FILE("vhdl4.vhd") "\",\"language\":\"vhdl\","
      "\"units\":[{\"name\":\"WORK.E\",\"kind\":\"entity\",\"ports\":true,"
      "\"instantiates\":[]}],"
      "\"requires\":[\"UTIL.HELPERS\"],\"includes\":[],\"errors\":1}\n";

   // Only the syntax error is shown, not the errors which follow from it
   ck_assert_uint_eq(scan(SCAN_FILE("vhdl4.vhd"), expect), 1);
}
END_TEST

START_TEST(test_work_name)
{
   // The name WORK is replaced by the name of the work library

   lib_t other = lib_tmp("mylib");
   lib_set_work(other);

   const char *expect =
      "{\"file\":\"" SCAN_FILE("vhdl2.vhd") "\",\"language\":\"vhdl\","
      "\"units\":["
      "{\"name\":\"MYLIB.TB\",\"kind\":\"entity\",\"ports\":false,"
      "\"instantiates\":[]},"
      "{\"name\":\"MYLIB.TB-SIM\",\"kind\":\"architecture\","
      "\"instantiates\":[\"LEAF\",\"UTIL.CORE\",\"MYLIB.OTHER\","
      "\"UTIL.CFG2\"]}],"
      "\"requires\":[\"MYLIB.MYPKG\",\"UTIL.HELPERS\",\"UTIL.CORE\","
      "\"MYLIB.OTHER\",\"UTIL.CFG2\"],\"includes\":[]}\n";

   set_standard(STD_08);
   ck_assert_uint_eq(scan(SCAN_FILE("vhdl2.vhd"), expect), 0);

   lib_set_work(NULL);
   lib_free(other);
}
END_TEST

START_TEST(test_sv1)
{
   // Packages, interfaces and modules with the files included found
   // with -I

   set_standard(STD_19);
   add_include_dir(SCAN_FILE("inc"));

   const char *expect =
      "{\"file\":\"" SCAN_FILE("sv1.sv") "\",\"language\":\"verilog\","
      "\"units\":["
      "{\"name\":\"WORK.MY_PKG\",\"kind\":\"package\","
      "\"instantiates\":[]},"
      "{\"name\":\"WORK.BUS_IF\",\"kind\":\"interface\",\"ports\":true,"
      "\"instantiates\":[]},"
      "{\"name\":\"WORK.LEAF\",\"kind\":\"module\",\"ports\":true,"
      "\"instantiates\":[]},"
      "{\"name\":\"WORK.TOP\",\"kind\":\"module\",\"ports\":true,"
      "\"instantiates\":[\"leaf\",\"missing_mod\"]},"
      "{\"name\":\"WORK.TB_PROG\",\"kind\":\"program\",\"ports\":false,"
      "\"instantiates\":[]},"
      "{\"name\":\"WORK.INV\",\"kind\":\"primitive\","
      "\"instantiates\":[]}],"
      "\"requires\":[\"?other_pkg\",\"?vif_if\",\"?ext_pkg\"],"
      "\"includes\":[\"" SCAN_FILE("inc/defs.svh") "\","
      "\"" SCAN_FILE("inc/nested.svh") "\"]}\n";

   ck_assert_uint_eq(scan(SCAN_FILE("sv1.sv"), expect), 0);
}
END_TEST

START_TEST(test_sv2)
{
   // An interface used as the type of a port is a requirement but an
   // interface instance or a module instance is not

   set_standard(STD_19);

   const char *expect =
      "{\"file\":\"" SCAN_FILE("sv4.sv") "\",\"language\":\"verilog\","
      "\"units\":[{\"name\":\"WORK.M1\",\"kind\":\"module\","
      "\"ports\":true,\"instantiates\":[\"ext2_if\"]}],"
      "\"requires\":[\"?ext_if\",\"?other_if\"],\"includes\":[]}\n";

   ck_assert_uint_eq(scan(SCAN_FILE("sv4.sv"), expect), 0);
}
END_TEST

START_TEST(test_sv_ifdef_a)
{
   // The first branch of a conditional is taken with -D

   set_standard(STD_19);
   pp_defines_add("USE_A", "");

   const char *expect =
      "{\"file\":\"" SCAN_FILE("sv2.sv") "\",\"language\":\"verilog\","
      "\"units\":[{\"name\":\"WORK.COND\",\"kind\":\"module\","
      "\"ports\":false,\"instantiates\":[]}],"
      "\"requires\":[\"?pkg_a\"],\"includes\":[]}\n";

   ck_assert_uint_eq(scan(SCAN_FILE("sv2.sv"), expect), 0);
}
END_TEST

START_TEST(test_sv_ifdef_b)
{
   // And the second without

   set_standard(STD_19);

   const char *expect =
      "{\"file\":\"" SCAN_FILE("sv2.sv") "\",\"language\":\"verilog\","
      "\"units\":[{\"name\":\"WORK.COND\",\"kind\":\"module\","
      "\"ports\":false,\"instantiates\":[]}],"
      "\"requires\":[\"?pkg_b\"],\"includes\":[]}\n";

   ck_assert_uint_eq(scan(SCAN_FILE("sv2.sv"), expect), 0);
}
END_TEST

START_TEST(test_sv_decls)
{
   // Only a name followed by a port list is an instance: declarations of
   // variables whose type is not available are not

   set_standard(STD_19);

   const char *expect =
      "{\"file\":\"" SCAN_FILE("sv5.sv") "\",\"language\":\"verilog\","
      "\"units\":[{\"name\":\"WORK.DECLS\",\"kind\":\"module\","
      "\"ports\":false,\"instantiates\":[\"counter\"]}],"
      "\"requires\":[\"?unk_pkg\"],\"includes\":[]}\n";

   ck_assert_uint_eq(scan(SCAN_FILE("sv5.sv"), expect), 0);
}
END_TEST

START_TEST(test_sv_syntax)
{
   set_standard(STD_19);

   const error_t expect_err[] = {
      { 1, "unexpected ; while parsing list of port declarations" },
      { -1, NULL }
   };
   expect_errors(expect_err);

   const char *expect =
      "{\"file\":\"" SCAN_FILE("sv3.sv") "\",\"language\":\"verilog\","
      "\"units\":[{\"name\":\"WORK.BAD\",\"kind\":\"module\","
      "\"ports\":true,\"instantiates\":[]}],"
      "\"requires\":[],\"includes\":[],\"errors\":1}\n";

   ck_assert_uint_eq(scan(SCAN_FILE("sv3.sv"), expect), 1);
}
END_TEST

Suite *get_scan_tests(void)
{
   Suite *s = suite_create("scan");

   TCase *tc = nvc_unit_test();
   tcase_add_test(tc, test_vhdl1);
   tcase_add_test(tc, test_vhdl2);
   tcase_add_test(tc, test_vhdl3);
   tcase_add_test(tc, test_vhdl4);
   tcase_add_test(tc, test_work_name);
   tcase_add_test(tc, test_sv1);
   tcase_add_test(tc, test_sv2);
   tcase_add_test(tc, test_sv_ifdef_a);
   tcase_add_test(tc, test_sv_ifdef_b);
   tcase_add_test(tc, test_sv_decls);
   tcase_add_test(tc, test_sv_syntax);
   suite_add_tcase(s, tc);

   return s;
}
