module svrandmodearr1;
  class cfg_c;
    rand bit [15:0] vals[4];
    rand bit [15:0] other;
  endclass

  initial begin
    automatic cfg_c c = new();
    c.vals[1] = 16'hBEEF;
    c.vals[1].rand_mode(0);
    if (c.vals[1].rand_mode() !== 0)
      $fatal(1, "FAIL: rand_mode did not disable");
    if (!c.randomize() with { other == 16'h1234; })
      $fatal(1, "FAIL: randomize failed");
    if (c.vals[1] !== 16'hBEEF || c.other !== 16'h1234)
      $fatal(1, "FAIL: vals[1]=%h other=%h", c.vals[1], c.other);
    $display("PASSED");
  end
endmodule
