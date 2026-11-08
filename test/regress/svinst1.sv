// Instances of the same module with different port connections
// Expected values: IEEE 1800-2017 23.3.2 (port connection), 23.10 (parameter override),
// 9.4.1 (delay control)
`timescale 1ns/1ns
module svinst1_m #(parameter P = 1) (input [3:0] id, input [3:0] other);
  reg [3:0] x;
  wire [3:0] w = id;
  initial begin
    #id;
    x = id;
    if (x !== id || w !== id) begin svinst1.errs++; $error("P=%0d id=%0d x=%0d w=%0d", P, id, x, w); end
    if ($time !== id) begin svinst1.errs++; $error("time %0t != id %0d", $time, id); end
    if (id == 4'd4 && P !== 4) begin svinst1.errs++; $error("P=%0d for id 4", P); end
    if (id != 4'd4 && P !== 1) begin svinst1.errs++; $error("P=%0d for id %0d", P, id); end
    case (id)
      1: if (other !== 4'd7) begin svinst1.errs++; $error("u1 other=%0d", other); end
      2: if (other !== 4'd8) begin svinst1.errs++; $error("u2 other=%0d", other); end
      3: if (other !== 4'd9) begin svinst1.errs++; $error("u3 other=%0d", other); end
      4: if (other !== 4'd7) begin svinst1.errs++; $error("u4 other=%0d", other); end
    endcase
    svinst1.done++;
  end
endmodule

module svinst1;
  reg [3:0] a = 4'd7, b = 4'd8;
  int done = 0, errs = 0;
  svinst1_m u1(1, a);
  svinst1_m u2(2, b);
  svinst1_m u3(.id(3), .other(4'd9));
  svinst1_m #(.P(4)) u4(4, a);
  initial begin
    #10;
    if (done == 4 && errs == 0) $display("PASSED"); else $error("done=%0d errs=%0d", done, errs);
  end
endmodule
