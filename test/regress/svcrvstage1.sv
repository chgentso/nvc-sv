// The arguments of a function called in a constraint are solved first, then the function is called
// with their values and the rest is solved (IEEE 1800-2017 18.5.12): in the class, in an object it holds
// and in an inline constraint.
class A;
  rand int x, r;
  constraint c { x inside {[100:110]}; r == add(x); }
  function int add(int v);
    return v + 1;
  endfunction
endclass

class B;
  rand int x;
  rand int y;
  constraint c { x inside {[1:5]}; y == f(x) + 1; y < 100; }
  function int f(int v);
    return v * 10;
  endfunction
endclass

class C;
  rand A a;
  rand int q;
  function new();
    a = new;
  endfunction
  constraint c { q == a.add(a.x); }
endclass

class D;
  rand int x;
  rand int y;
  constraint c { x inside {[1:5]}; y inside {[1:5]}; y == f(x); }
  function int f(int v);
    return v * 10;
  endfunction
endclass

module svcrvstage1;
  initial begin
    A a = new;
    B b = new;
    C c = new;
    D d = new;
    int ok;
    repeat (10) begin
      ok = a.randomize();
      if (ok != 1 || a.r != a.x + 1 || a.x < 100 || a.x > 110) $error("A %0d %0d %0d", ok, a.x, a.r);
      ok = b.randomize();
      if (ok != 1 || b.y != b.x * 10 + 1) $error("B %0d %0d %0d", ok, b.x, b.y);
      ok = c.randomize();
      if (ok != 1 || c.a.r != c.a.x + 1 || c.q != c.a.x + 1) $error("C %0d %0d %0d", ok, c.a.x, c.q);
    end
    ok = b.randomize() with { x == 3; };
    if (ok != 1 || b.x != 3 || b.y != 31) $error("with %0d %0d %0d", ok, b.x, b.y);
    ok = a.randomize() with { r == add(x) + 1000; };
    if (ok != 0) $error("conflict %0d", ok);

    // No value of y can be f(x) once x is chosen
    ok = d.randomize();
    if (ok != 0) $error("D %0d", ok);
    $display("PASSED");
  end
endmodule
