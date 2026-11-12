// The assertion to control may be given as a string holding its hierarchical
// path, as well as by name: $assertoff stops it reporting and $asserton
// switches it on again (IEEE 1800-2017 20.12; a string is accepted by common
// testbenches).
module svassertoffstr1_sub(input logic clk, input logic a);
  int fails = 0;
  A_chk: assert property (@(posedge clk) a) else fails++;
endmodule

module svassertoffstr1;
  logic clk = 0, a = 1;
  always #5 clk = ~clk;
  svassertoffstr1_sub dut(.clk, .a);
  int f1, f2, f3;

  initial begin
    #12 a = 0;
    #20 f1 = dut.fails;
    $assertoff(0, "svassertoffstr1.dut.A_chk");
    #30 f2 = dut.fails;
    $asserton(0, "svassertoffstr1.dut.A_chk");
    #30 f3 = dut.fails;
    if (f1 == 0 || f2 != f1 || f3 <= f2)
      $fatal(1, "failures %0d %0d %0d", f1, f2, f3);
    $display("PASSED");
    $finish;
  end
endmodule
