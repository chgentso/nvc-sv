`timescale 1ns/1ns

module vpi_island5_dut (
   input  logic       clk,
   input  logic       rst_n,
   input  logic [7:0] din,
   output logic [7:0] obs_out,
   output wire  [7:0] pruned_comb,
   output logic [7:0] pruned_ff
);
   logic [7:0] ff_q;

   assign pruned_comb = din ^ 8'h3c;

   always_comb begin
      obs_out = ff_q + din;
   end

   always_ff @(posedge clk or negedge rst_n) begin
      if (!rst_n) begin
         ff_q      <= 8'h10;
         pruned_ff <= 8'h20;
      end else begin
         ff_q      <= din + 8'h05;
         pruned_ff <= din + 8'h11;
      end
   end
endmodule

module vpi_island5;
   logic       clk   = 1'b0;
   logic       rst_n = 1'b0;
   logic [7:0] din   = 8'h01;
   logic [7:0] obs1, obs2;

   // Two instances share clk/rst_n so hier mode builds a super-domain.
   // pruned_comb and pruned_ff are unconnected (pruned under --island-fast).
   vpi_island5_dut u1 (
      .clk         (clk),
      .rst_n       (rst_n),
      .din         (din),
      .obs_out     (obs1),
      .pruned_comb (),
      .pruned_ff   ()
   );

   vpi_island5_dut u2 (
      .clk         (clk),
      .rst_n       (rst_n),
      .din         (din),
      .obs_out     (obs2),
      .pruned_comb (),
      .pruned_ff   ()
   );

   always #5 clk = ~clk;

   initial begin
      // t=0..11ns: reset active -> ff_q=0x10, pruned_ff=0x20, pruned_comb=0x3d
      #12;
      rst_n = 1'b1;
      din   = 8'h20; // pruned_comb=0x1c; at t=15ns: ff_q<=0x25, pruned_ff<=0x31
      #10; // t=22ns
      din   = 8'h40; // pruned_comb=0x7c; at t=25ns: ff_q<=0x45, pruned_ff<=0x51 (while forced!)
      #10; // t=32ns
      din   = 8'h20; // return din to 0x20 to test sibling LAST_OUT_VALID after release!
      #10; // t=42ns
      if (obs1 !== 8'h45 || obs2 !== 8'h45) begin
         $display("FAILED obs1=%h obs2=%h", obs1, obs2);
         $finish(1);
      end
      $finish;
   end
endmodule
