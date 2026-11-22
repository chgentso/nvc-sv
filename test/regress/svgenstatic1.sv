module svgenstatic1;
  logic [7:0] res [4:6];

  for (genvar k = 4; k <= 6; k++) begin : gen_blk
    initial begin : p_stim
      bit [k-1:0] tmp;
      tmp = k'(k + 1);
      res[k] = 8'(tmp);
    end
  end

  initial begin
    #2;
    if (res[4] !== 8'd5) $fatal(1, "res[4]=%0d exp=5", res[4]);
    if (res[5] !== 8'd6) $fatal(1, "res[5]=%0d exp=6", res[5]);
    if (res[6] !== 8'd7) $fatal(1, "res[6]=%0d exp=7", res[6]);
    $display("PASSED");
    $finish;
  end
endmodule
