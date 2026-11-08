// Self-checking. Expected values: IEEE 1800-2017 7.12.1 (locator methods on an associative array return queues
// of matching elements or of their indices (keys); item.index is the key), 7.9 (associative arrays; the
// traversal order is only fixed for first/next, so results with several matches are compared as sets, and
// find_first/find_last are required to return two different matches), 7.12.1 (no match gives an empty queue).
`define CHK(tag, got, exp) if ((got) !== (exp)) begin errs++; $error("%s: got %0d expected %0d", tag, got, exp); end
class Tx;
  int id; int data;
  function new(int i, int d); id = i; data = d; endfunction
endclass

module svassocfind1;
  int errs = 0;
  Tx txq[int];
  int vals[int];
  int sparse[int];
  int idx[$], vq[$], fi[$], la[$];
  Tx res[$];

  initial begin
    txq[10] = new(1, 100);
    txq[3] = new(2, 200);
    txq[7] = new(2, 300);
    vals[5] = 50; vals[1] = 10; vals[9] = 90;
    fi = txq.find_first_index() with (item.id == 2);
    la = txq.find_last_index() with (item.id == 2);
    `CHK("first size", fi.size(), 1) `CHK("last size", la.size(), 1)
    if (fi.size() == 1 && la.size() == 1) begin
      `CHK("first is a match", (fi[0] == 3) || (fi[0] == 7), 1'b1)
      `CHK("last is a match", (la[0] == 3) || (la[0] == 7), 1'b1)
      `CHK("first != last", fi[0] != la[0], 1'b1)
    end
    idx = txq.find_index() with (item.id == 2);
    idx.sort;
    `CHK("all size", idx.size(), 2)
    if (idx.size() == 2) begin `CHK("all[0]", idx[0], 3) `CHK("all[1]", idx[1], 7) end
    res = txq.find() with (item.id == 2);
    `CHK("find", res.size(), 2)
    res = txq.find_first() with (item.id == 1);
    `CHK("find_first size", res.size(), 1)
    if (res.size() == 1) `CHK("first data", res[0].data, 100)
    idx = txq.find_first_index with (item.id == 99);
    `CHK("none", idx.size(), 0)
    vq = vals.find() with (item > 20);
    vq.sort;
    `CHK("vals size", vq.size(), 2)
    if (vq.size() == 2) begin `CHK("vals[0]", vq[0], 50) `CHK("vals[1]", vq[1], 90) end
    idx = vals.find_index() with (item > 20 && item.index < 9);
    `CHK("idx size", idx.size(), 1)
    if (idx.size() == 1) `CHK("idx[0]", idx[0], 5)
    idx = sparse.find_first_index() with (item == 1);
    `CHK("empty", idx.size(), 0)
    vals.delete(1);
    idx = vals.find_first_index() with (item == 50);
    `CHK("after delete size", idx.size(), 1)
    if (idx.size() == 1) `CHK("after delete", idx[0], 5)
    if (errs == 0) $display("PASSED");
  end
endmodule
