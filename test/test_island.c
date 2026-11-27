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
#include "ident.h"
#include "lib.h"
#include "phase.h"
#include "rt/island.h"
#include "scan.h"
#include "vlog/vlog-island.h"
#include "vlog/vlog-node.h"
#include "vlog/vlog-phase.h"

#include <string.h>

static vlog_node_t build_inst_body(const char *src)
{
   set_default_keywords(VLOG_1800_2023);
   input_from_buffer(src, strlen(src), loc_file_ref("test_island.sv", NULL),
                     SOURCE_VERILOG);

   vlog_node_t m = vlog_parse();
   ck_assert_ptr_nonnull(m);
   ck_assert_vlog_kind(m, V_MODULE);
   lib_put_vlog(lib_work(), m);
   vlog_check(m);
   fail_if_errors();

   vlog_simp(m);

   vlog_node_t body = vlog_new_instance(m, NULL, vlog_ident(m));
   vlog_fold(body, get_mir(), get_jit());
   fail_if_errors();

   return body;
}

static const island_sig_t *find_sig(const vlog_island_t *isl, const char *name)
{
   ident_t id = ident_new(name);
   for (uint32_t i = 0; i < isl->nsigs; i++) {
      if (isl->sigs[i].name == id)
         return &(isl->sigs[i]);
   }
   return NULL;
}

START_TEST(test_island_pure_rtl)
{
   const char *src =
      "module pure_rtl (\n"
      "  input  logic        clk1,\n"
      "  input  logic        clk2,\n"
      "  input  logic        rst_n,\n"
      "  input  logic [7:0]  a,\n"
      "  input  bit   [7:0]  b,\n"
      "  output logic [7:0]  y,\n"
      "  output bit   [31:0] cnt_o\n"
      ");\n"
      "  logic [7:0]  comb1, comb2;\n"
      "  logic [15:0] mem [0:3];\n"
      "  bit   [31:0] cnt_q;\n"
      "  assign comb1 = a + b;\n"
      "  always_comb begin\n"
      "    comb2 = comb1 ^ 8'h5a;\n"
      "  end\n"
      "  always_ff @(posedge clk1 or negedge rst_n) begin\n"
      "    if (!rst_n) begin\n"
      "      y <= 8'h0;\n"
      "      for (int i = 0; i < 4; i++)\n"
      "        mem[i] <= 16'h0;\n"
      "    end else begin\n"
      "      y <= comb2;\n"
      "      mem[a[1:0]] <= {comb1, comb2};\n"
      "    end\n"
      "  end\n"
      "  always_ff @(negedge clk2) begin\n"
      "    cnt_q <= cnt_q + 32'd1;\n"
      "  end\n"
      "  assign cnt_o = cnt_q;\n"
      "endmodule\n";

   vlog_node_t body = build_inst_body(src);
   vlog_island_plan_t *plan = vlog_island_analyze(body, false);

   ck_assert_int_eq(plan->nislands, 1);
   ck_assert_int_eq(plan->n_total_procs, 5);
   ck_assert_int_eq(plan->n_fused_procs, 5);
   ck_assert_int_eq(plan->nejected_procs, 0);
   ck_assert_int_eq(plan->n_ejected_sigs, 0);

   const vlog_island_t *isl = plan->islands[0];
   ck_assert_int_eq(isl->n_assign, 2);
   ck_assert_int_eq(isl->n_comb, 1);
   ck_assert_int_eq(isl->n_ff, 2);
   ck_assert_int_eq(isl->ndomains, 2);
   ck_assert_int_eq(isl->comb_sccs, 0);
   ck_assert_int_eq(isl->comb_levels, 2);
   ck_assert_int_eq(isl->ntopo, 3);

   // Verify topological ordering: assign comb1 precedes always_comb comb2
   int idx_assign_comb1 = -1, idx_comb2 = -1;
   for (uint32_t i = 0; i < isl->nprocs; i++) {
      if (isl->procs[i].kind == IPROC_ASSIGN && isl->procs[i].nwrites == 1
          && isl->sigs[isl->procs[i].writes[0]].name == ident_new("comb1"))
         idx_assign_comb1 = (int)i;
      if (isl->procs[i].kind == IPROC_COMB)
         idx_comb2 = (int)i;
   }
   ck_assert_int_ge(idx_assign_comb1, 0);
   ck_assert_int_ge(idx_comb2, 0);
   ck_assert_int_lt(isl->procs[idx_assign_comb1].topo_order,
                    isl->procs[idx_comb2].topo_order);

   const island_sig_t *s_clk1 = find_sig(isl, "clk1");
   const island_sig_t *s_rst = find_sig(isl, "rst_n");
   const island_sig_t *s_a = find_sig(isl, "a");
   const island_sig_t *s_b = find_sig(isl, "b");
   const island_sig_t *s_y = find_sig(isl, "y");
   const island_sig_t *s_comb1 = find_sig(isl, "comb1");
   const island_sig_t *s_mem = find_sig(isl, "mem");
   const island_sig_t *s_cntq = find_sig(isl, "cnt_q");

   ck_assert_ptr_nonnull(s_clk1);
   ck_assert_int_eq(s_clk1->kind, ISIG_CLOCK);
   ck_assert_ptr_nonnull(s_rst);
   ck_assert_int_eq(s_rst->kind, ISIG_RESET);
   ck_assert_ptr_nonnull(s_a);
   ck_assert_int_eq(s_a->kind, ISIG_BOUNDARY_IN);
   ck_assert(!s_a->two_state);
   ck_assert_uint_ne(s_a->b_off, UINT32_MAX);
   ck_assert_ptr_nonnull(s_b);
   ck_assert_int_eq(s_b->kind, ISIG_BOUNDARY_IN);
   ck_assert(s_b->two_state);
   ck_assert_uint_eq(s_b->b_off, UINT32_MAX);
   ck_assert_ptr_nonnull(s_y);
   ck_assert_int_eq(s_y->kind, ISIG_BOUNDARY_OUT);
   ck_assert_ptr_nonnull(s_comb1);
   ck_assert_int_eq(s_comb1->kind, ISIG_INTERNAL);
   ck_assert_ptr_nonnull(s_mem);
   ck_assert_int_eq(s_mem->kind, ISIG_INTERNAL);
   ck_assert_int_eq(s_mem->nelems, 4);
   ck_assert_int_eq(s_mem->width, 16);
   ck_assert_ptr_nonnull(s_cntq);
   ck_assert_int_eq(s_cntq->kind, ISIG_INTERNAL);
   ck_assert(s_cntq->two_state);

   // Total state bytes = header(8) + 4*16(clk1,rst_n,a,y) + 2*8(b,cnt_q)
   //   + 3*16(comb1,comb2,mem? mem is 4*8*2=64) + 8(cnt_o)
   // Verify alignment and consistency
   ck_assert_uint_eq(isl->header_bytes, 8);
   ck_assert_uint_eq(isl->state_bytes % 8, 0);
   ck_assert_uint_gt(isl->state_bytes, isl->header_bytes);

   vlog_island_plan_free(plan);
}
END_TEST

