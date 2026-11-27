// Many partial writes into a large memory split its nexus: each split
// must find the nexus containing an offset in constant time and the
// values and events must stay exact for any split order and alignment
module store #(parameter AW = 14, DESC = 1)
   (input clk, input [AW-1:0] a, input [7:0] d, input we,
    output reg [7:0] q);
   reg [7:0] dmem [(2**AW)-1:0];
   reg [7:0] amem [0:(2**AW)-1];
   always @(posedge clk)
     if (we) begin
        if (DESC) dmem[a] <= d; else amem[a] <= d;
     end
     else
       q <= DESC ? dmem[a] : amem[a];
endmodule

module vlog_nexus_index1;
   reg clk = 0;
   reg [13:0] a = 0;
   reg [7:0] d = 0;
   reg we = 0;
   wire [7:0] q0, q1;
   integer i, sum, ev0, ev1, ev2;

   store #(.DESC(1)) s0 (clk, a, d, we, q0);
   store #(.DESC(0)) s1 (clk, a, d, we, q1);

   initial begin
      ev0 = 0; ev1 = 0; ev2 = 0;
   end

   always @(s0.dmem[100]) ev0 = ev0 + 1;
   always @(s1.amem[16383]) ev1 = ev1 + 1;
   always @(s0.dmem[7]) ev2 = ev2 + 1;

   task read(input [13:0] addr);
      begin
         a = addr; we = 0;
         #1 clk = 1; #1 clk = 0;
      end
   endtask

   initial begin
      // Whole elements in increasing index order: decreasing offsets
      // for the descending memory, increasing for the other
      for (i = 0; i < 2**14; i = i + 1) begin
         s0.dmem[i][7:0] = i[7:0];
         s1.amem[i][7:0] = i[10:3];
      end
      #1;
      // Unaligned bit slices split the elements further
      for (i = 0; i < 2**14; i = i + 3) begin
         s0.dmem[i][5:3] = 3'b101;
         s1.amem[i][6:6] = 1'b1;
      end
      #1;
      // Decreasing order with a different stride
      for (i = 2**14 - 1; i >= 0; i = i - 5) begin
         s0.dmem[i][1:0] = 2'b10;
         s1.amem[i][7:4] = 4'h9;
      end
      #1;
      sum = 0;
      for (i = 0; i < 2**14; i = i + 1)
         sum = sum + s0.dmem[i] + 3 * s1.amem[i];
      $display("sum %0d", sum);
      $display("events %0d %0d %0d", ev0, ev1, ev2);
      read(100);  $display("q %h %h", q0, q1);
      read(7);    $display("q %h %h", q0, q1);
      read(16383); $display("q %h %h", q0, q1);
      // Write through the design, read back through the hierarchy
      a = 100; d = 8'h3c; we = 1;
      #1 clk = 1; #1 clk = 0;
      we = 0;
      #1 $display("mem %h %h events %0d %0d", s0.dmem[100], s1.amem[100],
                  ev0, ev1);
      s0.dmem[100][7:4] = 4'h1;
      s0.dmem[7] = 8'hee;
      #1 $display("mem %h %h events %0d %0d %0d", s0.dmem[100], s0.dmem[7],
                  ev0, ev1, ev2);
      read(100);  $display("q %h %h", q0, q1);
      $finish;
   end
endmodule
