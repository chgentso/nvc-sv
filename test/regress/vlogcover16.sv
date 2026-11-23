// Coverage specification file on Verilog.  Instances of one module with the
// same parameters share their code, but an instance which the file
// excludes is counted by nothing.  The paths in the file may be spelled in any
// case, and generate blocks have the index in the name.

module vlogcover16_sub (input clk, output reg [1:0] q);
   always @(posedge clk) begin
      if (q == 2'b11)
         q <= 0;
      else
         q <= q + 1;
   end
   initial q = 0;
endmodule

module vlogcover16_other (input clk, output reg q);
   always @(posedge clk) q <= ~q;
   initial q = 0;
endmodule

module vlogcover16;
   reg clk = 0;
   wire [1:0] q0, q1;
   wire       o;

   vlogcover16_sub u1 (.clk(clk), .q(q0));
   vlogcover16_sub u2 (.clk(clk), .q(q1));

   for (genvar i = 0; i < 2; i++) begin : g
      wire [1:0] qg;
      vlogcover16_sub u (.clk(clk), .q(qg));
   end

   vlogcover16_other x (.clk(clk), .q(o));

   always #5 clk = ~clk;
   initial begin
      #22;
      $display("PASSED");
      $finish;
   end
endmodule
