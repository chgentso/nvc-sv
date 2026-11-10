// Self-checking. Expected values: IEEE 1800-2017 23.11 (a bind directive in a
// module may name an instance of that module as the target: only that instance
// gets the bound instantiation; a second instance of the same module does not).
module svbindinst2_chk(input logic [3:0] v, output logic [3:0] seen);
  always @(v) seen = v;
endmodule

module svbindinst2_dut(input logic [3:0] v, output int n);
  always_comb n = v;
endmodule

module svbindinst2;
  logic [3:0] a = 0, b = 0;
  int na, nb, errs = 0;
  svbindinst2_dut dut(.v(a), .n(na));
  svbindinst2_dut other(.v(b), .n(nb));
  bind dut svbindinst2_chk chk(.v(v), .seen());
  initial begin
    #1 a = 3; b = 4;
    #1;
    if (na !== 3 || nb !== 4) begin errs++; $error("na=%0d nb=%0d", na, nb); end
    if (dut.chk.seen !== 4'd3) begin errs++; $error("dut.chk.seen = %0d", dut.chk.seen); end
    if (errs == 0) $display("PASSED");
  end
endmodule
