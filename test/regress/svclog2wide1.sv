// $clog2 treats its argument as an arbitrary-width unsigned vector
// IEEE 1800-2017 20.8.1
module svclog2wide1;
  localparam int C0   = $clog2(0);
  localparam int C1   = $clog2(1);
  localparam int C2   = $clog2(2);
  localparam int C3   = $clog2(3);
  localparam int C31A = $clog2(32'h7fffffff);
  localparam int C31B = $clog2(32'h80000000);
  localparam int C32A = $clog2(32'h80000001);
  localparam int C32B = $clog2(32'hffffffff);
  localparam int C32C = $clog2(256'h1_0000_0000);
  localparam int C33  = $clog2(256'h1_0000_0001);
  localparam int C63  = $clog2(64'h8000_0000_0000_0000);
  localparam int C64  = $clog2(64'hffff_ffff_ffff_ffff);
  localparam int C100 = $clog2(256'd1 << 100);
  localparam int C101 = $clog2((256'd1 << 100) + 256'd1);

  logic [63:0] dyn64;
  logic [127:0] dyn128;

  initial begin
    if (C0 !== 0 || C1 !== 0 || C2 !== 1 || C3 !== 2)
      $fatal(1, "small: %0d %0d %0d %0d", C0, C1, C2, C3);
    if (C31A !== 31 || C31B !== 31 || C32A !== 32 || C32B !== 32)
      $fatal(1, "32b: %0d %0d %0d %0d", C31A, C31B, C32A, C32B);
    if (C32C !== 32 || C33 !== 33 || C63 !== 63 || C64 !== 64)
      $fatal(1, "64b: %0d %0d %0d %0d", C32C, C33, C63, C64);
    if (C100 !== 100 || C101 !== 101)
      $fatal(1, "wide: %0d %0d", C100, C101);

    dyn64 = 64'h8000_0000_0000_0000;
    if ($clog2(dyn64) !== 63)
      $fatal(1, "dyn64 pow2: %0d", $clog2(dyn64));
    dyn64 = 64'hffff_ffff_ffff_ffff;
    if ($clog2(dyn64) !== 64)
      $fatal(1, "dyn64 max: %0d", $clog2(dyn64));

    dyn128 = 128'd1 << 90;
    if ($clog2(dyn128) !== 90)
      $fatal(1, "dyn128 pow2: %0d", $clog2(dyn128));
    dyn128 = (128'd1 << 90) + 128'd1;
    if ($clog2(dyn128) !== 91)
      $fatal(1, "dyn128 pow2+1: %0d", $clog2(dyn128));

    $display("PASSED");
  end
endmodule
