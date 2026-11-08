// Killing a process from itself and a process which was not forked
module svproc2;
   `define CHECK(x, y) if ((x) !== (y)) begin \
      $display("FAILED line %0d: %0d !== %0d", `__LINE__, (x), (y)); \
      $finish; end

   process ip, fp;
   int x, y, z;

   task automatic stop_self();
      process::self().kill();
   endtask

   initial begin
      ip = process::self();
      #10;
      x = 1;           // Never reached as the process is killed at time 5
   end

   initial begin
      #5;
      `CHECK(ip.status(), process::WAITING);
      ip.kill();
      `CHECK(ip.status(), process::KILLED);
   end

   initial begin
      #20;
      `CHECK(x, 0);
      fork
         begin
            fp = process::self();
            y = 1;
            fp.kill();
            y = 2;     // Not executed
         end
         begin
            z = 1;
            stop_self();
            z = 2;     // Not executed
         end
      join_none
      #1;
      `CHECK(z, 1);
      `CHECK(y, 1);
      `CHECK(fp.status(), process::KILLED);
      fp.await();      // Already finished so does not wait
      `CHECK($time, 21);
      $display("PASSED");
   end
endmodule
