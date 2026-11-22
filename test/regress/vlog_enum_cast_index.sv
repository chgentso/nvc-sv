// An enumeration name whose value is a sized cast with the top bit set is
// unsigned like its base type when used as a constant index

module vlog_enum_cast_index;
  localparam int W = 3;
  typedef enum logic [W-1:0] { A, B, C = W'(4), D } e_t;
  logic [4:0][3:0] g;
  logic [3:0] x, y;
  initial begin
    g = 20'h12345;
    x = g[C];
    y = g[B];
    if (x === 4'h1 && y === 4'h4)
      $display("PASSED");
    else
      $display("FAILED %h %h", x, y);
  end
endmodule
