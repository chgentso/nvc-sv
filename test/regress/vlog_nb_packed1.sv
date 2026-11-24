`timescale 1ns/1ns

module vlog_nb_packed1;
   logic [15:0] a, b, c;
   bit   [31:0] u;
   logic [7:0]  watched;
   int          watched_evts = 0;

   always @(watched) watched_evts++;

   initial begin
      #1;
      watched_evts = 0;

      // Whole then slice NBA in same delta: slice must win (FIFO order)
      a <= 16'h1234;
      a[7:0] <= 8'hAA;
      #1;
      if (a !== 16'h12AA) $fatal(1, "whole-then-slice a=%h", a);

      // Slice then whole NBA in same delta: whole must win (FIFO order)
      b <= 16'h0000;
      #1;
      b[15:8] <= 8'hBB;
      b <= 16'h5678;
      #1;
      if (b !== 16'h5678) $fatal(1, "slice-then-whole b=%h", b);

      // Delayed NBA followed by 0-delay NBA at wakeup timestamp
      c <= 16'h1111;
      #5;
      c <= #2 16'h2222;
      #2;
      c <= 16'h3333;
      #1;
      if (c !== 16'h3333) $fatal(1, "delayed-then-zero c=%h", c);

      // Unwatched fast-direct NBA then value check
      u <= 32'h0000_0001;
      u <= 32'hdead_cafe;
      #1;
      if (u !== 32'hdead_cafe) $fatal(1, "u=%h", u);

      // Watched NBA no-op filtering
      watched <= 8'h42;
      #1;
      watched <= 8'h42;
      watched <= 8'h42;
      #1;
      if (watched_evts !== 1) $fatal(1, "watched_evts=%0d", watched_evts);

      $display("PASSED");
      $finish;
   end
endmodule
