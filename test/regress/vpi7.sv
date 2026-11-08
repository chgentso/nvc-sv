// VPI access to packages and parameters: vpi_iterate(vpiInstance, NULL)
// returns the package and the top module; parameters of a package and of
// a module with integer, real, string and vector values; parameters
// overridden at instantiation; a variable declared in a package (read
// and write); vpi_handle_by_name of package items.
// IEEE 1800-2017 37.10 (instance, package), 37.21 (parameters), 26.2
// (packages), 23.10 (parameter override).  The C side is test/vpi/vpi7.c; it works at 10 ns.

`timescale 1ns / 1ns

package vpi7_pkg;
   parameter int    P_INT = 5;
   parameter real   P_REAL = 1.5;
   parameter string P_STR = "abc";
   localparam logic [11:0] P_VEC = 12'ha5c;
   logic [7:0]      pkg_var = 8'h33;
endpackage

module vpi7_sub #(parameter int N = 1, parameter logic [3:0] M = 4'h0) ();
endmodule

module vpi7;
   import vpi7_pkg::*;

   vpi7_sub #(.N(P_INT * 3), .M(4'hb)) u1 ();
   vpi7_sub u2 ();

   initial begin
      #11;
      if (vpi7_pkg::pkg_var !== 8'h44)
        $fatal(1, "pkg_var = %h", vpi7_pkg::pkg_var);
   end
endmodule
