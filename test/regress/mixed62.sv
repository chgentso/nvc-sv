module mixed62_leaf (
  input  real a,
  output real b
);
  assign b = a + 0.5;
endmodule

module mixed62;
  real x = 3.5;
  real y;

  mixed62_sub #(.G_SCALE(1.5)) u (.r_in(x), .r_out(y));

  initial begin
    #2;
    if (y != 5.75) $fatal(1, "y1=%f exp=5.75", y);
    x = -2.0;
    #2;
    if (y != -2.5) $fatal(1, "y2=%f exp=-2.5", y);
    $display("PASSED");
    $finish;
  end
endmodule
