module virtover1;
class base;
  virtual function int f(int a); return a; endfunction
  virtual function void g(int a, string s); endfunction
  virtual task t(input int a); endtask
  virtual function int h(); return 0; endfunction
  virtual function int k(int a); return a; endfunction
  function int nv(int a); return a; endfunction
endclass
class d1 extends base;
  function int f(int a, int b); return a; endfunction
endclass
class d2 extends base;
  function void g(int a, int s); endfunction
endclass
class d3 extends base;
  task t(output int a); endtask
endclass
class d4 extends base;
  function bit h(); return 0; endfunction
endclass
class d5 extends base;
  function int k(int b); return b; endfunction
endclass
class d6 extends base;
  task f(int a); endtask
endclass
class d7 extends base;
  function int nv(int a, int b); return a; endfunction
endclass
endmodule
