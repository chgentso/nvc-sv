// fork with join and join_any, wait fork, and disable fork
module svfork1;
   int done_a, done_b, done_c;
   int counter;

   task automatic after(int d, ref int flag);
      #(d);
      flag = 1;
   endtask

   task automatic ticker();
      forever begin
         #1;
         counter++;
      end
   endtask

   initial begin
      int x, y;
      time t0;

      // join waits for everything
      t0 = $time;
      fork
         #5 x = 1;
         begin
            #10;
            y = 2;
         end
      join
      if ($time - t0 != 10) $display("FAILED join time %0d", $time - t0);
      if (x != 1 || y != 2) $display("FAILED join shared %0d %0d", x, y);

      // join_any continues after the first finishes
      t0 = $time;
      fork
         #3 done_a = 1;
         #8 done_b = 1;
      join_any
      if ($time - t0 != 3) $display("FAILED any time %0d", $time - t0);
      if (done_a != 1 || done_b != 0) $display("FAILED any %0d %0d", done_a, done_b);

      // wait fork waits for what is still running
      wait fork;
      if ($time - t0 != 8) $display("FAILED wait time %0d", $time - t0);
      if (done_b != 1) $display("FAILED wait");

      // wait fork with no children returns at once
      t0 = $time;
      wait fork;
      if ($time != t0) $display("FAILED empty wait");

      // Empty fork
      fork
      join

      // disable fork stops children, including those blocked in a task
      counter = 0;
      fork
         ticker();
      join_none
      #4;
      if (counter != 3 && counter != 4) $display("FAILED ticking %0d", counter);
      disable fork;
      x = counter;
      #10;
      if (counter != x) $display("FAILED disable %0d %0d", counter, x);

      // disable fork also stops grandchildren
      counter = 0;
      fork
         begin
            fork
               ticker();
            join_none
            #100;
            done_c = 1;
         end
      join_none
      #4;
      disable fork;
      x = counter;
      #200;
      if (counter != x || done_c != 0) $display("FAILED nested");

      // A child disabled while parent blocked in wait fork wakes the parent
      fork
         #1000 done_c = 1;
      join_none
      fork
         begin
            #1;
         end
      join_none
      #2;
      disable fork;
      wait fork;
      if (done_c != 0) $display("FAILED disabled child ran");

      // Locals declared in the fork, and copies with join_none
      for (int i = 0; i < 3; i++) begin
         fork
            automatic int j = i;
            begin
               #1;
               if (j != i && j > 2) $display("FAILED copy %0d", j);
            end
         join_none
      end
      wait fork;

      $display("PASSED");
      $finish;
   end
endmodule
