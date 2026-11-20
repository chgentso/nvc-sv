module svconstcast1;
  logic [7:0] x = 8'ha5;
  logic signed [3:0] s = 4'shf;
  localparam int P = const'(10 + 5);

  initial begin
    if (const'(x) !== 8'ha5) $fatal(1, "x=%h", const'(x));
    if (const'(s) !== 4'shf) $fatal(1, "s=%h", const'(s));
    if (const'(4'shf) >= 4'sh0) $fatal(1, "signedness lost");
    if (P !== 15) $fatal(1, "P=%0d", P);
    $display("PASSED");
    $finish;
  end
endmodule
