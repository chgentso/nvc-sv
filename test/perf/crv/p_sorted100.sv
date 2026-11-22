class A; rand bit [31:0] d[];
 constraint c { d.size() == 100; foreach (d[i]) if (i > 0) d[i] > d[i-1]; }
 function new;  endfunction endclass
module top; A a; int n, f; initial begin a = new; f = 0; void'($value$plusargs("N=%d", n));
 repeat (n) if (!a.randomize()) f++; $display("RES fails=%0d/%0d", f, n); $finish; end endmodule
