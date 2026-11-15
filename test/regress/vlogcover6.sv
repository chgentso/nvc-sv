// Branch coverage of if statements: with and without else, an else if
// chain, nested conditions, unique and priority if, and a condition with
// an unknown value which is false.

module vlogcover6;
   reg clk = 0;
   reg [2:0] sel = 0;
   reg [3:0] a = 0;
   reg [3:0] n = 0;
   reg x = 1'bx;
   integer r;

   always #5 clk = ~clk;

   always @(posedge clk) begin
      sel <= sel + 1;

      // Never false
      if (clk)
         a <= a + 1;

      // Without else, true twice
      if (sel == 1 || sel == 2)
         n <= n + 1;

      // A chain: the else is the false of the last condition
      if (sel == 0)
         r = 0;
      else if (sel == 1)
         r = 1;
      else if (sel == 7)
         r = 7;
      else
         r = 2;

      // Nested, the inner never true
      if (sel > 0) begin
         if (sel > 6)
            r = 9;
      end

      // An unknown condition takes the else
      if (x)
         r = 10;
      else
         r = 11;
   end

   always @(posedge clk) begin
      unique if (sel == 0)
         r = 20;
      else if (sel != 0)
         r = 21;

      priority if (sel == 1)
         r = 22;
      else
         r = 23;
   end

   initial begin
      #32;
      $display("PASSED");
      $finish;
   end
endmodule
