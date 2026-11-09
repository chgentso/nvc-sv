interface bus_if (input logic clk);
  logic [7:0] data;
  modport tx (output data);
  modport rx (input data);
endinterface

module sender (bus_if.tx b);
  initial b.data = 8'h3C;
endmodule

module receiver (bus_if.rx b, output logic [7:0] q);
  assign q = b.data;
endmodule
