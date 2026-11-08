// %p / %0p of queues, dynamic, fixed, multi-dimensional and associative arrays
// Expected values: IEEE 1800-2017 21.2.1.7 (%p prints an assignment pattern: '{v0, v1, ...}; associative
// arrays as key:value in ascending key order), 7.8, 7.10, 6.19 (enum values print as labels)
// Whitespace is ignored; %0p is implementation-dependent, so it is only checked for the values, in order.
module svpfmtarr1;
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
  typedef struct { int a; string s; } st_t;
  typedef enum { RED, GREEN, BLUE } color_t;
  int q[$];
  int e[$];
  int d[];
  int dz[];
  string sq[$];
  int u[3] = '{7, 8, 9};
  int v[3:1] = '{4, 5, 6};
  int n[2][2] = '{'{1, 2}, '{3, 4}};
  st_t sa[2];
  color_t cq[$];
  byte bq[$];
  logic [3:0] lq[$];
  real rq[$];
  string sn[2];
  int as[string];
  int ai[int];
  int ae[int];
  string ss[int];
  color_t ca[string];

  initial begin
    q = {1, 2, 3};
    d = new[3];
    d = '{4, 5, 6};
    sq = {"a", "bc"};
    sa[0] = '{1, "p"}; sa[1] = '{2, "q"};
    cq = {RED, BLUE};
    bq = {1, 2};
    lq = {4'b1x01, 4'b0000};
    rq = {1.5, 2.25};
    sn[0] = "x"; sn[1] = "yz";
    as["x"] = 1; as["y"] = 2;
    ai[10] = 1; ai[3] = 2;
    ss[2] = "b"; ss[1] = "a";
    ca["r"] = RED; ca["b"] = BLUE;
    exact("q", $sformatf("%p", q), "'{1,2,3}");
    exact("e", $sformatf("%p", e), "'{}");
    exact("d", $sformatf("%p", d), "'{4,5,6}");
    exact("dz", $sformatf("%p", dz), "'{}");
    cont("sq", $sformatf("%p", sq), "abc");
    exact("u", $sformatf("%p", u), "'{7,8,9}");
    exact("v", $sformatf("%p", v), "'{4,5,6}");
    exact("n", $sformatf("%p", n), "'{'{1,2},'{3,4}}");
    begin
      automatic string x = $sformatf("%p", sa);
      if (!has(x, "1") || !has(x, "p") || !has(x, "2") || !has(x, "q")) begin $error("sa: %s", x); errs++; end
    end
    cont("cq", $sformatf("%p", cq), "REDBLUE");
    cont("bq", $sformatf("%p", bq), "12");
    begin
      automatic string x = $sformatf("%p", lq);
      if (!has(x, "x") && !has(x, "X")) begin $error("lq lost its unknown bits: %s", x); errs++; end
      if (!has(x, "0")) begin $error("lq: %s", x); errs++; end
    end
    begin
      automatic string x = $sformatf("%p", rq);
      if (!has(x, "1.5") || !has(x, "2.25")) begin $error("rq: %s", x); errs++; end
    end
    cont("q0", $sformatf("%0p", q), "123");
    cont("e0", $sformatf("%0p", e), "");
    cont("sq0", $sformatf("%0p", sq), "abc");
    cont("u0", $sformatf("%0p", u), "789");
    cont("v0", $sformatf("%0p", v), "456");
    cont("n0", $sformatf("%0p", n), "1234");
    cont("sn", $sformatf("%p", sn), "xyz");
    cont("sn0", $sformatf("%0p", sn), "xyz");
    exact("as", $sformatf("%p", as), "'{\"x\":1,\"y\":2}");
    exact("ai", $sformatf("%p", ai), "'{3:2,10:1}");
    exact("ae", $sformatf("%p", ae), "'{}");
    cont("ss", $sformatf("%p", ss), "1a2b");
    cont("ca", $sformatf("%p", ca), "bBLUErRED");
    cont("as0", $sformatf("%0p", as), "x1y2");
    cont("ai0", $sformatf("%0p", ai), "32101");
    cont("two", $sformatf("two %p and %p", q, u), "two123and789");
    exact("sformatf", $sformatf("%p", q), "'{1,2,3}");
    exact("n[1]", $sformatf("%p", n[1]), "'{3,4}");
    cont("sq[1]", $sformatf("%p", sq[1]), "bc");
    q.push_back(4);
    exact("q+", $sformatf("%p", q), "'{1,2,3,4}");
    if (q.size() != 4) begin $error("size %0d", q.size()); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
