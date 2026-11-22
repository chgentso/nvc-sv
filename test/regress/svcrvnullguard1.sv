// A test that an object handle is not null guards the use of the handle in a constraint
// (IEEE 1800-2017 18.5.9, 18.5.7): with a null handle the guarded constraint is not applied and
// randomize() succeeds; with an object it is applied.
class It;
  rand bit [7:0] v;
endclass
class A;
  rand It h;
  rand It k;
  rand bit [7:0] x;
  constraint c1 { if (h != null) h.v == 7; }
  constraint c2 { (k != null) -> k.v == 9; }
  constraint c3 { x < 20; }
endclass
module svcrvnullguard1;
  A a;
  int fails = 0;
  initial begin
    a = new;
    repeat (5) begin
      if (!a.randomize() || a.x >= 20) begin fails++; $error("null handles: rc failed, x=%0d", a.x); end
    end
    a.h = new;
    repeat (5) begin
      if (!a.randomize() || a.h.v != 7) begin fails++; $error("h set: v=%0d", a.h.v); end
    end
    a.k = new;
    repeat (5) begin
      if (!a.randomize() || a.h.v != 7 || a.k.v != 9) begin fails++; $error("both set"); end
    end
    if (fails == 0) $display("PASSED");
  end
endmodule
