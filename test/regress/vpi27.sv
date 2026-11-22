// VPI access to the scopes of a for-generate loop whose body is shared
// by its iterations: a local parameter computed from the genvar is a
// parameter of each generate scope, signals can be read and written by
// name.  The C side is test/vpi/vpi27.c; it works at 10 ns.

`timescale 1ns / 1ns

module vpi27;
   logic [3:0] a = 4'b1001;
   logic [3:0] y;
   for (genvar i = 0; i < 4; i++) begin : g
      localparam int Pa = 3 * i + 1;
      logic t;
      assign t = a[i];
      assign y[i] = t;
   end
   initial #20 $display("y=%b", y);
endmodule
