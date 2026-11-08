// IEEE 1800-2017 7.6 / 6.22.2: unpacked arrays with equivalent element types
// can be assigned to each other, whether fixed-size, dynamic or queues.
// Packed and integral types are equivalent if they have the same number of
// bits, signedness and 2-state/4-state kind: byte unsigned and bit[7:0],
// int and bit signed[31:0].  With --relaxed nvc also accepts element types
// that are not equivalent, with a warning, converting each element as in an
// assignment (10.7).
module svunpackeq2;
  bit [7:0] a[2];
  byte unsigned u[2];
  int i[$];
  bit signed [31:0] s[$];
  logic [7:0] l[2];
  int errs;

  initial begin
    a = '{8'd5, 8'd200};
    u = a;
    if (u[0] != 5 || u[1] != 200) begin $error("u = %p", u); errs++; end
    s = '{-1, 7};
    i = s;
    if (i.size() != 2 || i[0] != -1 || i[1] != 7) begin $error("i = %p", i); errs++; end
    l = a;                              // Not equivalent: warning with --relaxed
    if (l[0] !== 8'd5 || l[1] !== 8'd200) begin $error("l = %p", l); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
