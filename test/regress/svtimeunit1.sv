// timeunit and timeprecision declarations, #1step, and time formatting with
// more than one time precision in the design
// Expected values: IEEE 1800-2017 3.14.2 (timeunit/timeprecision), 3.14.3 (global time precision = finest precision),
// 5.8 (time literals are scaled to the current time unit and rounded to the current precision),
// 20.3 ($time, $realtime in module units), 22.7. Exact ties (x.5 steps of the precision) may round either way,
// so tie cases accept both neighbours; every value must be a multiple of the module's precision.

timeunit 1ns;
timeprecision 1ns;

module svtimeunit1_a;
  timeunit 1ns;
  timeprecision 1ps;
  real r1, r2;
  initial begin
    #1.2345 r1 = $realtime;
    #1ps r2 = $realtime;
  end
endmodule

module svtimeunit1_b;
  timeunit 10ns / 1ns;
  real r1;
  initial begin
    #1.55 r1 = $realtime;
  end
endmodule

module svtimeunit1_c;
  timeunit 1ns;
  timeprecision 100ps;
  real r1, r2;
  initial begin
    #1.55 r1 = $realtime;
    #1step r2 = $realtime;
  end
endmodule

module svtimeunit1_d;
  real r1, r2;
  time t1, t2;
  initial begin
    #1.55 r1 = $realtime; t1 = $time;
    #0.5ns r2 = $realtime; t2 = $time;
  end
endmodule

package svtimeunit1_pkg;
  timeunit 1ps;
  timeprecision 1ps;
  real pr;
  task automatic wait_a_bit;
    #3.4 pr = $realtime;
  endtask
endpackage

module svtimeunit1;
  svtimeunit1_a a();
  svtimeunit1_b b();
  svtimeunit1_c c();
  svtimeunit1_d d();
  int errs;
  time tdone;
  real rdone;
  function automatic bit near(real x, real y);
    return ((x > y) ? (x - y) : (y - x)) < 1.0e-6;
  endfunction
  initial begin
    #100 svtimeunit1_pkg::wait_a_bit;
    tdone = $time; rdone = $realtime;
  end
  initial begin
    #200;
    // a: 1.2345 ns is a tie at 1 ps; 1234 or 1235 ps
    if (!(near(a.r1, 1.234) || near(a.r1, 1.235))) begin $error("a r1 %f", a.r1); errs++; end
    if (!near(a.r2, a.r1 + 0.001)) begin $error("a r2 %f", a.r2); errs++; end
    // b: 15.5 ns is a tie at 1 ns; 15 or 16 ns = 1.5 or 1.6 units of 10 ns
    if (!(near(b.r1, 1.5) || near(b.r1, 1.6))) begin $error("b r1 %f", b.r1); errs++; end
    // c: 1.55 ns is a tie at 100 ps; 1.5 or 1.6 ns; #1step is the global precision (1 ps)
    if (!(near(c.r1, 1.5) || near(c.r1, 1.6))) begin $error("c r1 %f", c.r1); errs++; end
    if (!near(c.r2, c.r1 + 0.001)) begin $error("c r2 %f", c.r2); errs++; end
    // d: inherits 1ns/1ns: 1.55 ns -> 2 ns; then 0.5 ns is a tie at 1 ns: the time must be a whole number of ns
    if (!near(d.r1, 2.0) || d.t1 !== 2) begin $error("d r1 %f t1 %0d", d.r1, d.t1); errs++; end
    if (!(near(d.r2, 2.0) || near(d.r2, 3.0))) begin $error("d r2 %f (not a multiple of 1ns precision)", d.r2); errs++; end
    if (!(d.t2 === 2 || d.t2 === 3)) begin $error("d t2 %0d", d.t2); errs++; end
    // package: 3.4 ps rounds to 3 ps; $realtime in package units (ps)
    if (!near(svtimeunit1_pkg::pr, 100003.0)) begin $error("pkg %f", svtimeunit1_pkg::pr); errs++; end
    // top: 100.003 ns; $time rounds to 100
    if (tdone !== 100) begin $error("done %0d", tdone); errs++; end
    if (!near(rdone, 100.003)) begin $error("done real %f", rdone); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
