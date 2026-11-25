`timescale 1ns/1ns

module vpi_island3_dut (
   input  logic       clk,
   input  logic [7:0] din,
   output logic [7:0] y,
   output logic [7:0] q
);
   always_comb y = din ^ 8'h55;

   always_ff @(posedge clk)
      q <= y;
endmodule

module vpi_island3;
   logic       clk = 1'b0;
   logic [7:0] din = 8'h00;
   logic [7:0] y;
   logic [7:0] q;

   vpi_island3_dut u_dut (
      .clk (clk),
      .din (din),
      .y   (y),
      .q   (q)
   );

   always #5 clk = ~clk;

   initial begin
      #50;
      $finish;
   end
endmodule
