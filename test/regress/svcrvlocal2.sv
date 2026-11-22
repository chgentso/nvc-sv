// local:: names in a with clause: a property of the caller is not the property of the same class
// of the object which is randomized, a function can be named without parentheses, and the names
// of an enumeration given in the type of an inherited property are visible in the derived class and
// in the with clause.
class c1;
  rand int c1_f;
endclass

class c2;
  rand int c2_f;
endclass

class Cls;
  rand int x;
  rand enum { ONE_Y, TWO_Y } y;
endclass

class SubA extends Cls;
  c1 e = new;
  rand enum { AMBIG, ONE_A, TWO_A } en;
  function c1 get_c();
    return e;
  endfunction
endclass

class SubB extends Cls;
  int z;
endclass

class SubC extends SubB;
  c2 e = new;
  rand enum { AMBIG2, ONE_B } en;
  SubA f = new;
  function c2 get_c();
    return e;
  endfunction
  function int doit;
    doit = 1;
    f.x = 4;
    x = 5;
    z = 6;
    doit = f.randomize() with { x == local::x; };
    if (doit != 1 || f.x != 5) $error("local::x %0d %0d", doit, f.x);

    doit = f.randomize() with { x == z; };
    if (doit != 1 || f.x != 6) $error("z %0d %0d", doit, f.x);

    // The objects are not randomized: their values are those they have now
    f.e.c1_f = 5;
    e.c2_f = 5;
    doit = f.randomize() with { e.c1_f == local::e.c2_f; };
    if (doit != 1) $error("local::e %0d", doit);
    doit = f.randomize() with { get_c().c1_f == local::get_c().c2_f; };
    if (doit != 1) $error("local call %0d", doit);
    doit = f.randomize() with { (get_c).c1_f == (local::get_c).c2_f; };
    if (doit != 1) $error("local call no parens %0d", doit);

    e.c2_f = 6;
    doit = f.randomize() with { (get_c).c1_f == (local::get_c).c2_f; };
    if (doit != 0) $error("local call no parens %0d", doit);

    // The names of the enumerations of Cls are visible here
    f.y = ONE_Y;
    y = TWO_Y;
    doit = f.randomize() with { y == local::y; };
    if (doit != 1 || f.y != TWO_Y) $error("local::y %0d", doit);

    doit = f.randomize() with { en == ONE_A; y == ONE_Y; };
    if (doit != 1 || f.en != SubA::ONE_A || f.y != ONE_Y) $error("enum names");
  endfunction
endclass

module svcrvlocal2;
  initial begin
    SubC obj = new;
    if (obj.doit() != 1) $error("doit");
    $display("PASSED");
  end
endmodule
