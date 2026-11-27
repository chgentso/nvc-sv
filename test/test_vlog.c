//
//  Copyright (C) 2022-2026 Nick Gasson
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
#include "object.h"
#include "option.h"
#include "phase.h"
#include "prim.h"
#include "rt/model.h"
#include "scan.h"
#include "test_mir.h"
#include "vlog/vlog-node.h"
#include "vlog/vlog-number.h"
#include "vlog/vlog-phase.h"
#include "vlog/vlog-util.h"
#include "vpi/vpi-model.h"

#include <inttypes.h>

static vlog_node_t do_parse_only(vlog_kind_t kind)
{
   vlog_node_t v = vlog_parse();
   ck_assert_ptr_nonnull(v);
   ck_assert_vlog_kind(v, kind);
   lib_put_vlog(lib_work(), v);
   return v;
}

static vlog_node_t do_parse_check(vlog_kind_t kind)
{
   const int base_errors = error_count();
   vlog_node_t v = do_parse_only(kind);
   ck_assert_int_eq(error_count(), base_errors);
   vlog_check(v);
   return v;
}

static vlog_node_t do_parse_check_fold(vlog_kind_t kind)
{
   const int base_errors = error_count();
   vlog_node_t v = do_parse_check(kind);
   ck_assert_int_eq(error_count(), base_errors);
   vlog_simp(v);
   vlog_fold(v, get_mir(), get_jit());
   return v;
}

START_TEST(test_dff)
{
   input_from_file(TESTDIR "/vlog/dff.v");

   vlog_node_t m = vlog_parse();
   fail_if(m == NULL);
   fail_unless(vlog_kind(m) == V_MODULE);
   fail_unless(vlog_stmts(m) == 1);
   fail_unless(vlog_ports(m) == 4);
   fail_unless(vlog_decls(m) == 4);

   vlog_node_t p0 = vlog_port(m, 0);
   fail_unless(vlog_kind(p0) == V_REF);
   fail_unless(vlog_ident(p0) == ident_new("d"));

   vlog_node_t p1 = vlog_port(m, 1);
   fail_unless(vlog_kind(p1) == V_REF);
   fail_unless(vlog_ident(p1) == ident_new("clk"));

   vlog_node_t p3 = vlog_port(m, 3);
   fail_unless(vlog_kind(p3) == V_REF);
   fail_unless(vlog_ident(p3) == ident_new("q"));

   vlog_node_t d0 = vlog_decl(m, 0);
   fail_unless(vlog_kind(d0) == V_PORT_DECL);
   fail_unless(vlog_subkind(d0) == V_PORT_INPUT);
   fail_unless(vlog_ident(d0) == ident_new("d"));
   fail_unless(vlog_ident2(d0) == ident_new("D"));

   vlog_node_t d1 = vlog_decl(m, 1);
   fail_unless(vlog_kind(d1) == V_PORT_DECL);
   fail_unless(vlog_subkind(d1) == V_PORT_INPUT);
   fail_unless(vlog_ident(d1) == ident_new("clk"));
   fail_unless(vlog_ident2(d1) == ident_new("CLK"));

   vlog_node_t d3 = vlog_decl(m, 3);
   fail_unless(vlog_kind(d3) == V_PORT_DECL);
   fail_unless(vlog_subkind(d3) == V_PORT_OUTPUT);
   fail_unless(vlog_ident(d3) == ident_new("q"));
   fail_unless(vlog_ident2(d3) == ident_new("Q"));

   vlog_node_t a = vlog_stmt(m, 0);
   fail_unless(vlog_kind(a) == V_ALWAYS);
   fail_unless(vlog_stmts(a) == 1);

   vlog_node_t e = vlog_stmt(a, 0);
   fail_unless(vlog_kind(e) == V_TIMING);
   fail_unless(vlog_stmts(e) == 1);

   vlog_node_t ctrl = vlog_value(e);
   fail_unless(vlog_kind(ctrl) == V_EVENT_CONTROL);
   fail_unless(vlog_params(ctrl) == 1);

   vlog_node_t v = vlog_param(ctrl, 0);
   fail_unless(vlog_kind(v) == V_EVENT);
   fail_unless(vlog_subkind(v) == V_EVENT_POSEDGE);

   vlog_node_t clk = vlog_value(v);
   fail_unless(vlog_kind(clk) == V_REF);
   fail_unless(vlog_ident(clk) == ident_new("clk"));

   vlog_node_t s = vlog_stmt(e, 0);
   fail_unless(vlog_kind(s) == V_NBASSIGN);

   vlog_node_t t = vlog_target(s);
   fail_unless(vlog_kind(t) == V_REF);
   fail_unless(vlog_ident(t) == ident_new("q"));

   fail_unless(vlog_parse() == NULL);

   fail_if_errors();
}
END_TEST

