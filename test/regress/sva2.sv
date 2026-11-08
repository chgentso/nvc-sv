// Concurrent assertions and covers over sequences with and / or / intersect /
// within / throughout / first_match, goto and consecutive repetition,
// implication (overlapped and non-overlapped), not, if/else, nexttime and
// disable iff, driven by a fixed 60-cycle stimulus.
// Expected values: IEEE 1800-2017 16.9 (sequence operators: 16.9.1 ##,
// 16.9.2 [*] [->], 16.9.5 and, 16.9.6 intersect, 16.9.7 or, 16.9.8 first_match,
// 16.9.9 throughout, 16.9.10 within), 16.12 (properties: |-> |=> not, if/else,
// nexttime, disable iff 16.12.? ), 16.14 (assertion statements, pass/fail action
// blocks, cover), 16.9.3 (sampling at the clock tick).
//
// Reference model.  The values of a, b, c, d, rst sampled at each posedge are
// recorded (tick k is at time 5 + 10 k).  For each property a function
// computes, for an attempt starting at tick t and a trace prefix of L ticks,
// the three-valued verdict (0 = already false for every continuation,
// 1 = already true for every continuation, 2 = undecided) with Kleene
// logic over the samples that are not yet known.  The tick at which an
// attempt first gets a verdict is the tick at which its pass/fail action
// runs; an attempt is dropped if rst was sampled high in any tick from its
// start up to that tick (disable iff).  Expected per property:
//   - number of fail actions at each tick: exactly the attempts with verdict 0;
//   - pass actions: at no tick earlier than the first possible verdict and
//     at most one per attempt, and at least all attempts whose antecedent
//     matched (non-vacuous passes) by the tick of their verdict (vacuous
//     passes may be reported or not, so that the model does not depend on
//     whether the pass action is executed for a vacuous success);
//   - cover property of a sequence: one action per attempt, at its first match
//     (a sequence used as a property holds as soon as it matches; only
//     `cover sequence` counts every match).
// For a[*0:2] ##1 b the matches that use at least one repetition are modelled
// exactly; for the zero-repetition match (an empty match concatenated with ##1 b,
// 16.9.2) three readings are accepted (no such match, a match at tick t, a match
// at tick t + 1) as long as one of them explains all hit counts: the empty
// sequence concatenation rules were not reproduced from the LRM text with
// certainty.
module sva2;
  logic clk = 0, a = 0, b = 0, c = 0, d = 0, rst = 0;
  always #5 clk = ~clk;
  localparam [59:0] PA = 60'b000101100011111001111100000010010111111011011111111101011111, PB = 60'b101101000001000100010011000000000110000100000101110110110100, PC = 60'b110000011001000001010110100000000101100010000101110010010000, PD = 60'b100000000000011110011001001100111101001001011100010110010011, PR = 60'b000000000000000000000001100000000000000000000000000000000000;
  integer i;
  initial begin for (i = 0; i < 60; i++) begin @(negedge clk); a = PA[i]; b = PB[i]; c = PC[i]; d = PD[i]; rst = PR[i]; end check_all; $finish; end

  localparam int NT = 60;       // number of ticks
  localparam int NP = 17;       // P0 .. P16
  int errors = 0;
  // samples: sm[0..4][k] for a b c d rst
  int sm[5][NT];
  int ntick = 0;
  always @(posedge clk) begin
    if (ntick < NT) begin
      sm[0][ntick] = a; sm[1][ntick] = b; sm[2][ntick] = c; sm[3][ntick] = d; sm[4][ntick] = rst;
    end
    ntick++;
  end
  // observed actions per property and tick
  int o_pass[NP][NT], o_fail[NP][NT];
  function automatic void ok(int p);  int k = ($time - 5) / 10; if (k >= 0 && k < NT) o_pass[p][k]++; endfunction
  function automatic void bad(int p); int k = ($time - 5) / 10; if (k >= 0 && k < NT) o_fail[p][k]++; endfunction

  P0: assert property (@(posedge clk) disable iff (rst) (a ##1 b) and (c ##2 d) |-> a) ok(0); else bad(0);
  P1: assert property (@(posedge clk) disable iff (rst) a |-> (b ##1 c) or (d ##2 c)) ok(1); else bad(1);
  P2: assert property (@(posedge clk) disable iff (rst) (a ##[1:3] b) intersect (c ##[1:3] d) |-> a) ok(2); else bad(2);
  P3: assert property (@(posedge clk) disable iff (rst) a ##1 b |-> (c[*1:2] within (d ##1 d ##1 d))) ok(3); else bad(3);
  P4: assert property (@(posedge clk) disable iff (rst) d throughout (a ##1 b ##1 c) |-> a) ok(4); else bad(4);
  P5: assert property (@(posedge clk) disable iff (rst) first_match(a ##[1:3] b) |=> c) ok(5); else bad(5);
  P6: assert property (@(posedge clk) disable iff (rst) a |-> not (b ##1 c)) ok(6); else bad(6);
  P7: assert property (@(posedge clk) disable iff (rst) a |-> if (b) c else d) ok(7); else bad(7);
  P8: assert property (@(posedge clk) disable iff (rst) a |=> nexttime c) ok(8); else bad(8);
  P9: assert property (@(posedge clk) disable iff (rst) a |-> nexttime[2] b) ok(9); else bad(9);
  P10: assert property (@(posedge clk) disable iff (rst) a |-> ##1 b ##1 c) ok(10); else bad(10);
  P11: assert property (@(posedge clk) disable iff (rst) a ##0 b |-> c) ok(11); else bad(11);
  P12: assert property (@(posedge clk) disable iff (rst) (a ##1 b) or (c ##1 d) |=> a) ok(12); else bad(12);
  P13: cover property (@(posedge clk) disable iff (rst) (a ##1 b) and (c ##1 d)) ok(13);
  P14: cover property (@(posedge clk) disable iff (rst) first_match(a ##[1:3] b)) ok(14);
  P15: cover property (@(posedge clk) disable iff (rst) a[->2] ##1 b) ok(15);
  P16: cover property (@(posedge clk) disable iff (rst) a[*0:2] ##1 b) ok(16);

  // ---------------- reference model ----------------
  int gL;                                    // length of the known prefix
  function automatic int S(int sig, int k);  // sampled value or 2 (unknown)
    if (k >= gL || k >= NT) return 2;
    return sm[sig][k];
  endfunction
  function automatic int kand(int x, int y);
    if (x == 0 || y == 0) return 0;
    if (x == 1 && y == 1) return 1;
    return 2;
  endfunction
  function automatic int kor(int x, int y);
    if (x == 1 || y == 1) return 1;
    if (x == 0 && y == 0) return 0;
    return 2;
  endfunction
  function automatic int knot(int x);
    return x == 2 ? 2 : 1 - x;
  endfunction
  function automatic int kite(int cnd, int x, int y);   // if (cnd) x else y
    if (cnd == 1) return x;
    if (cnd == 0) return y;
    return (x == y) ? x : 2;
  endfunction
  function automatic int A(int k); return S(0, k); endfunction
  function automatic int B(int k); return S(1, k); endfunction
  function automatic int C(int k); return S(2, k); endfunction
  function automatic int D(int k); return S(3, k); endfunction

  // verdict of property p for the attempt starting at tick t, prefix gL
  function automatic int verdict(int p, int t);
    int m, r, j, b1, b2, b3;
    case (p)
      0: begin   // (a ##1 b) and (c ##2 d) |-> a
        m = kand(kand(A(t), B(t + 1)), kand(C(t), D(t + 2)));
        return kor(knot(m), A(t + 2));
      end
      1: return kor(knot(A(t)), kor(kand(B(t), C(t + 1)), kand(D(t), C(t + 2))));
      2: begin   // (a ##[1:3] b) intersect (c ##[1:3] d) |-> a
        r = 1;
        for (j = 1; j <= 3; j++) begin
          m = kand(kand(A(t), B(t + j)), kand(C(t), D(t + j)));
          r = kand(r, kor(knot(m), A(t + j)));
        end
        return r;
      end
      3: begin   // a ##1 b |-> (c[*1:2] within (d ##1 d ##1 d)) starting at e = t + 1
        m = kand(A(t), B(t + 1));
        r = kand(kand(D(t + 1), kand(D(t + 2), D(t + 3))), kor(C(t + 1), kor(C(t + 2), C(t + 3))));
        return kor(knot(m), r);
      end
      4: begin   // d throughout (a ##1 b ##1 c) |-> a
        m = kand(kand(D(t), kand(D(t + 1), D(t + 2))), kand(A(t), kand(B(t + 1), C(t + 2))));
        return kor(knot(m), A(t + 2));
      end
      5: begin   // first_match(a ##[1:3] b) |=> c
        b1 = B(t + 1); b2 = B(t + 2); b3 = B(t + 3);
        r = kite(b1, C(t + 2), kite(b2, C(t + 3), kite(b3, C(t + 4), 1)));
        return kor(knot(A(t)), r);
      end
      6: return kor(knot(A(t)), knot(kand(B(t), C(t + 1))));
      7: return kor(knot(A(t)), kite(B(t), C(t), D(t)));
      8: return kor(knot(A(t)), C(t + 2));
      9: return kor(knot(A(t)), B(t + 2));
      10: return kor(knot(A(t)), kand(B(t + 1), C(t + 2)));
      11: return kor(knot(kand(A(t), B(t))), C(t));
      12: begin   // (a ##1 b) or (c ##1 d) |=> a
        m = kor(kand(A(t), B(t + 1)), kand(C(t), D(t + 1)));
        return kor(knot(m), A(t + 2));
      end
    endcase
    return 2;
  endfunction
  // was the antecedent matched (decided on the full trace)?
  function automatic int nonvac(int p, int t);
    case (p)
      0: return kand(kand(A(t), B(t + 1)), kand(C(t), D(t + 2))) == 1;
      2: begin
        for (int j = 1; j <= 3; j++)
          if (kand(kand(A(t), B(t + j)), kand(C(t), D(t + j))) == 1) return 1;
        return 0;
      end
      3: return kand(A(t), B(t + 1)) == 1;
      4: return kand(kand(D(t), kand(D(t + 1), D(t + 2))), kand(A(t), kand(B(t + 1), C(t + 2)))) == 1;
      5: return A(t) == 1 && (B(t + 1) == 1 || B(t + 2) == 1 || B(t + 3) == 1);
      11: return kand(A(t), B(t)) == 1;
      12: return kor(kand(A(t), B(t + 1)), kand(C(t), D(t + 1))) == 1;
      default: return A(t) == 1;
    endcase
  endfunction
  function automatic bit dis(int t, int e);    // rst sampled in [t, e]
    for (int k = t; k <= e; k++) if (sm[4][k]) return 1;
    return 0;
  endfunction

  // cover matches (end ticks), computed on the full trace
  function automatic void cover_ends(int p, int t, ref bit ends[NT], ref bit extra[NT]);
    int i1, i2;
    for (int k = 0; k < NT; k++) begin ends[k] = 0; extra[k] = 0; end
    gL = NT;
    case (p)
      13: if (kand(kand(A(t), B(t + 1)), kand(C(t), D(t + 1))) == 1) ends[t + 1] = 1;
      14: if (A(t) == 1) begin
        for (int j = 1; j <= 3; j++) if (B(t + j) == 1) begin ends[t + j] = 1; break; end
      end
      15: begin   // a[->2] ##1 b
        i1 = -1; i2 = -1;
        for (int k = t; k < NT; k++) if (sm[0][k]) begin
          if (i1 < 0) i1 = k; else begin i2 = k; break; end
        end
        if (i2 >= 0 && B(i2 + 1) == 1) ends[i2 + 1] = 1;
      end
      16: begin   // a[*0:2] ##1 b: matches with 1 or 2 repetitions; zero repetitions, see below
        if (A(t) == 1 && B(t + 1) == 1) ends[t + 1] = 1;
        if (A(t) == 1 && A(t + 1) == 1 && B(t + 2) == 1) ends[t + 2] = 1;
        if (B(t) == 1) extra[t] = 1;          // reading 0: (empty ##1 b) = (##0 b), end at t
        if (B(t + 1) == 1) extra[t + 1] = 1;  // reading 1: (empty ##1 b) = (##1 b), end at t + 1
      end
    endcase
  endfunction

  task automatic check_all;
    int e_fail[NT], e_lo[NT], e_hi[NT];
    bit ends[NT], extra[NT];
    int e_ex[NT];
    if (ntick < NT) begin $error("only %0d ticks sampled", ntick); errors++; end
    for (int p = 0; p < NP; p++) begin
      for (int k = 0; k < NT; k++) begin e_fail[k] = 0; e_lo[k] = 0; e_hi[k] = 0; e_ex[k] = 0; end
      if (p <= 12) begin
        for (int t = 0; t < NT; t++) begin
          int v = 2, dk = -1;
          for (int L = t + 1; L <= NT; L++) begin
            gL = L;
            v = verdict(p, t);
            if (v != 2) begin dk = L - 1; break; end
          end
          if (v == 2 || dis(t, dk)) continue;
          if (v == 0) e_fail[dk]++;
          else begin
            e_hi[dk]++;
            gL = NT;
            if (nonvac(p, t)) e_lo[dk]++;
          end
        end
        begin
          int cum_lo = 0, cum_hi = 0, cum_op = 0;
          for (int k = 0; k < NT; k++) begin
            if (o_fail[p][k] != e_fail[k]) begin
              $error("P%0d tick %0d (time %0d): %0d fail actions, expected %0d", p, k, 5 + 10 * k, o_fail[p][k], e_fail[k]);
              errors++;
            end
            cum_lo += e_lo[k]; cum_hi += e_hi[k]; cum_op += o_pass[p][k];
            if (cum_op < cum_lo || cum_op > cum_hi) begin
              $error("P%0d up to tick %0d: %0d pass actions, allowed %0d..%0d", p, k, cum_op, cum_lo, cum_hi);
              errors++;
            end
          end
        end
      end
      else begin
        // cover property (sequence): one action per attempt, at its first match.
        // Hypotheses for the zero-repetition match of P16: none / reading 0 / reading 1
        int hyp_hits[3][NT];
        for (int h = 0; h < 3; h++) for (int k = 0; k < NT; k++) hyp_hits[h][k] = 0;
        for (int t = 0; t < NT; t++) begin
          int first[3];
          cover_ends(p, t, ends, extra);
          for (int h = 0; h < 3; h++) first[h] = -1;
          for (int k = t; k < NT; k++) begin
            if (ends[k] && !dis(t, k)) begin
              if (first[0] < 0) first[0] = k;
              if (first[1] < 0) first[1] = k;
              if (first[2] < 0) first[2] = k;
            end
          end
          if (p == 16) begin
            // zero repetition matches and the other matches of the same attempt
            for (int k = t; k < NT; k++) begin
              bit zr0 = (k == t) && extra[k];            // reading 0
              bit zr1 = (k == t + 1) && extra[k];        // reading 1
              if (zr0 && !dis(t, k) && (first[1] < 0 || k < first[1])) first[1] = k;
              if (zr1 && !dis(t, k) && (first[2] < 0 || k < first[2])) first[2] = k;
            end
          end
          for (int h = 0; h < 3; h++) if (first[h] >= 0) hyp_hits[h][first[h]]++;
        end
        begin
          int nh = (p == 16) ? 3 : 1;
          bit any_ok = 0;
          for (int h = 0; h < nh; h++) begin
            bit match = 1;
            for (int k = 0; k < NT; k++) if (o_pass[p][k] != hyp_hits[h][k]) match = 0;
            if (match) any_ok = 1;
          end
          if (!any_ok) begin
            for (int k = 0; k < NT; k++)
              if (o_pass[p][k] != hyp_hits[0][k])
                $error("P%0d cover tick %0d (time %0d): %0d hits, expected %0d", p, k, 5 + 10 * k, o_pass[p][k], hyp_hits[0][k]);
            errors++;
          end
        end
      end
    end
    if (errors == 0) $display("PASSED");
  endtask
endmodule
