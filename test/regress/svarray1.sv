// Self-checking. Expected values: IEEE 1800-2017 7.10.2 (queue insert; index == size appends), 7.12.1 (array
// locator methods: find*, min, max, unique*; the order of unique()/unique_index() results and the chosen
// representative are unspecified, so those results are compared as sets), 7.12.2 (reverse, sort, rsort, shuffle;
// ties between elements with equal sort keys may come out in any order), 7.12.3 (reduction methods; the result
// has the element type), 11.8 (integral promotion), 21.2.1.
// Left out (not defined by the LRM): the sum of an empty array.
`define CHK(tag, got, exp) if ((got) !== (exp)) begin errs++; $error("%s: got %0d expected %0d", tag, got, exp); end
module svarray1;
  int errs = 0;
  int q[$];
  int d[];
  int f[5] = '{5, 3, 9, 3, 1};
  int r[$], ri[$];
  byte b[] = '{1, 2, 3, 4};
  real rl[$];
  int y;
  // compare a queue with an expected element list
  function automatic void eq(string tag, int got[$], int exp[$]);
    if (got.size() != exp.size()) begin
      errs++; $error("%s: size %0d expected %0d", tag, got.size(), exp.size());
    end else foreach (got[i]) if (got[i] !== exp[i]) begin
      errs++; $error("%s[%0d]: got %0d expected %0d", tag, i, got[i], exp[i]);
    end
  endfunction
  function automatic void eqa(string tag, int got[], int exp[]);
    int t[$]; foreach (got[i]) t.push_back(got[i]);
    eq(tag, t, exp);
  endfunction
  function automatic void eqf(string tag, int got[5], int exp[5]);
    int t[$]; foreach (got[i]) t.push_back(got[i]);
    eq(tag, t, exp);
  endfunction
  initial begin
    q = '{4, 2, 7, 2, 9};
    d = new[4];
    d = '{4, 5, 3, 1};
    q.insert(1, 100);   eq("insert1", q, '{4, 100, 2, 7, 2, 9});
    q.insert(0, -1);    eq("insert0", q, '{-1, 4, 100, 2, 7, 2, 9});
    q.insert(7, 55);    eq("insert@size", q, '{-1, 4, 100, 2, 7, 2, 9, 55});
    q.reverse();        eq("reverse", q, '{55, 9, 2, 7, 2, 100, 4, -1});
    q.sort();           eq("sort", q, '{-1, 2, 2, 4, 7, 9, 55, 100});
    q.rsort;            eq("rsort", q, '{100, 55, 9, 7, 4, 2, 2, -1});
    d.sort;             eqa("d.sort", d, '{1, 3, 4, 5});
    f.sort();           eqf("f.sort", f, '{1, 3, 3, 5, 9});
    f.reverse;          eqf("f.reverse", f, '{9, 5, 3, 3, 1});
    y = q.sum();     `CHK("sum", y, 178)
    y = q.product(); `CHK("prod", y, -5544000)
    y = f.sum;       `CHK("fsum", y, 21)
    y = b.sum;       `CHK("bsum", y, 10)
    y = b.and;       `CHK("and", y, 0)
    y = b.or;        `CHK("or", y, 7)
    y = b.xor;       `CHK("xor", y, 4)
    r = q.min();     eq("min", r, '{-1});
    r = q.max();     eq("max", r, '{100});
    r = f.max;       eq("fmax", r, '{9});
    q = '{4, 2, 7, 2, 9, 4};
    r = q.unique();  r.sort; eq("uniq", r, '{2, 4, 7, 9});
    ri = q.unique_index();
    begin
      int v[$];
      foreach (ri[i]) begin
        if (ri[i] < 0 || ri[i] >= q.size()) begin errs++; $error("uniqi index out of range"); end
        else v.push_back(q[ri[i]]);
      end
      v.sort; eq("uniqi values", v, '{2, 4, 7, 9});
    end
    r = q.find with (item > 3);              eq("find", r, '{4, 7, 9, 4});
    ri = q.find_index with (item > 3);       eq("findi", ri, '{0, 2, 4, 5});
    r = q.find_first with (item > 3);        eq("ff", r, '{4});
    ri = q.find_first_index with (item > 3); eq("ffi", ri, '{0});
    r = q.find_last with (item > 3);         eq("fl", r, '{4});
    ri = q.find_last_index with (item > 3);  eq("fli", ri, '{5});
    r = q.find with (item > 100);            eq("none", r, '{});
    ri = q.find_first_index with (item == 100); eq("none idx", ri, '{});
    r = q.find(x) with (x == 2);             eq("x", r, '{2, 2});
    ri = q.find_index(x) with (x == x.index + 2); eq("idx", ri, '{});
    y = q.sum with (item * 2); `CHK("sumw", y, 56)
    q.sort with (-item);       eq("sortw", q, '{9, 7, 4, 4, 2, 2});
    q.sort(x) with (x % 3);
    begin
      // keys 0,1,1,1,2,2: nondecreasing keys; elements with equal keys may be in any order
      automatic int s = 0;
      foreach (q[i]) begin s += q[i]; if (i > 0 && (q[i] % 3) < (q[i-1] % 3)) begin errs++; $error("sortw2 key order at %0d", i); end end
      `CHK("sortw2 size", q.size(), 6) `CHK("sortw2 sum", s, 28)
      `CHK("sortw2 first", q[0], 9)
    end
    q.rsort with (item % 4);   eq("rsortw", q, '{7, 2, 2, 9, 4, 4});
    r = q.min with (-item);    eq("minw", r, '{9});
    r = q.unique with (item % 3);
    begin
      int k[$];
      foreach (r[i]) k.push_back(r[i] % 3);
      k.sort; eq("uniqw keys", k, '{0, 1, 2});
    end
    y = f.sum with (item + 1); `CHK("fsumw", y, 26)
    r = f.find with (item == 3); eq("ffind", r, '{3, 3});
    q = {};
    r = q.min; eq("emin", r, '{});
    q = '{1,2,3,4,5,6,7,8};
    q.shuffle();
    y = q.sum; `CHK("shuf sum", y, 36) `CHK("shuf size", q.size(), 8)
    d.shuffle;
    y = d.sum; `CHK("dshuf", y, 13)
    rl = '{3.5, 1.5, 2.0};
    rl.sort;
    begin
      bit t;
      t = (rl[0] == 1.5);       `CHK("rl0", t, 1'b1)
      t = (rl[1] == 2.0);       `CHK("rl1", t, 1'b1)
      t = (rl[2] == 3.5);       `CHK("rl2", t, 1'b1)
      t = (rl.sum == 7.0);      `CHK("rl.sum", t, 1'b1)
      t = (rl.product == 10.5); `CHK("rl.product", t, 1'b1)
    end
    if (errs == 0) $display("PASSED");
  end
endmodule
