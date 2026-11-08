// Names in the generated module are escaped and port connections match
// the names of the VHDL ports without regard to case
module mixed24;
   reg a;
   wire y, m, l, y2, m2, l2;

   names24 #(.Gen(6)) u(.\reg (a), .\wire (y), .Mixed(m), .nnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnn_p(l)),
     v(.\Reg (a), .\WIRE (y2), .mixed(m2), .nnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnnn_P(l2));

   initial begin
      a = 1;
      #1;
      if (y !== 1 || m !== 1 || l !== 0 || y2 !== 1 || m2 !== 1 || l2 !== 0)
         $display("FAILED: %b %b %b %b %b %b", y, m, l, y2, m2, l2);
      else
         $display("PASSED");
      $finish;
   end
endmodule
