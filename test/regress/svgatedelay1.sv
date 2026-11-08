// Expected values: IEEE 1800-2017 28.16 (gate delays: rise/fall/turn-off selection, a single delay
// applies to every transition), 10.3.3 (continuous assignment delay), 22.7 (`timescale rounding: delays
// are in ns with 1 ps precision).
// Outputs {y1,y2,y3,y4,y5,y6,y7,w} are sampled at times strictly between the scheduled changes.
`timescale 1ns/1ps
module svgatedelay1;
  reg a = 0, b = 1;
  wire w; wire y1, y2, y3, y4, y5, y6, y7;
  and #1.5 g1(y1, a, b);
  or #(1.2, 2.3) g2(y2, a, b);
  buf #(1, 2) g3(y3, a);
  bufif1 #(1, 2, 3) g4(y4, a, b);
  not #0.4 g5(y5, a);
  nand #(0.3) g6(y6, a, b), g7(y7, b, a);
  assign (strong0, strong1) #2 w = a;
  int bad = 0;
  task automatic chk(real t, logic [7:0] exp);
    logic [7:0] got;
    #(t - $realtime);
    got = {y1, y2, y3, y4, y5, y6, y7, w};
    if (got !== exp) begin $error("t=%0f: {y1..y7,w}=%b, want %b", t, got, exp); bad++; end
  endtask
  initial begin
    #10 a = 1;
    #10 a = 0;
    #10 b = 0; a = 1;
    #10 a = 0; b = 1;
  end
  initial begin
    // a=0 b=1: and=0 or=1 buf=0 bufif1=0 not=1 nand=1 w=0
    chk(5,    8'b0_1_0_0_1_1_1_0);
    chk(10.2, 8'b0_1_0_0_1_1_1_0);  // a rose at 10: nothing has switched yet
    chk(10.35, 8'b0_1_0_0_1_0_0_0); // nand after 0.3
    chk(10.5, 8'b0_1_0_0_0_0_0_0);  // not after 0.4
    chk(11.2, 8'b0_1_1_1_0_0_0_0);  // buf, bufif1 rise 1
    chk(11.7, 8'b1_1_1_1_0_0_0_0);  // and 1.5
    chk(12.5, 8'b1_1_1_1_0_0_0_1);  // w after 2
    // a falls at 20
    chk(20.35, 8'b1_1_1_1_0_1_1_1);
    chk(20.5, 8'b1_1_1_1_1_1_1_1);
    chk(21.7, 8'b0_1_1_1_1_1_1_1);  // and after 1.5
    chk(22.5, 8'b0_1_0_0_1_1_1_0);  // buf, bufif1 fall 2, w after 2
    // b=0, a=1 at 30: bufif1 disabled, turn-off delay 3
    chk(30.5, 8'b0_1_0_0_0_1_1_0);
    chk(31.5, 8'b0_1_1_0_0_1_1_0);  // buf rise 1
    chk(32.5, 8'b0_1_1_0_0_1_1_1);  // w
    chk(33.5, 8'b0_1_1_z_0_1_1_1);  // bufif1 output z at 33
    // a=0, b=1 at 40
    chk(40.5, 8'b0_1_1_z_1_1_1_1);
    chk(42.5, 8'b0_1_0_0_1_1_1_0);  // z -> 0 uses the fall delay 2
    if (bad == 0) $display("PASSED"); else $display("FAILED");
  end
endmodule
