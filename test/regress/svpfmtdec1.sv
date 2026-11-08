// IEEE 1800-2017 21.2.1.7: %p prints a value as an assignment pattern;
// the layout is the implementation's.  nvc prints no space before the
// closing brace and '{} when empty, strings as escaped literals (5.9),
// only the first member of an untagged union (21.2.1.7), a tagged union
// as a tagged union expression (11.9), and %0p as %p without the spaces
// after commas.
// 8.4: a handle is not integral, so $display of a handle prints it as
// if by %p and a null handle is padded with spaces.
module svpfmtdec1;
  typedef struct { int a; string s; } st_t;
  typedef union { int i; byte b; } u_t;
  typedef union tagged { int I; void N; } tu_t;
  class C; int x = 5; C nxt; endclass
  int aa[int], ea[int];
  string sa[string], s = "a\nb\t\"q\"\\";
  int q[$] = '{1, 2};
  st_t st;
  u_t u;
  tu_t t1, t2;
  C c, cn;
  int err = 0;

  task automatic check(string got, string exp);
    if (got != exp) begin
      $display("FAILED got [%s] expected [%s]", got, exp);
      err++;
    end
  endtask

  initial begin
    aa[1] = 2; aa[3] = 1; sa["x"] = "y\n";
    st.a = 1; st.s = "h\"i"; u.i = 7;
    t1 = tagged I 5; t2 = tagged N; c = new;
    check($sformatf("%p", aa), "'{1:2, 3:1}");
    check($sformatf("%0p", aa), "'{1:2,3:1}");
    check($sformatf("%p|%0p", ea, ea), "'{}|'{}");
    check($sformatf("%p", sa), "'{\"x\":\"y\\n\"}");
    check($sformatf("%p", s), "\"a\\nb\\t\\\"q\\\"\\\\\"");
    check($sformatf("%p", st), "'{a:1, s:\"h\\\"i\"}");
    check($sformatf("%0p", st), "'{a:1,s:\"h\\\"i\"}");
    check($sformatf("%p", u), "'{i:7}");
    check($sformatf("%p|%p|%0p", t1, t2, t1), "tagged I 5|tagged N|tagged I 5");
    check($sformatf("%p", c), "'{x:5, nxt:null}");
    check($sformatf("%0p", c), "'{x:5,nxt:null}");
    check($sformatf("%p|%0p", q, q), "'{1, 2}|'{1,2}");
    check($sformatf("[%p]", cn), "[null]");
    if (err == 0) $display("PASSED");
  end
endmodule
