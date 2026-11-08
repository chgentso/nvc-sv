// Always blocks in nested module instances all wake up on the same clock edge (order not checked)
// Expected values: IEEE 1800-2017 9.4.2 (event control), 23.3 (module instances), 4.7 (the order
// of processes activated by one event is undefined: the set of wake-ups is compared).
`timescale 1ns/1ns
string log[$];
function automatic void rec(string s); log.push_back($sformatf("%0d %s", $time, s)); endfunction
module mon(input logic clk, input int id);
  always @(posedge clk) rec($sformatf("mon%0d", id));
endmodule
module wrap(input logic clk);
  always @(posedge clk) rec("wrap-a");
  mon m0(clk, 0);
  always @(posedge clk) rec("wrap-b");
  mon m1(clk, 1);
  always @(posedge clk) rec("wrap-c");
endmodule
module svwakeinst1;
  logic clk = 0;
  int errors = 0;
  always @(posedge clk) rec("top-a");
  wrap w(clk);
  always @(posedge clk) rec("top-b");
  mon t0(clk, 9);
  initial begin #5 clk = 1; end
  initial begin
    automatic string exp[$] = '{"top-a", "top-b", "wrap-a", "wrap-b", "wrap-c", "mon0", "mon1", "mon9"};
    #10;
    if (log.size() != exp.size()) begin $error("%0d wake-ups, exp %0d", log.size(), exp.size()); errors++; end
    foreach (exp[i]) begin
      automatic int n = 0;
      automatic string k = $sformatf("5 %s", exp[i]);
      foreach (log[j]) if (log[j] == k) n++;
      if (n != 1) begin $error("'%s' seen %0d times", k, n); errors++; end
    end
    if (errors == 0) $display("PASSED");
  end
endmodule
