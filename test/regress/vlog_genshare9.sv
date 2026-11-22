// Selects computed from the genvar of a shared loop which are out of
// range, sensitivity to them, and several drivers of one net
module vlog_genshare9;
   logic [7:0] a, b, c, q;
   logic [8:0] s;
   wire [3:0] w;
   wire [7:0] x;
   logic clk = 0;

   for (genvar i = 0; i < 8; i++) begin : g
      assign b[i] = a[i + 1];             // Out of range for i = 7
      always_comb c[i] = a[i + 1] | a[i];
      always_ff @(posedge clk) q[i] <= a[i + 1];
      assign s[i + 2] = a[i];             // Out of range for i = 7
      assign x[i] = a[i] ? 1'b1 : 1'bz;
      assign w[i / 2] = a[i] ? 1'b0 : 1'bz;   // Two drivers of each bit
   end

   initial begin
      a = 8'b10010110;
      #1;
      if (b !== 8'bx1001011 || c !== 8'b11011111)
         $display("FAILED comb %b %b", b, c);
      if (s[8:2] !== 7'b0010110 || x !== 8'b1zz1z11z || w !== 4'b0000)
         $display("FAILED drivers %b %b %b", s, x, w);
      clk = 1;
      #1 if (q !== 8'bx1001011) $display("FAILED ff %b", q);
      a = 8'b01000001;
      #1 if (b !== 8'bx0100000 || c !== 8'bx1100001)
         $display("FAILED change %b %b", b, c);
      clk = 0;
      #1 clk = 1;
      #1 if (q !== 8'bx0100000) $display("FAILED ff2 %b", q);
      $display("PASSED");
   end
endmodule
