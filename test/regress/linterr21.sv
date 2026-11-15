module linterr21(input logic clk, input logic rst, input logic go, output logic busy);
  int broken = ;
  logic [1:0] state;
  localparam IDLE = 0, RUN = 1, DONE = 2;
  always_ff @(posedge clk) begin
    if (rst) state <= IDLE;
    else case (state)
      IDLE: if (go) state <= RUN;
      RUN: state <= DONE;
      DONE: state <= IDLE;
      default: state <= IDLE;
    endcase
  end
  logic [1:0] other;
  always_ff @(posedge clk) begin
    if (rst) other <= 0;
    else case (other)
      0: other <= 1;
      1: other <=
    endcase
  end
  assign busy = state == RUN;
endmodule
