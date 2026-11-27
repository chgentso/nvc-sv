// Test folding of assignment pattern items inside V_PARAM_ASSIGN overrides

module sub_pat #(
  parameter int N = 1,
  parameter int unsigned A[N] = '{0}
) (
  output int unsigned sum
);
  int unsigned acc;
  always_comb begin
    acc = 0;
    for (int i = 0; i < N; i++)
      acc += A[i];
  end
  assign sum = acc;
endmodule

module svpatparam2;
  localparam int unsigned BASE[2] = '{10, 20};
  int unsigned s;

  sub_pat #(
    .N(3),
    .A({32'd7, 32'd8, BASE[1]})
  ) u (.sum(s));

  initial begin
    #1;
    if (s !== 35) $fatal(1, "s=%0d exp 35", s);
    $display("PASSED");
    $finish;
  end
endmodule
