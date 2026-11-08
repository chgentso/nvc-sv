`timescale 1ns/1ns
// User-defined nettypes with resolution functions, several drivers per net, a delayed driver
// Expected values: IEEE 1800-2017 6.6.7 (the value of a nettype net is the resolution function applied to all its
// driver values; a nettype without a resolution function allows a single driver), 10.3.3 (the delayed continuous
// assignment updates its driver after the delay), 6.12.2 (int'(real) rounds to the nearest integer, halves away
// from zero: int'(1.5) = 2, int'(2.5) = 3), 11.4.3 (|)
module svnettype2;
  function automatic int sres(input int d[]);
    int r = 0;
    foreach (d[i]) r += d[i];
    return r;
  endfunction
  function automatic real rmax(input real d[]);
    real m = -1e9;
    foreach (d[i]) if (d[i] > m) m = d[i];
    return m;
  endfunction
  function automatic logic [3:0] lor(input logic [3:0] d[]);
    logic [3:0] r = 4'b0;
    foreach (d[i]) r |= d[i];
    return r;
  endfunction
  nettype int sumnet with sres;
  nettype real maxnet with rmax;
  nettype logic [3:0] lnet with lor;
  nettype real single;
  maxnet m;
  sumnet s;
  lnet l;
  single sg;
  real a = 1.5, b = 2.5, c = -4.0;
  logic [3:0] p = 4'b0001, q = 4'b0100;
  int fails = 0;
  assign m = a;
  assign m = b;
  assign #2 m = c;
  assign sg = a * 2;
  assign s = int'(a);
  assign s = int'(b) + 1;
  assign l = p;
  assign l = q;
  initial begin
    // t=1: m = max(1.5, 2.5) (the delayed driver of c is not sampled before it first updates at t=2, and its
    // value is below both anyway), s = 2 + (3+1), sg = 3.0, l = 0001|0100
    #1 if (m != 2.5 || s != 6 || sg != 3.0 || l !== 4'b0101) begin
      $error("t=1: m=%f s=%0d sg=%f l=%b", m, s, sg, l); fails++;
    end
    a = 7.0; p = 4'b1000; #1;
    if (m != 7.0 || s != 11 || sg != 14.0 || l !== 4'b1100) begin
      $error("t=2: m=%f s=%0d sg=%f l=%b", m, s, sg, l); fails++;
    end
    b = 9.0; q = 4'b0010; #1;
    if (m != 9.0 || s != 17 || l !== 4'b1010) begin
      $error("t=3: m=%f s=%0d l=%b", m, s, l); fails++;
    end
    // c changes at t=3; its delayed driver follows at t=5, which is not sampled at the instant of the update
    // (the order of the update and of this process resuming at t=5 is not defined)
    c = 20.0; #1;
    if (m != 9.0) begin $error("t=4: m=%f", m); fails++; end
    #3;
    if (m != 20.0) begin $error("t=7: m=%f", m); fails++; end
    if (fails == 0) $display("PASSED");
  end
endmodule
