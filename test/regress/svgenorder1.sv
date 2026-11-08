// Expected values: IEEE 1800-2017 9.2.2 / 9.2 (each always/initial runs once at time 0), 27.4, 27.5.
// Seven processes (4 always at top level and in generate blocks, 1 in an instance, 1 initial) each append one
// decimal digit k (1..7) to ord at time 0. The order between them is not defined, so ord must be
// a permutation of the digits 1..7.
module sub(input integer k);
  always begin svgenorder1.ord = svgenorder1.ord * 10 + k; #320; end
endmodule

module svgenorder1;
  integer ord = 0;
  genvar n;
  always begin ord = ord * 10 + 1; #320; end
  generate for (n = 2; n < 4; n = n + 1) begin
    always begin ord = ord * 10 + n; #320; end
  end endgenerate
  sub u(4);
  initial begin ord = ord * 10 + 5; #10; end
  generate if (1) begin : b
    always begin ord = ord * 10 + 6; #320; end
  end endgenerate
  always begin ord = ord * 10 + 7; #320; end
  initial begin
    int seen, v, ndig;
    #1;
    seen = 0; v = ord; ndig = 0;
    while (v > 0) begin
      int d;
      d = v % 10;
      if (d < 1 || d > 7 || (seen & (1 << d))) $error("bad digit %0d in ord=%0d", d, ord);
      seen |= 1 << d;
      v = v / 10;
      ndig++;
    end
    if (ndig == 7 && seen == 'b11111110) $display("PASSED");
    else begin $error("ord=%0d is not a permutation of 1234567", ord); $display("FAILED"); end
    $finish;
  end
endmodule
