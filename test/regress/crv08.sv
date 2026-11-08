module crv08;
  class C;
    rand int x;
    rand int y;
    int lim;
    constraint base { x inside {[0:1000]}; y inside {[0:1000]}; }
  endclass
  C o = new();
  int errors = 0;
  int i, r;
  int x;          // module-level variable with same name as property
  int lo, hi;
  int px, py;
  function automatic void chk_name(C obj);
    int y;        // local with same name as property
    y = 500;
    if (obj.randomize() with { y == 7; } != 1) errors++;  // y resolves to the property
    else if (obj.y != 7) begin errors++; $display("y shadow: prop y=%0d", obj.y); end
  endfunction
  initial begin
    lo = 100; hi = 110;
    for (i = 0; i < 200; i++) begin
      // properties and caller locals/module vars
      if (o.randomize() with { x >= lo; x <= hi; y == x + 1; } != 1) begin errors++; $display("with fail"); end
      else begin
        if (o.x < lo || o.x > hi) begin errors++; $display("x=%0d", o.x); end
        if (o.y != o.x + 1) begin errors++; $display("y"); end
      end
    end
    // this.
    for (i = 0; i < 50; i++) begin
      if (o.randomize() with { this.x == 33; y > x; } != 1) begin errors++; $display("this fail"); end
      else if (o.x != 33 || o.y <= 33) begin errors++; $display("this vals"); end
    end
    // property name equals module-level variable name: property wins
    x = 999999;
    if (o.randomize() with { x < 10; } != 1) errors++;
    else if (o.x >= 10) begin errors++; $display("prop wins: x=%0d", o.x); end
    // local:: reaches the caller's x
    x = 42;
    if (o.randomize() with { x == local::x; } != 1) begin errors++; $display("local:: fail"); end
    else if (o.x != 42) begin errors++; $display("local:: x=%0d", o.x); end
    chk_name(o);
    // unsatisfiable: return 0, state unchanged
    if (o.randomize() != 1) errors++;
    px = o.x; py = o.y;
    for (i = 0; i < 20; i++) begin
      r = o.randomize() with { x > 2000; };
      if (r != 0) begin errors++; $display("unsat returned %0d", r); end
      if (o.x != px || o.y != py) begin errors++; $display("unsat changed state"); end
    end
    // randomize still works afterwards
    if (o.randomize() with { x == 1; } != 1 || o.x != 1) errors++;
    if (errors == 0) $display("PASSED"); else $display("FAILED errors=%0d", errors);
    $finish;
  end
endmodule
