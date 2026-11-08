// Statements expanded from the same macro are not always next to each other
// Self-checking. Expected values: IEEE 1800-2017 22.5.1 (text macro with arguments), 27.4 (loop generate),
// 10.3.2 (continuous assignment to a part select), 11.5.1 (indexed part select). Slice i of a 4-bit-sliced
// vector holds i+1 (a), i+4 (b), i+7 (c); f[i] = bit 0 of i, g[i] = bit 1 of i.
`define slice(array, index, size) array[(size) * ((index) + 1) - 1 : (size) * (index)]
module svmacrolabel1;
  wire [11:0] a, b, c;
  wire [2:0] f, g;
  generate
    genvar i;
    for (i = 0; i < 3; i++) begin
      assign `slice(a, i, 4) = i + 1;
      assign b[i*4 +: 4] = i + 4;
      assign `slice(c, i, 4) = i + 7;
      assign f[i] = i[0];
      assign `slice(g, i, 1) = i[1];
    end
  endgenerate
  initial begin
    #1;
    if (a !== 12'h321) $error("a = %h, expected 321", a);
    else if (b !== 12'h654) $error("b = %h, expected 654", b);
    else if (c !== 12'h987) $error("c = %h, expected 987", c);
    else if (f !== 3'b010) $error("f = %b, expected 010", f);
    else if (g !== 3'b100) $error("g = %b, expected 100", g);
    else $display("PASSED");
  end
endmodule
