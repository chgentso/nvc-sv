module crv07;
  class S;
    rand bit s;
    rand bit [7:0] v;
    constraint k { s -> v == 0; }
  endclass
  class SB;
    rand bit s;
    rand bit [7:0] v;
    constraint k { s -> v == 0; solve s before v; }
  endclass
  class F;
    rand int x, y, z;
    constraint soft1 { soft x == 5; }
    constraint soft2 { soft y inside {[10:20]}; }
    constraint hard  { y > 15; z > 0; z < 3; soft z == 2; }
  endclass
  class G;
    rand int x;
    constraint hard { x inside {[0:100]}; }
    constraint sft  { soft x > 90; }
  endclass
  S s0 = new();
  SB sb = new();
  F f = new();
  G g = new();
  int errors = 0;
  int N = 4000;
  int i, ns0, nsb, nx5, ng;
  initial begin
    for (i = 0; i < N; i++) begin
      if (sb.randomize() != 1) errors++;
      if (sb.s) begin nsb++; if (sb.v != 0) errors++; end
      if (s0.randomize() != 1) errors++;
      if (s0.s) begin ns0++; if (s0.v != 0) errors++; end
    end
    // with solve before: s is ~50%.  Without: s=1 has prob 1/256.
    if (nsb < N*0.35 || nsb > N*0.65) begin errors++; $display("solve: s=1 %0d of %0d", nsb, N); end
    // soft: hard y>15 overrides nothing in conflict -> y in [16:20]; x == 5; z == 2
    for (i = 0; i < 300; i++) begin
      if (f.randomize() != 1) begin errors++; $display("F fail"); end
      else begin
        if (f.x != 5) begin errors++; $display("soft x %0d", f.x); end
        if (!(f.y >= 16 && f.y <= 20)) begin errors++; $display("soft y %0d", f.y); end
        if (f.z != 2) begin errors++; $display("soft z %0d", f.z); end
      end
      if (g.randomize() != 1) errors++;
      else if (g.x <= 90 || g.x > 100) begin errors++; $display("g %0d", g.x); end
    end
    // inline hard overrides soft
    for (i = 0; i < 100; i++) begin
      if (f.randomize() with { x == 77; z == 1; y == 3 + 17; } != 1) begin errors++; $display("F with fail"); end
      else if (f.x != 77 || f.z != 1 || f.y != 20) begin errors++; $display("override %0d %0d %0d", f.x, f.y, f.z); end
      if (g.randomize() with { x < 10; } != 1) errors++;
      else if (g.x >= 10 || g.x < 0) begin errors++; $display("g2 %0d", g.x); end
    end
    if (errors == 0) $display("PASSED"); else $display("FAILED errors=%0d", errors);
    $finish;
  end
endmodule
