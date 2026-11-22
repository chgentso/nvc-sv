class A; rand bit [7:0] d[]; rand bit [5:0] n;
 constraint c { d.size() == n; n inside {[1:40]}; foreach (d[i]) d[i] > i; }
 function new;  endfunction endclass
module top; A a; int n, f; initial begin a = new; f = 0; void'($value$plusargs("N=%d", n));
 repeat (n) if (!a.randomize()) f++; $display("RES fails=%0d/%0d", f, n); $finish; end endmodule
