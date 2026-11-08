// Self-checking test of transition bins with consecutive, goto and nonconsecutive repetition, over 16
// pseudo-random 2-bit streams (the LCG below is part of the stimulus; nothing depends on the simulator's
// random numbers).  Every coverpoint has a single transition bin, so its coverage is 0 or 100.
// Expected values: IEEE 1800-2017 19.5.2 (transition bins: `=>`, [*n], [->n], [=n], ranges of repetition;
// a bin is hit when its sequence matches ending at some sample) and 19.11.  The expectation comes from the
// reference model below, which enumerates the matches over the sampled stream.
module svcovtrans1;
  bit [1:0] v;
  covergroup g;
    c1: coverpoint v { bins t = (1 [->2] => 3); }
    c2: coverpoint v { bins t = (1 [=2] => 3); }
    c3: coverpoint v { bins t = (2 [->3]); }
    c4: coverpoint v { bins t = (1 => 2 [->2] => 3); }
    c5: coverpoint v { bins t = (1 [->1:3] => 0); }
    c6: coverpoint v { bins t = (3 [=2] => 1 => 2); }
    c9: coverpoint v { bins t = (3 [=2:3] => 1); }
    c10: coverpoint v { bins t = (3 [=2] => 1); }
    c7: coverpoint v { bins t = (0 [*2] => 1 [->2] => 2); }
    c8: coverpoint v { bins t = (1, 2 [->2] => 3); }
    c11: coverpoint v { bins t = (1 [=1] => 2 => 3); }
    c12: coverpoint v { bins t = (2 [=1:2] => 0 => 1); }
  endgroup
  g gi;
  longint unsigned seed;
  int fails = 0, hits = 0, misses = 0;
  // ---- reference model of a transition bin over the stream of sampled values (19.5.2) ----
  // Element kinds: 0 = single value set, 1 = consecutive repetition [*lo:hi], 2 = goto [->lo:hi]
  // (ends at the last occurrence), 3 = nonconsecutive [=lo:hi] (may continue past the last occurrence
  // until the next occurrence).  Elements are joined by `=>` (one sample apart).
  bit [7:0] em[8]; int ek[8], elo[8], ehi[8]; int ne;
  int strm[$];
  function automatic void clr(); ne = 0; endfunction
  function automatic void el(bit [7:0] mask, int kind, int lo, int hi);
    em[ne] = mask; ek[ne] = kind; elo[ne] = lo; ehi[ne] = hi; ne++;
  endfunction
  function automatic bit mt(int e, int p);
    int n = strm.size();
    int cnt;
    if (e == ne) return 1;
    case (ek[e])
      0: return p < n && em[e][strm[p]] && mt(e + 1, p + 1);
      1: begin
        for (int k = 1; k <= ehi[e]; k++) begin
          if (p + k > n) break;
          if (!em[e][strm[p + k - 1]]) break;
          if (k >= elo[e] && mt(e + 1, p + k)) return 1;
        end
        return 0;
      end
      2: begin
        cnt = 0;
        for (int q = p; q < n; q++) if (em[e][strm[q]]) begin
          cnt++;
          if (cnt > ehi[e]) break;
          if (cnt >= elo[e] && mt(e + 1, q + 1)) return 1;
        end
        return 0;
      end
      default: begin
        cnt = 0;
        for (int q = p; q < n; q++) if (em[e][strm[q]]) begin
          cnt++;
          if (cnt > ehi[e]) break;
          if (cnt >= elo[e]) begin
            for (int t = q; t < n; t++) begin
              if (t > q && em[e][strm[t]]) break;
              if (mt(e + 1, t + 1)) return 1;
            end
          end
        end
        return 0;
      end
    endcase
  endfunction
  function automatic bit hit();
    for (int p = 0; p < strm.size(); p++) if (mt(0, p)) return 1;
    return 0;
  endfunction
  // model definitions of the twelve bins, in the order c1 c2 c3 c4 c5 c6 c7 c8 c9 c10 c11 c12
  function automatic void define(int id);
    clr();
    case (id)
      1:  begin el(8'h02, 2, 2, 2); el(8'h08, 0, 1, 1); end
      2:  begin el(8'h02, 3, 2, 2); el(8'h08, 0, 1, 1); end
      3:  begin el(8'h04, 2, 3, 3); end
      4:  begin el(8'h02, 0, 1, 1); el(8'h04, 2, 2, 2); el(8'h08, 0, 1, 1); end
      5:  begin el(8'h02, 2, 1, 3); el(8'h01, 0, 1, 1); end
      6:  begin el(8'h08, 3, 2, 2); el(8'h02, 0, 1, 1); el(8'h04, 0, 1, 1); end
      7:  begin el(8'h01, 1, 2, 2); el(8'h02, 2, 2, 2); el(8'h04, 0, 1, 1); end
      8:  begin el(8'h06, 2, 2, 2); el(8'h08, 0, 1, 1); end
      9:  begin el(8'h08, 3, 2, 3); el(8'h02, 0, 1, 1); end
      10: begin el(8'h08, 3, 2, 2); el(8'h02, 0, 1, 1); end
      11: begin el(8'h02, 3, 1, 1); el(8'h04, 0, 1, 1); el(8'h08, 0, 1, 1); end
      12: begin el(8'h04, 3, 1, 2); el(8'h01, 0, 1, 1); el(8'h02, 0, 1, 1); end
    endcase
  endfunction
  task automatic check(int run, int id, real cov);
    bit h; real exp;
    define(id); h = hit(); exp = h ? 100.0 : 0.0;
    if (h) hits++; else misses++;
    if (cov < exp - 0.005 || cov > exp + 0.005) begin
      fails++;
      $error("run %0d bin c%0d: coverage %0.2f, expected %0.2f for stream %p", run, id, cov, exp, strm);
    end
  endtask
  initial begin
    for (int run = 0; run < 16; run++) begin
      gi = new;
      seed = run * 7 + 3;
      strm.delete();
      for (int i = 0; i < 60; i++) begin
        seed = (seed * 1103515245 + 12345) & 64'h7fffffff;
        v = seed[17:16];
        strm.push_back(v);
        gi.sample();
      end
      check(run, 1, gi.c1.get_inst_coverage());
      check(run, 2, gi.c2.get_inst_coverage());
      check(run, 3, gi.c3.get_inst_coverage());
      check(run, 4, gi.c4.get_inst_coverage());
      check(run, 5, gi.c5.get_inst_coverage());
      check(run, 6, gi.c6.get_inst_coverage());
      check(run, 7, gi.c7.get_inst_coverage());
      check(run, 8, gi.c8.get_inst_coverage());
      check(run, 9, gi.c9.get_inst_coverage());
      check(run, 10, gi.c10.get_inst_coverage());
      check(run, 11, gi.c11.get_inst_coverage());
      check(run, 12, gi.c12.get_inst_coverage());
    end
    // the stimulus must exercise both outcomes, otherwise the comparison is vacuous
    if (hits == 0 || misses == 0) begin fails++; $error("stimulus not discriminating: %0d hits %0d misses", hits, misses); end
    if (fails == 0) $display("PASSED");
  end
endmodule
