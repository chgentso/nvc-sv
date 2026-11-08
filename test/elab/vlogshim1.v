module vlogshim1;
   reg a;
   reg [3:0] b;

   mode1 u1(a);
   uncon1 u2(b);
   type1 u3(a);
   gentype1 #(.t(1)) u4(a);
   rec1 u5(a);
   real1 u6(b);
   null1 u7(b);
   null2 u8(a);
   ovf1 u9(b);
   def1 u10(a);
   ext1 u11(a);
   shim1_pkg u12();
endmodule
