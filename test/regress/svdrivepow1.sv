// Continuous assignments to different bits of a variable selected with constant expressions using ** and $clog2
// Expected values: IEEE 1800-2017 6.5 (variables), 11.5.1 (longest static prefix)
module svdrivepow1 #(parameter int W = 8);
  localparam int L = 2;
  logic [15:0] sel;
  logic [7:0] vec;
  assign sel[2**L] = 1'b1;
  assign sel[2**L+1] = 1'b0;
  assign sel[$clog2(W)] = 1'b1;
  assign sel[(1 <<< L) + 2] = 1'b1;
  for (genvar k = 0; k < 3; k++) begin : g
    assign vec[2**k] = 1'b1;
  end
  initial begin
    #1;
    if (sel[4] === 1'b1 && sel[5] === 1'b0 && sel[3] === 1'b1 && sel[6] === 1'b1
        && vec[1] === 1'b1 && vec[2] === 1'b1 && vec[4] === 1'b1 && vec[0] === 1'bx)
      $display("PASSED");
    else
      $error("static prefix: %b %b", sel, vec);
  end
endmodule
