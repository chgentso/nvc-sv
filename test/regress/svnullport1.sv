module svnullport1_vm #(parameter int WA = 8, parameter int BW = 8) (input logic clk, input logic [(WA/BW)-1:0] we, output logic q);
  always @(posedge clk) q <= |we;
endmodule
module svnullport1;
  logic clk = 0, q;
  always #5 clk = ~clk;
  svnullport1_v u (.clk(clk), .q(q));
  initial begin #20 $display("q=%b", q); $finish; end
endmodule
