// Expected values: IEEE 1800-2017 15.3 (semaphores), 15.3.4 (get), 15.3.3
// (put), 9.3.2 (fork-join_none).  get(n) blocks until n keys are available;
// blocked requests are served first-in first-out.  Whether a *new* get(1)
// may overtake an already-blocked get(2) while one key is free is not fixed
// by the LRM, so scenarios S1 and S6 only check conservation of keys, and
// the order of processes released by a single put is not checked.
module svsem2;
  semaphore s1 = new(1), s2 = new(0), s3 = new(0), s4 = new(0), s5 = new(0);
  semaphore s6 = new(2);
  longint t_s1a = -1, t_s2a = -1, t_s2b = -1, t_s3a = -1, t_s3b = -1, t_s4a = -1, t_s4b = -1,
          t_s5a = -1, t_s5m = -1, t_s6s0 = -1, t_s6d0 = -1, t_s6s1 = -1, t_s6d1 = -1, t_s6all = -1;
  int errs;
  task automatic work(int k);
    #1;
  endtask
  initial begin  // S1: a get(2) can never complete with a single key in total
    #1 fork begin s1.get(2); t_s1a = $time; end join_none
    #1 fork begin s1.get(1); end join_none
  end
  initial begin  // S2: blocked equal requests are served in order
    #11 fork begin s2.get(1); t_s2a = $time; end join_none
    #1 fork begin s2.get(1); t_s2b = $time; end join_none
    #1 s2.put(1);
    #1 s2.put(1);
  end
  initial begin  // S3: blocked requests wait for the oldest (FIFO)
    #21 fork begin s3.get(2); t_s3a = $time; end join_none
    #1 fork begin s3.get(1); t_s3b = $time; end join_none
    #1 s3.put(1);
    #1 s3.put(1);
    #1 s3.put(1);
  end
  initial begin  // S4: one put serves two requests
    #31 fork begin s4.get(1); t_s4a = $time; end join_none
    #1 fork begin s4.get(1); t_s4b = $time; end join_none
    #1 s4.put(2);
  end
  initial begin  // S5: the putter queues behind a blocked request
    #41 fork begin s5.get(1); t_s5a = $time; end join_none
    #1 s5.put(1); s5.get(1); t_s5m = $time;
    #1 s5.put(1);
  end
  initial begin  // S6: workers and a parent compete for two keys
    #51;
    for (int i = 0; i < 2; i++) begin
      work(i);
      fork
        automatic int k = i;
        begin
          s6.get();
          if (k == 0) t_s6s0 = $time; else t_s6s1 = $time;
          #10;
          if (k == 0) t_s6d0 = $time; else t_s6d1 = $time;
          s6.put();
        end
      join_none
    end
    s6.get(2);
    t_s6all = $time;
  end
  task automatic eq(longint got, longint exp, string what);
    if (got !== exp) begin $error("%s: got %0d expected %0d", what, got, exp); errs++; end
  endtask
  initial begin
    #79;
    eq(t_s1a, -1, "S1 get(2) with 1 key in total never completes");
    eq(t_s2a, 13, "S2 A"); eq(t_s2b, 14, "S2 B");
    eq(t_s3a, 24, "S3 A (oldest)"); eq(t_s3b, 25, "S3 B");
    eq(t_s4a, 33, "S4 A"); eq(t_s4b, 33, "S4 B");
    eq(t_s5a, 42, "S5 A (queued first)"); eq(t_s5m, -1, "S5 main stays blocked: its key went to the older request");
    eq(t_s6s0, 52, "S6 worker 0 start"); eq(t_s6d0, 62, "S6 worker 0 done");
    // key conservation: worker 0 holds a key during [52,62]; the parent holds
    // both keys from t_s6all onward, so it cannot start before 62 and any
    // worker 1 interval must end before the parent holds its keys.
    if (t_s6all < 62) begin $error("S6 parent got 2 keys at %0d while a worker held one", t_s6all); errs++; end
    if (t_s6all < 0) begin $error("S6 parent never got 2 keys"); errs++; end
    if (t_s6s1 >= 0 && !(t_s6d1 >= 0 && t_s6d1 <= t_s6all)) begin
      $error("S6 worker 1 (start %0d, done %0d) overlaps parent holding both keys (%0d)", t_s6s1, t_s6d1, t_s6all);
      errs++;
    end
    if (errs == 0) $display("PASSED");
  end
  initial #80 $finish;
endmodule
