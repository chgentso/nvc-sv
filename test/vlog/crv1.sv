module crv1;

  int x;                   // Same names as properties below
  int y;
  int res;

  class base;
    rand int a;
    randc bit [3:0] r;
    rand bit [7:0] arr[4];
    rand int dyn[];
    int state;
    int x;
    constraint c1 { a > 0; soft a < 10; a inside {[1:5], 7, [8:$]}; }
    constraint c2 { a dist { 1 := 2, [2:3] :/ 4, 9 }; }
    static constraint c3 { (a > 1) -> state == 0; }
    constraint c4 {
      if (a == 1) state == 1;
      else if (a == 2) { state == 2; state != 3; }
      else state == 4;
    }
    constraint c5 { foreach (arr[i]) arr[i] > i; foreach (dyn[i]) dyn[i] < a; }
    constraint c6 { unique { a, state }; solve a, arr before state; }
    constraint c7 { x == a -> { state == 1; a < 100; } }
    function int again();
      return randomize() with { a == local::x; };
    endfunction
    function void off();
      rand_mode(0);
      a.rand_mode(0);
      c1.constraint_mode(0);
      constraint_mode(1);
    endfunction
  endclass

  class derived extends base;
    rand int b;
    constraint d1 { b == a + x; }
    constraint c1 { a == 5; }
  endclass

  class gen #(type T = int);
    rand T v;
    constraint k { v > 0; }
  endclass

  base o = new;
  derived d = new;
  gen#(bit [7:0]) g = new;

  function automatic void chk(derived obj);
    int a;   // Hides nothing inside the with block
    res = obj.randomize() with { a == 1; b == a; x == local::a; };
  endfunction

  initial begin
    res = o.randomize();
    o.randomize();
    void'(o.randomize(a));
    res = o.randomize(a, r) with { a == y; state == x; };
    res = d.randomize() with { b < 10; this.a == b; };
    res = g.randomize() with { v == 5; };
    res = std::randomize(x, y) with { x < y; y inside {1, 2}; };
    std::randomize(x);
    o.a.rand_mode(0);
    o.c1.constraint_mode(0);
    res = o.rand_mode();
    o.constraint_mode(0);
    o.randomize;
    assert(o.randomize());
    if (x inside {1, [3:4]} && x != 2) res = 1;
    randcase
      1: res = 1;
      3: begin res = 2; end
    endcase
  end

endmodule
