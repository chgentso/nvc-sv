// Self-checking. Expected values: IEEE 1800-2017 7.4 (unpacked arrays of class handles, strings, mailboxes,
// structs, multidimensional), 8.7 (constructors with arguments), 6.16 (string len), 15.4 (mailbox put/get),
// 12.7.3 (foreach with several indices), 8.4 (handle comparison with null).
`define CHK(tag, got, exp) if ((got) !== (exp)) begin errs++; $error("%s: got %0d expected %0d", tag, got, exp); end
`define CHKS(tag, got, exp) if ((got) != (exp)) begin errs++; $error("%s: got %s expected %s", tag, got, exp); end
class K; int v; function new(int x); v = x; endfunction endclass
module svarraystruct2;
  int errs = 0;
  K objs[3];
  string s[3] = '{"a", "bb", "ccc"};
  string g[2][2];
  mailbox mb[2];
  typedef struct { int x; } e_t;
  e_t m[2][2];
  initial begin
    foreach (objs[i]) objs[i] = new(i * 2);
    `CHK("objs[1].v", objs[1].v, 2) `CHK("objs[2].v", objs[2].v, 4)
    `CHKS("s[0]", s[0], "a") `CHKS("s[2]", s[2], "ccc") `CHK("s[1].len", s[1].len(), 2)
    g[1][0] = "hi"; `CHKS("g[1][0]", g[1][0], "hi")
    `CHK("g[0][0] empty", g[0][0].len(), 0)
    mb[0] = new; mb[0].put(5);
    begin int r; mb[0].get(r); `CHK("mb", r, 5) end
    foreach (m[i,j]) m[i][j].x = i * 10 + j;
    `CHK("m[1][1]", m[1][1].x, 11) `CHK("m[0][1]", m[0][1].x, 1)
    `CHK("objs[0] nonnull", objs[0] != null, 1'b1)
    if (errs == 0) $display("PASSED");
  end
endmodule
