// Real parameter overrides of a VHDL entity
module mixed60;
  mixed60_e #(.P(4), .Q(1.25), .S(2.5)) u1 ();
  mixed60_e #(.P(2.5), .Q(1.0), .S(0.5)) u2 ();
endmodule
