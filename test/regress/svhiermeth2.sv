// Methods of class handles reached by hierarchical references
class Base;
  int n = 3;
  function int poll();
    return n;
  endfunction
  virtual function string who(); return "base"; endfunction
  function int add(int a, int b); return a + b + n; endfunction
  task automatic bump(int by);
    n += by;
    #1;
  endtask
endclass
class Derived extends Base;
  virtual function string who(); return "derived"; endfunction
endclass
module svhiermeth2_leaf;
  Base h;
  initial begin h = new(); end
endmodule
module svhiermeth2_mid;
  svhiermeth2_leaf l();
  Derived d;
  initial d = new();
endmodule
module svhiermeth2;
  svhiermeth2_mid m();
  initial begin
    #1;
    $display("poll=%0d", m.l.h.poll());
    $display("add=%0d", m.l.h.add(4, 5));
    m.l.h.bump(10);
    $display("n=%0d poll=%0d", m.l.h.n, m.l.h.poll());
    $display("who=%s %s", m.l.h.who(), m.d.who());
    $display("inherited=%0d", m.d.poll());
    m.d.bump(2);
    $display("d.n=%0d", m.d.n);
    if (m.l.h.poll() > 5) $display("big");
  end
endmodule
