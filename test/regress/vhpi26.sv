// A VHPI program reaches the instances of VHDL entities in a design with a
// Verilog top-level module: the instance is found by its hierarchical name
// through the Verilog top and as a statement of the root instance, and
// its signals can be read, deposited and monitored with vhpiCbValueChange
// (IEEE 1076-2019 clause 21).  The C side is test/vhpi/vhpi26.c; it
// deposits 5 on the counter at 10 ns.

`timescale 1ns / 1ns

module vhpi26;
   logic       clk = 0;
   wire [7:0]  q;

   always #5 clk = ~clk;     // Rising edges at 5, 15, 25 ns

   vhpi26_cnt u_cnt (.clk(clk), .q(q));

   initial begin
      #12;
      if (q !== 8'd5) $fatal(1, "q = %0d at 12 ns", q);
      #5;
      if (q !== 8'd6) $fatal(1, "q = %0d at 17 ns", q);
      #10;
      $display("PASSED");
      $finish;
   end
endmodule
