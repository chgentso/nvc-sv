// Self-checking test of instance vs type coverage, illegal/ignore bins, type_option.weight, crosses with
// implicit coverpoints, and $get_coverage.
// Expected values: IEEE 1800-2017 19.5.1 (illegal/ignore bins excluded from the bin count), 19.6 (cross
// operands that are variables declare implicit coverpoints, which count as items), 19.7 and Table 19-3
// (type_option.merge_instances = 0: type coverage is the weighted average of the instances' coverage;
// type_option.weight), 19.8 (get_coverage, get_inst_coverage), 19.11 (coverage arithmetic), 20.14 ($get_coverage
// = type_option.weight weighted average over the covergroup types).
// Left out because the LRM does not define them: coverage of a coverpoint whose bins are all ignored
// (g3), so g3 and $get_coverage are only range-checked.
module svcovtype1;
  bit [3:0] v; bit [1:0] w; bit s;
  covergroup g1;
    cp1: coverpoint v { bins a = {1}; bins b = {2}; illegal_bins ill = {15}; }
  endgroup
  covergroup g2;
    option.per_instance = 1;
    type_option.weight = 2;
    cp1: coverpoint v { bins a = {1}; bins b = {2}; }
  endgroup
  covergroup g3;
    coverpoint v { ignore_bins i = {[0:15]}; }
    cp2: coverpoint w { bins a = {1}; }
  endgroup
  covergroup g4;
    cp1: coverpoint v[1:0] { bins a = {1}; bins b = {2,3}; }
    x: cross v, w;
    y: cross cp1, w, s;
    cp3: coverpoint (v + 1) { bins q = {[2:5]}; bins r = {[6:15]}; }
    z: cross cp1, cp3;
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
  initial begin
    g1 a, a2; g2 b, b2; g3 c; g4 d;
    real g4exp, g3any, sysc;
    a = new; a2 = new; b = new; b2 = new; c = new; d = new;
    v = 1; a.sample(); b.sample();
    chk("a",   a.get_inst_coverage(), 50.0);       // bin a of 2 (illegal bin excluded)
    chk("a2",  a2.get_inst_coverage(), 0.0);
    chk("gA",  g1::get_coverage(), 25.0);          // average of the instances 50 and 0
    chk("gAa", a2.get_coverage(), 25.0);           // get_coverage on an instance returns the type coverage
    v = 2; a2.sample(); b2.sample();
    chk("a'",  a.get_inst_coverage(), 50.0);
    chk("a2'", a2.get_inst_coverage(), 50.0);
    chk("gA'", g1::get_coverage(), 50.0);
    chk("b",   b.get_inst_coverage(), 50.0);
    chk("b2",  b2.get_inst_coverage(), 50.0);
    chk("gB",  g2::get_coverage(), 50.0);
    v = 3; w = 1; c.sample();
    chk("c cp2", c.cp2.get_inst_coverage(), 100.0);
    d.sample();    // v=3 w=1 s=0
    // cp1 = v[1:0] = 3 -> b: 1/2; x: v*w has 16*4 bins, 1 hit; y: cp1*w*s = 2*4*2 bins, 1 hit;
    // cp3 = v+1 = 4 -> q: 1/2; z: cp1*cp3 = 4 bins, 1 hit; implicit coverpoints v (1/16), w (1/4), s (1/2)
    chk("d cp1", d.cp1.get_inst_coverage(), 50.0);
    chk("d x",   d.x.get_inst_coverage(), 100.0 / 64);
    chk("d y",   d.y.get_inst_coverage(), 100.0 / 16);
    chk("d cp3", d.cp3.get_inst_coverage(), 50.0);
    chk("d z",   d.z.get_inst_coverage(), 25.0);
    g4exp = (50.0 + 100.0 / 64 + 100.0 / 16 + 50.0 + 25.0 + 100.0 / 16 + 25.0 + 50.0) / 8;
    chk("d",     d.get_inst_coverage(), g4exp);
    chk("g1",    g1::get_coverage(), 50.0);
    chk("g2",    g2::get_coverage(), 50.0);
    chk("g4",    g4::get_coverage(), g4exp);
    g3any = g3::get_coverage();
    if (g3any < 0.0 || g3any > 100.0) begin fails++; $error("g3 out of range: %f", g3any); end
    // $get_coverage: average over the types weighted by type_option.weight (g2 has 2, the others 1)
    sysc = $get_coverage();
    if (sysc < (50.0 + 2 * 50.0 + 0.0 + g4exp) / 5 - 0.005 || sysc > (50.0 + 2 * 50.0 + 100.0 + g4exp) / 5 + 0.005) begin
      fails++; $error("$get_coverage out of range: %f", sysc);
    end
    if (fails == 0) $display("PASSED");
  end
endmodule
