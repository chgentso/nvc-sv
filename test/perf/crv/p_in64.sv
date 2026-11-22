class A; rand bit [63:0] x, y;
 constraint c { x inside {[64'h1000:64'h1fff], [64'h8000_0000_0000_0000:64'h8fff_ffff_ffff_ffff]}; y inside {[x:x+100]}; }
 function new;  endfunction endclass
module top; A a; int n, f; initial begin a = new; f = 0; void'($value$plusargs("N=%d", n));
 repeat (n) if (!a.randomize()) f++; $display("RES fails=%0d/%0d", f, n); $finish; end endmodule
