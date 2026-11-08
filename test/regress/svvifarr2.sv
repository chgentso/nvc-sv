// Expected values: IEEE 1800-2017 25.9 (virtual interface arrays refer to interface instances; writes through the
// virtual interface update the instance), 7.6 (array assignment)
interface svvifarr2_if(input logic clk);
  logic [7:0] d;
endinterface
module svvifarr2;
  logic clk;
  svvifarr2_if x [3] (clk);
  virtual svvifarr2_if va[3];
  virtual svvifarr2_if vb[3];
  int i;
  int errs = 0;
  initial begin
    va[1] = x[1];
    va[1].d = 8'h42;
    #1;
    if (x[1].d !== 8'h42 || va[1].d !== 8'h42) begin errs++; $error("step1: %h %h", x[1].d, va[1].d); end
    va = x;
    for (i = 0; i < 3; i++) va[i].d = i + 1;
    #1;
    if (x[0].d !== 8'h01 || x[1].d !== 8'h02 || x[2].d !== 8'h03)
      begin errs++; $error("step2: %h %h %h", x[0].d, x[1].d, x[2].d); end
    vb = va;
    vb[2].d = 8'h99;
    #1;
    if (x[0].d !== 8'h01 || x[1].d !== 8'h02 || x[2].d !== 8'h99)
      begin errs++; $error("step3: %h %h %h", x[0].d, x[1].d, x[2].d); end
    if (errs == 0) $display("PASSED");
  end
endmodule
