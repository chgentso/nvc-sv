// A distribution in parentheses as the consequent of an implication, also nested, under a foreach or
// an if in a constraint; and a chain of unary operators (-~x) in an expression.
class A;
  rand bit [3:0] a[4];
  rand bit [3:0] b[4];
  bit gate, gateB;
  constraint c1 { foreach (a[i]) { gate -> (a[i] dist { 4'd0 := 3, [4'd1 : 4'd4] := 1 }); } }
  constraint c2 { foreach (b[i]) { gate -> (gateB -> (b[i] dist { 4'd9 := 1, 4'd10 := 1 })); } }
endclass
module svcrvdistparen1;
  A x;
  int fails = 0, seen0 = 0, seen9 = 0;
  bit [7:0] c = 8'h5a;
  initial begin
    x = new;
    if (-~c != 8'(c + 1)) begin fails++; $error("-~c = %0d", -~c); end
    if (!!~c != 1 || ~-c != 8'(c - 1)) begin fails++; $error("unary chain"); end
    x.gate = 1; x.gateB = 1;
    repeat (50) begin
      if (!x.randomize()) begin fails++; $error("randomize failed"); end
      foreach (x.a[i]) begin
        if (x.a[i] > 4) begin fails++; $error("a[%0d]=%0d", i, x.a[i]); end
        if (x.a[i] == 0) seen0++;
      end
      foreach (x.b[i]) begin
        if (x.b[i] != 9 && x.b[i] != 10) begin fails++; $error("b[%0d]=%0d", i, x.b[i]); end
        if (x.b[i] == 9) seen9++;
      end
    end
    if (seen0 == 0 || seen9 == 0) begin fails++; $error("buckets not all reached"); end
    x.gate = 0;
    if (!x.randomize()) begin fails++; $error("randomize failed with gate off"); end
    if (fails == 0) $display("PASSED");
  end
endmodule
