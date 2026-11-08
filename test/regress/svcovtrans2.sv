// Self-checking test of transition bins (consecutive, goto and nonconsecutive repetition, repetition ranges,
// value lists) over a fixed stream of sampled values, one covergroup per bin (cg0 .. cg9).
// Expected values: IEEE 1800-2017 19.5.2 (transition bins: `=>`, [*n], [->n], [=n], ranges of repetition;
// a bin is hit when its sequence matches ending at some sample) and 19.11 (single bin: 0 or 100).
// The expectation comes from the reference model below, which enumerates the matches over the stream of
// values sampled at each posedge (the initial 0 of x is sampled at the first posedge, then seq[0..]).
`timescale 1ns/1ns
module svcovtrans2;
  logic clk = 0;
  int x = 0;
  int seq[$] = '{1,2,3,4,5,4,5,6,7,1,2,2,2,3,4,4,3,1,1,1,2,5,5,6,6,6,7,1,2,1,2,3,1,5,6,7,1,1,2,2,3,3,4,4,5,5,6,6,2,3,4,1,2,2,3,3,3,4,5,5,5,6,7};
  always #5 clk = ~clk;
  covergroup cg0 @(posedge clk);
    cp: coverpoint x { bins b = (1 => 2 => 3); }
  endgroup
  cg0 c0 = new;
  covergroup cg1 @(posedge clk);
    cp: coverpoint x { bins b = (4 => 5); }
  endgroup
  cg1 c1 = new;
  covergroup cg2 @(posedge clk);
    cp: coverpoint x { bins b = (1, 2 => 3, 4); }
  endgroup
  cg2 c2 = new;
  covergroup cg3 @(posedge clk);
    cp: coverpoint x { bins b = (2 [* 3]); }
  endgroup
  cg3 c3 = new;
  covergroup cg4 @(posedge clk);
    cp: coverpoint x { bins b = (2 [* 2:4] => 3); }
  endgroup
  cg4 c4 = new;
  covergroup cg5 @(posedge clk);
    cp: coverpoint x { bins b = (5 [* 2:3] => 6); }
  endgroup
  cg5 c5 = new;
  covergroup cg6 @(posedge clk);
    cp: coverpoint x { bins b = (1 [-> 2]); }
  endgroup
  cg6 c6 = new;
  covergroup cg7 @(posedge clk);
    cp: coverpoint x { bins b = (3 [= 2]); }
  endgroup
  cg7 c7 = new;
  covergroup cg8 @(posedge clk);
    cp: coverpoint x { bins b = (1 => 2 [* 2] => 3); }
  endgroup
  cg8 c8 = new;
  covergroup cg9 @(posedge clk);
    cp: coverpoint x { bins b = (1 [-> 2] => 5); }
  endgroup
  cg9 c9 = new;
  int fails = 0, hits = 0, misses = 0;  // the fixed stream hits every bin
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
  function automatic void define(int id);
    clr();
    case (id)
      0: begin el(8'h02, 0, 1, 1); el(8'h04, 0, 1, 1); el(8'h08, 0, 1, 1); end
      1: begin el(8'h10, 0, 1, 1); el(8'h20, 0, 1, 1); end
      2: begin el(8'h06, 0, 1, 1); el(8'h18, 0, 1, 1); end
      3: begin el(8'h04, 1, 3, 3); end
      4: begin el(8'h04, 1, 2, 4); el(8'h08, 0, 1, 1); end
      5: begin el(8'h20, 1, 2, 3); el(8'h40, 0, 1, 1); end
      6: begin el(8'h02, 2, 2, 2); end
      7: begin el(8'h08, 3, 2, 2); end
      8: begin el(8'h02, 0, 1, 1); el(8'h04, 1, 2, 2); el(8'h08, 0, 1, 1); end
      9: begin el(8'h02, 2, 2, 2); el(8'h20, 0, 1, 1); end
    endcase
  endfunction
  task automatic check(int id, real cov);
    bit h; real exp;
    define(id); h = hit(); exp = h ? 100.0 : 0.0;
    if (h) hits++; else misses++;
    if (cov < exp - 0.005 || cov > exp + 0.005) begin
      fails++;
      $error("bin cg%0d: coverage %0.2f, expected %0.2f", id, cov, exp);
    end
  endtask
  initial begin
    strm.push_back(0);   // x is 0 at the first posedge
    foreach (seq[i]) strm.push_back(seq[i]);
    foreach (seq[i]) begin @(negedge clk); x = seq[i]; end
    @(negedge clk);
    check(0, c0.get_inst_coverage());
    check(1, c1.get_inst_coverage());
    check(2, c2.get_inst_coverage());
    check(3, c3.get_inst_coverage());
    check(4, c4.get_inst_coverage());
    check(5, c5.get_inst_coverage());
    check(6, c6.get_inst_coverage());
    check(7, c7.get_inst_coverage());
    check(8, c8.get_inst_coverage());
    check(9, c9.get_inst_coverage());
    if (fails == 0) $display("PASSED");
    $finish;
  end
endmodule
