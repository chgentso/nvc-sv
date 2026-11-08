module svlrm_c27a;
  // Self-checking test. Expected values: IEEE 1800-2017 27.2-27.5 (generate constructs: loop generate
  // with genvar, nested loops, conditional if-else generate, case generate; every generate block
  // instantiates its contents once per iteration/selected branch), 27.4 (generate blocks are
  // named scopes referenced as name[index], localparam P per block instance), 23.6 / 23.10
  // (hierarchical references into generate blocks, including function calls), 23.4 (nested module
  // declaration), 21.2.1 (%m prints the hierarchical name; only the generate-block part is compared).
  // The order of the time-0 initial blocks is not specified, so each block records into its own bit.
  parameter N = 4;
  genvar i, j;
  logic [N-1:0] v;
  bit [N-1:0] odd_seen, even_seen;
  bit [5:0] inner_seen;
  string mn [2][3];
  bit n4, n2, nother, bigseen, tinyseen;
  int pv [2][3];
  generate
    for (i = 0; i < N; i = i + 1) begin : g
      wire w = i[0];
      initial #1 v[i] = w;
      if (i % 2) begin : odd
        initial odd_seen[i] = 1;
      end
      else begin : even
        initial even_seen[i] = 1;
      end
    end
  endgenerate
  generate
    for (i = 0; i < 2; i++) begin : outer
      for (j = 0; j < 3; j++) begin : inner
        localparam P = i * 10 + j;
        initial begin inner_seen[i * 3 + j] = 1; mn[i][j] = $sformatf("%m"); pv[i][j] = P; end
      end
    end
  endgenerate
  generate
    case (N)
      2: initial #5 n2 = 1;
      4: initial #5 n4 = 1;
      default: initial #5 nother = 1;
    endcase
    if (N > 3) begin : big initial #6 bigseen = 1; end
    else begin : tiny initial #6 tinyseen = 1; end
  endgenerate
  genvar k;
  generate
    for (k = 0; k < 3; k++) begin : nm
      function automatic int f(int x); return x + k; endfunction
    end
  endgenerate
  int errs;
  function automatic void ck(bit ok, string m);
    if (!ok) begin errs++; $error("%s", m); end
  endfunction
  function automatic bit has(string s, string sub);
    for (int q = 0; q + sub.len() <= s.len(); q++)
      if (s.substr(q, q + sub.len() - 1) == sub) return 1;
    return 0;
  endfunction
  initial begin
    #2 ck(v === 4'b1010, "v[i] = i[0]");
    ck(odd_seen === 4'b1010 && even_seen === 4'b0101, "if-else generate picks the right branch per iteration");
    ck(inner_seen === 6'b111111, "nested loop generate instantiates 2x3 blocks");
    for (int a = 0; a < 2; a++) for (int b = 0; b < 3; b++) begin
      ck(has(mn[a][b], $sformatf("outer[%0d].inner[%0d]", a, b)), "%m names the generate blocks");
      ck(pv[a][b] == a * 10 + b, "localparam per block");
    end
    ck(nm[0].f(10) == 10 && nm[1].f(10) == 11 && nm[2].f(10) == 12, "function in generate block uses its genvar value");
    ck(g[1].w === 1'b1 && g[2].w === 1'b0, "hierarchical net in generate block");
    ck(outer[1].inner[2].P == 12, "hierarchical localparam");
    #4 ck(n4 && !n2 && !nother, "case generate");
    #1 ck(bigseen && !tinyseen, "if generate");
  end
  // nested module declaration and conditional instances
  module leaf #(parameter W = 1) (input logic [W-1:0] a, output logic [W-1:0] y);
    assign y = ~a;
  endmodule
  logic [3:0] x4 = 4'b1010; logic [1:0] x2 = 2'b01; wire [3:0] y4; wire [1:0] y2;
  leaf #(4) l4 (x4, y4);
  leaf #(2) l2 (x2, y2);
  initial begin #3 ck(y4 === 4'b0101 && y2 === 2'b10, "nested module instances"); end
  initial begin #20 if (errs == 0) $display("PASSED"); end
endmodule
