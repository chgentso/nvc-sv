// Expected values: IEEE 1800-2017 8.26 (interface class with a type parameter implemented by a
// class that passes itself as the argument; pure virtual method implemented in the class),
// 8.5 (methods), 8.7 (new with arguments).
module svclassself2;
  interface class Comparable #(type T = int);
    pure virtual function int compare(T other);
  endclass
  class Item implements Comparable#(Item);
    int v;
    Item last;
    function new(int v_); v = v_; endfunction
    function void note(Item o); last = o; endfunction
    virtual function int compare(Item other); return v - other.v; endfunction
  endclass
  Item a, b;
  int errors = 0;
  initial begin
    a = new(5); b = new(8);
    if (a.compare(b) !== -3) begin $error("a.compare(b) = %0d, expected -3", a.compare(b)); errors++; end
    if (b.compare(a) !== 3) begin $error("b.compare(a) = %0d, expected 3", b.compare(a)); errors++; end
    a.note(b);
    if (a.last.v !== 8) begin $error("a.last.v = %0d, expected 8", a.last.v); errors++; end
    // an Item handle is also a Comparable#(Item) handle
    begin
      Comparable#(Item) ci;
      ci = a;
      if (ci.compare(b) !== -3) begin $error("ci.compare(b) = %0d, expected -3", ci.compare(b)); errors++; end
    end
    if (errors == 0) $display("PASSED");
  end
endmodule
