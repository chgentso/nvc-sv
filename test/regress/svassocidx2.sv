// Self-checking associative arrays with packed-struct, enum, wide and string indices and locator methods.
// Expected values: IEEE 1800-2017 7.8 (associative arrays), 7.8.4 (integral index, numeric order),
// 7.8.2 (string index, lexicographic order), 7.9.4-7.9.7 (first/last/next/prev), 7.12.1 (locator methods:
// the order of unique_index results is unspecified, so results are sorted or compared as sets)
typedef struct packed { bit [3:0] a; bit [7:0] b; } ps_t;
typedef enum { RED, GREEN, BLUE } col_t;
module svassocidx2;
  int pk[ps_t];
  int ek[col_t];
  int wk[bit [99:0]];
  int sk[string];
  int nested[string][string];
  int cnt[int];
  ps_t k;
  col_t c;
  bit [99:0] w;
  string s;
  string sq[$];
  int iq[$];
  ps_t pq[$];
  col_t cq[$];
  bit [99:0] wq[$];
  int errs = 0;
  int n;

  task automatic ck(input bit c, input string m);
    if (!c) begin errs++; $error("%s", m); end
  endtask

  initial begin
    k = '{a: 4'h1, b: 8'h22}; pk[k] = 5;
    k = '{a: 4'h0, b: 8'hFF}; pk[k] = 6;
    k = '{a: 4'h9, b: 8'h01}; pk[k] = 5;
    ck(pk.num() == 3, "pk.num");
    n = 0;
    foreach (pk[i]) begin
      n++;
      if (i == 12'h122 || i == 12'h901) ck(pk[i] == 5, "pk value 5");
      else if (i == 12'h0FF) ck(pk[i] == 6, "pk value 6");
      else ck(0, "pk unexpected key");
    end
    ck(n == 3, "pk foreach count");
    pq = pk.unique_index();
    ck(pq.size() == 2, "pk unique_index size (values {5,6})");
    if (pq.size() == 2) ck(pk[pq[0]] != pk[pq[1]], "pk unique_index distinct values");
    pq = pk.find_index with (item == 5);
    ck(pq.size() == 2, "pk find_index size");
    if (pq.size() == 2) begin
      ck(pk[pq[0]] == 5 && pk[pq[1]] == 5 && pq[0] != pq[1], "pk find_index values");
      ck((pq[0] == 12'h122 && pq[1] == 12'h901) || (pq[0] == 12'h901 && pq[1] == 12'h122), "pk find_index keys");
    end

    ek[BLUE] = 3; ek[RED] = 3; ek[GREEN] = 1;
    cq = ek.unique_index();
    ck(cq.size() == 2, "ek unique_index size");
    if (cq.size() == 2) ck(ek[cq[0]] != ek[cq[1]], "ek unique_index distinct values");
    cq = ek.find_index with (item == 3 && item.index != RED);
    ck(cq.size() == 1 && cq[0] == BLUE && cq[0].name() == "BLUE", "ek find_index");

    wk[100'h1 << 80] = 7; wk[100'h5] = 8; wk[100'h1 << 99] = 7;
    wq = wk.unique_index();
    ck(wq.size() == 2, "wk unique_index size");
    if (wq.size() == 2) ck(wk[wq[0]] != wk[wq[1]], "wk unique_index distinct values");
    // numeric ordering: the last index holding 7 is the larger one
    wq = wk.find_last_index with (item == 7);
    ck(wq.size() == 1 && wq[0] == (100'h1 << 99), "wk find_last_index");
    wq = wk.find_first_index with (item == 7);
    ck(wq.size() == 1 && wq[0] == (100'h1 << 80), "wk find_first_index");

    nested["a"]["b"] = 1; nested["a"]["c"] = 2; nested["z"]["b"] = 3;
    sq = nested["a"].find_index with (item > 1);
    ck(sq.size() == 1 && sq[0] == "c", "nested find_index");

    cnt[5] = 1; cnt[2] = 1; cnt[9] = 4;
    iq = cnt.unique_index();
    ck(iq.size() == 2, "cnt unique_index size");
    if (iq.size() == 2) ck(cnt[iq[0]] != cnt[iq[1]], "cnt unique_index distinct values");
    iq = cnt.find_index with (item.index > 3 && item == 4);
    ck(iq.size() == 1 && iq[0] == 9, "cnt find_index");

    sk["foo"] = 1; sk["bar"] = 2;
    sq = sk.find_index with (item.index == "foo" || item == 2);
    sq.sort();
    ck(sq.size() == 2 && sq[0] == "bar" && sq[1] == "foo", "sk find_index");
    sq = sk.unique_index with (item % 2);
    ck(sq.size() == 2, "sk unique_index with (item % 2): parities differ");
    if (sq.size() == 2) ck(sq[0] != sq[1], "sk unique_index distinct indices");
    ck(sk.first(s) && s == "bar", "sk first");
    ck(sk.next(s) && s == "foo", "sk next");
    ck(!sk.next(s), "sk next at end");
    ck(wk.last(w) && w == (100'h1 << 99), "wk last");
    ck(wk.prev(w) && w == (100'h1 << 80), "wk prev");
    ck(wk.prev(w) && w == 100'h5, "wk prev 2");
    ck(!wk.prev(w), "wk prev at start");
    if (errs == 0) $display("PASSED");
  end
endmodule
