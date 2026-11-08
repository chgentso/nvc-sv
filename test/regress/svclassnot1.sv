// Expected values: IEEE 1800-2017 8.4, 11.4.7: logical negation of a class handle is 1 for a
// null handle and 0 for a non-null handle; a handle used as an if condition is true iff non-null.
class svclassnot1_c; int x = 3; endclass
module svclassnot1;
  svclassnot1_c c, d;
  int errors = 0;
  initial begin
    d = new;
    if (!(!c)) begin $error("!c false for null"); errors++; end
    if (!d) begin $error("!d true for non-null"); errors++; end
    if (c) begin $error("if (c) taken for null"); errors++; end
    if (!d) begin $error("if (!d) taken"); errors++; end
    if (!c !== 1'b1) begin $error("!c = %b, expected 1", !c); errors++; end
    if (!d !== 1'b0) begin $error("!d = %b, expected 0", !d); errors++; end
    if (errors == 0) $display("PASSED");
  end
endmodule
