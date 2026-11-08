// Self-checking: associative arrays and their methods.
// Expected values: IEEE 1800-2017 7.8, 7.8.1, 7.9.1-7.9.7, 7.12.1-7.12.3, 11.4.1, 12.7.3
// (traversal order is only checked through first/next/prev/last, never through foreach order)
`define CHK(g, e) if ((g) !== (e)) begin errs++; $error("check failed at line %0d", `__LINE__); end
module svlrm_c7c;
  int a[string]; int b[int]; int c[*]; bit [3:0] d[bit [3:0]]; int e[int unsigned];
  string k; int v; int errs;
  int cnt, sum;
  bit [3:0] dk;
  int unsigned uk;
  initial begin
    a["x"] = 1; a["y"] = 2; a["z"] = 3;
    `CHK(a.num(), 3) `CHK(a.size(), 3)
    `CHK(a.exists("y"), 1) `CHK(a.exists("q"), 0)
    `CHK(a.first(k), 1) `CHK(k, "x")
    `CHK(a.last(k), 1) `CHK(k, "z")
    k = "x"; `CHK(a.next(k), 1) `CHK(k, "y")
    k = "y"; `CHK(a.prev(k), 1) `CHK(k, "x")
    k = "z"; `CHK(a.next(k), 0)
    k = "x"; `CHK(a.prev(k), 0)
    a.delete("y"); `CHK(a.num(), 2) `CHK(a.exists("y"), 0)
    cnt = 0; sum = 0;
    foreach (a[i]) begin cnt++; sum += a[i]; `CHK(a.exists(i), 1) `CHK(i != "y", 1) end
    `CHK(cnt, 2) `CHK(sum, 4)
    a.delete(); `CHK(a.num(), 0)
    b[5] = 50; b[-3] = 30; b[100] = 1000; b[0] = 0;
    cnt = 0; sum = 0;
    foreach (b[i]) begin cnt++; sum += i; `CHK(b[i] != 0 || i == 0, 1) end
    `CHK(cnt, 4) `CHK(sum, 102)
    `CHK(b.first(v), 1) `CHK(v, -3)
    `CHK(b.last(v), 1) `CHK(v, 100)
    v = 0; `CHK(b.next(v), 1) `CHK(v, 5)
    v = 5; `CHK(b.prev(v), 1) `CHK(v, 0)
    c[3] = 1; c[1] = 2; `CHK(c.num(), 2)
    d[4'hf] = 4'ha; d[4'h1] = 4'hb;
    `CHK(d.num(), 2) `CHK(d[4'hf], 4'ha) `CHK(d[4'h1], 4'hb)
    `CHK(d.first(dk), 1) `CHK(dk, 4'h1)
    `CHK(d.next(dk), 1) `CHK(dk, 4'hf)
    e[32'hffff_fff0] = 1; e[5] = 2;
    `CHK(e.num(), 2)
    `CHK(e.first(uk), 1) `CHK(uk, 5)          // unsigned index ordering
    `CHK(e.last(uk), 1) `CHK(uk, 32'hffff_fff0)
    `CHK(b[5] + b[100], 1050)
    b[5]++; b[5] += 10; `CHK(b[5], 61)
    `CHK(b.exists(77), 0)
    `CHK(b.num(), 4)
    begin
      int q[$]; int s;
      q = b.find with (item > 40);
      `CHK(q.size(), 2)
      s = 0; foreach (q[i]) s += q[i];
      `CHK(s, 1061)
    end
    begin
      int q[$]; int s;
      q = b.find_index with (item > 40);
      `CHK(q.size(), 2)
      s = 0; foreach (q[i]) s += q[i];
      `CHK(s, 105)
    end
    begin
      int q[$];
      `CHK(b.sum(), 1091)
      q = b.max(); `CHK(q.size(), 1) `CHK(q[0], 1000)
    end
    begin int copy[int]; copy = b; copy[5] = -1; `CHK(b[5], 61) `CHK(copy[5], -1) `CHK(copy.num(), 4) end
    a["k1"] = 7; `CHK(a["k1"], 7)
    begin int idx; idx = 5; `CHK(b[idx], 61) end
    if (errs == 0) $display("PASSED");
  end
endmodule
