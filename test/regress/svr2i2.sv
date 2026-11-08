// IEEE 1800-2017 6.12.2 / 6.24.1: a real converted to an integral type is
// rounded to the nearest integer, ties away from zero, and the integer is
// then kept in the width of the target; this holds for any width,
// including vectors wider than 64 bits and size casts.
module svr2i2;
  real r; logic [127:0] w; bit [199:0] b; logic signed [127:0] sw;
  int errs;
  `define CK(G, E) if ((G) !== (E)) begin $error(`"G = %h, expected %h`", G, E); errs++; end
  initial begin
    r = -(2.0**100 + 2.0**60); sw = r;
    `CK(sw, -((128'sd1 << 100) + (128'sd1 << 60)))
    r = 3.0e40; b = r;
    `CK(b, 200'd30000000000000002120283904895640175378432)
    r = -2.5; w = r;
    `CK(w, -128'sd3)
    r = 12345.5; w = r;
    `CK(w, 128'd12346)
    r = 2.0**130; w = r;          // only the low 128 bits are kept
    `CK(w, 128'd0)
    r = 1.5 * 2.0**127; w = r;
    `CK(w, 128'h3 << 126)
    w = 128'(2.0**70);
    `CK(w, 128'h1 << 70)
    `CK(32'(2.5), 32'sd3)
    `CK(8'(-2.5), -8'sd3)
    if (errs == 0) $display("PASSED");
  end
endmodule
