// Expected values: IEEE 1800-2017 20.6.1 ($typename), 6.18 (typedef), 6.19
// (enum), 7.2-7.3 (struct/union), 7.4-7.9 (arrays, queues, associative arrays),
// 8 (classes), 6.11 / 6.12 (basic types), 15.4 (mailbox).
// Checked: keyword types print as their keyword; the string of an object equals
// the string of its type; a typedef alias of a user type prints like the
// original; a user-defined enum/struct/union/class type prints with its
// (scope-qualified) name, so the identifier occurs in the string and different
// types print differently; array types mention their element type and their
// dimension forms ([lo:hi], [], [$], [index type]).  Scope qualification
// syntax, exact spacing and the "$" separator in front of unpacked dimensions
// are implementation-defined and not compared literally; the strings of
// realtime, reg, wire, mailbox and semaphore objects are not checked either, and
// the sign suffix of "time" is not compared.
package pk;
  typedef enum {A, B} pe_t;
  typedef struct packed {logic [3:0] a; bit b;} ps_t;
  class PC; endclass
endpackage
typedef enum {UA, UB} ue_t;
typedef logic [3:0] nib_t;
module svtypename2;
  typedef enum {RED, GREEN} color_t;
  typedef enum logic [1:0] {X0, X1} e2_t;
  typedef struct { int a; byte b; } st_t;
  typedef struct packed { logic [3:0] a; bit b; } pst_t;
  typedef union { int a; byte b[4]; } un_t;
  typedef union packed { int a; bit [31:0] b; } pun_t;
  typedef union tagged { int a; void n; } tg_t;
  typedef color_t alias_t;
  class C; color_t e; endclass
  class B; endclass
  color_t c; e2_t e2; st_t st; pst_t pst; un_t un; pun_t pun; tg_t tg; C o; B bb;
  pk::pe_t pe; pk::ps_t ps; pk::PC pc; ue_t ue; nib_t nib; alias_t al;
  int aa [string]; int aab [byte]; int aai [int]; int aac [C]; int aa_star [*];
  color_t ea [string]; color_t eq [$];
  int q[$]; int da[]; int fa[4]; bit [7:0][3:0] md; logic [3:0] mu[2][3];
  string s; real r; shortreal sr; time t; realtime rt; integer i; byte by; shortint si;
  bit b; logic l; reg rg; wire w; wire [3:0] wv; logic [7:0] lv; bit signed [7:0] sb;
  event ev; chandle ch; mailbox mb; semaphore sm; mailbox #(int) mbi;
  int errs;
  function automatic bit has(string str, string sub);
    for (int i = 0; i + sub.len() <= str.len(); i++) if (str.substr(i, i + sub.len() - 1) == sub) return 1;
    return 0;
  endfunction
  task automatic seq_(string got, string exp, string what);
    if (got != exp) begin $error("%s: got \"%s\", expected \"%s\"", what, got, exp); errs++; end
  endtask
  task automatic hs(string got, string sub, string what);
    if (!has(got, sub)) begin $error("%s: \"%s\" does not contain \"%s\"", what, got, sub); errs++; end
  endtask
  initial begin
    string names[string];
    string tc, te2, tst, tpst, tun, tpun, ttg, to, tbb, tpe, tps, tpc, tue, tnib, tal;
    o = new;
    // basic keyword types
    seq_($typename(s), "string", "string"); seq_($typename(r), "real", "real"); seq_($typename(sr), "shortreal", "shortreal");
    hs($typename(t), "time", "time"); seq_($typename(i), "integer", "integer"); seq_($typename(by), "byte", "byte");
    seq_($typename(si), "shortint", "shortint"); seq_($typename(b), "bit", "bit");
    seq_($typename(ev), "event", "event"); seq_($typename(ch), "chandle", "chandle");
    tnib = $typename(nib); seq_(tnib, $typename(logic [3:0]), "typedef of logic [3:0]");
    seq_($typename(l), $typename(logic), "logic object");
    seq_($typename(l), "logic", "logic object keyword");
    seq_($typename(lv), $typename(logic [7:0]), "logic [7:0] object");
    seq_($typename(sb), $typename(bit signed [7:0]), "bit signed [7:0] object");
    // user-defined types: identifier in the string, object == type
    tc = $typename(c); te2 = $typename(e2); tst = $typename(st); tpst = $typename(pst); tun = $typename(un);
    tpun = $typename(pun); ttg = $typename(tg); to = $typename(o); tbb = $typename(bb); tpe = $typename(pe);
    tps = $typename(ps); tpc = $typename(pc); tue = $typename(ue); tal = $typename(al);
    hs(tc, "color_t", "c"); hs(te2, "e2_t", "e2"); hs(tst, "st_t", "st"); hs(tpst, "pst_t", "pst");
    hs(tun, "un_t", "un"); hs(tpun, "pun_t", "pun"); hs(ttg, "tg_t", "tg"); hs(to, "C", "o"); hs(tbb, "B", "bb");
    hs(tpe, "pe_t", "pe"); hs(tps, "ps_t", "ps"); hs(tpc, "PC", "pc"); hs(tue, "ue_t", "ue");
    seq_(tc, $typename(color_t), "color_t object vs type"); seq_(to, $typename(C), "class C object vs type");
    seq_(tpc, $typename(pk::PC), "class pk::PC object vs type");
    seq_(tal, tc, "alias_t prints like color_t");
    seq_($typename(o.e), tc, "class member of type color_t");
    // different types print differently
    names["c"] = tc; names["e2"] = te2; names["st"] = tst; names["pst"] = tpst; names["un"] = tun; names["pun"] = tpun;
    names["tg"] = ttg; names["o"] = to; names["bb"] = tbb; names["pe"] = tpe; names["ps"] = tps; names["pc"] = tpc; names["ue"] = tue;
    foreach (names[a]) foreach (names[bn]) if (a < bn && names[a] == names[bn]) begin $error("%s and %s print alike: %s", a, bn, names[a]); errs++; end
    // arrays
    hs($typename(aa), "int", "aa"); hs($typename(aa), "[string]", "aa");
    hs($typename(aab), "int", "aab"); hs($typename(aab), "[byte]", "aab");
    hs($typename(aai), "int", "aai"); hs($typename(aai), "[int]", "aai");
    hs($typename(aac), "int", "aac"); hs($typename(aac), "[", "aac"); hs($typename(aac), "C", "aac");
    hs($typename(aa_star), "[*]", "aa_star");
    hs($typename(ea), "color_t", "ea"); hs($typename(ea), "[string]", "ea");
    hs($typename(eq), "color_t", "eq"); hs($typename(eq), "[$]", "eq");
    hs($typename(q), "int", "q"); hs($typename(q), "[$]", "q");
    hs($typename(da), "int", "da"); hs($typename(da), "[]", "da");
    hs($typename(fa), "int", "fa"); hs($typename(fa), "[0:3]", "fa");
    hs($typename(md), "bit", "md"); hs($typename(md), "7:0", "md"); hs($typename(md), "3:0", "md");
    hs($typename(mu), "3:0", "mu"); hs($typename(mu), "[0:1]", "mu"); hs($typename(mu), "[0:2]", "mu");
    begin string tq, td; tq = $typename(q); td = $typename(da);
      if (tq == td) begin $error("queue and dynamic array print alike: %s", tq); errs++; end end
    if (errs == 0) $display("PASSED");
  end
endmodule
