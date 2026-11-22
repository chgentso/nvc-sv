class A; rand bit [7:0] x[20];
 constraint c {  soft x[0] == 0; soft x[1] == 1; soft x[2] == 2; soft x[3] == 3; soft x[4] == 4; soft x[5] == 5; soft x[6] == 6; soft x[7] == 7; soft x[8] == 8; soft x[9] == 9; soft x[10] == 10; soft x[11] == 11; soft x[12] == 12; soft x[13] == 13; soft x[14] == 14; soft x[15] == 15; soft x[16] == 16; soft x[17] == 17; soft x[18] == 18; soft x[19] == 19; }
 function new;  endfunction endclass
module top; A a; int n, f; initial begin a = new; f = 0; void'($value$plusargs("N=%d", n));
 repeat (n) if (!a.randomize()) f++; $display("RES fails=%0d/%0d", f, n); $finish; end endmodule
