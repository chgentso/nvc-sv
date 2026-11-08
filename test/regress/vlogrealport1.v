// An input port of type real connected to an expression: IEEE 1800-2017
// 23.3.3.2 (an input port of variable type is continuously assigned from
// the connected expression), 6.12.1 (an integral value assigned to a real
// is converted to real).
module vlogrealport1_sub #(parameter real E = 0.0) (input real v);
  initial begin
    #1;
    if (v != E) begin
      $display("FAILED: %f expected %f", v, E);
      $fatal;
    end
  end
endmodule

module vlogrealport1;
  integer   i = 3;
  reg [3:0] r = 4'd9;
  real      y = 2.5;

  vlogrealport1_sub #(1.0) u1(.v(1));
  vlogrealport1_sub #(3.0) u2(.v(i));
  vlogrealport1_sub #(9.0) u3(.v(r));
  vlogrealport1_sub #(2.5) u4(.v(y));
  vlogrealport1_sub #(3.5) u5(.v(y + 1));

  initial begin
    #2;
    $display("PASSED");
    $finish;
  end
endmodule // vlogrealport1
