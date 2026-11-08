// Self-checking: event controls (or / comma / iff), intra-assignment delays and event controls, #0, repeat events.
// Expected values: IEEE 1800-2017 9.4.1 (delay control), 9.4.2 (event control, edge and iff), 9.4.5
// (intra-assignment timing controls: a blocking "r = #5 e" evaluates e first and assigns after the delay; the
// nonblocking form schedules the pre-evaluated value, 10.4.2), 9.4.2.3 (x->0 is a change/negedge, not a posedge),
// 4.7 (#0 resumes later in the same time step). Times in ns (timescale given explicitly).
// Not checked: how many times "always @(a or b or c)" wakes for several changes made by one process within a
// time step (only bounds are checked), and the value of the final 'repeat' assignment.
`timescale 1ns/1ns
`define CHK(g, e) if ((g) !== (e)) begin errs++; $error("check failed at line %0d", `__LINE__); end
module svlrm_c9c;
  logic a, b, c;
  int cnt; int errs;
  int t_ab, t_abc, t_pc, t_iff, t_piff, t_z, cnt10;
  int r1, t_r1, r2, t_r2, r3, t_r3, t_end;
  initial begin
    #1 a = 0; b = 0; c = 0;
    #1 a = 1; #1 b = 1; #1 c = 1; #1 a = 0;
  end
  initial begin
    @(a or b); t_ab = $time;
    @(a, b, c); t_abc = $time;
    @(posedge c); t_pc = $time;
  end
  always @(a or b or c) cnt++;
  initial begin
    #10 cnt10 = cnt;
    repeat (3) @(posedge a);
  end
  logic [3:0] v; int r;
  initial begin
    v = 0; r = 0;
    r = #5 v + 1; r1 = r; t_r1 = $time;
    v = 4; r <= #2 v * 2; #3 r2 = r; t_r2 = $time;
    v = 3; r = @(a) v + 1; r3 = r; t_r3 = $time;
    r = repeat (2) @(posedge a) 99;
  end
  initial begin
    #20;
    fork begin #1 a = 1; end begin #2 a = 0; end join
    #1 a = 1;
    #1 t_end = $time;
  end
  initial begin
    @(a iff b) t_iff = $time;
    @(posedge a iff c) t_piff = $time;
    #0 t_z = $time;
  end
  initial begin
    #30;
    `CHK(t_ab, 1)
    `CHK(t_abc == 1 || t_abc == 2, 1)     // b and c change in the same step as a at t=1: see header
    `CHK(t_pc, 4)
    `CHK(r1, 1) `CHK(t_r1, 5)
    `CHK(r2, 8) `CHK(t_r2, 8)             // nonblocking value 4*2 evaluated at t=5, assigned at t=7
    `CHK(r3, 4) `CHK(t_r3, 21)            // v+1 evaluated before the wait, released by a changing at t=21
    `CHK(t_iff, 5)                        // a falls at t=5 while b is 1
    `CHK(t_piff, 21)                      // a rises at t=21 while c is 1
    `CHK(t_z, 21)
    `CHK(t_end, 24)
    `CHK(cnt10 >= 5 && cnt10 <= 7, 1)     // 5 time steps with changes, 7 individual changes
    if (errs == 0) $display("PASSED");
  end
endmodule
