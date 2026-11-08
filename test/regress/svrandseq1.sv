// randsequence with weights, arguments, break, return, if, case and repeat
module svrandseq1;
   `define CHECK(x, y) if ((x) !== (y)) begin \
      $display("FAILED line %0d: %0d !== %0d", `__LINE__, (x), (y)); \
      $finish; end

   int x, y, z;
   int cnt[3];

   function automatic int plain();
      int v;
      randsequence(main)
         main : first second done;
         first : { v = v + 1; };
         second : { v = v + 2; };
         done : { v = v + 3; };
      endsequence
      return v;
   endfunction

   function automatic int weighted();
      int v;
      randsequence(main)
         main : first := 1 | second := 0;
         first : { v = 2; };
         second : { v = -2; };
      endsequence
      return v;
   endfunction

   function automatic int args();
      int v;
      randsequence(main)
         main : first second third;
         first : add(10);
         second : add(5);
         third : add(2);
         void add(int y) : { v = v + y; };
      endsequence
      return v;
   endfunction

   function automatic int stop(int mode);
      int v;
      randsequence(main)
         main : first second third;
         first : { v = v + 10; };
         second : { if (mode == 1) break; } fourth;
         third : { v = v + 100; };
         fourth : { v = v + 1000; };
      endsequence
      return v;
   endfunction

   function automatic int leave(int mode);
      int v;
      randsequence(main)
         main : first second third;
         first : { v = v + 20; };
         second : { if (mode == 1) return; v = v + 10; };
         third : { v = v + 5; };
      endsequence
      return v;
   endfunction

   function automatic int choose(int sw);
      int v;
      randsequence(main)
         main : first;
         first : if (sw) second else third;
         second : { v = 10; };
         third : { v = 5; };
      endsequence
      return v;
   endfunction

   function automatic int pick(int sw);
      int v;
      randsequence(main)
         main : case (sw)
            0 : zero;
            1, 2 : some;
            default : other;
         endcase;
         zero : { v = 100; };
         some : { v = 200; };
         other : { v = 300; };
      endsequence
      return v;
   endfunction

   function automatic int again();
      int v;
      randsequence(main)
         main : repeat (7) bump;
         bump : { v = v + 1; };
      endsequence
      return v;
   endfunction

   function automatic int joined();
      int v;
      randsequence(main)
         main : rand join first second third;
         first : { v = v + 1; };
         second : { v = v + 2; };
         third : { v = v + 4; };
      endsequence
      return v;
   endfunction

   function automatic int loop_break();
      int v;
      randsequence()
         main : { for (int i = 0; i < 10; i++) begin
                     if (i == 4) break;
                     v++;
                  end
                } after;
         after : { v = v + 100; };
      endsequence
      return v;
   endfunction

   initial begin
      `CHECK(plain(), 6);
      `CHECK(weighted(), 2);
      `CHECK(args(), 17);
      `CHECK(stop(0), 1110);
      `CHECK(stop(1), 10);
      `CHECK(leave(0), 35);
      `CHECK(leave(1), 25);
      `CHECK(choose(1), 10);
      `CHECK(choose(0), 5);
      `CHECK(pick(0), 100);
      `CHECK(pick(2), 200);
      `CHECK(pick(9), 300);
      `CHECK(again(), 7);
      `CHECK(joined(), 7);
      `CHECK(loop_break(), 104);

      // Each rule is chosen according to its weight
      for (int i = 0; i < 600; i++) begin
         randsequence(top)
            top : a := 1 | b := 2 | c := 3 { z++; };
            a : { cnt[0]++; };
            b : { cnt[1]++; };
            c : { cnt[2]++; };
         endsequence
      end
      `CHECK(cnt[0] + cnt[1] + cnt[2], 600);
      `CHECK(z, cnt[2]);
      `CHECK(cnt[0] > 0 && cnt[1] > cnt[0] / 2 && cnt[2] > cnt[1] / 2, 1);
      `CHECK(cnt[0] < cnt[2], 1);
      $display("PASSED");
   end
endmodule
