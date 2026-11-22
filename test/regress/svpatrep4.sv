package svpatrep4_pkg;
  localparam int NumItems = 4;
  typedef logic [3:0] sel_t;
endpackage

module svpatrep4_sub #(
  parameter svpatrep4_pkg::sel_t Table[svpatrep4_pkg::NumItems] =
    '{svpatrep4_pkg::NumItems{4'h5}}
) (
  output logic [3:0] out0,
  output logic [3:0] out3
);
  assign out0 = Table[0];
  assign out3 = Table[3];
endmodule

module svpatrep4;
  logic [3:0] a, b;
  svpatrep4_sub u_sub (.out0(a), .out3(b));

  initial begin
    #1;
    if (a !== 4'h5 || b !== 4'h5)
      $fatal(1, "FAIL: a=%h b=%h", a, b);
    $display("PASSED");
  end
endmodule
