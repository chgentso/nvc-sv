// An unpacked array parameter with a default value or one that is overridden
// Expected values: IEEE 1800-2017 6.20.2 (parameter of unpacked array type), 23.10 (parameter override), 7.4
`timescale 1ns/1ns
module svparamarr1_ch #(parameter int P [3] = '{1, 2, 3}, parameter int N = 2)
  (output int o);
  assign o = P[N];
endmodule

module svparamarr1;
  parameter int PP [4] = '{10, 20, 30, 40};
  parameter bit [7:0] PB [2] = '{8'h12, 8'h34};
  int i = 2;
  int o1, o2;
  int errs = 0;
  svparamarr1_ch u1 (.o(o1));
  svparamarr1_ch #(.P('{4, 5, 6}), .N(1)) u2 (.o(o2));
  initial begin
    #1;
    if (PP[0] !== 10 || PP[i] !== 30 || PP[3] !== 40 || PP[1] !== 20) begin
      $error("PP: %0d %0d %0d %0d", PP[0], PP[i], PP[3], PP[1]); errs++;
    end
    if (PB[0] !== 8'h12 || PB[1] !== 8'h34) begin $error("PB: %h %h", PB[0], PB[1]); errs++; end
    if (o1 !== 3) begin $error("default P[N]: %0d expected 3", o1); errs++; end
    if (o2 !== 5) begin $error("overridden P[N]: %0d expected 5", o2); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
