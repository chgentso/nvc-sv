// IEEE 1800-2017 9.6.2: disabling a task name terminates all active
// executions of that task (from any caller), output arguments are not
// copied back, and the caller continues with the statement after the call.
// A task can be named hierarchically (mod.task), and a named block inside
// a task running in a fork can be disabled from outside.
module svdisabletask1_sub;
  int done = 0;
  task work(input int d);
    #d;
    done++;
  endtask
endmodule

module svdisabletask1;
  int ended[$];
  int after[$];
  int cont = 0;
  int outv = -1;

  svdisabletask1_sub u();

  task automatic slow(input int id, output int o);
    o = id;
    #100;
    ended.push_back(id);
  endtask

  task automatic loopy(output int count);
    count = 0;
    begin : body
      forever begin
        #2 count++;
      end
    end
    count += 1000;            // Runs after disable body
  endtask

  initial begin
    int o1, o2, o3, c;
    o1 = -1; o2 = -1;
    fork
      begin slow(1, o1); after.push_back(1); end
      begin slow(2, o2); after.push_back(2); end
      begin #10 disable slow; end
    join
    if ($time != 10) $fatal(1, "disable slow: join at %0d", $time);
    if (ended.size() != 0) $fatal(1, "task body continued %p", ended);
    if (after.size() != 2) $fatal(1, "callers did not continue %p", after);
    // 13.5 / 9.6.2: output arguments of a disabled task are unspecified;
    // only check the callers continued.

    // A later call of the same task is not affected
    slow(3, o3);
    if (o3 != 3 || ended.size() != 1 || ended[0] != 3)
      $fatal(1, "call after disable: o3=%0d %p", o3, ended);

    // Hierarchical task name
    fork
      u.work(50);
      u.work(60);
      begin #5 disable u.work; end
    join
    if (u.done != 0 || $time != 115)
      $fatal(1, "disable u.work: done=%0d t=%0d", u.done, $time);

    // Named block inside a forked task
    fork
      loopy(c);
      begin #5 disable loopy.body; end
    join
    if (c != 1002)
      $fatal(1, "disable loopy.body: count = %0d", c);

    $display("PASSED");
  end
endmodule
