// The smallest time precision of the design applies when the top is VHDL.
// Expected values: IEEE 1800-2017 5.8 (a time literal is scaled to the module's time unit and
// rounded to its time precision), 22.7 (`timescale), 20.3.1/20.3.2 ($time rounds to the unit,
// $realtime is a real number in the module's unit): #1.5ns in a 1ns/1ns module is 2 ns; #0.001 in a
// 1ns/1ps module is 1 ps.
`timescale 1ns/1ns
module svprec1_sub;
  initial begin
    #1.5ns;
    if ($time != 2 || $realtime != 2.0) $error("sub: time %0d realtime %f", $time, $realtime);
    else $display("PASSED");
  end
endmodule
`timescale 1ns/1ps
module svprec1_fast;
  initial begin
    #0.001;
    if ($realtime < 0.00099 || $realtime > 0.00101) $error("fast: realtime %f", $realtime);
  end
endmodule
