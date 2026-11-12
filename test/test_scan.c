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
#include <unistd.h>
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

START_TEST(test_vhdl5)
{
   // Binding to an architecture whose entity is not in the library

   set_standard(STD_08);

   const char *expect =
      "{\"file\":\"" SCAN_FILE("vhdl5.vhd") "\",\"language\":\"vhdl\","
      "\"units\":["
      "{\"name\":\"WORK.LOST_ENTITY-IMPL\",\"kind\":\"architecture\","
      "\"instantiates\":[]},"
      "{\"name\":\"WORK.TOP\",\"kind\":\"entity\",\"ports\":false,"
      "\"instantiates\":[]},"
      "{\"name\":\"WORK.TOP-RTL\",\"kind\":\"architecture\","
      "\"instantiates\":[\"LEAF\"]},"
      "{\"name\":\"WORK.CFG\",\"kind\":\"configuration\","
      "\"instantiates\":[]}],"
      "\"requires\":[\"WORK.LOST_ENTITY\"],\"includes\":[]}\n";

   scan(SCAN_FILE("vhdl5.vhd"), expect);
}
END_TEST

START_TEST(test_vhdl6)
{
   // Named association with a formal that no overload has

   set_standard(STD_08);

   const char *expect =
      "{\"file\":\"" SCAN_FILE("vhdl6.vhd") "\",\"language\":\"vhdl\","
      "\"units\":["
      "{\"name\":\"WORK.VHDL6\",\"kind\":\"package\","
      "\"instantiates\":[]},"
      "{\"name\":\"WORK.VHDL6-body\",\"kind\":\"package-body\","
      "\"instantiates\":[]}],"
      "\"requires\":[\"IEEE.NUMERIC_STD\",\"WORK.MISSING_PKG\"],"
      "\"includes\":[]}\n";

   scan(SCAN_FILE("vhdl6.vhd"), expect);
}
END_TEST

static void scan_symbols_contain(const char *file, const char *const *parts,
                                 unsigned nparts, bool expect);

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

START_TEST(test_m5_vhdl_comments)
{
   // Comments inside a declaration are not part of the text taken from the
   // source, but comment markers in literals are

   set_standard(STD_08);

   static const char *const parts[] = {
      "\"container\":\"WORK.M5VC\",\"type\":\"integer\",\"default\":\"1 + 2\",\"line\":2,",
      "\"container\":\"WORK.M5VC\",\"type\":\"string\",\"default\":\"\\\"x -- y\\\"\","
      "\"line\":4,",
      "\"container\":\"WORK.M5VC\",\"type\":\"character\",\"default\":\"'-'\",\"line\":5,",
      "\"container\":\"WORK.M5VC\",\"type\":\"bit_vector(3 downto 0)\",\"default\":\"\\\"10\\\" & \\\"10\\\"\","
      "\"line\":6,",
   };

   scan_symbols_contain(SCAN_FILE("m5v_comments.vhd"), parts,
                        ARRAY_LEN(parts), true);
}
END_TEST

