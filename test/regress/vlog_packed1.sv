`timescale 1ns/1ns

module vlog_packed1_sub;
   logic [63:0]  s64;
   logic [127:0] w128;
   bit   [31:0]  b2;

   initial begin
      s64  = 64'hdead_beef_cafe_0123;
      w128 = {64'h1234_5678_9abc_def0, 64'h0fed_cba9_8765_4321};
      b2   = 32'h55aa_ff00;
   end
endmodule

module vlog_packed1;
   typedef struct packed {
      logic [3:0] hi;
      logic [3:0] lo;
   } pbyte_t;

   vlog_packed1_sub sub ();

   // <=64-bit and >64-bit 4-state and 2-state packed vars
   logic         v1;
   logic [7:0]   v8;
   logic [63:0]  v64;
   logic [127:0] v128;
   logic [255:0] v256;
   pbyte_t       pbyte;

   bit           b1;
   bit   [31:0]  b32;
   bit   [199:0] b200;
   int           i32;

   // Bit-select level trigger test: must only wake when v8_sel[4] changes
   logic [7:0]   v8_sel = 8'h00;
   int           v8_sel_cnt = 0;
   always @(v8_sel[4]) v8_sel_cnt++;

   // Edge trigger tests on packed 1-bit and multi-bit vars
   logic         eclk = 1'bx;
   logic [7:0]   evec = 8'h00;
   int           pos_cnt = 0, neg_cnt = 0, vec_pos_cnt = 0;
   always @(posedge eclk) pos_cnt++;
   always @(negedge eclk) neg_cnt++;
   always @(posedge evec) vec_pos_cnt++;

   // always_comb rollback test: temporary mutation restored must not wake waiter
   logic [7:0]   comb_in = 8'h12;
   logic [7:0]   comb_out = 8'h24;
   int           comb_wake_cnt = 0;
   always @(comb_out) comb_wake_cnt++;
   always_comb begin
      logic [7:0] saved;
      saved = comb_in;
      comb_out = 8'hff;
      comb_out = saved + 8'h12;
   end

   // always_ff NBA whole + slice + delayed NBA
   logic         ff_clk = 0;
   logic [127:0] ff_q = '0;
   logic [15:0]  ff_del = 16'h0000;
   always_ff @(posedge ff_clk) begin
      ff_q <= {64'haaaa_bbbb_cccc_dddd, 64'h1111_2222_3333_4444};
      ff_q[67:60] <= 8'h9a;
      ff_del <= #2 16'hbeef;
   end

   // Continuous assign + delayed continuous assign to packed var
   logic [15:0]  drv_src = 16'h1234;
   logic [15:0]  drv_var;
   logic [15:0]  drv_del;
   assign drv_var = drv_src ^ 16'h00ff;
   assign #2 drv_del = drv_src + 16'h0001;

   // Force / release targets (procedural & continuous-driven, <=64 & >64)
   logic [15:0]  frc16 = 16'h1111;
   logic [127:0] frc128 = 128'h0;
   logic [15:0]  frc_drv;
   logic [15:0]  frc_drv_in = 16'h5555;
   assign frc_drv = frc_drv_in;

   int idx;
   logic [15:0] tmp16;

   initial begin
      // Allow time-0 init & always_comb settle
      #1;

      // 1. Packed 4-state 0/1/Z/X across <=64 and >64
      v1 = 1'bz;
      if (v1 !== 1'bz) $fatal(1, "v1 z mismatch: %b", v1);
      v1 = 1'bx;
      if (v1 !== 1'bx) $fatal(1, "v1 x mismatch: %b", v1);
      v8 = 8'b10zx_01xz;
      if (v8 !== 8'b10zx_01xz) $fatal(1, "v8 mismatch: %b", v8);
      pbyte = 8'h5a;
      if (pbyte.hi !== 4'h5 || pbyte.lo !== 4'ha) $fatal(1, "pbyte mismatch: %h", pbyte);

      v64 = 64'h0123_4567_89ab_cdef;
      v64[35:28] = 8'hzz;
      if (v64 !== 64'h0123_456z_z9ab_cdef) $fatal(1, "v64 slice mismatch: %h", v64);

      v128 = {64'h0011_2233_4455_6677, 64'h8899_aabb_ccdd_eeff};
      v128[67:60] = 8'h5x;
      if (v128[71:56] !== 16'h75x8) $fatal(1, "v128 cross-word mismatch: %h", v128[71:56]);

      v256 = {64'ha1a2_a3a4_a5a6_a7a8, 64'hb1b2_b3b4_b5b6_b7b8,
              64'hc1c2_c3c4_c5c6_c7c8, 64'hd1d2_d3d4_d5d6_d7d8};
      v256[195:188] = 8'hfe;
      if (v256[199:184] !== 16'hafe1) $fatal(1, "v256 slice mismatch: %h", v256[199:184]);

      // Dynamic indexing & out-of-range check
      idx = 60;
      v128[idx +: 8] = 8'hc3;
      if (v128[67:60] !== 8'hc3) $fatal(1, "v128 dyn write mismatch: %h", v128[67:60]);
      idx = 124;
      tmp16 = v128[idx +: 8];
      if (tmp16[7:0] !== 8'bxxxx_0000) $fatal(1, "v128 OOB read mismatch: %b", tmp16[7:0]);

      // 2-state variables mask X/Z -> 0
      b1 = 1'bx;
      b32 = 32'hzzzz_xxxx;
      i32 = -42;
      b200 = {8'hff, 64'h1234_5678_9abc_def0, 64'h0102_0304_0506_0708, 64'h8070_6050_4030_2010};
      b200[131:124] = 8'h55;
      if (b1 !== 1'b0 || b32 !== 32'h0 || i32 !== -42)
         $fatal(1, "2-state scalar mismatch: %b %h %0d", b1, b32, i32);
      if (b200[135:120] !== 16'hf551)
         $fatal(1, "b200 cross-word mismatch: %h", b200[135:120]);

      // 2. Bit-select sensitivity: only bit 4 transitions trigger v8_sel_cnt
      v8_sel_cnt = 0;
      #1 v8_sel = 8'h02;
      #1 v8_sel = 8'h03;
      #1 v8_sel = 8'h0f;
      #1 v8_sel = 8'h1f; // bit 4: 0->1 (#1)
      #1 v8_sel = 8'h10;
      #1 v8_sel = 8'hef; // bit 4: 1->0 (#2)
      #1;
      if (v8_sel_cnt !== 2) $fatal(1, "v8_sel_cnt=%0d expected 2", v8_sel_cnt);

      // 3. Edge triggers on packed vars
      pos_cnt = 0; neg_cnt = 0; vec_pos_cnt = 0;
      #1 eclk = 1'b0;   // x -> 0: negedge
      #1 eclk = 1'b1;   // 0 -> 1: posedge
      #1 eclk = 1'bz;   // 1 -> z: negedge
      #1 eclk = 1'bx;   // z -> x: NO edge
      #1 evec = 8'hfe;  // LSB 0->0: NO posedge
      #1 evec = 8'hff;  // LSB 0->1: posedge
      #1;
      if (pos_cnt !== 1 || neg_cnt !== 2 || vec_pos_cnt !== 1)
         $fatal(1, "edge mismatch pos=%0d neg=%0d vec=%0d", pos_cnt, neg_cnt, vec_pos_cnt);

      // 4. always_comb rollback check
      comb_wake_cnt = 0;
      #1 comb_in = 8'h12; // same value -> no wake
      #1;
      if (comb_wake_cnt !== 0) $fatal(1, "comb_wake_cnt=%0d expected 0", comb_wake_cnt);
      #1 comb_in = 8'h20;
      #1;
      if (comb_out !== 8'h32 || comb_wake_cnt !== 1)
         $fatal(1, "comb update mismatch out=%h cnt=%0d", comb_out, comb_wake_cnt);

      // 5. always_ff NBA + delayed NBA + continuous assign
      if (drv_var !== 16'h12cb || drv_del !== 16'h1235)
         $fatal(1, "drv mismatch var=%h del=%h", drv_var, drv_del);
      #1 drv_src = 16'h00ff;
      #1;
      if (drv_var !== 16'h0000 || drv_del !== 16'h1235)
         $fatal(1, "drv delay before 2ns mismatch var=%h del=%h", drv_var, drv_del);
      #2;
      if (drv_del !== 16'h0100) $fatal(1, "drv delay after 2ns mismatch del=%h", drv_del);

      ff_clk = 1;
      #1;
      if (ff_q[67:60] !== 8'h9a || ff_q[63:0] !== 64'ha111_2222_3333_4444)
         $fatal(1, "ff_q mismatch: %h", ff_q);
      if (ff_del !== 16'h0000) $fatal(1, "ff_del too early: %h", ff_del);
      #2;
      if (ff_del !== 16'hbeef) $fatal(1, "ff_del mismatch: %h", ff_del);

      // 6. Force / release (whole & partial-bit, procedural & driven)
      force frc16[7:4] = 4'ha;
      #1 frc16 = 16'h2222; // bits [7:4] stay forced to 'ha
      #1;
      if (frc16 !== 16'h22a2) $fatal(1, "partial force mismatch: %h", frc16);
      release frc16[7:4];
      #1;
      if (frc16 !== 16'h22a2) $fatal(1, "release procedural partial mismatch: %h", frc16);
      frc16 = 16'h3333;
      #1;
      if (frc16 !== 16'h3333) $fatal(1, "post-release deposit mismatch: %h", frc16);

      force frc128[67:60] = 8'he7;
      #1 frc128 = {64'hffff_ffff_ffff_ffff, 64'h0000_0000_0000_0000};
      #1;
      if (frc128[71:56] !== 16'hfe70) $fatal(1, "frc128 mismatch: %h", frc128[71:56]);
      release frc128;
      frc128[67:60] = 8'h19;
      #1;
      if (frc128[71:56] !== 16'hf190) $fatal(1, "frc128 release mismatch: %h", frc128[71:56]);

      force frc_drv[7:0] = 8'hcc;
      #1;
      if (frc_drv !== 16'h55cc) $fatal(1, "frc_drv force mismatch: %h", frc_drv);
      frc_drv_in = 16'h7788;
      #1;
      if (frc_drv !== 16'h77cc) $fatal(1, "frc_drv masked mismatch: %h", frc_drv);
      release frc_drv;
      #1;
      if (frc_drv !== 16'h7788) $fatal(1, "frc_drv release mismatch: %h", frc_drv);

      // 7. Hierarchical refs read & write
      if (sub.s64 !== 64'hdead_beef_cafe_0123 || sub.b2 !== 32'h55aa_ff00)
         $fatal(1, "href read mismatch");
      if (sub.w128[71:60] !== 12'hf00) $fatal(1, "href slice mismatch: %h", sub.w128[71:60]);
      sub.w128[67:60] = 8'h5a;
      sub.b2 = 32'h1234_5678;
      #1;
      if (sub.w128[67:60] !== 8'h5a || sub.b2 !== 32'h1234_5678)
         $fatal(1, "href write mismatch: %h %h", sub.w128[67:60], sub.b2);

      $display("PASSED");
      $finish;
   end
endmodule
