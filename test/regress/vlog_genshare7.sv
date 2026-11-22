// Instances inside a shared loop
module vlog_genshare7_cell #(parameter int W = 4) (
   input logic [W-1:0] a,
   output logic [W-1:0] y,
   output logic p);
   assign y = ~a;
   assign p = ^a;
endmodule

module vlog_genshare7;
   logic [31:0] a = 32'h12345678;
   logic [31:0] y1, y2, y3;
   logic [7:0] p1, p2;
   logic [9:0] p3;

   // Ports selected by the genvar: shared, one body for the cell
   for (genvar i = 0; i < 8; i++) begin : g1
      vlog_genshare7_cell u (.a(a[i*4 +: 4]), .y(y1[i*4 +: 4]), .p(p1[i]));
   end

   // Ports by position and an expression
   for (genvar i = 0; i < 8; i++) begin : g2
      logic [3:0] t;
      vlog_genshare7_cell u (a[i*4 +: 4] ^ 4'h0, t, p2[i]);
      assign y2[i*4 +: 4] = t;
   end

   // Parameter from the genvar: not shared
   for (genvar i = 1; i < 5; i++) begin : g3
      vlog_genshare7_cell #(.W(i)) u (.a(a[i-1:0]), .y(), .p(p3[i]));
   end

   // Instance array
   for (genvar i = 0; i < 2; i++) begin : g4
      logic [3:0] y [2];
      vlog_genshare7_cell u [1:0] (.a(8'h5a), .y({y[1], y[0]}), .p());
      assign y3[i*8 +: 8] = {y[1], y[0]};
   end

   initial begin
      #1;
      if (y1 !== ~a || y2 !== ~a || y3[15:0] !== 16'ha5a5)
         $display("FAILED y %h %h %h", y1, y2, y3);
      if (p1 !== p2 || p1 !== 8'b11010011 || p3[4:1] !== 4'b1000)
         $display("FAILED p %b %b %b", p1, p2, p3);
      $display("PASSED");
   end
endmodule
