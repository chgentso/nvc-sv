// Self-checking: queues and their methods.
// Expected values: IEEE 1800-2017 7.10, 7.10.1, 7.10.2, 7.10.3 (bounded queue q[$:3] holds indices 0..3, 7.10),
// 7.12.1, 7.12.2, 7.12.3, 11.4.12 (concatenation)
`define CHK(g, e) if ((g) !== (e)) begin errs++; $error("check failed at line %0d", `__LINE__); end
`define CHKQ(q, e) if (!eqq(q, e)) begin errs++; $error("queue check failed at line %0d", `__LINE__); end
`define CHKS(q, e) if (!eqs(q, e)) begin errs++; $error("string queue check failed at line %0d", `__LINE__); end
module svlrm_c7d;
  int q[$]; int r[$] = {1, 2, 3}; string s[$];
  int v; int errs;
  function automatic bit eqq(int a[$], int b[$]);
    if (a.size() != b.size()) return 0;
    foreach (a[i]) if (a[i] !== b[i]) return 0;
    return 1;
  endfunction
  function automatic bit eqs(string a[$], string b[$]);
    if (a.size() != b.size()) return 0;
    foreach (a[i]) if (a[i] != b[i]) return 0;
    return 1;
  endfunction
  initial begin
    q.push_back(1); q.push_back(2); q.push_front(0); q.push_front(-1);
    `CHKQ(q, '{-1, 0, 1, 2}) `CHK(q.size(), 4)
    v = q.pop_front(); `CHK(v, -1)
    v = q.pop_back(); `CHK(v, 2)
    `CHKQ(q, '{0, 1})
    q.insert(1, 50); `CHKQ(q, '{0, 50, 1})
    q.insert(0, 60); `CHKQ(q, '{60, 0, 50, 1})
    q.delete(1); `CHKQ(q, '{60, 50, 1})
    `CHK(q[0], 60) `CHK(q[$], 1)
    q[$] = 77; `CHKQ(q, '{60, 50, 77})
    q = {q, r}; `CHKQ(q, '{60, 50, 77, 1, 2, 3})
    q = {10, q}; `CHKQ(q, '{10, 60, 50, 77, 1, 2, 3})
    q = q[1:3]; `CHKQ(q, '{60, 50, 77})
    q = {}; `CHK(q.size(), 0)
    q = r; r.delete(); `CHKQ(q, '{1, 2, 3}) `CHK(r.size(), 0)
    q[1] = 20; `CHK(q[1], 20)
    r = q; r.push_back(9); `CHK(q.size(), 3) `CHK(r.size(), 4)
    q = '{5, 4, 3, 2, 1};
    begin int t[$]; t = q[2:$]; `CHKQ(t, '{3, 2, 1}) t = q[0:$-2]; `CHKQ(t, '{5, 4, 3}) end
    q.sort(); `CHKQ(q, '{1, 2, 3, 4, 5})
    q.reverse(); `CHKQ(q, '{5, 4, 3, 2, 1})
    `CHK(q.sum(), 15)
    begin int t[$]; t = q.find with (item > 2); `CHKQ(t, '{5, 4, 3}) end
    q.delete(); q.push_back(3); q.push_back(4); `CHKQ(q, '{3, 4})
    foreach (q[i]) `CHK(q[i], 3 + i)
    s.push_back("a"); s.push_back("bb"); s.push_front("c");
    `CHKS(s, '{"c", "a", "bb"})
    s.sort(); `CHKS(s, '{"a", "bb", "c"})
    `CHK(s[1], "bb")
    s.insert(2, "zz"); `CHKS(s, '{"a", "bb", "zz", "c"}) `CHK(s.size(), 4)
    begin
      int bq[$:3];
      bq.push_back(1); bq.push_back(2); bq.push_back(3); bq.push_back(4);
      `CHK(bq.size(), 4) `CHKQ(bq, '{1, 2, 3, 4})
    end
    begin int w[$]; int x; w = '{1,2,3}; x = w.pop_back() + w.pop_front(); `CHK(x, 4) `CHKQ(w, '{2}) end
    begin
      automatic int w[$] = '{3,1,2}; int idx[$];
      idx = w.find_index with (item == 1); `CHKQ(idx, '{1})
      w.delete(idx[0]); `CHKQ(w, '{3, 2})
    end
    if (errs == 0) $display("PASSED");
  end
endmodule
