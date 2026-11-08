module svlrm_c7a;
  // Self-checking test. Expected values: IEEE 1800-2017 7.2 (structures), 7.2.1 (packed structures),
  // 7.3 / 5.10 / 10.9.2 (assignment patterns for structs: positional, named, default), 7.4 (packed
  // and unpacked arrays, indexing), 7.4.2 and 10.10 (array assignment patterns incl. index and
  // replication keys), 11.4.5 (struct equality is member-wise), 20.7 (array query functions: $left,
  // $right, $low, $high, $size, $dimensions, $increment; an int counts one packed dimension),
  // 20.6.2 ($bits), 21.2.1.7 (%p: member names and values are checked, not the exact layout).
  typedef struct { int a; byte b; logic [3:0] c; } s_t;
  typedef struct packed { logic [3:0] hi; logic [3:0] lo; } p_t;
  typedef struct { s_t inner; int arr[3]; } n_t;
  s_t s1, s2; p_t p1; n_t n1;
  int a2[2][3];
  int a3 [1:3];
  int a4 [3:1];
  bit [7:0] m [4];
  int errs;
  function automatic void ck(bit ok, string m);
    if (!ok) begin errs++; $error("%s", m); end
  endfunction
  function automatic string nosp(string s);
    string r = "";
    foreach (s[i]) if (s[i] != " ") r = {r, string'(s[i])};
    return r;
  endfunction
  function automatic bit has(string s, string sub);
    s = nosp(s);
    for (int i = 0; i + sub.len() <= s.len(); i++)
      if (s.substr(i, i + sub.len() - 1) == sub) return 1;
    return 0;
  endfunction
  initial begin
    s1 = '{1, 2, 4'ha}; ck(s1.a == 1 && s1.b == 2 && s1.c === 4'ha, "positional pattern");
    s2 = '{a: 5, b: -1, c: 4'h3}; ck(s2.a == 5 && s2.b == -1 && s2.c === 4'h3, "named pattern");
    s2 = '{default: 7}; ck(s2.a == 7 && s2.b == 7 && s2.c === 4'h7, "default pattern");
    s1 = s2; s1.a = 9; ck(s1.a == 9 && s2.a == 7, "struct assignment copies");
    ck($sformatf("%b", s1 == s2) == "0", "unequal structs");
    s2.a = 9; ck($sformatf("%b", s1 == s2) == "1", "equal structs");
    p1 = 8'hc5; ck(p1.hi === 4'hc && p1.lo === 4'h5, "packed struct from vector");
    p1 = '{hi: 4'h1, lo: 4'h2}; ck(p1 === 8'h12, "packed struct named pattern");
    p1.hi = p1.lo + 1; ck(p1 === 8'h32, "packed struct member write");
    n1 = '{inner: '{1, 2, 3}, arr: '{4, 5, 6}};
    ck(n1.inner.a == 1 && n1.arr[0] == 4 && n1.arr[1] == 5 && n1.arr[2] == 6, "nested pattern");
    n1.arr[1] = 50; n1.inner.b = -3; ck(n1.arr[1] == 50 && n1.inner.b == -3, "nested member writes");
    a2 = '{'{1,2,3}, '{4,5,6}};
    ck(a2[0][2] == 3 && a2[1][0] == 4 && a2[1][2] == 6, "2-d pattern");
    a2[1] = '{7,8,9}; ck(a2[1][1] == 8, "row assignment");
    a3 = '{10, 20, 30}; ck(a3[1] == 10 && a3[2] == 20 && a3[3] == 30, "a3[1:3]");
    // the first pattern element goes to the leftmost index, which is 3 for [3:1]
    a4 = '{10, 20, 30}; ck(a4[3] == 10 && a4[2] == 20 && a4[1] == 30, "a4[3:1]");
    ck($left(a3) == 1 && $right(a3) == 3 && $low(a3) == 1 && $high(a3) == 3, "queries a3");
    ck($left(a4) == 3 && $right(a4) == 1 && $low(a4) == 1 && $high(a4) == 3, "queries a4");
    ck($size(a2) == 2 && $size(a2, 2) == 3 && $dimensions(a2) == 3, "size/dimensions");
    ck($increment(a3) == -1 && $increment(a4) == 1, "increment");
    m = '{8'h11, 8'h22, 8'h33, 8'h44}; ck(m[0] == 8'h11 && m[3] == 8'h44, "bit array pattern");
    m = '{4{8'hab}}; ck(m[1] == 8'hab && m[2] == 8'hab, "replication pattern");
    m = '{default: 8'h5}; ck(m[3] == 8'h5, "default array pattern");
    m = '{0: 8'h1, 3: 8'h9, default: 8'h0}; ck(m[0] == 8'h1 && m[1] == 8'h0 && m[3] == 8'h9, "index keys");
    ck(has($sformatf("%p", s1), "a:9") && has($sformatf("%p", s1), "b:7") && has($sformatf("%p", s1), "c:7"), "%p struct");
    begin automatic string t = $sformatf("%p", n1);
      ck(has(t, "a:1") && has(t, "b:-3") && has(t, "c:3") && has(t, "4,50,6"), "%p nested struct"); end
    ck(has($sformatf("%p", a3), "10,20,30"), "%p array");
    ck($bits(s1) == 44, "$bits struct");
    ck($bits(n1) == 140, "$bits nested struct");
    if (errs == 0) $display("PASSED");
  end
endmodule
