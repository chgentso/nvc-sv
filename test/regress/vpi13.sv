// VPI access to the names and values of the constants of an enumeration
// type through vpiTypespec and vpiEnumConst.  IEEE 1800-2017 37.19.  The
// C side is test/vpi/vpi13.c.

`timescale 1ns / 1ns

typedef enum logic [1:0] { IDLE, RUN = 2, DONE } vpi13_state_t;

module vpi13;
   vpi13_state_t state;
   enum { RED, GREEN, BLUE } colour;
   int plain;

   initial begin
      state = RUN;
      colour = BLUE;
      plain = 5;
   end
endmodule
