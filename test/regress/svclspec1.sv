// A function in a parameterised class assigns its own name through $cast and
// the class is specialised by a class declared in a module
// Expected values: IEEE 1800-2017 8.16 ($cast: failure returns 0 and leaves the
// destination unchanged, success assigns it), 8.25 (parameterised classes),
// 8.10 (static methods through a scope resolution), 13.4 (function name as return variable)
class base;
  int v;
endclass

class registry #(type T = int);
  static function T make(base o);
    if ($cast(make, o)) return make;
    return null;
  endfunction
endclass

module svclspec1;
  int errs = 0;

  class item extends base;
    typedef registry#(item) type_id;
  endclass

  initial begin
    item a;
    automatic base b = new();
    a = item::type_id::make(b);
    // b is a plain base object: the downcast fails, make returns null
    if (a != null) begin $error("cast of a base object must fail"); errs++; end
    a = new();
    b = a;
    a = item::type_id::make(b);
    // b refers to an item: the downcast succeeds and returns the same object
    if (a == null) begin $error("cast of an item object must succeed"); errs++; end
    if (a != b) begin $error("cast must return the same object"); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
