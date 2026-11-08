// Expected values: IEEE 1800-2017 5.8 (time literals are scaled to the time
// unit of the enclosing module and rounded to its time precision), 22.7 (`timescale / `resetall), 20.3 ($time,
// $realtime), 20.4.2 / 20.4.3 ($timeunit, $timeprecision), 9.4.1 (delays are
// rounded to the time precision).
// After `resetall the module has no `timescale directive, and the default
// time unit/precision is implementation-defined (22.7), so every expectation
// is expressed relative to the time unit of the module: one
// nanosecond literal equals 10**(-9 - $timeunit) module time units.  Delay
// results are compared to the exact value within half a time unit because the
// precision (<= unit) is unknown.
// The text printed by $printtimescale is not checked (implementation-defined).
`timescale 1ns/1ps
package p;
  int x;
endpackage
`resetall
module svtimedef1;
  import p::*;
  time t; real r; int i; real q;
  int errs;
  real ns, pu, e1, e3, t0, t1, t2;
  function automatic bit near(real a, real b);
    real d = a - b; if (d < 0) d = -d;
    return d <= 1e-6 * ((b < 0 ? -b : b) + 1.0);
  endfunction
  // a delay is rounded to the time precision, which is at most one time unit
  function automatic bit halfunit(real a, real b);
    real d = a - b; if (d < 0) d = -d;
    return d <= 0.5 + 1e-9;
  endfunction
  task automatic chk(bit c, string m); if (!c) begin $error("%s", m); errs++; end endtask
  initial begin
    $printtimescale;
    ns = 1ns;                               // one nanosecond in module time units
    pu = 10.0 ** ($timeprecision - $timeunit);  // time precision in time units
    t = 100ns; r = 1.5ns; i = 3ns;
    chk(near(r, $floor(1.5 * ns / pu + 0.5) * pu),
        $sformatf("r = 1.5ns gave %f, expected %f", r, $floor(1.5 * ns / pu + 0.5) * pu));
    chk(near(t, $floor(100 * ns + 0.5)), $sformatf("t = 100ns gave %0d", t));
    chk(i == int'($floor(3 * ns + 0.5)), $sformatf("i = 3ns gave %0d", i));
    q = 10ns;  chk(near(q, 10 * ns), $sformatf("10ns gave %f, expected %f", q, 10 * ns));
    q = 2ns + 1;  chk(near(q, 2 * ns + 1), $sformatf("2ns + 1 gave %f, expected %f", q, 2 * ns + 1));
    // delays
    t0 = $realtime;
    chk(t0 == 0, "simulation does not start at time 0");
    e1 = 1.5 * ns;
    #(1.5ns);
    t1 = $realtime;
    chk(halfunit(t1 - t0, e1), $sformatf("#(1.5ns) advanced %f, expected %f", t1 - t0, e1));
    chk($time == int'($floor(t1 + 0.5)), $sformatf("$time %0d does not match rounded $realtime %f", $time, t1));
    #5;
    t2 = $realtime;
    chk(near(t2 - t1, 5.0), $sformatf("#5 advanced %f, expected 5", t2 - t1));
    e3 = 100 * ns;
    #(100ns);
    chk(halfunit($realtime - t2, e3), $sformatf("#(100ns) advanced %f, expected %f", $realtime - t2, e3));
    // the literal scale agrees with $timeunit (20.4.2)
    chk(near(ns, 10.0 ** (-9 - $timeunit)), $sformatf("1ns = %f units but $timeunit = %0d", ns, $timeunit));
    chk($timeprecision <= $timeunit, "time precision coarser than time unit");
    if (errs == 0) $display("PASSED");
  end
endmodule
