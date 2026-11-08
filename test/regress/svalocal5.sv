// Local variables with initial values in sequences and properties, local
// input formal arguments (value captured when the instance starts) versus
// ordinary formal arguments (substituted and sampled where used), increments
// in repetitions, assignments to local input formals, composition of
// sequence instances with ##1 and ##0, with covers and assertions on a
// pseudo-random stimulus.
// Expected values: IEEE 1800-2017 16.8 (declaring sequences: local variables,
// initialisation at the start of each instance, assignment at the matched
// item, 16.8.2 local input formals, 16.8.? ordinary typed formals), 16.9.1
// (##n, ##0 fusion), 16.9.2 ([*]), 16.12 (|=>, and), 16.14.3 (cover property
// of a sequence: one action per attempt at its first match), 11.8, 16.9.3.
// Each cover/assertion has the closed-form match condition given next to its
// check below (for 100 sampled ticks, tick k at time 5 + 10 k); the
// byte values in the sequences wrap (10.7), so the closed forms use & 255.
// Covers: exact per-tick counts of the attempts that match.  Assertion P9:
// fails exactly where the consequent mismatches; P10 (p9(1) and p9(2), which
// can never hold together) fails exactly once at every attempt whose antecedent
// matched; passes are bounded by the attempts that can pass.  Event ticks are
// taken as time / 10.
module svalocal5;
  logic clk = 0;
  logic [7:0] d = 0;
  logic v = 0, w = 0;
  logic [15:0] lfsr = 16'hACE1;
  localparam int NT = 100;
  int hits [32][NT];
  int errors = 0;
  task automatic hit(int i);
    int k = $time / 10;
    if (k >= 0 && k < NT) hits[i][k]++;
  endtask
  int sv[NT], sw[NT], sd[NT];
  int ntick = 0;
  always @(posedge clk) begin
    if (ntick < NT) begin sv[ntick] = v; sw[ntick] = w; sd[ntick] = d; end
    ntick++;
  end
  always #5 clk = ~clk;
  always @(negedge clk) begin
    lfsr <= {lfsr[14:0], lfsr[15] ^ lfsr[13] ^ lfsr[12] ^ lfsr[10]};
    v <= lfsr[0]; w <= lfsr[1] | lfsr[2]; d <= lfsr[10:3];
  end
  sequence s0;
    int n = 5;
    (v, n = n + 1) ##1 (w, n = n + 1) ##1 (d[2:0] == n[2:0]);
  endsequence
  P0: cover property (@(posedge clk) s0) hit(0);
  P1: cover property (@(posedge clk) s0 ##1 s0) hit(1);
  sequence s2;
    int c = 0;
    (w, c++)[*1:3] ##1 (c == 2);
  endsequence
  P2: cover property (@(posedge clk) s2) hit(2);
  P3: cover property (@(posedge clk) v ##1 s2 ##1 s2) hit(3);
  sequence s4(local input byte x, y);
    byte a = 3;
    (v, a = a + x) ##1 (d[1:0] == (a[1:0] ^ (y & 3)));
  endsequence
  P4: cover property (@(posedge clk) s4(d, 1)) hit(4);
  P5: cover property (@(posedge clk) s4(d, 2) ##1 s4(d + 1, 3)) hit(5);
  sequence s6(local input byte x);
    ##2 (d == x);
  endsequence
  P6: cover property (@(posedge clk) v ##0 s6(d)) hit(6);
  P7: cover property (@(posedge clk) w ##0 s6(d + 1)) hit(7);
  sequence s8(byte x);
    ##2 (d == x);
  endsequence
  P8: cover property (@(posedge clk) v ##0 s8(d)) hit(8);
  property p9(local input byte x);
    byte t = 7;
    (v, t = t + x) |=> (d[2:0] == t[2:0]);
  endproperty
  P9: assert property (@(posedge clk) p9(d)) hit(9); else hit(10);
  P10: assert property (@(posedge clk) p9(1) and p9(2)) else hit(12);
  sequence s11(local input byte x);
    byte a = 1;
    (w, a = a + x) ##1 (d[1:0] == a[1:0]);
  endsequence
  sequence s12(local input byte y);
    s11(y) ##1 s11(y + 1);
  endsequence
  P11: cover property (@(posedge clk) v ##1 s12(d)) hit(13);
  P12: cover property (@(posedge clk) s12(1) ##1 s12(2)) hit(14);
  sequence s13(local input byte x);
    byte a = 2;
    (v, a = a + 1, x = x + a) ##1 (d[1:0] == x[1:0]);
  endsequence
  P13: cover property (@(posedge clk) s13(d) ##1 s13(d)) hit(15);
  initial begin
    #1000;
    check_all;
    $finish;
  end

  // ---------------- reference model ----------------
  function automatic int low2(int x); return x & 3; endfunction
  // match end tick (or -1) of the covers, attempt start t, closed forms
  function automatic int cover_match(int id, int t);
    int y, x, x2, a;
    case (id)
      0: begin   // s0: n = 5; (v, n = n+1) ##1 (w, n = n+1) ##1 (d[2:0] == n[2:0]) with n = 7
        if (t + 2 < NT && sv[t] && sw[t + 1] && (sd[t + 2] & 7) == 7) return t + 2;
      end
      1: begin   // s0 ##1 s0 (each instance has its own n)
        if (t + 5 < NT && sv[t] && sw[t + 1] && (sd[t + 2] & 7) == 7
            && sv[t + 3] && sw[t + 4] && (sd[t + 5] & 7) == 7) return t + 5;
      end
      2: begin   // s2: (w, c++)[*1:3] ##1 (c == 2): two repetitions
        if (t + 2 < NT && sw[t] && sw[t + 1]) return t + 2;
      end
      3: begin   // v ##1 s2 ##1 s2
        if (t + 6 < NT && sv[t] && sw[t + 1] && sw[t + 2] && sw[t + 4] && sw[t + 5]) return t + 6;
      end
      4: begin   // s4(d, 1): a = 3 + x
        if (t + 1 < NT && sv[t]) begin
          a = 3 + sd[t];
          if (low2(sd[t + 1]) == (low2(a) ^ (1 & 3))) return t + 1;
        end
      end
      5: begin   // s4(d, 2) ##1 s4(d + 1, 3)
        if (t + 3 < NT && sv[t] && sv[t + 2]) begin
          a = 3 + sd[t];
          x2 = (sd[t + 2] + 1) & 255;
          if (low2(sd[t + 1]) == (low2(a) ^ (2 & 3)) && low2(sd[t + 3]) == (low2(3 + x2) ^ (3 & 3))) return t + 3;
        end
      end
      6: begin   // v ##0 s6(d): ##2 (d == x), x = d at the start
        if (t + 2 < NT && sv[t] && sd[t + 2] == sd[t]) return t + 2;
      end
      7: begin   // w ##0 s6(d + 1)
        if (t + 2 < NT && sw[t] && sd[t + 2] == ((sd[t] + 1) & 255)) return t + 2;
      end
      8: begin   // v ##0 s8(d): ordinary formal, d == d at the later tick: always true
        if (t + 2 < NT && sv[t]) return t + 2;
      end
      13: begin  // v ##1 s12(d)
        if (t + 4 < NT && sv[t] && sw[t + 1] && sw[t + 3]) begin
          y = sd[t + 1];
          x = (y + 1) & 255;
          if (low2(sd[t + 2]) == low2(1 + y) && low2(sd[t + 4]) == low2(1 + x)) return t + 4;
        end
      end
      14: begin  // s12(1) ##1 s12(2)
        if (t + 7 < NT && sw[t] && low2(sd[t + 1]) == 2 && sw[t + 2] && low2(sd[t + 3]) == 3
            && sw[t + 4] && low2(sd[t + 5]) == 3 && sw[t + 6] && low2(sd[t + 7]) == 0) return t + 7;
      end
      15: begin  // s13(d) ##1 s13(d): x = d + 3 compared with d at the next tick
        if (t + 3 < NT && sv[t] && sv[t + 2]
            && low2(sd[t + 1]) == low2(sd[t] + 3) && low2(sd[t + 3]) == low2(sd[t + 2] + 3)) return t + 3;
      end
    endcase
    return -1;
  endfunction

  task automatic check_all;
    int e[NT], lo[NT], hi[NT];
    if (ntick < NT) begin $error("only %0d ticks sampled", ntick); errors++; end
    begin
      int ids[11] = '{0, 1, 2, 3, 4, 5, 6, 7, 8, 13, 14};
      for (int ci = 0; ci < 12; ci++) begin
        int id = (ci < 11) ? ids[ci] : 15;
        for (int k = 0; k < NT; k++) e[k] = 0;
        for (int t = 0; t < NT; t++) begin
          int f = cover_match(id, t);
          if (f >= 0) e[f]++;
        end
        for (int k = 0; k < NT; k++)
          if (hits[id][k] != e[k]) begin
            $error("cover id %0d tick %0d (time %0d): %0d hits, expected %0d", id, k, 5 + 10 * k, hits[id][k], e[k]);
            errors++;
          end
      end
    end
    // P9: p9(d): (v, t = 7 + x) |=> (d[2:0] == t[2:0]), x = d at the start
    begin
      int cum_lo = 0, cum_hi = 0, cum_op = 0;
      for (int k = 0; k < NT; k++) begin e[k] = 0; lo[k] = 0; hi[k] = 0; end
      for (int t = 0; t < NT; t++) begin
        if (!sv[t]) hi[t]++;                               // vacuous: verdict at tick t
        else if (t + 1 >= NT) ;                            // decided after the last tick
        else if (((sd[t + 1] ^ (7 + sd[t])) & 7) == 0) begin hi[t + 1]++; lo[t + 1]++; end
        else e[t + 1]++;
      end
      for (int k = 0; k < NT; k++) begin
        if (hits[10][k] != e[k]) begin
          $error("P9 tick %0d (time %0d): %0d fail actions, expected %0d", k, 5 + 10 * k, hits[10][k], e[k]); errors++;
        end
        cum_lo += lo[k]; cum_hi += hi[k]; cum_op += hits[9][k];
        if (cum_op < cum_lo || cum_op > cum_hi) begin
          $error("P9 up to tick %0d: %0d pass actions, allowed %0d..%0d", k, cum_op, cum_lo, cum_hi); errors++;
        end
      end
    end
    // P10: p9(1) and p9(2) never hold together: one fail at t + 1 for every v at t
    for (int k = 0; k < NT; k++) e[k] = 0;
    for (int t = 0; t + 1 < NT; t++) if (sv[t]) e[t + 1]++;
    for (int k = 0; k < NT; k++)
      if (hits[12][k] != e[k]) begin
        $error("P10 tick %0d (time %0d): %0d fail actions, expected %0d", k, 5 + 10 * k, hits[12][k], e[k]); errors++;
      end
    if (errors == 0) $display("PASSED");
  endtask
endmodule
