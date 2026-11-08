// Ports which are unpacked arrays of real
// Expected values: IEEE 1800-2017 23.2.2 (ports, unpacked array ports), 10.3 (continuous assignment), 9.4.2 (event control)
module sub (input real vin[3], output real vout[3], output real sum);
  integer k;
  assign sum = vin[0] + vin[1] + vin[2];
  always @(vin[0] or vin[1] or vin[2])
    for (k = 0; k < 3; k = k + 1) vout[k] = vin[k] * 2.0;
endmodule

module svrealarr2;
  real a[3];
  real b[3];
  real s;
  integer i;
  int err = 0;
  task automatic chk(bit ok, string m);
    if (!ok) begin $error("%s", m); err++; end
  endtask
  sub u (.vin(a), .vout(b), .sum(s));
  initial begin
    a[0] = 1.0; a[1] = 2.5; a[2] = 3.0;
    #1 chk(b[0] == 2.0 && b[1] == 5.0 && b[2] == 6.0 && s == 6.5, "step 1");
    a[1] = 4.0;
    #1 chk(b[0] == 2.0 && b[1] == 8.0 && b[2] == 6.0 && s == 8.0, "step 2");
    i = 1;
    a[i] = a[i] + 0.5;
    #1 chk(b[0] == 2.0 && b[1] == 9.0 && b[2] == 6.0 && s == 8.5, "step 3");
    if (err == 0) $display("PASSED");
  end
endmodule
