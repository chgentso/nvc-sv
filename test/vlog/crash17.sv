
module m (input clk);
  clocking cb @(posedge clk);
    foo bar;
    input clk;
  endclocking
endmodule
