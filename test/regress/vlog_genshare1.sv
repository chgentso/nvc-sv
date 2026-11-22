// One-hot multiplexer: the body of each loop is shared by its iterations
module vlog_genshare1_and #(parameter int W = 1) (
   input logic [W-1:0] a, b,
   output logic [W-1:0] y);
   assign y = a & b;
endmodule

module vlog_genshare1;
   localparam int W = 12, I = 20;
   logic [W-1:0] in_i [I];
   logic [I-1:0] sel;
   logic [W-1:0] out_o;
   logic [I-1:0] in_mux [W];

   for (genvar b = 0; b < W; ++b) begin : g_outer
      logic [I-1:0] bits;
      for (genvar i = 0; i < I; ++i) begin : g_inner
         assign in_mux[b][i] = in_i[i][b];
      end
      vlog_genshare1_and #(.W(I)) u_and (.a(in_mux[b]), .b(sel), .y(bits));
      assign out_o[b] = |bits;
   end

   initial begin
      for (int k = 0; k < I; k++)
         in_i[k] = 12'h100 + k * 7;
      for (int k = 0; k < I; k++) begin
         sel = '0;
         sel[k] = 1'b1;
         #1;
         if (out_o !== 12'h100 + k * 7) begin
            $display("FAILED %0d %h", k, out_o);
            $finish;
         end
      end
      sel = '0;
      #1 if (out_o !== '0) $display("FAILED none %h", out_o);
      else $display("PASSED");
   end
endmodule
