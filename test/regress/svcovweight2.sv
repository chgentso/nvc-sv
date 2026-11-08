// IEEE 1800-2017 19.7 (Table 19-1): option.weight of a coverpoint only sets
// its weight in the weighted average of its covergroup (19.11).  A weight of
// zero does not stop the coverpoint from being sampled and does not change
// its own coverage: get_inst_coverage() of the coverpoint returns the
// covered bins over all bins, and the coverpoint is left out of the
// covergroup's weighted average.
module svcovweight2;
  int a, b;
  int errs;

  covergroup cg;
    option.per_instance = 1;
    ca: coverpoint a { bins x[] = {0, 1, 2, 3}; option.weight = 0; }
    cb: coverpoint b { bins y[] = {0, 1}; }
  endgroup

  cg g = new;

  task automatic chk(string what, real got, real exp);
    if (got < exp - 0.005 || got > exp + 0.005) begin
      $error("%s: got %0.2f, expected %0.2f", what, got, exp);
      errs++;
    end
  endtask

  initial begin
    chk("ca before sampling", g.ca.get_inst_coverage(), 0.0);
    a = 0; b = 0; g.sample();
    chk("ca after one value", g.ca.get_inst_coverage(), 25.0);
    chk("cb after one value", g.cb.get_inst_coverage(), 50.0);
    chk("group after one value", g.get_inst_coverage(), 50.0);
    a = 1; g.sample();
    a = 2; g.sample();
    chk("ca after three values", g.ca.get_inst_coverage(), 75.0);
    chk("group after three values", g.get_inst_coverage(), 50.0);
    b = 1; g.sample();
    chk("group with cb covered", g.get_inst_coverage(), 100.0);
    chk("ca still 75", g.ca.get_inst_coverage(), 75.0);
    if (errs == 0) $display("PASSED");
  end
endmodule
