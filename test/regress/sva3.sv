// Concurrent assertions over until / until_with / s_until / s_until_with,
// implies, iff, eventually[m:n], always[m:n], not, followed-by (#-# and #=#),
// strong() and weak() sequences, nested in implications, with disable iff,
// driven by a fixed 60-cycle stimulus; one cover of an implication.
// Expected values: IEEE 1800-2017 16.12.9 (until family), 16.12.? (implies, iff),
// 16.12.11 (always / eventually / s_eventually with ranges), 16.12.14 (not),
// 16.12.? (#-#, #=#), 16.9.? (strong/weak), 16.12.? (|-> |=>), 16.12 (disable iff),
// 16.14.3 (cover property counts non-vacuous successes), 16.9.3 (sampling).
//
// Reference model.  The values of a, b, c, d, rst sampled at each posedge are
// recorded (tick k is at time 5 + 10 k).  For each property a function
// computes, for an attempt starting at tick t and a trace prefix of L ticks,
// the three-valued verdict (0 = false for every continuation, 1 = true for
// every continuation, 2 = undecided) with Kleene logic over the samples that
// are not yet known; until-type operators are unrolled over the trace.
// The tick at which an attempt first gets a verdict is the tick of its
// pass/fail action; an attempt is dropped if rst was sampled high in any tick
// from its start up to that tick (disable iff).  Attempts undecided at the
// last tick are not reported.  Expected per property:
//   - number of fail actions at each tick: exactly the attempts with verdict 0;
//   - pass actions: never earlier than the first possible verdict, at most one
//     per attempt, and, for properties with an antecedent (|-> / |=>), at least
//     all attempts whose antecedent matched (non-vacuous passes) by the tick of
//     their verdict.  Vacuous passes (and passes of properties without an
//     antecedent, whose vacuity is not modelled) may be reported or not, so the
//     model does not depend on whether the pass action runs for vacuous success;
//   - cover property (a |-> b): one action at each tick with a and b both true.
module sva3;
  logic clk = 0, a = 0, b = 0, c = 0, d = 0, rst = 0;
  always #5 clk = ~clk;
  localparam [59:0] PA = 60'b001100110011100010000101111110100010111111101010100110011010, PB = 60'b100111000111001000001110011101111011011111011010011111100011, PC = 60'b100110000001000001110010100001011001001000001101000001001000, PD = 60'b010111111111010001100111001000001001001001001011010000001001, PR = 60'b000000000000000000000001100000000000000000000000000000000000;
  integer i;
  initial begin for (i = 0; i < 60; i++) begin @(negedge clk); a = PA[i]; b = PB[i]; c = PC[i]; d = PD[i]; rst = PR[i]; end check_all; $finish; end

  localparam int NT = 60;       // number of ticks
  localparam int NP = 19;       // P0 .. P18 (P5 and P8 do not exist)
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

  P0: assert property (@(posedge clk) disable iff (rst) a |-> b until c) ok(0); else bad(0);
  P1: assert property (@(posedge clk) disable iff (rst) a |-> b until_with c) ok(1); else bad(1);
  P2: assert property (@(posedge clk) disable iff (rst) a |-> (b ##1 c) until d) ok(2); else bad(2);
  P3: assert property (@(posedge clk) disable iff (rst) a implies b) ok(3); else bad(3);
  P4: assert property (@(posedge clk) disable iff (rst) (a |-> b) iff (c |-> d)) ok(4); else bad(4);
  P6: assert property (@(posedge clk) disable iff (rst) a |-> eventually[1:3] c) ok(6); else bad(6);
  P7: assert property (@(posedge clk) disable iff (rst) always[0:2] (a |-> b)) ok(7); else bad(7);
  P9: assert property (@(posedge clk) disable iff (rst) a ##1 b |-> always[1:3] c) ok(9); else bad(9);
  P10: assert property (@(posedge clk) disable iff (rst) a |=> b s_until c) ok(10); else bad(10);
  P11: assert property (@(posedge clk) disable iff (rst) a |-> b s_until_with c) ok(11); else bad(11);
  P12: assert property (@(posedge clk) disable iff (rst) not (a ##1 b)) ok(12); else bad(12);
  P13: assert property (@(posedge clk) disable iff (rst) a |-> not (b until c)) ok(13); else bad(13);
  P14: assert property (@(posedge clk) disable iff (rst) (a ##1 b) #-# c) ok(14); else bad(14);
  P15: assert property (@(posedge clk) disable iff (rst) (a ##1 b) #=# c) ok(15); else bad(15);
  P16: assert property (@(posedge clk) disable iff (rst) a ##1 b |-> strong(c ##1 d)) ok(16); else bad(16);
  P17: assert property (@(posedge clk) disable iff (rst) a |-> weak(b ##[1:2] c)) ok(17); else bad(17);
  P18: cover property (@(posedge clk) disable iff (rst) a |-> b) ok(18);

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

  // until loop from tick t: b until c (with_ = 0) or b until_with c (with_ = 1)
  function automatic int until_loop(int t, bit with_);
    int bi, ci;
    for (int k = t; k < NT; k++) begin
      if (with_) begin
        bi = B(k); if (bi != 1) return bi;          // 0: false, 2: undecided
        ci = C(k); if (ci != 0) return ci;          // 1: true, 2: undecided
      end else begin
        ci = C(k); if (ci != 0) return ci;
        bi = B(k); if (bi != 1) return bi;
      end
    end
    return 2;
  endfunction
  // verdict of property p for the attempt starting at tick t, prefix gL
  function automatic int verdict(int p, int t);
    int m, r, p1, q1;
    case (p)
      0: return kor(knot(A(t)), until_loop(t, 0));
      1: return kor(knot(A(t)), until_loop(t, 1));
      2: begin   // a |-> (b ##1 c) until d : (b ##1 c) must match at every tick before the first d
        r = 2;
        for (int k = t; k < NT; k++) begin
          int dv = D(k);
          if (dv != 0) begin r = dv; break; end
          m = kand(B(k), C(k + 1));
          if (m != 1) begin r = m; break; end
        end
        return kor(knot(A(t)), r);
      end
      3: return kor(knot(A(t)), B(t));
      4: begin   // (a |-> b) iff (c |-> d)
        p1 = kor(knot(A(t)), B(t)); q1 = kor(knot(C(t)), D(t));
        return kor(kand(p1, q1), kand(knot(p1), knot(q1)));
      end
      6: return kor(knot(A(t)), kor(C(t + 1), kor(C(t + 2), C(t + 3))));
      7: begin   // always[0:2] (a |-> b)
        r = 1;
        for (int j = 0; j <= 2; j++) r = kand(r, kor(knot(A(t + j)), B(t + j)));
        return r;
      end
      9: begin   // a ##1 b |-> always[1:3] c, evaluated from the end tick t + 1
        m = kand(A(t), B(t + 1));
        return kor(knot(m), kand(C(t + 2), kand(C(t + 3), C(t + 4))));
      end
      10: return kor(knot(A(t)), until_loop(t + 1, 0));
      11: return kor(knot(A(t)), until_loop(t, 1));
      12: return knot(kand(A(t), B(t + 1)));
      13: return kor(knot(A(t)), knot(until_loop(t, 0)));
      14: return kand(kand(A(t), B(t + 1)), C(t + 1));
      15: return kand(kand(A(t), B(t + 1)), C(t + 2));
      16: begin
        m = kand(A(t), B(t + 1));
        return kor(knot(m), kand(C(t + 1), D(t + 2)));
      end
      17: return kor(knot(A(t)), kand(B(t), kor(C(t + 1), C(t + 2))));
    endcase
    return 2;
  endfunction
  // was the antecedent matched (decided on the full trace)? -1: no antecedent
  // (vacuity not modelled)
  function automatic int nonvac(int p, int t);
    case (p)
      0, 1, 2, 6, 10, 11, 13, 17: return A(t) == 1;
      9, 16: return kand(A(t), B(t + 1)) == 1;
      default: return 0;
    endcase
  endfunction
  function automatic bit dis(int t, int e);    // rst sampled in [t, e]
    for (int k = t; k <= e; k++) if (sm[4][k]) return 1;
    return 0;
  endfunction

  task automatic check_all;
    int e_fail[NT], e_lo[NT], e_hi[NT];
    if (ntick < NT) begin $error("only %0d ticks sampled", ntick); errors++; end
    for (int p = 0; p < NP; p++) begin
      for (int k = 0; k < NT; k++) begin e_fail[k] = 0; e_lo[k] = 0; e_hi[k] = 0; end
      if (p != 18) begin
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
        // cover property (a |-> b): a hit at each tick where a and b are both true
        for (int t = 0; t < NT; t++) begin
          gL = NT;
          if (kand(A(t), B(t)) == 1 && !dis(t, t)) e_hi[t]++;
        end
        for (int k = 0; k < NT; k++)
          if (o_pass[p][k] != e_hi[k]) begin
            $error("P%0d cover tick %0d (time %0d): %0d hits, expected %0d", p, k, 5 + 10 * k, o_pass[p][k], e_hi[k]);
            errors++;
          end
      end
    end
    if (errors == 0) $display("PASSED");
  endtask
endmodule
