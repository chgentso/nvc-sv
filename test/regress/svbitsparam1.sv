// Self-checking: $bits of a packed structure type whose member width depends on a parameter derived from
// a module parameter, used in a localparam and to size a vector, with the instance overriding the parameter.
// Expected values: IEEE 1800-2017 20.6.2 ($bits is a constant function), 6.20 (parameters and
// localparams), 7.2.1 (packed structures).
module sub #(parameter int DW = 32, parameter int MW = 4) (output int bits_o, output int w_o, output logic [7:0] rt_o);
  localparam int Bytes = DW / 8;
  localparam int WoW = (Bytes == MW) ? 1 : $clog2(Bytes / MW);
  typedef struct packed {
    logic [MW-1:0] mask;
    logic [WoW-1:0] woffset;
  } req_t;
  localparam int ReqW = $bits(req_t);
  localparam int FifoW = ReqW + (DW > 32 ? 12 : 0);
  logic [FifoW-1:0] fifo;
  req_t r;
  assign bits_o = ReqW;
  assign w_o = FifoW;
  initial rt_o = $bits(req_t);
  initial begin
    r = '{mask: {MW{1'b1}}, woffset: '1};
    fifo = r;
    #1;
    if (fifo[ReqW-1:0] !== {{MW{1'b1}}, {WoW{1'b1}}}) $error("round trip lost bits %b", fifo);
  end
endmodule

module svbitsparam1;
  int b1, w1, b2, w2, b3, w3;
  logic [7:0] r1, r2, r3;
  sub #(.DW(32), .MW(4)) u1 (b1, w1, r1);
  sub #(.DW(64), .MW(4)) u2 (b2, w2, r2);
  sub #(.DW(32), .MW(2)) u3 (b3, w3, r3);
  initial begin
    #2;
    if (b1 === 5 && w1 === 5 && r1 === 5 && b2 === 5 && w2 === 17 && r2 === 5 && b3 === 3 && w3 === 3 && r3 === 3)
      $display("PASSED");
    else
      $error("bits %0d %0d %0d widths %0d %0d %0d rt %0d %0d %0d, expected 5 5 3 / 5 17 3 / 5 5 3",
             b1, b2, b3, w1, w2, w3, r1, r2, r3);
  end
endmodule
