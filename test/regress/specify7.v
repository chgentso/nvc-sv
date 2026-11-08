`timescale 1ns/1ps
// $hold and $recovery with a zero limit, data changing at the same time as the
// reference edge.
// Expected values: IEEE 1364-2005 15.3 / IEEE 1800-2017 31.5 ($hold, $recovery):
// a violation needs the data (or recovery) event to follow the reference event by
// less than the limit; with a limit of 0.0 the time difference (0 or more) is
// never less than the limit, so no violation is reported and the notifiers
// (i_sub.h, i_sub.r) are not toggled.  The test runs until $finish at 15 ns (20.2).
module specify7;

    reg d   = 1'b0;
    reg clk = 1'b0;

    sub i_sub(
        .d   (d),
        .clk (clk)
    );

    always #5 clk = ~clk;

    initial
    begin
        #5;
        d = 1'b1;
        #10;
        if (i_sub.h !== 1'b0) $display("FAILED: $hold violated with a zero limit");
        else if (i_sub.r !== 1'b0) $display("FAILED: $recovery violated with a zero limit");
        else if ($time != 15) $display("FAILED: end time %0d ns", $time);
        else $display("PASSED");
        $finish;
    end

endmodule

module sub(
    input d,
    input clk
);
  reg h = 1'b0, r = 1'b0;

  specify
    $hold(posedge clk, d, 0.0, h);
    $recovery(posedge clk, d, 0.0, r);
  endspecify
endmodule
