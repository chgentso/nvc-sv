// %p of associative arrays with enum, packed and string keys
// Expected values: IEEE 1800-2017 21.2.1.7 (%p assignment-pattern style, key:value pairs),
// 7.8.2 / 7.8.4 (ascending key order for enum and integral keys), 6.19.4 (casting a non-member value into an enum)
// Whitespace is ignored; %0p is implementation-dependent and only checked for its content.
module svpfmtassoc1;
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
  typedef enum {DIRECT, VECTORED, EXTRA=7} mode_t;
  typedef enum logic [1:0] {A1=1, B1=2} e2_t;
  int a[mode_t];
  bit [3:0] b[e2_t];
  string s[mode_t];
  int n[string];
  int nested[mode_t][mode_t];
  struct packed { bit [3:0] x; bit [3:0] y; } ps;
  int c[bit [3:0]];
  string first;

  initial begin
    a[DIRECT] = 98; a[VECTORED] = 102;
    b[A1] = 3; b[B1] = 4;
    s[EXTRA] = "hi";
    n["k"] = 1;
    nested[DIRECT][EXTRA] = 5;
    c[4'd3] = 1;
    exact("a", $sformatf("%p", a), "'{DIRECT:98,VECTORED:102}");
    cont("a0", $sformatf("%0p", a), "DIRECT98VECTORED102");
    exact("b", $sformatf("%p", b), "'{A1:3,B1:4}");
    cont("s", $sformatf("%p", s), "EXTRAhi");
    cont("n", $sformatf("%p", n), "k1");
    cont("nested", $sformatf("%p", nested), "DIRECTEXTRA5");
    exact("c", $sformatf("%p", c), "'{3:1}");
    a[mode_t'(5)] = 1;
    first = $sformatf("%p", a);
    // key 5 is not an enum member: its text is not fixed, but it sorts after VECTORED (value 1)
    if (!has(first, "DIRECT") || !has(first, "VECTORED") || !has(first, "98") || !has(first, "102")) begin
      $error("a+: %s", first); errs++;
    end
    if ($sformatf("%p", a) != first) begin $error("%%p is not repeatable"); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
