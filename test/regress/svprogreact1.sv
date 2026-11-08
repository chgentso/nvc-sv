// Programs reacting to clock edges seen through a module, an interface and a continuous assign.
// Events are logged and compared as a set (no order between processes woken in the same time step
// is assumed); the time-0 x->0 transition of the net clkw is not checked.
// Expected values: IEEE 1800-2017 9.4.2 (edge events), 24.3 (program initial blocks run in the
// Reactive region), 24.7 (a program ends when its initial procedures finish; its descendant threads
// are then terminated), 4.4/4.7 (event regions)
`timescale 1ns/1ns
package pk;
  string ev[$];
  function void rec(string s);
    ev.push_back(s);
  endfunction
  function int cnt(string s);
    int n;
    n = 0;
    foreach (ev[i]) if (ev[i] == s) n++;
    return n;
  endfunction
endpackage
interface ifc(input logic clk);
  logic [3:0] d;
endinterface
module mon(ifc b);
  always @(posedge b.clk) pk::rec($sformatf("%0d mon iface", $time));
  always @(negedge b.clk) pk::rec($sformatf("%0d mon neg", $time));
endmodule
program pa(input logic clk);
  initial begin
    #2;
    repeat (3) begin @(posedge clk); pk::rec($sformatf("%0d A", $time)); end
  end
endprogram
program pb(input logic clk);
  initial begin
    #1;
    repeat (3) begin @(posedge clk); pk::rec($sformatf("%0d B", $time)); #2; end
  end
endprogram
program pc(ifc b);
  initial begin
    #1;
    repeat (3) begin
      @(negedge b.clk);
      pk::rec($sformatf("%0d C", $time));
      fork #1 pk::rec($sformatf("%0d C child", $time)); join_none
    end
  end
endprogram
module svprogreact1;
  bit clk;
  int errs = 0, total = 0;
  string exp[$] = '{"5 mon iface", "15 mon iface", "25 mon iface",
                    "10 mon neg", "20 mon neg", "30 mon neg",
                    "5 A", "15 A", "25 A", "5 B", "15 B", "25 B",
                    "10 C", "20 C", "30 C", "11 C child", "21 C child"};
  always #5 clk = ~clk;
  wire clkw = clk;
  ifc b(clkw);
  mon m(b);
  pa a(clk);
  pb p(clk);
  pc c(b);
  initial #27;
  final begin
    foreach (exp[i])
      if (pk::cnt(exp[i]) != 1) begin errs++; $error("event '%s' seen %0d times", exp[i], pk::cnt(exp[i])); end
    foreach (pk::ev[i])
      if (pk::ev[i].substr(0, 1) != "0 ") total++;
    if (total != exp.size()) begin errs++; $error("%0d events, expected %0d", total, exp.size()); end
    // pc ends at 30: its child forked at 30 is terminated (24.7)
    if (pk::cnt("31 C child") != 0) begin errs++; $error("child outlived program"); end
    if (errs == 0) $display("PASSED");
  end
endmodule
