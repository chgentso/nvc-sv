// Self-checking test of miscellaneous coverage features: enum bins with overlapping value sets, `with`
// bins, iff on a coverpoint, crosses with ignore_bins, get_inst_coverage(covered, total), option.weight and
// option.at_least set on an instance.
// Expected values: IEEE 1800-2017 19.5 (bin definitions, `with`), 19.5.1 (ignore_bins), 19.6 (cross),
// 19.7 (option.weight, option.at_least), 19.8.1 (get_inst_coverage with covered/total), 19.11 (coverage:
// bins covered / bins per item, group = average of items weighted by option.weight).
module svcovmisc1;
  typedef enum {A, B, C, D} e_t;
  e_t e;
  bit [3:0] v;
  bit en;
  covergroup g1;
    cp_e: coverpoint e { bins ab = {A, B}; bins c = {C}; bins each[] = {A, B, C}; }
    cp_v: coverpoint v { bins ev[] = {[0:15]} with (item % 4 == 0); bins odd = {[0:15]} with (item % 2 == 1); }
    cp_en: coverpoint v iff (en) { bins lo = {[0:7]}; bins hi = {[8:15]}; }
    x: cross cp_e, cp_en;
  endgroup
  covergroup g2;
    cp_e: coverpoint e;
    cp_v: coverpoint v { bins b[2] = {[0:7]}; }
    x: cross cp_e, cp_v { ignore_bins ig = binsof(cp_e) intersect {D}; }
  endgroup
  int fails = 0;
  function automatic bit near(real a, real b);
    return (a > b ? a - b : b - a) < 0.005;
  endfunction
  task automatic chk(string what, real got, real exp);
    if (!near(got, exp)) begin
      fails++; $error("%s: got %0.2f, expected %0.2f", what, got, exp);
    end
  endtask
  task automatic chki(string what, int got, int exp);
    if (got != exp) begin
      fails++; $error("%s: got %0d, expected %0d", what, got, exp);
    end
  endtask
  initial begin
    g1 a; g2 b; real r; int cov, tot;
    real p_e, p_v, p_en, p_x;
    a = new; b = new;
    // sample 1: e=A v=4 en=1
    // g1: cp_e ab,each[A] = 2/5; cp_v ev[4] = 1/5; cp_en lo = 1/2; cross ab*lo, each[A]*lo = 2/10
    e = A; v = 4; en = 1; a.sample(); b.sample();
    chk("g1 cp_e 1", a.cp_e.get_inst_coverage(), 40.0);
    chk("g1 cp_v 1", a.cp_v.get_inst_coverage(), 20.0);
    chk("g1 cp_en 1", a.cp_en.get_inst_coverage(), 50.0);
    chk("g1 x 1", a.x.get_inst_coverage(), 20.0);
    chk("g1 1", a.get_inst_coverage(), (40.0 + 20.0 + 50.0 + 20.0) / 4);
    // g2: cp_e 1/4; cp_v b[1] 1/2; cross (A,b[1]) 1 of 4*2-2(ignored D) = 6
    chk("g2 cp_e 1", b.cp_e.get_inst_coverage(), 25.0);
    chk("g2 cp_v 1", b.cp_v.get_inst_coverage(), 50.0);
    chk("g2 x 1", b.x.get_inst_coverage(), 100.0 / 6);
    chk("g2 1", b.get_inst_coverage(), (25.0 + 50.0 + 100.0 / 6) / 3);
    // sample 2: e=D v=9 en=0: D is in no g1 bin; 9 is odd; iff false so cp_en/cross unchanged
    e = D; v = 9; en = 0; a.sample(); b.sample();
    chk("g1 cp_e 2", a.cp_e.get_inst_coverage(), 40.0);
    chk("g1 cp_v 2", a.cp_v.get_inst_coverage(), 40.0);
    chk("g1 cp_en 2", a.cp_en.get_inst_coverage(), 50.0);
    chk("g1 x 2", a.x.get_inst_coverage(), 20.0);
    chk("g1 2", a.get_inst_coverage(), (40.0 + 40.0 + 50.0 + 20.0) / 4);
    chk("g2 cp_e 2", b.cp_e.get_inst_coverage(), 50.0);
    chk("g2 cp_v 2", b.cp_v.get_inst_coverage(), 50.0);   // 9 is outside [0:7]
    chk("g2 x 2", b.x.get_inst_coverage(), 100.0 / 6);    // (D,*) is ignored
    chk("g2 2", b.get_inst_coverage(), (50.0 + 50.0 + 100.0 / 6) / 3);
    // covered/total counts
    r = a.cp_e.get_inst_coverage(cov, tot); chki("cp_e covered", cov, 2); chki("cp_e total", tot, 5);
    chk("cp_e ret", r, 40.0);
    r = a.cp_v.get_inst_coverage(cov, tot); chki("cp_v covered", cov, 2); chki("cp_v total", tot, 5);
    chk("cp_v ret", r, 40.0);
    r = b.x.get_inst_coverage(cov, tot); chki("x covered", cov, 1); chki("x total", tot, 6);
    chk("x ret", r, 100.0 / 6);
    // option.weight = 3 on g2.cp_e
    b.cp_e.option.weight = 3;
    chk("g2 weighted", b.get_inst_coverage(), (3 * 50.0 + 50.0 + 100.0 / 6) / 5);
    // option.at_least = 2 on g2.cp_v: both bins were hit at most once
    b.cp_v.option.at_least = 2;
    chk("g2 at_least", b.get_inst_coverage(), (3 * 50.0 + 0.0 + 100.0 / 6) / 5);
    chk("g2 cp_v at_least", b.cp_v.get_inst_coverage(), 0.0);
    // sample 3: e=B v=9 en=1
    e = B; v = 9; en = 1; a.sample(); b.sample();
    // g1: cp_e ab, each[A], each[B] = 3/5; cp_v ev[4], odd = 2/5; cp_en lo, hi = 2/2; cross 4/10
    chk("g1 3", a.get_inst_coverage(), (60.0 + 40.0 + 100.0 + 40.0) / 4);
    // g2: cp_e A,B,D = 3/4; cp_v 0 (at_least 2); cross: (A,b[1]) only, 9 is in no cross bin
    chk("g2 3", b.get_inst_coverage(), (3 * 75.0 + 0.0 + 100.0 / 6) / 5);
    chk("g2 cp_v 3", b.cp_v.get_inst_coverage(), 0.0);
    if (fails == 0) $display("PASSED");
  end
endmodule
