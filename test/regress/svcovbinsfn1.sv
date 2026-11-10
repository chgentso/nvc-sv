// Self-checking. IEEE 1800-2017 19.5.1: the set covergroup expression of a
// bin may be any expression of an array or queue type, including the value of
// a function call using the covergroup arguments; bins b[] creates one bin per
// element. Coverage = covered bins / bins (19.11).
module svcovbinsfn1;
  typedef int mask_q_t[$];
  bit [3:0] v;

  function automatic mask_q_t single_masks(int unsigned n);
    mask_q_t ret;
    for (int i = 0; i < n; i++) ret.push_back(1 << i);
    return ret;
  endfunction

  covergroup cg(int unsigned n);
    cp: coverpoint v {
      bins none = { 0 };
      bins one[] = single_masks(n);
    }
  endgroup

  cg c;
  int errs = 0;

  task automatic smp(bit [3:0] x);
    v = x;
    c.sample();
  endtask

  initial begin
    c = new(3);          // bins: none, one[1], one[2], one[4]
    smp(0);
    if (c.get_inst_coverage() < 24.9 || c.get_inst_coverage() > 25.1) begin
      errs++; $error("coverage %f expected 25", c.get_inst_coverage());
    end
    smp(1); smp(2);
    if (c.get_inst_coverage() < 74.9 || c.get_inst_coverage() > 75.1) begin
      errs++; $error("coverage %f expected 75", c.get_inst_coverage());
    end
    smp(8);               // Not a bin
    smp(4);
    if (c.get_inst_coverage() < 99.9) begin
      errs++; $error("coverage %f expected 100", c.get_inst_coverage());
    end
    if (errs == 0) $display("PASSED");
  end
endmodule
