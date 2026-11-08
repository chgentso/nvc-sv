// Self-checking: static properties and class scope resolution (::), static initialisers.
// Expected values: IEEE 1800-2017 8.9 (static properties are shared by all instances and initialised before
// time 0 procedural code), 8.23 (class scope resolution), 7.10 / 7.4 (static queue and array members),
// 21.2.1.7 (%p: only the content is checked)
`define CHK(g, e) if ((g) !== (e)) begin errs++; $error("check failed at line %0d", `__LINE__); end
module svlrm_c8i;
  int errs;
  class Cnt;
    static int total = 100;
    static int next_id = 1;
    int id;
    function new(); id = next_id++; total += id; endfunction
    static function int peek(); return next_id; endfunction
  endclass
  class Cfg;
    static string name = "dflt";
    static int vals[3] = '{1, 2, 3};
    static int q[$] = '{9, 8};
    static function void set(string n); name = n; endfunction
  endclass
  Cnt c1, c2, c3;
  initial begin
    string str;
    `CHK(Cnt::total, 100) `CHK(Cnt::peek(), 1)
    c1 = new; c2 = new; c3 = new;
    `CHK(c1.id, 1) `CHK(c2.id, 2) `CHK(c3.id, 3)
    `CHK(Cnt::total, 106) `CHK(Cnt::next_id, 4)
    `CHK(Cfg::name, "dflt") Cfg::set("x"); `CHK(Cfg::name, "x")
    Cfg::vals[1] = 20; `CHK(Cfg::vals[1], 20) `CHK(Cfg::vals[2], 3) `CHK(Cfg::vals[0], 1)
    `CHK(Cfg::q.size(), 2) Cfg::q.push_back(7);
    `CHK(Cfg::q.size(), 3) `CHK(Cfg::q[0], 9) `CHK(Cfg::q[1], 8) `CHK(Cfg::q[2], 7)
    str = $sformatf("%p", Cfg::q);
    `CHK(str.len() > 0, 1)
    if (errs == 0) $display("PASSED");
  end
endmodule
