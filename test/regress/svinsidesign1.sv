// Self-checking. IEEE 1800-2017 11.8.1, 12.5.4: the operands of an inside
// comparison are signed only if all of them are, so a signed item meeting an
// unsigned expression is zero extended and the other way round.
module svinsidesign1;
  logic [1:0] a;
  logic signed [1:0] s;
  bit h1, h2, h3;
  initial begin
    a = 2'b11; s = 2'sb11;
    case (a) inside 1'sb1: h1 = 1; default: h1 = 0; endcase
    case (s) inside 1'sb1: h2 = 1; default: h2 = 0; endcase
    h3 = a inside {1'sb1};
    if (h1 !== 0) $error("FAIL unsigned expression");
    if (h2 !== 1) $error("FAIL signed expression");
    if (h3 !== 0) $error("FAIL operator");
    a = 2'b01; s = 2'sb01;
    case (a) inside 1'sb1: h1 = 1; default: h1 = 0; endcase
    if (h1 !== 1) $error("FAIL zero extend");
    $display("PASSED");
  end
endmodule
