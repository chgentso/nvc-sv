class A; rand bit [1023:0] m; bit [9:0] n = 3; int width = 39;
 constraint c { $countones(m) == n; (m >> width) == '0; }
 function new;  endfunction endclass
module top; A a; int n, f; initial begin a = new; f = 0; void'($value$plusargs("N=%d", n));
 repeat (n) if (!a.randomize()) f++; $display("RES fails=%0d/%0d", f, n); $finish; end endmodule
