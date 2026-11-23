// $finish in the delta cycle in which a clock edge wakes another process
// still writes the coverage database: the statements of the initial block
// are counted but the always block has not run.

module vlogcover18_leaf (input clk, output reg q);
   initial q = 0;
   always @(posedge clk) q <= ~q;
endmodule

module vlogcover18;
   reg clk = 0;
   wire q;

   vlogcover18_leaf u (.clk(clk), .q(q));

   initial begin
      #5 clk = 1;
      $display("PASSED");
      $finish;
   end
endmodule
