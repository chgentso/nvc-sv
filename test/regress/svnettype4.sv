`timescale 1ns/1ns
// Nets of user-defined nettypes connected through module ports of the same nettype: output, input and inout
// ports, ANSI and non-ANSI port declarations, two levels of hierarchy, drivers on both sides of a port
// Expected values: IEEE 1800-2017 6.6.7 (resolution function called with the values of all the drivers),
// 23.3.3.5 (a port that connects two nets of the same user-defined nettype merges them into a single simulated
// net, so the resolution function sees the drivers inside the instances and outside them, and the net has the
// same value through every name), 23.2.2.1 / 23.2.2.2 (non-ANSI and ANSI port declarations with a
// net_type_identifier), 23.3.2 (connection by order, by name and with .*), 26.3 (package import in a module
// header).
// tnet carries a struct {int sum; int n;}: sum of the driver values and number of drivers; rnet is a real sum.
package svnettype4_pkg;
  typedef struct { int sum; int n; } sn_t;

  function automatic sn_t res_sn (input sn_t d[]);
    res_sn = '{0, 0};
    foreach (d[i]) begin
      res_sn.sum += d[i].sum;
      res_sn.n += d[i].n;
    end
  endfunction

  function automatic real res_sum (input real d[]);
    res_sum = 0.0;
    foreach (d[i]) res_sum += d[i];
  endfunction

  nettype sn_t tnet with res_sn;
  nettype real rnet with res_sum;
endpackage

// Drives its output port
module svnettype4_drv import svnettype4_pkg::*; (output tnet o, input int v);
  assign o = '{v, 1};
endmodule

// Reads its input port
module svnettype4_rd import svnettype4_pkg::*; (input tnet i, output int sum, output int n);
  assign sum = i.sum;
  assign n = i.n;
endmodule

// Non-ANSI inout ports (one declared with its nettype, one redeclared as a net): drives them itself and
// through a nested instance
module svnettype4_mid (io, r, v);
  import svnettype4_pkg::*;
  inout tnet io;
  inout r;
  rnet r;
  input v;
  int v;
  assign io = '{100, 1};
  assign r = 0.25;
  svnettype4_drv d(.o(io), .v(v * 2));
endmodule

// Real nettype: an input port that also has a driver inside the instance
module svnettype4_radd import svnettype4_pkg::*; (input rnet ri, output real seen);
  assign ri = 1.5;
  assign seen = ri;
endmodule

module svnettype4;
  import svnettype4_pkg::*;
  tnet t;
  rnet r;
  int a = 1, b = 10, c = 1000;
  int rsum, rn;
  real seen;
  int fails = 0;

  svnettype4_drv u1(t, a);                       // by order
  svnettype4_mid u2(.io(t), .r(r), .v(b));       // by name
  svnettype4_rd u3(.i(t), .sum(rsum), .n(rn));
  svnettype4_radd u4(.ri(r), .seen);              // .seen implicit named connection
  assign t = '{c, 1};
  assign r = 2.0;

  task automatic check(string what, int esum, int en, real er);
    if (t.sum != esum || t.n != en || rsum != esum || rn != en
        || u1.o.sum != esum || u2.io.sum != esum || u2.d.o.sum != esum || u3.i.n != en
        || r != er || u2.r != er || u4.ri != er || seen != er) begin
      $error("%s: t = '{%0d, %0d} rd = %0d/%0d u1.o = %0d u2.io = %0d u2.d.o = %0d r = %f u4.ri = %f seen = %f",
             what, t.sum, t.n, rsum, rn, u1.o.sum, u2.io.sum, u2.d.o.sum, r, u4.ri, seen);
      fails++;
    end
  endtask

  initial begin
    // tnet drivers: u1 (a), u2 (100), u2.d (2*b), top (c); rnet drivers: u2 (0.25), u4 (1.5), top (2.0)
    #1 check("t=1", 1 + 100 + 20 + 1000, 4, 3.75);
    a = 5; b = -50; #1;
    check("t=2", 5 + 100 - 100 + 1000, 4, 3.75);
    c = 0; #1;
    check("t=3", 5, 4, 3.75);
    if (fails == 0) $display("PASSED");
  end
endmodule
