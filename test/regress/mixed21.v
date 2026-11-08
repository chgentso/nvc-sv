// Vector generic overrides take the width of the generic
module mixed21;
   wire [7:0] m1, u1, l1, m2, u2, l2, m3, u3, l3, m4, u4, l4;
   wire [3:0] i1, i2, i3, i4;

   genvec a(m1, u1, l1, i1);
   genvec #(.M(5)) b(m2, u2, l2, i2);
   genvec #(.M(4'hF)) c(m3, u3, l3, i3);
   genvec #(.U(8'hFF), .I(16'h1234)) d(m4, u4, l4, i4);

   initial begin
      #1;
      if (m1 !== 8'h00 || u1 !== 8'h0A || l1 !== 4 || i1 !== 4'b0000)
         $display("FAILED: default %h %h %h %b", m1, u1, l1, i1);
      else if (m2 !== 8'h05)
         $display("FAILED: M(5) %h", m2);
      else if (m3 !== 8'h0F)
         $display("FAILED: M(4'hF) %h", m3);
      else if (u4 !== 8'hFF || l4 !== 8 || i4 !== 4'b0100)
         $display("FAILED: U(8'hFF) %h %h I(16'h1234) %b", u4, l4, i4);
      else
         $display("PASSED");
      $finish;
   end
endmodule
