// A constraint in a foreach which refers to an element that does not exist is left out:
// arr[i-1] for the first element. An out of bounds index outside of a foreach makes randomize fail.
class A;
  rand int x;
  rand int fixed[5];
  rand int dyn[];
  constraint c {
    x == 7;
    foreach (fixed[i]) fixed[i] == fixed[i-1];
    foreach (dyn[i]) dyn[i] == dyn[i+1] + 1;
  }
endclass

class B;
  rand int fixed[5];
  rand int dyn[];
  int k = 9;
  bit use_dyn;
  constraint c {
    if (use_dyn) dyn[k] == 3;
    else fixed[k] == 3;
  }
endclass

module svcrvoob1;
  initial begin
    A a = new;
    int ok;
    a.dyn = new[4];
    repeat (5) begin
      ok = a.randomize();
      if (ok != 1 || a.x != 7) $error("randomize %0d", ok);
      for (int i = 0; i < 4; i++)
        if (a.fixed[i] != a.fixed[i + 1]) $error("fixed %p", a.fixed);
      for (int i = 0; i < 3; i++)
        if (a.dyn[i] != a.dyn[i + 1] + 1) $error("dyn %p", a.dyn);
    end
    begin
      B b = new;
      b.dyn = new[4];
      b.use_dyn = 0;
      if (b.randomize() != 0) $error("fixed out of bounds");
      b.use_dyn = 1;
      if (b.randomize() != 0) $error("dyn out of bounds");
    end
    $display("PASSED");
  end
endmodule
