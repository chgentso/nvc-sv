// Expected values: IEEE 1800-2017 8.4 / 8.8 (assignment of a derived handle to a base handle),
// 11.4.5 (equality operators on class handles: equal iff both refer to the same object, or
// both are null; comparison with null).
class base; endclass
class derived extends base; endclass
module svclasscmp1;
  derived d = new;
  base b;
  derived d2 = new;
  int errors = 0;
  initial begin
    b = d;
    if (d != b) begin $error("d != b true for the same object"); errors++; end
    if (!(b == d)) begin $error("b == d false for the same object"); errors++; end
    if (!(b != null)) begin $error("b != null false"); errors++; end
    if (b == null) begin $error("b == null true"); errors++; end
    if (d2 == b) begin $error("distinct objects compare equal"); errors++; end
    if (!(d2 != b)) begin $error("distinct objects compare not-equal false"); errors++; end
    b = null;
    if (b != null) begin $error("null handle != null"); errors++; end
    if (!(b == null)) begin $error("null handle == null false"); errors++; end
    if (errors == 0) $display("PASSED");
  end
endmodule
