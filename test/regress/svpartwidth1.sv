module svpartwidth1;
  logic [5:2] [7:2] [9:2] a;
  logic [3:0] [7:0] b;

  localparam int W1 = $bits(a[3:2]);
  localparam int W2 = $bits(a[2][5:2]);
  localparam int W3 = $bits(b[2:1]);

  initial begin
    if (W1 !== 96) $fatal(1, "W1=%0d", W1);
    if (W2 !== 32) $fatal(1, "W2=%0d", W2);
    if (W3 !== 16) $fatal(1, "W3=%0d", W3);
    if ($bits(a[3:2]) !== 96) $fatal(1, "rt W1=%0d", $bits(a[3:2]));
    b = 32'h1122_3344;
    if (b[2:1] !== 16'h2233) $fatal(1, "b[2:1]=%h", b[2:1]);
    $display("PASSED");
    $finish;
  end
endmodule
