// IEEE 1800-2017 6.12.1, 6.24.1: converting a real to an integral type
// rounds to the nearest integer, ties away from zero, and the result is
// exact for any representable real when the target is wide enough, here
// 65 to 128 bits, signed and unsigned.  Converting a wide integral value
// to real gives the nearest real (exact for these powers of two).
module svr2i128;
  bit signed [127:0] s;
  bit [127:0] u;
  logic [95:0] u96;
  bit signed [71:0] s72;
  real r;

  initial begin
    r = 2.0 ** 100;
    u = r;
    if (u !== 128'h1 << 100) $fatal(1, "2^100: %h", u);

    r = 1.5 * 2.0 ** 120;           // 3 * 2^119
    u = r;
    if (u !== 128'd3 << 119) $fatal(1, "1.5*2^120: %h", u);

    r = -(3.0 * 2.0 ** 80);
    s = r;
    if (s !== -(128'sd3 <<< 80)) $fatal(1, "-3*2^80: %h", s);

    r = 2.0 ** 64 + 2.0 ** 12;      // Beyond 64 bits, low bits set
    u96 = r;
    if (u96 !== (96'h1 << 64) + 96'h1000) $fatal(1, "2^64+2^12: %h", u96);

    r = -(2.0 ** 70) - 2.0 ** 20;
    s72 = r;
    if (s72 !== -((72'sd1 <<< 70) + (72'sd1 <<< 20)))
      $fatal(1, "-(2^70+2^20) in 72 bits: %h", s72);

    // Rounding of halves away from zero, through a wide target
    r = 2.5;      s = r; if (s !== 128'sd3) $fatal(1, "2.5 -> %0d", s);
    r = -2.5;     s = r; if (s !== -128'sd3) $fatal(1, "-2.5 -> %0d", s);
    r = 0.49999;  s = r; if (s !== 128'sd0) $fatal(1, "0.49999 -> %0d", s);
    r = -1.5;     u = r; if (u !== '1 - 1) $fatal(1, "-1.5 unsigned -> %h", u);

    // Cast operator
    s = 128'(2.0 ** 126);
    if (s !== 128'sd1 <<< 126) $fatal(1, "128'(2^126): %h", s);
    u = unsigned'(128'(-(2.0 ** 100)));
    if (u !== ~(128'h1 << 100) + 1) $fatal(1, "128'(-2^100): %h", u);

    // Wide integral to real
    u = 128'h1 << 110;
    r = u;
    if (r != 2.0 ** 110) $fatal(1, "2^110 to real: %g", r);
    s = -(128'sd1 <<< 100);
    r = s;
    if (r != -(2.0 ** 100)) $fatal(1, "-2^100 to real: %g", r);
    r = real'(128'h1 << 127);
    if (r != 2.0 ** 127) $fatal(1, "2^127 to real: %g", r);

    $display("PASSED");
  end
endmodule
