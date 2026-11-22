// Generate blocks of a module body shared by several instances
module cell #(parameter int N = 8) (input logic [N-1:0] a, output logic [N-1:0] y,
                                     output logic p);
   for (genvar i = 0; i < N; i++) begin : g
      if (i % 2 == 0) begin : even
         assign y[i] = ~a[i];
      end
      else begin : odd
         assign y[i] = a[i];
      end
   end
   begin : par
      assign p = ^a;
   end
endmodule

module vlog_gencache1;
   logic [7:0] a [100];
   logic [7:0] y [100];
   logic [99:0] p;
   logic [3:0] a2, y2;
   logic p2;

   for (genvar k = 0; k < 100; k++) begin : u
      cell c (.a(a[k]), .y(y[k]), .p(p[k]));
   end

   cell #(.N(4)) c2 (.a(a2), .y(y2), .p(p2));

   initial begin
      for (int k = 0; k < 100; k++)
         a[k] = k;
      a2 = 4'b0011;
      #1;
      for (int k = 0; k < 100; k++) begin
         if (y[k] !== (k[7:0] ^ 8'h55) || p[k] !== ^k[7:0]) begin
            $display("FAILED %0d %h %b", k, y[k], p[k]);
            $finish;
         end
      end
      if (y2 !== 4'b0110 || p2 !== 1'b0) begin
         $display("FAILED c2 %b %b", y2, p2);
         $finish;
      end
      $display("PASSED");
   end
endmodule
