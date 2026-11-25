module vlog_genvar_disp1;
  genvar i;
  logic [7:0] out [0:3];
  int sum = 0;

  for (i = 0; i < 4; i = i + 1) begin : g
    assign out[i] = 8'(i * 10);
    initial begin
      #1;
      if (out[i] !== 8'(i * 10))
        $fatal(1, "bad out[%0d]=%0d", i, out[i]);
      $display("idx=%0d val=%0d", i, out[i]);
    end
  end

  initial begin
    #10;
    for (int k = 0; k < 4; k++) sum += out[k];
    if (sum !== 60) $fatal(1, "bad sum=%0d", sum);
    $display("PASSED");
    $finish;
  end
endmodule
