// Covergroup sampled on a clock event with iff, ignore/illegal bins, transitions, cross
// Expected values: IEEE 1800-2017 19.4 (clocking event with iff), 19.5 (ignore_bins and
// illegal_bins values are removed from the bins), 19.5.1 (bins[]), 19.5.2 (transitions), 19.6
// (cross with ignore_bins/binsof), 19.8 (explicit sample), 19.11 (coverage arithmetic).
// Stimulus changes 1 time unit after each clock edge, so the sampled values are the ones
// assigned before the edge.
// Samples (v,m) at edges 5..75: (0,0) (1,1) (2,2) (5,3) (9,1) (3,2) (0,3) (7,0); later edges
// are skipped because en is 0.
//   cp_v: bins low, mid[4], mid[5], mid[6], hi (7 ignored, 15 illegal): hit low, mid[5], hi = 3/5
//   cp_m: m0, m1, rest all hit = 3/3;  cp_t: up (0=>1=>2) and wrap (3=>0) both hit = 2/2
//   x: 5 x 3 bins minus (low,m0) = 14; hit (low,m1) (low,rest) (mid5,rest) (hi,m1) = 4/14
`timescale 1ns/1ps
module svcovevent1;
  logic clk = 0; always #5 clk = ~clk;
  bit [3:0] v; bit [1:0] m; bit en;
  int errs = 0;
  covergroup cg @(posedge clk iff en);
    option.per_instance = 1;
    option.name = "cg_v";
    cp_v: coverpoint v {
      bins low = {[0:3]};
      bins mid[] = {[4:7]};
      bins hi  = {[8:15]};
      ignore_bins ign = {7};
      illegal_bins ill = {15};
    }
    cp_m: coverpoint m { bins m0 = {0}; bins m1 = {1}; bins rest = {2, 3}; }
    cp_t: coverpoint v { bins up = (0 => 1 => 2); bins wrap = (3 => 0); }
    x: cross cp_v, cp_m { ignore_bins both = binsof(cp_v.low) && binsof(cp_m.m0); }
  endgroup
  covergroup cg2 with function sample(bit [3:0] s);
    coverpoint s { bins a = {1}; bins b = {2}; bins c = {3}; }
  endgroup
  cg c1 = new;
  cg2 c2 = new;
  function automatic void chkr(string msg, real got, real exp);
    if (got > exp + 1.0e-6 || got < exp - 1.0e-6) begin
      $error("%s: got %f expected %f", msg, got, exp); errs++;
    end
  endfunction
  initial begin
    en = 1;
    v = 0; m = 0; @(posedge clk); #1;
    v = 1; m = 1; @(posedge clk); #1;
    v = 2; m = 2; @(posedge clk); #1;
    v = 5; m = 3; @(posedge clk); #1;
    v = 9; m = 1; @(posedge clk); #1;
    v = 3; m = 2; @(posedge clk); #1;
    v = 0; m = 3; @(posedge clk); #1;
    v = 7; m = 0; @(posedge clk); #1;
    en = 0; v = 6; @(posedge clk); @(posedge clk);
    c2.sample(1); c2.sample(2);
    #1;
    chkr("cp_v", c1.cp_v.get_inst_coverage(), 60.0);
    chkr("cp_m", c1.cp_m.get_inst_coverage(), 100.0);
    chkr("cp_t", c1.cp_t.get_inst_coverage(), 100.0);
    chkr("x", c1.x.get_inst_coverage(), 100.0 * 4 / 14);
    chkr("cg", c1.get_inst_coverage(), (60.0 + 100.0 + 100.0 + 100.0 * 4 / 14) / 4);
    chkr("cg2", c2.get_inst_coverage(), 200.0 / 3);
    c2.sample(3);
    chkr("cg2 after", c2.get_inst_coverage(), 100.0);
    c2.stop(); c2.sample(9); c2.start();
    chkr("cg2 stopped", c2.get_inst_coverage(), 100.0);
    if (errs == 0) $display("PASSED");
    $finish;
  end
endmodule
