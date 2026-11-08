// An input port of type real connected to an expression: IEEE 1800-2017
// 23.3.3.2 (an input port of variable type is continuously assigned from
// the connected expression, as by an assign statement), 6.12.1 (an
// integral value assigned to a real is converted to real).
module svrealinport1_drv(output real o, input real v);
  assign o = v;
endmodule

module svrealinport1;
  real x1, x2, x3, x4;
  real y = 2.0;
  int i = 3;
  logic [3:0] l = 4'd9;
  int fails = 0;

  svrealinport1_drv d1(.o(x1), .v(1));
  svrealinport1_drv d2(.o(x2), .v(y + 0.5));
  svrealinport1_drv d3(.o(x3), .v(i));
  svrealinport1_drv d4(.o(x4), .v(l));

  initial begin
    #1;
    if (x1 != 1.0 || x2 != 2.5 || x3 != 3.0 || x4 != 9.0) begin
      $error("t=1: %f %f %f %f", x1, x2, x3, x4);
      fails++;
    end
    y = 4.0; i = -2; l = 4'd15;
    #1;
    if (x2 != 4.5 || x3 != -2.0 || x4 != 15.0) begin
      $error("t=2: %f %f %f", x2, x3, x4);
      fails++;
    end
    if (fails == 0) $display("PASSED");
  end
endmodule
