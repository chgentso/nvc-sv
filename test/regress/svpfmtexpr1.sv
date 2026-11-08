// %p of expressions: slices, locator and reduction method results, struct and scalar values
// Expected values: IEEE 1800-2017 21.2.1.7 (%p), 7.10.2 (queue slices), 7.12.1 (locator methods; unique()
// returns the set of unique values in unspecified order), 7.12.3 (min/max return a queue), 6.16 (string)
// Whitespace is ignored; %0p is implementation-dependent and only checked for its content.
module svpfmtexpr1;
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
  int q[$] = '{5, 4, 3, 2, 1};
  int d[] = '{1, 2, 3};
  int a[3] = '{7, 8, 9};
  int aa[string];
  typedef struct { int x; int y; } s_t;
  s_t st = '{1, 2};
  function automatic int f(); return 5; endfunction
  function automatic s_t sf(); return '{3, 4}; endfunction
  string str = "hello", tmp;

  initial begin
    string u;
    tmp = $sformatf("%p", q[2:$]);
    exact("q[2:$]", tmp, "'{3,2,1}");
    tmp = $sformatf("%p", q[0:$-2]);
    exact("q[0:$-2]", tmp, "'{5,4,3}");
    tmp = $sformatf("%p", q[1:2]);
    exact("q[1:2]", tmp, "'{4,3}");
    tmp = $sformatf("%p", d[1:2]);
    exact("d[1:2]", tmp, "'{2,3}");
    tmp = $sformatf("%p", a[0:1]);
    exact("a[0:1]", tmp, "'{7,8}");
    tmp = $sformatf("%p", q.find with (item > 2));
    exact("find", tmp, "'{5,4,3}");
    tmp = $sformatf("%p", q.find_index with (item > 2));
    exact("find_index", tmp, "'{0,1,2}");
    tmp = $sformatf("%p", q.min());
    exact("min", tmp, "'{1}");
    tmp = $sformatf("%p", q.max());
    exact("max", tmp, "'{5}");
    u = $sformatf("%p", q.unique());
    if (strip(u).len() != 12 || strip(u).substr(0, 1) != "'{" ||
        !has(u, "1") || !has(u, "2") || !has(u, "3") || !has(u, "4") || !has(u, "5")) begin
      $error("unique: %s", u); errs++;
    end
    begin
      string x;
      x = $sformatf("%p", sf());
      if (content(x) != "x3y4" && content(x) != "34") begin $error("sf: %s", x); errs++; end
      x = $sformatf("%p", st);
      if (content(x) != "x1y2" && content(x) != "12") begin $error("st: %s", x); errs++; end
    end
    tmp = $sformatf("%p", f());
    exact("f()", tmp, "5");
    tmp = $sformatf("%p", q.size());
    exact("size", tmp, "5");
    tmp = $sformatf("%p", str);
    cont("str", tmp, "hello");
    tmp = $sformatf("%p", d);
    exact("d", tmp, "'{1,2,3}");
    tmp = $sformatf("%p", 5);
    exact("5", tmp, "5");
    if (!has($sformatf("%p", 3.5), "3.5")) begin $error("3.5: %s", $sformatf("%p", 3.5)); errs++; end
    begin
      automatic string x = $sformatf("%p", 4'b10x1);
      if (!has(x, "x") && !has(x, "X")) begin $error("4'b10x1 lost its x: %s", x); errs++; end
    end
    tmp = $sformatf("%p %p", q[0], q[$]);
    exact("q[0] q[$]", tmp, "51");
    tmp = $sformatf("%p", a);
    exact("a", tmp, "'{7,8,9}");
    tmp = $sformatf("%0p", q);
    cont("q0", tmp, "54321");
    begin
      automatic string x = $sformatf("%0p", st);
      if (content(x) != "x1y2" && content(x) != "12") begin $error("st0: %s", x); errs++; end
    end
    tmp = $sformatf("%p %s", d, str);
    cont("d str", tmp, "123hello");
    if (errs == 0) $display("PASSED");
  end
endmodule
