`timescale 1ns/1ns
// Unbounded and ranged repetition and delays ([*1:$], ##[1:$], [->m:n], [=m:n], ##[0:2],
// [*0:2]), intersect, throughout, within, first_match, or, boolean operators in
// implications, and covers of unbounded sequences, driven by a fixed 60-cycle
// stimulus.
// Expected values: IEEE 1800-2017 16.9.1 (##), 16.9.2 ([*], [->], [=]), 16.9.6 (intersect),
// 16.9.7 (or), 16.9.8 (first_match), 16.9.9 (throughout), 16.9.10 (within), 16.12.7 /
// 16.12.8 (|-> and |=>), 16.15 (disable iff), 16.14.1 (assertion actions; the pass
// action also runs for a vacuous success), 16.14.3 (cover property), 16.9.3 / 16.5.1
// (sampling).
//
// Reference model: the values of a, b, c, d, rst sampled at each posedge are recorded
// (tick k is at time 5 + 10 k).  For every property a function computes, for an attempt
// starting at tick t and a trace prefix of L ticks, the three-valued verdict (0 = already
// false for every continuation, 1 = already true, 2 = undecided) with Kleene logic over
// the samples not yet known; an implication is the conjunction, over all matches of the
// antecedent, of "consequent holds from the end of that match".  The tick at which an
// attempt first gets a verdict is the tick of its pass or fail action; an attempt is
// dropped by disable iff if rst was sampled high in any tick from its start up to that
// tick.  Expected per property:
//   - fail actions at each tick: exactly the attempts with verdict 0;
//   - pass actions: never more than the attempts with verdict 1 and at least the
//     non-vacuous ones (antecedent matched) by the tick of their verdict, and in total
//     (parameter VAC = 1) also every vacuous success decided before the last TAIL ticks
//     (16.14.1);
//   - P12, P13 (cover property of a sequence): one action per attempt at its first match.
// Match semantics used: b[=n] ends at any tick from the n-th occurrence of b until just
// before the (n+1)-th (b[=n] = b[->n] ##1 !b[*0:$]); a[*0:2] ##1 b with zero repetitions is
// (empty ##1 b) = (##0 b), a match of length one ending at the start tick (16.9.2: an
// empty match concatenated with ##n s is ##(n-1) s); `within` is
// (1[*0:$] ##1 s1 ##1 1[*0:$]) intersect s2 (16.9.10), `intersect` needs equal start
// and end ticks (16.9.6).
module sva9;
  // VAC = 1: the pass action of a vacuous success runs (16.14.1); 0 is only used
  // to look at the remaining checks separately from that rule.
  parameter bit VAC = 1;
  logic clk = 0, a = 0, b = 0, c = 0, d = 0, rst = 0;
  always #5 clk = ~clk;
  localparam [59:0] PA = 60'b111001001100011000000001110011001110110100010110000001111001, PB = 60'b101101000000110010011011001100010000110111000000111010100100, PC = 60'b100111010010000001001100010010100001111000010000000000011000, PD = 60'b011000111000101100011001101010101100000111111100101000111011, PR = 60'b000000000000000000000001100000000000000000000000000000000000;
  integer i;
  initial begin for (i = 0; i < 60; i++) begin @(negedge clk); a = PA[i]; b = PB[i]; c = PC[i]; d = PD[i]; rst = PR[i]; end check_all; $finish; end

  localparam int NT = 60;       // number of ticks
  localparam int NP = 16;       // P0 .. P15
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

  P0: assert property (@(posedge clk) disable iff (rst) a[*1:$] ##1 b |-> c) ok(0); else bad(0);
  P1: assert property (@(posedge clk) disable iff (rst) a |-> b[*1:$] ##1 c) ok(1); else bad(1);
  P2: assert property (@(posedge clk) disable iff (rst) a |-> ##[1:$] b) ok(2); else bad(2);
  P3: assert property (@(posedge clk) disable iff (rst) (a ##1 b)[*1:$] ##1 c |=> d) ok(3); else bad(3);
  P4: assert property (@(posedge clk) disable iff (rst) a ##[0:2] b |-> c ##[0:2] d) ok(4); else bad(4);
  P5: assert property (@(posedge clk) disable iff (rst) a[->1:2] ##1 b |-> c) ok(5); else bad(5);
  P6: assert property (@(posedge clk) disable iff (rst) b[=1:2] ##1 c |-> d) ok(6); else bad(6);
  P7: assert property (@(posedge clk) disable iff (rst) a |-> (b[*1:3] ##1 c) intersect (d[*2:4])) ok(7); else bad(7);
  P8: assert property (@(posedge clk) disable iff (rst) a[*0:2] ##1 b |=> c) ok(8); else bad(8);
  P9: assert property (@(posedge clk) disable iff (rst) (a or b) ##1 c |-> d) ok(9); else bad(9);
  P10: assert property (@(posedge clk) disable iff (rst) a && b |-> c || d) ok(10); else bad(10);
  P11: assert property (@(posedge clk) disable iff (rst) a |-> ##2 (b throughout (c ##1 d))) ok(11); else bad(11);
  P12: cover property (@(posedge clk) disable iff (rst) (a ##[1:$] b) intersect (c[*2:5])) ok(12);
  P13: cover property (@(posedge clk) disable iff (rst) a[*2:$] ##1 b) ok(13);
  P14: assert property (@(posedge clk) disable iff (rst) a |-> first_match(b[*1:$] ##1 c) ##1 d) ok(14); else bad(14);
  P15: assert property (@(posedge clk) disable iff (rst) (a ##1 b) within (c[*2:6]) |-> d) ok(15); else bad(15);
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

  function automatic bit is_assert(int p); return p != 12 && p != 13; endfunction
  function automatic bit use_dis(int p); return 1; endfunction
  function automatic string pname(int p); return $sformatf("P%0d", p); endfunction

  // verdict of property p for the attempt starting at tick t, prefix gL
  function automatic int verdict(int p, int t);
    int m, r, run, j, k, e, v, cnt, ex, cons, mj, found, q, t1, runc;
    case (p)
      0: begin   // a[*1:$] ##1 b |-> c: ends t + j, a at t .. t + j - 1, b at t + j, c at t + j
        r = 1; run = 1;
        for (j = 1; j <= NT + 1; j++) begin
          run = kand(run, A(t + j - 1));
          if (run == 0) return r;
          m = kand(run, B(t + j));
          r = kand(r, kor(knot(m), C(t + j)));
          if (run == 2) return kand(r, 2);
        end
        return kand(r, 2);
      end
      1: begin   // a |-> b[*1:$] ##1 c: some match of the consequent
        ex = 0; run = 1;
        for (j = 1; j <= NT + 1; j++) begin
          run = kand(run, B(t + j - 1));
          if (run == 0) break;
          ex = kor(ex, kand(run, C(t + j)));
          if (run == 2) begin ex = kor(ex, 2); break; end
        end
        if (j > NT + 1) ex = kor(ex, 2);
        return kor(knot(A(t)), ex);
      end
      2: begin   // a |-> ##[1:$] b
        ex = 0;
        for (j = 1; j <= NT + 1; j++) ex = kor(ex, B(t + j));
        return kor(knot(A(t)), ex);
      end
      3: begin   // (a ##1 b)[*1:$] ##1 c |=> d: n pairs, c at t + 2n, d at t + 2n + 1
        r = 1; run = 1;
        for (j = 1; j <= NT; j++) begin
          run = kand(run, kand(A(t + 2 * j - 2), B(t + 2 * j - 1)));
          if (run == 0) return r;
          m = kand(run, C(t + 2 * j));
          r = kand(r, kor(knot(m), D(t + 2 * j + 1)));
          if (run == 2) return kand(r, 2);
        end
        return kand(r, 2);
      end
      4: begin   // a ##[0:2] b |-> c ##[0:2] d
        r = 1;
        for (j = 0; j <= 2; j++) begin
          m = kand(A(t), B(t + j));
          cons = kand(C(t + j), kor(D(t + j), kor(D(t + j + 1), D(t + j + 2))));
          r = kand(r, kor(knot(m), cons));
        end
        return r;
      end
      5: begin   // a[->1:2] ##1 b |-> c: ends at the first and at the second a
        r = 1; found = 0;
        for (k = t; k <= NT + 1; k++) begin
          v = A(k);
          if (v == 2) return kand(r, 2);
          if (v == 1) begin
            found++;
            r = kand(r, kor(knot(B(k + 1)), C(k + 1)));
            if (found == 2) return r;
          end
        end
        return kand(r, 2);
      end
      6: begin   // b[=1:2] ##1 c |-> d: ends e while the number of b in [t, e] is 1 or 2
        r = 1; cnt = 0;
        for (e = t; e <= NT + 1; e++) begin
          v = B(e);
          if (v == 2) return kand(r, 2);
          if (v == 1) cnt++;
          if (cnt >= 3) return r;
          if (cnt >= 1) r = kand(r, kor(knot(C(e + 1)), D(e + 1)));
        end
        return kand(r, 2);
      end
      7: begin   // a |-> (b[*1:3] ##1 c) intersect (d[*2:4]): common length L = 2..4
        ex = 0;
        for (j = 2; j <= 4; j++) begin
          m = 1;
          for (q = 0; q <= j - 2; q++) m = kand(m, B(t + q));
          m = kand(m, C(t + j - 1));
          for (q = 0; q <= j - 1; q++) m = kand(m, D(t + q));
          ex = kor(ex, m);
        end
        return kor(knot(A(t)), ex);
      end
      8: begin   // a[*0:2] ##1 b |=> c: ends t + j (j repetitions), c at t + j + 1
        r = 1;
        for (j = 0; j <= 2; j++) begin
          m = (j == 0) ? B(t) : (j == 1) ? kand(A(t), B(t + 1)) : kand(kand(A(t), A(t + 1)), B(t + 2));
          r = kand(r, kor(knot(m), C(t + j + 1)));
        end
        return r;
      end
      9: begin   // (a or b) ##1 c |-> d
        m = kand(kor(A(t), B(t)), C(t + 1));
        return kor(knot(m), D(t + 1));
      end
      10: return kor(knot(kand(A(t), B(t))), kor(C(t), D(t)));
      11: return kor(knot(A(t)), kand(kand(B(t + 2), B(t + 3)), kand(C(t + 2), D(t + 3))));
      14: begin   // a |-> first_match(b[*1:$] ##1 c) ##1 d
        run = 1; cons = 2;
        for (j = 1; j <= NT + 1; j++) begin
          run = kand(run, B(t + j - 1));
          if (run == 0) begin cons = 0; break; end
          mj = kand(run, C(t + j));
          if (mj == 1) begin cons = D(t + j + 1); break; end
          if (mj == 2) begin cons = 2; break; end
        end
        return kor(knot(A(t)), cons);
      end
      15: begin  // (a ##1 b) within (c[*2:6]) |-> d: c[*k] from t, a ##1 b inside it
        r = 1;
        for (k = 2; k <= 6; k++) begin
          runc = 1;
          for (q = 0; q < k; q++) runc = kand(runc, C(t + q));
          ex = 0;
          for (t1 = t; t1 <= t + k - 2; t1++) ex = kor(ex, kand(A(t1), B(t1 + 1)));
          m = kand(runc, ex);
          r = kand(r, kor(knot(m), D(t + k - 1)));
        end
        return r;
      end
    endcase
    return 2;
  endfunction
  // was the antecedent matched (decided on the full trace)?
  function automatic int nonvac(int p, int t);
    int run, j, k, e, cnt, found, q, t1, runc, ex, m;
    case (p)
      0: begin
        run = 1;
        for (j = 1; j <= NT; j++) begin
          run = kand(run, A(t + j - 1));
          if (run != 1) break;
          if (B(t + j) == 1) return 1;
        end
        return 0;
      end
      3: begin
        run = 1;
        for (j = 1; j <= NT; j++) begin
          run = kand(run, kand(A(t + 2 * j - 2), B(t + 2 * j - 1)));
          if (run != 1) break;
          if (C(t + 2 * j) == 1) return 1;
        end
        return 0;
      end
      4: begin
        for (j = 0; j <= 2; j++) if (kand(A(t), B(t + j)) == 1) return 1;
        return 0;
      end
      5: begin
        found = 0;
        for (k = t; k < NT; k++) if (A(k) == 1) begin
          found++;
          if (B(k + 1) == 1) return 1;
          if (found == 2) break;
        end
        return 0;
      end
      6: begin
        cnt = 0;
        for (e = t; e < NT; e++) begin
          if (B(e) == 1) cnt++;
          if (cnt >= 3) break;
          if (cnt >= 1 && C(e + 1) == 1) return 1;
        end
        return 0;
      end
      8: begin
        for (j = 0; j <= 2; j++) begin
          m = (j == 0) ? B(t) : (j == 1) ? kand(A(t), B(t + 1)) : kand(kand(A(t), A(t + 1)), B(t + 2));
          if (m == 1) return 1;
        end
        return 0;
      end
      9: return kand(kor(A(t), B(t)), C(t + 1)) == 1;
      10: return kand(A(t), B(t)) == 1;
      15: begin
        for (k = 2; k <= 6; k++) begin
          runc = 1;
          for (q = 0; q < k; q++) runc = kand(runc, C(t + q));
          ex = 0;
          for (t1 = t; t1 <= t + k - 2; t1++) ex = kor(ex, kand(A(t1), B(t1 + 1)));
          if (kand(runc, ex) == 1) return 1;
        end
        return 0;
      end
      default: return A(t) == 1;
    endcase
  endfunction
  // cover: tick of the first match of the attempt at t (full trace), or -1
  function automatic int cover_end(int p, int t);
    int run, j, q, m;
    case (p)
      12: begin   // (a ##[1:$] b) intersect (c[*2:5]): a at t, b at t + j, c at t .. t + j
        for (j = 1; j <= 4; j++) begin
          m = kand(A(t), B(t + j));
          for (q = 0; q <= j; q++) m = kand(m, C(t + q));
          if (m == 1) return t + j;
        end
      end
      13: begin   // a[*2:$] ##1 b
        run = 1;
        for (j = 1; j < NT; j++) begin
          run = kand(run, A(t + j - 1));
          if (run != 1) break;
          if (j >= 2 && B(t + j) == 1) return t + j;
        end
      end
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
