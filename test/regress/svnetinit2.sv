// Value-change events seen at time zero on nets (continuous assignments,
// ports, gate outputs, drive strengths) and on variables with initialisers.
// Expected values: IEEE 1800-2017 6.8 (a variable initialiser is applied
// before any process starts and generates no event: n_a = n_b = 0), 6.6
// (a net that stays at its initial value z, or an undriven net, has no event:
// n_wz = n_wu = 0), 4.9 / 6.5 (continuous assignment and net updates happen in
// the active region, so a change from the initial z to a driven value is an
// event, but it races with the start of the always blocks at time zero: the
// count is 0 or 1, never more), final values.  After the first time unit
// every net has the value computed by hand below.
module svnetinit2_sub(output o, input i, output o2);
  assign o = 1'b1;
  assign o2 = ~i;
endmodule

module svnetinit2;
  reg a = 1;
  reg [2:0] b = 3'b000;
  reg r0 = 0;
  reg rx;
  reg r1 = 1;
  reg c;
  wire w_c = 1'b1;
  wire w_r0, w_rx, w_r1, w_n, w_g, so, so2, w_z, w_x, w_u;
  wire [3:0] vec;
  assign w_r0 = r0;
  assign w_rx = rx;
  assign w_r1 = r1;
  assign w_n = ~r0;
  and g1(w_g, r1, r1);
  svnetinit2_sub s(so, r0, so2);
  assign vec = {r1, r0, r1, r0};
  assign w_z = 1'bz;
  assign w_x = 1'bx;
  triand (weak1, strong0) w_t = r1;
  wire w_p = 1'b1;
  integer n_a, n_b, n_c, n_wc, n_wr0, n_wrx, n_wr1, n_wn, n_wg, n_so, n_so2;
  integer n_vec, n_wz, n_wx, n_wu, n_wt, n_pos;
  initial begin
    n_a = 0; n_b = 0; n_c = 0; n_wc = 0; n_wr0 = 0; n_wrx = 0; n_wr1 = 0;
    n_wn = 0; n_wg = 0; n_so = 0; n_so2 = 0; n_vec = 0; n_wz = 0; n_wx = 0;
    n_wu = 0; n_wt = 0; n_pos = 0;
  end
  always @(a) n_a = n_a + 1;
  always @(b) n_b = n_b + 1;
  always @(c) n_c = n_c + 1;
  always @(w_c) n_wc = n_wc + 1;
  always @(w_r0) n_wr0 = n_wr0 + 1;
  always @(w_rx) n_wrx = n_wrx + 1;
  always @(w_r1) n_wr1 = n_wr1 + 1;
  always @(w_n) n_wn = n_wn + 1;
  always @(w_g) n_wg = n_wg + 1;
  always @(so) n_so = n_so + 1;
  always @(so2) n_so2 = n_so2 + 1;
  always @(vec) n_vec = n_vec + 1;
  always @(w_z) n_wz = n_wz + 1;
  always @(w_x) n_wx = n_wx + 1;
  always @(w_u) n_wu = n_wu + 1;
  always @(w_t) n_wt = n_wt + 1;
  always @(posedge w_p) n_pos = n_pos + 1;
  initial c = 0;
  integer errs = 0;
  task automatic range(string name, integer got, integer lo, integer hi);
    if (got < lo || got > hi) begin
      $error("%s: %0d events, expected %0d..%0d", name, got, lo, hi); errs = errs + 1;
    end
  endtask
  initial begin
    #1;
    // variables with an initialiser: no event
    range("a", n_a, 0, 0);
    range("b", n_b, 0, 0);
    // x -> 0 by an initial procedure at time zero: may race with the always block
    range("c", n_c, 0, 1);
    // nets driven at time zero: one change from z, racing with the always block
    range("w_c", n_wc, 0, 1);
    range("w_r0", n_wr0, 0, 1);
    range("w_rx", n_wrx, 0, 1);
    range("w_r1", n_wr1, 0, 1);
    range("w_n", n_wn, 0, 1);
    range("w_g", n_wg, 0, 1);
    range("so", n_so, 0, 1);
    range("so2", n_so2, 0, 1);
    range("vec", n_vec, 0, 1);
    range("w_x", n_wx, 0, 1);
    range("w_t", n_wt, 0, 1);
    range("pos", n_pos, 0, 1);
    // nets that stay at z: no value change
    range("w_z", n_wz, 0, 0);
    range("w_u", n_wu, 0, 0);
    // final values
    if (w_c !== 1'b1 || w_r0 !== 1'b0 || w_rx !== 1'bx || w_r1 !== 1'b1 ||
        w_n !== 1'b1 || w_g !== 1'b1 || so !== 1'b1 || so2 !== 1'b1 ||
        vec !== 4'b1010 || w_z !== 1'bz || w_x !== 1'bx || w_u !== 1'bz ||
        w_t !== 1'b1 || w_p !== 1'b1) begin
      $error("final net values: c=%b r0=%b rx=%b r1=%b n=%b g=%b so=%b so2=%b vec=%b z=%b x=%b u=%b t=%b p=%b",
             w_c, w_r0, w_rx, w_r1, w_n, w_g, so, so2, vec, w_z, w_x, w_u, w_t, w_p);
      errs = errs + 1;
    end
    if (errs == 0) $display("PASSED");
  end
endmodule
