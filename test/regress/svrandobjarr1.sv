// Rand arrays (fixed, queue, dynamic) and a single handle of objects with foreach constraints.
// Expected values: IEEE 1800-2017 18.4 (rand object handles), 18.5.3 (foreach), 18.5.1 (inside),
// 18.6.3 (randomize returns 1), 18.5 constraint semantics for every solution; spread check is statistical
class item;
  rand bit [7:0] v;
  rand bit [3:0] w;
  constraint c { v inside {[10:40]}; }
endclass
class holder;
  rand item arr[4];
  rand item q[$];
  rand item dyn[];
  rand item single;
  rand bit [7:0] lim;
  constraint cs { single.v == 15; lim inside {[30:35]}; }
  constraint ca { foreach (arr[i]) arr[i].v > 15; }
  constraint co { foreach (arr[i]) if (i > 0) arr[i].v > arr[i-1].v; }
  constraint cq { foreach (q[i]) q[i].w == i + 1; }
  constraint cd { foreach (dyn[j]) dyn[j].v < lim; }
  function new();
    item t;
    foreach (arr[i]) arr[i] = new;
    single = new;
    t = new; q.push_back(t); t = new; q.push_back(t);
    dyn = new[2]; dyn[0] = new; dyn[1] = new;
  endfunction
endclass
module svrandobjarr1;
  initial begin
    automatic holder h = new;
    automatic int ok = 1, runs = 0;
    int distinct[int];
    repeat (60) begin
      if (!h.randomize()) ok = 0;
      runs++;
      foreach (h.arr[i]) begin
        if (!(h.arr[i].v inside {[16:40]})) ok = 0;
        if (i > 0 && h.arr[i].v <= h.arr[i-1].v) ok = 0;
        distinct[h.arr[i].v] = 1;
      end
      foreach (h.q[i]) if (h.q[i].w != i + 1 || !(h.q[i].v inside {[10:40]})) ok = 0;
      foreach (h.dyn[i]) if (h.dyn[i].v >= h.lim || h.dyn[i].v < 10) ok = 0;
      if (h.single.v != 15) ok = 0;
    end
    if (ok != 1) $error("a randomization failed or violated a constraint");
    else if (runs != 60) $error("runs=%0d", runs);
    else if (distinct.size() <= 6) $error("only %0d distinct values over 60 runs", distinct.size());
    else $display("PASSED");
  end
endmodule
