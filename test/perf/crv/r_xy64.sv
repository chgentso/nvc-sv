class A; rand bit [63:0] x, y, z;
 constraint c { x + y == z; z[7:0] == 8'h55; x[63:60] == 4'ha; y > x; }
 function new;  endfunction endclass
module top; A a; int n, f; initial begin a = new; f = 0; void'($value$plusargs("N=%d", n));
 repeat (n) if (!a.randomize()) f++; $display("RES fails=%0d/%0d", f, n); $finish; end endmodule
