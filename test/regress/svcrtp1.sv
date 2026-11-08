// IEEE 1800-2017 8.25, 8.26: a class may extend a parameterized base
// specialized with itself (class D extends B#(D)).  Inside B the type
// parameter T names the derived class: it can declare handles, be the
// return type of methods, be constructed, be the target of $cast from
// this, and have its static members and methods referenced through T::.
virtual class svcrtp1_base #(type T = int);
  static int count = 0;
  T next;

  function new();
    count++;
  endfunction

  function T self_t();
    T t;
    if (!$cast(t, this)) return null;
    return t;
  endfunction

  static function T create();
    T t = new;
    return t;
  endfunction

  function T chain(T other);
    next = other;
    return self_t();
  endfunction

  function int kind();
    return T::KIND;
  endfunction

  pure virtual function int value();
endclass

class svcrtp1_d1 extends svcrtp1_base #(svcrtp1_d1);
  static const int KIND = 1;
  int v = 10;
  virtual function int value(); return v; endfunction
endclass

class svcrtp1_d2 extends svcrtp1_base #(svcrtp1_d2);
  static const int KIND = 2;
  virtual function int value(); return 20; endfunction
endclass

module svcrtp1;
  initial begin
    svcrtp1_d1 a, b, c;
    svcrtp1_d2 x;
    a = svcrtp1_d1::create();
    b = svcrtp1_d1::create();
    x = svcrtp1_d2::create();
    b.v = 11;
    c = a.chain(b);
    if (c != a) $fatal(1, "self_t did not return this");
    if (a.next != b || a.next.value() != 11)
      $fatal(1, "next handle of type T");
    if (a.kind() != 1 || x.kind() != 2)
      $fatal(1, "T::KIND: %0d %0d", a.kind(), x.kind());
    // Each specialization has its own static count (8.25)
    if (svcrtp1_base#(svcrtp1_d1)::count != 2
        || svcrtp1_base#(svcrtp1_d2)::count != 1)
      $fatal(1, "static counts %0d %0d",
             svcrtp1_base#(svcrtp1_d1)::count,
             svcrtp1_base#(svcrtp1_d2)::count);
    if (x.self_t().value() != 20) $fatal(1, "d2 value");
    $display("PASSED");
  end
endmodule
