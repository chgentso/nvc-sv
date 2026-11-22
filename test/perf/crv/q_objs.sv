class Item; rand bit [31:0] addr; rand bit [7:0] len; rand bit [1:0] kind; constraint c { addr[1:0] == 0; len inside {[1:16]}; kind != 3; (kind == 0) -> len == 1; } endclass
class A; rand Item it[]; rand bit [7:0] total; constraint c { it.size() == 100; foreach (it[i]) if (i > 0) it[i].addr > it[i-1].addr; }
 function void pre_randomize(); if (it.size() == 0) begin it = new[100]; foreach (it[i]) it[i] = new; end endfunction endclass
module top; A a; int n, f; initial begin a = new; f = 0; void'($value$plusargs("N=%d", n));
 repeat (n) if (!a.randomize()) f++; $display("RES fails=%0d/%0d", f, n); $finish; end endmodule
