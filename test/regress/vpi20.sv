// Procedures as VPI objects: vpiInitial, vpiAlways and vpiFinal objects
// listed by vpi_iterate(vpiProcess, module), with their type, always type,
// source line, scope, the variables of their block and an extension for the
// state of the process.  IEEE 1800-2017 37.x (module, process), 37.17.  The
// C side is test/vpi/vpi20.c.

`timescale 1ns / 1ps

module vpi20;
   logic clk = 0;
   int   cnt = 0;
   int   comb = 0;
   int   ffv = 0;

   initial begin
      int lv;
      lv = 3;
      #10;
      lv = 7;
      #10;
      lv = 9;
   end

   always #5 clk = ~clk;

   always @(posedge clk) begin
      int k;
      k = cnt;
      cnt <= k + 1;
   end

   always_comb comb = cnt * 2;

   always_ff @(posedge clk) ffv <= cnt;

   initial #100 $finish;

   final $display("final done");
endmodule
