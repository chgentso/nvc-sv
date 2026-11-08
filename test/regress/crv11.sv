module crv11;
  class C;
    rand int x;
    rand int y;
    int z;   // state variable
    constraint k { x inside {[0:9]}; y inside {[0:9]}; }
  endclass
  int ma, mb;
  C o = new();
  int errors = 0;
  int i, r, nx, ny;
  int px, py;
  task automatic loc_test();
    int la, lb;
    bit [7:0] lc;
    for (int k = 0; k < 100; k++) begin
      r = std::randomize(la, lb) with { la > 10; la < 20; lb == la * 2; };
      if (r != 1 || la <= 10 || la >= 20 || lb != la * 2) begin errors++; $display("local std::randomize"); end
      r = std::randomize(lc) with { lc inside {3, 5, 7}; };
      if (r != 1 || !(lc == 3 || lc == 5 || lc == 7)) begin errors++; $display("lc"); end
    end
  endtask
  initial begin
    for (i = 0; i < 100; i++) begin
      r = std::randomize(ma, mb) with { ma inside {[1:5]}; mb > ma; mb < 8; };
      if (r != 1 || ma < 1 || ma > 5 || mb <= ma || mb >= 8) begin errors++; $display("module vars %0d %0d", ma, mb); end
    end
    // std::randomize without constraints
    nx = 0;
    px = 0;
    for (i = 0; i < 100; i++) begin
      if (std::randomize(ma) != 1) errors++;
      if (ma != px) nx++;
      px = ma;
    end
    if (nx < 90) begin errors++; $display("std::randomize(ma) not varying"); end
    // unsat std::randomize returns 0 and keeps values
    ma = 5; mb = 6;
    r = std::randomize(ma, mb) with { ma > 10; ma < 5; };
    if (r != 0 || ma != 5 || mb != 6) begin errors++; $display("unsat std::randomize"); end
    loc_test();
    // obj.randomize(x): only x is random, y and z are state
    o.y = 3; o.z = 55; o.x = -1;
    for (i = 0; i < 100; i++) begin
      r = o.randomize(x);
      if (r != 1) begin errors++; $display("arg fail"); end
      if (o.y != 3 || o.z != 55) begin errors++; $display("non-listed changed"); end
      if (o.x < 0 || o.x > 9) begin errors++; $display("x=%0d", o.x); end
    end
    // y is a state variable here: it must itself satisfy the constraints (y=3 does).
    o.x = 4; o.y = 3;
    for (i = 0; i < 50; i++) begin
      if (o.randomize(y) != 1) errors++;
      if (o.x != 4) begin errors++; $display("x changed"); end
      if (o.y < 0 || o.y > 9) errors++;
    end
    // randomize(null) checks constraints only
    o.x = 5; o.y = 5;
    if (o.randomize(null) != 1) begin errors++; $display("null check should succeed"); end
    o.x = 50;
    if (o.randomize(null) != 0) begin errors++; $display("null check should fail"); end
    if (o.x != 50) errors++;
    // with + arg list
    o.y = 7;
    for (i = 0; i < 50; i++) begin
      if (o.randomize(x) with { x > y; } != 1) errors++;
      else if (o.x != 8 && o.x != 9) begin errors++; $display("x with y state: %0d", o.x); end
    end
    if (errors == 0) $display("PASSED"); else $display("FAILED errors=%0d", errors);
    $finish;
  end
endmodule
