// std::randomize of elements of arrays wider than 64 bits, with constant
// and with variable indices, and of the whole array with a foreach
module svstdwide1;
  bit [94:0] l95[6];
  bit [94:0] x;
  int errs = 0;
  task automatic check(bit ok, string what);
    if (!ok) begin $error("%s", what); errs++; end
  endtask
  initial begin
    int r;
    r = std::randomize(l95[1]) with { l95[1] >= 95'd50; l95[1] < 95'd1000; };
    check(r == 1 && l95[1] >= 50 && l95[1] < 1000, "constant index");

    r = std::randomize(l95) with { foreach (l95[i]) l95[i] < 95'd1000000000000; };
    check(r == 1, "whole array");
    foreach (l95[i]) check(l95[i] < 95'd1000000000000, "foreach bound");

    foreach (l95[i]) begin
      r = std::randomize(l95[i]) with { l95[i] >= 95'd50; l95[i] < 95'd1000; };
      check(r == 1 && l95[i] >= 50 && l95[i] < 1000, "variable index");
    end

    // The upper chunk is solved too
    r = std::randomize(l95[2]) with { l95[2] > 95'h1_0000_0000_0000_0000; };
    check(r == 1 && l95[2] > 95'h1_0000_0000_0000_0000, "upper chunk");
    if (errs == 0) $display("PASSED");
  end
endmodule
