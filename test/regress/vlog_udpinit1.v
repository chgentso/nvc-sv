primitive dff1 (q, clk, d);
  output q; reg q; input clk, d;
  initial q = 1'b1;
  table
    (01) 0 : ? : 0;
    (01) 1 : ? : 1;
    (?0) ? : ? : -;
    ?   (??) : ? : -;
  endtable
endprimitive

primitive latch0 (q, en, d);
  output q; reg q; input en, d;
  initial q = 0;
  table
    1 0 : ? : 0;
    1 1 : ? : 1;
    0 ? : ? : -;
  endtable
endprimitive

module vlog_udpinit1;
  reg clk, d, en, d2;
  wire q1, q2;
  dff1 u1 (q1, clk, d);
  latch0 u2 (q2, en, d2);
  initial begin
    #0 $display("init q1=%b q2=%b", q1, q2);
    #1 $display("t1 q1=%b q2=%b", q1, q2);
    d = 0; en = 0; d2 = 1;
    #1 $display("t2 q1=%b q2=%b", q1, q2);
    clk = 0; #1 clk = 1; en = 1;
    #1 $display("t4 q1=%b q2=%b", q1, q2);
  end
endmodule
