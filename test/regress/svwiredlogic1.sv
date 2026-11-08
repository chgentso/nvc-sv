// The wand, wor, triand and trior nets resolve conflicting drivers
// Expected values: IEEE 1800-2017 6.6.2 (wired nets: wand/triand resolve to the AND of the
// driver values, wor/trior to the OR; a z driver does not contribute), 6.6 (a net with only z
// drivers is z) (Table 6-2).
`timescale 1ns/1ns
module svwiredlogic1;
  wand w1; wor w2; triand t1; trior t2; wand w3; wor w4; wand w5;
  reg a = 1, b = 0, z = 1'bz, x = 1'bx;
  int errors = 0;
  assign w1 = a; assign w1 = b;
  assign w2 = a; assign w2 = b;
  assign t1 = a; assign t1 = b;
  assign t2 = a; assign t2 = b;
  assign w3 = a; assign w3 = z;
  assign w4 = b; assign w4 = z;
  assign w5 = z;
  initial begin
    #1;
    if ({w1, w2, t1, t2, w3, w4, w5} !== 7'b0101_10z) begin
      $error("t=1: %b %b %b %b %b %b %b", w1, w2, t1, t2, w3, w4, w5); errors++;
    end
    a = 0; b = 1;
    #1;
    if ({w1, w2, t1, t2} !== 4'b0101) begin $error("t=2: %b %b %b %b", w1, w2, t1, t2); errors++; end
    a = 1; b = 1;
    #1;
    if ({w1, w2, t1, t2} !== 4'b1111) begin $error("t=3: %b %b %b %b", w1, w2, t1, t2); errors++; end
    if (errors == 0) $display("PASSED");
  end
endmodule
