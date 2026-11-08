// VPI view of SystemVerilog data types: object types (vpiIntVar,
// vpiShortIntVar, vpiLongIntVar, vpiByteVar, vpiBitVar, vpiLogicVar,
// vpiStringVar, vpiEnumVar, vpiStructVar, vpiUnionVar, vpiRealVar),
// vpiSize and vpiSigned of each, members of packed structures and unions
// (vpiMember, vpiPacked), values of the whole aggregate and of members,
// 64-bit values, and writes to a member and to an enum variable.
// IEEE 1800-2017 37.17 (variables), 37.18 (structure and union
// variables), 6.11 (integer data types), 7.2/7.3 (structures and
// unions), 6.19 (enumerations).  The C side is test/vpi/vpi6.c; it works
// at 10 ns.

`timescale 1ns / 1ns

module vpi6;
   typedef struct packed {
      logic [3:0] hi;
      logic [3:0] lo;
   } pair_t;

   typedef union packed {
      logic [7:0]      word;
      logic [1:0][3:0] nib;
   } word_t;

   typedef enum logic [1:0] { IDLE, RUN, DONE } state_t;

   int        i32;
   shortint   i16;
   longint    i64;
   byte       i8;
   bit        b1;
   bit [7:0]  bv;
   logic      l1;
   string     str;
   state_t    st;
   pair_t     pr;
   word_t     wu;
   real       re;

   initial begin
      i32 = -100;
      i16 = 16'h8001;
      i64 = 64'h8000_0000_0000_0003;
      i8 = -2;
      b1 = 1;
      bv = 8'hc3;
      l1 = 1'bx;
      str = "sv";
      st = RUN;
      pr = 8'h9c;
      wu = 8'h35;
      re = 1.0e10;

      #11;
      if (pr.lo !== 4'h1) $fatal(1, "pr = %h", pr);
      if (st !== DONE) $fatal(1, "st = %s", st.name());
      if (i64 !== 64'hffff_ffff_0000_0001) $fatal(1, "i64 = %h", i64);
   end
endmodule
