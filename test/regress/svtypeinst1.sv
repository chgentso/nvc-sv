module svtypeinst1_m #(parameter int P = 1);
   localparam int VAL = P;
endmodule

module svtypeinst1;
   typedef logic [3:0] svtypeinst1_m;

   svtypeinst1_m v = 4'ha;
   svtypeinst1_m arr [1:0];

   svtypeinst1_m u0();
   svtypeinst1_m #(.P(7)) u1();
   svtypeinst1_m u2[1:0](), u3();

   initial begin
      arr[0] = 4'h3;
      arr[1] = 4'hc;
      if ($bits(v) != 4 || v !== 4'ha) $fatal(1, "var v");
      if (arr[0] !== 4'h3 || arr[1] !== 4'hc) $fatal(1, "var arr");
      if (u0.VAL != 1 || u1.VAL != 7 || u2[0].VAL != 1 || u3.VAL != 1)
         $fatal(1, "inst VAL");
      $display("PASSED");
   end
endmodule
