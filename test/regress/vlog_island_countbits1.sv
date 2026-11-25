module vlog_island_countbits1;
  logic [69:0] in70;
  int c1, c0, co;
  logic oh, oh0;

  always @* begin
    c1  = $countbits(in70, 1'b1);
    c0  = $countbits(in70, 1'b0);
    co  = $countones(in70);
    oh  = $onehot(in70);
    oh0 = $onehot0(in70);
  end

  initial begin
    in70 = 70'h1_0000_0000_0000_0000;
    #1;
    if (c1 !== 1 || c0 !== 69 || co !== 1 || oh !== 1'b1 || oh0 !== 1'b1)
      $fatal(1, "bad1: c1=%0d c0=%0d co=%0d oh=%b oh0=%b", c1, c0, co, oh, oh0);
    in70 = 70'h3_0000_0000_0000_0005;
    #1;
    if (c1 !== 4 || c0 !== 66 || co !== 4 || oh !== 1'b0 || oh0 !== 1'b0)
      $fatal(1, "bad2: c1=%0d c0=%0d co=%0d oh=%b oh0=%b", c1, c0, co, oh, oh0);
    $display("PASSED");
  end
endmodule