START_TEST(test_simple_sem)
{
   const error_t expect[] = {
      {  7, "duplicate declaration of 'x'" },
      { 13, "no visible declaration for 'qq'" },
      { 13, "no visible declaration for 'dd'" },
      { 20, "'q' cannot be assigned in a procedural block" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/simple_sem.v");

   for (int i = 0; i < 3; i++)
      do_parse_only(V_MODULE);

   do_parse_check(V_MODULE);

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_ports1)
{
   input_from_file(TESTDIR "/vlog/ports1.v");

   const error_t expect[] = {
      {  4, "duplicate declaration of 'y'" },
      {  0, "duplicate declaration" },
      {  0, "'y' was previously declared here" },
      { 11, "no visible declaration for 'y'" },
      { 19, "duplicate declaration of 'x'" },
      { 22, "duplicate declaration of 'y'" },
      { 41, "'o' does not appear in module port list" },
      { 45, "'i1' does not appear in module port list" },
      { -1, NULL }
   };
   expect_errors(expect);

   for (int i = 0; i < 8; i++) {
      vlog_node_t m = vlog_parse();
      fail_if(m == NULL);
      fail_unless(vlog_kind(m) == V_MODULE);
   }

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_ports2)
{
   input_from_file(TESTDIR "/vlog/ports2.v");

   const error_t expect[] = {
      {  5, "'o3' cannot be assigned in a procedural block" },
      { 17, "inconsistent dimensions for 'y'" },
      { 21, "missing port declaration for 'zz'" },
      { 29, "'i0' cannot be driven by continuous assignment" },
      { -1, NULL }
   };
   expect_errors(expect);

   for (int i = 0; i < 6; i++)
      do_parse_check(V_MODULE);

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_ports3)
{
   input_from_file(TESTDIR "/vlog/ports3.v");

   const error_t expect[] = {
      { 22, "inconsistent dimensions for 'o'" },
      { 49, "inconsistent dimensions for 'o'" },
      { -1, NULL }
   };
   expect_errors(expect);

   for (int i = 0; i < 5; i++) {
      vlog_node_t m = do_parse_check(V_MODULE);
      vlog_fold(m, get_mir(), get_jit());
   }

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_parse1)
{
   input_from_file(TESTDIR "/vlog/parse1.v");

   set_default_keywords(VLOG_1800_2023);

   vlog_node_t m = vlog_parse();
   fail_if(m == NULL);
   fail_unless(vlog_kind(m) == V_MODULE);
   ck_assert_int_eq(vlog_stmts(m), 47);
   ck_assert_int_eq(vlog_ports(m), 0);
   ck_assert_int_eq(vlog_decls(m), 45);

   vlog_node_t x = vlog_decl(m, 0);
   fail_unless(vlog_kind(x) == V_NET_DECL);
   fail_unless(vlog_ranges(x) == 0);
   fail_unless(vlog_subkind(x) == V_NET_WIRE);

   vlog_node_t xdt = vlog_type(x);
   fail_unless(vlog_kind(xdt) == V_DATA_TYPE);
   fail_unless(vlog_subkind(xdt) == DT_IMPLICIT);
   fail_unless(vlog_ranges(xdt) == 1);

   vlog_node_t xd0 = vlog_range(xdt, 0);
   fail_unless(vlog_kind(xd0) == V_DIMENSION);
   fail_unless(vlog_subkind(xd0) == V_DIM_PACKED);

   vlog_node_t s1 = vlog_stmt(m, 1);
   fail_unless(vlog_kind(s1) == V_ALWAYS);
   fail_unless(vlog_stmts(s1) == 1);

   vlog_node_t s1s0 = vlog_stmt(s1, 0);
   fail_unless(vlog_kind(s1s0) == V_SEQ_BLOCK);
   fail_unless(vlog_ident(s1s0) == ident_new("foo"));

   vlog_node_t s1s0s0 = vlog_stmt(s1s0, 0);
   fail_unless(vlog_kind(s1s0s0) == V_SYS_TCALL);
   fail_unless(vlog_ident(s1s0s0) == ident_new("$display"));
   fail_unless(vlog_params(s1s0s0) == 1);

   vlog_node_t hello = vlog_param(s1s0s0, 0);
   fail_unless(vlog_kind(hello) == V_STRING);

   number_t n1 = vlog_number(hello);
   ck_assert_int_eq(number_byte(n1, 0), 'o');
   ck_assert_int_eq(number_byte(n1, 1), 'l');
   ck_assert_int_eq(number_byte(n1, 2), 'l');
   ck_assert_int_eq(number_byte(n1, 3), 'e');
   ck_assert_int_eq(number_byte(n1, 4), 'h');

   vlog_node_t s1s0s1 = vlog_stmt(s1s0, 1);
   fail_unless(vlog_kind(s1s0s1) == V_SYS_TCALL);
   fail_unless(vlog_ident(s1s0s1) == ident_new("$finish"));

   vlog_node_t s1s0s2 = vlog_stmt(s1s0, 2);
   fail_unless(vlog_kind(s1s0s2) == V_IF);
   fail_unless(vlog_conds(s1s0s2) == 1);

   vlog_node_t s1s0s3 = vlog_stmt(s1s0, 3);
   fail_unless(vlog_kind(s1s0s3) == V_IF);
   fail_unless(vlog_conds(s1s0s3) == 1);

   vlog_node_t s1s0s4 = vlog_stmt(s1s0, 4);
   fail_unless(vlog_kind(s1s0s4) == V_IF);
   fail_unless(vlog_conds(s1s0s4) == 3);

   vlog_node_t s2 = vlog_stmt(m, 2);
   fail_unless(vlog_kind(s2) == V_ASSIGN);

   vlog_node_t s2or = vlog_value(s2);
   fail_unless(vlog_kind(s2or) == V_BINARY);
   fail_unless(vlog_subkind(s2or) == V_BINARY_OR);

   vlog_node_t s1s0s5 = vlog_stmt(s1s0, 5);
   fail_unless(vlog_kind(s1s0s5) == V_BASSIGN);

   vlog_node_t s3 = vlog_stmt(m, 3);
   fail_unless(vlog_kind(s3) == V_GATE_INST);
   ck_assert_int_eq(vlog_subkind(s3), V_GATE_PULLDOWN);
   ck_assert_int_eq(vlog_params(s3), 1);
   fail_unless(vlog_ident(s3) == ident_new("p1"));
   fail_unless(vlog_kind(vlog_target(s3)) == V_REF);

   vlog_node_t s4 = vlog_stmt(m, 4);
   fail_unless(vlog_kind(s4) == V_GATE_INST);
   ck_assert_int_eq(vlog_subkind(s4), V_GATE_PULLDOWN);
   ck_assert_int_eq(vlog_params(s4), 1);
   fail_unless(vlog_ident(s4) == ident_new("p2"));
   fail_unless(vlog_kind(vlog_target(s4)) == V_REF);

   vlog_node_t s5 = vlog_stmt(m, 5);
   fail_unless(vlog_kind(s5) == V_GATE_INST);
   ck_assert_int_eq(vlog_subkind(s5), V_GATE_PULLUP);
   ck_assert_int_eq(vlog_params(s5), 1);
   fail_unless(vlog_ident(s5) == ident_new("p3"));
   fail_unless(vlog_kind(vlog_target(s5)) == V_REF);

   vlog_node_t s6 = vlog_stmt(m, 6);
   fail_unless(vlog_kind(s6) == V_GATE_INST);
   ck_assert_int_eq(vlog_subkind(s6), V_GATE_PULLUP);
   ck_assert_int_eq(vlog_params(s6), 1);
   fail_unless(vlog_ident(s6) == ident_new("p4"));
   fail_unless(vlog_kind(vlog_target(s6)) == V_REF);

   vlog_node_t s7 = vlog_stmt(m, 7);
   fail_unless(vlog_kind(s7) == V_GATE_INST);
   ck_assert_int_eq(vlog_subkind(s7), V_GATE_PULLUP);
   ck_assert_int_eq(vlog_params(s7), 1);
   fail_unless(vlog_has_ident(s7));
   fail_unless(vlog_kind(vlog_target(s7)) == V_REF);

   vlog_node_t s8 = vlog_stmt(m, 8);
   fail_unless(vlog_kind(s8) == V_ALWAYS);

   vlog_node_t s8ctrl = vlog_value(vlog_stmt(s8, 0));
   fail_unless(vlog_kind(s8ctrl) == V_EVENT_CONTROL);
   fail_unless(vlog_params(s8ctrl) == 3);

   vlog_node_t s9 = vlog_stmt(m, 9);
   fail_unless(vlog_kind(s9) == V_INITIAL);
   vlog_node_t s9b = vlog_stmt(s9, 0);
   fail_unless(vlog_kind(s9b) == V_SEQ_BLOCK);
   fail_unless(vlog_kind(vlog_stmt(s9b, 0)) == V_WHILE);
   fail_unless(vlog_kind(vlog_stmt(s9b, 1)) == V_REPEAT);
   fail_unless(vlog_kind(vlog_stmt(s9b, 2)) == V_DO_WHILE);

   vlog_node_t d15 = vlog_decl(m, 15);
   fail_unless(vlog_kind(d15) == V_LOCALPARAM);

   vlog_node_t d17 = vlog_decl(m, 17);
   fail_unless(vlog_kind(d17) == V_VAR_DECL);
   ck_assert_int_eq(vlog_ranges(d17), 1);
   fail_unless(vlog_subkind(vlog_range(d17, 0)) == V_DIM_UNPACKED);

   vlog_node_t d20 = vlog_decl(m, 20);
   fail_unless(vlog_kind(d20) == V_VAR_DECL);
   vlog_node_t d20t = vlog_type(d20);
   fail_unless(vlog_subkind(d20t) == DT_EVENT);

   vlog_node_t s10 = vlog_stmt(m, 16);
   fail_unless(vlog_kind(s10) == V_ALWAYS);
   fail_unless(vlog_subkind(s10) == V_ALWAYS_COMB);

   vlog_node_t s11 = vlog_stmt(m, 17);
   fail_unless(vlog_kind(s11) == V_ALWAYS);
   fail_unless(vlog_subkind(s11) == V_ALWAYS_FF);

   vlog_node_t s12 = vlog_stmt(m, 18);
   fail_unless(vlog_kind(s12) == V_ALWAYS);
   fail_unless(vlog_subkind(s12) == V_ALWAYS_LATCH);

   vlog_node_t s19 = vlog_stmt(m, 19);
   fail_unless(vlog_kind(s19) == V_ALWAYS);
   vlog_node_t s19b = vlog_stmt(s19, 0);
   vlog_node_t et = vlog_stmt(s19b, 0);
   fail_unless(vlog_kind(et) == V_EVENT_TRIGGER);

   vlog_node_t d30 = vlog_decl(m, 30);
   fail_unless(vlog_kind(d30) == V_NET_DECL);
   fail_unless(vlog_ident(d30) == ident_new("wire[x]net;"));

   fail_unless(vlog_parse() == NULL);

   fail_if_errors();
}
END_TEST

START_TEST(test_number1)
{
   number_t x = number_new("1'b1", NULL);
   ck_assert_int_eq(x.common.tag, TAG_BIGNUM);
   fail_unless(number_is_defined(x));
   fail_if(number_signed(x));
   ck_assert_int_eq(number_width(x), 1);
   ck_assert_int_eq(number_integer(x), 1);

   number_t y = number_new("5'b101", NULL);
   ck_assert_int_eq(y.common.tag, TAG_BIGNUM);
   fail_unless(number_is_defined(y));
   fail_if(number_signed(y));
   ck_assert_int_eq(number_width(y), 5);
   ck_assert_int_eq(number_integer(y), 5);

   number_t z = number_new("5'b1xx", NULL);
   ck_assert_int_eq(z.common.tag, TAG_BIGNUM);
   fail_if(number_is_defined(z));
   fail_if(number_signed(z));
   ck_assert_int_eq(number_width(z), 5);

   number_t n1 = number_new("1'bx", NULL);
   ck_assert_int_eq(n1.common.tag, TAG_BIGNUM);
   fail_if(number_is_defined(n1));
   fail_if(number_signed(n1));
   ck_assert_int_eq(number_width(n1), 1);

   number_t n2 = number_new("456", NULL);
   ck_assert_int_eq(n2.common.tag, TAG_BIGNUM);
   fail_unless(number_is_defined(n2));
   fail_unless(number_signed(n2));
   ck_assert_int_eq(number_width(n2), 32);
}
END_TEST

START_TEST(test_number2)
{
   struct {
      const char *input;
      unsigned    width;
      int64_t     ival;
      const char *string;
   } cases[] = {
      { "1'b1",          1,   1,           "1'b1"      },
      { "5'b100",        5,   4,           "5'd4"      },
      { "1",             32,  1,           "1"         },
      { "42",            32,  42,          "42"        },
      { "251251",        32,  251251,      "251251"    },
      { "'shffffFFff",   32,  -1,          "-1"        },
      { "33'h100000000", 33,  0x100000000,
        "33'b100000000000000000000000000000000"        },
      { "64'b0",         64,  0,           "64'b0"     },
      { "64'b101",       64,  5,           "64'b101"   },
      { "8'b00_01",      8,   1,           "8'd1"      },
      { "128'bx",        128, 0,           "128'bx"    },
      { "4'h12",         4,   2,           "4'd2"      },
      { "1'b110",        1,   0,           "1'b0"      },
      { "1_000",         32,  1000,        "1000"      },
      { "6'o02",         6,   2,           "6'd2"      },
      { "4'o_1",         4,   1,           "4'd1"      },
      { "2_0'b0",        20,  0,           "20'd0"     },
   };

   const error_t expect[] = {
      { LINE_INVALID, "excess digits in hex constant 4'h12" },
      { LINE_INVALID, "excess digits in binary constant 1'b110" },
      { LINE_INVALID, "number cannot start with an underscore" },
      { -1, NULL }
   };
   expect_errors(expect);

   LOCAL_TEXT_BUF tb = tb_new();

   for (int i = 0; i < ARRAY_LEN(cases); i++) {
      number_t n = number_new(cases[i].input, NULL);
      ck_assert_int_eq(number_width(n), cases[i].width);

      if (cases[i].width <= 64)
         ck_assert_int_eq(number_integer(n), cases[i].ival);

      tb_rewind(tb);
      number_print(n, tb);
      ck_assert_str_eq(tb_get(tb), cases[i].string);
   }

   check_expected_errors();
}
END_TEST

START_TEST(test_pp1)
{
   input_from_file(TESTDIR "/vlog/pp1.v");

   LOCAL_TEXT_BUF tb = tb_new();
   vlog_preprocess(tb, false);

   ck_assert_str_eq(
      tb_get(tb),
      "\n"
      "\n"
      " // comment\n"
      "bar = 1\n"
      "   // Another comment\n"
      "pass\n"
      "\n"
      "\n"
      "\n"
      "\n"
      "\n"
      "\n"
      "pass\n"
      "\n"
      "\""TESTDIR "/vlog/pp1.v""\"\n"
      "16\n"
      "\n"
      "\n"
      "\n"
      "\n"
      "\n"
      "\n");
}
END_TEST

START_TEST(test_empty1)
{
   input_from_buffer("", 0, FILE_INVALID, SOURCE_VERILOG);

   fail_unless(vlog_parse() == NULL);

   freeze_global_arena();   // Should not crash

   fail_if_errors();
}
END_TEST

START_TEST(test_timescale1)
{
   input_from_file(TESTDIR "/vlog/timescale1.v");

   const error_t expect[] = {
      {  2, "invalid order of magnitude in `timescale directive" },
      {  3, "the value in a `timescale directive must be an integer" },
      {  3, "unexpected identifier while parsing timescale compiler "
         "directive, expecting one of unsigned number or time literal" },
      { -1, NULL }
   };
   expect_errors(expect);

   vlog_node_t m = vlog_parse();
   fail_unless(m == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_timescale2)
{
   input_from_file(TESTDIR "/vlog/timescale2.v");

   const error_t expect[] = {
      {  2, "the time precision of a `timescale directive cannot be longer "
         "than the time unit" },
      { -1, NULL }
   };
   expect_errors(expect);

   vlog_node_t m = vlog_parse();
   fail_if(m == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_modport1)
{
   input_from_file(TESTDIR "/vlog/modport1.v");

   set_default_keywords(VLOG_1800_2023);

   const error_t expect[] = {
      {  7, "output modport expression for 'e' must be a net or variable" },
      { 13, "interface item 'x' is not in modport 'mi'" },
      { 14, "interface item 'g' is not in modport 'mi'" },
      { 12, "cannot assign a which is an input of modport mi" },
      { 22, "virtual interface element 'x' not found in modport 'mi'" },
      { 21, "cannot assign a which is an input of modport mi" },
      { -1, NULL }
   };
   expect_errors(expect);

   for (int i = 0; i < 3; i++) {
      vlog_node_t m = vlog_parse();
      fail_if(m == NULL);
      vlog_check(m);
   }

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_gate1)
{
   input_from_file(TESTDIR "/vlog/gate1.v");

   const error_t expect[] = {
      {  6, "duplicate declaration of 'p1'" },
      { -1, NULL }
   };
   expect_errors(expect);

   vlog_node_t m = vlog_parse();
   fail_if(m == NULL);
   fail_unless(vlog_kind(m) == V_MODULE);

   vlog_check(m);

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_pp2)
{
   input_from_file(TESTDIR "/vlog/pp2.v");

   const error_t expect[] = {
      {  1, "unexpected `else while parsing block of text" },
      {  3, "unexpected `endif while parsing block of text" },
      {  5, "unexpected `elsif while parsing block of text" },
      {  7, "unexpected comment while parsing ifdef condition" },
      {  9, "unexpected comment while parsing ifdef condition" },
      { 13, "unexpected comment while parsing ifdef condition" },
      { 21, "unexpected end of file while parsing conditional compilation" },
      { -1, NULL }
   };
   expect_errors(expect);

   LOCAL_TEXT_BUF tb = tb_new();
   vlog_preprocess(tb, false);

   check_expected_errors();
}
END_TEST

START_TEST(test_specify1)
{
   input_from_file(TESTDIR "/vlog/specify1.v");

   vlog_node_t m = vlog_parse();
   fail_if(m == NULL);
   fail_unless(vlog_kind(m) == V_MODULE);

   vlog_check(m);

   fail_unless(vlog_parse() == NULL);

   fail_if_errors();
}
END_TEST

START_TEST(test_udp1)
{
   input_from_file(TESTDIR "/vlog/udp1.v");

   const error_t expect[] = {
      { 13, "no visible declaration for 'z'" },
      { 13, "the first port of a primitive must be an output" },
      { 13, "all ports of a primitive except the first must be inputs" },
      { 25, "expected 3 symbols in UDP table entry but have 2" },
      { 58, "a sequential input list may have at most one edge indicator" },
      { 63, "unexpected identifier while parsing integral number, " },
      { -1, NULL }
   };
   expect_errors(expect);

   for (int i = 0; i < 6; i++) {
      vlog_node_t udp = vlog_parse();
      fail_if(udp == NULL);
      fail_unless(vlog_kind(udp) == V_PRIMITIVE);

      vlog_check(udp);
   }

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_implicit1)
{
   input_from_file(TESTDIR "/vlog/implicit1.v");

   vlog_node_t m = vlog_parse();
   fail_if(m == NULL);
   fail_unless(vlog_kind(m) == V_MODULE);

   vlog_check(m);

   ck_assert_int_eq(vlog_decls(m), 6);

   fail_unless(vlog_parse() == NULL);

   fail_if_errors();
}
END_TEST

START_TEST(test_struct1)
{
   input_from_file(TESTDIR "/vlog/struct1.sv");

   const error_t expect[] = {
      { 14, "duplicate declaration of 'a'" },
      { 19, "struct has no field named 'c'" },
      { 20, "prefix is not a struct, union, or class" },
      { -1, NULL }
   };
   expect_errors(expect);

   vlog_node_t m = do_parse_only(V_MODULE);
   ck_assert_int_eq(vlog_decls(m), 3);

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_struct2)
{
   input_from_file(TESTDIR "/vlog/struct2.sv");

   const error_t expect[] = {
      { 14, "'t_pair' cannot be assigned in a procedural block" },
      { 15, "value of type 'integral' is not compatible with 'struct'" },
      { -1, NULL }
   };
   expect_errors(expect);

   do_parse_check(V_MODULE);

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_enum1)
{
   input_from_file(TESTDIR "/vlog/enum1.sv");

   const error_t expect[] = {
      {  6, "'a' is not a data type" },
      { 20, "'x' has no method named 'invalid'" },
      { -1, NULL }
   };
   expect_errors(expect);

   vlog_node_t m = do_parse_only(V_MODULE);
   ck_assert_int_eq(vlog_decls(m), 9);

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_enum2)
{
   input_from_file(TESTDIR "/vlog/enum2.sv");

   const error_t expect[] = {
      {  8, "cannot reference variable 'zz' in constant expression" },
      { 10, "enum names 'aa' and 'bb' both have value 1" },
      { -1, NULL }
   };
   expect_errors(expect);

   do_parse_check_fold(V_MODULE);

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_union1)
{
   input_from_file(TESTDIR "/vlog/union1.sv");

   vlog_node_t m = do_parse_check(V_MODULE);
   ck_assert_int_eq(vlog_decls(m), 4);

   fail_unless(vlog_parse() == NULL);

   fail_if_errors();
}
END_TEST

START_TEST(test_param1)
{
   input_from_file(TESTDIR "/vlog/param1.v");

   set_default_keywords(VLOG_1800_2023);

   const error_t expect[] = {
      {  4, "duplicate declaration of 'p1'" },
      { -1, NULL }
   };
   expect_errors(expect);

   for (int i = 0; i < 2; i++) {
      vlog_node_t m = vlog_parse();
      fail_if(m == NULL);
      fail_unless(vlog_kind(m) == V_MODULE);
   }

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_param2)
{
   input_from_file(TESTDIR "/vlog/param2.v");

   set_default_keywords(VLOG_1800_2023);

   const error_t expect[] = {
      { 13, "duplicate declaration" },
      { 18, "local parameter declaration must have a default value" },
      { -1, NULL }
   };
   expect_errors(expect);

   vlog_node_t m1 = vlog_parse();
   fail_if(m1 == NULL);
   fail_unless(vlog_kind(m1) == V_MODULE);

   vlog_check(m1);

   vlog_node_t m2 = vlog_parse();
   fail_if(m2 == NULL);
   fail_unless(vlog_kind(m2) == V_MODULE);

   vlog_node_t p2 = vlog_decl(m2, 1);
   fail_unless(vlog_kind(p2) == V_LOCALPARAM);

   vlog_check(m2);

   vlog_node_t m3 = vlog_parse();
   fail_if(m3 == NULL);
   fail_unless(vlog_kind(m3) == V_MODULE);

   vlog_node_t p3 = vlog_decl(m3, 0);
   fail_unless(vlog_kind(p3) == V_LOCALPARAM);
   vlog_node_t p3v = vlog_value(p3);
   fail_unless(vlog_kind(p3v) == V_MIN_TYP_MAX);

   vlog_node_t min = vlog_left(p3v);
   fail_unless(number_integer(vlog_number(min)) == 5);

   vlog_node_t typ = vlog_value(p3v);
   fail_unless(number_integer(vlog_number(typ)) == 6);

   vlog_node_t max = vlog_right(p3v);
   fail_unless(number_integer(vlog_number(max)) == 7);

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_pp3)
{
   input_from_file(TESTDIR "/vlog/pp3.v");

   const error_t expect[] = {
      {  3, "macro 'FOO' undefined" },
      { -1, NULL }
   };
   expect_errors(expect);

   LOCAL_TEXT_BUF tb = tb_new();
   vlog_preprocess(tb, false);

   ck_assert_str_eq(
      tb_get(tb),
      "\n"
      "\n"
      "    // Warning\n");

   check_expected_errors();
}
END_TEST

START_TEST(test_concat1)
{
   input_from_file(TESTDIR "/vlog/concat1.v");

   const error_t expect[] = {
      { 12, "cannot reference net 'z' in constant expression" },
      { -1, NULL }
   };
   expect_errors(expect);

   do_parse_check_fold(V_MODULE);

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_pp4)
{
   input_from_file(TESTDIR "/vlog/pp4.v");

   const error_t expect[] = {
      { 16, "unexpected module while parsing expression" },
      {  0, "this token was unexpected" },
      {  0, "while expanding macro MACRO2" },
      {  0, "while expanding macro MACRO1" },
      { 21, "unexpected wire while parsing net declaration assignment" },
      { -1, NULL }
   };
   expect_errors(expect);

   LOCAL_TEXT_BUF tb = tb_new();
   vlog_preprocess(tb, true);

   file_ref_t file_ref = loc_file_ref(TESTDIR "/vlog/pp4.v", NULL);

   input_from_buffer(tb_get(tb), tb_len(tb), file_ref, SOURCE_VERILOG);

   vlog_node_t m = vlog_parse();
   fail_if(m == NULL);
   fail_unless(vlog_kind(m) == V_MODULE);

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_casename)
{
   input_from_file(TESTDIR "/vlog/casename.v");

   const error_t expect[] = {
      {  4, "design unit WORK.NAME replaces a previously analysed unit with "
         "the same name" },
      { -1, NULL }
   };
   expect_errors(expect);

   for (int i = 0; i < 2; i++) {
      vlog_node_t m = do_parse_check(V_MODULE);
      lib_put_vlog(lib_work(), m);
   }

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_dupmod)
{
   input_from_file(TESTDIR "/vlog/dupmod.v");

   const error_t expect[] = {
      {  4, "design unit WORK.DUPMOD is already defined" },
      { -1, NULL }
   };
   expect_errors(expect);

   for (int i = 0; i < 2; i++)
      do_parse_only(V_MODULE);

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_staticmeth)
{
   input_from_file(TESTDIR "/vlog/staticmeth.sv");

   const error_t expect[] = {
      {  2, "a class method cannot have a static lifetime" },
      {  4, "a class method cannot have a static lifetime" },
      { -1, NULL }
   };
   expect_errors(expect);

   fail_if(vlog_parse() == NULL);
   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_const1)
{
   input_from_file(TESTDIR "/vlog/const1.v");

   set_default_keywords(VLOG_1800_2023);

   const error_t expect[] = {
      {  4, "cannot reference net 'w1' in constant expression" },
      { 11, "cannot call system function '$time' in constant expression" },
      { -1, NULL }
   };
   expect_errors(expect);

   do_parse_check_fold(V_MODULE);

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_case1)
{
   input_from_file(TESTDIR "/vlog/case1.v");

   const error_t expect[] = {
      { 11, "no visible declaration for 'aaa'" },
      { -1, NULL }
   };
   expect_errors(expect);

   do_parse_only(V_MODULE);

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_case2)
{
   input_from_file(TESTDIR "/vlog/case2.v");

   const error_t expect[] = {
      { 13, "multiple default statements within a single case statement" },
      { -1, NULL }
   };
   expect_errors(expect);

   do_parse_check(V_MODULE);

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_direct1)
{
   input_from_file(TESTDIR "/vlog/direct1.v");

   set_default_keywords(VLOG_1800_2023);

   const error_t expect[] = {
      { 11, "unexpected identifier while parsing default_nettype directive, "
        "expecting one of wire, tri, tri0, tri1, wand, triand, wor, trior," },
      { 15, "no visible declaration for 'x'" },
      { 29, "unexpected wire while parsing unconnected_drive directive, "
        "expecting one of pull0 or pull1" },
      { -1, NULL }
   };
   expect_errors(expect);

   for (int i = 0; i < 2; i++)
      do_parse_only(V_MODULE);

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_string1)
{
   static const struct {
      const char *input;
      const char *output;
   } cases[] = {
      { "hello", "hello" },
      { "", "" },
      { "1\0002", "1\0002" },
   };

   for (int i = 0; i < ARRAY_LEN(cases); i++) {
      number_t n = number_from_string(cases[i].input, strlen(cases[i].input));
      ck_assert(number_is_defined(n));

      const int width = number_width(n);
      ck_assert_int_eq(width, strlen(cases[i].output) * 8);

      for (int j = 0; j < width / 8; j++)
         ck_assert_msg(number_byte(n, width/8 - 1 - j) == cases[i].output[j],
                       "%s != %s", cases[i].input, cases[i].output);
   }
}
END_TEST

START_TEST(test_generate1)
{
   input_from_file(TESTDIR "/vlog/generate1.v");

   const error_t expect[] = {
      { 11, "cannot specify both a label and a name for the same block" },
      { 12, "'g2' does not match label 'g1'" },
      { 13, "block does not have a label" },
      {  3, "cannot reference net 'x' in constant expression" },
      { -1, NULL }
   };
   expect_errors(expect);

   vlog_node_t m1 = do_parse_only(V_MODULE);
   vlog_check(m1);
   vlog_fold(m1, get_mir(), get_jit());

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_tfcall1)
{
   input_from_file(TESTDIR "/vlog/tfcall1.sv");

   const error_t expect[] = {
      { 36, "return statement can only be used in a subroutine" },
      { 39, "'bob' does not match task name 'task3'" },
      { 42, "'blah' does not match function name 'func2'" },
      { 11, "no visible declaration for 'not_here'" },
      { -1, NULL }
   };
   expect_errors(expect);

   do_parse_only(V_MODULE);

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_tfcall2)
{
   input_from_file(TESTDIR "/vlog/tfcall2.sv");

   const error_t expect[] = {
      { 40, "return statement in a task cannot have an expression" },
      { 44, "return statement in a non-void function must have an expression" },
      { 53, "completely declared port 'z' cannot be declared again in a data "
        "type declaration" },
      { 60, "expected at least 1 argument for 'optarg1' but have 0" },
      { 61, "expected at most 2 argument for 'optarg1' but have 3" },
      { 64, "'optarg1' argument 'x' cannot be empty as it does not have "
        "a default value" },
      { 71, "expected at least 1 argument for 'optarg2' but have 0" },
      { 72, "missing 'optarg2' argument 'y' without a default value" },
      {  9, "expected 2 arguments for 'sum' but have 1" },
      { 10, "expected 2 arguments for 'sum' but have 3" },
      { 13, "'x4' is not a function" },
      { 27, "return value of function 'no_args' is discarded" },
      { -1, NULL }
   };
   expect_errors(expect);

   do_parse_check(V_MODULE);

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_attr1)
{
   input_from_file(TESTDIR "/vlog/attr1.v");

   set_default_keywords(VLOG_1800_2023);

   const error_t expect[] = {
      { 11, "attribute instance is not allowed here" },
      { -1, NULL }
   };
   expect_errors(expect);

   for (int i = 0; i < 2; i++)
      do_parse_only(V_MODULE);

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_initial1)
{
   input_from_file(TESTDIR "/vlog/initial1.v");

   const error_t expect[] = {
      { 9, "unexpected reg while parsing statement item, expecting one of " },
      { 14, "initial block does not have a label" },
      { 16, "'another_label' does not match label 'a_label'" },
      { 22, "unexpected reg while parsing statement item, expecting one of " },
      { 28, "fork block does not have a label" },
      { 30, "'fork5' does not match label 'fork4'" },
      { -1, NULL }
   };
   expect_errors(expect);

   do_parse_only(V_MODULE);

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_nets1)
{
   input_from_file(TESTDIR "/vlog/nets1.v");

   const error_t expect[] = {
      { 10, "charge strength only allowed with the trireg keyword" },
      { 13, "vectored and scalared keywords are only allowed with at least "
        "a packed dimension" },
      { 27, "unexpected highz1 while parsing strength1, expecting one of "
        "supply1, strong1, pull1 or weak1" },
      { -1, NULL }
   };
   expect_errors(expect);

   vlog_node_t m1 = do_parse_only(V_MODULE);
   vlog_check(m1);

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_keywords)
{
   input_from_file(TESTDIR "/vlog/keywords.v");

   const error_t expect[] = {
      {  9, "'logic' is a keyword in System Verilog 1800-2005" },
      { 13, "`end_keywords directive without matching `begin_keywords" },
      { 14, "\"fizz\" is not a recognised Verilog or System Verilog version" },
      { 15, "unexpected unsigned number while parsing keywords directive, "
        "expecting string" },
      { -1, NULL }
   };
   expect_errors(expect);

   for (int i = 0; i < 2; i++)
      do_parse_only(V_MODULE);

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_error1)
{
   input_from_file(TESTDIR "/vlog/error1.v");

   const error_t expect[] = {
      {  2, "no data type declaration for 'foo'" },
      {  6, "'error1' does not match module name 'error2'" },
      { -1, NULL }
   };
   expect_errors(expect);

   for (int i = 0; i < 2; i++)
      do_parse_only(V_MODULE);

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_pp5)
{
   input_from_file(TESTDIR "/vlog/pp5.v");

   LOCAL_TEXT_BUF tb = tb_new();
   vlog_preprocess(tb, false);

   ck_assert_str_eq(tb_get(tb), "\n\n\n\n\n\n\n\n\n\n //  `ifdef VERIFIC\n");
}
END_TEST

START_TEST(test_integers1)
{
   input_from_file(TESTDIR "/vlog/integers1.sv");

   const error_t expect[] = {
      {  21, "unexpected identifier while parsing statement item, expecting ;" },
      {  28, "unexpected ' while parsing statement item, expecting ;" },
      { -1, NULL }
   };
   expect_errors(expect);

   do_parse_only(V_MODULE);

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_pp6)
{
   add_include_dir(TESTDIR "/vlog");

   input_from_file(TESTDIR "/vlog/pp6.v");

   const error_t expect[] = {
      {  2, "macro 'bad' undefined" },
      {  4, "only white space or a comment may appear on the same "
         "line as an `include directive" },
      {  4, "cannot find foo in the current working directory" },
      {  5, "cannot find not.here in the current working directory" },
      {  0, "searched include directory " TESTDIR "/vlog" },
      { -1, NULL }
   };
   expect_errors(expect);

   LOCAL_TEXT_BUF tb = tb_new();
   vlog_preprocess(tb, false);

   ck_assert_str_eq(
      tb_get(tb),
      "one\n"
      "hello\n"
      "\n"
      "two\n");

   check_expected_errors();
}
END_TEST

START_TEST(test_pp22)
{
   add_include_dir(TESTDIR "/vlog");

   input_from_file(TESTDIR "/vlog/pp22.v");

   const error_t expect[] = {
      {  6, "unexpected module while parsing expression" },
      {  0, "this token was unexpected" },
      {  0, "while processing `include \"pp22.vh\" directive" },
      {  0, "while expanding macro BAD" },
      { 11, "unexpected wire while parsing net declaration assignment" },
      { -1, NULL }
   };
   expect_errors(expect);

   LOCAL_TEXT_BUF tb = tb_new();
   vlog_preprocess(tb, true);

   file_ref_t file_ref = loc_file_ref(TESTDIR "/vlog/pp22.v", NULL);

   input_from_buffer(tb_get(tb), tb_len(tb), file_ref, SOURCE_VERILOG);

   vlog_node_t m = vlog_parse();
   fail_if(m == NULL);
   fail_unless(vlog_kind(m) == V_MODULE);

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_label1)
{
   input_from_file(TESTDIR "/vlog/label1.v");

   const error_t expect[] = {
      { 10, "cannot specify both a statement label and a block name" },
      { 20, "cannot specify both a statement label and a block name" },
      { -1, NULL }
   };
   expect_errors(expect);

   do_parse_only(V_MODULE);

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_href1)
{
   input_from_file(TESTDIR "/vlog/href1.v");

   const error_t expect[] = {
      {  5, "prefix of hierarchical identifier is not an instance" },
      { -1, NULL }
   };
   expect_errors(expect);

   do_parse_check(V_MODULE);

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_package1)
{
   input_from_file(TESTDIR "/vlog/package1.sv");

   const error_t expect[] = {
      {  3, "'different_name' does not match package name 'dummy_pkg'" },
      { 15, "no visible declaration for 'b'" },
      { -1, NULL }
   };
   expect_errors(expect);

   do_parse_only(V_PACKAGE);
   do_parse_only(V_PACKAGE);
   do_parse_only(V_NAMESPACE);
   do_parse_only(V_PACKAGE);
   do_parse_only(V_NAMESPACE);
   do_parse_only(V_PACKAGE);

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_class1)
{
   input_from_file(TESTDIR "/vlog/class1.sv");

   const error_t expect[] = {
      { 12, "'foo_t' does not match class name 'bar_t'" },
      { 18, "new class expression must have class type" },
      { 20, "class 'foo_t' has no field named 'c'" },
      { 21, "new can only be used as the value of an assignment" },
      { 24, "'bob' does not match program name 'main'" },
      { -1, NULL }
   };
   expect_errors(expect);

   do_parse_only(V_PROGRAM);

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_class2)
{
   input_from_file(TESTDIR "/vlog/class2.sv");

   const error_t expect[] = {
      { 20, "invalid operands for binary expression" },
      {  0, "have 'int' and 'class'" },
      { 22, "invalid operands for binary expression" },
      {  0, "have 'class' and 'class'" },
      { 23, "value of type 'bar_t' is not compatible with 'foo_t'" },
      { 24, "value of type 'integral' is not compatible with 'class'" },
      { 25, "value of type 'bar_t' is not compatible with 'foo_t'" },
      { -1, NULL }
   };
   expect_errors(expect);

   do_parse_check(V_PROGRAM);

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_class3)
{
   input_from_file(TESTDIR "/vlog/class3.sv");

   do_parse_check(V_PACKAGE);
   do_parse_check(V_MODULE);

   fail_unless(vlog_parse() == NULL);

   fail_if_errors();
}
END_TEST

START_TEST(test_namespace1)
{
   input_from_file(TESTDIR "/vlog/namespace1.sv");

   do_parse_only(V_NAMESPACE);
   do_parse_only(V_MODULE);
   do_parse_only(V_NAMESPACE);

   fail_unless(vlog_parse() == NULL);

   fail_if_errors();
}
END_TEST

START_TEST(test_real1)
{
   input_from_file(TESTDIR "/vlog/real1.v");

   const error_t expect[] = {
      {  8, "invalid operands for binary expression" },
      {  9, "invalid operands for binary expression" },
      { 10, "invalid operands for binary expression" },
      { 11, "invalid operands for binary expression" },
      {  0, "have 'real' and 'real'" },
      { 13, "value of type 'real' cannot be indexed" },
      { 14, "invalid operands for unary expression" },
      { -1, NULL }
   };
   expect_errors(expect);

   do_parse_check(V_MODULE);

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_simp1)
{
   input_from_file(TESTDIR "/vlog/simp1.v");

   vpi_context_new();

   vlog_node_t m = do_parse_check_fold(V_MODULE);

   const int64_t expect[] = { 5, 1, 7, 1, 2, 2, -4, 6, -2, -4, 4, 3 };

   ck_assert_int_eq(ARRAY_LEN(expect), vlog_decls(m));

   for (int i = 0; i < ARRAY_LEN(expect); i++) {
      vlog_node_t d = vlog_decl(m, i), value = vlog_value(d);
      if (vlog_kind(value) == V_NUMBER) {
         number_t n = vlog_number(value);
         if (number_integer(n) == expect[i])
            continue;
      }

      vlog_dump(d, 0);
      ck_abort_msg("expected constant %"PRIi64, expect[i]);
   }

   fail_unless(vlog_parse() == NULL);

   fail_if_errors();
}
END_TEST

START_TEST(test_lower1)
{
   opt_set_int(OPT_RTL_ISLANDS, RTL_ISLANDS_OFF);

   input_from_file(TESTDIR "/vlog/lower1.v");

   run_elab();

   mir_context_t *mc = get_mir();

   {
      mir_unit_t *mu = mir_get_unit(mc, ident_new("WORK.LOWER1#0"));
      ck_assert_ptr_nonnull(mu);

      static const mir_match_t bb0[] = {
         { MIR_OP_PACKAGE_INIT, LINK("NVC.VERILOG") },
         { MIR_OP_LINK_VAR, LINK("NVC.VERILOG"), NODE(_),
           EXTVAR("NVC.VERILOG.T_WIRE$resolution") },
         { MIR_OP_DEBUG_LOCUS },
         { MIR_OP_INIT_SIGNAL },
         { MIR_OP_RESOLVE_SIGNAL },
         { MIR_OP_STORE, VAR("x") },
         { MIR_OP_DEBUG_LOCUS },
         { MIR_OP_INIT_SIGNAL },
         { MIR_OP_RESOLVE_SIGNAL },
         { MIR_OP_STORE, VAR("y") },
         { MIR_OP_DEBUG_LOCUS },
         { MIR_OP_INIT_SIGNAL },
         { MIR_OP_RESOLVE_SIGNAL },
         { MIR_OP_STORE, VAR("z") },
         { MIR_OP_CONST_VEC },
         { MIR_OP_DEBUG_LOCUS },
         { MIR_OP_INIT_SIGNAL },
         { MIR_OP_STORE, VAR("r") },
         { MIR_OP_CONTEXT_UPREF, ENUM(0) },
         { MIR_OP_CLOSURE, LINK("WORK.LOWER1#0.assign#3#9") },
         { MIR_OP_DEBUG_LOCUS },
         { MIR_OP_PROCESS_INIT },
         { MIR_OP_CLOSURE, LINK("WORK.LOWER1#0.always#6#2") },
         { MIR_OP_DEBUG_LOCUS },
         { MIR_OP_PROCESS_INIT },
         { MIR_OP_RETURN },
      };
      mir_match(mu, 0, bb0);
   }

   {
      mir_unit_t *mu = mir_get_unit(mc, ident_new("WORK.LOWER1#0.always#6#2"));
      ck_assert_ptr_nonnull(mu);

      static const mir_match_t bb0[] = {
         { MIR_OP_VAR_UPREF, ENUM(1), UPREF("x") },
         { MIR_OP_LOAD },
         { MIR_OP_SCHED_EVENT },
         { MIR_OP_RETURN },
      };
      mir_match(mu, 0, bb0);

      static const mir_match_t bb1[] = {
         { MIR_OP_WAIT, BLOCK(2) },
      };
      mir_match(mu, 1, bb1);

      static const mir_match_t bb2[] = {
         { MIR_OP_VAR_UPREF, ENUM(1), UPREF("r") },
         { MIR_OP_LOAD },
         { MIR_OP_VAR_UPREF, ENUM(1), UPREF("x") },
         { MIR_OP_LOAD },
         { MIR_OP_RESOLVED },
         { MIR_OP_LOAD },
         { MIR_OP_PACK },
         { MIR_OP_UNARY, ENUM(MIR_VEC_LOG_NOT) },
         { MIR_OP_DEPOSIT_SIGNAL },
         { MIR_OP_JUMP, BLOCK(1) },
      };
      mir_match(mu, 2, bb2);
   }

   fail_if_errors();
}
END_TEST

START_TEST(test_pp7)
{
   input_from_file(TESTDIR "/vlog/pp7.v");

   const error_t expect[] = {
      { 12, "unterminated block comment" },
      { -1, NULL }
   };
   expect_errors(expect);

   LOCAL_TEXT_BUF tb = tb_new();
   vlog_preprocess(tb, false);

   ck_assert_str_eq(
      tb_get(tb),
      "// This file has DOS line endings\n"
      "1234\n"
      "/* `endif */\n"
      "/*/ a multi-line\n"
      " comment\n"
      "\n"
      " `endif\n"
      "*/\n"
      "\n"
      "\n"
      " /*\n"
      "  * untermintated\n");

   check_expected_errors();
}
END_TEST

START_TEST(test_pp8)
{
   input_from_file(TESTDIR "/vlog/pp8.v");

   const error_t expect[] = {
      {  7, "macro 'add' is missing argument 'y' with no default value" },
      {  8, "macro 'baz' undefined" },
      { 10, "macro 'foo' requires exactly 1 arguments" },
      { -1, NULL }
   };
   expect_errors(expect);

   LOCAL_TEXT_BUF tb = tb_new();
   vlog_preprocess(tb, false);

   ck_assert_str_eq(
      tb_get(tb),
      "\n"
      "((1) + (2))\n"
      "hello\n"
      "\n"
      "assert(x == y);\n"
      "(((1 + 2)) + (ident))\n"
      "((1) + (y)) // Error\n"
      "(4, 5, 6) // Warning\n"
      "\n"
      " // Error\n"
      "\n"
      "'hAbCdEf10 * 'hAbCdEf10         // hex literal (no size)\n"
      "12'b0100_0000_0000 * 12'b0100_0000_0000 // unsigned binary literal (with underscores)\n"
      "12'd1024 * 12'd1024           // unsigned decimal literal\n"
      "12'sh400 * 12'sh400           // signed hex literal\n"
      "12'o2000 * 12'o2000           // unsigned octal literal\n"
      );

   check_expected_errors();
}
END_TEST

START_TEST(test_generate2)
{
   input_from_file(TESTDIR "/vlog/generate2.v");

   set_default_keywords(VLOG_1800_2023);

   // Should parse and check without errors: generate case and else-if
   vlog_node_t m = do_parse_check(V_MODULE);
   fail_unless(vlog_stmts(m) > 0);
}
END_TEST

START_TEST(test_pp9)
{
   input_from_file(TESTDIR "/vlog/pp9.v");

   // Test that macro expansion inside tokens works correctly in
   // precise mode (used during analysis).  32'd`MY_VAL should
   // expand to 32'd42 without location tracking directives
   // breaking the number literal.
   LOCAL_TEXT_BUF tb = tb_new();
   vlog_preprocess(tb, true);   // precise=true, as in -a

   // Feed the preprocessed text back to the parser
   file_ref_t file_ref = loc_file_ref(TESTDIR "/vlog/pp9.v", NULL);
   input_from_buffer(tb_get(tb), tb_len(tb), file_ref, SOURCE_VERILOG);

   set_default_keywords(VLOG_1800_2023);

   do_parse_check(V_MODULE);

   fail_unless(vlog_parse() == NULL);
}
END_TEST

START_TEST(test_const2)
{
   input_from_file(TESTDIR "/vlog/const2.v");

   set_default_keywords(VLOG_1800_2023);

   const error_t expect[] = {
      { 37, "cannot reference net 'w1' in constant expression" },
      { -1, NULL }
   };
   expect_errors(expect);

   do_parse_check_fold(V_MODULE);

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_ppfile1)
{
#ifndef __MINGW32__
   // A file name with a backslash and a quote (as on Windows) must be
   // escaped in the string literal that `__FILE__ expands to (22.13)
   const char *path = "ppfile1\\a\"b.v";

   FILE *f = fopen(path, "w");
   ck_assert_ptr_nonnull(f);
   fputs("`__FILE__\n", f);
   fclose(f);

   input_from_file(path);

   LOCAL_TEXT_BUF tb = tb_new();
   vlog_preprocess(tb, false);

   ck_assert_str_eq(tb_get(tb), "\"ppfile1\\\\a\\\"b.v\"\n");

   remove(path);

   fail_if_errors();
#endif
}
END_TEST

START_TEST(test_pp10)
{
   input_from_file(TESTDIR "/vlog/pp10.v");

   LOCAL_TEXT_BUF tb = tb_new();
   vlog_preprocess(tb, false);

   ck_assert_str_eq(
      tb_get(tb),
      "// Test `define with trailing // comment (IEEE 1364-2005 §19.3.1).\n"
      "// The comment is NOT part of the macro text.\n"
      "\n"
      "\n"
      "\n"
      "\n"
      "\n"
      "10'b0011110110   42               \"hello\"         \n");

   fail_if_errors();
}
END_TEST

START_TEST(test_pp11)
{
   input_from_file(TESTDIR "/vlog/pp11.v");

   LOCAL_TEXT_BUF tb = tb_new();
   vlog_preprocess(tb, false);

   ck_assert_str_eq(
      tb_get(tb),
      "\n"
      "\n"
      "module pp11;\n"
      "  reg mem_do_wdata;\n"
      "\n"
      "  initial begin\n"
      "    if (mem_do_wdata)\n"
      "      empty_statement;\n"
      "  end\n"
      "\n"
      "\n"
      "\n"
      "\n"
      "\n"
      "\n"
      "  task empty_statement;\n"
      "    begin end\n"
      "  endtask\n"
      "endmodule\n");

   fail_if_errors();
}
END_TEST

START_TEST(test_simp2)
{
   input_from_file(TESTDIR "/vlog/simp2.v");

   vlog_node_t m = do_parse_check_fold(V_MODULE);

   vlog_node_t p1 = vlog_decl(m, 1);
   ck_assert_vlog_kind(p1, V_LOCALPARAM);
   ck_assert_vlog_kind(vlog_value(p1), V_NUMBER);

   number_t p1n = vlog_number(vlog_value(p1));
   ck_assert_int_eq(number_width(p1n), 32);
   ck_assert(!number_is_defined(p1n));

   vlog_node_t p2 = vlog_decl(m, 2);
   ck_assert_vlog_kind(p2, V_LOCALPARAM);
   ck_assert_vlog_kind(vlog_value(p2), V_NUMBER);

   number_t p2n = vlog_number(vlog_value(p2));
   ck_assert_int_eq(number_width(p2n), 32);
   ck_assert(!number_is_defined(p2n));

   vlog_node_t p3 = vlog_decl(m, 3);
   ck_assert_vlog_kind(p3, V_LOCALPARAM);

   vlog_node_t p3t = vlog_type(p3);
   ck_assert_vlog_kind(p3t, V_DATA_TYPE);
   ck_assert_int_eq(vlog_subkind(p3t), DT_LOGIC);
   ck_assert(!(vlog_flags(p3t) & VLOG_F_SIGNED));

   fail_unless(vlog_parse() == NULL);

   fail_if_errors();
}
END_TEST

START_TEST(test_lower2)
{
   input_from_file(TESTDIR "/vlog/lower2.v");

   run_elab();

   mir_context_t *mc = get_mir();

   {
      mir_unit_t *mu = mir_get_unit(mc, ident_new("WORK.LOWER2#0.initial#5#2"));
      ck_assert_ptr_nonnull(mu);

      static const mir_match_t bb1[] = {
         { MIR_OP_VAR_UPREF, ENUM(1), UPREF("y1") },
         { MIR_OP_LOAD },
         { MIR_OP_VAR_UPREF, ENUM(1), UPREF("x") },
         { MIR_OP_LOAD },
         { MIR_OP_LOAD_PACKED, NODE(_), CONST(3), CONST(4) },
         { MIR_OP_DEPOSIT_SIGNAL },
         { MIR_OP_RETURN },
      };
      mir_match(mu, 1, bb1);
   }

   {
      mir_unit_t *mu = mir_get_unit(mc, ident_new("WORK.LOWER2#0.initial#7#2"));
      ck_assert_ptr_nonnull(mu);

      static const mir_match_t bb1[] = {
         { MIR_OP_VAR_UPREF, ENUM(1), UPREF("y2") },
         { MIR_OP_LOAD },
         { MIR_OP_VAR_UPREF, ENUM(1), UPREF("x") },
         { MIR_OP_LOAD },
         { MIR_OP_LOAD_PACKED, NODE(_), CONST(0), CONST(8) },
         { MIR_OP_CONST_VEC },
         { MIR_OP_CONST_VEC },
         { MIR_OP_CAST },
         { MIR_OP_BINARY, ENUM(MIR_VEC_SLL) },
         { MIR_OP_BINARY, ENUM(MIR_VEC_SUB) },
         { MIR_OP_CAST },
         { MIR_OP_BINARY, ENUM(MIR_VEC_SRL) },
         { MIR_OP_CAST },
         { MIR_OP_BINARY, ENUM(MIR_VEC_BIT_AND) },
         { MIR_OP_CAST },
         { MIR_OP_BINARY, ENUM(MIR_VEC_SLL) },
         { MIR_OP_BINARY, ENUM(MIR_VEC_SLL) },
         { MIR_OP_UNARY, ENUM(MIR_VEC_BIT_NOT) },
         { MIR_OP_BINARY, ENUM(MIR_VEC_BIT_AND) },
         { MIR_OP_BINARY, ENUM(MIR_VEC_BIT_OR) },
         { MIR_OP_DEPOSIT_SIGNAL },
         { MIR_OP_RETURN },
      };
      mir_match(mu, 1, bb1);
   }

   fail_if_errors();
}
END_TEST

START_TEST(test_lower3)
{
   input_from_file(TESTDIR "/vlog/lower3.sv");

   run_elab();

   mir_context_t *mc = get_mir();

   mir_unit_t *mu = mir_get_unit(mc, ident_new("WORK.LOWER3#0.initial#5#3"));
   ck_assert_ptr_nonnull(mu);

   static const mir_match_t bb1[] = {
      { MIR_OP_FCALL },
      { MIR_OP_CMP },
      { MIR_OP_COND, NODE(_), BLOCK(3), BLOCK(2) },
   };
   mir_match(mu, 1, bb1);

   static const mir_match_t bb2w[] = {
      { MIR_OP_WAIT, BLOCK(4) },
   };
   mir_match(mu, 2, bb2w);

   static const mir_match_t bb3s[] = {
      { MIR_OP_VAR_UPREF, ENUM(1), UPREF("sum") },
      { MIR_OP_LOAD },
      { MIR_OP_CONST_VEC },
      { MIR_OP_DEPOSIT_SIGNAL, NODE(_), CONST(32) },
      { MIR_OP_VAR_UPREF, ENUM(1), UPREF("idx") },
      { MIR_OP_LOAD },
      { MIR_OP_CONST_VEC },
      { MIR_OP_DEPOSIT_SIGNAL, NODE(_), CONST(1) },
      { MIR_OP_STORE, VAR("idx") },
      { MIR_OP_JUMP, BLOCK(7) },
   };
   mir_match(mu, 3, bb3s);

   static const mir_match_t bb2[] = {
      { MIR_OP_LOAD },
      { MIR_OP_LOAD_PACKED, NODE(_), CONST(0), CONST(32) },
      { MIR_OP_LOAD, VAR("idx") },
      { MIR_OP_BINARY, ENUM(MIR_VEC_ADD) },
      { MIR_OP_LOAD },
      { MIR_OP_DEPOSIT_SIGNAL, NODE(_), CONST(32) },
      { MIR_OP_JUMP, BLOCK(6) },
   };
   mir_match(mu, 5, bb2);

   static const mir_match_t bb3[] = {
      { MIR_OP_LOAD, VAR("idx") },
      { MIR_OP_CONST_VEC },
      { MIR_OP_BINARY, ENUM(MIR_VEC_ADD) },
      { MIR_OP_STORE, VAR("idx") },
      { MIR_OP_JUMP, BLOCK(7) },
   };
   mir_match(mu, 6, bb3);

   static const mir_match_t bb4[] = {
      { MIR_OP_LOAD, VAR("idx") },
      { MIR_OP_CONST_VEC },
      { MIR_OP_CAST },
      { MIR_OP_BINARY, ENUM(MIR_VEC_LT) },
      { MIR_OP_TEST },
      { MIR_OP_COND, NODE(_), BLOCK(5), BLOCK(8) },
   };
   mir_match(mu, 7, bb4);

   fail_if_errors();
}
END_TEST

START_TEST(test_force1)
{
   input_from_file(TESTDIR "/vlog/force1.v");

   const error_t expect[] = {
      { -1, NULL }
   };
   expect_errors(expect);

   vlog_node_t m1 = do_parse_only(V_MODULE);
   vlog_check(m1);

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_constfunc1)
{
   input_from_file(TESTDIR "/vlog/constfunc1.v");

   const error_t expect[] = {
      { 16, "cannot reference variable 'i' in constant expression" },
      { 22, "cannot call non-constant user function 'get_i' in constant "
        "expression" },
      { 25, "cannot reference variable 'i' in constant expression" },
      { -1, NULL }
   };
   expect_errors(expect);

   vpi_context_new();

   do_parse_check_fold(V_MODULE);

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_pp12)
{
   input_from_file(TESTDIR "/vlog/pp12.v");

   LOCAL_TEXT_BUF tb = tb_new();
   vlog_preprocess(tb, false);

   ck_assert_str_eq(
      tb_get(tb),
      "\n"
      "bit_out = data[1];\n");

   fail_if_errors();
}
END_TEST

START_TEST(test_pp13)
{
   add_include_dir(TESTDIR "/vlog");

   input_from_file(TESTDIR "/vlog/pp13.v");

   LOCAL_TEXT_BUF tb = tb_new();
   vlog_preprocess(tb, false);

   ck_assert_str_eq(tb_get(tb), "\n\n\n\n8\n");

   fail_if_errors();
}
END_TEST

START_TEST(test_pp14)
{
   input_from_file(TESTDIR "/vlog/pp14.v");

   const error_t expect[] = {
      { 15, "macro 'MACRO1' is missing argument 'c' with no default value" },
      { 16, "macro 'MACRO2' requires exactly 3 arguments, 4 were given" },
      { 17, "macro 'MACRO3' requires arguments" },
      { -1, NULL }
   };
   expect_errors(expect);

   LOCAL_TEXT_BUF tb = tb_new();
   vlog_preprocess(tb, false);

   ck_assert_str_eq(
      tb_get(tb),
      "\n"
      "$display(5,,2,,3 );\n"
      "$display(1 ,,\"B\",,3 );\n"
      "$display(5,,2,,);\n"
      "\n"
      "\n"
      "$display(1,,,,3);\n"
      "$display(5,,2,,\"C\");\n"
      "$display(5,,2,,\"C\");\n"
      "\n"
      "\n"
      "$display(1 ,,0,,\"C\");\n"
      "$display(5,,0,,\"C\");\n"
      "\n"
      "$display(1 ,,\"B\",,c);       // ILLEGAL: b and c omitted, no default for c\n"
      "$display(5,,,,3); // ILLEGAL: more argument than declared\n"
      "$display(a,,b,,c);             // ILLEGAL: parentheses required\n");

   check_expected_errors();
}

START_TEST(test_href2)
{
   input_from_file(TESTDIR "/vlog/href2.v");

   vlog_node_t m1 = do_parse_check(V_MODULE);
   vlog_check(m1);

   vlog_node_t m2 = do_parse_check(V_MODULE);
   vlog_check(m2);

   jit_t *j = get_jit();
   rt_model_t *m = model_new(j, NULL);
   mir_context_t *mc = get_mir();
   unit_registry_t *ur = get_registry();

   object_t *top[2] = { vlog_to_object(m1), vlog_to_object(m2) };

   tree_t e = elab(top, ARRAY_LEN(top), j, ur, mc, NULL, NULL, m);
   ck_assert_ptr_nonnull(e);

   model_free(m);

   fail_if_errors();
}
END_TEST

START_TEST(test_href3)
{
   input_from_file(TESTDIR "/vlog/href3.v");

   const error_t expect[] = {
      {  7, "name 'xxx' not found in 'u1'" },
      {  0, "'u1' is an instance of WORK.SUB1" },
      {  8, "cannot find 'glbl' in hierarchical identifier" },
      { -1, NULL }
   };
   expect_errors(expect);

   run_elab();

   check_expected_errors();
}
END_TEST

START_TEST(test_constfunc2)
{
   input_from_file(TESTDIR "/vlog/constfunc2.v");

   vlog_node_t m = do_parse_check_fold(V_MODULE);

   vlog_node_t p1 = vlog_decl(m, 1);
   ck_assert_vlog_kind(p1, V_LOCALPARAM);
   ck_assert_vlog_kind(vlog_value(p1), V_NUMBER);

   number_t p1n = vlog_number(vlog_value(p1));
   ck_assert_int_eq(number_width(p1n), 4);
   ck_assert(number_is_defined(p1n));
   ck_assert_int_eq(number_integer(p1n), 10);

   vlog_node_t p3 = vlog_decl(m, 3);
   ck_assert_vlog_kind(p3, V_LOCALPARAM);
   ck_assert_vlog_kind(vlog_value(p3), V_USER_FCALL);  // Cannot fold now

   fail_unless(vlog_parse() == NULL);

   fail_if_errors();
}
END_TEST

START_TEST(test_disable1)
{
   input_from_file(TESTDIR "/vlog/disable1.v");

   const error_t expect[] = {
      {  7, "cannot disable 'x'" },
      {  6, "no visible label 'bar'" },
      { -1, NULL }
   };
   expect_errors(expect);

   do_parse_only(V_MODULE);

   check_expected_errors();
}
END_TEST

START_TEST(test_issue1596)
{
   input_from_file(TESTDIR "/vlog/issue1596.v");

   const error_t expect[] = {
      { 10, "unexpected identifier while parsing module or generate item" },
      { -1, NULL }
   };
   expect_errors(expect);

   do_parse_only(V_MODULE);

   check_expected_errors();
}
END_TEST

START_TEST(test_issue1678)
{
   input_from_file(TESTDIR "/vlog/issue1678.v");

   do_parse_check(V_MODULE);

   fail_unless(vlog_parse() == NULL);

   fail_if_errors();
}
END_TEST

START_TEST(test_pp15)
{
   input_from_file(TESTDIR "/vlog/pp15.v");

   LOCAL_TEXT_BUF tb = tb_new();
   vlog_preprocess(tb, false);

   ck_assert_str_eq(
      tb_get(tb),
      "\n"
      "\n"
      "module pp15;\n"
      "  wire [1:0] in;\n"
      "  wire [1:0] out;\n"
      "\n"
      "  assign out = {in[0], in[1]};\n"
      "endmodule\n");

   fail_if_errors();
}
END_TEST

START_TEST(test_prop1)
{
   input_from_file(TESTDIR "/vlog/prop1.sv");

   const error_t expect[] = {
      { -1, NULL }
   };
   expect_errors(expect);

   do_parse_check(V_MODULE);

   check_expected_errors();
}
END_TEST

START_TEST(test_pattern1)
{
   input_from_file(TESTDIR "/vlog/pattern1.sv");

   const error_t expect[] = {
      { 11, "struct or union has no field 'bogus'" },
      { 12, "cannot use structure pattern key with this data type" },
      { -1, NULL }
   };
   expect_errors(expect);

   do_parse_only(V_MODULE);

   check_expected_errors();
}
END_TEST

START_TEST(test_pp16)
{
   input_from_file(TESTDIR "/vlog/pp16.v");

   const error_t expect[] = {
      { 17, "unexpected module while parsing statement item" },
      { -1, NULL }
   };
   expect_errors(expect);

   LOCAL_TEXT_BUF tb = tb_new();
   vlog_preprocess(tb, true);

   file_ref_t file_ref = loc_file_ref(TESTDIR "/vlog/pp16.v", NULL);
   input_from_buffer(tb_get(tb), tb_len(tb), file_ref, SOURCE_VERILOG);

   do_parse_only(V_MODULE);

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_pp17)
{
   input_from_file(TESTDIR "/vlog/pp17.v");

   LOCAL_TEXT_BUF tb = tb_new();
   vlog_preprocess(tb, false);

   ck_assert_str_eq(
      tb_get(tb),
      "\n"
      "\n"
      "\n"
      "\n"
      "module pp17;\n"
      "endmodule\n");

   fail_if_errors();
}
END_TEST

START_TEST(test_number3)
{
   uint64_t a, b;

   number_t u5 = number_new("5'b10101", NULL);
   number_word(u5, 0, &a, &b);
   ck_assert_uint_eq(a, UINT64_C(0x15));
   ck_assert_uint_eq(b, 0);
   number_word(u5, 1, &a, &b);
   ck_assert_uint_eq(a, 0);
   ck_assert_uint_eq(b, 0);

   number_t s5 = number_new("5'sb10101", NULL);
   number_word(s5, 0, &a, &b);
   ck_assert_uint_eq(a, UINT64_C(0xfffffffffffffff5));
   ck_assert_uint_eq(b, 0);
   number_word(s5, 100, &a, &b);
   ck_assert_uint_eq(a, UINT64_MAX);
   ck_assert_uint_eq(b, 0);

   number_t sx5 = number_new("5'sbx0101", NULL);
   number_word(sx5, 0, &a, &b);
   ck_assert_uint_eq(a, UINT64_C(0xfffffffffffffff5));
   ck_assert_uint_eq(b, UINT64_C(0xfffffffffffffff0));
   number_word(sx5, 1, &a, &b);
   ck_assert_uint_eq(a, UINT64_MAX);
   ck_assert_uint_eq(b, UINT64_MAX);

   number_t sz5 = number_new("5'sbz0101", NULL);
   number_word(sz5, 0, &a, &b);
   ck_assert_uint_eq(a, UINT64_C(0x5));
   ck_assert_uint_eq(b, UINT64_C(0xfffffffffffffff0));
   number_word(sz5, 1, &a, &b);
   ck_assert_uint_eq(a, 0);
   ck_assert_uint_eq(b, UINT64_MAX);

   number_t s68 = number_new("68'sh8_0123456789abcdef", NULL);
   number_word(s68, 0, &a, &b);
   ck_assert_uint_eq(a, UINT64_C(0x0123456789abcdef));
   ck_assert_uint_eq(b, 0);
   number_word(s68, 1, &a, &b);
   ck_assert_uint_eq(a, UINT64_C(0xfffffffffffffff8));
   ck_assert_uint_eq(b, 0);
   number_word(s68, 2, &a, &b);
   ck_assert_uint_eq(a, UINT64_MAX);
   ck_assert_uint_eq(b, 0);
}
END_TEST

START_TEST(test_pp20)
{
   add_include_dir(TESTDIR "/vlog");

   input_from_file(TESTDIR "/vlog/pp20.v");

   LOCAL_TEXT_BUF tb = tb_new();
   vlog_preprocess(tb, false);

   ck_assert_str_eq(
      tb_get(tb),
      "\n"
      "\n"
      "hello\n"
      " hello\n"
      "\n"
      "hello\n"
      "end\n");
}
END_TEST

START_TEST(test_pp19)
{
   input_from_file(TESTDIR "/vlog/pp19.v");

   const error_t expect[] = {
      { 1, "cannot redefine compiler directive 'define' as a text macro" },
      { 2, "string literal in macro text must end on the same line" },
      { 5, "cannot redefine compiler directive 'timescale' as a text macro" },
      { 6, "cannot redefine compiler directive 'line' as a text macro" },
      { -1, NULL }
   };
   expect_errors(expect);

   LOCAL_TEXT_BUF tb = tb_new();
   vlog_preprocess(tb, false);

   check_expected_errors();
}
END_TEST

START_TEST(test_pp21)
{
   input_from_file(TESTDIR "/vlog/pp21.v");

   const error_t expect[] = {
      {  1, "cannot redefine compiler directive 'timescale' as a "
        "text macro" },
      {  2, "cannot redefine compiler directive 'resetall' as a "
        "text macro" },
      {  3, "cannot redefine compiler directive 'celldefine' as a "
        "text macro" },
      {  4, "cannot redefine compiler directive 'endcelldefine' as a "
        "text macro" },
      {  5, "cannot redefine compiler directive 'default_nettype' as a "
        "text macro" },
      {  6, "cannot redefine compiler directive 'unconnected_drive' as a "
        "text macro" },
      {  7, "cannot redefine compiler directive 'nounconnected_drive' as a "
        "text macro" },
      {  8, "cannot redefine compiler directive 'begin_keywords' as a "
        "text macro" },
      {  9, "cannot redefine compiler directive 'end_keywords' as a "
        "text macro" },
      { 10, "cannot redefine compiler directive 'pragma' as a "
        "text macro" },
      { 11, "cannot redefine compiler directive '__FILE__' as a "
        "text macro" },
      { 12, "cannot redefine compiler directive '__LINE__' as a "
        "text macro" },
      { -1, NULL }
   };
   expect_errors(expect);

   LOCAL_TEXT_BUF tb = tb_new();
   vlog_preprocess(tb, false);

   check_expected_errors();
}
END_TEST

START_TEST(test_typedef1)
{
   const error_t expect[] = {
      {  1, "forward type declaration of 'never_defined' is never resolved "
         "to a data type" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/typedef1.sv");

   while (vlog_parse() != NULL)
      ;

   check_expected_errors();
}
END_TEST

START_TEST(test_inval5)
{
   const error_t expect[] = {
      {  2, "enumeration name 'c' without a value cannot follow a name with "
         "an unknown value" },
      {  4, "the value of enumeration name 'j' cannot contain x or z as the "
         "base type is two-state" },
      { 24, "specparam 'sp' cannot be used in the value of a parameter" },
      { 11, "variable 'v' cannot be written by both a continuous assignment "
         "and a procedural assignment" },
      { 15, "variable 'x' cannot be written by both a continuous assignment "
         "and a procedural assignment" },
      { 17, "variable 'r1' cannot be driven by more than one continuous "
         "assignment" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/inval5.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_pp18)
{
   input_from_file(TESTDIR "/vlog/pp18.v");

   LOCAL_TEXT_BUF tb = tb_new();
   vlog_preprocess(tb, false);

   ck_assert_str_eq(
      tb_get(tb),
      "\n"
      "\n"
      "\n"
      "\n"
      "\n"
      "\n"
      "\n"
      "\n"
      "\n"
      "\n"
      "\n"
      "\n"
      "\n"
      "\n"
      "\n"
      "module pp18;\n"
      "  wire [3:0] i;\n"
      "  wire [7:0] o = \n"
      "    /* verilator lint_off WIDTH */ \n"
      "    ((8) == (4) ? i : {{((8) - (4)){1'b0}}, i}) \n"
      "    /* verilator lint_on WIDTH */;\n"
      "  wire [3:0] p = 1 /* a block comment */ + 1;\n"
      "  wire [3:0] q = 1 /* a block\n"
      "comment */ + 2;\n"
      "endmodule\n");

   fail_if_errors();
}
END_TEST

static vlog_node_t find_decl(vlog_node_t parent, const char *name)
{
   ident_t id = ident_new(name);
   const int ndecls = vlog_decls(parent);
   for (int i = 0; i < ndecls; i++) {
      vlog_node_t d = vlog_decl(parent, i);
      if (vlog_has_ident(d) && vlog_ident(d) == id)
         return d;
   }

   ck_abort_msg("no declaration named %s", name);
   return NULL;
}

static vlog_node_t cons_stmt(vlog_node_t c, int n, vlog_kind_t kind)
{
   vlog_node_t s = vlog_stmt(c, n);
   ck_assert_vlog_kind(s, kind);
   return s;
}

START_TEST(test_crv1)
{
   input_from_file(TESTDIR "/vlog/crv1.sv");

   set_default_keywords(VLOG_1800_2023);

   vlog_node_t m = do_parse_check(V_MODULE);

   fail_unless(vlog_parse() == NULL);
   fail_if_errors();

   vlog_node_t base = find_decl(m, "base");
   ck_assert_vlog_kind(base, V_CLASS_DECL);

   vlog_node_t a = find_decl(base, "a");
   ck_assert_int_eq(vlog_flags(a) & (VLOG_F_RAND | VLOG_F_RANDC), VLOG_F_RAND);

   vlog_node_t r = find_decl(base, "r");
   ck_assert_int_eq(vlog_flags(r) & (VLOG_F_RAND | VLOG_F_RANDC), VLOG_F_RANDC);

   vlog_node_t state = find_decl(base, "state");
   ck_assert_int_eq(vlog_flags(state) & (VLOG_F_RAND | VLOG_F_RANDC), 0);

   vlog_node_t bx = find_decl(base, "x");

   // Hidden properties follow the declared ones
   vlog_node_t rand_off = find_decl(base, "$rand_off");
   ck_assert_vlog_kind(rand_off, V_VAR_DECL);
   ck_assert_int_eq(vlog_flags(rand_off) & (VLOG_F_RAND | VLOG_F_RANDC), 0);
   ck_assert_int_eq(vlog_subkind(vlog_type(rand_off)), DT_LONGINT);
   ck_assert_vlog_kind(vlog_value(rand_off), V_NUMBER);
   ck_assert_int_eq(number_integer(vlog_number(vlog_value(rand_off))), 0);
   find_decl(base, "$cons_off");
   find_decl(base, "$seed");
   find_decl(base, "r$randc");
   ck_assert_int_eq(vlog_decl(base, vlog_decls(base) - 4) == rand_off, 1);

   vlog_node_t c1 = find_decl(base, "c1");
   ck_assert_vlog_kind(c1, V_CONSTRAINT);
   ck_assert_ptr_eq(vlog_ref(c1), base);
   ck_assert_int_eq(vlog_flags(c1) & VLOG_F_STATIC, 0);
   ck_assert_int_eq(vlog_stmts(c1), 3);

   vlog_node_t e0 = cons_stmt(c1, 0, V_CONS_EXPR);
   ck_assert_int_eq(vlog_flags(e0) & VLOG_F_SOFT, 0);
   ck_assert_vlog_kind(vlog_value(e0), V_BINARY);
   ck_assert_int_eq(vlog_subkind(vlog_value(e0)), V_BINARY_GT);

   vlog_node_t e1 = cons_stmt(c1, 1, V_CONS_EXPR);
   ck_assert_int_eq(vlog_flags(e1) & VLOG_F_SOFT, VLOG_F_SOFT);

   vlog_node_t in = vlog_value(cons_stmt(c1, 2, V_CONS_EXPR));
   ck_assert_vlog_kind(in, V_INSIDE);
   ck_assert_vlog_kind(vlog_value(in), V_REF);
   ck_assert_ptr_eq(vlog_ref(vlog_value(in)), a);
   ck_assert_int_eq(vlog_params(in), 3);
   ck_assert_vlog_kind(vlog_param(in, 0), V_INSIDE_RANGE);
   ck_assert_vlog_kind(vlog_param(in, 1), V_NUMBER);
   ck_assert_vlog_kind(vlog_param(in, 2), V_INSIDE_RANGE);
   ck_assert_vlog_kind(vlog_right(vlog_param(in, 2)), V_DOLLAR);

   vlog_node_t c2 = find_decl(base, "c2");
   vlog_node_t dist = vlog_value(cons_stmt(c2, 0, V_CONS_EXPR));
   ck_assert_vlog_kind(dist, V_DIST);
   ck_assert_int_eq(vlog_params(dist), 3);

   vlog_node_t d0 = vlog_param(dist, 0);
   ck_assert_vlog_kind(d0, V_DIST_ITEM);
   ck_assert_int_eq(vlog_subkind(d0), V_DIST_PER_VALUE);
   fail_if(vlog_has_right(d0));
   ck_assert_int_eq(number_integer(vlog_number(vlog_delay(d0))), 2);

   vlog_node_t d1 = vlog_param(dist, 1);
   ck_assert_int_eq(vlog_subkind(d1), V_DIST_PER_RANGE);
   ck_assert_int_eq(number_integer(vlog_number(vlog_left(d1))), 2);
   ck_assert_int_eq(number_integer(vlog_number(vlog_right(d1))), 3);
   ck_assert_int_eq(number_integer(vlog_number(vlog_delay(d1))), 4);

   vlog_node_t d2 = vlog_param(dist, 2);
   ck_assert_int_eq(vlog_subkind(d2), V_DIST_PER_VALUE);
   ck_assert_int_eq(number_integer(vlog_number(vlog_delay(d2))), 1);

   vlog_node_t c3 = find_decl(base, "c3");
   ck_assert_int_eq(vlog_flags(c3) & VLOG_F_STATIC, VLOG_F_STATIC);
   vlog_node_t imp = cons_stmt(c3, 0, V_CONS_IMPLY);
   ck_assert_vlog_kind(vlog_value(imp), V_BINARY);
   ck_assert_int_eq(vlog_stmts(imp), 1);
   cons_stmt(imp, 0, V_CONS_EXPR);

   vlog_node_t c4 = find_decl(base, "c4");
   ck_assert_int_eq(vlog_stmts(c4), 1);
   vlog_node_t if1 = cons_stmt(c4, 0, V_CONS_IF);
   ck_assert_vlog_kind(vlog_left(if1), V_CONS_BLOCK);
   ck_assert_int_eq(vlog_stmts(vlog_left(if1)), 1);
   ck_assert_vlog_kind(vlog_right(if1), V_CONS_BLOCK);
   ck_assert_int_eq(vlog_stmts(vlog_right(if1)), 1);
   vlog_node_t if2 = cons_stmt(vlog_right(if1), 0, V_CONS_IF);
   ck_assert_int_eq(vlog_stmts(vlog_left(if2)), 2);
   ck_assert_int_eq(vlog_stmts(vlog_right(if2)), 1);

   vlog_node_t c5 = find_decl(base, "c5");
   vlog_node_t fe = cons_stmt(c5, 0, V_CONS_FOREACH);
   ck_assert_vlog_kind(vlog_value(fe), V_REF);
   ck_assert_ptr_eq(vlog_ref(vlog_value(fe)), find_decl(base, "arr"));
   ck_assert_int_eq(vlog_decls(fe), 1);
   vlog_node_t idx = vlog_decl(fe, 0);
   ck_assert_vlog_kind(idx, V_LOCAL_DECL);
   ck_assert_ptr_eq(vlog_ident(idx), ident_new("i"));
   ck_assert_int_eq(vlog_subkind(vlog_type(idx)), DT_INT);
   ck_assert_int_eq(vlog_stmts(fe), 1);
   vlog_node_t fbody = vlog_value(cons_stmt(fe, 0, V_CONS_EXPR));
   ck_assert_vlog_kind(fbody, V_BINARY);
   vlog_node_t sel = vlog_left(fbody);
   ck_assert_vlog_kind(sel, V_BIT_SELECT);
   ck_assert_ptr_eq(vlog_ref(vlog_param(sel, 0)), idx);
   cons_stmt(c5, 1, V_CONS_FOREACH);

   vlog_node_t c6 = find_decl(base, "c6");
   vlog_node_t uniq = cons_stmt(c6, 0, V_CONS_UNIQUE);
   ck_assert_int_eq(vlog_params(uniq), 2);
   vlog_node_t solve = cons_stmt(c6, 1, V_CONS_SOLVE);
   ck_assert_vlog_kind(vlog_left(solve), V_CONCAT);
   ck_assert_int_eq(vlog_params(vlog_left(solve)), 2);
   ck_assert_vlog_kind(vlog_right(solve), V_CONCAT);
   ck_assert_int_eq(vlog_params(vlog_right(solve)), 1);

   vlog_node_t c7 = find_decl(base, "c7");
   vlog_node_t imp2 = cons_stmt(c7, 0, V_CONS_IMPLY);
   ck_assert_int_eq(vlog_stmts(imp2), 2);

   // The caller's scope is skipped by local::
   vlog_node_t again = find_decl(base, "again");
   vlog_node_t ret = vlog_stmt(again, 0);
   ck_assert_vlog_kind(ret, V_RETURN);
   vlog_node_t call = vlog_value(ret);
   ck_assert_vlog_kind(call, V_METHOD_CALL);
   ck_assert_int_eq(vlog_subkind(call), V_METHOD_RANDOMIZE);
   ck_assert_vlog_kind(vlog_value(call), V_THIS);
   ck_assert_int_eq(vlog_params(call), 0);
   ck_assert_int_eq(vlog_stmts(call), 1);
   vlog_node_t eq = vlog_value(cons_stmt(call, 0, V_CONS_EXPR));
   ck_assert_ptr_eq(vlog_ref(vlog_left(eq)), a);
   ck_assert_ptr_eq(vlog_ref(vlog_right(eq)), bx);

   // Built-in methods without a prefix in a class
   vlog_node_t off = find_decl(base, "off");
   ck_assert_int_eq(vlog_stmts(off), 4);
   call = vlog_stmt(off, 0);
   ck_assert_vlog_kind(call, V_METHOD_CALL);
   ck_assert_int_eq(vlog_subkind(call), V_METHOD_RAND_MODE);
   ck_assert_vlog_kind(vlog_value(call), V_THIS);
   call = vlog_stmt(off, 1);
   ck_assert_int_eq(vlog_subkind(call), V_METHOD_RAND_MODE);
   ck_assert_vlog_kind(vlog_value(call), V_REF);
   ck_assert_ptr_eq(vlog_ref(vlog_value(call)), a);
   call = vlog_stmt(off, 2);
   ck_assert_int_eq(vlog_subkind(call), V_METHOD_CONSTRAINT_MODE);
   ck_assert_ptr_eq(vlog_ref(vlog_value(call)), c1);
   call = vlog_stmt(off, 3);
   ck_assert_int_eq(vlog_subkind(call), V_METHOD_CONSTRAINT_MODE);
   ck_assert_vlog_kind(vlog_value(call), V_THIS);

   // A derived class adds its own state only if the base has none
   vlog_node_t derived = find_decl(m, "derived");
   ck_assert_ptr_eq(vlog_ref(derived), base);
   vlog_node_t b = find_decl(derived, "b");
   ck_assert_int_eq(vlog_flags(b) & VLOG_F_RAND, VLOG_F_RAND);
   for (int i = 0; i < vlog_decls(derived); i++) {
      vlog_node_t d = vlog_decl(derived, i);
      if (vlog_kind(d) == V_VAR_DECL)
         fail_if(vlog_ident(d) == ident_new("$rand_off"));
   }

   vlog_node_t k1 = find_decl(derived, "d1");
   vlog_node_t k1eq = vlog_value(cons_stmt(k1, 0, V_CONS_EXPR));
   ck_assert_ptr_eq(vlog_ref(vlog_left(k1eq)), b);
   vlog_node_t k1sum = vlog_right(k1eq);
   ck_assert_ptr_eq(vlog_ref(vlog_left(k1sum)), a);
   ck_assert_ptr_eq(vlog_ref(vlog_right(k1sum)), bx);

   // A member of the object hides a variable of the caller
   vlog_node_t chk = find_decl(m, "chk");
   vlog_node_t local_a = find_decl(chk, "a");
   ck_assert_vlog_kind(local_a, V_LOCAL_DECL);
   call = vlog_value(vlog_stmt(chk, 0));
   ck_assert_vlog_kind(call, V_METHOD_CALL);
   ck_assert_int_eq(vlog_stmts(call), 3);
   eq = vlog_value(cons_stmt(call, 0, V_CONS_EXPR));
   ck_assert_ptr_eq(vlog_ref(vlog_left(eq)), a);
   eq = vlog_value(cons_stmt(call, 1, V_CONS_EXPR));
   ck_assert_ptr_eq(vlog_ref(vlog_left(eq)), b);
   ck_assert_ptr_eq(vlog_ref(vlog_right(eq)), a);
   eq = vlog_value(cons_stmt(call, 2, V_CONS_EXPR));
   ck_assert_ptr_eq(vlog_ref(vlog_left(eq)), bx);
   ck_assert_ptr_eq(vlog_ref(vlog_right(eq)), local_a);

   vlog_node_t mx = find_decl(m, "x");
   vlog_node_t my = find_decl(m, "y");

   vlog_node_t init = vlog_stmt(m, 0);
   ck_assert_vlog_kind(init, V_INITIAL);
   vlog_node_t blk = vlog_stmt(init, 0);
   ck_assert_vlog_kind(blk, V_SEQ_BLOCK);
   ck_assert_int_eq(vlog_stmts(blk), 16);

   vlog_node_t o = find_decl(m, "o");

   call = vlog_value(cons_stmt(blk, 0, V_BASSIGN));
   ck_assert_vlog_kind(call, V_METHOD_CALL);
   ck_assert_int_eq(vlog_subkind(call), V_METHOD_RANDOMIZE);
   ck_assert_ptr_eq(vlog_ref(vlog_value(call)), o);
   ck_assert_int_eq(vlog_params(call), 0);
   ck_assert_int_eq(vlog_stmts(call), 0);

   call = cons_stmt(blk, 1, V_METHOD_CALL);
   ck_assert_int_eq(vlog_subkind(call), V_METHOD_RANDOMIZE);

   call = vlog_value(cons_stmt(blk, 2, V_VOID_CALL));
   ck_assert_int_eq(vlog_subkind(call), V_METHOD_RANDOMIZE);
   ck_assert_int_eq(vlog_params(call), 1);
   ck_assert_ptr_eq(vlog_ref(vlog_param(call, 0)), a);

   // Arguments are properties of the object too
   call = vlog_value(cons_stmt(blk, 3, V_BASSIGN));
   ck_assert_int_eq(vlog_params(call), 2);
   ck_assert_ptr_eq(vlog_ref(vlog_param(call, 1)), r);
   ck_assert_int_eq(vlog_stmts(call), 2);
   eq = vlog_value(cons_stmt(call, 0, V_CONS_EXPR));
   ck_assert_ptr_eq(vlog_ref(vlog_left(eq)), a);
   ck_assert_ptr_eq(vlog_ref(vlog_right(eq)), my);
   eq = vlog_value(cons_stmt(call, 1, V_CONS_EXPR));
   ck_assert_ptr_eq(vlog_ref(vlog_left(eq)), state);
   ck_assert_ptr_eq(vlog_ref(vlog_right(eq)), bx);

   call = vlog_value(cons_stmt(blk, 4, V_BASSIGN));
   ck_assert_int_eq(vlog_stmts(call), 2);
   eq = vlog_value(cons_stmt(call, 0, V_CONS_EXPR));
   ck_assert_ptr_eq(vlog_ref(vlog_left(eq)), b);
   eq = vlog_value(cons_stmt(call, 1, V_CONS_EXPR));
   ck_assert_vlog_kind(vlog_left(eq), V_MEMBER_REF);
   ck_assert_vlog_kind(vlog_value(vlog_left(eq)), V_THIS);
   ck_assert_ptr_eq(vlog_ref(vlog_left(eq)), a);

   // Also for a specialisation of a generic class
   call = vlog_value(cons_stmt(blk, 5, V_BASSIGN));
   vlog_node_t gtype = vlog_get_type(vlog_value(call));
   ck_assert_vlog_kind(gtype, V_CLASS_DECL);
   eq = vlog_value(cons_stmt(call, 0, V_CONS_EXPR));
   ck_assert_ptr_eq(vlog_ref(vlog_left(eq)), find_decl(gtype, "v"));
   find_decl(gtype, "$rand_off");

   call = vlog_value(cons_stmt(blk, 6, V_BASSIGN));
   ck_assert_vlog_kind(call, V_STD_RANDOMIZE);
   ck_assert_int_eq(vlog_params(call), 2);
   ck_assert_ptr_eq(vlog_ref(vlog_param(call, 0)), mx);
   ck_assert_ptr_eq(vlog_ref(vlog_param(call, 1)), my);
   ck_assert_int_eq(vlog_stmts(call), 2);
   eq = vlog_value(cons_stmt(call, 0, V_CONS_EXPR));
   ck_assert_ptr_eq(vlog_ref(vlog_left(eq)), mx);
   in = vlog_value(cons_stmt(call, 1, V_CONS_EXPR));
   ck_assert_vlog_kind(in, V_INSIDE);

   call = cons_stmt(blk, 7, V_STD_RANDOMIZE);
   ck_assert_int_eq(vlog_stmts(call), 0);

   call = cons_stmt(blk, 8, V_METHOD_CALL);
   ck_assert_int_eq(vlog_subkind(call), V_METHOD_RAND_MODE);
   ck_assert_vlog_kind(vlog_value(call), V_MEMBER_REF);
   ck_assert_ptr_eq(vlog_ref(vlog_value(call)), a);
   ck_assert_int_eq(vlog_params(call), 1);

   call = cons_stmt(blk, 9, V_METHOD_CALL);
   ck_assert_int_eq(vlog_subkind(call), V_METHOD_CONSTRAINT_MODE);
   ck_assert_vlog_kind(vlog_value(call), V_MEMBER_REF);
   ck_assert_ptr_eq(vlog_ref(vlog_value(call)), c1);

   call = vlog_value(cons_stmt(blk, 10, V_BASSIGN));
   ck_assert_int_eq(vlog_subkind(call), V_METHOD_RAND_MODE);
   ck_assert_vlog_kind(vlog_value(call), V_REF);
   ck_assert_int_eq(vlog_params(call), 0);

   call = cons_stmt(blk, 11, V_METHOD_CALL);
   ck_assert_int_eq(vlog_subkind(call), V_METHOD_CONSTRAINT_MODE);

   call = cons_stmt(blk, 12, V_METHOD_CALL);
   ck_assert_int_eq(vlog_subkind(call), V_METHOD_RANDOMIZE);

   cons_stmt(blk, 13, V_ASSERT);
   cons_stmt(blk, 14, V_IF);

   vlog_node_t rc = cons_stmt(blk, 15, V_RANDCASE);
   ck_assert_int_eq(vlog_stmts(rc), 2);
   vlog_node_t ri = cons_stmt(rc, 1, V_RANDCASE_ITEM);
   ck_assert_int_eq(number_integer(vlog_number(vlog_value(ri))), 3);
   ck_assert_int_eq(vlog_stmts(ri), 1);
}
END_TEST

START_TEST(test_crv2)
{
   input_from_file(TESTDIR "/vlog/crv2.sv");

   set_default_keywords(VLOG_1800_2023);

   const error_t expect[] = {
      { 11, "duplicate declaration of 'k3'" },
      {  8, "sorry, skipping a loop variable in foreach is not supported" },
      {  9, "no visible declaration for 'zzz'" },
      { 26, "no visible declaration for 'zzz'" },
      { 27, "'x' has no method named 'randomize'" },
      { 28, "sorry, an identifier list after with is not supported" },
      { 29, "rand_mode can only be called on an object or a random "
        "variable" },
      { 30, "constraint_mode can only be called on an object or a "
        "constraint" },
      { 35, "local:: can only be used in the constraint block of a "
        "randomize call" },
      { -1, NULL }
   };
   expect_errors(expect);

   do_parse_only(V_MODULE);

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_crv3)
{
   input_from_file(TESTDIR "/vlog/crv3.sv");

   set_default_keywords(VLOG_1800_2023);

   const error_t expect[] = {
      { 12, "rand variable 'r' must have an integral, enum, or class handle "
        "type or be an array of these" },
      { 14, "randc variable 'h' must have an integral or enum type" },
      { 15, "rand variable 's' must have an integral, enum, or class handle "
        "type or be an array of these" },
      { 18, "constraint expression must have an integral type" },
      { 19, "sorry, real operands in a condition are not supported" },
      { 20, "inside set item must be integral or an array" },
      { 21, "soft constraint cannot be used with dist" },
      { 22, "inside set item must be integral or an array" },
      { 23, "sorry, real operands in a constraint expression are not "
        "supported" },
      { 30, "target cannot be assigned in a procedural block" },
      { 31, "inside set item must be integral or an array" },
      { 32, "expression before inside must have an integral type" },
      { 34, "randcase weight must have an integral type" },
      { -1, NULL }
   };
   expect_errors(expect);

   do_parse_check(V_MODULE);

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_crv4)
{
   // The keywords added for constrained random are identifiers in older
   // standards
   input_from_file(TESTDIR "/vlog/crv4.v");

   do_parse_check(V_MODULE);
   do_parse_check(V_MODULE);

   fail_unless(vlog_parse() == NULL);

   fail_if_errors();
}
END_TEST

START_TEST(test_inval1)
{
   const error_t expect[] = {
      {  2, "`pragma requires a pragma name" },
      {  3, "`line requires a line number" },
      {  4, "`line requires a line number" },
      {  5, "`line requires a line number" },
      {  6, "`line requires a line number" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/inval1.sv");

   LOCAL_TEXT_BUF tb = tb_new();
   vlog_preprocess(tb, false);

   check_expected_errors();
}
END_TEST

START_TEST(test_inval3)
{
   const error_t expect[] = {
      {  7, "return statement in a void function cannot have an expression" },
      { 10, "index expression cannot have real type" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/inval3.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_inval2)
{
   const error_t expect[] = {
      {  4, "width of an indexed part-select must be positive" },
      {  4, "width of an indexed part-select must be positive" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/inval2.sv");

   vlog_node_t m = do_parse_check(V_MODULE);
   vlog_fold(m, get_mir(), get_jit());

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_assoc1)
{
   const error_t expect[] = {
      {  2, "the index of an associative array must be an integral type or "
         "string" },
      {  3, "the index of an associative array must be an integral type or "
         "string" },
      {  7, "foreach over an associative array with a wildcard index is not "
         "allowed" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/assoc1.sv");

   (void)do_parse_only(V_MODULE);

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_assoc2)
{
   const error_t expect[] = {
      {  8, "index of an associative array with a string index must have "
         "type string" },
      {  9, "method 'exists' of an associative array expects 1 argument but "
         "has 0" },
      { 10, "target cannot be assigned in a procedural block" },
      { 11, "method 'num' of an associative array expects 0 arguments but "
         "has 1" },
      { 12, "argument of an associative array with an integral index must "
         "have an integral type" },
      { 13, "value is not compatible with an associative array of a "
         "different type" },
      { 14, "value is not compatible with an associative array of a "
         "different type" },
      { 15, "operator cannot be applied to an associative array" },
      { 16, "index of an associative array with an integral index must have "
         "an integral type" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/assoc2.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_scope1)
{
   const error_t expect[] = {
      {  9, "'missing' is not a member of class 'A'" },
      { 11, "no package named 'nopkg' in library WORK or any other library on the search path" },
      { 12, "'other' is not a member of class 'A'" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/scope1.sv");

   (void)do_parse_only(V_MODULE);

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_extern1)
{
   const error_t expect[] = {
      { 12, "ports of 'f' do not match the prototype in class 'A'" },
      { 16, "'g' was not declared extern in class 'A'" },
      { 20, "class 'A' has no function named 'h'" },
      { 24, "no class named 'B'" },
      { 28, "ports of 't' do not match the prototype in class 'A'" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/extern1.sv");

   (void)do_parse_only(V_MODULE);

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_cast1)
{
   const error_t expect[] = {
      { 13, "$cast expects 2 arguments but has 1" },
      { 15, "a class handle can only be cast to a class handle" },
      { 16, "the source of $cast to a class handle must be a class handle" },
      { 17, "target cannot be assigned in a procedural block" },
      { 18, "sorry, $cast to or from this type is not supported" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/cast1.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_ifc1)
{
   const error_t expect[] = {
      {  4, "an interface class cannot have properties" },
      {  5, "methods of an interface class must be pure virtual" },
      { 13, "class 'NotIface' is not an interface class" },
      { 16, "class 'C3' cannot extend the interface class 'IA': use "
         "implements" },
      { 19, "an interface class cannot implement another: use extends" },
      { 23, "an interface class cannot be nested within another interface "
         "class" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/ifc1.sv");

   (void)do_parse_only(V_MODULE);

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_ifc2)
{
   const error_t expect[] = {
      {  6, "class 'C1' does not implement method 'f' of interface class "
         "'IA'" },
      { 10, "method 'f' has a different number of arguments than in "
         "interface class 'IA'" },
      { 24, "the name 'T' must be defined in 'P3' due to having conflicting "
         "visibility in multiple base classes" },
      { 40, "method 'm' has a different return type than in interface class "
         "'R2'" },
      { 45, "cannot create an instance of interface class 'IA'" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/ifc2.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_invalid1)
{
   const error_t expect[] = {
      { 10, "virtual method 'check' has a different number of arguments or "
        "return type than the method in class 'base'" },
      { 32, "constructor of class 'rect' expects 2 arguments but have 1" },
      { 34, "constructor of class 'point' expects 0 arguments but have 1" },
      { 39, "variable 'q' written in an always_ff process cannot be written "
        "by any other process" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/invalid1.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_virtover1)
{
   const error_t expect[] = {
      { 11, "virtual method 'f' has a different number of arguments or return "
        "type than the method in class 'base'" },
      { 14, "type of argument 's' of virtual method 'g' in class 'd2' does "
        "not match the type of the argument in superclass 'base'" },
      { 17, "direction of argument 'a' for virtual method 't' in class 'd3' "
        "does not match the direction in superclass 'base'" },
      { 20, "virtual method 'h' has a different number of arguments or return "
        "type than the method in class 'base'" },
      { 23, "argument name 'b' for virtual method 'k' in class 'd5' does not "
        "match the argument name 'a' in superclass 'base'" },
      { 26, "task 'f' in class 'd6' cannot override function in superclass "
        "'base'" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/virtover1.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_process1)
{
   const error_t expect[] = {
      {  3, "new class expression must have class type" },
      {  7, "'p' has no method named 'frobnicate'" },
      {  8, "process has no member named 'bogus'" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/process1.sv");

   (void)do_parse_only(V_MODULE);

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_process2)
{
   const error_t expect[] = {
      {  6, "method 'kill' of process expects no arguments but has 1" },
      {  7, "value of type 'integral' is not compatible with 'process'" },
      {  8, "value of type 'process' is not compatible with 'int'" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/process2.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_soft1)
{
   const error_t expect[] = {
      {  5, "the operand of disable soft must be a random variable" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/soft1.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_cons1)
{
   const error_t expect[] = {
      {  7, "an extern or pure constraint must not have a block" },
      { 12, "constraint 'c3' was not declared extern in class 'A'" },
      { 14, "class 'A' has no constraint named 'cz'" },
      { 16, "no class named 'B'" },
      { 18, "the static qualifier of constraint 'c2' does not match the "
         "prototype" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/cons1.sv");

   (void)do_parse_only(V_MODULE);

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_cons2)
{
   const error_t expect[] = {
      {  4, "constraint 'c1' of class 'A' is declared extern but is never "
         "defined" },
      {  5, "constraint 'c2' of class 'A' is declared without a block but is "
         "never defined" },
      {  6, "method 'f' of class 'A' is declared extern but is never "
         "defined" },
      {  7, "a pure constraint can only be declared in a virtual class" },
      { 16, "class 'C' does not implement the pure constraint 'p2'" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/cons2.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_crv5)
{
   const error_t expect[] = {
      { 6, "randc variable 'b2' cannot be used in a solve before constraint" },
      { 7, "randc variable 'b2' cannot be used in a soft constraint" },
      { 8, "randc variable 'b3' cannot be used in a dist constraint" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/crv5.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_inval4)
{
   const error_t expect[] = {
      { 52, "packed dimensions cannot be added to unpacked data type" },
      { 53, "unsized dimension is not allowed in $bits" },
      { 54, "queue dimension is not allowed in $bits" },
      { 87, "a class constructor cannot be declared virtual" },
      { 92, "cannot redeclare enclosing function name 'f_redecl_var'" },
      { 97, "cannot redeclare enclosing function name 'f_redecl_type'" },
      { 102, "cannot redeclare enclosing function name 'f_redecl_blk'" },
      {  8, "the size of the value of enumeration name 'G' is 4 but the base "
         "type has 3 bits" },
      {  9, "the value of enumeration name 'Q' cannot contain x or z as the "
         "base type is two-state" },
      { 10, "enumeration name 'U' without a value cannot follow a name with "
         "an unknown value" },
      { 13, "a member of a packed structure cannot have a default value" },
      { 19, "built-in method 'rand_mode' cannot be overridden" },
      { 22, "a pure virtual method can only be declared in a virtual class" },
      { 26, "a return statement is not allowed inside a fork block" },
      { 45, "cannot assign this value to a variable of enumeration type "
         "without a cast" },
      { 47, "enumeration name 'W1' overflows base type" },
      { 62, "string literal can only be assigned to a 1-dimensional unpacked "
         "array of 8-bit 2-state elements" },
      { 36, "cannot assign this value to a variable of enumeration type "
         "without a cast" },
      { 37, "an arithmetic assignment cannot be used with a variable of "
         "enumeration type" },
      { 38, "cannot assign this value to a variable of enumeration type "
         "without a cast" },
      { 39, "cannot assign this value to a variable of enumeration type "
         "without a cast" },
      { 41, "an edge event control cannot be used with a real value" },
      { 58, "cannot assign to const variable 'cvar1'" },
      { 64, "array assignment pattern key must be a constant expression" },
      { 65, "unpacked array cannot be used in an lvalue concatenation" },
      { 66, "non-integral target of type 'string' cannot be used in an "
         "lvalue concatenation" },
      { 67, "non-integral target of type 'dynamic array' cannot be used in "
         "an lvalue concatenation" },
      { 68, "cannot assign to const variable 'cvar2'" },
      { 69, "part-select direction [0:1] does not match declared range [3:0]" },
      { 70, "an edge event control cannot be used with a named event" },
      { 71, "method 'push_back' of a queue expects 1 argument but has 0" },
      { 72, "method 'insert' of a queue expects 2 arguments but has 1" },
      { 73, "cannot cast string to 'real'" },
      { 74, "cannot cast queue to 'real'" },
      { 75, "duplicate default item in assignment pattern" },
      { 76, "cannot mix positional and keyed items in assignment pattern" },
      { 77, "duplicate key 1 in array assignment pattern" },
      { 78, "array assignment pattern key 4 is outside declared range [0:3]" },
      { 79, "keyed array assignment pattern does not cover all 4 elements and "
         "has no default" },
      { 80, "array assignment pattern has 3 elements but target array has 4" },
      { 81, "assignment pattern replication count must be positive" },
      { 82, "cannot assign scalar value to unpacked array" },
      { 83, "'$' index is only allowed for queues" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/inval4.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_callorder1)
{
   const error_t expect[] = {
      {  9, "'f_ev' is not a task" },
      { 10, "'t_var' is not a task" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/callorder1.sv");

   vlog_check(do_parse_only(V_NAMESPACE));
   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_final1)
{
   const error_t expect[] = {
      {  5, "a delay control inside a final block is illegal as final "
         "procedures must not block" },
      {  6, "an event control inside a final block is illegal as final "
         "procedures must not block" },
      {  7, "a wait statement inside a final block is illegal as final "
         "procedures must not block" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/final1.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_nba1)
{
   const error_t expect[] = {
      { 10, "a non-blocking assignment cannot write to an automatic" },
      { 11, "a non-blocking assignment cannot write to an automatic" },
      { 12, "a non-blocking assignment cannot write to an automatic" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/nba1.sv");

   (void)do_parse_only(V_NAMESPACE);
   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_rs1)
{
   const error_t expect[] = {
      {  5, "no production named 'nothere'" },
      { 11, "sorry, the recursive production 'a' is not supported" },
      { 16, "too many arguments for production 'add'" },
      { 16, "missing argument for 'y' of production 'add'" },
      { 20, "no production named 'missing'" },
      { 26, "sorry, rand join of more than three productions is not "
        "supported" },
      { 34, "no visible declaration for 'zzz'" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/rs1.sv");

   (void)do_parse_only(V_MODULE);

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_taskout1)
{
   const error_t expect[] = {
      { 15, "illegal output argument for 'y'" },
      { 16, "illegal inout argument for 'z'" },
      { 17, "illegal inout argument for 'z'" },
      { 18, "illegal output argument for 'y'" },
      { 19, "illegal output argument for 'y'" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/taskout1.sv");

   (void)do_parse_only(V_NAMESPACE);
   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_crosshier1)
{
   const error_t expect[] = {
      { 12, "cross items must be coverpoints or variables; declare a "
        "coverpoint" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/crosshier1.sv");

   for (int i = 0; i < 2; i++)
      fail_if(vlog_parse() == NULL);

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_casereal1)
{
   const error_t expect[] = {
      { 10, "case equality is not allowed on an aggregate with a real member" },
      { 11, "case equality is not allowed on an aggregate with a real member" },
      { 14, "invalid operands for binary expression" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/casereal1.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_newtype1)
{
   const error_t expect[] = {
      {  7, "'Other' is a type but new must be followed by an object handle" },
      {  8, "'C_t' is a type but new must be followed by an object handle" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/newtype1.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_unpackeq1)
{
   const error_t expect[] = {
      { 12, "cannot assign unpacked array a to unpacked array b as their "
        "element types are not equivalent" },
      { 13, "cannot assign unpacked array a to unpacked array c as their "
        "element types are not equivalent" },
      { 14, "cannot assign unpacked array qa to unpacked array qb as their "
        "element types are not equivalent" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/unpackeq1.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_discard1)
{
   const error_t expect[] = {
      { 20, "return value of function 'f' is discarded" },
      { 21, "return value of function 'm' is discarded" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/discard1.sv");

   vlog_check(do_parse_only(V_NAMESPACE));
   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_fwdarg1)
{
   const error_t expect[] = {
      {  7, "'later_sig' is used before its declaration" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/fwdarg1.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_modportin1)
{
   const error_t expect[] = {
      { 11, "cannot assign a which is an input of modport mp" },
      { 13, "cannot assign a which is an input of modport mp" },
      { 14, "cannot assign v which is an input of modport mp" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/modportin1.sv");

   for (int i = 0; i < 2; i++) {
      vlog_node_t m = vlog_parse();
      fail_if(m == NULL);
      vlog_check(m);
   }

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_pkghier1)
{
   const error_t expect[] = {
      { 15, "hierarchical reference to 'top' in package 'pkghier1'" },
      {  8, "hierarchical reference to 'top' in package 'pkghier1'" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/pkghier1.sv");

   vlog_check(do_parse_only(V_PACKAGE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_vifmodportin1)
{
   input_from_file(TESTDIR "/vlog/vifmodportin1.sv");

   const error_t expect[] = {
      { 15, "cannot assign a which is an input of modport mp" },
      { 26, "cannot assign v which is an input of modport mp" },
      { 31, "cannot assign a which is an input of modport mp" },
      { 32, "cannot assign a which is an input of modport mp" },
      { 33, "cannot assign v which is an input of modport mp" },
      { 34, "cannot assign v which is an input of modport mp" },
      { 35, "cannot assign a which is an input of modport mp" },
      { 36, "cannot assign a which is an input of modport mp" },
      { -1, NULL }
   };
   expect_errors(expect);

   for (int i = 0; i < 3; i++) {
      vlog_node_t m = vlog_parse();
      fail_if(m == NULL);
      vlog_check(m);
   }

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_lifetime1)
{
   input_from_file(TESTDIR "/vlog/lifetime1.sv");

   const error_t expect[] = {
      { 12, "initialised variable 'a' in a static subroutine must be "
         "declared static or automatic" },
      { 24, "initialised variable 'k' in a static block must be declared" },
      { 26, "initialised variable 'loop3' in a static block must be" },
      { 32, "initialised variable 'v' in a static block must be declared" },
      { 32, "initialised variable 'w' in a static block must be declared" },
      { 44, "initial value of static variable 'r' cannot refer to "
         "automatic argument 'p'" },
      { 49, "initial value of static variable 'q' cannot refer to "
         "automatic argument 'a'" },
      { -1, NULL }
   };
   expect_errors(expect);

   fail_if(vlog_parse() == NULL);
   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_dpiopen1)
{
   input_from_file(TESTDIR "/vlog/dpiopen1.sv");

   const error_t expect[] = {
      { 12, "exported DPI subroutine 'exp_open' cannot have dynamic array "
        "argument 'x'" },
      { 16, "unsized packed dimension is only allowed in a formal argument "
        "of a DPI import" },
      { 28, "dynamic array cannot be passed to unsized output argument 'x' "
        "of DPI import 'imp_out'" },
      { 29, "actual argument is not compatible with open array argument "
        "'x'" },
      { 30, "actual argument is not compatible with open array argument "
        "'x'" },
      { 31, "actual argument is not compatible with open array argument "
        "'x'" },
      { -1, NULL }
   };
   expect_errors(expect);

   vlog_node_t m = vlog_parse();
   fail_if(m == NULL);
   vlog_check(m);
   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_nettype1)
{
   const error_t expect[] = {
      { 21, "return type of resolution function 'bad_ret' must be the data "
        "type of nettype n1" },
      { 22, "resolution function 'bad_arg' must have a single input argument "
        "that is a dynamic array of the data type of nettype n2" },
      { 23, "resolution function 'bad_two' must have a single input argument "
        "that is a dynamic array of the data type of nettype n3" },
      { 25, "resolution function 'bad_dir' must have a single input argument "
        "that is a dynamic array of the data type of nettype n5" },
      { 26, "'bad_task' is not a function" },
      { 27, "invalid data type string for nettype n7" },
      { 28, "invalid data type for nettype n8" },
      { 29, "invalid data type for nettype n9" },
      { 38, "net 'q' of nettype plain without a resolution function cannot "
        "have more than one driver" },
      { 39, "'a' cannot be assigned in a procedural block" },
      { 40, "'p' cannot be assigned in a procedural block" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/nettype1.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_interconnect1)
{
   const error_t expect[] = {
      {  6, "interconnect 'p' can only be used in a port connection" },
      { 14, "interconnect 'ic' can only be used in a port connection" },
      { 15, "interconnect 'ic' can only be used in a port connection" },
      { 16, "interconnect 'ib' can only be used in a port connection" },
      { 19, "interconnect 'ic' can only be used in a port connection" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/interconnect1.sv");

   for (int i = 0; i < 2; i++) {
      vlog_node_t m = vlog_parse();
      fail_if(m == NULL);
      vlog_check(m);
   }

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_svalocal1)
{
   const error_t expect[] = {
      { 21, "local variable 'y' cannot be used in a disable iff condition" },
      { 22, "local variable 'x' cannot be used in a disable iff condition" },
      { 26, "local variable 'x' may be unassigned here" },
      { 29, "local variable 'x' is assigned in both operands of and and "
        "cannot be referenced after it" },
      { 30, "local variable 'x' is assigned in both operands of intersect "
        "and cannot be referenced after it" },
      { 31, "local variable 'x' may be unassigned here" },
      { 37, "local variable 'x' may be unassigned here" },
      { 38, "function 'fo' with an output argument cannot be called in a "
        "sequence or property expression" },
      { 39, "function 'fr' with a ref argument cannot be called in a "
        "sequence or property expression" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/svalocal1.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_functask1)
{
   const error_t expect[] = {
      {  6, "task 't' cannot be called from a function as functions must "
         "not block" },
      { 14, "task 't' cannot be called from a function as functions must "
         "not block" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/functask1.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_config1)
{
   input_from_file(TESTDIR "/vlog/config1.sv");

   const error_t expect[] = {
      { 12, "unexpected default while parsing configuration declaration, "
        "expecting design" },
      { 18, "configuration 'cfg_twodefault' has more than one default "
        "clause" },
      { 23, "'other' is not a top-level cell of configuration 'cfg_badpath'" },
      { 27, "unexpected parameter while parsing configuration declaration" },
      { 33, "'cfg_wrong' does not match configuration name 'cfg_label'" },
      { -1, NULL }
   };
   expect_errors(expect);

   while (vlog_parse() != NULL)
      ;

   check_expected_errors();
}
END_TEST

START_TEST(test_refarg1)
{
   const error_t expect[] = {
      { 10, "ref argument 'r' is not allowed in static task 't3'" },
      { 13, "illegal ref argument for 'r'" },
      { 14, "illegal ref argument for 'r'" },
      { 15, "type of actual for ref argument 'r' is not equivalent to int" },
      { 17, "'v' is not an event" },
      { 18, "'v' is not an event" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/refarg1.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_binnand1)
{
   const error_t expect[] = {
      {  6, "'~&' is a unary reduction operator and not a binary operator" },
      {  7, "'~|' is a unary reduction operator and not a binary operator" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/binnand1.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_netdtype1)
{
   const error_t expect[] = {
      { 10, "net 'bad_real' cannot have a real data type" },
      { 11, "net 'bad_class' cannot have a class handle data type" },
      { 12, "net 'bad_string' cannot have a string data type" },
      { 13, "net 'bad_bit' cannot have a 2-state data type" },
      { 14, "net 'bad_vec' cannot have a 2-state data type" },
      { 15, "net 'bad_int' cannot have a 2-state data type" },
      { 16, "net 'bad_struct' cannot have a 2-state packed data type" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/netdtype1.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_taskexpr1)
{
   const error_t expect[] = {
      { 19, "task 't' cannot be called in an expression" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/taskexpr1.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_packedmem1)
{
   const error_t expect[] = {
      {  8, "member 'r' of a packed structure cannot be a real" },
      {  9, "member 's' of a packed structure cannot be a string" },
      { 10, "member 'e' of a packed structure cannot be an event" },
      { 11, "member 'sp' of a packed structure cannot be an unpacked "
        "structure or union" },
      { 12, "member 'ua' of a packed structure cannot be an unpacked "
        "array" },
      { 13, "member 'c' of a packed structure cannot be a class handle" },
      { 18, "member 'sr' of a packed union cannot be a real" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/packedmem1.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_finaltask1)
{
   const error_t expect[] = {
      {  9, "task 't' cannot be called from a final procedure as it must "
        "not block" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/finaltask1.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_repl2)
{
   const error_t expect[] = {
      {  9, "a replication with a constant of zero can only be an operand"
        " of a concatenation with another operand" },
      { 10, "a replication with a constant of zero can only be an operand"
        " of a concatenation with another operand" },
      { 11, "the replication constant cannot be negative" },
      { 12, "the replication constant cannot have an unknown value" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/repl2.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_defdup1)
{
   const error_t expect[] = {
      {  6, "only one default clocking block is allowed in a module or "
        "generate block" },
      {  9, "only one default disable iff is allowed in a module or "
        "generate block" },
      { 12, "only one default disable iff is allowed in a module or "
        "generate block" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/defdup1.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_classitem1)
{
   const error_t expect[] = {
      { 16, "only a class method can be declared virtual" },
      { 17, "an import statement is not allowed directly within a class "
        "scope" },
      { 20, "super.new must be the first statement in the constructor" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/classitem1.sv");

   do_parse_only(V_PACKAGE);
   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_assertlabel1)
{
   const error_t expect[] = {
      {  6, "duplicate declaration of 'covlabel'" },
      { 10, "duplicate declaration of 'CHECK'" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/assertlabel1.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_lifetime2)
{
   const error_t expect[] = {
      {  4, "a variable declared in a module, interface, program or "
        "package cannot be automatic" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/lifetime2.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_genvar3)
{
   const error_t expect[] = {
      {  6, "the loop variable 'i' of a generate for must be a genvar" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/genvar3.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_waitevent1)
{
   const error_t expect[] = {
      {  7, "the condition of a wait statement cannot be a named event" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/waitevent1.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_forcesel1)
{
   const error_t expect[] = {
      { 13, "the index of a select in the target of a force or release "
        "must be constant" },
      { 14, "the index of a select in the target of a force or release "
        "must be constant" },
      { 15, "the range of a select in the target of a force or release "
        "must be constant" },
      { 16, "the range of a select in the target of a force or release "
        "must be constant" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/forcesel1.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_castsize1)
{
   const error_t expect[] = {
      {  6, "the width of a size cast must be positive but is -1" },
      {  7, "the width of a size cast must be positive but is 0" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/castsize1.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_dimsize1)
{
   const error_t expect[] = {
      {  3, "the size of an unpacked array dimension must be positive" },
      {  4, "the size of an unpacked array dimension must be positive" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/dimsize1.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_selcount1)
{
   const error_t expect[] = {
      { 10, "cannot select a bit or part of 'unk' which is a scalar" },
      { 11, "too many selects for 'm' which has only 2 dimensions" },
      { 12, "too many selects for 'mem' which has only 2 dimensions" },
      {  9, "cannot select a bit or part of 'b' which is a scalar" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/selcount1.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_dotname1)
{
   const error_t expect[] = {
      {  7, "no visible declaration for 'o'" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/dotname1.sv");

   do_parse_only(V_MODULE);
   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_dotstar2)
{
   const error_t expect[] = {
      {  6, "only one .* port connection is allowed in an instance" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/dotstar2.sv");

   do_parse_only(V_MODULE);
   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_crash1)
{
   const error_t expect[] = {
      {  6, "no visible declaration for 'MISSING'" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/crash1.sv");

   while (vlog_parse() != NULL)
      ;

   check_expected_errors();
}
END_TEST

START_TEST(test_crash2)
{
   const error_t expect[] = {
      {  3, "no visible label 'abcd'" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/crash2.sv");

   while (vlog_parse() != NULL)
      ;

   check_expected_errors();
}
END_TEST

START_TEST(test_crash3)
{
   const error_t expect[] = {
      {  2, "recursive use of let 'recurse' is not allowed" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/crash3.sv");

   while (vlog_parse() != NULL)
      ;

   check_expected_errors();
}
END_TEST

START_TEST(test_crash4)
{
   const error_t expect[] = {
      {  2, "the size of a number cannot be greater than 16777216 bits" },
      {  3, "the size of a number must be greater than zero" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/crash4.sv");

   while (vlog_parse() != NULL)
      ;

   check_expected_errors();
}
END_TEST

START_TEST(test_crash5)
{
   const error_t expect[] = {
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/crash5.sv");

   while (vlog_parse() != NULL)
      ;

   check_expected_errors();
}
END_TEST

START_TEST(test_crash6)
{
   const error_t expect[] = {
      {  1, "class 'self_ext' cannot be derived from itself" },
      { 13, "class 'c' cannot be derived from itself" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/crash6.sv");

   while (vlog_parse() != NULL)
      ;

   check_expected_errors();
}
END_TEST

START_TEST(test_crash7)
{
   const error_t expect[] = {
      {  8, "specialisations of class 'cls_a' are nested too deeply: the "
        "parameters may be circular" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/crash7.sv");

   while (vlog_parse() != NULL)
      ;

   check_expected_errors();
}
END_TEST

START_TEST(test_crash8)
{
   input_from_file(TESTDIR "/vlog/crash8.sv");

   const error_t expect[] = {
      {  3, "macro 'e' is nested too deeply: it may expand itself "
         "recursively" },
      { -1, NULL }
   };
   expect_errors(expect);

   LOCAL_TEXT_BUF tb = tb_new();
   vlog_preprocess(tb, true);

   check_expected_errors();
}
END_TEST

START_TEST(test_crash9)
{
   add_include_dir(TESTDIR "/vlog");

   input_from_file(TESTDIR "/vlog/crash9.sv");

   const error_t expect[] = {
      {  2, "`include nested too deeply: file crash9.sv may include itself" },
      { -1, NULL }
   };
   expect_errors(expect);

   LOCAL_TEXT_BUF tb = tb_new();
   vlog_preprocess(tb, true);

   check_expected_errors();
}
END_TEST

START_TEST(test_crash10)
{
   const error_t expect[] = {
      { 3, "method 'getc' of a string expects 1 argument but has 0" },
      { 4, "method 'getc' of a string expects 1 argument but has 2" },
      { 5, "method 'len' of a string expects 0 arguments but has 1" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/crash10.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_crash11)
{
   const error_t expect[] = {
      {  3, "sorry, a coverpoint label that is also a name in its expression "
         "is not supported in a covergroup" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/crash11.sv");

   while (vlog_parse() != NULL)
      ;

   check_expected_errors();
}
END_TEST

START_TEST(test_crash12)
{
   const error_t expect[] = {
      {  4, "always procedure has no timing control, delay or blocking "
         "statement so it would loop forever without advancing time" },
      { 10, "always procedure has no timing control, delay or blocking "
         "statement so it would loop forever without advancing time" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/crash12.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_crash13)
{
   // A hierarchical name which is not resolved until elaboration is the
   // target of an assignment
   const error_t expect[] = {
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/crash13.sv");

   while (vlog_parse() != NULL)
      ;

   check_expected_errors();
}
END_TEST

START_TEST(test_crash14)
{
   // A default value for a class parameter is missing

   const error_t expect[] = {
      {  2, "missing default value for parameter W" },
      {  7, "parameter 'W' of class 'gen' has no default value" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/crash14.sv");

   while (vlog_parse() != NULL)
      ;

   check_expected_errors();
}
END_TEST

START_TEST(test_crash15)
{
   // The width of an unresolved hierarchical reference in a clocking
   // block is not known

   const error_t expect[] = {
      {  3, "cannot determine the width of the expression for clocking "
         "signal q" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/crash15.sv");

   while (vlog_parse() != NULL)
      ;

   check_expected_errors();
}
END_TEST

START_TEST(test_crash16)
{
   // Skipping a bind directive after an error must not consume the next
   // semicolon

   const error_t expect[] = {
      {  4, "cannot find module or interface NOSUCH to bind into" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/crash16.sv");

   while (vlog_parse() != NULL)
      ;

   check_expected_errors();
}
END_TEST

START_TEST(test_crash17)
{
   // The same for an item in a clocking block

   const error_t expect[] = {
      {  3, "sorry, this item is not supported in a clocking block" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/crash17.sv");

   while (vlog_parse() != NULL)
      ;

   check_expected_errors();
}
END_TEST

START_TEST(test_crash18)
{
   // A pattern match in a statement that is not allowed at this level must
   // not leave the scope of its variables open at the end of the file

   const error_t expect[] = {
      {  2, "no visible declaration for 'u'" },
      {  2, "the value matched by a tagged pattern must have a tagged "
         "union type" },
      {  2, "unexpected = while parsing hierarchical instance, expecting "
         "identifier" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/crash18.sv");

   while (vlog_parse() != NULL)
      ;

   check_expected_errors();
}
END_TEST

START_TEST(test_genvaruse1)
{
   const error_t expect[] = {
      { 9, "genvar 'j' used outside generate for loop" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/genvaruse1.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_gendup1)
{
   const error_t expect[] = {
      {  5, "duplicate declaration of 'block'" },
      {  9, "duplicate declaration of 'block1'" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/gendup1.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_impimp1)
{
   const error_t expect[] = {
      { 12, "no data type declaration for 'base_cfg'" },
      { 12, "base of class must be a class type" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/impimp1.sv");

   for (int i = 0; i < 3; i++)
      vlog_check(do_parse_only(V_PACKAGE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_puremeth1)
{
   const error_t expect[] = {
      { 16, "class 'bad' does not implement the pure virtual method 'f'" },
      { 16, "class 'bad' does not implement the pure virtual method 't'" },
      { 19, "class 'bad2' does not implement the pure virtual method 't'" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/puremeth1.sv");

   vlog_check(do_parse_only(V_PACKAGE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_letbad1)
{
   const error_t expect[] = {
      { 7, "let 'add1' cannot be called as a statement" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/letbad1.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_misstatic1)
{
   const error_t expect[] = {
      { 12, "cannot call non-static method 'ns' without an object handle" },
      { 13, "cannot call non-static method 'nsf' without an object handle" },
      { 25, "cannot call non-static method 'nsf' from a static method" },
      { 26, "cannot call non-static method 'ns' from a static method" },
      { 35, "cannot call non-static method 'ns' without an object handle" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/misstatic1.sv");

   vlog_check(do_parse_only(V_PACKAGE));
   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_override1)
{
   const error_t expect[] = {
      {  6, "method 'bad_root' with :extends specifier does not override any "
         "superclass method" },
      { 11, "method 'm_ok' with :extends specifier does not override any "
         "superclass method" },
      { 13, "cannot override final method 'm_f' of superclass 'base'" },
      { 14, "cannot override final method 'm_if' of superclass 'base'" },
      { 18, "method 'm_i' with :initial specifier cannot override method in "
         "superclass 'child'" },
      { 24, "class 'bad_sub' cannot extend final class 'leaf'" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/override1.sv");

   vlog_check(do_parse_only(V_PACKAGE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_constprop1)
{
   const error_t expect[] = {
      { 11, "cannot assign to const variable 'gs'" },
      { 12, "cannot assign to const variable 'sg'" },
      { 13, "cannot assign to const variable 'g'" },
      { 17, "cannot assign to const variable 'gs'" },
      { 18, "cannot assign to const variable 'sg'" },
      { 22, "cannot assign to const variable 'inst_c'" },
      { 29, "cannot assign to const variable 'gs'" },
      { 30, "cannot assign to const variable 'sg'" },
      { 31, "cannot assign to const variable 'g'" },
      { 32, "cannot assign to const variable 'inst_c'" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/constprop1.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_castcls1)
{
   const error_t expect[] = {
      { 18, "cannot statically cast class 'base' to incompatible class 'derived'" },
      { 19, "cannot statically cast class 'derived' to incompatible class 'unrelated'" },
      { 20, "cannot cast int to class 'derived'" },
      { 21, "cannot cast class 'derived' to 'int'" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/castcls1.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_countbits1)
{
   const error_t expect[] = {
      { 15, "argument to '$countones' must be a bit vector" },
      { 16, "argument to '$countbits' must be a bit vector" },
      { 17, "argument to '$onehot' must be a bit vector" },
      { 18, "argument to '$onehot0' must be a bit vector" },
      { 19, "argument to '$isunknown' must be a bit vector" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/countbits1.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_funcnb1)
{
   const error_t expect[] = {
      { 9, "non-blocking assignment is not allowed in a final procedure" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/funcnb1.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_funcnb2)
{
   const error_t expect[] = {
      { 7, "non-blocking assignment is not allowed in a function" },
      { -1, NULL }
   };
   expect_errors(expect);

   set_default_keywords(VLOG_1364_2005);
   input_from_file(TESTDIR "/vlog/funcnb2.v");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_dpiname1)
{
   const error_t expect[] = {
      { 4, "DPI-C identifier 'bad.import' is not a valid C identifier" },
      { 6, "DPI-C identifier 'bad.export' is not a valid C identifier" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/dpiname1.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_evnba1)
{
   const error_t expect[] = {
      { 20, "a non-blocking event trigger cannot target an automatic variable" },
      { 28, "a non-blocking event trigger cannot target an element of a "
        "dynamically sized array" },
      { 29, "a non-blocking event trigger cannot target an element of a "
        "dynamically sized array" },
      { 30, "a non-blocking event trigger cannot target an element of a "
        "dynamically sized array" },
      { 31, "a non-blocking event trigger cannot target an element of a "
        "dynamically sized array" },
      { 32, "a non-blocking event trigger cannot target an element of a "
        "dynamically sized array" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/evnba1.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_covbin1)
{
   const error_t expect[] = {
      {  8, "duplicate bin name 'a' in coverpoint 'cp1'" },
      { 12, "duplicate bin name 'a' in coverpoint 'cp2'" },
      { 16, "a default bin cannot be declared as ignore_bins" },
      { 20, "a default bin cannot be declared as ignore_bins" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/covbin1.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_methargs1)
{
   const error_t expect[] = {
      { 24, "cannot pass class 'item' to argument 's' of type 'string'" },
      { 25, "cannot pass string to class argument 'it'" },
      { 27, "expected at least 1 argument for 'sample' but have 0" },
      { 28, "expected at most 2 argument for 'sample' but have 3" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/methargs1.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_covreal1)
{
   const error_t expect[] = {
      { 14, "wildcard bins are not allowed on a real coverpoint" },
      { 17, "transition bins are not allowed on a real coverpoint" },
      { 20, "with clause is not allowed on a non-integral coverpoint" },
      { 23, "with clause is not allowed on a non-integral coverpoint" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/covreal1.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_dpichk1)
{
   const error_t expect[] = {
      {  3, "subroutine 'dup_t' is already exported via DPI-C" },
      {  8, "DPI-C import 'c_fn' conflicts with previous declaration" },
      { 15, "DPI-C unpacked struct member 'q' has a type not allowed in DPI-C" },
      { 16, "DPI-C unpacked struct member 'u' cannot be an unpacked union" },
      { 17, "DPI-C unpacked struct member 'e' has a type not allowed in DPI-C" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/dpichk1.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_pkgexp1)
{
   const error_t expect[] = {
      { 13, "cannot export 'y' from package 'WORK.P1' without importing it" },
      { 18, "'y' is not exported by package 'WORK.P_OK'" },
      { 23, "exported symbol 'x' conflicts with local declaration in package 'WORK.P_BAD3'" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/pkgexp1.sv");

   for (int i = 0; i < 5; i++)
      vlog_check(do_parse_only(V_PACKAGE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_typeref1)
{
   const error_t expect[] = {
      {  5, "type reference can only be compared with another type reference" },
      { 10, "type reference can only be compared with another type reference" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/typeref1.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_fwdrec1)
{
   const error_t expect[] = {
      { 6, "recursive typedef 'foo_t'" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/fwdrec1.sv");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_portexpr2)
{
   const error_t expect[] = {
      { 1, "all the names in a port expression must have the same direction" },
      { 1, "sorry, inout ports in port expressions are not supported" },
      { 8, "'nope' in a port expression must be declared with a port "
        "direction" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/portexpr2.v");

   vlog_check(do_parse_only(V_MODULE));
   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

START_TEST(test_wreal1)
{
   const error_t expect[] = {
      { 1, "sorry, wreal nets of Verilog-AMS are not supported" },
      { 2, "sorry, wreal nets of Verilog-AMS are not supported" },
      { -1, NULL }
   };
   expect_errors(expect);

   input_from_file(TESTDIR "/vlog/wreal1.v");

   vlog_check(do_parse_only(V_MODULE));

   fail_unless(vlog_parse() == NULL);

   check_expected_errors();
}
END_TEST

Suite *get_vlog_tests(void)
{
   Suite *s = suite_create("vlog");

   TCase *tc = nvc_unit_test();
   tcase_add_test(tc, test_dff);
   tcase_add_test(tc, test_simple_sem);
   tcase_add_test(tc, test_ports1);
   tcase_add_test(tc, test_ports2);
   tcase_add_test(tc, test_ports3);
   tcase_add_test(tc, test_parse1);
   tcase_add_test(tc, test_number1);
   tcase_add_test(tc, test_number2);
   tcase_add_test(tc, test_pp1);
   tcase_add_test(tc, test_empty1);
   tcase_add_test(tc, test_timescale1);
   tcase_add_test(tc, test_timescale2);
   tcase_add_test(tc, test_gate1);
   tcase_add_test(tc, test_pp2);
   tcase_add_test(tc, test_specify1);
   tcase_add_test(tc, test_udp1);
   tcase_add_test(tc, test_implicit1);
   tcase_add_test(tc, test_struct1);
   tcase_add_test(tc, test_struct2);
   tcase_add_test(tc, test_enum1);
   tcase_add_test(tc, test_enum2);
   tcase_add_test(tc, test_union1);
   tcase_add_test(tc, test_param1);
   tcase_add_test(tc, test_param2);
   tcase_add_test(tc, test_pp3);
   tcase_add_test(tc, test_concat1);
   tcase_add_test(tc, test_pp4);
   tcase_add_test(tc, test_casename);
   tcase_add_test(tc, test_dupmod);
   tcase_add_test(tc, test_staticmeth);
   tcase_add_test(tc, test_const1);
   tcase_add_test(tc, test_case1);
   tcase_add_test(tc, test_case2);
   tcase_add_test(tc, test_direct1);
   tcase_add_test(tc, test_string1);
   tcase_add_test(tc, test_generate1);
   tcase_add_test(tc, test_tfcall1);
   tcase_add_test(tc, test_tfcall2);
   tcase_add_test(tc, test_attr1);
   tcase_add_test(tc, test_initial1);
   tcase_add_test(tc, test_nets1);
   tcase_add_test(tc, test_keywords);
   tcase_add_test(tc, test_error1);
   tcase_add_test(tc, test_pp5);
   tcase_add_test(tc, test_integers1);
   tcase_add_test(tc, test_pp6);
   tcase_add_test(tc, test_label1);
   tcase_add_test(tc, test_href1);
   tcase_add_test(tc, test_package1);
   tcase_add_test(tc, test_class1);
   tcase_add_test(tc, test_class2);
   tcase_add_test(tc, test_class3);
   tcase_add_test(tc, test_namespace1);
   tcase_add_test(tc, test_real1);
   tcase_add_test(tc, test_simp1);
   tcase_add_test(tc, test_lower1);
   tcase_add_test(tc, test_pp7);
   tcase_add_test(tc, test_pp8);
   tcase_add_test(tc, test_generate2);
   tcase_add_test(tc, test_pp9);
   tcase_add_test(tc, test_const2);
   tcase_add_test(tc, test_pp10);
   tcase_add_test(tc, test_ppfile1);
   tcase_add_test(tc, test_pp11);
   tcase_add_test(tc, test_simp2);
   tcase_add_test(tc, test_lower2);
   tcase_add_test(tc, test_lower3);
   tcase_add_test(tc, test_force1);
   tcase_add_test(tc, test_constfunc1);
   tcase_add_test(tc, test_pp12);
   tcase_add_test(tc, test_pp13);
   tcase_add_test(tc, test_pp14);
   tcase_add_test(tc, test_href2);
   tcase_add_test(tc, test_href3);
   tcase_add_test(tc, test_constfunc2);
   tcase_add_test(tc, test_disable1);
   tcase_add_test(tc, test_issue1596);
   tcase_add_test(tc, test_issue1678);
   tcase_add_test(tc, test_pp15);
   tcase_add_test(tc, test_prop1);
   tcase_add_test(tc, test_pattern1);
   tcase_add_test(tc, test_pp16);
   tcase_add_test(tc, test_pp17);
   tcase_add_test(tc, test_number3);
   tcase_add_test(tc, test_pp18);
   tcase_add_test(tc, test_pp19);
   tcase_add_test(tc, test_pp21);
   tcase_add_test(tc, test_pp22);
   tcase_add_test(tc, test_pp20);
   tcase_add_test(tc, test_crv1);
   tcase_add_test(tc, test_crv2);
   tcase_add_test(tc, test_crv3);
   tcase_add_test(tc, test_crv4);
   tcase_add_test(tc, test_inval1);
   tcase_add_test(tc, test_inval2);
   tcase_add_test(tc, test_inval3);
   tcase_add_test(tc, test_assoc1);
   tcase_add_test(tc, test_assoc2);
   tcase_add_test(tc, test_scope1);
   tcase_add_test(tc, test_extern1);
   tcase_add_test(tc, test_cast1);
   tcase_add_test(tc, test_ifc1);
   tcase_add_test(tc, test_ifc2);
   tcase_add_test(tc, test_virtover1);
   tcase_add_test(tc, test_process1);
   tcase_add_test(tc, test_process2);
   tcase_add_test(tc, test_soft1);
   tcase_add_test(tc, test_cons1);
   tcase_add_test(tc, test_cons2);
   tcase_add_test(tc, test_inval4);
   tcase_add_test(tc, test_crv5);
   tcase_add_test(tc, test_inval5);
   tcase_add_test(tc, test_typedef1);
   tcase_add_test(tc, test_rs1);
   tcase_add_test(tc, test_nba1);
   tcase_add_test(tc, test_modport1);
   tcase_add_test(tc, test_final1);
   tcase_add_test(tc, test_invalid1);
   tcase_add_test(tc, test_taskout1);
   tcase_add_test(tc, test_crosshier1);
   tcase_add_test(tc, test_casereal1);
   tcase_add_test(tc, test_newtype1);
   tcase_add_test(tc, test_unpackeq1);
   tcase_add_test(tc, test_discard1);
   tcase_add_test(tc, test_fwdarg1);
   tcase_add_test(tc, test_modportin1);
   tcase_add_test(tc, test_pkghier1);
   tcase_add_test(tc, test_lifetime1);
   tcase_add_test(tc, test_dpiopen1);
   tcase_add_test(tc, test_vifmodportin1);
   tcase_add_test(tc, test_nettype1);
   tcase_add_test(tc, test_interconnect1);
   tcase_add_test(tc, test_svalocal1);
   tcase_add_test(tc, test_functask1);
   tcase_add_test(tc, test_config1);
   tcase_add_test(tc, test_refarg1);

   tcase_add_test(tc, test_binnand1);
   tcase_add_test(tc, test_netdtype1);

   tcase_add_test(tc, test_taskexpr1);
   tcase_add_test(tc, test_packedmem1);

   tcase_add_test(tc, test_finaltask1);
   tcase_add_test(tc, test_repl2);
   tcase_add_test(tc, test_defdup1);
   tcase_add_test(tc, test_classitem1);
   tcase_add_test(tc, test_assertlabel1);
   tcase_add_test(tc, test_lifetime2);
   tcase_add_test(tc, test_genvar3);
   tcase_add_test(tc, test_waitevent1);
   tcase_add_test(tc, test_forcesel1);
   tcase_add_test(tc, test_castsize1);
   tcase_add_test(tc, test_dimsize1);
   tcase_add_test(tc, test_selcount1);
   tcase_add_test(tc, test_dotname1);
   tcase_add_test(tc, test_dotstar2);
   tcase_add_test(tc, test_crash1);
   tcase_add_test(tc, test_crash2);
   tcase_add_test(tc, test_crash3);
   tcase_add_test(tc, test_crash4);
   tcase_add_test(tc, test_crash5);
   tcase_add_test(tc, test_crash6);
   tcase_add_test(tc, test_crash7);
   tcase_add_test(tc, test_crash8);
   tcase_add_test(tc, test_crash9);
   tcase_add_test(tc, test_crash10);
   tcase_add_test(tc, test_crash11);
   tcase_add_test(tc, test_crash12);
   tcase_add_test(tc, test_crash13);
   tcase_add_test(tc, test_crash14);
   tcase_add_test(tc, test_crash15);
   tcase_add_test(tc, test_crash16);
   tcase_add_test(tc, test_crash17);
   tcase_add_test(tc, test_crash18);
   tcase_add_test(tc, test_genvaruse1);
   tcase_add_test(tc, test_gendup1);
   tcase_add_test(tc, test_callorder1);
   tcase_add_test(tc, test_impimp1);
   tcase_add_test(tc, test_puremeth1);
   tcase_add_test(tc, test_letbad1);
   tcase_add_test(tc, test_misstatic1);
   tcase_add_test(tc, test_override1);
   tcase_add_test(tc, test_constprop1);
   tcase_add_test(tc, test_castcls1);
   tcase_add_test(tc, test_countbits1);
   tcase_add_test(tc, test_funcnb1);
   tcase_add_test(tc, test_funcnb2);
   tcase_add_test(tc, test_dpiname1);
   tcase_add_test(tc, test_evnba1);
   tcase_add_test(tc, test_covbin1);
   tcase_add_test(tc, test_methargs1);
   tcase_add_test(tc, test_covreal1);
   tcase_add_test(tc, test_dpichk1);
   tcase_add_test(tc, test_pkgexp1);
   tcase_add_test(tc, test_typeref1);
   tcase_add_test(tc, test_fwdrec1);
   tcase_add_test(tc, test_portexpr2);
   tcase_add_test(tc, test_wreal1);
   suite_add_tcase(s, tc);

   return s;
}
