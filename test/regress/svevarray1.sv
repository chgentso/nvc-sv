// Arrays of events, events in classes, and event handle assignment.
// Expected values: IEEE 1800-2017 15.5.1 (event trigger), 15.5.2 (wait for event),
// 15.5.3 (persistent trigger .triggered), 15.5.4 (event assignment: both names
// then refer to one synchronization object), 7.4 (arrays of events), 8 (classes).
// Waiters woken by the same trigger are not ordered; each wake-up time is recorded
// separately and compared.
`timescale 1ns/1ns
`define CHK(c, m) if (!(c)) begin err++; $error(m); end
int err = 0;
time t_cfgw, t_vs, t_mod[3];
bit seen_mod[3];
class cfgc;
  localparam int N = 3;
  event ev[N];
  task fire(int i); ->ev[i]; endtask
  task wait_ev(int i); @(ev[i]); t_cfgw = $time; endtask
endclass
class vs;
  event pev;
  cfgc cfg;
  task go(int i);
    pev = cfg.ev[i];
    fork
      begin @(pev); t_vs = $time; end
    join_none
    #1 cfg.fire(i);
    #1;
  endtask
endclass
module svevarray1;
  event mev[3];
  vs v = new();
  int count;
  initial begin
    v.cfg = new();
    for (int i = 0; i < 3; i++) begin
      automatic int k = i;
      fork begin @(mev[k]); count++; t_mod[k] = $time; seen_mod[k] = 1; end join_none
    end
    fork v.cfg.wait_ev(1); join_none
    v.go(1);
    ->mev[2];
    #1 ->mev[0];
    #1;
    wait (mev[1].triggered == 0);
    `CHK(t_cfgw == 1, "cfg waiter woke at 1")
    `CHK(t_vs == 1, "vs waiter (aliased event) woke at 1")
    `CHK(seen_mod[2] && t_mod[2] == 2, "mev[2] waiter at 2")
    `CHK(seen_mod[0] && t_mod[0] == 3, "mev[0] waiter at 3")
    `CHK(!seen_mod[1], "mev[1] was never triggered")
    `CHK(count == 2, "count")
    if (err == 0) $display("PASSED");
    $finish;
  end
endmodule
