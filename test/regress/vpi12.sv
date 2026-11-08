// VPI access to values held in memory rather than in signals: an
// unpacked structure with a nested unpacked structure, a member that is
// an unpacked array and a wide member, an unpacked array of unpacked
// structures, and package and module variables wider than 64 bits.
// IEEE 1800-2017 37.17, 37.18, 38.15, 38.34.  The C side is
// test/vpi/vpi12.c; it works at 10 ns.

`timescale 1ns / 1ns

package vpi12_pkg;
   logic [99:0] wide4 = {4'h9, 32'h01234567, 32'h89abcdef, 32'hfedcba98};
   bit [79:0]   wide2 = 80'h1234_5678_9abc_def0_1357;
endpackage

typedef struct {
   logic [3:0] x;
   int         n;
} vpi12_inner_t;

typedef struct {
   logic          a;
   logic [7:0]    b [0:2];
   vpi12_inner_t  in;
   logic [71:0]   w;
} vpi12_rec_t;

module vpi12;
   vpi12_rec_t rec;
   vpi12_rec_t arr [0:1];
   logic [127:0] mwide;

   initial begin
      rec.a = 1;
      rec.b[0] = 8'h10; rec.b[1] = 8'h11; rec.b[2] = 8'h12;
      rec.in.x = 4'h5;
      rec.in.n = -7;
      rec.w = 72'hab_0123456789abcdef;
      arr[0].a = 0;
      arr[1].a = 1;
      arr[1].b[2] = 8'h42;
      arr[1].in.n = 99;
      mwide = 128'h0;

      #11;
      if (rec.b[1] !== 8'h77) $fatal(1, "rec.b[1] = %h", rec.b[1]);
      if (rec.in.x !== 4'hc) $fatal(1, "rec.in.x = %h", rec.in.x);
      if (rec.w !== 72'hcd_fedcba9876543210) $fatal(1, "rec.w = %h", rec.w);
      if (arr[1].in.n != 1234) $fatal(1, "arr[1].in.n = %0d", arr[1].in.n);
      if (arr[0].b[0] !== 8'h3c) $fatal(1, "arr[0].b[0] = %h", arr[0].b[0]);
      if (vpi12_pkg::wide4 !== {4'h3, 96'h0}) $fatal(1, "wide4 = %h", vpi12_pkg::wide4);
      if (vpi12_pkg::wide2 !== 80'h1) $fatal(1, "wide2 = %h", vpi12_pkg::wide2);
      if (mwide !== {64'h1, 64'h2}) $fatal(1, "mwide = %h", mwide);
   end
endmodule
