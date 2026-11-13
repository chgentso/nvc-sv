// Delay lists on continuous assignments, min:typ:max timing check limits
// and part-select terminals in path declarations
`timescale 1ps/1ps
module svspecify5_dut (input clk, input [1:0] d, output reg [7:0] o, output x);
  reg [1:0] dd;
  reg notif;
  assign #(3, 0) x = d[0];
  specify
    $setuphold (posedge clk, d, 0:0:0, 0:0:0, notif);
    $setup (d, posedge clk, 1:2:3, notif);
    $hold (posedge clk, d, 1:2:3, notif);
    $recovery (posedge clk, d, 1:2:3, notif);
    $width (posedge clk, 1:2:3, 0);
    $period (posedge clk, 1:2:3);
    ( d[1:1] *> o[7:0]) = (0, 0);
    ( d *> o[3:0]) = (1, 2);
  endspecify
  always @(posedge clk) o <= {6'b0, d};
endmodule

module svspecify5;
  reg clk = 0;
  reg [1:0] d = 2'b01;
  wire [7:0] o;
  wire x;
  svspecify5_dut u (clk, d, o, x);
  initial begin
    #5 if (x !== 1'b1) $display("FAILED rise %b", x);
    d = 2'b00;
    #1 if (x !== 1'b0) $display("FAILED fall %b", x);
    #1 clk = 1;
    #1 if (o === 8'd0) $display("PASSED"); else $display("FAILED o=%b", o);
  end
endmodule
