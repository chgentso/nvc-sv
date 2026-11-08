// IEEE 1800-2017 23.10.1: with several defparams for one parameter the
// last in the source text is used.  nvc warns about the conflict and
// names both defparams.
module svdefparam7_sub #(parameter int P = 1) ();
endmodule
module svdefparam7;
  svdefparam7_sub u();
  defparam u.P = 5;
  defparam u.P = 7;
  initial begin
    if (u.P == 7) $display("PASSED");
    else $display("FAILED %0d", u.P);
  end
endmodule
