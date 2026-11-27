module sv_var_init_hier1_sub #(parameter int W = 1);
  typedef enum logic [7:0] {
    E_ZERO = 8'd0,
    E_VAL  = 8'(W + 10)
  } kind_t;
endmodule

module sv_var_init_hier1;
  for (genvar g = 0; g < 2; g = g + 1) begin : gen
    sv_var_init_hier1_sub #(.W(g + 3)) u();
    int val = int'(u.E_VAL);
  end

  sv_var_init_hier1_sub #(.W(5)) top_u();
  logic [7:0] top_val = top_u.E_VAL;

  initial begin
    #1;
    if (gen[0].val !== 13 || gen[1].val !== 14)
      $fatal(1, "gen=%0d %0d", gen[0].val, gen[1].val);
    if (top_val !== 8'd15)
      $fatal(1, "top_val=%0d", top_val);
    $display("PASSED");
  end
endmodule
