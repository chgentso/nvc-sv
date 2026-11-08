// Expected values: IEEE 1800-2017 14.3-14.4 (an input clockvar holds the value sampled in the
// Preponed region of the clocking event, default skew #1step), 8.5 / 8.18 (class property
// names are looked up in the class scope: members named cb and sig do not clash with the
// module-level clocking block cb and variable sig).
// sig = 1 from time 0; the clock edge is at 5.  The clockvar is read 1 time unit after the event
// so the check does not depend on the order of same-time activities (4.7).
`timescale 1ns/1ns
class C;
  int cb = 3;
  int sig = 4;
endclass

module svclk5;
  logic clk = 0;
  logic sig;
  int errors = 0;
  initial repeat (6) #5 clk = ~clk;
  clocking cb @(posedge clk);
    input sig;
  endclocking
  C o;
  initial begin
    o = new;
    o.cb = o.cb + 1;
    if (o.cb !== 4) begin $error("o.cb = %0d, expected 4", o.cb); errors++; end
    if (o.sig !== 4) begin $error("o.sig = %0d, expected 4", o.sig); errors++; end
    sig = 1;
    @(cb);
    if ($time != 5) begin $error("@(cb) at %0t, expected 5", $time); errors++; end
    #1;
    if (cb.sig !== 1'b1) begin $error("cb.sig = %b, expected 1", cb.sig); errors++; end
    if (errors == 0) $display("PASSED");
  end
endmodule
