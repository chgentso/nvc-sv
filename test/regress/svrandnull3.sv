// A constraint which reads through a null handle makes randomize fail
// IEEE 1800-2017 18.5.11: a constraint guard is needed to protect against a null handle,
// an unguarded dereference of a null handle is an error; 18.6.3: randomize returns 0
// when it fails and the random variables keep their values.
class svrandnull3_item;
  int v = 7;
endclass
class svrandnull3_c;
  rand int x;
  svrandnull3_item o;
  constraint c { x == o.v; }
endclass
module svrandnull3;
  initial begin
    automatic svrandnull3_c c = new;
    c.x = 3;
    if (c.randomize() == 0 && c.x == 3) $display("PASSED");
  end
endmodule
