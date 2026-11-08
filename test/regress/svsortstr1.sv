// Expected values: IEEE 1800-2017 7.12.1 (ordering methods; the order of unique()/unique_index()
// results is unrelated to the array order), 7.12.2 (sort/rsort, ties in a with-expression key are
// unordered), 7.12.3 (min/max return a queue of one element), 7.12.1 (find keeps array order),
// 6.16 (string comparison is lexicographic by byte value, "" < "Apple" < "apple")
module svsortstr1;
  string q[$] = '{"pear", "apple", "fig", "banana", "Apple", ""};
  string d[];
  string a[4] = '{"d", "bb", "a", "c"};
  string m[$];
  string as[string];
  int errs = 0;

  function automatic bit has(string s[$], string x);
    foreach (s[i]) if (s[i] == x) return 1;
    return 0;
  endfunction

  // exact ordered comparison
  task automatic same(string name, string got[$], string exp[$]);
    if (got.size() != exp.size()) begin
      $error("%s: size %0d expected %0d", name, got.size(), exp.size()); errs++;
    end else
      foreach (got[i]) if (got[i] != exp[i]) begin
        $error("%s[%0d]: got \"%s\" expected \"%s\"", name, i, got[i], exp[i]); errs++;
      end
  endtask

  // same elements in any order (all expected elements distinct)
  task automatic sameset(string name, string got[$], string exp[$]);
    if (got.size() != exp.size()) begin
      $error("%s: size %0d expected %0d", name, got.size(), exp.size()); errs++;
    end else
      foreach (exp[i]) if (!has(got, exp[i])) begin
        $error("%s: missing \"%s\"", name, exp[i]); errs++;
      end
  endtask

  string t[$], pre[$];
  int ix[$];
  initial begin
    q.sort();  same("sort", q, '{"", "Apple", "apple", "banana", "fig", "pear"});
    q.rsort(); same("rsort", q, '{"pear", "fig", "banana", "apple", "Apple", ""});
    d = new[3]; d[0] = "z"; d[1] = "y"; d[2] = "x";
    d.sort();  t = d; same("dyn sort", t, '{"x", "y", "z"});
    d.rsort(); t = d; same("dyn rsort", t, '{"z", "y", "x"});
    a.sort();  t = a; same("fixed sort", t, '{"a", "bb", "c", "d"});
    a.rsort(); t = a; same("fixed rsort", t, '{"d", "c", "bb", "a"});
    m = q.find(x) with (x.len() > 3);
    same("find", m, '{"pear", "banana", "apple", "Apple"});
    // keys: "" 0, fig 3, pear 4, apple 5, Apple 5, banana 6 (apple/Apple tie)
    q.sort(x) with (x.len());
    if (q.size() != 6 || q[0] != "" || q[1] != "fig" || q[2] != "pear" || q[5] != "banana" ||
        !((q[3] == "apple" && q[4] == "Apple") || (q[3] == "Apple" && q[4] == "apple"))) begin
      $error("sort with len key wrong"); errs++;
    end
    q.rsort(x) with (x.len());
    if (q.size() != 6 || q[0] != "banana" || q[3] != "pear" || q[4] != "fig" || q[5] != "" ||
        !((q[1] == "apple" && q[2] == "Apple") || (q[1] == "Apple" && q[2] == "apple"))) begin
      $error("rsort with len key wrong"); errs++;
    end
    m = q.min(); same("min", m, '{""});
    m = q.max(); same("max", m, '{"pear"});
    m = q.unique();
    sameset("unique (all distinct)", m, '{"banana", "apple", "Apple", "pear", "fig", ""});
    pre = q;
    q.reverse();
    if (q.size() != pre.size()) begin $error("reverse size"); errs++; end
    else foreach (q[i]) if (q[i] != pre[pre.size()-1-i]) begin $error("reverse[%0d]", i); errs++; end
    q = '{"b", "a", "b", "c", "a"};
    ix = q.unique_index();
    if (ix.size() != 3) begin $error("unique_index size %0d expected 3", ix.size()); errs++; end
    else begin
      string v[$];
      foreach (ix[i]) begin
        if (ix[i] < 0 || ix[i] > 4) begin $error("unique_index %0d out of range", ix[i]); errs++; end
        else v.push_back(q[ix[i]]);
      end
      sameset("unique_index values", v, '{"a", "b", "c"});
    end
    m = q.unique(); sameset("unique", m, '{"b", "a", "c"});
    m = q.min(); same("min2", m, '{"a"});
    m = q.max(); same("max2", m, '{"c"});
    if (errs == 0) $display("PASSED");
  end
endmodule
