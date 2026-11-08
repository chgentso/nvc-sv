// Program initial blocks start in the Reactive region, after module initial blocks of time 0.
// Expected values: IEEE 1800-2017 4.4/4.5 and 24.3 (program initial procedures are Reactive
// events, run after all Active events of the time step), 9.4.2 (@ on a net, a bus, posedge),
// 24.7 (simulation ends when the programs have finished)
`timescale 1ns/1ns
package pk;
  string log = "";
endpackage
program p(input logic clk, input logic a, input logic [3:0] bus);
  initial begin
    pk::log = {pk::log, $sformatf("p start %0d;", $time)};
    @(a);
    pk::log = {pk::log, $sformatf("p after a %0d;", $time)};
    @(bus);
    pk::log = {pk::log, $sformatf("p after bus %0d;", $time)};
    @(posedge clk);
    pk::log = {pk::log, $sformatf("p after posedge %0d;", $time)};
  end
  initial begin
    @(posedge clk);
    pk::log = {pk::log, $sformatf("p2 posedge %0d;", $time)};
  end
endprogram
module svprogstart1;
  logic clk = 0;
  logic a = 0;
  logic [3:0] bus = 4'd5;
  logic w;
  assign w = clk;
  always #5 clk = ~clk;
  p u(clk, a, bus);
  initial begin
    pk::log = {pk::log, $sformatf("m start %0d;", $time)};
    #12 a = 1;
    #3 bus = 7;
    #40 $finish;
  end
  // The module initial block (Active) precedes the program (Reactive) at time 0. At 15 the program
  // wakes on bus in the Reactive region, after the clk edge of time 15 (Active), so the next
  // posedge it sees is at 25.
  final begin
    if (pk::log != "m start 0;p start 0;p2 posedge 5;p after a 12;p after bus 15;p after posedge 25;")
      $error("log '%s'", pk::log);
    else $display("PASSED");
  end
endmodule
