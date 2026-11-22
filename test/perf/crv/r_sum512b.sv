class A; rand bit [15:0] d[];
 constraint c { d.size() == 512; d.sum() with (32'(item)) == 51200; foreach (d[i]) d[i] < 1000; }
 function new;  endfunction endclass
module top; A a; int n, f; initial begin a = new; f = 0; void'($value$plusargs("N=%d", n));
 repeat (n) if (!a.randomize()) f++; $display("RES fails=%0d/%0d", f, n); $finish; end endmodule
