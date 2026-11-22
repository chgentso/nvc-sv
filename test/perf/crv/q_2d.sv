class A; rand bit [7:0] m[16][16];
 constraint c { foreach (m[i,j]) { m[i][j] inside {[1:9]}; if (j > 0) m[i][j] != m[i][j-1]; if (i > 0) m[i][j] != m[i-1][j]; } }
 function new;  endfunction endclass
module top; A a; int n, f; initial begin a = new; f = 0; void'($value$plusargs("N=%d", n));
 repeat (n) if (!a.randomize()) f++; $display("RES fails=%0d/%0d", f, n); $finish; end endmodule
