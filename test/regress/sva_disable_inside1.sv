// SVA disable iff with inside, range, cast, class member and function call
// conditions: signal-only conditions use triggers instead of polling while
// preserving asynchronous disable semantics (IEEE 1800-2017 16.12).

module sva_disable_inside1;
   typedef enum logic [1:0] { S_RUN = 0, S_INIT = 1, S_WAIT = 2, S_ERR = 3 } st_e;

   class Ctrl;
      int dis = 0;
   endclass

   logic clk = 0;
   logic rst_ni = 1;
   logic req = 0;
   logic ack = 0;
   st_e  state = S_RUN;
   logic [3:0] code = 0;
   logic [1:0] flag = 0;
   logic side_dis = 0;
   Ctrl  ctrl;

   int fails_inside = 0;
   int fails_range  = 0;
   int fails_cast   = 0;
   int fails_cls    = 0;
   int fails_fn     = 0;

   always #5 clk = ~clk;

   function automatic bit fn_dis();
      return side_dis;
   endfunction

   // Signal-only inside condition (async glitch between clocks must abort)
   a_inside: assert property (
      @(posedge clk) disable iff (!rst_ni || state inside {S_INIT, S_WAIT})
      req |=> ack
   ) else begin
      fails_inside++;
      $display("%0t: a_inside failed", $time);
   end

   // Standalone inside with range
   a_range: assert property (
      @(posedge clk) disable iff (code inside {[2:4], 7})
      req |=> ack
   ) else begin
      fails_range++;
      $display("%0t: a_range failed", $time);
   end

   // Cast condition
   a_cast: assert property (
      @(posedge clk) disable iff (bit'(flag))
      req |=> ack
   ) else begin
      fails_cast++;
      $display("%0t: a_cast failed", $time);
   end

   // Class member condition (falls back to polled watch)
   a_cls: assert property (
      @(posedge clk) disable iff (ctrl != null && ctrl.dis inside {1, 2})
      req |=> ack
   ) else begin
      fails_cls++;
      $display("%0t: a_cls failed", $time);
   end

   // Function call with side input (falls back to sampled disable check)
   a_fn: assert property (
      @(posedge clk) disable iff (fn_dis())
      req |=> ack
   ) else begin
      fails_fn++;
      $display("%0t: a_fn failed", $time);
   end

   initial begin
      ctrl = new;

      // Cycle 1 (t=5): req=1, ack=0 at t=15 -> all 5 should fail once
      @(negedge clk); req = 1; ack = 0;
      @(negedge clk); req = 0; ack = 0;
      @(negedge clk);
      if (fails_inside != 1 || fails_range != 1 || fails_cast != 1
          || fails_cls != 1 || fails_fn != 1)
         $fatal(1, "expected 1 fail each: %0d %0d %0d %0d %0d",
                fails_inside, fails_range, fails_cast, fails_cls, fails_fn);

      // Cycle 2: req=1, then async glitch on state/code/flag/ctrl between
      // clocks, restored before next posedge -> must abort a_inside, a_range,
      // a_cast, a_cls (while a_fn has side_dis=1 at posedge).
      req = 1; ack = 0;
      @(posedge clk);
      #1;
      state    = S_INIT;
      code     = 4'd3;
      flag     = 2'b01;
      ctrl.dis = 2;
      side_dis = 1;
      #1;
      state    = S_RUN;
      code     = 4'd0;
      flag     = 2'b00;
      ctrl.dis = 0;
      @(negedge clk); req = 0;
      @(posedge clk);
      #1 side_dis = 0;
      @(negedge clk);
      if (fails_inside != 1 || fails_range != 1 || fails_cast != 1
          || fails_cls != 1 || fails_fn != 1)
         $fatal(1, "glitch did not disable: %0d %0d %0d %0d %0d",
                fails_inside, fails_range, fails_cast, fails_cls, fails_fn);

      // Cycle 3: non-matching state/code changes (S_ERR, code=5, flag=2'b10)
      // must NOT disable the assertions
      req = 1; ack = 0;
      @(posedge clk);
      #1;
      state = S_ERR;
      code  = 4'd5;
      flag  = 2'b10;    // bit'(2'b10) == 1'b0
      #1;
      state = S_RUN;
      code  = 4'd0;
      flag  = 2'b00;
      @(negedge clk); req = 0;
      @(negedge clk);
      if (fails_inside != 2 || fails_range != 2 || fails_cast != 2
          || fails_cls != 2 || fails_fn != 2)
         $fatal(1, "non-matching glitch disabled: %0d %0d %0d %0d %0d",
                fails_inside, fails_range, fails_cast, fails_cls, fails_fn);

      $display("PASSED");
      $finish;
   end
endmodule
