`timescale 1ns/1ps

module svtimefmt1;
  string text, exp1, exp2;

  initial begin
    exp1 = "12.";
    repeat (200) exp1 = {exp1, "0"};
    exp1 = {exp1, " ns"};

    exp2 = "1.5";
    repeat (249) exp2 = {exp2, "0"};
    exp2 = {exp2, " ns"};

    $timeformat(-9, 200, " ns", 0);
    $sformat(text, "%t", 12);
    if (text != exp1) $fatal(1, "mismatch 1: %s", text);
    $sformat(text, "%.250t", 1.5);
    if (text != exp2) $fatal(1, "mismatch 2: %s", text);
    $display("PASSED");
  end
endmodule
