// Unary operators can be applied to each other: -~x, ~-x, !~x (IEEE 1800-2017 11.4.1)
module vlog_unarychain1;
  bit [7:0] c = 8'h5a;
  int fails = 0;
  initial begin
    if (-~c != 8'(c + 1)) begin fails++; $error("-~c = %0d", -~c); end
    if (~-c != 8'(c - 1)) begin fails++; $error("~-c = %0d", ~-c); end
    if (!~c != 0) begin fails++; $error("!~c"); end
    if (- -c != c) begin fails++; $error("- -c"); end
    if (fails == 0) $display("PASSED");
  end
endmodule
