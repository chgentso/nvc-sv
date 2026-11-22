// Constructs which keep one body for each iteration of a loop
module vlog_genshare11;
   logic clk = 0;
   logic [3:0] a = 4'b0101, f, q;

   for (genvar i = 0; i < 4; i++) begin : g_assert
      always_comb assert (a[i] === (i % 2 == 0)) else $error("bad %0d", i);
   end

   for (genvar i = 0; i < 4; i++) begin : g_past
      always @(posedge clk) q[i] <= $past(a[i]);
   end

   for (genvar i = 0; i < 4; i++) begin : g_func
      function automatic logic inv(logic x);
         return ~x;
      endfunction
      assign f[i] = inv(a[i]);
   end

   initial begin
      #1 clk = 1;
      #1 clk = 0;
      #1 clk = 1;
      #1;
      if (f !== 4'b1010 || q !== 4'b0101) $display("FAILED %b %b", f, q);
      else $display("PASSED");
   end
endmodule
