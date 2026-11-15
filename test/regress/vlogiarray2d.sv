// An array of instances may have more than one unpacked dimension
// (IEEE 1800-2017 23.3.2); an element is selected with an index for each.

interface simple_if;
   logic [7:0] data;
endinterface

module leaf (output logic [7:0] o);
   assign o = 8'h40;
endmodule

module sink (simple_if b[1:0][2:0], output logic [7:0] sum);
   always_comb sum = b[0][0].data + b[1][2].data;
endmodule

module vlogiarray2d;
   localparam int A = 2;
   localparam int B = 3;

   simple_if bus[A-1:0][0:B-1] ();
   leaf l[1:2][4:5] (.o());

   genvar gi, gj;
   for (gi = 0; gi < A; gi++) begin : g_a
      for (gj = 0; gj < B; gj++) begin : g_b
         initial bus[gi][gj].data = 8'(gi * B + gj + 1);
      end
   end

   simple_if neg[1:-1][-2:0] ();
   simple_if pb[1:0][2:0] ();
   logic [7:0] sum;
   sink snk (.b(pb), .sum);

   initial begin
      neg[-1][-2].data = 8'd5;
      neg[1][0].data = 8'd7;
      pb[0][0].data = 8'd1;
      pb[1][2].data = 8'd2;
   end

   logic [7:0] chk [A-1:0][B-1:0];
   for (gi = 0; gi < A; gi++) begin : g_c
      for (gj = 0; gj < B; gj++) begin : g_d
         always_comb chk[gi][gj] = bus[gi][gj].data;
      end
   end

   initial begin
      #1;
      for (int i = 0; i < A; i++)
         for (int j = 0; j < B; j++)
            if (chk[i][j] !== 8'(i * B + j + 1))
               $fatal(1, "bus[%0d][%0d] = %0d", i, j, chk[i][j]);
      if (neg[-1][-2].data !== 8'd5 || neg[1][0].data !== 8'd7)
         $fatal(1, "negative indices");
      if (sum !== 8'd3) $fatal(1, "sum %0d", sum);
      if (l[2][4].o !== 8'h40 || l[1][5].o !== 8'h40)
         $fatal(1, "leaf");
      $display("PASSED");
   end
endmodule
