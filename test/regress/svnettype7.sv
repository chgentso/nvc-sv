`timescale 1ns/1ns
// Real nettypes with a summing resolution function: a net declaration assignment as a driver, drivers from a
// generate loop, an unpacked array of nets, a nettype declared as another name for an existing one and
// connected through ports of either name, a net with no drivers
// Expected values: IEEE 1800-2017 6.6.7 (the net value is the resolution function applied to the values of all
// the drivers; "nettype net_type_identifier net_type_identifier" declares another name for the same nettype, so
// ports of either name match; nets of a nettype may be unpacked arrays, each element resolved on its own; the
// resolution function is called at time zero even for a net with no drivers, here with an empty array),
// 6.6.7 / 10.3.1 (a net declaration assignment is a continuous assignment driver), 27.4 (generate loops),
// 23.3.3.5 (ports of matching nettypes merge the nets).
// All the sums are exact in binary floating point.
package svnettype7_pkg;
  function automatic real res_sum (input real d[]);
    if (d.size() == 0) return -1.0;
    res_sum = 0.0;
    foreach (d[i]) res_sum += d[i];
  endfunction
  nettype real rsum with res_sum;
  nettype rsum rsum_alias;
endpackage

module svnettype7_drv import svnettype7_pkg::*; (output rsum_alias o, input real v);
  assign o = v;
endmodule

module svnettype7;
  import svnettype7_pkg::*;
  localparam int N = 3;
  real base = 0.5;
  real k = 0.25;
  rsum n = k;                   // net declaration assignment: driver 1
  rsum_alias m;
  rsum arr [2];
  rsum none;
  int fails = 0;

  assign n = 2.0;               // driver 2
  for (genvar i = 0; i < N; i++) begin : g
    assign n = base * (i + 1);  // drivers 3 .. N + 2
  end

  svnettype7_drv d1(.o(m), .v(1.0));     // alias port, alias net
  svnettype7_drv d2(.o(n), .v(8.0));     // alias port, original net
  assign m = 4.0;

  assign arr[0] = 1.5;
  assign arr[0] = 1.5;
  assign arr[1] = -3.0;

  initial begin
    #1;
    // n = 0.25 + 2 + (0.5 + 1 + 1.5) + 8
    if (n != 13.25 || m != 5.0 || arr[0] != 3.0 || arr[1] != -3.0 || none != -1.0 || d2.o != n) begin
      $error("t=1: n = %f m = %f arr = %f %f none = %f d2.o = %f", n, m, arr[0], arr[1], none, d2.o);
      fails++;
    end
    k = 1.0; base = 2.0; #1;
    // n = 1 + 2 + (2 + 4 + 6) + 8
    if (n != 23.0) begin
      $error("t=2: n = %f", n);
      fails++;
    end
    if (fails == 0) $display("PASSED");
  end
endmodule
