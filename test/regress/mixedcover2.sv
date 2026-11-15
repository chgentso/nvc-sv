// Verilog module instantiating a VHDL entity twice with different
// generics.  The architecture is at the place of the instance.

module mixedcover2;
   reg a = 0;
   wire y1, y2;

   mixedcover2_sub #(.G(1)) u1 (.a(a), .y(y1));
   mixedcover2_sub #(.G(0)) u2 (.a(a), .y(y2));

   initial begin
      #1 a = 1;
      #1 a = 0;
      #1 $finish;
   end
endmodule
