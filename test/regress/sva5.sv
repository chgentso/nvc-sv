`timescale 1ns/1ns
// Sampled value functions inside assertions: $rose, $fell, $stable, $changed,
// $past (with and without a tick count), $sampled, on single bits and on vectors,
// driven by a fixed 60-cycle stimulus.
// Expected values: IEEE 1800-2017 16.9.3 (sampled value functions: $rose / $fell look at
// the least significant bit, $stable / $changed at the whole expression, all compared
// with the value sampled at the previous clock tick, $past(e, n) is the value n ticks
// earlier; before the first tick the value is the initial value of the variable,
// 16.5.1), 16.12.7 (|-> and |=>), 16.15 (disable iff), 16.14.1 (assertion actions; the
// pass action also runs for a vacuous success), 16.14.3 (cover property).
//
// Reference model: the values of a, b, c, d, rst sampled at each posedge are
// recorded (tick k is at time 5 + 10 k; the sample before tick 0 is the initial
// value 0 of every signal).  For every property a function computes, for an attempt
// starting at tick t and a trace prefix of L ticks, the three-valued verdict (0 = already
// false, 1 = already true, 2 = undecided) with Kleene logic; the tick at which an
// attempt first gets a verdict is the tick of its pass or fail action, and an attempt is
// dropped by disable iff if rst was sampled high in any tick from its start up to that
// tick.  Expected per property:
//   - fail actions at each tick: exactly the attempts with verdict 0;
//   - pass actions: never more than the attempts with verdict 1 and at least the
//     non-vacuous ones (antecedent true) by the tick of their verdict, and in total
//     (parameter VAC = 1) also every vacuous success decided before the last TAIL
//     ticks (16.14.1);
//   - P9 (cover property of a sequence): one action per attempt at its first match.
module sva5;
  // VAC = 1: the pass action of a vacuous success runs (16.14.1); 0 is only used
  // to look at the remaining checks separately from that rule.
  parameter bit VAC = 1;
  logic clk = 0, a = 0, b = 0, c = 0, d = 0, rst = 0;
  always #5 clk = ~clk;
  localparam [59:0] PA = 60'b110100001101000011010001000000001100001101100101101011111011, PB = 60'b001011011101000001111101101001101101110001100010011110001100, PC = 60'b110100000000000101001100000000010100000011100011100011100011, PD = 60'b110011111111111101110001110101110001000010000010100111100010, PR = 60'b000000000000000000000001100000000000000000000000000000000000;
  integer i;
  initial begin for (i = 0; i < 60; i++) begin @(negedge clk); a = PA[i]; b = PB[i]; c = PC[i]; d = PD[i]; rst = PR[i]; end check_all; $finish; end

  localparam int NT = 60;       // number of ticks
  localparam int NP = 13;       // P0 .. P12
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

  P0: assert property (@(posedge clk) disable iff (rst) $rose(a) |-> b) ok(0); else bad(0);
  P1: assert property (@(posedge clk) disable iff (rst) $fell(a) |=> c) ok(1); else bad(1);
  P2: assert property (@(posedge clk) disable iff (rst) $stable(b) |-> d) ok(2); else bad(2);
  P3: assert property (@(posedge clk) disable iff (rst) $changed(c) |=> a) ok(3); else bad(3);
  P4: assert property (@(posedge clk) disable iff (rst) a |-> $past(b)) ok(4); else bad(4);
  P5: assert property (@(posedge clk) disable iff (rst) a |-> $past(c, 2) == d) ok(5); else bad(5);
  P6: assert property (@(posedge clk) disable iff (rst) b |=> $past(a, 3)) ok(6); else bad(6);
  P7: assert property (@(posedge clk) disable iff (rst) $rose(a) && $rose(b) |-> c) ok(7); else bad(7);
  P8: assert property (@(posedge clk) disable iff (rst) ($past(a) && $past(b)) |-> c) ok(8); else bad(8);
  P9: cover property (@(posedge clk) disable iff (rst) $rose(c) ##1 $fell(c)) ok(9);
  P10: assert property (@(posedge clk) disable iff (rst) $sampled(a) |-> b) ok(10); else bad(10);
  P11: assert property (@(posedge clk) disable iff (rst) $rose({a,b}) |-> c) ok(11); else bad(11);
  P12: assert property (@(posedge clk) disable iff (rst) $stable({a,b,c}) |=> d) ok(12); else bad(12);
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

  function automatic bit is_assert(int p); return p != 9; endfunction
  function automatic bit use_dis(int p); return 1; endfunction
  function automatic string pname(int p); return $sformatf("P%0d", p); endfunction

  function automatic int rose(int sig, int t); return kand(S(sig, t), knot(S(sig, t - 1))); endfunction
  function automatic int fell(int sig, int t); return kand(knot(S(sig, t)), S(sig, t - 1)); endfunction
  function automatic int stable(int sig, int t); return keq(S(sig, t), S(sig, t - 1)); endfunction

  // antecedent value (three-valued) of the implication of property p at tick t
  function automatic int ante(int p, int t);
    case (p)
      0: return rose(0, t);
      1: return fell(0, t);
      2: return stable(1, t);
      3: return knot(stable(2, t));
      7: return kand(rose(0, t), rose(1, t));
      8: return kand(A(t - 1), B(t - 1));
      10: return A(t);
      11: return rose(1, t);          // the least significant bit of {a, b} is b
      12: return kand(stable(0, t), kand(stable(1, t), stable(2, t)));
      default: return A(t);          // 4, 5, 6 (b for 6, see below)
    endcase
  endfunction

  // verdict of property p for the attempt starting at tick t, prefix gL
  function automatic int verdict(int p, int t);
    case (p)
      0: return kor(knot(ante(p, t)), B(t));
      1: return kor(knot(ante(p, t)), C(t + 1));
      2: return kor(knot(ante(p, t)), D(t));
      3: return kor(knot(ante(p, t)), A(t + 1));
      4: return kor(knot(A(t)), B(t - 1));
      5: return kor(knot(A(t)), keq(C(t - 2), D(t)));
      6: return kor(knot(B(t)), (D(t + 1) == 2 ? 2 : A(t - 2)));   // $past(a, 3) is evaluated at tick t + 1
      7: return kor(knot(ante(p, t)), C(t));
      8: return kor(knot(ante(p, t)), C(t));
      10: return kor(knot(ante(p, t)), B(t));
      11: return kor(knot(ante(p, t)), C(t));
      12: return kor(knot(ante(p, t)), D(t + 1));
    endcase
    return 2;
  endfunction
  // was the antecedent matched (decided on the full trace)?
  function automatic int nonvac(int p, int t);
    case (p)
      6: return B(t) == 1;
      4, 5: return A(t) == 1;
      default: return ante(p, t) == 1;
    endcase
  endfunction
  // cover: tick of the first match of the attempt at t (full trace), or -1
  function automatic int cover_end(int p, int t);
    if (p == 9 && kand(rose(2, t), fell(2, t + 1)) == 1) return t + 1;
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
