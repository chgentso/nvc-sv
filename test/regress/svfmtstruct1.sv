// %p of packed and unpacked structs, nested structs and a struct held in a class object
// Expected values: IEEE 1800-2017 21.2.1.7 (%p prints the value as an assignment pattern: member names and
// values), 7.2 (structures), 8.5 (class members). The exact layout (spacing, radix of packed members, quoting) is
// not specified by 21.2.1.7, so each result is checked for the member names and values it must contain
// after removing blanks; text around %p is checked exactly.
module svfmtstruct1;
  typedef struct packed { bit [3:0] a; bit [1:0] b; } ps_t;
  typedef enum {X, Y} e_t;
  typedef struct { bit [3:0] a; int b; string s; e_t e; ps_t p; real r; int arr[3]; } us_t;
  typedef struct { us_t u; ps_t q; bit [7:0] v; } nest_t;
  ps_t p; us_t u; nest_t n;
  class c; us_t m; endclass
  int errs = 0;
  function automatic string nows(string x);
    string o = "";
    foreach (x[k]) if (x[k] != " ") o = {o, x[k]};
    return o;
  endfunction
  function automatic bit has(string x, string sub);
    for (int k = 0; k + sub.len() <= x.len(); k++)
      if (x.substr(k, k + sub.len() - 1) == sub) return 1;
    return 0;
  endfunction
  task automatic need(string got, string sub, string m);
    if (!has(got, sub)) begin $error("%s: '%s' lacks '%s'", m, got, sub); errs++; end
  endtask
  task automatic needu(string got, string m);
    need(got, "a:5", m); need(got, "b:-2", m); need(got, "s:\"hi\"", m); need(got, "e:Y", m);
    need(got, "p:6", m); need(got, "r:1.5", m);
    need(got, "arr:", m); need(got, "4,5,6", m);
  endtask
  initial begin
    string t, g;
    automatic c o = new;
    p = '{a:3, b:1}; u = '{a:5, b:-2, s:"hi", e:Y, p:'{a:1,b:2}, r:1.5, arr:'{4,5,6}};
    n.u = u; n.q = p; n.v = 8'hab; o.m = u;
    $display("%p", p);
    $display("%p", u);
    $display("x %p y %p z", p, u);
    $display("%p", n);
    $display("%p and %0d %s", o.m, 7, "str");
    $display("%s", $sformatf("<%p>", u));
    // packed struct: an assignment pattern with its members, or its integer value 13
    g = nows($sformatf("%p", p));
    if (!(has(g, "a:3") && has(g, "b:1")) && g != "13") begin $error("%%p packed struct: '%s'", g); errs++; end
    g = nows($sformatf("%p", u));
    needu(g, "%p unpacked struct");
    g = nows($sformatf("x %p y %p z", p, u));
    if (g[0] != "x" || g[g.len()-1] != "z" || !has(g, "y") || g[g.len()-2] != "}" && g[g.len()-2] != "3") begin $error("%%p between text: '%s'", g); errs++; end
    needu(g, "x %p y %p z");
    g = nows($sformatf("%p", n));
    needu(g, "%p nested"); need(g, "u:", "%p nested"); need(g, "q:", "%p nested"); need(g, "v:", "%p nested");
    if (!has(g, "v:171") && !has(g, "v:'hab") && !has(g, "v:8'hab") && !has(g, "v:ab")) begin $error("%%p nested v: '%s'", g); errs++; end
    t = $sformatf("%p and %0d %s", o.m, 7, "str");
    if (t.len() < 10 || t.substr(t.len()-10, t.len()-1) != " and 7 str") begin $error("%%p class member tail: '%s'", t); errs++; end
    needu(nows(t), "%p class member struct");
    t = $sformatf("<%p>", u);
    if (t[0] != "<" || t[t.len()-1] != ">") begin $error("<%%p>: '%s'", t); errs++; end
    needu(nows(t), "<%p>");
    if (errs == 0) $display("PASSED");
  end
endmodule
