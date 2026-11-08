module assoc2;
  int ia[int], ib[int];
  int sa[string];
  int q[$];
  string s;
  int r;
  initial begin
    sa[1] = 2;
    r = ia.exists();
    r = ia.first(1);
    r = ia.num(3);
    r = ia.first(s);
    q = ia;
    ia = sa;
    r = (ia == ib);
    r = ia[s];
  end
endmodule
