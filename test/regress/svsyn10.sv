// Expected values: IEEE 1800-2017 15.3 (semaphore: mutual exclusion), 15.4
// (bounded mailbox, put blocks when full, get/try_peek/num), 9.3.2-9.6.3
// (fork join_none / join_any / wait fork / disable fork), 15.5.3 (event
// triggered state lasts for the whole time step), 8 (class handles).
// Which consumer takes which transaction is not fixed by the LRM, so only
// order-independent properties are checked: every transaction served exactly
// once, bus held by one consumer at a time, mailbox never over its bound,
// each age >= producer gap + consumer cost, collector sum, serialised
// minimum completion time, wait-fork semantics.
// Producer / consumer with mailboxes, semaphores, events and fork join_any
class txn;
  int id; int data; time born;
  function new(int i, int d); id = i; data = d; born = $time; endfunction
  function string str(); return $sformatf("txn#%0d(%0d)", id, data); endfunction
endclass
module svsyn10;
  mailbox #(txn) req_mb = new(3);
  mailbox #(int) done_mb = new;
  semaphore bus = new(1);
  semaphore slots = new(2);
  event all_done, kick;
  int served[$];
  int log_n;
  txn dummy;
  int errs, bus_users, max_mb, seen_cnt[int];
  time t_collect;
  task automatic fail(string m); $error("%s", m); errs++; endtask

  task automatic producer(int base, int n, int gap);
    for (int i = 0; i < n; i++) begin
      txn t = new(base + i, (base + i) * 3);
      #(gap);
      req_mb.put(t);
      if (req_mb.num() > max_mb) max_mb = req_mb.num();
    end
  endtask

  task automatic consumer(string who, int cost);
    txn t;
    forever begin
      req_mb.get(t);
      slots.get();
      bus.get();
      bus_users++; if (bus_users != 1) fail("bus used by more than one consumer");
      #(cost);
      bus_users--;
      if ($time - t.born < cost + ((t.id < 20) ? 3 : 5)) fail($sformatf("%s age %0t too small for %s", who, $time - t.born, t.str()));
      seen_cnt[t.id] = (seen_cnt.exists(t.id) ? seen_cnt[t.id] : 0) + 1;
      served.push_back(t.id);
      bus.put();
      slots.put();
      done_mb.put(t.id);
    end
  endtask

  task automatic collector(int expect_n);
    int got, id, sum;
    while (got < expect_n) begin
      done_mb.get(id);
      sum += id; got++;
    end
    t_collect = $time;
    if (got != 7 || sum != 109) fail($sformatf("collector got %0d sum=%0d, expected 7 / 109", got, sum));
    -> all_done;
  endtask

  initial begin : main
    fork
      producer(10, 4, 3);
      producer(20, 3, 5);
      begin
        fork
          consumer("c1", 4);
          consumer("c2", 6);
        join_none
      end
    join_none
    fork
      collector(7);
      begin #200; fail("timeout"); end
    join_any
    disable fork;
    if ($time != t_collect) fail("join_any did not return at collector completion");
    if (t_collect >= 200 || t_collect < 31) fail($sformatf("collector finished at %0d, expected 31..199", t_collect));
    if (!all_done.triggered) fail("all_done not triggered in the time step it was triggered");
    if (served.size() != 7) fail($sformatf("served %0d transactions, expected 7", served.size()));
    foreach (seen_cnt[i]) if (seen_cnt[i] != 1) fail($sformatf("txn %0d served %0d times", i, seen_cnt[i]));
    if (seen_cnt.num() != 7) fail("wrong number of distinct transactions");
    if (max_mb > 3) fail($sformatf("mailbox held %0d messages, bound is 3", max_mb));
    if (req_mb.try_peek(dummy) != 0 || req_mb.num() != 0) fail("mailbox should be empty (try_peek returns 0)");
    begin
      automatic int ok = bus.try_get();
      automatic int ok2 = bus.try_get();
      if (ok != 1 || ok2 != 0) fail($sformatf("semaphore try_get %0d %0d, expected 1 0", ok, ok2));
      bus.put();
    end
    wait_fork_test();
    if (errs == 0) $display("PASSED");
    $finish;
  end

  task automatic wait_fork_test();
    int cnt;
    time t0;
    fork
      begin #5; cnt++; end
      begin #10; cnt += 10; end
    join_none
    #7;
    t0 = $time;
    if (cnt != 1) fail($sformatf("cnt=%0d before wait fork, expected 1", cnt));
    wait fork;
    if (cnt != 11 || $time - t0 != 3) fail($sformatf("after wait fork cnt=%0d (exp 11) after %0d (exp 3)", cnt, $time - t0));
  endtask
endmodule
