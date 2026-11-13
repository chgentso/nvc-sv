// Self-checking. IEEE 1800-2017 8.25, 13.4: a static method of a class
// specialised with a parameter of the module assigns its own name to give the
// result, and the result has the width of the parameter.
class svclassfn1_c #(parameter p = 4);
  static function logic [p-1:0] func(input logic [p-1:0] inb);
    func = inb;
  endfunction
endclass

module svclassfn1_sub #(parameter p = 5) (output logic [p-1:0] b);
  logic [p-1:0] a = 3;
  assign b = svclassfn1_c#(p)::func(a);
endmodule

module svclassfn1;
  logic [2:0] b3;
  logic [4:0] b5;
  svclassfn1_sub #(3) s3(b3);
  svclassfn1_sub s5(b5);
  initial begin
    #1;
    if (b3 !== 3'd3 || b5 !== 5'd3) $error("FAIL %0d %0d", b3, b5);
    else $display("PASSED");
  end
endmodule
