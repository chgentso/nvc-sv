// Self-checking: virtual methods, polymorphism, abstract classes, super, $cast.
// Expected values: IEEE 1800-2017 8.15 (super), 8.16 ($cast; a failed dynamic cast leaves the destination
// unchanged), 8.20 (virtual methods; a non-virtual method is bound to the handle's static type),
// 8.21 (abstract classes, pure virtual methods)
`define CHKB(c) if (!(c)) begin errs++; $error("check failed at line %0d", `__LINE__); end
`define CHK(g, e) if ((g) !== (e)) begin errs++; $error("check failed at line %0d", `__LINE__); end
module svlrm_c8b;
  int errs;
  virtual class Shape;
    string name;
    function new(string n); name = n; endfunction
    pure virtual function real area();
    virtual function string describe(); return $sformatf("%s area=%0.2f", name, area()); endfunction
    function string kind(); return "shape"; endfunction
  endclass
  class Circle extends Shape;
    real r;
    function new(real r_); super.new("circle"); r = r_; endfunction
    virtual function real area(); return 3.14159 * r * r; endfunction
    function string kind(); return "circle"; endfunction
  endclass
  class Rect extends Shape;
    real w, h;
    function new(real w_, real h_); super.new("rect"); w = w_; h = h_; endfunction
    virtual function real area(); return w * h; endfunction
    virtual function string describe(); return {super.describe(), $sformatf(" w=%0.1f h=%0.1f", w, h)}; endfunction
  endclass
  class Square extends Rect;
    function new(real s); super.new(s, s); name = "square"; endfunction
  endclass
  Shape shapes[$];
  Shape s;
  Circle c; Rect r; Square sq;
  initial begin
    begin automatic Circle cc = new(1.0); shapes.push_back(cc); end
    begin automatic Rect rr = new(2.0, 3.0); shapes.push_back(rr); end
    begin automatic Square ss = new(4.0); shapes.push_back(ss); end
    `CHK(shapes[0].describe(), "circle area=3.14")
    `CHK(shapes[1].describe(), "rect area=6.00 w=2.0 h=3.0")
    `CHK(shapes[2].describe(), "square area=16.00 w=4.0 h=4.0")
    foreach (shapes[i]) `CHK(shapes[i].kind(), "shape")   // non-virtual: static type Shape
    s = shapes[2];
    `CHK($cast(r, s), 1) `CHKB(r.w == 4.0)
    `CHK($cast(sq, s), 1)
    `CHK($cast(c, s), 0) `CHK(c == null, 1)
    s = shapes[0];
    `CHK($cast(c, s), 1) `CHKB(c.r == 1.0)
    `CHK($cast(r, s), 0) `CHK(r == shapes[2], 1)  // r kept its previous value
    `CHKB(s.area() > 3.0)
    `CHK(s.kind(), "shape")
    `CHK(c.kind(), "circle")
    if (errs == 0) $display("PASSED");
  end
endmodule
