// VPI callbacks: cbValueChange on a scalar variable, a vector variable
// updated by a nonblocking assignment and a net, with vpiSimTime and
// vpiSuppressTime and vpiScalarVal/vpiIntVal value formats;
// vpi_remove_cb of a value change callback and of a pending cbAfterDelay;
// cbReadWriteSynch (writes in it wake processes in the same time step),
// cbReadOnlySynch (sees the values after the nonblocking assignments of
// the time step), cbNextSimTime and cbAfterDelay.
// IEEE 1800-2017 38.36 (vpi_register_cb), 38.36.1 (object value change
// callbacks), 38.36.2 (time callbacks), 38.33 (vpi_remove_cb), 4.4.2
// (scheduling regions).  The C side is test/vpi/vpi4.c.

`timescale 1ns / 1ns

module vpi4;
   reg clk = 0;
   reg [7:0] cnt = 0;
   wire [7:0] cnt_w;
   reg [7:0] rw_target = 0;
   integer   rw_wakeups = 0;

   assign cnt_w = cnt;

   always #5 clk = ~clk;      // Edges at 5, 10, 15, ...

   always @(posedge clk) cnt <= cnt + 1;

   always @(rw_target) begin
      // Written from the cbReadWriteSynch callback at 15 ns
      if ($time != 15) $fatal(1, "rw_target changed at %0t", $time);
      if (rw_target !== 8'h5a) $fatal(1, "rw_target = %h", rw_target);
      rw_wakeups++;
   end

   initial begin
      #100;
      if (rw_wakeups != 1) $fatal(1, "rw_wakeups = %0d", rw_wakeups);
      $finish;
   end
endmodule
