`timescale 1ns/1ns
// Local variables flowing through or, and and intersect: variables initialised in their declaration and assigned
// in only one operand, variables assigned in different operands, an or inside an and, intersect with ranges, and
// an assertion of a sequence with an or (fails only when every thread has failed), over an LFSR stimulus
// Expected values: IEEE 1800-2017 16.10 (a local variable with a declaration assignment is assigned at the start
// of each evaluation attempt and flows into both operands; after "or" each thread keeps its own values, so a
// variable assigned in one operand only has its initial value on the threads of the other operand; after
// "and" / "intersect" a variable assigned in exactly one operand has the value from that operand, one assigned
// in neither keeps its value), 16.9.5 (and: both operands match, the composite ends at the later end),
// 16.9.6 (intersect: same start and end), 16.9.7 (or), 16.9.1 (##[m:n]), 16.14.3 (cover property of a
// sequence: one action per attempt, at its first match), 16.12.2 (a sequence property in an assertion fails when
// no thread can match any more), 16.14.1 (fail action), 16.9.3 (sampled values).
//
// Reference model (tick k at time 5 + 10 k; D, V, W, U the values of d, v, w, u sampled at tick k; arithmetic on
// the 4-bit d is done modulo 16 as written in the sequences):
//   f1 int x = 5; ((v ##1 (1, x = d)) or w) ##1 (d == x)
//        w thread: W[t] and D[t+1] == 5 at t+1; v thread: V[t] and D[t+2] == D[t+1] at t+2; first match
//   f2 int x; ((v, x = d) or (w ##1 (1, x = d + 1))) ##1 (d == (x & 15))
//        v thread: V[t] and D[t+1] == D[t] at t+1; w thread: W[t] and D[t+2] == (D[t+1] + 1) & 15 at t+2
//   f3 int x = 1, y, z = 3; ((v, x = d) and (w ##1 (1, y = d))) ##1 (d == ((x + y + z) & 15))
//        V[t], W[t] and D[t+2] == (D[t] + D[t+1] + 3) & 15, at t+2
//   f4 int x = 1; (((v, x = d) or u) and (1 ##1 1)) ##1 (d == x)
//        at t+2: (V[t] and D[t+2] == D[t]) or (U[t] and D[t+2] == 1)
//   f5 int x, y, k = 7; (((v, x = d) ##1 (1, k = d)) intersect (w ##1 (1, y = d))) ##1 (d == (((x ^ y) + k) & 15))
//        V[t], W[t] and D[t+2] == ((D[t] ^ D[t+1]) + D[t+1]) & 15, at t+2
//   f6 int x = 2, y; (((v, x = d) ##[1:2] 1) intersect (w ##[1:2] (1, y = d))) ##1 (d == ((x + y) & 15))
//        V[t], W[t]; for e = t+1 then t+2: D[e+1] == (D[t] + D[e]) & 15 matches at e+1; first match
//   a7 int x = 5; ((v ##1 (1, x = d)) or 1) ##1 (d == x), asserted
//        passes if D[t+1] == 5 or (V[t] and D[t+2] == D[t+1]); otherwise fails at t+1 if !V[t], else at t+2
module svalocalflow1;
  localparam int NT = 300;
  logic clk = 0;
  logic [3:0] d = 0;
  logic v = 0, w = 0, u = 0;
  logic [15:0] lfsr = 16'hACE1;
  int hits [8][NT];
  int errors = 0;

  function automatic void hit(int i);
    int k = $time / 10;
    if (k >= 0 && k < NT) hits[i][k]++;
  endfunction

  always #5 clk = ~clk;
  always @(negedge clk) begin
    lfsr <= {lfsr[14:0], lfsr[15] ^ lfsr[13] ^ lfsr[12] ^ lfsr[10]};
    v <= lfsr[0] | lfsr[3];
    w <= lfsr[1] | lfsr[2];
    u <= lfsr[4];
    d <= lfsr[11:10] == 2'b00 ? 4'd1 : lfsr[9:6];
  end

  int sv[NT], sw[NT], su[NT], sd[NT];
  int ntick = 0;
  always @(posedge clk) begin
    if (ntick < NT) begin sv[ntick] = v; sw[ntick] = w; su[ntick] = u; sd[ntick] = d; end
    ntick++;
  end

  sequence f1; int x = 5; ((v ##1 (1, x = d)) or w) ##1 (d == x); endsequence
  sequence f2; int x; ((v, x = d) or (w ##1 (1, x = d + 1))) ##1 (d == (x & 15)); endsequence
  sequence f3; int x = 1, y, z = 3; ((v, x = d) and (w ##1 (1, y = d))) ##1 (d == ((x + y + z) & 15)); endsequence
  sequence f4; int x = 1; (((v, x = d) or u) and (1 ##1 1)) ##1 (d == x); endsequence
  sequence f5;
    int x, y, k = 7;
    (((v, x = d) ##1 (1, k = d)) intersect (w ##1 (1, y = d))) ##1 (d == (((x ^ y) + k) & 15));
  endsequence
  sequence f6;
    int x = 2, y;
    (((v, x = d) ##[1:2] 1) intersect (w ##[1:2] (1, y = d))) ##1 (d == ((x + y) & 15));
  endsequence
  property a7; int x = 5; ((v ##1 (1, x = d)) or 1) ##1 (d == x); endproperty

  F1: cover property (@(posedge clk) f1) hit(1);
  F2: cover property (@(posedge clk) f2) hit(2);
  F3: cover property (@(posedge clk) f3) hit(3);
  F4: cover property (@(posedge clk) f4) hit(4);
  F5: cover property (@(posedge clk) f5) hit(5);
  F6: cover property (@(posedge clk) f6) hit(6);
  A7: assert property (@(posedge clk) a7) else hit(7);

  // ---------------- reference model ----------------
  int en [8][NT];
  function automatic void exp_hit(int i, int k);
    if (k < NT) en[i][k]++;
  endfunction
  function automatic int D(int k); return k < NT ? sd[k] : -1; endfunction
  task automatic model;
    for (int i = 0; i < 8; i++)
      for (int k = 0; k < NT; k++) en[i][k] = 0;
    for (int t = 0; t < NT; t++) begin
      // f1
      if (sw[t] && D(t + 1) == 5) exp_hit(1, t + 1);
      else if (sv[t] && t + 2 < NT && D(t + 2) == D(t + 1)) exp_hit(1, t + 2);
      // f2
      if (sv[t] && D(t + 1) == D(t)) exp_hit(2, t + 1);
      else if (sw[t] && t + 2 < NT && D(t + 2) == ((D(t + 1) + 1) & 15)) exp_hit(2, t + 2);
      // f3
      if (sv[t] && sw[t] && t + 2 < NT && D(t + 2) == ((D(t) + D(t + 1) + 3) & 15)) exp_hit(3, t + 2);
      // f4
      if (t + 2 < NT && ((sv[t] && D(t + 2) == D(t)) || (su[t] && D(t + 2) == 1))) exp_hit(4, t + 2);
      // f5
      if (sv[t] && sw[t] && t + 2 < NT && D(t + 2) == (((D(t) ^ D(t + 1)) + D(t + 1)) & 15)) exp_hit(5, t + 2);
      // f6
      if (sv[t] && sw[t]) begin
        if (t + 2 < NT && D(t + 2) == ((D(t) + D(t + 1)) & 15)) exp_hit(6, t + 2);
        else if (t + 3 < NT && D(t + 3) == ((D(t) + D(t + 2)) & 15)) exp_hit(6, t + 3);
      end
      // a7
      if (t + 1 < NT && D(t + 1) != 5) begin
        if (!sv[t]) exp_hit(7, t + 1);
        else if (t + 2 < NT && D(t + 2) != D(t + 1)) exp_hit(7, t + 2);
      end
    end
  endtask

  initial begin
    #(10 * NT);
    model;
    if (ntick < NT) begin $error("only %0d ticks sampled", ntick); errors++; end
    for (int i = 1; i < 8; i++) begin
      automatic int ne = 0;
      for (int k = 0; k < NT; k++) begin
        ne += en[i][k];
        if (hits[i][k] != en[i][k]) begin
          $error("id %0d tick %0d: %0d actions, expected %0d", i, k, hits[i][k], en[i][k]); errors++;
        end
      end
      if (ne == 0) begin $error("id %0d: stimulus never exercises it", i); errors++; end
    end
    if (errors == 0) $display("PASSED");
    $finish;
  end
endmodule
