// A final procedure in a program block runs at $finish.
// Expected values: IEEE 1800-2017 9.2.3 (final), 24.3 (program block), 9.4.1 (p = 8 at time 3), 20.2 ($finish).
`timescale 1ns/1ns
program svfinal6;
  int p = 5;
  initial begin #3 p = 8; $finish; end
  final begin
    if (p !== 8) $error("p must be 8, got %0d", p);
    else $display("PASSED");
  end
endprogram
