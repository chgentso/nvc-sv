// Statement breakpoints of the nvc VPI extension in SystemVerilog: the
// callback runs before a statement on a marked line, in the initial or
// always block that executes it.  The C side is test/vpi/vpi19.c; it needs
// --debug-stmts.

module vpi19;
  logic [7:0] a;
  logic [7:0] b;

  initial begin
    a = 0;
    b = 0;
    #1;
    a = a + 1;
    #1;
    a = a + 1;
  end

  always begin
    #3;
    b = b + 1;
  end

  initial #7 $finish;
endmodule
