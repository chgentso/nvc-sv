// bins c[N] over fewer than N values
// IEEE 1800-2017 19.5: a fixed number of bins N is created and the values are
// distributed over them, so with fewer values than N some bins are empty;
// 19.11: every bin of the coverpoint counts in the denominator, so the empty
// bins stay uncovered.  nvc warns that the extra bins are empty.
module svcovfewbins1;
  bit [3:0] v;
  covergroup cg;
    a: coverpoint v { bins c[5] = {1, 2, 3}; }
    b: coverpoint v { bins r[4] = {[8:9]}; bins s = {15}; }
    d: coverpoint v { bins e[2] = {4, 5}; }
  endgroup
  cg g = new;
  int errs = 0;
  function automatic void chkr(string m, real got, real exp);
    if (got > exp + 1.0e-6 || got < exp - 1.0e-6) begin
      $error("%s: got %f expected %f", m, got, exp); errs++;
    end
  endfunction
  initial begin
    v = 1; g.sample(); v = 2; g.sample(); v = 3; g.sample();
    chkr("a", g.a.get_inst_coverage(), 60.0);
    v = 8; g.sample(); v = 9; g.sample(); v = 15; g.sample();
    chkr("b", g.b.get_inst_coverage(), 60.0);
    v = 4; g.sample();
    chkr("d", g.d.get_inst_coverage(), 50.0);
    if (errs == 0) $display("PASSED");
  end
endmodule
