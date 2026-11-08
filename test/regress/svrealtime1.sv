// fmc-delay hdl/include/tunable_clock_gen.svh:59: realtime variable declared in a begin block, used in an expression with an int parameter
// Expected values: IEEE 1800-2017 6.12 (real/realtime), 11.4.3, 11.3.1 (integer division g_period/2 = 4), 6.24.1 (casts)
// g_period/2 = 4 (integer division); real'(4) * 0.5 = 2.0; cur_period = 4 + 2.0 = 6.0 (tunable), else 4.
module svrealtime1 #(parameter g_period = 8, parameter g_tunable = 1);
  real cur_period;
  initial begin
    if (g_tunable) begin
      realtime period_tune;
      period_tune = realtime'(real'(g_period/2) * 0.5);
      cur_period = g_period/2 + period_tune;
    end else cur_period = g_period/2;
    if (cur_period == 6.0) $display("PASSED");
    else $error("cur_period=%f, expected 6.0", cur_period);
  end
endmodule
