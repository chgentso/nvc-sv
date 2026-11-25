module vlog_for_sens1;
  integer i;
  reg [3:0] a, b;
  reg [3:0] y1, y2;

  always @* begin
    for (i = 0; i < 4; i = i + 1)
      y1[i] = a[i];
  end

  always @* begin
    for (i = 0; i < 4; i = i + 1)
      y2[i] = ~b[i];
  end

  initial begin
    a = 4'b1010;
    b = 4'b1100;
    #1;
    if (y1 !== 4'b1010 || y2 !== 4'b0011)
      $fatal(1, "y1=%b y2=%b", y1, y2);
    a = 4'b0101;
    b = 4'b0011;
    #1;
    if (y1 !== 4'b0101 || y2 !== 4'b1100)
      $fatal(1, "y1=%b y2=%b", y1, y2);
    $display("PASSED");
    $finish;
  end
endmodule
