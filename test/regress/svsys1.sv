`timescale 1ns/1ps
// System functions for arrays, bits, real numbers and random distributions
// Expected values: IEEE 1800-2017 20.5 (conversion), 20.6 ($bits, $typename), 20.7 (array queries),
// 20.8.2 (math), 20.9 (bit vector system functions), 20.15 and Annex N (distribution functions),
// 21.6 (plusargs), 6.11 (integer types)
module svsys1;
  integer seed = 1234;
  integer i;
  real r;
  logic [7:0] v = 8'b0001_0000;
  logic [7:0] u = 8'b0001_x000;
  logic [3:0][7:0] pp;
  int a[3:1][0:4];
  bit [7:0] m[2][5];
  logic s;
  bit b; byte by; shortint sh; longint lg; byte unsigned bu;
  bit [3:0] bv; bit signed [7:0] bsv;
  byte q[$];
  int d[];
  typedef logic [11:4] t_t;
  t_t tv;
  int errs;
  integer d1, d2, d3, d4, d5, d6;
  string ts;

  function automatic string nows(string x);
    string o = "";
    for (int k = 0; k < x.len(); k++) if (x[k] != " ") o = {o, string'(x[k])};
    return o;
  endfunction
  function automatic int pos(string h, string n);
    for (int k = 0; k + n.len() <= h.len(); k++) if (h.substr(k, k + n.len() - 1) == n) return k;
    return -1;
  endfunction
  function automatic bit near(real x, real y);
    return ((x > y) ? (x - y) : (y - x)) < 1.0e-5;
  endfunction

  initial begin
    $printtimescale;
    // distribution functions: Annex N algorithm, evaluated left to right with a shared seed
    d1 = $dist_normal(seed, 0, 100);
    d2 = $dist_exponential(seed, 10);
    d3 = $dist_poisson(seed, 5);
    d4 = $dist_chi_square(seed, 3);
    d5 = $dist_t(seed, 3);
    d6 = $dist_erlang(seed, 3, 10);
    if (d1 !== -10 || d2 !== 23 || d3 !== 4) begin $error("dist 1-3: %0d %0d %0d", d1, d2, d3); errs++; end
    if (d4 !== 5 || d5 !== 0 || d6 !== 9) begin $error("dist 4-6: %0d %0d %0d", d4, d5, d6); errs++; end
    if (seed !== 711633057) begin $error("seed %0d", seed); errs++; end
    // plusargs: none given on the command line
    if ($test$plusargs("FOO") !== 0 || $test$plusargs("BAR") !== 0) begin $error("test\$plusargs"); errs++; end
    i = 0;
    if ($value$plusargs("FOO=%d", i) !== 0) begin $error("value\$plusargs"); errs++; end
    if (i !== 0) begin $error("value\$plusargs changed i"); errs++; end
    // 20.9
    if ($onehot(v) !== 1'b1 || $onehot0(v) !== 1'b1) begin $error("onehot"); errs++; end
    if ($countones(v) !== 1 || $countones(u) !== 1 || $isunknown(u) !== 1'b1) begin $error("countones/isunknown"); errs++; end
    if ($isunknown(v) !== 1'b0 || $countbits(u, 1'bx) !== 1) begin $error("isunknown v / countbits x: %0d", $countbits(u, 1'bx)); errs++; end
    if ($countbits(u, '1, 'x) !== 2) begin $error("countbits 1,x: %0d", $countbits(u, '1, 'x)); errs++; end
    // 20.5
    if ($realtobits(1.5) !== 64'h3ff8000000000000) begin $error("realtobits"); errs++; end
    if (!near($bitstoreal(64'h3ff8000000000000), 1.5)) begin $error("bitstoreal"); errs++; end
    if ($shortrealtobits(1.5) !== 32'h3fc00000) begin $error("shortrealtobits"); errs++; end
    if (!near($bitstoshortreal(32'h3fc00000), 1.5)) begin $error("bitstoshortreal"); errs++; end
    // 20.8.2
    if (!near($atan2(1.0, 2.0), 0.4636476)) begin $error("atan2"); errs++; end
    if (!near($hypot(3.0, 4.0), 5.0)) begin $error("hypot"); errs++; end
    if (!near($asin(0.5), 0.5235988)) begin $error("asin"); errs++; end
    if (!near($acos(0.5), 1.0471976)) begin $error("acos"); errs++; end
    if (!near($atan(1.0), 0.7853982)) begin $error("atan"); errs++; end
    if (!near($sinh(1.0), 1.1752012)) begin $error("sinh"); errs++; end
    if (!near($cosh(1.0), 1.5430806)) begin $error("cosh"); errs++; end
    if (!near($tanh(1.0), 0.7615942)) begin $error("tanh"); errs++; end
    if (!near($asinh(1.0), 0.8813736)) begin $error("asinh"); errs++; end
    if (!near($acosh(2.0), 1.3169579)) begin $error("acosh"); errs++; end
    if (!near($atanh(0.5), 0.5493061)) begin $error("atanh"); errs++; end
    q = '{1,2,3};
    d = new[5];
    // 20.7
    if ($dimensions(v) !== 1 || $dimensions(pp) !== 2 || $dimensions(a) !== 3 || $dimensions(m) !== 3 ||
        $dimensions(s) !== 0 || $dimensions(i) !== 1 || $dimensions(q) !== 2) begin
      $error("dimensions %0d %0d %0d %0d %0d %0d %0d", $dimensions(v), $dimensions(pp), $dimensions(a), $dimensions(m), $dimensions(s), $dimensions(i), $dimensions(q)); errs++;
    end
    if ($unpacked_dimensions(v) !== 0 || $unpacked_dimensions(a) !== 2 || $unpacked_dimensions(m) !== 2 ||
        $unpacked_dimensions(s) !== 0 || $unpacked_dimensions(q) !== 1) begin $error("unpacked_dimensions"); errs++; end
    if ($left(v) !== 7 || $right(v) !== 0 || $low(v) !== 0 || $high(v) !== 7 || $increment(v) !== 1 || $size(v) !== 8) begin $error("v queries"); errs++; end
    if ($left(a) !== 3 || $right(a) !== 1 || $low(a) !== 1 || $high(a) !== 3 || $increment(a) !== 1 || $size(a) !== 3) begin $error("a queries"); errs++; end
    if ($left(a,2) !== 0 || $right(a,2) !== 4 || $low(a,2) !== 0 || $high(a,2) !== 4 || $increment(a,2) !== -1 || $size(a,2) !== 5) begin $error("a dim2 queries"); errs++; end
    if ($left(pp) !== 3 || $size(pp) !== 4 || $left(pp,2) !== 7 || $size(pp,2) !== 8) begin $error("pp queries"); errs++; end
    if ($left(m) !== 0 || $right(m) !== 1 || $size(m) !== 2 || $size(m,2) !== 5 || $size(m,3) !== 8) begin $error("m queries"); errs++; end
    if (!$isunknown($left(s)) || !$isunknown($right(s)) || !$isunknown($size(s))) begin $error("scalar queries must be x"); errs++; end
    if ($left(i) !== 31 || $right(i) !== 0 || $size(i) !== 32) begin $error("int queries"); errs++; end
    if ($left(q) !== 0 || $right(q) !== 2 || $low(q) !== 0 || $high(q) !== 2 || $size(q) !== 3) begin $error("queue queries"); errs++; end
    if ($left(d) !== 0 || $right(d) !== 4 || $low(d) !== 0 || $high(d) !== 4 || $increment(d) !== -1 || $size(d) !== 5) begin $error("dynamic array queries"); errs++; end
    if ($left(tv) !== 11 || $right(tv) !== 4 || $size(tv) !== 8) begin $error("typedef queries"); errs++; end
    if ($bits(v) !== 8 || $bits(a) !== 480 || $bits(m) !== 80) begin $error("bits"); errs++; end
    if (!$isunknown($size(v, 2)) || !$isunknown($left(v, 0))) begin $error("out of range dimension must give x"); errs++; end
    if ($size(a[2]) !== 5 || $size(a[2], 2) !== 32) begin $error("a[2] queries"); errs++; end
    // 20.6.1: scalar type names; vector and array forms are checked for content only
    if ($typename(b) != "bit" || $typename(by) != "byte" || $typename(sh) != "shortint" || $typename(i) != "integer" ||
        $typename(lg) != "longint" || $typename(bu) != "byte unsigned") begin
      $error("typename scalars: %s|%s|%s|%s|%s|%s", $typename(b), $typename(by), $typename(sh), $typename(i), $typename(lg), $typename(bu)); errs++;
    end
    ts = nows($typename(bv));
    if (ts != "bit[3:0]") begin $error("typename bv '%s'", ts); errs++; end
    ts = nows($typename(bsv));
    if (ts != "bitsigned[7:0]") begin $error("typename bsv '%s'", ts); errs++; end
    ts = nows($typename(a));
    if (pos(ts, "int") != 0 || pos(ts, "[3:1]") < 0 || pos(ts, "[0:4]") < 0 || pos(ts, "[3:1]") > pos(ts, "[0:4]")) begin
      $error("typename a '%s' (unpacked dimensions must keep declaration order)", ts); errs++;
    end
    ts = nows($typename(q));
    if (pos(ts, "byte") != 0 || pos(ts, "[$]") < 0) begin $error("typename q '%s' (queue must show [$])", ts); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
