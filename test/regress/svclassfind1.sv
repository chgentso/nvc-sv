// Expected values: IEEE 1800-2017 8.5 (class methods; a user method may be named like a
// built-in array method), 7.10 (queue size and indexing), 6.16 (string equality),
// 8.18 (protected members accessed inside the class).
class coll;
  string c[$];
  function int find(string id);
    for (int i = 0; i < c.size(); i++) if (c[i] == id) return i;
    return -1;
  endfunction
endclass
class drv;
  protected coll rules;
  int last_idx, last_d;
  function new(); rules = new; rules.c.push_back("a"); endfunction
  task set_rule(string id, int d);
    int idx;
    idx = this.rules.find(id);
    last_idx = idx; last_d = d;
  endtask
endclass
module svclassfind1;
  drv d = new;
  int errors = 0;
  initial begin
    d.set_rule("a", 3);
    if (d.last_idx !== 0 || d.last_d !== 3) begin $error("rule a: idx=%0d d=%0d, expected 0 3", d.last_idx, d.last_d); errors++; end
    d.set_rule("b", 4);
    if (d.last_idx !== -1 || d.last_d !== 4) begin $error("rule b: idx=%0d d=%0d, expected -1 4", d.last_idx, d.last_d); errors++; end
    if (errors == 0) $display("PASSED");
  end
endmodule
