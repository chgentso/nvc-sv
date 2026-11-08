// VPI access to interface instances: vpi_iterate(vpiInterface) from a
// module, vpiDefName of an interface instance, variables inside it,
// modports (vpiModport), arrays of interface instances
// (vpiInterfaceArray, vpi_handle_by_index), a module port of interface
// type seen through a vpiRefObj whose vpiActual is the connected
// instance, and a write to an interface variable observed by a module
// connected through a modport.
// IEEE 1800-2017 37.10 (instance), 37.11 (interface, modport), 37.17
// (ref obj, vpiActual), 25.3/25.5 (interfaces and modports).  The C side
// is test/vpi/vpi8.c; it works at 10 ns.

`timescale 1ns / 1ns

interface vpi8_if;
   logic [7:0] data;
   logic       valid;
   modport mp (input data, input valid);
endinterface

module vpi8_sub (vpi8_if.mp b, output logic [7:0] seen);
   assign seen = b.valid ? b.data : 8'h00;
endmodule

module vpi8;
   vpi8_if bus ();
   vpi8_if arr [2] ();
   logic [7:0] seen;

   vpi8_sub u_sub (.b(bus), .seen(seen));

   initial begin
      bus.data = 8'h00;
      bus.valid = 1'b1;
      arr[1].data = 8'h9e;

      #11;
      if (seen !== 8'h6d) $fatal(1, "seen = %h", seen);
   end
endmodule
