// Expected values: IEEE 1800-2017 9.4.3 (wait statements on class/assoc/signal
// conditions wake immediately when memory or watched signals change, even after
// 100k pure-RTL signal-only cycles that skip sv_watched scanning).
module svpollskip1;
  class Box;
    int cnt = 0;
    bit go = 0;
  endclass

  Box c;
  int aa[int];
  logic clk = 0;
  logic sig = 0;
  logic [7:0] rtl_q = 8'h00;
  logic [7:0] rtl_d;
  int woke_mem = 0;
  int woke_aa = 0;
  int woke_mix = 0;
  time t_mem, t_aa, t_mix;

  always #5 clk = ~clk;

  // Pure signal-only RTL processes
  always_comb begin
    rtl_d = rtl_q + 8'h01;
    for (int i = 0; i < 2; i++)
      rtl_d = rtl_d ^ 8'h03;
  end

  always_ff @(posedge clk) begin
    rtl_q <= rtl_d;
  end

  initial begin
    c = new;

    fork
      begin
        wait (c.cnt == 42);
        woke_mem++;
        t_mem = $time;
      end
      begin
        wait (aa.exists(7) && aa[7] == 99);
        woke_aa++;
        t_aa = $time;
      end
      begin
        wait (sig == 1'b1 && c.go == 1'b1);
        woke_mix++;
        t_mix = $time;
      end
    join_none

    // Run 100,000 pure-RTL signal-only clock cycles with mixed sig toggling
    // while c.go == 0 (should NOT fire woke_mix).
    repeat (100000) @(posedge clk);
    if (woke_mem != 0 || woke_aa != 0 || woke_mix != 0)
      $fatal(1, "premature wake after 100k cycles: %0d %0d %0d",
             woke_mem, woke_aa, woke_mix);

    // Toggle watched signal while c.go == 0
    #1 sig = 1'b1;
    #1 sig = 1'b0;
    if (woke_mix != 0)
      $fatal(1, "woke_mix fired with c.go == 0");

    // NBA write to class member after 100k signal-only cycles
    #1 c.cnt <= 42;
    #1;
    if (woke_mem != 1)
      $fatal(1, "woke_mem=%0d expected 1", woke_mem);

    // Assoc array write
    #1 aa[7] = 99;
    #1;
    if (woke_aa != 1)
      $fatal(1, "woke_aa=%0d expected 1", woke_aa);

    // Set c.go = 1 first, then toggle sig = 1
    #1 c.go = 1'b1;
    #1;
    if (woke_mix != 0)
      $fatal(1, "woke_mix fired with sig == 0");
    #1 sig = 1'b1;
    #1;
    if (woke_mix != 1)
      $fatal(1, "woke_mix=%0d expected 1", woke_mix);

    $display("PASSED");
    $finish;
  end
endmodule