START_TEST(test_island_mixed_ejections)
{
   const char *src =
      "module mixed_eject (\n"
      "  input  logic clk,\n"
      "  input  logic d,\n"
      "  inout  wire  io_p,\n"
      "  output logic q_o\n"
      ");\n"
      "  logic init_drv, obs_sig, dly_out;\n"
      "  wire  md_wire;\n"
      "  logic q_int;\n"
      "  initial begin\n"
      "    init_drv = 1'b0;\n"
      "  end\n"
      "  assign #5 dly_out = d;\n"
      "  assign md_wire = d ? 1'b1 : 1'bz;\n"
      "  assign md_wire = ~d ? 1'b0 : 1'bz;\n"
      "  always_ff @(posedge clk) begin\n"
      "    q_int <= d & init_drv & md_wire;\n"
      "  end\n"
      "  always_comb begin\n"
      "    obs_sig = q_int ^ dly_out;\n"
      "    q_o = obs_sig;\n"
      "  end\n"
      "  initial begin\n"
      "    if (obs_sig) $display(\"hi\");\n"
      "  end\n"
      "  assert property (@(posedge clk) q_o == q_int);\n"
      "endmodule\n";

   vlog_node_t body = build_inst_body(src);
   vlog_island_plan_t *plan = vlog_island_analyze(body, false);

   ck_assert_int_eq(plan->nislands, 1);
   // Eligible: always_ff and always_comb (2)
   // Ejected: 2x initial, 1x #5 assign, 2x multi-driver assign, 1x SVA
   ck_assert_int_eq(plan->n_fused_procs, 2);
   ck_assert_int_ge(plan->nejected_procs, 6);
   ck_assert_int_eq(plan->proc_eject_counts[IPROC_EJ_INITIAL], 2);
   ck_assert_int_eq(plan->proc_eject_counts[IPROC_EJ_DELAY], 1);
   ck_assert_int_eq(plan->proc_eject_counts[IPROC_EJ_MULTI_DRIVER_TARGET], 2);
   ck_assert_int_ge(plan->proc_eject_counts[IPROC_EJ_SVA], 1);

   const vlog_island_t *isl = plan->islands[0];
   const island_sig_t *s_init = find_sig(isl, "init_drv");
   const island_sig_t *s_dly = find_sig(isl, "dly_out");
   const island_sig_t *s_md = find_sig(isl, "md_wire");
   const island_sig_t *s_obs = find_sig(isl, "obs_sig");
   const island_sig_t *s_qint = find_sig(isl, "q_int");

   ck_assert_ptr_nonnull(s_init);
   ck_assert_int_eq(s_init->kind, ISIG_BOUNDARY_IN);
   ck_assert_ptr_nonnull(s_dly);
   ck_assert_int_eq(s_dly->kind, ISIG_BOUNDARY_IN);
   ck_assert_ptr_nonnull(s_md);
   ck_assert_int_eq(s_md->kind, ISIG_BOUNDARY_IN);
   ck_assert_int_eq(s_md->eject_reason, ISIG_EJ_MULTI_DRIVER);
   ck_assert_ptr_nonnull(s_obs);
   ck_assert_int_eq(s_obs->kind, ISIG_BOUNDARY_OUT);
   ck_assert_ptr_nonnull(s_qint);
   ck_assert_int_eq(s_qint->kind, ISIG_BOUNDARY_OUT);

   vlog_island_plan_free(plan);
}
END_TEST

