// Every branch scope has the line of its own if, else if, else or case item

module vlogcover11;
   reg clk = 0;
   reg [2:0] sel = 0;
   reg [3:0] y;

   always #5 clk = ~clk;

   always @(posedge clk) begin
      sel <= sel + 1;

      if (sel == 0)
         y = 1;
      else if (sel == 1)
         y = 2;
      else if (sel == 2)
         y = 3;
      else
         y = 4;

      if (sel[0]) begin
         if (sel[1])
            y = 5;
         else if (sel[2])
            y = 6;
      end
      else
         y = 7;

      case (sel)
         0: y = 8;
         1, 2:
            y = 9;
      endcase
   end

   initial begin
      #32;
      $display("PASSED");
      $finish;
   end
endmodule
