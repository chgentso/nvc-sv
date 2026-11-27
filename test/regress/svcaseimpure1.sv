module svcaseimpure1;
   int cnt;

   function automatic int bump(int v);
      cnt++;
      return v;
   endfunction

   function automatic int pick(int x);
      case (x)
         bump(1): return 10;
         bump(2), bump(3): return 20;
         default: return 30;
      endcase
   endfunction

   initial begin
      int r;
      cnt = 0; r = pick(1);
      if (r != 10 || cnt != 1) $fatal(1, "pick(1) r=%0d cnt=%0d", r, cnt);
      cnt = 0; r = pick(2);
      if (r != 20 || cnt != 2) $fatal(1, "pick(2) r=%0d cnt=%0d", r, cnt);
      cnt = 0; r = pick(3);
      if (r != 20 || cnt != 3) $fatal(1, "pick(3) r=%0d cnt=%0d", r, cnt);
      cnt = 0; r = pick(4);
      if (r != 30 || cnt != 3) $fatal(1, "pick(4) r=%0d cnt=%0d", r, cnt);

      cnt = 0;
      case (bump(2))
         bump(1): r = 1;
         bump(2): r = 2;
         default: r = 3;
      endcase
      if (r != 2 || cnt != 3) $fatal(1, "case expr r=%0d cnt=%0d", r, cnt);

      $display("PASSED");
   end
endmodule
