// Self-checking: arrays of queues, queues of queues, nested associative arrays.
// Expected values: IEEE 1800-2017 7.4.5, 7.5, 7.8, 7.10, 12.7.3 (foreach with several indices; associative
// traversal order is not relied on), 21.2.1.7 (%p content only)
`define CHK(g, e) if ((g) !== (e)) begin errs++; $error("check failed at line %0d", `__LINE__); end
module svlrm_c7h;
  int qa[3][$];
  int aq[$][2];
  int aa[string][string];
  int ad[string][];
  int qq[$][$];
  int aaq[int][$];
  int errs; int n; int sum;
  function automatic bit has(string s, string sub);
    for (int i = 0; i + sub.len() <= s.len(); i++)
      if (s.substr(i, i + sub.len() - 1) == sub) return 1;
    return 0;
  endfunction
  initial begin
    string str;
    qa[0].push_back(1); qa[0].push_back(2); qa[2].push_back(9);
    `CHK(qa[0].size(), 2) `CHK(qa[1].size(), 0) `CHK(qa[2].size(), 1)
    `CHK(qa[0][1], 2) `CHK(qa[2][0], 9)
    aa["a"]["x"] = 1; aa["a"]["y"] = 2; aa["b"]["x"] = 3;
    `CHK(aa.num(), 2) `CHK(aa["a"].num(), 2)
    `CHK(aa["a"]["y"] + aa["b"]["x"], 5)
    n = 0; sum = 0;
    foreach (aa[i, j]) begin n++; sum += aa[i][j]; `CHK(aa[i].exists(j), 1) end
    `CHK(n, 3) `CHK(sum, 6)
    ad["k"] = new[3]; ad["k"][1] = 5; `CHK(ad["k"].size(), 3) `CHK(ad["k"][1], 5)
    qq.push_back('{1,2}); qq.push_back('{3});
    `CHK(qq.size(), 2) `CHK(qq[0].size(), 2) `CHK(qq[1][0], 3)
    qq[1].push_back(4); `CHK(qq[1].size(), 2)
    n = 0;
    foreach (qq[i, j]) begin
      `CHK(qq[i][j], (i == 0) ? j + 1 : j + 3)
      n++;
    end
    `CHK(n, 4)
    aaq[5].push_back(1); aaq[5].push_back(2); aaq[7].push_back(3);
    `CHK(aaq[5].size(), 2) `CHK(aaq[7].size(), 1)
    n = 0;
    foreach (aaq[k]) begin n++; `CHK(aaq[k].size(), (k == 5) ? 2 : 1) end
    `CHK(n, 2)
    aq.push_back('{1,2}); aq.push_back('{3,4});
    `CHK(aq[1][0], 3) `CHK(aq.size(), 2)
    str = $sformatf("%p", aa);
    `CHK(has(str, "a"), 1) `CHK(has(str, "b"), 1) `CHK(has(str, "x"), 1) `CHK(has(str, "y"), 1)
    `CHK(has(str, "3"), 1)
    if (errs == 0) $display("PASSED");
  end
endmodule
