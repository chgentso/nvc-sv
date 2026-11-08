module leaf #(parameter A = 1) ();
  initial $display("A=%0d", A);
endmodule
module svdefparam3;
  leaf l();
  defparam nosuch.A = 3;
  defparam l.NOPE = 4;
  defparam l.A = 5;
endmodule
