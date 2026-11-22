module svforkescape1;
  logic clk = 0;
  always #5 clk = ~clk;

  logic [7:0] observed = 0;
  int seen = 0;

  task automatic start_responder();
    logic [7:0] sr = 8'h00;
    fork
      begin
        @(posedge clk);
        sr = 8'h5a;
        @(posedge clk);
        sr = 8'ha5;
      end
      begin
        @(sr);
        observed = sr;
        seen++;
        @(sr);
        observed = sr;
        seen++;
      end
    join_none
  endtask

  task automatic clobber_stack(int depth);
    logic [7:0] junk [8];
    for (int i = 0; i < 8; i++)
      junk[i] = 8'hff - 8'(depth + i);
    if (depth > 0)
      clobber_stack(depth - 1);
    else
      repeat (3) @(posedge clk);
  endtask

  initial begin
    start_responder();
    clobber_stack(8);
    #1;
    if (seen != 2 || observed !== 8'ha5)
      $fatal(1, "seen=%0d observed=%h", seen, observed);
    $display("PASSED");
    $finish;
  end
endmodule
