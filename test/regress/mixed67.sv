// A VHDL input port with a null range and a default value may be left
// unconnected in a Verilog instance
module mixed67;
  logic clk = 0, q;
  mixed67_core dut (.clk_i(clk), .q_o(q));
  initial begin
    #1 clk = 1;
    #1 $display("q=%b", q);
  end
endmodule
