module g3(input clk, rst, go, output reg done);
  localparam IDLE = 2'd0, RUN = 2'd1, FIN = 2'd2;
  reg [1:0] state_reg, state_next;
  always @* begin
    state_next = IDLE;
    case (state_reg)
      IDLE: if (go) state_next = RUN; else state_next = IDLE;
      RUN: state_next = FIN;
      FIN: state_next = IDLE;
    endcase
  end
  always @(posedge clk) begin
    if (rst) state_reg <= IDLE;
    else state_reg <= state_next;
  end
endmodule