START_TEST(test_m5_vhdl_fields)
{
   set_standard(STD_08);

   // Direction, type as written and width of ports; type, default and
   // class of generics; the unit and generic map of instances.  The detail
   // text is unchanged.
   static const char *const parts[] = {
      "{\"name\":\"W\",\"kind\":\"generic\",\"detail\":\"POSITIVE\",\"container\":\"WORK.M5V_FIELDS\",\"type\":\"positive\",\"default\":\"8\"",
      "{\"name\":\"NAME\",\"kind\":\"generic\",\"detail\":\"STRING\",\"container\":\"WORK.M5V_FIELDS\",\"type\":\"string\",\"default\":\"\\\"x\\\"\"",
      "{\"name\":\"T\",\"kind\":\"generic\",\"detail\":\"T\",\"container\":\"WORK.M5V_FIELDS\",\"class\":\"type\"",
      "{\"name\":\"P\",\"kind\":\"generic\",\"container\":\"WORK.M5V_FIELDS\",\"class\":\"package\"",
      "{\"name\":\"F\",\"kind\":\"generic\",\"detail\":\"F [INTEGER return INTEGER]\",\"container\":\"WORK.M5V_FIELDS\",\"class\":\"subprogram\"",
      "{\"name\":\"NODEF\",\"kind\":\"generic\",\"detail\":\"INTEGER\",\"container\":\"WORK.M5V_FIELDS\",\"type\":\"integer\"",
      "{\"name\":\"CLK\",\"kind\":\"port\",\"detail\":\"in STD_LOGIC\",\"container\":\"WORK.M5V_FIELDS\",\"direction\":\"input\",\"type\":\"std_logic\",\"width\":1",
      "{\"name\":\"D\",\"kind\":\"port\",\"detail\":\"in STD_LOGIC_VECTOR\",\"container\":\"WORK.M5V_FIELDS\",\"direction\":\"input\",\"type\":\"std_logic_vector(7 downto 0)\",\"width\":8",
      "{\"name\":\"Q\",\"kind\":\"port\",\"detail\":\"out STD_LOGIC_VECTOR\",\"container\":\"WORK.M5V_FIELDS\",\"direction\":\"output\",\"type\":\"std_logic_vector(W - 1 downto 0)\"",
      "{\"name\":\"S\",\"kind\":\"port\",\"detail\":\"out SIGNED\",\"container\":\"WORK.M5V_FIELDS\",\"direction\":\"output\",\"type\":\"signed(15 downto 0)\",\"width\":16",
      "{\"name\":\"U\",\"kind\":\"port\",\"detail\":\"out UNSIGNED\",\"container\":\"WORK.M5V_FIELDS\",\"direction\":\"output\",\"type\":\"unsigned(0 to 3)\",\"width\":4",
      "{\"name\":\"W2\",\"kind\":\"port\",\"detail\":\"in STD_LOGIC_VECTOR\",\"container\":\"WORK.M5V_FIELDS\",\"direction\":\"input\",\"type\":\"std_logic_vector(WIDTH - 1 downto 0)\",\"width\":12",
      "{\"name\":\"BY\",\"kind\":\"port\",\"detail\":\"in BYTE_T\",\"container\":\"WORK.M5V_FIELDS\",\"direction\":\"input\",\"type\":\"byte_t\",\"width\":8",
      "{\"name\":\"FLAG\",\"kind\":\"port\",\"detail\":\"in BOOLEAN\",\"container\":\"WORK.M5V_FIELDS\",\"direction\":\"input\",\"type\":\"boolean\"",
      "{\"name\":\"N\",\"kind\":\"port\",\"detail\":\"out INTEGER\",\"container\":\"WORK.M5V_FIELDS\",\"direction\":\"output\",\"type\":\"integer range 0 to 15\"",
      "{\"name\":\"R\",\"kind\":\"port\",\"detail\":\"in REC_T\",\"container\":\"WORK.M5V_FIELDS\",\"direction\":\"input\",\"type\":\"rec_t\"",
      "{\"name\":\"ANY\",\"kind\":\"port\",\"detail\":\"in STD_LOGIC_VECTOR\",\"container\":\"WORK.M5V_FIELDS\",\"direction\":\"input\",\"type\":\"std_logic_vector\"",
      "{\"name\":\"B\",\"kind\":\"port\",\"detail\":\"inout BIT\",\"container\":\"WORK.M5V_FIELDS\",\"direction\":\"inout\",\"type\":\"bit\",\"width\":1",
      "{\"name\":\"BI\",\"kind\":\"port\",\"detail\":\"buffer STD_ULOGIC\",\"container\":\"WORK.M5V_FIELDS\",\"direction\":\"buffer\",\"type\":\"std_ulogic\",\"width\":1",
      "{\"name\":\"UE\",\"kind\":\"instance\",\"detail\":\"M5V_FIELDS\",\"container\":\"WORK.M5V_FIELDS-RTL\",\"unit\":\"WORK.M5V_FIELDS\",\"generic_map\":[{\"name\":\"NODEF\",\"value\":\"1\"}]",
      "{\"name\":\"UC\",\"kind\":\"instance\",\"detail\":\"M5V_COMP\",\"container\":\"WORK.M5V_FIELDS-RTL\",\"unit\":\"M5V_COMP\"",
   };

   scan_symbols_contain(SCAN_FILE("m5v_fields.vhd"), parts,
                        ARRAY_LEN(parts), true);

   // No width when a bound depends on a generic or the type is not bits
   static const char *const none[] = {
      "\"type\":\"std_logic_vector(W - 1 downto 0)\",\"width\"",
      "\"type\":\"integer range 0 to 15\",\"width\"",
      "\"type\":\"rec_t\",\"width\"",
      "\"type\":\"std_logic_vector\",\"width\"",
   };
   scan_symbols_contain(SCAN_FILE("m5v_fields.vhd"), none, ARRAY_LEN(none),
                        false);
}
END_TEST

