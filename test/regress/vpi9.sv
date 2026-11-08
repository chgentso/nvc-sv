// A VPI application in a mixed-language design with a Verilog top
// level: the top-level module iteration returns the Verilog top only;
// the nets of the Verilog module connected to a VHDL instance can be
// read, written and monitored with cbValueChange; a value written with
// vpi_put_value propagates through the VHDL instance within the same
// time step.  VPI only describes the Verilog part of the design (IEEE
// 1800-2017 36.1); the VHDL instance is a named object of the nvc
// extension type vpiForeignInst without VPI contents.
// IEEE 1800-2017 37.10, 38.15, 38.34, 38.36.1.  The C side is
// test/vpi/vpi9.c; it writes at 10 ns.

`timescale 1ns / 1ns

module vpi9;
   logic [7:0] a = 8'h00;
   wire [7:0]  y;

   vpi9_inc u_inc (.a(a), .y(y));

   initial begin
      #11;
      if (a !== 8'h42) $fatal(1, "a = %h", a);
      if (y !== 8'h43) $fatal(1, "y = %h", y);
   end
endmodule
