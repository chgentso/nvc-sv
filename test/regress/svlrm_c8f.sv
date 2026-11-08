// Self-checking: interface classes, class-scoped enums and nested classes.
// Expected values: IEEE 1800-2017 8.26 (interface classes, implements, parameterized interface class, $cast to
// an implemented interface class), 8.23 (class scope resolution), 6.19.5.6 (enum name()), 6.19.4 (enum cast)
`define CHK(g, e) if ((g) !== (e)) begin errs++; $error("check failed at line %0d", `__LINE__); end
module svlrm_c8f;
  int errs;
  interface class Printable;
    pure virtual function string to_string();
  endclass
  interface class Comparable #(type T = int);
    pure virtual function int compare(T other);
  endclass
  class Item implements Printable, Comparable#(Item);
    int v;
    function new(int v_); v = v_; endfunction
    virtual function string to_string(); return $sformatf("Item(%0d)", v); endfunction
    virtual function int compare(Item other); return v - other.v; endfunction
  endclass
  class Holder;
    typedef enum {IDLE, BUSY, DONE} state_e;
    state_e st = IDLE;
    class Node;
      int val;
      function new(int v); val = v; endfunction
    endclass
    Node head;
    function void advance(); st = state_e'((st + 1) % 3); endfunction
    function void mk(int v); head = new(v); endfunction
  endclass
  Item i1, i2; Printable p; Holder h;
  Holder::state_e se;
  initial begin
    i1 = new(5); i2 = new(8);
    p = i1; `CHK(p.to_string(), "Item(5)")
    `CHK(i1.compare(i2), -3) `CHK(i2.compare(i1), 3)
    h = new; `CHK(h.st.name(), "IDLE")
    h.advance(); h.advance(); `CHK(h.st.name(), "DONE")
    h.advance(); `CHK(h.st.name(), "IDLE")
    se = Holder::BUSY; `CHK(se.name(), "BUSY") `CHK(int'(se), 1)
    h.mk(7); `CHK(h.head.val, 7)
    begin automatic Holder::Node n = new(3); `CHK(n.val, 3) end
    `CHK($cast(p, i2), 1) `CHK(p.to_string(), "Item(8)")
    if (errs == 0) $display("PASSED");
  end
endmodule
