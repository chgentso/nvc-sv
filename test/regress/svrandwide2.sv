// A variable local to a class method may be wider than 64 bits and be
// constrained by std::randomize ... with (IEEE 1800-2017 18.12).
class svrandwide2_c;
  function int check(bit single);
    bit [511:0] mask;
    if (!std::randomize(mask) with { mask != 0; single -> $countones(mask) == 1; })
      return -1;
    if (mask == 0)
      return -2;
    if (single && $countones(mask) != 1)
      return -3;
    return 0;
  endfunction
endclass

module svrandwide2;
  initial begin
    automatic svrandwide2_c o = new;
    for (int i = 0; i < 20; i++) begin
      automatic int r = o.check(i % 2);
      if (r != 0) $fatal(1, "iteration %0d failed with %0d", i, r);
    end
    $display("PASSED");
  end
endmodule
