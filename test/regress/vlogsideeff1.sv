// Side effects in the index of an assignment target are evaluated once also
// when the assignment has an intra-assignment delay or is a concatenation
// (IEEE 1800-2017 10.4.1, 9.4.5, 11.4.1), and a side effect in the delay of
// a non-blocking assignment does not change the value assigned.

module vlogsideeff1;
   int arr4[4];
   logic [7:0] bits8;
   int idx, b;
   int x, y;

   initial begin
      arr4 = '{0, 0, 0, 0};
      idx = 0; b = 1;
      arr4[idx++] = #(b++) 5;
      if (idx !== 1 || b !== 2 || arr4[0] !== 5)
         $fatal(1, "blocking %0d %0d %0d", idx, b, arr4[0]);

      arr4[idx++] <= #(b--) 6;
      if (idx !== 2 || b !== 1) $fatal(1, "nba %0d %0d", idx, b);
      #3;
      if (arr4[1] !== 6) $fatal(1, "nba value %0d", arr4[1]);

      idx = 0; arr4[0] = 1; bits8 = 8'hff;
      {arr4[idx++], bits8} += 1;
      if (idx !== 1 || arr4[0] !== 2 || bits8 !== 8'h00)
         $fatal(1, "concat %0d %0d %h", idx, arr4[0], bits8);

      b = 2;
      x <= #(b) 9;
      y <= #(b++) 5;
      #5;
      if (x !== 9 || y !== 5 || b !== 3) $fatal(1, "delay %0d %0d %0d", x, y, b);
      $display("PASSED");
   end
endmodule
