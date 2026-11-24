interface if_p #(
   parameter int W = 8,
   parameter type T = logic
) (
   input logic clk
);
   localparam int LP = W * 2;
   logic sig;
   task foo(); endtask
endinterface

module sub_p #(parameter int A = 1);
   localparam int B = 2;
endmodule

module vlogparam1;
   logic clk;
   if_p #(.W(16), .T(int)) ok_if (clk);
   if_p #(.LP(4)) bad_lp (clk);
   if_p #(.sig(1)) bad_sig (clk);
   if_p #(.foo(1)) bad_task (clk);
   if_p #(.nope(1)) bad_none (clk);
   sub_p #(.B(3)) bad_mod_lp ();
   sub_p #(.missing(3)) bad_mod_none ();
endmodule
