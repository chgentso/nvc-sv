// Test nested array reduction methods on multi-dimensional fixed unpacked arrays

module svwith2d1;
  initial begin
    automatic logic [7:0] m[2][2];
    automatic int s;
    automatic int p;

    m = '{'{5, 10}, '{15, 20}};
    s = m.sum with (item.sum with (item));
    if (s !== 50) $fatal(1, "sum=%0d exp 50", s);

    p = m.product with (item.sum with (int'(item)));
    if (p !== (5 + 10) * (15 + 20)) $fatal(1, "prod=%0d exp 525", p);

    $display("PASSED");
    $finish;
  end
endmodule
