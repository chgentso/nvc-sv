module svwild_child(
  input wire source = 1'b0,
  input wire value = 1'b0,
  output wire result
);
  assign result = value;
endmodule

module svwildport1;
  wire r1, r2;
  svwild_child u1(.result(r1), .*);
  wire value = 1'b1;

  wire r3;
  svwild_child u2(.source(s2), .result(r2), .*);
  assign s2 = 1'b1;

  svwild_child u3(.result(r3), .*, .source(s3));
  assign s3 = 1'b1;

  initial begin
    #1;
    if (r1 !== 1'b0) $fatal(1, "r1=%b expected 0", r1);
    if (r2 !== 1'b1) $fatal(1, "r2=%b expected 1", r2);
    if (r3 !== 1'b1) $fatal(1, "r3=%b expected 1", r3);
    $display("PASSED");
  end
endmodule
