// The branches of a conditional generate without begin and end are each in
// their own scope, so they can declare the same name (IEEE 1800-2017 27.5).

module sub #(parameter A = 0) (output int o);
   if (A == 1) int i = 10;
   else if (A == 2) int i = 20;
   else int i = 30;

   // The same names again in a second construct of the module
   if (A == 1) int i = 11;
   else int i = 31;

   assign o = A;
endmodule

module vlogifgen1;
   int o0, o1, o2;
   sub #(0) s0 (o0);
   sub #(1) s1 (o1);
   sub #(2) s2 (o2);

   initial begin
      #1;
      if (o0 !== 0 || o1 !== 1 || o2 !== 2)
         $fatal(1, "values %0d %0d %0d", o0, o1, o2);
      $display("PASSED");
   end
endmodule
