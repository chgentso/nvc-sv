// Expected values: IEEE 1800-2017 20.3 ($time in module units, $realtime), 20.4.2/21.3.2 ($timeformat, %t),
// 3.14.2 (time literals). The time unit 1ns and precision 1ps come from the run option `timescale=1ns/1ps`.
module svtimescaleopt1;
  int p = 20000;
  int errs;
  string st;
  function automatic bit near(real x, real y);
    return ((x > y) ? (x - y) : (y - x)) < 1.0e-6;
  endfunction
  initial begin
    $timeformat(-12, 0, "ps", 0);
    #(p * 1ps);
    if ($time !== 20) begin $error("time %0d", $time); errs++; end
    if (!near($realtime, 20.0)) begin $error("realtime %f", $realtime); errs++; end
    st = $sformatf("%0t", $time);
    if (st != "20000ps") begin $error("%%0t '%s'", st); errs++; end
    #(2.5ns);
    if (!near($realtime, 22.5)) begin $error("realtime2 %f", $realtime); errs++; end
    st = $sformatf("%0t", $realtime);
    if (st != "22500ps") begin $error("%%0t2 '%s'", st); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
