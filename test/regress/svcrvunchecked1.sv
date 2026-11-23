// A randomize() call whose result is discarded and which fails prints a warning; a call whose result is
// used does not (IEEE 1800-2017 18.6.1).
class A;
  rand bit [7:0] x;
  constraint c { x > 5; x < 3; }
endclass
class G;
  rand bit [7:0] x;
  constraint c { x > 5; x < 9; }
endclass
module svcrvunchecked1;
  A a;
  G g;
  int r;
  initial begin
    a = new; g = new;
    r = a.randomize();
    g.randomize();
    void'(g.randomize());
    if (r != 0 || g.x < 6) $error("unexpected result");
    a.randomize();
    $display("PASSED");
  end
endmodule
