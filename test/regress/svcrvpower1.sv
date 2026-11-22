// The power operator with a constant exponent in a constraint (IEEE 1800-2017 11.4.3)
class A;
  rand bit [7:0] x;
  rand int s;
  rand bit [3:0] y;
  constraint c1 { x ** 2 == 144; }
  constraint c2 { s ** -2 == 1; }
  constraint c3 { y ** 0 == 1; y ** 3 < 30; y > 1; }
endclass
module svcrvpower1;
  A a;
  int fails = 0;
  initial begin
    a = new;
    repeat (50) begin
      if (!a.randomize()) begin fails++; $error("randomize failed"); end
      else begin
        if (a.x != 12) begin fails++; $error("x=%0d", a.x); end
        if (a.s != 1 && a.s != -1) begin fails++; $error("s=%0d", a.s); end
        if (a.y < 2 || a.y > 3) begin fails++; $error("y=%0d", a.y); end
      end
    end
    if (fails == 0) $display("PASSED");
  end
endmodule
