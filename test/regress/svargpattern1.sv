// Self-checking. Expected values: IEEE 1800-2017 7.10.2 (queue push/insert), 10.9.2 (structure assignment
// patterns, positional/named/default), 13.5 (pass by value of a struct argument), 8.5 (class methods),
// 6.11/5.8 (time literals in the module time unit: 40ns is 40 in a 1ns unit).
`timescale 1ns/1ps
`define CHK(tag, got, exp) if ((got) !== (exp)) begin errs++; $error("%s: got %0d expected %0d", tag, got, exp); end
module svargpattern1;
  int errs = 0;
  typedef struct { int channel; time ts; } ent_t;
  typedef struct { int a; int b; } p_t;
  ent_t q[$];
  p_t arr[$], dyn[];
  p_t r;
  int ta, tb, tx, kput;
  task automatic show(p_t p, int extra);
    ta = p.a; tb = p.b; tx = extra;
  endtask
  function automatic int sum(p_t p); return p.a + p.b; endfunction
  class K;
    function void put(p_t p); kput = p.a * p.b; endfunction
  endclass
  initial begin
    automatic K k = new;
    q.push_back('{channel: 3, ts: 40ns});
    q.push_front('{channel: 1, ts: 5ns});
    q.insert(1, '{channel: 2, ts: 7ns});
    `CHK("q.size", q.size(), 3)
    `CHK("q[0].channel", q[0].channel, 1)
    `CHK("q[1].channel", q[1].channel, 2)
    `CHK("q[2].channel", q[2].channel, 3)
    `CHK("q[0].ts", q[0].ts, 5)
    `CHK("q[1].ts", q[1].ts, 7)
    `CHK("q[2].ts", q[2].ts, 40)
    arr.push_back('{1, 2});
    arr.push_back('{default: 9});
    dyn = new[1];
    `CHK("arr[0].a", arr[0].a, 1)
    `CHK("arr[0].b", arr[0].b, 2)
    `CHK("arr[1].a", arr[1].a, 9)
    `CHK("arr[1].b", arr[1].b, 9)
    `CHK("arr.size", arr.size(), 2)
    `CHK("dyn.size", dyn.size(), 1)
    show('{a: 5, b: 6}, 7);
    `CHK("task a", ta, 5) `CHK("task b", tb, 6) `CHK("task extra", tx, 7)
    `CHK("sum", sum('{a: 5, b: 6}), 11)
    k.put('{3, 4});
    `CHK("put", kput, 12)
    r = '{a: 1, b: 2};
    `CHK("r.a", r.a, 1)
    `CHK("r.b", r.b, 2)
    if (errs == 0) $display("PASSED");
  end
endmodule
