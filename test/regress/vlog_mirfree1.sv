// Units nested in a process are lowered after the process is compiled
module vlog_mirfree1;
   int r1, r2, r3;

   initial begin : blk
      automatic int a = 3;
      automatic int b = 4;
      #1;
      fork
         begin
            #1 r1 = a + b;
         end
         begin : named
            automatic int c = a * 2;
            #2 r2 = c + b;
         end
      join
      begin : later
         automatic int d = a + 10;
         #1 r3 = d;
      end
   end

   always @(r3) begin : watch
      automatic int e = r1 + r2 + r3;
      fork
         #1 if (e == 7 + 10 + 13) $display("PASSED");
            else $display("FAILED %0d", e);
      join_none
   end
endmodule
