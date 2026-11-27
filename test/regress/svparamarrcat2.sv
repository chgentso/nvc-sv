// Test unpacked array concatenation {a, b, c} as module parameter override
// in both declaration-before-use and use-before-declaration orders

module sub_before #(
  parameter int N = 1,
  parameter int A[N-1:0] = '{0}
) (
  output int sum
);
  int acc;
  always_comb begin
    acc = 0;
    for (int i = 0; i < N; i++)
      acc += A[i];
  end
  assign sum = acc;
endmodule

module svparamarrcat2;
  int s1, s2;
  sub_before #(.N(3), .A({2, 4, 6})) u1 (.sum(s1));
  sub_after  #(.N(3), .A({1, 3, 5})) u2 (.sum(s2));

  initial begin
    #1;
    if (s1 !== 12) $fatal(1, "s1=%0d exp 12", s1);
    if (s2 !== 9)  $fatal(1, "s2=%0d exp 9", s2);
    $display("PASSED");
    $finish;
  end
endmodule

module sub_after #(
  parameter int N = 1,
  parameter int A[N-1:0] = '{0}
) (
  output int sum
);
  int acc;
  always_comb begin
    acc = 0;
    for (int i = 0; i < N; i++)
      acc += A[i];
  end
  assign sum = acc;
endmodule
