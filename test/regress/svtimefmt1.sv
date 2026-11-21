`timescale 1ns/1ps

module svtimefmt1;
  reg [8*512-1:0] text;

  initial begin
    $timeformat(-9, 200, " ns", 0);
    $sformat(text, "%t", 12);
    if (text != {"12.", {200{"0"}}, " ns"}) $fatal(1, "mismatch 1");
    $sformat(text, "%.250t", 1.5);
    if (text != {"1.5", {249{"0"}}, " ns"}) $fatal(1, "mismatch 2");
    $display("PASSED");
  end
endmodule
