// $countbits with x and z control bits counts the bits with those values
// IEEE 1800-2017 20.9 (bit vector system functions: $countbits counts the
// bits that have any of the control bit values, which may be 0, 1, x or z)
module svcountbits2;
  logic [7:0] u = 8'b1x0z_x1z0;
  int errs;
  initial begin
    if ($countbits(u, 1'bx) != 2) begin errs++; $display("FAILED x %0d", $countbits(u, 1'bx)); end
    if ($countbits(u, 1'bz) != 2) begin errs++; $display("FAILED z %0d", $countbits(u, 1'bz)); end
    if ($countbits(u, '1, 'x) != 4) begin errs++; $display("FAILED 1,x %0d", $countbits(u, '1, 'x)); end
    if ($countbits(u, '0, '1, 'x, 'z) != 8) begin errs++; $display("FAILED all"); end
    if ($countbits(u, '0) != 2) begin errs++; $display("FAILED 0"); end
    if (errs == 0) $display("PASSED");
  end
endmodule
