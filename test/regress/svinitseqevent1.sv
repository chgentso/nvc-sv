// Expected values: IEEE 1800-2017 15.5.1 (event trigger ->), 15.5.3 (triggered: stays true for the whole time
// step in which the event is triggered, so a wait on it at time 0 is race free), 9.4.2 (@ event control: only a
// waiter already blocked sees a trigger, hence the trigger of the @-waiters is delayed by #1), 23.6 (hierarchical
// event trigger of an interface event), 9.2.1.
`timescale 1ns/1ns
interface c_if;
  event ev, ev2;
  time t_ev = 99, t_ev2 = 99;
  initial begin
    wait (ev.triggered);
    t_ev = $time;
  end
  initial begin
    @ev2;
    t_ev2 = $time;
  end
endinterface

module svinitseqevent1;
  event e1, e2, e3, e4;
  c_if ci();
  time t_a = 99, t_b = 99, t_c = 99, t_d = 99;
  int err = 0;
  initial begin
    wait (e1.triggered);
    t_a = $time;
  end
  initial begin
    wait (e2.triggered);
    t_b = $time;
  end
  initial begin
    @e3;
    t_c = $time;
  end
  initial begin
    @e4;
    t_d = $time;
  end
  initial begin
    -> e1;
    -> e2;
    -> ci.ev;
    #1;
    -> e3;
    -> e4;
    -> ci.ev2;
    #1;
    if (t_a != 0 || t_b != 0 || ci.t_ev != 0) begin
      err++; $error("time-0 triggers seen at %0t %0t %0t", t_a, t_b, ci.t_ev);
    end
    if (t_c != 1 || t_d != 1 || ci.t_ev2 != 1) begin
      err++; $error("time-1 triggers seen at %0t %0t %0t", t_c, t_d, ci.t_ev2);
    end
    if (err == 0) $display("PASSED");
  end
endmodule
