// $random with and without seed, and the $dist_* functions.
// Expected values: IEEE 1800-2017 20.15.1 ($random: seed argument is an inout variable updated by
// every call), 20.15.2 ($dist_*: same seed gives the same results; ranges of each distribution) and
// Annex N (normative algorithms: seed = 69069*seed + 1, the float mantissa mapping of uniform(), the
// truncation of rtl_dist_uniform, rtl_exponential). The initial internal seed of unseeded $random is
// implementation-defined, so unseeded values are only checked for varying.
module svrandom1;
  int s1 = 7, s2 = -99, r;
  int errs = 0;

  // Annex N uniform(): advances the seed and maps the new seed to [start, end)
  function automatic real uni(inout int seed, input int start, input int fin);
    real d, a, b, c;
    bit [31:0] us;
    d = 0.00000011920928955078125;
    if (seed == 0) seed = 259341593;
    a = start; b = fin;
    seed = 69069 * seed + 1;
    us = seed;
    us = (us >> 9) | 32'h3f800000;
    c = $bitstoshortreal(us);
    c = c + (c * d);
    c = ((b - a) * (c - 1.0)) + a;
    return c;
  endfunction
  // truncation toward minus infinity as in Annex N: (long) r for r >= 0, (long) (r - 1) otherwise
  function automatic int rnd_floor(real x);
    longint i;
    if (x >= 0) i = $rtoi(x); else i = $rtoi(x - 1);
    if (i > 64'sd2147483647) i = 64'sd2147483647;
    if (i < -64'sd2147483648) i = -64'sd2147483648;
    return int'(i);
  endfunction
  // $random(seed) is rtl_dist_uniform(seed, INT_MIN, INT_MAX): full-range branch of Annex N
  function automatic int model_random(inout int seed);
    real r;
    r = (uni(seed, -2147483648, 2147483647) + 2147483648.0) / 4294967295.0;
    r = r * 4294967296.0;
    r = r - 2147483648.0;
    return rnd_floor(r);
  endfunction
  // $dist_uniform(seed, start, end): end is incremented, result is truncated toward minus infinity
  function automatic int model_uniform(inout int seed, input int start, input int fin);
    return rnd_floor(uni(seed, start, fin + 1));
  endfunction
  function automatic int model_exponential(inout int seed, input int mean);
    real n;
    n = uni(seed, 0, 1);
    if (n != 0) n = -$ln(n) * mean;
    if (n >= 0) n = n + 0.5; else n = n - 0.5;
    return int'($rtoi(n));
  endfunction

  int m1, m2, sold, sdiff, first, v;
  bit varied;
  int a_seq[20], b_seq[20];
  int t1, t2;
  initial begin
    // unseeded: should not be constant over 20 calls
    first = $random;
    for (int i = 1; i < 20; i++) begin r = $random; if (r != first) varied = 1; end
    if (!varied) begin errs++; $error("unseeded $random returned a constant"); end

    // seeded: seed update and values follow Annex N
    m1 = s1; m2 = s2;
    for (int i = 0; i < 20; i++) begin
      sold = s1;
      r = $random(s1);
      v = model_random(m1);
      a_seq[i] = r;
      if (s1 != 69069 * sold + 1) begin errs++; $error("a[%0d]: seed %0d, expected %0d", i, s1, 69069 * sold + 1); end
      if (s1 != m1 || r !== v) begin errs++; $error("a[%0d]: got %0d seed %0d expected %0d seed %0d", i, r, s1, v, m1); end
    end
    for (int i = 0; i < 20; i++) begin
      sold = s2;
      r = $random(s2);
      v = model_random(m2);
      b_seq[i] = r;
      if (s2 != 69069 * sold + 1) begin errs++; $error("b[%0d]: bad seed update", i); end
      if (s2 != m2 || r !== v) begin errs++; $error("b[%0d]: got %0d expected %0d", i, r, v); end
    end
    // reproducibility from the same starting seed
    t1 = 7;
    for (int i = 0; i < 20; i++) begin r = $random(t1); if (r !== a_seq[i]) begin errs++; $error("a not reproducible"); end end
    t2 = -99;
    for (int i = 0; i < 20; i++) begin r = $random(t2); if (r !== b_seq[i]) begin errs++; $error("b not reproducible"); end end

    // $dist_*: each call advances the seed; exponential and uniform match Annex N exactly
    m1 = s1;
    sold = s1;
    r = $dist_exponential(s1, 10);
    v = model_exponential(m1, 10);
    if (r !== v || s1 != m1) begin errs++; $error("dist_exponential got %0d seed %0d expected %0d seed %0d", r, s1, v, m1); end
    if (r < 0) begin errs++; $error("exponential negative"); end
    sold = s1; r = $dist_poisson(s1, 10);
    if (r < 0 || s1 == sold) begin errs++; $error("dist_poisson %0d", r); end
    sold = s1; r = $dist_chi_square(s1, 3);
    if (r < 0 || s1 == sold) begin errs++; $error("dist_chi_square %0d", r); end
    sold = s1; r = $dist_t(s1, 4);
    if (s1 == sold) begin errs++; $error("dist_t did not update seed"); end
    sold = s1; r = $dist_erlang(s1, 2, 10);
    if (r < 0 || s1 == sold) begin errs++; $error("dist_erlang %0d", r); end
    sold = s1; r = $dist_normal(s1, 100, 20);
    if (s1 == sold) begin errs++; $error("dist_normal did not update seed"); end
    m2 = s2;
    r = $dist_uniform(s2, -50, 50);
    v = model_uniform(m2, -50, 50);
    if (r !== v || s2 != m2) begin errs++; $error("dist_uniform got %0d seed %0d expected %0d seed %0d", r, s2, v, m2); end
    if (r < -50 || r > 50) begin errs++; $error("uniform out of range %0d", r); end
    // reproducibility of a distribution from the same seed
    t1 = 1234; t2 = 1234;
    if ($dist_normal(t1, 100, 20) !== $dist_normal(t2, 100, 20) || t1 != t2) begin errs++; $error("dist_normal not reproducible"); end
    if (errs == 0) $display("PASSED");
  end
endmodule
