module mixedext1_dut (
  input  logic       clk,
  input  logic [7:0] din,
  output logic [7:0] q0,
  output logic [7:0] q2,
  output logic [7:0] qo
);
  logic [7:0] lane_out [0:3];

  for (genvar i = 0; i < 4; i++) begin : gen_lane
    logic [7:0] lane_sig = 8'h00;
    wire  [7:0] leaf_obs;

    always_ff @(posedge clk)
      lane_sig <= din + i[7:0];

    mixedext1_leaf u_leaf (.clk(clk), .obs(leaf_obs));
    assign lane_out[i] = lane_sig;
  end

  localparam bit HAS_OPT = 1'b1;
  if (HAS_OPT) begin : gen_opt
    logic [7:0] opt_reg = 8'h00;
    always_ff @(posedge clk)
      opt_reg <= din + 8'h05;
    assign qo = opt_reg;
  end

  assign q0 = lane_out[0];
  assign q2 = lane_out[2];
endmodule
