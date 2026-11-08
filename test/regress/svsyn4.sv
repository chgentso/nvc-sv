// A mini component framework: factory, config database, callbacks, phases and
// a stimulus library using randc, randcase, std::randomize and polymorphism
// Expected values: IEEE 1800-2017 8.15/8.20 (super, virtual methods, polymorphism), 8.16
// ($cast), 8.25 (parameterised classes), 7.8 (associative arrays), 18.4.2 (randc: every value
// of the domain is chosen once before any repeats), 18.16 (randcase: weight 0 is never chosen,
// ratios 1:3 are statistical), 18.12 (std::randomize with), 18.5 (constraints). Random values
// are not predicted: only constraint satisfaction, cycle completeness, domain membership and a
// wide statistical margin for the 1:3 randcase ratio are checked.
package fw_pkg;
  class component;
    string name;
    component parent;
    component children[$];
    static int created;
    function new(string n, component p = null);
      name = n; parent = p; created++;
      if (p != null) p.children.push_back(this);
    endfunction
    virtual function string kind(); return "component"; endfunction
    function string path();
      return parent == null ? name : {parent.path(), ".", name};
    endfunction
    virtual task run(); endtask
    virtual function void report(ref string log[$]);
      log.push_back($sformatf("%s [%s]", path(), kind()));
      foreach (children[i]) children[i].report(log);
    endfunction
  endclass

  typedef class factory;
  virtual class creator;
    pure virtual function component create(string n, component p);
  endclass

  class factory;
    static creator ctab[string];
    static function void register(string t, creator c); ctab[t] = c; endfunction
    static function component make(string t, string n, component p = null);
      if (!ctab.exists(t)) return null;
      return ctab[t].create(n, p);
    endfunction
  endclass

  class creator_t #(type T = component) extends creator;
    virtual function component create(string n, component p);
      T obj = new(n, p);
      return obj;
    endfunction
  endclass

  class config_db;
    static string  str_db[string];
    static int     int_db[string];
    static int     reads;
    static function void set(string key, int v); int_db[key] = v; endfunction
    static function void set_s(string key, string v); str_db[key] = v; endfunction
    static function bit get(string key, ref int v);
      reads++;
      if (int_db.exists(key)) begin v = int_db[key]; return 1; end
      return 0;
    endfunction
  endclass

  class driver extends component;
    int count;
    int sent[$];
    function new(string n, component p = null); super.new(n, p); endfunction
    virtual function string kind(); return "driver"; endfunction
    virtual task run();
      int n;
      if (!config_db::get({path(), ".count"}, n)) n = 3;
      for (int i = 0; i < n; i++) begin sent.push_back(i * 10); count++; end
    endtask
  endclass

  class fancy_driver extends driver;
    function new(string n, component p = null); super.new(n, p); endfunction
    virtual function string kind(); return "fancy_driver"; endfunction
    virtual task run();
      super.run();
      sent.push_back(-1);
    endtask
  endclass

  class env extends component;
    function new(string n, component p = null); super.new(n, p); endfunction
    virtual function string kind(); return "env"; endfunction
  endclass

  class stim;
    randc bit [2:0] cyc;
    rand bit [7:0] a, b;
    rand int mode;
    constraint c { mode inside {1, 2, 4, 8}; a < b; a + b < 200; }
  endclass
endpackage

