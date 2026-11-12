module m (input clk);
  logic [7:0] d;
  clocking cb @(posedge clk);
    input  q = other.q;
    output d;
  endclocking
endmodule
