// defparam and instance parameter overrides, in nested instances.
// Expected values: IEEE 1800-2017 23.10.1 (defparam overrides the value given by an instance parameter
// assignment; with several defparams for one parameter the last in source text wins), 23.10.2 (#() overrides),
// 23.6 (hierarchical references to parameters). Each leaf records the time its parameter-dependent
// delay expired, so the delay is checked too. Hierarchical-name text (%m) is not checked.
module leaf #(parameter A = 1, parameter B = 2, parameter C = 3) (input clk);
   time fired = 0;
   initial begin #(A*10000 + B*100 + C + 500); fired = $time; end
endmodule

module mid #(parameter W = 8) (input clk);
   leaf #(.A(W)) l1(clk);
   leaf #(10, 20) l2(clk);
   defparam l1.B = 55;
endmodule

module svdefparam1;
   reg clk = 0;
   localparam int K = 4 * 5;
   mid m1(clk);
   mid #(.W(3)) m2(clk);
   leaf lf(clk);
   leaf #(.A(7)) lg(clk);
   leaf #(5, 6, 7) lh(clk);
   defparam m1.l1.A = 100;
   defparam m1.l2.C = K + 1, m1.l2.A = 200;
   defparam m2.l2.B = 77;
   defparam lf.A = 11;
   defparam lf.B = 12;
   defparam lg.A = 13;
   defparam lh.C = -9;
  int errs = 0;
  task automatic chk(string what, int got, int exp);
    if (got !== exp) begin errs++; $error("%s: got %0d, expected %0d", what, got, exp); end
  endtask
   task automatic leafchk(string n, int a, int b, int c, int ea, int eb, int ec, time f);
     chk({n, ".A"}, a, ea); chk({n, ".B"}, b, eb); chk({n, ".C"}, c, ec);
     chk({n, " fired"}, int'(f), ea*10000 + eb*100 + ec + 500);
   endtask
   initial begin
     #3000000;
     leafchk("m1.l1", m1.l1.A, m1.l1.B, m1.l1.C, 100, 55, 3, m1.l1.fired);
     leafchk("m1.l2", m1.l2.A, m1.l2.B, m1.l2.C, 200, 20, 21, m1.l2.fired);
     leafchk("m2.l1", m2.l1.A, m2.l1.B, m2.l1.C, 3, 55, 3, m2.l1.fired);
     leafchk("m2.l2", m2.l2.A, m2.l2.B, m2.l2.C, 10, 77, 3, m2.l2.fired);
     leafchk("lf", lf.A, lf.B, lf.C, 11, 12, 3, lf.fired);
     leafchk("lg", lg.A, lg.B, lg.C, 13, 2, 3, lg.fired);
     leafchk("lh", lh.A, lh.B, lh.C, 5, 6, -9, lh.fired);
     if (errs == 0) $display("PASSED");
   end
endmodule
