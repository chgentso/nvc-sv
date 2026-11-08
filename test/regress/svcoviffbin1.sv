// Self-checking test of bin-level iff guards (plain bins, ignore_bins, transition bins).
// Expected values: IEEE 1800-2017 19.5 (iff guard: a false guard at a sample means the bin is not
// incremented; for a transition bin the guard belongs to the sample that completes the sequence),
// 19.5.1 (ignore/illegal bins), 19.11 (coverage = covered bins / bins, ignore/illegal excluded).
module svcoviffbin1;
  bit [3:0] v; bit en;
  covergroup g;
    cp: coverpoint v {
      bins lo = {[0:3]} iff (en);
      bins hi[] = {[4:6]} iff (!en);
      ignore_bins ig = {7} iff (en);
      bins rest = {[8:15]};
    }
  endgroup
  covergroup g2;
    cp: coverpoint v {
      bins t12 = (1 => 2) iff (en);
      bins t34 = (3 => 4);
      illegal_bins il = (5 => 5) iff (en);
    }
  endgroup
  g gi = new;
  g2 g2i = new;
  int fails = 0;
  int n = 0;

  // g: bins lo, hi[4], hi[5], hi[6], rest = 5 bins, each worth 20%
  // g2: bins t12, t34 = 2 bins, each worth 50%
  bit [3:0] sv_[21] = '{1,1,4,4,5,7,7,9,6, 1,2,1,2,5,5,5,6, 3,4,1,2};
  bit       se_[21] = '{0,1,1,0,0,0,1,0,0, 0,0,1,0,0,0,0,1, 0,0,0,1};
  // reference model on the sampled values
  bit lo_h, rest_h; bit hi_h[4:6];
  bit t12_h, t34_h;
  bit [3:0] prev; bit have_prev;

  function automatic real ref1();
    int c = 0;
    if (lo_h) c++;
    if (rest_h) c++;
    for (int k = 4; k <= 6; k++) if (hi_h[k]) c++;
    return 100.0 * c / 5;
  endfunction
  function automatic real ref2();
    int c = 0;
    if (t12_h) c++;
    if (t34_h) c++;
    return 100.0 * c / 2;
  endfunction

  function automatic bit near(real a, real b);
    return (a > b ? a - b : b - a) < 0.005;
  endfunction

  task s(input [3:0] x, input e);
    real g1c, g2c;
    v = x; en = e; gi.sample(); g2i.sample();
    // model
    if (x <= 3 && e) lo_h = 1;
    if (x >= 4 && x <= 6 && !e) hi_h[x] = 1;
    if (x >= 8) rest_h = 1;
    if (have_prev) begin
      if (prev == 1 && x == 2 && e) t12_h = 1;
      if (prev == 3 && x == 4) t34_h = 1;
    end
    prev = x; have_prev = 1;
    g1c = gi.get_inst_coverage(); g2c = g2i.get_inst_coverage();
    if (!near(g1c, ref1())) begin
      fails++; $error("sample %0d (v=%0d en=%0d): g coverage %0.2f, expected %0.2f", n, x, e, g1c, ref1());
    end
    if (!near(g2c, ref2())) begin
      fails++; $error("sample %0d (v=%0d en=%0d): g2 coverage %0.2f, expected %0.2f", n, x, e, g2c, ref2());
    end
    n++;
  endtask

  initial begin
    for (int i = 0; i < 21; i++) s(sv_[i], se_[i]);
    // final values hand-derived from 19.5: g 5/5 bins, g2 2/2 bins (1=>2 completes with en=1 at the end)
    if (!near(gi.get_inst_coverage(), 100.0)) begin fails++; $error("final g coverage"); end
    if (!near(g2i.get_inst_coverage(), 100.0)) begin fails++; $error("final g2 coverage"); end
    if (fails == 0) $display("PASSED");
  end
endmodule
