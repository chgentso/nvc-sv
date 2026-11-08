// VPI application startup and simulation control as used by a loaded
// VPI library: the routines in vlog_startup_routines are called when the
// library is loaded (36.9.1), simulation action callbacks
// cbStartOfSimulation and cbEndOfSimulation (38.36.3), vpi_get_vlog_info
// (38.20), vpi_get_time (38.17), vpiTimeUnit and vpiTimePrecision of a
// module and of the simulation (NULL handle) (37.10, 38.11), cbAfterDelay
// (38.36.2) whose callback object is freed once it has happened,
// vpi_printf (38.29) and vpi_control(vpiFinish) (38.4).  The
// C side is test/vpi/vpi1.c; it prints PASSED from its
// cbEndOfSimulation callback.  Clause references are to IEEE 1800-2017.

`timescale 1ns / 1ps

module vpi1;
   reg clk = 0;

   always #1 clk = ~clk;      // Keeps the simulation going

   initial begin
      #100;
      // The application calls vpi_control(vpiFinish) at 5 ns
      $fatal(1, "simulation was not finished by the VPI application");
   end
endmodule
