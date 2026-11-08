// Time scale applies to delays, literals, and system functions
`timescale 1ns/1ps

module vlog65_sub;
   time t = 0;
   initial begin
      #3;
      t = $time;
   end
endmodule

`timescale 1us/1ns

module vlog65;
   vlog65_sub sub ();

   time d;
   real r;
   string s;
   reg [3:0] q = 0;

   initial begin
      if ($time != 0) $display("FAILED start");
      #5;                       // 5us
      if ($time != 5) $display("FAILED time %0d", $time);
      if (sub.t != 3) $display("FAILED sub %0d", sub.t);
      #(2);                     // 7us
      if ($time != 7) $display("FAILED paren %0d", $time);
      d = 3;
      #d;                       // 10us
      if ($time != 10) $display("FAILED var %0d", $time);
      #(d);                     // 13us
      if ($time != 13) $display("FAILED var paren %0d", $time);
      #1.5;                     // 14.5us
      if ($realtime != 14.5) $display("FAILED real %f", $realtime);
      if ($time != 15) $display("FAILED round %0d", $time);
      #500ns;                   // 15us
      if ($time != 15 || $realtime != 15.0) $display("FAILED literal %0d", $time);
      d = 2us;
      if (d != 2) $display("FAILED value %0d", d);
      r = 500ns;
      if (r != 0.5) $display("FAILED fraction %f", r);
      q <= #1 4'd5;             // 16us
      #2;                       // 17us
      if (q !== 4'd5) $display("FAILED nba");
      $timeformat(-9, 1, "ns", 10);
      s = $sformatf("%t", $realtime);
      if (s != " 17000.0ns") $display("FAILED format <%s>", s);
      $display("PASSED");
      $finish;
   end
endmodule
