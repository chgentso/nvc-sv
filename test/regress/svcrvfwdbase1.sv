// A constraint of a class can use the properties of a base class which is declared later (the base is
// forward declared with typedef class), also through a chain of bases (IEEE 1800-2017 8.27, 18.5)
typedef class C;

class D extends C;
  constraint x_lt_y { x < y; }
endclass

class A;
endclass

class B extends A;
  rand int x;
  constraint x_gt_0 { x > 0; }
endclass

class C extends B;
  rand int y;
  constraint y_lt_10 { y < 10; }
endclass

module svcrvfwdbase1;
  D d;
  int fails = 0;
  initial begin
    d = new;
    repeat (20) begin
      if (!d.randomize() || d.x <= 0 || d.x >= d.y || d.y >= 10) begin
        fails++; $error("x=%0d y=%0d", d.x, d.y);
      end
    end
    if (fails == 0) $display("PASSED");
  end
endmodule
