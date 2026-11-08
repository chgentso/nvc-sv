`timescale 1ns/1ns
// Named sequences and properties with arguments (positional and named),
// default clocking and default disable iff, property instances in implications,
// not, an explicit disable iff (1'b0) that overrides the default, and a cover of a
// sequence concatenation, driven by a fixed 70-cycle stimulus.
// Expected values: IEEE 1800-2017 16.8 (sequence declarations and arguments), 16.12.1
// (property declarations and instances), 16.9.1 (##, sequence concatenation), 16.9.2
// ([*]), 16.12.7 / 16.12.8 (|-> and |=>), 16.12.3 (not), 14.12 (default clocking),
// 16.15 (default disable iff, explicit disable iff overrides it), 16.14.1 (assertion
// actions; the pass action also runs for a vacuous success), 16.14.3 (cover
// property), 16.9.3 / 16.5.1 (sampling).
//
// Reference model: the values of a, b, c, d, rst sampled at each posedge are
// recorded (tick k is at time 5 + 10 k).  For every property a function computes,
// for an attempt starting at tick t and a trace prefix of L ticks, the three-valued
// verdict (0 = already false for every continuation, 1 = already true, 2 = undecided)
// with Kleene logic.  The tick at which an attempt first gets a verdict is the tick of
// its pass or fail action; an attempt is dropped by disable iff if rst was sampled high
// in any tick from its start up to that tick (not for A7, whose disable iff (1'b0)
// replaces the default).  Expected per property:
//   - fail actions at each tick: exactly the attempts with verdict 0;
//   - pass actions: never more than the attempts with verdict 1 and at least the
//     non-vacuous ones by the tick of their verdict, and in total (parameter VAC = 1)
//     also every vacuous success decided before the last TAIL ticks (16.14.1);
//   - C0 (cover property of a sequence): one action per attempt at its first match.
module sva4;
  // VAC = 1: the pass action of a vacuous success runs (16.14.1); 0 is only used
  // to look at the remaining checks separately from that rule.
  parameter bit VAC = 1;
  logic clk = 0, a = 0, b = 0, c = 0, d = 0, rst = 0;
  always #5 clk = ~clk;
  localparam [69:0] PA = 70'b1010001000011000100001000011001000100001111111000011111001010110011111, PB = 70'b0011001111101100100100111001110111110000000010110011100111110110000100, PC = 70'b1000001000101111001111100011100010010110101000100110011101111000010101, PD = 70'b0110010101101110000001011000000100010101110011100010000010011000010010, PR = 70'b0000000000000000000000000000001100000000000000000000000000000000000000;
  integer i;
  initial begin for (i = 0; i < 70; i++) begin @(negedge clk); a = PA[i]; b = PB[i]; c = PC[i]; d = PD[i]; rst = PR[i]; end check_all; $finish; end

  localparam int NT = 70;       // number of ticks
  localparam int NP = 10;       // A0 .. A8, C0 (index 9)

  default clocking cb @(posedge clk); endclocking
  default disable iff (rst);

  sequence s_ab; a ##1 b; endsequence
  sequence s_n(x, y, n); x ##n y; endsequence
  property p_imp(s, p); s |-> p; endproperty
  property p_resp(x, y); x |=> y; endproperty
  property p_own; @(posedge clk) disable iff (rst) a |-> ##1 b; endproperty
  property p_no(x); not (x ##1 x ##1 x); endproperty
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

  A0: assert property (a |-> b) ok(0); else bad(0);
  A1: assert property (s_ab |-> c) ok(1); else bad(1);
  A2: assert property (s_n(a, b, 2) |=> c) ok(2); else bad(2);
  A3: assert property (p_imp(s_ab, c ##1 d)) ok(3); else bad(3);
  A4: assert property (p_resp(.y(b), .x(a))) ok(4); else bad(4);
  A5: assert property (p_own) ok(5); else bad(5);
  A6: assert property (p_no(a)) ok(6); else bad(6);
  A7: assert property (@(posedge clk) disable iff (1'b0) a |=> b) ok(7); else bad(7);
  C0: cover property (s_ab ##1 s_n(c, d, 1)) ok(9);
  A8: assert property (a ##1 s_ab[*2] |-> c) ok(8); else bad(8);
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
  function automatic bit use_dis(int p); return p != 7; endfunction
  function automatic string pname(int p); return p == 9 ? "C0" : $sformatf("A%0d", p); endfunction

  // verdict of property p for the attempt starting at tick t, prefix gL
  function automatic int verdict(int p, int t);
    int m;
    case (p)
      0: return kor(knot(A(t)), B(t));
      1: begin m = kand(A(t), B(t + 1)); return kor(knot(m), C(t + 1)); end
      2: begin m = kand(A(t), B(t + 2)); return kor(knot(m), C(t + 3)); end
      3: begin m = kand(A(t), B(t + 1)); return kor(knot(m), kand(C(t + 1), D(t + 2))); end
      4: return kor(knot(A(t)), B(t + 1));
      5: return kor(knot(A(t)), B(t + 1));
      6: return knot(kand(A(t), kand(A(t + 1), A(t + 2))));   // not (a ##1 a ##1 a)
      7: return kor(knot(A(t)), B(t + 1));
      8: begin   // a ##1 (a ##1 b)[*2] = a ##1 a ##1 b ##1 a ##1 b, end t + 4
        m = kand(kand(A(t), A(t + 1)), kand(B(t + 2), kand(A(t + 3), B(t + 4))));
        return kor(knot(m), C(t + 4));
      end
    endcase
    return 2;
  endfunction
  // was the antecedent matched (decided on the full trace)?
  function automatic int nonvac(int p, int t);
    case (p)
      1: return kand(A(t), B(t + 1)) == 1;
      2: return kand(A(t), B(t + 2)) == 1;
      3: return kand(A(t), B(t + 1)) == 1;
      6: return 1;      // no antecedent: every success is a real one
      8: return kand(kand(A(t), A(t + 1)), kand(B(t + 2), kand(A(t + 3), B(t + 4)))) == 1;
      default: return A(t) == 1;
    endcase
  endfunction
  // cover: tick of the first match of the attempt at t (full trace), or -1
  function automatic int cover_end(int p, int t);
    if (p == 9 && kand(kand(A(t), B(t + 1)), kand(C(t + 2), D(t + 3))) == 1) return t + 3;
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
