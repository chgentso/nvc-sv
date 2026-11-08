// A reversed range in a transition bin is empty
// IEEE 1800-2017 11.4.13: a range [a:b] with a > b contains no values; 19.5.2: a
// transition step with no values never matches; 19.11: coverage = covered bins / bins.
module svcovrev2;
  bit [3:0] v;
  covergroup cg;
    t: coverpoint v { bins up = (1 => [5:3]); bins ok = (1 => 2); }
  endgroup
  cg c = new;
  int errs = 0;
  function automatic void chkr(string m, real got, real exp);
    if (got > exp + 1.0e-6 || got < exp - 1.0e-6) begin
      $error("%s: got %f expected %f", m, got, exp); errs++;
    end
  endfunction
  initial begin
    v = 1; c.sample(); v = 4; c.sample(); v = 1; c.sample(); v = 2; c.sample();
    // t: up never hit, ok hit -> 50
    chkr("t", c.t.get_inst_coverage(), 50.0);
    if (errs == 0) $display("PASSED");
  end
endmodule
