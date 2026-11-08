// A structure with strings, enumerations and reals as the index of an associative array (self-checking).
// Expected values: IEEE 1800-2017 7.8 (write allocates, overwrite of an existing key, missing key reads default),
// 7.9.1-7.9.7 (num, exists, delete, first/next/last/prev; no order is defined for such keys, so traversals are
// checked as sets), 12.7.3 (foreach).  Keys are compared member by member.
// No key has x/z bits: 7.8.4 makes an index with x/z bits invalid.
typedef enum {R, G, B} col_t;
typedef struct { int a; string s; } in_t;
typedef struct { in_t i; col_t c; real r; logic [3:0] l; } k_t;
module svassockey2;
  int aa [k_t];
  int bb [in_t];
  k_t k, kf; in_t n, nf;
  int errs = 0;
  int cnt, sum, mask;

  task automatic ck(input bit c, input string m);
    if (!c) begin errs++; $error("%s", m); end
  endtask

  initial begin
    n.a = 1; n.s = "b"; bb[n] = 1;
    n.a = 1; n.s = "a"; bb[n] = 2;
    n.a = 0; n.s = "z"; bb[n] = 3;
    n.a = 1; n.s = "b"; bb[n] = 4;
    ck(bb.num() == 3, "A: overwrite keeps 3 keys");
    ck(bb[n] == 4, "A: overwritten value");
    cnt = 0; mask = 0;
    if (bb.first(nf)) begin
      do begin
        cnt++;
        if (nf.a == 0 && nf.s == "z") begin mask |= 1; ck(bb[nf] == 3, "B: (0,z)"); end
        else if (nf.a == 1 && nf.s == "a") begin mask |= 2; ck(bb[nf] == 2, "B: (1,a)"); end
        else if (nf.a == 1 && nf.s == "b") begin mask |= 4; ck(bb[nf] == 4, "B: (1,b)"); end
        else ck(0, "B: unexpected key");
      end while (bb.next(nf));
    end
    ck(cnt == 3 && mask == 7, "B: traversal visits every key once");

    k.i.a = 2; k.i.s = "x"; k.c = G; k.r = 1.5; k.l = 4'h3; aa[k] = 7;
    k.r = 0.5; aa[k] = 8;
    k.c = R; aa[k] = 9;
    k.l = 4'h5; aa[k] = 10;
    ck(aa.num() == 4, "C: four distinct keys");
    ck(aa[k] == 10, "C: last value");
    cnt = 0; mask = 0;
    if (aa.last(kf)) begin
      do begin
        cnt++;
        ck(kf.i.a == 2 && kf.i.s == "x", "D: key.i");
        if (kf.c == G && kf.r == 1.5 && kf.l === 4'h3) begin mask |= 1; ck(aa[kf] == 7, "D: value 7"); end
        else if (kf.c == G && kf.r == 0.5 && kf.l === 4'h3) begin mask |= 2; ck(aa[kf] == 8, "D: value 8"); end
        else if (kf.c == R && kf.r == 0.5 && kf.l === 4'h3) begin mask |= 4; ck(aa[kf] == 9, "D: value 9"); end
        else if (kf.c == R && kf.r == 0.5 && kf.l === 4'h5) begin mask |= 8; ck(aa[kf] == 10, "D: value 10"); end
        else ck(0, "D: unexpected key");
      end while (aa.prev(kf));
    end
    ck(cnt == 4 && mask == 15, "D: backward traversal visits every key once");

    n.a = 1; n.s = "b";
    bb.delete(n);
    ck(bb.num() == 2 && !bb.exists(n), "E: delete");
    cnt = 0; mask = 0;
    foreach (bb[q]) begin
      cnt++;
      if (q.a == 0 && q.s == "z") mask |= 1;
      else if (q.a == 1 && q.s == "a") mask |= 2;
      else ck(0, "F: unexpected key");
    end
    ck(cnt == 2 && mask == 3, "F: foreach");
    n.a = 5; n.s = "q";
    ck(bb[n] == 0, "G: missing key reads default");
    ck(bb.num() == 2, "G: read does not allocate");
    bb.delete();
    ck(bb.num() == 0, "H: delete() empties");
    if (errs == 0) $display("PASSED");
  end
endmodule
