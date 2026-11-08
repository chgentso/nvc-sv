// Sequence and property local variables (byte a assigned with (v, a = d))
// used in goto / non-consecutive repetition, within, throughout, delays,
// nexttime, always / s_eventually ranges, until / until_with, implies, iff,
// or, accept_on / reject_on and disable iff, with covers and assertions on
// a pseudo-random stimulus.
// Expected values: IEEE 1800-2017 16.8 / 16.10 (local variables: assigned
// when the sequence item matches, with the sampled value, and seen by the
// rest of that thread), 16.9.1-16.9.10 (##, [->], [=], throughout, within),
// 16.12 (|-> |=>, nexttime, always, s_eventually, until, until_with,
// implies, iff, or, disable iff, accept_on / reject_on 16.12.14), 16.14.3
// (cover property of a sequence: one action per attempt at its first
// match), 11.8 (unsigned context: d != a + 1 zero-extends the byte), 16.9.3.
//
// Reference model: the values of v, w, d sampled at each of the 100 posedges
// (tick k at time 5 + 10 k) are recorded.  For the covers the first match of
// each attempt is computed directly on the trace.  For the assertions a
// three-valued (Kleene) verdict function over trace prefixes gives, for each
// attempt, the tick at which the pass or fail action runs (first prefix with
// a verdict; accept_on / reject_on: the first tick whose sampled d is 0x80
// aborts with pass / fail, before the inner property is looked at; disable
// iff (d == 8'h55): attempts are dropped if that held in any tick from the
// start to the verdict).  Fail actions must match the model exactly per tick;
// pass actions cumulatively between all non-vacuous passes (antecedent
// matched) and all passes (vacuous passes may or may not be reported, and
// the passes caused by accept_on are not required).  Event ticks are taken
// as time / 10 so that an action run at the preceding negedge (asynchronous
// abort) or at the tick counts for the same tick.
module svalocal3;
  logic clk = 0;
  logic [7:0] d = 0;
  logic v = 0, w = 0;
  logic [15:0] lfsr = 16'hACE1;
  localparam int NT = 100;
  localparam int NID = 27;
  int hits [NID][NT];       // [id][tick]
  int errors = 0;
  task automatic hit(int i);
    int k = $time / 10;
    if (k >= 0 && k < NT) hits[i][k]++;
  endtask
  always #5 clk = ~clk;
  always @(negedge clk) begin
    lfsr <= {lfsr[14:0], lfsr[15] ^ lfsr[13] ^ lfsr[12] ^ lfsr[10]};
    v <= lfsr[0]; w <= lfsr[1] | lfsr[2]; d <= lfsr[10:3];
  end
  // samples
  int sv[NT], sw[NT], sd[NT];
  int ntick = 0;
  always @(posedge clk) begin
    if (ntick < NT) begin sv[ntick] = v; sw[ntick] = w; sd[ntick] = d; end
    ntick++;
  end

  sequence g1;
    byte a;
    (v, a = d) ##1 w[->2] ##1 (d[1:0] == a[1:0]);
  endsequence
  P0: cover property (@(posedge clk) g1) hit(0);
  sequence g2;
    byte a;
    (v, a = d) ##1 w[=2] ##1 (d[1:0] == a[1:0]);
  endsequence
  P1: cover property (@(posedge clk) g2) hit(1);
  sequence g3;
    byte a, b;
    (v, a = d) ##2 (w, b = d) ##0 1;
  endsequence
  sequence g4;
    byte a;
    ((v, a = d) ##3 (d[0] == a[0])) within (1 ##5 1);
  endsequence
  P2: cover property (@(posedge clk) g4) hit(2);
  sequence g5;
    byte a;
    (v, a = d) ##1 1[*2:3] ##1 (d[2:0] == a[2:0]) ;
  endsequence
  P3: assert property (@(posedge clk) g5 |-> nexttime w) hit(3); else hit(4);
  property q1;
    byte a;
    (v, a = d) |-> always [1:2] (d != a + 1);
  endproperty
  P4: assert property (@(posedge clk) q1) hit(5); else hit(6);
  property q2;
    byte a;
    (v, a = d) |-> s_eventually [1:3] (d[1:0] == a[1:0]);
  endproperty
  P5: assert property (@(posedge clk) q2) hit(7); else hit(8);
  property q3;
    byte a;
    (v, a = d) |=> (d[0] == a[0]) until (w);
  endproperty
  P6: assert property (@(posedge clk) q3) hit(9); else hit(10);
  property q4;
    byte a;
    (v, a = d) |=> (d[0] == a[0]) until_with (w);
  endproperty
  P7: assert property (@(posedge clk) q4) hit(11); else hit(12);
  property q5;
    byte a;
    (v, a = d) |=> ((d[0] == a[0]) implies (w));
  endproperty
  P8: assert property (@(posedge clk) q5) hit(13); else hit(14);
  property q6;
    byte a;
    (v, a = d) |=> ((d[0] == a[0]) iff (w));
  endproperty
  P9: assert property (@(posedge clk) q6) hit(15); else hit(16);
  property q7;
    byte a;
    (v, a = d) |=> ((d[0] == a[0]) or (w ##1 (d[1] == a[1])));
  endproperty
  P10: assert property (@(posedge clk) disable iff (d == 8'h55) q7) hit(17); else hit(18);
  property q8;
    byte a;
    accept_on (d == 8'h80) ((v, a = d) |-> ##2 (d[1:0] == a[1:0]));
  endproperty
  P11: assert property (@(posedge clk) q8) hit(19); else hit(20);
  property q9;
    byte a;
    reject_on (d == 8'h80) ((v, a = d) |-> ##2 (d[1:0] == a[1:0]));
  endproperty
  P12: assert property (@(posedge clk) q9) hit(21); else hit(22);
  property q10;
    byte a;
    (v, a = d) |-> nexttime [2] (d[2:0] == a[2:0]);
  endproperty
  P13: assert property (@(posedge clk) q10) hit(23); else hit(24);
  sequence g6;
    byte a;
    w throughout ((v, a = d) ##1 (d[0] == a[0]) ##1 1);
  endsequence
  P14: cover property (@(posedge clk) g6) hit(25);
  sequence g7;
    byte a;
    (d[3]) throughout ((v, a = d) ##1 (d[0] == a[0]) ##1 1[*2]);
  endsequence
  P15: cover property (@(posedge clk) g7) hit(26);

  initial begin
    #1000;
    check_all;
    $finish;
  end

  // ---------------- reference model ----------------
  int gL;
  function automatic int SV(int k); return (k >= gL || k >= NT) ? 2 : sv[k]; endfunction
  function automatic int SW(int k); return (k >= gL || k >= NT) ? 2 : sw[k]; endfunction
  function automatic int SD(int k); return (k >= gL || k >= NT) ? -1 : sd[k]; endfunction
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
  function automatic int knot(int x); return x == 2 ? 2 : 1 - x; endfunction
  // (d at tick i)[mask bits] == (a = d at tick j)[mask bits]
  function automatic int ceq(int i, int j, int mask);
    if (SD(i) < 0 || SD(j) < 0) return 2;
    return ((SD(i) ^ SD(j)) & mask) == 0;
  endfunction
  // d at tick i != a + 1 with a = d at tick j, unsigned 32-bit arithmetic
  function automatic int neq1(int i, int j);
    if (SD(i) < 0 || SD(j) < 0) return 2;
    return SD(i) != SD(j) + 1;
  endfunction
  // (cond(k)) until w / until_with w from tick s, cond = d[0] == a[0], a = d at tick t
  function automatic int until_loop(int s, int t, bit with_);
    int cd, wk;
    for (int k = s; k < NT; k++) begin
      if (with_) begin
        cd = ceq(k, t, 1); if (cd != 1) return cd;
        wk = SW(k); if (wk != 0) return wk;
      end else begin
        wk = SW(k); if (wk != 0) return wk;
        cd = ceq(k, t, 1); if (cd != 1) return cd;
      end
    end
    return 2;
  endfunction

  // verdict of assertion p (3..13) for the attempt at tick t with prefix gL
  function automatic int verdict(int p, int t);
    int m3, m4, vt, c1, c2, c3;
    vt = SV(t);
    case (p)
      3: begin
        m3 = kand(vt, ceq(t + 3, t, 7));
        m4 = kand(vt, ceq(t + 4, t, 7));
        return kand(kor(knot(m3), SW(t + 4)), kor(knot(m4), SW(t + 5)));
      end
      4: return kor(knot(vt), kand(neq1(t + 1, t), neq1(t + 2, t)));
      5: return kor(knot(vt), kor(ceq(t + 1, t, 3), kor(ceq(t + 2, t, 3), ceq(t + 3, t, 3))));
      6: return kor(knot(vt), until_loop(t + 1, t, 0));
      7: return kor(knot(vt), until_loop(t + 1, t, 1));
      8: return kor(knot(vt), kor(knot(ceq(t + 1, t, 1)), SW(t + 1)));
      9: begin
        c1 = ceq(t + 1, t, 1); c2 = SW(t + 1);
        return kor(knot(vt), kor(kand(c1, c2), kand(knot(c1), knot(c2))));
      end
      10: return kor(knot(vt), kor(ceq(t + 1, t, 1), kand(SW(t + 1), ceq(t + 2, t, 2))));
      11, 12: return kor(knot(vt), ceq(t + 2, t, 3));
      13: return kor(knot(vt), ceq(t + 2, t, 7));
    endcase
    return 2;
  endfunction
  function automatic int nonvac(int p, int t);
    gL = NT;
    case (p)
      3: return kand(SV(t), ceq(t + 3, t, 7)) == 1 || kand(SV(t), ceq(t + 4, t, 7)) == 1;
      8, 11, 12: return 0;     // implies / accept_on: vacuity of the inner operator not modelled
      default: return SV(t) == 1;
    endcase
  endfunction
  function automatic bit dis(int p, int t, int e);
    if (p != 10) return 0;
    for (int k = t; k <= e; k++) if (sd[k] == 8'h55) return 1;
    return 0;
  endfunction
  // result of attempt (p, t): v = 0/1/2, tick dk
  function automatic void attempt(int p, int t, output int v, output int dk);
    v = 2; dk = -1;
    for (int k = t; k < NT; k++) begin
      if ((p == 11 || p == 12) && sd[k] == 8'h80) begin v = (p == 11); dk = k; return; end
      gL = k + 1;
      v = verdict(p, t);
      if (v != 2) begin dk = k; return; end
    end
  endfunction

  // first match tick of the cover attempt starting at t, -1 if none
  function automatic int cover_first(int id, int t);
    int a, i1, i2, i3, e;
    gL = NT;
    case (id)
      0, 1: begin
        if (!sv[t]) return -1;
        a = sd[t];
        i1 = -1; i2 = -1; i3 = -1;
        for (int k = t + 1; k < NT; k++) if (sw[k]) begin
          if (i1 < 0) i1 = k; else if (i2 < 0) i2 = k; else begin i3 = k; break; end
        end
        if (i2 < 0) return -1;
        if (id == 0) begin
          e = i2 + 1;
          if (e < NT && ((sd[e] ^ a) & 3) == 0) return e;
          return -1;
        end
        // w[=2]: the sequence may end at x in [i2, i3 - 1], compare at x + 1
        for (int x = i2; x < NT - 1 && (i3 < 0 || x <= i3 - 1); x++)
          if (((sd[x + 1] ^ a) & 3) == 0) return x + 1;
        return -1;
      end
      2: begin
        if (t + 5 >= NT) return -1;
        for (int t1 = t; t1 <= t + 2; t1++)
          if (sv[t1] && ((sd[t1 + 3] ^ sd[t1]) & 1) == 0) return t + 5;
        return -1;
      end
      25: begin
        if (t + 2 >= NT) return -1;
        if (sw[t] && sw[t + 1] && sw[t + 2] && sv[t] && ((sd[t + 1] ^ sd[t]) & 1) == 0) return t + 2;
        return -1;
      end
      26: begin
        if (t + 3 >= NT) return -1;
        if (sv[t] && ((sd[t + 1] ^ sd[t]) & 1) == 0 && ((sd[t] >> 3) & 1) && ((sd[t + 1] >> 3) & 1)
            && ((sd[t + 2] >> 3) & 1) && ((sd[t + 3] >> 3) & 1)) return t + 3;
        return -1;
      end
    endcase
    return -1;
  endfunction

  task automatic check_all;
    int e_fail[NT], e_lo[NT], e_hi[NT];
    int pass_id, fail_id;
    if (ntick < NT) begin $error("only %0d ticks sampled", ntick); errors++; end
    // covers
    foreach (e_hi[k]) e_hi[k] = 0;
    begin
      int cov_ids[6] = '{0, 1, 2, 25, 26, -1};
      for (int ci = 0; ci < 5; ci++) begin
        int id = cov_ids[ci];
        for (int k = 0; k < NT; k++) e_hi[k] = 0;
        for (int t = 0; t < NT; t++) begin
          int f = cover_first(id, t);
          if (f >= 0) e_hi[f]++;
        end
        for (int k = 0; k < NT; k++)
          if (hits[id][k] != e_hi[k]) begin
            $error("cover id %0d tick %0d (time %0d): %0d hits, expected %0d", id, k, 5 + 10 * k, hits[id][k], e_hi[k]);
            errors++;
          end
      end
    end
    // assertions P3..P13: ids 2p-3 (pass), 2p-2 (fail)
    for (int p = 3; p <= 13; p++) begin
      int cum_lo = 0, cum_hi = 0, cum_op = 0;
      pass_id = 2 * p - 3; fail_id = 2 * p - 2;
      for (int k = 0; k < NT; k++) begin e_fail[k] = 0; e_lo[k] = 0; e_hi[k] = 0; end
      for (int t = 0; t < NT; t++) begin
        int v, dk;
        attempt(p, t, v, dk);
        if (v == 2 || dis(p, t, dk)) continue;
        if (v == 0) e_fail[dk]++;
        else begin
          e_hi[dk]++;
          if (nonvac(p, t)) e_lo[dk]++;
        end
      end
      for (int k = 0; k < NT; k++) begin
        if (hits[fail_id][k] != e_fail[k]) begin
          $error("P%0d tick %0d (time %0d): %0d fail actions, expected %0d", p, k, 5 + 10 * k, hits[fail_id][k], e_fail[k]);
          errors++;
        end
        cum_lo += e_lo[k]; cum_hi += e_hi[k]; cum_op += hits[pass_id][k];
        if (cum_op < cum_lo || cum_op > cum_hi) begin
          $error("P%0d up to tick %0d: %0d pass actions, allowed %0d..%0d", p, k, cum_op, cum_lo, cum_hi);
          errors++;
        end
      end
    end
    if (errors == 0) $display("PASSED");
  endtask
endmodule
