// Expected values: IEEE 1800-2017 15.3 (semaphore: get blocks until keys are available; the bus semaphore with one
// key gives mutual exclusion), 15.4 (mailbox: bounded mailbox, get blocks until a message is put; messages are
// delivered in FIFO order), 9.4.1 (delay control).
// Which consumer receives which message, and the interleaving of same-time events, are not fixed by the LRM, so only
// order-independent properties are checked: the times messages were taken, mutual exclusion on the bus, service time
// equal to the consumer's cost, and that every message is served exactly once.
`timescale 1ns/1ns
module svsem1;
  mailbox #(int) mb = new(3);
  semaphore bus = new(1);
  semaphore slots = new(2);
  int tget[1:3], tbus[1:3], tend[1:3], cost_of[1:3], seen[1:3];
  int err = 0;
  function automatic void chk(bit ok, string m);
    if (!ok) begin $error("%s", m); err++; end
  endfunction
  task automatic consumer(string who, int cost);
    int t;
    forever begin
      mb.get(t);
      tget[t] = $time;
      slots.get();
      bus.get();
      tbus[t] = $time;
      cost_of[t] = cost;
      #(cost);
      tend[t] = $time;
      seen[t]++;
      bus.put();
      slots.put();
    end
  endtask
  initial begin
    fork
      begin fork consumer("c1", 4); consumer("c2", 6); join_none end
    join_none
    #3 mb.put(1); #2 mb.put(2); #1 mb.put(3);
    #49;
    for (int i = 1; i <= 3; i++) begin
      chk(seen[i] == 1, "message served exactly once");
      chk(tbus[i] >= tget[i], "bus taken after message");
      chk(tend[i] - tbus[i] == cost_of[i], "service time equals cost");
      chk(cost_of[i] == 4 || cost_of[i] == 6, "cost");
    end
    chk(tget[1] == 3 && tbus[1] == 3, "first message taken at once at t=3");
    chk(tget[2] == 5, "second message taken at t=5 by the idle consumer");
    chk(tbus[2] == tend[1], "second waits for the bus until the first is served");
    chk(tget[3] >= 6, "third message not taken before it is put");
    for (int i = 1; i <= 3; i++)
      for (int j = i + 1; j <= 3; j++)
        chk(tend[i] <= tbus[j] || tend[j] <= tbus[i], "bus intervals overlap");
    if (err == 0) $display("PASSED");
    $finish;
  end
endmodule
