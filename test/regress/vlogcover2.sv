// Hierarchy: two instances of a module, one with a parameter, generate
// loops and conditions, an instance array, an interface and a program.  The
// counts differ for each instance.

interface vlogcover2_if (input clk);
   logic [3:0] d;
   always @(posedge clk) d <= d + 1;
endinterface

program vlogcover2_prg (input clk);
   int n;
   initial begin
      repeat (2) @(posedge clk) n++;
      $display("PASSED");
   end
endprogram

module vlogcover2_sub #(parameter N = 1) (input clk, output reg [3:0] q);
   initial q = 0;
   always @(posedge clk) begin
      q <= q + N;
      if (N > 1)
         q <= q + 2;
   end
endmodule

module vlogcover2;
   reg clk = 0;
   wire [3:0] q0, q1, q2, q3, q4;

   vlogcover2_sub #(.N(1)) u1 (.clk(clk), .q(q0));
   vlogcover2_sub #(.N(2)) u2 (.clk(clk), .q(q1));

   for (genvar i = 0; i < 2; i++) begin : g
      vlogcover2_sub #(.N(i + 1)) u (.clk(clk), .q(q2));
   end

   if (1) begin : gi
      reg [3:0] r;
      initial r = 1;
   end
   else begin : ge
      reg [3:0] r;
      initial r = 2;
   end

   vlogcover2_sub arr [0:1] (.clk(clk), .q(q3));

   vlogcover2_if ifc (clk);
   vlogcover2_prg prg (clk);

   always #5 clk = ~clk;
   initial begin
      #42;
      $finish;
   end
endmodule
