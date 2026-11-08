// Expected values: IEEE 1800-2017 6.19.5 (enum methods first/last/next/name/num; next of the last label wraps to the
// first), 7.10 (queue methods), 6.16 (string methods len/toupper), 7.2 (unpacked struct members),
// 23.6 (hierarchical references to variables of module and interface instances).
typedef enum logic [1:0] {A, B, C} k_e;
typedef struct { k_e kind; string tag; } rec_t;
module child;
  k_e k = B;
  int q[$] = '{1, 2, 3};
  string s = "hi";
  rec_t r = '{kind: C, tag: "xy"};
endmodule
interface bi;
  k_e k;
  rec_t sel_beat;
endinterface
module svhiermethod1;
  child u();
  bi b();
  int err = 0;
  `define CHKI(got, exp) if ((got) !== (exp)) begin err++; $error("%s: got %0d expected %0d", `"got`", got, exp); end
  `define CHKS(got, exp) if ((got) != (exp)) begin err++; $error("%s: got '%s' expected '%s'", `"got`", got, exp); end
  initial begin
    b.k = C;
    b.sel_beat.kind = A;
    b.sel_beat.tag = "abc";
    #1;
    `CHKS(u.k.name(), "B") `CHKI(u.k.num(), 3)
    `CHKS(b.k.name(), "C") `CHKI(b.k.first(), A) `CHKI(b.k.last(), C) `CHKI(b.k.next(), A)
    `CHKI(u.q.size(), 3) `CHKI(u.s.len(), 2) `CHKS(u.s.toupper(), "HI")
    `CHKS(u.r.kind.name(), "C") `CHKS(u.r.tag, "xy") `CHKI(u.r.tag.len(), 2)
    `CHKS(b.sel_beat.kind.name(), "A") `CHKS(b.sel_beat.tag, "abc") `CHKI(b.sel_beat.tag.len(), 3)
    u.q.push_back(9);
    `CHKI(u.q.size(), 4) `CHKI(u.q[3], 9)
    if (err == 0) $display("PASSED");
  end
endmodule
