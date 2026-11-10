// Self-checking. IEEE 1800-2017 19.5.1 ($ in a value range of a coverpoint bin
// is the minimum or maximum value of the coverpoint) and 19.6.1 (binsof(cp)
// intersect {range}: the range may use $ in the same way; ignore_bins removes
// the selected cross bins from the coverage, 19.11).
module svcovxdollar1;
  bit [1:0] a;
  bit [1:0] b;
  covergroup cg;
    ca: coverpoint a { bins a0 = {0}; bins a1 = {1}; }
    cb: coverpoint b { bins b[] = {[0:3]}; }
    x: cross ca, cb {
      ignore_bins ig = binsof(ca) intersect {1} && binsof(cb) intersect {[1:$]};
    }
  endgroup
  cg c = new;
  int errs = 0;
  task automatic smp(bit [1:0] x, bit [1:0] y);
    a = x; b = y;
    c.sample();
  endtask
  initial begin
    // Cross bins left: (a0,b0..b3) and (a1,b0): 5 of them
    smp(0, 0); smp(0, 1); smp(0, 2); smp(0, 3);
    smp(1, 3);            // Ignored: not a bin of the cross
    if (c.x.get_inst_coverage() < 79.9 || c.x.get_inst_coverage() > 80.1) begin
      errs++; $error("cross coverage %f expected 80", c.x.get_inst_coverage());
    end
    smp(1, 0);
    if (c.x.get_inst_coverage() < 99.9) begin
      errs++; $error("cross coverage %f expected 100", c.x.get_inst_coverage());
    end
    if (errs == 0) $display("PASSED");
  end
endmodule
