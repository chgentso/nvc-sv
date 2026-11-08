// Bins, transitions, crosses with binsof/intersect, wildcard bins, iff, ref arguments, start/stop
// Expected values: IEEE 1800-2017 19.5 (bins, ignore_bins, illegal_bins, default, wildcard),
// 19.5.1 (bins[N]: first N-1 bins take floor(M/N) values, last bin the rest), 19.5.2 (transition
// bins; t4[] = (8 => 9, 10) makes two bins), 19.6 (cross; binsof, intersect, user-defined cross
// bins, ignore_bins/illegal_bins of a cross), 19.7 (per-item weights default to 1), 19.8
// (start/stop, get_inst_coverage with covered/total), 19.11 (coverage arithmetic: default,
// ignore and illegal bins are not counted).
// Left out: the value of $get_coverage (set of covergroup types is implementation-defined).
// x2 has a third user bin (b3 = hi x a) so that every product of the cross is either in a user
// bin or ignored, and the expected numbers do not depend on automatically created cross bins.
module svcovcross1;
  bit [3:0] v; bit [1:0] w; bit en;
  int errs = 0;
  covergroup g1;
    cp1: coverpoint v { bins b[3] = {[0:9]}; }
    cp2: coverpoint v { bins b[4] = {[0:5]}; }
  endgroup
  covergroup g2;
    cpt: coverpoint v { bins t1 = (1 => 2 => 3); bins t2 = (4 => 5), (6 => 7); bins t3 = (2[*2] => 3); bins t4[] = (8 => 9, 10); ignore_bins ig = (0 => 15); illegal_bins il = (14 => 13); }
  endgroup
  covergroup g3;
    cpw: coverpoint w { bins a = {0}; bins b = {1}; bins c = {2}; bins d = {3}; }
    cpv: coverpoint v { bins lo = {[0:7]}; bins hi = {[8:15]}; }
    x1: cross cpv, cpw;
    x2: cross cpv, cpw { bins b1 = binsof(cpv.lo) && binsof(cpw.a); bins b3 = binsof(cpv.hi) && binsof(cpw.a); bins b2 = binsof(cpw) intersect {2,3}; ignore_bins ig = binsof(cpw.b); }
    x3: cross cpv, cpw { ignore_bins ig = binsof(cpw.b); illegal_bins ill = binsof(cpv.hi) && binsof(cpw.c); }
  endgroup
  covergroup g4;
    cp1: coverpoint v { wildcard bins w = {4'b1??0}; bins d = default; }
    cp2: coverpoint w iff (en) { bins a = {1}; bins b = {2}; }
  endgroup
  covergroup g5(int lim);
    cp1: coverpoint v { bins a = {[0:lim]}; }
  endgroup
  function automatic void chkr(string m, real got, real exp);
    if (got > exp + 1.0e-6 || got < exp - 1.0e-6) begin
      $error("%s: got %f expected %f", m, got, exp); errs++;
    end
  endfunction
  function automatic void chki(string m, int got, int exp);
    if (got != exp) begin $error("%s: got %0d expected %0d", m, got, exp); errs++; end
  endfunction
  initial begin
    g1 a; g2 b; g3 c; g4 d; g5 f; real r; int cov, tot;
    // g1: cp1 = {0-2},{3-5},{6-9}; cp2 = {0},{1},{2},{3-5}
    a = new;
    v = 0; a.sample(); v = 3; a.sample();
    chkr("g1.1a", a.cp1.get_inst_coverage(), 200.0/3); chkr("g1.1b", a.cp2.get_inst_coverage(), 50.0);
    a = new; v = 0; a.sample(); v = 2; a.sample();
    chkr("g1.2a", a.cp1.get_inst_coverage(), 100.0/3); chkr("g1.2b", a.cp2.get_inst_coverage(), 50.0);
    a = new; v = 0; a.sample(); v = 6; a.sample();
    chkr("g1.3a", a.cp1.get_inst_coverage(), 200.0/3); chkr("g1.3b", a.cp2.get_inst_coverage(), 25.0);
    a = new; v = 0; a.sample(); v = 4; a.sample();
    chkr("g1.4a", a.cp1.get_inst_coverage(), 200.0/3); chkr("g1.4b", a.cp2.get_inst_coverage(), 50.0);
    a = new; v = 5; a.sample(); v = 4; a.sample();
    chkr("g1.5a", a.cp1.get_inst_coverage(), 100.0/3); chkr("g1.5b", a.cp2.get_inst_coverage(), 25.0);
    a = new; v = 1; a.sample(); v = 2; a.sample();
    chkr("g1.6a", a.cp1.get_inst_coverage(), 100.0/3); chkr("g1.6b", a.cp2.get_inst_coverage(), 50.0);
    // g2: 5 countable bins (t1, t2, t3, t4[0] = 8=>9, t4[1] = 8=>10)
    b = new;
    for (int i = 0; i < 4; i++) begin
      int seq[$];
      case (i) 0: seq = '{1,2,3}; 1: seq = '{4,5,6,7}; 2: seq = '{2,2,3}; 3: seq = '{8,9,10};
      endcase
      foreach (seq[j]) begin v = seq[j]; b.sample(); end
      chkr($sformatf("g2 after seq%0d", i), b.get_inst_coverage(), 20.0 * (i + 1));
    end
    v = 1; b.sample(); v = 2; b.sample(); v = 2; b.sample(); v = 3; b.sample();
    chkr("g2 repeat", b.get_inst_coverage(), 80.0);
    v = 0; b.sample(); v = 15; b.sample();
    chkr("g2 ign", b.get_inst_coverage(), 80.0);
    // g3: cpw 4 bins, cpv 2 bins, x1 8 bins, x2 3 bins (b1, b3, b2), x3 5 bins (8 - 2 ignored - 1 illegal)
    c = new;
    v = 1; w = 0; c.sample();
    chkr("g3.1 cpv", c.cpv.get_inst_coverage(), 50.0);
    chkr("g3.1 cpw", c.cpw.get_inst_coverage(), 25.0);
    chkr("g3.1 x1", c.x1.get_inst_coverage(), 12.5);
    chkr("g3.1 x2", c.x2.get_inst_coverage(), 100.0/3);
    chkr("g3.1 x3", c.x3.get_inst_coverage(), 20.0);
    chkr("g3.1", c.get_inst_coverage(), (25.0 + 50.0 + 12.5 + 100.0/3 + 20.0) / 5);
    v = 9; w = 1; c.sample();   // (hi, b) is ignored in x2 and x3
    chkr("g3.2 x1", c.x1.get_inst_coverage(), 25.0);
    chkr("g3.2 x2", c.x2.get_inst_coverage(), 100.0/3);
    chkr("g3.2 x3", c.x3.get_inst_coverage(), 20.0);
    chkr("g3.2", c.get_inst_coverage(), (50.0 + 100.0 + 25.0 + 100.0/3 + 20.0) / 5);
    v = 9; w = 3; c.sample();
    chkr("g3.3 x1", c.x1.get_inst_coverage(), 37.5);
    chkr("g3.3 x2", c.x2.get_inst_coverage(), 200.0/3);
    chkr("g3.3 x3", c.x3.get_inst_coverage(), 40.0);
    chkr("g3.3", c.get_inst_coverage(), (75.0 + 100.0 + 37.5 + 200.0/3 + 40.0) / 5);
    v = 2; w = 3; c.sample();
    chkr("g3.4 x1", c.x1.get_inst_coverage(), 50.0);
    chkr("g3.4 x2", c.x2.get_inst_coverage(), 200.0/3);
    chkr("g3.4 x3", c.x3.get_inst_coverage(), 60.0);
    chkr("g3.4", c.get_inst_coverage(), (75.0 + 100.0 + 50.0 + 200.0/3 + 60.0) / 5);
    // g4: the default bin is not counted; cp2 is sampled only when en is 1
    d = new; v = 8; w = 1; en = 0; d.sample();
    chkr("g4.1 cp1", d.cp1.get_inst_coverage(), 100.0);
    chkr("g4.1 cp2", d.cp2.get_inst_coverage(), 0.0);
    chkr("g4.1", d.get_inst_coverage(), 50.0);
    v = 3; en = 1; d.sample();
    chkr("g4.2 cp1", d.cp1.get_inst_coverage(), 100.0);
    chkr("g4.2 cp2", d.cp2.get_inst_coverage(), 50.0);
    chkr("g4.2", d.get_inst_coverage(), 75.0);
    r = d.get_inst_coverage(cov, tot);
    chki("ref covered", cov, 2); chki("ref total", tot, 3);
    chkr("ref value", r, 75.0);
    f = new(5); v = 5; f.sample();
    chkr("g5", f.get_inst_coverage(), 100.0);
    // stop/start: a stopped instance ignores samples
    d.set_inst_name("foo");
    d.stop(); v = 12; w = 2; d.sample();
    chkr("stop inst", d.get_inst_coverage(), 75.0);
    chkr("stop type", g4::get_coverage(), 75.0);
    d.start(); d.sample();
    chkr("start inst", d.get_inst_coverage(), 100.0);
    chkr("start type", g4::get_coverage(), 100.0);
    r = $get_coverage();
    if (!(r >= 0.0 && r <= 100.0)) begin $error("$get_coverage %f outside 0..100", r); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
