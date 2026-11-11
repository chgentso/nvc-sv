// A Verilog design for the drivers reported by vpi_nvc_connections: net and
// variable drivers (continuous assignments, always_ff, always_comb,
// initial), a module port connection and a net with two drivers.  The C side
// is test/vpi/vpi22.c.

module vpi22_cnt (input logic clk, input logic [3:0] d,
                  output logic [3:0] q);
   always_ff @(posedge clk) q <= d;
endmodule

module vpi22;
   logic       clk = 1'b0;
   logic [3:0] d = 4'd5;
   wire [3:0]  q;
   wire [3:0]  n;
   logic [3:0] r;
   logic [3:0] s;
   wire        bus;

   vpi22_cnt u_cnt (.clk(clk), .d(d), .q(q));

   assign n = ~q;
   assign bus = d[0];
   assign bus = d[1];

   always_comb r = n + 1;

   initial begin
      s = 4'd0;
      #1 s = 4'd3;
   end

   always #5 clk = ~clk;
endmodule
