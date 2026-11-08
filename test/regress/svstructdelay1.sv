// A continuous assignment with a delay to a member of an unpacked structure
// Expected values: IEEE 1800-2017 10.3.3 (delay on a continuous assignment: the
// new value is assigned that long after the right-hand side changed; a change
// during the delay replaces the pending update, so the earlier one is never seen),
// 22.7 (`timescale; fractional delays), 11.4.3. Samples are taken between the
// update instants, so no same-time ordering is relied on, and nothing is sampled
// before the first update matured.
`timescale 1ns/1ps
typedef struct { logic [7:0] x; logic [7:0] y; } d_t;
module svstructdelay1;
  d_t s;
  reg [7:0] a = 1;
  int errs;
  task automatic ck(string tag, logic [7:0] ex, logic [7:0] ey);
    if (s.x !== ex || s.y !== ey) begin
      $error("%s at %t: s.x=%0d s.y=%0d expected %0d %0d", tag, $realtime, s.x, s.y, ex, ey); errs++;
    end
  endtask
  assign #2.5 s.x = a + 1;
  assign #(1) s.y = a;
  initial begin
    // s.y = 1 from t=1, s.x = 2 from t=2.5
    #1.5 if (s.y !== 8'd1) begin $error("y at 1.5: %0d", s.y); errs++; end
    #1.2 ck("t=2.7", 2, 1);
    #0.3 a = 5;                  // t=3: y -> 5 at 4, x -> 6 at 5.5
    #0.5 ck("t=3.5", 2, 1);
    #1.0 ck("t=4.5", 2, 5);
    #0.5 a = 7;                  // t=5: y -> 7 at 6, x -> 8 at 7.5; the pending x=6 is replaced
    #0.7 ck("t=5.7", 2, 5);      // x=6 would have appeared at 5.5 without replacement
    #0.8 ck("t=6.5", 2, 7);
    #1.2 ck("t=7.7", 8, 7);
    if (errs == 0) $display("PASSED");
  end
endmodule
