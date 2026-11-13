module lintvlogwidth2(input logic [7:0] in, output logic [3:0] o);
  localparam logic [7:0] Q = 8'hAB;
  localparam logic [3:0] P = Q;
  parameter int N = 64'd5000000000;
  parameter M = 8'hff;
  logic a = 2'b11;
  logic [3:0] b = in;
  logic [3:0] ok1 = 4'hf;
  logic [7:0] ok2 = 8'b0000_0001;
  wire c = 2'b10;
  wire [1:0] d = in[3:0];
  assign o = b;
endmodule
