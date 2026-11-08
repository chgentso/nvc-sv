// Expected values: IEEE 1800-2017 6.19.4 (cast to enum), 6.19.5 (first/next/num), 6.20.3 (type parameters), 6.8
module m #(parameter type StateEnumT = logic [1:0]) (input StateEnumT sig, output logic bad);
  function automatic logic undef(StateEnumT s);
    logic d = 1'b0;
    for (int i = 0, StateEnumT t = t.first(); i < t.num(); i += 1, t = t.next()) d |= (s === t);
    return ~d;
  endfunction
  assign bad = undef(sig);
endmodule
module svtypeparamenumself1;
  typedef enum logic [1:0] {A=0, B=1, C=2} e_t;
  e_t s = B; logic b;
  int errs = 0;
  m #(e_t) u(s, b);
  initial begin
    #1;
    // B is a member of e_t: not undefined
    if (b !== 1'b0) begin errs++; $error("member B reported undefined: b=%b", b); end
    s = e_t'(3); #1;
    // 2'b11 is not an enumeration member: undefined
    if (b !== 1'b1) begin errs++; $error("value 3 not reported undefined: b=%b", b); end
    if (errs == 0) $display("PASSED");
  end
endmodule
