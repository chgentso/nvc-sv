module svlrm_c7b;
  // Self-checking test. Expected values: IEEE 1800-2017 7.5 (dynamic arrays: new[], new[n](src),
  // size, delete), 7.6 (array assignment, copy semantics), 7.4 and 10.10 (array concatenation of
  // unpacked arrays), 7.12 (array manipulation methods: reduction sum/product/and/or/xor, ordering
  // reverse/sort/rsort/shuffle, locator methods min/max/find*/unique*, and the `with` clause),
  // 21.2.1.7 (%p: only the element values are checked, not the layout).
  // sort with a key leaves elements with equal keys in unspecified relative order (7.12.2).
  int d[], e[];
  int fix[4] = '{1, 2, 3, 4};
  int errs;
  function automatic void ck(bit ok, string m);
    if (!ok) begin errs++; $error("%s", m); end
  endfunction
  function automatic bit hasp(string s, string sub);
    string r = "";
    foreach (s[i]) if (s[i] != " ") r = {r, string'(s[i])};
    for (int i = 0; i + sub.len() <= r.len(); i++)
      if (r.substr(i, i + sub.len() - 1) == sub) return 1;
    return 0;
  endfunction
  initial begin
    d = new[3]; ck(d.size() == 3, "new[3]");
    foreach (d[i]) d[i] = i * 10;
    ck(d[0] == 0 && d[1] == 10 && d[2] == 20, "fill");
    e = d; e[0] = 99; ck(d[0] == 0 && e[0] == 99, "copy on assignment");
    d = new[5](d); ck(d.size() == 5 && d[0] == 0 && d[2] == 20 && d[4] == 0, "new[5](d) keeps old, zero fills");
    d = new[2]; ck(d.size() == 2 && d[0] == 0, "new[2] re-allocates with default values");
    d.delete(); ck(d.size() == 0, "delete");
    d = '{5, 6, 7}; ck(d.size() == 3 && d[2] == 7, "pattern assign");
    d = {d, 8}; ck(d.size() == 4 && d[3] == 8, "concat append");
    d = {0, d}; ck(d.size() == 5 && d[0] == 0, "concat prepend");
    d = fix; ck(d.size() == 4 && d[3] == 4, "fixed to dynamic");
    fix = '{9, 8, 7, 6}; ck(fix[0] == 9, "fixed re-assign");
    d = new[4]; foreach (d[i]) d[i] = fix[i] * 2;      // 18 16 14 12
    ck(hasp($sformatf("%p", d), "18,16,14,12"), "%p dynamic array");
    ck(d.sum() == 60, "sum");
    ck(d.product() == 18 * 16 * 14 * 12, "product");
    ck(d.min()[0] == 12, "min");
    ck(d.max()[0] == 18, "max");
    d.reverse(); ck(d[0] == 12 && d[1] == 14 && d[2] == 16 && d[3] == 18, "reverse");
    d.sort();    ck(d[0] == 12 && d[1] == 14 && d[2] == 16 && d[3] == 18, "sort");
    d.rsort();   ck(d[0] == 18 && d[1] == 16 && d[2] == 14 && d[3] == 12, "rsort");
    d.shuffle(); d.sort(); ck(d[0] == 12 && d[1] == 14 && d[2] == 16 && d[3] == 18, "shuffle then sort");
    ck((d.sum() with (item * 2)) == 120, "sum with");
    ck((d.sum() with (item > 12 ? 1 : 0)) == 3, "sum with conditional");
    begin int q[$]; q = d.find with (item > 14); ck(q.size() == 2 && q[0] == 16 && q[1] == 18, "find"); end
    begin int q[$]; q = d.find_first with (item > 14); ck(q.size() == 1 && q[0] == 16, "find_first"); end
    begin int q[$]; q = d.find_last with (item < 14); ck(q.size() == 1 && q[0] == 12, "find_last"); end
    begin int q[$]; q = d.find_index with (item == 14); ck(q.size() == 1 && q[0] == 1, "find_index"); end
    begin int q[$]; q = d.find_first_index with (item > 100); ck(q.size() == 0, "find_first_index none"); end
    begin int q[$]; q = d.unique(); ck(q.size() == 4 && q[0] == 12 && q[3] == 18, "unique of distinct"); end
    begin automatic int u[] = '{1,2,2,3,3,3}; int q[$];
      q = u.unique();       ck(q.size() == 3 && q[0] == 1 && q[1] == 2 && q[2] == 3, "unique");
      q = u.unique_index(); ck(q.size() == 3 && q[0] == 0 && q[1] == 1 && q[2] == 3, "unique_index"); end
    begin automatic int u[] = '{5,1,4}; u.sort() with (item % 3);
      ck(u[2] == 5 && ((u[0] == 1 && u[1] == 4) || (u[0] == 4 && u[1] == 1)), "sort with key"); end
    begin automatic bit [3:0] b[] = '{1,2,4}; ck(b.or() == 7 && b.and() == 0 && b.xor() == 7, "or/and/xor"); end
    begin automatic int u[] = '{3,5,6}; ck((u.or() with (item - 1)) == 7, "or with"); end
    ck(fix.sum() == 30, "fixed sum");
    if (errs == 0) $display("PASSED");
  end
endmodule
