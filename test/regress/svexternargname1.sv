// Extern class methods whose formal argument has the same name as its class type;
// handle arrays and queues of class objects.
// Expected values: IEEE 1800-2017 8.24 (out-of-block method declarations; default
// argument values are given in the extern prototype), 8.7, 13.5.3 (default arguments),
// 7.10 (queue of handles), 7.4 (array of handles).
`define CHK(c, m) if (!(c)) begin err++; $error(m); end
int err = 0;
int ping_cnt;
int ping_id[2], ping_n[2];
class cfg;
  int id = 3;
endclass
class r;
  int v;
  function bit set(int x); v = x; return 1; endfunction
  function int get(); return v; endfunction
endclass
class vseq;
  cfg c0;
  extern function new(cfg cfg);
  extern task wait_ping(cfg cfg, int n = 1);
  extern function int f(cfg cfg, int n);
  task run();
    cfg c = new;
    wait_ping(c);
    wait_ping(c, 4);
    `CHK(f(c, 2) == 6, "f(c, 2)")
    `CHK(c0.id == 3, "c0.id")
  endtask
endclass
function vseq::new(cfg cfg);
  c0 = cfg;
endfunction
// the default value of n is given once, in the extern prototype above
task vseq::wait_ping(cfg cfg, int n);
  ping_id[ping_cnt] = cfg.id; ping_n[ping_cnt] = n; ping_cnt++;
endtask
function int vseq::f(cfg cfg, int n);
  return cfg.id * n;
endfunction
module svexternargname1;
  r qs[$];
  r arr[2];
  initial begin
    automatic cfg k = new;
    automatic vseq v = new(k);
    automatic r t0 = new, t1 = new;
    v.run();
    `CHK(ping_cnt == 2, "two pings")
    `CHK(ping_id[0] == 3 && ping_n[0] == 1, "first ping uses the default n")
    `CHK(ping_id[1] == 3 && ping_n[1] == 4, "second ping n = 4")
    qs.push_back(t0); qs.push_back(t1);
    arr[0] = new; arr[1] = new;
    void'(qs[1].set(5));
    void'(arr[1].set(6));
    void'(qs[0].get());
    `CHK(qs[1].get() == 5, "qs[1]")
    `CHK(arr[1].get() == 6, "arr[1]")
    `CHK(qs[0].get() == 0, "qs[0]")
    if (err == 0) $display("PASSED");
  end
endmodule
