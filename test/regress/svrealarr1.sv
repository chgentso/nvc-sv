// Elements of an unpacked array of real
// Expected values: IEEE 1800-2017 7.4 (unpacked arrays), 6.12 (real), 11.4.3 (arithmetic), 7.4.6 (oob write ignored, oob read gives default value)
module svrealarr1;
  real a[3];
  real c[4:5];
  real s;
  integer i;
  int err = 0;
  task automatic chk(bit ok, string m);
    if (!ok) begin $error("%s", m); err++; end
  endtask
  initial begin
    a[0] = 1.0; a[1] = 2.5; a[2] = 3.0;
    c[4] = 0.25; c[5] = 0.5;
    s = a[0] + a[1] + a[2];
    chk(s == 6.5, "sum");
    i = 1;
    a[i] = a[i] + c[5];
    a[7] = 9.0;
    chk(a[0] == 1.0 && a[1] == 3.0 && a[2] == 3.0, "indexed update / oob write changed array");
    chk(a[5-i] == 0.0, "oob read of real is 0.0");
    chk(c[4] == 0.25 && c[5] == 0.5, "c");
    for (i = 0; i < 3; i = i + 1) a[i] = a[i] * 2.0;
    chk(a[0] == 2.0 && a[1] == 6.0 && a[2] == 6.0, "scaled");
    chk(!(a[1] == 9.0), "oob write leaked");
    if (err == 0) $display("PASSED");
  end
endmodule
