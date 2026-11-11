// A variable output port assigned in a procedural block and connected to a
// net with another driver keeps driving the net when the other driver changes
module svoutvarnet1_a (output logic [4:0] o);
   initial o = 5'h15;
endmodule

module svoutvarnet1_b (input logic clk, output logic [3:0] o);
   always_ff @(posedge clk) o <= 4'ha;
endmodule

module svoutvarnet1;
   logic [1:0] mode = 0;
   logic       clk = 0;
   wire [4:0]  w;
   wire [3:0]  v;

   assign w = (mode == 1) ? 5'h1f : 'z;
   assign v = 'z;

   svoutvarnet1_a a (.o(w));
   svoutvarnet1_b b (.clk(clk), .o(v));

   initial begin
      #1;
      if (w !== 5'h15) $fatal(1, "w %b", w);
      mode = 2;
      #1;
      if (w !== 5'h15) $fatal(1, "w after other driver update %b", w);
      mode = 1;
      #1;
      if (w !== 5'b1x1x1) $fatal(1, "w conflict %b", w);
      mode = 0;
      #1;
      if (w !== 5'h15) $fatal(1, "w back %b", w);
      clk = 1;
      #1;
      if (v !== 4'ha) $fatal(1, "v %b", v);
      $display("PASSED");
   end
endmodule
