// Expected values: IEEE 1800-2017 11.4.7 (logical implication -> and equivalence <-> in expressions:
// a -> b is (!a || b), a <-> b is (a -> b) && (b -> a); implication is right associative),
// 16.12 / 16.12.7 (property with -> inside a sequence expression; it never fails here)
module svlogimpl1;
  bit clk = 0; always #5 clk = ~clk;
  logic [2:0] hsize = 0; logic [3:0] w = 1; bit t = 1;
  int r;
  int errs = 0, fails = 0, covers = 0;
  initial begin
    r = (hsize == 0 -> $countones(w) == 1);
    if (r !== 1) begin errs++; $error("1: %0d", r); end
    r = (hsize == 1 -> $countones(w) == 5);
    if (r !== 1) begin errs++; $error("2: %0d", r); end
    r = (hsize == 1 <-> $countones(w) == 5);
    if (r !== 1) begin errs++; $error("3: %0d", r); end
    r = (hsize == 0 -> t -> 0);
    if (r !== 0) begin errs++; $error("4: %0d", r); end
    #50;
    if (fails != 0) begin errs++; $error("assertion failed %0d times", fails); end
    if (covers == 0) begin errs++; $error("property never evaluated true"); end
    if (errs == 0) $display("PASSED");
    $finish;
  end
  property p; @(posedge clk) t |-> ((hsize == 3'b000 -> $countones(w)== 1 )) || ((hsize ==3'b001 -> $countones(w)==2)); endproperty
  a: assert property (p) else fails++;
  c: cover property (p) covers++;
endmodule
