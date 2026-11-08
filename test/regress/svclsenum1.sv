// Expected values: IEEE 1800-2017 8.25 (parameterized class with an enum-typed value
// parameter, default value SV39), 6.19.5.6 (enum name()), 6.20.2 (localparam computed
// from the parameter), 11.4.11 (?:).
typedef enum {SV32, SV39, SV48} sm_t;
class pt #(sm_t MODE = SV39);
  localparam int Lv = (MODE == SV32) ? 2 : ((MODE == SV39) ? 3 : 4);
  sm_t mode = MODE;
endclass
module svclsenum1;
  int errors = 0;
  initial begin
    pt#(SV32) a; pt#() b; pt#(SV48) c; a = new; b = new; c = new;
    if (a.mode.name() != "SV32") begin $error("a.mode = %s", a.mode.name()); errors++; end
    if (b.mode.name() != "SV39") begin $error("b.mode = %s", b.mode.name()); errors++; end
    if (c.mode.name() != "SV48") begin $error("c.mode = %s", c.mode.name()); errors++; end
    if (a.Lv !== 2) begin $error("a.Lv = %0d", a.Lv); errors++; end
    if (b.Lv !== 3) begin $error("b.Lv = %0d", b.Lv); errors++; end
    if (c.Lv !== 4) begin $error("c.Lv = %0d", c.Lv); errors++; end
    if (errors == 0) $display("PASSED");
  end
endmodule
