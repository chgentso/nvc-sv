// A genvar needed as a constant keeps one body for each iteration
module vlog_genshare3;
   logic [31:0] src = 32'hdeadbeef;
   logic [63:0] w, r, c, p, u;

   for (genvar i = 1; i < 5; i++) begin : g_width
      logic [i:0] v;                       // Width of a declaration
      assign v = src[i:0];
      assign w[i*8 +: 8] = {{(8-i-1){1'b0}}, v};
   end

   for (genvar i = 0; i < 4; i++) begin : g_unpacked
      logic [3:0] m [i+1];                 // Unpacked dimension
      always_comb for (int k = 0; k <= i; k++) m[k] = src[k*4 +: 4];
      assign u[i*4 +: 4] = m[i];
   end

   for (genvar i = 0; i < 4; i++) begin : g_part
      assign p[i*8 +: 8] = src[i*8+7:i*8];  // Constant part select bounds
   end

   for (genvar i = 1; i < 4; i++) begin : g_repl
      assign r[i*8 +: 8] = {i{src[i]}};     // Replication count
   end

   for (genvar i = 1; i < 4; i++) begin : g_cast
      assign c[i*8 +: 8] = 8'((i*2)'(src));  // Size of a cast
   end

   initial begin
      #1;
      if (w[39:8] !== 32'h0f0f0703 || p[31:0] !== 32'hdeadbeef
          || r[31:8] !== 24'h070301 || c[31:8] !== 24'h2f0f03
          || u[15:0] !== 16'hbeef)
         $display("FAILED %h %h %h %h %h", w[39:8], p[31:0], r[31:8],
                  c[31:8], u[15:0]);
      else
         $display("PASSED");
   end
endmodule
