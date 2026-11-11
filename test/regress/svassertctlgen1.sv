// $assertoff naming an assertion inside generate blocks selected by a
// constant index
module svassertctlgen1_leaf(input logic clk, input logic bad,
                            output int fails);
   chk_A: assert property (@(posedge clk) !bad) else fails++;
   initial fails = 0;
endmodule

module svassertctlgen1_mid(input logic clk, input logic bad,
                           output int fails0, output int fails1);
   for (genvar i = 0; i < 2; i++) begin : gen_fsm
      if (i == 0) begin : gen_fsm_p
         svassertctlgen1_leaf u_leaf(.clk(clk), .bad(bad), .fails(fails0));
      end
      else begin : gen_fsm_n
         svassertctlgen1_leaf u_leaf(.clk(clk), .bad(bad), .fails(fails1));
      end
   end
endmodule

module svassertctlgen1;
   logic clk = 0, bad = 0;
   int f0, f1, f0_at_off, f1_at_off;
   always #5 clk = ~clk;
   svassertctlgen1_mid u_mid(.clk(clk), .bad(bad), .fails0(f0), .fails1(f1));

   initial begin
      #12 bad = 1;
      #10;
      if (f0 == 0 || f1 == 0) $fatal(1, "assertions did not fail %0d %0d", f0, f1);
      $assertoff(0, svassertctlgen1.u_mid.gen_fsm[0].gen_fsm_p.u_leaf.chk_A);
      #1 f0_at_off = f0;
      f1_at_off = f1;
      #40;
      if (f0 != f0_at_off) $fatal(1, "first assertion still fails after $assertoff");
      if (f1 <= f1_at_off) $fatal(1, "second assertion was turned off");
      $assertoff(0, svassertctlgen1.u_mid.gen_fsm[1].gen_fsm_n.u_leaf.chk_A);
      #1 f1_at_off = f1;
      #40;
      if (f1 != f1_at_off) $fatal(1, "assertion still fails after $assertoff");
      $display("PASSED");
      $finish;
   end
endmodule
