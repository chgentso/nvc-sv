// vpi_get_value in the binary, octal, hex, decimal string, integer,
// scalar, vector, real, string and object type formats and
// vpi_put_value with vpiNoDelay, vpiInertialDelay, vpiTransportDelay,
// vpiForceFlag and vpiReleaseFlag on variables and nets of 4-state,
// 2-state, real and string types, including values wider than 64 bits.
// vpiLeftRange/vpiRightRange expressions of a vector.
// IEEE 1800-2017 38.15 (vpi_get_value), 38.34 (vpi_put_value), 37.13,
// 37.17 (vpiLeftRange, vpiRightRange, vpiSize, vpiSigned, vpiVector).
// The C side is test/vpi/vpi3.c; it works at 10 ns and the module
// checks the effect of the writes.

`timescale 1ns / 1ns

module vpi3;
   logic [7:0]  v8;
   reg [3:0]    r4;
   integer      i;
   real         re;
   string       s;
   logic [99:0] wide;
   logic [99:0] wide_in;
   bit [15:0]   b16;
   logic        l;
   wire         w;
   wire [7:0]   wv;
   logic [7:0]  inr, intr;     // Inertial and transport targets

   assign w = l;
   assign wv = v8;

   initial begin
      v8 = 8'b1010x0z1;
      r4 = 4'h3;
      i = -5;
      re = 2.5;
      s = "hello";
      wide = {4'ha, 32'h12345678, 32'h9abcdef0, 32'h0fedcba9};
      b16 = 16'hbeef;
      l = 1'b0;
      inr = 0;
      intr = 0;

      #11;                     // The application wrote at 10 ns
      if (r4 !== 4'b10z1) $fatal(1, "r4 = %b", r4);
      if (i !== 1234) $fatal(1, "i = %0d", i);
      if (re != -0.75) $fatal(1, "re = %f", re);
      if (s != "world") $fatal(1, "s = %s", s);
      if (wide_in !== {4'h5, 32'hdeadbeef, 32'h01234567, 32'h89abcdef})
        $fatal(1, "wide_in = %h", wide_in);
      if (b16 !== 16'h1234) $fatal(1, "b16 = %h", b16);
      if (w !== 1'b1) $fatal(1, "w not forced: %b", w);
      if (wv !== 8'h77) $fatal(1, "wv not forced: %h", wv);
      if (inr !== 8'h11) $fatal(1, "inr = %h", inr);  // Zero delay
      if (intr !== 8'h00) $fatal(1, "intr = %h", intr);
      #3;                      // 14 ns: transport write applied at 13 ns
      if (intr !== 8'h22) $fatal(1, "intr = %h", intr);
      if (w !== 1'b1) $fatal(1, "w not forced: %b", w);
      #2;                      // 16 ns: released at 15 ns
      if (w !== 1'b0) $fatal(1, "w not released: %b", w);
      if (wv !== v8) $fatal(1, "wv not released: %h", wv);
   end
endmodule
