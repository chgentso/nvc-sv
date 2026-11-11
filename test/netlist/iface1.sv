interface bus_if (input logic clk);
  logic [7:0] data;
  logic valid;
  modport src (output data, valid, input clk);
  modport snk (input data, valid, clk);
endinterface

typedef struct packed {
  logic [3:0] kind;
  logic       last;
} hdr_t;

module prod (bus_if.src b);
  always @(posedge b.clk) begin
    b.data <= b.data + 1;
    b.valid <= 1;
  end
endmodule

module cons (bus_if.snk b, output logic [7:0] o);
  assign o = b.data;
endmodule

module iface1;
  logic clk = 0;
  logic [7:0] o0, o1;
  hdr_t hdr;
  bus_if bi0 (clk);
  bus_if bi1 (clk);
  prod p0 (bi0);
  cons c0 (bi0, o0);
  prod p1 (bi1);
  cons c1 (bi1, o1);

  always #5 clk = ~clk;
  assign hdr.last = bi0.valid & bi1.valid;
endmodule
