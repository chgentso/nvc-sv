`timescale 1ns/1ns
module svlrm_c15a;
  // Self-checking test. Expected values: IEEE 1800-2017 15.3 (semaphores: new(keys), get blocks until
  // enough keys, try_get returns 0/1 without blocking, put adds keys), 15.4 (mailboxes: unbounded and
  // bounded, put blocks / try_put returns 0 when full, get/peek/try_get/try_peek, FIFO order, num),
  // 15.5 (events: -> wakes waiters in the same time step, @e), 15.5.3 (e.triggered stays true for the
  // whole time step in which e was triggered), 9.3.2 (join waits for all branches).
  // Time is 1ns units; all delays are explicit.
  semaphore sem = new(2);
  mailbox #(int) mb = new();
  mailbox mbu = new(2);
  mailbox #(string) smb = new(1);
  event e;
  int v;
  int errs;
  time tg1, tg2, tg3, tpd, tget, te, twt, tdone;
  int r_try1, r_try2, r_try3, r_try4;
  function automatic void ck(bit ok, string m);
    if (!ok) begin errs++; $error("%s", m); end
  endfunction
  initial begin
    sem.get(1); tg1 = $time;
    sem.get(1); tg2 = $time;
    r_try1 = sem.try_get(1);
    fork begin #5 sem.put(1); end join_none
    sem.get(1); tg3 = $time;
    sem.put(2); r_try2 = sem.try_get(2);
    sem.put(3);
    r_try3 = sem.try_get(3);
    r_try4 = sem.try_get(1);
  end
  initial begin
    int g1, g2, n, pk, pkv, tg, tgv, tg_empty, tp, pkv2;
    #10;
    fork
      begin mb.put(1); mb.put(2); mb.put(3); tpd = $time; end
      begin #3; mb.get(v); tget = $time; g1 = v; mb.get(v); g2 = v; end
    join
    n = mb.num();
    pk = mb.try_peek(v); pkv = v;
    tg = mb.try_get(v); tgv = v;
    tg_empty = mb.try_get(v);
    tp = mb.try_put(9);
    mb.peek(v); pkv2 = v;
    ck(g1 == 1 && g2 == 2, "FIFO order of get");
    ck(tpd == 10 && tget == 13, "unbounded put does not block; get after #3");
    ck(n == 1, "num after two gets of three puts");
    ck(pk == 1 && pkv == 3, "try_peek returns 1 and the oldest message");
    ck(tg == 1 && tgv == 3, "try_get returns 1 and removes it");
    ck(tg_empty == 0, "try_get on empty mailbox returns 0");
    ck(tp == 1, "try_put on unbounded mailbox");
    ck(pkv2 == 9, "peek returns the message");
    mbu.put(1); mbu.put("s");
    ck(mbu.try_put(3) == 0, "try_put on full bounded mailbox returns 0");
    ck(mbu.num() == 2, "bounded num");
    smb.put("x"); ck(smb.try_put("y") == 0, "try_put on full string mailbox");
    begin string s; smb.get(s); ck(s == "x", "string mailbox get"); end
  end
  initial begin
    #20;
    fork
      begin @e; te = $time; end
      begin #2 -> e; end
    join
    fork
      begin wait (e.triggered); twt = $time; end
      begin #1 -> e; end
    join
    tdone = $time;
    ck(tg1 == 0 && tg2 == 0, "semaphore gets with keys available do not block");
    ck(r_try1 == 0, "try_get with no keys returns 0");
    ck(tg3 == 5, "get blocks until put at 5");
    ck(r_try2 == 1 && r_try3 == 1 && r_try4 == 0, "try_get after puts");
    ck(te == 22, "waiter woken by -> in the same time step");
    ck(twt == 22, "e.triggered true in the time step of the trigger");
    ck(tdone == 23, "join waits for the slower branch");
    if (errs == 0) $display("PASSED");
  end
endmodule
