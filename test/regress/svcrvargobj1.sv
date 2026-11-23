// Arguments of randomize which name another object: a handle solves the
// rand properties of the object, a member solves just that member
class leaf;
  rand int a;
  rand int b;
  int c;
  constraint k { a inside {[1:5]}; }
endclass

class mid;
  leaf lf;
  int d;
  function new();
    lf = new;
  endfunction
endclass

class top;
  rand int own;
  mid m;
  function new();
    m = new;
  endfunction
endclass

module svcrvargobj1;
  top t;
  int bad, b_changed, ok;
  initial begin
    t = new;
    t.own = 7;
    t.m.d = 9;
    t.m.lf.a = 100; t.m.lf.b = 200; t.m.lf.c = 300;
    repeat (20) begin
      void'(t.randomize(m));       // Nothing: mid has no rand property
      if (t.own != 7 || t.m.d != 9 || t.m.lf.a != 100) bad++;
      void'(t.randomize(m.lf));
      if (t.own != 7 || t.m.d != 9 || t.m.lf.c != 300) bad++;
      if (t.m.lf.a < 1 || t.m.lf.a > 5) bad++;
      if (t.m.lf.b != 200) b_changed++;
      t.m.lf.a = 3;
      ok = t.randomize(m.d, m.lf.c) with { m.d inside {[20:30]}; };
      if (!ok || t.own != 7 || t.m.lf.a != 3) bad++;
      if (t.m.d < 20 || t.m.d > 30) bad++;
      t.m.d = 9; t.m.lf.c = 300; t.m.lf.b = 200; t.m.lf.a = 100;
    end
    if (bad == 0 && b_changed > 10) $display("PASSED");
    else $display("FAILED %0d %0d", bad, b_changed);
  end
endmodule
