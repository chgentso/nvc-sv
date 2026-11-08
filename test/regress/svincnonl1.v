// The last line of an include file is a macro definition with no newline
`include "svinc/nonl.vh"

module svincnonl_m(input a1, b1, c);
endmodule

module svincnonl1;
  wire x, y;
  svincnonl_m u(`FOO(1) .c(1'b0));
  initial #1 $display("PASSED");
endmodule
