// Result width and value of $onehot, $onehot0, $isunknown and $countones
// Expected values: IEEE 1800-2017 20.9 ($onehot/$onehot0/$isunknown return 1'b1 / 1'b0,
// $countones returns an int), 11.6 (self-determined expression width)
module svonehotwidth1;
  logic [7:0] x = 8'b0001_0000;
  logic [7:0] u = 8'bx000_0001;
  logic [3:0] v;
  int errs = 0;
  initial begin
    if ($onehot(x) !== 1'b1) begin $error("onehot(x)"); errs++; end
    if ($onehot0(x) !== 1'b1) begin $error("onehot0(x)"); errs++; end
    if ($isunknown(x) !== 1'b0) begin $error("isunknown(x)"); errs++; end
    if ($onehot(u) !== 1'b1) begin $error("onehot(u) (x bit is not high)"); errs++; end
    if ($isunknown(u) !== 1'b1) begin $error("isunknown(u)"); errs++; end
    if ($bits($onehot(x)) != 1 || $bits($onehot0(x)) != 1 || $bits($isunknown(x)) != 1) begin
      $error("results of the 1-bit functions are not 1 bit wide"); errs++;
    end
    // 1-bit + 1-bit in a self-determined context wraps
    if (($onehot(x) + $onehot0(x)) !== 1'b0) begin $error("1 + 1 in one bit"); errs++; end
    if (($isunknown(u) & 1'b1) !== 1'b1) begin $error("isunknown(u) & 1"); errs++; end
    if ($countones(x) != 1) begin $error("countones(x)"); errs++; end
    if ($bits($countones(x)) != 32) begin $error("countones is not an int"); errs++; end
    v = {$onehot(x), $onehot0(x), $isunknown(x), $isunknown(u)};
    if (v !== 4'b1101) begin $error("v=%b", v); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
