class A; rand bit [7:0] v[4];
 constraint c {  (v[0] > 100) -> (v[1] > 100 && v[1] < 200); (v[1] > 100) -> (v[2] > 100 && v[2] < 200); (v[2] > 100) -> (v[3] > 100 && v[3] < 200); v[0] > 100; }
 function new;  endfunction endclass
module top; A a; int n, f; initial begin a = new; f = 0; void'($value$plusargs("N=%d", n));
 repeat (n) if (!a.randomize()) f++; $display("RES fails=%0d/%0d", f, n); $finish; end endmodule
