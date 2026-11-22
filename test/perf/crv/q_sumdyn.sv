class A; rand bit [7:0] d[];
 constraint c { d.size() inside {[1:64]}; foreach (d[i]) d[i] inside {[1:16]}; d.sum() with (int'(item)) == 200; }
 function new;  endfunction endclass
module top; A a; int n, f; initial begin a = new; f = 0; void'($value$plusargs("N=%d", n));
 repeat (n) if (!a.randomize()) f++; $display("RES fails=%0d/%0d", f, n); $finish; end endmodule
