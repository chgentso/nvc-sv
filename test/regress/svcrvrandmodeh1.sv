// rand_mode on a rand object handle changes the mode of the handle only, on any other handle it changes
// every property of the object (IEEE 1800-2017 18.8).
class Foo;
  rand int a;
  rand int b;
endclass

class Bar;
  rand int x;
  rand Foo foo;
  constraint c { x inside {[1:9]}; }
  function new;
    foo = new;
  endfunction
endclass

module svcrvrandmodeh1;
  initial begin
    Bar bar = new;
    Foo other = new;
    int ok, changed;

    bar.foo.a.rand_mode(0);
    bar.foo.rand_mode(0);
    if (bar.foo.a.rand_mode() != 0 || bar.foo.b.rand_mode() != 1) $error("object modes changed");
    bar.foo.a = 15;
    bar.foo.b = 16;
    ok = bar.randomize();
    if (ok != 1 || bar.foo.a != 15 || bar.foo.b != 16) $error("foo is not randomized %0d", ok);

    bar.foo.rand_mode(1);
    changed = 0;
    repeat (10) begin
      bar.foo.b = 42;
      ok = bar.randomize();
      if (ok != 1 || bar.foo.a != 15) $error("a keeps its value %0d", ok);
      if (bar.foo.b != 42) changed = 1;
    end
    if (!changed) $error("b is randomized");

    // Not a rand property: every property of the object
    other.rand_mode(0);
    if (other.a.rand_mode() != 0 || other.b.rand_mode() != 0) $error("object mode");
    other.rand_mode(1);
    if (other.a.rand_mode() != 1 || other.b.rand_mode() != 1) $error("object mode 1");
    $display("PASSED");
  end
endmodule
