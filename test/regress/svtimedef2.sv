// Expected values: IEEE 1800-2017 22.7 (`timescale), 22.3 (`resetall), 20.3 ($time, $realtime: module time units;
// $time is rounded to an integer of the module time unit), 3.14.2 (time units and precision)
`timescale 10ns/1ns
module sub;
  real at_ns;
  real rt;
  time tt;
  initial begin #1.5; at_ns = $realtime * 10.0; rt = $realtime; tt = $time; end
endmodule
`resetall
`timescale 1ns/1ns
module svtimedef2;
  sub s();
  int errs;
  function automatic bit near(real x, real y);
    return ((x > y) ? (x - y) : (y - x)) < 1.0e-6;
  endfunction
  initial begin
    $printtimescale;
    #3;
    if (!near($realtime, 3.0) || $time !== 3) begin $error("top time %f %0d", $realtime, $time); errs++; end
    #20;
    // sub: 1.5 units of 10ns = 15ns
    if (!near(s.at_ns, 15.0)) begin $error("sub absolute time %f ns", s.at_ns); errs++; end
    if (!near(s.rt, 1.5)) begin $error("sub realtime %f", s.rt); errs++; end
    if (s.tt !== 2) begin $error("sub time %0d", s.tt); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
