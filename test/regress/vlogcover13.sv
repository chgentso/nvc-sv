// Toggle coverage with the options to count a change from x and to and from
// z.  The signal starts at x, goes to 0 and 1, then through z and x.

module vlogcover13;
   reg [1:0] v;       // Starts at x
   wire [1:0] n;
   reg       d;
   assign n = d ? v : 2'bzz;

   initial begin
      d = 1;
      v = 2'b01;      // x to 0 and to 1
      #1 v = 2'b10;   // 0 to 1, 1 to 0
      #1 d = 0;       // n to z: 1 to z and 0 to z
      #1 d = 1;       // n from z
      #1 v = 2'bxx;   // To x is not counted
      #1 v = 2'b11;   // From x
      #1 $display("PASSED");
      $finish;
   end
endmodule
