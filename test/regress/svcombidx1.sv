// Self-checking. Expected values: IEEE 1800-2017 9.2.2.2.1 (the implicit
// sensitivity of always_comb includes the expressions read in the block: the
// index of a select that is the target of an assignment is read, so a change of
// the index re-evaluates the block), 9.2.2.2 (it also runs once at time zero).
module svcombidx1;
  logic [3:0] idx_a = 0;
  logic [3:0] marks;
  logic [3:0] idx [2];
  logic [3:0] perm;
  int errs = 0;
  assign idx[0] = idx_a;
  assign idx[1] = 4'd3;
  // The index of the target is not a constant
  always_comb begin
    marks = '0;
    marks[idx_a[1:0]] = 1'b1;
  end
  // As in a permutation check: the target index is an element of an array
  always_comb begin : p_perm
    perm = '0;
    for (int k = 0; k < 2; k++) perm[idx[k][1:0]] = 1'b1;
  end
  initial begin
    #1 if (marks !== 4'b0001) begin errs++; $error("marks = %b", marks); end
    if (perm !== 4'b1001) begin errs++; $error("perm = %b", perm); end
    idx_a = 2;
    #1 if (marks !== 4'b0100) begin errs++; $error("marks = %b", marks); end
    if (perm !== 4'b1100) begin errs++; $error("perm = %b", perm); end
    if (errs == 0) $display("PASSED");
  end
endmodule
