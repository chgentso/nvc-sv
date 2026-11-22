// A constraint on a fixed element of a rand dynamic array applies when the size the solver chooses
// includes the element, not only when the array already has that size.
class A;
  rand int n;
  rand bit [7:0] name[];
  constraint c {
    n == 4;
    name.size() == n;
    name[0] == 75;
    name[3] == 67;
    name[7] == 1;
  }
endclass

module svcrvoob2;
  initial begin
    A a = new;
    int ok;
    repeat (5) begin
      ok = a.randomize();
      if (ok != 1 || a.name.size() != 4 || a.name[0] != 75 || a.name[3] != 67)
        $error("randomize %0d size %0d", ok, a.name.size());
    end
    $display("PASSED");
  end
endmodule
