// Self-checking. Expected values: IEEE 1800-2017 23.11 (a bind directive
// inserts an instance into the target module in the elaborated design; a
// directive in a module which is not part of the design, here never
// instantiated and not a top, has no effect) and 23.3.1 (modules which are not
// instantiated are not elaborated).
module svbindscope1_bad(input logic v);
  initial $error("bind in a module outside the design was applied");
endmodule

module svbindscope1_good(input logic v, output logic seen);
  initial seen = 1'b1;
endmodule

module svbindscope1_dut(input logic v);
endmodule

module svbindscope1_unused;
  bind svbindscope1_dut svbindscope1_bad u_bad(.v(v));
endmodule

module svbindscope1_used;
  bind svbindscope1_dut svbindscope1_good u_good(.v(v), .seen());
endmodule

module svbindscope1;
  logic a = 0;
  int errs = 0;
  svbindscope1_dut dut(.v(a));
  svbindscope1_used u();
  initial begin
    #1;
    if (dut.u_good.seen !== 1'b1) begin errs++; $error("good bind missing"); end
    if (errs == 0) $display("PASSED");
  end
endmodule
