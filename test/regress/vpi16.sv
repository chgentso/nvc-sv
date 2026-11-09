// Value change callbacks on values held in memory: members of unpacked
// structures, elements of unpacked arrays, whole structures and arrays,
// string variables and package variables, and the string and time values
// a debugger reads in the callback.  IEEE 1800-2017 38.36.  The C side is
// test/vpi/vpi16.c.

`timescale 1ns / 1ps

package vpi16_pkg;
   int counter = 0;
endpackage

typedef struct {
   int         a;
   logic [7:0] b;
   real        r;
   string      s;
   int         arr [0:1];
} vpi16_rec_t;

module vpi16;
   vpi16_rec_t rec;
   vpi16_rec_t recs [0:1];
   string str = "";
   time   t = 0;
   int    plain = 0;

   initial begin
      rec.a = 0; rec.b = 0; rec.r = 0.0; rec.s = ""; rec.arr[0] = 0;
      rec.arr[1] = 0;
      recs[0].a = 0; recs[1].a = 0;
      #1 rec.a = 1;                // a: 1
      #1 rec.b = 8'h55;            // b: 1
      #1 recs[1].a = 5;            // recs[1].a: 1
      #1 rec.arr[1] = 3;           // arr[1]: 1
      #1 rec.r = 1.5;              // r: 1
      #1 rec.s = "hello";          // s: 1
      #1 str = "world";            // str: 1
      #1 t = $time;                // t: 1
      #1 rec.a = 1;                // unchanged
      #1 plain = 1;                // unchanged
      #1 vpi16_pkg::counter = 7;   // counter: 1
      #1 rec.a = 2;                // a: 2
      #1 $display("done");
   end
endmodule
