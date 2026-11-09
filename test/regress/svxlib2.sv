import mypkg::*;

module svxlib2;
  pair_t p;
  counter c;

  initial begin
    p = 8'hA5;
    c = new;
    c.bump(twice(4));
    c.bump(mypkg::twice(1));
    if (p.hi !== 4'hA || p.lo !== 4'h5) $fatal(1, "bad pair");
    if (c.n != 10) $fatal(1, "bad count %0d", c.n);
    $display("PASS %0d", c.n);
  end
endmodule
