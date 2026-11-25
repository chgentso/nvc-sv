`timescale 1ns/1ns

module vpi_island4_sub (
   input  wire       clk,
   input  wire       rst,
   input  wire [7:0] din,
   output wire       ready,
   output wire [7:0] q_out
);
   integer i;
   reg ready_reg = 1'b1;
   reg [7:0] q_reg = 8'h5a;
   reg [7:0] q_next;

   assign ready = ready_reg;
   assign q_out = q_reg;

   // Module-level loop var shared with initial ejects always @* from island,
   // while always @(posedge clk) stays in the island.
   initial begin
      for (i = 0; i < 1; i = i + 1) begin end
   end

   always @* begin
      q_next = din;
      for (i = 0; i < 4; i = i + 1)
         q_next = q_next + 8'h01;
   end

   always @(posedge clk) begin
      if (rst) begin
         ready_reg <= 1'b0;
         q_reg     <= 8'h00;
      end else begin
         ready_reg <= 1'b1;
         q_reg     <= q_next;
      end
   end
endmodule

module vpi_island4 (
   input  wire       clk,
   input  wire       rst,
   input  wire [7:0] din,
   output wire       ready1,
   output wire       ready2,
   output wire [7:0] q1,
   output wire [7:0] q2
);
   // Two instances share clk so hier mode builds a super-domain.
   vpi_island4_sub u1 (
      .clk   (clk),
      .rst   (rst),
      .din   (din),
      .ready (ready1),
      .q_out (q1)
   );

   vpi_island4_sub u2 (
      .clk   (clk),
      .rst   (rst),
      .din   (din),
      .ready (ready2),
      .q_out (q2)
   );
endmodule
