// Value-change callbacks on RTL island signals must match
// --rtl-islands=off (gold produced with it): always_comb outputs change at
// time zero, a released variable written by an always_comb block keeps the
// forced value until the block runs again (IEEE 1800-2017 10.6.2) and a
// released register output keeps it until the next clock edge.

`timescale 1ns/1ns

module vpi_island_cb1_a (input logic clk, input logic [7:0] i,
                         output logic [7:0] o, output logic [7:0] q);
   always_comb o = i + 8'd1;
   always_ff @(posedge clk) q <= o;
endmodule

module vpi_island_cb1_b (input logic [7:0] x, output logic [7:0] y);
   always_comb y = x ^ 8'h5a;
endmodule

module vpi_island_cb1;
   logic       clk = 0;
   logic [7:0] din = 8'h00;
   logic [7:0] w1, q, obs;

   vpi_island_cb1_a ua (.clk(clk), .i(din), .o(w1), .q(q));
   vpi_island_cb1_b ub (.x(w1), .y(obs));

   always #5 clk = ~clk;

   initial begin
      for (int n = 0; n < 8; n++) begin
         #4 din = din + 8'd3;
         #1 $display("%0t: din=%02x w1=%02x o=%02x obs=%02x q=%02x", $time, din, w1, ua.o, obs, q);
      end
      $finish;
   end
endmodule
