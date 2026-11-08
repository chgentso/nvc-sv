`timescale 1ns/1ns
// interconnect nets and interconnect ports joining ports of a user-defined nettype: two drivers and two readers
// on a scalar interconnect, a module whose port is an interconnect passing it on to an instance with a nettype
// port, and a nettype net on the other side of an interconnect port
// Expected values: IEEE 1800-2017 6.6.8 (an interconnect net has no data type of its own; the simulated net
// takes the nettype of the ports it connects), 23.2.2.1 / 23.2.2.2 (interconnect port declarations), 23.3.3.4
// (an interconnect connected to ports of a user-defined nettype becomes a net of that nettype, all the connected
// ports merge into one simulated net), 6.6.7 (the resolution function sees every driver of the merged net).
// rnet resolves to the sum of the drivers plus 1000 times their number.
package svinterconn1_pkg;
  function automatic real res_sumn (input real d[]);
    res_sumn = 1000.0 * d.size();
    foreach (d[i]) res_sumn += d[i];
  endfunction
  nettype real rnet with res_sumn;
endpackage

module svinterconn1_src import svinterconn1_pkg::*; (output rnet o, input real v);
  assign o = v;
endmodule

module svinterconn1_snk import svinterconn1_pkg::*; (input rnet i, output real seen);
  assign seen = i;
endmodule

// Generic module: its port has no nettype of its own
module svinterconn1_pass (inout interconnect p, input real v);
  svinterconn1_src s(.o(p), .v(v));
endmodule

module svinterconn1_pass2 (p, v);
  inout interconnect p;
  input real v;
  svinterconn1_src s(.o(p), .v(v));
endmodule

module svinterconn1;
  import svinterconn1_pkg::*;
  interconnect ic;
  rnet n;
  real a = 1.5, b = 2.25, c = 4.0, d = 8.0;
  real s1, s2, s3;
  int fails = 0;

  svinterconn1_src u1(.o(ic), .v(a));
  svinterconn1_src u2(.o(ic), .v(b));
  svinterconn1_snk k1(.i(ic), .seen(s1));
  svinterconn1_snk k2(ic, s2);
  svinterconn1_pass p1(.p(ic), .v(c));

  // A nettype net on the outside of an interconnect port
  svinterconn1_pass2 p2(.p(n), .v(d));
  svinterconn1_snk k3(.i(n), .seen(s3));
  assign n = 0.5;

  initial begin
    // ic: u1, u2, p1.s -> 3 drivers; n: p2.s and the assignment here -> 2 drivers
    #1 if (s1 != 3007.75 || s2 != s1 || k1.i != s1 || p1.s.o != s1 || n != 2008.5 || s3 != n || p2.s.o != n) begin
      $error("t=1: s1 = %f s2 = %f k1.i = %f p1.s.o = %f n = %f s3 = %f", s1, s2, k1.i, p1.s.o, n, s3);
      fails++;
    end
    a = -1.5; d = 0.0; #1;
    if (s1 != 3004.75 || s2 != s1 || n != 2000.5 || s3 != n) begin
      $error("t=2: s1 = %f s2 = %f n = %f s3 = %f", s1, s2, n, s3);
      fails++;
    end
    if (fails == 0) $display("PASSED");
  end
endmodule
