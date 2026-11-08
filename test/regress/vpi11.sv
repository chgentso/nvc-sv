// VPI objects used by testbench frameworks: members of an unpacked
// structure variable (37.18), a named gate instance (vpiGate, 28.3),
// unknown bits written to a two-state variable become 0 (6.3.2.1, 38.34),
// the range of an array of interface instances (37.10/37.11), an array
// of interface ports as a vpiRefObj whose vpiActual is the connected
// array of instances (37.17), the
// compilation-unit scope $unit seen as a package (3.12.1, 37.10) and the
// name of an unnamed generate block (27.6).  The C side is
// test/vpi/vpi11.c; it works at 10 ns.  IEEE 1800-2017.

`timescale 1ns / 1ns

parameter int vpi11_unit_param = 4;

typedef struct {
   logic [3:0] lv;
   bit [7:0]   bv;
   real        r;
} vpi11_rec_t;

interface vpi11_if;
   logic x;
endinterface

module vpi11_sub (vpi11_if p [1:3]);
endmodule

module vpi11;
   vpi11_rec_t rec;
   bit [1:0]   b2;
   logic       a, b, y;
   vpi11_if    ifs [1:3] ();

   and g_and (y, a, b);

   vpi11_sub u_sub (.p(ifs));

   if (1) begin
      logic in_gen;
   end

   initial begin
      rec.lv = 4'b10x1;
      rec.bv = 8'h5a;
      rec.r = 0.5;
      b2 = 2'b11;
      #11;
      if (rec.lv !== 4'b0110) $fatal(1, "rec.lv = %b", rec.lv);
      if (rec.bv !== 8'hc3) $fatal(1, "rec.bv = %h", rec.bv);
      if (rec.r != 2.25) $fatal(1, "rec.r = %f", rec.r);
      if (b2 !== 2'b00) $fatal(1, "b2 = %b", b2);
   end
endmodule
