// Self-checking. Expected values: IEEE 1800-2017 12.7.3 (foreach iterates over
// the dimensions of an array in order, the loop variables going from the left
// to the right bound of each dimension); the array may be an element of a
// larger array selected by a constant index, whose dimensions the loop
// variables then take in turn (extension accepted by common tools).
module svforeachsel1;
  bit [3:0][7:0] key [2];
  int m [3][2];
  int errs = 0, n, s0, s1;
  initial begin
    foreach (key[0][i]) key[0][i] = 8'h10 + i;
    foreach (key[1][i]) key[1][i] = 8'h20 + i;
    n = 0; s0 = 0; s1 = 0;
    foreach (key[0][i]) begin n++; s0 += key[0][i]; end
    foreach (key[1][j]) s1 += key[1][j];
    if (n != 4) begin errs++; $error("n = %0d", n); end
    if (s0 != 'h10 + 'h11 + 'h12 + 'h13) begin errs++; $error("s0 = %0d", s0); end
    if (s1 != 'h20 + 'h21 + 'h22 + 'h23) begin errs++; $error("s1 = %0d", s1); end
    foreach (m[1][j]) m[1][j] = j + 5;
    foreach (m[1][j]) n += m[1][j];
    if (n != 4 + 5 + 6) begin errs++; $error("n2 = %0d", n); end
    if (m[1][0] != 5 || m[1][1] != 6 || m[0][0] != 0) begin errs++; $error("m"); end
    if (errs == 0) $display("PASSED");
  end
endmodule
