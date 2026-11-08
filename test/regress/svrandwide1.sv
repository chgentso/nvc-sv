// Scope randomize of variables wider than 64 bits
// Expected values: IEEE 1800-2017 18.12 (scope randomize function), 18.4 (rand variables). Self-consistency only.
module svrandwide1;
  reg [0:11] [7:0] a;
  reg [95:0] c;
  reg [129:0] e;
  int ok, hi_c, hi_e, hi_a, same;
  reg [95:0] prev;
  initial begin
    ok = 1;
    for (int i = 0; i < 20; i++) begin
      ok &= randomize(c, e, a);
      if (|c[95:64]) hi_c++;
      if (|e[129:64]) hi_e++;
      if (|a[0:3]) hi_a++;
      if (i > 0 && c == prev) same++;
      prev = c;
    end
    if (ok !== 1) $error("randomize failed");
    if (!(hi_c > 15)) $error("c high bits never randomized");
    if (!(hi_e > 15)) $error("e high bits never randomized");
    if (!(hi_a > 15)) $error("a high bits never randomized");
    if (same != 0) $error("c repeated");
    if (ok === 1 && hi_c > 15 && hi_e > 15 && hi_a > 15 && same == 0) $display("PASSED");
  end
endmodule
