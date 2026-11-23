// The size of an array of objects in a constraint is its length: the solver does not hold the array.
class It; rand int v; endclass

class A;
  rand It it[];
  rand int other;
  constraint c { it.size() == 3; other == 5; }
  function new(); it = new[3]; foreach (it[i]) it[i] = new; endfunction
endclass

class B;
  rand It it[];
  rand int other;
  constraint c { it.size() == 100; }
  function new(); it = new[3]; foreach (it[i]) it[i] = new; endfunction
endclass

module svcrvobjsize1;
  initial begin
    automatic A a = new;
    automatic B b = new;
    if (a.randomize() != 1 || a.other != 5 || a.it.size() != 3) $error("A");
    if (b.randomize() != 0) $error("B succeeded");
    $display("PASSED");
  end
endmodule
