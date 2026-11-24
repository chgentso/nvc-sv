`timescale 1ns/1ns

module vlog_fast_write1;
   logic [11:0] s12;
   bit   [31:0] b32;
   logic [63:0] w64;
   logic [15:0] p16;
   logic [7:0]  dyn_wait;
   logic [7:0]  dyn_evt;
   logic [7:0]  comb_in;
   logic [7:0]  comb_out;

   int s12_evts = 0;
   int b32_evts = 0;
   int w64_evts = 0;
   int dyn_evt_cnt = 0;

   always @(s12) s12_evts++;
   always @(b32) b32_evts++;
   always @(w64) w64_evts++;

   always_comb begin
      comb_out = comb_in + 8'h1;
   end

   initial begin
      int wide_src;
      #1;
      s12_evts = 0;
      b32_evts = 0;
      w64_evts = 0;

      // Changed write should fire event once
      s12 = 12'h123;
      #1;
      if (s12_evts !== 1) $fatal(1, "s12_evts after 1st change=%0d", s12_evts);

      // No-op write with dirty upper bits outside 12-bit mask must not fire
      wide_src = 32'hcafe_1123;
      s12 = wide_src[11:0];
      s12 = wide_src;
      #1;
      if (s12 !== 12'h123) $fatal(1, "s12 value=%h", s12);
      if (s12_evts !== 1) $fatal(1, "s12_evts after noop=%0d", s12_evts);

      // 2-state 32-bit change and repeated no-op
      b32 = 32'hdead_beef;
      #1;
      b32 = 32'hdead_beef;
      b32 = 32'hdead_beef;
      #1;
      if (b32_evts !== 1) $fatal(1, "b32_evts=%0d", b32_evts);

      // 4-state 64-bit X/Z transitions and no-op
      w64 = 64'h0123_4567_89ab_cdef;
      #1;
      w64 = 64'h0123_4567_89ab_cdef;
      #1;
      if (w64_evts !== 1) $fatal(1, "w64_evts after data=%0d", w64_evts);
      w64 = 64'h0123_zzzz_xxxx_cdef;
      #1;
      w64 = 64'h0123_zzzz_xxxx_cdef;
      #1;
      if (w64_evts !== 2) $fatal(1, "w64_evts after xz=%0d", w64_evts);

      // Force and release must bypass fast-noop/direct and restore cleanly
      p16 = 16'h1111;
      #1;
      force p16[7:0] = 8'hAA;
      p16 = 16'h2233;
      #1;
      if (p16 !== 16'h22AA) $fatal(1, "p16 partial force=%h", p16);
      release p16;
      #1;
      p16 = 16'h4455;
      #1;
      if (p16 !== 16'h4455) $fatal(1, "p16 post-release=%h", p16);

      // Unwatched writes (SIG_F_FAST_DIRECT) followed by dynamic wait
      dyn_wait = 8'h10;
      dyn_wait = 8'h20;
      dyn_wait = 8'h30;
      fork
         begin
            wait (dyn_wait == 8'h42);
            dyn_wait = 8'h99;
         end
      join_none
      #1;
      dyn_wait = 8'h41;
      #1;
      if (dyn_wait !== 8'h41) $fatal(1, "dyn_wait premature=%h", dyn_wait);
      dyn_wait = 8'h42;
      #2;
      if (dyn_wait !== 8'h99) $fatal(1, "dyn_wait not woken=%h", dyn_wait);

      // Unwatched writes followed by dynamic @(dyn_evt)
      dyn_evt = 8'h01;
      dyn_evt = 8'h02;
      fork
         forever begin
            @(dyn_evt);
            dyn_evt_cnt++;
         end
      join_none
      #1;
      dyn_evt = 8'h02; // No-op after attach
      #1;
      if (dyn_evt_cnt !== 0) $fatal(1, "dyn_evt_cnt noop=%0d", dyn_evt_cnt);
      dyn_evt = 8'h03; // Change after attach
      #1;
      if (dyn_evt_cnt !== 1) $fatal(1, "dyn_evt_cnt chg=%0d", dyn_evt_cnt);

      // Unwatched comb input/output transition
      comb_in = 8'h10;
      #1;
      if (comb_out !== 8'h11) $fatal(1, "comb_out=%h", comb_out);
      comb_in = 8'hfe;
      #1;
      if (comb_out !== 8'hff) $fatal(1, "comb_out2=%h", comb_out);

      $display("PASSED");
      $finish;
   end
endmodule
