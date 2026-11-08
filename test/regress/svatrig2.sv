// IEEE 1800-2017 16.13.6: s.triggered is true only in the time step in
// which the sequence s matched.  A process that waits for it, records the
// time in a queue and waits for it to be false again sees each match once,
// and the flag is false at every falling clock edge.
module svatrig2;
  logic clk = 0, a = 0;
  always #5 clk = ~clk;
  sequence s; @(posedge clk) a; endsequence
  int q[$];
  int errs, nneg;
  initial forever begin
    wait (s.triggered);
    q.push_back($time);
    wait (!s.triggered);
  end
  always @(negedge clk) begin
    nneg++;
    if (s.triggered) begin $error("s.triggered still true at %0t", $time); errs++; end
  end
  initial begin
    #12 a = 1;
    #10 a = 0;
    #20 a = 1;
    #10 a = 0;
    #20;
    if (q.size() != 2 || q[0] != 15 || q[1] != 45) begin
      $error("matches seen at %p, expected '{15, 45}", q); errs++;
    end
    if (nneg != 7) begin $error("%0d falling edges", nneg); errs++; end
    if (errs == 0) $display("PASSED");
    $finish;
  end
endmodule
