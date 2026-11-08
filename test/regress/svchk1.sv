checker chk_pair(logic a, b, event clk);
  default clocking @(clk); endclocking
  a1: assert property (a |=> b);
  c1: cover property (a ##1 b);
endchecker
module svchk1;
  logic clk = 0, a = 0, b = 0;
  always #5 clk = ~clk;
  chk_pair p1(a, b, posedge clk);
  initial begin
    @(negedge clk); a = 1; b = 0;
    @(negedge clk); a = 0; b = 1;
    @(negedge clk); a = 1; b = 1;
    @(negedge clk); a = 0; b = 0;
    @(negedge clk);
    $finish;
  end
endmodule
