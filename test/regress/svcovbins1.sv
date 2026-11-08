// Coverpoint bins: ranges, bins[] arrays, ignore_bins, default, crosses, sample() arguments
// Expected values: IEEE 1800-2017 19.5 (bins; ignore_bins values are removed from the
// coverpoint; default bin), 19.5.1 (bins[]), 19.6 (cross), 19.8.1 (covergroup sample
// arguments), 19.11 (coverage = covered bins / total bins, default/ignore/illegal bins
// not counted; covergroup coverage = weighted average of its items, weight 1)
module svcovbins1;
  bit [3:0] v; bit w; bit clk;
  int errs = 0;
  covergroup cg;
    option.per_instance = 1;
    cp_v: coverpoint v { bins lo = {[0:3]}; bins mid[] = {[4:6]}; bins hi = {[8:15]}; ignore_bins ig = {7}; }
    cp_w: coverpoint w;
    cp_a: coverpoint v;
    x: cross cp_v, cp_w;
  endgroup
  cg c = new;
  covergroup cg2 with function sample(int s);
    coverpoint s { bins a = {1}; bins b = {[2:3]}; bins d = default; }
  endgroup
  cg2 c2 = new;
  function automatic void chkr(string m, real got, real exp);
    if (got > exp + 1.0e-6 || got < exp - 1.0e-6) begin
      $error("%s: got %f expected %f", m, got, exp); errs++;
    end
  endfunction
  initial begin
    // cp_v has 5 bins (lo, mid[0..2], hi), cp_w 2, cp_a 16, cross 5*2 = 10
    chkr("0.inst", c.get_inst_coverage(), 0.0);
    chkr("0.type", cg::get_coverage(), 0.0);
    chkr("0.cpv", c.cp_v.get_inst_coverage(), 0.0);
    v = 1; c.sample();
    chkr("1.inst", c.get_inst_coverage(), (20.0 + 50.0 + 6.25 + 10.0) / 4);
    chkr("1.type", cg::get_coverage(), (20.0 + 50.0 + 6.25 + 10.0) / 4);
    chkr("1.cpv", c.cp_v.get_inst_coverage(), 20.0);
    chkr("1.cpw", c.cp_w.get_inst_coverage(), 50.0);
    chkr("1.cpa", c.cp_a.get_inst_coverage(), 6.25);
    chkr("1.x", c.x.get_inst_coverage(), 10.0);
    v = 5; w = 1; c.sample();
    chkr("2.inst", c.get_inst_coverage(), (40.0 + 100.0 + 12.5 + 20.0) / 4);
    chkr("2.type", cg::get_coverage(), (40.0 + 100.0 + 12.5 + 20.0) / 4);
    chkr("2.cpv", c.cp_v.get_inst_coverage(), 40.0);
    chkr("2.cpw", c.cp_w.get_inst_coverage(), 100.0);
    chkr("2.cpa", c.cp_a.get_inst_coverage(), 12.5);
    chkr("2.x", c.x.get_inst_coverage(), 20.0);
    // 7 is an ignored value: no cp_v bin and no cross bin, but an automatic bin of cp_a
    v = 7; c.sample();
    chkr("3.inst", c.get_inst_coverage(), (40.0 + 100.0 + 18.75 + 20.0) / 4);
    chkr("3.cpv", c.cp_v.get_inst_coverage(), 40.0);
    // cg2: bins a and b count, the default bin does not
    c2.sample(1);
    chkr("c2.1", c2.get_inst_coverage(), 50.0);
    c2.sample(9);
    chkr("c2.2", c2.get_inst_coverage(), 50.0);
    c2.sample(3);
    chkr("c2.3", c2.get_inst_coverage(), 100.0);
    chkr("c2.3t", cg2::get_coverage(), 100.0);
    if (errs == 0) $display("PASSED");
  end
endmodule
