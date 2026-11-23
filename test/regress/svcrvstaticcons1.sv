// The constraint_mode of a static constraint is shared by all objects of the class, and the
// constraint can be named through the class (IEEE 1800-2017 18.9).
class B;
  rand bit [7:0] y;
  static constraint sc { y == 3; }
  constraint c { y > 0; }
endclass
class D extends B;
  rand bit [7:0] z;
  static constraint sd { z == 5; }
endclass
module svcrvstaticcons1;
  B p, q;
  D d;
  int fails = 0, r;
  bit seen;
  initial begin
    p = new; q = new; d = new;
    r = p.randomize();
    if (!r || p.y != 3) begin fails++; $error("on: y=%0d", p.y); end
    r = d.randomize();
    if (!r || d.y != 3 || d.z != 5) begin fails++; $error("derived on: y=%0d z=%0d", d.y, d.z); end

    p.sc.constraint_mode(0);
    if (q.sc.constraint_mode() != 0 || p.sc.constraint_mode() != 0 || d.sc.constraint_mode() != 0) begin
      fails++; $error("mode not shared");
    end
    repeat (40) begin
      r = q.randomize();
      if (!r) begin fails++; $error("q failed"); end
      if (q.y != 3) seen = 1;
    end
    if (!seen) begin fails++; $error("static constraint still applied to the other object"); end

    B::sc.constraint_mode(1);
    if (p.sc.constraint_mode() != 1 || q.sc.constraint_mode() != 1) begin fails++; $error("not on again"); end
    r = q.randomize();
    if (!r || q.y != 3) begin fails++; $error("on again: y=%0d", q.y); end

    d.sd.constraint_mode(0);
    r = d.randomize();
    if (!r || d.y != 3) begin fails++; $error("derived: y=%0d", d.y); end
    if (d.sd.constraint_mode() != 0 || d.c.constraint_mode() != 1) begin fails++; $error("derived modes"); end
    if (fails == 0) $display("PASSED");
  end
endmodule
