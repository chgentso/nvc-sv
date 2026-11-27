// Wide replications are built by doubling one copy: check the bits for
// any count, unit width and value, 4-state and 2-state
module vlog_replicate_wide1;
   reg        b;
   reg [1:0]  p;
   reg [2:0]  q;
   reg [69:0] w;
   bit [4:0]  t;
   reg [1022:0] r1;
   reg [299:0]  r2;
   reg [209:0]  r3;
   reg [4607:0] r4;
   bit [64:0]   r5;
   reg [99:0]   r6;
   reg [127:0]  r7;
   integer i, ones, xs, zs;

   task count(input [4607:0] v, input integer n);
      begin
         ones = 0; xs = 0; zs = 0;
         for (i = 0; i < n; i = i + 1)
           if (v[i] === 1'b1) ones = ones + 1;
           else if (v[i] === 1'bx) xs = xs + 1;
           else if (v[i] === 1'bz) zs = zs + 1;
         $display("%0d ones %0d x %0d z", ones, xs, zs);
      end
   endtask

   initial begin
      b = 1; p = 2'b1x; q = 3'b10z; t = 5'b10110;
      w = {1'b1, 68'h0, 1'b1};
      r1 = {1023{b}};            count(r1, 1023);
      r1 = {1023{1'b0}} | 1;     count(r1, 1023);
      r2 = {100{p, b}};          count(r2, 300);
      $display("%h", r2[17:0]);
      r3 = {3{w}};               count(r3, 210);
      $display("%b", r3[71:68]);
      r4 = {768{q, q}};          count(r4, 4608);
      $display("%b", r4[11:0]);
      r5 = {13{t}};              $display("%h", r5);
      r6 = {33{p, b}};           count(r6, 100);
      $display("%b", r6[99:93]);
      r7 = {5{q, 1'b0}} ^ {16{8'h81}};
      $display("%h", r7);
      r1 = {341{q}};             count(r1, 1023);
      $display("%b", r1[1022:1014]);
      b = 1'bx;
      r1 = ~{1023{b}};           count(r1, 1023);
   end
endmodule