START_TEST(test_m5_vhdl_generate)
{
   set_standard(STD_08);

   // For, if (with alternative labels) and case generate statements, their
   // scheme and the instances inside them; an alternative label replaces
   // the label of the statement in the path
   static const char *const parts[] = {
      "{\"name\":\"G1\",\"kind\":\"generate\",\"container\":\"WORK.M5V_GEN-RTL\",\"gen\":\"for\",\"range\":\"i in 0 to W - 1\"",
      "{\"name\":\"U1\",\"kind\":\"instance\",\"detail\":\"M5V_GEN\",\"container\":\"WORK.M5V_GEN-RTL.G1\",\"unit\":\"WORK.M5V_GEN\",\"generic_map\":[{\"name\":\"W\",\"value\":\"4\"}]",
      "{\"name\":\"G2\",\"kind\":\"generate\",\"container\":\"WORK.M5V_GEN-RTL\",\"gen\":\"if\"",
      "{\"name\":\"U3\",\"kind\":\"instance\",\"detail\":\"M5V_GEN\",\"container\":\"WORK.M5V_GEN-RTL.G2\",\"unit\":\"WORK.M5V_GEN\"",
      "{\"name\":\"G3\",\"kind\":\"generate\",\"container\":\"WORK.M5V_GEN-RTL\",\"gen\":\"case\"",
      "{\"name\":\"ALT1\",\"kind\":\"generate\",\"detail\":\"alternative\",\"container\":\"WORK.M5V_GEN-RTL\",\"gen\":\"case\"",
      "{\"name\":\"U4\",\"kind\":\"instance\",\"detail\":\"M5V_GEN\",\"container\":\"WORK.M5V_GEN-RTL.ALT1\",\"unit\":\"WORK.M5V_GEN\"",
      "{\"name\":\"U5\",\"kind\":\"instance\",\"detail\":\"M5V_GEN\",\"container\":\"WORK.M5V_GEN-RTL.G3\",\"unit\":\"WORK.M5V_GEN\"",
      "{\"name\":\"G4\",\"kind\":\"generate\",\"container\":\"WORK.M5V_GEN-RTL\",\"gen\":\"if\"",
      "{\"name\":\"A1\",\"kind\":\"generate\",\"detail\":\"alternative\",\"container\":\"WORK.M5V_GEN-RTL\",\"gen\":\"if\"",
      "{\"name\":\"U7\",\"kind\":\"instance\",\"detail\":\"M5V_GEN\",\"container\":\"WORK.M5V_GEN-RTL.A2\",\"unit\":\"WORK.M5V_GEN\"",
   };

   scan_symbols_contain(SCAN_FILE("m5v_generate.vhd"), parts,
                        ARRAY_LEN(parts), true);
}
END_TEST

