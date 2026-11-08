// Expected values: IEEE 1800-2017 22.7 (`timescale 100s/10s), 20.3 ($time rounded to integer units, $realtime),
// 20.4.2/21.3.2 ($timeformat controls %t; units 0 = seconds)
`timescale 100s/10s
module svtimescale100s1;
  int errs;
  string st;
  function automatic bit near(real x, real y);
    return ((x > y) ? (x - y) : (y - x)) < 1.0e-9;
  endfunction
  initial begin
    $timeformat(0, 0, "", 0);
    #1;
    // 100 s
    if ($time !== 1) begin $error("time %0d", $time); errs++; end
    if (!near($realtime, 1.0)) begin $error("realtime %f", $realtime); errs++; end
    st = $sformatf("%0t", $realtime);
    if (st != "100") begin $error("%%0t '%s'", st); errs++; end
    #0.5;
    // 50 s more (a multiple of the 10 s precision): 150 s
    if (!near($realtime, 1.5)) begin $error("realtime2 %f", $realtime); errs++; end
    if ($time !== 2) begin $error("time2 %0d", $time); errs++; end
    st = $sformatf("%0t", $realtime);
    if (st != "150") begin $error("%%0t2 '%s'", st); errs++; end
    if (errs == 0) $display("PASSED");
    $finish;
  end
endmodule
