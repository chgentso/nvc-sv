// A design for the merge, report and exclude file tests: the number of
// generate blocks depends on a parameter, and a counter is incremented
// a number of times given by another.

module vlogcover17_leaf (input clk, output reg [1:0] q);
   initial q = 0;
   always @(posedge clk) begin
      if (q == 2'b11)
         q <= 0;
      else
         q <= q + 1;
   end
endmodule

module vlogcover17;
   parameter N = 2;
   parameter CYCLES = 3;

   reg clk = 0;
   reg [1:0] mode = 0;

   for (genvar i = 0; i < N; i++) begin : g
      wire [1:0] q;
      vlogcover17_leaf u (.clk(clk), .q(q));
   end

   initial begin
      repeat (CYCLES) begin
         #5 clk = 1;
         #5 clk = 0;
      end
      mode = 3;
      $display("PASSED");
      $finish;
   end
endmodule
