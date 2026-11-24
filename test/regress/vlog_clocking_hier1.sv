module cb_sub;
   bit clk = 1'b0;
   bit inp = 1'b0;
   always #1 clk = ~clk;
   always @(posedge clk) inp <= 1'b1;

   clocking cb @(posedge clk);
      input #1 inp;
   endclocking
endmodule

module vlog_clocking_hier1;
   bit clk = 1'b0;
   bit d = 1'b0;
   bit out_q;
   always #2 clk = ~clk;

   // Forward reference to clocking block in same module
   assign out_q = local_cb.d;

   clocking local_cb @(posedge clk);
      input #0 d;
   endclocking

   cb_sub u1 ();

   initial begin
      #1;
      if (vlog_clocking_hier1.u1.cb.inp !== 1'b0) $fatal(1, "FAIL 1");
      d = 1'b1;
      #4;
      if (vlog_clocking_hier1.u1.cb.inp !== 1'b1) $fatal(1, "FAIL 2");
      if (out_q !== 1'b1) $fatal(1, "FAIL 3");
      $display("PASSED");
      $finish;
   end
endmodule
