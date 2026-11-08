`timescale 1ns/1ns
// Concurrent assertions and covers with implication (|-> and |=>), cycle delay
// ranges, consecutive repetition, goto and non-consecutive repetition and
// disable iff, driven by a fixed 60-cycle stimulus.
// Expected values: IEEE 1800-2017 16.9.1 (##), 16.9.2 ([*], [->], [=]), 16.12.7 / 16.12.8
// (|-> and |=>), 16.15 (disable iff), 16.14.1 (assertion actions; the pass action
// also runs for a vacuous success), 16.14.3 (cover property), 16.9.3 / 16.5.1
// (values sampled in the preponed region of the clock tick).
//
// Reference model.  The values of a, b, c, d, rst sampled at each posedge are
// recorded (tick k is at time 5 + 10 k).  For every property a function computes,
// for an attempt starting at tick t and a trace prefix of L ticks, the three-valued
// verdict (0 = already false for every continuation, 1 = already true for every
// continuation, 2 = undecided) with Kleene logic over the samples that are not
// yet known.  The tick at which an attempt first gets a verdict is the tick at
// which its pass or fail action runs; an attempt is dropped if rst was sampled high
// in any tick from its start up to that tick (disable iff).  Expected per property:
//   - fail actions at each tick: exactly the attempts with verdict 0;
//   - pass actions: never more than the attempts with verdict 1 and at least the
//     non-vacuous ones (antecedent matched) by the tick of their verdict, and in
//     total (parameter VAC = 1) also every vacuous success decided before the
//     last TAIL ticks (a vacuous success is a success whose pass action runs,
//     16.14.1); the exact tick of a vacuous pass is not pinned down;
//   - cover property of a sequence: one action per attempt, at its first match
//     (only `cover sequence` counts every match).
// Match semantics used: a[=n] = a[->n] ##1 !a[*0:$], so it can end at any tick from the
// n-th occurrence of a until just before the (n+1)-th.
module sva1;
  // VAC = 1: the pass action of a vacuous success runs (16.14.1); 0 is only used
  // to look at the remaining checks separately from that rule.
  parameter bit VAC = 1;
  logic clk = 0, a = 0, b = 0, c = 0, d = 0, rst = 0;
  always #5 clk = ~clk;
  localparam [59:0] PA = 60'b001011110010110110010000101001101001101001011011110101101101, PB = 60'b001110101100000011111010010110111110110000010000101010011000, PC = 60'b010101100100000000000001101000000010011101011100110011011001, PD = 60'b100010011011000010010100101011101110001011010111000000001111, PR = 60'b000000000000000000000001100000000000000000000000000000000000;
  integer i;
  initial begin for (i = 0; i < 60; i++) begin @(negedge clk); a = PA[i]; b = PB[i]; c = PC[i]; d = PD[i]; rst = PR[i]; end check_all; $finish; end

  localparam int NT = 60;       // number of ticks
  localparam int NP = 12;       // P0 .. P11
  localparam int TAIL = 6;      // vacuous passes of attempts decided in the last TAIL ticks are not required
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

  P0: assert property (@(posedge clk) disable iff (rst) a |-> b) ok(0); else bad(0);
  P1: assert property (@(posedge clk) disable iff (rst) a |=> b) ok(1); else bad(1);
  P2: assert property (@(posedge clk) disable iff (rst) a ##1 b |-> c) ok(2); else bad(2);
  P3: assert property (@(posedge clk) disable iff (rst) a ##2 b |=> c) ok(3); else bad(3);
  P4: assert property (@(posedge clk) disable iff (rst) a |-> ##[1:3] b) ok(4); else bad(4);
  P5: assert property (@(posedge clk) disable iff (rst) a ##[1:3] b |-> c) ok(5); else bad(5);
  P6: assert property (@(posedge clk) disable iff (rst) (a ##1 b)[*2] |-> c) ok(6); else bad(6);
  P7: assert property (@(posedge clk) disable iff (rst) a[*2:3] ##1 b |=> c) ok(7); else bad(7);
  P8: assert property (@(posedge clk) disable iff (rst) b[->2] |=> c) ok(8); else bad(8);
  P9: assert property (@(posedge clk) disable iff (rst) a[=2] ##1 b |-> c) ok(9); else bad(9);
  P10: cover property (@(posedge clk) disable iff (rst) a ##1 b ##1 c) ok(10);
  P11: cover property (@(posedge clk) disable iff (rst) a[*2]) ok(11);
  // ---------------- reference model ----------------
  // Three-valued logic over the samples that are known: 0, 1, or 2 (not known yet).
  int gL;                                    // length of the known prefix
  function automatic int S(int sig, int k);  // sampled value; before the first tick the initial value 0
    if (k < 0) return 0;
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
  function automatic int keq(int x, int y);
    if (x == 2 || y == 2) return 2;
    return x == y;
  endfunction
  function automatic int A(int k); return S(0, k); endfunction
  function automatic int B(int k); return S(1, k); endfunction
  function automatic int C(int k); return S(2, k); endfunction
  function automatic int D(int k); return S(3, k); endfunction
  function automatic bit dis(int t, int e);    // rst sampled in [t, e]
    for (int k = t; k <= e; k++) if (sm[4][k]) return 1;
    return 0;
  endfunction

  function automatic bit is_assert(int p); return p <= 9; endfunction
  function automatic bit use_dis(int p); return 1; endfunction
  function automatic string pname(int p); return $sformatf("P%0d", p); endfunction

  // verdict of property p for the attempt starting at tick t, prefix gL
  function automatic int verdict(int p, int t);
    int m, m2, m3, r, j, e, cnt, v;
    case (p)
      0: return kor(knot(A(t)), B(t));
      1: return kor(knot(A(t)), B(t + 1));
      2: begin m = kand(A(t), B(t + 1)); return kor(knot(m), C(t + 1)); end
      3: begin m = kand(A(t), B(t + 2)); return kor(knot(m), C(t + 3)); end
      4: return kor(knot(A(t)), kor(B(t + 1), kor(B(t + 2), B(t + 3))));
      5: begin   // a ##[1:3] b |-> c: every match end t + j needs c at t + j
        r = 1;
        for (j = 1; j <= 3; j++) begin
          m = kand(A(t), B(t + j));
          r = kand(r, kor(knot(m), C(t + j)));
        end
        return r;
      end
      6: begin   // (a ##1 b)[*2] = a ##1 b ##1 a ##1 b, end t + 3
        m = kand(kand(A(t), B(t + 1)), kand(A(t + 2), B(t + 3)));
        return kor(knot(m), C(t + 3));
      end
      7: begin   // a[*2:3] ##1 b |=> c: ends t + 2 and t + 3, c one tick later
        m2 = kand(kand(A(t), A(t + 1)), B(t + 2));
        m3 = kand(kand(A(t), A(t + 1)), kand(A(t + 2), B(t + 3)));
        return kand(kor(knot(m2), C(t + 3)), kor(knot(m3), C(t + 4)));
      end
      8: begin   // b[->2] |=> c: the match ends at the second b; undecided until it exists
        cnt = 0;
        for (e = t; e <= NT + 1; e++) begin
          v = B(e);
          if (v == 2) return 2;
          if (v == 1) begin cnt++; if (cnt == 2) return C(e + 1); end
        end
        return 2;
      end
      9: begin   // a[=2] ##1 b |-> c: ends e from the 2nd a until before the 3rd a, then b at e + 1
        cnt = 0; r = 1;
        for (e = t; e <= NT + 1; e++) begin
          v = A(e);
          if (v == 2) return kand(r, 2);
          if (v == 1) begin
            cnt++;
            if (cnt == 3) return r;
          end
          if (cnt == 2) r = kand(r, kor(knot(B(e + 1)), C(e + 1)));
        end
        return kand(r, 2);
      end
    endcase
    return 2;
  endfunction
  // was the antecedent matched (decided on the full trace)?
  function automatic int nonvac(int p, int t);
    int cnt, e, v;
    case (p)
      2: return kand(A(t), B(t + 1)) == 1;
      3: return kand(A(t), B(t + 2)) == 1;
      5: begin
        for (int j = 1; j <= 3; j++) if (kand(A(t), B(t + j)) == 1) return 1;
        return 0;
      end
      6: return kand(kand(A(t), B(t + 1)), kand(A(t + 2), B(t + 3))) == 1;
      7: return kand(kand(A(t), A(t + 1)), B(t + 2)) == 1 || kand(kand(A(t), A(t + 1)), kand(A(t + 2), B(t + 3))) == 1;
      8: return 1;     // b[->2] matched, or the attempt is not decided
      9: begin
        cnt = 0;
        for (e = t; e < NT; e++) begin
          if (A(e) == 1) cnt++;
          if (cnt == 3) break;
          if (cnt == 2 && B(e + 1) == 1) return 1;
        end
        return 0;
      end
      default: return A(t) == 1;
    endcase
  endfunction
  // cover: tick of the first match of the attempt at t (full trace), or -1
  function automatic int cover_end(int p, int t);
    case (p)
      10: if (kand(kand(A(t), B(t + 1)), C(t + 2)) == 1) return t + 2;
      11: if (kand(A(t), A(t + 1)) == 1) return t + 1;
    endcase
    return -1;
  endfunction
  // ---------------- comparison ----------------
  // Assertions: the fail actions of each tick must equal the attempts whose verdict
  // is false at that tick (an attempt dropped by disable iff has no action).  The
  // pass actions are bounded tick by tick between the non-vacuous passes (antecedent
  // matched) and all passes; at the end every pass must have been reported, including
  // the vacuous successes (16.14.1), except for the vacuous successes of attempts decided
  // in the last TAIL ticks, whose reporting tick is not pinned down here.
  // Covers (cover property of a sequence, 16.14.3): one action per attempt at its
  // first match, exactly per tick.
  task automatic check_all;
    int e_fail[NT], e_lo[NT], e_hi[NT], e_cov[NT];
    int vac_head, lo_tot, hi_tot;
    if (ntick < NT) begin $error("only %0d ticks sampled", ntick); errors++; end
    for (int p = 0; p < NP; p++) begin
      for (int k = 0; k < NT; k++) begin e_fail[k] = 0; e_lo[k] = 0; e_hi[k] = 0; e_cov[k] = 0; end
      vac_head = 0; lo_tot = 0; hi_tot = 0;
      if (is_assert(p)) begin
        for (int t = 0; t < NT; t++) begin
          int v = 2, dk = -1;
          for (int L = t + 1; L <= NT; L++) begin
            gL = L;
            v = verdict(p, t);
            if (v != 2) begin dk = L - 1; break; end
          end
          if (v == 2) continue;
          if (use_dis(p) && dis(t, dk)) continue;
          if (v == 0) e_fail[dk]++;
          else begin
            e_hi[dk]++; hi_tot++;
            gL = NT;
            if (nonvac(p, t)) begin e_lo[dk]++; lo_tot++; end
            else if (dk <= NT - 1 - TAIL) vac_head++;
          end
        end
        begin
          int cum_lo = 0, cum_hi = 0, cum_op = 0;
          for (int k = 0; k < NT; k++) begin
            if (o_fail[p][k] != e_fail[k]) begin
              $error("%s tick %0d (time %0d): %0d fail actions, expected %0d", pname(p), k, 5 + 10 * k, o_fail[p][k], e_fail[k]);
              errors++;
            end
            cum_lo += e_lo[k]; cum_hi += e_hi[k]; cum_op += o_pass[p][k];
            if (cum_op < cum_lo || cum_op > cum_hi) begin
              $error("%s up to tick %0d: %0d pass actions, allowed %0d..%0d", pname(p), k, cum_op, cum_lo, cum_hi);
              errors++;
            end
          end
          if (cum_op < lo_tot + (VAC ? vac_head : 0) || cum_op > hi_tot) begin
            $error("%s: %0d pass actions in total, expected %0d..%0d (%0d non-vacuous, %0d vacuous decided early)",
                   pname(p), cum_op, lo_tot + (VAC ? vac_head : 0), hi_tot, lo_tot, vac_head);
            errors++;
          end
        end
      end
      else begin
        gL = NT;
        for (int t = 0; t < NT; t++) begin
          int e = cover_end(p, t);
          if (e >= 0 && !dis(t, e)) e_cov[e]++;
        end
        for (int k = 0; k < NT; k++)
          if (o_pass[p][k] != e_cov[k]) begin
            $error("%s cover tick %0d (time %0d): %0d hits, expected %0d", pname(p), k, 5 + 10 * k, o_pass[p][k], e_cov[k]);
            errors++;
          end
      end
    end
    if (errors == 0) $display("PASSED");
  endtask
endmodule
