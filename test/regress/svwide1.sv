// Increment, decrement, compound assignment and shifts on vectors wider than 64 bits.
// Expected values: IEEE 1800-2017 11.4.2 (++/--), 11.4.1 (compound assignment), 11.4.10 (shifts),
// 11.3.3 (arithmetic wraps modulo 2**N), 11.4.8 (bitwise operators).
module svwide1;
  logic [99:0] v;
  logic [127:0] w;
  int i;
  int errors = 0;
  initial begin
    v = 100'd5; v++;
    if (v !== 100'd6) begin $error("1: %h", v); errors++; end
    v += 1;
    if (v !== 100'd7) begin $error("2: %h", v); errors++; end
    v = 100'hFFFFFFFFFFFFFFFFF; v++;
    if (v !== 100'h0000000100000000000000000) begin $error("3: %h", v); errors++; end
    v--;
    if (v !== 100'h00000000fffffffffffffffff) begin $error("4: %h", v); errors++; end
    v -= 3;
    if (v !== 100'h00000000ffffffffffffffffc) begin $error("5: %h", v); errors++; end
    w = 1; w *= 3; w <<= 70;
    if (w !== 128'h00000000000000c00000000000000000) begin $error("6: %h", w); errors++; end
    w >>= 2; w |= 1; w ^= 128'h3;
    if (w !== 128'h00000000000000300000000000000002) begin $error("7: %h", w); errors++; end
    for (i = 0; i < 3; i++) v++;
    if (v !== 100'h00000000fffffffffffffffff) begin $error("8: %h", v); errors++; end
    if (errors == 0) $display("PASSED");
  end
endmodule
