// Self-checking. Expected values: IEEE 1800-2017 10.9.1 (array assignment
// patterns: the type of the pattern comes from the context, here the formal of
// the port it is connected to), 10.9.1 default: key, 23.3.2.2 (named port
// connections), 23.3.2.1 (ordered port connections).
module svportpat1_sub #(parameter int N = 2, parameter int DW = 4)
  (input logic [DW-1:0] d [N], input logic [N-1:0][DW-1:0] p,
   output int sum, output int psum);
  always_comb begin
    sum = 0;
    for (int i = 0; i < N; i++) sum += d[i];
    psum = p[0] + p[N-1];
  end
endmodule

module svportpat1;
  int s1, s2, s3, ps1, ps2, ps3;
  int errs = 0;
  svportpat1_sub #(.N(3), .DW(4)) u1(.d('{default: 4'd5}), .p('{default: 4'd2}),
                                      .sum(s1), .psum(ps1));
  svportpat1_sub #(.N(2), .DW(4)) u2('{4'd1, 4'd2}, '{4'd3, 4'd4}, s2, ps2);
  svportpat1_sub u3(.d('{0: 4'd7, default: 4'd1}), .p('{default: 4'd0}),
                    .sum(s3), .psum(ps3));
  initial begin
    #1;
    if (s1 !== 15) begin errs++; $error("s1 = %0d expected 15", s1); end
    if (ps1 !== 4) begin errs++; $error("ps1 = %0d expected 2+2", ps1); end
    if (s2 !== 3) begin errs++; $error("s2 = %0d expected 3", s2); end
    if (ps2 !== 7) begin errs++; $error("ps2 = %0d expected 3+4", ps2); end
    if (s3 !== 8) begin errs++; $error("s3 = %0d expected 8", s3); end
    if (ps3 !== 0) begin errs++; $error("ps3 = %0d expected 0", ps3); end
    if (errs == 0) $display("PASSED");
  end
endmodule
