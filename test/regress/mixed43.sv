`timescale 1ns/1ns
// force and release of Verilog variables, nets, and VHDL signals
// Expected values: IEEE 1800-2017 10.6.2 (force and release of variables and
// nets: a forced variable keeps the forced value, procedural assignments do not
// change it, release of a variable leaves the forced value until the next
// procedural assignment; a forced net ignores its drivers and returns to the
// driven value on release), 6.5 (net driven by a continuous assignment),
// 9.4.1 (#delay), 4.2.
// Force of a part-select of a variable is illegal under 10.6.2 and is not
// used.  The VHDL side (counter q counting rising edges of clk at 5, 15, 25, ...
// from 0) follows IEEE 1076; a Verilog force of a VHDL signal keeps its forced
// value while the force is active.  The value of a forced VHDL signal after
// release (and of the signal that depends on it) is implementation-defined and
// is not checked.
module mixed43_sub(input wire d, output wire y);
  assign y = d;
endmodule
module mixed43;
  reg clk = 0;
  reg [7:0] r = 8'h11;
  wire n;
  reg drv = 0;
  wire [3:0] q;
  int errors = 0;
  mixed43_sub s(.d(drv), .y(n));
  mixed43_e u(.clk(clk), .q(q));
  always #5 clk = ~clk;
  task automatic chk8(string nm, logic [7:0] got, logic [7:0] exp);
    if (got !== exp) begin $error("%s: got %h expected %h", nm, got, exp); errors++; end
  endtask
  task automatic chk4(string nm, logic [3:0] got, logic [3:0] exp);
    if (got !== exp) begin $error("%s: got %h expected %h", nm, got, exp); errors++; end
  endtask
  task automatic chk1(string nm, logic got, logic exp);
    if (got !== exp) begin $error("%s: got %b expected %b", nm, got, exp); errors++; end
  endtask
  initial begin
    #1 force r = 8'hAA; chk8("a", r, 8'hAA);
    r = 8'h55; #1 chk8("b", r, 8'hAA);
    release r; #1 chk8("c", r, 8'hAA);
    r = 8'h77; #1 chk8("d", r, 8'h77);
    force n = 1'b1; #1 chk1("e", n, 1'b1);
    drv = 1; #1 drv = 0; #1 chk1("f", n, 1'b1);
    release n; #1 chk1("g", n, 1'b0);
    #1;
    #20 chk4("i", q, 4'h3);
    force u.cnt = 4'h9; #1 chk4("j", q, 4'h9);
    #20 chk4("k", q, 4'h9);
    release u.cnt; #1;
    #20;
    force u.flag = 1'b1; #1 chk1("n", u.flag, 1'b1);
    release u.flag; #1;
    if (errors == 0) $display("PASSED");
    $finish;
  end
endmodule
