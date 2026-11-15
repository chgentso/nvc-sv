// IEEE 1800-2017 11.4.12.1: the replication constant is a non-negative
// constant and a zero replication is only an operand of a concatenation
// with another operand.
module repl2;
  localparam ZERO = 0;
  parameter PAD = 2;
  logic [3:0] a;
  logic [7:0] b;
  wire [3:0] w1 = a ? 4'b0001 : {0{1'b0}};      // Error
  wire [3:0] w2 = {ZERO{1'b0}};                 // Error
  wire [7:0] w3 = {-1{1'b1}};                   // Error
  wire [7:0] w4 = {32'bz{1'b1}};                // Error
  wire [7:0] w6 = {{0{1'b0}}, a};               // OK
  wire [7:0] w7 = {a, {ZERO{1'b0}}};            // OK
  wire [7:0] w8 = {{PAD{1'b0}}, a};             // OK
  wire [7:0] w9 = {2{a}};                       // OK
endmodule
