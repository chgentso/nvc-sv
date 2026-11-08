// Self-checking test of sum() over dynamic arrays, fixed arrays and arrays of bit[7:0] in constraints,
// with a non-rand state variable as the target, and of an infeasible constraint set.
// Expected values: IEEE 1800-2017 18.5.8 (array reduction methods and foreach in constraints), 7.12.3 (the
// result of sum() has the element type, so the sum of the bit[7:0] elements of b is taken modulo 256 and
// the constraint b.sum() == 9 is checked on the 8-bit sum), 18.6.1
// (randomize() returns 0 when the constraints cannot be satisfied), 7.5 (dynamic array size constraint).
// With total = 100: a has 4 elements in [10:40] summing to 100; with total = 70 likewise; with total = 500
// the maximum is 4*40 = 160 so randomize() must fail.
class C;
  rand int a[];
  rand int fx[3];
  rand bit [7:0] b[];
  int total = 100;
  constraint c1 { a.size() == 4; a.sum() == total; foreach (a[i]) a[i] inside {[10:40]}; }
  constraint c2 { fx.sum() == 30; b.size() == 3; b.sum() == 9; foreach (b[i]) b[i] > 0; }
endclass
module svcrvdynsum1;
  C c;
  int t;
  bit [7:0] bs;
  int fails = 0;
  initial begin
    c = new;
    repeat (3) begin
      if (!c.randomize()) begin fails++; $error("randomize failed with total=100"); end
      else begin
        t = 0; foreach (c.a[i]) begin t += c.a[i]; if (c.a[i] < 10 || c.a[i] > 40) begin fails++; $error("a[%0d]=%0d out of range", i, c.a[i]); end end
        bs = 8'(c.b[0]) + 8'(c.b[1]) + 8'(c.b[2]);
        if (c.a.size() != 4 || t != 100 || c.fx[0] + c.fx[1] + c.fx[2] != 30 ||
            c.b.size() != 3 || bs != 8'd9 || c.b[0] == 0 || c.b[1] == 0 || c.b[2] == 0) begin
          fails++; $error("bad solution: size %0d sum %0d fx %0d b %0d", c.a.size(), t, c.fx[0] + c.fx[1] + c.fx[2], bs);
        end
      end
    end
    c.total = 70;
    if (!c.randomize()) begin fails++; $error("randomize failed with total=70"); end
    else begin
      t = 0; foreach (c.a[i]) t += c.a[i];
      if (c.a.size() != 4 || t != 70) begin fails++; $error("total=70: size %0d sum %0d", c.a.size(), t); end
    end
    c.total = 500;
    if (c.randomize()) begin fails++; $error("infeasible constraints were satisfied"); end
    if (fails == 0) $display("PASSED");
  end
endmodule
