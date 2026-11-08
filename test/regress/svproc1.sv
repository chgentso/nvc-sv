// The process class: self, status, kill and await
module svproc1;
   `define CHECK(x, y) if ((x) !== (y)) begin \
      $display("FAILED line %0d: %0d !== %0d", `__LINE__, (x), (y)); \
      $finish; end

   process main_p, child1, child2, child3, nop;
   int log_a, log_b, log_c;
   process::state st;

   initial begin
      main_p = process::self();
      `CHECK(main_p != null, 1);
      `CHECK(nop == null, 1);
      `CHECK(main_p.status(), process::RUNNING);

      fork
         begin
            child1 = process::self();
            #10;
            log_a = 1;
         end
         begin
            child2 = process::self();
            #5;
            log_b = 1;
            #100;
            log_b = 2;       // Never reached as it is killed
         end
         begin
            child3 = process::self();
            #1000;
            log_c = 1;       // Killed by another process
         end
      join_none

      #1;
      `CHECK(child1.status(), process::WAITING);
      `CHECK(child2.status() == process::WAITING, 1);
      `CHECK(child1 != child2, 1);

      child3.kill();
      `CHECK(child3.status(), process::KILLED);

      child1.await();       // Waits for the first process to finish
      `CHECK($time, 10);
      `CHECK(log_a, 1);
      `CHECK(child1.status(), process::FINISHED);

      child2.kill();
      `CHECK(child2.status(), process::KILLED);
      `CHECK(log_b, 1);

      #2000;
      `CHECK(log_b, 1);
      `CHECK(log_c, 0);
      st = child1.status();
      `CHECK(st, process::FINISHED);

      // Killing a process which has finished does nothing
      child1.kill();
      `CHECK(child1.status(), process::FINISHED);
      nop = main_p;
      `CHECK(nop == main_p, 1);
      $display("PASSED");
   end
endmodule
