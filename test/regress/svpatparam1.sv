package svpatparam1_pkg;
   typedef struct packed {
      int a;
      int b;
   } cfg_t;
endpackage

module svpatparam1;
   import svpatparam1_pkg::*;

   svpatparam1_sub #(
      .CFG('{b: 20, a: 10}),
      .MAT('{'{1, 2}, '{3, 4}})
   ) u_sub ();
endmodule

module svpatparam1_sub
   import svpatparam1_pkg::*;
#(
   parameter cfg_t CFG = '0,
   parameter int MAT[2][2] = '{'{0, 0}, '{0, 0}}
);
   initial begin
      if (CFG.a != 10 || CFG.b != 20)
         $fatal(1, "CFG mismatch a=%0d b=%0d", CFG.a, CFG.b);
      if (MAT[0][0] != 1 || MAT[0][1] != 2 || MAT[1][0] != 3 || MAT[1][1] != 4)
         $fatal(1, "MAT mismatch");
      $display("PASSED");
   end
endmodule
