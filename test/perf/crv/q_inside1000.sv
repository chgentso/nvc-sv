class A; rand bit [31:0] x; int vals[$];
 constraint c { x inside {vals}; }
 function new; for (int i = 0; i < 1000; i++) vals.push_back(i * 7 + 3); endfunction endclass
module top; A a; int n, f; initial begin a = new; f = 0; void'($value$plusargs("N=%d", n));
 repeat (n) if (!a.randomize()) f++; $display("RES fails=%0d/%0d", f, n); $finish; end endmodule
