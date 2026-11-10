// Self-checking. Expected values: IEEE 1800-2017 23.11 (bind scope :
// instance_list: the interface is bound into the listed instances only; the
// hierarchical names are relative to the module which contains the directive).
interface svbindrelpath1_if(input logic [3:0] v, output logic [3:0] seen);
  always @(v) seen = v;
endinterface

module svbindrelpath1_leaf(input logic [3:0] v);
endmodule

module svbindrelpath1_mid(input logic [3:0] v);
  svbindrelpath1_leaf u_leaf(.v(v));
endmodule

module svbindrelpath1;
  logic [3:0] a = 0, b = 0;
  int errs = 0;
  svbindrelpath1_mid dut(.v(a));
  svbindrelpath1_mid other(.v(b));
  bind svbindrelpath1_leaf : dut.u_leaf svbindrelpath1_if u_chk(.v(v), .seen());
  initial begin
    #1 a = 5; b = 6;
    #1;
    if (dut.u_leaf.u_chk.seen !== 4'd5) begin errs++; $error("seen = %0d", dut.u_leaf.u_chk.seen); end
    if (errs == 0) $display("PASSED");
  end
endmodule
