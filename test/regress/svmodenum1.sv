// Enum-typed parameter of a module and the name() of a variable initialised from it
// Expected values: IEEE 1800-2017 6.19.5.6 (name() returns the constant's identifier), 23.10 (parameter override
// and default value)
typedef enum {SV32, SV39, SV48} sm_t;
module sub #(sm_t M = SV39);
  sm_t mode = M;
  string nm;
  initial nm = mode.name();
endmodule
module svmodenum1;
  sub #(SV48) s1();
  sub s2();
  int fails = 0;
  initial begin
    #1;
    if (s1.nm != "SV48") begin $error("s1: got %s", s1.nm); fails++; end
    if (s2.nm != "SV39") begin $error("s2: got %s", s2.nm); fails++; end
    if (s1.mode != SV48 || s2.mode != SV39) begin $error("wrong mode value"); fails++; end
    if (fails == 0) $display("PASSED");
  end
endmodule
