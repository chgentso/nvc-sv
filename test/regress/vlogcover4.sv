// Statements from an included file belong to a class of their own in the
// Cobertura export and a macro with statements counts at the line of its
// use.  The modules use the same name for a generate block.

`define COUNT(v) v = v + 1;

module vlogcover4_a;
   reg [3:0] x = 0;
   initial x = 1;
`include "vlogcover4.svh"
endmodule

module vlogcover4_b;
   reg [3:0] y = 0;

   if (1) begin : blk
      initial begin
         `COUNT(y)
         `COUNT(y)
      end
   end
endmodule

module vlogcover4;
   vlogcover4_a a ();
   vlogcover4_b b ();

   initial begin
      #1;
      $display("PASSED");
   end
endmodule
