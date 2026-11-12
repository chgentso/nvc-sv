package pk;
  typedef logic [3:0] nib_t;
endpackage

interface bus_if;
  logic [7:0] data;
  modport master (output data);
endinterface

module sub #(parameter int W = 8, parameter logic [3:0] INIT = 4'hA,
             parameter type T = logic [1:0], parameter V,
             localparam int L = W * 2)
  (input wire [W-1:0] d, output logic [7:0] q [0:1],
   input logic [3:0][1:0] m, inout tri t, input int c, output byte b,
   input pk::nib_t n, bus_if.master bus, input e,
   input logic [$clog2(16)-1:0] f);
endmodule

module old(a, b, c);
  input [7:0] a;
  output reg [15:0] b;
  inout c;
endmodule

module top;
  logic x;
  wire [7:0] y;
  bus_if bi ();
  genvar i;
  for (i = 0; i < 4; i = i + 1) begin : g
    sub #(.W(8), .INIT(4'h5)) u (.d(y), .q(), .m(), .t(), .c(), .b(), .n(),
                                 .bus(bi), .e(x), .f());
  end
  if (x) begin : yes
    sub #(4) u1 (.d(), .q(), .m(), .t(), .c(), .b(), .n(), .bus(bi),
                 .e(x), .f()), u2 (.d(), .q(), .m(), .t(), .c(), .b(), .n(),
                 .bus(bi), .e(x), .f());
  end
  old uo (.a(y), .b(), .c());
endmodule
