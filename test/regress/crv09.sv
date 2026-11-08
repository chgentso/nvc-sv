module crv09;
  int seq = 0;
  class Inner;
    rand int v;
    constraint c { v inside {[1:5]}; }
  endclass
  class Base;
    rand int a;
    rand Inner in;
    int pre_at, post_at, pre_n, post_n;
    int a_in_post;
    function new(); in = new(); endfunction
    constraint ca { a inside {[0:10]}; }
    constraint cb { a != 100; }
    function void pre_randomize();
      pre_n++; seq++; pre_at = seq;
    endfunction
    function void post_randomize();
      post_n++; seq++; post_at = seq; a_in_post = a;
    endfunction
  endclass
  class Derived extends Base;
    rand int b;
    constraint ca { a inside {[50:60]}; }   // replaces Base::ca
    constraint cd { b == a + 1; }
  endclass
  class Derived2 extends Base;
    rand int e;
    constraint ce { e < 0; }
  endclass
  class Plain;
    rand int p;
    int pre_n, post_n;
    function void pre_randomize(); pre_n++; endfunction
    function void post_randomize(); post_n++; endfunction
  endclass
  Base bh;
  Derived d = new();
  Derived2 d2 = new();
  Base b0 = new();
  Plain pl = new();
  int errors = 0;
  int i;
  initial begin
    for (i = 0; i < 100; i++) begin
      if (b0.randomize() != 1) errors++;
      if (b0.pre_n != i + 1 || b0.post_n != i + 1) begin errors++; $display("hook counts"); end
      if (b0.pre_at + 1 != b0.post_at) begin errors++; $display("hook order"); end
      if (b0.a_in_post != b0.a) errors++;
      if (b0.a < 0 || b0.a > 10 || b0.a == 100) begin errors++; $display("base a=%0d", b0.a); end
      if (b0.in.v < 1 || b0.in.v > 5) begin errors++; $display("nested v=%0d", b0.in.v); end
    end
    // derived: ca replaced, base cb (a != 100) still applies, new rand b
    for (i = 0; i < 100; i++) begin
      if (d.randomize() != 1) begin errors++; $display("derived fail"); end
      else begin
        if (d.a < 50 || d.a > 60) begin errors++; $display("derived a=%0d", d.a); end
        if (d.b != d.a + 1) begin errors++; $display("derived b"); end
        if (d.in.v < 1 || d.in.v > 5) errors++;
        if (d.pre_n != i + 1 || d.post_n != i + 1) begin errors++; $display("derived hooks"); end
      end
      if (d2.randomize() != 1) errors++;
      else begin
        if (d2.a < 0 || d2.a > 10) begin errors++; $display("d2 a=%0d", d2.a); end
        if (d2.e >= 0) begin errors++; $display("d2 e"); end
      end
    end
    // dynamic dispatch: call through base handle uses derived constraints
    bh = d;
    for (i = 0; i < 100; i++) begin
      if (bh.randomize() != 1) begin errors++; $display("bh fail"); end
      else if (bh.a < 50 || bh.a > 60) begin errors++; $display("bh a=%0d", bh.a); end
      else if (d.b != d.a + 1) begin errors++; $display("bh b"); end
    end
    bh = d2;
    if (bh.randomize() != 1 || d2.e >= 0) begin errors++; $display("bh d2"); end
    // class without constraints on hooks
    for (i = 0; i < 10; i++) if (pl.randomize() != 1) errors++;
    if (pl.pre_n != 10 || pl.post_n != 10) begin errors++; $display("plain hooks"); end
    // nested object is recursively randomized: v takes several values
    begin
      int seen [8];
      int nseen, jx;
      for (i = 0; i < 200; i++) begin
        void'(b0.randomize());
        if (b0.in.v >= 0 && b0.in.v < 8) seen[b0.in.v] = 1;
      end
      nseen = 0;
      for (jx = 0; jx < 8; jx++) if (seen[jx] != 0) nseen++;
      if (nseen != 5) begin errors++; $display("nested coverage %0d", nseen); end
    end
    if (errors == 0) $display("PASSED"); else $display("FAILED errors=%0d", errors);
    $finish;
  end
endmodule
