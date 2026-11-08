// Expected values: IEEE 1800-2017 14.3-14.4 (clocking block: input clockvar sampled in the
// Preponed region of each clocking event with default #1step skew; the clocking event @(cb)),
// 9.4.2 (edge events: posedge is any 0->1, 0->x, x->1 transition), 25.9 (virtual interface).
// sig is 1 during [3,13), 0 during [13,23), 1 during [23,33), 0 during [33,43), 1 from 43.
// Clock edges at 5, 15, 25, ... so cb.sig takes the values x,1 (5), 0 (15), 1 (25), 0 (35), 1 (45), 1 (55).
// Whether a process woken by a clockvar edge at an edge time sees the clocking event of the same
// time is not fixed (4.7), so every wait is entered 1 time unit after the previous wake-up.
`timescale 1ns/1ns
interface bus_if(input logic clk);
  logic sig, other;
  clocking cb @(posedge clk);
    input sig;
    output other;
  endclocking
endinterface
module svcbwake1;
  logic clk = 0;
  bus_if vif(clk);
  virtual bus_if v;
  int errors = 0;
  time t1, t2, t3, t4, t5;
  initial repeat (12) #5 clk = ~clk;
  initial begin
    #3 vif.sig = 1;
    #10 vif.sig = 0;
    #10 vif.sig = 1;
    #10 vif.sig = 0;
    #10 vif.sig = 1;
  end
  initial begin
    v = vif;
    @(posedge v.cb.sig);     // clockvar goes x -> 1 at the first clocking event (5)
    t1 = $time;
    #1 @(v.cb);              // next clocking event: 15
    t2 = $time;
    #1 @(v.cb);              // 25
    t3 = $time;
    #1 @(posedge v.cb.sig);  // 0 -> 1 only at 45 (35 samples 0)
    t4 = $time;
    #1 @(v.cb);              // 55
    t5 = $time;
    if (t1 !== 5)  begin $error("posedge cb.sig at %0t, expected 5", t1); errors++; end
    if (t2 !== 15) begin $error("@(cb) at %0t, expected 15", t2); errors++; end
    if (t3 !== 25) begin $error("second @(cb) at %0t, expected 25", t3); errors++; end
    if (t4 !== 45) begin $error("second posedge cb.sig at %0t, expected 45", t4); errors++; end
    if (t5 !== 55) begin $error("final @(cb) at %0t, expected 55", t5); errors++; end
    if (errors == 0) $display("PASSED");
  end
endmodule
