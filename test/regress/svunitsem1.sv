// Expected values: IEEE 1800-2017 15.3 (semaphore get/put: one key, so the second get waits for the put),
// 15.4 (mailbox put/get preserves the value), 9.3.2 (fork-join), 3.14 (timescale)
// Which of the two forked tasks gets the key first is not specified (9.3.2): only the set of names is checked.
`timescale 1ns/1ns
semaphore sem = new(1);
mailbox #(int) mb = new();
int cnt = 3;
string order[$];
time   when[$];
task automatic grab(string who);
  sem.get(1);
  order.push_back(who);
  when.push_back($time);
  #5;
  sem.put(1);
endtask
module svunitsem1;
  int errs = 0;
  initial begin
    fork grab("a"); grab("b"); join
    mb.put(cnt);
    begin
      int x; mb.get(x);
      if (x !== 3) begin errs++; $error("mailbox value %0d, expected 3", x); end
      if ($time !== 10) begin errs++; $error("time %0t, expected 10", $time); end
    end
    if (order.size() != 2) begin errs++; $error("expected 2 grabs, got %0d", order.size()); end
    else begin
      if (!((order[0] == "a" && order[1] == "b") || (order[0] == "b" && order[1] == "a")))
        begin errs++; $error("grab order %s %s is not a permutation of a,b", order[0], order[1]); end
      if (when[0] !== 0 || when[1] !== 5) begin errs++; $error("get times %0t %0t, expected 0 and 5", when[0], when[1]); end
    end
    if (errs == 0) $display("PASSED");
  end
endmodule
