// A Verilog top level instantiating the VHDL entity LEAF from vhdl1.vhd
module mixed2;
  reg  [3:0] a = 4'b0101;
  wire [3:0] b;
  wire       io;
  leaf #(.N(4)) u_vhdl (.a(a), .b(b), .io(io));
endmodule
