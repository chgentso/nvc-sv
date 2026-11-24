module vlog_wide_tlab1;
   localparam logic [7:0] LUT [0:15] = '{
      8'h63, 8'h7c, 8'h77, 8'h7b, 8'hf2, 8'h6b, 8'h6f, 8'hc5,
      8'h30, 8'h01, 8'h67, 8'h2b, 8'hfe, 8'hd7, 8'hab, 8'h76
   };

   function automatic logic [127:0] wide_mix(input logic [127:0] a,
                                             input logic [127:0] b);
      logic [127:0] t;
      t = (a ^ b) + {a[63:0], b[127:64]};
      t[7:0] = LUT[a[3:0]];
      return t;
   endfunction

   function automatic logic [31:0] wide_fold(input logic [127:0] a,
                                             input logic [127:0] b);
      logic [127:0] x;
      x = (a + b) ^ (a << 3) ^ (b >> 5);
      return x[127:96] ^ x[95:64] ^ x[63:32] ^ x[31:0];
   endfunction

   logic clk = 1'b0;
   always #5 clk = ~clk;

   task automatic wide_across_yield(input logic [127:0] seed,
                                    output logic [127:0] out_v);
      logic [127:0] hold1, hold2;
      hold1 = seed ^ 128'h1111_2222_3333_4444_5555_6666_7777_8888;
      @(posedge clk);
      hold2 = wide_mix(hold1, seed);
      @(posedge clk);
      out_v = hold1 ^ hold2;
   endtask

   logic [127:0] acc, live_a, live_b, task_out;
   logic [31:0]  folded;
   bit   [191:0] b192;

   initial begin
      // 1. Live wide value in caller across scalar-returning wide helper
      live_a = 128'h0123_4567_89ab_cdef_fedc_ba98_7654_3210;
      live_b = 128'ha5a5_5a5a_0f0f_f0f0_1234_5678_9abc_def0;
      acc    = wide_mix(live_a, live_b);
      folded = wide_fold(live_a, live_b);
      // Verify acc was not clobbered by wide_fold's TLAB trim
      if (acc !== wide_mix(live_a, live_b))
         $fatal(1, "acc clobbered after wide_fold: %h", acc);
      if (folded === 32'hx)
         $fatal(1, "folded is X");

      // 2. Tight loop of wide 4-state and 2-state ops (TLAB reuse / overflow)
      b192 = 192'h1;
      for (int i = 0; i < 4000; i++) begin
         acc  = wide_mix(acc, {96'h0, folded});
         b192 = (b192 << 1) ^ (b192 >> 3) ^ {160'h0, folded};
      end
      if (^acc === 1'bx)
         $fatal(1, "acc became X after loop");

      // 3. Task preserving wide vectors across clock edges
      wide_across_yield(128'hdead_beef_cafe_babe_0102_0304_0506_0708, task_out);
      if (task_out !== ((128'hdead_beef_cafe_babe_0102_0304_0506_0708 ^
                         128'h1111_2222_3333_4444_5555_6666_7777_8888) ^
                        wide_mix(128'hdead_beef_cafe_babe_0102_0304_0506_0708 ^
                                 128'h1111_2222_3333_4444_5555_6666_7777_8888,
                                 128'hdead_beef_cafe_babe_0102_0304_0506_0708)))
         $fatal(1, "wide_across_yield mismatch: %h", task_out);

      // 4. 4-state X propagation on wide ternary & LUT
      acc = (1'bx) ? live_a : live_b;
      if (acc[127:0] === live_a && acc[127:0] === live_b)
         $fatal(1, "expected X on ambiguous wide ternary");

      $display("PASSED");
      $finish;
   end
endmodule
