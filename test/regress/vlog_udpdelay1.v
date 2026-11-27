`timescale 1ns/1ps
primitive inv (o, a);
  output o; input a;
  table
    0 : 1;
    1 : 0;
  endtable
endprimitive

primitive tff (q, clk, d);
  output q; reg q; input clk, d;
  table
    (01) 0 : ? : 0;
    (01) 1 : ? : 1;
    (?0) ? : ? : -;
    ?   (??) : ? : -;
  endtable
endprimitive

module sub (output y, input a);
  inv #3 u (y, a);   // Delay in the unit of this module
endmodule

module vlog_udpdelay1;
  reg a, clk, d;
  wire o1, o2, o3, o4, q, y;
  inv #(2) i1 (o1, a);
  inv #(1, 3) i2 (o2, a), i3 (o3, a);
  inv #1.5 i4 (o4, a);
  tff #(4) f1 (q, clk, d);
  sub s (y, a);
  initial $monitor("%t o1=%b o2=%b o3=%b o4=%b q=%b y=%b", $realtime,
                   o1, o2, o3, o4, q, y);
  initial begin
    a = 0; clk = 0; d = 1;
    #10 a = 1; clk = 1;
    #10 a = 0; clk = 0;
    #10 $finish;
  end
endmodule
