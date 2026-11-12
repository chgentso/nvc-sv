// An assertion control task of an interface is called from the testbench with
// a name relative to the interface, which holds an up-reference into the
// module it is bound to (OpenTitan's edn_assert_if): the assertion is switched
// off and on again.
module svassertoffrel1_sm(input logic clk, input logic a);
  int fails = 0;
  u_state_regs_A: assert property (@(posedge clk) a) else fails++;
endmodule

module svassertoffrel1_core(input logic clk, input logic a);
  svassertoffrel1_sm u_sm(.clk, .a);
endmodule

interface svassertoffrel1_ctl();
  task automatic assert_off();
    $assertoff(0, u_sm.u_state_regs_A);
  endtask
  task automatic assert_on();
    $asserton(0, u_sm.u_state_regs_A);
  endtask
endinterface

module svassertoffrel1;
  logic clk = 0, a = 1;
  always #5 clk = ~clk;
  svassertoffrel1_core dut(.clk, .a);
  bind svassertoffrel1_core svassertoffrel1_ctl ctl();
  int f1, f2, f3;

  initial begin
    #12 a = 0;
    #20 f1 = dut.u_sm.fails;
    dut.ctl.assert_off();
    #30 f2 = dut.u_sm.fails;
    dut.ctl.assert_on();
    #30 f3 = dut.u_sm.fails;
    if (f1 == 0 || f2 != f1 || f3 <= f2)
      $fatal(1, "failures %0d %0d %0d", f1, f2, f3);
    $display("PASSED");
    $finish;
  end
endmodule
