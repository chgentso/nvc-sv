// VPI names and scopes: the vpiName and vpiFullName of objects with
// escaped identifiers can be passed back to vpi_handle_by_name, with a
// scope and from the top, and give the same object; a named block is an
// internal scope of the module (vpiInternalScope, vpiNamedBegin) whose
// variables have hierarchical full names; a named event is visible as
// vpiNamedEvent.
// IEEE 1800-2017 5.6.1 (escaped identifiers), 37.10 (module, internal
// scope), 37.12 (scope), 37.29 (named event), 38.19
// (vpi_handle_by_name).  The C side is test/vpi/vpi10.c.

`timescale 1ns / 1ns

module vpi10;
   wire \weird.sig[1] ;
   wire \a+b ;
   wire plain;
   event ev;

   assign \weird.sig[1] = 1'b1;
   assign \a+b = 1'b0;
   assign plain = 1'b1;

   initial begin : blk
      reg [3:0] v;
      v = 4'h9;
      #5;
   end
endmodule
