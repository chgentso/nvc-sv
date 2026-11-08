// Expected values: IEEE 1800-2017 11.5.1 (indexed part-selects +: and -:), 11.4.12 (concatenation),
// 10.3 (continuous assignment to part-selects of a net/variable from generate loops), 27.4.
// word_i = {04,03,02,01}: word_i[0]=01 ... word_i[3]=04; be_i = 1011.
//   w1 = 04030201; w2 = {1_04, 0_03, 1_02, 1_01}; w3 nibbles {1,2,3,4}.
module svgenpartassign1;
  logic [3:0][7:0] word_i = 32'h04030201;
  logic [3:0] be_i = 4'b1011;
  logic [31:0] w1;
  logic [35:0] w2;
  logic [15:0] w3;
  for (genvar ii = 0; ii < 4; ii++) begin : gen_a
    assign w1[8*ii +: 8] = word_i[ii];
  end
  for (genvar ii = 0; ii < 4; ii++) begin : gen_b
    assign w2[9*ii +: 9] = { be_i[ii], word_i[ii] };
  end
  for (genvar ii = 0; ii < 4; ii++) begin : gen_c
    assign w3[15 - 4*ii -: 4] = word_i[ii][3:0];
  end
  int bad = 0;
  initial begin
    #1;
    if (w1 !== 32'h04030201) begin $error("w1=%h", w1); bad++; end
    if (w2 !== {9'h104, 9'h003, 9'h102, 9'h101}) begin $error("w2=%h", w2); bad++; end
    if (w3 !== 16'h1234) begin $error("w3=%h", w3); bad++; end
    if (bad == 0) $display("PASSED"); else $display("FAILED");
  end
endmodule
