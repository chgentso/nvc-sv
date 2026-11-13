// A cast in a parameter value assignment of an instance whose module is
// declared later in the file (IEEE 1800-2017 23.10)
module svpcast1;
  localparam real c = 40e6;
  localparam int w = 3;
  logic [7:0] y1, y2, y3, y4;
  sub #(.b(int'(c/1e6))) u1 (.x(y1));
  sub #(int'(c/2e6)) u2 (.x(y2));
  sub #(.b(logic [7:0]'(w + 1))) u3 (.x(y3));
  sub #(.b(signed'(8'd5))) u4 (.x(y4));
  tsub #(.T(int)) u5 ();
  initial begin
    #1;
    if (y1 === 8'd40 && y2 === 8'd20 && y3 === 8'd4 && y4 === 8'd5)
      $display("PASSED");
    else
      $display("FAILED %0d %0d %0d %0d", y1, y2, y3, y4);
  end
endmodule

module sub #(parameter int b = 0) (output logic [7:0] x);
  assign x = b;
endmodule

module tsub #(parameter type T = logic);
  T v = 5;
  initial if (v != 5) $display("FAILED type");
endmodule
