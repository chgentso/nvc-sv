// Type parameters: defaults, named and positional overrides, package types, $bits of port and variable
// Expected values: IEEE 1800-2017 6.20.3 (type parameters), 23.10 (overrides), 20.6.2 ($bits),
// 9.2.2.2 (always_comb), 9.2.2 (always @(posedge))
// Clock rises at 5 and 15; checks at 2 (combinational results) and 22 (registered values).
`timescale 1ns/1ns
package pk; typedef struct packed { logic [3:0] hi; logic [3:0] lo; } nib_t; endpackage
module sub #(parameter type T = int, parameter int W = 4) (input T a, output T y);
  T tmp;
  always_comb begin tmp = a; y = tmp + T'(1); end
endmodule
module sub2 #(type T = logic [3:0]) (input logic clk, input T d);
  T q;
  always @(posedge clk) q <= d;
endmodule
module svparamtype1;
  logic [7:0] a8, y8; bit [15:0] a16, y16, y16b; int ai, yi;
  pk::nib_t nib, ny;
  int errs = 0;
  logic clk = 0; initial repeat (10) #5 clk = ~clk;
  sub #(.T(logic [7:0])) s1(a8, y8);
  sub #(bit [15:0], 3) s2(a16, y16);
  sub #(.W(9), .T(bit [15:0])) s3(a16, y16b);
  sub s4(ai, yi);
  sub #(pk::nib_t) s5(nib, ny);
  sub2 #(.T(pk::nib_t)) u1(clk, nib);
  sub2 u2(clk, nib[3:0]);
  initial begin
    a8 = 5; a16 = 9; ai = 41; nib = 8'h5a;
    #2;
    if (y8 !== 8'd6) begin $error("y8=%0d", y8); errs++; end
    if (y16 !== 16'd10 || y16b !== 16'd10) begin $error("y16=%0d y16b=%0d", y16, y16b); errs++; end
    if (yi !== 42) begin $error("yi=%0d", yi); errs++; end
    if (ny !== 8'h5b) begin $error("ny=%h", ny); errs++; end
    if ($bits(s1.a) != 8 || $bits(s2.a) != 16 || $bits(s3.a) != 16 || $bits(s4.a) != 32 ||
        $bits(s5.a) != 8 || $bits(u1.q) != 8 || $bits(u2.q) != 4) begin
      $error("bits %0d %0d %0d %0d %0d %0d %0d", $bits(s1.a), $bits(s2.a), $bits(s3.a), $bits(s4.a),
             $bits(s5.a), $bits(u1.q), $bits(u2.q)); errs++;
    end
    if (s1.W != 4 || s2.W != 3 || s3.W != 9 || s4.W != 4) begin
      $error("W %0d %0d %0d %0d", s1.W, s2.W, s3.W, s4.W); errs++;
    end
    #20;
    if (u1.q !== 8'h5a || u2.q !== 4'ha) begin $error("q %h %h", u1.q, u2.q); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
