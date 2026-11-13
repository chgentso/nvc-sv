module linterr1(input clk, input logic [3:0] d, output logic [1:0] q);
  wire w;
  logic a = 2'b11;
  localparam logic [3:0] P = 8'hff;
  logic [1:0] x;
  always @(posedge clk) w <= 1'b1;
  always @(posedge clk) x <= d;
  always @* begin
    case (d)
      4'd0: q = 2'd1;
    endcase
  end
endmodule

module linterr1_other(input logic [3:0] d, output logic [1:0] y);
  assign y = d;
endmodule
