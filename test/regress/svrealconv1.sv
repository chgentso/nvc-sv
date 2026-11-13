// Self-checking. IEEE 1800-2017 6.12.2, 20.5: converting a four-state value
// to real treats x and z bits as zero and $itor takes an integral value of
// any width, signed or not.
module svrealconv1;
  real r;
  logic [47:0] u48 = 48'hff00_00000000;
  logic signed [47:0] s48 = 48'shff00_00000000;
  initial begin
    r = 32'bxz000_111;
    if (r != 7.0) $error("FAIL xz %f", r);
    r = $itor(u48);
    if (r != 280375465082880.0) $error("FAIL itor u %f", r);
    r = $itor(s48);
    if (r != -1099511627776.0) $error("FAIL itor s %f", r);
    r = $itor(-3);
    if (r != -3.0) $error("FAIL itor int %f", r);
    $display("PASSED");
  end
endmodule
