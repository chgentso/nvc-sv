// size() of a random-size dynamic array inside a foreach guard or body must be the size chosen by the
// solver (IEEE 1800-2017 18.5.8.1, 7.5), also on the very first call when the array is still empty.
class A;
  rand bit [7:0] d[];
  constraint c { d.size() == 4; foreach (d[i]) i < d.size() - 1 -> d[i] < d[i+1]; }
endclass
class B;
  rand bit [7:0] d[];
  constraint c { d.size() == 4; foreach (d[i]) if (i < d.size() - 1) d[i] < d[i+1]; }
endclass
class C;
  rand bit [7:0] d[];
  constraint c { d.size() inside {[3:6]}; foreach (d[i]) (i == d.size() - 1) -> (d[i] == 200); }
endclass
module svcrvsizeforeach1;
  A a; B b; C c;
  int fails = 0;
  initial begin
    a = new; b = new; c = new;
    repeat (20) begin
      if (!a.randomize() || a.d.size() != 4 || !(a.d[0] < a.d[1] && a.d[1] < a.d[2] && a.d[2] < a.d[3])) begin
        fails++; $error("A: %p", a.d);
      end
      if (!b.randomize() || b.d.size() != 4 || !(b.d[0] < b.d[1] && b.d[1] < b.d[2] && b.d[2] < b.d[3])) begin
        fails++; $error("B: %p", b.d);
      end
      if (!c.randomize() || c.d.size() < 3 || c.d.size() > 6 || c.d[c.d.size() - 1] != 200) begin
        fails++; $error("C: %p", c.d);
      end
    end
    if (fails == 0) $display("PASSED");
  end
endmodule
