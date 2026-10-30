// Output argument of a task which suspends
module vlog54;
   task get(input [7:0] a, output [7:0] b);
      begin
         #5;
         b = a + 1;
      end
   endtask

   reg [7:0] r;

   initial begin
      get(8'd4, r);
      if (r !== 8'd5) $display("FAILED %d", r);
      $display("PASSED");
   end
endmodule
