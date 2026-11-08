// Nets declared in generate blocks of a module with an interface port, and driving an interface member
// Expected values: IEEE 1800-2017 27.4 (loop generate constructs: one block instance per iteration, each with its
// own nets), 6.5 / 10.3 (net with a continuous assignment follows its operands), 25.3 (interface port),
// 23.6 (hierarchical reference to u.g[i].x). With data = 9: w = 10, x[i] = w + i, e = b.en = w[0] = 0.
interface bus_if;
  logic [7:0] data;
  logic en;
endinterface
module blk #(parameter N = 2)(bus_if b);
  wire [7:0] w = b.data + 8'd1;
  wire e = b.en;
  int fails = 0;
  genvar i;
  generate
    for (i = 0; i < N; i++) begin : g
      wire [7:0] x = w + i;
      initial begin
        #3;
        if (x !== 8'd10 + i || e !== 1'b0 || w !== 8'd10) begin
          $error("g[%0d] x=%0d e=%b w=%0d", i, x, e, w); fails++;
        end
      end
    end
    if (N > 1) begin : h
      assign b.en = w[0];
    end
  endgenerate
endmodule
module svnetgen2;
  bus_if b();
  blk u(b);
  initial begin
    #4;
    if (u.g[0].x !== 8'd10 || u.g[1].x !== 8'd11) $error("%0d %0d", u.g[0].x, u.g[1].x);
    else if (u.fails == 0) $display("PASSED");
  end
  initial begin
    b.data = 5;
    #1 b.data = 9;
  end
endmodule
