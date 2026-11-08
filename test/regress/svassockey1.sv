// An unpacked structure as the index of an associative array (self-checking).
// Expected values: IEEE 1800-2017 7.8 (associative arrays: write allocates, read of a missing entry gives the
// default value), 7.9.1 (num/size), 7.9.3 (exists), 7.9.4-7.9.7 (first/last/next/prev; no order is specified
// for an unpacked-struct index, so the checks are on the set of visited keys and on next/prev being inverse),
// 7.8.4 (integral/packed index: numeric order)
typedef struct { byte B; int I; } up_t;
typedef struct packed { logic [3:0] x; bit [3:0] y; } pk_t;
module svassockey1;
  int a1 [up_t];
  int a2 [pk_t];
  int a3 [bit [3:0]];
  up_t u; pk_t p; up_t uf;
  pk_t pf;
  up_t fwd[$], bwd[$];
  int errs = 0;
  int seen;

  task automatic ck(input bit c, input string m);
    if (!c) begin errs++; $error("%s", m); end
  endtask

  initial begin
    u.B = 1; u.I = 5;
    a1[u] = 10;
    u.B = 0; u.I = 9; a1[u] = 20;
    u.B = 1; u.I = 2; a1[u] = 30;
    ck(a1.num() == 3 && a1.size() == 3, "A: three distinct keys");
    u.B = 1; u.I = 5;
    ck(a1[u] == 10 && a1.exists(u), "B: existing key");
    u.I = 6;
    ck(a1.exists(u) == 0, "C: missing key does not exist");
    ck(a1[u] == 0, "C: missing key reads default");
    ck(a1.num() == 3, "C: read does not allocate");
    // forward traversal with first/next
    fwd.delete();
    if (a1.first(uf)) begin
      fwd.push_back(uf);
      while (a1.next(uf)) fwd.push_back(uf);
    end
    ck(fwd.size() == 3, "D/E: forward traversal visits 3 keys");
    // backward traversal with last/prev
    bwd.delete();
    if (a1.last(uf)) begin
      bwd.push_back(uf);
      while (a1.prev(uf)) bwd.push_back(uf);
    end
    ck(bwd.size() == 3, "F/G: backward traversal visits 3 keys");
    if (fwd.size() == 3 && bwd.size() == 3)
      for (int i = 0; i < 3; i++)
        ck(fwd[i].B == bwd[2-i].B && fwd[i].I == bwd[2-i].I, "prev traversal is the reverse of next traversal");
    seen = 0;
    foreach (fwd[i]) begin
      if (fwd[i].B == 1 && fwd[i].I == 5) begin seen |= 1; ck(a1[fwd[i]] == 10, "value of (1,5)"); end
      else if (fwd[i].B == 0 && fwd[i].I == 9) begin seen |= 2; ck(a1[fwd[i]] == 20, "value of (0,9)"); end
      else if (fwd[i].B == 1 && fwd[i].I == 2) begin seen |= 4; ck(a1[fwd[i]] == 30, "value of (1,2)"); end
      else ck(0, "unexpected key");
    end
    ck(seen == 7, "all three keys visited");
    u.B = 0; u.I = 9; a1.delete(u);
    ck(a1.num() == 2 && !a1.exists(u), "H: delete one key");

    p = 8'h12; a2[p] = 1; p = 8'h03; a2[p] = 2; p = 8'hf0; a2[p] = 3;
    // packed index: numeric order (7.8.4)
    ck(a2.first(pf) && pf == 8'h03, "I: first packed key");
    ck(a2.next(pf) && pf == 8'h12, "J: second packed key");
    ck(a2.next(pf) && pf == 8'hf0, "J: third packed key");
    ck(!a2.next(pf), "J: no fourth key");
    ck(a2.num() == 3 && a2.exists(p), "K: packed keys");
    a2.delete(p);
    ck(a2.num() == 2 && !a2.exists(p), "L: after delete");
    if (errs == 0) $display("PASSED");
  end
endmodule
