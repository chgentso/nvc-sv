// The equivalence operator <-> in a constraint expression (IEEE 1800-2017 11.4.7, 18.5.6) and a dist
// constraint under if/else and foreach, next to implication constraints.
class A;
  rand bit [3:0] a, b, c;
  rand bit s;
  rand bit [7:0] d[3];
  constraint c1 { (a > 3) <-> (b > 3); }
  constraint c2 { if (s) c dist {1:=1, 2:=1}; else c dist {5:=1, 6:=1}; }
  constraint c3 { foreach (d[i]) d[i] dist {1:=1, [10:12]:/3}; }
  constraint c4 { (a == 0) -> (b == 0); }
  constraint c5 { ((a > 7) -> (b > 7)) <-> (c > 0); }
endclass
module svcrvequiv1;
  A x;
  int fails = 0;
  initial begin
    x = new;
    repeat (200) begin
      if (!x.randomize()) begin fails++; $error("randomize failed"); end
      else begin
        if ((x.a > 3) != (x.b > 3)) begin fails++; $error("<->: a=%0d b=%0d", x.a, x.b); end
        if (x.s ? !(x.c inside {1, 2}) : !(x.c inside {5, 6})) begin fails++; $error("if/dist: s=%0d c=%0d", x.s, x.c); end
        foreach (x.d[i]) if (!(x.d[i] == 1 || (x.d[i] >= 10 && x.d[i] <= 12))) begin fails++; $error("foreach dist: %0d", x.d[i]); end
        if (x.a == 0 && x.b != 0) begin fails++; $error("->: a=%0d b=%0d", x.a, x.b); end
      end
    end
    if (fails == 0) $display("PASSED");
  end
endmodule
