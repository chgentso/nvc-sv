// Bit and part selects of elements of queues, dynamic arrays, associative arrays and arrays of
// packed arrays, as rvalues and lvalues.
// Expected values: IEEE 1800-2017 7.10 (queue elements), 7.5, 7.8, 11.5.1 (bit/part select),
// 7.4.5 (multidimensional: pk[0][1] is the upper nibble of the packed element), 11.3/11.4.9
module svqbitsel1;
  parameter int SW = 4;
  bit [SW-1:0] q [$];
  logic [7:0] d [];
  bit [3:0] aa [string];
  bit [1:0][3:0] pk [$];
  int errs = 0, msb_set = 0;
  task automatic chk(string n, int got, int exp);
    if (got !== exp) begin errs++; $error("%s: got %0d expected %0d", n, got, exp); end
  endtask
  initial begin
    q.push_back(4'b1010); q.push_back(4'b0110);
    d = new[2];
    d[0] = 8'hA5; d[1] = 8'h0F;
    aa["x"] = 4'b1001;
    pk.push_back(8'h5A);
    for (int i = 0; i < 2; i++) begin
      if (q[i][SW-1] && i >= 0) msb_set++;
      chk("q bit1", q[i][1], 1);
      chk("q and", q[i][1] & q[i][2], i == 0 ? 0 : 1);
      chk("q not", !q[i][0], 1);
      chk("d [7]", d[i][7], i == 0 ? 1 : 0);
      chk("d [3:0]", d[i][3:0], i == 0 ? 5 : 15);
    end
    chk("msb_set", msb_set, 1);       // only q[0] = 1010 has its msb set
    q[0][0] = 1;
    d[1][7] = 1'b1;
    chk("q[0]", q[0], 4'b1011);
    chk("d[1]", d[1], 8'h8F);
    chk("aa", aa["x"][3] && aa["x"][0], 1);
    chk("pk[0][1]", pk[0][1], 4'b0101);
    chk("pk[0][1][0]", pk[0][1][0], 1);
    q[1][2:1] = 2'b11;
    chk("q[1]", q[1], 4'b0110);
    chk("sum", q[0][0] + q[1][3] + 1, 2);
    if (errs == 0) $display("PASSED");
  end
endmodule
