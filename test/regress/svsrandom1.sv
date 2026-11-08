// Expected values: IEEE 1800-2017 18.13.3 (srandom: an object seeded with the same value produces
// the same random sequence; the per-object sequence continues over successive randomize() calls),
// 18.13.2 ($urandom with a seed argument: the same seed gives the same value),
// 8.15 / 18.13.3 (srandom is inherited by derived classes)
// The only statistical check (different seeds give different values) fails with probability 2^-32.
class base;
  rand bit [31:0] a;
  rand bit [7:0] b[4];
  function void reseed(int s); this.srandom(s); endfunction
endclass
class derived extends base;
  rand bit [15:0] c;
endclass
module svsrandom1;
  int errs = 0;
  task automatic check(bit ok, string what);
    if (!ok) begin $error("%s", what); errs++; end
  endtask
  initial begin
    automatic base x = new, y = new, z = new;
    automatic derived d1 = new, d2 = new;
    x.srandom(7); y.srandom(7); z.srandom(8);
    void'(x.randomize()); void'(y.randomize()); void'(z.randomize());
    check(x.a == y.a && x.b[0] == y.b[0] && x.b[3] == y.b[3], "same seed same values");
    check(x.a != z.a, "different seed differs");
    void'(x.randomize()); void'(y.randomize());
    check(x.a == y.a, "second call same");
    x.srandom(7); void'(x.randomize()); y.srandom(7); void'(y.randomize());
    check(x.a == y.a, "reseed reproduces");
    d1.srandom(3); d2.reseed(3);
    void'(d1.randomize()); void'(d2.randomize());
    check(d1.a == d2.a && d1.c == d2.c, "derived");
    d1.srandom(3);
    void'(d1.randomize());
    check(d1.a == d2.a && d1.c == d2.c, "sequence restarts");
    check($urandom(5) == $urandom(5), "urandom(5) repeatable");
    if (errs == 0) $display("PASSED");
  end
endmodule
