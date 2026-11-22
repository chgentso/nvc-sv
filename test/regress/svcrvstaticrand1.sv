// A static rand variable is randomized by every object of the class which is randomized, held once for
// all of them, and its rand_mode is shared (IEEE 1800-2017 18.4, 18.8).
class Base;
  static rand bit [3:0] bs;
  rand int unsigned a;
  constraint cb { bs inside {[4:6]}; a < 10; }
endclass

class Derived extends Base;
  static rand int unsigned sx;
  rand int unsigned d;
  constraint c { sx inside {[100:110]}; d == sx + 1; }
endclass

module svcrvstaticrand1;
  initial begin
    Derived o1 = new, o2 = new;
    int ok;
    repeat (10) begin
      ok = o1.randomize();
      if (ok != 1) $error("randomize %0d", ok);
      if (o1.sx < 100 || o1.sx > 110 || o2.sx != o1.sx) $error("sx %0d %0d", o1.sx, o2.sx);
      if (o1.d != o1.sx + 1) $error("d %0d sx %0d", o1.d, o1.sx);
      if (o1.bs < 4 || o1.bs > 6 || o2.bs != o1.bs) $error("bs %0d", o1.bs);
    end

    // Not in the list of variables: it keeps its value, which must satisfy the constraints
    o1.sx = 105;
    ok = o2.randomize(d);
    if (ok != 1 || o2.d != 106 || o1.sx != 105) $error("frozen %0d %0d", ok, o2.d);
    o1.sx = 7;
    ok = o2.randomize(d);
    if (ok != 0 || o1.sx != 7) $error("frozen outside %0d", ok);

    // The mode is shared by the objects, as is that of a variable of the base class
    o1.sx = 7;
    if (o2.sx.rand_mode() != 1) $error("mode");
    o1.sx.rand_mode(0);
    if (o2.sx.rand_mode() != 0 || o2.d.rand_mode() != 1) $error("mode 0");
    ok = o2.randomize();
    if (ok != 0 || o2.sx != 7) $error("rand_mode(0) %0d", ok);
    o2.sx.rand_mode(1);
    if (o1.sx.rand_mode() != 1) $error("mode 1");
    ok = o1.randomize();
    if (ok != 1 || o1.sx < 100) $error("rand_mode(1) %0d", ok);

    o1.bs.rand_mode(0);
    if (o2.bs.rand_mode() != 0) $error("base mode");
    o1.bs.rand_mode(1);

    // Class level rand_mode also changes the static variables
    o1.rand_mode(0);
    if (o1.sx.rand_mode() != 0 || o2.sx.rand_mode() != 0 || o2.bs.rand_mode() != 0)
      $error("class mode");
    if (o2.d.rand_mode() != 1) $error("d keeps its mode");
    o1.rand_mode(1);
    if (o2.sx.rand_mode() != 1 || o2.bs.rand_mode() != 1) $error("class mode 1");
    $display("PASSED");
  end
endmodule
