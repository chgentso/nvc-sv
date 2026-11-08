// Expected values: IEEE 1800-2017 9.2.1/9.2.2 (initial and always procedures start at time 0), 9.4.1 (delay
// control), 9.4.3 (wait), 9.4.2 (event control), 9.3.2 (fork-join), 9.4.5 (intra-assignment delay), 10.4.2
// (nonblocking assignment with intra-assignment delay), 4.7. The order in which the processes of one time step
// start is not defined, so only completion times, final values and the clk samples are checked; the delay
// of the clock generators is set in the same process so that it does not depend on that order.
`timescale 1ns/1ns
module svproctime1;
  reg [1:0] clk = 0;
  time periods[2];
  integer x, y, z;
  time t_tk, t_for, t_fork, t_intra, t_wait, t_event;
  int a15 = 0;
  int err = 0;
  string s_at1, s_at9, s_at16;
  task automatic tk; #1; endtask
  initial begin tk; t_tk = $time; end
  initial begin for (y = 0; y < 2; y++) #1; t_for = $time; end
  initial begin fork #1; join t_fork = $time; end
  initial begin z = #1 5; t_intra = $time; end
  initial x <= #1 7;
  initial begin wait (x == 7); t_wait = $time; end
  initial begin @(x); t_event = $time; end
  always begin a15++; #20; end
  generate
    genvar i;
    for (i = 0; i < 2; i++) begin
      initial begin
        periods[i] = 8;
        forever #(periods[i]) clk[i] <= ~clk[i];
      end
    end
  endgenerate
  initial begin
    #1 s_at1 = $sformatf("%b", clk);
    #8 s_at9 = $sformatf("%b", clk);
    #7 s_at16 = $sformatf("%b", clk);
    if (t_tk != 1) begin err++; $error("task delay finished at %0t", t_tk); end
    if (t_for != 2 || y != 2) begin err++; $error("for loop finished at %0t y=%0d", t_for, y); end
    if (t_fork != 1) begin err++; $error("fork finished at %0t", t_fork); end
    if (t_intra != 1 || z != 5) begin err++; $error("intra-assignment delay: t=%0t z=%0d", t_intra, z); end
    if (x !== 7) begin err++; $error("x=%0d", x); end
    if (t_wait != 1) begin err++; $error("wait finished at %0t", t_wait); end
    if (t_event != 1) begin err++; $error("event control finished at %0t", t_event); end
    if (a15 != 1) begin err++; $error("always ran %0d times by t=16, expected 1", a15); end
    if (s_at1 != "00") begin err++; $error("clk at 1: %s", s_at1); end
    if (s_at9 != "11") begin err++; $error("clk at 9: %s", s_at9); end
    if (s_at16 != "11") begin err++; $error("clk at 16 (updates by nonblocking assignment come after): %s", s_at16); end
    if (err == 0) $display("PASSED");
    $finish;
  end
endmodule
