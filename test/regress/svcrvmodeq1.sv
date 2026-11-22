// constraint_mode and rand_mode read without parentheses give the mode of a constraint or a random
// variable (IEEE 1800-2017 18.8, 18.9), also through a handle
class Bar;
  rand int x, y;
  constraint cx { x > 0; x < 10; }
  constraint cy { y == 10; }
endclass
class Foo;
  rand int a;
  rand Bar bar;
  constraint ca { a == 3; }
  function new; bar = new; endfunction
  function int test;
    int fails = 0;
    ca.constraint_mode(0);
    if (ca.constraint_mode != 0) fails++;
    bar.cy.constraint_mode(0);
    if (bar.cy.constraint_mode != 0 || bar.cx.constraint_mode != 1) fails++;
    ca.constraint_mode(1);
    if (ca.constraint_mode != 1) fails++;
    a.rand_mode(0);
    if (a.rand_mode != 0) fails++;
    a.rand_mode(1);
    if (a.rand_mode != 1) fails++;
    return fails;
  endfunction
endclass
module svcrvmodeq1;
  Foo f;
  int fails = 0;
  initial begin
    f = new;
    fails += f.test();
    f.bar.x.rand_mode(0);
    if (f.bar.x.rand_mode != 0) fails++;
    if (f.bar.cx.constraint_mode != 1 || f.ca.constraint_mode != 1) fails++;
    if (fails == 0) $display("PASSED"); else $error("fails=%0d", fails);
  end
endmodule
