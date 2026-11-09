// Limits of function calls with local variable arguments in an assertion
// (16.10): an argument wider than 64 bits
// The assertion is rejected with a diagnostic naming the construct.
module svalocallim2;
  logic clk = 0;
  int c = 0;
  always #5 clk = ~clk;
  always @(posedge clk) c <= c + 1;
  function automatic bit weq(logic [99:0] a, logic [99:0] b); return a == b; endfunction
  property q;
    logic [99:0] y;
    @(posedge clk) (1, y = c) |-> ##1 weq(y, c - 1);
  endproperty
  a: assert property (q);
  initial #20 $finish;
endmodule
