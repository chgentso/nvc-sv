// IEEE 1800-2017 25.5: a modport restricts the access of a module to the
// interface items it lists, and an item listed as an input cannot be
// assigned through it (23.3.3.2).
interface modportin1_if;
  logic a, b;
  logic [3:0] v;
  modport mp(input a, v, output b);
endinterface

module modportin1(modportin1_if.mp p);
  assign p.a = 1;                       // Error
  initial begin
    p.a = 1;                            // Error
    p.v[2] <= 1;                        // Error
    p.b = p.a;                          // OK
  end
endmodule
