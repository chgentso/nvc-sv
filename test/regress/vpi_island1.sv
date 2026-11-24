`timescale 1ns/1ns

module vpi_island1_dut (
   input  logic        clk,
   input  logic        rst_n,
   input  logic [7:0]  din,
   output logic [7:0]  dout
);
   logic [7:0]  comb_sum;
   logic [7:0]  comb_xor;
   logic [7:0]  ff_q;
   bit   [15:0] ff_2s;
   logic [95:0] wide_q;

   assign comb_sum = din + 8'h10;

   always_comb begin
      comb_xor = ff_q ^ comb_sum;
   end

   always_ff @(posedge clk or negedge rst_n) begin
      if (!rst_n) begin
         ff_q   <= 8'h05;
         ff_2s  <= 16'h1234;
         wide_q <= 96'h0123_4567_89ab_cdef_fedc_ba98;
      end else begin
         ff_q   <= comb_sum;
         ff_2s  <= ff_2s + {8'h00, din};
         wide_q <= {wide_q[87:0], din};
      end
   end

   assign dout = comb_xor;
endmodule

module vpi_island1;
   logic       clk = 1'b0;
   logic       rst_n = 1'b0;
   logic [7:0] din = 8'h01;
   logic [7:0] dout;

   vpi_island1_dut u_dut (
      .clk   (clk),
      .rst_n (rst_n),
      .din   (din),
      .dout  (dout)
   );

   always #5 clk = ~clk;

   initial begin
      // t=0..9ns: reset asserted -> ff_q=0x05, comb_sum=0x11, comb_xor=0x14
      #12;
      rst_n = 1'b1;
      din = 8'h20; // comb_sum=0x30
      // t=15ns posedge: ff_q <= 0x30
      #10; // t=22ns
      din = 8'h03; // comb_sum=0x13
      // t=25ns posedge: ff_q <= 0x13 (unless forced!)
      #10; // t=32ns
      din = 8'h04; // comb_sum=0x14
      // t=35ns posedge: ff_q <= 0x14
      #10; // t=42ns
      din = 8'h05; // comb_sum=0x15
      // t=45ns posedge: ff_q <= 0x15
      #10; // t=52ns
      din = 8'h06;
      #10; // t=62ns
      $finish;
   end
endmodule
