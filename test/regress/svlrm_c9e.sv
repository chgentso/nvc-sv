// Self-checking: process execution, blocking vs nonblocking assignment ordering, delays in nested automatic
// tasks, events and event.triggered.
// Expected values: IEEE 1800-2017 4.4/4.5 (#0 resumes in the Inactive region, before the NBA region, so
// NBA values are not yet visible; they are visible one time step later), 9.3.2 (fork-join), 10.4.2 (several NBAs
// to one variable in a time step: the last one wins), 13.4 (automatic tasks with delays), 15.5 (named events),
// 15.5.3 (e.triggered is true only in the time step in which e was triggered), 9.4.3 (wait on a level that never
// becomes true blocks forever).
// Order-independence (4.7 leaves the order of same-time processes unspecified): the time-0 write a = 1 may or may not be seen by the
// "always @(a)" observer, so only the last value seen per time step is checked. Times in ns (explicit timescale).
`timescale 1ns/1ns
`define CHK(g, e) if ((g) !== (e)) begin errs++; $error("check failed at line %0d", `__LINE__); end
module svlrm_c9e;
  int a, b, order; int errs;
  int tt[int];                         // t1(n) completion times
  int a0, b0, a1, b1, t_t2t1, a22;
  int log_t[$], log_v[$];              // observer log of a
  int te1, te2, trig35, trig35b, trig36; bit wait_done;
  task automatic t1(int n); #n tt[n] = $time; endtask
  task automatic t2(); t1(2); t1(1); endtask
  function automatic int f(int x); return x * 2; endfunction
  initial begin
    a = 1; b = 2;
    a <= b; b <= a;
    #0 a0 = a; b0 = b;
    #1 a1 = a; b1 = b;
  end
  initial begin
    fork t2(); t1(10); join
    t_t2t1 = $time;
  end
  always @(a) begin log_t.push_back($time); log_v.push_back(a); end
  initial begin
    #20;
    a = f(a); a = f(a);
    #1 a <= f(a); a <= f(a);
    #1 a22 = a;
  end
  event e1, e2;
  initial begin
    #30;
    fork
      begin @e1 te1 = $time; -> e2; end
      begin #1 -> e1; end
      begin @e2 te2 = $time; end
    join
    #1;
  end
  function automatic int last_at(int t);
    int r = -1000;
    foreach (log_t[i]) if (log_t[i] == t) r = log_v[i];
    return r;
  endfunction
  initial begin
    #35; trig35 = e1.triggered;
    -> e1;
    trig35b = e1.triggered;
    #1 trig36 = e1.triggered;
    wait (e1.triggered);
    wait_done = 1;
  end
  initial begin
    #40;
    `CHK(a0, 1) `CHK(b0, 2)            // NBAs not applied at #0
    `CHK(a1, 2) `CHK(b1, 1)
    `CHK(last_at(0), 2)                // NBA update seen in time step 0
    `CHK(tt[2], 2) `CHK(tt[1], 3) `CHK(tt[10], 10) `CHK(t_t2t1, 10)
    `CHK(last_at(20), 8) `CHK(last_at(21), 16) `CHK(a22, 16)
    `CHK(te1, 31) `CHK(te2, 31)
    `CHK(trig35, 0) `CHK(trig35b, 1) `CHK(trig36, 0)
    `CHK(wait_done, 0)
    if (errs == 0) $display("PASSED");
    $finish;
  end
endmodule
