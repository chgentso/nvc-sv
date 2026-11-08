// Self-checking test of coverage queries on several covergroup shapes: signed ranges with $, auto bins,
// sample() arguments, expressions as coverpoints, iff, constructor arguments, per-instance and type coverage.
// Expected values: IEEE 1800-2017 19.5 (bins, `$` = type min/max, auto bins), 19.5.1 (default bins are not
// counted), 19.7 (option.at_least; type_option.merge_instances = 0 so type coverage is the average of the
// instances), 19.8 (get_coverage, get_inst_coverage, covered/total counts), 19.11 (coverage arithmetic).
// Left out because the LRM does not define them: coverage of a coverpoint that has only a default bin, and
// of an empty covergroup beyond the 0..100 range; covered/total of type-level queries.
module svcovquery2;
  bit [2:0] u; byte s; bit signed [3:0] n; int a, b;
  covergroup g1;
    cp1: coverpoint s { bins neg = {[-5:-1]}; bins zero = {0}; bins pos = {[1:$]}; bins low = {[$:-100]}; }
    cp2: coverpoint n;
    cp3: coverpoint u { bins all[] = {[0:$]}; }
  endgroup
  covergroup g2 with function sample(int x, int y);
    cp1: coverpoint x + y { bins s0 = {[0:9]}; bins s1 = {[10:19]}; }
    cp2: coverpoint y iff (x > 0) { bins pos = {[1:100]}; }
  endgroup
  covergroup g3;
    option.at_least = 2;
    cp1: coverpoint u { bins d = default; }
    cp2: coverpoint u { bins a = {1}; }
  endgroup
  covergroup g4;
  endgroup
  covergroup g5(input int lim, input int off);
    cp1: coverpoint u + off { bins lo = {[0:lim]}; bins hi = {[lim+1:7]}; }
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
    g1 c1; g2 c2; g3 c3; g4 c4; g5 c5; g1 d1, d2; int cov, tot; real r;
    real k1, k2, k3, gi;
    c1 = new; c2 = new; c3 = new; c4 = new; c5 = new(3, 1);
    // g1: cp1 has 4 bins, cp2 16 auto bins, cp3 8 bins
    s = -3; n = -1; u = 7; c1.sample();
    chk("g1 s1 cp1", c1.cp1.get_inst_coverage(), 25.0);
    chk("g1 s1 cp2", c1.cp2.get_inst_coverage(), 100.0 / 16);
    chk("g1 s1 cp3", c1.cp3.get_inst_coverage(), 12.5);
    chk("g1 s1", c1.get_inst_coverage(), (25.0 + 100.0 / 16 + 12.5) / 3);
    s = 100; n = 7; u = 0; c1.sample();
    chk("g1 s2 cp1", c1.cp1.get_inst_coverage(), 50.0);
    chk("g1 s2 cp2", c1.cp2.get_inst_coverage(), 200.0 / 16);
    chk("g1 s2 cp3", c1.cp3.get_inst_coverage(), 25.0);
    chk("g1 s2", c1.get_inst_coverage(), (50.0 + 200.0 / 16 + 25.0) / 3);
    s = -128; n = -8; u = 3; c1.sample();
    chk("g1 s3 cp1", c1.cp1.get_inst_coverage(), 75.0);   // [$:-100] is [-128:-100]
    chk("g1 s3 cp2", c1.cp2.get_inst_coverage(), 300.0 / 16);
    chk("g1 s3 cp3", c1.cp3.get_inst_coverage(), 37.5);
    chk("g1 s3", c1.get_inst_coverage(), (75.0 + 300.0 / 16 + 37.5) / 3);
    // g2: sampled with arguments; x+y is evaluated as an int
    c2.sample(5, 4);       // sum 9 -> s0; y=4 with x>0 -> pos
    chk("g2 s1 cp1", c2.cp1.get_inst_coverage(), 50.0);
    chk("g2 s1 cp2", c2.cp2.get_inst_coverage(), 100.0);
    chk("g2 s1", c2.get_inst_coverage(), 75.0);
    c2.sample(-5, 50);     // sum 45 matches no bin; guard x>0 false
    chk("g2 s2", c2.get_inst_coverage(), 75.0);
    c2.sample(12, 7);      // sum 19 -> s1
    chk("g2 s3 cp1", c2.cp1.get_inst_coverage(), 100.0);
    chk("g2 s3", c2.get_inst_coverage(), 100.0);
    // g3: covergroup-level at_least = 2 applies to the coverpoints; bin a needs two hits
    u = 1; c3.sample();
    chk("g3 one hit cp2", c3.cp2.get_inst_coverage(), 0.0);
    c3.sample();
    chk("g3 two hits cp2", c3.cp2.get_inst_coverage(), 100.0);
    // g4: empty covergroup: only the range is defined here
    r = c4.get_inst_coverage();
    if (r < 0.0 || r > 100.0) begin fails++; $error("g4 out of range %f", r); end
    // g5: constructor arguments give the bin boundaries (lim=3, off=1): lo=[0:3], hi=[4:7]
    u = 1; c5.sample();    // 1+1 = 2 -> lo
    chk("g5 s1", c5.get_inst_coverage(), 50.0);
    u = 5; c5.sample();    // 5+1 = 6 -> hi
    chk("g5 s2", c5.get_inst_coverage(), 100.0);
    // per-instance counts and type coverage (average over instances c1, d1, d2)
    d1 = new; d2 = new;
    s = 2; n = 0; u = 4; d1.sample();
    r = d1.get_inst_coverage(cov, tot);
    chki("d1 covered", cov, 3); chki("d1 total", tot, 4 + 16 + 8);
    chk("d1 ret", r, (25.0 + 100.0 / 16 + 12.5) / 3);
    r = d1.cp1.get_inst_coverage(cov, tot);
    chki("d1cp1 covered", cov, 1); chki("d1cp1 total", tot, 4);
    chk("d1cp1 ret", r, 25.0);
    k1 = c1.get_inst_coverage(); k2 = d1.get_inst_coverage(); k3 = d2.get_inst_coverage();
    chk("d2 untouched", k3, 0.0);
    gi = (k1 + k2 + k3) / 3;
    r = g1::get_coverage();
    chk("type g1", r, gi);
    r = d2.get_coverage();
    chk("type via instance", r, gi);
    chk("type cp1", d1.cp1.get_coverage(), (75.0 + 25.0 + 0.0) / 3);
    if (fails == 0) $display("PASSED");
  end
endmodule
