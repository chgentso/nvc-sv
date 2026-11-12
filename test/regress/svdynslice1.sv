// An indexed part select (base +: width, base -: width) of a dynamic array
// or queue is an lvalue that replaces that many elements in place
// (IEEE 1800-2017 7.4.6 and 11.5.1).
module svdynslice1;
  logic data[] = new[16];
  logic part[] = new[4];
  int q[$] = '{0, 1, 2, 3, 4, 5};
  int errs = 0;

  initial begin
    foreach (data[i]) data[i] = 1'b0;
    part = '{1'b1, 1'b0, 1'b1, 1'b1};
    data[4 +: 4] = part;
    for (int i = 0; i < 16; i++) begin
      automatic logic exp = (i == 4 || i == 6 || i == 7);
      if (data[i] !== exp) begin errs++; $error("data[%0d]=%b", i, data[i]); end
    end

    data[11 -: 4] = part;
    for (int i = 0; i < 16; i++) begin
      automatic logic exp = (i == 4 || i == 6 || i == 7 || i == 8 || i == 10 || i == 11);
      if (data[i] !== exp) begin errs++; $error("b data[%0d]=%b", i, data[i]); end
    end

    for (int k = 0; k < 2; k++) begin
      automatic logic byte_p[] = new[8];
      foreach (byte_p[i]) byte_p[i] = 1'b1;
      data[k*8 +: 8] = byte_p;
    end
    foreach (data[i]) if (data[i] !== 1'b1) begin errs++; $error("c data[%0d]=%b", i, data[i]); end

    q[2 +: 2] = '{20, 30};
    if (q[1] != 1 || q[2] != 20 || q[3] != 30 || q[4] != 4) begin
      errs++; $error("queue %p", q);
    end

    if (errs == 0) $display("PASSED");
  end
endmodule
