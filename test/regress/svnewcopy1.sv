// Shallow copy with new <handle>: dynamic type, independence from the source, virtual dispatch, copy in a method
// Expected values: IEEE 1800-2017 8.12 (shallow copy: a new object with the same property values; later writes
// to either object do not show in the other; handles inside are copied as handles), 8.20 (virtual methods),
// 8.5 (new within a class, this)
virtual class absb;
  int x = 1;
  pure virtual function string who();
  function absb dup();
    return this;
  endfunction
endclass
class conc extends absb;
  int y = 3;
  function new(int a);
    y = a;
  endfunction
  virtual function string who(); return $sformatf("conc%0d", y); endfunction
endclass
class pb #(type T = int);
  T v;
  virtual function string who(); return "pb"; endfunction
endclass
class pd #(type T = int) extends pb #(T);
  int extra = 9;
  virtual function string who(); return "pd"; endfunction
  function pb #(T) copy();
    pd #(T) r = new this;
    return r;
  endfunction
endclass
class plain;
  int a = 4;
  plain n;
  function plain cp(); plain r = new this; return r; endfunction
endclass
module svnewcopy1;
  int fails = 0;
  initial begin
    automatic conc c1 = new(7);
    absb a1, a2;
    automatic pd #(byte) p1 = new;
    pb #(byte) p2, p3;
    automatic plain q1 = new, q2, q3;
    c1.x = 42;
    a1 = c1;
    a2 = new a1;
    if (a2.who() != "conc7" || a2.x != 42) begin $error("a2: %s %0d", a2.who(), a2.x); fails++; end
    c1.x = 43;
    if (a2.who() != "conc7" || a2.x != 42) begin $error("a2 after source change: %s %0d", a2.who(), a2.x); fails++; end
    if (a2 == a1) begin $error("copy is the same object"); fails++; end
    p1.v = 5; p1.extra = 11;
    p2 = p1;
    p3 = new p2;
    if (p3.who() != "pd" || p3.v != 5) begin $error("p3: %s %0d", p3.who(), p3.v); fails++; end
    p3 = p1.copy();
    if (p3.who() != "pd" || p3.v != 5) begin $error("p3 copy: %s %0d", p3.who(), p3.v); fails++; end
    q1.a = 8; q1.n = q1;
    q2 = new q1;
    q3 = q1.cp();
    if (q2.a != 8 || q3.a != 8 || q2.n != q1 || q3.n != q1 || q2 == q1 || q3 == q1) begin
      $error("plain copies: %0d %0d", q2.a, q3.a); fails++;
    end
    if (fails == 0) $display("PASSED");
  end
endmodule
