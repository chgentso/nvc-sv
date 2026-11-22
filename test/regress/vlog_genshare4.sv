// Hierarchical names into and out of the iterations of a shared loop
module vlog_genshare4;
   logic [7:0] a = 8'b10110010;
   logic [7:0] y;

   for (genvar i = 0; i < 8; i++) begin : g
      localparam int Pa = 3 * i + 1;
      logic t;
      assign t = a[i] ^ Pa[0];
      assign y[i] = t;
   end

   for (genvar i = 1; i < 4; i++) begin : s
      logic t;
      assign t = g[2].t & a[i];             // Hierarchical name: not shared
   end

   logic [g[2].Pa:0] wide;                 // Width from a parameter

   initial begin
      #1;
      if (y !== 8'b11100111) $display("FAILED y %b", y);
      if (g[5].t !== 1'b1 || g[2].Pa != 7 || $bits(wide) != 8)
         $display("FAILED read %b %0d %0d", g[5].t, g[2].Pa, $bits(wide));
      if (s[3].t !== g[2].t && a[3]) $display("FAILED sibling");
      force g[3].t = 1'b0;
      #1 if (y[3] !== 1'b0) $display("FAILED force %b", y);
      release g[3].t;
      #1 if (y !== 8'b11100111) $display("FAILED release %b", y);
      $display("PASSED");
   end
endmodule
