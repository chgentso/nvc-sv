// Expected values: IEEE 1800-2017 7.6 (array assignment/equality rules), 11.4.5 (equality operators: element-wise
// comparison of unpacked arrays of the same type and shape, indices matched by position, not by index value)
module svrealarrcmp1;
  real u[1:3], w[3:1];
  int a[2], b[2]; bit [7:0] c[3], d[3]; real r[2], q[2]; shortreal sr[2], sq[2];
  int err = 0;
  task automatic chk(bit ok, string m);
    if (!ok) begin $error("%s", m); err++; end
  endtask
  initial begin
    a[0] = 1; a[1] = 2; b[0] = 1; b[1] = 3;
    chk((a == b) === 1'b0 && (a != b) === 1'b1, "int differ");
    c[0] = 1; c[1] = 2; c[2] = 3; d[0] = 1; d[1] = 2; d[2] = 4;
    chk((c == d) === 1'b0 && (c != d) === 1'b1, "byte differ");
    d[2] = 3;
    chk((c == d) === 1'b1 && (c != d) === 1'b0, "byte equal");
    r[0] = 1; r[1] = 2; q[0] = 1; q[1] = 2.5;
    chk(!(r == q) && (r != q), "real differ");
    q[1] = 2;
    chk((r == q) && !(r != q), "real equal");
    u[1] = 1; u[2] = 2; u[3] = 3; w[3] = 1; w[2] = 2; w[1] = 3;
    chk((u == w) && !(u != w), "positional compare of opposite ranges");
    w[1] = 4;
    chk(!(u == w) && (u != w), "positional compare differs");
    sr[0] = 1; sr[1] = 2; sq[0] = 1; sq[1] = 3;
    chk(!(sr == sq) && (sr != sq), "shortreal differ");
    if (err == 0) $display("PASSED");
  end
endmodule
