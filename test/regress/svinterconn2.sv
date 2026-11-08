`timescale 1ns/1ns
// A packed interconnect whose bits connect ports of different user-defined nettypes, a whole-interconnect
// connection to a generic module with an interconnect port whose bits go to ports of two user-defined nettypes
// and a built-in net type, and an unpacked array of interconnect nets
// Expected values: IEEE 1800-2017 6.6.8 (an interconnect may be a packed or unpacked array; each bit or element
// becomes a net of the nettype of the ports it connects, so the bits of one interconnect may resolve with
// different nettypes; an interconnect may only be used in port connections), 23.3.3.4 (port connection rules for
// interconnect nets), 6.6.7 (resolution functions).
// rnet: real sum; inet: integer maximum.
package svinterconn2_pkg;
  function automatic real res_sum (input real d[]);
    res_sum = 0.0;
    foreach (d[i]) res_sum += d[i];
  endfunction
  function automatic int res_max (input int d[]);
    res_max = -1000;
    foreach (d[i]) if (d[i] > res_max) res_max = d[i];
  endfunction
  nettype real rnet with res_sum;
  nettype int inet with res_max;
endpackage

module svinterconn2_rdrv import svinterconn2_pkg::*; (output rnet o, input real v);
  assign o = v;
endmodule

module svinterconn2_idrv import svinterconn2_pkg::*; (output inet o, input int v);
  assign o = v;
endmodule

// Two nettype ports of different nettypes, driven and read here
module svinterconn2_both import svinterconn2_pkg::*; (inout rnet r, inout inet i, output real rv, output int iv);
  assign r = 0.5;
  assign i = 3;
  assign rv = r;
  assign iv = i;
endmodule

module svinterconn2_ldrv (output wire logic o, input logic v);
  assign o = v;
endmodule

module svinterconn2_lrd (input wire logic i, output logic seen);
  assign seen = i;
endmodule

// Generic module: passes the bits of its interconnect port to ports of different net types
module svinterconn2_split (inout interconnect [0:2] p, output real rv, output int iv, output logic lv);
  svinterconn2_both b(.r(p[0]), .i(p[1]), .rv, .iv);
  svinterconn2_lrd l(.i(p[2]), .seen(lv));
endmodule

module svinterconn2;
  interconnect [0:1] ib;
  interconnect [0:2] ic;
  interconnect ia [2];
  real rv, rv2, rv3;
  int iv, iv2, iv3;
  logic lv3, z = 1'b0;
  real x = 1.25;
  int y = 9;
  int fails = 0;

  svinterconn2_rdrv r1(.o(ib[0]), .v(x));
  svinterconn2_idrv i1(.o(ib[1]), .v(y));
  svinterconn2_both m(.r(ib[0]), .i(ib[1]), .rv, .iv);

  svinterconn2_rdrv r2(.o(ia[0]), .v(2.0 * x));
  svinterconn2_idrv i2(.o(ia[1]), .v(y - 20));
  svinterconn2_both m2(.r(ia[0]), .i(ia[1]), .rv(rv2), .iv(iv2));

  svinterconn2_rdrv r3(.o(ic[0]), .v(x + 10.0));
  svinterconn2_idrv i3(.o(ic[1]), .v(y + 20));
  svinterconn2_ldrv l3(.o(ic[2]), .v(z));
  svinterconn2_split s3(.p(ic), .rv(rv3), .iv(iv3), .lv(lv3));

  initial begin
    #1 if (rv3 != 11.75 || iv3 != 29 || lv3 !== 1'b0) begin
      $error("t=1: rv3 = %f iv3 = %0d lv3 = %b", rv3, iv3, lv3);
      fails++;
    end
    if (rv != 1.75 || iv != 9 || m.r != rv || m.i != iv || rv2 != 3.0 || iv2 != 3) begin
      $error("t=1: rv = %f iv = %0d rv2 = %f iv2 = %0d", rv, iv, rv2, iv2);
      fails++;
    end
    x = -0.5; y = 2; z = 1'b1; #1;
    if (rv3 != 10.0 || iv3 != 22 || lv3 !== 1'b1) begin
      $error("t=2: rv3 = %f iv3 = %0d lv3 = %b", rv3, iv3, lv3);
      fails++;
    end
    if (rv != 0.0 || iv != 3 || rv2 != -0.5 || iv2 != 3) begin
      $error("t=2: rv = %f iv = %0d rv2 = %f iv2 = %0d", rv, iv, rv2, iv2);
      fails++;
    end
    if (fails == 0) $display("PASSED");
  end
endmodule
