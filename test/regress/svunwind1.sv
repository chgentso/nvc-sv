// $finish called by a non-first process in a multi-process deferq batch
`timescale 1ns / 1ns

module svunwind1;
   initial begin
      #5 $display("p1 at 5");
   end

   initial begin
      #5 $display("p2 finish");
      $finish;
      $display("UNEXPECTED after finish");
   end

   initial begin
      #5 $display("p3 at 5");
   end

   initial begin
      #10 $display("UNEXPECTED at 10");
   end
endmodule
