module svpatpack1;
  logic [0:1][1:0][0:1] mixed;
  logic [1:0][3:0][3:0] simp;

  initial begin
    mixed = '{ '{ '{1, 0}, '{0, 1} }, '{ '{1, 0}, '{0, 1} } };
    if (mixed !== 8'b10011001) $fatal(1, "mixed=%b", mixed);

    simp = '{ '{4'd3, 4'd2, 4'd1, 4'd0}, '{4'd1, 4'd2, 4'd3, 4'd4} };
    if (simp !== 32'h3210_1234) $fatal(1, "simp1=%h", simp);

    simp = '{2{ '{4'd3, 4'd2, 4'd1, 4'd0} }};
    if (simp !== 32'h3210_3210) $fatal(1, "simp2=%h", simp);

    simp = '{2{ '{4{ 4'd3 }} }};
    if (simp !== 32'h3333_3333) $fatal(1, "simp3=%h", simp);

    $display("PASSED");
    $finish;
  end
endmodule
