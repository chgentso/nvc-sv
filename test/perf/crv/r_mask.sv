class A; rand bit [31:0] x;
 constraint c { (x & 32'h0ff0_0f00) == 32'h0550_0a00; (x | 32'hffff0000) == 32'hffff1234; }
 function new;  endfunction endclass
module top; A a; int n, f; initial begin a = new; f = 0; void'($value$plusargs("N=%d", n));
 repeat (n) if (!a.randomize()) f++; $display("RES fails=%0d/%0d", f, n); $finish; end endmodule
