module mixed64_sv #(
  parameter int ADD = 1
) (
  input  logic [7:0] a,
  output logic [7:0] y
);
  assign y = a + ADD[7:0];
endmodule
