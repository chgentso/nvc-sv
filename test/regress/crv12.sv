module crv12;
  typedef enum {RED, GREEN, BLUE = 5, BLACK = 9} color_t;
  typedef enum bit [1:0] {A = 1, B = 2} small_t;
  class C;
    rand color_t col;
    rand small_t sm;
    rand int x;
    int lim;       // state variable
    constraint k { x > lim; x < lim + 10; }
  endclass
  class U;
    rand int x;
    constraint k1 { x > 10; }
    constraint k2 { x < 5; }
  endclass
  C o = new();
  U u = new();
  int errors = 0;
  int i, r, px, n;
  int cc [10];   // indexed by color_t value
  int cs [4];    // indexed by small_t value
  int ncc, ncs, jj;
  int rc [4];
  int ur_lo, ur_hi, ur_bad, rcs;
  int k;
  initial begin
    // state variable in constraint
    for (k = 0; k < 4; k++) begin
      o.lim = k * 1000 - 2000;
      for (i = 0; i < 50; i++) begin
        if (o.randomize() != 1) errors++;
        else if (o.x <= o.lim || o.x >= o.lim + 10) begin errors++; $display("state lim x=%0d", o.x); end
      end
    end
    if (o.lim != 1000) begin errors++; $display("state var modified"); end
    // enums
    for (i = 0; i < 600; i++) begin
      if (o.randomize() != 1) errors++;
      cc[o.col]++;
      cs[o.sm]++;
    end
    ncc = 0; ncs = 0;
    for (jj = 0; jj < 10; jj++) begin
      if (cc[jj] != 0) ncc++;
      if (cc[jj] != 0 && jj != RED && jj != GREEN && jj != BLUE && jj != BLACK) errors++;
    end
    for (jj = 0; jj < 4; jj++) begin
      if (cs[jj] != 0) ncs++;
      if (cs[jj] != 0 && jj != A && jj != B) errors++;
    end
    if (ncc != 4) begin errors++; $display("colors seen %0d", ncc); end
    if (ncs != 2) begin errors++; $display("small seen %0d", ncs); end
    // unsat: returns 0, state unchanged
    u.x = 42;
    for (i = 0; i < 10; i++) begin
      r = u.randomize();
      if (r != 0) begin errors++; $display("unsat r=%0d", r); end
      if (u.x != 42) begin errors++; $display("unsat changed x"); end
    end
    // randcase
    n = 4000;
    for (i = 0; i < n; i++) begin
      randcase
        1: rc[0]++;
        3: rc[1]++;
        0: rc[2]++;
        4: rc[3]++;
      endcase
    end
    if (rc[2] != 0) begin errors++; $display("weight 0 chosen"); end
    if (rc[0] < n*1/8*0.6 || rc[0] > n*1/8*1.4) begin errors++; $display("rc0 %0d", rc[0]); end
    if (rc[1] < n*3/8*0.8 || rc[1] > n*3/8*1.2) begin errors++; $display("rc1 %0d", rc[1]); end
    if (rc[3] < n*4/8*0.8 || rc[3] > n*4/8*1.2) begin errors++; $display("rc3 %0d", rc[3]); end
    // $urandom_range
    for (i = 0; i < 3000; i++) begin
      px = $urandom_range(10, 20);
      if (px < 10 || px > 20) ur_bad++;
      if (px == 10) ur_lo++;
      if (px == 20) ur_hi++;
      px = $urandom_range(7, 3);   // swapped args allowed
      if (px < 3 || px > 7) ur_bad++;
      px = $urandom_range(5);
      if (px < 0 || px > 5) ur_bad++;
    end
    if (ur_bad != 0) begin errors++; $display("urandom_range out of range %0d", ur_bad); end
    if (ur_lo < 100 || ur_hi < 100) begin errors++; $display("urandom_range ends %0d %0d", ur_lo, ur_hi); end
    if (errors == 0) $display("PASSED"); else $display("FAILED errors=%0d", errors);
    $finish;
  end
endmodule
