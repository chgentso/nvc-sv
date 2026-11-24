`timescale 1ns/1ns

module vpi_island2_src (
   input  logic       clk,
   input  logic       rst_n,
   input  logic [7:0] din,
   output logic [7:0] y,
   output logic [7:0] q
);
   always_comb y = din + 8'h10;

   always_ff @(posedge clk or negedge rst_n) begin
      if (!rst_n)
         q <= 8'h05;
      else
         q <= din;
   end
endmodule

module vpi_island2_dst (
   input  logic       clk,
   input  logic       rst_n,
   input  logic [7:0] mid,
   input  logic [7:0] q_in,
   output logic [7:0] dout,
   output logic [7:0] q_out
);
   always_comb dout = mid + 8'h01;

   always_ff @(posedge clk or negedge rst_n) begin
      if (!rst_n)
         q_out <= 8'h00;
      else
         q_out <= dout ^ q_in;
   end
endmodule

module vpi_island2;
   logic       clk = 1'b0;
   logic       rst_n = 1'b0;
   logic [7:0] din = 8'h01;
   logic [7:0] w_mid;
   logic [7:0] w_q;
   logic [7:0] dout;
   logic [7:0] q_out;

   vpi_island2_src u_src (
      .clk   (clk),
      .rst_n (rst_n),
      .din   (din),
      .y     (w_mid),
      .q     (w_q)
   );

   vpi_island2_dst u_dst (
      .clk   (clk),
      .rst_n (rst_n),
      .mid   (w_mid),
      .q_in  (w_q),
      .dout  (dout),
      .q_out (q_out)
   );

   always #5 clk = ~clk;

   initial begin
      // t=0..9ns: din=0x01 -> w_mid=0x11, dout=0x12
      #12;
      rst_n = 1'b1;
      din = 8'h20; // w_mid=0x30, dout=0x31
      // t=15ns posedge: u_src.q <= 0x20, u_dst.q_out <= 0x31 ^ 0x05 = 0x34
      #10;
      // t=22ns: w_mid is forced to 0xa5 by VPI from t=20ns..28ns
      din = 8'h03;
      // t=25ns posedge: u_src.q <= 0x03, u_dst.q_out <= (0xa5+1) ^ 0x20 = 0x86
      #10;
      // t=32ns: force released at 28ns; din=0x04 -> w_mid=0x14, dout=0x15
      din = 8'h04;
      #10;
      $finish;
   end
endmodule
