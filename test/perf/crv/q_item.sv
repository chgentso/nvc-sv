class A; rand bit [31:0] addr; rand bit [31:0] data; rand bit [3:0] mask; rand bit [2:0] size; rand bit write; rand bit [7:0] len; rand bit [1:0] burst; rand bit [3:0] id; rand bit [7:0] delay; rand bit lock;
 constraint c { addr inside {[32'h1000:32'h1fff], [32'h4000_0000:32'h4000_ffff]}; size <= 2; (addr & ((32'd1 << size) - 32'd1)) == 0; write -> (mask != 0); !write -> (mask == 0); len inside {[0:15]}; burst != 3; delay dist {0 := 5, [1:10] :/ 3, [11:255] :/ 1}; solve write before mask; (size == 0) -> ($countones(mask) <= 1); lock -> len == 0; id < 8; }
 function new;  endfunction endclass
module top; A a; int n, f; initial begin a = new; f = 0; void'($value$plusargs("N=%d", n));
 repeat (n) if (!a.randomize()) f++; $display("RES fails=%0d/%0d", f, n); $finish; end endmodule
