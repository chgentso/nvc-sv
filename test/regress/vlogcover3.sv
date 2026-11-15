// Statement coverage of functions and tasks declared in a module and in a
// generate block, recursion, fork, continuous assignment and the
// assignment in a net declaration.

module vlogcover3;
   reg [7:0] a = 0;
   wire [7:0] b = a + 1;       // Net declaration assignment
   wire [7:0] c;
   assign c = a + b;           // Continuous assignment
   integer r, k;

   function automatic int fact(input int n);
      if (n <= 1)
         return 1;
      else
         return n * fact(n - 1);
   endfunction

   task automatic bump(input int n);
      for (int i = 0; i < n; i++)
         k = k + 1;
      #1;
   endtask

   // Never called
   function int unused(input int x);
      return x + 1;
   endfunction

   for (genvar g = 0; g < 2; g++) begin : gen
      function int twice(input int x);
         return x * 2;
      endfunction

      initial begin
         if (g == 1)
            r = twice(g);
      end
   end

   initial begin
      k = 0;
      r = fact(4);
      bump(3);
      fork
         #1 a = 1;
         #2 a = 2;
      join
      #1 a = 3;
      if (r !== 24 || k !== 3)
         $display("FAILED %0d %0d", r, k);
      else
         $display("PASSED");
   end
endmodule
