// A bin value outside the enumerators of an enum-typed coverpoint expression
// Expected values: IEEE 1800-2017 19.5.7 (values are evaluated in the base type of the enum,
// so ID[4] exists although 4 is not an enumerator), 19.5.1 (bins[]), 19.7 (per_instance),
// 19.11 (coverage = covered/total bins; group = average of two equal-weight coverpoints)
module svcovenum3;
  typedef enum bit [15:0] {I0=0, I1=1, I2=2, I3=3} id_e;
  class tx;
    rand id_e id;
    bit [3:0] n;
  endclass
  int errs = 0;
  covergroup cg with function sample (tx packet);
    option.per_instance = 1;
    A: coverpoint packet.id { bins ID[] = {[0:4]}; }
    B: coverpoint packet.n { bins N[] = {[0:4]}; }
  endgroup
  cg c = new;
  tx t = new;
  function automatic void chkr(string m, real got, real exp);
    if (got > exp + 1.0e-6 || got < exp - 1.0e-6) begin
      $error("%s: got %f expected %f", m, got, exp); errs++;
    end
  endfunction
  initial begin
    for (int i = 0; i < 4; i++) begin
      t.id = id_e'(i);
      t.n = i;
      c.sample(t);
      // 5 bins each, i+1 of them hit
      chkr($sformatf("i=%0d group", i), c.get_coverage(), 20.0 * (i + 1));
      chkr($sformatf("i=%0d A", i), c.A.get_inst_coverage(), 20.0 * (i + 1));
      chkr($sformatf("i=%0d B", i), c.B.get_inst_coverage(), 20.0 * (i + 1));
    end
    if (errs == 0) $display("PASSED");
  end
endmodule
