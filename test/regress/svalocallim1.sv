// Limits of function calls with local variable arguments in an assertion
// (16.10): more than six arguments
// The assertion is rejected with a diagnostic naming the construct.
module svalocallim1;
  logic clk = 0;
  int c = 0;
  always #5 clk = ~clk;
  always @(posedge clk) c <= c + 1;
  function automatic int sum7(int a, int b, int d, int e, int f, int g, int h);
    return a + b + d + e + f + g + h;
  endfunction
  property q;
    int x;
    @(posedge clk) (1, x = c) |-> ##1 (sum7(x, x, x, x, x, x, x) == 7 * (c - 1));
  endproperty
  a: assert property (q);
  initial #20 $finish;
endmodule
