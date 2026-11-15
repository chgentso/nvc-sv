// IEEE 1800-2017 11.4.9: ~& and ~| are only unary reduction operators, but
// ~^ is also a binary operator.
module binnand1;
  reg [7:0] a, b, c;
  initial begin
    c = a ~& b;                                 // Error
    c = a ~| b;                                 // Error
    c = a ~^ b;                                 // OK
    c = {7'b0, ~&a};                            // OK
    c = {7'b0, ~|a};                            // OK
  end
endmodule
