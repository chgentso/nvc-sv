`timescale 1ns/1ns
// Drivers of a net of a user-defined nettype reached through the hierarchy: continuous assignments to a
// hierarchical net name (downward and upward), instances in a generate loop connected to the same net through
// ports, hierarchical reads of the resolved value
// Expected values: IEEE 1800-2017 6.6.7 (the resolution function is called with the values of all the drivers
// of the net, wherever they are), 10.3.1 / A.8.5 (the target of a continuous assignment can be a hierarchical net
// name), 23.6 / 23.8 (downward and upward hierarchical names), 27.4 (loop generate constructs), 23.3.3.5 (ports of
// the same nettype merge the nets).
// The resolution function returns the sum of the drivers times 1000 plus their number, so a lost or duplicated
// driver changes the result.
package svnettype5_pkg;
  function automatic int res_cnt (input int d[]);
    int s = 0;
    foreach (d[i]) s += d[i];
    return s * 1000 + d.size();
  endfunction
  nettype int cnet with res_cnt;
endpackage

module svnettype5_leaf import svnettype5_pkg::*; (output cnet o, input int v);
  assign o = v;
endmodule

// Has a net of its own that is driven from the top through a hierarchical name, and drives the top-level net
// with an upward hierarchical name
module svnettype5_sub;
  import svnettype5_pkg::*;
  cnet local_n;
  int k = 3;
  assign local_n = k;
  assign svnettype5.up = 7;
endmodule

module svnettype5;
  import svnettype5_pkg::*;
  localparam int N = 4;
  cnet bus;
  cnet up;
  int base = 1;
  int fails = 0;

  for (genvar i = 0; i < N; i++) begin : g
    svnettype5_leaf l(.o(bus), .v(base + i));
  end

  svnettype5_sub s();
  assign s.local_n = 40;
  assign up = 2;

  initial begin
    #1;
    // bus: drivers 1, 2, 3, 4; s.local_n: 3 (inside) and 40 (from here); up: 2 (here) and 7 (from s)
    if (bus != 10 * 1000 + N || g[2].l.o != bus || s.local_n != 43 * 1000 + 2 || up != 9 * 1000 + 2) begin
      $error("t=1: bus = %0d g[2].l.o = %0d s.local_n = %0d up = %0d", bus, g[2].l.o, s.local_n, up);
      fails++;
    end
    base = 10; s.k = -40; #1;
    if (bus != 46 * 1000 + N || g[0].l.o != bus || s.local_n != 0 * 1000 + 2) begin
      $error("t=2: bus = %0d g[0].l.o = %0d s.local_n = %0d", bus, g[0].l.o, s.local_n);
      fails++;
    end
    if (fails == 0) $display("PASSED");
  end
endmodule
