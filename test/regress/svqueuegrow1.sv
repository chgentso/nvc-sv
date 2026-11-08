// Growing a queue of structures once per clock must not exhaust the heap

module svqueuegrow1;
  typedef struct { real phase; real mag; logic signed [15:0] x, y, z; } d_t;
  reg clk = 0;
  always #5ns clk <= ~clk;
  d_t q[$], e;
  int j;
  initial begin
    for (j = 0; j < 20000; j++) begin
      @(posedge clk);
      e.x = j;
      q.push_back(e);
    end
    if (q.size() == 20000 && q[$].x == 19999 && q[100].x == 100)
      $display("PASSED");
    else
      $display("FAILED size=%0d", q.size());
    $finish;
  end
endmodule
