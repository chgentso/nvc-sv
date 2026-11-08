// Expected values: IEEE 1800-2017 6.20 (localparam arrays and struct values used as parameter overrides),
// 27 (generate loops/if), 7.2 (structs), 20.7 ($size, $left; an unpacked dimension [3] is [0:2]),
// 26 (package items), 10.3 (continuous assignment), 9.4.1 (delay)
`timescale 1ns/1ns
typedef struct { bit mul; bit [3:0] div; } t_cfg;
localparam int n[3] = '{1, 2, 3};
localparam t_cfg configs[2] = '{ '{mul: 0, div: 4}, '{mul: 1, div: 9} };
localparam int n_configs = $size(configs);

package pk;
   localparam logic [7:0] tab[4] = '{8'h10, 8'h20, 8'h30, 8'h40};
endpackage

module leaf #(parameter bit g_mul = 0, parameter int g_div = 0) ();
   initial begin
      #1;
      if (g_mul === 1'b0 && g_div == 4) svlparamarr1.seen0++;
      else if (g_mul === 1'b1 && g_div == 9) svlparamarr1.seen1++;
      else begin svlparamarr1.errs++; $error("leaf mul=%b div=%0d", g_mul, g_div); end
   end
endmodule

module svlparamarr1;
   localparam int m[2] = '{7, 8};
   localparam int total = n[0] + n[1] + n[2] + m[1];
   int k = 1;
   int errs = 0, seen0 = 0, seen1 = 0, genhits = 0;
   wire [7:0] w = pk::tab[k];
   wire dm = configs[k].mul;
   for (genvar i = 0; i < n_configs; i++) begin : g
      leaf #(.g_mul(configs[i].mul), .g_div(configs[i].div)) u();
      if (configs[i].mul) begin : gm
         initial begin
            genhits++;
            if (i != 1) begin errs++; $error("gen mul %0d", i); end
         end
      end
   end
   initial begin
      #2;
      if (n[1] !== 2 || m[1] !== 8 || total !== 14) begin errs++; $error("params %0d %0d %0d", n[1], m[1], total); end
      if ($size(n) !== 3 || $left(n) !== 0) begin errs++; $error("size/left"); end
      if (pk::tab[k] !== 8'h20 || w !== 8'h20 || dm !== 1'b1) begin errs++; $error("tab k=1: %h %h %b", pk::tab[k], w, dm); end
      k = 0;
      #1;
      if (w !== 8'h10 || dm !== 1'b0 || configs[k].mul !== 1'b0 || configs[k].div !== 4'd4) begin
         errs++; $error("k=0: w=%h dm=%b", w, dm); end
      for (int j = 0; j < 3; j++) if (n[j] !== j + 1) begin errs++; $error("n[%0d]=%0d", j, n[j]); end
      if (seen0 != 1 || seen1 != 1 || genhits != 1) begin errs++; $error("seen %0d %0d %0d", seen0, seen1, genhits); end
      if (errs == 0) $display("PASSED");
   end
endmodule
