// Self-checking associative arrays with multiple/aggregate/wide indices and locator methods.
// Expected values: IEEE 1800-2017 7.8 (associative arrays), 7.8.2 (string index), 7.8.3 (class index),
// 7.8.4 (integral index, ordered numerically), 7.9 (array methods), 7.12.1 (locator methods), 12.7.3 (foreach)
class key_c;
  int id;
  function new(int i); id = i; endfunction
endclass
module svassocidx1;
  int a1[string][string];
  int a2[int][4];
  int a3[3][string];
  int a4[key_c];
  int a5[bit [127:0]];
  int a6[logic [3:0]];
  int a7[string][int];
  int cnt[string];
  int q[$];
  string sq[$];
  key_c k1, k2;
  int errs = 0;
  int n, s, e;
  bit [127:0] bk;
  string sk;

  task automatic ck(input bit c, input string m);
    if (!c) begin errs++; $error("%s", m); end
  endtask

  initial begin
    a1["x"]["y"] = 1; a1["x"]["z"] = 2; a1["w"]["y"] = 3;
    ck(a1["x"]["y"] == 1 && a1["x"]["z"] == 2 && a1["w"]["y"] == 3, "a1 values");
    ck(a1.num() == 2, "a1.num");
    ck(a1["x"].num() == 2, "a1[x].num");
    ck(a1["w"].num() == 1, "a1[w].num");
    n = 0; s = 0; e = 0;
    foreach (a1[i, j]) begin
      n++; s += a1[i][j];
      if (i == "x" && j == "y") e += (a1[i][j] == 1);
      if (i == "x" && j == "z") e += (a1[i][j] == 2);
      if (i == "w" && j == "y") e += (a1[i][j] == 3);
    end
    ck(n == 3 && s == 6 && e == 3, "a1 foreach");

    a2[5][2] = 7; a2[9][0] = 8;
    ck(a2[5][2] == 7 && a2[9][0] == 8, "a2 values");
    ck(a2.num() == 2, "a2.num");
    n = 0; e = 0;
    foreach (a2[i, j]) begin
      n++;
      if (i == 5 && j == 2) e += (a2[i][j] == 7);
      else if (i == 9 && j == 0) e += (a2[i][j] == 8);
      else e += (a2[i][j] == 0);
    end
    ck(n == 8 && e == 8, "a2 foreach (2 keys x 4 elements)");

    a3[1]["p"] = 4; a3[2]["q"] = 5;
    ck(a3[1]["p"] == 4 && a3[2]["q"] == 5, "a3 values");
    ck(a3[0].num() == 0 && a3[1].num() == 1 && a3[2].num() == 1, "a3 nums");

    a7["s"][3] = 1; a7["s"][1] = 2;
    n = 0; e = 0;
    foreach (a7[i, j]) begin
      n++;
      if (i == "s" && j == 3) e += (a7[i][j] == 1);
      if (i == "s" && j == 1) e += (a7[i][j] == 2);
    end
    ck(n == 2 && e == 2, "a7 foreach");

    k1 = new(1); k2 = new(2);
    a4[k1] = 10; a4[k2] = 20;
    ck(a4[k1] == 10 && a4[k2] == 20, "a4 values");
    ck(a4.num() == 2 && a4.exists(k1), "a4 num/exists");
    a4.delete(k1);
    ck(a4.num() == 1 && !a4.exists(k1) && a4.exists(k2), "a4 after delete");

    a5[128'h1_0000_0000_0000_0000] = 1; a5[128'h5] = 2;
    ck(a5[128'h1_0000_0000_0000_0000] == 1 && a5[5] == 2 && a5.num() == 2, "a5 values");
    n = 0;
    foreach (a5[i]) begin
      n++;
      if (i == 128'h5) ck(a5[i] == 2, "a5[5]");
      else if (i == 128'h1_0000_0000_0000_0000) ck(a5[i] == 1, "a5[2^64]");
      else ck(0, "a5 unexpected key");
    end
    ck(n == 2, "a5 foreach count");
    // 7.8.4: integral indices are ordered numerically
    ck(a5.first(bk) && bk == 128'h5, "a5 first");
    ck(a5.next(bk) && bk == 128'h1_0000_0000_0000_0000, "a5 next");
    ck(!a5.next(bk), "a5 next at end");

    a6[4'h3] = 1;
    ck(a6.num() == 1 && a6.exists(4'h3) && !a6.exists(4'h4), "a6");

    cnt["a"] = 3; cnt["b"] = 1; cnt["c"] = 3; cnt["d"] = 2;
    sq = cnt.unique_index();
    ck(sq.size() == 3, "unique_index size (3 distinct values)");
    begin
      int seen[int];
      foreach (sq[i]) seen[cnt[sq[i]]] = 1;
      ck(seen.num() == 3, "unique_index covers distinct values");
    end
    sq = cnt.find_index with (item == 3);
    sq.sort();
    ck(sq.size() == 2 && sq[0] == "a" && sq[1] == "c", "find_index item==3");
    q = cnt.find with (item > 1);
    q.sort();
    ck(q.size() == 3 && q[0] == 2 && q[1] == 3 && q[2] == 3, "find item>1");
    q = cnt.unique();
    q.sort();
    ck(q.size() == 3 && q[0] == 1 && q[1] == 2 && q[2] == 3, "unique");
    q = cnt.max();
    ck(q.size() == 1 && q[0] == 3, "max");
    q = cnt.min();
    ck(q.size() == 1 && q[0] == 1, "min");
    ck(cnt.sum() == 9, "sum");
    // string indices are ordered lexicographically (7.8.2); first/last follow that order
    sq = cnt.find_first_index with (item == 3);
    ck(sq.size() == 1 && sq[0] == "a", "find_first_index");
    sq = cnt.find_last_index with (item == 3);
    ck(sq.size() == 1 && sq[0] == "c", "find_last_index");
    if (errs == 0) $display("PASSED");
  end
endmodule