START_TEST(test_island_comb_cycle)
{
   const char *src =
      "module comb_cycle (\n"
      "  input  logic en,\n"
      "  output logic y\n"
      ");\n"
      "  logic a, b, c;\n"
      "  assign a = b & en;\n"
      "  assign b = ~a;\n"
      "  assign c = ~c;\n"
      "  assign y = a ^ c;\n"
      "endmodule\n";

   vlog_node_t body = build_inst_body(src);
   vlog_island_plan_t *plan = vlog_island_analyze(body, false);

   ck_assert_int_eq(plan->nislands, 1);
   ck_assert_int_eq(plan->n_fused_procs, 4);
   ck_assert_int_eq(plan->islands[0]->comb_sccs, 2);

   vlog_island_plan_free(plan);
}
END_TEST

START_TEST(test_island_mixed_vhdl_and_func)
{
   const char *src =
      "module mixed_func (\n"
      "  input  logic       clk,\n"
      "  input  logic [3:0] a,\n"
      "  output logic [3:0] y\n"
      ");\n"
      "  localparam logic [3:0] LUT [0:1] = '{4'h5, 4'ha};\n"
      "  function automatic logic [3:0] f(input logic [3:0] x);\n"
      "    return x ^ LUT[x[0]];\n"
      "  endfunction\n"
      "  logic [3:0] m_bad;\n"
      "  always_ff @(posedge clk) begin\n"
      "    y <= f(a);\n"
      "  end\n"
      "  always_ff @(posedge clk) begin\n"
      "    m_bad = a;\n"
      "  end\n"
      "endmodule\n";

   vlog_node_t body = build_inst_body(src);
   vlog_island_plan_t *plan = vlog_island_analyze(body, true);

   ck_assert_int_eq(plan->nislands, 1);
   ck_assert_int_eq(plan->n_fused_procs, 1);
   ck_assert_int_eq(plan->proc_eject_counts[IPROC_EJ_MIXED_ASSIGN_IN_FF], 1);
   ck_assert_int_ge(plan->sig_eject_counts[ISIG_EJ_MIXED_PORT], 3);

   vlog_island_plan_free(plan);
}
END_TEST

