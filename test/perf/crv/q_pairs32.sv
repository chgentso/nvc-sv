class A; rand bit [15:0] d[32];
 constraint c { foreach (d[i]) foreach (d[j]) if (i < j) d[i] != d[j]; }
 function new;  endfunction endclass
module top; A a; int n, f; initial begin a = new; f = 0; void'($value$plusargs("N=%d", n));
 repeat (n) if (!a.randomize()) f++; $display("RES fails=%0d/%0d", f, n); $finish; end endmodule
