// Self-checking test of per-bit constraints: foreach over the bits of a packed vector, in a class and in
// std::randomize ... with.
// Expected values: IEEE 1800-2017 18.5.8 (foreach over a packed vector iterates its bits), 18.5.6 (if
// constraints), 18.12 (std::randomize with), 11.5.1 (bit-select); every solution must satisfy
// bit 7, 15, 23, 31 == 0 (when (i+1)%8 == 0) and not (bit i == 0 && bit i+1 == 1) for i%8 == 0.
module svcrvbits1;
  class c;
    rand bit [15:0] v;
    constraint k {
      foreach (v[i]) {
        if ((i + 1) % 8 == 0) v[i] == 1'b0;
        if (i % 8 == 0) !((v[i] == 0) && (v[i+1] == 1'b1));
      }
    }
  endclass
  initial begin
    bit [31:0] pv;
    automatic c o = new;
    automatic int bad = 0, bad2 = 0;
    repeat (50) begin
      if (!std::randomize(pv) with { foreach (pv[i]) { if ((i + 1) % 8 == 0) pv[i] == 1'b0;
                                                    if (i % 8 == 0) !((pv[i] == 0) && (pv[i+1] == 1'b1)); } }) bad++;
      if (pv[7] || pv[15] || pv[23] || pv[31]) bad++;
      for (int b = 0; b < 32; b += 8) if (!pv[b] && pv[b+1]) bad++;
      if (!o.randomize()) bad2++;
      if (o.v[7] || o.v[15] || (!o.v[0] && o.v[1]) || (!o.v[8] && o.v[9])) bad2++;
    end
    if (bad != 0 || bad2 != 0) begin $error("bad=%0d bad2=%0d", bad, bad2); end
    else $display("PASSED");
  end
endmodule
