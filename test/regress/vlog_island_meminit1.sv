// Initial values of island memories (four-state, element widths that are
// not a multiple of eight, multi-word elements) loaded at reset
module vlog_island_meminit1;
  logic clk = 0;
  logic [12:0] m13 [4] = '{13'b1_0x1z_0101_1100, 13'h1fff, 13'bz_zzzz_xxxx_0110,
                           13'h0a5};
  logic [69:0] m70 [3] = '{70'h2a_5555_aaaa_0f0f_1234,
                           {6'b10x1z0, 64'hffff_0000_ffff_0000},
                           {3'bx01, 3'bz10, 64'h8000_0000_0000_0001}};
  logic [7:0]  m8 [5] = '{8'bx1z0_1010, 8'h00, 8'hff, 8'b0101_zzzz, 8'h3c};
  logic        m1 [9] = '{1'b0, 1'b1, 1'bx, 1'bz, 1'b1, 1'b0, 1'bx, 1'b1, 1'bz};
  logic [12:0] r13;
  logic [69:0] r70;
  logic [7:0]  r8;
  logic        r1;
  logic [3:0]  i = 0;

  always_ff @(posedge clk) begin
    r13 <= m13[i[1:0]];
    r70 <= m70[i % 3];
    r8  <= m8[i % 5];
    r1  <= m1[i];
    i   <= i + 1;
  end

  initial begin
    repeat (10) begin
      #5 clk = 1;
      #5 clk = 0;
      $display("%0d: %b %b %b %b", i, r13, r70, r8, r1);
    end
    $finish;
  end
endmodule
