// A bind into a module type that nothing instantiates has no effect and is
// not an error (IEEE 1800-2017 23.11); the other targets still bind.
module tgt(input logic c);
endmodule

module unused(input logic c);
endmodule

module chk #(parameter string N = "") (input logic c);
  initial $display("chk %s %m", N);
endmodule

module binder;
  bind unused chk #("unused") u_unused(.*);
  bind tgt chk #("tgt") u_tgt(.*);
endmodule

module svbindnoinst1;
  logic c;
  tgt t(.c(c));
endmodule
