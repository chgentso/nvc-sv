// Elaboration statistics for generate blocks
module sub #(parameter int W = 1) (input logic [W-1:0] a, output logic y);
   assign y = |a;
endmodule

module vlog_genstats1;
   logic [3:0] v [4];
   logic [3:0] r;
   for (genvar i = 0; i < 4; i++) begin : g
      sub #(.W(4)) u (.a(v[i]), .y(r[i]));
      for (genvar j = 0; j < 2; j++) begin : h
         logic t;
         assign t = v[i][j];
      end
   end
   if (1) begin : c
      logic z;
   end
   initial begin
      v[0] = 0; v[1] = 1; v[2] = 0; v[3] = 8;
      #1 if (r === 4'b1010) $display("PASSED");
      else $display("FAILED %b", r);
   end
endmodule
