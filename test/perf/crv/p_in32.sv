class A; rand bit [31:0] x;
 constraint c { x inside {[32'h1000:32'h1fff], [32'h8000_0000:32'h8fff_ffff], 5, 7}; }
 function new;  endfunction endclass
module top; A a; int n, f; initial begin a = new; f = 0; void'($value$plusargs("N=%d", n));
 repeat (n) if (!a.randomize()) f++; $display("RES fails=%0d/%0d", f, n); $finish; end endmodule
