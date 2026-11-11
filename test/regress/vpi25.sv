// Depth-aware stepping of the nvc VPI extension in SystemVerilog: over a
// function, over a task that waits for a clock edge while another process
// runs, a breakpoint inside it, and into the task and out of it.  The C side
// is test/vpi/vpi25.c; it needs --debug-stmts.

module vpi25;
  logic clk = 0;
  int a, b, c;

  always #5 clk = ~clk;

  function automatic int sq(int x);
    return x * x;
  endfunction

  task automatic waitclk(int n);
    @(posedge clk);
    a = a + n;
    @(posedge clk);
    a = a + n;
  endtask

  initial begin
    a = 0;
    b = sq(3);
    waitclk(1);
    b = b + 1;
    waitclk(2);
    b = b + 2;
    waitclk(3);
    b = b + 3;
  end

  initial begin
    #3 c = c + 1;
    #3 c = c + 1;
    #3 c = c + 1;
    #3 c = c + 1;
  end

  initial #60 $finish;
endmodule
