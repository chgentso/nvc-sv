// Expected values: IEEE 1800-2017 11.4.14 (streaming concatenation: >> keeps order, << reverses 8-bit
// slices), 6.8 (variable declaration initializers, here also for unpacked arrays and in a procedural block),
// 10.9.1 (array initialization). The initializers use a constant so that no order between initializers
// is assumed; the same streams of the variable a_data are checked with run-time assignments.
module svstreaminit1;
  localparam int N = 4;
  localparam logic [31:0] A0 = 32'h11223344;
  logic [31:0] a_data = A0;
  bit [7:0] m1[4] = {<< 8{A0}};
  bit [3:0] m2[2][2] = {>> 4{16'habcd}};
  int errs = 0;
  task automatic ck(string w, int got, int exp);
    if (got != exp) begin $error("%s: got %h expected %h", w, got, exp); errs++; end
  endtask
  initial begin
    automatic bit [7:0] tl_byte[N] = {<< 8{A0}};
    automatic bit [7:0] tl_b2[N] = {>> 8{A0}};
    bit [7:0] r1[N], r2[N];
    r1 = {<< 8{a_data}};
    r2 = {>> 8{a_data}};
    for (int i = 0; i < N; i++) begin
      ck("tl_byte", tl_byte[i], 8'h44 - 8'h11 * i);
      ck("tl_b2", tl_b2[i], 8'h11 * (i + 1));
      ck("m1", m1[i], 8'h44 - 8'h11 * i);
      ck("r1", r1[i], 8'h44 - 8'h11 * i);
      ck("r2", r2[i], 8'h11 * (i + 1));
    end
    ck("m2[0][0]", m2[0][0], 4'ha);
    ck("m2[0][1]", m2[0][1], 4'hb);
    ck("m2[1][0]", m2[1][0], 4'hc);
    ck("m2[1][1]", m2[1][1], 4'hd);
    if (errs == 0) $display("PASSED");
  end
endmodule
