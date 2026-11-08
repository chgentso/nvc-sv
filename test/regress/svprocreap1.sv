// Many short-lived processes: process handles of finished and of disabled (killed) workers.
// Expected values: IEEE 1800-2017 9.6.1 (join_any), 9.6.3 (disable fork), 9.7 (process class:
// enum state {FINISHED, RUNNING, WAITING, SUSPENDED, KILLED}; kill() and await() on a terminated
// process have no effect / return immediately)
`timescale 1ns/1ns
module svprocreap1;
  process fin[$], kld[$];
  int n_done;
  int errs = 0;

  task automatic worker(bit hang);
    process p = process::self();
    if (hang) begin
      kld.push_back(p);
      #1000000;
    end
    else begin
      fin.push_back(p);
      #1;
    end
    n_done++;
  endtask

  initial begin
    for (int i = 0; i < 2000; i++) begin
      fork
        worker(0);
        worker(1);
        #2;
      join_any
      #1;
      disable fork;
      #1;
    end
    if (fin.size() != 2000 || kld.size() != 2000 || n_done != 2000) begin
      errs++; $error("fin=%0d kld=%0d done=%0d", fin.size(), kld.size(), n_done);
    end
    foreach (fin[i])
      if (fin[i].status() != process::FINISHED) begin
        errs++; $error("fin %0d status %0d", i, int'(fin[i].status()));
      end
    foreach (kld[i])
      if (kld[i].status() != process::KILLED) begin
        errs++; $error("kld %0d status %0d", i, int'(kld[i].status()));
      end
    fin[0].kill();
    fin[1999].await();
    kld[0].await();
    kld[1999].kill();
    if ($time != 6000) begin errs++; $error("time %0t", $time); end
    if (fin[0].status() != process::FINISHED || kld[1999].status() != process::KILLED) begin
      errs++; $error("final status %0d %0d", int'(fin[0].status()), int'(kld[1999].status()));
    end
    if (errs == 0) $display("PASSED");
  end
endmodule
