// %p applied to function-call results; each call is evaluated exactly once
// Expected values: IEEE 1800-2017 21.2.1.7 (%p), 13.4 (a function call in an expression is evaluated once),
// 7.10 / 7.4 (aggregate return values), 8.7 (class methods)
module svpfmtcall1;
  function automatic string strip(string x);
    string r = "";
    for (int i = 0; i < x.len(); i++)
      if (x[i] != " " && x[i] != 8'h09 && x[i] != 8'h0a) r = {r, string'(x[i])};
    return r;
  endfunction
  // keep letters, digits, ".", "-" and "_" only: the values, member names and keys in order
  function automatic string content(string x);
    string r = "";
    for (int i = 0; i < x.len(); i++)
      if ((x[i] >= "0" && x[i] <= "9") || (x[i] >= "a" && x[i] <= "z") || (x[i] >= "A" && x[i] <= "Z") ||
          x[i] == "." || x[i] == "-" || x[i] == "_") r = {r, string'(x[i])};
    return r;
  endfunction
  function automatic bit has(string x, string sub);
    for (int i = 0; i + sub.len() <= x.len(); i++) if (x.substr(i, i + sub.len() - 1) == sub) return 1;
    return 0;
  endfunction
  int errs = 0;
  task automatic exact(string what, string got, string exp);   // whitespace-insensitive
    if (strip(got) != exp) begin $error("%s: got \"%s\" expected \"%s\"", what, got, exp); errs++; end
  endtask
  task automatic cont(string what, string got, string exp);    // content only
    if (content(got) != exp) begin $error("%s: got \"%s\" content \"%s\" expected \"%s\"", what, got, content(got), exp); errs++; end
  endtask
  typedef struct { int x; bit [3:0] y; } st_t;
  typedef int arr_t[3];
  typedef int q_t[$];
  int cnt;
  class C;
    st_t s;
    function st_t get(); cnt++; return s; endfunction
    function q_t gq(); q_t r; cnt++; r.push_back(7); r.push_back(8); return r; endfunction
  endclass
  function st_t mk(int v); st_t s; cnt++; s.x = v; s.y = v+1; return s; endfunction
  function arr_t fa(); arr_t a; cnt++; a = '{1,2,3}; return a; endfunction
  function q_t fq(); cnt++; return '{4,5}; endfunction
  string str, tmp;
  // a struct prints either positionally or with member names; both carry the same values
  task automatic st_eq(string what, string got, string v1, string v2);
    if (content(got) != {"x", v1, "y", v2} && content(got) != {v1, v2}) begin
      $error("%s: got \"%s\"", what, got); errs++;
    end
  endtask

  initial begin
    static C o = new;
    string t1;
    o.s.x = 9;
    tmp = $sformatf("%p", mk(3));
    st_eq("mk(3)", tmp, "3", "4");
    if (cnt != 1) begin $error("cnt=%0d expected 1", cnt); errs++; end
    tmp = $sformatf("%p", fa());
    exact("fa", tmp, "'{1,2,3}");
    tmp = $sformatf("%p", fq());
    exact("fq", tmp, "'{4,5}");
    if (cnt != 3) begin $error("cnt=%0d expected 3", cnt); errs++; end
    t1 = $sformatf("%p|%p", o.get(), o.gq());
    if (cnt != 5) begin $error("cnt=%0d expected 5", cnt); errs++; end
    begin
      automatic int bar = -1;
      for (int i = 0; i < t1.len(); i++) if (t1[i] == "|") bar = i;
      if (bar < 0) begin $error("no separator in %s", t1); errs++; end
      else begin
        st_eq("o.get()", t1.substr(0, bar - 1), "9", "0");
        exact("o.gq()", t1.substr(bar + 1, t1.len() - 1), "'{7,8}");
      end
    end
    $write("%p\n", mk(5));
    if (cnt != 6) begin $error("cnt=%0d expected 6 after $write", cnt); errs++; end
    str = $sformatf("%p", mk(6));
    st_eq("mk(6)", str, "6", "7");
    if (cnt != 7) begin $error("cnt=%0d expected 7", cnt); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
