// IEEE 1800-2017 11.4.14: streaming with a slice size of 1 reverses every
// bit of the stream ({<< 1{..}} == {<<{..}}), and {>>{..}} keeps the
// order.  Sources and targets may be variable-sized: queues, dynamic
// arrays and strings (11.4.14.1, 11.4.14.2).  A variable-sized target
// takes all remaining bits of the stream; a string target gets one
// character per 8 bits.  With the "with" clause (11.4.14.4) only the
// selected elements of an array take part.
module svstreamvar1;
  bit q1[$];
  bit [3:0] nq[$];
  byte d[];
  string s, s2;
  bit [15:0] h;
  bit [7:0] b8;
  int n;

  initial begin
    bit [31:0] w;

    // Bit queue source: q1[0] is the leftmost bit of the stream
    q1 = '{1, 0, 0, 0, 1, 1, 0, 1};
    b8 = {>> {q1}};
    if (b8 !== 8'b1000_1101) $fatal(1, ">> bit queue: %b", b8);
    b8 = {<< 1 {q1}};
    if (b8 !== 8'b1011_0001) $fatal(1, "<< 1 bit queue: %b", b8);
    b8 = {<< {q1}};
    if (b8 !== 8'b1011_0001) $fatal(1, "<< bit queue: %b", b8);

    // Bit queue target sized to the stream
    q1 = {<< 1 {16'hF00D}};
    if (q1.size() != 16) $fatal(1, "bit queue size %0d", q1.size());
    h = {>> {q1}};
    if (h !== 16'hB00F) $fatal(1, "<< 1 into bit queue: %h", h);

    // Nibble queue, << 1 across elements
    nq = '{4'h1, 4'h2, 4'h3};
    w = {>> {nq}};
    if (w !== 32'h1230_0000) $fatal(1, ">> nibble queue: %h", w);
    w = {<< 1 {nq}};
    if (w !== {12'b1100_0100_1000, 20'b0})
      $fatal(1, "<< 1 nibble queue: %h", w);

    // Dynamic array of bytes <-> string
    s = "AbC";
    d = {>> {s}};
    if (d.size() != 3 || d[0] != "A" || d[2] != "C")
      $fatal(1, "string to byte array: size %0d", d.size());
    d = {<< 8 {s}};
    if (d.size() != 3 || d[0] != "C" || d[2] != "A")
      $fatal(1, "<< 8 string to byte array");
    s2 = {>> {d}};
    if (s2 != "CbA") $fatal(1, "byte array to string: %s", s2);
    s2 = {<< 1 {8'h82, 8'h42}};       // Bits reversed: 8'h42 8'h41
    if (s2 != "BA") $fatal(1, "<< 1 into string: %s", s2);

    // Fixed field first, variable-sized target takes the rest
    {>> {b8, d}} = 32'hAA_01_02_03;
    if (b8 !== 8'hAA || d.size() != 3 || d[0] != 1 || d[2] != 3)
      $fatal(1, "fixed + dynamic unpack: %h %0d", b8, d.size());
    {>> {b8, s}} = 32'h41_42_43_44;
    if (b8 !== 8'h41 || s != "BCD") $fatal(1, "fixed + string: %s", s);

    // With clause: only the first n elements; target sized by the range
    nq = '{4'hA, 4'hB, 4'hC, 4'hD};
    n = 2;
    b8 = {>> {nq with [0 +: n]}};
    if (b8 !== 8'hAB) $fatal(1, "with source: %h", b8);
    b8 = {<< 1 {nq with [2 : 3]}};    // CD reversed bitwise -> B3
    if (b8 !== 8'hB3) $fatal(1, "<< 1 with source: %h", b8);
    {>> {n, d with [0 +: n]}} = {32'd2, 8'h11, 8'h22};
    if (d.size() != 2 || d[1] != 8'h22) $fatal(1, "with target");

    $display("PASSED");
  end
endmodule
