`timescale 1ns/1ns
// Local variables with match items that call tasks and functions: a task called
// from the match list of a sequence element ((v, x = d, hit(0, x))), calls in
// the consequent of an implication and in a repetition with a local counter, first_match
// with an assignment, a task called from a bare sequence property, and the
// action blocks of the assertions, driven by an LFSR stimulus.
// Expected values: IEEE 1800-2017 16.8.1 / 16.10 (local variables), 16.11 (a subroutine
// attached to a sequence element is called, in the Reactive region, each time the
// element matches on a thread, with the local variable values of that thread), 16.9.8
// (first_match: the later matches of its operand are discarded, so their match items do
// not run), 16.9.2 ([*]), 16.14.1 (a property that is a sequence fails if the sequence does
// not match; fail action), 16.14.3 (cover property of a sequence: one action per
// attempt, at its first match; no vacuous attempts for a sequence), 16.12.7 (|->).
//
// Reference model: v, w, d, q sampled at each of the 100 posedges (tick k at time 5 + 10 k)
// are recorded and the call counts n and the sums of $time + 1000 * x per id are computed
// from them:
//   hit 0, 1, 2   c1: v at t (x = d); then w at t+1; then d[0] at t+2
//   hit 3, 4, 9   c2: antecedent v at t calls hit 3; at t+2 q[0] == x[0] calls hit 4,
//                 otherwise the assertion fails and calls hit 9 (x = 0)
//   hit 5         c3: (w, c++, hit(5, c)) repetitions after v at t: repetition r (1..3) at t+r
//                 when w holds at t+1 .. t+r, with c = r (the repetition r = 3 is only
//                 reached when w holds at t+3, in which case the attempt has not matched
//                 before)
//   hit 6, 7      c4: first match of v ##[1:3] (w, n = d): first j with w at t+j calls hit 6
//                 (x = d at t+j); then d[0] == n[0] at t+j+1 calls hit 7
//   hit 8, 10     C5: (v, note(8)) ##1 w as a property: v at t calls hit 8; the attempt fails
//                 at t if !v and at t+1 if v and !w, calling hit 10
// All the checked calls are fail or match-item calls, so the result does not depend on
// whether pass actions run for vacuous successes.
module svalocal2;
  localparam int NT = 100;
  logic clk = 0;
  logic [7:0] d = 0, q = 0;
  logic v = 0, w = 0;
  logic [15:0] lfsr = 16'hACE1;
  logic [7:0] p1 = 0, p2 = 0, p3 = 0;
  int hits [0:63];
  longint tsum [0:63];
  int errors = 0;
  task hit(int i, int x);
    hits[i]++;
    tsum[i] += $time + 1000 * x;
  endtask
  always #5 clk = ~clk;
  always @(negedge clk) begin
    lfsr <= {lfsr[14:0], lfsr[15] ^ lfsr[13] ^ lfsr[12] ^ lfsr[10]};
    v <= lfsr[0];
    w <= lfsr[1] | lfsr[2];
    d <= lfsr[10:3];
  end
  always @(posedge clk) begin
    p1 <= d; p2 <= p1; p3 <= p2;
    q <= p2 ^ {7'b0, lfsr[5] & lfsr[6] & lfsr[7]};
  end
  // samples of v, w, d, q at each tick
  int sv[NT], sw[NT], sd[NT], sq[NT];
  int ntick = 0;
  always @(posedge clk) begin
    if (ntick < NT) begin sv[ntick] = v; sw[ntick] = w; sd[ntick] = d; sq[ntick] = q; end
    ntick++;
  end

  sequence c1;
    int x;
    (v, x = d, hit(0, x)) ##1 (w, hit(1, x)) ##1 (d[0] == 1, hit(2, x));
  endsequence
  C1: cover property (@(posedge clk) c1);

  property c2;
    int x;
    (v, x = d, hit(3, x)) |-> ##2 (q[0] == x[0], hit(4, x));
  endproperty
  C2: assert property (@(posedge clk) c2) else hit(9, 0);

  sequence c3;
    int c;
    (v, c = 0) ##1 (w, c++, hit(5, c))[*2:3] ##1 !w;
  endsequence
  C3: cover property (@(posedge clk) c3);

  sequence c4;
    int n;
    first_match(v ##[1:3] (w, n = d, hit(6, n))) ##1 (d[0] == n[0], hit(7, n));
  endsequence
  C4: cover property (@(posedge clk) c4);

  task note(int i); hit(i, 0); endtask
  C5: assert property (@(posedge clk) (v, note(8)) ##1 w) else hit(10, 0);

  // ---------------- reference model ----------------
  int en[0:63];
  longint et[0:63];
  function automatic int V(int k); return (k >= 0 && k < NT) ? sv[k] : 0; endfunction
  function automatic int W(int k); return (k >= 0 && k < NT) ? sw[k] : 0; endfunction
  function automatic int D(int k); return (k >= 0 && k < NT) ? sd[k] : 0; endfunction
  function automatic int Q(int k); return (k >= 0 && k < NT) ? sq[k] : 0; endfunction
  function automatic void exp_hit(int i, int tick, int x);   // a call at the tick (inside the trace)
    if (tick < NT) begin en[i]++; et[i] += 5 + 10 * tick + 1000 * x; end
  endfunction
  task automatic model;
    int jj;
    for (int i = 0; i < 64; i++) begin en[i] = 0; et[i] = 0; end
    for (int t = 0; t < NT; t++) begin
      // c1
      if (V(t)) begin
        exp_hit(0, t, D(t));
        if (t + 1 < NT && W(t + 1)) begin
          exp_hit(1, t + 1, D(t));
          if (t + 2 < NT && (D(t + 2) & 1)) exp_hit(2, t + 2, D(t));
        end
      end
      // c2
      if (V(t)) begin
        exp_hit(3, t, D(t));
        if (t + 2 < NT) begin
          if ((Q(t + 2) & 1) == (D(t) & 1)) exp_hit(4, t + 2, D(t));
          else exp_hit(9, t + 2, 0);
        end
      end
      // c3
      if (V(t)) begin
        if (W(t + 1)) begin
          exp_hit(5, t + 1, 1);
          if (W(t + 2)) begin
            exp_hit(5, t + 2, 2);
            if (W(t + 3)) exp_hit(5, t + 3, 3);
          end
        end
      end
      // c4
      if (V(t)) begin
        jj = 0;
        for (int j = 1; j <= 3; j++) if (jj == 0 && t + j < NT && W(t + j)) jj = j;
        if (jj != 0) begin
          exp_hit(6, t + jj, D(t + jj));
          if (t + jj + 1 < NT && (D(t + jj + 1) & 1) == (D(t + jj) & 1)) exp_hit(7, t + jj + 1, D(t + jj));
        end
      end
      // C5
      if (V(t)) exp_hit(8, t, 0);
      if (!V(t)) exp_hit(10, t, 0);
      else if (t + 1 < NT && !W(t + 1)) exp_hit(10, t + 1, 0);
    end
  endtask

  initial begin
    #1000;
    model;
    if (ntick < NT) begin $error("only %0d ticks sampled", ntick); errors++; end
    for (int i = 0; i < 64; i++)
      if (hits[i] != en[i] || tsum[i] != et[i]) begin
        $error("hit(%0d): n = %0d, time sum = %0d, expected n = %0d, time sum = %0d", i, hits[i], tsum[i], en[i], et[i]); errors++;
      end
    if (errors == 0) $display("PASSED");
    $finish;
  end
endmodule
