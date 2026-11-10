// Self-checking. Expected values: IEEE 1800-2017 23.11 (bind inserts an
// instance into every instance of the target module) and 27.5 (a conditional
// generate construct elaborates only the branch whose constant expression
// selects it, so a bind directive in an untaken branch has no effect; it may
// name a block label).
module svbindgen1_chk_a(input logic [3:0] v, output logic [3:0] seen);
  always @(v) seen = v;
endmodule

module svbindgen1_chk_b(input logic [3:0] v, output logic [3:0] seen);
  always @(v) seen = v + 1;
endmodule

module svbindgen1_dut(input logic [3:0] v);
endmodule

module svbindgen1_bind;
  if (1) begin : gen_a
    bind svbindgen1_dut svbindgen1_chk_a chk_a(.v(v), .seen());
  end
  if (0) begin : gen_b
    bind svbindgen1_dut svbindgen1_chk_b chk_b(.v(v), .seen());
  end
endmodule

module svbindgen1;
  logic [3:0] a = 0;
  int errs = 0;
  svbindgen1_dut dut(.v(a));
  svbindgen1_bind b();
  initial begin
    #1 a = 5;
    #1;
    if (dut.chk_a.seen !== 4'd5) begin errs++; $error("chk_a.seen = %0d", dut.chk_a.seen); end
    if (errs == 0) $display("PASSED");
  end
endmodule
