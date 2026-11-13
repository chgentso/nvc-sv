`timescale 1ns/1ns
// A scalar std_logic or std_ulogic generic of a VHDL entity is set from a
// Verilog parameter value assignment or keeps its default
module mixed55;
  wire [2:0] x1, x2;
  mixed55_sub #(.g_rst(1'b1), .g_z(1'bz)) u1 (.x(x1));
  mixed55_sub u2 (.x(x2));
  initial begin
    #1;
    if (x1 !== 3'bz11) $error("x1 = %b, expected z11", x1);
    else if (x2 !== 3'b010) $error("x2 = %b, expected 010", x2);
    else $display("PASSED");
  end
endmodule
