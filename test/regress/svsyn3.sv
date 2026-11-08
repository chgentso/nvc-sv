// Parameterised containers, generic scoreboards, synchronisation primitives,
// constraint solving over arrays and covergroups with transitions
// Expected values: IEEE 1800-2017 18.5 (constraints, unique, array sum), 18.5.8 (foreach
// constraints), 8.21/8.26 (virtual and interface classes), 15.3/15.4 (semaphore, mailbox-like
// FIFO order), 9.3.2 (fork/join, join_any, disable fork), 6.19 (enum name()), 19.5.1
// (transition bins, iff: a sample whose guard is false is not seen; illegal/ignore bins are not
// counted), 19.11 (coverage = covered bins / bins). Random values are not predicted; the
// property that every randomized object satisfies its constraints is checked, and the
// deterministic results are derived by hand.
package gen_pkg;
  virtual class base_item;
    pure virtual function string name();
    virtual function int weight(); return 1; endfunction
  endclass

  class item_a extends base_item;
    rand int unsigned val;
    constraint c { val inside {[10:20]}; }
    virtual function string name(); return "A"; endfunction
    virtual function int weight(); return 2; endfunction
  endclass

  class item_b extends base_item;
    rand bit [7:0] arr [4];
    constraint c_uniq { unique {arr}; foreach (arr[i]) arr[i] inside {[1:6]}; }
    constraint c_sum  { arr.sum() with (int'(item)) == 14; }
    virtual function string name(); return "B"; endfunction
  endclass

  class fifo #(type T = int, int DEPTH = 4);
    T q[$];
    semaphore sem = new(DEPTH);
    event pushed, popped;
    int overflow;
    task put(T v);
      sem.get(1);
      q.push_back(v);
      ->pushed;
    endtask
    task get(output T v);
      wait (q.size() > 0);
      v = q.pop_front();
      sem.put(1);
      ->popped;
    endtask
    function int size(); return q.size(); endfunction
  endclass

  class registry #(type T = base_item);
    static T items[string];
    static int count[string];
    static function void add(T t);
      items[t.name()] = t;
      count[t.name()]++;
    endfunction
    static function int total_weight();
      int w = 0;
      foreach (items[k]) w += items[k].weight() * count[k];
      return w;
    endfunction
  endclass

  typedef enum { IDLE, REQ, WAIT, DONE } st_e;
  class fsm_mon;
    st_e last = IDLE;
    int  trans[string];
    covergroup cg with function sample(st_e s, bit en);
      cp: coverpoint s iff (en) {
        bins fwd = (IDLE => REQ => WAIT => DONE);
        bins loop = (WAIT => WAIT);
        bins back = (DONE => IDLE);
        illegal_bins bad = (IDLE => DONE);
        ignore_bins skip = (REQ => IDLE);
      }
    endgroup
    function new(); cg = new(); endfunction
    function void step(st_e s, bit en = 1);
      trans[$sformatf("%s->%s", last.name(), s.name())]++;
      cg.sample(s, en);
      last = s;
    endfunction
  endclass
endpackage

module svsyn3;
  import gen_pkg::*;
  fifo #(string, 3) sf = new();
  fifo #(int) inf = new();
  registry #(base_item) reg_h;
  item_a ia; item_b ib;
  fsm_mon mon = new();
  int got_n;
  string got_s[$];
  int ok_a, ok_b, errs;
  st_e path[$] = '{REQ, WAIT, WAIT, DONE, IDLE, REQ, WAIT, DONE, IDLE};

  initial begin
    for (int i = 0; i < 30; i++) begin
      ia = new(); ib = new();
      if (!ia.randomize()) begin $error("A failed"); errs++; end
      if (!ib.randomize()) begin $error("B failed"); errs++; end
      if (ia.val inside {[10:20]}) ok_a++;
      begin
        automatic bit uniq = 1; automatic int sum = 0;
        for (int x = 0; x < 4; x++) begin
          sum += ib.arr[x];
          for (int y = x + 1; y < 4; y++) if (ib.arr[x] == ib.arr[y]) uniq = 0;
          if (!(ib.arr[x] inside {[1:6]})) uniq = 0;
        end
        if (uniq && sum == 14) ok_b++;
      end
      registry#(base_item)::add(ia);
      registry#(base_item)::add(ib);
    end
    begin
      automatic int e = 0;
      if (ok_a != 30 || ok_b != 30) begin $error("constraints violated: ok_a=%0d ok_b=%0d", ok_a, ok_b); errs++; end
      // item_a weight 2 and item_b weight 1 (base class default), 30 of each registered
      if (registry#(base_item)::total_weight() != 2 * 30 + 1 * 30) begin
        $error("total_weight %0d", registry#(base_item)::total_weight()); errs++;
      end
      if (registry#(base_item)::items.num() != 2 || !registry#(base_item)::items.exists("A") || !registry#(base_item)::items.exists("B")) begin
        $error("registry keys"); errs++;
      end
    end

    fork
      begin
        foreach (path[i]) mon.step(path[i], i != 3);
      end
      begin
        for (int i = 0; i < 8; i++) begin sf.put($sformatf("msg%0d", i)); end
      end
      begin
        string s;
        repeat (8) begin sf.get(s); got_s.push_back(s); #1; end
      end
      begin
        for (int i = 0; i < 6; i++) inf.put(i * i);
      end
      begin
        int v;
        fork
          begin #50; $error("timeout"); errs++; end
          begin repeat (6) begin inf.get(v); got_n += v; end end
        join_any
        disable fork;
      end
    join
    if (got_n != 0 + 1 + 4 + 9 + 16 + 25) begin $error("got_n %0d", got_n); errs++; end
    if (got_s.size() != 8) begin $error("msgs %0d", got_s.size()); errs++; end
    else begin
      // a FIFO keeps the order of the puts
      foreach (got_s[i]) if (got_s[i] != $sformatf("msg%0d", i)) begin $error("got_s[%0d] = %s", i, got_s[i]); errs++; end
    end
    if (sf.size() != 0) begin $error("sf.size %0d", sf.size()); errs++; end
    // transitions seen by the monitor (last starts at IDLE)
    cmp_t("IDLE->REQ", 2); cmp_t("REQ->WAIT", 2); cmp_t("WAIT->WAIT", 1); cmp_t("WAIT->DONE", 2); cmp_t("DONE->IDLE", 2);
    if (mon.trans.num() != 5) begin $error("%0d distinct transitions", mon.trans.num()); errs++; end
    // samples seen by cp (iff en, the DONE at index 3 is not sampled):
    //   REQ WAIT WAIT IDLE REQ WAIT DONE IDLE  -> fwd (IDLE=>REQ=>WAIT=>DONE), loop (WAIT=>WAIT),
    //   back (DONE=>IDLE) all hit; illegal and ignore bins do not count: 3 of 3 bins
    if ($sformatf("%0.2f", mon.cg.cp.get_inst_coverage()) != "100.00") begin
      $error("fsm cov %0.2f", mon.cg.cp.get_inst_coverage()); errs++;
    end
    if (errs == 0) $display("PASSED");
    $finish;
  end
  task automatic cmp_t(string k, int n);
    if (!mon.trans.exists(k) || mon.trans[k] != n) begin $error("trans %s: expected %0d", k, n); errs++; end
  endtask
endmodule
