// Generic defaults of VHDL entities which are not literals
module mixed23;
   reg  [3:0] a;
   reg  [5:0] b;
   wire [3:0] y1;
   wire [5:0] y2;
   wire ok1, ok2;

   defgen u1(a, y1, ok1);
   defgen #(.W(6), .S("dd")) u2(b, y2, ok2);

   initial begin
      a = 5;
      b = 33;
      #1;
      if (y1 !== 5 || ok1 !== 1)
         $display("FAILED: default %b %b", y1, ok1);
      else if (y2 !== 33 || ok2 !== 0)
         $display("FAILED: override %b %b", y2, ok2);
      else
         $display("PASSED");
      $finish;
   end
endmodule
