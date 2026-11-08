// Self-checking test of option.weight / type_option.weight at covergroup and coverpoint level.
// Expected values: IEEE 1800-2017 19.7 (option.weight and type_option.weight: the weight of an item in
// the weighted average of its container; a covergroup's own weight only matters relative to other
// covergroups), 19.8, 19.11 (item coverage = covered bins / bins; group = weighted average of its items).
// Left out because the LRM does not define a weighted average with total weight 0: coverage of gF and
// the type-level coverage of gB.  gG is an added group whose coverpoints have different weights.
module svcovweight1;
  bit [1:0] v;
  covergroup gB; option.weight = 0; option.per_instance = 1; c1: coverpoint v { bins a = {0}; bins b = {1}; } endgroup
  covergroup gC; type_option.weight = 0; c1: coverpoint v { bins a = {0}; bins b = {1}; } endgroup
  covergroup gD; option.per_instance = 1; c1: coverpoint v { bins a = {0}; bins b = {1}; option.weight = 0; } c2: coverpoint v { bins a = {0}; bins b = {1}; } endgroup
  covergroup gE; c1: coverpoint v { bins a = {0}; bins b = {1}; type_option.weight = 0; } c2: coverpoint v { bins a = {0}; bins b = {1}; } endgroup
  covergroup gF; option.per_instance = 1; c1: coverpoint v { bins a = {0}; bins b = {1}; option.weight = 0; } endgroup
  covergroup gG;
    c1: coverpoint v { bins a = {0}; bins b = {1}; option.weight = 0; }
    c2: coverpoint v { bins a = {0}; option.weight = 1; }
    c3: coverpoint v { bins a = {1}; bins b = {2}; option.weight = 2; }
  endgroup
  gB b = new; gC c = new; gD d = new; gE e = new; gF f = new; gG g = new;
  int fails = 0;
  task automatic chk(string what, real got, real exp);
    if (got < exp - 0.005 || got > exp + 0.005) begin
      fails++; $error("%s: got %0.2f, expected %0.2f", what, got, exp);
    end
  endtask
  // cov: coverage of the two-bin coverpoints (all equal) in this step
  task automatic show(string t, real cov);
    real fi, ft;
    chk({t, " B inst"}, b.get_inst_coverage(), cov);
    chk({t, " C inst"}, c.get_inst_coverage(), cov);
    chk({t, " C type"}, gC::get_coverage(), cov);
    chk({t, " D inst"}, d.get_inst_coverage(), cov);
    chk({t, " D type"}, gD::get_coverage(), cov);
    chk({t, " E inst"}, e.get_inst_coverage(), cov);
    chk({t, " E type"}, gE::get_coverage(), cov);
    fi = f.get_inst_coverage(); ft = gF::get_coverage();
    if (fi < 0.0 || fi > 100.0 || ft < 0.0 || ft > 100.0) begin fails++; $error("%s F out of range", t); end
  endtask
  initial begin
    show("init", 0.0);
    chk("init G", g.get_inst_coverage(), 0.0);
    v = 0; b.sample(); c.sample(); d.sample(); e.sample(); f.sample(); g.sample();
    show("v0", 50.0);
    // c1 (weight 0) is out of the average: c2 = 100 (weight 1), c3 = 0 (weight 2)
    chk("v0 G", g.get_inst_coverage(), 100.0 / 3);
    v = 1; b.sample(); c.sample(); d.sample(); e.sample(); f.sample(); g.sample();
    show("v1", 100.0);
    // c2 = 100 (weight 1), c3 = 50 (weight 2)
    chk("v1 G", g.get_inst_coverage(), (100.0 + 2 * 50.0) / 3);
    if (fails == 0) $display("PASSED");
  end
endmodule
