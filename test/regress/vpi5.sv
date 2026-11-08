// VPI access to arrays: unpacked arrays of variables and nets (type,
// vpiSize, vpiRange, element iteration, vpi_handle_by_index, element
// names, read and write of elements), elements of a two-dimensional
// unpacked array by name, bit-selects of a vector by index, and the
// ranges of a two-dimensional packed array.
// IEEE 1800-2017 37.17 (variables, var select, vpiRange), 37.13 (nets
// and net arrays), 38.18 (vpi_handle_by_index), 7.4 (packed and unpacked
// arrays).  The C side is test/vpi/vpi5.c; it works at 10 ns.

`timescale 1ns / 1ns

module vpi5;
   reg [7:0]        mem [0:3];
   wire [3:0]       wa [1:0];
   logic [7:0]      m2 [0:1][0:2];
   reg [7:0]        vec;
   logic [1:0][3:0] p2;

   assign wa[0] = 4'h3;
   assign wa[1] = 4'hc;

   initial begin
      for (int k = 0; k < 4; k++)
        mem[k] = 8'h10 + k;
      m2[1][2] = 8'hee;
      vec = 8'h00;
      p2 = 8'h5a;

      #11;
      if (mem[2] !== 8'hab) $fatal(1, "mem[2] = %h", mem[2]);
      if (mem[1] !== 8'h11) $fatal(1, "mem[1] = %h", mem[1]);
      if (m2[0][1] !== 8'h42) $fatal(1, "m2[0][1] = %h", m2[0][1]);
      if (vec !== 8'h08) $fatal(1, "vec = %h", vec);
   end
endmodule
