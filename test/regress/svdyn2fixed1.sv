// Test assigning multi-dimensional dynamic arrays to fixed unpacked arrays

module svdyn2fixed1;
  initial begin
    automatic int a[2][4:1];
    automatic int b[];
    automatic int d[3][][];
    automatic int dd[][];
    automatic int c[2][2:1];

    b = new[4];
    for (int i = 0; i < 4; i++)
      b[i] = (i + 1) * 10;

    d[2] = new[2];
    d[2][0] = new[4];
    d[2][0] = '{1, 2, 3, 4};
    d[2][1] = b;

    a = d[2];
    if (a[0][4] !== 1 || a[0][1] !== 4)
      $fatal(1, "row0 mismatch: %0d %0d", a[0][4], a[0][1]);
    if (a[1][4] !== 10 || a[1][1] !== 40)
      $fatal(1, "row1 mismatch: %0d %0d", a[1][4], a[1][1]);

    dd = '{'{5, 6}, '{7, 8}};
    c = dd;
    if (c[0][2] !== 5 || c[0][1] !== 6 || c[1][2] !== 7 || c[1][1] !== 8)
      $fatal(1, "dd mismatch");

    $display("PASSED");
    $finish;
  end
endmodule
