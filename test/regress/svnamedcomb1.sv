// Named and defaulted arguments of a function called in always_comb: the
// block is sensitive to the actual arguments (IEEE 1800-2017 9.2.2.2.1).
module svnamedcomb1;
  function automatic logic [7:0] f(input logic [3:0] a, input logic [3:0] b,
                                   input logic [3:0] c = 4'd9);
    return {a, b} + c;
  endfunction

  logic [7:0] o1, o2;
  logic [3:0] x = 4'd3, y = 4'd1;

  always_comb o1 = f(.a(x), .b(4'd1));
  always_comb o2 = f(.c(y), .b(x), .a(4'd2));

  initial begin
    #1 $display("%0d %0d", o1, o2);
    x = 4'd4;
    #1 $display("%0d %0d", o1, o2);
    y = 4'd5;
    #1 $display("%0d %0d", o1, o2);
  end
endmodule
