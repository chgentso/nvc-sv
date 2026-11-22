// A loop with many iterations shares one body
module vlog_genshare10;
   localparam int N = 10000;
   logic [N-1:0] a, y;
   for (genvar i = 0; i < N; i++) begin : g
      assign y[i] = ~a[N - 1 - i];
   end
   initial begin
      a = '0;
      a[17] = 1'b1;
      #1;
      if (y[N - 18] !== 1'b0 || y[0] !== 1'b1 || $countones(y) != N - 1)
         $display("FAILED");
      else
         $display("PASSED");
   end
endmodule
