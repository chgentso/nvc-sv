// Limits of function calls with local variable arguments in an assertion
// (16.10): more than 32 calls in one assertion
// The assertion is rejected with a diagnostic naming the construct.
module svalocallim3;
  logic clk = 0;
  int c = 0;
  always #5 clk = ~clk;
  always @(posedge clk) c <= c + 1;
  function automatic int f(int a); return a; endfunction
  property q;
    int x;
    @(posedge clk) (1, x = c) |-> ##1 ((f(x) == x) && (f(x) == x) && (f(x) == x) && (f(x) == x) && (f(x) == x) && (f(x) == x) && (f(x) == x) && (f(x) == x) && (f(x) == x) && (f(x) == x) && (f(x) == x) && (f(x) == x) && (f(x) == x) && (f(x) == x) && (f(x) == x) && (f(x) == x) && (f(x) == x) && (f(x) == x) && (f(x) == x) && (f(x) == x) && (f(x) == x) && (f(x) == x) && (f(x) == x) && (f(x) == x) && (f(x) == x) && (f(x) == x) && (f(x) == x) && (f(x) == x) && (f(x) == x) && (f(x) == x) && (f(x) == x) && (f(x) == x) && (f(x) == x));
  endproperty
  a: assert property (q);
  initial #20 $finish;
endmodule