START_TEST(test_island_spec_pack)
{
   // Mode B layout: an 11-bit std_logic vector, a 70-bit bit vector, a 3x5
   // std_logic 2D array, a boolean and a 4-byte integer
   enum { NSIGS = 5 };
   static const uint32_t width[NSIGS]  = { 11, 70, 5, 1, 1 };
   static const uint32_t nelems[NSIGS] = { 1, 1, 3, 1, 1 };
   static const uint32_t b_off[NSIGS]  = { 8, 24, 96, 112, 120 };
   static const uint32_t b_bytes[NSIGS] = { 11, 70, 15, 1, 4 };
   static const uint32_t a_off[NSIGS]  = { 8, 16, 32, 56, 64 };
   static const uint32_t a_flags[NSIGS] = {
      RT_ISIG_TWO_STATE | RT_ISIG_STD_LOGIC_ELEM, RT_ISIG_TWO_STATE,
      RT_ISIG_TWO_STATE | RT_ISIG_STD_LOGIC_ELEM, RT_ISIG_TWO_STATE, 0,
   };

   rt_island_sig_t sigs[NSIGS] = {};
   for (int i = 0; i < NSIGS; i++) {
      sigs[i].a_off = b_off[i];
      sigs[i].width = width[i];
      sigs[i].nelems = nelems[i];
      sigs[i].nwords = (width[i] + 63) / 64;
   }

   uint32_t spec_a_off[NSIGS], spec_a_flags[NSIGS], spec_b_bytes[NSIGS];
   memcpy(spec_a_off, a_off, sizeof(a_off));
   memcpy(spec_a_flags, a_flags, sizeof(a_flags));
   memcpy(spec_b_bytes, b_bytes, sizeof(b_bytes));

   rt_island_spec_t spec = {
      .state_bytes = 72,
      .stride      = 128,
      .hdr_bytes   = 8,
      .a_off       = spec_a_off,
      .a_flags     = spec_a_flags,
      .b_bytes     = spec_b_bytes,
   };
   rt_island_t isl = { .sigs = sigs, .nsigs = NSIGS, .spec = &spec };

   uint8_t b[128] = {}, b2[128] = {};
   uint64_t a[16];
   for (int i = 0; i < 8; i++)
      b[i] = 0x11 * i;   // Header bytes copied unchanged
   for (int k = 0; k < 11; k++)
      b[8 + k] = 2 + ((k * 7) % 3 == 0);   // '0' and '1'
   for (int k = 0; k < 70; k++)
      b[24 + k] = (k % 5) == 1;
   for (int k = 0; k < 15; k++)
      b[96 + k] = 2 + (k % 2);
   b[112] = 1;
   memcpy(b + 120, "\x78\x56\x34\x12", 4);

   ck_assert(rt_island_spec_pack(&isl, b, (uint8_t *)a));
   ck_assert_int_eq(a[1] & 0x7ff, 0x492);   // Bit 0 is the last element
   ck_assert_int_eq(a[7] & 1, 1);
   ck_assert_int_eq(((uint8_t *)a)[64], 0x78);

   rt_island_spec_unpack(&isl, (uint8_t *)a, b2);
   for (int i = 0; i < NSIGS; i++)
      ck_assert_mem_eq(b + b_off[i], b2 + b_off[i], b_bytes[i]);
   ck_assert_mem_eq(b, b2, 8);

   // Every std_logic value other than '0' and '1' makes the state unclean
   for (int sig = 0; sig < 3; sig += 2) {
      for (uint8_t v = 0; v < 9; v++) {
         uint8_t *p = b + b_off[sig] + (sig == 0 ? 10 : 7);
         const uint8_t save = *p;
         *p = v;
         ck_assert_int_eq(rt_island_spec_pack(&isl, b, (uint8_t *)a),
                          v == 2 || v == 3);
         *p = save;
      }
   }

   // A bit or boolean byte above 1 is not a BIT value either
   b[24 + 3] = 2;
   ck_assert(!rt_island_spec_pack(&isl, b, (uint8_t *)a));
}
END_TEST

Suite *get_island_tests(void)
{
   Suite *s = suite_create("island");

   TCase *tc = nvc_unit_test();
   tcase_add_test(tc, test_island_pure_rtl);
   tcase_add_test(tc, test_island_mixed_ejections);
   tcase_add_test(tc, test_island_comb_cycle);
   tcase_add_test(tc, test_island_mixed_vhdl_and_func);
   tcase_add_test(tc, test_island_spec_pack);
   suite_add_tcase(s, tc);

   return s;
}
