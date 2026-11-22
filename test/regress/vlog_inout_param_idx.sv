interface ii(input logic clk, inout wire scl_io);
endinterface

module vlog_inout_param_idx;
  wire [7:0] mios;
  localparam int IDX [3] = '{5, 2, 7};
  logic clk = 0;
  // The index of the bit is an element of a parameter array
  for (genvar i = 0; i < 3; i++) begin : g
    ii u(.clk(clk), .scl_io(mios[IDX[i]]));
  end
  assign mios[2] = 1'b1;
  assign mios[7] = 1'b0;
  initial begin
    #1;
    if (mios[2] === 1'b1 && g[1].u.scl_io === 1'b1
        && g[2].u.scl_io === 1'b0 && g[0].u.scl_io === 1'bz)
      $display("PASSED");
    else
      $display("FAILED %b %b %b", g[0].u.scl_io, g[1].u.scl_io, g[2].u.scl_io);
  end
endmodule
