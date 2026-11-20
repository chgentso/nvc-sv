interface bus_if #(parameter int WIDTH = 8);
  localparam int BYTES = WIDTH / 8;
  logic [WIDTH-1:0] data;
  modport src (input data);
endinterface

module consumer (
  bus_if.src port,
  output int w_out,
  output int b_out
);
  bus_if #(.WIDTH(port.WIDTH)) inner();
  assign inner.data = port.data;
  assign w_out = inner.WIDTH;
  assign b_out = port.BYTES;
endmodule

module svmodportparam1;
  bus_if #(.WIDTH(32)) b();
  int w, by;

  consumer u (.port(b), .w_out(w), .b_out(by));

  initial begin
    b.data = 32'h12345678;
    #1;
    if (w !== 32) $fatal(1, "expected w=32 got %0d", w);
    if (by !== 4) $fatal(1, "expected by=4 got %0d", by);
    $display("PASSED");
    $finish;
  end
endmodule
