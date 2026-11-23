// The result of an array locator method is not an array the solver can use in a constraint.
class C;
  rand int a[4];
  rand int n;
  constraint c { a.find(x) with (x > 3).size() == n; n == 2; foreach (a[i]) a[i] inside {[0:9]}; }
endclass
module svcrvsorryfind1;
  C c = new;
  initial void'(c.randomize());
endmodule
