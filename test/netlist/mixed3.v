// Verilog top level with VHDL entities in an instance array, a generate
// loop and below a VHDL entity which instantiates a Verilog module
module mixed3;
  reg  [3:0] a = 4'b0101;
  wire [7:0] b;
  wire [1:0] io;
  wire [3:0] bg [1:0];
  wire       iog [1:0];

  leaf #(.N(4)) ua [1:0] (.a(a), .b(b), .io(io));

  genvar i;
  generate
    for (i = 0; i < 2; i = i + 1) begin : g
      leaf #(.N(4)) u (.a(a), .b(bg[i]), .io(iog[i]));
    end
  endgenerate

  mixed1 u_mid ();
endmodule
