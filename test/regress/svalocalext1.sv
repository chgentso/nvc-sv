// Local variables of assertions (IEEE 1800-2017 16.10, 16.12.14, 16.8):
//  - a function called with the values of local variables as arguments,
//    including more than three arguments;
//  - a local variable in the condition of accept_on / reject_on, with the
//    value it has where the abort operator starts;
//  - initial values of local variables evaluated in the order of
//    declaration so that one can use an earlier variable;
//  - `and` with locals which are initialised, assigned by one operand and
//    read by the other.
// Each property has its own counter of failures and the expected number is
// derived from the sampled count c: the attempt starts at tick 3 (c == 3).
module svalocalext1;
  logic clk = 0;
  int c = 0;
  int errors = 0;
  int f[0:15];
  always #5 clk = ~clk;
  always @(posedge clk) c <= c + 1;
  function automatic int sum4(int a, int b, int d, int e);
    return a + b + d + e;
  endfunction
  function automatic logic [7:0] lo8(logic [31:0] a); return a[7:0]; endfunction
  property q1;
    int x;
    @(posedge clk) (1, x = c) |-> ##1 (sum4(x, x, x, x) == 4 * (c - 1));
  endproperty
  a1: assert property (q1) else f[1]++;
  property q1b;
    int x;
    @(posedge clk) (1, x = lo8(c) + sum4(c, 1, 2, 3))
      |-> ##1 (x == (c - 1) + 6 + (c - 1));
  endproperty
  a1b: assert property (q1b) else f[2]++;
  // The abort holds at tick 5 (c == 5 > x + 1 with x == 3): accept_on makes
  // the attempt pass although ##3 c == 100 never holds
  property q2;
    int x;
    @(posedge clk) (c == 3, x = c) |-> accept_on(c > x + 1) (##3 c == 100);
  endproperty
  a2: assert property (q2) else f[3]++;
  // reject_on fails the attempt at tick 5 although c == 6 holds at tick 6
  property q3;
    int x;
    @(posedge clk) (c == 3, x = c) |-> reject_on(c == x + 2) (##3 c == 6);
  endproperty
  a3: assert property (q3) else f[4]++;
  // decided at tick 4 before the abort condition holds at tick 5
  property q3b;
    int x;
    @(posedge clk) (c == 3, x = c) |-> reject_on(c == x + 2) (##1 c == 4);
  endproperty
  a3b: assert property (q3b) else f[5]++;
  property q4;
    int x = c; int y = x + 1;
    @(posedge clk) 1 |-> ##1 (y == c);
  endproperty
  a4: assert property (q4) else f[6]++;
  property q5;
    int x = 5; int y = 7;
    @(posedge clk) c == 3 |-> ((1, x = x + c) ##1 (x == 8))
      and ((1, y = y + 1) ##1 (y == 8));
  endproperty
  a5: assert property (q5) else f[7]++;
  // each operand of and has its own copy of the variable
  property q6;
    int x = 5;
    @(posedge clk) c == 3 |-> ((1, x = x + c) ##1 (x == 8))
      and (1 ##1 (x == 5));
  endproperty
  a6: assert property (q6) else f[8]++;
  // the variable assigned by only one operand flows out of the and
  property q7;
    int x = 5;
    @(posedge clk) c == 3 |-> (((1, x = x + c) ##1 (x == 8)) and (1 ##1 1))
      ##1 (x == 8);
  endproperty
  a7: assert property (q7) else f[9]++;
  initial begin
    #100;
    for (int i = 1; i <= 9; i++)
      if ((i == 4 ? f[i] != 1 : f[i] != 0)) begin
        $error("property %0d failed %0d times", i, f[i]); errors++;
      end
    if (errors == 0) $display("PASSED");
    $finish;
  end
endmodule
