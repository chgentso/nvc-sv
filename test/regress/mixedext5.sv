module mixedext5_dut #(
  parameter int          DEPTH   = 8,
  parameter int          NEG_VAL = -25,
  parameter real         GAIN    = 3.25,
  parameter bit          ENABLE  = 1'b1,
  parameter logic        FLAG    = 1'b1,
  parameter logic [7:0]  MASK    = 8'hC3,
  parameter bit   [3:0]  NIB     = 4'b1011,
  parameter logic signed [7:0] OFFS = -8'sd7,
  parameter string       TAG     = "hello"
);
  localparam int DOUBLED = DEPTH * 2;

  for (genvar i = 0; i < 4; i++) begin : gen_c
    localparam int GP = 3 * i + 1;
    logic dummy;
    assign dummy = 1'b0;
  end
endmodule
