// A bind target may name an instance inside a generate block of another
// instance (IEEE 1800-2017 23.11, 27.5). The bound instance goes into that
// instance only; the sibling instance of the same module is untouched.
module svbindgen2_leaf(input logic clk);
endmodule

module svbindgen2_core #(parameter bit EN = 1) (input logic clk);
  if (EN) begin : gen_on
    svbindgen2_leaf u_leaf(.clk);
  end
endmodule

interface svbindgen2_probe(input logic clk);
  initial begin
    #1;
    if ($sformatf("%m") != "svbindgen2.dut.gen_on.u_leaf.u_probe")
      $fatal(1, "unexpected bind site %m");
    $display("PASSED");
  end
endinterface

module svbindgen2;
  logic clk = 0;
  svbindgen2_core #(.EN(1)) dut(.clk);
  svbindgen2_core #(.EN(0)) other(.clk);
  bind dut.gen_on.u_leaf svbindgen2_probe u_probe(.clk);
  initial #2 $finish;
endmodule
