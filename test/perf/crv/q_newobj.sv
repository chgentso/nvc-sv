class Item; rand bit [31:0] addr; rand bit [7:0] len; rand bit [1:0] kind; constraint c { addr[1:0] == 0; len inside {[1:16]}; kind != 3; (kind == 0) -> len == 1; } endclass
module top; Item a; int n, f; initial begin f = 0; void'($value$plusargs("N=%d", n));
 repeat (n) begin a = new; if (!a.randomize() with { addr < 32'h1000; }) f++; end $display("RES fails=%0d/%0d", f, n); $finish; end endmodule
