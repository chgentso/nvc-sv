// Subroutines of the compilation unit may use others declared later, and
// system task arguments and event controls may refer to later declarations

task automatic outer(int a);
  inner(a);
  if (twice(a) != 8) $fatal(1, "FAILED twice");
endtask

task automatic inner(int a);
  if (a != 4) $fatal(1, "FAILED inner");
endtask

function int twice(int a);
  return 2 * a;
endfunction

module svfwdref1;
  reg clk = 0;
  task automatic mt(int a);
    mfn(a);
  endtask
  function void mfn(int a);
    if (a != 5) $fatal(1, "FAILED mfn");
  endfunction
  always @(posedge clk) $display("w=%0d", w);
  initial begin
    outer(4);
    mt(5);
    #1 clk = 1;
    #1 $display("PASSED");
  end
  integer w = 5;
endmodule
