// Delays in net declarations apply to every driver of the net
// Expected values: IEEE 1800-2017 6.7.1 (net delays: rise 2.4 / fall 1.3 for w, 1.5 for vw and u; a change of
// the net value takes effect after the delay of the transition), 10.3.3 (delay of a continuous assignment, 0.5 for u,
// added to the net delay). All samples are taken away from the instants at which a net changes and after every net
// has taken its first driven value (a net driven only by a delayed assignment is not sampled before then).
`timescale 1ns/1ps
module svnetdelay1;
  reg a = 0; reg [3:0] v = 4'h3;
  wire #(2.4, 1.3) w; assign w = a;
  wire [3:0] #1.5 vw = v;
  wire #1.5 u; assign #0.5 u = a;
  int fails = 0;
  task automatic chk(input string at, input logic ew, input logic [3:0] evw, input logic eu);
    if (w !== ew || vw !== evw || u !== eu) begin
      $error("at %s: w=%b vw=%b u=%b, expected %b %b %b", at, w, vw, u, ew, evw, eu); fails++;
    end
  endtask
  initial begin
    #3.1 chk("3.1", 1'b0, 4'h3, 1'b0);
    // a rises and v changes at 3.1: vw at 4.6, w at 5.5 (rise 2.4), u at 3.1+0.5+1.5 = 5.1
    a = 1; v = 4'hc; #1 chk("4.1", 1'b0, 4'h3, 1'b0);
    #1.05 chk("5.15", 1'b0, 4'hc, 1'b1);
    #0.95 chk("6.1", 1'b1, 4'hc, 1'b1);
    // a falls at 6.1: w at 7.4 (fall 1.3), u at 6.1+0.5+1.5 = 8.1
    a = 0; #1.2 chk("7.3", 1'b1, 4'hc, 1'b1);
    #0.2 chk("7.5", 1'b0, 4'hc, 1'b1);
    #1 chk("8.5", 1'b0, 4'hc, 1'b0);
    if (fails == 0) $display("PASSED");
  end
endmodule
