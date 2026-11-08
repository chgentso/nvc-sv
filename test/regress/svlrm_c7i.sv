// Self-checking: array reduction and locator methods with several element types.
// Expected values: IEEE 1800-2017 7.12.1 (locator methods; the order of unique() results is unspecified, so the
// result is sorted before comparing), 7.12.2 (ordering; min/max of an empty array return an empty queue),
// 7.12.3 (reduction methods: result has the element type, a with-expression widens/narrows it; x element gives x)
`define CHK(g, e) if ((g) !== (e)) begin errs++; $error("check failed at line %0d", `__LINE__); end
`define CHKR(g, e) if ((g) != (e)) begin errs++; $error("real check failed at line %0d", `__LINE__); end
module svlrm_c7i;
  bit [7:0] b[] = '{1, 2, 3, 250};
  int i[] = '{-5, 3, 0, 8};
  real r[] = '{1.5, 2.5, -0.5};
  string s[] = '{"b", "a", "c"};
  logic [3:0] l[] = '{4'h1, 4'hx, 4'h3};
  int errs;
  initial begin
    `CHK(b.sum(), 8'd0)                       // 256 wraps in bit [7:0]
    `CHK(b.sum() with (int'(item)), 256)
    `CHK(b.sum() with (32'(item)), 256)
    `CHK(i.sum(), 6)
    begin int q[$]; q = i.min(); `CHK(q.size(), 1) `CHK(q[0], -5) end
    begin int q[$]; q = i.max(); `CHK(q.size(), 1) `CHK(q[0], 8) end
    `CHK(i.product(), 0)
    `CHK(i.and(), 0) `CHK(i.or(), -5)
    `CHK(i.xor(), -16)
    `CHKR(r.sum(), 3.5)
    `CHKR(r.product(), -1.875)
    begin real q[$]; q = r.min(); `CHKR(q[0], -0.5) end
    begin real q[$]; q = r.max(); `CHKR(q[0], 2.5) end
    begin string q[$]; q = s.min(); `CHK(q[0], "a") q = s.max(); `CHK(q[0], "c") end
    s.sort(); `CHK(s[0], "a") `CHK(s[1], "b") `CHK(s[2], "c")
    s.rsort(); `CHK(s[0], "c") `CHK(s[1], "b") `CHK(s[2], "a")
    `CHK(l.sum() === 4'bx, 1)
    begin int q[$]; q = i.find with (item < 0); `CHK(q.size(), 1) `CHK(q[0], -5) end
    begin int q[$]; q = i.find_index with (item > 0); `CHK(q.size(), 2) `CHK(q[0], 1) `CHK(q[1], 3) end
    begin int q[$]; q = i.find_last_index with (item >= 0); `CHK(q.size(), 1) `CHK(q[0], 3) end
    begin int q[$]; q = i.min with (item * item); `CHK(q.size(), 1) `CHK(q[0], 0) end
    begin int q[$]; q = i.max with (item * item); `CHK(q.size(), 1) `CHK(q[0], 8) end
    begin
      int q[$];
      q = i.unique with (item * item);        // squares 25, 9, 0, 64 are all distinct
      q.sort();
      `CHK(q.size(), 4) `CHK(q[0], -5) `CHK(q[1], 0) `CHK(q[2], 3) `CHK(q[3], 8)
    end
    `CHK(i.sum with (item) + 100, 106)
    begin automatic int j[] = new[0]; int q[$]; q = j.min(); `CHK(q.size(), 0) end
    begin automatic bit [3:0] z[3] = '{1,1,1}; `CHK(z.sum(), 4'd3) `CHK(z.sum() with (4'(item)), 4'd3) end
    begin automatic byte by[] = '{100, 100, 100}; `CHK(by.sum(), 8'sd44) end   // 300 wraps in byte
    begin automatic int a[] = '{1,2,3}; foreach (a[k]) a[k] = a[k] * 2; `CHK(a.size(), 3) `CHK(a[0], 2) `CHK(a[1], 4) `CHK(a[2], 6) end
    begin int a[5]; a = '{default: 1}; `CHK(a.sum(), 5) end
    if (errs == 0) $display("PASSED");
  end
endmodule
