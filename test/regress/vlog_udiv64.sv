// Unsigned division and remainder of 64-bit values above 2^63 (IEEE 1800-2017 11.4.2)
module vlog_udiv64;
  bit [63:0] u, v, q, r;
  int fails = 0;
  initial begin
    u = 64'hce918b1d658c1991;
    if (u % 3 != 0) begin fails++; $error("u %% 3 = %0d", u % 3); end
    u = 64'hffff_ffff_ffff_fffe;
    if (u % 3 != 2) begin fails++; $error("u %% 3 = %0d", u % 3); end
    if (u / 3 != 64'd6148914691236517204) begin fails++; $error("u / 3 = %0d", u / 3); end
    v = 64'h8000_0000_0000_0001;
    q = u / v; r = u % v;
    if (q != 1 || r != 64'h7fff_ffff_ffff_fffd) begin fails++; $error("u / v = %0d rem %0d", q, r); end
    v = 64'd0;
    if (!$isunknown(u / v) && u / v != 0) begin fails++; $error("division by zero"); end
    if (fails == 0) $display("PASSED");
  end
endmodule
