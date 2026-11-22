// A constraint which refers to an element that does not exist is left out (IEEE 1800-2017 18.5.8.1):
// arr[i-1] for the first element of a foreach, and an element selected by a state value.
class A;
  rand int x;
  rand int fixed[5];
  rand int dyn[];
  int k = 9;
  constraint c {
    x == 7;
    foreach (fixed[i]) fixed[i] == fixed[i-1];
    foreach (dyn[i]) dyn[i] == dyn[i+1] + 1;
    fixed[k] == 3;
    dyn[k] == 3;
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
    $display("PASSED");
  end
endmodule
