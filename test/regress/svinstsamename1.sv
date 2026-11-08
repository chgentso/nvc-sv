// Expected values: IEEE 1800-2017 23.3.2 (instances), 23.10 (parameter override), 6.20 (localparam),
// 23.6 (hierarchical reference to a localparam), 11.4.8 (~ on one bit)
module leaf #(parameter int P = 1) (input a, output b);
  assign b = ~a;
  localparam int PP = P;
endmodule
module svinstsamename1;
  logic x = 0, y, z, w;
  int errs = 0;
  leaf leaf (.a(x), .b(y));
  leaf second (.a(y), .b(z));
  leaf #(.P(3)) third (.a(z), .b(w));
  initial begin
    #1;
    if (y !== 1'b1) begin errs++; $error("y=%b", y); end
    if (z !== 1'b0) begin errs++; $error("z=%b", z); end
    if (w !== 1'b1) begin errs++; $error("w=%b", w); end
    if (third.PP !== 3) begin errs++; $error("third.PP=%0d", third.PP); end
    if (second.PP !== 1) begin errs++; $error("second.PP=%0d", second.PP); end
    if (errs == 0) $display("PASSED");
  end
endmodule
