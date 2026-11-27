// Test folding of unpacked array localparams initialized from slices,
// constant functions (including static functions per 1800-2023 13.4.3),
// and row selects passed as module parameter overrides.

module sub_slice #(
  parameter int unsigned VAL[2] = '{0, 0}
) (
  output int unsigned o0,
  output int unsigned o1
);
  assign o0 = VAL[0];
  assign o1 = VAL[1];
endmodule

module sub_row #(
  parameter logic [7:0] R[3:1] = '{default: 8'd0}
) (
  output int total
);
  assign total = int'(R[3]) + int'(R[2]) + int'(R[1]);
endmodule

module svparamarr3;
  localparam int unsigned LARGE[5] = '{10, 20, 30, 40, 50};
  localparam int unsigned SMALL[2] = LARGE[1+:2];

  typedef int arr4_t[3:0];
  localparam int SIZES[3:0] = '{1, 2, 3, 4};

  function static arr4_t calc_sums;
    int acc = 0;
    for (int i = 0; i < 4; i++) begin
      acc = acc + SIZES[i];
      calc_sums[i] = acc;
    end
  endfunction

  localparam int SUMS[3:0] = calc_sums();
  localparam int SUMS2[3:0] = calc_sums();

  localparam logic [7:0] GRID[2:1][3:1] = '{'{8'd1, 8'd2, 8'd3},
                                            '{8'd10, 8'd20, 8'd30}};

  int unsigned s0, s1;
  int row_sum;

  sub_slice #(.VAL(SMALL)) u_slice (.o0(s0), .o1(s1));
  sub_row   #(.R(GRID[1])) u_row   (.total(row_sum));

  initial begin
    #1;
    if (s0 !== 20 || s1 !== 30)
      $fatal(1, "slice mismatch: %0d %0d", s0, s1);
    if (SUMS[0] !== 4 || SUMS[1] !== 7 || SUMS[2] !== 9 || SUMS[3] !== 10)
      $fatal(1, "sums mismatch: %0d %0d %0d %0d", SUMS[0], SUMS[1], SUMS[2], SUMS[3]);
    if (SUMS2 != SUMS)
      $fatal(1, "static const func mismatch");
    if (row_sum !== 60)
      $fatal(1, "row_sum=%0d exp 60", row_sum);
    $display("PASSED");
    $finish;
  end
endmodule
