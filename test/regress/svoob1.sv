// Out-of-bounds and x indices: reads return the default of the element type, writes are ignored
// Expected values: IEEE 1800-2017 7.4.6 (array index out of range), 7.10.1 / 7.5 (queue, dynamic array),
// 11.5.1 (bit-select and part-select out of range, x/z index), 6.8 (default values: x for 4-state, 0 for 2-state)
module svoob1;
  int ai[4] = '{1,2,3,4};
  bit [7:0] ab[4] = '{1,2,3,4};
  logic [7:0] al[4] = '{1,2,3,4};
  integer ag[4] = '{1,2,3,4};
  real ar[4] = '{1.0,2.0,3.0,4.0};
  byte aby[2][2] = '{'{1,2},'{3,4}};
  logic [3:0] vec = 4'b1010;
  bit [3:0] bvec = 4'b1010;
  int q[$] = '{1,2};
  int d[] = '{1,2};
  int aa[int];
  int i, j;
  int errs = 0;
  initial begin
    i = 7; j = -1;
    if (ai[i] !== 0 || ai[j] !== 0 || ai[4] !== 0 || ai['x] !== 0) begin $error("int array"); errs++; end
    if (ab[i] !== 8'd0 || ab[j] !== 8'd0 || ab[4] !== 8'd0) begin $error("bit array"); errs++; end
    if (al[i] !== 8'bx || al[j] !== 8'bx || al[4] !== 8'bx) begin $error("logic array"); errs++; end
    if (ag[i] !== 32'bx || ag[j] !== 32'bx || ag[4] !== 32'bx) begin $error("integer array"); errs++; end
    if (ar[i] != 0.0 || ar[j] != 0.0) begin $error("real array"); errs++; end
    if (aby[i][0] !== 8'sd0 || aby[0][i] !== 8'sd0 || aby[1][1] !== 8'sd4) begin $error("2-D byte array"); errs++; end
    if (vec[i] !== 1'bx || vec[j] !== 1'bx || vec[4] !== 1'bx || vec['x] !== 1'bx) begin $error("logic bit-select"); errs++; end
    if (bvec[i] !== 1'b0 || bvec[j] !== 1'b0 || bvec[4] !== 1'b0 || bvec['x] !== 1'b0) begin $error("bit bit-select"); errs++; end
    if (vec[i +: 2] !== 2'bxx || bvec[i +: 2] !== 2'b00) begin $error("part-select +: fully outside"); errs++; end
    if (vec[3 +: 3] !== 3'bxx1 || bvec[3 +: 3] !== 3'b001) begin $error("part-select +: partly outside"); errs++; end
    if (vec[2 -: 4] !== 4'b010x || bvec[2 -: 4] !== 4'b0100) begin $error("part-select -: partly outside"); errs++; end
    if (q[i] !== 0 || q[j] !== 0) begin $error("queue"); errs++; end
    if (d[i] !== 0 || d[j] !== 0) begin $error("dynamic array"); errs++; end
    ai[i] = 5; ab[j] = 5; vec[i] = 1; bvec[j] = 1;
    if (ai[0] !== 1 || ab[0] !== 8'd1 || vec !== 4'b1010 || bvec !== 4'b1010) begin
      $error("out-of-range writes changed something"); errs++;
    end
    if (errs == 0) $display("PASSED");
  end
endmodule
