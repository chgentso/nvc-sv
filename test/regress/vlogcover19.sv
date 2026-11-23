// Comments which turn coverage off and on.  Nothing between them has an item
// for statements, branches or toggles.  The state at the start of a module
// is the one left by the comments before it in the file.

module vlogcover19;
   reg clk = 0;
   wire yb;

   vlogcover19_b ub (.a(clk), .y(yb));
   vlogcover19_c uc ();

   reg [1:0] on_sig = 0;
   // coverage off
   reg [1:0] off_sig = 0;
   reg       other = 0;
   always #5 clk = ~clk;       // Not counted
   initial begin
      #6 off_sig = 1;          // Not counted
      if (off_sig == 1)        // Nor the branches
         other = 1;
      // coverage on
      #6 on_sig = 1;
      if (on_sig == 1)
         other = 0;
      else
         other = 1;
      // Coverage Off   is not case sensitive
      case (other)
         0: other = 1;
         default: other = 0;
      endcase
      // COVERAGE ON  and so is on
      #6 on_sig = 2;
      $display("PASSED");
      $finish;
   end
endmodule

// coverage off
// Coverage is off for the whole of this module

module vlogcover19_b (input a, output reg y);
   always @(a) y = a;
endmodule

// coverage on

module vlogcover19_c;
   // Same name as the one in another module and not an off comment:
   // coveragexoff and coverage offline do nothing
   // coverage offline
   reg z = 0;
   initial z = 1;
endmodule
