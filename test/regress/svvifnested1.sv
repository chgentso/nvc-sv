// Virtual interface handle to an interface that contains nested interface instances.
// Expected values: IEEE 1800-2017 25.9 (virtual interface members, tasks and functions),
// 25.3 (interface instances inside interfaces), 9.4.2 (posedge wait), 13.4.
`timescale 1ns/1ns
interface clkrst(input logic clk, input logic rst_n);
  int freq = 0;
  logic [3:0] cnt = 0;
  task automatic set_freq(int f);
    freq = f;
    @(posedge clk);
    cnt = cnt + 1;
  endtask
  function automatic int get(); return freq; endfunction
endinterface
interface inner(input logic clk);
  logic x = 0;
  clkrst deep(.clk(clk), .rst_n(x));
endinterface
interface outer(input logic clk);
  logic rst_n = 1;
  clkrst sub(.clk(clk), .rst_n(rst_n));
  inner in(.clk(clk));
endinterface
int errors = 0;
`define CHK(got, exp, what) if ((got) !== (exp)) begin $error("%s: got %0d exp %0d", what, int'(got), int'(exp)); errors++; end
class drv;
  virtual outer vif;
  task run(int base);
    vif.sub.set_freq(base);
    vif.in.deep.set_freq(base + 100);
    `CHK(vif.sub.freq, base, "sub.freq")
    `CHK(vif.sub.get(), base, "sub.get")
    `CHK(vif.in.deep.get(), base + 100, "deep.get")
    `CHK(vif.sub.rst_n, 1'b1, "rst_n")
    `CHK(vif.sub.cnt, 4'd1, "sub.cnt")
    `CHK(vif.in.deep.cnt, 4'd1, "deep.cnt")
    vif.sub.freq = 55;
    `CHK(vif.sub.get(), 55, "get after write")
  endtask
endclass
module svvifnested1;
  logic clk = 0;
  outer o1(clk), o2(clk);
  drv d1 = new, d2 = new;
  always #5 clk = ~clk;
  initial begin
    d1.vif = o1; d2.vif = o2;
    d1.run(7); d2.run(9);
    `CHK(o1.sub.freq, 55, "o1.sub.freq")
    `CHK(o2.sub.freq, 55, "o2.sub.freq")
    `CHK(o1.in.deep.freq, 107, "o1.in.deep.freq")
    `CHK(o2.in.deep.freq, 109, "o2.in.deep.freq")
    if (errors == 0) $display("PASSED");
    $finish;
  end
endmodule
