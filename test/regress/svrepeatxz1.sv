module svrepeatxz1;
   function automatic bit run_u(input [64:0] n);
      run_u = 1'b0;
      repeat (n) begin
         run_u = 1'b1;
         break;
      end
   endfunction

   function automatic bit run_s(input signed [64:0] n);
      run_s = 1'b0;
      repeat (n) begin
         run_s = 1'b1;
         break;
      end
   endfunction

   localparam bit P1 = run_u(65'bx);
   localparam bit P2 = run_u({1'bx, 64'd1});
   localparam bit P3 = run_s({1'bz, 64'd1});

   initial begin
      automatic logic [64:0] c;
      automatic int hits = 0;

      c = 65'bx;            repeat (c) hits++;
      c = 65'bz;            repeat (c) hits++;
      c = 65'b1x;           repeat (c) hits++;
      c = {1'bx, 64'd1};    repeat (c) hits++;
      c = {1'bz, 64'd1};    repeat (c) hits++;
      c = 65'd3;            repeat (c) hits++;

      if (hits != 3 || P1 !== 1'b0 || P2 !== 1'b0 || P3 !== 1'b0)
         $fatal(1, "hits=%0d P=%b%b%b", hits, P1, P2, P3);

      $display("PASSED");
   end
endmodule
