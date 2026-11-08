// VPI navigation of the design hierarchy: top-level modules from a NULL
// handle, module instances, ports, nets, variables, parameters and
// generate scopes; the vpiName, vpiFullName, vpiDefName, vpiDefFile,
// vpiTopModule, vpiType, vpiDirection, vpiLocalParam properties;
// vpi_handle_by_name with and without a scope, vpi_handle(vpiModule)
// to the parent, vpi_compare_objects and vpi_release_handle.
// IEEE 1800-2017 37.10 (module), 37.12 (ports), 37.13 (nets), 37.17
// (variables), 37.21 (parameters), 37.31 (generate scopes), 38.6
// (vpi_compare_objects), 38.12/38.13 (vpi_get/vpi_get_str), 38.19
// (vpi_handle_by_name), 38.22/38.31 (vpi_iterate/vpi_scan).

`timescale 1ns / 1ns

module vpi2_sub #(parameter int W = 4) (input logic [W-1:0] a,
                                         output logic [W-1:0] y);
   assign y = ~a;
endmodule

module vpi2;
   parameter int WIDTH = 8;
   localparam int LP = WIDTH * 2;

   wire [WIDTH-1:0] w;
   reg [WIDTH-1:0]  r;
   integer          i;

   vpi2_sub #(.W(WIDTH)) u_sub (.a(r), .y(w));

   for (genvar g = 0; g < 3; g++) begin : gen
      reg [g:0] q;
   end

   if (WIDTH == 8) begin : g_if
      wire inner;
   end

   initial begin
      r = 8'h0f;
      #1;
      if (w !== 8'hf0) $fatal(1, "w = %h", w);
   end
endmodule
