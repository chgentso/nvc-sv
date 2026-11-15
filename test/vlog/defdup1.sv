// IEEE 1800-2017 14.12, 16.15: a module or generate block has at most one
// default clocking and one default disable iff.
module defdup1(input clk, input rstn);
  default clocking @(posedge clk);
  endclocking
  default clocking @(negedge clk);              // Error
  endclocking
  default disable iff (!rstn);
  default disable iff (!rstn);                  // Error
  if (1) begin : g
    default disable iff (!rstn);                // OK
    default disable iff (rstn);                 // Error
  end
  if (1) begin : h
    default disable iff (!rstn);                // OK
  end
endmodule
