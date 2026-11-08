`timescale 1ns/1ns
// Sequence and property local variables, assigned with match items such as
// (v, x = d) and (w, c++), in assertions and covers over a pseudo-random stimulus
// (LFSR): delays and ranges, repetition with an assignment, first_match, or, and,
// intersect, throughout, if / else, not, $past, sequence arguments, byte, shortint,
// longint and 4-state local variables.
// Expected values: IEEE 1800-2017 16.8.1 / 16.10 (local variables: assigned when the
// sequence item matches with the sampled values, seen by the rest of that thread, one
// copy per thread), 16.9.1 (##), 16.9.2 ([*]), 16.9.3 (sampling, $past), 16.9.5 (and),
// 16.9.6 (intersect), 16.9.7 (or), 16.9.8 (first_match), 16.9.9 (throughout),
// 16.12.6 (if / else), 16.12.3 (not), 16.12.7 / 16.12.8 (|-> and |=>), 16.14.1
// (assertion actions; the pass action also runs for a vacuous success), 16.14.3 (cover
// property: one action per attempt, at its first match), 11.8 (expression types and
// sizes: d == t + 1 with a byte t and an unsigned d is evaluated unsigned, so t is
// zero-extended; the shortint / int assignments are exact), 11.4.14 (replication
// {d, d, ...}).
//
// Reference model: the values of v, w, d, q sampled at each of the 100 posedges (tick k
// is at time 5 + 10 k) are recorded; $past(d) at tick k is d sampled at tick k - 1.
// For each assertion or cover attempt (one per tick) the model enumerates the threads
// of the sequence with their local variables on the recorded trace.  An assertion
// attempt fails at the first tick where one of its threads fails, and passes at
// the tick where its last thread ends; it is non-vacuous if the antecedent matched.
// A cover attempt reports once, at its first match.  Per id the action count at each
// tick is compared: fail and cover actions exactly; pass actions cumulatively between
// the non-vacuous passes and all passes, and in total (parameter VAC = 1) including the
// vacuous successes decided before the last TAIL ticks (16.14.1); the exact tick of a
// vacuous pass is not pinned down.
module svalocal1;
  // VAC = 1: the pass action of a vacuous success runs (16.14.1); 0 is only used
  // to look at the remaining checks separately from that rule.
  parameter bit VAC = 1;
  localparam int NT = 100;       // ticks (posedges 5 .. 995)
  localparam int NID = 25;
  localparam int TAIL = 6;
  logic clk = 0;
  logic [7:0] d = 0, q = 0;
  logic v = 0, w = 0;
  logic [15:0] lfsr = 16'hACE1;
  logic [7:0] p1 = 0, p2 = 0, p3 = 0;
  int hits [NID][NT];
  int errors = 0;
  task automatic hit(int i);
    int k = $time / 10;
    if (k >= 0 && k < NT) hits[i][k]++;
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

  property p0;
    logic [7:0] x;
    (v, x = d) |-> ##3 (q == x);
  endproperty
  P0: assert property (@(posedge clk) p0) hit(0); else hit(1);

  sequence s1;
    int n;
    (v, n = 1) ##1 (w, n = n + 1)[*1:3] ##1 (d[1:0] == n[1:0]);
  endsequence
  P1: cover property (@(posedge clk) s1) hit(2);
  P2: assert property (@(posedge clk) s1 |=> v) hit(3); else hit(4);

  property p3x;
    byte a, b;
    (w, a = d, b = d + 1) |=> ((d == a) or (d == b)) ##1 (d != b);
  endproperty
  P3: assert property (@(posedge clk) p3x) hit(5); else hit(6);

  sequence s4;
    int c;
    (v, c = 0) ##1 (w, c++)[*1:$] ##1 (!w && c == 2);
  endsequence
  P4: cover property (@(posedge clk) s4) hit(7);

  sequence s5;
    logic [3:0] y;
    v ##1 (w, y = d[3:0]) ##1 (y === d[3:0]);
  endsequence
  P5: cover property (@(posedge clk) s5) hit(8);

  sequence s6;
    shortint s;
    (v, s = -d) ##1 (s < 0);
  endsequence
  P6: cover property (@(posedge clk) s6) hit(9);

  sequence s8;
    int n;
    first_match(v ##[1:3] (w, n = d)) ##1 (d[1:0] == n[1:0]);
  endsequence
  P8: cover property (@(posedge clk) s8) hit(10);

  sequence s9;
    int a;
    ((v, a = 1) or (w, a = 2)) ##1 (d[1:0] == a);
  endsequence
  P9: cover property (@(posedge clk) s9) hit(11);

  property p11;
    byte t;
    (v, t = d) |-> if (t[0]) ##1 w else ##1 !w;
  endproperty
  P11: assert property (@(posedge clk) p11) hit(12); else hit(13);

  property p12;
    byte t;
    not ((v, t = d) ##1 (d == t + 1));
  endproperty
  P12: assert property (@(posedge clk) p12) hit(14); else hit(15);

  property p13;
    int t;
    (v, t = d) ##2 (w, t += d) |-> ##1 (d[3:0] == t[3:0]);
  endproperty
  P13: assert property (@(posedge clk) p13) hit(16); else hit(17);

  sequence s14;
    byte a, b;
    ((v, a = d) and (w, b = d)) ##1 (d[2:0] == a[2:0] || d[2:0] == b[2:0]);
  endsequence
  P14: cover property (@(posedge clk) s14) hit(18);

  sequence s15;
    byte a;
    ((v, a = d) ##2 (d[1:0] == a[1:0])) intersect (1 ##2 1);
  endsequence
  P15: cover property (@(posedge clk) s15) hit(19);

  property p16;
    byte a;
    (v ##[1:2] (w, a = d)) |=> ##1 (d[1:0] == a[1:0]);
  endproperty
  P16: assert property (@(posedge clk) p16) hit(20); else hit(21);

  sequence s17;
    longint L;
    (v, L = {d, d, d, d, d, d, d, d}) ##1 (L[63:56] == $past(d));
  endsequence
  P17: cover property (@(posedge clk) s17) hit(22);

  sequence s18;
    int a;
    w throughout ((v, a = d) ##2 (d[2:0] == a[2:0]));
  endsequence
  P18: cover property (@(posedge clk) s18) hit(23);

  sequence s19(int k);
    int a;
    (v, a = d + k) ##1 (d == a);
  endsequence
  P19: cover property (@(posedge clk) s19(1) ##1 s19(2)) hit(24);

  initial begin
    #1000;
    check_all;
    $finish;
  end

  // ---------------- reference model ----------------
  // samples (out-of-range reads give 0; a thread whose tick is outside the trace is unresolved)
  function automatic int V(int k); return (k >= 0 && k < NT) ? sv[k] : 0; endfunction
  function automatic int W(int k); return (k >= 0 && k < NT) ? sw[k] : 0; endfunction
  function automatic int D(int k); return (k >= 0 && k < NT) ? sd[k] : 0; endfunction
  function automatic int Q(int k); return (k >= 0 && k < NT) ? sq[k] : 0; endfunction
  function automatic int sgn(int x); return x >= 128 ? x - 256 : x; endfunction   // signed'(d)

  // threads of the current assertion attempt: resolution tick, verdict (1 pass, 0 fail), antecedent matched
  int th_t[16], th_v[16], th_nv[16], nth;
  function automatic void thr(int tick, int verdict, int nv);
    th_t[nth] = tick; th_v[nth] = (tick >= NT) ? 2 : verdict; th_nv[nth] = nv; nth++;
  endfunction
  // result of the attempt: 0 = fails (at rt), 1 = passes (at rt, nv = antecedent matched), 2 = unresolved
  function automatic int finish(output int rt, output int nv);
    int best = -1, mx = -1;
    nv = 0; rt = -1;
    for (int i = 0; i < nth; i++) if (th_v[i] == 0 && (best < 0 || th_t[i] < best)) best = th_t[i];
    if (best >= 0) begin rt = best; return 0; end
    for (int i = 0; i < nth; i++) begin
      if (th_v[i] == 2) return 2;
      if (th_t[i] > mx) mx = th_t[i];
      if (th_nv[i]) nv = 1;
    end
    rt = mx; return 1;
  endfunction

  // assertion items: m = 0 P0, 1 P2, 2 P3, 3 P11, 4 P12, 5 P13, 6 P16
  function automatic int attempt(int m, int t, output int rt, output int nv);
    int x, a, b, tt, j, i, ok, ex;
    nth = 0;
    case (m)
      0: begin   // P0: (v, x = d) |-> ##3 (q == x)
        if (!V(t)) thr(t, 1, 0);
        else begin x = D(t); thr(t + 3, Q(t + 3) == x, 1); end
      end
      1: begin   // P2: s1 |=> v, threads of j = 1..3 repetitions
        if (!V(t)) thr(t, 1, 0);
        else
          for (j = 1; j <= 3; j++) begin
            ok = 1;
            for (i = 1; i <= j; i++) if (ok && !W(t + i)) begin thr(t + i, 1, 0); ok = 0; end
            if (ok) begin
              if ((D(t + j + 1) & 3) != ((1 + j) & 3)) thr(t + j + 1, 1, 0);
              else thr(t + j + 2, V(t + j + 2), 1);
            end
          end
      end
      2: begin   // P3: (w, a = d, b = d + 1) |=> ((d == a) or (d == b)) ##1 (d != b)
        if (!W(t)) thr(t, 1, 0);
        else begin
          a = D(t); b = (D(t) + 1) & 255;
          if (!(D(t + 1) == a || D(t + 1) == b)) thr(t + 1, 0, 1);
          else thr(t + 2, D(t + 2) != b, 1);
        end
      end
      3: begin   // P11: (v, t = d) |-> if (t[0]) ##1 w else ##1 !w
        if (!V(t)) thr(t, 1, 0);
        else begin tt = D(t); thr(t + 1, (tt & 1) ? W(t + 1) : !W(t + 1), 1); end
      end
      4: begin   // P12: not ((v, t = d) ##1 (d == t + 1)); t is zero-extended (unsigned context)
        if (!V(t)) thr(t, 1, 1);
        else thr(t + 1, !(D(t + 1) == D(t) + 1), 1);
      end
      5: begin   // P13: (v, t = d) ##2 (w, t += d) |-> ##1 (d[3:0] == t[3:0])
        if (!V(t)) thr(t, 1, 0);
        else if (!W(t + 2)) thr(t + 2, 1, 0);
        else begin tt = D(t) + D(t + 2); thr(t + 3, (D(t + 3) & 15) == (tt & 15), 1); end
      end
      6: begin   // P16: (v ##[1:2] (w, a = d)) |=> ##1 (d[1:0] == a[1:0])
        if (!V(t)) thr(t, 1, 0);
        else
          for (j = 1; j <= 2; j++) begin
            if (!W(t + j)) thr(t + j, 1, 0);
            else thr(t + j + 2, (D(t + j + 2) & 3) == (D(t + j) & 3), 1);
          end
      end
    endcase
    return finish(rt, nv);
  endfunction

  // cover items: m = 0 P1, 1 P4, 2 P5, 3 P6, 4 P8, 5 P9, 6 P14, 7 P15, 8 P17, 9 P18, 10 P19;
  // tick of the first match of the attempt at t, or -1
  function automatic int cover_end(int m, int t);
    int j, jj;
    case (m)
      0: begin   // s1: (v, n = 1) ##1 (w, n = n + 1)[*1:3] ##1 (d[1:0] == n[1:0])
        if (V(t))
          for (j = 1; j <= 3; j++) begin
            bit allw = 1;
            for (int i = 1; i <= j; i++) if (!W(t + i)) allw = 0;
            if (!allw) break;
            if (t + j + 1 < NT && (D(t + j + 1) & 3) == ((1 + j) & 3)) return t + j + 1;
          end
      end
      1: if (t + 3 < NT && V(t) && W(t + 1) && W(t + 2) && !W(t + 3)) return t + 3;   // s4: c == 2 after two repetitions
      2: if (t + 2 < NT && V(t) && W(t + 1) && (D(t + 2) & 15) == (D(t + 1) & 15)) return t + 2;
      3: if (t + 1 < NT && V(t) && D(t) != 0) return t + 1;     // s6: s = -d (16 bits) is negative iff d != 0
      4: begin   // s8: first_match(v ##[1:3] (w, n = d)) ##1 (d[1:0] == n[1:0])
        if (V(t)) begin
          jj = 0;
          for (j = 1; j <= 3; j++) if (jj == 0 && W(t + j)) jj = j;
          if (jj != 0 && t + jj + 1 < NT && (D(t + jj + 1) & 3) == (D(t + jj) & 3)) return t + jj + 1;
        end
      end
      5: if (t + 1 < NT && ((V(t) && (D(t + 1) & 3) == 1) || (W(t) && (D(t + 1) & 3) == 2))) return t + 1;   // s9
      6: if (t + 1 < NT && V(t) && W(t) && (D(t + 1) & 7) == (D(t) & 7)) return t + 1;   // s14: a == b == d at t
      7: if (t + 2 < NT && V(t) && (D(t + 2) & 3) == (D(t) & 3)) return t + 2;           // s15
      8: if (t + 1 < NT && V(t)) return t + 1;                // s17: L[63:56] is d, equal to $past(d) one tick later
      9: if (t + 2 < NT && W(t) && W(t + 1) && W(t + 2) && V(t) && (D(t + 2) & 7) == (D(t) & 7)) return t + 2;   // s18
      10: if (t + 3 < NT && V(t) && D(t + 1) == D(t) + 1 && V(t + 2) && D(t + 3) == D(t + 2) + 2) return t + 3;   // s19(1) ##1 s19(2)
    endcase
    return -1;
  endfunction

  function automatic int pass_id(int m); case (m) 0: return 0; 1: return 3; 2: return 5; 3: return 12; 4: return 14; 5: return 16; default: return 20; endcase endfunction
  function automatic int cover_id(int m); case (m) 0: return 2; 1: return 7; 2: return 8; 3: return 9; 4: return 10; 5: return 11; 6: return 18; 7: return 19; 8: return 22; 9: return 23; default: return 24; endcase endfunction

  task automatic check_all;
    int e_fail[NT], e_lo[NT], e_hi[NT], e_cov[NT];
    int vac_head, lo_tot, hi_tot, pid, fid;
    int used[NID];
    for (int i = 0; i < NID; i++) used[i] = 0;
    if (ntick < NT) begin $error("only %0d ticks sampled", ntick); errors++; end
    for (int m = 0; m < 7; m++) begin
      pid = pass_id(m); fid = pid + 1; used[pid] = 1; used[fid] = 1;
      for (int k = 0; k < NT; k++) begin e_fail[k] = 0; e_lo[k] = 0; e_hi[k] = 0; end
      vac_head = 0; lo_tot = 0; hi_tot = 0;
      for (int t = 0; t < NT; t++) begin
        int rt, nv, res;
        res = attempt(m, t, rt, nv);
        if (res == 0) e_fail[rt]++;
        else if (res == 1) begin
          e_hi[rt]++; hi_tot++;
          if (nv) begin e_lo[rt]++; lo_tot++; end
          else if (rt <= NT - 1 - TAIL) vac_head++;
        end
      end
      begin
        int cum_lo = 0, cum_hi = 0, cum_op = 0;
        for (int k = 0; k < NT; k++) begin
          if (hits[fid][k] != e_fail[k]) begin
            $error("hit(%0d) tick %0d (time %0d): %0d fail actions, expected %0d", fid, k, 5 + 10 * k, hits[fid][k], e_fail[k]); errors++; end
          cum_lo += e_lo[k]; cum_hi += e_hi[k]; cum_op += hits[pid][k];
          if (cum_op < cum_lo || cum_op > cum_hi) begin
            $error("hit(%0d) up to tick %0d: %0d pass actions, allowed %0d..%0d", pid, k, cum_op, cum_lo, cum_hi); errors++; end
        end
        if (cum_op < lo_tot + (VAC ? vac_head : 0) || cum_op > hi_tot) begin
          $error("hit(%0d): %0d pass actions in total, expected %0d..%0d (%0d non-vacuous, %0d vacuous decided early)",
                 pid, cum_op, lo_tot + (VAC ? vac_head : 0), hi_tot, lo_tot, vac_head); errors++; end
      end
    end
    for (int m = 0; m < 11; m++) begin
      pid = cover_id(m); used[pid] = 1;
      for (int k = 0; k < NT; k++) e_cov[k] = 0;
      for (int t = 0; t < NT; t++) begin
        int e = cover_end(m, t);
        if (e >= 0) e_cov[e]++;
      end
      for (int k = 0; k < NT; k++)
        if (hits[pid][k] != e_cov[k]) begin
          $error("hit(%0d) cover tick %0d (time %0d): %0d hits, expected %0d", pid, k, 5 + 10 * k, hits[pid][k], e_cov[k]); errors++; end
    end
    for (int i = 0; i < NID; i++)
      if (!used[i]) for (int k = 0; k < NT; k++) if (hits[i][k] != 0) begin $error("hit(%0d) used unexpectedly", i); errors++; end
    if (errors == 0) $display("PASSED");
  endtask
endmodule
