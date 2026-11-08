// fork/join_any with processes blocked on a signal edge, an event, a
// semaphore and a wait condition that are then killed by `disable fork`,
// join, join_none processes that outlive their parent, process handles.
// Expected values (IEEE 1800-2017 9.3.2 fork-join_any, 9.3.3 join_none,
// 9.6.3 disable fork, 9.7 process::status, 15.3 semaphore, 15.5 event):
//  - a process killed by disable fork never resumes, so the event trigger,
//    the semaphore put and the flag set after the loop are seen by nobody:
//    n_ev = n_sem = n_poll = 0;
//  - per iteration the joined blocks add 2 to n_nest and nested(2) adds 3
//    (one per recursion level, all finishing within 2 time units), so
//    n_nest = 1500 * 5 = 7500;
//  - 15 handles are taken (i = 0, 100, ..., 1400) and each of those processes
//    has finished (FINISHED) or been killed (KILLED).
// n_sig depends on whether a posedge falls into the same time step as the
// start of an iteration (a race between the clock process and the fork), so
// it is only required to lie in 0..1500.
class c;
  int n_sig, n_ev, n_sem, n_poll, n_nest, n_aw;
  bit flag;
  event ev;
  semaphore sem = new(0);
  process handles[$];

  task sig_wait(virtual clk_if v);
    @(posedge v.clk); n_sig++;
  endtask

  task nested(int depth);
    if (depth > 0) begin
      fork nested(depth - 1); join_none
      #1;
    end
    n_nest++;
  endtask

  task run(virtual clk_if v);
    for (int i = 0; i < 1500; i++) begin
      // killed while waiting for a signal, an event, a semaphore, a condition
      fork
        sig_wait(v);
        begin @ev; n_ev++; end
        begin sem.get(1); n_sem++; end
        begin wait (flag); n_poll++; end
        #1;
      join_any
      disable fork;
      // finishes normally
      fork
        begin #1; n_nest++; end
        begin #2; n_nest++; end
      join
      // outlives its parent
      fork nested(2); join_none
      if (i % 100 == 0) begin
        process p;
        fork p = process::self(); join_none
        #0;
        handles.push_back(p);
      end
      #3;
    end
  endtask
endclass

interface clk_if;
  bit clk;
endinterface

module svforkreap1;
  clk_if vif();
  c o = new;
  int errors = 0;
  initial forever #2 vif.clk = ~vif.clk;
  initial begin
    fork o.run(vif); join_none
    #20000;
    // the signal, event and semaphore wakers must find no dead process
    ->o.ev; o.sem.put(1); o.flag = 1;
    #10;
    foreach (o.handles[i])
      if (o.handles[i] == null) begin
        $error("handle %0d is null", i); errors++;
      end
      else if (o.handles[i].status() != process::FINISHED
               && o.handles[i].status() != process::KILLED) begin
        $error("handle %0d status %0d", i, int'(o.handles[i].status())); errors++;
      end
    if (o.n_ev != 0 || o.n_sem != 0 || o.n_poll != 0) begin
      $error("killed waiter resumed: ev=%0d sem=%0d poll=%0d", o.n_ev, o.n_sem, o.n_poll); errors++;
    end
    if (o.n_nest != 7500) begin $error("n_nest=%0d expected 7500", o.n_nest); errors++; end
    if (o.handles.size() != 15) begin $error("handles=%0d expected 15", o.handles.size()); errors++; end
    if (o.n_sig < 0 || o.n_sig > 1500) begin $error("n_sig=%0d out of range", o.n_sig); errors++; end
    if (errors == 0) $display("PASSED");
    $finish;
  end
endmodule
