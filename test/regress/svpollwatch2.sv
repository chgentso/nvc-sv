// Order of polled waits against writes in the same time step, with
// non-blocking assignments, disabled threads and interleaved waits.

module svpollwatch2;
   class C;
      int a = 0;
      int b = 0;
      bit go = 0;
   endclass

   C c;
   logic clk = 0;
   logic sig = 0;
   int n = 0;

   task automatic say(string s);
      $display("%0t %s", $time, s);
      n++;
   endtask

   initial begin
      c = new;
      fork
         begin wait (c.a == 1); say("A woke"); c.b = 1; end
         begin wait (c.b == 1); say("B woke"); c.go = 1; end
         begin wait (c.go); say("GO woke"); end
         begin wait (c.a == 2 || c.b == 7); say("OR woke"); end
         begin wait (c.a == 3 && sig); say("AND woke"); end
         begin
            fork
               begin wait (c.a == 100); say("KILLED"); end
            join_none
            #1 disable fork;
            say("disabled");
         end
      join_none

      #3;
      c.a = 1;          // wakes A in this time step, then B, then GO
      #2;
      c.a <= 2;         // non-blocking
      #2;
      c.a = 3;
      #1 sig = 1;
      #1;
      c.a = 100;
      #3;
      if (n != 6) $fatal(1, "n = %0d", n);
      $display("PASSED");
   end
endmodule
