// A constraint over the whole of an array passed to std::randomize is not supported yet: it must be
// reported (it used to crash), not solved against the old contents of the array.
module svcrvstdarr1;
  bit [15:0] w[4];
  int r;
  initial begin
    r = std::randomize(w) with { foreach (w[i]) w[i] == i * 3; };
    $display("not reached");
  end
endmodule
