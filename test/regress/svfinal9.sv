// SystemVerilog final procedure in a module instantiated from VHDL (svfinal9.vhd).
// Expected values: IEEE 1800-2017 9.2.3 (final), 9.4.2 (posedge). The VHDL clock (IEEE 1076 concurrent
// conditional assignment `clk <= not clk after 5 ns when now < 42 ns`) rises at 5, 15, 25, 35 and 45 ns, and the VHDL
// process finishes the simulation at 50 ns, so n=5 and $time=50 in the final procedure.
`timescale 1ns/1ps
module svfinal9_sub(input logic clk);
  int n = 0;
  always @(posedge clk) n++;
  final begin
    if (n !== 5) $error("n must be 5, got %0d", n);
    else if ($time != 50) $error("$time must be 50, got %0t", $time);
    else $display("PASSED");
  end
endmodule