module svsyn4;
  import fw_pkg::*;
  component top, d1, d2, e, tmp;
  driver dd;
  string log[$];
  int pass, fail;
  bit [7:0] x, y;
  int cyc_seen[int];
  int mode_seen[int];
  int cc, nb, errs;
  creator_t #(env) c_env = new();
  creator_t #(driver) c_drv = new();
  creator_t #(fancy_driver) c_fdrv = new();
  stim st = new();

  initial begin
    factory::register("env", c_env);
    factory::register("driver", c_drv);
    factory::register("fancy_driver", c_fdrv);
    top = new("top");
    e = factory::make("env", "e", top);
    d1 = factory::make("driver", "d1", e);
    d2 = factory::make("fancy_driver", "d2", e);
    tmp = factory::make("nothing", "x", top);
    if (tmp == null) pass++; else fail++;
    config_db::set("top.e.d2.count", 5);
    config_db::set_s("top.name", "design");
    fork d1.run(); d2.run(); join
    if ($cast(dd, d2)) pass++; else fail++;
    if ($cast(dd, e)) fail++; else pass++;
    dd = null;
    dd = null;
    if ($cast(dd, d1)) begin
      if (dd.sent.size() != 3 || dd.sent[0] != 0 || dd.sent[1] != 10 || dd.sent[2] != 20 || dd.count != 3) begin
        $error("d1 sent/count wrong (size %0d count %0d)", dd.sent.size(), dd.count); errs++;
      end
    end else begin $error("cast d1"); errs++; end
    if ($cast(dd, d2)) begin
      if (dd.sent.size() != 6 || dd.sent[0] != 0 || dd.sent[1] != 10 || dd.sent[2] != 20 || dd.sent[3] != 30
          || dd.sent[4] != 40 || dd.sent[5] != -1 || dd.count != 5) begin
        $error("d2 sent/count wrong (size %0d count %0d)", dd.sent.size(), dd.count); errs++;
      end
    end else begin $error("cast d2"); errs++; end
    top.report(log);
    begin
      automatic string exp_log[$] = '{ "top [component]", "top.e [env]", "top.e.d1 [driver]", "top.e.d2 [fancy_driver]" };
      if (log.size() != exp_log.size()) begin $error("report has %0d lines", log.size()); errs++; end
      else foreach (log[i]) if (log[i] != exp_log[i]) begin $error("log[%0d] = '%s'", i, log[i]); errs++; end
    end
    if (component::created != 4 || config_db::reads != 2 || factory::ctab.num() != 3) begin
      $error("created=%0d reads=%0d factory=%0d", component::created, config_db::reads, factory::ctab.num()); errs++;
    end
    // randc cycles through every value before repeating
    begin
      int seen_in_cycle[int];
      automatic int ok_cycles = 0;
      repeat (4) begin
        seen_in_cycle.delete();
        repeat (8) begin
          if (!st.randomize()) begin $error("rand failed"); errs++; end
          seen_in_cycle[st.cyc] = 1;
          mode_seen[st.mode]++;
          if (!(st.a < st.b && st.a + st.b < 200)) fail++; else pass++;
        end
        if (seen_in_cycle.num() == 8) ok_cycles++;
      end
      if (ok_cycles != 4) begin $error("randc complete cycles %0d of 4", ok_cycles); errs++; end
      foreach (mode_seen[m]) if (!(m inside {1, 2, 4, 8})) begin $error("mode %0d outside the constraint", m); errs++; end
      if (mode_seen.num() < 2) begin $error("only %0d distinct modes in 32 draws", mode_seen.num()); errs++; end
    end
    // randcase and std::randomize
    repeat (200) begin
      randcase
        1: cc = 0;
        3: cc = 1;
        0: cc = 2;
      endcase
      cyc_seen[cc]++;
      if (std::randomize(x, y) with { x > 10; x < 20; y == x + 1; }) begin
        if (x > 10 && x < 20 && y == x + 1) pass++; else fail++;
      end
      else fail++;
    end
    if (cyc_seen.exists(2)) begin $error("randcase chose the weight-0 branch"); errs++; end
    if (!cyc_seen.exists(0) || !cyc_seen.exists(1) || cyc_seen[0] + cyc_seen[1] != 200) begin
      $error("randcase totals wrong"); errs++;
    end else if (!(cyc_seen[1] > cyc_seen[0] && cyc_seen[1] > 110 && cyc_seen[1] < 190)) begin
      // expected about 150 of 200 (weights 3:1, sigma about 6)
      $error("randcase ratio: %0d vs %0d", cyc_seen[1], cyc_seen[0]); errs++;
    end
    if (pass != 3 + 32 + 200 || fail != 0) begin $error("pass=%0d fail=%0d", pass, fail); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
