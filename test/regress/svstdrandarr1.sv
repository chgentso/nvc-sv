// Expected values: IEEE 1800-2017 18.12 (std::randomize with inline constraints), 18.5.3 (foreach iterative
// constraints), 18.5.4/11.4.13 (inside, with an unpacked array/queue as the value set), 7.5 (dynamic array size)
module svstdrandarr1;
  initial begin
    int a[];
    int unsigned b[];
    int pool[];
    automatic int lst[$] = {4, 5, 6};
    automatic int errs = 0;
    int sz[];
    a = new[6];
    repeat (20) begin
      if (!std::randomize(a) with { foreach (a[i]) a[i] inside {[1:3]}; }) begin $error("randomize a failed"); errs++; end
      foreach (a[i]) if (a[i] < 1 || a[i] > 3) begin $error("a[%0d]=%0d outside 1..3", i, a[i]); errs++; end
      if (a.size() != 6) begin $error("a.size()=%0d", a.size()); errs++; end
    end
    b = new[4];
    if (!std::randomize(b) with { foreach (b[j]) { if (j < 2) b[j] == j + 10; else b[j] inside {lst}; } })
      begin $error("randomize b failed"); errs++; end
    if (b[0] != 10 || b[1] != 11) begin $error("b[0..1]=%0d,%0d expected 10,11", b[0], b[1]); errs++; end
    if (!(b[2] inside {lst}) || !(b[3] inside {lst})) begin $error("b[2..3]=%0d,%0d not in {4,5,6}", b[2], b[3]); errs++; end
    pool = new[5];
    if (!std::randomize(pool) with { foreach (pool[k]) pool[k] == k * k; }) begin $error("randomize pool failed"); errs++; end
    foreach (pool[k]) if (pool[k] != k * k) begin $error("pool[%0d]=%0d expected %0d", k, pool[k], k * k); errs++; end
    if (pool.size() != 5) begin $error("pool.size()=%0d", pool.size()); errs++; end
    if (!std::randomize(sz) with { sz.size() == 3; foreach (sz[m]) sz[m] == 7 - m; }) begin $error("randomize sz failed"); errs++; end
    if (sz.size() != 3) begin $error("sz.size()=%0d expected 3", sz.size()); errs++; end
    else foreach (sz[m]) if (sz[m] != 7 - m) begin $error("sz[%0d]=%0d expected %0d", m, sz[m], 7 - m); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
