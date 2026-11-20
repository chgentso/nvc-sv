// Replication in assignment patterns
// Expected values: IEEE 1800-2017 10.9.1 (array patterns with replication '{n{...}}),
// 10.9.2 (structure patterns), 7.10 (queues), 5.11
typedef struct { int a; int b; } s_t;
typedef struct packed { logic [3:0] x; logic [3:0] y; } ps_t;
module svpatrep1;
  localparam int LP [3] = '{3{7}};
  parameter int PP [4] = '{2{1, 2}};
  int a3 [3] = '{3{1}};
  int a6 [6];
  s_t sa [2] = '{2{'{a:1, b:2}}};
  s_t sb [4];
  bit [7:0] pk = '{8{1'b1}};
  ps_t pst = '{2{4'h5}};
  int m2 [2][3] = '{2{'{3{9}}}};
  int q[$];
  typedef int t3 [3];
  function automatic t3 ret(); ret = '{3{4}}; endfunction
  function automatic s_t rets(); return '{a:1, b:2}; endfunction
  int errs = 0;
  initial begin
    if (a3[0] !== 1 || a3[1] !== 1 || a3[2] !== 1) begin $error("A"); errs++; end
    if (LP[0] !== 7 || LP[1] !== 7 || LP[2] !== 7) begin $error("B"); errs++; end
    if (PP[0] !== 1 || PP[1] !== 2 || PP[2] !== 1 || PP[3] !== 2) begin $error("C"); errs++; end
    if (sa[0].a !== 1 || sa[0].b !== 2 || sa[1].a !== 1 || sa[1].b !== 2) begin $error("F"); errs++; end
    sb = '{2{'{a:3, b:4}, '{a:5, b:6}}};
    if (sb[0].a !== 3 || sb[0].b !== 4 || sb[1].a !== 5 || sb[1].b !== 6 ||
        sb[2].a !== 3 || sb[2].b !== 4 || sb[3].a !== 5 || sb[3].b !== 6) begin $error("G"); errs++; end
    if (pk !== 8'b11111111 || pst !== 8'h55) begin $error("H %b %h", pk, pst); errs++; end
    if (m2[0][0] !== 9 || m2[0][2] !== 9 || m2[1][0] !== 9 || m2[1][1] !== 9 || m2[1][2] !== 9 || m2[0][1] !== 9) begin
      $error("I"); errs++;
    end
    a3 = ret();
    if (a3[0] !== 4 || a3[1] !== 4 || a3[2] !== 4) begin $error("J"); errs++; end
    q = '{3{5}};
    if (q.size() != 3 || q[0] !== 5 || q[2] !== 5) begin $error("K"); errs++; end
    a3 = '{default:2};
    if (a3[0] !== 2 || a3[1] !== 2 || a3[2] !== 2) begin $error("N"); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
