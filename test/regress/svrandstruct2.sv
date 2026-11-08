// Expected values: IEEE 1800-2017 18.4 (rand on an unpacked struct randomizes only members declared rand/randc),
// 18.5 (constraints), 18.7 (randomize with).
typedef struct { rand bit [7:0] data; rand bit [1:0] c; bit [7:0] k; } t_t;
typedef struct { bit [7:0] a; bit [7:0] b; } plain_t;
class C;
  rand t_t s;
  t_t ns;
  rand plain_t p;
  rand int x;
  constraint cs { s.data inside {[10:20]}; s.c != 0; }
endclass
module svrandstruct2;
  int err = 0;
  task automatic chk(bit ok, string m);
    if (!ok) begin $error("%s", m); err++; end
  endtask
  initial begin
    automatic C o = new;
    automatic int dmin = 999, dmax = 0, ndiff = 0, pdiff = 0, kdiff = 0, cz = 0, ok;
    o.s.k = 8'h55; o.ns.data = 8'h33; o.ns.k = 8'h44;
    repeat (200) begin
      ok = o.randomize();
      chk(ok == 1, "randomize failed");
      if (o.s.data < dmin) dmin = o.s.data;
      if (o.s.data > dmax) dmax = o.s.data;
      if (o.ns.data != 8'h33 || o.ns.k != 8'h44) ndiff++;
      if (o.p.a != 0 || o.p.b != 0) pdiff++;
      if (o.s.k != 8'h55) kdiff++;
      if (o.s.c == 0) cz++;
    end
    chk(dmin >= 10 && dmax <= 20, "s.data outside constraint");
    chk(dmax - dmin >= 5, "s.data shows no spread");
    chk(ndiff == 0, "non-rand struct changed");
    chk(pdiff == 0, "struct without rand members changed");
    chk(kdiff == 0, "non-rand member of rand struct changed");
    chk(cz == 0, "s.c constraint violated");
    ok = o.randomize() with { s.data == 15; };
    chk(ok == 1 && o.s.data == 15, "inline constraint");
    if (err == 0) $display("PASSED");
  end
endmodule
