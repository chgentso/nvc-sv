// Scope randomize without the std:: prefix
// Expected values: IEEE 1800-2017 18.12 (scope randomize function: returns 1 on success; inline constraints on the
// listed variables), 13.4.1 (non-void function call as a statement)
module svscoperand1;
  bit [7:0] fan_status = 0;
  int unsigned temps[3];
  int a, b;
  int err = 0;
  initial begin
    int ok;
    ok = randomize(fan_status);
    if (ok !== 1) begin $error("randomize(fan_status)"); err++; end
    ok = randomize(temps);
    if (ok !== 1) begin $error("randomize(temps)"); err++; end
    ok = randomize(a, b) with { a > 10; a < 20; b == a + 1; };
    if (ok !== 1) begin $error("randomize(a, b) with"); err++; end
    if (b - a != 1) begin $error("b == a + 1 violated"); err++; end
    if (!(a > 10 && a < 20)) begin $error("a out of range"); err++; end
    void'(randomize(fan_status));
    randomize(b);
    if (err == 0) $display("PASSED");
  end
endmodule
