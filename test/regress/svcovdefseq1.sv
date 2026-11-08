// default sequence bins next to transition bins and value bins
// Expected values: IEEE 1800-2017 19.5.2 (transition bins; a sequence completes at the
// sample that matches its last element; default sequence bin), 19.5 (default bins), 19.8
// (get_inst_coverage with covered/total), 19.11 (default and default sequence bins are
// not counted in the coverage of the coverpoint)
module svcovdefseq1;
  bit [2:0] v;
  int errs = 0;
  covergroup g;
    cp: coverpoint v {
      bins a = (1 => 2);
      bins b = (3 => 4);
      bins other = default sequence;
    }
    cr: coverpoint v { bins a = {1}; bins d = default sequence; }
  endgroup
  g gi = new;
  function automatic void chkr(string m, real got, real exp);
    if (got > exp + 1.0e-6 || got < exp - 1.0e-6) begin
      $error("%s: got %f expected %f", m, got, exp); errs++;
    end
  endfunction
  // x: sampled value; expected covered bins of cp (of 2) and coverage of cr
  task automatic s(input [2:0] x, int exp_c, real exp_cr);
    int c, t;
    real r;
    string m = $sformatf("v=%0d", x);
    v = x; gi.sample();
    r = gi.cp.get_inst_coverage(c, t);
    if (c != exp_c || t != 2) begin $error("%s: covered/total %0d/%0d expected %0d/2", m, c, t, exp_c); errs++; end
    chkr({m, " cp"}, r, 50.0 * exp_c);
    chkr({m, " cr"}, gi.cr.get_inst_coverage(), exp_cr);
  endtask
  initial begin
    s(0, 0, 0.0);
    s(1, 0, 100.0);
    s(2, 1, 100.0);   // 1 => 2 completes
    s(3, 1, 100.0);
    s(5, 1, 100.0);
    s(3, 1, 100.0);
    s(4, 2, 100.0);   // 3 => 4 completes
    if (errs == 0) $display("PASSED");
  end
endmodule
