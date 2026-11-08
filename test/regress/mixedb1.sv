// VHDL output ports connected to members of an unpacked structure net
module mixedb1;
  typedef struct { logic [7:0] x; logic [7:0] y; } d_t;
  wire d_t o;
  mixedb1_e #(.g_a(18)) u (.xo(o.x), .yo(o.y));
  initial begin
    #1;
    if (o.x === 8'h12 && o.y === 8'h34) $display("PASSED");
    else $display("FAILED x=%h y=%h", o.x, o.y);
  end
endmodule
