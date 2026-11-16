// A variable of a type parameter declared in a generate block and connected to an instance port
// Expected values: IEEE 1800-2017 6.20.3 (type parameters), 27.5 (generate blocks), 23.3.3 (port connections)
module svtypeparamgen1_sub(input logic [3:0] i, output logic [3:0] o);
  assign o = ~i;
endmodule

module svtypeparamgen1_dut #(parameter int N = 4, parameter type idx_t = logic [N-1:0])
  (input idx_t a, output idx_t y);
  if (N > 1) begin : g
    idx_t up, lo;
    svtypeparamgen1_sub s1 (.i(a), .o(up));
    svtypeparamgen1_sub s2 (.i(up), .o(lo));
    assign y = lo;
  end
endmodule

module svtypeparamgen1;
  logic [3:0] a, y;
  svtypeparamgen1_dut #(.N(4)) dut (.a(a), .y(y));
  initial begin
    a = 4'b1010;
    #1;
    if (y === 4'b1010 && dut.g.up === 4'b0101)
      $display("PASSED");
    else
      $error("generate type parameter var: %b %b", y, dut.g.up);
  end
endmodule
