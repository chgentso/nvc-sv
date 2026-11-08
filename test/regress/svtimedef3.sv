// IEEE 1800-2017 3.14.2.3: a design element without a `timescale directive
// or timeunit declaration uses the default time unit and precision, which
// the LRM leaves to the tool.  nvc uses a fixed 1ns/1ns, independent of the
// time units of the other design elements.  20.4.2: $timeunit and
// $timeprecision.  20.3.3: $realtime is scaled to the time unit of the module
// that calls it.  9.4.1: a delay is rounded to the time precision of its
// module.
module svtimedef3_sub;                  // no time unit
  real at[2];
  int  tu, tp;
  time t;
  initial begin
    tu = $timeunit;
    tp = $timeprecision;
    #3;
    at[0] = svtimedef3.now();
    #1.4;                               // rounded to 1ns
    at[1] = svtimedef3.now();
    t = $time;
  end
endmodule

`timescale 1ns/1ps
module svtimedef3_fine;                 // makes the design precision 1ps
  real at;
  initial begin
    #1.5;
    at = $realtime;
  end
endmodule

`timescale 10ns/10ns
module svtimedef3;
  svtimedef3_sub s();
  svtimedef3_fine f();
  int errs;

  function real now();
    return $realtime;
  endfunction

  function automatic bit near(real a, real b);
    return (a > b ? a - b : b - a) < 1.0e-9;
  endfunction

  task automatic chk(bit c, string m);
    if (!c) begin $error("%s", m); errs++; end
  endtask

  initial begin
    #1;
    chk(s.tu == -9, $sformatf("sub $timeunit %0d", s.tu));
    chk(s.tp == -9, $sformatf("sub $timeprecision %0d", s.tp));
    chk(near(s.at[0], 0.3), $sformatf("#3 in sub fired at %f x 10ns", s.at[0]));
    chk(near(s.at[1], 0.4), $sformatf("#1.4 in sub fired at %f x 10ns", s.at[1]));
    chk(s.t == 4, $sformatf("sub $time %0d", s.t));
    chk(near(f.at, 1.5), $sformatf("fine module $realtime %f", f.at));
    if (errs == 0) $display("PASSED");
  end
endmodule
