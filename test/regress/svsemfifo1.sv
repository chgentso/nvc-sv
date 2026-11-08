// Expected values: IEEE 1800-2017 15.3 (semaphore: mutual exclusion, blocked
// requests served FIFO), 15.4 (mailbox: FIFO messages and FIFO service of
// blocked get calls), 15.5.3 / 9.4.2 (event trigger wakes all waiters).
// The order in which concurrently forked processes first run is not fixed by
// the LRM, so the checks are stated relative to the order the requests were
// actually issued (tickets), not to worker numbering.
`timescale 1ns/1ps
module svsemfifo1;
  semaphore sem = new(1);
  mailbox #(int) mb = new();
  event ev;
  int errs;
  int holders, maxholders, acquired[3];
  int semq[$], mbq[$];
  int ticket;
  int got_val[3], got_tick[3], blk_tick[3];
  longint ev_t[3] = '{-1, -1, -1};
  task automatic worker(int id);
    repeat (2) begin
      int tk = ticket++;
      semq.push_back(tk);
      sem.get();
      if (semq.pop_front() != tk) begin $error("w%0d: semaphore not served FIFO", id); errs++; end
      holders++; if (holders > maxholders) maxholders = holders;
      acquired[id]++;
      #(2 + id);
      holders--; sem.put(); #1;
    end
  endtask
  task automatic reader(int id);
    int v;
    int tk = ticket++;
    mbq.push_back(tk);
    mb.get(v);
    got_val[id] = v;
    got_tick[id] = tk;
  endtask
  task automatic waiter(int id);
    @ev;
    ev_t[id] = $time;
  endtask
  initial begin
    longint t0, t1;
    fork worker(0); worker(1); worker(2); join
    t0 = $time;
    // Each worker holds the semaphore for 2+id twice: 2*(2+3+4) = 18 ns minimum
    if (t0 < 18) begin $error("workers finished at %0d, earlier than serialised minimum 18", t0); errs++; end
    if (maxholders != 1) begin $error("mutual exclusion violated: %0d holders", maxholders); errs++; end
    for (int i = 0; i < 3; i++) if (acquired[i] != 2) begin $error("worker %0d acquired %0d times", i, acquired[i]); errs++; end
    ticket = 0;
    fork reader(0); reader(1); reader(2); #1 begin mb.put(10); mb.put(11); mb.put(12); end join
    t1 = $time;
    if (t1 != t0 + 1) begin $error("mailbox phase ended at %0d, expected %0d", t1, t0 + 1); errs++; end
    // The k-th reader to block receives the k-th message.
    for (int i = 0; i < 3; i++) if (got_val[i] != 10 + got_tick[i]) begin
      $error("reader %0d (blocked #%0d) got %0d, expected %0d", i, got_tick[i], got_val[i], 10 + got_tick[i]); errs++;
    end
    fork waiter(0); waiter(1); waiter(2); #1 -> ev; join
    for (int i = 0; i < 3; i++) if (ev_t[i] != t1 + 1) begin
      $error("event waiter %0d woke at %0d, expected %0d", i, ev_t[i], t1 + 1); errs++;
    end
    if (errs == 0) $display("PASSED");
  end
endmodule
