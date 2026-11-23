// How a process uses the bits it reads
module ev_reads;
  logic clk, s;
  logic [3:0] a, b, c, d, e, f, g, h;
  logic [1:0] idx;
  logic [7:0] w;
  logic [3:0] y1, y2, y3, y4, y5, y6, y7, y10;
  logic [7:0] y8;
  logic y9;
  logic [3:0] mem [0:3];
  logic [3:0] t;

  // A copy, and the same word as an operand of an addition
  always_ff @(posedge clk) y1 <= a;
  always_ff @(posedge clk) y2 <= b + 4'd1;
  // Bitwise operators and a multiplexer
  always_ff @(posedge clk) y3 <= (c & d) ^ ~e;
  always_ff @(posedge clk) y4 <= s ? f : g;
  // A comparison with a constant, with another word, and a bit select
  always_ff @(posedge clk) y9 <= (h == 4'd5) | (a < b) | w[3];
  // An index
  always_ff @(posedge clk) y5 <= mem[idx];
  // A word stored extended and concatenated
  always_ff @(posedge clk) y8 <= {c, d};
  // A slice of constant position
  always_ff @(posedge clk) y6[1:0] <= e[3:2];
  // A word through a variable that is then added
  always_comb begin
    t = f;
    y7 = t + g;
  end
  // A call
  function automatic logic [3:0] fn(input logic [3:0] x);
    return x ^ h;
  endfunction
  always_ff @(posedge clk) y10 <= fn(d);
endmodule
