// The same file included twice without a guard redefines a module
module vlog67_sub;
   function automatic int f(int x);
      return x;
   endfunction
   parameter P = f(1);
   int p = P;
endmodule

module vlog67_sub;
   function automatic int f(int x);
      return x + 1;
   endfunction
   parameter P = f(1);
   int p = P;
endmodule

module vlog67;
   vlog67_sub u ();
   initial begin
      #1;
      if (u.p != 2) $display("FAILED %0d", u.p);
      $display("PASSED");
   end
endmodule
