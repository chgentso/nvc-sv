// Program vs module wake-up in one time step: module processes (Active) run before program
// processes (Reactive).
// Expected values: IEEE 1800-2017 4.4/4.5 and 24.3 (program initial and wake-ups are Reactive
// events), 9.4.2 (@ on a variable)
`timescale 1ns/1ns
package pk;
  string log = "";
endpackage
program p(input logic a, input logic [3:0] bus);
  initial begin
    pk::log = {pk::log, $sformatf("p start %0d;", $time)};
    @(a);
    pk::log = {pk::log, $sformatf("p after a %0d;", $time)};
  end
endprogram
module svprogstart2;
  logic a = 0;
  logic b = 0;
  logic [3:0] bus = 4'd5;
  p u(a, bus);
  initial begin
    pk::log = {pk::log, $sformatf("m start %0d;", $time)};
    @(b);
    pk::log = {pk::log, $sformatf("m after b %0d;", $time)};
  end
  initial begin
    #3 a = 1; b = 1;
    #4 $finish;
  end
  final begin
    if (pk::log != "m start 0;p start 0;m after b 3;p after a 3;") $error("log '%s'", pk::log);
    else $display("PASSED");
  end
endmodule
