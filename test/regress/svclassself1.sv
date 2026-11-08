// Expected values: IEEE 1800-2017 8.26 (interface classes, implements, parameterized
// interface class with the implementing class as argument), 8.12 (shallow copy `new c`:
// unpacked-array, real and dynamic-array properties are copied by value, so the copy is
// independent of the original), 7.5 (dynamic arrays), 11.4.5 (== on unpacked arrays).
module svclassself1;
  interface class Comparable #(type T = int);
    pure virtual function int compare(T other);
  endclass
  class Item implements Comparable#(Item);
    Item last;
    function void note(Item v); last = v; endfunction
    int v;
    function new(int v_); v = v_; endfunction
    virtual function int compare(Item other); return v - other.v; endfunction
  endclass
  class Cp;
    int fixed[3] = '{1, 2, 3};
    real r2[2];
    bit [3:0] m[2][2];
    int dyn[];
    function new(); dyn = new[2]; m[1][1] = 4'h9; r2[0] = 1.5; r2[1] = 2.5; endfunction
  endclass
  Item a, b; Cp c, d;
  int errors = 0;
  initial begin
    a = new(5); b = new(8);
    if (a.compare(b) !== -3) begin $error("a.compare(b) = %0d, expected -3", a.compare(b)); errors++; end
    if (b.compare(a) !== 3) begin $error("b.compare(a) = %0d, expected 3", b.compare(a)); errors++; end
    a.note(b);
    if (a.last.v !== 8) begin $error("a.last.v = %0d, expected 8", a.last.v); errors++; end
    c = new; d = new c;
    // the copy starts equal to the original
    if (d.fixed[0] !== 1 || d.fixed[1] !== 2 || d.fixed[2] !== 3) begin $error("copy of fixed array wrong"); errors++; end
    if (d.r2[1] != 2.5 || d.m[1][1] !== 4'h9 || d.dyn.size() != 2) begin $error("copy of members wrong"); errors++; end
    d.fixed[0] = 100; d.r2[1] = 7.5; d.m[1][1] = 4'h1; d.dyn[0] = 5;
    if (c.fixed[0] !== 1) begin $error("c.fixed[0] = %0d, expected 1", c.fixed[0]); errors++; end
    if (d.fixed[0] !== 100) begin $error("d.fixed[0] = %0d, expected 100", d.fixed[0]); errors++; end
    if (d.fixed[2] !== 3) begin $error("d.fixed[2] = %0d, expected 3", d.fixed[2]); errors++; end
    if (c.r2[1] != 2.5) begin $error("c.r2[1] = %f, expected 2.5", c.r2[1]); errors++; end
    if (d.r2[1] != 7.5) begin $error("d.r2[1] = %f, expected 7.5", d.r2[1]); errors++; end
    if (c.m[1][1] !== 4'h9) begin $error("c.m[1][1] = %h, expected 9", c.m[1][1]); errors++; end
    if (d.m[1][1] !== 4'h1) begin $error("d.m[1][1] = %h, expected 1", d.m[1][1]); errors++; end
    if (c.dyn[0] !== 0) begin $error("c.dyn[0] = %0d, expected 0", c.dyn[0]); errors++; end
    if (d.dyn[0] !== 5) begin $error("d.dyn[0] = %0d, expected 5", d.dyn[0]); errors++; end
    if ((c.dyn == d.dyn) !== 1'b0) begin $error("c.dyn == d.dyn is not 0"); errors++; end
    if (errors == 0) $display("PASSED");
  end
endmodule
