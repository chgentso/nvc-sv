// Self-checking. IEEE 1800-2017 25.5.4: a modport expression of an interface
// with a parameter names a member whose size depends on the parameter.
interface svmodportexpr2_if #(parameter W = 1);
  logic [W-1:0] sig_a, sig_b;
  modport mp1(input .a(sig_a), output .b(sig_b));
endinterface

module svmodportexpr2_sub(svmodportexpr2_if.mp1 i);
  assign i.b = i.a;
endmodule

module svmodportexpr2;
  svmodportexpr2_if #(8) f();
  assign f.sig_a = 8'h42;
  svmodportexpr2_sub s(f.mp1);
  initial begin
    #1;
    if (f.sig_b !== 8'h42) $error("FAIL %h", f.sig_b);
    else $display("PASSED");
  end
endmodule
