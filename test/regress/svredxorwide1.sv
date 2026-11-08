// Expected values: IEEE 1800-2017 11.4.9 (reduction operators; x/z in operand gives x for ^, & and | per 11.4.9
// tables), 20.9 ($countones)
module svredxorwide1;
  logic [99:0] a = 100'h1_0000_0000_0000_0000_0000_0003; // 3 ones
  logic [99:0] b = 100'h0;
  logic [99:0] c = {100{1'b1}};                          // 100 ones
  logic [99:0] d = 100'h0_0000_0000_0000_0000_0000_0001; // 1 one
  logic [99:0] x = {50'bx, 50'b1};
  int err = 0;
  initial begin
    if ((^a) !== 1'b1) begin $error("^a"); err++; end
    if ((^b) !== 1'b0) begin $error("^b"); err++; end
    if ((^c) !== 1'b0) begin $error("^c"); err++; end
    if ((^d) !== 1'b1) begin $error("^d"); err++; end
    if ((^x) !== 1'bx) begin $error("^x"); err++; end
    if ((~^a) !== 1'b0) begin $error("~^a"); err++; end
    if ((~^b) !== 1'b1) begin $error("~^b"); err++; end
    if ((~^c) !== 1'b1) begin $error("~^c"); err++; end
    if ((~^d) !== 1'b0) begin $error("~^d"); err++; end
    if ((&a) !== 1'b0) begin $error("&a"); err++; end
    if ((|a) !== 1'b1) begin $error("|a"); err++; end
    if ((~&c) !== 1'b0) begin $error("~&c"); err++; end
    if ($countones(a) !== 3) begin $error("countones"); err++; end
    if (err == 0) $display("PASSED");
  end
endmodule
