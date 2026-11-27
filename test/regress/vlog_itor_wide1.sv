module vlog_itor_wide1;
  reg signed [63:0] w64;
  reg signed [64:0] w65;
  reg signed [2047:0] w2048;
  reg [8*32-1:0] text;

  initial begin
    w64 = -1;
    $sformat(text, "%.0f", w64 ^ 64'sb0);
    if (text != "-1") $fatal(1, "64-bit -1: got %0s", text);

    w65 = -1;
    $sformat(text, "%.0f", w65 ^ 65'sb0);
    if (text != "-1") $fatal(1, "65-bit -1: got %0s", text);

    w2048 = -3;
    $sformat(text, "%.0f", w2048 ^ 2048'sb0);
    if (text != "-3") $fatal(1, "2048-bit -3: got %0s", text);

    w64 = 64'sh8000000000000000;
    $sformat(text, "%.0f", w64 ^ 64'sb0);
    if (text != "-9223372036854775808") $fatal(1, "64-bit min: got %0s", text);

    $display("PASSED");
  end
endmodule
