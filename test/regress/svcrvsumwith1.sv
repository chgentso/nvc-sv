// sum() with (item) / (int'(item)) over a dynamic array or queue with a random size in a constraint:
// the sum is taken over the elements the solver chooses (IEEE 1800-2017 7.12.3, 18.5.8).
class A;
  rand bit [7:0] q[$];
  constraint c { q.size() inside {[3:6]}; q.sum() with (int'(item)) == 100; }
endclass
class B;
  rand bit [7:0] d[];
  constraint c { d.size() inside {[3:5]}; d.sum() with (int'(item)) == 400; }
endclass
class C;
  rand int s[];
  constraint c { s.size() == 4; s.sum() with (item) == -20; foreach (s[i]) s[i] inside {[-30:30]}; }
endclass
module svcrvsumwith1;
  A a; B b; C c;
  int fails = 0, t;
  initial begin
    a = new; b = new; c = new;
    repeat (20) begin
      if (!a.randomize()) begin fails++; $error("A failed"); end
      else begin
        t = 0; foreach (a.q[i]) t += a.q[i];
        if (t != 100 || a.q.size() < 3 || a.q.size() > 6) begin fails++; $error("A: size %0d sum %0d", a.q.size(), t); end
      end
      if (!b.randomize()) begin fails++; $error("B failed"); end
      else begin
        t = 0; foreach (b.d[i]) t += b.d[i];
        if (t != 400 || b.d.size() < 3) begin fails++; $error("B: size %0d sum %0d", b.d.size(), t); end
      end
      if (!c.randomize()) begin fails++; $error("C failed"); end
      else begin
        t = 0; foreach (c.s[i]) t += c.s[i];
        if (t != -20 || c.s.size() != 4) begin fails++; $error("C: size %0d sum %0d", c.s.size(), t); end
      end
    end
    if (fails == 0) $display("PASSED");
  end
endmodule
