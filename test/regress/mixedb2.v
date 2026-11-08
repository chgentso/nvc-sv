// A parameter override for a generic that the entity does not have is
// ignored with a warning
module mixedb2;
  wire [7:0] x, y;
  mixedb2_e #(.g_a(18), .g_missing(7)) u (.xo(x), .yo(y));
  initial begin
    #1;
    if (x === 8'h12 && y === 8'h34) $display("PASSED");
    else $display("FAILED x=%h y=%h", x, y);
  end
endmodule
