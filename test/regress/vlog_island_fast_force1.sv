`timescale 1ns/1ns

module sv_fast_force_dut (
   input  logic       clk,
   input  logic [7:0] din,
   output logic [7:0] sib_comb,
   output logic [7:0] sib_ff,
   output wire  [7:0] pruned_comb,
   output logic [7:0] pruned_ff
);
   assign pruned_comb = din ^ 8'haa;

   always_comb begin
      sib_comb = din ^ 8'h55;
   end

   always_ff @(posedge clk) begin
      sib_ff    <= din + 8'h02;
      pruned_ff <= din + 8'h03;
   end
endmodule

module vlog_island_fast_force1;
   logic       clk = 1'b0;
   logic [7:0] din = 8'h11;
   logic [7:0] sib_comb, sib_ff;

   sv_fast_force_dut u_dut (
      .clk         (clk),
      .din         (din),
      .sib_comb    (sib_comb),
      .sib_ff      (sib_ff),
      .pruned_comb (),
      .pruned_ff   ()
   );

   initial begin
      // Cycle 1 with din=0x11 before touching pruned outputs
      #5 clk = 1'b1; #5 clk = 1'b0; #1;
      if (sib_comb !== (8'h11 ^ 8'h55) || sib_ff !== 8'h13) begin
         $display("FAILED cycle1 sib_comb=%h sib_ff=%h", sib_comb, sib_ff);
         $finish(1);
      end

      // Dynamic hierarchical read of pruned outputs
      if (u_dut.pruned_comb !== (8'h11 ^ 8'haa) || u_dut.pruned_ff !== 8'h14) begin
         $display("FAILED hier read pruned_comb=%h pruned_ff=%h",
                  u_dut.pruned_comb, u_dut.pruned_ff);
         $finish(1);
      end

      // Force both pruned outputs
      force u_dut.pruned_comb = 8'hee;
      force u_dut.pruned_ff   = 8'hdd;
      #1;
      if (u_dut.pruned_comb !== 8'hee || u_dut.pruned_ff !== 8'hdd) begin
         $display("FAILED force pruned_comb=%h pruned_ff=%h",
                  u_dut.pruned_comb, u_dut.pruned_ff);
         $finish(1);
      end

      // Cycle 2 with din=0x22 while forced: sibling outputs must update
      din = 8'h22;
      #5 clk = 1'b1; #5 clk = 1'b0; #1;
      if (u_dut.pruned_comb !== 8'hee || u_dut.pruned_ff !== 8'hdd) begin
         $display("FAILED hold force pruned_comb=%h pruned_ff=%h",
                  u_dut.pruned_comb, u_dut.pruned_ff);
         $finish(1);
      end
      if (sib_comb !== (8'h22 ^ 8'h55) || sib_ff !== 8'h24) begin
         $display("FAILED cycle2 sib_comb=%h sib_ff=%h", sib_comb, sib_ff);
         $finish(1);
      end

      // Release both pruned outputs: net pruned_comb immediately restores driven value
      release u_dut.pruned_comb;
      release u_dut.pruned_ff;
      #1;
      if (u_dut.pruned_comb !== (8'h22 ^ 8'haa)) begin
         $display("FAILED release pruned_comb=%h", u_dut.pruned_comb);
         $finish(1);
      end

      // Cycle 3: return din to 0x11 (verifies sibling LAST_OUT_VALID invalidation!)
      din = 8'h11;
      #5 clk = 1'b1; #5 clk = 1'b0; #1;
      if (sib_comb !== (8'h11 ^ 8'h55) || sib_ff !== 8'h13) begin
         $display("FAILED cycle3 sib_comb=%h sib_ff=%h", sib_comb, sib_ff);
         $finish(1);
      end
      if (u_dut.pruned_comb !== (8'h11 ^ 8'haa) || u_dut.pruned_ff !== 8'h14) begin
         $display("FAILED cycle3 pruned_comb=%h pruned_ff=%h",
                  u_dut.pruned_comb, u_dut.pruned_ff);
         $finish(1);
      end

      $display("PASSED");
      $finish;
   end
endmodule
