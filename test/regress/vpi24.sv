// The call stack of the nvc VPI extension in SystemVerilog: the frames of a
// statement callback in a function called by a task called by an initial
// block, and the lines that have a statement (also in subprograms that have
// not been called yet).  The C side is test/vpi/vpi24.c; it needs
// --debug-stmts.

module vpi24;
  logic [7:0] a;

  function automatic int sq(int x);
    return x * x;
  endfunction

  // Not called before the check
  function automatic int later(int x);
    return x + 1;
  endfunction

  task automatic work(int x);
    int r;
    r = sq(x);
    a = r[7:0];
  endtask

  initial begin
    a = 0;
    work(3);
    #1;
    work(4);
    a = later(a);
  end

  initial #5 $finish;
endmodule
