// Hitting an illegal bin is a run-time error and the value is not counted
// IEEE 1800-2017 19.5.6 (illegal_bins of a coverpoint, also of a transition: an illegal bin
// that is hit causes a run-time error), 19.6.3 (illegal_bins of a cross), 19.11 (illegal
// bins are not counted in coverage).
module svcovill1;
  bit [3:0] v; bit [1:0] w;
  covergroup cg;
    cp: coverpoint v { bins lo = {[0:3]}; illegal_bins bad = {7}; }
    ct: coverpoint v { bins t = (1 => 2); illegal_bins bt = (2 => 7); }
    cw: coverpoint w { bins a = {0}; bins b = {1}; }
    cx: cross cp, cw { illegal_bins bx = binsof(cp.lo) intersect {3} && binsof(cw.b); }
  endgroup
  cg c = new;
  initial begin
    v = 1; w = 0; c.sample();
    v = 2; c.sample();
    v = 7; c.sample();
    v = 7; c.sample();
    v = 3; w = 1; c.sample();
    if (c.cp.get_inst_coverage() == 100.0) $display("PASSED");
  end
endmodule
