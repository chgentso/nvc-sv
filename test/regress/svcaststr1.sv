// IEEE 1800-2017 6.24.2: $cast(dest, src) to a string destination from an
// integral or string source succeeds (returns 1) and converts as string'()
// does (6.16, 6.24.1): each 8 bits of the integral value become one
// character, leading zero bytes are dropped (6.16: \0 is ignored), and a
// value whose width is not a multiple of 8 is zero-extended on the left.
// Called as a task, $cast assigns the same value.
module svcaststr1;
  string s, t;
  bit [15:0] b16 = 16'h4142;
  logic [23:0] l24 = 24'h00_43_44;
  bit [11:0] b12 = 12'h141;       // Zero-extended to 16'h0141 -> "\x01A"
  int i = 32'h3132_3334;
  byte by = 8'h5a;
  typedef enum bit [7:0] { CH_A = 8'h61 } ch_t;
  ch_t ch = CH_A;
  int ok;

  initial begin
    ok = $cast(s, b16);
    if (!ok || s != "AB") $fatal(1, "bit[15:0]: %0d '%s'", ok, s);

    ok = $cast(s, l24);
    if (!ok || s != "CD" || s.len() != 2)
      $fatal(1, "leading zero byte: %0d '%s' len %0d", ok, s, s.len());

    ok = $cast(s, b12);
    if (!ok || s.len() != 2 || s[0] != 8'h01 || s[1] != "A")
      $fatal(1, "12-bit value: %0d len %0d", ok, s.len());

    ok = $cast(s, i);
    if (!ok || s != "1234") $fatal(1, "int: %0d '%s'", ok, s);

    ok = $cast(s, by);
    if (!ok || s != "Z") $fatal(1, "byte: %0d '%s'", ok, s);

    ok = $cast(s, ch);
    if (!ok || s != "a") $fatal(1, "enum: %0d '%s'", ok, s);

    ok = $cast(s, 32'h0);
    if (!ok || s != "" || s.len() != 0)
      $fatal(1, "zero: %0d len %0d", ok, s.len());

    t = "hello";
    ok = $cast(s, t);
    if (!ok || s != "hello") $fatal(1, "string: %0d '%s'", ok, s);

    $cast(s, 16'h7879);             // Task form
    if (s != "xy") $fatal(1, "task form '%s'", s);

    $cast(s, {t, "!"});
    if (s != "hello!") $fatal(1, "task form string '%s'", s);

    if (string'(b16) != "AB") $fatal(1, "string'() of bit[15:0]");

    $display("PASSED");
  end
endmodule