START_TEST(test_m5_vhdl_calls)
{
   // A call inside the range of a port does not cut the declaration text

   set_standard(STD_19);

   static const char *const vlog[] = {
      "\"detail\":\"input wire [$clog2(16)-1:0]\"",
      "\"detail\":\"output logic [$clog2(N+1)-1:0][3:0]\"",
      "\"detail\":\"input logic [(N>4 ? 8 : 4)-1:0]\"",
      "\"detail\":\"input wire [3:0]\"",
   };
   scan_symbols_contain(SCAN_FILE("m5v_calls.sv"), vlog, ARRAY_LEN(vlog),
                        true);

   static const char *const vhdl[] = {
      "\"detail\":\"in BIT_VECTOR\",\"container\":\"WORK.M5V_CALLS\","
      "\"direction\":\"input\",\"type\":\"bit_vector(f(N) - 1 downto 0)\"",
      "\"type\":\"bit_vector(f(f(N)) - 1 downto 0)\"",
   };
   scan_symbols_contain(SCAN_FILE("m5v_calls.vhd"), vhdl, ARRAY_LEN(vhdl),
                        true);
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


START_TEST(test_sv_bad_name)
{
   // A module whose name is a syntax error is not a unit and the error
   // marker is not an instance

   set_standard(STD_19);

   const error_t expect_err[] = {
      { 2, "unexpected ( while parsing module declaration, expecting "
        "identifier" },
      { -1, NULL }
   };
   expect_errors(expect_err);

   const char *expect =
      "{\"file\":\"" SCAN_FILE("sv6.sv") "\",\"language\":\"verilog\","
      "\"units\":[{\"name\":\"WORK.AFTER_BAD\",\"kind\":\"module\","
      "\"ports\":true,\"instantiates\":[\"child\"]}],"
      "\"requires\":[],\"includes\":[],\"errors\":1}\n";

   ck_assert_uint_eq(scan(SCAN_FILE("sv6.sv"), expect), 1);
}
END_TEST

START_TEST(test_sv_no_unit)
{
   // A file which gives no unit because of a syntax error must not leave
   // anything behind for the next one

   set_standard(STD_19);

   const error_t expect_err[] = {
      { 1, "unexpected string while parsing description, expecting one of "
        "primitive, module, package, program, class, typedef, import or "
        "parameter" },
      { -1, NULL }
   };
   expect_errors(expect_err);

   const char *expect =
      "{\"file\":\"" SCAN_FILE("sv7.sv") "\",\"language\":\"verilog\","
      "\"units\":[],\"requires\":[],\"includes\":[],\"errors\":1}\n";

   ck_assert_uint_eq(scan(SCAN_FILE("sv7.sv"), expect), 1);

   const char *expect2 =
      "{\"file\":\"" SCAN_FILE("sv8.sv") "\",\"language\":\"verilog\","
      "\"units\":[{\"name\":\"WORK.LEAK_A\",\"kind\":\"module\","
      "\"ports\":false,\"instantiates\":[]}],"
      "\"requires\":[],\"includes\":[]}\n";

   ck_assert_uint_eq(scan(SCAN_FILE("sv8.sv"), expect2), 0);
}
END_TEST

START_TEST(test_sv_macro_isolation)
{
   // Macros defined by one file are not visible in the next

   set_standard(STD_19);

   const char *expect_a =
      "{\"file\":\"" SCAN_FILE("sv8.sv") "\",\"language\":\"verilog\","
      "\"units\":[{\"name\":\"WORK.LEAK_A\",\"kind\":\"module\","
      "\"ports\":false,\"instantiates\":[]}],"
      "\"requires\":[],\"includes\":[]}\n";

   const char *expect_b =
      "{\"file\":\"" SCAN_FILE("sv9.sv") "\",\"language\":\"verilog\","
      "\"units\":[{\"name\":\"WORK.LEAK_B\",\"kind\":\"module\","
      "\"ports\":false,\"instantiates\":[]}],"
      "\"requires\":[],\"includes\":[]}\n";

   ck_assert_uint_eq(scan(SCAN_FILE("sv8.sv"), expect_a), 0);
   ck_assert_uint_eq(scan(SCAN_FILE("sv9.sv"), expect_b), 0);
}
END_TEST

START_TEST(test_sv_clocking_unknown)
{
   // A clocking block may name a variable whose type is not available

   set_standard(STD_19);

   const char *expect =
      "{\"file\":\"" SCAN_FILE("sv10.sv") "\",\"language\":\"verilog\","
      "\"units\":[{\"name\":\"WORK.UNK_IF\",\"kind\":\"interface\","
      "\"ports\":true,\"instantiates\":[]}],"
      "\"requires\":[\"?unk_pkg\"],\"includes\":[]}\n";

   ck_assert_uint_eq(scan(SCAN_FILE("sv10.sv"), expect), 0);
}
END_TEST

START_TEST(test_sv_large_unit)
{
   // Covergroups are expanded to classes so a few of them need more than
   // the default arena size

   set_standard(STD_19);

   const char *expect =
      "{\"file\":\"" SCAN_FILE("sv11.sv") "\",\"language\":\"verilog\","
      "\"units\":[{\"name\":\"WORK.BIG_PKG\",\"kind\":\"package\","
      "\"instantiates\":[]}],\"requires\":[],\"includes\":[]}\n";

   ck_assert_uint_eq(scan(SCAN_FILE("sv11.sv"), expect), 0);
}
END_TEST

START_TEST(test_sv_pending_binds)
{
   // Binds which could not be replayed must not be left for the next file

   set_standard(STD_19);

   const char *expect =
      "{\"file\":\"" SCAN_FILE("sv12.sv") "\",\"language\":\"verilog\","
      "\"units\":[{\"name\":\"WORK.BINDS\",\"kind\":\"module\","
      "\"ports\":false,\"instantiates\":[]}],"
      "\"requires\":[],\"includes\":[]}\n";

   ck_assert_uint_eq(scan(SCAN_FILE("sv12.sv"), expect), 0);

   const char *expect2 =
      "{\"file\":\"" SCAN_FILE("sv8.sv") "\",\"language\":\"verilog\","
      "\"units\":[{\"name\":\"WORK.LEAK_A\",\"kind\":\"module\","
      "\"ports\":false,\"instantiates\":[]}],"
      "\"requires\":[],\"includes\":[]}\n";

   ck_assert_uint_eq(scan(SCAN_FILE("sv8.sv"), expect2), 0);
}
END_TEST

static void scan_symbols_contain(const char *file, const char *const *parts,
                                 unsigned nparts, bool expect)
{
   LOCAL_TEXT_BUF tb = tb_new();
   scan_set_symbols(true);
   const unsigned errors = scan_file_deps(file, tb);
   scan_set_symbols(false);
   ck_assert_uint_eq(errors, 0);

   const char *json = tb_get(tb);
   for (unsigned i = 0; i < nparts; i++) {
      if ((strstr(json, parts[i]) != NULL) != expect) {
         printf("%s: %s\n%s", expect ? "missing" : "unexpected", parts[i],
                json);
         ck_abort_msg("symbols differ for %s", file);
      }
   }
}

START_TEST(test_symbols_vhdl)
{
   set_standard(STD_08);

   static const char *const parts[] = {
      // Units carry the position of their name and the extent
      "\"name\":\"WORK.E\",\"kind\":\"entity\",\"ports\":true,"
      "\"instantiates\":[],\"line\":22,\"col\":8,\"len\":1,\"span\":[22,1,25]}",
      "\"name\":\"WORK.E-RTL\",\"kind\":\"architecture\","
      "\"instantiates\":[\"LEAF\"],\"line\":27,\"col\":14,",
      // Declarations with kind, type and container
      "{\"name\":\"CLK\",\"kind\":\"port\",\"detail\":\"in BIT\","
      "\"container\":\"WORK.E\",\"direction\":\"input\",\"type\":\"bit\","
      "\"width\":1,\"line\":24,\"col\":9,\"len\":3,",
      "{\"name\":\"N\",\"kind\":\"generic\",\"detail\":\"NATURAL\","
      "\"container\":\"WORK.E\",\"type\":\"natural\",\"default\":\"4\",\"line\":23,"
      "\"col\":12,",
      "{\"name\":\"S\",\"kind\":\"signal\",\"detail\":\"INTEGER\","
      "\"container\":\"WORK.E-RTL\",\"line\":28,\"col\":10,",
      "{\"name\":\"REC_T\",\"kind\":\"type\",\"container\":\"WORK.MYPKG\",",
      "{\"name\":\"A\",\"kind\":\"field\",\"detail\":\"INTEGER\","
      "\"container\":\"WORK.MYPKG.REC_T\",\"line\":8,\"col\":5,",
      "{\"name\":\"IDLE\",\"kind\":\"enum-literal\","
      "\"container\":\"WORK.MYPKG.STATE_T\",",
      "{\"name\":\"F\",\"kind\":\"function\",\"detail\":\"F [INTEGER return "
      "INTEGER]\",\"container\":\"WORK.MYPKG\",\"line\":11,\"col\":12,",
      "{\"name\":\"X\",\"kind\":\"param\",\"detail\":\"in INTEGER\","
      "\"container\":\"WORK.MYPKG-body.F\",",
      "{\"name\":\"Y\",\"kind\":\"variable\",\"detail\":\"INTEGER\","
      "\"container\":\"WORK.MYPKG-body.F\",",
      "{\"name\":\"U1\",\"kind\":\"instance\",\"detail\":\"LEAF\","
      "\"container\":\"WORK.E-RTL\",\"unit\":\"LEAF\",\"line\":32,\"col\":3,",
      "{\"name\":\"P\",\"kind\":\"process\",\"container\":\"WORK.E-RTL\","
      "\"line\":33,\"col\":3,\"len\":1,\"span\":[33,3,39]}",
      "{\"name\":\"V\",\"kind\":\"variable\",\"detail\":\"INTEGER\","
      "\"container\":\"WORK.E-RTL.P\",",
      // References resolved while parsing point at the declaration
      "{\"name\":\"S\",\"kind\":\"name\",\"line\":37,\"col\":5,\"len\":1,"
      "\"to\":[28,10]}",
      "{\"name\":\"CLK\",\"kind\":\"name\",\"line\":32,\"col\":27,\"len\":3,"
      "\"to\":[24,9]}",
      "{\"name\":\"A\",\"kind\":\"field\",\"line\":38,\"col\":12,\"len\":1,"
      "\"to\":[8,5]}",
      // Units are named by a hint
      "{\"name\":\"LEAF\",\"kind\":\"component\",\"line\":32,\"col\":7,"
      "\"len\":4,\"hint\":\"LEAF\"}",
      "{\"name\":\"MYPKG\",\"kind\":\"use\",\"line\":3,\"col\":10,\"len\":5,"
      "\"hint\":\"WORK.MYPKG\"}",
      "{\"name\":\"STD_LOGIC_1164\",\"kind\":\"use\",\"line\":2,\"col\":10,"
      "\"len\":14,\"hint\":\"IEEE.STD_LOGIC_1164\"}",
   };

   scan_symbols_contain(SCAN_FILE("sym1.vhd"), parts, ARRAY_LEN(parts), true);

   // Without the option nothing extra is written
   static const char *const none[] = { "\"symbols\"", "\"refs\"", "\"line\"" };
   LOCAL_TEXT_BUF tb = tb_new();
   scan_file_deps(SCAN_FILE("sym1.vhd"), tb);
   for (unsigned i = 0; i < ARRAY_LEN(none); i++)
      ck_assert_ptr_null(strstr(tb_get(tb), none[i]));
}
END_TEST

START_TEST(test_symbols_vlog)
{
   static const char *const parts[] = {
      "\"name\":\"WORK.LEAF\",\"kind\":\"module\",\"ports\":true,"
      "\"instantiates\":[],\"line\":14,\"col\":8,\"len\":4,",
      "{\"name\":\"base\",\"kind\":\"class\","
      "\"container\":\"WORK.P\",",
      "{\"name\":\"f\",\"kind\":\"function\",\"detail\":\"function void\","
      "\"container\":\"WORK.P.base\",\"line\":5,\"col\":19,",
      "{\"name\":\"x\",\"kind\":\"variable\",\"detail\":\"int\","
      "\"container\":\"WORK.P.base\",",
      "{\"name\":\"data\",\"kind\":\"variable\",\"detail\":\"logic [7:0]\","
      "\"container\":\"WORK.BUS_IF\",",
      "{\"name\":\"m\",\"kind\":\"modport\",\"container\":\"WORK.BUS_IF\",",
      "{\"name\":\"W\",\"kind\":\"parameter\",\"detail\":\"parameter\","
      "\"container\":\"WORK.LEAF\",\"default\":\"4\",\"line\":14,\"col\":25,",
      "{\"name\":\"q\",\"kind\":\"port\",\"detail\":\"output logic [W-1:0]\","
      "\"container\":\"WORK.LEAF\",\"direction\":\"output\","
      "\"type\":\"logic [W-1:0]\",\"line\":14,\"col\":71,",
      "{\"name\":\"K\",\"kind\":\"localparam\",\"detail\":\"localparam\","
      "\"container\":\"WORK.LEAF\",",
      "{\"name\":\"v\",\"kind\":\"param\",\"detail\":\"int\","
      "\"container\":\"WORK.LEAF.g\",",
      "{\"name\":\"u\",\"kind\":\"instance\",\"detail\":\"leaf\","
      "\"container\":\"WORK.TOP\",\"unit\":\"leaf\","
      "\"generic_map\":[{\"name\":\"W\",\"value\":\"4\"}],\"line\":34,"
      "\"col\":17,",
      "{\"name\":\"cnt\",\"kind\":\"name\",\"line\":24,\"col\":12,\"len\":3,"
      "\"to\":[15,15]}",
      "{\"name\":\"g\",\"kind\":\"call\",\"line\":25,\"col\":10,\"len\":1,"
      "\"to\":[18,26]}",
      "{\"name\":\"x\",\"kind\":\"name\",\"line\":5,\"col\":29,\"len\":1,"
      "\"to\":[4,9]}",
      "{\"name\":\"leaf\",\"kind\":\"module\",\"line\":34,\"col\":3,\"len\":4,"
      "\"hint\":\"?leaf\"}",
      "{\"name\":\"bus_if\",\"kind\":\"module\",\"line\":33,\"col\":3,"
      "\"len\":6,\"hint\":\"?bus_if\"}",
      "{\"name\":\"P\",\"kind\":\"import\",\"line\":30,\"col\":10,"
      "\"len\":1,\"hint\":\"?P\"}",
   };

   scan_symbols_contain(SCAN_FILE("sym2.sv"), parts, ARRAY_LEN(parts), true);
}
END_TEST

START_TEST(test_bigarena)
{
   // An aggregate too large for the default arena must not be fatal

   set_standard(STD_08);

   char path[] = "/tmp/nvc-scan-XXXXXX";
   int fd = mkstemp(path);
   ck_assert_int_ge(fd, 0);
   FILE *f = fdopen(fd, "w");
   fputs("library ieee; use ieee.std_logic_1164.all;\n"
         "entity bigrom is\n"
         "  generic (w : integer := 1; r : integer := 80000);\n"
         "end entity;\n"
         "architecture a of bigrom is\n"
         "  type mem_t is array (0 to r - 1) of std_logic_vector(w - 1 downto 0);\n"
         "  signal mem : mem_t := (\n", f);
   for (int i = 0; i < 80000; i++)
      fprintf(f, "    %d => \"1\"%s\n", i, i == 79999 ? "" : ",");
   fputs("  );\nbegin\nend architecture;\n", f);
   fclose(f);

   LOCAL_TEXT_BUF tb = tb_new();
   const unsigned errors = scan_file_deps(path, tb);
   unlink(path);

   ck_assert_int_eq(errors, 0);
   ck_assert_ptr_nonnull(strstr(tb_get(tb), "\"name\":\"WORK.BIGROM\""));
}
END_TEST

START_TEST(test_m5_vlog_generate)
{
   static const char *const parts[] = {
      "{\"name\":\"g\",\"kind\":\"generate\",\"container\":\"WORK.TOP\",\"gen\":\"for\""
      ",\"range\":\"i = 0; i < N; i++\"",
      "{\"name\":\"u\",\"kind\":\"instance\",\"detail\":\"leaf\",\"container\":\"WORK."
      "TOP.g\",\"unit\":\"leaf\"",
      "{\"name\":\"h\",\"kind\":\"generate\",\"container\":\"WORK.TOP.g\",\"gen\":\"fo"
      "r\",\"range\":\"j = 0; j < 2; j++\"",
      "{\"name\":\"v\",\"kind\":\"instance\",\"detail\":\"leaf\",\"container\":\"WORK."
      "TOP.g.h\",\"unit\":\"leaf\"",
      "{\"name\":\"yes\",\"kind\":\"generate\",\"container\":\"WORK.TOP\",\"gen\":\"if"
      "\"",
      "{\"name\":\"c2\",\"kind\":\"instance\",\"detail\":\"leaf\",\"container\":\"WORK"
      ".TOP.no\",\"unit\":\"leaf\"",
      "{\"name\":\"genblk3\",\"kind\":\"generate\",\"container\":\"WORK.TOP\",\"gen\""
      ":\"case\"",
      "{\"name\":\"two\",\"kind\":\"generate\",\"container\":\"WORK.TOP\",\"gen\":\"ca"
      "se\"",
      "{\"name\":\"c6\",\"kind\":\"instance\",\"detail\":\"leaf\",\"container\":\"WORK"
      ".TOP.genblk4\",\"unit\":\"leaf\"",
      "{\"name\":\"genblk5\",\"kind\":\"generate\",\"container\":\"WORK.TOP\",\"gen\""
      ":\"if\"",
      "{\"name\":\"c8\",\"kind\":\"instance\",\"detail\":\"leaf\",\"container\":\"WORK"
      ".TOP.genblk6\",\"unit\":\"leaf\"",
   };

   scan_symbols_contain(SCAN_FILE("m5g_generate.sv"), parts, ARRAY_LEN(parts),
                        true);
}
END_TEST

START_TEST(test_m5_vlog_fields)
{
   static const char *const parts[] = {
      "{\"name\":\"W\",\"kind\":\"parameter\",\"detail\":\"parameter int\",\"contain"
      "er\":\"WORK.SUB\",\"type\":\"int\",\"default\":\"8\"",
      "{\"name\":\"INIT\",\"kind\":\"parameter\",\"detail\":\"parameter logic [3:0"
      "]\",\"container\":\"WORK.SUB\",\"type\":\"logic [3:0]\",\"default\":\"4'hA\"",
      "{\"name\":\"T\",\"kind\":\"parameter\",\"detail\":\"parameter type\",\"contai"
      "ner\":\"WORK.SUB\",\"class\":\"type\",\"default\":\"logic [1:0]\"",
      "{\"name\":\"V\",\"kind\":\"parameter\",\"detail\":\"parameter\",\"container\":"
      "\"WORK.SUB\"",
      "{\"name\":\"L\",\"kind\":\"localparam\",\"detail\":\"localparam int\",\"conta"
      "iner\":\"WORK.SUB\",\"type\":\"int\",\"default\":\"W * 2\"",
      "{\"name\":\"d\",\"kind\":\"port\",\"detail\":\"input wire [W-1:0]\",\"contain"
      "er\":\"WORK.SUB\",\"direction\":\"input\",\"type\":\"wire [W-1:0]\"",
      "{\"name\":\"q\",\"kind\":\"port\",\"detail\":\"output logic [7:0]\",\"contain"
      "er\":\"WORK.SUB\",\"direction\":\"output\",\"type\":\"logic [7:0]\",\"unpack"
      "ed\":\"[0:1]\",\"width\":8",
      "{\"name\":\"m\",\"kind\":\"port\",\"detail\":\"input logic [3:0][1:0]\",\"con"
      "tainer\":\"WORK.SUB\",\"direction\":\"input\",\"type\":\"logic [3:0][1:0]\""
      ",\"width\":8",
      "{\"name\":\"t\",\"kind\":\"port\",\"detail\":\"inout tri\",\"container\":\"WORK"
      ".SUB\",\"direction\":\"inout\",\"type\":\"tri\",\"width\":1",
      "{\"name\":\"c\",\"kind\":\"port\",\"detail\":\"input int\",\"container\":\"WORK"
      ".SUB\",\"direction\":\"input\",\"type\":\"int\",\"width\":32",
      "{\"name\":\"n\",\"kind\":\"port\",\"detail\":\"input pk::nib_t\",\"container\""
      ":\"WORK.SUB\",\"direction\":\"input\",\"type\":\"pk::nib_t\"",
      "{\"name\":\"e\",\"kind\":\"port\",\"detail\":\"input\",\"container\":\"WORK.SUB"
      "\",\"direction\":\"input\",\"width\":1",
      "{\"name\":\"f\",\"kind\":\"port\",\"detail\":",
      "\"container\":\"WORK.SUB\",\"direction\":\"input\",",
      "\"type\":\"logic [$clog2(16)-1:0]\",\"width\":4",
      "\"unit\":\"sub\",\"generic_map\":[{\"name\":\"W\",\"value\":\"8\"},"
      "{\"name\":\"INIT\",\"value\":\"4'h5\"}]",
      "{\"name\":\"u2\",\"kind\":\"instance\",\"detail\":\"sub\","
      "\"container\":\"WORK.TOP.yes\",\"unit\":\"sub\","
      "\"generic_map\":[{\"value\":\"4\"}]",
      "{\"name\":\"a\",\"kind\":\"port\",\"detail\":\"input [7:0]\",\"container\":\"WO"
      "RK.OLD\",\"direction\":\"input\",\"type\":\"[7:0]\",\"width\":8",
      "{\"name\":\"b\",\"kind\":\"port\",\"detail\":\"output byte\",\"container\":\"WO"
      "RK.SUB\",\"direction\":\"output\",\"type\":\"byte\",\"width\":8",
      "{\"name\":\"b\",\"kind\":\"port\",\"detail\":\"output reg [15:0]\",\"containe"
      "r\":\"WORK.OLD\",\"direction\":\"output\",\"type\":\"reg [15:0]\",\"width\":1"
      "6",
      "{\"name\":\"g\",\"kind\":\"generate\",\"container\":\"WORK.TOP\","
      "\"gen\":\"for\",\"range\":\"i = 0; i < 4; i = i + 1\"",
      "{\"name\":\"uo\",\"kind\":\"instance\",\"detail\":\"old\",\"container\":\"WORK."
      "TOP\",\"unit\":\"old\"",
   };

   scan_symbols_contain(SCAN_FILE("m5g_fields.sv"), parts, ARRAY_LEN(parts),
                        true);
}
END_TEST

START_TEST(test_m5_vlog_requires)
{
   // Names declared in the file are not units which are required, nor are
   // the later names of a chain of scopes

   LOCAL_TEXT_BUF tb = tb_new();
   ck_assert_uint_eq(scan_file_deps(SCAN_FILE("m5g_requires.sv"), tb), 0);

   const char *json = tb_get(tb);
   ck_assert_ptr_nonnull(strstr(
      json, "\"requires\":[\"?uvm_pkg\",\"?user_pkg\",\"?uvm_report_cb\"]"));
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
   tcase_add_test(tc, test_vhdl5);
   tcase_add_test(tc, test_vhdl6);
   tcase_add_test(tc, test_work_name);
   tcase_add_test(tc, test_m5_vhdl_comments);
   tcase_add_test(tc, test_m5_vhdl_fields);
   tcase_add_test(tc, test_m5_vhdl_generate);
   tcase_add_test(tc, test_m5_vhdl_calls);
   tcase_add_test(tc, test_sv1);
   tcase_add_test(tc, test_sv2);
   tcase_add_test(tc, test_sv_ifdef_a);
   tcase_add_test(tc, test_sv_ifdef_b);
   tcase_add_test(tc, test_sv_decls);
   tcase_add_test(tc, test_sv_syntax);
   tcase_add_test(tc, test_sv_bad_name);
   tcase_add_test(tc, test_sv_no_unit);
   tcase_add_test(tc, test_sv_macro_isolation);
   tcase_add_test(tc, test_sv_clocking_unknown);
   tcase_add_test(tc, test_sv_pending_binds);
   tcase_add_test(tc, test_symbols_vhdl);
   tcase_add_test(tc, test_symbols_vlog);
   tcase_add_test(tc, test_bigarena);
   tcase_add_test(tc, test_m5_vlog_requires);
   tcase_add_test(tc, test_m5_vlog_generate);
   tcase_add_test(tc, test_m5_vlog_fields);
   suite_add_tcase(s, tc);

   // Expanding covergroups is slow with the debug and sanitiser options
   TCase *tc_large = nvc_unit_test();
   tcase_set_timeout(tc_large, 60.0);
   tcase_add_test(tc_large, test_sv_large_unit);
   suite_add_tcase(s, tc_large);

   return s;
}
