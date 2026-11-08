// Class-embedded covergroup sampled by an event
// Expected values: IEEE 1800-2017 19.4 (covergroup with a clocking event in a class),
// 19.5, 19.11 (coverage = covered bins / total bins)
`timescale 1ns/1ns
module svcovclass3;
  int errs = 0;
  function automatic void chkr(string m, real got, real exp);
    if (got > exp + 1.0e-6 || got < exp - 1.0e-6) begin
      $error("%s: got %f expected %f", m, got, exp); errs++;
    end
  endfunction
  class mon;
    bit [3:0] v;
    event ev;
    covergroup cg @ev;
      cp: coverpoint v { bins lo = {[0:7]}; bins hi = {[8:15]}; }
    endgroup
    function new();
      cg = new;
    endfunction
    task put(bit [3:0] x);
      v = x;
      -> ev;
      #1;
    endtask
  endclass
  initial begin
    automatic mon m = new;
    #1;
    chkr("0", m.cg.get_inst_coverage(), 0.0);
    m.put(3);
    chkr("1", m.cg.get_inst_coverage(), 50.0);
    m.put(12);
    chkr("2", m.cg.get_inst_coverage(), 100.0);
    if (errs == 0) $display("PASSED");
  end
endmodule
