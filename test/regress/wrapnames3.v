// Waveform scopes of a VHDL entity instantiated from Verilog
module wrapnames3;
  reg  [1:0] a = 2'b01;
  wire [1:0] b;

  wn3_ent u_e (.a(a), .b(b));

  initial begin
    #1 a = 2'b10;
    #1 $finish;
  end
endmodule
