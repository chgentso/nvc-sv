// Arguments and local variables of a function and of a task that a statement
// breakpoint is in, through vpi_nvc_frame_variables.  The C side is
// test/vpi/vpi26.c; it needs --debug-stmts.

module vpi26;
  logic clk = 0;
  int total;

  always #5 clk = ~clk;

  function automatic int mix(int a, logic [7:0] b, real r, string s);
    int sum;
    logic [7:0] x;
    bit flag;
    sum = a + 1;
    x = b ^ 8'h0f;
    flag = 1;
    return sum + x;
  endfunction

  function automatic logic [99:0] wide(logic [99:0] v, byte c);
    logic [99:0] w;
    w = v << 1;
    return w;
  endfunction

  task automatic waiter(input int n, output int result);
    int i;
    logic [3:0] nib;
    nib = 4'b10x1;
    @(posedge clk);
    i = n * 2;
    result = i;
    @(posedge clk);
    total = total + i;
  endtask

  initial begin
    int r;
    logic [99:0] t;
    r = mix(5, 8'hf0, 2.5, "hey");
    r = mix(7, 8'h01, 1.5, "yo");
    t = wide(100'd3, -8'sd2);
    waiter(4, r);
  end

  initial #60 $finish;
endmodule
